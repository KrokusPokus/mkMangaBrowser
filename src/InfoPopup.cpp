#include "InfoPopup.h"

#include <QGridLayout>
#include <QLabel>

namespace {
constexpr int kMargin        = 20;
constexpr int kLabelColumn   = 87;
constexpr int kCommentsWidth = 480;
constexpr int kGap           = 8;
const QString kStyle = QStringLiteral(
    "CInfoPopup { background-color: #222225; border: 1px solid #4a4a50; }"
    "QLabel { color: #DDDDDD; background: transparent; }"
    "QLabel#chapters-heading { color: #FF0000; font-weight: bold; }");
} // namespace

CInfoPopup::CInfoPopup(QWidget* pParent)
    : QFrame(pParent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setStyleSheet(kStyle);
    m_pLayout = new QGridLayout(this);
    m_pLayout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    m_pLayout->setHorizontalSpacing(0);
    m_pLayout->setVerticalSpacing(6);
    m_pLayout->setColumnMinimumWidth(0, kLabelColumn);
}

void CInfoPopup::Clear()
{
    while (QLayoutItem* pItem = m_pLayout->takeAt(0)) {
        delete pItem->widget();
        delete pItem;
    }
}

void CInfoPopup::SetInfo(const SBook& book, const SBookMeta& meta)
{
    Clear();
    int nRow = 0;
    auto addRow = [this, &nRow](const QString& sLabel, const QString& sValue) {
        if (sValue.isEmpty())
            return;
        m_pLayout->addWidget(new QLabel(sLabel, this), nRow, 0, Qt::AlignTop);
        auto* pValue = new QLabel(sValue, this);
        pValue->setTextFormat(Qt::PlainText);
        m_pLayout->addWidget(pValue, nRow, 1, Qt::AlignTop);
        ++nRow;
    };
    // Second line only if it differs from the first
    auto other = [](const QString& sFirst, const QString& sSecond) { return sSecond == sFirst ? QString() : sSecond; };

    const QString sTitle = meta.sTitle.isEmpty() ? book.sTitle : meta.sTitle;
    const QString sAuthor = meta.sAuthor.isEmpty() ? book.sAuthor : meta.sAuthor;
    addRow(tr("Title"), sTitle);
    addRow(QString(), other(sTitle, meta.sTitleOrig));
    addRow(tr("Author"), sAuthor);
    addRow(QString(), other(sAuthor, meta.sAuthorOrig));
    addRow(tr("Type"), meta.sType);
    addRow(tr("Date"), meta.sDate.isEmpty() ? book.sDate : meta.sDate);
    addRow(tr("Pages"), book.aPages.isEmpty() ? QString() : QString::number(book.aPages.size()));

    if (!meta.aChapters.isEmpty()) {
        auto* pHeading = new QLabel(tr("Chapters:"), this);
        pHeading->setObjectName(QStringLiteral("chapters-heading"));
        QFont fontHeading = pHeading->font();
        fontHeading.setPointSizeF(fontHeading.pointSizeF() + 2.0);
        pHeading->setFont(fontHeading);
        if (nRow > 0)
            m_pLayout->setRowMinimumHeight(nRow++, 5);
        m_pLayout->addWidget(pHeading, nRow++, 0, 1, 2);

        for (const SChapter& ch : meta.aChapters) {
            QString sText = ch.sTitle.isEmpty() ? ch.sTitleOrig : ch.sTitle;
            if (!ch.sTitle.isEmpty() && !ch.sTitleOrig.isEmpty() && ch.sTitleOrig != ch.sTitle)
                sText += QStringLiteral("  \u00B7  ") + ch.sTitleOrig;
            auto* pLabel = new QLabel(sText, this);
            pLabel->setTextFormat(Qt::PlainText);
            pLabel->setWordWrap(true);
            pLabel->setMaximumWidth(kCommentsWidth);
            m_pLayout->addWidget(new QLabel(ch.sStart, this), nRow, 0, Qt::AlignTop);
            m_pLayout->addWidget(pLabel, nRow++, 1, Qt::AlignTop);
        }
    }
    adjustSize();
}

void CInfoPopup::ShowNear(const QRect& rcThumb, const QPoint& ptInThumb, int nBelowThumb, const QRect& rcWork)
{
    adjustSize();
    const QSize sz = size();
    const QRect rcArea = rcWork.isValid() ? rcWork : QRect(rcThumb.center() - QPoint(5000, 5000), QSize(10000, 10000));

    // Horizontally centered over the thumbnail
    int x = rcThumb.center().x() - sz.width() / 2;
    x = std::clamp(x, rcArea.left(), std::max(rcArea.left(), rcArea.right() + 1 - sz.width()));
    int y = 0;

    const int nBelowY = rcThumb.bottom() + 1 + nBelowThumb;
    const int nAboveY = rcThumb.top() - kGap - sz.height();
    const bool bFitsBelow = nBelowY + sz.height() <= rcArea.bottom() + 1;
    const bool bFitsAbove = nAboveY >= rcArea.top();

    auto placeBeside = [&]() {
        y = rcThumb.center().y() - sz.height() / 2;
        y = std::clamp(y, rcArea.top(), std::max(rcArea.top(), rcArea.bottom() + 1 - sz.height()));
        const int nRightX = rcThumb.right() + 1 + kGap;
        const int nLeftX = rcThumb.left() - kGap - sz.width();
        if (ptInThumb.x() > rcThumb.width() / 2)    // right half -> prefer to the right
            x = (nRightX + sz.width() <= rcArea.right() + 1) ? nRightX : nLeftX;
        else
            x = (nLeftX >= rcArea.left()) ? nLeftX : nRightX;
    };

    if (ptInThumb.y() > rcThumb.height() / 2) {     // lower half -> prefer below
        if (bFitsBelow)      y = nBelowY;
        else if (bFitsAbove) y = nAboveY;
        else                 placeBeside();
    }
    else {                                          // upper half -> prefer above
        if (bFitsAbove)      y = nAboveY;
        else if (bFitsBelow) y = nBelowY;
        else                 placeBeside();
    }

    move(x, y);
    show();
    raise();
}
