#include <QtTest>

#include "qvapplication.h"
#include "qvoptionsdialog.h"
#include "qvplaybackloopmode.h"
#include "qvexportdialog.h"
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
    void testImageRequestAfterVideoIsNotDiscarded();
    void testSvgViewport();
    void testExportDialog();
    void testExportCanvasState();
};

ActionManagerTests::ActionManagerTests() { }

ActionManagerTests::~ActionManagerTests() { }

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
    QCOMPARE(name->text(), QString("example-frame.png"));
    name->setText("My edited name.png");
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
    QCOMPARE(name->text(), QString("My edited name.gif"));
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
    auto source = canvas.exportSource();
    QCOMPARE(source.rotation, 90);
    QVERIFY(source.mirrored);
    QVERIFY(source.flipped);
    QVERIFY(source.loop);
    QVExportDialog dialog(source);
    auto *rotation = dialog.findChild<QCheckBox *>("exportRotate");
    QVERIFY(rotation->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>("exportMirror")->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>("exportFlip")->isChecked());
    QVERIFY(dialog.findChild<QCheckBox *>("exportLoop")->isChecked());
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
                                        "mirror",      "flip", "saveframeas" };
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
    QVERIFY(defaultShortcuts.value("mute").contains(QKeySequence(Qt::Key_M).toString()));
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
    QVApplication app(argc, argv);
    ActionManagerTests actionManagerTests;
    return QTest::qExec(&actionManagerTests, argc, argv);
}

#include "tst_actionmanagertests.moc"
