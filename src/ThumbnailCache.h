#ifndef THUMBNAILCACHE_H
#define THUMBNAILCACHE_H

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

// Persistent thumbnail cache as in the AHK script:
//  - two files per book in the cache folder: "<Marker>.png" and "<Marker>.grey.png" (darkened variant)
//  - index "_List.txt" with lines "<Marker>||<Width>|<CoverSize>|<CoverModificationTime>"
//  - a thumbnail is only regenerated if the size or modification time of the cover has changed
//    or the file is missing
// The cache folder is named "ThumbsCache<PixelHeight>" (original: "ThumbsCache<DPI>"), so that a different
// scaling gets its own sharp thumbnails.
// New thumbnails are generated in the background; ThumbnailReady() signals completion.
class CThumbnailCache : public QObject
{
    Q_OBJECT

public:
    CThumbnailCache(const QString& sBaseDir, int nLogicalHeight, qreal dDevicePixelRatio, QObject* pParent = nullptr);
    ~CThumbnailCache() override;

    int LogicalHeight() const { return m_nLogicalHeight; }
    qreal DevicePixelRatio() const { return m_dDpr; }

    // Width (logical pixels) of the thumbnail; generates it in the background if needed.
    // sCoverPath empty -> placeholder image "Missing".
    int Prepare(const QString& sMarker, const QString& sCoverPath);

    // Finished thumbnail, or a null pixmap while it is still being generated
    QPixmap Pixmap(const QString& sMarker, bool bGrey);

    // Write the index (only entries used in this session, as in the original)
    void SaveIndex();

    // Free memory (e.g. before reloading)
    void ClearMemory();

signals:
    void ThumbnailReady(const QString& sMarker);
    void ThumbnailFailed(const QString& sMarker, const QString& sCoverPath);

private:
    struct SIndexEntry {
        int     nWidthPx = 0;       // physical pixels
        qint64  nCoverSize = 0;
        QString sCoverDate;         // yyyyMMddHHmmss
    };

    QString ThumbPath(const QString& sMarker, bool bGrey) const;
    void LoadIndex();
    void StartGeneration(const QString& sMarker, const QString& sSourcePath, qint64 nSize, const QString& sDate);
    void OnGenerated(const QString& sMarker, int nWidthPx, qint64 nSize, const QString& sDate, bool bOk, const QString& sSourcePath);

    QString                     m_sDir;
    int                         m_nLogicalHeight;
    qreal                       m_dDpr;
    int                         m_nPixelHeight;

    QHash<QString, SIndexEntry> m_mapIndexOld;      // from disk
    QHash<QString, SIndexEntry> m_mapIndexNew;      // used in this session
    QSet<QString>               m_aPending;         // currently being generated
    QHash<QString, QPixmap>     m_mapPixmaps;       // key: marker, or marker + ".grey"
    bool                        m_bIndexDirty = false;
};

#endif // THUMBNAILCACHE_H
