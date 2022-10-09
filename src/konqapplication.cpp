/* This file is part of the KDE project
   Copyright (C) 2006 David Faure <faure@kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include <QtGlobal>

#include "konqapplication.h"
#include "konqsettings.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include "konqmainwindow.h"
#include "KonquerorAdaptor.h"
#include "konqviewmanager.h"
#include <KSharedConfig>

KonquerorApplication::KonquerorApplication(int &argc, char **argv)
    : QApplication(argc, argv)
{
    // enable high dpi support
    setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    new KonquerorAdaptor; // not really an adaptor
    const QString dbusInterface = QStringLiteral("org.kde.Konqueror.Main");
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.connect(QString(), KONQ_MAIN_PATH, dbusInterface, QStringLiteral("reparseConfiguration"), this, SLOT(slotReparseConfiguration()));
    dbus.connect(QString(), KONQ_MAIN_PATH, dbusInterface, QStringLiteral("addToCombo"), this,
                 SLOT(slotAddToCombo(QString,QDBusMessage)));
    dbus.connect(QString(), KONQ_MAIN_PATH, dbusInterface, QStringLiteral("removeFromCombo"), this,
                 SLOT(slotRemoveFromCombo(QString,QDBusMessage)));
    dbus.connect(QString(), KONQ_MAIN_PATH, dbusInterface, QStringLiteral("comboCleared"), this, SLOT(slotComboCleared(QDBusMessage)));

#ifdef WEBENGINEPART_DICTIONARY_DIR
    if (!qEnvironmentVariableIsSet("QTWEBENGINE_DICTIONARIES_PATH")) {
        qputenv("QTWEBENGINE_DICTIONARIES_PATH", WEBENGINEPART_DICTIONARY_DIR);
    }
#endif
}

void KonquerorApplication::slotReparseConfiguration()
{
    KSharedConfig::openConfig()->reparseConfiguration();
    KonqFMSettings::reparseConfiguration();

    QList<KonqMainWindow *> *mainWindows = KonqMainWindow::mainWindowList();
    if (mainWindows) {
        foreach (KonqMainWindow *window, *mainWindows) {
            window->reparseConfiguration();
        }
    }
}

void KonquerorApplication::slotAddToCombo(const QString &url, const QDBusMessage &msg)
{
    KonqMainWindow::comboAction(KonqMainWindow::ComboAdd, url, msg.service());
}

void KonquerorApplication::slotRemoveFromCombo(const QString &url, const QDBusMessage &msg)
{
    KonqMainWindow::comboAction(KonqMainWindow::ComboRemove, url, msg.service());
}

void KonquerorApplication::slotComboCleared(const QDBusMessage &msg)
{
    KonqMainWindow::comboAction(KonqMainWindow::ComboClear, QString(), msg.service());
}

