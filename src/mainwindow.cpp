#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "qvrenamedialog.h"
#include "qvclipboard.h"
#include "qvexportdialog.h"
#include "qvlayershud.h"
#include "qvfiltersdialog.h"
#include "qvgalleryview.h"
#include "qvfileoperations.h"
#include <QScopedValueRollback>
#include <QStackedWidget>

#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QMovie>
#include <QImageWriter>
#include <QSettings>
#include <QStyle>
#include <QIcon>
#include <QMimeDatabase>
#include <QScreen>
#include <QCursor>
#include <QInputDialog>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QMenu>
#include <QWindow>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTemporaryFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QKeySequenceEdit>

namespace {
class ContentPages : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;
    // The home page's layout must not constrain native-size media windows.
    QSize minimumSizeHint() const override { return QSize(0, 0); }
};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);

#if defined COCOA_LOADED && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Allow the titlebar to overlap widgets with full size content view
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    centralWidget()->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
#endif

    // Initialize variables
    justLaunchedWithImage = false;
    storedWindowState = Qt::WindowNoState;

    // Images and videos share one graphics canvas so navigation and transforms
    // behave consistently for every visual media type.
    graphicsView = new QVGraphicsView(this);
    contentPages = new ContentPages(this);
    galleryView = new QVGalleryView(contentPages);
    contentPages->addWidget(galleryView);
    contentPages->addWidget(graphicsView);
    centralWidget()->layout()->addWidget(contentPages);
    connect(galleryView, &QVGalleryView::pathActivated, this, &MainWindow::openFile);
    connect(galleryView, &QVGalleryView::openFileRequested, this, [this] { qvApp->pickFile(this); });
    connect(galleryView, &QVGalleryView::fullscreenRequested, this, &MainWindow::toggleFullScreen);
    connect(galleryView, &QVGalleryView::selectionChanged, this, &MainWindow::disableActions);
    connect(graphicsView, &QVGraphicsView::folderRequested, this, [this](const QString &path) {
        showFolder(path);
    });
    connect(graphicsView, &QVGraphicsView::mediaRequested, this, &MainWindow::showViewer);
    qApp->installEventFilter(this);

    // Hide fullscreen label by default
    ui->fullscreenLabel->hide();

    // Connect graphicsview signals
    connect(graphicsView, &QVGraphicsView::fileChanged, this, &MainWindow::fileChanged);
    connect(graphicsView, &QVGraphicsView::updatedLoadedPixmapItem, this,
            &MainWindow::setWindowSize);
    connect(graphicsView, &QVGraphicsView::cancelSlideshow, this, &MainWindow::cancelSlideshow);
    connect(graphicsView, &QVGraphicsView::videoPlaybackStateChanged, this,
            &MainWindow::disableActions);
    connect(graphicsView, &QVGraphicsView::fullscreenRequested, this,
            &MainWindow::toggleFullScreen);
    connect(graphicsView, &QVGraphicsView::videoErrorOccurred, this, [this]() {
        if (!graphicsView->videoErrorString().isEmpty())
            QMessageBox::critical(this, tr("Error"), graphicsView->videoErrorString());
        fileChanged();
    });

    // Initialize escape shortcut
    escShortcut = new QShortcut(Qt::Key_Escape, this);
    escShortcut->setAutoRepeat(false);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (isBrowsingFolder() && galleryView->hasSelection()) galleryView->clearSelection();
        else if (layersHud && layersHud->isVisible()) layersHud->setVisible(false);
        else if (windowState().testFlag(Qt::WindowFullScreen))
            toggleFullScreen();
    });

    // Enable drag&dropping
    setAcceptDrops(true);

    // Make info dialog object
    info = new QVInfoDialog(this);

    // Timer for slideshow
    slideshowTimer = new QTimer(this);
    slideshowTimer->setObjectName("slideshowTimer");
    slideshowTimer->setTimerType(Qt::PreciseTimer);
    connect(slideshowTimer, &QTimer::timeout, this, &MainWindow::slideshowAction);

    // Context menu
    auto &actionManager = qvApp->getActionManager();

    contextMenu = new QMenu(this);

    actionManager.addCloneOfAction(contextMenu, "open");
    actionManager.addCloneOfAction(contextMenu, "openfolder");
    actionManager.addCloneOfAction(contextMenu, "browsefolder");
    actionManager.addCloneOfAction(contextMenu, "browsechild");
    actionManager.addCloneOfAction(contextMenu, "home");
    actionManager.addCloneOfAction(contextMenu, "openurl");
    contextMenu->addMenu(actionManager.buildRecentsMenu(true, contextMenu));
    contextMenu->addMenu(actionManager.buildOpenWithMenu(contextMenu));
    actionManager.addCloneOfAction(contextMenu, "opencontainingfolder");
    actionManager.addCloneOfAction(contextMenu, "showfileinfo");
    contextMenu->addSeparator();
    actionManager.addCloneOfAction(contextMenu, "rename");
    actionManager.addCloneOfAction(contextMenu, "delete");
    contextMenu->addSeparator();
    actionManager.addCloneOfAction(contextMenu, "nextfile");
    actionManager.addCloneOfAction(contextMenu, "previousfile");
    contextMenu->addSeparator();
    contextMenu->addMenu(actionManager.buildViewMenu(true, contextMenu));
    contextMenu->addMenu(actionManager.buildToolsMenu(true, contextMenu));
    contextMenu->addMenu(actionManager.buildHelpMenu(true, contextMenu));

    connect(contextMenu, &QMenu::triggered, this, [this](QAction *triggeredAction) {
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Initialize menubar
    setMenuBar(actionManager.buildMenuBar(this));
    // Stop actions conflicting with the window's actions
    const auto menubarActions = ActionManager::getAllNestedActions(menuBar()->actions());
    for (auto action : menubarActions) {
        action->setShortcutContext(Qt::WidgetShortcut);
    }
    connect(menuBar(), &QMenuBar::triggered, this, [this](QAction *triggeredAction) {
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Add all actions to this window so keyboard shortcuts are always triggered
    // using virtual menu to hold them so i can connect to the triggered signal
    virtualMenu = new QMenu(this);
    const auto &actionKeys = actionManager.getActionLibrary().keys();
    for (const QString &key : actionKeys) {
        actionManager.addCloneOfAction(virtualMenu, key);
    }
    addActions(virtualMenu->actions());
    connect(virtualMenu, &QMenu::triggered, this, [this](QAction *triggeredAction) {
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Enable actions related to having a window
    disableActions();

    // Connect functions to application components
    connect(&qvApp->getShortcutManager(), &ShortcutManager::shortcutsUpdated, this,
            &MainWindow::shortcutsUpdated);
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &MainWindow::settingsUpdated);
    settingsUpdated();
    shortcutsUpdated();

    // Timer for delayed-load Open With menu
    populateOpenWithTimer = new QTimer(this);
    populateOpenWithTimer->setSingleShot(true);
    populateOpenWithTimer->setInterval(250);
    connect(populateOpenWithTimer, &QTimer::timeout, this,
            &MainWindow::requestPopulateOpenWithMenu);

    // Connection for open with menu population futurewatcher
    connect(&openWithFutureWatcher, &QFutureWatcher<QList<OpenWith::OpenWithItem>>::finished, this,
            [this]() { populateOpenWithMenu(openWithFutureWatcher.result()); });

    // Load window geometry
    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
    showHome();
    connect(&actionManager, &ActionManager::recentsMenuUpdated, this, [this] {
        if (contentPages->currentWidget() == galleryView && galleryView->isHome()) refreshHomeRecents();
    });

    // Show welcome dialog on first launch
    if (!settings.value("firstlaunch", false).toBool()) {
        settings.setValue("firstlaunch", true);
        settings.setValue("configversion", VERSION);
        qvApp->openWelcomeDialog(this);
    }
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    delete ui;
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        qvApp->addToLastActiveWindows(this);
    }
    return QMainWindow::event(event);
}

void MainWindow::finishDistortShortcut()
{
    distortHeldKey = 0;
    if (temporaryDistort) {
        temporaryDistort = false;
        graphicsView->setDistortActive(false);
        if (layersHud) layersHud->setDistortActive(false);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (speedIndicator && watched == graphicsView->viewport()
            && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        positionSpeedIndicator();
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        auto *widget = qobject_cast<QWidget *>(watched);
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (widget && widget->window() == this
                && (mouse->button() == Qt::BackButton || mouse->button() == Qt::ForwardButton)) {
            if (event->type() == QEvent::MouseButtonPress) {
                if (mouse->button() == Qt::BackButton) browseParentFolder();
                else browseChildFolder();
            }
            event->accept();
            return true;
        }
    }
    // A release can reach a different widget, or never arrive after switching apps.
    if ((compareHeldKey || distortHeldKey) && (event->type() == QEvent::ApplicationDeactivate
            || (event->type() == QEvent::WindowDeactivate
                && (watched == this || watched == filtersDialog))
            || (event->type() == QEvent::Hide
                && (watched == this || watched == filtersDialog)))) {
        compareHeldKey = 0;
        graphicsView->setCompareOriginal(false);
        finishDistortShortcut();
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease
            && event->type() != QEvent::ShortcutOverride)
        return QMainWindow::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    if (distortHeldKey && key->key() == distortHeldKey) {
        if (event->type() == QEvent::KeyRelease && !key->isAutoRepeat()) finishDistortShortcut();
        event->accept();
        return true;
    }
    if (compareHeldKey && key->key() == compareHeldKey) {
        if (event->type() == QEvent::KeyRelease && !key->isAutoRepeat()) {
            compareHeldKey = 0;
            graphicsView->setCompareOriginal(false);
        }
        event->accept();
        return true;
    }
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || (widget->window() != this && widget->window() != filtersDialog)
            || event->type() == QEvent::KeyRelease)
        return false;
    // Also check focus: ignored editor keys can bubble up to their parent window.
    const auto isEditor = [](QWidget *input) {
        for (QWidget *editor = input; editor; editor = editor->parentWidget()) {
            if (qobject_cast<QLineEdit *>(editor) || qobject_cast<QTextEdit *>(editor)
                    || qobject_cast<QPlainTextEdit *>(editor)
                    || qobject_cast<QAbstractSpinBox *>(editor)
                    || qobject_cast<QKeySequenceEdit *>(editor))
                return true;
        }
        return false;
    };
    QWidget *focused = QApplication::focusWidget();
    const QKeySequence pressed(key->key() | int(key->modifiers()));
    if (isEditor(widget) || (focused && focused->window() == widget->window()
                            && isEditor(focused))) {
        if (event->type() == QEvent::ShortcutOverride) {
            for (const QString &name : { QString("browsefolder"), QString("browsechild") }) {
                const auto *action = qvApp->getActionManager().getAction(name);
                if (action && action->shortcuts().contains(pressed)) {
                    event->accept();
                    return true;
                }
            }
        }
        return false;
    }
    if (widget->window() == this) {
        // Item views normally reserve Up/Down during ShortcutOverride. Route the
        // configured hierarchy keys before that, while leaving editors and Shift selection alone.
        for (const QString &name : { QString("browsefolder"), QString("browsechild") }) {
            const auto *action = qvApp->getActionManager().getAction(name);
            if (action && action->shortcuts().contains(pressed)) {
                if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                    if (name == "browsefolder") browseParentFolder();
                    else browseChildFolder();
                }
                event->accept();
                return true;
            }
        }
    }
    if (!graphicsView->isMediaLoaded()) return false;
    if (graphicsView->isCropActive() && key->modifiers() == Qt::NoModifier
            && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape)) {
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
            if (key->key() == Qt::Key_Escape) graphicsView->setCropActive(false);
            else graphicsView->applyCrop();
        }
        event->accept();
        return true;
    }
    if (layersHud && layersHud->isVisible() && widget->window() == this
            && key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) layersHud->setVisible(false);
        event->accept();
        return true;
    }
    if (graphicsView->isDistortActive() && (pressed == QKeySequence(Qt::CTRL | Qt::Key_Z)
            || pressed == QKeySequence(Qt::CTRL | Qt::Key_Y))) {
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
            if (key->key() == Qt::Key_Y) graphicsView->redoDistort();
            else graphicsView->undoDistort();
        }
        event->accept();
        return true;
    }
    if (distortShortcuts.contains(pressed) && graphicsView->canDistort()) {
        if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
            distortHeldKey = key->key();
            temporaryDistort = !layersHud || !layersHud->isVisible();
            const bool active = temporaryDistort || !graphicsView->isDistortActive();
            graphicsView->setDistortActive(active);
            if (layersHud) layersHud->setDistortActive(active);
        }
        event->accept();
        return true;
    }
    if (key->isAutoRepeat() || !compareShortcuts.contains(pressed)) return false;
    if (event->type() == QEvent::KeyPress) {
        compareHeldKey = key->key();
        graphicsView->setCompareOriginal(true);
    }
    event->accept();
    return true;
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMainWindow::contextMenuEvent(event);

    // Show native menu on macOS with cocoa framework loaded
#ifdef COCOA_LOADED
    // On regular context menu, recents submenu updates right before it is shown.
    // The native cocoa menu does not update elements until the entire menu is reopened, so we
    // update first
    qvApp->getActionManager().loadRecentsList();
    QVCocoaFunctions::showMenu(contextMenu, event->pos(), windowHandle());
#else
    contextMenu->popup(event->globalPos());
#endif
}

void MainWindow::showEvent(QShowEvent *event)
{
#ifdef COCOA_LOADED
    // Enable full size content view. With some Qt versions, this can break its restoreGeometry
    // functionality even if we enable this after. Presumably restoreGeometry saves data for
    // further processing after the window is shown, hence the timer here to queue this on the
    // event loop and run after that processing happens.
    QTimer::singleShot(
            0, this, [this]() { QVCocoaFunctions::setFullSizeContentView(windowHandle(), true); });
#endif

    if (!menuBar()->sizeHint().isEmpty()) {
        ui->fullscreenLabel->setMargin(0);
        ui->fullscreenLabel->setMinimumHeight(menuBar()->sizeHint().height());
    }

    QMainWindow::showEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
#ifdef COCOA_LOADED
    // Full size content view can confuse saveGeometry, making it think the window is taller than
    // it really is, so the restored window ends up taller. Turn it off before saving geometry.
    QVCocoaFunctions::setFullSizeContentView(windowHandle(), false);
#endif

    QSettings settings;
    settings.setValue("geometry", saveGeometry());

    qvApp->deleteFromLastActiveWindows(this);
    qvApp->getActionManager().untrackClonedActions(contextMenu);
    qvApp->getActionManager().untrackClonedActions(menuBar());
    qvApp->getActionManager().untrackClonedActions(virtualMenu);

    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        const auto *changeEvent = static_cast<QWindowStateChangeEvent *>(event);
        if (windowState().testFlag(Qt::WindowFullScreen)
            != changeEvent->oldState().testFlag(Qt::WindowFullScreen))
            fullscreenChanged();
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (contentPages->currentWidget() == galleryView) {
        if (event->button() == Qt::MouseButton::BackButton) browseParentFolder();
        else if (event->button() == Qt::MouseButton::ForwardButton) browseChildFolder();
        QMainWindow::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::MouseButton::BackButton)
        browseParentFolder();
    else if (event->button() == Qt::MouseButton::ForwardButton)
        browseChildFolder();
    else if (event->button() == Qt::MouseButton::MiddleButton)
        resetZoom();

    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::LeftButton)
        toggleFullScreen();
    QMainWindow::mouseDoubleClickEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    const QColor backgroundColor = alternateBackgroundEnabled ? alternateBackgroundColor
            : customBackgroundColor.isValid() ? customBackgroundColor
                                              : painter.background().color();

    // Find the top of the viewport to account for the menu bar if it's inside the window
    // and/or the label that displays titlebar text in full screen mode.
    const int viewportY = graphicsView->mapTo(this, QPoint()).y();
    // On macOS, part of the viewport may be additionally covered with the window's translucent
    // titlebar due to full size content view.
    const int unobscuredViewportY = qMax(getTitlebarOverlap(), viewportY);

    // Erase the area above the viewport, i.e. fill it with the painter's default color.
    const QRect headerRect = QRect(0, 0, width(), viewportY);
    if (headerRect.isValid()) {
        painter.eraseRect(headerRect);
    }

    // Fill the viewport with the background color.
    const QRect viewportRect = rect().adjusted(0, viewportY, 0, 0);
    if (viewportRect.isValid()) {
        painter.fillRect(viewportRect, backgroundColor);
    }

    // If there's an error message, draw it centered inside the unobscured area of the viewport.
    const QRect unobscuredViewportRect = rect().adjusted(0, unobscuredViewportY, 0, 0);
    if (getImageDetails().errorData.hasError && unobscuredViewportRect.isValid()) {
        const QVImageCore::ErrorData &errorData = getImageDetails().errorData;
        const QString errorMessage =
                tr("Error occurred opening\n%3\n%2 (Error %1)")
                        .arg(QString::number(errorData.errorNum), errorData.errorString,
                             getCurrentMedia().fileInfo.fileName());
        painter.setFont(font());
        painter.setPen(QVApplication::getPerceivedBrightness(backgroundColor) > 0.5 ? Qt::black
                                                                                    : Qt::white);
        painter.drawText(unobscuredViewportRect, errorMessage, QTextOption(Qt::AlignCenter));
    }
    QMainWindow::paintEvent(event);
}

void MainWindow::toggleBackgroundColor()
{
    // Keep the override local to this window; never change the saved background preference.
    alternateBackgroundEnabled = !alternateBackgroundEnabled;
    updateGalleryBackground();
    update();
}

void MainWindow::updateGalleryBackground()
{
    QPainter defaults;
    galleryView->setBackgroundColor(alternateBackgroundEnabled ? alternateBackgroundColor
            : customBackgroundColor.isValid() ? customBackgroundColor : defaults.background().color());
}

void MainWindow::fullscreenChanged()
{
    const bool isFullscreen = windowState().testFlag(Qt::WindowFullScreen);
    const auto fullscreenActions =
            qvApp->getActionManager().getAllClonesOfAction("fullscreen", this);
    for (const auto &fullscreenAction : fullscreenActions) {
        fullscreenAction->setText(isFullscreen ? tr("Exit F&ull Screen")
                                               : tr("Enter F&ull Screen"));
        fullscreenAction->setIcon(isFullscreen ? QIcon::fromTheme("view-restore")
                                               : QIcon::fromTheme("view-fullscreen"));
    }
    ui->fullscreenLabel->setVisible(
            isFullscreen
            && qvApp->getSettingsManager().getBool(SettingsManager::Setting::FullScreenDetails));
}

void MainWindow::openFile(const QString &fileName)
{
    if (isBrowsingFolder() && QFileInfo(fileName).isFile()
            && QFileInfo(fileName).absolutePath() == galleryView->folderPath())
        graphicsView->setFolderOrder(galleryView->folderPath(), galleryView->mediaFiles(), galleryScanOptions());
    graphicsView->loadFile(fileName);
    cancelSlideshow();
}

MainWindow *MainWindow::duplicateWindow()
{
    auto *window = qvApp->newWindow();
    window->resize(size());
    if (isBrowsingFolder()) {
        window->showFolder(galleryView->folderPath(), galleryView->selectedPaths().value(0));
    } else if (getCurrentMedia().isLoadRequested) {
        window->openFile(getCurrentMedia().fileInfo.absoluteFilePath());
    }
    window->folderHistory = folderHistory;
    return window;
}

QVMediaCatalog::ScanOptions MainWindow::galleryScanOptions() const
{
    QVMediaCatalog::ScanOptions options;
    options.supportedMedia.append({ QVMediaCatalog::MediaType::Image,
                                    qvApp->getFileExtensionList(), qvApp->getMimeTypeNameList() });
    options.supportedMedia.append({ QVMediaCatalog::MediaType::Video,
                                    qvApp->getVideoExtensionList(), qvApp->getVideoMimeTypeNameList() });
    options.allowMimeContentDetection = qvGetSettingBool(AllowMimeContentDetection);
    options.includeHidden = !qvApp->getSettingsManager().getBool("skiphidden");
    options.sortMode = qvGetSettingInt(SortMode);
    options.sortDescending = qvGetSettingBool(SortDescending);
    return options;
}

bool MainWindow::isBrowsingFolder() const
{
    return contentPages->currentWidget() == galleryView && !galleryView->isHome();
}

void MainWindow::showViewer()
{
    if (!navigatingHierarchy) folderHistory.clear();
    contentPages->setCurrentWidget(graphicsView);
    graphicsView->setFocus();
}

void MainWindow::showHome()
{
    folderHistory.clear();
    contentPages->setCurrentWidget(galleryView);
    cancelSlideshow();
    graphicsView->closeImage();
    if (layersHud) layersHud->setVisible(false);
    if (filtersDialog) filtersDialog->hide();
    refreshHomeRecents();
    updateWindowTitle();
    disableActions();
}

void MainWindow::refreshHomeRecents()
{
    QList<QPair<QString, QString>> recents;
    for (const auto &recent : qvApp->getActionManager().getRecentsList()) {
        recents.append({ recent.fileName, recent.filePath });
        if (recents.size() == 4) break;
    }
    galleryView->showHome(recents);
}

void MainWindow::showFolder(const QString &path, const QString &selectedPath)
{
    if (!navigatingHierarchy) folderHistory.clear();
    if (path.isEmpty()) { showHome(); return; }
    const QString folderPath = QDir(path).absolutePath();
    contentPages->setCurrentWidget(galleryView);
    cancelSlideshow();
    graphicsView->closeImage();
    if (layersHud) layersHud->setVisible(false);
    if (filtersDialog) filtersDialog->hide();
    galleryView->openFolder(folderPath, galleryScanOptions(), selectedPath);
    qvApp->getActionManager().addFileToRecentsList(QFileInfo(folderPath));
    justLaunchedWithImage = false;
    updateWindowTitle();
    updateWindowFilePath();
    disableActions();
}

void MainWindow::browseParentFolder()
{
    QString previous;
    QString parentPath;
    if (contentPages->currentWidget() == graphicsView) {
        const QFileInfo file = getCurrentMedia().fileInfo;
        if (file.filePath().isEmpty()) return;
        previous = file.absoluteFilePath();
        parentPath = file.absolutePath();
    } else if (!galleryView->isHome()) {
        previous = galleryView->folderPath();
        QDir parent(previous);
        if (!parent.cdUp()) return;
        parentPath = parent.absolutePath();
    } else return;
    folderHistory.remember(parentPath, previous);
    QScopedValueRollback<bool> navigating(navigatingHierarchy, true);
    showFolder(parentPath, previous);
}

void MainWindow::browseChildFolder()
{
    if (!isBrowsingFolder()) return;
    const QString path = folderHistory.takeChild(galleryView->folderPath());
    if (path.isEmpty()) return;
    if (!QFileInfo::exists(path)) { folderHistory.clear(); disableActions(); return; }
    QScopedValueRollback<bool> navigating(navigatingHierarchy, true);
    openFile(path);
    disableActions();
}

void MainWindow::pickFolder()
{
    auto *dialog = new QFileDialog(this, tr("Open folder"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly);
    dialog->setDirectory(isBrowsingFolder() ? galleryView->folderPath()
                        : getCurrentMedia().fileInfo.filePath().isEmpty() ? QDir::homePath()
                        : getCurrentMedia().fileInfo.absolutePath());
    dialog->setWindowModality(Qt::WindowModal);
    connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &path) { showFolder(path); });
    dialog->open();
}

void MainWindow::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    updateWindowTitle();

    // Cache custom background color since we read it in paintEvent
    customBackgroundColor = settingsManager.getBool(SettingsManager::Setting::BgColorEnabled)
            ? QColor(settingsManager.getString(SettingsManager::Setting::BgColor))
            : QColor();
    alternateBackgroundColor =
            QColor(settingsManager.getString(SettingsManager::Setting::AlternateBgColor));
    updateGalleryBackground();

    // menubarenabled
    bool menuBarEnabled = settingsManager.getBool(SettingsManager::Setting::MenuBarEnabled);
#ifdef Q_OS_MACOS
    // Menu bar is effectively always enabled on macOS
    menuBarEnabled = true;
#endif
    menuBar()->setVisible(menuBarEnabled);

#ifdef COCOA_LOADED
    // titlebaralwaysdark/hidetitlebar
    QVCocoaFunctions::setVibrancy(
            settingsManager.getBool(SettingsManager::Setting::HideTitlebar),
            settingsManager.getBool(SettingsManager::Setting::ForceDarkMode), windowHandle());
    // quitonlastwindow
    qvApp->setQuitOnLastWindowClosed(
            settingsManager.getBool(SettingsManager::Setting::QuitOnLastWindow));
#endif

    // slideshow timer
    const int configuredInterval = qBound(100, qRound(
            settingsManager.getDouble(SettingsManager::Setting::SlideshowTimer) * 1000), 100000);
    if (configuredInterval != slideshowDefaultIntervalMs) {
        updateSlideshowInterval(slideshowDefaultIntervalMs == 0 ? configuredInterval
                : qRound(slideshowIntervalMs * (double(configuredInterval) / slideshowDefaultIntervalMs)));
        slideshowDefaultIntervalMs = configuredInterval;
    }

    ui->fullscreenLabel->setVisible(
            settingsManager.getBool(SettingsManager::Setting::FullScreenDetails)
            && windowState().testFlag(Qt::WindowFullScreen));

    setWindowSize();

    // repaint in case background color changed
    update();
}

void MainWindow::shortcutsUpdated()
{
    compareHeldKey = 0;
    graphicsView->setCompareOriginal(false);
    compareShortcuts.clear();
    finishDistortShortcut();
    distortShortcuts.clear();
    for (const auto &shortcut : qvApp->getShortcutManager().getShortcutsList()) {
        if (shortcut.name == "compareoriginal")
            compareShortcuts = ShortcutManager::stringListToKeySequenceList(shortcut.shortcuts);
        if (shortcut.name == "distort")
            distortShortcuts = ShortcutManager::stringListToKeySequenceList(shortcut.shortcuts);
    }
    // If esc is not used in a shortcut, let it exit fullscreen
    escShortcut->setKey(Qt::Key_Escape);

    const auto &actionLibrary = qvApp->getActionManager().getActionLibrary();
    for (const auto &action : actionLibrary) {
        if (action->shortcuts().contains(QKeySequence(Qt::Key_Escape))) {
            escShortcut->setKey({});
            break;
        }
    }
}

void MainWindow::openRecent(int i)
{
    auto recentsList = qvApp->getActionManager().getRecentsList();
    openFile(recentsList.value(i).filePath);
}

void MainWindow::fileChanged()
{
    if (layersHud) {
        layersHud->setSource(getCurrentMedia().fileInfo.fileName(), getIsMediaLoaded());
        layersHud->setDistortAvailable(graphicsView->canDistort());
        layersHud->setDistortActive(graphicsView->isDistortActive());
    }
    if (getIsMediaLoaded()) populateOpenWithTimer->start();
    else populateOpenWithTimer->stop();
    disableActions();

    if (info->isVisible())
        refreshProperties();
    updateWindowTitle();
    updateWindowFilePath();

    // repaint to handle error message
    update();
}

void MainWindow::disableActions()
{
    const auto &actionLibrary = qvApp->getActionManager().getActionLibrary();
    for (const auto &action : actionLibrary) {
        const auto &data = action->data().toStringList();
        const auto &clonesOfAction =
                qvApp->getActionManager().getAllClonesOfAction(data.first(), this);

        // Enable this window's actions when a file is loaded
        if (data.last().contains("disable")) {
            for (const auto &clone : clonesOfAction) {
                const auto &cloneData = clone->data().toStringList();
                if (cloneData.last() == "disable") {
                    clone->setEnabled(getImageDetails().isPixmapLoaded);
                } else if (cloneData.last() == "mediadisable") {
                    clone->setEnabled(getIsMediaLoaded()
                                      || ((data.first() == "reloadfile" || data.first() == "zoomin" || data.first() == "zoomout"
                                           || data.first() == "resetzoom" || data.first() == "originalsize")
                                          && isBrowsingFolder())
                                      || (data.first() == "delete" && isBrowsingFolder()
                                          && galleryView->hasSelection() && !galleryTrashInProgress));
                } else if (cloneData.last() == "historydisable") {
                    clone->setEnabled(isBrowsingFolder()
                                      && !folderHistory.childOf(galleryView->folderPath()).isEmpty());
                } else if (cloneData.last() == "gifdisable") {
                    clone->setEnabled(getImageDetails().isMovieLoaded);
                } else if (cloneData.last() == "speeddisable") {
                    clone->setEnabled(slideshowTimer->isActive()
                                      || getImageDetails().isMovieLoaded
                                      || graphicsView->isVideoLoaded());
                } else if (cloneData.last() == "playbackdisable") {
                    clone->setEnabled(getImageDetails().isMovieLoaded
                                      || graphicsView->isVideoLoaded());
                } else if (cloneData.last() == "videodisable") {
                    clone->setEnabled(graphicsView->isVideoLoaded());
                } else if (cloneData.last() == "undodisable") {
                    clone->setEnabled(!lastDeletedFiles.isEmpty()
                                      && !lastDeletedFiles.top().pathInTrash.isEmpty());
                } else if (cloneData.last() == "folderdisable") {
                    clone->setEnabled(contentPages->currentWidget() == graphicsView
                                      && !getCurrentMedia().folderFiles.isEmpty());
                } else if (cloneData.last() == "windowdisable") {
                    clone->setEnabled(true);
                }
            }
        }
    }

    const auto &openWithMenus = qvApp->getActionManager().getAllClonesOfMenu("openwith", this);
    for (const auto &menu : openWithMenus) {
        menu->setEnabled(getIsMediaLoaded());
    }
}

void MainWindow::requestPopulateOpenWithMenu()
{
    const QString path = getCurrentMedia().fileInfo.absoluteFilePath();
    if (path.isEmpty()) return;
    openWithFutureWatcher.setFuture(QtConcurrent::run([path] {
        return OpenWith::getOpenWithItems(path);
    }));
}

void MainWindow::populateOpenWithMenu(const QList<OpenWith::OpenWithItem> openWithItems)
{
    for (int i = 0; i < qvApp->getActionManager().getOpenWithMaxLength(); i++) {
        const auto clonedActions = qvApp->getActionManager().getAllClonesOfAction(
                "openwith" + QString::number(i), this);
        for (const auto &action : clonedActions) {
            // If we are within the bounds of the open with list
            if (i < openWithItems.length()) {
                auto openWithItem = openWithItems.value(i);

                action->setVisible(true);
                action->setIconVisibleInMenu(
                        false); // Hide icon temporarily to speed up updates in certain cases
                action->setText(openWithItem.name);
                if (!openWithItem.iconName.isEmpty())
                    action->setIcon(QIcon::fromTheme(openWithItem.iconName));
                else
                    action->setIcon(openWithItem.icon);
                auto data = action->data().toList();
                data.replace(1, QVariant::fromValue(openWithItem));
                action->setData(data);
                action->setIconVisibleInMenu(true);
            } else {
                action->setVisible(false);
            }
        }
    }
}

void MainWindow::refreshProperties()
{
    int value4;
    if (getImageDetails().isMovieLoaded)
        value4 = graphicsView->getLoadedMovie().frameCount();
    else
        value4 = 0;
    const QSize mediaSize = getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video
            ? graphicsView->currentMediaSize()
            : getImageDetails().baseImageSize;
    info->setInfo(getCurrentMedia().fileInfo, mediaSize.width(), mediaSize.height(), value4);
}

void MainWindow::updateWindowTitle()
{
    QString newString = "qMedia";
    if (isBrowsingFolder()) newString = QDir::toNativeSeparators(galleryView->folderPath()) + " - qMedia";
    if (getCurrentMedia().fileInfo.isFile()) {
        switch (qvApp->getSettingsManager().getInt(SettingsManager::Setting::TitleBarMode)) {
        case 1: {
            newString = getCurrentMedia().fileInfo.fileName();
            break;
        }
        case 2: {
            newString = QString::number(getCurrentMedia().currentIndexInFolder + 1);
            newString += "/" + QString::number(getCurrentMedia().folderFiles.count());
            newString += " - " + getCurrentMedia().fileInfo.fileName();
            break;
        }
        case 3: {
            newString = QString::number(getCurrentMedia().currentIndexInFolder + 1);
            newString += "/" + QString::number(getCurrentMedia().folderFiles.count());
            newString += " - " + getCurrentMedia().fileInfo.fileName();
            if (getImageDetails().isPixmapLoaded && !getImageDetails().errorData.hasError) {
                newString += " - " + QString::number(getImageDetails().baseImageSize.width());
                newString += "x" + QString::number(getImageDetails().baseImageSize.height());
            }
            newString += " - " + QVInfoDialog::formatBytes(getCurrentMedia().fileInfo.size());
            newString += " - qMedia";
            break;
        }
        }
    }

    setWindowTitle(newString);

    // Update fullscreen label to titlebar text as well
    ui->fullscreenLabel->setText(newString);
}

void MainWindow::updateWindowFilePath()
{
    if (!windowHandle())
        return;

    const bool shouldPopulate = getIsMediaLoaded();
    windowHandle()->setFilePath(shouldPopulate ? getCurrentMedia().fileInfo.absoluteFilePath()
                                               : "");
}

void MainWindow::setWindowSize()
{
    if (!getIsMediaLoaded())
        return;

    // check if the program is configured to resize the window
    int windowResizeMode =
            qvApp->getSettingsManager().getInt(SettingsManager::Setting::WindowResizeMode);
    if (!(windowResizeMode == 2 || (windowResizeMode == 1 && justLaunchedWithImage)))
        return;

    justLaunchedWithImage = false;

    // check if window is maximized or fullscreened
    if (windowState().testFlag(Qt::WindowMaximized) || windowState().testFlag(Qt::WindowFullScreen))
        return;

    qreal minWindowResizedPercentage =
            qvApp->getSettingsManager().getInt(SettingsManager::Setting::MinWindowResizedPercentage)
            / 100.0;
    qreal maxWindowResizedPercentage =
            qvApp->getSettingsManager().getInt(SettingsManager::Setting::MaxWindowResizedPercentage)
            / 100.0;

    QSize mediaSize = graphicsView->currentMediaSize();
    if (mediaSize.isEmpty())
        return;
    mediaSize -= QSize(4, 4);

    // Try to grab the current screen
    QScreen *currentScreen = screenContaining(frameGeometry());

    // makeshift validity check
    bool screenValid = QGuiApplication::screens().contains(currentScreen);
    // Use first screen as fallback
    if (!screenValid)
        currentScreen = QGuiApplication::screens().at(0);

    QSize extraWidgetsSize{ 0, 0 };

    if (menuBar()->isVisible())
        extraWidgetsSize.rheight() += menuBar()->height();

    const int titlebarOverlap = getTitlebarOverlap();
    if (titlebarOverlap != 0)
        extraWidgetsSize.rheight() += titlebarOverlap;

    const QSize windowFrameSize = frameGeometry().size() - geometry().size();
    const QSize hardLimitSize = currentScreen->availableSize() - windowFrameSize - extraWidgetsSize;
    const QSize screenSize = currentScreen->size();
    const QSize minWindowSize = (screenSize * minWindowResizedPercentage).boundedTo(hardLimitSize);
    const QSize maxWindowSize = (screenSize * maxWindowResizedPercentage).boundedTo(hardLimitSize);

    if (mediaSize.width() < minWindowSize.width() && mediaSize.height() < minWindowSize.height()) {
        mediaSize.scale(minWindowSize, Qt::KeepAspectRatio);
    } else if (mediaSize.width() > maxWindowSize.width()
               || mediaSize.height() > maxWindowSize.height()) {
        mediaSize.scale(maxWindowSize, Qt::KeepAspectRatio);
    }

    // Windows reports the wrong minimum width, so we constrain the image size relative to the dpi
    // to stop weirdness with tiny images
#ifdef Q_OS_WIN
    auto minimumMediaSize = QSize(qRound(logicalDpiX() * 1.5), logicalDpiY() / 2);
    if (mediaSize.boundedTo(minimumMediaSize) == mediaSize)
        mediaSize = minimumMediaSize;
#endif

    // Match center after new geometry
    // This is smoother than a single geometry set for some reason
    QRect oldRect = geometry();
    resize(mediaSize + extraWidgetsSize);
    QRect newRect = geometry();
    newRect.moveCenter(oldRect.center());

    // Ensure titlebar is not above or below the available screen area
    const QRect availableScreenRect = currentScreen->availableGeometry();
    const int topFrameHeight = geometry().top() - frameGeometry().top();
    const int windowMinY = availableScreenRect.top() + topFrameHeight;
    const int windowMaxY =
            availableScreenRect.top() + availableScreenRect.height() - titlebarOverlap;
    if (newRect.top() < windowMinY)
        newRect.moveTop(windowMinY);
    if (newRect.top() > windowMaxY)
        newRect.moveTop(windowMaxY);

    setGeometry(newRect);
}

// Initially copied from Qt source code (QGuiApplication::screenAt) and then customized
QScreen *MainWindow::screenContaining(const QRect &rect)
{
    QScreen *bestScreen = nullptr;
    int bestScreenArea = 0;
    QVarLengthArray<const QScreen *, 8> visitedScreens;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        if (visitedScreens.contains(screen))
            continue;
        // The virtual siblings include the screen itself, so iterate directly
        const auto siblings = screen->virtualSiblings();
        for (QScreen *sibling : siblings) {
            const QRect intersect = sibling->geometry().intersected(rect);
            const int area = intersect.width() * intersect.height();
            if (area > bestScreenArea) {
                bestScreen = sibling;
                bestScreenArea = area;
            }
            visitedScreens.append(sibling);
        }
    }
    return bestScreen;
}

bool MainWindow::getIsPixmapLoaded() const
{
    return getImageDetails().isPixmapLoaded;
}

bool MainWindow::getIsMediaLoaded() const
{
    return graphicsView->isMediaLoaded();
}

void MainWindow::setJustLaunchedWithImage(bool value)
{
    justLaunchedWithImage = value;
}

void MainWindow::openUrl(const QUrl &url, const QImage &fallback)
{
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
        QMessageBox::critical(this, tr("Error"), tr("Enter a direct HTTP or HTTPS media URL."));
        return;
    }

    auto *tempFile = new QTemporaryFile(qvApp);
    tempFile->setFileTemplate(QDir::tempPath() + "/qMedia-XXXXXX");
    if (!tempFile->open()) {
        QMessageBox::critical(this, tr("Error"), tempFile->errorString());
        delete tempFile;
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = networkAccessManager.get(request);
    auto *progress = new QProgressDialog(tr("Downloading media..."), tr("Cancel"), 0, 0, this);
    progress->setWindowTitle(tr("Open URL..."));
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->open();
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
                progress->setRange(0, total > 0 ? 100 : 0);
                if (total > 0)
                    progress->setValue(int(100.0 * received / total));
            });
    // Drain each chunk on the reply's owning thread; large videos stay off the heap.
    connect(reply, &QNetworkReply::readyRead, this, [reply, tempFile] {
        const QByteArray chunk = reply->readAll();
        if (tempFile->write(chunk) != chunk.size()) {
            reply->setProperty("saveError", tempFile->errorString());
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, tempFile, progress, fallback] {
        progress->close();
        progress->deleteLater();
        reply->deleteLater();
        QString error = reply->property("saveError").toString();
        if (reply->error() != QNetworkReply::NoError && error.isEmpty()) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                error = reply->errorString();
        } else if (error.isEmpty()) {
            const QByteArray tail = reply->readAll();
            if (tempFile->write(tail) != tail.size() || !tempFile->flush())
                error = tempFile->errorString();
            else {
                tempFile->seek(0);
                const QString suffix = QVClipboard::mediaSuffix(tempFile->read(65536),
                        reply->header(QNetworkRequest::ContentTypeHeader).toString(), reply->url());
                tempFile->close();
                if (suffix.isEmpty())
                    error = tr("The URL did not return supported image or video data. Use a direct media link.");
                else if (!tempFile->rename(tempFile->fileName() + "." + suffix))
                    error = tempFile->errorString();
                else {
                    openFile(tempFile->fileName());
                    return;
                }
            }
        }
        tempFile->deleteLater();
        if (!error.isEmpty()) {
            if (!fallback.isNull())
                saveClipboardMedia({}, {}, fallback);
            else
                QMessageBox::critical(this, tr("Error"), error);
        }
    });
}

void MainWindow::pickUrl()
{
    auto inputDialog = new QInputDialog(this);
    inputDialog->setWindowTitle(tr("Open URL..."));
    inputDialog->setLabelText(tr("URL of a supported image or video file:"));
    inputDialog->resize(350, inputDialog->height());
    inputDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    connect(inputDialog, &QInputDialog::finished, this, [inputDialog, this](int result) {
        if (result) {
            const auto url = QUrl(inputDialog->textValue());
            openUrl(url);
        }
        inputDialog->deleteLater();
    });
    inputDialog->open();
}

void MainWindow::reloadFile()
{
    if (isBrowsingFolder()) {
        galleryView->openFolder(galleryView->folderPath(), galleryScanOptions(), {}, true);
        return;
    }
    if (getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video)
        graphicsView->reloadVideo();
    else
        graphicsView->reloadFile();
}

void MainWindow::openWith(const OpenWith::OpenWithItem &openWithItem)
{
    OpenWith::openWith(getCurrentMedia().fileInfo.absoluteFilePath(), openWithItem);
}

void MainWindow::openContainingFolder()
{
    if (!getIsMediaLoaded())
        return;

    const QFileInfo selectedFileInfo = getCurrentMedia().fileInfo;

#ifdef Q_OS_WIN
    QProcess::startDetached(
            "explorer",
            QStringList() << "/select,"
                          << QDir::toNativeSeparators(selectedFileInfo.absoluteFilePath()));
#elif defined Q_OS_MACOS
    QProcess::execute("open", QStringList() << "-R" << selectedFileInfo.absoluteFilePath());
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(selectedFileInfo.absolutePath()));
#endif
}

void MainWindow::showFileInfo()
{
    if (info->isVisible()) {
        info->close();
        return;
    }
    refreshProperties();
    info->show();
    info->raise();
}

void MainWindow::askTrashGallerySelection()
{
    const QStringList paths = galleryView->selectedPaths();
    if (paths.isEmpty() || galleryTrashInProgress) return;
    if (!qvGetSettingBool(AskDelete)) { trashGalleryPaths(paths); return; }
#ifdef Q_OS_WIN
    const QString question = tr("Move %1 selected items to the Recycle Bin?").arg(paths.size());
#else
    const QString question = tr("Move %1 selected items to the Trash?").arg(paths.size());
#endif
    auto *dialog = new QMessageBox(QMessageBox::Question, tr("Move to Trash"), question,
                                   QMessageBox::Yes | QMessageBox::No, this);
    dialog->setObjectName("galleryTrashConfirmation");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setDefaultButton(QMessageBox::No);
    QStringList names;
    bool containsFolder = false;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        containsFolder = containsFolder || info.isDir();
        if (names.size() < 10) names.append(info.fileName());
    }
    if (paths.size() > names.size()) names.append(tr("…and %1 more").arg(paths.size() - names.size()));
    if (containsFolder) names.append(tr("Selected folders include their contents."));
    dialog->setInformativeText(names.join('\n'));
    dialog->setCheckBox(new QCheckBox(tr("Do not ask again")));
    connect(dialog, &QMessageBox::finished, this, [this, dialog, paths](int result) {
        if (result != QMessageBox::Yes) return;
        QSettings().setValue("options/askdelete", !dialog->checkBox()->isChecked());
        qvApp->getSettingsManager().loadSettings();
        // The confirmed selection is immutable, even if the displayed folder changes.
        trashGalleryPaths(paths);
    });
    dialog->open();
}

void MainWindow::trashGalleryPaths(const QStringList &paths)
{
    if (paths.isEmpty() || galleryTrashInProgress) return;
    galleryTrashInProgress = true;
    const QString origin = QFileInfo(paths.first()).absolutePath();
    disableActions();
    using Results = QList<QVFileOperations::TrashResult>;
    auto *watcher = new QFutureWatcher<Results>(this);
    connect(watcher, &QFutureWatcher<Results>::finished, this, [this, watcher, origin] {
        const Results results = watcher->result();
        watcher->deleteLater();
        galleryTrashInProgress = false;
        QStringList errors;
        int failed = 0;
        for (const auto &result : results) {
            if (!result.error.isEmpty()) {
                ++failed;
                if (errors.size() < 10)
                    errors.append(QFileInfo(result.originalPath).fileName() + ": " + result.error);
            } else if (!result.trashPath.isEmpty()) {
                lastDeletedFiles.push({ result.trashPath, result.originalPath });
            }
        }
        if (isBrowsingFolder() && galleryView->folderPath() == origin) reloadFile();
        disableActions();
        if (failed) {
            auto *error = new QMessageBox(QMessageBox::Warning, tr("Move to Trash"),
                    tr("Could not move %1 items to the trash.\n%2").arg(failed).arg(errors.join('\n')),
                    QMessageBox::Ok, this);
            error->setAttribute(Qt::WA_DeleteOnClose);
            error->open();
        }
    });
    watcher->setFuture(QtConcurrent::run([paths] { return QVFileOperations::trash(paths); }));
}

void MainWindow::askDeleteFile(bool permanent)
{
    if (isBrowsingFolder()) {
        if (!permanent) askTrashGallerySelection();
        return;
    }
    if (!permanent && !qvApp->getSettingsManager().getBool(SettingsManager::Setting::AskDelete)) {
        deleteFile(permanent);
        return;
    }

    const QFileInfo &fileInfo = getCurrentMedia().fileInfo;
    const QString fileName = getCurrentMedia().fileInfo.fileName();

    if (!fileInfo.isWritable()) {
        QMessageBox::critical(
                this, tr("Error"),
                tr("Can't delete %1:\nNo write permission or file is read-only.").arg(fileName));
        return;
    }

    QString messageText;
    if (permanent) {
        messageText = tr("Are you sure you want to delete %1 permanently? This can't be undone.")
                              .arg(fileName);
    } else {
#ifdef Q_OS_WIN
        messageText = tr("Are you sure you want to move %1 to the Recycle Bin?").arg(fileName);
#else
        messageText = tr("Are you sure you want to move %1 to the Trash?").arg(fileName);
#endif
    }

    auto *msgBox = new QMessageBox(QMessageBox::Question, tr("Delete"), messageText,
                                   QMessageBox::Yes | QMessageBox::No, this);
    if (!permanent)
        msgBox->setCheckBox(new QCheckBox(tr("Do not ask again")));

    connect(msgBox, &QMessageBox::finished, this, [this, msgBox, permanent](int result) {
        if (result != QMessageBox::Yes)
            return;

        if (!permanent) {
            QSettings settings;
            settings.beginGroup("options");
            settings.setValue("askdelete", !msgBox->checkBox()->isChecked());
            qvApp->getSettingsManager().loadSettings();
        }
        this->deleteFile(permanent);
    });

    msgBox->open();
}

void MainWindow::deleteFile(bool permanent)
{
    if (isBrowsingFolder()) {
        if (!permanent) trashGalleryPaths(galleryView->selectedPaths());
        return;
    }
    const QFileInfo &fileInfo = getCurrentMedia().fileInfo;
    const QString filePath = fileInfo.absoluteFilePath();
    const QString fileName = fileInfo.fileName();

    graphicsView->closeVideo();
    graphicsView->closeImage();

    bool success;
    QString trashFilePath;
    if (permanent) {
        success = QFile::remove(filePath);
    } else {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
        QFile file(filePath);
        success = file.moveToTrash();
        if (success)
            trashFilePath = file.fileName();
#elif defined Q_OS_MACOS && COCOA_LOADED
        QString trashedFile = QVCocoaFunctions::deleteFile(filePath);
        success = !trashedFile.isEmpty();
        if (success)
            trashFilePath = QUrl(trashedFile).toLocalFile(); // remove file:// protocol
#elif defined Q_OS_UNIX && !defined Q_OS_MACOS
        trashFilePath = deleteFileLinuxFallback(filePath, false);
        success = !trashFilePath.isEmpty();
#else
        QMessageBox::critical(this, tr("Not Supported"),
                              tr("This program was compiled with an old version of Qt and this "
                                 "feature is not available.\n"
                                 "If you see this message, please report a bug!"));

        return;
#endif
    }

    if (!success || QFile::exists(filePath)) {
        openFile(filePath);
        QMessageBox::critical(this, tr("Error"), tr("Can't delete %1.").arg(fileName));
        return;
    }

    auto afterDelete = qvApp->getSettingsManager().getInt(SettingsManager::Setting::AfterDelete);
    if (afterDelete > 1)
        nextFile();
    else if (afterDelete < 1)
        previousFile();

    if (!trashFilePath.isEmpty())
        lastDeletedFiles.push({ trashFilePath, filePath });

    disableActions();
}

QString MainWindow::deleteFileLinuxFallback(const QString &path, bool putBack)
{
    QStringList gioArgs = { "trash", path };
    if (putBack)
        gioArgs.insert(1, "--restore");

    QProcess process;
    process.start("gio", gioArgs);
    process.waitForFinished();

    if (process.error() != QProcess::FailedToStart && !putBack) {
        process.start("gio", { "trash", "--list" });
        process.waitForFinished();

        const auto &output = QString(process.readAllStandardOutput()).split("\n");
        for (const auto &line : output) {
            if (line.contains(path))
                return line.split("\t").at(0);
        }
    }

    qWarning("Failed to use linux fallback delete");
    return "";
}

void MainWindow::undoDelete()
{
    if (lastDeletedFiles.isEmpty())
        return;

    const DeletedPaths lastDeletedFile = lastDeletedFiles.pop();
    if (lastDeletedFile.pathInTrash.isEmpty() || lastDeletedFile.previousPath.isEmpty())
        return;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)) || (defined Q_OS_MACOS && COCOA_LOADED)
    const QFileInfo fileInfo(lastDeletedFile.pathInTrash);
    if (!fileInfo.isWritable()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Can't undo deletion of %1:\n"
                                 "No write permission or file is read-only.")
                                      .arg(fileInfo.fileName()));
        return;
    }

    bool success = QDir().rename(lastDeletedFile.pathInTrash, lastDeletedFile.previousPath);
    if (!success) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed undoing deletion of %1.").arg(fileInfo.fileName()));
    }
#elif defined Q_OS_UNIX && !defined Q_OS_MACOS
    deleteFileLinuxFallback(lastDeletedFile.pathInTrash, true);
#else
    QMessageBox::critical(this, tr("Not Supported"),
                          tr("This program was compiled with an old version of Qt and this feature "
                             "is not available.\n"
                             "If you see this message, please report a bug!"));

    return;
#endif

    if (isBrowsingFolder()) {
        if (galleryView->folderPath() == QFileInfo(lastDeletedFile.previousPath).absolutePath()) reloadFile();
    } else openFile(lastDeletedFile.previousPath);
    disableActions();
}

void MainWindow::copy()
{
    auto *mimeData = graphicsView->getMimeData();
    if (!mimeData->hasImage()) {
        mimeData->deleteLater();
        return;
    }

    QApplication::clipboard()->setMimeData(mimeData);
}

void MainWindow::paste()
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData)
        return;
    const auto media = QVClipboard::read(*mimeData);
    if (!media.urls.isEmpty()) {
        bool first = true;
        for (const QUrl &url : media.urls) {
            if (url.isLocalFile()) {
                if (first) openFile(url.toLocalFile());
                else QVApplication::openFile(url.toLocalFile());
            } else {
                openUrl(url, media.image);
            }
            first = false;
        }
        return;
    }
    saveClipboardMedia(media.bytes, media.mimeType, media.image);
}

void MainWindow::saveClipboardMedia(const QByteArray &bytes, const QString &mimeType, const QImage &image)
{
    const QString suffix = !bytes.isEmpty()
            ? QVClipboard::mediaSuffix(bytes.left(65536), mimeType)
            : (!image.isNull() ? QString("png") : QString());
    if (suffix.isEmpty()) {
        if (!image.isNull() && !bytes.isEmpty()) {
            saveClipboardMedia({}, {}, image);
            return;
        }
        QMessageBox::information(this, tr("Paste"),
                tr("The clipboard contains no supported image, video, file path, or direct media URL."));
        return;
    }
    auto *file = new QTemporaryFile(qvApp);
    file->setFileTemplate(QDir::tempPath() + "/qMedia-XXXXXX." + suffix);
    bool saved = file->open();
    if (saved)
        saved = bytes.isEmpty() ? image.save(file, "PNG")
                                : file->write(bytes) == bytes.size();
    saved = saved && file->flush();
    file->close();
    if (saved)
        openFile(file->fileName());
    else {
        QMessageBox::critical(this, tr("Error"), tr("Could not save clipboard media to a temporary file."));
        file->deleteLater();
    }
}

void MainWindow::rename()
{
    if (!getIsMediaLoaded())
        return;

    auto *renameDialog = new QVRenameDialog(this, getCurrentMedia().fileInfo);
    connect(renameDialog, &QVRenameDialog::newFileToOpen, this, &MainWindow::openFile);
    connect(renameDialog, &QVRenameDialog::readyToRenameFile, this, [this]() {
        graphicsView->closeVideo();
        if (auto device = graphicsView->getLoadedMovie().device()) {
            device->close();
        }
    });

    renameDialog->open();
}

void MainWindow::zoomIn()
{
    if (isBrowsingFolder()) galleryView->zoom(1);
    else graphicsView->zoomIn();
}

void MainWindow::zoomOut()
{
    if (isBrowsingFolder()) galleryView->zoom(-1);
    else graphicsView->zoomOut();
}

void MainWindow::resetZoom()
{
    if (isBrowsingFolder()) galleryView->resetZoom();
    else graphicsView->resetScale();
}

void MainWindow::resetView()
{
    if (isBrowsingFolder()) galleryView->resetZoom();
    else graphicsView->resetView();
}

void MainWindow::rotateRight()
{
    graphicsView->rotateImage(90);
    resetZoom();
}

void MainWindow::rotateLeft()
{
    graphicsView->rotateImage(-90);
    resetZoom();
}

void MainWindow::mirror()
{
    graphicsView->scale(-1, 1);
    resetZoom();
}

void MainWindow::flip()
{
    graphicsView->scale(1, -1);
    resetZoom();
}

void MainWindow::firstFile()
{
    graphicsView->goToFile(QVGraphicsView::GoToFileMode::first);
}

void MainWindow::previousFile()
{
    graphicsView->goToFile(QVGraphicsView::GoToFileMode::previous);
}

void MainWindow::nextFile()
{
    graphicsView->goToFile(QVGraphicsView::GoToFileMode::next);
}

void MainWindow::lastFile()
{
    graphicsView->goToFile(QVGraphicsView::GoToFileMode::last);
}

void MainWindow::saveFrameAs()
{
    if (!graphicsView->isMediaLoaded())
        return;
    cancelSlideshow();
    if (graphicsView->isVideoPlaying()
        || (getImageDetails().isMovieLoaded
            && graphicsView->getLoadedMovie().state() == QMovie::Running)) {
        pause();
    }
    auto *dialog = new QVExportDialog(graphicsView->exportSource(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void MainWindow::pause()
{
    const bool isVideo = getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video;
    if (isVideo) {
        if (!graphicsView->isVideoLoaded())
            return;

        graphicsView->toggleVideoPaused();
        const auto pauseActions = qvApp->getActionManager().getAllClonesOfAction("pause", this);
        for (const auto &pauseAction : pauseActions) {
            pauseAction->setText(graphicsView->isVideoPlaying() ? tr("Pause") : tr("Res&ume"));
            pauseAction->setIcon(QIcon::fromTheme(graphicsView->isVideoPlaying()
                                                          ? "media-playback-pause"
                                                          : "media-playback-start"));
        }
        return;
    }

    if (!getImageDetails().isMovieLoaded)
        return;

    const auto pauseActions = qvApp->getActionManager().getAllClonesOfAction("pause", this);

    if (graphicsView->getLoadedMovie().state() == QMovie::Running) {
        graphicsView->setPaused(true);
        for (const auto &pauseAction : pauseActions) {
            pauseAction->setText(tr("Res&ume"));
            pauseAction->setIcon(QIcon::fromTheme("media-playback-start"));
        }
    } else {
        graphicsView->setPaused(false);
        for (const auto &pauseAction : pauseActions) {
            pauseAction->setText(tr("Pause"));
            pauseAction->setIcon(QIcon::fromTheme("media-playback-pause"));
        }
    }
}

void MainWindow::nextFrame()
{
    const bool isVideo = getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video;
    if ((isVideo && !graphicsView->isVideoLoaded())
        || (!isVideo && !getImageDetails().isMovieLoaded)) {
        return;
    }

    graphicsView->jumpToNextFrame();
    const auto pauseActions = qvApp->getActionManager().getAllClonesOfAction("pause", this);
    for (const auto &pauseAction : pauseActions) {
        pauseAction->setText(tr("Res&ume"));
        pauseAction->setIcon(QIcon::fromTheme("media-playback-start"));
    }
}

void MainWindow::ensureLayersHud()
{
    if (layersHud) return;
    layersHud = new QVLayersHud(graphicsView->layerModel(), graphicsView->viewport());
    connect(layersHud, &QVLayersHud::filtersRequested, this, &MainWindow::openFilters);
    connect(layersHud, &QVLayersHud::distortRequested, graphicsView, &QVGraphicsView::setDistortActive);
    connect(layersHud, &QVLayersHud::cropRequested, graphicsView, &QVGraphicsView::setCropActive);
    connect(layersHud, &QVLayersHud::cropApplyRequested, graphicsView, &QVGraphicsView::applyCrop);
    connect(layersHud, &QVLayersHud::cropResetRequested, graphicsView, &QVGraphicsView::resetCrop);
    connect(graphicsView, &QVGraphicsView::cropActiveChanged, layersHud, &QVLayersHud::setCropActive);
    connect(layersHud, &QVLayersHud::layerSelected, graphicsView, &QVGraphicsView::setDistortLayer);
    connect(layersHud, &QVLayersHud::distortSizeChanged, graphicsView, &QVGraphicsView::setDistortRadius);
    connect(graphicsView, &QVGraphicsView::distortLayerCreated, layersHud, &QVLayersHud::selectLayer);
    connect(graphicsView, &QVGraphicsView::distortRadiusChanged, layersHud, &QVLayersHud::setBrushRadius);
    connect(layersHud, &QVLayersHud::exportRequested, this, &MainWindow::saveFrameAs);
    connect(layersHud, &QVLayersHud::resetViewRequested, this, &MainWindow::resetView);
    layersHud->setSource(getCurrentMedia().fileInfo.fileName(), getIsMediaLoaded());
    layersHud->setDistortAvailable(graphicsView->canDistort());
    graphicsView->setDistortLayer(layersHud->selectedLayerId());
}

void MainWindow::toggleLayers()
{
    ensureLayersHud();
    layersHud->toggle();
}

void MainWindow::openFilters(quint64 layerId)
{
    if (!graphicsView->isMediaLoaded()) return;
    auto *model = graphicsView->layerModel();
    bool hasFilter = false;
    for (const auto &layer : model->stack().layers)
        hasFilter |= layer.kind == QVLayers::Kind::Filter;
    if (!hasFilter) layerId = model->addFilter();
    if (!filtersDialog) filtersDialog = new QVFiltersDialog(model, this);
    filtersDialog->selectLayer(layerId);
    filtersDialog->show();
    filtersDialog->raise();
    filtersDialog->activateWindow();
}

void MainWindow::showFilters()
{
    if (filtersDialog && filtersDialog->isVisible()) {
        filtersDialog->close();
        return;
    }
    openFilters(layersHud ? layersHud->selectedLayerId() : 0);
}

void MainWindow::previousFrame()
{
    const bool isVideo = getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video;
    if ((isVideo && !graphicsView->isVideoLoaded())
        || (!isVideo && !getImageDetails().isMovieLoaded)) {
        return;
    }

    graphicsView->jumpToPreviousFrame();
    const auto pauseActions = qvApp->getActionManager().getAllClonesOfAction("pause", this);
    for (const auto &pauseAction : pauseActions) {
        pauseAction->setText(tr("Res&ume"));
        pauseAction->setIcon(QIcon::fromTheme("media-playback-start"));
    }
}

void MainWindow::toggleMute()
{
    if (getCurrentMedia().mediaType != QVMediaCatalog::MediaType::Video
        || !graphicsView->isVideoLoaded()) {
        return;
    }

    graphicsView->toggleVideoMuted();
    const auto muteActions = qvApp->getActionManager().getAllClonesOfAction("mute", this);
    for (const auto &muteAction : muteActions) {
        muteAction->setText(graphicsView->isVideoMuted() ? tr("Un&mute") : tr("&Mute"));
        muteAction->setIcon(QIcon::fromTheme(graphicsView->isVideoMuted()
                                                     ? "audio-volume-muted"
                                                     : "audio-volume-high"));
    }
}

void MainWindow::toggleLoop()
{
    if (!getImageDetails().isMovieLoaded && !graphicsView->isVideoLoaded())
        return;

    graphicsView->togglePlaybackLoopMode();
    const auto loopActions = qvApp->getActionManager().getAllClonesOfAction("loop", this);
    for (const auto &loopAction : loopActions)
        loopAction->setChecked(graphicsView->isLoopingForced());
}

void MainWindow::seekToPercent(int percent)
{
    const bool isVideo = getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video;
    if ((isVideo && !graphicsView->isVideoLoaded())
        || (!isVideo && !getImageDetails().isMovieLoaded)) {
        return;
    }

    graphicsView->seekToPercent(percent);
}

void MainWindow::toggleSlideshow()
{
    const auto slideshowActions = qvApp->getActionManager().getAllClonesOfAction("slideshow", this);

    if (slideshowTimer->isActive()) {
        slideshowTimer->stop();
        for (const auto &slideshowAction : slideshowActions) {
            slideshowAction->setText(tr("Start S&lideshow"));
            slideshowAction->setIcon(QIcon::fromTheme("media-playback-start"));
        }
    } else {
        slideshowTimer->start(slideshowIntervalMs);
        for (const auto &slideshowAction : slideshowActions) {
            slideshowAction->setText(tr("Stop S&lideshow"));
            slideshowAction->setIcon(QIcon::fromTheme("media-playback-stop"));
        }
    }
    disableActions();
}

void MainWindow::cancelSlideshow()
{
    if (slideshowTimer->isActive())
        toggleSlideshow();
}

void MainWindow::slideshowAction()
{
    // A speed change may have scheduled only the remainder of the previous slide.
    // Subsequent slides use the full interval, without changing slideshow active state.
    if (slideshowTimer->isActive())
        slideshowTimer->setInterval(slideshowIntervalMs);
    if (qvApp->getSettingsManager().getBool(SettingsManager::Setting::SlideshowReversed))
        previousFile();
    else
        nextFile();
}

void MainWindow::decreaseSpeed()
{
    changeSpeed(-25);
}

void MainWindow::resetSpeed()
{
    changeSpeed(0, true);
}

void MainWindow::increaseSpeed()
{
    changeSpeed(25);
}

void MainWindow::updateSlideshowInterval(int intervalMs)
{
    intervalMs = qBound(100, intervalMs, 100000);
    if (slideshowTimer->isActive()) {
        if (intervalMs == slideshowIntervalMs) return;
        // Keep the fraction of the current slide still to play. In particular,
        // repeated shortcut presses must never postpone advancement indefinitely.
        const double remaining = qMax(0, slideshowTimer->remainingTime())
                / double(slideshowIntervalMs);
        slideshowTimer->start(qMax(1, qRound(remaining * intervalMs)));
    } else {
        slideshowTimer->setInterval(intervalMs);
    }
    slideshowIntervalMs = intervalMs;
}

void MainWindow::changeSpeed(int delta, bool reset)
{
    // During a slideshow the shared shortcuts control slide timing, even on animated media.
    if (slideshowTimer->isActive()) {
        updateSlideshowInterval(reset ? slideshowDefaultIntervalMs
                : qRound(slideshowIntervalMs * (delta > 0 ? 1.0 / 1.25 : 1.25)));
        showSpeedIndicator(tr("Slideshow: %1 s / slide")
                                   .arg(slideshowIntervalMs / 1000.0, 0, 'f', 2));
        return;
    }

    int speed;
    if (getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video) {
        if (!graphicsView->isVideoLoaded()) return;
        graphicsView->setVideoPlaybackSpeed(reset ? 100 : graphicsView->videoPlaybackSpeed() + delta);
        speed = graphicsView->videoPlaybackSpeed();
    } else {
        if (!getImageDetails().isMovieLoaded) return;
        graphicsView->setSpeed(reset ? 100 : graphicsView->getLoadedMovie().speed() + delta);
        speed = graphicsView->getLoadedMovie().speed();
    }
    showSpeedIndicator(tr("Playback: %1%").arg(speed));
}

void MainWindow::showSpeedIndicator(const QString &text)
{
    if (!speedIndicator) {
        // Viewport children move when QGraphicsView scrolls the canvas.
        speedIndicator = new QLabel(graphicsView);
        speedIndicator->setObjectName("speedIndicator");
        speedIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
        speedIndicator->setFocusPolicy(Qt::NoFocus);
        speedIndicator->setAlignment(Qt::AlignCenter);
        speedIndicator->setStyleSheet(QStringLiteral(
                "QLabel { background: rgba(30, 30, 30, 220); color: white;"
                " border-radius: 8px; padding: 8px 14px; font-weight: bold; }"));
        speedIndicatorTimer = new QTimer(this);
        speedIndicatorTimer->setSingleShot(true);
        connect(speedIndicatorTimer, &QTimer::timeout, speedIndicator, &QWidget::hide);
    }
    speedIndicator->setText(text);
    positionSpeedIndicator();
    speedIndicator->show();
    speedIndicator->raise();
    speedIndicatorTimer->start(1500);
}

void MainWindow::positionSpeedIndicator()
{
    const QSize available = graphicsView->viewport()->size();
    speedIndicator->resize(speedIndicator->sizeHint().boundedTo(available));
    speedIndicator->move(graphicsView->viewport()->pos()
                         + QPoint((available.width() - speedIndicator->width()) / 2,
                                  qMax(0, available.height() - speedIndicator->height() - 24)));
}

void MainWindow::toggleFullScreen()
{
    // Note: This is only triggered by the menu action, so the logic here should be kept to a
    // minimum. Anything that needs to run even if the window manager initiated the change should be
    // triggered by QEvent::WindowStateChange.

    // Disable updates during window state change to resolve visual glitches on macOS if the
    // titlebar is hidden
    setUpdatesEnabled(false);

    if (windowState().testFlag(Qt::WindowFullScreen)) {
        setWindowState(storedWindowState);
    } else {
        storedWindowState = windowState();

        showFullScreen();
    }

    setUpdatesEnabled(true);
}

int MainWindow::getTitlebarOverlap() const
{
#ifdef COCOA_LOADED
    // To account for fullsizecontentview on mac
    return QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
#endif

    return 0;
}
