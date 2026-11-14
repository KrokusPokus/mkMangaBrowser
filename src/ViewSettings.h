#ifndef VIEWSETTINGS_H
#define VIEWSETTINGS_H

#include "MangaLibrary.h"

#include <QList>
#include <QString>

struct SViewSettings {
    EField  eGroup = EField::Author;
    EField  eSort  = EField::Date;
    EField  eText  = EField::Title;
    QString sTextFilter;                    // filter bar (Ctrl+F), not saved
};

// A group in the window
struct SGroup {
    QString     sTitle;
    QList<int>  aMembers;           // indices into the book list
    int         nHidden = 0;
};

namespace ViewLogic {

// Sets bVisible/bShaded of each book
void ApplyFilter(QList<SBook>& aBooks, const SViewSettings& settings);

// Groups and sorts
QList<SGroup> BuildGroups(const QList<SBook>& aBooks, const SViewSettings& settings);

} // namespace ViewLogic

#endif // VIEWSETTINGS_H
