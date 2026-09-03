#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "qvrenamedialog.h"
#include "qvclipboard.h"

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
    centralWidget()->layout()->addWidget(graphicsView);

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
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (windowState().testFlag(Qt::WindowFullScreen))
            toggleFullScreen();
    });

    // Enable drag&dropping
    setAcceptDrops(true);

    // Make info dialog object
    info = new QVInfoDialog(this);

    // Timer for slideshow
    slideshowTimer = new QTimer(this);
    connect(slideshowTimer, &QTimer::timeout, this, &MainWindow::slideshowAction);

    // Context menu
    auto &actionManager = qvApp->getActionManager();

    contextMenu = new QMenu(this);

    actionManager.addCloneOfAction(contextMenu, "open");
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

    // Show welcome dialog on first launch
    if (!settings.value("firstlaunch", false).toBool()) {
        settings.setValue("firstlaunch", true);
        settings.setValue("configversion", VERSION);
        qvApp->openWelcomeDialog(this);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        qvApp->addToLastActiveWindows(this);
    }
    return QMainWindow::event(event);
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
    if (event->button() == Qt::MouseButton::BackButton)
        previousFile();
    else if (event->button() == Qt::MouseButton::ForwardButton)
        nextFile();
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

    const QColor &backgroundColor =
            customBackgroundColor.isValid() ? customBackgroundColor : painter.background().color();

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
    graphicsView->loadFile(fileName);
    cancelSlideshow();
}

void MainWindow::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    updateWindowTitle();

    // Cache custom background color since we read it in paintEvent
    customBackgroundColor = settingsManager.getBool(SettingsManager::Setting::BgColorEnabled)
            ? QColor(settingsManager.getString(SettingsManager::Setting::BgColor))
            : QColor();

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
    slideshowTimer->setInterval(static_cast<int>(
            settingsManager.getDouble(SettingsManager::Setting::SlideshowTimer) * 1000));

    ui->fullscreenLabel->setVisible(
            settingsManager.getBool(SettingsManager::Setting::FullScreenDetails)
            && windowState().testFlag(Qt::WindowFullScreen));

    setWindowSize();

    // repaint in case background color changed
    update();
}

void MainWindow::shortcutsUpdated()
{
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
    graphicsView->loadFile(recentsList.value(i).filePath);
    cancelSlideshow();
}

void MainWindow::fileChanged()
{
    populateOpenWithTimer->start();
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
                    clone->setEnabled(getIsMediaLoaded());
                } else if (cloneData.last() == "gifdisable") {
                    clone->setEnabled(getImageDetails().isMovieLoaded);
                } else if (cloneData.last() == "playbackdisable") {
                    clone->setEnabled(getImageDetails().isMovieLoaded
                                      || graphicsView->isVideoLoaded());
                } else if (cloneData.last() == "videodisable") {
                    clone->setEnabled(graphicsView->isVideoLoaded());
                } else if (cloneData.last() == "undodisable") {
                    clone->setEnabled(!lastDeletedFiles.isEmpty()
                                      && !lastDeletedFiles.top().pathInTrash.isEmpty());
                } else if (cloneData.last() == "folderdisable") {
                    clone->setEnabled(!getCurrentMedia().folderFiles.isEmpty());
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
    openWithFutureWatcher.setFuture(QtConcurrent::run([&] {
        const auto &curFilePath = getCurrentMedia().fileInfo.absoluteFilePath();
        return OpenWith::getOpenWithItems(curFilePath);
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
    refreshProperties();
    info->show();
    info->raise();
}

void MainWindow::askDeleteFile(bool permanent)
{
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

    bool success = QFile::rename(lastDeletedFile.pathInTrash, lastDeletedFile.previousPath);
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

    openFile(lastDeletedFile.previousPath);
    disableActions();
}

void MainWindow::copy()
{
    auto *mimeData = graphicsView->getMimeData();
    if (!mimeData->hasImage() || !mimeData->hasUrls()) {
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
    graphicsView->zoomIn();
}

void MainWindow::zoomOut()
{
    graphicsView->zoomOut();
}

void MainWindow::resetZoom()
{
    graphicsView->resetScale();
}

void MainWindow::resetView()
{
    graphicsView->resetView();
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
    QSettings settings;
    settings.beginGroup("recents");
    if (!getImageDetails().isMovieLoaded)
        return;

    if (graphicsView->getLoadedMovie().state() == QMovie::Running) {
        pause();
    }
    QFileDialog *saveDialog = new QFileDialog(this, tr("Save Frame As..."));
    saveDialog->setDirectory(settings.value("lastFileDialogDir", QDir::homePath()).toString());
    saveDialog->setNameFilters(qvApp->getNameFilterList());
    saveDialog->selectFile(getCurrentMedia().fileInfo.baseName() + "-"
                           + QString::number(graphicsView->getLoadedMovie().currentFrameNumber())
                           + ".png");
    saveDialog->setDefaultSuffix("png");
    saveDialog->setAcceptMode(QFileDialog::AcceptSave);
    saveDialog->open();
    connect(saveDialog, &QFileDialog::fileSelected, this, [=](const QString &fileName) {
        graphicsView->originalSize();
        for (int i = 0; i < graphicsView->getLoadedMovie().frameCount(); i++)
            nextFrame();

        graphicsView->getLoadedMovie().currentPixmap().save(fileName, nullptr, 100);
        graphicsView->resetScale();
    });
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
        slideshowTimer->start();
        for (const auto &slideshowAction : slideshowActions) {
            slideshowAction->setText(tr("Stop S&lideshow"));
            slideshowAction->setIcon(QIcon::fromTheme("media-playback-stop"));
        }
    }
}

void MainWindow::cancelSlideshow()
{
    if (slideshowTimer->isActive())
        toggleSlideshow();
}

void MainWindow::slideshowAction()
{
    if (qvApp->getSettingsManager().getBool(SettingsManager::Setting::SlideshowReversed))
        previousFile();
    else
        nextFile();
}

void MainWindow::decreaseSpeed()
{
    if (getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video) {
        if (graphicsView->isVideoLoaded()) {
            graphicsView->setVideoPlaybackSpeed(graphicsView->videoPlaybackSpeed() - 25);
        }
        return;
    }

    if (!getImageDetails().isMovieLoaded)
        return;

    graphicsView->setSpeed(graphicsView->getLoadedMovie().speed() - 25);
}

void MainWindow::resetSpeed()
{
    if (getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video) {
        if (graphicsView->isVideoLoaded())
            graphicsView->setVideoPlaybackSpeed(100);
        return;
    }

    if (!getImageDetails().isMovieLoaded)
        return;

    graphicsView->setSpeed(100);
}

void MainWindow::increaseSpeed()
{
    if (getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video) {
        if (graphicsView->isVideoLoaded()) {
            graphicsView->setVideoPlaybackSpeed(graphicsView->videoPlaybackSpeed() + 25);
        }
        return;
    }

    if (!getImageDetails().isMovieLoaded)
        return;

    graphicsView->setSpeed(graphicsView->getLoadedMovie().speed() + 25);
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
