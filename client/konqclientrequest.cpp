/* This file is part of the KDE project
   Copyright (C) 2017 David Faure <faure@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "konqclientrequest.h"
#include <config-konqueror.h> // KONQ_HAVE_X11
#include <konq_main_interface.h>
#include <konq_mainwindow_interface.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QProcess>
#include <QUrl>

#if KONQ_HAVE_X11
#include <QX11Info>
#endif

#include <KConfig>
#include <KConfigGroup>
#include <KStartupInfo>
#include <KWindowSystem>

#include "kfmclient_debug.h"

class KonqClientRequestPrivate
{
public:
    void sendASNChange();

    QUrl url;
    bool newTab = false;
    bool tempFile = false;
    QString mimeType;
    QByteArray startup_id_str;
};

KonqClientRequest::KonqClientRequest()
    : d(new KonqClientRequestPrivate)
{
    // read ASN env. variable
    d->startup_id_str = KStartupInfo::currentStartupIdEnv().id();
}

KonqClientRequest::~KonqClientRequest()
{
}

void KonqClientRequest::setUrl(const QUrl& url)
{
    d->url = url;
}

void KonqClientRequest::setNewTab(bool newTab)
{
    d->newTab = newTab;
}

void KonqClientRequest::setTempFile(bool tempFile)
{
    d->tempFile = tempFile;
}

void KonqClientRequest::setMimeType(const QString &mimeType)
{
    d->mimeType = mimeType;
}

void KonqClientRequestPrivate::sendASNChange()
{
#if KONQ_HAVE_X11
    if (KWindowSystem::platform() == KWindowSystem::Platform::X11) {
        KStartupInfoId id;
        id.initId(startup_id_str);
        KStartupInfoData data;
        data.addPid(0);     // say there's another process for this ASN with unknown PID
        data.setHostname(); // ( no need to bother to get this konqy's PID )
        KStartupInfo::sendChangeXcb(QX11Info::connection(), QX11Info::appScreen(), id, data);
    }
#endif
}

bool KonqClientRequest::openUrl()
{
    QDBusConnection dbus = QDBusConnection::sessionBus();
    const QString appId = QStringLiteral("org.kde.konqueror");
    org::kde::Konqueror::Main konq(appId, QStringLiteral("/KonqMain"), dbus);

    if (!d->newTab) {
        KConfig cfg(QStringLiteral("konquerorrc"));
        d->newTab = cfg.group("FMSettings").readEntry("KonquerorTabforExternalURL", false);
    }
    if (d->newTab) {
        QDBusObjectPath foundObj;
        QDBusReply<QDBusObjectPath> windowReply = konq.windowForTab();
        if (windowReply.isValid()) {
            QDBusObjectPath path = windowReply;
            // "/" is the indicator for "no object found", since we can't use an empty path
            if (path.path() != QLatin1String("/")) {
                org::kde::Konqueror::MainWindow konqWindow(appId, path.path(), dbus);
                QDBusReply<void> newTabReply = konqWindow.newTabASNWithMimeType(d->url.toString(), d->mimeType, d->startup_id_str, d->tempFile);
                if (newTabReply.isValid()) {
                    d->sendASNChange();
                    return true;
                }
            }
        }
    }

    QDBusReply<QDBusObjectPath> reply = konq.createNewWindow(d->url.toString(), d->mimeType, d->startup_id_str, d->tempFile);
    if (reply.isValid()) {
        d->sendASNChange();
        return true;
    } else {
        // pass kfmclient's startup id to konqueror using kshell
        KStartupInfoId id;
        id.initId(d->startup_id_str);
        id.setupStartupEnv();
        QStringList args;
        args << QStringLiteral("konqueror");
        if (!d->mimeType.isEmpty()) {
            args << QStringLiteral("--mimetype") << d->mimeType;
        }
        if (d->tempFile) {
            args << QStringLiteral("-tempfile");
        }
        args << d->url.toEncoded();
        qint64 pid;
#ifdef Q_OS_WIN
        const bool ok = QProcess::startDetached(QStringLiteral("kwrapper5"), args, QString(), &pid);
#else
        const bool ok = QProcess::startDetached(QStringLiteral("kshell5"), args, QString(), &pid);
#endif
        KStartupInfo::resetStartupEnv();
        if (ok) {
            qCDebug(KFMCLIENT_LOG) << "Konqueror started, pid=" << pid;
        } else {
            qCWarning(KFMCLIENT_LOG) << "Error starting konqueror";
        }
        return ok;
    }
}
