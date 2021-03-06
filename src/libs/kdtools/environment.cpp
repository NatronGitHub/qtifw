/****************************************************************************
**
** Copyright (C) 2013 Klaralvdalens Datakonsult AB (KDAB)
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
****************************************************************************/

#include "environment.h"

#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>

using namespace KDUpdater;

Environment &Environment::instance()
{
    static Environment s_instance;
    return s_instance;
}

QString Environment::value(const QString &key, const QString &defvalue) const
{
    const QHash<QString, QString>::ConstIterator it = m_tempValues.constFind(key);
    if (it != m_tempValues.constEnd())
        return *it;
    return QProcessEnvironment::systemEnvironment().value(key, defvalue);
}

void Environment::setTemporaryValue(const QString &key, const QString &value)
{
    m_tempValues.insert(key, value);
}

QProcessEnvironment Environment::applyTo(const QProcessEnvironment &qpe_) const
{
    QProcessEnvironment qpe(qpe_);
    QHash<QString, QString>::ConstIterator it = m_tempValues.constBegin();
    const QHash<QString, QString>::ConstIterator end = m_tempValues.constEnd();
    for ( ; it != end; ++it)
        qpe.insert(it.key(), it.value());
    return qpe;
}

void Environment::applyTo(QProcess *proc)
{
    proc->setProcessEnvironment(applyTo(proc->processEnvironment()));
}
