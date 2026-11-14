#ifndef MANGALIBRARY_H
#define MANGALIBRARY_H

#include <QList>
#include <QString>
#include <QStringList>

// Fields that can be used for grouping, sorting and labeling
enum class EField {
    None,
    Author,
    Title,
    Type,
    Date,
    DateYM          // grouping only (year/month)
};

QString FieldKey(EField eField);                        // "Author", ... or "|" for None
EField  FieldFromKey(const QString& sKey, EField eFallback);

struct SChapter {
    QString sStart;             // page base name, e.g. "028": opening the chapter starts here
    QString sCover;             // page base name of the title image, e.g. "029-030" (empty: use sStart)
    QString sTitleOrig;
    QString sTitleEn;
};

// Contents of the meta text file in a book folder:
//   author_orig=, author_en=, name_orig=, name_en=, type=, date=, cover=, chapters=,
//   ch<N>_start=, ch<N>_cover=, ch<N>_title_orig=, ch<N>_title_en=
// Specification: doc/MetadataFormat.md
struct SBookMeta {
    QString         sAuthorOrig;
    QString         sAuthorEn;
    QString         sNameOrig;
    QString         sNameEn;
    QString         sType;          // "manga", ...
    QString         sDate;          // "YYYY-MM-DD", "YYYY", ...
    QString         sCover;         // page base name, e.g. "000a"
    QList<SChapter> aChapters;
};

// One book (= folder <Manga>/<Author>/<Author (Date) Title>)
struct SBook {
    SBookMeta   meta;
    QString     sBookPath;          // book folder
    QString     sMetaPath;          // <book>/metadata.conf, or empty if it does not exist
    QString     sCoverPath;         // full path of the cover image or empty
    QString     sThumbMarker;       // "<AuthorFolder>_<BookFolder>" -> file name in the thumbnail cache
    QStringList aPages;             // image file names, naturally sorted

    // Taken from the meta file, otherwise from the folder name "Author (Date) Title"
    QString     sAuthor;
    QString     sTitle;
    QString     sDate;

    // set by the filter
    bool        bVisible = true;
    bool        bShaded  = false;

    QString Field(EField eField) const;
    QString PagePath(const QString& sBaseName) const;   // image whose base name matches, or empty
    QString ChapterCoverPath(int nChapter) const;       // title image (chN_cover, else chN_start), or empty
};

namespace MangaLibrary {

extern const QString kMetaFileName;     // "metadata.conf"

// Reads all books from <Manga>/<Author>/<Book> (folders starting with "_" are ignored).
// A book folder without metadata.conf gets a new one, generated from the folder names
// (see doc/MetadataFormat.md). Files that could not be written are reported in pErrors.
QList<SBook> Load(const QString& sMangaDir, QStringList* pErrors = nullptr);

// Read only (for the info popup)
SBookMeta ReadMeta(const QString& sMetaPath);

bool IsImageFile(const QString& sFileName);
int CompareLogical(const QString& s1, const QString& s2);

} // namespace MangaLibrary

#endif // MANGALIBRARY_H
