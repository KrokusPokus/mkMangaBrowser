#include "ThumbnailCache.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPointer>
#include <QSaveFile>
#include <QTextStream>
#include <QThreadPool>

#include <cmath>

namespace {

const QString kMissingCover = QStringLiteral(":/img/Missing.png");
const QString kIndexFile    = QStringLiteral("_List.txt");
const QString kExt          = QStringLiteral("png");

QString CoverDate(const QFileInfo& fi)
{
    // FileGetTime in the original: local time, accurate to the second
    return fi.lastModified().isValid() ? fi.lastModified().toString(QStringLiteral("yyyyMMddHHmmss")) : QStringLiteral("0");
}

// "Grayscale, 50 % brightness, 50 % opacity" - the same color matrix as in the original:
// grey = 0.150 R + 0.294 G + 0.057 B, Alpha * 0.5
QImage MakeGrey(const QImage& src)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto* pLine = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = pLine[x];
            const int nGrey = qBound(0, qRound(0.150 * qRed(c) + 0.294 * qGreen(c) + 0.057 * qBlue(c)), 255);
            pLine[x] = qRgba(nGrey, nGrey, nGrey, qAlpha(c) / 2);
        }
    }
    return img;
}

bool SavePng(const QImage& img, const QString& sPath)
{
    QSaveFile f(sPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (!img.save(&f, "PNG"))
        return false;
    return f.commit();
}

} // namespace

CThumbnailCache::CThumbnailCache(const QString& sBaseDir, int nLogicalHeight, qreal dDevicePixelRatio, QObject* pParent)
    : QObject(pParent)
    , m_nLogicalHeight(nLogicalHeight)
    , m_dDpr(dDevicePixelRatio > 0 ? dDevicePixelRatio : 1.0)
{
    m_nPixelHeight = qRound(m_nLogicalHeight * m_dDpr);
    m_sDir = QDir::cleanPath(sBaseDir) + QStringLiteral("/ThumbsCache%1").arg(m_nPixelHeight);
    QDir().mkpath(m_sDir);
    LoadIndex();
}

CThumbnailCache::~CThumbnailCache()
{
    // Running background jobs must no longer access this object (QPointer in OnGenerated)
    SaveIndex();
}

QString CThumbnailCache::ThumbPath(const QString& sMarker, bool bGrey) const
{
    return m_sDir + QLatin1Char('/') + sMarker + (bGrey ? QStringLiteral(".grey.") : QStringLiteral(".")) + kExt;
}

void CThumbnailCache::LoadIndex()
{
    QFile f(m_sDir + QLatin1Char('/') + kIndexFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString sLine = ts.readLine();
        // <Marker>|<Overlay>|<Width>|<Size>|<Date> (the overlay field is always empty in the original)
        const QStringList a = sLine.split(QLatin1Char('|'));
        if (a.size() != 5 || a.at(0).isEmpty())
            continue;
        SIndexEntry e;
        e.nWidthPx = a.at(2).toInt();
        e.nCoverSize = a.at(3).toLongLong();
        e.sCoverDate = a.at(4).trimmed();
        m_mapIndexOld.insert(a.at(0), e);
    }
}

void CThumbnailCache::SaveIndex()
{
    if (!m_bIndexDirty && m_mapIndexNew.size() == m_mapIndexOld.size())
        return;

    QSaveFile f(m_sDir + QLatin1Char('/') + kIndexFile);
    if (!f.open(QIODevice::WriteOnly))
        return;
    QTextStream ts(&f);
    for (auto it = m_mapIndexNew.cbegin(); it != m_mapIndexNew.cend(); ++it)
        ts << it.key() << "||" << it->nWidthPx << '|' << it->nCoverSize << '|' << it->sCoverDate << '\n';
    ts.flush();
    if (f.commit()) {
        m_mapIndexOld = m_mapIndexNew;
        m_bIndexDirty = false;
    }
}

void CThumbnailCache::ClearMemory()
{
    m_mapPixmaps.clear();
}

int CThumbnailCache::Prepare(const QString& sMarker, const QString& sCoverPath)
{
    const QString sSource = sCoverPath.isEmpty() ? kMissingCover : sCoverPath;
    const QFileInfo fiSource(sSource);
    const qint64 nSize = fiSource.size();
    const QString sDate = CoverDate(fiSource);
    auto toLogical = [this](int nPx) { return std::max(1, static_cast<int>(std::lround(nPx / m_dDpr))); };

    // Already prepared in this session (e.g. book in several groups)?
    if (auto it = m_mapIndexNew.constFind(sMarker); it != m_mapIndexNew.cend()
        && it->nCoverSize == nSize && it->sCoverDate == sDate)
        return toLogical(it->nWidthPx);

    // Valid entry in the saved index?
    if (auto it = m_mapIndexOld.constFind(sMarker); it != m_mapIndexOld.cend()
        && it->nCoverSize == nSize && it->sCoverDate == sDate && it->nWidthPx > 0
        && QFileInfo::exists(ThumbPath(sMarker, false)) && QFileInfo::exists(ThumbPath(sMarker, true))) {
        m_mapIndexNew.insert(sMarker, *it);
        return toLogical(it->nWidthPx);
    }

    // Regenerate. The width is already known from the image header, so the layout does not have to wait.
    QImageReader reader(sSource);
    QSize sz = reader.size();
    QString sUsedSource = sSource;
    if (!sz.isValid() || sz.height() <= 0) {
        emit ThumbnailFailed(sMarker, sSource);
        sUsedSource = kMissingCover;
        sz = QImageReader(kMissingCover).size();
    }
    const int nWidthPx = std::max(1, (m_nPixelHeight * sz.width()) / sz.height());   // Target_W := (Target_H*Source_W)//Source_H

    m_mapIndexNew.insert(sMarker, { nWidthPx, nSize, sDate });
    m_mapPixmaps.remove(sMarker);
    m_mapPixmaps.remove(sMarker + QStringLiteral(".grey"));
    StartGeneration(sMarker, sUsedSource, nSize, sDate);
    return toLogical(nWidthPx);
}

void CThumbnailCache::StartGeneration(const QString& sMarker, const QString& sSourcePath, qint64 nSize, const QString& sDate)
{
    if (m_aPending.contains(sMarker))
        return;
    m_aPending.insert(sMarker);

    const QString sPath = ThumbPath(sMarker, false);
    const QString sPathGrey = ThumbPath(sMarker, true);
    const int nHeight = m_nPixelHeight;
    QPointer<CThumbnailCache> pSelf(this);

    QThreadPool::globalInstance()->start([=]() {
        QImageReader reader(sSourcePath);
        QImage img = reader.read();
        bool bOk = !img.isNull() && img.height() > 0;
        int nWidth = 0;
        if (bOk) {
            nWidth = std::max(1, (nHeight * img.width()) / img.height());
            const QImage thumb = img.convertToFormat(QImage::Format_ARGB32)
                                     .scaled(nWidth, nHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            bOk = SavePng(thumb, sPath) && SavePng(MakeGrey(thumb), sPathGrey);
        }
        // Back to the GUI thread; the cache may have been deleted in the meantime (QPointer)
        QMetaObject::invokeMethod(QCoreApplication::instance(), [=]() {
            if (pSelf)
                pSelf->OnGenerated(sMarker, nWidth, nSize, sDate, bOk, sSourcePath);
        }, Qt::QueuedConnection);
    });
}

void CThumbnailCache::OnGenerated(const QString& sMarker, int nWidthPx, qint64 nSize, const QString& sDate, bool bOk, const QString& sSourcePath)
{
    m_aPending.remove(sMarker);
    if (!bOk) {
        m_mapIndexNew.remove(sMarker);
        emit ThumbnailFailed(sMarker, sSourcePath);
        // Cover not readable -> use placeholder image (width as the placeholder image)
        if (sSourcePath != kMissingCover) {
            const QSize sz = QImageReader(kMissingCover).size();
            const int nWidth = std::max(1, (m_nPixelHeight * sz.width()) / std::max(1, sz.height()));
            m_mapIndexNew.insert(sMarker, { nWidth, nSize, sDate });
            StartGeneration(sMarker, kMissingCover, nSize, sDate);
        }
        return;
    }
    m_mapIndexNew.insert(sMarker, { nWidthPx, nSize, sDate });
    m_bIndexDirty = true;
    m_mapPixmaps.remove(sMarker);
    m_mapPixmaps.remove(sMarker + QStringLiteral(".grey"));
    emit ThumbnailReady(sMarker);
}

QPixmap CThumbnailCache::Pixmap(const QString& sMarker, bool bGrey)
{
    const QString sKey = bGrey ? sMarker + QStringLiteral(".grey") : sMarker;
    if (auto it = m_mapPixmaps.constFind(sKey); it != m_mapPixmaps.cend())
        return *it;
    if (m_aPending.contains(sMarker))
        return {};

    QPixmap pm;
    if (pm.load(ThumbPath(sMarker, bGrey))) {
        pm.setDevicePixelRatio(m_dDpr);
        m_mapPixmaps.insert(sKey, pm);
    }
    return pm;
}
