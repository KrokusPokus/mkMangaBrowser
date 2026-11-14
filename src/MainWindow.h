#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "Config.h"
#include "MangaLibrary.h"
#include "ViewSettings.h"

#include <QMainWindow>
#include <QSettings>

#include <functional>
#include <memory>

class CBookGridView;
class CThumbnailCache;
class QLineEdit;
class QMenu;

class CMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CMainWindow(QSettings& settings, const CConfig& config, QWidget* pParent = nullptr);
    ~CMainWindow() override;

    void LoadLibrary();     // (re)load books; F5

protected:
    void closeEvent(QCloseEvent* pEvent) override;
    void changeEvent(QEvent* pEvent) override;
    bool eventFilter(QObject* pObj, QEvent* pEvent) override;

private:
    void ApplyView(bool bRestoreScroll);
    void UpdateTitle();
    void SaveSettings();
    void RestoreWindow();
    void InitStateFile();
    void UpdateWidgetStyles();
    void ShowFilterBox();
    void HideFilterBox();
    void OnFilterBoxChange(const QString& sText);

    void ShowBackgroundMenu(const QPoint& ptGlobal);
    void ShowBookMenu(int nBook, const QPoint& ptGlobal);
    void AddFieldItems(QMenu* pMenu, const QList<QPair<EField, QString>>& aItems, EField eCurrent,
                       const std::function<void(EField)>& fnSelect);

    void OpenBook(int nBook, const QString& sPagePath = {});
    void OpenFile(const QString& sPath);
    void EditMeta(int nBook);
    void WebSearch(int nBook);
    void OpenFolder(int nBook);
    bool openWithSystemDefault(const QUrl &url);
    void ShowHelp();
    void OnThumbnailFailed(const QString& sMarker, const QString& sCoverPath);

    QSettings&          m_settings;             // configuration (~/.config/mkMangaBrowser/mkMangaBrowser.ini)
    QString             m_stateFileFullPath;    // state: view, window, scroll position (StateLocation/state.ini)
    std::unique_ptr<QSettings> m_pState;
    CConfig             m_config;
    SViewSettings       m_view;
    QList<SBook>        m_aBooks;
    QList<SGroup>       m_aGroups;
    CThumbnailCache*    m_pCache = nullptr;
    CBookGridView*      m_pView = nullptr;
    QWidget*            m_pTopControlsContainerWidget = nullptr;   // filter bar (Ctrl+F), as in mkFolderWidget
    QLineEdit*          m_pFilterEdit = nullptr;
    bool                m_bStyleDark = false;
    bool                m_bStyleSet = false;
    bool                m_bFirstShow = true;
};

#endif // MAINWINDOW_H
