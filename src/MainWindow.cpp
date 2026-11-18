#include "MainWindow.h"

#include "BookGridView.h"
#include "ThumbnailCache.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kThumbHeight = 172;

} // namespace

CMainWindow::CMainWindow(QSettings& settings, const CConfig& config, QWidget* pParent)
    : QMainWindow(pParent)
    , m_settings(settings)
    , m_config(config)
{
    InitStateFile();

    // ---- saved view
    if (m_pState) {
        m_pState->beginGroup(QStringLiteral("View"));
        m_view.eGroup = FieldFromKey(m_pState->value(QStringLiteral("GroupMode")).toString(), EField::Author);
        m_view.eSort  = FieldFromKey(m_pState->value(QStringLiteral("SortMode")).toString(), EField::Date);
        m_view.eText  = FieldFromKey(m_pState->value(QStringLiteral("TextMode")).toString(), EField::Title);
        m_pState->endGroup();
    }
    if (m_view.eGroup == EField::None)
        m_view.eGroup = EField::Author;
    if (m_view.eSort == EField::None || m_view.eSort == EField::DateYM)
        m_view.eSort = EField::Date;

    // ---- Layout as in mkFolderWidget: filter bar (initially hidden) above the view
    auto* pCentralWidget = new QWidget(this);
    setCentralWidget(pCentralWidget);
    auto* pMainLayout = new QVBoxLayout(pCentralWidget);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    pMainLayout->setSpacing(0);

    m_pTopControlsContainerWidget = new QWidget();
    auto* pTopControlsHBoxLayout = new QHBoxLayout(m_pTopControlsContainerWidget);
    pTopControlsHBoxLayout->setContentsMargins(0, 0, 0, 0);
    pTopControlsHBoxLayout->setSpacing(0);

    m_pFilterEdit = new QLineEdit();
    pTopControlsHBoxLayout->addWidget(m_pFilterEdit);

    pMainLayout->addWidget(m_pTopControlsContainerWidget);
    m_pTopControlsContainerWidget->hide();

    m_pView = new CBookGridView(this);
    pMainLayout->addWidget(m_pView);

    connect(m_pFilterEdit, &QLineEdit::textChanged, this, &CMainWindow::OnFilterBoxChange);
    UpdateWidgetStyles();
    qApp->installEventFilter(this);

    connect(m_pView, &CBookGridView::OpenBookRequested, this, [this](int nBook) { OpenBook(nBook); });
    connect(m_pView, &CBookGridView::OpenChapterRequested, this, [this](int nBook, int nChapter) {
        // Open at the chapter's start page (not its title image); without one at the title image
        const SBook& book = m_aBooks.at(nBook);
        QString sPage = book.PagePath(book.meta.aChapters.at(nChapter).sStart);
        if (sPage.isEmpty())
            sPage = book.ChapterCoverPath(nChapter);
        OpenBook(nBook, sPage);
    });
    connect(m_pView, &CBookGridView::ContextMenuRequested, this, [this](int nBook, const QPoint& ptGlobal) {
        if (nBook < 0)
            ShowBackgroundMenu(ptGlobal);
        else
            ShowBookMenu(nBook, ptGlobal);
    });
    connect(m_pView, &CBookGridView::EditMetaRequested, this, &CMainWindow::EditMeta);
    connect(m_pView, &CBookGridView::WebSearchRequested, this, &CMainWindow::WebSearch);
    connect(m_pView, &CBookGridView::OpenFolderRequested, this, &CMainWindow::OpenFolder);
    connect(m_pView, &CBookGridView::ReloadRequested, this, &CMainWindow::LoadLibrary);
    connect(m_pView, &CBookGridView::HelpRequested, this, &CMainWindow::ShowHelp);
    connect(m_pView, &CBookGridView::GroupHeaderActivated, this, &CMainWindow::AddGroupFilter);

    RestoreWindow();
}

CMainWindow::~CMainWindow()
{
    qApp->removeEventFilter(this);
}

// =============================================================================================== Loading / display

void CMainWindow::LoadLibrary()
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const QPoint ptScroll = m_pView->ScrollPosition();

    // Thumbnail cache matching the current screen scaling. Only recreate it when the scaling changed;
    // otherwise it is enough to discard the images in memory (the cache detects changed covers itself
    // by size and modification time).
    const qreal dDpr = devicePixelRatioF();
    if (m_pCache && qFuzzyCompare(m_pCache->DevicePixelRatio(), dDpr)) {
        m_pCache->ClearMemory();
    }
    else {
        m_pView->SetContent(nullptr, {}, m_view.eText, nullptr);   // view must no longer use the old cache
        if (m_pCache)
            m_pCache->SaveIndex();
        delete m_pCache;
        m_pCache = new CThumbnailCache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation), kThumbHeight, dDpr, this);
        connect(m_pCache, &CThumbnailCache::ThumbnailFailed, this, &CMainWindow::OnThumbnailFailed);
    }

    QStringList aErrors;
    m_aBooks = MangaLibrary::Load(m_config.sMangaDir, &aErrors);

    const bool bFirstLoad = m_bFirstShow;   // ApplyView() restores the saved position the first time
    ApplyView(false);
    if (!bFirstLoad)
        m_pView->SetScrollPosition(ptScroll);
    m_pCache->SaveIndex();
    QGuiApplication::restoreOverrideCursor();

    if (!aErrors.isEmpty())
        QMessageBox::warning(this, tr("Manga Browser"), aErrors.join(QLatin1Char('\n')));

    // Empty library: usually a wrong folder -> explain and let the user choose another one
    if (m_aBooks.isEmpty()) {
        const auto eAnswer = QMessageBox::question(this, tr("Manga Browser"),
            tr("No books found in\n\t%1\n\nThe Manga folder must contain one subfolder per author, "
               "each with one subfolder per book (e.g. \"Manga/Kishiro Yukito/Kishiro Yukito (1991-09-19) Battle Angel Alita v01\").\n\n"
               "Select a different folder?")
                .arg(m_config.sMangaDir),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (eAnswer != QMessageBox::Yes)
            return;
        const QString sDir = QFileDialog::getExistingDirectory(this, tr("Select the Manga folder"), m_config.sMangaDir);
        if (sDir.isEmpty())
            return;
        m_config.SetMangaDir(m_settings, sDir);
        m_settings.sync();
        QTimer::singleShot(0, this, &CMainWindow::LoadLibrary);
    }
}

void CMainWindow::ApplyView(bool bRestoreScroll)
{
    const QPoint ptScroll = m_pView->ScrollPosition();
    ViewLogic::ApplyFilter(m_aBooks, m_view);
    m_aGroups = ViewLogic::BuildGroups(m_aBooks, m_view);
    // Chapter view as soon as the automatic grouping after group filters ended up at "none"
    const bool bChapterMode = m_view.eGroup == EField::None && !m_view.aGroupFilters.isEmpty();
    m_pView->SetContent(&m_aBooks, m_aGroups, m_view.eText, m_pCache, bChapterMode);

    if (m_bFirstShow) {
        m_bFirstShow = false;
        if (m_pState) {
            m_pState->beginGroup(QStringLiteral("Window"));
            const QPoint ptSaved(m_pState->value(QStringLiteral("ScrollX"), 0).toInt(), m_pState->value(QStringLiteral("ScrollY"), 0).toInt());
            m_pState->endGroup();
            m_pView->SetScrollPosition(ptSaved);
        }
    }
    else if (bRestoreScroll) {
        m_pView->SetScrollPosition(ptScroll);
    }
    UpdateTitle();
}

// =============================================================================================== Filter bar

void CMainWindow::ShowFilterBox()
{
    if (m_pTopControlsContainerWidget->isHidden())
        m_pTopControlsContainerWidget->show();
    if (!m_pFilterEdit->hasFocus())
        m_pFilterEdit->setFocus();
    m_pFilterEdit->selectAll();
}

void CMainWindow::HideFilterBox()
{
    m_pFilterEdit->clear();
    m_pTopControlsContainerWidget->hide();
    m_pView->setFocus();
}

void CMainWindow::OnFilterBoxChange(const QString& sText)
{
    if (sText == m_view.sTextFilter)
        return;
    m_view.sTextFilter = sText;
    ApplyView(false);
    m_pView->SetScrollPosition({});     // matches are at the top
}

void CMainWindow::UpdateWidgetStyles()
{
    // Same look as the filter bar in mkFolderWidget
    static const QString lineEditStyleDark = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid #555555;"
        "  padding: 0px;"
        "  padding-left: 4px;"
        "  background-color: #1a1a1a;"
        "  color: #ffffff;"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid #666666;"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid #222222;"
        "  color: #666666;"
        "}"
        );

    static const QString lineEditStyleLight = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  padding: 0px;"
        "  padding-left: 4px;"
        "  background-color: palette(window);"
        "  color: palette(window-text);"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid #ff0000;"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid #222222;"
        "  color: #666666;"
        "}"
        );

    const bool bDark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    if (m_bStyleSet && bDark == m_bStyleDark)
        return;
    m_bStyleSet = true;
    m_bStyleDark = bDark;
    m_pFilterEdit->setStyleSheet(bDark ? lineEditStyleDark : lineEditStyleLight);
}

void CMainWindow::changeEvent(QEvent* pEvent)
{
    switch (pEvent->type()) {
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::ThemeChange:
            m_bStyleSet = false;    // palette (accent color) can also change without a light/dark switch
            UpdateWidgetStyles();
            break;
        default:
            break;
    }
    QMainWindow::changeEvent(pEvent);
}

// Installed on qApp (as in mkFolderWidget)
bool CMainWindow::eventFilter(QObject* pObj, QEvent* pEvent)
{
    // Mouse button "Back" (XButton1) anywhere in the window: remove the last group filter
    if (pEvent->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(pEvent)->button() == Qt::BackButton) {
        auto* pWidget = qobject_cast<QWidget*>(pObj);
        if (pWidget && pWidget->window() == this) {
            RemoveLastGroupFilter();
            return true;
        }
    }
    if (pEvent->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(pObj, pEvent);

    auto* pKeyEvent = static_cast<QKeyEvent*>(pEvent);
    auto* pTargetWidget = qobject_cast<QWidget*>(pObj);
    if (!pTargetWidget || pTargetWidget->window() != this)
        return QMainWindow::eventFilter(pObj, pEvent);     // not our window

    // Window-wide: Ctrl+F opens the filter bar
    if (pKeyEvent->modifiers() == Qt::ControlModifier && pKeyEvent->key() == Qt::Key_F) {
        ShowFilterBox();
        return true;
    }

    // Keys in the filter bar
    if (pTargetWidget == m_pFilterEdit) {
        if (pKeyEvent->key() == Qt::Key_Escape) {
            HideFilterBox();
            return true;
        }
        if (pKeyEvent->key() == Qt::Key_Backspace && pKeyEvent->modifiers() == Qt::ControlModifier) {
            if (m_pFilterEdit->text().isEmpty())
                HideFilterBox();
            else
                m_pFilterEdit->clear();
            return true;
        }
        return QMainWindow::eventFilter(pObj, pEvent);
    }

    // Keys in the view
    if (pTargetWidget == m_pView || pTargetWidget->parent() == m_pView) {
        if (pKeyEvent->key() == Qt::Key_Backspace) {
            // Open filter bar: back into it; otherwise remove the last group filter
            if (!m_pTopControlsContainerWidget->isHidden()) {
                m_pFilterEdit->setFocus();
                if (pKeyEvent->modifiers() == Qt::ControlModifier)
                    m_pFilterEdit->selectAll();
            }
            else {
                RemoveLastGroupFilter();
            }
            return true;
        }
        if (pKeyEvent->key() == Qt::Key_Escape && pKeyEvent->modifiers() == Qt::NoModifier) {
            // Close an open filter bar first, otherwise the window
            if (!m_pTopControlsContainerWidget->isHidden())
                HideFilterBox();
            else
                close();
            return true;
        }
    }
    return QMainWindow::eventFilter(pObj, pEvent);
}

// "Manga Browser - Author "Kei Sanbe", Type "Doujinshi" (12 of 340 Books)"
void CMainWindow::UpdateTitle()
{
    int nShown = 0;
    for (const SBook& book : std::as_const(m_aBooks))
        nShown += book.bVisible ? 1 : 0;

    QStringList aFilters;
    for (const SGroupFilter& f : std::as_const(m_view.aGroupFilters))
        aFilters << tr("%1 \"%2\"").arg(FieldLabel(f.eField), f.sValue.isEmpty() ? tr("Unspecified") : f.sValue);
    if (const QString sText = m_view.sTextFilter.trimmed(); !sText.isEmpty())
        aFilters << tr("Text \"%1\"").arg(sText);

    if (aFilters.isEmpty())
        setWindowTitle(tr("Manga Browser - All Books (%1 total)").arg(m_aBooks.size()));
    else
        setWindowTitle(tr("Manga Browser - %1 (%2 of %3 Books)").arg(aFilters.join(QStringLiteral(", "))).arg(nShown).arg(m_aBooks.size()));
}

QString CMainWindow::FieldLabel(EField eField) const
{
    switch (eField) {
        case EField::None:   return tr("None");
        case EField::Author: return tr("Author");
        case EField::Title:  return tr("Title");
        case EField::Series: return tr("Series");
        case EField::Type:   return tr("Type");
        case EField::Date:   return tr("Year");
        case EField::DateYM: return tr("Year/Month");
    }
    return {};
}

// =============================================================================================== Group filters

// Double-click on a group header: its value becomes a filter, and the grouping switches to a field that
// still separates the remaining books
void CMainWindow::AddGroupFilter(int nGroup)
{
    if (nGroup < 0 || nGroup >= m_aGroups.size() || m_view.eGroup == EField::None)
        return;
    SGroupFilter filter;
    filter.eField = m_view.eGroup;
    filter.sValue = m_aGroups.at(nGroup).sValue;
    filter.eGroupBefore = m_view.eGroup;
    filter.ptScrollBefore = m_pView->ScrollPosition();
    m_view.aGroupFilters.append(filter);

    ViewLogic::ApplyFilter(m_aBooks, m_view);
    m_view.eGroup = ViewLogic::ChooseGrouping(m_aBooks, m_view, filter.eField);
    ApplyView(false);
    m_pView->SetScrollPosition({});
}

// Backspace / mouse button "Back": undo the last group filter including grouping and scroll position
bool CMainWindow::RemoveLastGroupFilter()
{
    if (m_view.aGroupFilters.isEmpty())
        return false;
    const SGroupFilter filter = m_view.aGroupFilters.takeLast();
    m_view.eGroup = filter.eGroupBefore;
    ApplyView(false);
    m_pView->SetScrollPosition(filter.ptScrollBefore);
    return true;
}

// =============================================================================================== Settings

void CMainWindow::InitStateFile()
{
    // State (view, window geometry, scroll position) separate from the configuration:
    // ~/.local/state/mkProjects/mkMangaBrowser_state.conf
    const QString statePath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)).filePath(QStringLiteral("mkProjects"));
    if (!QDir().mkpath(statePath)) { // creates the directory including subfolders
        return;
    }

    m_stateFileFullPath = QDir(statePath).filePath(QStringLiteral("mkMangaBrowser_state.conf"));
    m_pState = std::make_unique<QSettings>(m_stateFileFullPath, QSettings::IniFormat);
}

void CMainWindow::RestoreWindow()
{
    QByteArray baGeometry;
    if (m_pState) {
        m_pState->beginGroup(QStringLiteral("Window"));
        baGeometry = m_pState->value(QStringLiteral("Geometry")).toByteArray();
        m_pState->endGroup();
    }

    if (baGeometry.isEmpty() || !restoreGeometry(baGeometry)) {
        // Default: entire work area of the monitor
        if (QScreen* pScreen = QGuiApplication::primaryScreen())
            setGeometry(pScreen->availableGeometry());
        else
            resize(1280, 800);
    }
    if (width() < 192 || height() < 160)
        resize(std::max(width(), 192), std::max(height(), 160));
}

void CMainWindow::SaveSettings()
{
    if (!m_pState)
        return;

    m_pState->beginGroup(QStringLiteral("Window"));
    m_pState->setValue(QStringLiteral("Geometry"), saveGeometry());
    m_pState->setValue(QStringLiteral("ScrollX"), m_pView->ScrollPosition().x());
    m_pState->setValue(QStringLiteral("ScrollY"), m_pView->ScrollPosition().y());
    m_pState->endGroup();

    m_pState->beginGroup(QStringLiteral("View"));
    // With group filters the grouping was chosen automatically; the grouping before the first filter is saved
    const EField eGroup = m_view.aGroupFilters.isEmpty() ? m_view.eGroup : m_view.aGroupFilters.first().eGroupBefore;
    m_pState->setValue(QStringLiteral("GroupMode"), FieldKey(eGroup));
    m_pState->setValue(QStringLiteral("SortMode"), FieldKey(m_view.eSort));
    m_pState->setValue(QStringLiteral("TextMode"), FieldKey(m_view.eText));
    m_pState->endGroup();
    m_pState->sync();
}

void CMainWindow::closeEvent(QCloseEvent* pEvent)
{
    m_pView->HideInfoPopup();
    SaveSettings();
    if (m_pCache)
        m_pCache->SaveIndex();
    QMainWindow::closeEvent(pEvent);
}

// =============================================================================================== Background context menu

void CMainWindow::AddFieldItems(QMenu* pMenu, const QList<QPair<EField, QString>>& aItems, EField eCurrent,
                                const std::function<void(EField)>& fnSelect, const std::function<bool(EField)>& fnEnabled)
{
    for (const auto& [eField, sText] : aItems) {
        if (sText.isEmpty()) {
            pMenu->addSeparator();
            continue;
        }
        QAction* pAction = pMenu->addAction(sText);
        pAction->setCheckable(true);
        pAction->setChecked(eField == eCurrent);
        if (fnEnabled)
            pAction->setEnabled(fnEnabled(eField));
        const EField eSelected = eField;
        connect(pAction, &QAction::triggered, this, [fnSelect, eSelected]() { fnSelect(eSelected); });
    }
}

void CMainWindow::ShowBackgroundMenu(const QPoint& ptGlobal)
{
    QMenu menu(this);

    // ---- Group by
    QMenu* pGroup = menu.addMenu(tr("Group by"));
    // Fields with a group filter would only produce a single group
    AddFieldItems(pGroup, { { EField::Author, tr("Author") }, { EField::Series, tr("Series") }, { EField::Type, tr("Type") },
                            { EField::Title, tr("Title") }, { EField::None, {} },
                            { EField::Date, tr("Date (Year)") }, { EField::DateYM, tr("Date (Year/Month)") } },
                  m_view.eGroup, [this](EField e) { if (e != m_view.eGroup) { m_view.eGroup = e; ApplyView(true); } },
                  [this](EField e) { return !m_view.IsFiltered(e); });

    // ---- Sort by
    QMenu* pSort = menu.addMenu(tr("Sort by"));
    AddFieldItems(pSort, { { EField::Date, tr("Date") }, { EField::None, {} }, { EField::Title, tr("Title") },
                           { EField::Author, tr("Author") }, { EField::Series, tr("Series") }, { EField::Type, tr("Type") } },
                  m_view.eSort, [this](EField e) { if (e != m_view.eSort) { m_view.eSort = e; ApplyView(true); } });

    // ---- Description
    QMenu* pText = menu.addMenu(tr("Description"));
    QAction* pNone = pText->addAction(tr("None"));
    pNone->setCheckable(true);
    pNone->setChecked(m_view.eText == EField::None);
    connect(pNone, &QAction::triggered, this, [this]() { if (m_view.eText != EField::None) { m_view.eText = EField::None; ApplyView(true); } });
    pText->addSeparator();
    AddFieldItems(pText, { { EField::Title, tr("Title") }, { EField::Author, tr("Author") }, { EField::Series, tr("Series") },
                           { EField::Type, tr("Type") },
                           { EField::None, {} }, { EField::Date, tr("Date") } },
                  m_view.eText, [this](EField e) { if (e != m_view.eText) { m_view.eText = e; ApplyView(true); } });

    menu.exec(ptGlobal);
}

// =============================================================================================== Book context menu

void CMainWindow::ShowBookMenu(int nBook, const QPoint& ptGlobal)
{
    const SBook& book = m_aBooks.at(nBook);
    QMenu menu(this);

    // ---- Open (first page) and chapters
    QAction* pOpen = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), tr("Open"));
    connect(pOpen, &QAction::triggered, this, [this, nBook]() { OpenBook(nBook); });
    pOpen->setEnabled(!book.aPages.isEmpty());

    QMenu* pChapters = menu.addMenu(tr("Chapters"));
    for (int i = 0; i < book.meta.aChapters.size(); ++i) {
        const SChapter& ch = book.meta.aChapters.at(i);
        const QString sPage = book.PagePath(ch.sStart);
        QString sTitle = ch.sTitle.isEmpty() ? ch.sTitleOrig : ch.sTitle;
        if (sTitle.isEmpty())
            sTitle = tr("Chapter %1").arg(i + 1);
        QAction* pAction = pChapters->addAction(QStringLiteral("%1\t%2").arg(sTitle, ch.sStart));
        pAction->setEnabled(!sPage.isEmpty());
        connect(pAction, &QAction::triggered, this, [this, nBook, sPage]() { OpenBook(nBook, sPage); });
    }
    pChapters->setEnabled(!pChapters->isEmpty());

    menu.addSeparator();

    // ---- Meta file, folder
    QAction* pEdit = menu.addAction(QIcon::fromTheme(QStringLiteral("document-edit")),
                                    book.sMetaPath.isEmpty() ? tr("Edit Info") : tr("Edit %1").arg(QFileInfo(book.sMetaPath).fileName()));
    pEdit->setEnabled(!book.sMetaPath.isEmpty());
    connect(pEdit, &QAction::triggered, this, [this, nBook]() { EditMeta(nBook); });

    QAction* pFolder = menu.addAction(QIcon::fromTheme(QStringLiteral("folder")), tr("Open Folder"));
    connect(pFolder, &QAction::triggered, this, [this, nBook]() { OpenFolder(nBook); });

    // ---- Web search
    menu.addSeparator();
    QAction* pGoogle = menu.addAction(QStringLiteral("Google"));
    connect(pGoogle, &QAction::triggered, this, [this, nBook]() { WebSearch(nBook); });

    menu.exec(ptGlobal);
}

// =============================================================================================== Actions

// Reading is done by the system image viewer (mkImageViewer), which handles folders and CBZ itself
void CMainWindow::OpenBook(int nBook, const QString& sPagePath)
{
    const SBook& book = m_aBooks.at(nBook);
    QString sPath = sPagePath;
    if (sPath.isEmpty() && !book.aPages.isEmpty())
        sPath = book.sBookPath + QLatin1Char('/') + book.aPages.first();
    if (sPath.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("No images found in\n\t\"%1\"").arg(book.sBookPath));
        return;
    }
    OpenFile(sPath);
}

void CMainWindow::OpenFile(const QString& sPath)
{
    if (QFileInfo::exists(sPath)) {
        openWithSystemDefault(QUrl::fromLocalFile(sPath));
    }
}

void CMainWindow::EditMeta(int nBook)
{
    OpenFile(m_aBooks.at(nBook).sMetaPath);
}

// Links always open in the system default browser
void CMainWindow::WebSearch(int nBook)
{
    const SBook& book = m_aBooks.at(nBook);
    const QString sName = book.meta.sTitleOrig.isEmpty() ? book.sTitle : book.meta.sTitleOrig;
    if (sName.isEmpty())
        return;
    QString sQuery = QString::fromUtf8(QUrl::toPercentEncoding(sName));
    sQuery.replace(QStringLiteral("%20"), QStringLiteral("+"));
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com/search?q=") + sQuery));
}

void CMainWindow::OpenFolder(int nBook)
{
    // Ctrl+L without a book under the mouse -> Manga main folder
    const QString sPath = nBook >= 0 ? m_aBooks.at(nBook).sBookPath : m_config.sMangaDir;
    if (QFileInfo(sPath).isDir())
        openWithSystemDefault(QUrl::fromLocalFile(sPath));
}

void CMainWindow::ShowHelp()
{
    QMessageBox::information(this, tr("Hotkeys"),
        tr("Ctrl+E\tEdit the info file of the book under the mouse\n"
           "Ctrl+G\tSearch on Google\n"
           "Ctrl+L\tOpen the folder of the book under the mouse\n"
           "Ctrl+F\tFilter (Esc closes the filter line)\n"
           "A-Z, 0-9\tJump to group\n"
           "Double-click\tOpen book\n"
           "Double-click on a group header\tShow only this group (filter)\n"
           "Backspace, mouse button Back\tRemove the last group filter\n\n"
           "F5\tReload Manga Browser\n"
           "Esc\tClose"));
}

void CMainWindow::OnThumbnailFailed(const QString& sMarker, const QString& sCoverPath)
{
    qWarning("Could not create thumbnail \"%s\" from \"%s\"", qPrintable(sMarker), qPrintable(sCoverPath));
}

// Opens a file or URL using the system's default application.
// We deliberately avoid using QDesktopServices directly: in that case, Qt—even outside of
//  a sandbox—uses xdg-desktop-portal, which ignores the user's configured default for local files
// (instead presenting a selection dialog, followed by remembering the chosen application).
// In contrast, xdg-open queries the desktop environment (e.g., using kde-open on Plasma).
// Returns true if the launch process was initiated.
// This does not guarantee that xdg-open was actually able to open the file.
bool CMainWindow::openWithSystemDefault(const QUrl &url) {
    if (!url.isValid()) return false;

    const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
    if (!xdgOpen.isEmpty()) {
        const QString target = url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded);
        if (QProcess::startDetached(xdgOpen, { target })) return true;
    }
    return QDesktopServices::openUrl(url);
}
