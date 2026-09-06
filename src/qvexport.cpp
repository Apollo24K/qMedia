#include "qvexport.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QProcess>
#include <QPainter>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#include <QColorSpace>
#endif

namespace QVExport {
qint64 mediaDuration(const Source &source, const std::shared_ptr<std::atomic_bool> &cancel)
{
    if (source.durationMs >= 0 || !source.animated)
        return source.durationMs;
    QImageReader reader(source.path);
    reader.setFormat(source.animationFormat);
    qint64 duration = 0;
    while (reader.canRead()) {
        if (cancel->load() || reader.read().isNull()) return -1;
        duration += qMax(10, reader.nextImageDelay());
    }
    return duration > 0 ? duration : -1;
}

QString findFFmpeg()
{
    const QString configured = QSettings().value("export/ffmpeg").toString();
    if (QFileInfo(configured).isExecutable() && QFileInfo(configured).isFile())
        return configured;
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QStringList imageFormats()
{
    QStringList result;
    const auto supported = QImageWriter::supportedImageFormats();
    for (const auto &format : { "png", "jpeg", "webp", "tiff", "bmp" }) {
        if (supported.contains(format))
            result.append(QString::fromLatin1(format));
    }
    return result;
}

bool sameFile(const QString &source, const QString &destination)
{
    if (source.isEmpty())
        return false;
    const QFileInfo a(source), b(destination);
    const QString left = a.exists() ? a.canonicalFilePath() : a.absoluteFilePath();
    const QString right = b.exists() ? b.canonicalFilePath() : b.absoluteFilePath();
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

QStringList arguments(const QString &input, const QString &output, const Options &o, bool concat)
{
    QStringList args{ "-hide_banner", "-loglevel", "error", "-nostdin", "-y" };
    if (concat)
        args << "-f" << "concat" << "-safe" << "0";
    args << "-i" << input;
    QStringList filters;
    const int rotation = (o.rotation % 360 + 360) % 360;
    if (rotation == 90) filters << "transpose=clock";
    if (rotation == 180) filters << "hflip" << "vflip";
    if (rotation == 270) filters << "transpose=cclock";
    if (o.mirrored) filters << "hflip";
    if (o.flipped) filters << "vflip";
    if (!o.layers.isNeutral()) filters << QVLayers::ffmpegFilter(o.layers);
    filters << QString("scale=%1:%2:flags=lanczos").arg(o.size.width()).arg(o.size.height());
    if (o.reverse && !concat) filters << "reverse" << "setpts=PTS-STARTPTS";
    if (!qFuzzyCompare(o.speed, 1.0))
        filters << QString("setpts=(PTS-STARTPTS)/%1").arg(o.speed, 0, 'f', 4);
    const QString scale = filters.join(',');
    if (o.format == "gif") {
        args << "-filter_complex"
             << QString("[0:v:0]%1,split[a][b];[a]palettegen[p];[b][p]paletteuse[v]").arg(scale)
             << "-map" << "[v]" << "-an" << "-loop" << (o.loop ? "0" : "-1");
    } else {
        args << "-map" << "0:v:0" << "-vf" << (scale + ",setsar=1");
        if (o.format == "mp4" || o.format == "webm") {
            if (o.audio)
                args << "-map" << "0:a:0?" << "-c:a" << (o.format == "mp4" ? "aac" : "libopus");
            else
                args << "-an";
            QStringList audioFilters;
            if (o.reverse) audioFilters << "areverse" << "asetpts=PTS-STARTPTS";
            double tempo = o.speed;
            while (tempo > 2.0) { audioFilters << "atempo=2"; tempo /= 2.0; }
            while (tempo < 0.5) { audioFilters << "atempo=0.5"; tempo *= 2.0; }
            if (!qFuzzyCompare(tempo, 1.0))
                audioFilters << QString("atempo=%1").arg(tempo, 0, 'f', 4);
            if (o.audio && !audioFilters.isEmpty()) args << "-af" << audioFilters.join(',');
            args << "-c:v" << (o.format == "mp4" ? "libx264" : "libvpx-vp9")
                 << "-crf" << QString::number(qRound(45.0 - o.quality * 0.35))
                 << "-pix_fmt" << "yuv420p";
            if (o.format == "mp4")
                args << "-movflags" << "+faststart";
            else
                args << "-b:v" << "0";
        } else if (o.format == "webp") {
            args << "-an" << "-c:v" << "libwebp_anim" << "-quality" << QString::number(o.quality)
                 << "-loop" << (o.loop ? "0" : "1");
        }
    }
    if (!concat && !qFuzzyCompare(o.speed, 1.0)) args << "-fps_mode" << "vfr";
    args << output;
    return args;
}

static Result process(const QString &program, const QStringList &args,
                      const std::shared_ptr<std::atomic_bool> &cancel)
{
    QProcess encoder;
    encoder.start(program, args);
    if (!encoder.waitForStarted())
        return { encoder.errorString(), false };
    QByteArray errors;
    while (!encoder.waitForFinished(100)) {
        errors += encoder.readAllStandardError();
        errors = errors.right(8192);
        if (cancel->load()) {
            encoder.kill();
            encoder.waitForFinished();
            return { {}, true };
        }
    }
    errors += encoder.readAllStandardError();
    if (encoder.exitStatus() != QProcess::NormalExit || encoder.exitCode() != 0)
        return { QString::fromLocal8Bit(errors.right(8192)).trimmed().isEmpty()
                         ? QStringLiteral("FFmpeg could not encode this file.")
                         : QString::fromLocal8Bit(errors.right(8192)).trimmed(), false };
    return {};
}

Result run(const Source &source, const Options &options, const QString &destination,
           const QString &ffmpeg, const std::shared_ptr<std::atomic_bool> &cancel)
{
    if (sameFile(source.path, destination))
        return { QStringLiteral("Choose a different filename to preserve the original."), false };
    if (!options.size.isValid())
        return { QStringLiteral("The output dimensions must be positive."), false };
    if (!(options.speed >= 0.1 && options.speed <= 10.0))
        return { QStringLiteral("Playback speed must be between 0.1 and 10."), false };
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return { QStringLiteral("Could not create temporary export files."), false };
    if (cancel->load())
        return { {}, true };

    if (options.wholeMedia && (options.layers.hasDistortion() || options.layers.hasCanvas()))
        return { QStringLiteral("Canvas edits currently support still-image export only."), false };
    if (!options.wholeMedia) {
        QImage frame = source.frame;
        if (frame.isNull() && !source.video) {
            QImageReader reader(source.path);
            if (source.animated)
                reader.setFormat(source.animationFormat);
            reader.setAutoTransform(true);
            if (reader.format() == "svg" || reader.format() == "svgz")
                reader.setScaledSize(options.rotation % 180
                                             ? options.size.transposed() : options.size);
            for (int index = 0; index <= source.frameNumber; ++index) {
                if (cancel->load())
                    return { {}, true };
                frame = reader.read();
                if (frame.isNull())
                    return { reader.errorString(), false };
            }
        } else if (frame.isNull()) {
            // Qt 5 backends without a readable video surface can extract by timestamp.
            const QString captured = temporary.filePath("frame.png");
            const Result extracted = process(ffmpeg,
                    { "-v", "error", "-nostdin", "-y", "-i", source.path, "-ss",
                      QString::number(source.positionMs / 1000.0, 'f', 3), "-frames:v", "1", captured }, cancel);
            if (!extracted.error.isEmpty() || extracted.cancelled)
                return extracted;
            frame.load(captured);
        }
        if (frame.isNull())
            return { QStringLiteral("The current frame is not available yet."), false };
        frame = frame.transformed(QTransform().rotate(options.rotation));
        // Match the canvas: image rotation precedes effects; view mirroring follows.
        if ((options.layers.hasDistortion() || options.layers.hasCanvas()))
            frame = QVLayers::apply(frame, QVLayers::rotated(options.layers, options.rotation));
        frame = frame.mirrored(options.mirrored, options.flipped);
        frame = frame.scaled(options.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (!(options.layers.hasDistortion() || options.layers.hasCanvas())) frame = QVLayers::apply(frame, options.layers);
        // Flatten transparency explicitly for formats without an alpha channel.
        if (options.format == "jpeg" || options.format == "bmp") {
            QImage opaque(frame.size(), QImage::Format_RGB32);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            opaque.setColorSpace(frame.colorSpace());
#endif
            opaque.fill(Qt::white);
            QPainter painter(&opaque);
            painter.drawImage(0, 0, frame);
            painter.end();
            frame = opaque;
        }
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly))
            return { output.errorString(), false };
        QImageWriter writer(&output, options.format.toLatin1());
        writer.setQuality(options.quality);
        if (!writer.write(frame))
            return { writer.errorString(), false };
        if (cancel->load())
            return { {}, true };
        if (!output.commit())
            return { output.errorString(), false };
        return {};
    }

    QString input = source.path;
    double duration = 0;
    double lastFrameDelay = 0;
    if (source.animated) {
        // Decode through Qt so every animation supported by the viewer can be exported,
        // including formats not supported by the installed FFmpeg decoder.
        QImageReader reader(source.path);
        reader.setFormat(source.animationFormat);
        reader.setAutoTransform(true);
        QFile manifest(temporary.filePath("frames.txt"));
        if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text))
            return { manifest.errorString(), false };
        QTextStream stream(&manifest);
        int index = 0;
        QList<double> delays;
        while (reader.canRead()) {
            if (cancel->load())
                return { {}, true };
            const QImage frame = reader.read();
            if (frame.isNull())
                return { reader.errorString(), false };
            const QString name = QString("frame%1.png").arg(index++);
            if (!frame.save(temporary.filePath(name)))
                return { QStringLiteral("Could not write an animation frame. Check free disk space."), false };
            const double seconds = qMax(10, reader.nextImageDelay()) / 1000.0;
            delays.append(seconds);
            duration += seconds;
        }
        if (!index)
            return { QStringLiteral("The animation contains no readable frames."), false };
        lastFrameDelay = options.reverse ? delays.first() : delays.last();
        for (int position = 0; position < index; ++position) {
            const int frameIndex = options.reverse ? index - 1 - position : position;
            stream << "file 'frame" << frameIndex << ".png'\noption framerate 1000\nduration "
                   << QString::number(delays.at(frameIndex), 'f', 3) << '\n';
        }
        stream << "file 'frame" << (options.reverse ? 0 : index - 1)
               << ".png'\noption framerate 1000\n";
        stream.flush();
        manifest.close();
        input = manifest.fileName();
    }
    const QString encoded = temporary.filePath("export." + options.format);
    QStringList args = arguments(input, encoded, options, source.animated);
    // A layer stack can exceed Windows' command-line limit. Keep long graphs
    // in the existing export scratch directory for the lifetime of the encoder.
    for (const QString &option : { QString("-vf"), QString("-filter_complex") }) {
        const int index = args.indexOf(option);
        if (index < 0 || args[index + 1].size() < 8192) continue;
        QFile graph(temporary.filePath("layers.ffgraph"));
        if (!graph.open(QIODevice::WriteOnly)) return { graph.errorString(), false };
        const QByteArray contents = args[index + 1].toUtf8();
        if (graph.write(contents) != contents.size()) return { graph.errorString(), false };
        graph.close();
        args[index] = option == "-vf" ? "-filter_script:v" : "-filter_complex_script";
        args[index + 1] = graph.fileName();
    }
    if (source.animated) {
        if (options.format == "gif") {
            args.insert(args.size() - 1, "-final_delay");
            args.insert(args.size() - 1, QString::number(qMax(1, qRound(lastFrameDelay * 100 / options.speed))));
        }
        args.insert(args.size() - 1, "-t");
        args.insert(args.size() - 1, QString::number(duration / options.speed, 'f', 3));
        args.insert(args.size() - 1, "-fps_mode");
        args.insert(args.size() - 1, "vfr");
    }
    const Result converted = process(ffmpeg, args, cancel);
    if (!converted.error.isEmpty() || converted.cancelled)
        return converted;
    QFile inputFile(encoded);
    QSaveFile output(destination);
    if (!inputFile.open(QIODevice::ReadOnly))
        return { inputFile.errorString(), false };
    if (inputFile.size() == 0)
        return { QStringLiteral("The encoder produced an empty file."), false };
    if (!output.open(QIODevice::WriteOnly))
        return { output.errorString(), false };
    while (!inputFile.atEnd()) {
        if (cancel->load())
            return { {}, true };
        const QByteArray bytes = inputFile.read(1024 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            return { QStringLiteral("Could not write the exported file. Check free disk space."), false };
    }
    if (cancel->load())
        return { {}, true };
    if (!output.commit())
        return { output.errorString(), false };
    return { {}, false, source.animated ? qRound64(duration * 1000 / options.speed)
                                       : source.durationMs > 0 ? qRound64(source.durationMs / options.speed) : -1 };
}
}
