#include <QtTest>

#include "qvapplication.h"
#include "qvoptionsdialog.h"
#include "qvplaybackloopmode.h"
#include "qvexportdialog.h"
#include "qvfiltersdialog.h"
#include "qvlayershud.h"
#include <QTableWidget>
#include <QListWidget>
#include <QToolButton>
#include <QScrollBar>
#include <QContextMenuEvent>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QThreadPool>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QBuffer>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include "qvclipboard.h"
#include <QSlider>
#include <QWheelEvent>
#include <QGroupBox>
#include <QShortcut>
#include <QPainter>
#include <QGraphicsPixmapItem>
#include "qvfiltereffect.h"
#include "qvgalleryview.h"
#include "qvgallerymodel.h"
#include <QListView>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QTreeView>
#include <QDialogButtonBox>
#include <QMessageBox>
#include "qvfileoperations.h"

class ActionManagerTests : public QObject
{
    Q_OBJECT

public:
    ActionManagerTests();
    ~ActionManagerTests();

private slots:
    void testClonedActionsUntracked();
    void testDuplicateWindow();
    void testCanvasActionsSupportAllVisualMedia();
    void testPlaybackActionsAndDefaultShortcuts();
    void testSpeedControlsAndIndicator();
    void testMediaBackendSetting();
    void testShortcutSearch();
    void testImageRequestAfterVideoIsNotDiscarded();
    void testSvgViewport();
    void testExportDialog();
    void testExportCanvasState();
    void testFilterControlsWheelStep();
    void testHoldCompare();
    void testDistortTool();
    void testDistortShortcut();
    void testCanvasCopy();
    void testCanvasCrop();
    void testDistortCache();
    void testBrushZoom();
    void testEditedCanvasNavigationCost();
    void testNativeComposite();
    void testSessionComposite();
    void testLayersHud();
    void testLayersHudCursor();
    void testDialogToggleShortcuts();
    void testFolderGallery();
    void testGalleryAsyncRequests();
    void testGalleryQuickActions();
    void testCombinedOpenDialog();
    void testGalleryLayoutAndSelection();
    void testGalleryRefreshPosition();
    void testGalleryThumbnailRefresh();
    void testGalleryZoom();
    void testFolderArrowNavigation();
    void testFolderShortcutMigration();
    void testBatchTrash();
};

ActionManagerTests::ActionManagerTests() { }

ActionManagerTests::~ActionManagerTests() { }

void ActionManagerTests::testDuplicateWindow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const QString path = directory.filePath("photo.png");
    QVERIFY(image.save(path));
    MainWindow source;
    source.setAttribute(Qt::WA_DeleteOnClose, false);
    source.show();
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{source};

    struct WindowCleanup {
        static void cleanup(MainWindow *window) {
            window->setAttribute(Qt::WA_DeleteOnClose, false);
            window->close();
            delete window;
        }
    };
    QScopedPointer<MainWindow, WindowCleanup> home(source.duplicateWindow());
    QVERIFY(home->findChild<QVGalleryView *>()->isHome());
    source.showFolder(directory.path());
    QScopedPointer<MainWindow, WindowCleanup> folder(source.duplicateWindow());
    QVERIFY(folder->isBrowsingFolder());
    QCOMPARE(folder->findChild<QVGalleryView *>()->folderPath(), directory.path());

    source.openFile(path);
    QTRY_VERIFY(source.getIsMediaLoaded());
    const auto before = QApplication::topLevelWidgets();
    const auto actions = qvApp->getActionManager().getAllClonesOfAction("newwindow", &source);
    QVERIFY(!actions.isEmpty());
    actions.first()->trigger();
    MainWindow *created = nullptr;
    for (auto *widget : QApplication::topLevelWidgets()) {
        if (!before.contains(widget) && qobject_cast<MainWindow *>(widget))
            created = qobject_cast<MainWindow *>(widget);
    }
    QVERIFY(created);
    QScopedPointer<MainWindow, WindowCleanup> duplicate(created);
    QTRY_VERIFY(duplicate->getIsMediaLoaded());
    QCOMPARE(duplicate->getCurrentMedia().fileInfo.absoluteFilePath(), path);
    duplicate->showHome();
    QVERIFY(source.getIsMediaLoaded());
    QCOMPARE(source.getCurrentMedia().fileInfo.absoluteFilePath(), path);
}

void ActionManagerTests::testFolderGallery()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkdir("empty"));
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const QString path = directory.filePath("photo.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    auto *gallery = window.findChild<QVGalleryView *>();
    auto *grid = window.findChild<QListView *>("folderGrid");
    auto *model = window.findChild<QVGalleryModel *>();
    auto *canvas = window.findChild<QVGraphicsView *>();
    QVERIFY(gallery && grid && model && canvas);
    QVERIFY(gallery->isHome());
    QVERIFY(gallery->isVisible());
    const QString previewDir = qEnvironmentVariable("QMEDIA_GALLERY_PREVIEW_DIR");
    if (!previewDir.isEmpty()) {
        window.resize(800, 600);
        window.findChild<QPushButton *>("homeOpen")->setFocus(Qt::TabFocusReason);
        QVERIFY(window.grab().save(previewDir + "/home.png"));
    }
    // A pasted folder path must browse, even when it contains supported media.
    QApplication::clipboard()->setText(QDir::toNativeSeparators(directory.path()));
    window.paste();
    QVERIFY(window.isBrowsingFolder());
    QVERIFY(!window.getIsMediaLoaded());
    QTRY_COMPARE(model->rowCount(), 2);
    QVERIFY(!window.getCurrentMedia().isLoadRequested);
    QCOMPARE(model->index(0).data(QVGalleryModel::DirectoryRole).toBool(), true);
    if (!previewDir.isEmpty()) {
        QTRY_VERIFY(!qvariant_cast<QImage>(model->index(1).data(Qt::DecorationRole)).isNull());
        QVERIFY(window.grab().save(previewDir + "/folder.png"));
    }
    for (auto *action : qvApp->getActionManager().getAllClonesOfAction("nextfile", &window))
        QVERIFY(!action->isEnabled());
    grid->setCurrentIndex(model->index(1));
    grid->setFocus();
    QTest::keyClick(grid, Qt::Key_Return);
    QTRY_VERIFY(window.getIsMediaLoaded());
    QVERIFY(!gallery->isVisible());
    QVERIFY(canvas->isVisible());
    QCOMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), path);
    const QSize mediaSize = window.size();
    window.browseParentFolder();
    QTRY_COMPARE(model->rowCount(), 2);
    QTRY_COMPARE(grid->currentIndex().data(QVGalleryModel::PathRole).toString(), path);
    QCOMPARE(window.size(), mediaSize);
    QVERIFY(!window.getIsMediaLoaded());
    window.openFile(directory.filePath("empty"));
    QVERIFY(window.isBrowsingFolder());
    QTRY_COMPARE(model->rowCount(), 0);
    window.browseParentFolder();
    QTRY_COMPARE(model->rowCount(), 2);
    QCOMPARE(grid->currentIndex().data(QVGalleryModel::PathRole).toString(), directory.filePath("empty"));
    // A drop routed through the existing canvas takes the same folder path.
    QMimeData drop;
    drop.setUrls({ QUrl::fromLocalFile(directory.path()) });
    canvas->loadMimeData(&drop);
    QVERIFY(window.isBrowsingFolder());
    // Completion of a pending image load must not resurrect media on Home.
    window.openFile(path);
    window.showHome();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    QVERIFY(gallery->isHome());
    QVERIFY(!window.getIsMediaLoaded());
    QVERIFY(!window.getCurrentMedia().isLoadRequested);
    window.openFile(path);
    QTRY_VERIFY(window.getIsMediaLoaded());
}

void ActionManagerTests::testGalleryQuickActions()
{
    const QVariant previousColor = QSettings().value("options/bgcolor");
    QSettings().setValue("options/bgcolor", "#183040");
    qvApp->getSettingsManager().loadSettings();
    struct RestoreColor {
        QVariant previous;
        ~RestoreColor() {
            if (previous.isValid()) QSettings().setValue("options/bgcolor", previous);
            else QSettings().remove("options/bgcolor");
            qvApp->getSettingsManager().loadSettings();
        }
    } restore{previousColor};
    QTemporaryDir directory;
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const QString path = directory.filePath("photo.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    auto *gallery = window.findChild<QVGalleryView *>();
    auto *grid = window.findChild<QListView *>("folderGrid");
    auto *canvas = window.findChild<QVGraphicsView *>();
    QCOMPARE(gallery->palette().color(QPalette::Base), QColor("#183040"));
    QCOMPARE(grid->viewport()->palette().color(QPalette::Base), QColor("#183040"));
    window.toggleBackgroundColor();
    QCOMPARE(gallery->palette().color(QPalette::Base), QColor(qvGetSettingString(AlternateBgColor)));
    window.toggleBackgroundColor();
    QCOMPARE(gallery->palette().color(QPalette::Base), QColor("#183040"));
    // Check propagation from a blank child on Home, not just the window itself.
    QTRY_VERIFY(window.isActiveWindow());
    const QPoint blank = gallery->rect().bottomRight() - QPoint(30, 30);
    QWidget *surface = gallery->childAt(blank);
    QVERIFY(surface);
    QTest::mouseDClick(surface, Qt::LeftButton, Qt::NoModifier, surface->mapFrom(gallery, blank));
    QVERIFY(window.isFullScreen());
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!window.isFullScreen());
    QVERIFY(gallery->isHome());
    window.showFolder(directory.path());
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    const auto doubleClickBlankGrid = [&] {
        const QPoint point(1, 1); // A card gutter stays empty even in a tiny media-sized window.
        QVERIFY(!grid->indexAt(point).isValid());
        QTest::mouseDClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    };
    doubleClickBlankGrid();
    QVERIFY(window.isFullScreen());
    doubleClickBlankGrid();
    QVERIFY(!window.isFullScreen());
    QVERIFY(window.isBrowsingFolder());
    window.openFile(path);
    QTRY_VERIFY(window.getIsMediaLoaded());
    QVERIFY(!window.findChild<QToolButton *>("viewerFolderButton"));
    QTest::keyClick(canvas, Qt::Key_Escape);
    QVERIFY(canvas->isVisible());
    QVERIFY(!window.isBrowsingFolder());
    QCOMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), path);
    window.showFullScreen();
    QTest::keyClick(canvas, Qt::Key_Escape);
    QVERIFY(!window.isFullScreen());
    QVERIFY(canvas->isVisible());
    QCOMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), path);
}

void ActionManagerTests::testCombinedOpenDialog()
{
    QTemporaryDir directory;
    QVERIFY(QDir(directory.path()).mkdir("album"));
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const QString path = directory.filePath("photo.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    auto *open = window.findChild<QPushButton *>("homeOpen");
    QVERIFY(open);
    for (const QString &selected : { directory.filePath("album"), path }) {
        open->click();
        auto *dialog = window.findChild<QFileDialog *>();
        QVERIFY(dialog);
        dialog->setViewMode(QFileDialog::Detail);
        dialog->setDirectory(directory.path());
        auto *tree = dialog->findChild<QTreeView *>("treeView");
        QVERIFY(tree);
        auto *files = qobject_cast<QFileSystemModel *>(tree->model());
        QVERIFY(files);
        QTRY_VERIFY(tree->model()->rowCount(tree->rootIndex()) >= 2);
        const QModelIndex index = files->index(selected);
        QVERIFY(index.isValid());
        tree->setCurrentIndex(index);
        tree->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        QTRY_COMPARE(dialog->selectedFiles(), QStringList{selected});
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(buttons && buttons->button(QDialogButtonBox::Open));
        QVERIFY(buttons->button(QDialogButtonBox::Open)->isEnabled());
        QSignalSpy accepted(dialog, &QFileDialog::filesSelected);
        buttons->button(QDialogButtonBox::Open)->click();
        QCOMPARE(accepted.count(), 1);
        if (QFileInfo(selected).isDir()) {
            QVERIFY(window.isBrowsingFolder());
            QCOMPARE(window.findChild<QVGalleryView *>()->folderPath(), selected);
        } else {
            QTRY_VERIFY(window.getIsMediaLoaded());
            QCOMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), selected);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.showHome();
    }
}

void ActionManagerTests::testGalleryLayoutAndSelection()
{
    QTemporaryDir directory;
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::green);
    for (int i = 0; i < 18; ++i) QVERIFY(image.save(directory.filePath(QString("photo%1.png").arg(i))));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.showFolder(directory.path());
    auto *gallery = window.findChild<QVGalleryView *>();
    auto *grid = window.findChild<QListView *>("folderGrid");
    QTRY_COMPARE(grid->model()->rowCount(), 18);
    const auto fillsWidth = [&] {
        const int width = grid->viewport()->width();
        const int columns = qMax(1, width / 176);
        const QRect first = grid->visualRect(grid->model()->index(0, 0));
        const QRect last = grid->visualRect(grid->model()->index(columns - 1, 0));
        const int rounding = width - 2 - last.right();
        return first.left() == 0 && last.top() == first.top() && rounding >= 0 && rounding < columns;
    };
    const auto layoutDetails = [&] {
        QString details;
        QDebug log(&details);
        log << "viewport" << grid->viewport()->size() << "rectangles";
        for (int i = 0; i < 8; ++i) log << grid->visualRect(grid->model()->index(i, 0));
        const QString previewDir = qEnvironmentVariable("QMEDIA_GALLERY_PREVIEW_DIR");
        if (!previewDir.isEmpty()) window.grab().save(previewDir + "/adaptive-folder.png");
        return details;
    };
    for (int width : {640, 777, 1001, 1459}) {
        window.resize(width, 580);
        QTRY_VERIFY2(fillsWidth(), qPrintable(layoutDetails()));
        if (width == 640) {
            QVERIFY(!grid->verticalScrollBar()->isVisible());
            QTRY_VERIFY(grid->verticalScrollBar()->maximum() > 0);
            const QPoint point = grid->viewport()->rect().center();
            QWheelEvent wheel(point, grid->viewport()->mapToGlobal(point), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(grid->viewport(), &wheel);
            QVERIFY(grid->verticalScrollBar()->value() > 0);
            grid->verticalScrollBar()->setValue(0);
        }
    }
    window.showFullScreen();
    QTRY_VERIFY(fillsWidth());
    window.showNormal();
    QTRY_VERIFY(fillsWidth());
    grid->verticalScrollBar()->setValue(0);
    QTRY_VERIFY(window.isActiveWindow());
    const QPoint first = grid->visualRect(grid->model()->index(0, 0)).center();
    const QPoint second = grid->visualRect(grid->model()->index(1, 0)).center();
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, first);
    QCOMPARE(gallery->selectedPaths().size(), 1);
    QVERIFY(window.isBrowsingFolder());
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::ControlModifier, second);
    QCOMPARE(gallery->selectedPaths().size(), 2);
    auto *countOverlay = window.findChild<QLabel *>("gallerySelectionCount");
    QVERIFY(countOverlay && countOverlay->isVisible());
    QCOMPARE(countOverlay->text(), QString("2 selected"));
    QTest::keyClick(grid, Qt::Key_Escape);
    QVERIFY(!gallery->hasSelection());
    QVERIFY(!countOverlay->isVisible());
    QVERIFY(!grid->verticalScrollBar()->isVisible());
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, first);
    const QPoint empty(1, 1); // The gutter is also empty space, not part of a tile.
    QVERIFY(!grid->indexAt(empty).isValid());
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, empty);
    QVERIFY(!gallery->hasSelection());
    QTest::mousePress(grid->viewport(), Qt::LeftButton, Qt::NoModifier, empty);
    const QPoint end(second.x(), second.y());
    QMouseEvent move(QEvent::MouseMove, end, grid->viewport()->mapToGlobal(end),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(grid->viewport(), &move);
    QTest::mouseRelease(grid->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(gallery->selectedPaths().size(), 2);
    // Delete works on the selection, with cancellation leaving every item intact.
    const QString previewDir = qEnvironmentVariable("QMEDIA_GALLERY_PREVIEW_DIR");
    if (!previewDir.isEmpty()) QVERIFY(window.grab().save(previewDir + "/adaptive-folder.png"));
    QTest::keyClick(grid, Qt::Key_Delete);
    auto *confirmation = window.findChild<QMessageBox *>("galleryTrashConfirmation");
    QVERIFY(confirmation);
    QVERIFY(confirmation->text().contains("2"));
    confirmation->button(QMessageBox::No)->click();
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 18);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // QTest's double-click helper sends only the second press of the sequence.
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, first);
    QTest::mouseDClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, first);
    QTRY_VERIFY(window.getIsMediaLoaded());
}

void ActionManagerTests::testGalleryThumbnailRefresh()
{
    QTemporaryDir directory;
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    for (const QString &name : {QString("a.png"), QString("b.png"), QString("c.png")})
        QVERIFY(image.save(directory.filePath(name)));
    QVMediaCatalog::ScanOptions options;
    options.supportedMedia.append({QVMediaCatalog::MediaType::Image, {".png"}, {}});
    QVGalleryModel model;
    QSignalSpy loaded(&model, &QVGalleryModel::folderLoaded);
    model.openFolder(directory.path(), options);
    QTRY_COMPARE(model.rowCount(), 3);
    QTRY_VERIFY(!qvariant_cast<QImage>(model.index(1).data(Qt::DecorationRole)).isNull());
    QTRY_VERIFY(!qvariant_cast<QImage>(model.index(2).data(Qt::DecorationRole)).isNull());
    const QImage b = qvariant_cast<QImage>(model.index(1).data(Qt::DecorationRole));
    const QImage c = qvariant_cast<QImage>(model.index(2).data(Qt::DecorationRole));
    QVERIFY(QFile::remove(directory.filePath("a.png")));
    model.openFolder(directory.path(), options);
    QCOMPARE(model.rowCount(), 3); // The current view stays populated during the scan.
    QTRY_COMPARE(loaded.count(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(qvariant_cast<QImage>(model.index(0).data(Qt::DecorationRole)).cacheKey(), b.cacheKey());
    QCOMPARE(qvariant_cast<QImage>(model.index(1).data(Qt::DecorationRole)).cacheKey(), c.cacheKey());
    image = QImage(100, 90, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(directory.filePath("b.png")));
    model.openFolder(directory.path(), options);
    QTRY_COMPARE(loaded.count(), 3);
    QTRY_COMPARE(qvariant_cast<QImage>(model.index(0).data(Qt::DecorationRole)).pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(qvariant_cast<QImage>(model.index(1).data(Qt::DecorationRole)).cacheKey(), c.cacheKey());
}

void ActionManagerTests::testGalleryZoom()
{
    QTemporaryDir directory;
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    for (int i = 0; i < 24; ++i) QVERIFY(image.save(directory.filePath(QString("photo%1.png").arg(i))));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.resize(1000, 600);
    window.showFolder(directory.path());
    auto *grid = window.findChild<QListView *>("folderGrid");
    QTRY_COMPARE(grid->model()->rowCount(), 24);
    QTRY_VERIFY(grid->gridSize().width() > 0);
    grid->setFocus();
    const int original = grid->gridSize().width();
    QTest::keyClick(grid, Qt::Key_Plus);
    QTRY_VERIFY(grid->gridSize().width() > original);
    const auto fillsWidth = [&] {
        const int columns = qMax(1, grid->viewport()->width() / grid->property("preferredTileWidth").toInt());
        const QRect first = grid->visualRect(grid->model()->index(0, 0));
        const QRect last = grid->visualRect(grid->model()->index(columns - 1, 0));
        return first.top() == last.top() && grid->viewport()->width() - 1 - last.right() <= columns;
    };
    QTRY_VERIFY(fillsWidth());
    window.resize(873, 600);
    QTRY_VERIFY(fillsWidth());
    QTest::keyClick(grid, Qt::Key_0, Qt::ControlModifier);
    QTRY_COMPARE(grid->property("preferredTileWidth").toInt(), 176);
    QTest::keyClick(grid, Qt::Key_Minus);
    QTRY_VERIFY(grid->property("preferredTileWidth").toInt() < 176);
    QTRY_VERIFY(fillsWidth());
    window.openFile(directory.filePath("photo0.png"));
    QTRY_VERIFY(window.getIsMediaLoaded());
    auto *canvas = window.findChild<QVGraphicsView *>();
    const qreal scale = canvas->transform().m11();
    QTest::keyClick(canvas, Qt::Key_Plus);
    QTRY_VERIFY(canvas->transform().m11() > scale);
    const qreal larger = canvas->transform().m11();
    QTest::keyClick(canvas, Qt::Key_Minus);
    QTRY_VERIFY(canvas->transform().m11() < larger);
    const auto *reset = qvApp->getActionManager().getAction("originalsize");
    QVERIFY(reset->shortcuts().contains(QKeySequence(Qt::Key_O)));
    QVERIFY(reset->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_0)));
    QVERIFY(!qvApp->getActionManager().getAction("resetzoom")->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_0)));
    QTest::keyClick(canvas, Qt::Key_O);
    const QTransform fitted = canvas->transform();
    const QPointF center = canvas->mapToScene(canvas->viewport()->rect().center());
    canvas->scale(2.0, 2.0);
    static_cast<QGraphicsView *>(canvas)->centerOn(center + QPointF(15, 10));
    QTest::keyClick(canvas, Qt::Key_0, Qt::ControlModifier);
    QCOMPARE(canvas->transform(), fitted);
    QCOMPARE(canvas->mapToScene(canvas->viewport()->rect().center()), center);
}

void ActionManagerTests::testGalleryRefreshPosition()
{
    QTemporaryDir directory;
    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    for (int i = 0; i < 320; ++i)
        QVERIFY(image.save(directory.filePath(QString("photo%1.png").arg(i))));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.resize(640, 480);
    window.showFolder(directory.path());
    auto *grid = window.findChild<QListView *>("folderGrid");
    auto *gallery = window.findChild<QVGalleryView *>();
    QTRY_COMPARE(grid->model()->rowCount(), 320);
    QTRY_VERIFY(grid->visualRect(grid->model()->index(0, 0)).top() >= 60);
    grid->setCurrentIndex(grid->model()->index(0, 0));
    grid->setFocus();
    QTest::keyClick(grid, Qt::Key_Down);
    QVERIFY(grid->currentIndex().row() > 0);
    QVERIFY(window.isBrowsingFolder());
    QTest::keyClick(grid, Qt::Key_Up);
    QCOMPARE(grid->currentIndex().row(), 0);
    QTRY_VERIFY(grid->verticalScrollBar()->maximum() > 8000);
    grid->verticalScrollBar()->setValue(8000);
    const int position = grid->verticalScrollBar()->value();
    // Simulate a successful deletion, then use the same reload path as batch trash.
    QVERIFY(QFile::remove(directory.filePath("photo175.png")));
    window.reloadFile();
    QTRY_COMPARE(grid->model()->rowCount(), 319);
    QTRY_COMPARE(grid->verticalScrollBar()->value(), position);
    QTRY_VERIFY(grid->visualRect(grid->model()->index(318, 0)).isValid());
    QCOMPARE(grid->verticalScrollBar()->value(), position);
    QVERIFY(!gallery->hasSelection());
    grid->verticalScrollBar()->setValue(grid->verticalScrollBar()->maximum());
    for (int i = 310; i < 320; ++i)
        QVERIFY(QFile::remove(directory.filePath(QString("photo%1.png").arg(i))));
    window.reloadFile();
    QTRY_COMPARE(grid->model()->rowCount(), 309);
    QTRY_VERIFY(grid->visualRect(grid->model()->index(308, 0)).isValid());
    QTRY_COMPARE(grid->verticalScrollBar()->value(), grid->verticalScrollBar()->maximum());
    grid->verticalScrollBar()->setValue(grid->verticalScrollBar()->minimum());
    QVERIFY(grid->visualRect(grid->model()->index(0, 0)).top() >= 60);
    const auto ordered = gallery->mediaFiles();
    window.openFile(ordered.first().absoluteFilePath);
    QTRY_VERIFY(window.getIsMediaLoaded());
    auto *core = window.findChild<QVImageCore *>();
    QVERIFY(core);
    for (int i = 1; i < 5; ++i) {
        QTRY_VERIFY(!core->isLoadInProgress());
        window.nextFile();
        QTRY_COMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), ordered[i].absoluteFilePath);
        QTRY_VERIFY(!core->isLoadInProgress());
        QTRY_VERIFY(window.getIsMediaLoaded());
    }
    for (int i = 3; i >= 0; --i) {
        window.previousFile();
        QTRY_COMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), ordered[i].absoluteFilePath);
        QTRY_VERIFY(!core->isLoadInProgress());
    }
}

void ActionManagerTests::testFolderArrowNavigation()
{
    QTemporaryDir directory;
    QVERIFY(QDir(directory.path()).mkpath("album/set"));
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const QString path = directory.filePath("album/set/photo.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.openFile(path);
    QTRY_VERIFY(window.getIsMediaLoaded());
    auto *gallery = window.findChild<QVGalleryView *>();
    auto *grid = window.findChild<QListView *>("folderGrid");
    auto *canvas = window.findChild<QVGraphicsView *>();
    {
        QLineEdit editor(&window);
        editor.show();
        editor.setFocus();
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), path);
        QVERIFY(!window.isBrowsingFolder());
    }
    window.resize(321, 237);
    const QSize mediaWindowSize = window.size();
    canvas->setFocus();
    QTest::mouseClick(canvas->viewport(), Qt::BackButton);
    QVERIFY(window.isBrowsingFolder());
    QCOMPARE(window.size(), mediaWindowSize);
    QCOMPARE(gallery->folderPath(), directory.filePath("album/set"));
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QCOMPARE(window.size(), mediaWindowSize);
    QTest::mouseClick(grid->viewport(), Qt::BackButton);
    QCOMPARE(gallery->folderPath(), directory.filePath("album"));
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QTest::mouseClick(grid->viewport(), Qt::ForwardButton);
    QCOMPARE(gallery->folderPath(), directory.filePath("album/set"));
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QTest::mouseClick(grid->viewport(), Qt::ForwardButton);
    QTRY_VERIFY(window.getIsMediaLoaded());
    QCOMPARE(window.getCurrentMedia().fileInfo.absoluteFilePath(), path);
    // Explicit navigation starts a new branch instead of reopening an unrelated file.
    window.showFolder(directory.path());
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QTest::mouseClick(grid->viewport(), Qt::ForwardButton);
    QCOMPARE(gallery->folderPath(), directory.path());
    QVERIFY(image.save(directory.filePath("new.png")));
    QTest::keyClick(grid, Qt::Key_R, Qt::ControlModifier);
    QTRY_COMPARE(grid->model()->rowCount(), 2);
    const QSize gallerySize = window.size();
    QTest::keyClick(grid, Qt::Key_H, Qt::ControlModifier);
    QVERIFY(gallery->isHome());
    QCOMPARE(window.size(), gallerySize);
}

void ActionManagerTests::testFolderShortcutMigration()
{
    const QStringList keys{"rotateright", "rotateleft", "browsefolder", "browsechild", "home", "reloadfile", "rename", "folderNavigationDefaultsMigrated", "minimalGalleryDefaultsMigrated", "gallerySelectionArrowsMigrated"};
    QMap<QString, QVariant> before;
    for (const QString &key : keys) before.insert("shortcuts/" + key, QSettings().value("shortcuts/" + key));
    struct Restore {
        QMap<QString, QVariant> values;
        ~Restore() {
            for (auto i = values.begin(); i != values.end(); ++i) {
                if (i.value().isValid()) QSettings().setValue(i.key(), i.value());
                else QSettings().remove(i.key());
            }
            qvApp->getShortcutManager().updateShortcuts();
        }
    } restore{before};
    QSettings().setValue("shortcuts/rotateright", QStringList{QKeySequence(Qt::Key_Up).toString()});
    QSettings().setValue("shortcuts/rotateleft", QStringList{QKeySequence(Qt::Key_Down).toString()});
    QSettings().remove("shortcuts/folderNavigationDefaultsMigrated");
    QSettings().remove("shortcuts/minimalGalleryDefaultsMigrated");
    QSettings().remove("shortcuts/gallerySelectionArrowsMigrated");
    qvApp->getShortcutManager().updateShortcuts();
    QCOMPARE(qvApp->getActionManager().getAction("rotateright")->shortcut(), QKeySequence(Qt::Key_T));
    QCOMPARE(qvApp->getActionManager().getAction("rotateleft")->shortcut(), QKeySequence(Qt::Key_R));
    QCOMPARE(qvApp->getActionManager().getAction("browsefolder")->shortcut(), QKeySequence(Qt::ALT | Qt::Key_Up));
    QCOMPARE(qvApp->getActionManager().getAction("browsechild")->shortcut(), QKeySequence(Qt::ALT | Qt::Key_Down));
    QSettings().setValue("shortcuts/rotateright", QStringList{QKeySequence(Qt::ALT | Qt::Key_X).toString()});
    QSettings().remove("shortcuts/folderNavigationDefaultsMigrated");
    QSettings().remove("shortcuts/minimalGalleryDefaultsMigrated");
    QSettings().remove("shortcuts/gallerySelectionArrowsMigrated");
    qvApp->getShortcutManager().updateShortcuts();
    QCOMPARE(qvApp->getActionManager().getAction("rotateright")->shortcut(), QKeySequence(Qt::ALT | Qt::Key_X));
}

void ActionManagerTests::testBatchTrash()
{
    QTemporaryDir directory;
    const QString file = directory.filePath("photo.png");
    const QString folder = directory.filePath("album");
    const QString missing = directory.filePath("missing.png");
    QVERIFY(QDir(directory.path()).mkdir("album"));
    QVERIFY(QDir(directory.path()).mkdir("test-trash"));
    QFile fixture(file);
    QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write("fixture");
    fixture.close();
    int operations = 0;
    const auto results = QVFileOperations::trash({file, folder, file, missing},
            [&](const QString &source, QString *destination, QString *error) {
        ++operations;
        *destination = directory.filePath("test-trash/" + QFileInfo(source).fileName());
        if (QDir().rename(source, *destination)) return true;
        *error = "Unable to move item";
        return false;
    });
    QCOMPARE(operations, 3);
    QCOMPARE(results.size(), 3);
    QVERIFY(results[0].error.isEmpty());
    QVERIFY(results[1].error.isEmpty());
    QVERIFY(QFileInfo::exists(results[0].trashPath));
    QVERIFY(QFileInfo(results[1].trashPath).isDir());
    QVERIFY(!results[2].error.isEmpty());
    QVERIFY(results[2].trashPath.isEmpty());
}

void ActionManagerTests::testGalleryAsyncRequests()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QImage image(80, 50, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(first.filePath("first.png")));
    QVERIFY(image.save(second.filePath("second.png")));
    QVMediaCatalog::ScanOptions options;
    options.supportedMedia.append({ QVMediaCatalog::MediaType::Image, { ".png" }, {} });
    QVGalleryModel model;
    QSignalSpy loaded(&model, &QVGalleryModel::folderLoaded);
    model.openFolder(first.path(), options);
    model.openFolder(second.path(), options);
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.index(0).data().toString(), QString("second.png"));
    // The first request returns a placeholder; a worker produces the preview.
    QVERIFY(model.index(0).data(Qt::DecorationRole).isNull());
    QTRY_VERIFY(!qvariant_cast<QImage>(model.index(0).data(Qt::DecorationRole)).isNull());
    const QImage thumbnail = qvariant_cast<QImage>(model.index(0).data(Qt::DecorationRole));
    QVERIFY(thumbnail.width() <= 320 && thumbnail.height() <= 224);
    QCOMPARE(thumbnail.pixelColor(0, 0), QColor(Qt::red));
    model.openFolder(first.path(), options);
    model.clear();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(loaded.count(), 1);
    model.openFolder(first.filePath("missing"), options);
    QTRY_COMPARE(loaded.count(), 2);
    QVERIFY(!loaded.last().first().toString().isEmpty());
}

void ActionManagerTests::testFilterControlsWheelStep()
{
    QVLayerModel model;
    const auto id = model.addFilter();
    QVFiltersDialog controls(&model);
    auto *brightness = controls.findChild<QSlider *>("brightnessSlider");
    QVERIFY(brightness);
    QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(brightness, &wheel);
    QCOMPARE(model.stack().layers[0].filter.brightness, 1);
    auto entry = model.stack().layers[0];
    entry.name = "Evening";
    entry.strength = 40;
    entry.visible = false;
    entry.filter.hue = 90;
    model.update(entry);
    QCOMPARE(controls.findChild<QSlider *>("hueSlider")->value(), 90);
    QCOMPARE(controls.findChild<QComboBox *>("filterLayerSelector")->currentText(), QString("Evening"));
    controls.findChild<QPushButton *>("filterResetLayer")->click();
    QVERIFY(model.stack().layers[0].filter.isNeutral());
    QCOMPARE(model.stack().layers[0].strength, 40);
    QVERIFY(!model.stack().layers[0].visible);
    const auto duplicate = model.duplicate(id);
    controls.selectLayer(id);
    model.move(id, 0);
    brightness->setValue(23);
    QCOMPARE(model.stack().layers[model.indexOf(id)].filter.brightness, 23);
    QCOMPARE(model.stack().layers[model.indexOf(duplicate)].filter.brightness, 0);
    model.remove(id);
    model.remove(duplicate);
    QVERIFY(!brightness->isEnabled());
    controls.findChild<QPushButton *>("filterAddLayer")->click();
    QVERIFY(brightness->isEnabled());
    controls.show();
    auto *toggle = controls.findChild<QShortcut *>("filtersToggleShortcut");
    QVERIFY(toggle);
    QCOMPARE(toggle->key(), QKeySequence(Qt::Key_U));
    QVERIFY(QMetaObject::invokeMethod(toggle, "activated"));
    QVERIFY(!controls.isVisible());
}

void ActionManagerTests::testShortcutSearch()
{
    QVOptionsDialog options;
    options.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *search = options.findChild<QLineEdit *>("shortcutsSearch");
    auto *mode = options.findChild<QComboBox *>("shortcutsSearchMode");
    auto *table = options.findChild<QTableWidget *>("shortcutsTable");
    QVERIFY(search && mode && table);
    const auto shortcuts = qvApp->getShortcutManager().getShortcutsList();
    const auto rowFor = [&](const QString &name) {
        for (int row = 0; row < shortcuts.size(); ++row)
            if (shortcuts[row].name == name) return row;
        return -1;
    };
    const int compare = rowFor("compareoriginal");
    const int copy = rowFor("copy");
    const int background = rowFor("togglewhitebackground");
    QVERIFY(compare >= 0);
    QVERIFY(copy >= 0);
    QVERIFY(background >= 0);
    search->setText(" c ");
    QVERIFY(!table->isRowHidden(background));
    mode->setCurrentIndex(2);
    QVERIFY(!table->isRowHidden(compare));
    QVERIFY(!table->isRowHidden(copy));
    QVERIFY(table->isRowHidden(background));
    const auto copyKeys = ShortcutManager::stringListToKeySequenceList(shortcuts[copy].shortcuts);
    QVERIFY(!copyKeys.isEmpty());
    search->setText(copyKeys.first().toString(QKeySequence::NativeText));
    QVERIFY(!table->isRowHidden(copy));
    QVERIFY(table->isRowHidden(compare));
    search->setText("not a key");
    for (int row = 0; row < table->rowCount(); ++row) QVERIFY(table->isRowHidden(row));
    mode->setCurrentIndex(1);
    search->setText("COMPARE ORIGINAL");
    QVERIFY(!table->isRowHidden(compare));
    QVERIFY(table->isRowHidden(copy));
    search->clear();
    for (int row = 0; row < table->rowCount(); ++row) QVERIFY(!table->isRowHidden(row));

    // Language selection already exists on the Miscellaneous page.
    auto *categories = options.findChild<QListWidget *>("categoryList");
    auto *languages = options.findChild<QComboBox *>("langComboBox");
    QVERIFY(categories && languages);
    categories->setCurrentRow(2);
    QVERIFY(languages->isVisibleTo(&options));
    QVERIFY(languages->findData("system") >= 0);
    QVERIFY(languages->findData("en") >= 0);
    // This test target may omit translation resources; validate any it includes.
    for (int index = 2; index < languages->count(); ++index) {
        const QString locale = languages->itemData(index).toString();
        QVERIFY(!locale.startsWith('_'));
        QVERIFY(QLocale(locale).language() != QLocale::C);
    }
}

void ActionManagerTests::testHoldCompare()
{
    QTemporaryDir directory;
    QImage original(80, 60, QImage::Format_ARGB32);
    original.fill(QColor(40, 90, 140));
    const QString path = directory.filePath("compare.png");
    QVERIFY(original.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{ window };
    window.show();
    window.openFile(path);
    auto *canvas = window.findChild<QVGraphicsView *>();
    QTRY_VERIFY(canvas->isMediaLoaded());
    auto *model = canvas->layerModel();
    model->addFilter();
    auto layer = model->stack().layers[0];
    layer.filter.brightness = 40;
    model->update(layer);
    const auto stack = model->stack();
    canvas->zoom(1.7);
    const auto transform = canvas->transform();
    const auto center = canvas->mapToScene(canvas->viewport()->rect().center());
    QTest::keyPress(canvas, Qt::Key_C);
    QVERIFY(canvas->isComparingOriginal());
    QKeyEvent repeatRelease(QEvent::KeyRelease, Qt::Key_C, Qt::NoModifier, "c", true);
    QApplication::sendEvent(canvas, &repeatRelease);
    QVERIFY(canvas->isComparingOriginal());
    QVERIFY(model->stack().samePixels(stack));
    QVERIFY(canvas->exportSource().layers.samePixels(stack));
    QCOMPARE(canvas->transform(), transform);
    QCOMPARE(canvas->mapToScene(canvas->viewport()->rect().center()), center);
    QTest::keyRelease(&window, Qt::Key_C);
    QVERIFY(!canvas->isComparingOriginal());

    QLineEdit editor(&window);
    QTest::keyClick(&editor, Qt::Key_C);
    QCOMPARE(editor.text(), QString("c"));
    QVERIFY(!canvas->isComparingOriginal());
    QTest::keyPress(canvas, Qt::Key_C);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&window, &deactivate);
    QVERIFY(!canvas->isComparingOriginal());
    QTest::keyRelease(canvas, Qt::Key_C);

    window.showFilters();
    auto *filters = window.findChild<QVFiltersDialog *>();
    QVERIFY(filters);
    auto *slider = filters->findChild<QSlider *>("brightnessSlider");
    QVERIFY(slider);
    slider->setFocus();
    QTest::keyPress(slider, Qt::Key_C);
    QVERIFY(canvas->isComparingOriginal());
    filters->hide();
    QVERIFY(!canvas->isComparingOriginal());
    QTest::keyRelease(canvas, Qt::Key_C);
    window.activateWindow();
    canvas->setFocus();

    QSettings settings;
    settings.setValue("shortcuts/compareoriginal", QStringList{ "Shift+B" });
    qvApp->getShortcutManager().updateShortcuts();
    struct ResetShortcut {
        ~ResetShortcut() {
            QSettings().remove("shortcuts/compareoriginal");
            qvApp->getShortcutManager().updateShortcuts();
        }
    } resetShortcut;
    QTest::keyPress(canvas, Qt::Key_C);
    QVERIFY(!canvas->isComparingOriginal());
    QTest::keyRelease(canvas, Qt::Key_C);
    QTest::keyPress(canvas, Qt::Key_B, Qt::ShiftModifier);
    QVERIFY(canvas->isComparingOriginal());
    QTest::keyRelease(canvas, Qt::Key_B); // Modifiers may be released first.
    QVERIFY(!canvas->isComparingOriginal());

    // Check pixels numerically without launching or inspecting the app visually.
    QGraphicsScene scene;
    auto *item = scene.addPixmap(QPixmap::fromImage(original));
    auto *effect = new QVFilterEffect;
    item->setGraphicsEffect(effect);
    effect->setLayerStack(stack);
    const auto render = [&]() {
        QImage result(original.size(), QImage::Format_ARGB32);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        scene.render(&painter, QRectF(original.rect()), QRectF(original.rect()));
        return result;
    };
    const auto edited = render();
    QVERIFY(edited.pixelColor(20, 20) != original.pixelColor(20, 20));
    effect->setCompareOriginal(true);
    QCOMPARE(render().pixelColor(20, 20), original.pixelColor(20, 20));
    effect->setCompareOriginal(false);
    QCOMPARE(render(), edited);
}

void ActionManagerTests::testDistortShortcut()
{
    QTemporaryDir directory;
    QImage image(400, 300, QImage::Format_ARGB32);
    image.fill(Qt::gray);
    const QString path = directory.filePath("shortcut.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.openFile(path);
    auto *canvas = window.findChild<QVGraphicsView *>();
    QTRY_VERIFY(canvas->canDistort());
    QVERIFY(!canvas->findChild<QVLayersHud *>());
    QTest::keyPress(canvas, Qt::Key_D);
    QVERIFY(canvas->isDistortActive());
    QKeyEvent repeatRelease(QEvent::KeyRelease, Qt::Key_D, Qt::NoModifier, "d", true);
    QApplication::sendEvent(canvas, &repeatRelease);
    QVERIFY(canvas->isDistortActive());
    QKeyEvent repeatPress(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier, "d", true);
    QApplication::sendEvent(canvas, &repeatPress);
    QVERIFY(canvas->isDistortActive());
    QTest::keyRelease(&window, Qt::Key_D);
    QVERIFY(!canvas->isDistortActive());
    QVERIFY(!canvas->findChild<QVLayersHud *>()); // Holding D never opens the HUD.
    QTest::keyPress(canvas, Qt::Key_D);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&window, &deactivate);
    QVERIFY(!canvas->isDistortActive());
    QApplication::sendEvent(canvas, &repeatPress);
    QVERIFY(!canvas->isDistortActive());
    QTest::keyRelease(canvas, Qt::Key_D);

    window.toggleLayers();
    auto *tool = canvas->findChild<QToolButton *>("layersDistort");
    auto *pan = canvas->findChild<QToolButton *>("layersPan");
    QVERIFY(tool && pan);
    QTest::keyClick(canvas, Qt::Key_D);
    QVERIFY(canvas->isDistortActive());
    QVERIFY(tool->isChecked());
    QTest::keyClick(canvas, Qt::Key_D); // Toggle back to Pan with the HUD visible.
    QVERIFY(!canvas->isDistortActive());
    QVERIFY(pan->isChecked());
    QVERIFY(!tool->isChecked());
    pan->click();
    QVERIFY(!canvas->isDistortActive());
    QLineEdit editor(&window);
    QTest::keyClick(&editor, Qt::Key_D);
    QCOMPARE(editor.text(), QString("d"));
    QVERIFY(!canvas->isDistortActive());
    window.toggleLayers();
    QTest::keyPress(canvas, Qt::Key_D);
    QVERIFY(canvas->isDistortActive());
    QTest::keyRelease(canvas, Qt::Key_D);
    QVERIFY(!canvas->isDistortActive());
    QVERIFY(pan->isChecked());
    window.toggleLayers();
    window.showFullScreen();
    QTest::keyClick(canvas, Qt::Key_Escape);
    QVERIFY(!canvas->findChild<QVLayersHud *>()->isVisible());
    QVERIFY(window.isFullScreen());
    QTest::keyClick(canvas, Qt::Key_Escape);
    QVERIFY(!window.isFullScreen());

    QSettings().setValue("shortcuts/distort", QStringList{"Shift+D"});
    qvApp->getShortcutManager().updateShortcuts();
    struct ResetShortcut {
        ~ResetShortcut() {
            QSettings().remove("shortcuts/distort");
            qvApp->getShortcutManager().updateShortcuts();
        }
    } resetShortcut;
    QTest::keyPress(canvas, Qt::Key_D);
    QVERIFY(!canvas->isDistortActive());
    QTest::keyRelease(canvas, Qt::Key_D);
    QTest::keyPress(canvas, Qt::Key_D, Qt::ShiftModifier);
    QVERIFY(canvas->isDistortActive());
    QTest::keyRelease(canvas, Qt::Key_D);
    QVERIFY(!canvas->isDistortActive());
}

void ActionManagerTests::testCanvasCrop()
{
    QTemporaryDir directory;
    QImage original(80, 60, QImage::Format_ARGB32);
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 80; ++x) original.setPixel(x, y, qRgb(x*3, y*4, 90));
    const QString first = directory.filePath("a.png"), second = directory.filePath("b.png");
    QVERIFY(original.save(first));
    QVERIFY(original.save(second));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.openFile(first);
    auto *canvas = window.findChild<QVGraphicsView *>();
    QTRY_VERIFY(canvas->canDistort());
    window.resize(700, 500);
    QCoreApplication::processEvents();
    window.toggleLayers();
    auto *crop = canvas->findChild<QToolButton *>("layersCrop");
    QVERIFY(crop && crop->isEnabled());
    const auto transform = canvas->transform();
    const auto center = canvas->mapToScene(canvas->viewport()->rect().center());
    crop->click();
    QVERIFY(canvas->isCropActive());
    const QPoint edge = canvas->mapFromScene(QPointF(80, 30));
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, edge);
    QMouseEvent drag(QEvent::MouseMove, QPointF(edge+QPoint(12, 0)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &drag);
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, edge+QPoint(12, 0));
    QVERIFY(canvas->cropDraftRect().width() > 1);
    QVERIFY(canvas->setCropDraft(QRectF(-0.25, 0, 1.25, 1.5)));
    QTest::keyClick(canvas, Qt::Key_Return);
    QVERIFY(!canvas->isCropActive());
    QCOMPARE(canvas->currentMediaSize(), QSize(100, 90));
    QCOMPARE(canvas->transform(), transform);
    QCOMPARE(canvas->mapToScene(canvas->viewport()->rect().center()), center);
    std::unique_ptr<QMimeData> mime(canvas->getMimeData());
    QImage copied = qvariant_cast<QImage>(mime->imageData());
    QCOMPARE(copied.size(), QSize(100, 90));
    for (int y = 0; y < 90; ++y) for (int x = 0; x < 100; ++x)
        QCOMPARE(copied.pixelColor(x, y), x >= 20 && y < 60 ? original.pixelColor(x-20, y) : QColor(Qt::transparent));
    QVERIFY(!mime->hasUrls());
    QVExportDialog exportDialog(canvas->exportSource(), &window);
    QCOMPARE(exportDialog.findChild<QSpinBox *>("exportWidth")->value(), 100);
    QCOMPARE(exportDialog.findChild<QSpinBox *>("exportHeight")->value(), 90);
    exportDialog.findChild<QCheckBox *>("exportApplyFilters")->setChecked(false);
    QCOMPARE(exportDialog.findChild<QSpinBox *>("exportWidth")->value(), 80);
    QCOMPARE(exportDialog.findChild<QSpinBox *>("exportHeight")->value(), 60);
    crop->click();
    QVERIFY(canvas->setCropDraft(QRectF(0, 0, 0.5, 0.5)));
    QTest::keyClick(canvas, Qt::Key_Escape);
    QCOMPARE(canvas->currentMediaSize(), QSize(100, 90));
    crop->click();
    QTest::keyClick(canvas, Qt::Key_D);
    QVERIFY(!canvas->isCropActive());
    QVERIFY(canvas->isDistortActive());
    QVERIFY(!crop->isChecked());
    crop->click();
    QVERIFY(!canvas->isDistortActive());
    canvas->setCropActive(false);
    const auto id = canvas->layerModel()->addDistort();
    auto layer = canvas->layerModel()->stack().layers[0];
    QCOMPARE(layer.id, id);
    QVLayers::DistortStroke stroke;
    stroke.radius = 0.25;
    stroke.points = { QPointF(0.4, 0.5), QPointF(0.55, 0.5) };
    layer.strokes.append(stroke);
    layer.name = "Remember me";
    canvas->layerModel()->update(layer);
    canvas->rotateImage(90);
    window.mirror();
    const auto saved = canvas->layerModel()->stack();
    std::unique_ptr<QMimeData> beforeNavigation(canvas->getMimeData());
    window.openFile(second);
    QTRY_VERIFY(canvas->canDistort());
    QTRY_COMPARE(canvas->getCurrentMedia().fileInfo.absoluteFilePath(), second);
    QVERIFY(canvas->layerModel()->stack().isNeutral());
    window.openFile(first);
    QTRY_VERIFY(canvas->canDistort());
    QTRY_COMPARE(canvas->getCurrentMedia().fileInfo.absoluteFilePath(), first);
    QVERIFY(canvas->layerModel()->stack().samePixels(saved));
    QCOMPARE(canvas->layerModel()->stack().layers[0].name, QString("Remember me"));
    std::unique_ptr<QMimeData> afterNavigation(canvas->getMimeData());
    QCOMPARE(qvariant_cast<QImage>(afterNavigation->imageData()), qvariant_cast<QImage>(beforeNavigation->imageData()));
    canvas->resetCrop();
    QCOMPARE(canvas->currentMediaSize(), original.size().transposed());
}

void ActionManagerTests::testCanvasCopy()
{
    QTemporaryDir directory;
    QImage original(80, 60, QImage::Format_ARGB32);
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 80; ++x) original.setPixel(x, y, qRgba(x*3, y*4, 90, 180));
    const QString path = directory.filePath("copy.png");
    QVERIFY(original.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.openFile(path);
    auto *canvas = window.findChild<QVGraphicsView *>();
    QTRY_VERIFY(canvas->canDistort());
    QScopedPointer<QMimeData> unchanged(canvas->getMimeData());
    QVERIFY(unchanged->hasUrls());
    QCOMPARE(qvariant_cast<QImage>(unchanged->imageData()).size(), original.size());
    auto *model = canvas->layerModel();
    model->addFilter();
    auto filter = model->stack().layers[0];
    filter.filter.brightness = 15;
    model->update(filter);
    model->addDistort();
    auto layer = model->stack().layers[0];
    QVLayers::DistortStroke stroke;
    stroke.radius = 0.25;
    stroke.points = { QPointF(0.4, 0.5), QPointF(0.55, 0.5) };
    layer.strokes = {stroke};
    model->update(layer);
    canvas->rotateImage(90);
    window.mirror();
    canvas->zoom(1.7);
    QVERIFY(canvas->transform().m11() < 0);
    const auto viewTransform = canvas->transform();
    const auto center = canvas->mapToScene(canvas->viewport()->rect().center());
    const auto expected = QVLayers::apply(canvas->getLoadedPixmap().toImage(), QVLayers::rotated(model->stack(), 90))
            .mirrored(true, false);
    canvas->setCompareOriginal(true);
    QScopedPointer<QMimeData> edited(canvas->getMimeData());
    QVERIFY(edited->hasImage());
    QVERIFY(!edited->hasUrls());
    QCOMPARE(qvariant_cast<QImage>(edited->imageData()), expected);
    QCOMPARE(QImage::fromData(edited->data("image/png")), expected);
    const auto pasted = QVClipboard::read(*edited);
    QVERIFY(pasted.urls.isEmpty());
    QCOMPARE(QImage::fromData(pasted.bytes), expected);
    window.copy();
    QCOMPARE(QApplication::clipboard()->image(), expected);
    QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
    QCOMPARE(canvas->transform(), viewTransform);
    QCOMPARE(canvas->mapToScene(canvas->viewport()->rect().center()), center);
    canvas->setCompareOriginal(false);
}

void ActionManagerTests::testSessionComposite()
{
    QTemporaryDir directory;
    QImage image(800, 600, QImage::Format_RGB32);
    image.fill(QColor(60,100,140));
    const auto first = directory.filePath("a.png"), next = directory.filePath("b.png");
    QVERIFY(image.save(first));
    QVERIFY(image.save(next));
    QVGraphicsView canvas;
    canvas.resize(640, 480);
    canvas.show();
    canvas.loadFile(first);
    QTRY_VERIFY(canvas.canDistort());
    canvas.layerModel()->addFilter();
    auto layer = canvas.layerModel()->stack().layers[0];
    layer.filter.brightness = 20;
    canvas.layerModel()->update(layer);
    QImage output(canvas.viewport()->size(), QImage::Format_ARGB32);
    const auto paint = [&] { output.fill(Qt::transparent); canvas.viewport()->render(&output); };
    const auto effect = [&]() -> QVFilterEffect * {
        for (auto *item : canvas.scene()->items())
            if (item->graphicsEffect()) return static_cast<QVFilterEffect *>(item->graphicsEffect());
        return nullptr;
    };
    paint();
    QVERIFY(effect());
    QCOMPARE(effect()->compositionCount(), quint64(1));
    const auto expected = effect()->cachedComposite().toImage();
    canvas.goToFile(QVGraphicsView::GoToFileMode::next);
    QTRY_COMPARE(canvas.getCurrentMedia().fileInfo.absoluteFilePath(), next);
    QTRY_VERIFY(canvas.canDistort());
    paint();
    QElapsedTimer elapsed;
    elapsed.start();
    canvas.goToFile(QVGraphicsView::GoToFileMode::previous);
    QTRY_COMPARE(canvas.getCurrentMedia().fileInfo.absoluteFilePath(), first);
    QTRY_VERIFY(canvas.canDistort());
    paint();
    QVERIFY(effect());
    QCOMPARE(effect()->compositionCount(), quint64(0));
    QCOMPARE(effect()->cachedComposite().toImage(), expected);
    qInfo() << "Returning to edited image:" << elapsed.elapsed() << "ms, zero recomposites";
    // A replacement with identical dimensions must invalidate the saved composite.
    image.fill(Qt::yellow);
    QVERIFY(image.save(first));
    canvas.reloadFile();
    QTRY_VERIFY(canvas.canDistort());
    paint();
    QVERIFY(effect()->compositionCount() > 0);
    QVERIFY(effect()->cachedComposite().toImage() != expected);
}

void ActionManagerTests::testNativeComposite()
{
    QImage source(80, 60, QImage::Format_ARGB32);
    for (int y = 0; y < 60; ++y) for (int x = 0; x < 80; ++x)
        source.setPixel(x, y, qRgba(x*3,y*4,90,255));
    QGraphicsScene scene;
    auto *item = scene.addPixmap(QPixmap::fromImage(source.scaled(40, 30)));
    auto *effect = new QVFilterEffect;
    item->setGraphicsEffect(effect);
    effect->setSourcePixmap(QPixmap::fromImage(source));
    effect->setSmoothScaling(false);
    QVLayers::Stack stack;
    QVLayers::Layer filter;
    filter.kind = QVLayers::Kind::Filter;
    filter.filter.brightness = 12;
    stack.layers.prepend(filter);
    stack.canvas = QRectF(-.25, 0, 1.5, 1.5);
    effect->setLayerStack(stack);
    const auto paint = [&](const QRect &bounds, const QSize &displaySize) {
        QImage result(bounds.size(), QImage::Format_ARGB32);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        const double sx = double(displaySize.width())/source.width();
        const double sy = double(displaySize.height())/source.height();
        scene.render(&painter, QRectF(result.rect()), QRectF(bounds.x()*sx, bounds.y()*sy,
                     bounds.width()*sx, bounds.height()*sy));
        return result;
    };
    const auto bounds = QVLayers::canvasPixels(source.size(), stack.canvas);
    const auto expected = QVLayers::apply(source, stack);
    QCOMPARE(paint(bounds, QSize(40,30)), expected);
    const auto count = effect->compositionCount();
    // Different display pixels and size must not change the native composite.
    item->setPixmap(QPixmap::fromImage(source.scaled(160,120)));
    QCOMPARE(paint(bounds, QSize(160,120)), expected);
    QCOMPARE(effect->compositionCount(), count);
    effect->setCompareOriginal(true);
    QCOMPARE(paint(source.rect(), QSize(160,120)), source);
    effect->setCompareOriginal(false);
    QCOMPARE(paint(bounds, QSize(160,120)), expected);
    QCOMPARE(effect->compositionCount(), count);
    source.fill(Qt::green);
    effect->setSourcePixmap(QPixmap::fromImage(source));
    QCOMPARE(paint(bounds, QSize(160,120)), QVLayers::apply(source, stack));
    QCOMPARE(effect->compositionCount(), count+1);
}

void ActionManagerTests::testEditedCanvasNavigationCost()
{
    QTemporaryDir directory;
    QImage image(1200, 900, QImage::Format_RGB32);
    image.fill(QColor(80,120,160));
    const auto path = directory.filePath("performance.png");
    QVERIFY(image.save(path));
    QVGraphicsView canvas;
    canvas.resize(800, 600);
    canvas.show();
    canvas.loadFile(path);
    QTRY_VERIFY(canvas.canDistort());
    for (int n = 0; n < 3; ++n) {
        const auto id = canvas.layerModel()->addFilter();
        auto layer = canvas.layerModel()->stack().layers[canvas.layerModel()->indexOf(id)];
        layer.filter.brightness = 10;
        layer.filter.contrast = 15;
        layer.filter.saturation = -20;
        canvas.layerModel()->update(layer);
    }
    canvas.layerModel()->addDistort();
    auto layer = canvas.layerModel()->stack().layers[0];
    QVLayers::DistortStroke stroke;
    stroke.radius = .2;
    stroke.points = {QPointF(.4,.5), QPointF(.55,.5)};
    layer.strokes.append(stroke);
    canvas.layerModel()->update(layer);
    QImage output(canvas.viewport()->size(), QImage::Format_ARGB32);
    const auto paint = [&] { output.fill(Qt::transparent); canvas.viewport()->render(&output); };
    paint();
    QVFilterEffect *effect = nullptr;
    for (auto *item : canvas.scene()->items())
        if (item->graphicsEffect()) effect = static_cast<QVFilterEffect *>(item->graphicsEffect());
    QVERIFY(effect);
    const auto before = effect->compositionCount();
    QElapsedTimer elapsed;
    elapsed.start();
    for (int n = 0; n < 16; ++n) {
        canvas.horizontalScrollBar()->setValue(canvas.horizontalScrollBar()->value()+12);
        paint();
        canvas.zoom(n%2 ? 1/1.12 : 1.12);
        canvas.scaleExpensively();
        paint();
    }
    qInfo() << "Edited canvas: 16 pan/zoom steps:" << elapsed.elapsed() << "ms; recomposites:"
            << effect->compositionCount()-before;
    QCOMPARE(effect->compositionCount(), before);
    canvas.setCompareOriginal(true);
    paint();
    canvas.setCompareOriginal(false);
    paint();
    QCOMPARE(effect->compositionCount(), before);
    auto edited = canvas.layerModel()->stack().layers[1];
    edited.filter.brightness += 5;
    canvas.layerModel()->update(edited);
    paint();
    QCOMPARE(effect->compositionCount(), before+1);
}

void ActionManagerTests::testBrushZoom()
{
    QTemporaryDir directory;
    QImage image(200, 160, QImage::Format_RGB32);
    image.fill(Qt::gray);
    const auto path = directory.filePath("brush.png");
    QVERIFY(image.save(path));
    struct Canvas : QVGraphicsView { using QVGraphicsView::drawForeground; } canvas;
    canvas.resize(640, 480);
    canvas.show();
    canvas.loadFile(path);
    QTRY_VERIFY(canvas.canDistort());
    canvas.setDistortActive(true);
    QTest::mouseMove(canvas.viewport(), QPoint(210, 190));
    const auto overlay = [&] {
        QImage result(canvas.viewport()->size(), QImage::Format_ARGB32);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        canvas.drawForeground(&painter, canvas.sceneRect());
        return result;
    };
    const auto before = overlay();
    QVERIFY(before.pixelColor(258, 190).alpha() > 0);
    canvas.zoom(1.5);
    QCOMPARE(overlay(), before);
    canvas.scaleExpensively();
    QCOMPARE(overlay(), before);
    canvas.zoom(0.5);
    QCOMPARE(overlay(), before);
}

void ActionManagerTests::testDistortCache()
{
    QImage source(80, 60, QImage::Format_ARGB32);
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 80; ++x) source.setPixel(x, y, qRgb(x*3, y*4, 80));
    QGraphicsScene scene;
    auto *item = scene.addPixmap(QPixmap::fromImage(source));
    auto *effect = new QVFilterEffect;
    item->setGraphicsEffect(effect);
    QVLayers::Stack stack;
    QVLayers::Layer layer;
    layer.kind = QVLayers::Kind::Distort;
    QVLayers::DistortStroke stroke;
    stroke.radius = 0.2;
    stroke.points = { QPointF(0.35, 0.5), QPointF(0.45, 0.5) };
    layer.strokes = { stroke };
    stack.layers.prepend(layer);
    const auto check = [&]() {
        effect->setLayerStack(stack);
        const QRect bounds = QVLayers::canvasPixels(source.size(), stack.canvas);
        QImage rendered(bounds.size(), QImage::Format_ARGB32);
        rendered.fill(Qt::transparent);
        QPainter painter(&rendered);
        scene.render(&painter, QRectF(rendered.rect()), QRectF(bounds));
        painter.end();
        const auto expected = QVLayers::apply(source, stack);
        QCOMPARE(rendered.size(), expected.size());
        for (int y = 0; y < expected.height(); ++y) for (int x = 0; x < expected.width(); ++x)
            QCOMPARE(rendered.pixelColor(x,y), expected.pixelColor(x,y));
    };
    check();
    stack.layers[0].strokes[0].points.append(QPointF(0.55, 0.5));
    check(); // Extend the in-progress stroke through the incremental cache.
    stack.layers[0].strokes.append(stroke);
    check(); // New stroke.
    stack.layers[0].strokes.removeLast();
    check(); // Undo invalidates the cache.
    stack.layers[0].strength = 50;
    check(); // Partial-strength layers use a complete composite.
    stack.layers[0].strokes[0].points.append(QPointF(0.6, 0.55));
    check();
    stack.canvas = QRectF(-0.25, 0, 1.5, 1.5);
    check();
    stack.canvas = QRectF(0.25, 0.5, 0.5, 0.5);
    check();
    source.fill(Qt::green);
    item->setPixmap(QPixmap::fromImage(source));
    check();
    source = QImage(100, 80, QImage::Format_ARGB32);
    source.fill(Qt::blue);
    item->setPixmap(QPixmap::fromImage(source));
    check();
}

void ActionManagerTests::testDistortTool()
{
    QTemporaryDir directory;
    QImage image(400, 300, QImage::Format_ARGB32);
    image.fill(QColor(80, 120, 160));
    const QString path = directory.filePath("distort.png");
    const QString next = directory.filePath("next.png");
    QVERIFY(image.save(path));
    QVERIFY(image.save(next));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.openFile(path);
    auto *canvas = window.findChild<QVGraphicsView *>();
    QTRY_VERIFY(canvas->canDistort());
    window.toggleLayers();
    auto *tool = canvas->findChild<QToolButton *>("layersDistort");
    auto *pan = canvas->findChild<QToolButton *>("layersPan");
    QVERIFY(tool && pan && tool->isEnabled());
    tool->click();
    QVERIFY(canvas->isDistortActive());
    const auto transform = canvas->transform();
    const auto center = canvas->mapToScene(canvas->viewport()->rect().center());
    const QPoint start = canvas->viewport()->rect().center();
    const QPoint end = start + QPoint(24, 0);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move);
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QVERIFY(canvas->layerModel()->stack().hasDistortion());
    QCOMPARE(canvas->layerModel()->stack().layers[0].strokes.size(), 1);
    // Text editors retain their own undo even with the Distort tool active.
    QLineEdit editor(&window);
    QTest::keyClicks(&editor, "abc");
    QTest::keyClick(&editor, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(editor.text(), QString());
    QCOMPARE(canvas->layerModel()->stack().layers[0].strokes.size(), 1);
    QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(canvas->layerModel()->stack().layers[0].strokes.isEmpty());
    QTest::keyClick(canvas, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(canvas->layerModel()->stack().layers[0].strokes.size(), 1);
    QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(canvas->layerModel()->stack().layers[0].strokes.isEmpty());
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QApplication::sendEvent(canvas->viewport(), &move);
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(canvas->layerModel()->stack().layers[0].strokes.size(), 1);
    QKeyEvent repeatUndo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier, QString(), true);
    QApplication::sendEvent(canvas, &repeatUndo);
    QCOMPARE(canvas->layerModel()->stack().layers[0].strokes.size(), 1);
    QCOMPARE(canvas->transform(), transform);
    QCOMPARE(canvas->mapToScene(canvas->viewport()->rect().center()), center);
    QTest::keyPress(canvas, Qt::Key_C);
    QVERIFY(canvas->isComparingOriginal());
    QTest::keyRelease(canvas, Qt::Key_C);
    QVERIFY(!canvas->isComparingOriginal());
    auto *size = canvas->findChild<QSlider *>("distortBrushSize");
    QVERIFY(size);
    QWheelEvent wheel(QPointF(start), QPointF(canvas->viewport()->mapToGlobal(start)), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas->viewport(), &wheel);
    QCOMPARE(size->value(), 52);
    QCOMPARE(canvas->transform(), transform);
    pan->click();
    QVERIFY(!canvas->isDistortActive());
    tool->click();
    window.toggleLayers();
    QVERIFY(!canvas->isDistortActive());
    window.openFile(next);
    QTRY_COMPARE(canvas->getCurrentMedia().fileInfo.absoluteFilePath(), next);
    QTRY_VERIFY(canvas->canDistort());
    QVERIFY(!canvas->layerModel()->stack().hasDistortion());
    QCOMPARE(canvas->layerModel()->stack().layers.size(), 1);
}

void ActionManagerTests::testLayersHud()
{
    QTemporaryDir directory;
    QImage image(480, 360, QImage::Format_ARGB32);
    image.fill(QColor(50, 100, 150));
    const QString path = directory.filePath("layers.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{ window };
    window.show();
    window.openFile(path);
    auto *canvas = window.findChild<QVGraphicsView *>();
    QVERIFY(canvas);
    QTRY_VERIFY(canvas->isMediaLoaded());
    window.resize(1000, 760);
    QCoreApplication::processEvents();
    canvas->zoom(1.7);
    const QRect viewportGeometry = canvas->viewport()->geometry();
    const QTransform transform = canvas->transform();
    const QPointF center = canvas->mapToScene(canvas->viewport()->rect().center());
    window.activateWindow();
    QCoreApplication::processEvents();
    QTest::keyClick(&window, Qt::Key_H);
    auto *hud = canvas->findChild<QVLayersHud *>();
    QVERIFY(hud && hud->isVisible());
    auto *list = canvas->findChild<QListWidget *>("layersList");
    auto *add = canvas->findChild<QToolButton *>("layersAddFilter");
    QVERIFY(list && add);
    QCOMPARE(list->count(), 1);
    auto *rightPanel = qobject_cast<QVOverlayPanel *>(list->parentWidget());
    QVERIFY(rightPanel);
    QCOMPARE(rightPanel->geometry().bottom(), canvas->viewport()->geometry().bottom() - 12);
    rightPanel->resetPlacement();
    QCOMPARE(rightPanel->geometry().bottom(), canvas->viewport()->geometry().bottom() - 12);
    add->click();
    QCOMPARE(list->count(), 2);
    QVERIFY(!canvas->findChild<QSlider *>("brightnessSlider"));
    window.showFilters();
    auto *dialog = window.findChild<QVFiltersDialog *>();
    QVERIFY(dialog && dialog->isWindow() && dialog->isVisible());
    auto *brightness = dialog->findChild<QSlider *>("brightnessSlider");
    brightness->setValue(20);
    window.showFilters();
    QVERIFY(!dialog->isVisible());
    QVERIFY(hud->isVisible());
    QCOMPARE(canvas->layerModel()->stack().layers[0].filter.brightness, 20);
    auto *strength = canvas->findChild<QSlider *>("layerStrength");
    strength->setValue(50);
    QCOMPARE(canvas->layerModel()->stack().layers[0].strength, 50);
    auto *percent = canvas->findChild<QLabel *>("layerStrengthValue");
    auto *blend = canvas->findChild<QComboBox *>("layerBlend");
    auto *filename = canvas->findChild<QLabel *>("layersSource");
    QVERIFY(percent && blend && filename);
    QVERIFY(!canvas->findChild<QLineEdit *>("layerName"));
    QVERIFY(!canvas->findChild<QSpinBox *>("layerStrengthValue"));
    QCOMPARE(percent->text(), QString("50%"));
    QCoreApplication::processEvents();
    QVERIFY(strength->mapTo(rightPanel, QPoint()).y() < list->y());
    QCOMPARE(percent->mapTo(rightPanel, percent->rect().center()).y(),
             blend->mapTo(rightPanel, blend->rect().center()).y());
    QCOMPARE(filename->mapTo(rightPanel, filename->rect().center()).y(),
             rightPanel->findChild<QToolButton *>("layersClose")->geometry().center().y());
    QVERIFY(list->height() > rightPanel->height() * 0.65);
    auto wheel = [](QWidget *widget, int delta) {
        QWheelEvent event(QPointF(5, 5), QPointF(widget->mapToGlobal(QPoint(5, 5))),
                          QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(widget, &event);
        QVERIFY(event.isAccepted());
    };
    wheel(percent, 60);
    QCOMPARE(strength->value(), 50);
    wheel(percent, 60);
    QCOMPARE(strength->value(), 51);
    wheel(strength, -120);
    QCOMPARE(strength->value(), 50);
    QCOMPARE(canvas->layerModel()->stack().layers[0].strength, 50);
    const quint64 filterId = hud->selectedLayerId();
    struct Inspector : QStyledItemDelegate { using QStyledItemDelegate::initStyleOption; } inspector;
    QStyleOptionViewItem option;
    option.initFrom(list);
    option.widget = list;
    option.decorationSize = list->iconSize();
    inspector.initStyleOption(&option, list->currentIndex());
    option.rect = list->visualItemRect(list->currentItem());
    const QRect iconRect = list->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, list);
    QSignalSpy filterRequests(hud, &QVLayersHud::filtersRequested);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, iconRect.center());
    QCOMPARE(filterRequests.count(), 1);
    QCOMPARE(filterRequests.first().first().toULongLong(), filterId);
    QVERIFY(dialog->isVisible());
    window.showFilters();
    const auto distortId = canvas->layerModel()->addDistort();
    list->setCurrentRow(0);
    inspector.initStyleOption(&option, list->currentIndex());
    option.rect = list->visualItemRect(list->currentItem());
    const auto distortIcon = list->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, list);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, distortIcon.center());
    QVERIFY(canvas->isDistortActive());
    QCOMPARE(hud->selectedLayerId(), distortId);
    QCOMPARE(filterRequests.count(), 1);
    canvas->findChild<QToolButton *>("layersPan")->click();
    canvas->layerModel()->remove(distortId);
    auto contextMenu = [list, rightPanel](int row) {
        const QPoint position = list->visualItemRect(list->item(row)).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, position, list->viewport()->mapToGlobal(position));
        QApplication::sendEvent(list->viewport(), &event);
        return rightPanel->findChild<QMenu *>("layerContextMenu");
    };
    auto *sourceMenu = contextMenu(1);
    QVERIFY(sourceMenu);
    QVERIFY(!sourceMenu->findChild<QAction *>("layerRemove")->isEnabled());
    QVERIFY(!sourceMenu->findChild<QAction *>("layerEditFilter"));
    sourceMenu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto *filterMenu = contextMenu(0);
    QVERIFY(filterMenu);
    QVERIFY(filterMenu->findChild<QAction *>("layerEditFilter"));
    QVERIFY(filterMenu->findChild<QAction *>("layerRename"));
    filterMenu->findChild<QAction *>("layerDuplicate")->trigger();
    QCOMPARE(list->count(), 3);
    const quint64 duplicateId = hud->selectedLayerId();
    QVERIFY(duplicateId != filterId);
    filterMenu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    canvas->layerModel()->remove(duplicateId);
    list->setCurrentRow(0);
    list->currentItem()->setText("Warm light");
    QCOMPARE(canvas->layerModel()->stack().layers[0].name, QString("Warm light"));
    list->currentItem()->setCheckState(Qt::Unchecked);
    QVERIFY(!canvas->layerModel()->stack().layers[0].visible);
    const auto sourceId = canvas->layerModel()->stack().layers[1].id;
    canvas->layerModel()->move(sourceId, 0);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toULongLong(), sourceId);
    QVERIFY(canvas->exportSource().layers.samePixels(canvas->layerModel()->stack()));
    for (auto *panel : canvas->findChildren<QVOverlayPanel *>()) {
        QVERIFY(!panel->isWindow());
        QTest::mousePress(panel, Qt::LeftButton, Qt::NoModifier, QPoint(panel->width()-2, panel->height()-2));
        QMouseEvent move(QEvent::MouseMove, QPointF(panel->width()+25, panel->height()+25),
                         QPointF(panel->mapToGlobal(QPoint(panel->width()+25, panel->height()+25))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(panel, &move);
        QTest::mouseRelease(panel, Qt::LeftButton);
        QVERIFY(canvas->viewport()->geometry().contains(panel->geometry()));
    }
    const auto panels = canvas->findChildren<QVOverlayPanel *>();
    QVector<QRect> positions;
    for (auto *panel : panels) positions.append(panel->geometry());
    const int scrollPosition = canvas->horizontalScrollBar()->value();
    canvas->horizontalScrollBar()->setValue(scrollPosition + 50);
    for (int i = 0; i < panels.size(); ++i) QCOMPARE(panels[i]->geometry(), positions[i]);
    canvas->horizontalScrollBar()->setValue(scrollPosition);
    hud->toggle();
    QVERIFY(!hud->isVisible());
    QCOMPARE(canvas->viewport()->geometry(), viewportGeometry);
    QCOMPARE(canvas->transform(), transform);
    QCOMPARE(canvas->mapToScene(canvas->viewport()->rect().center()), center);
    window.showFilters();
    QVERIFY(!hud->isVisible());
    QVERIFY(dialog->isVisible());
    QCOMPARE(list->count(), 2); // U reuses the existing filter independently of H.
    window.showFilters();
    hud->setVisible(true);
    window.resize(220, 180);
    QCoreApplication::processEvents();
    for (auto *panel : canvas->findChildren<QVOverlayPanel *>()) {
        QVERIFY2(canvas->viewport()->geometry().contains(panel->geometry()),
                 qPrintable(QString("Viewport %1x%2, panel %3,%4 %5x%6, minimum %7x%8")
                            .arg(canvas->viewport()->width()).arg(canvas->viewport()->height())
                            .arg(panel->x()).arg(panel->y()).arg(panel->width()).arg(panel->height())
                            .arg(panel->minimumWidth()).arg(panel->minimumHeight())));
    }
    hud->setVisible(false);
}

void ActionManagerTests::testLayersHudCursor()
{
    QWidget host;
    host.resize(800, 600);
    QWidget viewport(&host);
    viewport.setGeometry(host.rect());
    QVLayerModel model;
    QVLayersHud hud(&model, &viewport);
    host.show();
    hud.setVisible(true);
    QCoreApplication::processEvents();
    auto *list = host.findChild<QListWidget *>("layersList");
    auto *panel = qobject_cast<QVOverlayPanel *>(list->parentWidget());
    auto *slider = host.findChild<QSlider *>("layerStrength");
    auto *percent = host.findChild<QLabel *>("layerStrengthValue");
    auto *header = host.findChild<QLabel *>("layersSource");
    QVERIFY(panel && slider && percent && header);
    auto move = [panel](const QPoint &position) {
        QMouseEvent event(QEvent::MouseMove, QPointF(position),
                          QPointF(panel->mapToGlobal(position)), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(panel, &event);
    };
    // No intermediate frame move is sent when entering a child from an edge.
    move(QPoint(2, panel->height() / 2));
    QCOMPARE(panel->cursor().shape(), Qt::SizeHorCursor);
    QCOMPARE(list->viewport()->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(slider->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(percent->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(host.findChild<QToolButton *>("layersAddFilter")->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(header->cursor().shape(), Qt::SizeAllCursor);
    list->editItem(list->item(0));
    QCoreApplication::processEvents();
    auto *editor = list->findChild<QLineEdit *>();
    QVERIFY(editor);
    QCOMPARE(editor->cursor().shape(), Qt::IBeamCursor);
    move(QPoint(2, 2));
    QCOMPARE(panel->cursor().shape(), Qt::SizeFDiagCursor);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(panel, &leave);
    QCOMPARE(panel->cursor().shape(), Qt::ArrowCursor);
    move(QPoint(2, panel->height() / 2));
    QTest::mousePress(panel, Qt::LeftButton, Qt::NoModifier, QPoint(2, panel->height() / 2));
    QTest::mouseRelease(panel, Qt::LeftButton, Qt::NoModifier, QPoint(2, panel->height() / 2));
    QCOMPARE(panel->cursor().shape(), Qt::ArrowCursor);
    move(QPoint(2, 2));
    hud.setVisible(false);
    hud.setVisible(true);
    QCOMPARE(panel->cursor().shape(), Qt::ArrowCursor);
}

void ActionManagerTests::testDialogToggleShortcuts()
{
    QVInfoDialog details;
    details.show();
    auto *detailsToggle = details.findChild<QShortcut *>("detailsToggleShortcut");
    QVERIFY(detailsToggle);
    QCOMPARE(detailsToggle->key(), QKeySequence(Qt::Key_I));
    QVERIFY(QMetaObject::invokeMethod(detailsToggle, "activated"));
    QVERIFY(!details.isVisible());

    QVOptionsDialog settings;
    settings.setAttribute(Qt::WA_DeleteOnClose, false);
    settings.show();
    auto *settingsToggle = settings.findChild<QShortcut *>("settingsToggleShortcut");
    QVERIFY(settingsToggle);
    QCOMPARE(settingsToggle->key(), QKeySequence(Qt::Key_S));
    QVERIFY(QMetaObject::invokeMethod(settingsToggle, "activated"));
    QVERIFY(!settings.isVisible());
}

void ActionManagerTests::testExportDialog()
{
    QVExport::Source source;
    source.size = QSize(320, 240);
    source.path = "example.gif";
    source.animated = true;
    QVExportDialog dialog(source);
    auto *scope = dialog.findChild<QComboBox *>("exportScope");
    auto *format = dialog.findChild<QComboBox *>("exportFormat");
    auto *width = dialog.findChild<QSpinBox *>("exportWidth");
    auto *height = dialog.findChild<QSpinBox *>("exportHeight");
    QVERIFY(scope && format && width && height);
    QCOMPARE(scope->count(), 2);
    QCOMPARE(scope->currentIndex(), 0);
    QCOMPARE(format->currentData().toString(), QString("png"));
    auto *name = dialog.findChild<QLineEdit *>("exportFileName");
    QCOMPARE(name->text(), QString("example-frame"));
    dialog.show();
    QVERIFY(!name->hasFocus());
    QVERIFY(!name->hasSelectedText());
    name->setFocus();
    name->selectAll();
    QVERIFY(name->hasSelectedText());
    QTest::mouseClick(dialog.findChild<QLabel *>("exportPreview"), Qt::LeftButton);
    QVERIFY(!name->hasSelectedText());
    QVERIFY(!name->hasFocus());
    name->setText("My edited name.v2");
    name->setModified(true);
    auto *mode = dialog.findChild<QComboBox *>("exportSizeMode");
    auto *percent = dialog.findChild<QDoubleSpinBox *>("exportPercentage");
    mode->setCurrentIndex(1);
    percent->setValue(60);
    QCOMPARE(width->value(), 192);
    QCOMPARE(height->value(), 144);
    QVERIFY(!width->isEnabled());
    dialog.findChild<QPushButton *>("exportOriginalSize")->click();
    QCOMPARE(percent->value(), 100.0);
    QCOMPARE(width->value(), 320);
    mode->setCurrentIndex(0);
    width->setValue(160);
    QCOMPARE(height->value(), 120);
    height->setValue(60);
    QCOMPARE(width->value(), 80);
    scope->setCurrentIndex(1);
    QCOMPARE(format->currentData().toString(), QString("gif"));
    QCOMPARE(name->text(), QString("My edited name.v2"));
    QVERIFY(format->findData("mp4") >= 0);
    scope->setCurrentIndex(0);
    QVERIFY(format->findData("mp4") < 0);
    source.animated = false;
    QVExportDialog still(source);
    QCOMPARE(still.findChild<QComboBox *>("exportScope")->count(), 1);
    source.video = true;
    source.speed = 1.5;
    source.durationMs = 6000;
    QVExportDialog video(source);
    auto *speed = video.findChild<QDoubleSpinBox *>("exportSpeed");
    QCOMPARE(speed->value(), 1.5);
    QVERIFY(!speed->isEnabled());
    video.findChild<QComboBox *>("exportScope")->setCurrentIndex(1);
    QVERIFY(speed->isEnabled());
    speed->setValue(2);
    QVERIFY(video.findChild<QLabel *>("exportDetails")->text().contains("3.00"));
}

void ActionManagerTests::testExportCanvasState()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("orientation.png");
    QImage image(32, 24, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    MainWindow window;
    window.show();
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    auto &canvas = *view;
    canvas.loadFile(path);
    QTRY_VERIFY(canvas.getImageDetails().isPixmapLoaded);
    canvas.rotateImage(90);
    canvas.scale(-1, -1);
    canvas.togglePlaybackLoopMode();
    QVFilters::Settings filters;
    filters.layers[0].brightness = 15;
    filters.layers[0].contrast = -20;
    filters.layers[0].saturation = 30;
    canvas.layerModel()->setStack(QVLayers::fromFilters(filters));
    auto source = canvas.exportSource();
    QCOMPARE(source.rotation, 90);
    QVERIFY(source.mirrored);
    QVERIFY(source.flipped);
    QVERIFY(source.loop);
    QVERIFY(source.layers.samePixels(QVLayers::fromFilters(filters)));
    QVExportDialog dialog(source);
    auto *rotation = dialog.findChild<QCheckBox *>("exportRotate");
    QVERIFY(rotation->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>("exportMirror")->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>("exportFlip")->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>("exportLoop")->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>("exportApplyFilters")->isChecked());
    auto *width = dialog.findChild<QSpinBox *>("exportWidth");
    auto *height = dialog.findChild<QSpinBox *>("exportHeight");
    QCOMPARE(width->value(), 24);
    QCOMPARE(height->value(), 32);
    rotation->setChecked(false);
    QCOMPARE(width->value(), 32);
    QCOMPARE(height->value(), 24);
    // Rapid updates must finish with the newest preview, rather than an older job.
    width->setValue(16);
    width->setValue(8);
    auto *preview = dialog.findChild<QLabel *>("exportPreview");
    QTRY_VERIFY_WITH_TIMEOUT(preview->text().isEmpty(), 5000);
    const QString details = dialog.findChild<QLabel *>("exportDetails")->text();
    QVERIFY(details.contains("8 x 6 px"));
    QVERIFY(details.contains("4:3"));
    QVERIFY(details.contains("KiB"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QVERIFY(!preview->pixmap().isNull());
#else
    QVERIFY(preview->pixmap() && !preview->pixmap()->isNull());
#endif
    QThreadPool::globalInstance()->waitForDone();
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.close();
}

void ActionManagerTests::testClonedActionsUntracked()
{
    // Get initial counts of certain actions
    int fullscreenCount = qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length();
    int openCount = qvApp->getActionManager().getAllInstancesOfAction("open").length();
    qDebug() << fullscreenCount;

    // Have window clone actions
    MainWindow window;
    window.show();
    // Make sure they were cloned
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length()
            != fullscreenCount);
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("open").length() != openCount);
    // Untrack them
    window.close();

    // Make sure the count has not changed from the initial
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length(),
             fullscreenCount);
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("open").length(), openCount);
}

void ActionManagerTests::testCanvasActionsSupportAllVisualMedia()
{
    const QStringList canvasActions = { "zoomin",      "zoomout",    "resetzoom",
                                        "originalsize", "rotateright", "rotateleft",
                                        "mirror",      "flip", "saveframeas", "filters" };
    const auto &actionLibrary = qvApp->getActionManager().getActionLibrary();
    for (const QString &key : canvasActions) {
        QVERIFY2(actionLibrary.contains(key), qPrintable(key));
        QCOMPARE(actionLibrary.value(key)->data().toStringList().constLast(),
                 QString("mediadisable"));
    }
}

void ActionManagerTests::testSpeedControlsAndIndicator()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    const QString path = directory.filePath("photo.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct CloseWindow { MainWindow &window; ~CloseWindow() { window.close(); } } cleanup{window};
    window.show();
    window.openFile(path);
    QTRY_VERIFY(window.getIsPixmapLoaded());
    auto *canvas = window.findChild<QVGraphicsView *>();
    auto *timer = window.findChild<QTimer *>("slideshowTimer");
    QVERIFY(canvas && timer);
    const auto action = [&](const QString &key) {
        return qvApp->getActionManager().getAllClonesOfAction(key, &window).first();
    };
    QVERIFY(!action("increasespeed")->isEnabled());
    const int baseInterval = timer->interval();
    window.toggleSlideshow();
    QVERIFY(action("increasespeed")->isEnabled());
    QTest::qWait(150);
    action("increasespeed")->trigger();
    QVERIFY(timer->remainingTime() <= qRound((baseInterval - 100) / 1.25));
    QVERIFY(timer->isActive());
    auto *indicator = window.findChild<QLabel *>("speedIndicator");
    QVERIFY(indicator && indicator->isVisible());
    QVERIFY(indicator->text().contains(QString::number(baseInterval / 1250.0, 'f', 2)));
    action("decreasespeed")->trigger();
    QVERIFY(timer->remainingTime() <= baseInterval - 100);
    action("resetspeed")->trigger();
    QVERIFY(timer->remainingTime() <= baseInterval - 100);
    for (int i = 0; i < 50; ++i) action("decreasespeed")->trigger();
    QVERIFY(indicator->text().contains("100.00"));
    window.cancelSlideshow();
    window.toggleSlideshow();
    QCOMPARE(timer->interval(), 100000);
    for (int i = 0; i < 50; ++i) action("increasespeed")->trigger();
    QVERIFY(indicator->text().contains("0.10"));
    QSignalSpy advances(timer, &QTimer::timeout);
    QTRY_VERIFY_WITH_TIMEOUT(advances.count() >= 2, 1500);
    QCOMPARE(timer->interval(), 100);
    action("resetspeed")->trigger();
    window.cancelSlideshow();
    window.toggleSlideshow();
    QCOMPARE(timer->interval(), baseInterval);
    window.resize(700, 500);
    QCoreApplication::processEvents();
    QVERIFY2(qAbs(indicator->geometry().center().x() - canvas->viewport()->geometry().center().x()) <= 1,
             qPrintable(QString("Indicator %1,%2 %3x%4; viewport %5x%6")
                     .arg(indicator->x()).arg(indicator->y()).arg(indicator->width())
                     .arg(indicator->height()).arg(canvas->viewport()->width())
                     .arg(canvas->viewport()->height())));
    QCOMPARE(indicator->geometry().bottom(), canvas->viewport()->geometry().bottom() - 24);
    QTRY_VERIFY_WITH_TIMEOUT(!indicator->isVisible(), 2500);
    window.cancelSlideshow();
    QVERIFY(!action("increasespeed")->isEnabled());

    // Two-frame GIF, avoiding a hardware-dependent video decoder in this integration test.
    QFile gif(directory.filePath("animation.gif"));
    QVERIFY(gif.open(QIODevice::WriteOnly));
    gif.write(QByteArray::fromHex(
            "47494638396101000100800000000000ffffff"
            "21ff0b4e45545343415045322e300301000000"
            "21f904000a0000002c0000000001000100000202440100"
            "21f904000a0000002c00000000010001000002024c01003b"));
    gif.close();
    window.openFile(gif.fileName());
    QTRY_VERIFY(window.getImageDetails().isMovieLoaded);
    action("increasespeed")->trigger();
    QCOMPARE(canvas->getLoadedMovie().speed(), 125);
    QVERIFY(indicator->isVisible());
    QVERIFY(indicator->text().contains("125%"));
    window.toggleSlideshow();
    action("increasespeed")->trigger();
    QCOMPARE(canvas->getLoadedMovie().speed(), 125);
    QVERIFY(timer->interval() <= qRound(baseInterval / 1.25));
    QVERIFY(indicator->text().contains(QString::number(baseInterval / 1250.0, 'f', 2)));
    window.cancelSlideshow();
    action("resetspeed")->trigger();
    QCOMPARE(canvas->getLoadedMovie().speed(), 100);
    QVERIFY(indicator->text().contains("100%"));
}

void ActionManagerTests::testPlaybackActionsAndDefaultShortcuts()
{
    const auto &actionLibrary = qvApp->getActionManager().getActionLibrary();
    const QStringList sharedPlaybackActions = { "pause",         "loop",
                                                "previousframe",
                                                "nextframe" };
    for (const QString &key : sharedPlaybackActions) {
        QVERIFY2(actionLibrary.contains(key), qPrintable(key));
        QCOMPARE(actionLibrary.value(key)->data().toStringList().constLast(),
                 QString("playbackdisable"));
    }

    QVERIFY(actionLibrary.contains("mute"));
    QCOMPARE(actionLibrary.value("mute")->data().toStringList().constLast(),
             QString("videodisable"));

    for (int position = 0; position <= 9; ++position) {
        const QString key = "seekposition" + QString::number(position);
        QVERIFY2(actionLibrary.contains(key), qPrintable(key));
        QCOMPARE(actionLibrary.value(key)->data().toStringList().constLast(),
                 QString("playbackdisable"));
    }

    QHash<QString, QStringList> defaultShortcuts;
    for (const auto &shortcut : qvApp->getShortcutManager().getShortcutsList())
        defaultShortcuts.insert(shortcut.name, shortcut.defaultShortcuts);
    QVERIFY(defaultShortcuts.value("newwindow").contains(QKeySequence(Qt::CTRL | Qt::Key_N).toString()));

    QVERIFY(defaultShortcuts.value("pause").contains(QKeySequence(Qt::Key_Space).toString()));
    QVERIFY(!defaultShortcuts.value("pause").contains(QKeySequence(Qt::Key_P).toString()));
    QCOMPARE(defaultShortcuts.value("filters"),
             QStringList(QKeySequence(Qt::Key_U).toString()));
    QVERIFY(defaultShortcuts.value("mute").contains(QKeySequence(Qt::Key_M).toString()));
    QCOMPARE(defaultShortcuts.value("layers"), QStringList(QKeySequence(Qt::Key_H).toString()));
    QVERIFY(defaultShortcuts.value("loop").contains(QKeySequence(Qt::Key_L).toString()));
    QCOMPARE(defaultShortcuts.value("opencontainingfolder"),
             QStringList(QKeySequence(Qt::Key_E).toString()));
    QCOMPARE(defaultShortcuts.value("options"),
             QStringList(QKeySequence(Qt::Key_S).toString()));
    QVERIFY(actionLibrary.value("loop")->isCheckable());
    QMenu cloneParent;
    const auto loopClone = qvApp->getActionManager().addCloneOfAction(&cloneParent, "loop");
    QVERIFY(loopClone);
    QVERIFY(loopClone->isCheckable());
    qvApp->getActionManager().untrackClonedActions(cloneParent.actions());
    QCOMPARE(nextManualLoopMode(QVPlaybackLoopMode::Default, false),
             QVPlaybackLoopMode::ForceLoop);
    QCOMPARE(nextManualLoopMode(QVPlaybackLoopMode::Default, true),
             QVPlaybackLoopMode::ForceStop);
    QCOMPARE(nextManualLoopMode(QVPlaybackLoopMode::ForceLoop, false),
             QVPlaybackLoopMode::ForceStop);
    QCOMPARE(nextManualLoopMode(QVPlaybackLoopMode::ForceStop, true),
             QVPlaybackLoopMode::ForceLoop);
    QVERIFY(defaultShortcuts.value("previousframe")
                    .contains(QKeySequence(Qt::Key_Comma).toString()));
    QCOMPARE(ShortcutManager::stringListToKeySequenceList(
                     defaultShortcuts.value("previousframe"))
                     .constFirst(),
             QKeySequence(Qt::Key_Comma));
    QVERIFY(defaultShortcuts.value("nextframe")
                    .contains(QKeySequence(Qt::Key_Period).toString()));
    for (int position = 0; position <= 9; ++position) {
        QVERIFY(defaultShortcuts.value("seekposition" + QString::number(position))
                        .contains(QKeySequence(Qt::Key_0 + position).toString()));
    }
}

void ActionManagerTests::testMediaBackendSetting()
{
    QCOMPARE(qvApp->getSettingsManager().getString(SettingsManager::Setting::MediaBackend, true),
             QString("ffmpeg"));

    QVOptionsDialog dialog;
    auto *backendComboBox = dialog.findChild<QComboBox *>("mediaBackendComboBox");
    QVERIFY(backendComboBox);
    QCOMPARE(backendComboBox->count(), 2);
    QCOMPARE(backendComboBox->itemData(0).toString(), QString("ffmpeg"));
    QCOMPARE(backendComboBox->itemData(1).toString(), QString("windows"));
}

void ActionManagerTests::testImageRequestAfterVideoIsNotDiscarded()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString videoPath = directory.filePath("video.mp4");
    QFile videoFile(videoPath);
    QVERIFY(videoFile.open(QIODevice::WriteOnly));
    videoFile.close();

    const QString imagePath = directory.filePath("image.png");
    QImage image(8, 6, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));

    MainWindow window;
    window.show();
    window.openFile(videoPath);
    QCOMPARE(window.getCurrentMedia().mediaType, QVMediaCatalog::MediaType::Video);

    window.openFile(imagePath);
    QCOMPARE(window.getCurrentMedia().mediaType, QVMediaCatalog::MediaType::Image);
    QTRY_VERIFY_WITH_TIMEOUT(window.getImageDetails().isPixmapLoaded, 2000);
    QThreadPool::globalInstance()->waitForDone();
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.close();
}

void ActionManagerTests::testSvgViewport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage embedded(100, 150, QImage::Format_ARGB32);
    embedded.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(embedded.save(&buffer, "PNG"));
    const QByteArray svg =
            "<svg xmlns='http://www.w3.org/2000/svg' "
            "xmlns:xlink='http://www.w3.org/1999/xlink' width='20px' height='30px'>"
            "<g transform='scale(0.2)'><use xlink:href='#image' width='100' height='150'/></g>"
            "<defs><image id='image' width='100' height='150' xlink:href='data:image/png;base64,"
            + png.toBase64() + "'/></defs></svg>";
    QFile file(directory.filePath("viewport.svg"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(svg), qint64(svg.size()));
    file.close();
    QVImageCore core;
    const auto decoded = core.readFile(file.fileName(), QColorSpace());
    QVERIFY(!decoded.errorData.hasError);
    QCOMPARE(decoded.imageSize, QSize(20, 30));
    const QImage image = decoded.image;
    QVERIFY(!image.isNull());
    QCOMPARE(image.devicePixelRatio(), qreal(1));
    QCOMPARE(image.pixelColor(image.width() * 9 / 10, image.height() * 9 / 10), QColor(Qt::red));
}

int main(int argc, char *argv[])
{
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QCoreApplication::setOrganizationName("qMedia-tests");
    QCoreApplication::setApplicationName("actionmanagertests");
    QSettings().setValue("firstlaunch", true);
    QSettings().setValue("updatenotifications", false);
    QVApplication app(argc, argv);
    ActionManagerTests actionManagerTests;
    return QTest::qExec(&actionManagerTests, argc, argv);
}

#include "tst_actionmanagertests.moc"
