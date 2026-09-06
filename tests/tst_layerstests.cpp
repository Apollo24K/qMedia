#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "qvlayers.h"

class LayersTests : public QObject
{
    Q_OBJECT
private slots:
    void sourceAndAdjustment();
    void blendModes();
    void modelEdits();
    void ffmpegMatchesComposite();
};

void LayersTests::sourceAndAdjustment()
{
    QImage source(1, 1, QImage::Format_ARGB32);
    source.fill(QColor(100, 120, 140, 128));
    QVLayers::Stack stack;
    QVERIFY(stack.isNeutral());
    QCOMPARE(QVLayers::apply(source, stack), source);
    stack.layers[0].visible = false;
    QVERIFY(!stack.isNeutral());
    QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0).alpha(), 0);
    stack.layers[0].visible = true;
    stack.layers[0].strength = 50;
    QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0), QColor(100, 120, 140, 64));
    stack.layers[0].strength = 100;
    stack.layers.prepend(stack.layers[0]);
    QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0), QColor(100, 120, 140, 192));
    stack.layers.removeFirst();
    QVLayers::Layer filter;
    filter.kind = QVLayers::Kind::Filter;
    filter.filter.brightness = 20;
    filter.strength = 50;
    stack.layers.prepend(filter);
    QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0), QColor(126, 146, 166, 128));
    stack.layers[0].strength = 0;
    QVERIFY(stack.isNeutral());
    QCOMPARE(QVLayers::apply(source, stack), source);
    stack.layers[0].strength = 100;
    // Adjustment below the source has no visible effect. Reordering is real.
    stack.layers.move(0, 1);
    QCOMPARE(QVLayers::apply(source, stack), source);
    stack.layers.move(1, 0);
    QVERIFY(QVLayers::apply(source, stack) != source);
    stack.layers[0].filter.transparency = 100;
    QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0).alpha(), 0);
}

void LayersTests::blendModes()
{
    QImage source(1, 1, QImage::Format_ARGB32);
    source.fill(QColor(100, 100, 100));
    QVLayers::Stack stack;
    stack.layers.prepend(stack.layers[0]);
    const int expected[] = { 100, 39, 161, 78, 100, 100 };
    for (int mode = 0; mode < 6; ++mode) {
        stack.layers[0].blend = QVLayers::Blend(mode);
        QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0).red(), expected[mode]);
    }
    stack.layers[0].kind = QVLayers::Kind::Filter;
    stack.layers[0].filter.brightness = 20;
    stack.layers[0].blend = QVLayers::Blend::Multiply;
    QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0).red(), 59);
    stack.layers[0].strength = 50;
    QCOMPARE(QVLayers::apply(source, stack).pixelColor(0, 0).red(), 80);
}

void LayersTests::modelEdits()
{
    QVLayerModel model;
    QSignalSpy pixels(&model, &QVLayerModel::pixelsChanged);
    const quint64 source = model.stack().layers[0].id;
    QVERIFY(!model.remove(source));
    const quint64 adjustment = model.addFilter();
    QVERIFY(adjustment != source);
    auto layer = model.stack().layers[0];
    layer.name = "  Evening  ";
    model.update(layer);
    QCOMPARE(model.stack().layers[0].name, QString("Evening"));
    QCOMPARE(pixels.count(), 0); // Renaming does not rerender the image.
    layer.filter.saturation = -50;
    layer.strength = 150;
    model.update(layer);
    QCOMPARE(model.stack().layers[0].strength, 100);
    QCOMPARE(pixels.count(), 1);
    const quint64 copy = model.duplicate(adjustment);
    QVERIFY(copy && copy != adjustment);
    layer = model.stack().layers[0];
    layer.filter.saturation = 30;
    model.update(layer);
    QCOMPARE(model.stack().layers[model.indexOf(adjustment)].filter.saturation, -50);
    QVERIFY(model.move(copy, 2));
    QCOMPARE(model.indexOf(copy), 2);
    QVERIFY(!model.move(copy, -1));
    QVERIFY(!model.move(123456789, 1));
    const quint64 sourceCopy = model.duplicate(source);
    QVERIFY(model.remove(source));
    QVERIFY(!model.remove(sourceCopy));
    QVERIFY(model.remove(copy));
    QVERIFY(model.remove(adjustment));
    QCOMPARE(model.stack().layers.size(), 1);
}

void LayersTests::ffmpegMatchesComposite()
{
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) QSKIP("Optional external export encoder is not installed");
    QTemporaryDir directory;
    QImage source(3, 2, QImage::Format_ARGB32);
    const QColor colors[] = { QColor(100, 150, 200, 128), QColor(250, 10, 90, 255),
                              QColor(0, 255, 120, 0), QColor(30, 80, 120, 200),
                              QColor(90, 130, 20, 70), QColor(255, 255, 255, 255) };
    for (int i = 0; i < 6; ++i) source.setPixelColor(i % 3, i / 3, colors[i]);
    const QString input = directory.filePath("source.png");
    const QString output = directory.filePath("output.png");
    QVERIFY(source.save(input));
    QVLayers::Layer filter;
    filter.kind = QVLayers::Kind::Filter;
    filter.filter.brightness = 12;
    filter.filter.hue = 31;
    filter.filter.contrast = 20;
    filter.filter.saturation = -17;
    filter.filter.transparency = 30;
    filter.filter.gradient = true;
    filter.filter.direction = 25;
    filter.strength = 63;
    for (int mode = 0; mode < 6; ++mode) {
        QVLayers::Stack stack;
        stack.layers[0].strength = 70;
        filter.blend = QVLayers::Blend(mode);
        stack.layers.prepend(filter);
        QVLayers::Layer copy;
        copy.strength = 40;
        copy.blend = QVLayers::Blend(mode);
        stack.layers.prepend(copy);
        QProcess process;
        process.start(ffmpeg, { "-hide_banner", "-loglevel", "error", "-y", "-i", input,
                              "-vf", QVLayers::ffmpegFilter(stack), "-frames:v", "1", "-update", "1", output });
        QVERIFY(process.waitForFinished(15000));
        const QByteArray error = process.readAllStandardError();
        QVERIFY2(process.exitCode() == 0, error.constData());
        const QImage encoded(output);
        const QImage expected = QVLayers::apply(source, stack);
        QCOMPARE(encoded.size(), expected.size());
        for (int y = 0; y < source.height(); ++y) {
            for (int x = 0; x < source.width(); ++x) {
                const auto a = encoded.pixelColor(x, y), b = expected.pixelColor(x, y);
                QVERIFY2(qAbs(a.red()-b.red()) <= 1 && qAbs(a.green()-b.green()) <= 1
                         && qAbs(a.blue()-b.blue()) <= 1 && qAbs(a.alpha()-b.alpha()) <= 1,
                         qPrintable(QString("Mode %1 at %2,%3: exported %4, expected %5")
                                    .arg(mode).arg(x).arg(y).arg(a.name(QColor::HexArgb), b.name(QColor::HexArgb))));
            }
        }
    }
}

QTEST_GUILESS_MAIN(LayersTests)
#include "tst_layerstests.moc"
