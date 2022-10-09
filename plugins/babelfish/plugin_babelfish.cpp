/* This file is part of the KDE Project
   Copyright (C) 2001 Kurt Granroth <granroth@kde.org>
     Original code: plugin code, connecting to Babelfish and support for selected text
   Copyright (C) 2003 Rand2342 <rand2342@yahoo.com>
     Submenus, KConfig file and support for other translation engines
   Copyright (C) 2008 Montel Laurent <montel@kde.org>
     Add webkitkde support
   Copyright (C) 2010 David Faure <faure@kde.org>
     Ported to KParts::TextExtension

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/
#include "plugin_babelfish.h"

#include <kparts/part.h>
#include <kparts/browserextension.h>
#include <kparts/textextension.h>

#include <kwidgetsaddons_version.h>
#include <kactioncollection.h>
#include <QMenu>
#include <kmessagebox.h>
#include <KLocalizedString>
#include <kconfig.h>
#include <kpluginfactory.h>
#include <kaboutdata.h>

#include <KConfigGroup>

#include <QDebug>

static const KAboutData aboutdata(QStringLiteral("babelfish"), i18n("Translate Web Page"), QStringLiteral("1.0"));
K_PLUGIN_FACTORY(BabelFishFactory, registerPlugin<PluginBabelFish>();)

PluginBabelFish::PluginBabelFish(QObject *parent,
                                 const QVariantList &)
    : Plugin(parent),
      m_actionGroup(this)
{
    m_menu = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("Translate Web &Page"),
                             actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage"), m_menu);
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 77, 0)
    m_menu->setPopupMode(QToolButton::InstantPopup);
#else
    m_menu->setDelayed(false);
#endif
    connect(m_menu->menu(), SIGNAL(aboutToShow()),
            this, SLOT(slotAboutToShow()));

    KParts::ReadOnlyPart *part = qobject_cast<KParts::ReadOnlyPart *>(parent);
    if (part) {
        connect(part, SIGNAL(started(KIO::Job*)), this,
                SLOT(slotEnableMenu()));
        connect(part, SIGNAL(completed()), this,
                SLOT(slotEnableMenu()));
        connect(part, SIGNAL(completed(bool)), this,
                SLOT(slotEnableMenu()));
    }
}

void PluginBabelFish::addTopLevelAction(const QString &name, const QString &text)
{
    QAction *a = actionCollection()->addAction(name);
    a->setText(text);
    m_menu->addAction(a);
    m_actionGroup.addAction(a);
}

void PluginBabelFish::slotAboutToShow()
{
    if (!m_menu->menu()->actions().isEmpty()) { // already populated
        return;
    }

    connect(&m_actionGroup, SIGNAL(triggered(QAction*)), this, SLOT(translateURL(QAction*)));

    // Create a menu for each "source->dest" translation possibility where
    // there's more than one dest for a given source.

    KActionMenu *menu_en = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&English To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_en"), menu_en);

    KActionMenu *menu_fr = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&French To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_fr"), menu_fr);

    KActionMenu *menu_de = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&German To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_de"), menu_de);

    KActionMenu *menu_el = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&Greek To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_el"), menu_el);

    KActionMenu *menu_es = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&Spanish To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_es"), menu_es);

    KActionMenu *menu_pt = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&Portuguese To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_pt"), menu_pt);

    KActionMenu *menu_it = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&Italian To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_it"), menu_it);

    KActionMenu *menu_nl = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&Dutch To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_nl"), menu_nl);

    KActionMenu *menu_ru = new KActionMenu(QIcon::fromTheme(QStringLiteral("babelfish")), i18n("&Russian To"), actionCollection());
    actionCollection()->addAction(QStringLiteral("translatewebpage_ru"), menu_ru);

    // List of translation possibilities for filling the above menus
    // Mostly from babelfish.
    // TODO: add all translation paths from www.reverso.net
    // and all translation paths from www.freetranslation.com
    // and all those from translate.google.com!! Full matrix supported... we need a different system...
    // [possibly in a different list, so that we can remove it more easily if
    //  it goes offline?]
    // TODO: Maybe the entire list of possibilities could come from translaterc?
    // (It would need source, dest, engine, language-code-on-that-engine)
    // Here we would just have the i18n texts for each lang code,
    // and we would make up the menus on the fly.

    static const char *const translations[] = {
        I18N_NOOP("&Chinese (Simplified)"), "en_zh",
        I18N_NOOP("Chinese (&Traditional)"), "en_zt",
        I18N_NOOP("&Dutch"), "en_nl",
        I18N_NOOP("&French"), "en_fr",
        I18N_NOOP("&German"), "en_de",
        I18N_NOOP("&Greek"), "en_el",
        I18N_NOOP("&Italian"), "en_it",
        I18N_NOOP("&Japanese"), "en_ja",
        I18N_NOOP("&Korean"), "en_ko",
        I18N_NOOP("&Norwegian"), "en_no",
        I18N_NOOP("&Portuguese"), "en_pt",
        I18N_NOOP("&Russian"), "en_ru",
        I18N_NOOP("&Spanish"), "en_es",
        I18N_NOOP("T&hai"), "en_th", // only on parsit
        I18N_NOOP("&Arabic"), "fr_ar", // google
        I18N_NOOP("&Dutch"), "fr_nl",
        I18N_NOOP("&English"), "fr_en",
        I18N_NOOP("&German"), "fr_de",
        I18N_NOOP("&Greek"), "fr_el",
        I18N_NOOP("&Italian"), "fr_it",
        I18N_NOOP("&Portuguese"), "fr_pt",
        I18N_NOOP("&Spanish"), "fr_es",
        I18N_NOOP("&Russian"), "fr_ru", // only on voila
        I18N_NOOP("&English"), "de_en",
        I18N_NOOP("&French"), "de_fr",
        I18N_NOOP("&English"), "el_en",
        I18N_NOOP("&French"), "el_fr",
        I18N_NOOP("&English"), "es_en",
        I18N_NOOP("&French"), "es_fr",
        I18N_NOOP("&English"), "pt_en",
        I18N_NOOP("&French"), "pt_fr",
        I18N_NOOP("&English"), "it_en",
        I18N_NOOP("&French"), "it_fr",
        I18N_NOOP("&English"), "nl_en",
        I18N_NOOP("&French"), "nl_fr",
        I18N_NOOP("&English"), "ru_en",
        I18N_NOOP("&French"), "ru_fr", // only on voila
        nullptr, nullptr
    };

    for (int i = 0; translations[i]; i += 2) {
        const QString translation = QString::fromLatin1(translations[i + 1]);
        const int underScorePos = translation.indexOf(QLatin1Char('_'));
        const QString srcLang = translation.left(underScorePos);
        QAction *srcAction = actionCollection()->action(QLatin1String("translatewebpage_") + srcLang);
        if (KActionMenu *actionMenu = qobject_cast<KActionMenu *>(srcAction)) {
            QAction *a = actionCollection()->addAction(translation);
            m_actionGroup.addAction(a);
            a->setText(i18n(translations[i]));
            actionMenu->addAction(a);
        } else {
            qWarning() << "No menu found for" << srcLang;
        }
    }

    // Fill the toplevel menu, both with single source->dest possibilities
    // and with submenus.

    addTopLevelAction(QStringLiteral("zh_en"), i18n("&Chinese (Simplified) to English"));
    addTopLevelAction(QStringLiteral("zt_en"), i18n("Chinese (&Traditional) to English"));

    m_menu->addAction(menu_nl);
    m_menu->addAction(menu_en);
    m_menu->addAction(menu_fr);
    m_menu->addAction(menu_de);
    m_menu->addAction(menu_el);
    m_menu->addAction(menu_it);

    addTopLevelAction(QStringLiteral("ja_en"), i18n("&Japanese to English"));
    addTopLevelAction(QStringLiteral("ko_en"), i18n("&Korean to English"));

    m_menu->addAction(menu_pt);
    m_menu->addAction(menu_ru);
    m_menu->addAction(menu_es);

    // TODO we could sort the action texts alphabetically, so that they wouldn't
    // be sorted in English only...
}

PluginBabelFish::~PluginBabelFish()
{
    delete m_menu;
}

// Decide whether or not to enable the menu
void PluginBabelFish::slotEnableMenu()
{
    KParts::ReadOnlyPart *part = qobject_cast<KParts::ReadOnlyPart *>(parent());
    KParts::TextExtension *textExt = KParts::TextExtension::childObject(parent());

    //if (part)
    //    kDebug() << part->url();

    if (part && textExt) {
        const QString scheme = part->url().scheme();	// always lower case
        if ((scheme == QLatin1String("http")) || (scheme == QLatin1String("https"))) {
            if (KParts::BrowserExtension::childObject(part)) {
                m_menu->setEnabled(true);
                return;
            }
        }
    }

    m_menu->setEnabled(false);
}

void PluginBabelFish::translateURL(QAction *action)
{
    // KHTMLPart and KWebKitPart provide a TextExtension, at least.
    // So if we got a part without a TextExtension, give an error.
    KParts::TextExtension *textExt = KParts::TextExtension::childObject(parent());
    Q_ASSERT(textExt); // already checked in slotAboutToShow

    // Select engine
    const KConfig cfg(QStringLiteral("translaterc"));
    const KConfigGroup grp(&cfg, QString());
    const QString language = action->objectName();
    const QString engine = grp.readEntry(language, QStringLiteral("google"));

    KParts::BrowserExtension *ext = KParts::BrowserExtension::childObject(parent());
    if (!ext) {
        return;
    }
    KParts::ReadOnlyPart *part = qobject_cast<KParts::ReadOnlyPart *>(parent());

    // we check if we have text selected.  if so, we translate that. If
    // not, we translate the url
    QString textForQuery;
    QUrl url = part->url();
    const bool hasSelection = textExt->hasSelection();

    if (hasSelection) {
        QString selection = textExt->selectedText(KParts::TextExtension::PlainText);
        textForQuery = QString::fromLatin1(QUrl::toPercentEncoding(selection));
    } else {
        // Check syntax
        if (!url.isValid()) {
            QString title = i18nc("@title:window", "Malformed URL");
            QString text = i18n("The URL you entered is not valid, please "
                                "correct it and try again.");
            KMessageBox::sorry(part->widget(), text, title);
            return;
        }
    }
    const QString urlForQuery = QLatin1String(QUrl::toPercentEncoding(url.url()));

    if (url.scheme() == QLatin1String("https")) {
        if (KMessageBox::warningContinueCancel(part->widget(),
                                               xi18nc("@info", "\
You are viewing this page over a secure connection.<nl/><nl/>\
The URL of the page will sent to the online translation service,<nl/>\
which may fetch the insecure version of the page."),
                                               i18nc("@title:window", "Security Warning"),
                                               KStandardGuiItem::cont(),
                                               KStandardGuiItem::cancel(),
                                               QStringLiteral("insecureTranslate")) != KMessageBox::Continue) {
            return;
        }
    }

    // Create URL
    QUrl result;
    QString query;

    if (engine == QLatin1String("freetranslation")) {
        query = QStringLiteral("sequence=core&language=");
        if (language == QStringLiteral("en_es")) {
            query += QLatin1String("English/Spanish");
        } else if (language == QStringLiteral("en_de")) {
            query += QLatin1String("English/German");
        } else if (language == QStringLiteral("en_it")) {
            query += QLatin1String("English/Italian");
        } else if (language == QStringLiteral("en_nl")) {
            query += QLatin1String("English/Dutch");
        } else if (language == QStringLiteral("en_pt")) {
            query += QLatin1String("English/Portuguese");
        } else if (language == QStringLiteral("en_no")) {
            query += QLatin1String("English/Norwegian");
        } else if (language == QStringLiteral("en_zh")) {
            query += QLatin1String("English/SimplifiedChinese");
        } else if (language == QStringLiteral("en_zhTW")) {
            query += QLatin1String("English/TraditionalChinese");
        } else if (language == QStringLiteral("es_en")) {
            query += QLatin1String("Spanish/English");
        } else if (language == QStringLiteral("fr_en")) {
            query += QLatin1String("French/English");
        } else if (language == QStringLiteral("de_en")) {
            query += QLatin1String("German/English");
        } else if (language == QStringLiteral("it_en")) {
            query += QLatin1String("Italian/English");
        } else if (language == QStringLiteral("nl_en")) {
            query += QLatin1String("Dutch/English");
        } else if (language == QStringLiteral("pt_en")) {
            query += QLatin1String("Portuguese/English");
        } else { // Should be en_fr
            query += QLatin1String("English/French");
        }
        if (hasSelection) {
            // ## does not work
            result = QUrl(QStringLiteral("http://ets.freetranslation.com"));
            query += QLatin1String("&mode=html&template=results_en-us.htm&srctext=");
            query += textForQuery;
        } else {
            result = QUrl(QStringLiteral("http://www.freetranslation.com/web.asp"));
            query += QLatin1String("&url=");
            query += urlForQuery;
        }
    } else if (engine == QLatin1String("parsit")) {
        // Does only English -> Thai
        result = QUrl(QStringLiteral("http://c3po.links.nectec.or.th/cgi-bin/Parsitcgi.exe"));
        query = QStringLiteral("mode=test&inputtype=");
        if (hasSelection) {
            query += QLatin1String("text&TxtEng=");
            query += textForQuery;
        } else {
            query += QLatin1String("html&inputurl=");
            query += urlForQuery;
        }
    } else if (engine == QLatin1String("reverso")) {
        result = QUrl(QStringLiteral("http://www.reverso.net/url/frame.asp"));
        query = QStringLiteral("autotranslate=on&templates=0&x=0&y=0&directions=");
        if (language == QStringLiteral("de_fr")) {
            query += QLatin1String("524292");
        } else if (language == QStringLiteral("fr_en")) {
            query += QLatin1String("65544");
        } else if (language == QStringLiteral("fr_de")) {
            query += QLatin1String("262152");
        } else if (language == QStringLiteral("de_en")) {
            query += QLatin1String("65540");
        } else if (language == QStringLiteral("en_de")) {
            query += QLatin1String("262145");
        } else if (language == QStringLiteral("en_es")) {
            query += QLatin1String("2097153");
        } else if (language == QStringLiteral("es_en")) {
            query += QLatin1String("65568");
        } else if (language == QStringLiteral("fr_es")) {
            query += QLatin1String("2097160");
        } else { // "en_fr"
            query += QLatin1String("524289");
        }
        query += QLatin1String("&url=");
        query += urlForQuery;
    } else if (engine == QLatin1String("tsail")) {
        result = QUrl(QStringLiteral("http://www.t-mail.com/cgi-bin/tsail"));
        query = QStringLiteral("sail=full&lp=");
        if (language == QStringLiteral("zhTW_en")) {
            query += QLatin1String("tw-en");
        } else if (language == QStringLiteral("en_zhTW")) {
            query += QLatin1String("en-tw");
        } else {
            query += language;
            query[15] = '-';
        }
        query += urlForQuery;
    } else if (engine == QLatin1String("voila")) {
        result = QUrl(QStringLiteral("http://tr.voila.fr/traduire-une-page-web-frame.php"));
        const QStringList parts = language.split('_');
        if (parts.count() == 2) {
            // The translation direction is "first letter of source, first letter of dest"
            query = QStringLiteral("translationDirection=");
            query += parts[0][0];
            query += parts[1][0];
            if (false /*hasSelection*/) {   // TODO: needs POST
                query += QLatin1String("&isText=1&stext=");
                query += textForQuery;
            } else {
                query += QLatin1String("&urlToTranslate=");
                query += urlForQuery;
            }
        }
    } else {

        const QStringList parts = language.split('_');

        if (hasSelection) { //https://translate.google.com/#en|de|text_to_translate
            query = QStringLiteral("https://translate.google.com/#");
            if (parts.count() == 2) {
                query += parts[0] + "|" + parts[1];
            }
            query += "|" + textForQuery;
            result = QUrl(query);
        } else { //https://translate.google.com/translate?hl=en&sl=en&tl=de&u=http%3A%2F%2Fkde.org%2F%2F
            result = QUrl(QStringLiteral("https://translate.google.com/translate"));
            query = QStringLiteral("ie=UTF-8");
            if (parts.count() == 2) {
                query += "&sl=" + parts[0] + "&tl=" + parts[1];
            }
            query += "&u=" + urlForQuery;
            result.setQuery(query);
        }
    }

    // Connect to the fish
    emit ext->openUrlRequest(result);
}

#include <plugin_babelfish.moc>
