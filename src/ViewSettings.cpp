#include "ViewSettings.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

// Pad the date to full length so that "1991" is sorted after "1991-12-31"
QString PadDate(const QString& sDate, const QString& sMask)
{
    return sDate.size() >= sMask.size() ? sDate : sDate + sMask.mid(sDate.size());
}

bool IsDateField(EField e)
{
    return e == EField::Date || e == EField::DateYM;
}

} // namespace

bool SViewSettings::IsFiltered(EField eField) const
{
    for (const SGroupFilter& f : aGroupFilters) {
        if (f.eField == eField || (IsDateField(f.eField) && IsDateField(eField)))
            return true;
    }
    return false;
}

namespace ViewLogic {

QString GroupValue(const SBook& book, EField eField)
{
    const QString sRaw = book.Field(eField).trimmed();
    switch (eField) {
        case EField::Title:  return sRaw.left(1).toUpper();    // first letter only
        case EField::Date:   return sRaw.left(4);
        case EField::DateYM: return sRaw.left(7);
        default:             return sRaw;
    }
}

void ApplyFilter(QList<SBook>& aBooks, const SViewSettings& settings)
{
    // Filter bar: every word must occur (as in mkFolderWidget); all names (original and English),
    // type, date and chapter titles are searched
    const QStringList aWords = settings.sTextFilter.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    for (SBook& book : aBooks) {
        // Group filters (case-insensitive, like the merging of group headers)
        book.bInScope = true;
        for (const SGroupFilter& f : settings.aGroupFilters) {
            if (GroupValue(book, f.eField).compare(f.sValue, Qt::CaseInsensitive) != 0) {
                book.bInScope = false;
                break;
            }
        }

        bool bShow = book.bInScope;
        if (bShow && !aWords.isEmpty()) {
            QStringList aHay{ book.sAuthor, book.sTitle, book.sDate, book.meta.sAuthorOrig, book.meta.sAuthor,
                              book.meta.sTitleOrig, book.meta.sTitle, book.meta.sSeriesOrig, book.meta.sSeries,
                              book.meta.sType };
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
    const QString sUnspecified = QCoreApplication::translate("ViewLogic", "Unspecified");

    struct SEntry {
        QString sGroupKey;
        QString sSortKey;
        QString sHeader;
        QString sValue;
        int     nBook;
    };
    QList<SEntry> aEntries;
    aEntries.reserve(aBooks.size());

    for (int i = 0; i < aBooks.size(); ++i) {
        const SBook& book = aBooks.at(i);
        if (!book.bInScope)
            continue;       // excluded by a group filter: not part of any group

        // ---- Group key and header
        // EField::None: one group without header (empty title)
        const QString sValue = GroupValue(book, eGroup);
        QString sGroupKey = (sValue.isEmpty() && eGroup != EField::None) ? sUnspecified : sValue;
        const QString sHeader = sGroupKey;
        if (eGroup == EField::DateYM && sGroupKey != sUnspecified)
            sGroupKey = PadDate(sGroupKey, QStringLiteral("xxxx-xx"));

        // ---- Sort key within the group (title as tie breaker)
        QString sSortKey = (eSort == EField::Date || eSort == EField::DateYM)
                               ? PadDate(book.sDate, QStringLiteral("xxxx-xx-xx"))
                               : book.Field(eSort);
        if (sSortKey.isEmpty())
            sSortKey = sUnspecified;

        aEntries.append({ sGroupKey, sSortKey + QLatin1Char('\t') + book.sTitle, sHeader, sValue, i });
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
            aGroups.append({ e.sHeader, e.sValue, {}, 0 });
        aGroups.last().aMembers.append(e.nBook);
        if (!aBooks.at(e.nBook).bVisible)
            ++aGroups.last().nHidden;
    }
    return aGroups;
}

EField ChooseGrouping(const QList<SBook>& aBooks, const SViewSettings& settings, EField eFiltered)
{
    // A series is treated as belonging to exactly one author, so after a series filter the author is pointless.
    // Title and year are never chosen automatically.
    QList<EField> aCandidates;
    switch (eFiltered) {
        case EField::Author: aCandidates = { EField::Series, EField::Type }; break;
        case EField::Type:   aCandidates = { EField::Author, EField::Series }; break;
        case EField::Series: aCandidates = { EField::Type }; break;
        default:             aCandidates = { EField::Author, EField::Series, EField::Type }; break;
    }

    for (const EField eField : std::as_const(aCandidates)) {
        if (settings.IsFiltered(eField))
            continue;
        QString sFirst;
        bool bHaveFirst = false;
        for (const SBook& book : aBooks) {
            if (!book.bInScope)
                continue;
            const QString sValue = GroupValue(book, eField);
            if (!bHaveFirst) {
                sFirst = sValue;
                bHaveFirst = true;
            }
            else if (sValue.compare(sFirst, Qt::CaseInsensitive) != 0) {
                return eField;      // at least two different values
            }
        }
    }
    return EField::None;    // no grouping: a single list without header
}

} // namespace ViewLogic
