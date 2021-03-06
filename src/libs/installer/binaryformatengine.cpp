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

#include "binaryformatengine.h"

using namespace QInstallerCreator;

namespace {

class StringListIterator : public QAbstractFileEngineIterator
{
public:
    StringListIterator( const QStringList &list, QDir::Filters filters, const QStringList &nameFilters)
        : QAbstractFileEngineIterator(filters, nameFilters)
        , list(list)
        , index(-1)
    {
    }

    bool hasNext() const
    {
        return index < list.size() - 1;
    }

    QString next()
    {
        if(!hasNext())
            return QString();
        ++index;
        return currentFilePath();
    }

    QString currentFileName() const
    {
        return index < 0 ? QString() : list[index];
    }

private:
    const QStringList list;
    int index;
};

} // anon namespace

BinaryFormatEngine::BinaryFormatEngine(const ComponentIndex &index, const QString &fileName)
    : m_index(index)
    , m_hasComponent(false)
    , m_hasArchive(false)
    , m_archive(0)
{
    setArchive(fileName);
}

BinaryFormatEngine::~BinaryFormatEngine()
{
}

void BinaryFormatEngine::setArchive(const QString &file)
{
    m_fileNamePath = file;

    static const QChar sep = QLatin1Char('/');
    static const QString prefix = QLatin1String("installer://");
    Q_ASSERT(file.toLower().startsWith(prefix));

    // cut the prefix
    QString path = file.mid(prefix.length());
    while (path.endsWith(sep))
        path.chop(1);

    QString arch;
    const QString comp = path.section(sep, 0, 0);
    m_hasComponent = !comp.isEmpty();
    m_hasArchive = path.contains(sep);
    if (m_hasArchive)
        arch = path.section(sep, 1, 1);

    m_component = m_index.componentByName(comp.toUtf8());
    m_archive = m_component.archiveByName(arch.toUtf8());
}

/**
 * \reimp
 */
void BinaryFormatEngine::setFileName(const QString &file)
{
    setArchive(file);
}

/**
 * \reimp
 */
bool BinaryFormatEngine::close()
{
    if (m_archive == 0)
        return false;

    const bool result = m_archive->isOpen();
    m_archive->close();
    return result;
}

/**
 * \reimp
 */
bool BinaryFormatEngine::open(QIODevice::OpenMode mode)
{
    return m_archive == 0 ? false : m_archive->open(mode);
}

/**
 * \reimp
 */
qint64 BinaryFormatEngine::pos() const
{
    return m_archive == 0 ? 0 : m_archive->pos();
}

/**
 * \reimp
 */
qint64 BinaryFormatEngine::read(char *data, qint64 maxlen)
{
    return m_archive == 0 ? -1 : m_archive->read(data, maxlen);
}

/**
 * \reimp
 */
bool BinaryFormatEngine::seek(qint64 offset)
{
    return m_archive == 0 ? false : m_archive->seek(offset);
}

/**
 * \reimp
 */
QString BinaryFormatEngine::fileName(FileName file) const
{
    switch(file) {
    case BaseName:
        return m_fileNamePath.section(QChar::fromLatin1('/'), -1, -1, QString::SectionSkipEmpty);
    case PathName:
    case AbsolutePathName:
    case CanonicalPathName:
        return m_fileNamePath.section(QChar::fromLatin1('/'), 0, -2, QString::SectionSkipEmpty);
    case DefaultName:
    case AbsoluteName:
    case CanonicalName:
        return m_fileNamePath;
    default:
        return QString();
    }
}

/**
 * \reimp
 */
bool BinaryFormatEngine::copy(const QString &newName)
{
    if (QFile::exists(newName))
        return false;

    QFile target(newName);
    if (!target.open(QIODevice::WriteOnly))
        return false;

    qint64 bytesLeft = size();
    if (!open(QIODevice::ReadOnly))
        return false;

    char data[4096];
    while(bytesLeft > 0) {
        const qint64 len = qMin<qint64>(bytesLeft, 4096);
        const qint64 bytesRead = read(data, len);
        if (bytesRead != len) {
            close();
            return false;
        }
        const qint64 bytesWritten = target.write(data, len);
        if (bytesWritten != len) {
            close();
            return false;
        }
        bytesLeft -= len;
    }
    close();

    return true;
}

/**
 * \reimp
 */
QAbstractFileEngine::FileFlags BinaryFormatEngine::fileFlags(FileFlags type) const
{
    FileFlags result;
    if ((type & FileType) && m_archive != 0)
        result |= FileType;
    if ((type & DirectoryType) && !m_hasArchive)
        result |= DirectoryType;
    if ((type & ExistsFlag) && m_hasArchive && m_archive != 0)
        result |= ExistsFlag;
    if ((type & ExistsFlag) && !m_hasArchive && !m_component.name().isEmpty())
        result |= ExistsFlag;

    return result;
}

/**
 * \reimp
 */
QAbstractFileEngineIterator *BinaryFormatEngine::beginEntryList(QDir::Filters filters, const QStringList &filterNames)
{
    const QStringList entries = entryList(filters, filterNames);
    return new StringListIterator(entries, filters, filterNames);
}

/**
 * \reimp
 */
QStringList BinaryFormatEngine::entryList(QDir::Filters filters, const QStringList &filterNames) const
{
    if (m_hasArchive)
        return QStringList();
    
    QStringList result;

    if (m_hasComponent && (filters & QDir::Files)) {
        const QVector< QSharedPointer<Archive> > archives = m_component.archives();
        foreach (const QSharedPointer<Archive> &i, archives)
            result.push_back(QString::fromUtf8(i->name()));
    }
    else if (!m_hasComponent && (filters & QDir::Dirs)) {
        const QVector<Component> components = m_index.components();
        foreach (const Component &i, components)
            result.push_back(QString::fromUtf8(i.name()));
    }

    if (filterNames.isEmpty())
        return result;

    QList<QRegExp> regexps;
    foreach (const QString &i, filterNames)
        regexps.push_back(QRegExp(i, Qt::CaseInsensitive, QRegExp::Wildcard));

    QStringList entries;
    foreach (const QString &i, result) {
        bool matched = false;
        foreach (const QRegExp &reg, regexps) {
            matched = reg.exactMatch(i);
            if (matched)
                break;
        }
        if (matched)
            entries.push_back(i);
    }

    return entries;
}
 
/**
 * \reimp
 */
qint64 BinaryFormatEngine::size() const
{
    return m_archive == 0 ? 0 : m_archive->size();
}
