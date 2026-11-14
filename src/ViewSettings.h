#ifndef VIEWSETTINGS_H
#define VIEWSETTINGS_H

#include "MangaLibrary.h"

#include <QList>
#include <QPoint>
#include <QString>

// Filter set by double-clicking a group header: only books whose GroupValue(eField) equals sValue.
// eField/sValue describe the filter completely (e.g. for saving as FieldKey(eField) + sValue);
// the other members only serve to undo it.
struct SGroupFilter {
    EField  eField = EField::None;
    QString sValue;                         // GroupValue(); empty = "Unspecified"
    EField  eGroupBefore = EField::None;    // grouping before the filter was added
    QPoint  ptScrollBefore;                 // scroll position before the filter was added
};

struct SViewSettings {
    EField  eGroup = EField::Author;
    EField  eSort  = EField::Date;
    EField  eText  = EField::Title;
    QList<SGroupFilter> aGroupFilters;      // in the order they were added; not saved yet
    QString sTextFilter;                    // filter bar (Ctrl+F), not saved

    bool IsFiltered(EField eField) const;   // is there a group filter for this field (Date/DateYM count as one)?
};

// A group in the window
struct SGroup {
    QString     sTitle;             // header text; empty = no header (grouping EField::None)
    QString     sValue;             // GroupValue() of its members; empty = "Unspecified"
    QList<int>  aMembers;           // indices into the book list
    int         nHidden = 0;
};

namespace ViewLogic {

// Value a book is grouped and group-filtered by: first letter for Title, year for Date,
// year/month for DateYM, otherwise the field itself. Empty = "Unspecified".
QString GroupValue(const SBook& book, EField eField);

// Sets bInScope (group filters), bVisible (additionally the text filter) and bShaded of each book
void ApplyFilter(QList<SBook>& aBooks, const SViewSettings& settings);

// Grouping after a group filter for eFiltered was added (call after ApplyFilter()): the first field
// of a list that depends on eFiltered (only author, series, type), is not filtered yet and has at least
// two different values among the books in scope; otherwise EField::None (no grouping).
// BuildGroups() with EField::None returns a single group with an empty title (drawn without header).
EField ChooseGrouping(const QList<SBook>& aBooks, const SViewSettings& settings, EField eFiltered);

// Groups and sorts
QList<SGroup> BuildGroups(const QList<SBook>& aBooks, const SViewSettings& settings);

} // namespace ViewLogic

#endif // VIEWSETTINGS_H
