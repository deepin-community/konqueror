/*
 * This file is part of the KDE project.
 *
 * Copyright (C) 2009 Dawit Alemayehu <adawit@kde.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * The code for the function slotPrintPreview was adapted from the PrintMe Qt example
 * and is distributed under the BSD license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of The Qt Company Ltd nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 */

#include "webenginepart_ext.h"
#include <QtWebEngine/QtWebEngineVersion>

#include "webenginepart.h"
#include "webengineview.h"
#include "webenginepage.h"
#include "settings/webenginesettings.h"
#include <webenginepart_debug.h>

#include <QWebEngineSettings>

#include <KDesktopFile>
#include <KConfigGroup>
#include <KToolInvocation>
#include <KSharedConfig>
#include <KProtocolInfo>
#include <QInputDialog>
#include <KLocalizedString>
#include <QTemporaryFile>
#include <KUriFilter>
#include <Sonnet/Dialog>
#include <sonnet/backgroundchecker.h>
#include <KIO/JobUiDelegate>
#include <KIO/OpenUrlJob>
#include <KParts/BrowserRun>

#include <QBuffer>
#include <QVariant>
#include <QClipboard>
#include <QApplication>
#include <QAction>
#include <QPrinter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QWebEngineHistory>
#include <QMimeData>
#include <QPrinterInfo>
#define QL1S(x)     QLatin1String(x)
#define QL1C(x)     QLatin1Char(x)

// A functor that calls a member function
template<typename Arg, typename R, typename C>
struct InvokeWrapper {
    R *receiver;
    void (C::*memberFun)(Arg);
    void operator()(Arg result)
    {
        (receiver->*memberFun)(result);
    }
};

template<typename Arg, typename R, typename C>
InvokeWrapper<Arg, R, C> invoke(R *receiver, void (C::*memberFun)(Arg))
{
    return InvokeWrapper<Arg, R, C>{receiver, memberFun};
}

WebEngineBrowserExtension::WebEngineBrowserExtension(WebEnginePart *parent, const QByteArray& cachedHistoryData)
                       :KParts::BrowserExtension(parent),
                        m_part(parent),
                        mCurrentPrinter(nullptr)
{
    emit enableAction("cut", false);
    emit enableAction("copy", false);
    emit enableAction("paste", false);
    emit enableAction("print", true);

    if (cachedHistoryData.isEmpty()) {
        return;
    }

    QBuffer buffer;
    buffer.setData(cachedHistoryData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return;
    }

    // NOTE: When restoring history, webengine PORTING_TODO automatically navigates to
    // the previous "currentItem". Since we do not want that to happen,
    // we set a property on the WebEnginePage object that is used to allow or
    // disallow history navigation in WebEnginePage::acceptNavigationRequest.
    view()->page()->setProperty("HistoryNavigationLocked", true);
    QDataStream s (&buffer);
    s >> *(view()->history());
}

WebEngineBrowserExtension::~WebEngineBrowserExtension()
{
}

WebEngineView* WebEngineBrowserExtension::view()
{
    if (!m_view && m_part) {
        m_view = qobject_cast<WebEngineView*>(m_part->view());
    }

    return m_view;
}

int WebEngineBrowserExtension::xOffset()
{
    if (view()) {
        return view()->page()->scrollPosition().x();
    }

    return KParts::BrowserExtension::xOffset();
}

int WebEngineBrowserExtension::yOffset()
{
   if (view()) {
        return view()->page()->scrollPosition().y();
   }

    return KParts::BrowserExtension::yOffset();
}

void WebEngineBrowserExtension::saveState(QDataStream &stream)
{
    // TODO: Save information such as form data from the current page.
    QWebEngineHistory* history = (view() ? view()->history() : nullptr);
    const int historyIndex = (history ? history->currentItemIndex() : -1);
    const QUrl historyUrl = (history && historyIndex > -1) ? QUrl(history->currentItem().url()) : m_part->url();

    stream << historyUrl
           << static_cast<qint32>(xOffset())
           << static_cast<qint32>(yOffset())
           << historyIndex
           << m_historyData;
}

void WebEngineBrowserExtension::restoreState(QDataStream &stream)
{
    QUrl u;
    QByteArray historyData;
    qint32 xOfs = -1, yOfs = -1, historyItemIndex = -1;
    stream >> u >> xOfs >> yOfs >> historyItemIndex >> historyData;

    QWebEngineHistory* history = (view() ? view()->page()->history() : nullptr);
    if (history) {
        bool success = false;
        if (history->count() == 0) {   // Handle restoration: crash recovery, tab close undo, session restore
            if (!historyData.isEmpty()) {
                historyData = qUncompress(historyData); // uncompress the history data...
                QBuffer buffer (&historyData);
                if (buffer.open(QIODevice::ReadOnly)) {
                    QDataStream stream (&buffer);
                    view()->page()->setProperty("HistoryNavigationLocked", true);
                    stream >> *history;
                    QWebEngineHistoryItem currentItem(history->currentItem());
                    if (currentItem.isValid()) {
                        if (currentItem.isValid() && (xOfs != -1 || yOfs != -1)) {
                            const QPoint scrollPos (xOfs, yOfs);
//                            currentItem.setUserData(scrollPos);
                        }
                        // NOTE 1: The following Konqueror specific workaround is necessary
                        // because Konqueror only preserves information for the last visited
                        // page. However, we save the entire history content in saveState and
                        // and hence need to eliminate all but the current item here.
                        // NOTE 2: This condition only applies when Konqueror is restored from
                        // abnormal termination ; a crash and/or a session restoration.
                        if (QCoreApplication::applicationName() == QLatin1String("konqueror")) {
                            history->clear();
                        }
                        //qCDebug(WEBENGINEPART_LOG) << "Restoring URL:" << currentItem.url();
                        m_part->setProperty("NoEmitOpenUrlNotification", true);
                        history->goToItem(currentItem);
                    }
                }
            }
            success = (history->count() > 0);
        } else {        // Handle navigation: back and forward button navigation.
            //qCDebug(WEBENGINEPART_LOG) << "history count:" << history->count() << "request index:" << historyItemIndex;
            if (history->count() > historyItemIndex && historyItemIndex > -1) {
                QWebEngineHistoryItem item (history->itemAt(historyItemIndex));
                //qCDebug(WEBENGINEPART_LOG) << "URL:" << u << "Item URL:" << item.url();
                if (u == item.url()) {
                    if (item.isValid() && (xOfs != -1 || yOfs != -1)) {
                        const QPoint scrollPos (xOfs, yOfs);
//                        item.setUserData(scrollPos);
                    }
                    m_part->setProperty("NoEmitOpenUrlNotification", true);
                    history->goToItem(item);
                    success = true;
                }
            }
        }

        if (success) {
            return;
        }
    }

    // As a last resort, in case the history restoration logic above fails,
    // attempt to open the requested URL directly.
    qCDebug(WEBENGINEPART_LOG) << "Normal history navigation logic failed! Falling back to opening url directly.";
    m_part->openUrl(u);
}


void WebEngineBrowserExtension::cut()
{
    if (view())
        view()->triggerPageAction(QWebEnginePage::Cut);
}

void WebEngineBrowserExtension::copy()
{
    if (view())
        view()->triggerPageAction(QWebEnginePage::Copy);
}

void WebEngineBrowserExtension::paste()
{
    if (view())
        view()->triggerPageAction(QWebEnginePage::Paste);
}

void WebEngineBrowserExtension::slotSaveDocument()
{
    if (view())
        emit saveUrl(view()->url());
}

void WebEngineBrowserExtension::slotSaveFrame()
{
    if (view())
        emit saveUrl(view()->page()->url()); // TODO lol
}

void WebEngineBrowserExtension::print()
{
    if (view()) {
        mCurrentPrinter = new QPrinter();
        QPointer<QPrintDialog> dialog = new QPrintDialog(mCurrentPrinter, nullptr);
        dialog->setWindowTitle(i18n("Print Document"));
        if (dialog->exec() != QDialog::Accepted) {
            slotHandlePagePrinted(false);
            delete dialog;
            return;
        }
        delete dialog;
        view()->page()->print(mCurrentPrinter, invoke(this, &WebEngineBrowserExtension::slotHandlePagePrinted));
    }
}

void WebEngineBrowserExtension::slotHandlePagePrinted(bool result)
{
    Q_UNUSED(result);
    delete mCurrentPrinter;
    mCurrentPrinter = nullptr;
}


void WebEngineBrowserExtension::updateEditActions()
{
    if (!view())
        return;

    emit enableAction("cut", view()->pageAction(QWebEnginePage::Cut)->isEnabled());
    emit enableAction("copy", view()->pageAction(QWebEnginePage::Copy)->isEnabled());
    emit enableAction("paste", view()->pageAction(QWebEnginePage::Paste)->isEnabled());
}

void WebEngineBrowserExtension::updateActions()
{
    const QString protocol (m_part->url().scheme());
    const bool isValidDocument = (protocol != QL1S("about") && protocol != QL1S("error"));
    emit enableAction("print", isValidDocument);
}

void WebEngineBrowserExtension::searchProvider()
{
    if (!view())
        return;

    QAction *action = qobject_cast<QAction*>(sender());
    if (!action)
        return;

    QUrl url = action->data().toUrl();

    if (url.host().isEmpty()) {
        KUriFilterData data;
        data.setData(action->data().toString());
        if (KUriFilter::self()->filterSearchUri(data, KUriFilter::WebShortcutFilter))
            url = data.uri();
    }

    if (!url.isValid())
      return;

    KParts::BrowserArguments bargs;
    bargs.frameName = QL1S("_blank");
    emit openUrlRequest(url, KParts::OpenUrlArguments(), bargs);
}

void WebEngineBrowserExtension::reparseConfiguration()
{
    // Force the configuration stuff to reparse...
    WebEngineSettings::self()->init();
}

void WebEngineBrowserExtension::disableScrolling()
{
    QWebEngineView* currentView = view();
    QWebEnginePage* page = currentView ? currentView->page() : nullptr;

    if (!page)
        return;

    page->runJavaScript(QStringLiteral("document.documentElement.style.overflow = 'hidden';"));
}

void WebEngineBrowserExtension::zoomIn()
{
    if (view())
        view()->setZoomFactor(view()->zoomFactor() + 0.1);
}

void WebEngineBrowserExtension::zoomOut()
{
    if (view())
        view()->setZoomFactor(view()->zoomFactor() - 0.1);
}

void WebEngineBrowserExtension::zoomNormal()
{
    if (view()) {
        if (WebEngineSettings::self()->zoomToDPI())
            view()->setZoomFactor(view()->logicalDpiY() / 96.0f);
        else
            view()->setZoomFactor(1);
    }
}

void WebEngineBrowserExtension::toogleZoomTextOnly()
{
    if (!view())
        return;

    KConfigGroup cgHtml(KSharedConfig::openConfig(), "HTML Settings");
    bool zoomTextOnly = cgHtml.readEntry( "ZoomTextOnly", false );
    cgHtml.writeEntry("ZoomTextOnly", !zoomTextOnly);
    cgHtml.sync();

    // view()->settings()->setAttribute(QWebEngineSettings::ZoomTextOnly, !zoomTextOnly);
}

void WebEngineBrowserExtension::toogleZoomToDPI()
{
    if (!view())
        return;

    bool zoomToDPI = !WebEngineSettings::self()->zoomToDPI();
    WebEngineSettings::self()->setZoomToDPI(zoomToDPI);
    
    if (zoomToDPI)
        view()->setZoomFactor(view()->zoomFactor() * view()->logicalDpiY() / 96.0f);
    else 
        view()->setZoomFactor(view()->zoomFactor() * 96.0f / view()->logicalDpiY());
    
    // Recompute default font-sizes since they are only DPI dependent when zoomToDPI is false.
    WebEngineSettings::self()->computeFontSizes(view()->logicalDpiY());
}

void WebEngineBrowserExtension::slotSelectAll()
{
    if (view())
        view()->triggerPageAction(QWebEnginePage::SelectAll);
}

void WebEngineBrowserExtension::slotSaveImageAs()
{
    if (view())
        view()->triggerPageAction(QWebEnginePage::DownloadImageToDisk);
}

void WebEngineBrowserExtension::slotSendImage()
{
    if (!view()) {
        return;
    }

    QStringList urls;
    urls.append(view()->contextMenuResult().mediaUrl().path());
    const QString subject = view()->contextMenuResult().mediaUrl().path();
    KToolInvocation::invokeMailer(QString(), QString(), QString(), subject,
                                  QString(), //body
                                  QString(),
                                  urls); // attachments
}

void WebEngineBrowserExtension::slotCopyImageURL()
{
    if (!view()) {
        return;
    }

    QUrl safeURL = view()->contextMenuResult().mediaUrl();
    safeURL.setPassword(QString());
    // Set it in both the mouse selection and in the clipboard
    QMimeData* mimeData = new QMimeData;
//TODO: Porting: test
    QList<QUrl> safeURLList;
    safeURLList.append(safeURL);
    mimeData->setUrls(safeURLList);
    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);

    mimeData = new QMimeData;
    mimeData->setUrls(safeURLList);
    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Selection);
}


void WebEngineBrowserExtension::slotCopyImage()
{
    if (!view()) {
        return;
    }

    QUrl safeURL; //(view()->contextMenuResult().imageUrl());
    safeURL.setPassword(QString());

    // Set it in both the mouse selection and in the clipboard
    QMimeData* mimeData = new QMimeData;
//    mimeData->setImageData(view()->contextMenuResult().pixmap());
//TODO: Porting: test
    QList<QUrl> safeURLList;
    safeURLList.append(safeURL);
    mimeData->setUrls(safeURLList);
    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);

    mimeData = new QMimeData;
//    mimeData->setImageData(view()->contextMenuResult().pixmap());
    mimeData->setUrls(safeURLList);
    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Selection);
}

void WebEngineBrowserExtension::slotViewImage()
{
    if (view()) {
        emit createNewWindow(view()->contextMenuResult().mediaUrl());
    }
}

void WebEngineBrowserExtension::slotBlockImage()
{
    if (!view()) {
        return;
    }

    bool ok = false;
    const QString url = QInputDialog::getText(view(), i18n("Add URL to Filter"),
                                              i18n("Enter the URL:"), QLineEdit::Normal,
                                              view()->contextMenuResult().mediaUrl().toString(),
                                              &ok);
    if (ok) {
        WebEngineSettings::self()->addAdFilter(url);
        reparseConfiguration();
    }
}

void WebEngineBrowserExtension::slotBlockHost()
{
    if (!view())
        return;

    QUrl url; // (view()->contextMenuResult().imageUrl());
    url.setPath(QL1S("/*"));
    WebEngineSettings::self()->addAdFilter(url.toString(QUrl::RemoveUserInfo | QUrl::RemovePort));
    reparseConfiguration();
}

void WebEngineBrowserExtension::slotCopyLinkURL()
{
    if (view())
        view()->triggerPageAction(QWebEnginePage::CopyLinkToClipboard);
}

void WebEngineBrowserExtension::slotCopyLinkText()
{
    if (view()) {
        QMimeData* data = new QMimeData;
        data->setText(view()->contextMenuResult().linkText());
        QApplication::clipboard()->setMimeData(data, QClipboard::Clipboard);
    }
}

void WebEngineBrowserExtension::slotCopyEmailAddress()
{
    if (view()) {
        QMimeData* data = new QMimeData;
        const QUrl url(view()->contextMenuResult().linkUrl());
        data->setText(url.path());
        QApplication::clipboard()->setMimeData(data, QClipboard::Clipboard);
    }
}

void WebEngineBrowserExtension::slotSaveLinkAs(const QUrl &url)
{
    if (view()) {
        if (!url.isEmpty()) {
            KParts::BrowserRun::saveUrl(url, url.path(), view(), KParts::OpenUrlArguments());
        } else {
            view()->triggerPageAction(QWebEnginePage::DownloadLinkToDisk);
        }
    }
}

void WebEngineBrowserExtension::slotViewDocumentSource()
{
    if (!view())
        return;

    const QUrl pageUrl (view()->url());
    if (pageUrl.isLocalFile()) {
        KIO::OpenUrlJob *job = new KIO::OpenUrlJob(pageUrl, QL1S("text/plain"));
        job->setUiDelegate(new KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, view()));
        job->start();
    } else {
        view()->page()->toHtml([this](const QString& html) {
            QTemporaryFile tempFile;
            tempFile.setFileTemplate(tempFile.fileTemplate() + QL1S(".html"));
            tempFile.setAutoRemove(false);
            if (tempFile.open()) {
                tempFile.write(html.toUtf8());
                tempFile.close();
                KIO::OpenUrlJob *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(tempFile.fileName()), QL1S("text/plain"));
                job->setUiDelegate(new KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, view()));
                job->setDeleteTemporaryFile(true);
                job->start();
            }
        });
    }
}

static bool isMultimediaElement(QWebEngineContextMenuData::MediaType mediaType)
{
    switch(mediaType)
    {
        case QWebEngineContextMenuData::MediaTypeVideo:
        case QWebEngineContextMenuData::MediaTypeAudio:
            return true;
        default:
            return false;
    }
}

void WebEngineBrowserExtension::slotLoopMedia()
{
    if (!view()) {
        return;
    }

    QWebEngineContextMenuData data =  view()->contextMenuResult();
    if (!isMultimediaElement( data.mediaType()))
        return;
    view()->page()->triggerAction(QWebEnginePage::ToggleMediaLoop);
}

void WebEngineBrowserExtension::slotMuteMedia()
{
    if (!view()) {
        return;
    }

    QWebEngineContextMenuData data =  view()->contextMenuResult();
    if (!isMultimediaElement( data.mediaType()))
        return;
    view()->page()->triggerAction(QWebEnginePage::ToggleMediaMute);
}

void WebEngineBrowserExtension::slotPlayMedia()
{
    if (!view()) {
        return;
    }

    QWebEngineContextMenuData data =  view()->contextMenuResult();
    if (!isMultimediaElement( data.mediaType()))
        return;
    view()->page()->triggerAction(QWebEnginePage::ToggleMediaPlayPause);
}

void WebEngineBrowserExtension::slotShowMediaControls()
{
    if (!view()) {
        return;
    }

    QWebEngineContextMenuData data =  view()->contextMenuResult();
    if (!isMultimediaElement( data.mediaType()))
        return;
    view()->page()->triggerAction(QWebEnginePage::ToggleMediaControls);
}

#if 0
static QUrl mediaUrlFrom(QWebElement& element)
{
    QWebFrame* frame = element.webFrame();
    QString src = frame ? element.attribute(QL1S("src")) : QString();
    if (src.isEmpty())
        src = frame ? element.evaluateJavaScript(QL1S("this.src")).toString() : QString();

    if (src.isEmpty())
        return QUrl();

    return QUrl(frame->baseUrl().resolved(QUrl::fromEncoded(QUrl::toPercentEncoding(src), QUrl::StrictMode)));
}
#endif

void WebEngineBrowserExtension::slotSaveMedia()
{
    if (!view()) {
        return;
    }

    QWebEngineContextMenuData data =  view()->contextMenuResult();
    if (!isMultimediaElement( data.mediaType()))
        return;
    emit saveUrl(data.mediaUrl());
}

void WebEngineBrowserExtension::slotCopyMedia()
{
    if (!view()) {
        return;
    }
    QWebEngineContextMenuData data =  view()->contextMenuResult();
    if (!isMultimediaElement( data.mediaType()))
        return;

    QUrl safeURL(data.mediaUrl());
    if (!safeURL.isValid())
        return;

    safeURL.setPassword(QString());
    // Set it in both the mouse selection and in the clipboard
    QMimeData* mimeData = new QMimeData;
//TODO: Porting: test
    QList<QUrl> safeURLList;
    safeURLList.append(safeURL);
    mimeData->setUrls(safeURLList);
    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);

    mimeData = new QMimeData;
    mimeData->setUrls(safeURLList);
    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Selection);
}

void WebEngineBrowserExtension::slotTextDirectionChanged()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        bool ok = false;
        const int value = action->data().toInt(&ok);
        if (ok) {
            view()->triggerPageAction(static_cast<QWebEnginePage::WebAction>(value));
        }
    }
}

void WebEngineBrowserExtension::slotCheckSpelling()
{
    view()->page()->runJavaScript(QL1S("this.value"), [this](const QVariant &value) {
        const QString text = value.toString();
        if (!text.isEmpty()) {
            m_spellTextSelectionStart = 0;
            m_spellTextSelectionEnd = 0;

            Sonnet::BackgroundChecker *backgroundSpellCheck = new Sonnet::BackgroundChecker;
            Sonnet::Dialog* spellDialog = new Sonnet::Dialog(backgroundSpellCheck, view());
            backgroundSpellCheck->setParent(spellDialog);
            spellDialog->setAttribute(Qt::WA_DeleteOnClose, true);
            spellDialog->showSpellCheckCompletionMessage(true);
            connect(spellDialog, &Sonnet::Dialog::replace, this, &WebEngineBrowserExtension::spellCheckerCorrected);
            connect(spellDialog, &Sonnet::Dialog::misspelling, this, &WebEngineBrowserExtension::spellCheckerMisspelling);
            spellDialog->setBuffer(text);
            spellDialog->show();
        }
    });
}

void WebEngineBrowserExtension::slotSpellCheckSelection()
{
    view()->page()->runJavaScript(QL1S("this.value"), [this](const QVariant &value) {
        const QString text = value.toString();
        if (!text.isEmpty()) {
            view()->page()->runJavaScript(QL1S("this.selectionStart + ' ' + this.selectionEnd"), [this, text](const QVariant &value) {
                const QString values = value.toString();
                const int pos = values.indexOf(' ');
                m_spellTextSelectionStart = qMax(0, values.leftRef(pos).toInt());
                m_spellTextSelectionEnd = qMax(0, values.midRef(pos + 1).toInt());
                // qCDebug(WEBENGINEPART_LOG) << "selection start:" << m_spellTextSelectionStart << "end:" << m_spellTextSelectionEnd;

                Sonnet::BackgroundChecker *backgroundSpellCheck = new Sonnet::BackgroundChecker;
                Sonnet::Dialog* spellDialog = new Sonnet::Dialog(backgroundSpellCheck, view());
                backgroundSpellCheck->setParent(spellDialog);
                spellDialog->setAttribute(Qt::WA_DeleteOnClose, true);
                spellDialog->showSpellCheckCompletionMessage(true);
                connect(spellDialog, &Sonnet::Dialog::replace, this, &WebEngineBrowserExtension::spellCheckerCorrected);
                connect(spellDialog, &Sonnet::Dialog::misspelling, this, &WebEngineBrowserExtension::spellCheckerMisspelling);
                connect(spellDialog, &Sonnet::Dialog::spellCheckDone, this, &WebEngineBrowserExtension::slotSpellCheckDone);
                spellDialog->setBuffer(text.mid(m_spellTextSelectionStart, (m_spellTextSelectionEnd - m_spellTextSelectionStart)));
                spellDialog->show();
            });
        }
    });
}

void WebEngineBrowserExtension::spellCheckerCorrected(const QString& original, int pos, const QString& replacement)
{
    // Adjust the selection end...
    if (m_spellTextSelectionEnd > 0) {
        m_spellTextSelectionEnd += qMax (0, (replacement.length() - original.length()));
    }

    const int index = pos + m_spellTextSelectionStart;
    QString script(QL1S("this.value=this.value.substring(0,"));
    script += QString::number(index);
    script += QL1S(") + \"");
    script +=  replacement;
    script += QL1S("\" + this.value.substring(");
    script += QString::number(index + original.length());
    script += QL1S(")");

    //qCDebug(WEBENGINEPART_LOG) << "**** script:" << script;
    view()->page()->runJavaScript(script);
}

void WebEngineBrowserExtension::spellCheckerMisspelling(const QString& text, int pos)
{
    // qCDebug(WEBENGINEPART_LOG) << text << pos;
    QString selectionScript (QL1S("this.setSelectionRange("));
    selectionScript += QString::number(pos + m_spellTextSelectionStart);
    selectionScript += QL1C(',');
    selectionScript += QString::number(pos + text.length() + m_spellTextSelectionStart);
    selectionScript += QL1C(')');
    view()->page()->runJavaScript(selectionScript);
}

void WebEngineBrowserExtension::slotSpellCheckDone(const QString&)
{
    // Restore the text selection if one was present before we started the
    // spell check.
    if (m_spellTextSelectionStart > 0 || m_spellTextSelectionEnd > 0) {
        QString script (QL1S("; this.setSelectionRange("));
        script += QString::number(m_spellTextSelectionStart);
        script += QL1C(',');
        script += QString::number(m_spellTextSelectionEnd);
        script += QL1C(')');
        view()->page()->runJavaScript(script);
    }
}

void WebEngineBrowserExtension::saveHistory()
{
    QWebEngineHistory* history = (view() ? view()->history() : nullptr);

    if (history && history->count() > 0) {
        //qCDebug(WEBENGINEPART_LOG) << "Current history: index=" << history->currentItemIndex() << "url=" << history->currentItem().url();
        QByteArray histData;
        QBuffer buff (&histData);
        m_historyData.clear();
        if (buff.open(QIODevice::WriteOnly)) {
            QDataStream stream (&buff);
            stream << *history;
            m_historyData = qCompress(histData, 9);
        }
        QWidget* mainWidget = m_part ? m_part->widget() : nullptr;
        QWidget* frameWidget = mainWidget ? mainWidget->parentWidget() : nullptr;
        if (frameWidget) {
            emit saveHistory(frameWidget, m_historyData);
            // qCDebug(WEBENGINEPART_LOG) << "# of items:" << history->count() << "current item:" << history->currentItemIndex() << "url:" << history->currentItem().url();
        }
    } else {
        Q_ASSERT(false); // should never happen!!!
    }
}

void WebEngineBrowserExtension::slotPrintPreview()
{
    QPrinter printer;
    QPrintPreviewDialog dlg(&printer, view());
    auto printPreview = [this](QPrinter *p){
        QEventLoop loop;
        auto preview = [&](bool) {loop.quit();};
        m_view->page()->print(p, preview);
        loop.exec();
    };
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, printPreview);
    dlg.exec();
}

void WebEngineBrowserExtension::slotOpenSelection()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if (action) {
        KParts::BrowserArguments browserArgs;
        browserArgs.frameName = QStringLiteral("_blank");
        emit openUrlRequest(QUrl(action->data().toUrl()), KParts::OpenUrlArguments(), browserArgs);
    }
}

void WebEngineBrowserExtension::slotLinkInTop()
{
    if (!view()) {
        return;
    }

    KParts::OpenUrlArguments uargs;
    uargs.setActionRequestedByUser(true);

    KParts::BrowserArguments bargs;
    bargs.frameName = QL1S("_top");

    const QUrl url(view()->contextMenuResult().linkUrl());

    emit openUrlRequest(url, uargs, bargs);
}

////

WebEngineTextExtension::WebEngineTextExtension(WebEnginePart* part)
    : KParts::TextExtension(part)
{
    connect(part->view(), &QWebEngineView::selectionChanged, this, &KParts::TextExtension::selectionChanged);
}

WebEnginePart* WebEngineTextExtension::part() const
{
    return static_cast<WebEnginePart*>(parent());
}

bool WebEngineTextExtension::hasSelection() const
{
    return part()->view()->hasSelection();
}

QString WebEngineTextExtension::selectedText(Format format) const
{
    switch(format) {
    case PlainText:
        return part()->view()->selectedText();
    case HTML:
        // PORTING_TODO selectedText might not be html
        return part()->view()->selectedText();
    }
    return QString();
}

QString WebEngineTextExtension::completeText(Format format) const
{
    // TODO David will hunt me down with a rusty spork if he sees this
    QEventLoop ev;
    QString str;
    switch(format) {
    case PlainText:
        part()->view()->page()->toPlainText([&ev,&str](const QString& data) {
            str = data;
            ev.quit();
        });
        break;
    case HTML:
        part()->view()->page()->toHtml([&ev,&str](const QString& data) {
            str = data;
            ev.quit();
        });
        break;
    }
    ev.exec();
    return str;
}

////

WebEngineHtmlExtension::WebEngineHtmlExtension(WebEnginePart* part)
    : KParts::HtmlExtension(part)
{
}


QUrl WebEngineHtmlExtension::baseUrl() const
{
    return part()->view()->page()->url();
}

bool WebEngineHtmlExtension::hasSelection() const
{
    return part()->view()->hasSelection();
}

KParts::SelectorInterface::QueryMethods WebEngineHtmlExtension::supportedQueryMethods() const
{
    return (KParts::SelectorInterface::EntireContent
            | KParts::SelectorInterface::SelectedContent);
}

#if 0
static KParts::SelectorInterface::Element convertWebElement(const QWebElement& webElem)
{
    KParts::SelectorInterface::Element element;
    element.setTagName(webElem.tagName());
    Q_FOREACH(const QString &attr, webElem.attributeNames()) {
        element.setAttribute(attr, webElem.attribute(attr));
    }
    return element;
}

static QString queryOne(const QString& query)
{
   QString jsQuery = QL1S("(function(query) { var element; var selectedElement = window.getSelection().getRangeAt(0).cloneContents().querySelector(\"");
   jsQuery += query;
   jsQuery += QL1S("\"); if (selectedElement && selectedElement.length > 0) { element = new Object; "
                   "element.tagName = String(selectedElements[0].tagName); element.href = String(selectedElements[0].href); } "
                   "return element; }())");
   return jsQuery;
}

static QString queryAll(const QString& query)
{
   QString jsQuery = QL1S("(function(query) { var elements = []; var selectedElements = window.getSelection().getRangeAt(0).cloneContents().querySelectorAll(\"");
   jsQuery += query;
   jsQuery += QL1S("\"); var numSelectedElements = (selectedElements ? selectedElements.length : 0);"
                   "for (var i = 0; i < numSelectedElements; ++i) { var element = new Object; "
                   "element.tagName = String(selectedElements[i].tagName); element.href = String(selectedElements[i].href);"
                   "elements.push(element); } return elements; } ())");
   return jsQuery;
}

static KParts::SelectorInterface::Element convertSelectionElement(const QVariant& variant)
{
    KParts::SelectorInterface::Element element;
    if (!variant.isNull() && variant.type() == QVariant::Map) {
        const QVariantMap elementMap (variant.toMap());
        element.setTagName(elementMap.value(QL1S("tagName")).toString());
        element.setAttribute(QL1S("href"), elementMap.value(QL1S("href")).toString());
    }
    return element;
}

static QList<KParts::SelectorInterface::Element> convertSelectionElements(const QVariant& variant)
{
    QList<KParts::SelectorInterface::Element> elements;
    const QVariantList resultList (variant.toList());
    Q_FOREACH(const QVariant& result, resultList) {
        const QVariantMap elementMap = result.toMap();
        KParts::SelectorInterface::Element element;
        element.setTagName(elementMap.value(QL1S("tagName")).toString());
        element.setAttribute(QL1S("href"), elementMap.value(QL1S("href")).toString());
        elements.append(element);
    }
    return elements;
}
#endif

KParts::SelectorInterface::Element WebEngineHtmlExtension::querySelector(const QString& query, KParts::SelectorInterface::QueryMethod method) const
{
    KParts::SelectorInterface::Element element;

    // If the specified method is None, return an empty list...
    if (method == KParts::SelectorInterface::None)
        return element;

    // If the specified method is not supported, return an empty list...
    if (!(supportedQueryMethods() & method))
        return element;

#if 0
    switch (method) {
    case KParts::SelectorInterface::EntireContent: {
        const QWebFrame* webFrame = part()->view()->page()->mainFrame();
        element = convertWebElement(webFrame->findFirstElement(query));
        break;
    }
    case KParts::SelectorInterface::SelectedContent: {
        QWebFrame* webFrame = part()->view()->page()->mainFrame();
        element = convertSelectionElement(webFrame->evaluateJavaScript(queryOne(query)));
        break;
    }
    default:
        break;
    }
#endif

    return element;
}

QList<KParts::SelectorInterface::Element> WebEngineHtmlExtension::querySelectorAll(const QString& query, KParts::SelectorInterface::QueryMethod method) const
{
    QList<KParts::SelectorInterface::Element> elements;

    // If the specified method is None, return an empty list...
    if (method == KParts::SelectorInterface::None)
        return elements;

    // If the specified method is not supported, return an empty list...
    if (!(supportedQueryMethods() & method))
        return elements;
#if 0
    switch (method) {
    case KParts::SelectorInterface::EntireContent: {
        const QWebFrame* webFrame = part()->view()->page()->mainFrame();
        const QWebElementCollection collection = webFrame->findAllElements(query);
        elements.reserve(collection.count());
        Q_FOREACH(const QWebElement& element, collection)
            elements.append(convertWebElement(element));
        break;
    }
    case KParts::SelectorInterface::SelectedContent: {
        QWebFrame* webFrame = part()->view()->page()->mainFrame();
        elements = convertSelectionElements(webFrame->evaluateJavaScript(queryAll(query)));
        break;
    }
    default:
        break;
    }
#endif
    return elements;
}

QVariant WebEngineHtmlExtension::htmlSettingsProperty(KParts::HtmlSettingsInterface::HtmlSettingsType type) const
{
    QWebEngineView* view = part() ? part()->view() : nullptr;
    QWebEnginePage* page = view ? view->page() : nullptr;
    QWebEngineSettings* settings = page ? page->settings() : nullptr;

    if (settings) {
        switch (type) {
        case KParts::HtmlSettingsInterface::AutoLoadImages:
            return settings->testAttribute(QWebEngineSettings::AutoLoadImages);
        case KParts::HtmlSettingsInterface::JavaEnabled:
            return false; // settings->testAttribute(QWebEngineSettings::JavaEnabled);
        case KParts::HtmlSettingsInterface::JavascriptEnabled:
            return settings->testAttribute(QWebEngineSettings::JavascriptEnabled);
        case KParts::HtmlSettingsInterface::PluginsEnabled:
            return settings->testAttribute(QWebEngineSettings::PluginsEnabled);
        case KParts::HtmlSettingsInterface::DnsPrefetchEnabled:
            return false; //settings->testAttribute(QWebEngineSettings::DnsPrefetchEnabled);
        case KParts::HtmlSettingsInterface::MetaRefreshEnabled:
            return view->pageAction(QWebEnginePage::Stop)->isEnabled();
        case KParts::HtmlSettingsInterface::LocalStorageEnabled:
            return settings->testAttribute(QWebEngineSettings::LocalStorageEnabled);
        case KParts::HtmlSettingsInterface::OfflineStorageDatabaseEnabled:
            return false; //settings->testAttribute(QWebEngineSettings::OfflineStorageDatabaseEnabled);
        case KParts::HtmlSettingsInterface::OfflineWebApplicationCacheEnabled:
            return false ;//settings->testAttribute(QWebEngineSettings::OfflineWebApplicationCacheEnabled);
        case KParts::HtmlSettingsInterface::PrivateBrowsingEnabled:
            return false; //settings->testAttribute(QWebEngineSettings::PrivateBrowsingEnabled);
        case KParts::HtmlSettingsInterface::UserDefinedStyleSheetURL:
            return false; //settings->userStyleSheetUrl();
        default:
            break;
        }
    }

    return QVariant();
}

bool WebEngineHtmlExtension::setHtmlSettingsProperty(KParts::HtmlSettingsInterface::HtmlSettingsType type, const QVariant& value)
{
    QWebEngineView* view = part() ? part()->view() : nullptr;
    QWebEnginePage* page = view ? view->page() : nullptr;
    QWebEngineSettings* settings = page ? page->settings() : nullptr;

    if (settings) {
        switch (type) {
        case KParts::HtmlSettingsInterface::AutoLoadImages:
            settings->setAttribute(QWebEngineSettings::AutoLoadImages, value.toBool());
            return true;
        case KParts::HtmlSettingsInterface::JavaEnabled:
            //settings->setAttribute(QWebESettings::JavaEnabled, value.toBool());
            return false;
        case KParts::HtmlSettingsInterface::JavascriptEnabled:
            settings->setAttribute(QWebEngineSettings::JavascriptEnabled, value.toBool());
            return true;
        case KParts::HtmlSettingsInterface::PluginsEnabled:
            settings->setAttribute(QWebEngineSettings::PluginsEnabled, value.toBool());
            return true;
        case KParts::HtmlSettingsInterface::DnsPrefetchEnabled:
//            settings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, value.toBool());
            return false;
        case KParts::HtmlSettingsInterface::MetaRefreshEnabled:
            view->triggerPageAction(QWebEnginePage::Stop);
            return true;
        case KParts::HtmlSettingsInterface::LocalStorageEnabled:
            settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, value.toBool());
            return false;
        case KParts::HtmlSettingsInterface::OfflineStorageDatabaseEnabled:
            //settings->setAttribute(QWebEngineSettings::OfflineStorageDatabaseEnabled, value.toBool());
            return false;
        case KParts::HtmlSettingsInterface::OfflineWebApplicationCacheEnabled:
            //settings->setAttribute(QWebEngineSettings::OfflineWebApplicationCacheEnabled, value.toBool());
            return false;
        case KParts::HtmlSettingsInterface::PrivateBrowsingEnabled:
            //settings->setAttribute(QWebEnngineSettings::PrivateBrowsingEnabled, value.toBool());
            return false;
        case KParts::HtmlSettingsInterface::UserDefinedStyleSheetURL:
            //qCDebug(WEBENGINEPART_LOG) << "Setting user style sheet for" << page << "to" << value.toUrl();
          //  settings->setUserStyleSheetUrl(value.toUrl());
            return false;
        default:
            break;
        }
    }

    return false;
}

WebEnginePart* WebEngineHtmlExtension::part() const
{
    return static_cast<WebEnginePart*>(parent());
}

WebEngineScriptableExtension::WebEngineScriptableExtension(WebEnginePart* part)
    : ScriptableExtension(part)
{
}

QVariant WebEngineScriptableExtension::rootObject()
{
    return QVariant::fromValue(KParts::ScriptableExtension::Object(this, reinterpret_cast<quint64>(this)));
}

bool WebEngineScriptableExtension::setException (KParts::ScriptableExtension* callerPrincipal, const QString& message)
{
    return KParts::ScriptableExtension::setException (callerPrincipal, message);
}

QVariant WebEngineScriptableExtension::get (KParts::ScriptableExtension* callerPrincipal, quint64 objId, const QString& propName)
{
    //qCDebug(WEBENGINEPART_LOG) << "caller:" << callerPrincipal << "id:" << objId << "propName:" << propName;
    return callerPrincipal->get (nullptr, objId, propName);
}

bool WebEngineScriptableExtension::put (KParts::ScriptableExtension* callerPrincipal, quint64 objId, const QString& propName, const QVariant& value)
{
    return KParts::ScriptableExtension::put (callerPrincipal, objId, propName, value);
}

static QVariant exception(const char* msg)
{
    qCWarning(WEBENGINEPART_LOG) << msg;
    return QVariant::fromValue(KParts::ScriptableExtension::Exception(QString::fromLatin1(msg)));
}

QVariant WebEngineScriptableExtension::evaluateScript (KParts::ScriptableExtension* callerPrincipal,
                                                     quint64 contextObjectId,
                                                     const QString& code,
                                                     KParts::ScriptableExtension::ScriptLanguage lang)
{
    Q_UNUSED(contextObjectId);
    Q_UNUSED(code)
    //qCDebug(WEBENGINEPART_LOG) << "principal:" << callerPrincipal << "id:" << contextObjectId << "language:" << lang << "code:" << code;

    if (lang != ECMAScript)
        return exception("unsupported language");


    KParts::ReadOnlyPart* part = callerPrincipal ? qobject_cast<KParts::ReadOnlyPart*>(callerPrincipal->parent()) : nullptr;
   // QWebFrame* frame = part ? qobject_cast<QWebFrame*>(part->parent()) : 0;
   // if (!frame)
        return exception("failed to resolve principal");
#if 0
    QVariant result (frame->evaluateJavaScript(code));

    if (result.type() == QVariant::Map) {
        const QVariantMap map (result.toMap());
        for (QVariantMap::const_iterator it = map.constBegin(), itEnd = map.constEnd(); it != itEnd; ++it) {
            callerPrincipal->put(callerPrincipal, 0, it.key(), it.value());
        }
    } else {
        const QString propName(code.contains(QLatin1String("__nsplugin")) ? QLatin1String("__nsplugin") : QString());
        callerPrincipal->put(callerPrincipal, 0, propName, result.toString());
    }

    return QVariant::fromValue(ScriptableExtension::Null());
#endif
}

bool WebEngineScriptableExtension::isScriptLanguageSupported (KParts::ScriptableExtension::ScriptLanguage lang) const
{
    return (lang == KParts::ScriptableExtension::ECMAScript);
}

QVariant WebEngineScriptableExtension::encloserForKid (KParts::ScriptableExtension* kid)
{
#if 0
    KParts::ReadOnlyPart* part = kid ? qobject_cast<KParts::ReadOnlyPart*>(kid->parent()) : 0;
    QWebFrame* frame = part ? qobject_cast<QWebFrame*>(part->parent()) : 0;
    if (frame) {
        return QVariant::fromValue(KParts::ScriptableExtension::Object(kid, reinterpret_cast<quint64>(kid)));
    }
#endif

    return QVariant::fromValue(ScriptableExtension::Null());
}

WebEnginePart* WebEngineScriptableExtension::part()
{
    return qobject_cast<WebEnginePart*>(parent());
}

