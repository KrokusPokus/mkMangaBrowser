#ifndef CONFIG_H
#define CONFIG_H

#include <QString>

class QSettings;

// Settings file ~/.config/mkProjects/mkMangaBrowser.conf. For now it holds a single value:
//   [Paths]
//   MangaDir=<archive folder: one subfolder per author, each with one subfolder per book>
class CConfig
{
public:
    void Load(QSettings& settings);

    bool HasValidMangaDir() const;
    void SetMangaDir(QSettings& settings, const QString& sDir);

    QString sMangaDir;
};

#endif // CONFIG_H
