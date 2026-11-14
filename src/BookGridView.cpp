#include "BookGridView.h"

#include "InfoPopup.h"
#include "ThumbnailCache.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QWheelEvent>

#include <climits>

namespace {

// Dimensions from the AHK script (there * A_ScreenDPI/96, here logical pixels)
constexpr int kThumbHeight      = 172;
constexpr int kLeftMarginImage  = 32;
constexpr int kHeaderToImage    = 32;
constexpr int kImageDistanceH   = 32;
constexpr int kImageDistanceV   = 32;
constexpr int kGroupDistanceV   = 40;
constexpr int kGroupDistanceTop = 18;   // less spacing at the top or after groups without images
constexpr int kUnderlineDist    = 8;
constexpr int kHeaderX          = 14;
constexpr int kCountGapX        = 10;
constexpr int kTextGapY         = 7;
constexpr int kRightReserve     = 20;   // (g_iClientW - 20)
constexpr int kLineWidth        = 300;
constexpr int kExtraScroll      = 16;   // "nMax + 16 so we can scroll a little bit further out"
constexpr int kLineStep         = 42;   // SB_LINEUP/SB_LINEDOWN
constexpr int kHoverDelayMs     = 400;  // TrackMouseEvent dwHoverTime
constexpr int kTypeResetMs      = 500;

const QColor kBackground(0x20, 0x20, 0x20);
const QColor kPlaceholder(0x32, 0x32, 0x32);
const QColor kTextColor(0xFF, 0xFF, 0xFF);
const QColor kCountColor(0x88, 0x88, 0x88);
const QColor kLineFrom(0xCB, 0xCB, 0xCB);   // IMG\Gradient.bmp: 300x1, light -> background
const QColor kLineTo(0x21, 0x21, 0x21);

constexpr int kTextFlags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;

} // namespace

CBookGridView::CBookGridView(QWidget* pParent)
    : QAbstractScrollArea(pParent)
{
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);       // Original: WS_VSCROLL always present
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);    // paintEvent() draws the background itself
    QPalette pal = palette();
    pal.setColor(QPalette::Window, kBackground);
    pal.setColor(QPalette::Base, kBackground);
    setPalette(pal);

    m_fontSmall = font();
    m_fontHeader = font();
    m_fontHeader.setPointSizeF(14.0);
    m_fontHeader.setWeight(QFont::Normal);

    m_pPopup = new CInfoPopup(this);

    m_timerHover.setSingleShot(true);
    m_timerHover.setInterval(kHoverDelayMs);
    connect(&m_timerHover, &QTimer::timeout, this, &CBookGridView::OnHoverTimeout);

    m_timerTypeClear.setSingleShot(true);
    m_timerTypeClear.setInterval(kTypeResetMs);
    connect(&m_timerTypeClear, &QTimer::timeout, this, [this]() { m_sTypeBuffer.clear(); });
}

CBookGridView::~CBookGridView() = default;

void CBookGridView::SetContent(const QList<SBook>* pBooks, const QList<SGroup>& aGroups, EField eText, CThumbnailCache* pCache,
                               bool bChapterMode)
{
    HideInfoPopup();
    if (m_pCache != pCache) {
        if (m_pCache)
            disconnect(m_pCache, nullptr, this, nullptr);
        m_pCache = pCache;
        if (m_pCache)
            connect(m_pCache, &CThumbnailCache::ThumbnailReady, this, &CBookGridView::OnThumbnailReady);
    }
    m_pBooks = pBooks;
    m_aGroups = aGroups;
    m_eText = eText;
    m_bChapterMode = bChapterMode;
    m_mapThumbWidths.clear();
    m_nLayoutWidth = -1;
    Relayout();
    viewport()->update();
}

// =============================================================================================== Layout

void CBookGridView::Relayout()
{
    m_aHeaders.clear();
    m_aThumbs.clear();
    m_nPopupThumb = -1;
    if (!m_pBooks || !m_pCache) {
        m_szContent = {};
        UpdateScrollBars();
        return;
    }

    const int nClientW = viewport()->width();
    m_nLayoutWidth = nClientW;
    const QFontMetrics fmHeader(m_fontHeader);
    const QFontMetrics fmSmall(m_fontSmall);

    int  nYMax = 0;
    int  nXMax = 0;
    bool bAfterImage = false;

    for (int nGroup = 0; nGroup < m_aGroups.size(); ++nGroup) {
        const SGroup& group = m_aGroups.at(nGroup);
        const int nMembers = group.aMembers.size();
        if (nMembers == 0 || nMembers == group.nHidden)
            continue;   // group without visible books -> no header

        bool bAfterHeader = true;
        if (group.sTitle.isEmpty()) {
            // No grouping: thumbnails only, no header (saves vertical space)
            bAfterImage = false;
        }
        else {
            // ---- Header "Title  (n total / m hidden)" + gradient line
            SHeaderItem header;
            header.nGroup = nGroup;
            header.sTitle = group.sTitle;
            header.sCount = group.nHidden > 0 ? tr("(%1 total / %2 hidden)").arg(nMembers).arg(group.nHidden)
                                              : tr("(%1 total)").arg(nMembers);
            int nY = nYMax + kGroupDistanceV;
            if (!bAfterImage)
                nY -= kGroupDistanceTop;

            header.rcTitle = QRect(kHeaderX, nY, fmHeader.horizontalAdvance(header.sTitle), fmHeader.height());
            const int nBaseline = nY + fmHeader.ascent();
            header.rcCount = QRect(header.rcTitle.right() + 1 + kCountGapX, nBaseline - fmSmall.ascent(),
                                   fmSmall.horizontalAdvance(header.sCount), fmSmall.height());
            header.nCountBaseline = nBaseline;
            const int nLineY = std::max(header.rcTitle.bottom(), header.rcCount.bottom()) + 1 + kUnderlineDist;
            header.rcLine = QRect(0, nLineY, kLineWidth, 1);
            m_aHeaders.append(header);

            nYMax = header.rcLine.bottom() + 1;
            nXMax = std::max({ nXMax, header.rcLine.right() + 1, header.rcCount.right() + 1 });
            bAfterImage = false;
        }

        // ---- Thumbnails (wrap as soon as the next image would extend past the right edge)
        int nPreviousX = 0;
        QPoint ptNext;
        bool bFirstBook = true;
        for (const int nBook : group.aMembers) {
            const SBook& book = m_pBooks->at(nBook);
            if (!book.bVisible)
                continue;

            // ---- Thumbnails of this book: cover, in chapter mode also one per defined chapter
            struct SEntry {
                int     nChapter;
                QString sMarker;
                QString sSource;    // empty -> placeholder "Missing"
                QString sText;
            };
            QList<SEntry> aEntries;
            aEntries.append({ -1, book.sThumbMarker, book.sCoverPath, m_eText != EField::None ? book.Field(m_eText) : QString() });
            if (m_bChapterMode) {
                for (int i = 0; i < book.meta.aChapters.size(); ++i) {
                    const SChapter& ch = book.meta.aChapters.at(i);
                    if (ch.sCover.isEmpty() && ch.sStart.isEmpty())
                        continue;   // not defined yet
                    // Marker named after the image, so that a changed chN_cover gets its own thumbnail
                    const QString sSource = book.ChapterCoverPath(i);
                    const QString sMarker = book.sThumbMarker + QLatin1Char('#')
                                            + (sSource.isEmpty() ? QStringLiteral("ch%1-missing").arg(i + 1)
                                                                 : QFileInfo(sSource).completeBaseName());
                    QString sText;
                    if (m_eText != EField::None) {
                        sText = !ch.sTitle.isEmpty() ? ch.sTitle : ch.sTitleOrig;
                        if (sText.isEmpty())
                            sText = tr("Chapter %1").arg(i + 1);
                    }
                    aEntries.append({ i, sMarker, sSource, sText });
                }
            }

            // Chapter mode: chapter thumbnails that wrap into a new row start at the x position of the
            // first chapter (right of the book cover), so that only book covers start at the left margin
            int nChapterIndentX = kLeftMarginImage;

            for (const SEntry& entry : std::as_const(aEntries)) {
                auto itWidth = m_mapThumbWidths.find(entry.sMarker);
                if (itWidth == m_mapThumbWidths.end())
                    itWidth = m_mapThumbWidths.insert(entry.sMarker, m_pCache->Prepare(entry.sMarker, entry.sSource));
                const int nWidth = *itWidth;

                // In chapter mode every book starts a new row
                const bool bNewBookRow = m_bChapterMode && entry.nChapter < 0 && !bFirstBook;

                QPoint ptImage;
                if (bNewBookRow || nPreviousX + kImageDistanceH + nWidth > nClientW - kRightReserve) {
                    int nX = kLeftMarginImage;
                    if (m_bChapterMode && entry.nChapter >= 0 && nChapterIndentX + nWidth <= nClientW - kRightReserve)
                        nX = nChapterIndentX;   // too narrow window: left margin as before
                    ptImage = QPoint(nX, nYMax + kImageDistanceV);
                    nPreviousX = nX + nWidth;
                }
                else if (bAfterHeader) {
                    ptImage = QPoint(kLeftMarginImage, nYMax + kHeaderToImage);
                    nPreviousX = kLeftMarginImage + nWidth;
                }
                else {
                    ptImage = ptNext;
                    nPreviousX += kImageDistanceH + nWidth;
                }

                SThumbItem item;
                item.nBook = nBook;
                item.nChapter = entry.nChapter;
                item.sMarker = entry.sMarker;
                item.rcImage = QRect(ptImage, QSize(nWidth, kThumbHeight));

                if (!entry.sText.isEmpty()) {
                    item.sText = entry.sText;
                    const QRect rcBound = fmSmall.boundingRect(QRect(0, 0, nWidth, INT_MAX / 2), kTextFlags, item.sText);
                    item.rcText = QRect(ptImage.x(), item.rcImage.bottom() + 1 + kTextGapY, nWidth, rcBound.height());
                }

                const int nBottom = item.rcText.isNull() ? item.rcImage.bottom() + 1 : item.rcText.bottom() + 1;
                nYMax = std::max(nYMax, nBottom);
                nXMax = std::max(nXMax, item.rcImage.right() + 1);
                ptNext = QPoint(item.rcImage.right() + 1 + kImageDistanceH, item.rcImage.top());
                if (entry.nChapter < 0)
                    nChapterIndentX = ptNext.x();
                m_aThumbs.append(item);

                bAfterHeader = false;
                bAfterImage = true;
            }
            bFirstBook = false;
        }
    }

    m_szContent = QSize(nXMax + kExtraScroll, nYMax + kExtraScroll);
    UpdateScrollBars();
}

void CBookGridView::UpdateScrollBars()
{
    const QSize szView = viewport()->size();
    horizontalScrollBar()->setRange(0, std::max(0, m_szContent.width() - szView.width()));
    horizontalScrollBar()->setPageStep(szView.width());
    horizontalScrollBar()->setSingleStep(kLineStep);
    verticalScrollBar()->setRange(0, std::max(0, m_szContent.height() - szView.height()));
    verticalScrollBar()->setPageStep(szView.height());
    verticalScrollBar()->setSingleStep(kLineStep);
}

QPoint CBookGridView::ContentOffset() const
{
    return { horizontalScrollBar()->value(), verticalScrollBar()->value() };
}

QPoint CBookGridView::ScrollPosition() const
{
    return ContentOffset();
}

void CBookGridView::SetScrollPosition(const QPoint& pt)
{
    horizontalScrollBar()->setValue(pt.x());
    verticalScrollBar()->setValue(pt.y());
}

// =============================================================================================== Painting

void CBookGridView::paintEvent(QPaintEvent* pEvent)
{
    QPainter painter(viewport());
    painter.fillRect(pEvent->rect(), kBackground);      // Gui,Color,202020
    const QPoint ptOffset = ContentOffset();
    const QRect rcVisible = pEvent->rect().translated(ptOffset);
    painter.translate(-ptOffset);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // ---- Headers
    for (const SHeaderItem& header : std::as_const(m_aHeaders)) {
        const QRect rcAll = header.rcTitle.united(header.rcCount).united(header.rcLine);
        if (!rcAll.intersects(rcVisible))
            continue;
        painter.setFont(m_fontHeader);
        painter.setPen(kTextColor);
        painter.drawText(header.rcTitle, Qt::AlignLeft | Qt::AlignTop, header.sTitle);

        painter.setFont(m_fontSmall);
        painter.setPen(kCountColor);
        painter.drawText(QPoint(header.rcCount.left(), header.nCountBaseline), header.sCount);

        QLinearGradient gradient(header.rcLine.topLeft(), header.rcLine.topRight());
        gradient.setColorAt(0.0, kLineFrom);
        gradient.setColorAt(1.0, kLineTo);
        painter.fillRect(header.rcLine, gradient);
    }

    // ---- Thumbnails
    painter.setFont(m_fontSmall);
    for (const SThumbItem& item : std::as_const(m_aThumbs)) {
        const QRect rcAll = item.rcText.isNull() ? item.rcImage : item.rcImage.united(item.rcText);
        if (!rcAll.intersects(rcVisible))
            continue;
        const SBook& book = m_pBooks->at(item.nBook);
        const QPixmap pm = m_pCache->Pixmap(item.sMarker, book.bShaded);
        if (pm.isNull())
            painter.fillRect(item.rcImage, kPlaceholder);     // still being generated
        else
            painter.drawPixmap(item.rcImage, pm);

        if (!item.sText.isEmpty()) {
            painter.setPen(kTextColor);
            painter.drawText(item.rcText, kTextFlags, item.sText);
        }
    }
}

void CBookGridView::OnThumbnailReady(const QString& sMarker)
{
    const QPoint ptOffset = ContentOffset();
    const QRect rcView(ptOffset, viewport()->size());
    for (const SThumbItem& item : std::as_const(m_aThumbs)) {
        if (item.sMarker == sMarker && item.rcImage.intersects(rcView))
            viewport()->update(item.rcImage.translated(-ptOffset));
    }
}

void CBookGridView::resizeEvent(QResizeEvent* pEvent)
{
    QAbstractScrollArea::resizeEvent(pEvent);
    HideInfoPopup();
    if (viewport()->width() != m_nLayoutWidth)
        Relayout();     // adapt line wrapping to the new window width (original: only after reloading)
    else
        UpdateScrollBars();
}

void CBookGridView::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    HideInfoPopup();
    viewport()->update();
}

// =============================================================================================== Hit testing

const CBookGridView::SThumbItem* CBookGridView::ThumbAt(const QPoint& ptViewport) const
{
    const QPoint pt = ptViewport + ContentOffset();
    for (const SThumbItem& item : m_aThumbs) {
        if (item.rcImage.contains(pt))
            return &item;
    }
    return nullptr;
}

int CBookGridView::HeaderAt(const QPoint& ptViewport) const
{
    // Whole header row: title, counter and gradient line
    const QPoint pt = ptViewport + ContentOffset();
    for (const SHeaderItem& header : m_aHeaders) {
        QRect rc = header.rcTitle.united(header.rcCount).united(header.rcLine);
        rc.setLeft(0);
        if (rc.contains(pt))
            return header.nGroup;
    }
    return -1;
}

int CBookGridView::BookAt(const QPoint& ptViewport) const
{
    const SThumbItem* pItem = ThumbAt(ptViewport);
    return pItem ? pItem->nBook : -1;
}

int CBookGridView::BookUnderCursor() const
{
    const QPoint pt = viewport()->mapFromGlobal(QCursor::pos());
    return viewport()->rect().contains(pt) ? BookAt(pt) : -1;
}

// =============================================================================================== Scrolling

void CBookGridView::ScrollBy(int dx, int dy)
{
    if (dx) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    if (dy) verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void CBookGridView::ScrollToY(int y)
{
    verticalScrollBar()->setValue(y);
}

void CBookGridView::wheelEvent(QWheelEvent* pEvent)
{
    HideInfoPopup();
    const Qt::KeyboardModifiers mods = pEvent->modifiers();
    const bool bRightButton = pEvent->buttons() & Qt::RightButton;
    const bool bHorizontal = (mods & Qt::ShiftModifier) || (pEvent->angleDelta().x() != 0 && pEvent->angleDelta().y() == 0);
    const int nDelta = bHorizontal && pEvent->angleDelta().x() != 0 ? pEvent->angleDelta().x() : pEvent->angleDelta().y();
    if (nDelta == 0)
        return;

    QScrollBar* pBar = bHorizontal ? horizontalScrollBar() : verticalScrollBar();
    int nStep = 0;
    if ((mods & Qt::ControlModifier) || bRightButton) {
        // Page-wise; with the right mouse button held, no context menu afterwards (as in the original)
        nStep = (nDelta > 0 ? -1 : 1) * pBar->pageStep();
        m_bIgnoreNextContextMenu = bRightButton;
    }
    else {
        nStep = -nDelta * kLineStep / 120;
        if (nStep == 0)
            nStep = nDelta > 0 ? -1 : 1;
        m_bIgnoreNextContextMenu = false;
    }
    pBar->setValue(pBar->value() + nStep);
    pEvent->accept();
}

void CBookGridView::keyPressEvent(QKeyEvent* pEvent)
{
    HideInfoPopup();
    const Qt::KeyboardModifiers mods = pEvent->modifiers();
    const bool bShift = mods & Qt::ShiftModifier;
    const int nKey = pEvent->key();

    if (mods & Qt::ControlModifier) {
        switch (nKey) {
            case Qt::Key_E: { const int n = BookUnderCursor(); if (n >= 0) emit EditMetaRequested(n); return; }
            case Qt::Key_G: { const int n = BookUnderCursor(); if (n >= 0) emit WebSearchRequested(n); return; }
            case Qt::Key_L: emit OpenFolderRequested(BookUnderCursor()); return;
            default: break;
        }
        QAbstractScrollArea::keyPressEvent(pEvent);
        return;
    }

    QScrollBar* pBar = bShift ? horizontalScrollBar() : verticalScrollBar();
    switch (nKey) {
        case Qt::Key_PageUp:   pBar->setValue(pBar->value() - pBar->pageStep()); return;
        case Qt::Key_PageDown: pBar->setValue(pBar->value() + pBar->pageStep()); return;
        case Qt::Key_Home:     pBar->setValue(pBar->minimum()); return;
        case Qt::Key_End:      pBar->setValue(pBar->maximum()); return;
        case Qt::Key_Left:     ScrollBy(-kLineStep, 0); return;
        case Qt::Key_Right:    ScrollBy(kLineStep, 0); return;
        case Qt::Key_Up:       ScrollBy(0, -kLineStep); return;
        case Qt::Key_Down:     ScrollBy(0, kLineStep); return;
        case Qt::Key_F1:       emit HelpRequested(); return;
        case Qt::Key_F5:       emit ReloadRequested(); return;
        default: break;
    }

    // Letters/digits: jump to the first header that starts with the input
    const QString sText = pEvent->text();
    if (sText.size() == 1 && sText.at(0).isLetterOrNumber()) {
        TypeToJump(sText);
        return;
    }
    QAbstractScrollArea::keyPressEvent(pEvent);
}

void CBookGridView::TypeToJump(const QString& sChar)
{
    m_timerTypeClear.start();
    m_sTypeBuffer += sChar;
    for (const SHeaderItem& header : std::as_const(m_aHeaders)) {
        if (header.sTitle.startsWith(m_sTypeBuffer, Qt::CaseInsensitive)) {
            ScrollToY(header.rcTitle.top() - 3);
            return;
        }
    }
}

// =============================================================================================== Mouse

void CBookGridView::mouseDoubleClickEvent(QMouseEvent* pEvent)
{
    if (pEvent->button() == Qt::LeftButton) {
        HideInfoPopup();
        const QPoint pt = pEvent->position().toPoint();
        if (const SThumbItem* pItem = ThumbAt(pt)) {
            if (pItem->nChapter >= 0)
                emit OpenChapterRequested(pItem->nBook, pItem->nChapter);
            else
                emit OpenBookRequested(pItem->nBook);
        }
        else if (const int nGroup = HeaderAt(pt); nGroup >= 0)
            emit GroupHeaderActivated(nGroup);
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(pEvent);
}

void CBookGridView::mousePressEvent(QMouseEvent* pEvent)
{
    HideInfoPopup();
    if (pEvent->button() == Qt::RightButton)
        m_bIgnoreNextContextMenu = false;
    QAbstractScrollArea::mousePressEvent(pEvent);
}

void CBookGridView::mouseMoveEvent(QMouseEvent* pEvent)
{
    const QPoint pt = pEvent->position().toPoint();
    if (pt == m_ptLastMouse)
        return;
    m_ptLastMouse = pt;

    // Group headers can be double-clicked (filter)
    if (HeaderAt(pt) >= 0)
        viewport()->setCursor(Qt::PointingHandCursor);
    else
        viewport()->unsetCursor();

    // Different item under the mouse -> close popup immediately (as in the original)
    if (m_pPopup->isVisible()) {
        const SThumbItem* pItem = ThumbAt(pt);
        const int nThumb = pItem ? static_cast<int>(pItem - m_aThumbs.constData()) : -1;
        if (nThumb != m_nPopupThumb)
            HideInfoPopup();
    }
    m_timerHover.start();
}

void CBookGridView::contextMenuEvent(QContextMenuEvent* pEvent)
{
    HideInfoPopup();
    if (m_bIgnoreNextContextMenu) {
        m_bIgnoreNextContextMenu = false;
        return;
    }
    const QPoint ptViewport = viewport()->mapFromGlobal(pEvent->globalPos());
    emit ContextMenuRequested(BookAt(ptViewport), pEvent->globalPos());
}

bool CBookGridView::viewportEvent(QEvent* pEvent)
{
    if (pEvent->type() == QEvent::Leave) {
        m_timerHover.stop();
        HideInfoPopup();
    }
    return QAbstractScrollArea::viewportEvent(pEvent);
}

void CBookGridView::changeEvent(QEvent* pEvent)
{
    if (pEvent->type() == QEvent::ActivationChange)
        HideInfoPopup();
    QAbstractScrollArea::changeEvent(pEvent);
}

// =============================================================================================== Info-Popup

void CBookGridView::HideInfoPopup()
{
    m_timerHover.stop();
    if (m_pPopup && m_pPopup->isVisible())
        m_pPopup->hide();
    m_nPopupThumb = -1;
}

void CBookGridView::OnHoverTimeout()
{
    // Not while modifiers or the right mouse button are pressed
    const Qt::KeyboardModifiers mods = QApplication::queryKeyboardModifiers();
    if ((mods & (Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier)) || (QApplication::mouseButtons() & Qt::RightButton))
        return;
    if (!isActiveWindow())
        return;

    const QPoint ptViewport = viewport()->mapFromGlobal(QCursor::pos());
    const SThumbItem* pItem = viewport()->rect().contains(ptViewport) ? ThumbAt(ptViewport) : nullptr;
    if (!pItem) {
        HideInfoPopup();
        return;
    }
    const int nThumb = static_cast<int>(pItem - m_aThumbs.constData());
    if (nThumb == m_nPopupThumb && m_pPopup->isVisible())
        return;

    // Read the meta file fresh (edits show up without reloading)
    const SBook& book = m_pBooks->at(pItem->nBook);
    m_pPopup->SetInfo(book, MangaLibrary::ReadMeta(book.sMetaPath));

    const QPoint ptOffset = ContentOffset();
    const QRect rcThumbGlobal(viewport()->mapToGlobal(pItem->rcImage.topLeft() - ptOffset), pItem->rcImage.size());
    const QPoint ptInThumb = ptViewport + ptOffset - pItem->rcImage.topLeft();
    const int nBelowThumb = pItem->rcText.isNull() ? 8 : 8 + kTextGapY + pItem->rcText.height();
    QScreen* pScreen = screen();
    m_pPopup->ShowNear(rcThumbGlobal, ptInThumb, nBelowThumb, pScreen ? pScreen->availableGeometry() : QRect());
    m_nPopupThumb = nThumb;
}
