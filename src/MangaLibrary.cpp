#include "MangaLibrary.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {

const QCollator& LogicalCollator()
{
    static const QCollator s_collator = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    return s_collator;
}

QStringList SortedEntries(const QString& sDir, QDir::Filters filters, const QStringList& aNameFilters = {})
{
    QStringList a = QDir(sDir).entryList(aNameFilters, filters);
    std::sort(a.begin(), a.end(), [](const QString& s1, const QString& s2) { return LogicalCollator().compare(s1, s2) < 0; });
    return a;
}

// metadata.conf is UTF-8 without BOM
QString ReadText(const QString& sPath)
{
    QFile f(sPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

bool WriteText(const QString& sPath, const QString& sText)
{
    QSaveFile f(sPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(sText.toUtf8());    // UTF-8 without BOM, LF line endings
    return f.commit();
}

// Book folder name -> title and date. Tried in this order:
//   "Text (Date) Text"  e.g. "Kishiro Yukito (1991-09-19) Battle Angel Alita v01"
//   "(Date) Text"       e.g. "(1991-09-19) Battle Angel Alita v01"
//   otherwise the folder name is the title and the date stays empty.
// "Date" is YYYY, YYYY-MM or YYYY-MM-DD, so that other text in parentheses is not taken for a date.
void ParseFolderName(const QString& sName, QString& sDate, QString& sTitle)
{
    static const QRegularExpression s_reFull(QStringLiteral(R"(^(.+?)\s+\((\d{4}(?:-\d{2}){0,2})\)\s+(.+)$)"));
    static const QRegularExpression s_reDateTitle(QStringLiteral(R"(^\((\d{4}(?:-\d{2}){0,2})\)\s+(.+)$)"));

    if (const QRegularExpressionMatch m = s_reFull.match(sName); m.hasMatch()) {
        sDate  = m.captured(2);
        sTitle = m.captured(3).trimmed();
    }
    else if (const QRegularExpressionMatch m2 = s_reDateTitle.match(sName); m2.hasMatch()) {
        sDate  = m2.captured(1);
        sTitle = m2.captured(2).trimmed();
    }
    else {
        sDate.clear();
        sTitle = sName;
    }
}

// New metadata.conf from the folder names (layout as in the original files; empty fields for manual editing)
QString GenerateMeta(const QString& sAuthorFolderName, const QString& sBookFolderName, const QStringList& aPages)
{
    QString sDate, sTitle;
    ParseFolderName(sBookFolderName, sDate, sTitle);
    const QString sCover = aPages.isEmpty() ? QString() : QFileInfo(aPages.first()).completeBaseName();
    return QStringLiteral("author_orig=\nauthor=%1\n\ntitle_orig=\ntitle=%2\n\nseries_orig=\nseries=\n\ntype=\ndate=%3\n\ncover=%4\nchapters=\n")
        .arg(sAuthorFolderName, sTitle, sDate, sCover);
}

// Key of a "key=value" line in lower case, or empty for empty lines, comments and lines without "="
QString LineKey(const QString& sRawLine)
{
    const QString sLine = sRawLine.trimmed();
    const qsizetype nPos = sLine.indexOf(QLatin1Char('='));
    if (nPos <= 0 || sLine.startsWith(QLatin1Char(';')) || sLine.startsWith(QLatin1Char('#')))
        return {};
    return sLine.left(nPos).trimmed().toLower();
}

// "ch12_title" -> 12 and "title"; -1 if the key is not a chapter key
int ChapterOfKey(const QString& sKey, QString* pField = nullptr)
{
    static const QRegularExpression s_re(QStringLiteral(R"(^ch(\d+)_(.+)$)"));
    const QRegularExpressionMatch m = s_re.match(sKey);
    if (!m.hasMatch())
        return -1;
    if (pField)
        *pField = m.captured(2);
    return m.captured(1).toInt();
}

// Adjusts the chapter entries of an existing metadata.conf to "chapters=N" (only if N is a number):
//  - entries of chapters > N are removed (all chN_* keys, also unknown ones)
//  - missing chN_start/cover/title_orig/title of chapters 1..N are added with empty values.
//    Within a chapter they are inserted in this order next to the existing keys; a missing chapter
//    is added as a block after the previous chapter (chapter 1: after "chapters=").
// All other lines stay unchanged. The file is only written if something changed.
bool UpdateChapterEntries(const QString& sPath, QStringList* pErrors)
{
    static const QStringList s_aFields = { QStringLiteral("start"), QStringLiteral("cover"),
                                           QStringLiteral("title_orig"), QStringLiteral("title") };
    const QString sOriginal = ReadText(sPath);
    QStringList aLines = sOriginal.split(QLatin1Char('\n'));
    if (!aLines.isEmpty() && aLines.last().isEmpty())
        aLines.removeLast();    // final line break

    int nChapters = -1;
    int nChaptersLine = -1;
    for (int i = 0; i < aLines.size(); ++i) {
        if (LineKey(aLines.at(i)) == QLatin1String("chapters")) {
            bool bOk = false;
            const int n = aLines.at(i).section(QLatin1Char('='), 1).trimmed().toInt(&bOk);
            nChapters = (bOk && n >= 0) ? n : -1;
            nChaptersLine = i;
        }
    }
    if (nChapters < 0)
        return true;            // empty or invalid: leave the file alone

    bool bChanged = false;
    for (QString& sLine : aLines) {
        if (sLine.endsWith(QLatin1Char('\r'))) {    // LF only
            sLine.chop(1);
            bChanged = true;
        }
    }

    // ---- Remove chapters beyond N
    for (int i = aLines.size() - 1; i >= 0; --i) {
        if (ChapterOfKey(LineKey(aLines.at(i))) > nChapters) {
            aLines.removeAt(i);
            if (i < nChaptersLine)
                --nChaptersLine;
            bChanged = true;
        }
    }

    auto linesOf = [&aLines](int nChapter) {
        QList<int> a;
        for (int i = 0; i < aLines.size(); ++i) {
            if (ChapterOfKey(LineKey(aLines.at(i))) == nChapter)
                a.append(i);
        }
        return a;
    };
    auto isBlank = [&aLines](int i) { return i < 0 || i >= aLines.size() || aLines.at(i).trimmed().isEmpty(); };

    // ---- Add missing entries of chapters 1..N
    for (int n = 1; n <= nChapters; ++n) {
        const QString sPrefix = QStringLiteral("ch%1_").arg(n);
        const QList<int> aChapterLines = linesOf(n);

        if (aChapterLines.isEmpty()) {
            // New block after the previous chapter or after "chapters="
            const QList<int> aPrevious = n > 1 ? linesOf(n - 1) : QList<int>();
            int nPos = aPrevious.isEmpty() ? nChaptersLine + 1 : aPrevious.last() + 1;
            QStringList aBlock;
            aBlock << QString();
            for (const QString& sField : s_aFields)
                aBlock << sPrefix + sField + QLatin1Char('=');
            if (!isBlank(nPos))
                aBlock << QString();
            if (isBlank(nPos - 1))
                aBlock.removeFirst();   // there is already a blank line before it
            for (const QString& sLine : std::as_const(aBlock))
                aLines.insert(nPos++, sLine);
            bChanged = true;
            continue;
        }

        // Insert each missing field after the preceding field of the same chapter (or before the first line)
        for (int f = 0; f < s_aFields.size(); ++f) {
            const QList<int> aCurrent = linesOf(n);
            QHash<QString, int> mapPos;
            for (int i : aCurrent) {
                QString sField;
                ChapterOfKey(LineKey(aLines.at(i)), &sField);
                mapPos.insert(sField, i);
            }
            if (mapPos.contains(s_aFields.at(f)))
                continue;
            int nPos = aCurrent.first();
            for (int g = f - 1; g >= 0; --g) {
                if (mapPos.contains(s_aFields.at(g))) {
                    nPos = mapPos.value(s_aFields.at(g)) + 1;
                    break;
                }
            }
            aLines.insert(nPos, sPrefix + s_aFields.at(f) + QLatin1Char('='));
            bChanged = true;
        }
    }

    if (!bChanged)
        return true;

    // Collapse consecutive blank lines (e.g. left over after removing a chapter block)
    for (int i = aLines.size() - 1; i > 0; --i) {
        if (aLines.at(i).trimmed().isEmpty() && aLines.at(i - 1).trimmed().isEmpty())
            aLines.removeAt(i);
    }

    if (WriteText(sPath, aLines.join(QLatin1Char('\n')) + QLatin1Char('\n')))
        return true;
    if (pErrors)
        pErrors->append(QObject::tr("Could not write file: \"%1\"").arg(sPath));
    return false;
}

QString FirstNonEmpty(std::initializer_list<QString> a)
{
    for (const QString& s : a) {
        if (!s.trimmed().isEmpty())
            return s.trimmed();
    }
    return {};
}

SBook LoadBook(const QString& sMangaDir, const QString& sBookPath, QStringList* pErrors)
{
    const QFileInfo fiBook(sBookPath);
    const QString sBookFolderName = fiBook.fileName();
    const QString sAuthorFolderName = QFileInfo(fiBook.path()).fileName();

    SBook book;
    book.sBookPath = sBookPath;
    book.sThumbMarker = sAuthorFolderName + QLatin1Char('_') + sBookFolderName;

    // ---- Pages
    for (const QString& sFile : SortedEntries(sBookPath, QDir::Files)) {
        if (MangaLibrary::IsImageFile(sFile))
            book.aPages.append(sFile);
    }

    // ---- metadata.conf: read it, or create it for an unpacked book at the level <Manga>/<Author>/<Book>
    const QString sMetaPath = sBookPath + QLatin1Char('/') + MangaLibrary::kMetaFileName;
    if (QFileInfo(sMetaPath).isFile()) {
        book.sMetaPath = sMetaPath;
        UpdateChapterEntries(sMetaPath, pErrors);
    }
    else if (fiBook.isDir() && QDir::cleanPath(QFileInfo(fiBook.path()).path()) == QDir::cleanPath(sMangaDir)) {
        if (WriteText(sMetaPath, GenerateMeta(sAuthorFolderName, sBookFolderName, book.aPages)))
            book.sMetaPath = sMetaPath;
        else if (pErrors)
            pErrors->append(QObject::tr("Could not create file: \"%1\"").arg(sMetaPath));
    }
    if (!book.sMetaPath.isEmpty())
        book.meta = MangaLibrary::ReadMeta(book.sMetaPath);

    // ---- Display values: metadata.conf (English preferred), then the folder names (same rules as for a new file)
    QString sFolderDate, sFolderTitle;
    ParseFolderName(sBookFolderName, sFolderDate, sFolderTitle);
    book.sAuthor = FirstNonEmpty({ book.meta.sAuthor, book.meta.sAuthorOrig, sAuthorFolderName });
    book.sTitle  = FirstNonEmpty({ book.meta.sTitle, book.meta.sTitleOrig, sFolderTitle, sBookFolderName });
    book.sDate   = FirstNonEmpty({ book.meta.sDate, sFolderDate });

    // ---- Cover: "cover=" from the meta file, otherwise the first page
    if (!book.meta.sCover.isEmpty())
        book.sCoverPath = book.PagePath(book.meta.sCover);
    if (book.sCoverPath.isEmpty() && !book.aPages.isEmpty())
        book.sCoverPath = sBookPath + QLatin1Char('/') + book.aPages.first();

    return book;
}

} // namespace

QString FieldKey(EField eField)
{
    switch (eField) {
        case EField::None:   return QStringLiteral("|");
        case EField::Author: return QStringLiteral("Author");
        case EField::Title:  return QStringLiteral("Title");
        case EField::Type:   return QStringLiteral("Type");
        case EField::Date:   return QStringLiteral("Date");
        case EField::DateYM: return QStringLiteral("DateYM");
    }
    return {};
}

EField FieldFromKey(const QString& sKey, EField eFallback)
{
    for (EField e : { EField::None, EField::Author, EField::Title, EField::Type, EField::Date, EField::DateYM }) {
        if (FieldKey(e) == sKey)
            return e;
    }
    return eFallback;
}

QString SBook::Field(EField eField) const
{
    switch (eField) {
        case EField::None:   return {};
        case EField::Author: return sAuthor;
        case EField::Title:  return sTitle;
        case EField::Type:   return meta.sType;
        case EField::Date:
        case EField::DateYM: return sDate;
    }
    return {};
}

QString SBook::PagePath(const QString& sBaseName) const
{
    for (const QString& sPage : aPages) {
        if (QFileInfo(sPage).completeBaseName().compare(sBaseName, Qt::CaseInsensitive) == 0)
            return sBookPath + QLatin1Char('/') + sPage;
    }
    return {};
}

QString SBook::ChapterCoverPath(int nChapter) const
{
    if (nChapter < 0 || nChapter >= meta.aChapters.size())
        return {};
    const SChapter& ch = meta.aChapters.at(nChapter);
    QString sPath = ch.sCover.isEmpty() ? QString() : PagePath(ch.sCover);
    if (sPath.isEmpty())
        sPath = PagePath(ch.sStart);
    return sPath;
}

namespace MangaLibrary {

const QString kMetaFileName = QStringLiteral("metadata.conf");

QList<SBook> Load(const QString& sMangaDir, QStringList* pErrors)
{
    // Collect all book folders and sort them naturally by full path
    QStringList aFolders;
    for (const QString& sAuthor : SortedEntries(sMangaDir, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (sAuthor.startsWith(QLatin1Char('_')))
            continue;
        const QString sAuthorPath = sMangaDir + QLatin1Char('/') + sAuthor;
        for (const QString& sBook : SortedEntries(sAuthorPath, QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (sBook.startsWith(QLatin1Char('_')))     // ignore folders starting with "_"
                continue;
            aFolders.append(sAuthorPath + QLatin1Char('/') + sBook);
        }
    }

    QList<SBook> aBooks;
    aBooks.reserve(aFolders.size());
    for (const QString& sFolder : std::as_const(aFolders))
        aBooks.append(LoadBook(sMangaDir, sFolder, pErrors));
    return aBooks;
}

SBookMeta ReadMeta(const QString& sMetaPath)
{
    SBookMeta meta;
    if (sMetaPath.isEmpty())
        return meta;

    static const QRegularExpression s_reChapter(QStringLiteral(R"(^ch(\d+)_(start|cover|title_orig|title)$)"));
    QMap<int, SChapter> mapChapters;
    int nChapters = -1;

    const QStringList aLines = ReadText(sMetaPath).split(QLatin1Char('\n'));
    for (const QString& sRawLine : aLines) {
        const QString sLine = sRawLine.trimmed();
        const qsizetype nPos = sLine.indexOf(QLatin1Char('='));
        if (nPos <= 0 || sLine.startsWith(QLatin1Char(';')) || sLine.startsWith(QLatin1Char('#')))
            continue;
        const QString sKey = sLine.left(nPos).trimmed().toLower();
        const QString sValue = sLine.mid(nPos + 1).trimmed();

        if      (sKey == QLatin1String("author_orig")) meta.sAuthorOrig = sValue;
        else if (sKey == QLatin1String("author"))      meta.sAuthor = sValue;
        else if (sKey == QLatin1String("title_orig"))  meta.sTitleOrig = sValue;
        else if (sKey == QLatin1String("title"))       meta.sTitle = sValue;
        else if (sKey == QLatin1String("series_orig")) meta.sSeriesOrig = sValue;
        else if (sKey == QLatin1String("series"))      meta.sSeries = sValue;
        else if (sKey == QLatin1String("type"))        meta.sType = sValue;
        else if (sKey == QLatin1String("date"))        meta.sDate = sValue;
        else if (sKey == QLatin1String("cover"))       meta.sCover = sValue;
        else if (sKey == QLatin1String("chapters")) {
            bool bOk = false;
            const int n = sValue.toInt(&bOk);
            nChapters = (bOk && n >= 0) ? n : -1;     // empty: no limit
        }
        else if (const QRegularExpressionMatch m = s_reChapter.match(sKey); m.hasMatch()) {
            SChapter& ch = mapChapters[m.captured(1).toInt()];
            const QString sField = m.captured(2);
            if      (sField == QLatin1String("start"))      ch.sStart = sValue;
            else if (sField == QLatin1String("cover"))      ch.sCover = sValue;
            else if (sField == QLatin1String("title_orig")) ch.sTitleOrig = sValue;
            else                                            ch.sTitle = sValue;
        }
    }

    // "chapters=" limits the list, if given (chapter numbers start at 1)
    for (auto it = mapChapters.cbegin(); it != mapChapters.cend(); ++it) {
        if (nChapters >= 0 && it.key() > nChapters)
            break;
        meta.aChapters.append(it.value());
    }
    return meta;
}

bool IsImageFile(const QString& sFileName)
{
    static const QSet<QString> s_setExt = [] {
        QSet<QString> set;
        for (const QByteArray& ba : QImageReader::supportedImageFormats())
            set.insert(QString::fromLatin1(ba).toLower());
        return set;
    }();
    return s_setExt.contains(QFileInfo(sFileName).suffix().toLower());
}

int CompareLogical(const QString& s1, const QString& s2)
{
    return LogicalCollator().compare(s1, s2);
}

} // namespace MangaLibrary
