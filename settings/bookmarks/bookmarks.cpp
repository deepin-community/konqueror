/*
Copyright (C) 2008 Xavier Vello <xavier.vello@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

// Own
#include "bookmarks.h"

// Qt
#include <QCheckBox>
#include <QPushButton>

// KDE
#include <kconfig.h>
#include <kpluginfactory.h>
#include <KLocalizedString>
#include <KConfigGroup>
#include <kimagecache.h>

K_PLUGIN_FACTORY_DECLARATION(KioConfigFactory)

BookmarksConfigModule::BookmarksConfigModule(QWidget *parent, const QVariantList &)
    : KCModule(parent)
{
    ui.setupUi(this);
}

BookmarksConfigModule::~BookmarksConfigModule()
{
}

void BookmarksConfigModule::clearCache()
{
    KImageCache::deleteCache(QStringLiteral("kio_bookmarks"));
}

void BookmarksConfigModule::load()
{
    KConfig *c = new KConfig(QStringLiteral("kiobookmarksrc"));
    KConfigGroup group = c->group("General");

    ui.sbColumns->setValue(group.readEntry("Columns", 4));
    ui.cbShowBackgrounds->setChecked(group.readEntry("ShowBackgrounds", true));
    ui.cbShowRoot->setChecked(group.readEntry("ShowRoot", true));
    ui.cbFlattenTree->setChecked(group.readEntry("FlattenTree", false));
    ui.cbShowPlaces->setChecked(group.readEntry("ShowPlaces", true));
    ui.sbCacheSize->setValue(group.readEntry("CacheSize", 5 * 1024));

    // Config changed notifications...
    connect(ui.sbColumns, QOverload<int>::of(&QSpinBox::valueChanged), this, &BookmarksConfigModule::configChanged);
    connect(ui.cbShowBackgrounds, &QAbstractButton::toggled, this, &BookmarksConfigModule::configChanged);
    connect(ui.cbShowRoot, &QAbstractButton::toggled, this, &BookmarksConfigModule::configChanged);
    connect(ui.cbFlattenTree, &QAbstractButton::toggled, this, &BookmarksConfigModule::configChanged);
    connect(ui.cbShowPlaces, &QAbstractButton::toggled, this, &BookmarksConfigModule::configChanged);
    connect(ui.sbCacheSize, QOverload<int>::of(&QSpinBox::valueChanged), this, &BookmarksConfigModule::configChanged);

    connect(ui.clearCacheButton, &QAbstractButton::clicked, this, &BookmarksConfigModule::clearCache);

    delete c;
    emit changed(false);
}

void BookmarksConfigModule::save()
{
    KConfig *c = new KConfig(QStringLiteral("kiobookmarksrc"));
    KConfigGroup group = c->group("General");
    group.writeEntry("Columns", ui.sbColumns->value());
    group.writeEntry("ShowBackgrounds", ui.cbShowBackgrounds->isChecked());
    group.writeEntry("ShowRoot", ui.cbShowRoot->isChecked());
    group.writeEntry("FlattenTree", ui.cbFlattenTree->isChecked());
    group.writeEntry("ShowPlaces", ui.cbShowPlaces->isChecked());
    group.writeEntry("CacheSize", ui.sbCacheSize->value());

    c->sync();
    delete c;
    emit changed(false);
}

void BookmarksConfigModule::defaults()
{
    ui.sbColumns->setValue(4);
    ui.cbShowBackgrounds->setChecked(true);
    ui.cbShowRoot->setChecked(true);
    ui.cbShowPlaces->setChecked(true);
    ui.cbFlattenTree->setChecked(false);
    ui.sbCacheSize->setValue(5 * 1024);
}

QString BookmarksConfigModule::quickHelp() const
{
    return i18n("<h1>My Bookmarks</h1><p>This module lets you configure the bookmarks home page.</p>"
                "<p>The bookmarks home page is accessible at <a href=\"bookmarks:/\">bookmarks:/</a>.</p>");
}

void BookmarksConfigModule::configChanged()
{
    emit changed(true);
}

//
