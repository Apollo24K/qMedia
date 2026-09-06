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

class ActionManagerTests : public QObject
{
    Q_OBJECT

public:
    ActionManagerTests();
    ~ActionManagerTests();

private slots:
    void testClonedActionsUntracked();
    void testCanvasActionsSupportAllVisualMedia();
    void testPlaybackActionsAndDefaultShortcuts();
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
    void testDistortCache();
    void testLayersHud();
    void testLayersHudCursor();
    void testDialogToggleShortcuts();
};

ActionManagerTests::ActionManagerTests() { }

ActionManagerTests::~ActionManagerTests() { }

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
    QTest::keyClick(canvas, Qt::Key_D); // Select, not toggle.
    QVERIFY(canvas->isDistortActive());
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
        QImage rendered(source.size(), QImage::Format_ARGB32);
        rendered.fill(Qt::transparent);
        QPainter painter(&rendered);
        scene.render(&painter, QRectF(source.rect()), QRectF(source.rect()));
        painter.end();
        QCOMPARE(rendered, QVLayers::apply(source, stack));
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

void ActionManagerTests::testPlaybackActionsAndDefaultShortcuts()
{
    const auto &actionLibrary = qvApp->getActionManager().getActionLibrary();
    const QStringList sharedPlaybackActions = { "pause",         "loop",
                                                "previousframe",
                                                "nextframe",     "decreasespeed",
                                                "resetspeed",    "increasespeed" };
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
