#include "MangaLibrary.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

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

// The meta files may come from the Windows era: honor a BOM (UTF-8/UTF-16), otherwise UTF-8
QString ReadText(const QString& sPath)
{
    QFile f(sPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray ba = f.readAll();
    const auto eEncoding = QStringConverter::encodingForData(ba).value_or(QStringConverter::Utf8);
    QStringDecoder decoder(eEncoding);
    return decoder.decode(ba);
}

// "Kishiro Yukito (1991-09-19) Battle Angel Alita v01" -> author, date, title
void ParseFolderName(const QString& sName, QString& sAuthor, QString& sDate, QString& sTitle)
{
    static const QRegularExpression s_re(QStringLiteral(R"(^(.*?)\s*\(([^)]*)\)\s*(.*)$)"));
    const QRegularExpressionMatch m = s_re.match(sName);
    if (m.hasMatch()) {
        sAuthor = m.captured(1).trimmed();
        sDate   = m.captured(2).trimmed();
        sTitle  = m.captured(3).trimmed();
    }
    else {
        sAuthor.clear();
        sDate.clear();
        sTitle = sName;
    }
}

QString FirstNonEmpty(std::initializer_list<QString> a)
{
    for (const QString& s : a) {
        if (!s.trimmed().isEmpty())
            return s.trimmed();
    }
    return {};
}

SBook LoadBook(const QString& sBookPath)
{
    const QFileInfo fiBook(sBookPath);
    const QString sBookFolderName = fiBook.fileName();
    const QString sAuthorFolderName = QFileInfo(fiBook.path()).fileName();

    SBook book;
    book.sBookPath = sBookPath;
    book.sThumbMarker = sAuthorFolderName + QLatin1Char('_') + sBookFolderName;

    // ---- Pages and meta file (first *.txt)
    for (const QString& sFile : SortedEntries(sBookPath, QDir::Files)) {
        if (MangaLibrary::IsImageFile(sFile))
            book.aPages.append(sFile);
        else if (book.sMetaPath.isEmpty() && sFile.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
            book.sMetaPath = sBookPath + QLatin1Char('/') + sFile;
    }
    if (!book.sMetaPath.isEmpty())
        book.meta = MangaLibrary::ReadMeta(book.sMetaPath);

    // ---- Display values: meta file (English preferred), then folder name
    QString sFolderAuthor, sFolderDate, sFolderTitle;
    ParseFolderName(sBookFolderName, sFolderAuthor, sFolderDate, sFolderTitle);
    book.sAuthor = FirstNonEmpty({ book.meta.sAuthorEn, book.meta.sAuthorOrig, sFolderAuthor, sAuthorFolderName });
    book.sTitle  = FirstNonEmpty({ book.meta.sNameEn, book.meta.sNameOrig, sFolderTitle, sBookFolderName });
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

namespace MangaLibrary {

QList<SBook> Load(const QString& sMangaDir)
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
        aBooks.append(LoadBook(sFolder));
    return aBooks;
}

SBookMeta ReadMeta(const QString& sMetaPath)
{
    SBookMeta meta;
    if (sMetaPath.isEmpty())
        return meta;

    static const QRegularExpression s_reChapter(QStringLiteral(R"(^ch(\d+)_(start|title_orig|title_en)$)"));
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
        else if (sKey == QLatin1String("author_en"))   meta.sAuthorEn = sValue;
        else if (sKey == QLatin1String("name_orig"))   meta.sNameOrig = sValue;
        else if (sKey == QLatin1String("name_en"))     meta.sNameEn = sValue;
        else if (sKey == QLatin1String("type"))        meta.sType = sValue;
        else if (sKey == QLatin1String("date"))        meta.sDate = sValue;
        else if (sKey == QLatin1String("cover"))       meta.sCover = sValue;
        else if (sKey == QLatin1String("chapters"))    nChapters = sValue.toInt();
        else if (const QRegularExpressionMatch m = s_reChapter.match(sKey); m.hasMatch()) {
            SChapter& ch = mapChapters[m.captured(1).toInt()];
            const QString sField = m.captured(2);
            if      (sField == QLatin1String("start"))      ch.sStart = sValue;
            else if (sField == QLatin1String("title_orig")) ch.sTitleOrig = sValue;
            else                                            ch.sTitleEn = sValue;
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
