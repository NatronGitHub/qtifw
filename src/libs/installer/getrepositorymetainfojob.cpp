/**************************************************************************
**
** Copyright (C) 2012-2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Installer Framework.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
**************************************************************************/
#include "getrepositorymetainfojob.h"

#include "errors.h"
#include "lib7z_facade.h"
#include "messageboxhandler.h"
#include "packagemanagercore_p.h"
#include "utils.h"

#include "kdupdaterfiledownloader.h"
#include "kdupdaterfiledownloaderfactory.h"

#include "productkeycheck.h"

#include <QTimer>


using namespace KDUpdater;
using namespace QInstaller;


// -- GetRepositoryMetaInfoJob::ZipRunnable

class GetRepositoryMetaInfoJob::ZipRunnable : public QObject, public QRunnable
{
    Q_OBJECT

public:
    ZipRunnable(const QString &archive, const QString &targetDir, QPointer<FileDownloader> downloader)
        : QObject()
        , QRunnable()
        , m_archive(archive)
        , m_targetDir(targetDir)
        , m_downloader(downloader)
    {}

    ~ZipRunnable()
    {
        if (m_downloader)
            m_downloader->deleteLater();
    }

    void run()
    {
        QFile archive(m_archive);
        if (archive.open(QIODevice::ReadOnly)) {
            try {
                Lib7z::extractArchive(&archive, m_targetDir);
                if (!archive.remove()) {
                    qWarning("Could not delete file %s: %s", qPrintable(m_archive),
                        qPrintable(archive.errorString()));
                }
                emit finished(true, QString());
            } catch (const Lib7z::SevenZipException& e) {
                emit finished(false, tr("Error while extracting '%1': %2").arg(m_archive, e.message()));
            } catch (...) {
                emit finished(false, tr("Unknown exception caught while extracting %1.").arg(m_archive));
            }
        } else {
            emit finished(false, tr("Could not open %1 for reading. Error: %2").arg(m_archive,
                archive.errorString()));
        }
    }

Q_SIGNALS:
    void finished(bool success, const QString &errorString);

private:
    const QString m_archive;
    const QString m_targetDir;
    QPointer<FileDownloader> m_downloader;
};


// -- GetRepositoryMetaInfoJob

GetRepositoryMetaInfoJob::GetRepositoryMetaInfoJob(PackageManagerCore *core, QObject *parent)
    : KDJob(parent)
    , m_canceled(false)
    , m_silentRetries(4)
    , m_retriesLeft(m_silentRetries)
    , m_downloader(0)
    , m_waitForDone(false)
    , m_core(core)
{
    setCapabilities(Cancelable);
}

GetRepositoryMetaInfoJob::~GetRepositoryMetaInfoJob()
{
    if (m_downloader)
        m_downloader->deleteLater();
}

Repository GetRepositoryMetaInfoJob::repository() const
{
    return m_repository;
}

void GetRepositoryMetaInfoJob::setRepository(const Repository &r)
{
    m_repository = r;
    qDebug() << "Setting repository with URL:" << r.displayname();
}

int GetRepositoryMetaInfoJob::silentRetries() const
{
    return m_silentRetries;
}

void GetRepositoryMetaInfoJob::setSilentRetries(int retries)
{
    m_silentRetries = retries;
}

void GetRepositoryMetaInfoJob::doStart()
{
    m_retriesLeft = m_silentRetries;
    startUpdatesXmlDownload();
}

void GetRepositoryMetaInfoJob::doCancel()
{
    m_canceled = true;
    if (m_downloader)
        m_downloader->cancelDownload();
}

void GetRepositoryMetaInfoJob::finished(int error, const QString &errorString)
{
    m_waitForDone = true;
    m_threadPool.waitForDone();
    (error > KDJob::NoError) ? emitFinishedWithError(error, errorString) : emitFinished();
}

QString GetRepositoryMetaInfoJob::temporaryDirectory() const
{
    return m_temporaryDirectory;
}

QString GetRepositoryMetaInfoJob::releaseTemporaryDirectory() const
{
    m_tempDirDeleter.releaseAll();
    return m_temporaryDirectory;
}

// Updates.xml download

void GetRepositoryMetaInfoJob::startUpdatesXmlDownload()
{
    if (m_downloader) {
        m_downloader->deleteLater();
        m_downloader = 0;
    }

    const QUrl url = m_repository.url();
    if (url.isEmpty()) {
        finished(QInstaller::InvalidUrl, tr("Empty repository URL."));
        return;
    }

    if (!url.isValid()) {
        finished(QInstaller::InvalidUrl, tr("Invalid repository URL: %1").arg(m_repository.displayname()));
        return;
    }

    m_downloader = FileDownloaderFactory::instance().create(url.scheme(), this);
    if (!m_downloader) {
        finished(QInstaller::InvalidUrl, tr("URL scheme not supported: %1 (%2)").arg(url.scheme(),
            m_repository.displayname()));
        return;
    }

    QString updatesFileName = QString::fromLatin1("Updates.xml");
    if (m_retriesLeft == m_silentRetries)
        updatesFileName = QString::fromLatin1("Updates_%1.xml").arg(QLocale().name().toLower());

    // append a random string to avoid proxy caches
    m_downloader->setUrl(QUrl(url.toString() + QString::fromLatin1("/%1?").arg(updatesFileName)
        .append(QString::number(qrand() * qrand()))));

    QAuthenticator auth;
    auth.setUser(m_repository.username());
    auth.setPassword(m_repository.password());
    m_downloader->setAuthenticator(auth);

    m_downloader->setAutoRemoveDownloadedFile(false);
    connect(m_downloader, SIGNAL(downloadCompleted()), this, SLOT(updatesXmlDownloadFinished()));
    connect(m_downloader, SIGNAL(downloadCanceled()), this, SLOT(updatesXmlDownloadCanceled()));
    connect(m_downloader, SIGNAL(downloadAborted(QString)), this,
        SLOT(updatesXmlDownloadError(QString)), Qt::QueuedConnection);
    connect(m_downloader, SIGNAL(authenticatorChanged(QAuthenticator)), this,
        SLOT(onAuthenticatorChanged(QAuthenticator)));
    m_downloader->download();
}

void GetRepositoryMetaInfoJob::updatesXmlDownloadCanceled()
{
    finished(KDJob::Canceled, m_downloader->errorString());
}

void GetRepositoryMetaInfoJob::updatesXmlDownloadFinished()
{
    emit infoMessage(this, tr("Retrieving component meta information..."));

    const QString fn = m_downloader->downloadedFileName();
    Q_ASSERT(!fn.isEmpty());
    Q_ASSERT(QFile::exists(fn));

    QFile updatesFile(fn);

    if (!updatesFile.open(QIODevice::ReadOnly)) {
        finished(QInstaller::DownloadError, tr("Could not open Updates.xml for reading. Error: %1")
            .arg(updatesFile.errorString()));
        return;
    }

    QString err;
    QDomDocument doc;
    const bool success = doc.setContent(&updatesFile, &err);
    updatesFile.close();

    if (!success) {
        const QString msg =  tr("Could not fetch a valid version of Updates.xml from repository: %1. "
            "Error: %2").arg(m_repository.displayname(), err);

        const QMessageBox::StandardButton b =
            MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(),
            QLatin1String("updatesXmlDownloadError"), tr("Download Error"), msg, QMessageBox::Cancel);

        if (b == QMessageBox::Cancel || b == QMessageBox::NoButton) {
            finished(KDJob::Canceled, msg);
            return;
        }
    }

    emit infoMessage(this, tr("Parsing component meta information..."));

    const QDomElement root = doc.documentElement();
    // search for additional repositories that we might need to check
    const QDomNode repositoryUpdate  = root.firstChildElement(QLatin1String("RepositoryUpdate"));
    if (!repositoryUpdate.isNull()) {
        QHash<QString, QPair<Repository, Repository> > repositoryUpdates;
        const QDomNodeList children = repositoryUpdate.toElement().childNodes();
        for (int i = 0; i < children.count(); ++i) {
            const QDomElement el = children.at(i).toElement();
            if (!el.isNull() && el.tagName() == QLatin1String("Repository")) {
                const QString action = el.attribute(QLatin1String("action"));
                if (action == QLatin1String("add")) {
                    // add a new repository to the defaults list
                    Repository repository(el.attribute(QLatin1String("url")), true);
                    repository.setUsername(el.attribute(QLatin1String("username")));
                    repository.setPassword(el.attribute(QLatin1String("password")));
                    repository.setDisplayName(el.attribute(QLatin1String("displayname")));
                    if (ProductKeyCheck::instance()->isValidRepository(repository)) {
                        repositoryUpdates.insertMulti(action, qMakePair(repository, Repository()));
                        qDebug() << "Repository to add:" << repository.displayname();
                    }
                } else if (action == QLatin1String("remove")) {
                    // remove possible default repositories using the given server url
                    Repository repository(el.attribute(QLatin1String("url")), true);
                    repositoryUpdates.insertMulti(action, qMakePair(repository, Repository()));

                    qDebug() << "Repository to remove:" << repository.displayname();
                } else if (action == QLatin1String("replace")) {
                    // replace possible default repositories using the given server url
                    Repository oldRepository(el.attribute(QLatin1String("oldUrl")), true);
                    Repository newRepository(el.attribute(QLatin1String("newUrl")), true);
                    newRepository.setUsername(el.attribute(QLatin1String("username")));
                    newRepository.setPassword(el.attribute(QLatin1String("password")));
                    newRepository.setDisplayName(el.attribute(QLatin1String("displayname")));

                    if (ProductKeyCheck::instance()->isValidRepository(newRepository)) {
                        // store the new repository and the one old it replaces
                        repositoryUpdates.insertMulti(action, qMakePair(newRepository, oldRepository));
                        qDebug() << "Replace repository:" << oldRepository.displayname() << "with:"
                            << newRepository.displayname();
                    }
                } else {
                    qDebug() << "Invalid additional repositories action set in Updates.xml fetched from:"
                        << m_repository.displayname() << "Line:" << el.lineNumber();
                }
            }
        }

        if (!repositoryUpdates.isEmpty()) {
            const QSet<Repository> temporaries = m_core->settings().temporaryRepositories();
            // in case the temp repository introduced something new, we only want that temporary
            if (temporaries.contains(m_repository)) {

                QSet<Repository> childTempRepositories;
                typedef QPair<Repository, Repository> RepositoryPair;

                QList<RepositoryPair> values = repositoryUpdates.values(QLatin1String("add"));
                foreach (const RepositoryPair &value, values)
                    childTempRepositories.insert(value.first);

                values = repositoryUpdates.values(QLatin1String("replace"));
                foreach (const RepositoryPair &value, values)
                    childTempRepositories.insert(value.first);

                QSet<Repository> newChildTempRepositories = childTempRepositories.subtract(temporaries);
                if (newChildTempRepositories.count() > 0) {
                    m_core->settings().addTemporaryRepositories(newChildTempRepositories, true);
                    finished(QInstaller::RepositoryUpdatesReceived, tr("Repository updates received."));
                    return;
                }
            } else if (m_core->settings().updateDefaultRepositories(repositoryUpdates) == Settings::UpdatesApplied) {
                if (m_core->isUpdater() || m_core->isPackageManager())
                    m_core->writeMaintenanceConfigFiles();
                finished(QInstaller::RepositoryUpdatesReceived, tr("Repository updates received."));
                return;
            }
        }
    }

    const QDomNodeList children = root.childNodes();
    for (int i = 0; i < children.count(); ++i) {
        const QDomElement el = children.at(i).toElement();
        if (el.isNull())
            continue;
        if (el.tagName() == QLatin1String("PackageUpdate")) {
            const QDomNodeList c2 = el.childNodes();
            for (int j = 0; j < c2.count(); ++j) {
                if (c2.at(j).toElement().tagName() == scName)
                    m_packageNames << c2.at(j).toElement().text();
                else if (c2.at(j).toElement().tagName() == scRemoteVersion)
                    m_packageVersions << c2.at(j).toElement().text();
                else if (c2.at(j).toElement().tagName() == QLatin1String("SHA1"))
                    m_packageHash << c2.at(j).toElement().text();
            }
        }
    }

    try {
        m_temporaryDirectory = createTemporaryDirectory(QLatin1String("remoterepo-"));
        m_tempDirDeleter.add(m_temporaryDirectory);
    } catch (const QInstaller::Error& e) {
        finished(QInstaller::ExtractionError, e.message());
        return;
    }
    if (!updatesFile.rename(m_temporaryDirectory + QLatin1String("/Updates.xml"))) {
        finished(QInstaller::DownloadError, tr("Could not move Updates.xml to target location. Error: %1")
            .arg(updatesFile.errorString()));
        return;
    }

    setTotalAmount(m_packageNames.count() + 1);
    setProcessedAmount(1);
    emit infoMessage(this, tr("Finished updating component meta information."));

    if (m_packageNames.isEmpty())
        finished(KDJob::NoError);
    else
        fetchNextMetaInfo();
}

void GetRepositoryMetaInfoJob::updatesXmlDownloadError(const QString &err)
{
    if (m_retriesLeft <= 0) {
        const QString msg = tr("Could not fetch Updates.xml from repository: %1. Error: %2")
            .arg(m_repository.displayname(), err);

        QMessageBox::StandardButtons buttons = QMessageBox::Retry | QMessageBox::Cancel;
        const QMessageBox::StandardButton b =
            MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(),
            QLatin1String("updatesXmlDownloadError"), tr("Download Error"), msg, buttons);

        if (b == QMessageBox::Cancel || b == QMessageBox::NoButton) {
            finished(KDJob::Canceled, msg);
            return;
        }
    }

    m_retriesLeft--;
    QTimer::singleShot(1500, this, SLOT(startUpdatesXmlDownload()));
}

// meta data download

void GetRepositoryMetaInfoJob::fetchNextMetaInfo()
{
    emit infoMessage(this, tr("Retrieving component information from remote repository..."));

    if (m_canceled) {
        finished(KDJob::Canceled, m_downloader->errorString());
        return;
    }

    if (m_packageNames.isEmpty() && m_currentPackageName.isEmpty()) {
        finished(KDJob::NoError);
        return;
    }

    QString next = m_currentPackageName;
    QString nextVersion = m_currentPackageVersion;
    if (next.isEmpty()) {
        m_retriesLeft = m_silentRetries;
        next = m_packageNames.takeLast();
        nextVersion = m_packageVersions.takeLast();
    }

    qDebug() << "fetching metadata of" << next << "in version" << nextVersion;

    bool online = true;
    if (m_repository.url().scheme().isEmpty())
        online = false;

    const QString repoUrl = m_repository.url().toString();
    const QUrl url = QString::fromLatin1("%1/%2/%3meta.7z").arg(repoUrl, next,
        online ? nextVersion : QString());
    m_downloader = FileDownloaderFactory::instance().create(url.scheme(), this);

    if (!m_downloader) {
        m_currentPackageName.clear();
        m_currentPackageVersion.clear();
        qWarning() << "Scheme not supported:" << m_repository.displayname();
        QMetaObject::invokeMethod(this, "fetchNextMetaInfo", Qt::QueuedConnection);
        return;
    }

    m_currentPackageName = next;
    m_currentPackageVersion = nextVersion;
    m_downloader->setUrl(url);
    m_downloader->setAutoRemoveDownloadedFile(true);

    QAuthenticator auth;
    auth.setUser(m_repository.username());
    auth.setPassword(m_repository.password());
    m_downloader->setAuthenticator(auth);

    connect(m_downloader, SIGNAL(downloadCanceled()), this, SLOT(metaDownloadCanceled()));
    connect(m_downloader, SIGNAL(downloadCompleted()), this, SLOT(metaDownloadFinished()));
    connect(m_downloader, SIGNAL(downloadAborted(QString)), this, SLOT(metaDownloadError(QString)),
        Qt::QueuedConnection);
    connect(m_downloader, SIGNAL(authenticatorChanged(QAuthenticator)), this,
        SLOT(onAuthenticatorChanged(QAuthenticator)));

    m_downloader->download();
}

void GetRepositoryMetaInfoJob::metaDownloadCanceled()
{
    finished(KDJob::Canceled, m_downloader->errorString());
}

void GetRepositoryMetaInfoJob::metaDownloadFinished()
{
    const QString fn = m_downloader->downloadedFileName();
    Q_ASSERT(!fn.isEmpty());

    QFile arch(fn);
    if (!arch.open(QIODevice::ReadOnly)) {
        finished(QInstaller::ExtractionError, tr("Could not open meta info archive: %1. Error: %2").arg(fn,
            arch.errorString()));
        return;
    }

    if (!m_packageHash.isEmpty()) {
        // verify file hash
        const QByteArray expectedFileHash = m_packageHash.back().toLatin1();
        const QByteArray realFileHash = QInstaller::calculateHash(&arch, QCryptographicHash::Sha1).toHex();
        if (expectedFileHash != realFileHash) {
            emit infoMessage(this, tr("The hash of one component does not match the expected one."));
            metaDownloadError(tr("Bad hash."));
            return;
        }
        m_packageHash.removeLast();
    }
    arch.close();
    m_currentPackageName.clear();

    ZipRunnable *runnable = new ZipRunnable(fn, m_temporaryDirectory, m_downloader);
    connect(runnable, SIGNAL(finished(bool,QString)), this, SLOT(unzipFinished(bool,QString)));
    m_threadPool.start(runnable);

    if (!m_waitForDone)
        fetchNextMetaInfo();
}

void GetRepositoryMetaInfoJob::metaDownloadError(const QString &err)
{
    if (m_retriesLeft <= 0) {
        const QString msg = tr("Could not download meta information for component: %1. Error: %2")
            .arg(m_currentPackageName, err);

        QMessageBox::StandardButtons buttons = QMessageBox::Retry | QMessageBox::Cancel;
        const QMessageBox::StandardButton b =
            MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(),
            QLatin1String("updatesXmlDownloadError"), tr("Download Error"), msg, buttons);

        if (b == QMessageBox::Cancel || b == QMessageBox::NoButton) {
            finished(KDJob::Canceled, msg);
            return;
        }
    }

    m_retriesLeft--;
    QTimer::singleShot(1500, this, SLOT(fetchNextMetaInfo()));
}

void GetRepositoryMetaInfoJob::unzipFinished(bool ok, const QString &error)
{
    if (!ok)
        finished(QInstaller::ExtractionError, error);
}

bool GetRepositoryMetaInfoJob::updateRepositories(QSet<Repository> *repositories, const QString &username,
    const QString &password, const QString &displayname)
{
    if (!repositories->contains(m_repository))
        return false;

    repositories->remove(m_repository);
    m_repository.setUsername(username);
    m_repository.setPassword(password);
    m_repository.setDisplayName(displayname);
    repositories->insert(m_repository);
    return true;
}

void GetRepositoryMetaInfoJob::onAuthenticatorChanged(const QAuthenticator &authenticator)
{
    const QString username = authenticator.user();
    const QString password = authenticator.password();
    if (username != m_repository.username() || password != m_repository.password()) {
        QSet<Repository> repositories = m_core->settings().defaultRepositories();
        bool reposChanged = updateRepositories(&repositories, username, password);
        if (reposChanged)
            m_core->settings().setDefaultRepositories(repositories);

        repositories = m_core->settings().temporaryRepositories();
        reposChanged |= updateRepositories(&repositories, username, password);
        if (reposChanged) {
            m_core->settings().setTemporaryRepositories(repositories,
            m_core->settings().hasReplacementRepos());
        }

        repositories = m_core->settings().userRepositories();
        reposChanged |= updateRepositories(&repositories, username, password);
        if (reposChanged)
            m_core->settings().setUserRepositories(repositories);

        if (reposChanged) {
            if (m_core->isUpdater() || m_core->isPackageManager())
                m_core->writeMaintenanceConfigFiles();
            finished(QInstaller::RepositoryUpdatesReceived, tr("Repository updates received."));
        }
    }
}

#include "getrepositorymetainfojob.moc"
#include "moc_getrepositorymetainfojob.cpp"
