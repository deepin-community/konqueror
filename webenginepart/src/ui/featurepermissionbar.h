/*
 * This file is part of the KDE project.
 *
 * Copyright (C) 2013 Allan Sandfeld Jensen <sandfeld @ kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef FEATUREPERMISSIONBAR_H
#define FEATUREPERMISSIONBAR_H

#include <KMessageWidget>
#include <KLocalizedString>

#include <QWebEnginePage>
#include <QUrl>


class FeaturePermissionBar : public KMessageWidget
{
    Q_OBJECT
public:
    explicit FeaturePermissionBar(QWidget *parent = nullptr);
    ~FeaturePermissionBar() override;

    QWebEnginePage::Feature feature() const;
    QUrl url() const;

    void setFeature(QWebEnginePage::Feature);
    void setUrl(const QUrl &url);

Q_SIGNALS:
    void permissionPolicyChosen(QWebEnginePage::Feature feature, QWebEnginePage::PermissionPolicy policy);
    void done();

private Q_SLOTS:
    void onDeniedButtonClicked();
    void onGrantedButtonClicked();

private:
    QString labelText() const;

private:
    QWebEnginePage::Feature m_feature;
    QUrl m_url;
};

#endif // FEATUREPERMISSIONBAR_H
