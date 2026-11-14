#include "ViewSettings.h"

#include <algorithm>

namespace {

// Pad the date to full length so that "1991" is sorted after "1991-12-31"
QString PadDate(const QString& sDate, const QString& sMask)
{
    return sDate.size() >= sMask.size() ? sDate : sDate + sMask.mid(sDate.size());
}

} // namespace

namespace ViewLogic {

void ApplyFilter(QList<SBook>& aBooks, const SViewSettings& settings)
{
    // Filter bar: every word must occur (as in mkFolderWidget); all names (original and English),
    // type, date and chapter titles are searched
    const QStringList aWords = settings.sTextFilter.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    for (SBook& book : aBooks) {
        bool bShow = true;
        if (!aWords.isEmpty()) {
            QStringList aHay{ book.sAuthor, book.sTitle, book.sDate, book.meta.sAuthorOrig, book.meta.sAuthor,
                              book.meta.sTitleOrig, book.meta.sTitle, book.meta.sType };
            for (const SChapter& ch : std::as_const(book.meta.aChapters))
                aHay << ch.sTitleOrig << ch.sTitle;
            const QString sHaystack = aHay.join(QLatin1Char('\n'));
            for (const QString& sWord : aWords) {
                if (!sHaystack.contains(sWord, Qt::CaseInsensitive)) {
                    bShow = false;
                    break;
                }
            }
        }
        book.bVisible = bShow;
        book.bShaded = false;
    }
}

QList<SGroup> BuildGroups(const QList<SBook>& aBooks, const SViewSettings& settings)
{
    const EField eGroup = settings.eGroup;
    const EField eSort  = settings.eSort;
    const QString sUnspecified = QStringLiteral("Unspecified");

    struct SEntry {
        QString sGroupKey;
        QString sSortKey;
        QString sHeader;
        int     nBook;
    };
    QList<SEntry> aEntries;
    aEntries.reserve(aBooks.size());

    for (int i = 0; i < aBooks.size(); ++i) {
        const SBook& book = aBooks.at(i);
        const QString sRaw = book.Field(eGroup);

        // ---- Group key and header
        QString sGroupKey;
        switch (eGroup) {
            case EField::Title:  sGroupKey = sRaw.left(1).toUpper(); break;   // first letter only
            case EField::Date:   sGroupKey = sRaw.left(4); break;
            case EField::DateYM: sGroupKey = sRaw.left(7); break;
            default:             sGroupKey = sRaw; break;
        }
        if (sGroupKey.isEmpty())
            sGroupKey = sUnspecified;
        const QString sHeader = sGroupKey;
        if (eGroup == EField::DateYM && sGroupKey != sUnspecified)
            sGroupKey = PadDate(sGroupKey, QStringLiteral("xxxx-xx"));

        // ---- Sort key within the group (title as tie breaker)
        QString sSortKey = (eSort == EField::Date || eSort == EField::DateYM)
                               ? PadDate(book.sDate, QStringLiteral("xxxx-xx-xx"))
                               : book.Field(eSort);
        if (sSortKey.isEmpty())
            sSortKey = sUnspecified;

        aEntries.append({ sGroupKey, sSortKey + QLatin1Char('\t') + book.sTitle, sHeader, i });
    }

    std::stable_sort(aEntries.begin(), aEntries.end(), [](const SEntry& a, const SEntry& b) {
        if (const int n = MangaLibrary::CompareLogical(a.sGroupKey, b.sGroupKey); n != 0)
            return n < 0;
        if (const int n = MangaLibrary::CompareLogical(a.sSortKey, b.sSortKey); n != 0)
            return n < 0;
        return a.nBook < b.nBook;
    });

    // New group as soon as the header changes (case-insensitive)
    QList<SGroup> aGroups;
    for (const SEntry& e : std::as_const(aEntries)) {
        if (aGroups.isEmpty() || aGroups.last().sTitle.compare(e.sHeader, Qt::CaseInsensitive) != 0)
            aGroups.append({ e.sHeader, {}, 0 });
        aGroups.last().aMembers.append(e.nBook);
        if (!aBooks.at(e.nBook).bVisible)
            ++aGroups.last().nHidden;
    }
    return aGroups;
}

} // namespace ViewLogic
