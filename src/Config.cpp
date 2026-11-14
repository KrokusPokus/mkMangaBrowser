#include "Config.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {
const QString kKeyMangaDir = QStringLiteral("Paths/MangaDir");
} // namespace

void CConfig::Load(QSettings& settings)
{
    sMangaDir = settings.value(kKeyMangaDir).toString();
}

bool CConfig::HasValidMangaDir() const
{
    return !sMangaDir.isEmpty() && QFileInfo(sMangaDir).isDir();
}

void CConfig::SetMangaDir(QSettings& settings, const QString& sDir)
{
    sMangaDir = QDir::cleanPath(sDir);
    settings.setValue(kKeyMangaDir, sMangaDir);
}
