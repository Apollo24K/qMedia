#include <QtTest>
#include <QImageReader>
#include <QProcess>
#include <QTemporaryDir>
#include "qvexport.h"

class ExportTests : public QObject
{
    Q_OBJECT
private slots:
    void stillImage();
    void preservesFilesOnFailureAndCancellation();
    void conversionArguments();
    void wholeMedia();
    void transforms();
    void videoSpeed();
};

void ExportTests::stillImage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVExport::Source source;
    source.path = directory.filePath("source.png");
    QImage image(32, 24, QImage::Format_ARGB32);
    image.fill(QColor(255, 0, 0, 0));
    QVERIFY(image.save(source.path));
    QVExport::Options options;
    options.size = QSize(16, 12);
    auto cancel = std::make_shared<std::atomic_bool>(false);
    const QString output = directory.filePath("export.png");
    QVERIFY(QVExport::run(source, options, output, {}, cancel).error.isEmpty());
    QImage result(output);
    QCOMPARE(result.size(), options.size);
    QCOMPARE(result.pixelColor(0, 0).alpha(), 0);
    options.format = "jpeg";
    const QString jpeg = directory.filePath("export.jpg");
    QVERIFY(QVExport::run(source, options, jpeg, {}, cancel).error.isEmpty());
    QCOMPARE(QImage(jpeg).pixelColor(0, 0), QColor(Qt::white));
    QCOMPARE(QImage(source.path).size(), image.size());
}

void ExportTests::transforms()
{
    QTemporaryDir directory;
    QVExport::Source source;
    source.frame = QImage(2, 3, QImage::Format_RGB32);
    const QColor colors[] = { Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::magenta };
    for (int i = 0; i < 6; ++i) source.frame.setPixelColor(i % 2, i / 2, colors[i]);
    QVExport::Options options;
    options.size = QSize(3, 2);
    options.rotation = 90;
    options.mirrored = true;
    options.flipped = true;
    const QString output = directory.filePath("transformed.png");
    const auto result = QVExport::run(source, options, output, {}, std::make_shared<std::atomic_bool>(false));
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    const QImage image(output);
    QCOMPARE(image.size(), QSize(3, 2));
    QCOMPARE(image.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(image.pixelColor(2, 1), QColor(Qt::cyan));
}

void ExportTests::preservesFilesOnFailureAndCancellation()
{
    QTemporaryDir directory;
    QVExport::Source source;
    source.path = directory.filePath("original.png");
    source.frame = QImage(4, 4, QImage::Format_RGB32);
    source.frame.fill(Qt::red);
    QVERIFY(source.frame.save(source.path));
    QVExport::Options options;
    options.size = QSize(2, 2);
    auto cancel = std::make_shared<std::atomic_bool>(false);
    QVERIFY(!QVExport::run(source, options, source.path, {}, cancel).error.isEmpty());
    const QString output = directory.filePath("existing.png");
    QFile file(output);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("keep me");
    file.close();
    options.format = "invalid-format";
    QVERIFY(!QVExport::run(source, options, output, {}, cancel).error.isEmpty());
    cancel->store(true);
    options.format = "png";
    QVERIFY(QVExport::run(source, options, output, {}, cancel).cancelled);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep me"));
    QCOMPARE(QImage(source.path).size(), QSize(4, 4));
}

void ExportTests::conversionArguments()
{
    QVExport::Options options;
    options.wholeMedia = true;
    options.size = QSize(640, 360);
    options.format = "mp4";
    const QString input = "a file with spaces.mov";
    auto args = QVExport::arguments(input, "output.mp4", options);
    QCOMPARE(args.at(args.indexOf("-i") + 1), input);
    QVERIFY(args.contains("0:a:0?"));
    QVERIFY(args.contains("libx264"));
    options.audio = false;
    args = QVExport::arguments(input, "output.mp4", options);
    QVERIFY(args.contains("-an"));
    QVERIFY(!args.contains("0:a:0?"));
    options.format = "gif";
    options.loop = false;
    args = QVExport::arguments(input, "output.gif", options);
    QVERIFY(args.at(args.indexOf("-filter_complex") + 1).contains("paletteuse"));
    QCOMPARE(args.at(args.indexOf("-loop") + 1), QString("-1"));
}

void ExportTests::wholeMedia()
{
    const QString ffmpeg = QVExport::findFFmpeg();
    if (ffmpeg.isEmpty())
        QSKIP("FFmpeg is optional and not installed");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.gif");
    QProcess fixture;
    fixture.start(ffmpeg, { "-v", "error", "-f", "lavfi", "-i", "testsrc=size=32x24:rate=5",
                           "-t", "0.6", "-y", input });
    QVERIFY(fixture.waitForFinished(30000));
    QCOMPARE(fixture.exitCode(), 0);
    // Unequal frame delays catch reversal bugs hidden by a constant frame rate.
    QFile gif(input);
    QVERIFY(gif.open(QIODevice::ReadWrite));
    QByteArray gifBytes = gif.readAll();
    int offset = 0;
    int frameIndex = 0;
    while ((offset = gifBytes.indexOf(QByteArray::fromHex("21f904"), offset)) >= 0) {
        gifBytes[offset + 4] = char(++frameIndex * 10);
        gifBytes[offset + 5] = 0;
        offset += 8;
    }
    QCOMPARE(frameIndex, 3);
    QVERIFY(gif.seek(0));
    QCOMPARE(gif.write(gifBytes), qint64(gifBytes.size()));
    gif.close();
    QVExport::Source source;
    source.path = input;
    source.animated = true;
    source.frameNumber = 1;
    QVExport::Options options;
    options.size = QSize(32, 24);
    auto cancel = std::make_shared<std::atomic_bool>(false);
    const QString still = directory.filePath("selected.png");
    auto result = QVExport::run(source, options, still, ffmpeg, cancel);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QImageReader reader(input);
    reader.read();
    QCOMPARE(QImage(still), reader.read());
    options.wholeMedia = true;
    options.size = QSize(16, 12);
    for (const QString &format : { "gif", "webp", "mp4", "webm" }) {
        options.format = format;
        const QString output = directory.filePath("converted." + format);
        result = QVExport::run(source, options, output, ffmpeg, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(QFileInfo(output).size() > 0);
        if (format == "gif") {
            QImageReader converted(output);
            QCOMPARE(converted.size(), QSize(16, 12));
            QCOMPARE(converted.imageCount(), 3);
            int duration = 0;
            while (converted.canRead()) {
                QVERIFY(!converted.read().isNull());
                duration += converted.nextImageDelay();
            }
            QCOMPARE(duration, 600);
        }
    }
    // Rotation precedes screen-axis flips; reversing preserves frames and their duration.
    options.format = "gif";
    options.size = QSize(24, 32);
    options.rotation = 90;
    options.mirrored = true;
    options.flipped = true;
    options.reverse = true;
    options.loop = false;
    const QString reversedPath = directory.filePath("reversed.gif");
    result = QVExport::run(source, options, reversedPath, ffmpeg, cancel);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QImageReader original(input), reversed(reversedPath);
    QList<QImage> frames;
    while (original.canRead()) frames.append(original.read());
    QCOMPARE(reversed.imageCount(), frames.size());
    QCOMPARE(reversed.loopCount(), 0);
    for (int index = frames.size() - 1; index >= 0; --index) {
        const auto expected = frames[index].transformed(QTransform().rotate(90)).mirrored(true, true);
        QCOMPARE(reversed.read().convertToFormat(QImage::Format_RGB32), expected.convertToFormat(QImage::Format_RGB32));
        QCOMPARE(reversed.nextImageDelay(), (index + 1) * 100);
    }
    options.rotation = 0;
    options.mirrored = options.flipped = false;
    options.size = QSize(16, 12);
    // Exercise the direct video path, including reversal and optional audio mapping on a silent clip.
    source.path = directory.filePath("converted.mp4");
    source.video = true;
    source.animated = false;
    options.format = "webm";
    result = QVExport::run(source, options, directory.filePath("video.webm"), ffmpeg, cancel);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

    const QString withAudio = directory.filePath("with-audio.mp4");
    fixture.start(ffmpeg, { "-v", "error", "-i", source.path, "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=0.6", "-c:v", "copy", "-c:a", "aac",
                           "-shortest", "-y", withAudio });
    QVERIFY(fixture.waitForFinished(30000));
    QCOMPARE(fixture.exitCode(), 0);
    source.path = withAudio;
    for (bool includeAudio : { true, false }) {
        options.audio = includeAudio;
        const QString output = directory.filePath(includeAudio ? "audible.webm" : "silent.webm");
        result = QVExport::run(source, options, output, ffmpeg, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        fixture.start(ffmpeg, { "-v", "error", "-i", output, "-map", "0:a:0",
                               "-frames:a", "1", "-f", "null", "-" });
        QVERIFY(fixture.waitForFinished(30000));
        QCOMPARE(fixture.exitCode() == 0, includeAudio);
    }
}

void ExportTests::videoSpeed()
{
    const QString ffmpeg = QVExport::findFFmpeg();
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is not installed");
    QTemporaryDir directory;
    QVExport::Source source;
    source.path = directory.filePath("speed-source.mp4");
    source.video = true;
    source.durationMs = 2000;
    QProcess process;
    process.start(ffmpeg, { "-v", "error", "-f", "lavfi", "-i", "testsrc=size=32x24:rate=10",
                           "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=8000",
                           "-t", "2", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", source.path });
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);
    QVExport::Options options;
    options.wholeMedia = true;
    options.format = "mp4";
    options.size = QSize(32, 24);
    auto cancel = std::make_shared<std::atomic_bool>(false);
    for (double speed : { 0.5, 2.0, 4.0 }) {
        options.speed = speed;
        const QString output = directory.filePath(QString("speed-%1.mp4").arg(speed));
        const auto result = QVExport::run(source, options, output, ffmpeg, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.durationMs, qRound64(2000 / speed));
        process.start(ffmpeg, { "-v", "error", "-i", output, "-map", "0:a:0", "-ac", "1",
                               "-ar", "8000", "-f", "s16le", "-" });
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        const double audioSeconds = process.readAllStandardOutput().size() / 16000.0;
        QVERIFY2(qAbs(audioSeconds - 2.0 / speed) < 0.25, qPrintable(QString::number(audioSeconds)));
        process.start(ffmpeg, { "-v", "error", "-i", output, "-map", "0:v:0", "-progress", "pipe:1",
                               "-f", "null", "-" });
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        const auto outputLines = process.readAllStandardOutput().split('\n');
        double videoSeconds = -1;
        for (const auto &line : outputLines)
            if (line.startsWith("out_time_us=")) videoSeconds = line.mid(12).trimmed().toDouble() / 1000000.0;
        QVERIFY2(qAbs(videoSeconds - 2.0 / speed) < 0.25, qPrintable(QString::number(videoSeconds)));
    }
}

QTEST_GUILESS_MAIN(ExportTests)
#include "tst_exporttests.moc"
