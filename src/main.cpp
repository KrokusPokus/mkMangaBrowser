#include "Config.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mkMangaBrowser"));
    QApplication::setApplicationVersion(QStringLiteral(MKMANGABROWSER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("mkMangaBrowser"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/mkMangaBrowser.svg")));

    QTranslator translatorQt;
    if (translatorQt.load(QLocale::system(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&translatorQt);
    QTranslator translator;
    if (translator.load(QLocale::system(), QStringLiteral("mkMangaBrowser"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Browses a manga/comic collection by cover thumbnails."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption optManga(QStringLiteral("manga"),
                                      QCoreApplication::translate("main", "Use (and remember) this Manga folder."),
                                      QStringLiteral("folder"));
    parser.addOption(optManga);
    parser.process(app);

    // ~/.config/mkMangaBrowser/mkMangaBrowser.ini
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("mkMangaBrowser"), QStringLiteral("mkMangaBrowser"));
    CConfig config;
    config.Load(settings);

    if (parser.isSet(optManga))
        config.SetMangaDir(settings, QFileInfo(parser.value(optManga)).absoluteFilePath());

    if (!config.HasValidMangaDir()) {
        const QString sDir = QFileDialog::getExistingDirectory(nullptr,
            QCoreApplication::translate("main", "Select the Manga folder (contains one subfolder per author)"),
            config.sMangaDir.isEmpty() ? QDir::homePath() : config.sMangaDir);
        if (sDir.isEmpty())
            return 1;
        config.SetMangaDir(settings, sDir);
    }
    settings.sync();

    CMainWindow window(settings, config);
    window.show();
    // Load only after showing: then the final window width for line wrapping is known
    QTimer::singleShot(0, &window, &CMainWindow::LoadLibrary);
    return app.exec();
}
