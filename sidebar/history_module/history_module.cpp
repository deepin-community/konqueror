/*
    Copyright (c) 2009 David Faure <faure@kde.org>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2 of the License or ( at
    your option ) version 3 or, at the discretion of KDE e.V. ( which shall
    act as a proxy as in section 14 of the GPLv3 ), any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "history_module.h"
#include <konqhistoryview.h>

#include <QAction>
#include <QApplication>
#include <QTreeView>
#include <QIcon>

#include <KLocalizedString>
#include <kpluginfactory.h>

KonqSidebarHistoryModule::KonqSidebarHistoryModule(QWidget *parent,
        const KConfigGroup &configGroup)
    : KonqSidebarModule(parent, configGroup), m_settings(KonqHistorySettings::self())
{
    m_historyView = new KonqHistoryView(parent);
    connect(m_historyView->treeView(), &QAbstractItemView::activated, this, &KonqSidebarHistoryModule::slotActivated);
    connect(m_historyView->treeView(), &QAbstractItemView::pressed, this, &KonqSidebarHistoryModule::slotPressed);
    connect(m_historyView->treeView(), &QAbstractItemView::clicked, this, &KonqSidebarHistoryModule::slotClicked);
    connect(m_historyView, &KonqHistoryView::openUrlInNewWindow, this, &KonqSidebarHistoryModule::slotOpenWindow);
    connect(m_historyView, &KonqHistoryView::openUrlInNewTab, this, &KonqSidebarHistoryModule::slotOpenTab);
    connect(m_settings, &KonqHistorySettings::settingsChanged, this, &KonqSidebarHistoryModule::reparseConfiguration);
    reparseConfiguration();
}

KonqSidebarHistoryModule::~KonqSidebarHistoryModule()
{
}

void KonqSidebarHistoryModule::reparseConfiguration()
{
    m_defaultAction = m_settings->m_defaultAction;
}


QWidget *KonqSidebarHistoryModule::getWidget()
{
    return m_historyView;
}

void KonqSidebarHistoryModule::slotCurViewUrlChanged(const QUrl& url)
{
    m_currentUrl = url;
}


// LMB activation (single or double click) handling
void KonqSidebarHistoryModule::slotActivated(const QModelIndex &index)
{
    if (m_lastPressedButtons == Qt::MiddleButton) { // already handled by slotClicked
        return;
    }
    const QUrl url = m_historyView->urlForIndex(index);
    if (!url.isValid()) {
        return;
    }
    if (m_defaultAction == KonqHistorySettings::Action::OpenNewWindow) {
        slotOpenWindow(url);
        return;
    }
    KParts::BrowserArguments bargs;
    //Ideally, if m_defaultAction is Auto, the current tab should only be created if the current tab
    //has a konq: or an empty URL. However, it seems you can't get this information from here, so always
    //open a new tab unless the default action is OpenCurrentTab
    if (m_defaultAction == KonqHistorySettings::Action::OpenCurrentTab) {
        bargs.setNewTab(true);
    } else if (m_defaultAction == KonqHistorySettings::Action::Auto && !(m_currentUrl.isEmpty() || m_currentUrl.scheme() == "konq")) {
        bargs.setNewTab(true);
    }
    emit openUrlRequest(url, KParts::OpenUrlArguments(), bargs);
}

// Needed for MMB handling; no convenient API in QAbstractItemView
void KonqSidebarHistoryModule::slotPressed(const QModelIndex &index)
{
    Q_UNUSED(index);
    m_lastPressedButtons = qApp->mouseButtons();
}

// MMB handling
void KonqSidebarHistoryModule::slotClicked(const QModelIndex &index)
{
    if (m_lastPressedButtons & Qt::MiddleButton) {
        const QUrl url = m_historyView->urlForIndex(index);
        if (url.isValid()) {
            createNewWindow(url);
        }
    }
}

void KonqSidebarHistoryModule::slotOpenWindow(const QUrl &url)
{
    KParts::OpenUrlArguments args;
    args.setActionRequestedByUser(true);
    KParts::BrowserArguments browserArgs;
    browserArgs.setForcesNewWindow(true);
    createNewWindow(url, args, browserArgs);
}

void KonqSidebarHistoryModule::slotOpenTab(const QUrl &url)
{
    KParts::OpenUrlArguments args;
    args.setActionRequestedByUser(true);
    KParts::BrowserArguments browserArgs;
    browserArgs.setNewTab(true);
    createNewWindow(url, args, browserArgs);
}

class KonqSidebarHistoryPlugin : public KonqSidebarPlugin
{
public:
    KonqSidebarHistoryPlugin(QObject *parent, const QVariantList &args)
        : KonqSidebarPlugin(parent, args) {}
    ~KonqSidebarHistoryPlugin() override {}

    KonqSidebarModule *createModule(QWidget *parent,
                                            const KConfigGroup &configGroup,
                                            const QString &desktopname,
                                            const QVariant &unused) override
    {
        Q_UNUSED(unused);
        Q_UNUSED(desktopname);
        return new KonqSidebarHistoryModule(parent, configGroup);
    }

    QList<QAction *> addNewActions(QObject *parent,
                                           const QList<KConfigGroup> &existingModules,
                                           const QVariant &unused) override
    {
        Q_UNUSED(unused);
        Q_UNUSED(existingModules);
        QAction *action = new QAction(parent);
        action->setText(i18nc("@action:inmenu Add", "History Sidebar Module"));
        action->setIcon(QIcon::fromTheme("view-history"));
        return QList<QAction *>() << action;
    }

    QString templateNameForNewModule(const QVariant &actionData,
            const QVariant &unused) const override
    {
        Q_UNUSED(actionData);
        Q_UNUSED(unused);
        return QString::fromLatin1("historyplugin%1.desktop");
    }

    bool createNewModule(const QVariant &actionData, KConfigGroup &configGroup,
                                 QWidget *parentWidget,
                                 const QVariant &unused) override
    {
        Q_UNUSED(parentWidget);
        Q_UNUSED(actionData);
        Q_UNUSED(unused);

        configGroup.writeEntry("Type", "Link");
        configGroup.writeEntry("Icon", "view-history");
        configGroup.writeEntry("Name", i18nc("@title:tab", "History"));
        configGroup.writeEntry("X-KDE-KonqSidebarModule", "konqsidebar_history");
        return true;
    }
};

K_PLUGIN_FACTORY(KonqSidebarHistoryPluginFactory, registerPlugin<KonqSidebarHistoryPlugin>();)

#include "history_module.moc"
