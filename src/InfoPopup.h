#ifndef INFOPOPUP_H
#define INFOPOPUP_H

#include "MangaLibrary.h"

#include <QFrame>

class QGridLayout;

// Info window shown when hovering over a thumbnail
class CInfoPopup : public QFrame
{
    Q_OBJECT

public:
    explicit CInfoPopup(QWidget* pParent);

    // meta: freshly read meta file (book provides fallbacks and the page count)
    void SetInfo(const SBook& book, const SBookMeta& meta);

    // Positioning as in WM_MouseHover(): preferably below (mouse in the lower half) or above
    // (upper half) the thumbnail, otherwise beside it; always within the work area.
    void ShowNear(const QRect& rcThumbGlobal, const QPoint& ptInThumb, int nBelowThumb, const QRect& rcWorkArea);

private:
    void Clear();

    QGridLayout* m_pLayout = nullptr;
};

#endif // INFOPOPUP_H
