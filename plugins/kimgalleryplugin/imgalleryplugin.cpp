/* This file is part of the KDE project

Copyright (C) 2001, 2003 Lukas Tinkl <lukas@kde.org>
Andreas Schlapbach <schlpbch@iam.unibe.ch>

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

#include "imgalleryplugin.h"

#include <QTextStream>
#include <QFile>
#include <QDateTime>
#include <QPixmap>
#include <QImage>
#include <QTextCodec>
#include <QApplication>
#include <QDesktopServices>
#include <QImageReader>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPushButton>
#include <QProgressDialog>

#include <klocalizedstring.h>
#include <kmessagebox.h>
#include <kpluginfactory.h>
#include <kactioncollection.h>

#include <kparts/part.h>

#include "imgallerydialog.h"
#include "imgallery_debug.h"

K_PLUGIN_FACTORY(KImGalleryPluginFactory, registerPlugin<KImGalleryPlugin>();)

// Eliminate lots of deprecation warnings with Qt 5.15.
// Using a macro to redefine well known symbols is not good practice, but
// the alternative is lots of QT_VERSION_CHECK conditionals everywhere.
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#define endl Qt::endl
#endif

static QString directory(const QUrl &url) {
    return url.adjusted(QUrl::StripTrailingSlash).adjusted(QUrl::RemoveFilename).toLocalFile();
}

KImGalleryPlugin::KImGalleryPlugin(QObject *parent, const QVariantList &)
    : KParts::Plugin(parent), m_commentMap(nullptr)
{
    QAction *a = actionCollection()->addAction(QStringLiteral("create_img_gallery"));
    a->setText(i18n("&Create Image Gallery..."));
    a->setIcon(QIcon::fromTheme(QStringLiteral("imagegallery")));
    actionCollection()->setDefaultShortcut(a, QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(a, &QAction::triggered, this, &KImGalleryPlugin::slotExecute);
}

void KImGalleryPlugin::slotExecute()
{
    m_progressDlg = nullptr;
    if (!parent()) {
        KMessageBox::sorry(nullptr, i18n("Could not create the plugin, please report a bug."));
        return;
    }
    m_part = qobject_cast<KParts::ReadOnlyPart *>(parent());

    if (!m_part || !m_part->url().isLocalFile()) {  //TODO support remote URLs too?
        KMessageBox::sorry(m_part->widget(), i18n("Creating an image gallery works only on local folders."));
        return;
    }

    QString path = m_part->url().adjusted(QUrl::StripTrailingSlash).toLocalFile() + '/';
    m_configDlg = new KIGPDialog(m_part->widget(), path);

    if (m_configDlg->exec() == QDialog::Accepted) {
        qCDebug(IMAGEGALLERY_LOG) << "dialog is ok";
        m_configDlg->writeConfig();
        m_copyFiles = m_configDlg->copyOriginalFiles();
        m_recurseSubDirectories = m_configDlg->recurseSubDirectories();
        m_useCommentFile = m_configDlg->useCommentFile();
        m_imagesPerRow = m_configDlg->getImagesPerRow();

        QUrl url(m_configDlg->getImageUrl());
        if (!url.isEmpty() && url.isValid()) {
            m_progressDlg = new QProgressDialog(m_part->widget());
            connect(m_progressDlg, &QProgressDialog::canceled, this, &KImGalleryPlugin::slotCancelled);

            m_progressDlg->setLabelText(i18n("Creating thumbnails"));
            QPushButton *button = new QPushButton(m_progressDlg);
            KGuiItem::assign(button, KStandardGuiItem::cancel());
            m_progressDlg->setCancelButton(button);
            m_cancelled = false;
            m_progressDlg->show();
            if (createHtml(url, m_part->url().path(), m_configDlg->recursionLevel() > 0 ? m_configDlg->recursionLevel() + 1 : 0, m_configDlg->getImageFormat())) {
                QDesktopServices::openUrl(url);		// Open a browser to show the result
            } else {
                deleteCancelledGallery(url, m_part->url().path(), m_configDlg->recursionLevel() > 0 ? m_configDlg->recursionLevel() + 1 : 0, m_configDlg->getImageFormat());
            }
        }
    }
    delete m_progressDlg;
}

bool KImGalleryPlugin::createDirectory(const QDir &dir, const QString &imgGalleryDir, const QString &dirName)
{
    QDir thumb_dir(dir);

    if (!thumb_dir.exists()) {
        thumb_dir.setPath(imgGalleryDir);
        if (!(thumb_dir.mkdir(dirName/*, false*/))) {
            KMessageBox::sorry(m_part->widget(), i18n("Could not create folder: %1", thumb_dir.path()));
            return false;
        } else {
            thumb_dir.setPath(imgGalleryDir + '/' + dirName + '/');
            return true;
        }
    } else {
        return true;
    }
}

void KImGalleryPlugin::createHead(QTextStream &stream)
{
    const QString chsetName = QTextCodec::codecForLocale()->name();

    stream << "<?xml version=\"1.0\" encoding=\"" +  chsetName + "\" ?>" << endl;
    stream << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">" << endl;
    stream << "<html xmlns=\"http://www.w3.org/1999/xhtml\">" << endl;
    stream << "<head>" << endl;
    stream << "<title>" << m_configDlg->getTitle().toHtmlEscaped() << "</title>" << endl;
    stream << "<meta http-equiv=\"content-type\" content=\"text/html; charset=" << chsetName << "\"/>" << endl;
    stream << "<meta name=\"GENERATOR\" content=\"KDE Konqueror KImgallery plugin version " KDE_VERSION_STRING "\"/>" << endl;
    createCSSSection(stream);
    stream << "</head>" << endl;
}

void KImGalleryPlugin::createCSSSection(QTextStream &stream)
{
    const QString backgroundColor = m_configDlg->getBackgroundColor().name();
    const QString foregroundColor = m_configDlg->getForegroundColor().name();
    //adding a touch of style
    stream << "<style type='text/css'>\n";
    stream << "BODY {color: " << foregroundColor << "; background: " << backgroundColor << ";" << endl;
    stream << "          font-family: " << m_configDlg->getFontName() << ", sans-serif;" << endl;
    stream << "          font-size: " << m_configDlg->getFontSize() << "pt; margin: 8%; }" << endl;
    stream << "H1       {color: " << foregroundColor << ";}" << endl;
    stream << "TABLE    {text-align: center; margin-left: auto; margin-right: auto;}" << endl;
    stream << "TD       { color: " << foregroundColor << "; padding: 1em}" << endl;
    stream << "IMG      { border: 1px solid " << foregroundColor << "; }" << endl;
    stream << "</style>" << endl;
}

QString KImGalleryPlugin::extension(const QString &imageFormat)
{
    if (imageFormat == QLatin1String("PNG")) {
        return QStringLiteral(".png");
    }
    if (imageFormat == QLatin1String("JPEG")) {
        return QStringLiteral(".jpg");
    }
    Q_ASSERT(false);
    return QString();
}

void KImGalleryPlugin::createBody(QTextStream &stream, const QString &sourceDirName, const QStringList &subDirList,
                                  const QDir &imageDir, const QUrl &url, const QString &imageFormat)
{
    int numOfImages = imageDir.count();
    const QString imgGalleryDir = directory(url);
    const QString today(QLocale().toString(QDate::currentDate()));

    stream << "<body>\n<h1>" << m_configDlg->getTitle().toHtmlEscaped() << "</h1><p>" << endl;
    stream << i18n("<i>Number of images</i>: %1", numOfImages) << "<br/>" << endl;
    stream << i18n("<i>Created on</i>: %1", today) << "</p>" << endl;

    stream << "<hr/>" << endl;

    if (m_recurseSubDirectories && subDirList.count() > 2) { //subDirList.count() is always >= 2 because of the "." and ".." directories
        stream << i18n("<i>Subfolders</i>:") << "<br>" << endl;
        for (QStringList::ConstIterator it = subDirList.constBegin(); it != subDirList.constEnd(); it++) {
            if (*it == QLatin1String(".") || *it == QLatin1String("..")) {
                continue;    //disregard the "." and ".." directories
            }
            stream << "<a href=\"" << *it << "/" << url.fileName()
                   << "\">" << *it << "</a><br>" << endl;
        }
        stream << "<hr/>" << endl;
    }

    stream << "<table>" << endl;

    //table with images
    int imgIndex;
    QFileInfo imginfo;
    QPixmap  imgProp;
    for (imgIndex = 0; !m_cancelled && (imgIndex < numOfImages);) {
        stream << "<tr>" << endl;

        for (int col = 0; !m_cancelled && (col < m_imagesPerRow) && (imgIndex < numOfImages); col++) {
            const QString imgName = imageDir[imgIndex];

            if (m_copyFiles) {
                stream << "<td align='center'>\n<a href=\"images/" << imgName << "\">";
            } else {
                stream << "<td align='center'>\n<a href=\"" << imgName << "\">";
            }

            if (createThumb(imgName, sourceDirName, imgGalleryDir, imageFormat)) {
                const QString imgPath("thumbs/" + imgName + extension(imageFormat));
                stream << "<img src=\"" << imgPath << "\" width=\"" << m_imgWidth << "\" ";
                stream << "height=\"" << m_imgHeight << "\" alt=\"" << imgPath << "\"/>";
                m_progressDlg->setLabelText(i18n("Created thumbnail for: \n%1", imgName));
            } else {
                qCDebug(IMAGEGALLERY_LOG) << "Creating thumbnail for " << imgName << " failed";
                m_progressDlg->setLabelText(i18n("Creating thumbnail for: \n%1\n failed", imgName));
            }
            stream << "</a>" << endl;

            if (m_configDlg->printImageName()) {
                stream << "<div>" << imgName << "</div>" << endl;
            }

            if (m_configDlg->printImageProperty()) {
                imgProp.load(imageDir.absoluteFilePath(imgName));
                stream << "<div>" << imgProp.width() << " x " << imgProp.height() << "</div>" << endl;
            }

            if (m_configDlg->printImageSize()) {
                imginfo.setFile(imageDir, imgName);
                stream << "<div>(" << (imginfo.size() / 1024) << " " <<  i18n("KiB") << ")" << "</div>" << endl;
            }

            if (m_useCommentFile) {
                QString imgComment = (*m_commentMap)[imgName];
                stream << "<div>" << imgComment.toHtmlEscaped() << "</div>" << endl;
            }
            stream << "</td>" << endl;

            m_progressDlg->setMaximum(numOfImages);
            m_progressDlg->setValue(imgIndex);
            qApp->processEvents();
            imgIndex++;
        }
        stream << "</tr>" << endl;
    }
    //close the HTML
    stream << "</table>\n</body>\n</html>" << endl;
}

bool KImGalleryPlugin::createHtml(const QUrl &url, const QString &sourceDirName, int recursionLevel, const QString &imageFormat)
{
    if (m_cancelled) {
        return false;
    }

    if (!parent() || !parent()->inherits("DolphinPart")) {
        return false;
    }

    QStringList subDirList;
    if (m_recurseSubDirectories && (recursionLevel >= 0)) { //recursionLevel == 0 means endless
        QDir toplevel_dir = QDir(sourceDirName);
        toplevel_dir.setFilter(QDir::Dirs | QDir::Readable | QDir::Writable);
        subDirList = toplevel_dir.entryList();

        for (QStringList::ConstIterator it = subDirList.constBegin(); it != subDirList.constEnd() && !m_cancelled; it++) {
            const QString currentDir = *it;
            if (currentDir == QLatin1String(".") || currentDir == QLatin1String("..")) {
                continue;   //disregard the "." and ".." directories
            }
            QDir subDir = QDir(directory(url) + '/' + currentDir);
            if (!subDir.exists()) {
                subDir.setPath(directory(url));
                if (!(subDir.mkdir(currentDir/*, false*/))) {
                    KMessageBox::sorry(m_part->widget(), i18n("Could not create folder: %1", subDir.path()));
                    continue;
                } else {
                    subDir.setPath(directory(url) + '/' + currentDir);
                }
            }
            if (!createHtml(QUrl::fromLocalFile(subDir.path() + '/' + url.fileName()), sourceDirName + '/' + currentDir,
                            recursionLevel > 1 ? recursionLevel - 1 : 0, imageFormat)) {
                return false;
            }
        }
    }

    if (m_useCommentFile) {
        loadCommentFile();
    }

    qCDebug(IMAGEGALLERY_LOG) << "sourceDirName: " << sourceDirName;

    QMimeDatabase db;
    QStringList imageNameFilters;
    const QList<QByteArray> &mimeNames = QImageReader::supportedMimeTypes();
    for (const QByteArray &mimeName : mimeNames)
    {
        const QMimeType mimeType = db.mimeTypeForName(mimeName);
        imageNameFilters.append(mimeType.globPatterns());
    }

    QDir imageDir(sourceDirName, QString(),
                  QDir::Name | QDir::IgnoreCase, QDir::Files | QDir::Readable);
    imageDir.setNameFilters(imageNameFilters);

    const QString imgGalleryDir = directory(url);
    qCDebug(IMAGEGALLERY_LOG) << "imgGalleryDir: " << imgGalleryDir;

    // Create the "thumbs" subdirectory if necessary
    QDir thumb_dir(imgGalleryDir + QLatin1String("/thumbs/"));
    if (createDirectory(thumb_dir, imgGalleryDir, QStringLiteral("thumbs")) == false) {
        return false;
    }

    // Create the "images" subdirectory if necessary
    QDir images_dir(imgGalleryDir + QLatin1String("/images/"));
    if (m_copyFiles) {
        if (createDirectory(images_dir, imgGalleryDir, QStringLiteral("images")) == false) {
            return false;
        }
    }

    QFile file(url.path());
    qCDebug(IMAGEGALLERY_LOG) << "url.path(): " << url.path() << ", thumb_dir: " << thumb_dir.path()
                  << ", imageDir: " << imageDir.path();

    if (imageDir.exists() && file.open(QIODevice::WriteOnly)) {
        QTextStream stream(&file);
        stream.setCodec(QTextCodec::codecForLocale());

        createHead(stream);
        createBody(stream, sourceDirName, subDirList, imageDir, url, imageFormat); //ugly

        file.close();

        return !m_cancelled;

    } else {
        QString path = url.toLocalFile();
        if (!path.endsWith("/")) {
            path += '/';
        }
        KMessageBox::sorry(m_part->widget(), i18n("Could not open file: %1", path));
        return false;
    }
}

void KImGalleryPlugin::deleteCancelledGallery(const QUrl &url, const QString &sourceDirName, int recursionLevel, const QString &imageFormat)
{
    if (m_recurseSubDirectories && (recursionLevel >= 0)) {
        QStringList subDirList;
        QDir toplevel_dir = QDir(sourceDirName);
        toplevel_dir.setFilter(QDir::Dirs);
        subDirList = toplevel_dir.entryList();

        for (QStringList::ConstIterator it = subDirList.constBegin(); it != subDirList.constEnd(); it++) {
            if (*it == QLatin1String(".") || *it == QLatin1String("..") || *it == QLatin1String("thumbs") || (m_copyFiles && *it == QLatin1String("images"))) {
                continue; //disregard the "." and ".." directories
            }
            deleteCancelledGallery(QUrl::fromLocalFile(directory(url) + '/' + *it + '/' + url.fileName()),
                                   sourceDirName + '/' + *it,
                                   recursionLevel > 1 ? recursionLevel - 1 : 0, imageFormat);
        }
    }

    const QString imgGalleryDir = directory(url);
    QDir thumb_dir(imgGalleryDir + QLatin1String("/thumbs/"));
    QDir images_dir(imgGalleryDir + QLatin1String("/images/"));
    QDir imageDir(sourceDirName, QStringLiteral("*.png *.PNG *.gif *.GIF *.jpg *.JPG *.jpeg *.JPEG *.bmp *.BMP"),
                  QDir::Name | QDir::IgnoreCase, QDir::Files | QDir::Readable);
    QFile file(url.path());

    // Remove the image file ..
    file.remove();
    // ..all the thumbnails ..
    for (uint i = 0; i < imageDir.count(); i++) {
        const QString imgName = imageDir[i];
        const QString imgNameFormat = imgName + extension(imageFormat);
        bool isRemoved = thumb_dir.remove(imgNameFormat);
        qCDebug(IMAGEGALLERY_LOG) << "removing: " << thumb_dir.path() << "/" << imgNameFormat << "; " << isRemoved;
    }
    // ..and the thumb directory
    thumb_dir.rmdir(thumb_dir.path());

    // ..and the images directory if images were to be copied
    if (m_copyFiles) {
        for (uint i = 0; i < imageDir.count(); i++) {
            const QString imgName = imageDir[i];
            bool isRemoved = images_dir.remove(imgName);
            qCDebug(IMAGEGALLERY_LOG) << "removing: " << images_dir.path() << "/" << imgName << "; " << isRemoved;
        }
        images_dir.rmdir(images_dir.path());
    }
}

void KImGalleryPlugin::loadCommentFile()
{
    QFile file(m_configDlg->getCommentFile());
    if (file.open(QIODevice::ReadOnly)) {
        qCDebug(IMAGEGALLERY_LOG) << "File opened.";

        QTextStream *m_textStream = new QTextStream(&file);
        m_textStream->setCodec(QTextCodec::codecForLocale());

        delete m_commentMap;
        m_commentMap = new CommentMap;

        QString picName, picComment, curLine, curLineStripped;
        while (!m_textStream->atEnd()) {
            curLine = m_textStream->readLine();
            curLineStripped = curLine.trimmed();
            // Lines starting with '#' are comment
            if (!(curLineStripped.isEmpty()) && !curLineStripped.startsWith(QLatin1String("#"))) {
                if (curLineStripped.endsWith(QLatin1String(":"))) {
                    picComment.clear();
                    picName = curLineStripped.left(curLineStripped.length() - 1);
                    qCDebug(IMAGEGALLERY_LOG) << "picName: " << picName;
                } else {
                    do {
                        //qCDebug(IMAGEGALLERY_LOG) << "picComment";
                        picComment += curLine + '\n';
                        curLine = m_textStream->readLine();
                    } while (!m_textStream->atEnd() && !(curLine.trimmed().isEmpty()) &&
                             !curLine.trimmed().startsWith(QLatin1String("#")));
                    //qCDebug(IMAGEGALLERY_LOG) << "Pic comment: " << picComment;
                    m_commentMap->insert(picName, picComment);
                }
            }
        }
        CommentMap::ConstIterator it;
        for (it = m_commentMap->constBegin(); it != m_commentMap->constEnd(); ++it) {
            qCDebug(IMAGEGALLERY_LOG) << "picName: " << it.key() << ", picComment: " << it.value();
        }
        file.close();
        qCDebug(IMAGEGALLERY_LOG) << "File closed.";
        delete m_textStream;
    } else {
        KMessageBox::sorry(m_part->widget(), i18n("Could not open file: %1", m_configDlg->getCommentFile()));
        m_useCommentFile = false;
    }
}

bool KImGalleryPlugin::createThumb(const QString &imgName, const QString &sourceDirName,
                                   const QString &imgGalleryDir, const QString &imageFormat)
{
    QImage img;
    const QString pixPath = sourceDirName + QLatin1String("/") + imgName;

    if (m_copyFiles) {
        QFile::copy(pixPath, imgGalleryDir + QLatin1String("/images/") + imgName);
    }

    const QString imgNameFormat = imgName + extension(imageFormat);
    const QString thumbDir = imgGalleryDir + QLatin1String("/thumbs/");
    int extent = m_configDlg->getThumbnailSize();

    // this code is stolen from kdebase/kioslave/thumbnail/imagecreator.cpp
    // (c) 2000 gis and malte

    m_imgWidth = 120; // Setting the size of the images is
    m_imgHeight = 90; // required to generate faster 'loading' pages
    if (img.load(pixPath)) {
        int w = img.width(), h = img.height();
        // scale to pixie size
        // qCDebug(IMAGEGALLERY_LOG) << "w: " << w << " h: " << h;
        // Resizing if to big
        if (w > extent || h > extent) {
            if (w > h) {
                h = (int)((double)(h * extent) / w);
                if (h == 0) {
                    h = 1;
                }
                w = extent;
                Q_ASSERT(h <= extent);
            } else {
                w = (int)((double)(w * extent) / h);
                if (w == 0) {
                    w = 1;
                }
                h = extent;
                Q_ASSERT(w <= extent);
            }
            const QImage scaleImg(img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            if (scaleImg.width() != w || scaleImg.height() != h) {
                qCDebug(IMAGEGALLERY_LOG) << "Resizing failed. Aborting.";
                return false;
            }
            img = scaleImg;
            if (m_configDlg->colorDepthSet() == true) {
                QImage::Format format;
                switch (m_configDlg->getColorDepth()) {
                case 1:
                    format = QImage::Format_Mono;
                    break;
                case 8:
                    format = QImage::Format_Indexed8;
                    break;
                case 16:
                    format = QImage::Format_RGB16;
                    break;
                case 32:
                default:
                    format = QImage::Format_RGB32;
                    break;
                }

                const QImage depthImg(img.convertToFormat(format));
                img = depthImg;
            }
        }
        qCDebug(IMAGEGALLERY_LOG) << "Saving thumbnail to: " << thumbDir + imgNameFormat;
        if (!img.save(thumbDir + imgNameFormat, imageFormat.toLatin1())) {
            qCDebug(IMAGEGALLERY_LOG) << "Saving failed. Aborting.";
            return false;
        }
        m_imgWidth = w;
        m_imgHeight = h;
        return true;
    }
    return false;
}

void KImGalleryPlugin::slotCancelled()
{
    m_cancelled = true;
}

#include "imgalleryplugin.moc"
