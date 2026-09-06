#include "qvexportdialog.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QImageReader>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <QHBoxLayout>
#include <QTimer>
#include <QTemporaryDir>
#include <QMovie>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QStackedWidget>
#include <QUrl>
#include <QPainter>
#include <QApplication>
#include <QEvent>

QVExportDialog::QVExportDialog(const QVExport::Source &source, QWidget *parent)
    : QDialog(parent), source(source)
{
    setWindowTitle(tr("Export"));
    setWindowModality(Qt::WindowModal);
    setMinimumWidth(740);
    auto *layout = new QVBoxLayout(this);
    fileName = new QLineEdit(this);
    fileName->setObjectName("exportFileName");
    fileName->setPlaceholderText(tr("File name"));
    fileName->setAccessibleName(tr("File name"));
    layout->addWidget(fileName);
    auto *body = new QHBoxLayout;
    auto *previewLayout = new QVBoxLayout;
    previewStack = new QStackedWidget(this);
    previewStack->setFixedSize(340, 260);
    preview = new QLabel(this);
    preview->setObjectName("exportPreview");
    preview->setAlignment(Qt::AlignCenter);
    QPixmap checkerboard(16, 16);
    checkerboard.fill(QColor(230, 230, 230));
    QPainter checker(&checkerboard);
    checker.fillRect(0, 0, 8, 8, QColor(200, 200, 200));
    checker.fillRect(8, 8, 8, 8, QColor(200, 200, 200));
    checker.end();
    QPalette previewPalette = preview->palette();
    previewPalette.setBrush(QPalette::Window, QBrush(checkerboard));
    previewPalette.setColor(QPalette::WindowText, Qt::black);
    preview->setPalette(previewPalette);
    preview->setAutoFillBackground(true);
    preview->setText(tr("Preparing preview…"));
    previewStack->addWidget(preview);
    previewLayout->addWidget(previewStack);
    previewStatus = new QLabel(this);
    previewStatus->setWordWrap(true);
    previewStatus->setObjectName("exportDetails");
    previewStatus->setMaximumWidth(340);
    previewStatus->setMaximumHeight(90);
    previewLayout->addWidget(previewStatus);
    playPreview = new QPushButton(tr("Play preview"), this);
    previewLayout->addWidget(playPreview);
    previewLayout->addStretch();
    body->addLayout(previewLayout);
    auto *form = new QFormLayout;
    scope = new QComboBox(this);
    scope->setObjectName("exportScope");
    scope->addItem(source.video || source.animated ? tr("Current frame") : tr("Image"));
    if (source.video || source.animated)
        scope->addItem(source.video ? tr("Entire video") : tr("Entire animation"));
    form->addRow(tr("Source"), scope);
    format = new QComboBox(this);
    format->setObjectName("exportFormat");
    form->addRow(tr("Format"), format);
    sizeMode = new QComboBox(this);
    sizeMode->setObjectName("exportSizeMode");
    sizeMode->addItems({ tr("Pixels"), tr("Percentage") });
    percentage = new QDoubleSpinBox(this);
    percentage->setObjectName("exportPercentage");
    percentage->setRange(0.1, 10000.0);
    percentage->setDecimals(1);
    percentage->setSuffix(" %");
    percentage->setValue(100);
    const int longestSide = qMax(source.size.width(), source.size.height());
    if (longestSide > 0)
        percentage->setMaximum(qMin(10000.0, int(65535000.0 / longestSide) / 10.0));
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(sizeMode);
    sizeRow->addWidget(percentage);
    form->addRow(tr("Resize"), sizeRow);
    width = new QSpinBox(this);
    height = new QSpinBox(this);
    width->setObjectName("exportWidth");
    height->setObjectName("exportHeight");
    for (auto *dimension : { width, height }) {
        dimension->setRange(1, 65535);
        dimension->setSuffix(tr(" px"));
    }
    width->setValue(source.size.width());
    height->setValue(source.size.height());
    form->addRow(tr("Width"), width);
    form->addRow(tr("Height"), height);
    aspect = new QCheckBox(tr("Keep aspect ratio"), this);
    aspect->setChecked(true);
    auto *original = new QPushButton(tr("Original size"), this);
    original->setObjectName("exportOriginalSize");
    auto *aspectRow = new QHBoxLayout;
    aspectRow->addWidget(aspect);
    aspectRow->addWidget(original);
    form->addRow(QString(), aspectRow);
    quality = new QSpinBox(this);
    quality->setRange(1, 100);
    quality->setValue(90);
    quality->setToolTip(tr("Higher quality produces larger files. Lossless formats preserve image quality."));
    form->addRow(tr("Quality"), quality);
    speed = new QDoubleSpinBox(this);
    speed->setObjectName("exportSpeed");
    speed->setRange(0.1, 10.0);
    speed->setDecimals(2);
    speed->setSingleStep(0.25);
    speed->setSuffix(tr(" times"));
    speed->setValue(source.speed);
    speedLabel = new QLabel(tr("Speed"), this);
    form->addRow(speedLabel, speed);
    audio = new QCheckBox(tr("Include audio"), this);
    audio->setChecked(!source.muted);
    form->addRow(QString(), audio);
    loop = new QCheckBox(tr("Loop animation"), this);
    loop->setChecked(source.loop);
    loop->setObjectName("exportLoop");
    form->addRow(QString(), loop);
    rotate = new QCheckBox(tr("Rotate"), this);
    rotate->setObjectName("exportRotate");
    rotate->setChecked(source.rotation != 0);
    rotation = new QComboBox(this);
    rotation->setObjectName("exportRotation");
    rotation->addItem(tr("90° clockwise"), 90);
    rotation->addItem(tr("180°"), 180);
    rotation->addItem(tr("90° counterclockwise"), 270);
    rotation->setCurrentIndex(qMax(0, rotation->findData(source.rotation)));
    form->addRow(rotate, rotation);
    mirror = new QCheckBox(tr("Mirror horizontally"), this);
    mirror->setObjectName("exportMirror");
    mirror->setChecked(source.mirrored);

    flip = new QCheckBox(tr("Flip vertically"), this);
    flip->setObjectName("exportFlip");
    flip->setChecked(source.flipped);
    auto *flipRow = new QHBoxLayout;
    flipRow->addWidget(mirror);
    flipRow->addWidget(flip);
    form->addRow(QString(), flipRow);
    reverse = new QCheckBox(tr("Reverse playback"), this);
    reverse->setObjectName("exportReverse");
    reverse->setToolTip(tr("Reverse the entire clip, including audio. Long videos can require substantial memory."));
    form->addRow(QString(), reverse);
    applyFilters = new QCheckBox(tr("Apply canvas layers"), this);
    applyFilters->setObjectName("exportApplyFilters");
    applyFilters->setChecked(true);
    form->addRow(QString(), applyFilters);
    lastRotation = source.rotation;
    width->setValue(orientedSize().width());
    height->setValue(orientedSize().height());
    connect(applyFilters, &QCheckBox::toggled, this, [this] {
        if (this->source.layers.hasCanvas()) {
            width->setValue(orientedSize().width());
            height->setValue(orientedSize().height());
        }
    });
    body->addLayout(form);
    layout->addLayout(body);
    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);
    ffmpegButton = new QPushButton(tr("Locate FFmpeg…"), this);
    layout->addWidget(ffmpegButton);
    progress = new QProgressBar(this);
    progress->setRange(0, 0);
    progress->hide();
    layout->addWidget(progress);
    auto *buttons = new QDialogButtonBox(this);
    exportButton = buttons->addButton(tr("Export…"), QDialogButtonBox::AcceptRole);
    exportButton->setObjectName("exportButton");
    exportButton->setDefault(true);
    cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QVExportDialog::startExport);
    connect(buttons, &QDialogButtonBox::rejected, this, &QVExportDialog::reject);
    connect(scope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVExportDialog::updateFormats);
    connect(format, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVExportDialog::updateControls);
    previewTimer = new QTimer(this);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(200);
    previewMovie = new QMovie(this);
    connect(previewTimer, &QTimer::timeout, this, &QVExportDialog::startPreview);
    connect(playPreview, &QPushButton::clicked, this, [this] { requestPreview(true); });
    connect(rotate, &QCheckBox::toggled, this, &QVExportDialog::rotationChanged);
    connect(rotation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVExportDialog::rotationChanged);
    for (auto *box : { mirror, flip, reverse, loop, audio, applyFilters })
        connect(box, &QCheckBox::toggled, this, [this] { requestPreview(); });
    connect(sizeMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index == 1) {
            aspect->setChecked(true);
            applyPercentage();
        }
        updateControls();
    });
    connect(percentage, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this] { if (sizeMode->currentIndex() == 1) applyPercentage(); });
    connect(speed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { requestPreview(); });
    connect(quality, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { requestPreview(); });
    connect(width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (aspect->isChecked() && orientedSize().width() > 0) {
            const QSignalBlocker blocker(height);
            height->setValue(qRound(value * double(orientedSize().height()) / orientedSize().width()));
        }
        if (orientedSize().width() > 0) {
            const QSignalBlocker blocker(percentage);
            percentage->setValue(100.0 * value / orientedSize().width());
        }
        requestPreview();
    });
    connect(height, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (aspect->isChecked() && orientedSize().height() > 0) {
            const QSignalBlocker blocker(width);
            width->setValue(qRound(value * double(orientedSize().width()) / orientedSize().height()));
        }
        if (orientedSize().height() > 0) {
            const QSignalBlocker blocker(percentage);
            percentage->setValue(100.0 * value / orientedSize().height());
        }
        requestPreview();
    });
    connect(aspect, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && orientedSize().width() > 0)
            height->setValue(qRound(width->value() * double(orientedSize().height()) / orientedSize().width()));
    });
    connect(original, &QPushButton::clicked, this, [this] {
        const QSignalBlocker blockWidth(width), blockHeight(height), blockPercent(percentage);
        percentage->setValue(100);
        width->setValue(orientedSize().width());
        height->setValue(orientedSize().height());
        requestPreview();
    });
    connect(ffmpegButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Locate FFmpeg executable"));
        if (!path.isEmpty())
            QSettings().setValue("export/ffmpeg", path);
        updateControls();
    });
    connect(&watcher, &QFutureWatcher<QVExport::Result>::finished, this, [this, original] {
        busy = false;
        const auto result = watcher.result();
        progress->hide();
        original->setEnabled(true);
        cancelButton->setEnabled(true);
        cancelButton->setText(tr("Cancel"));
        updateControls();
        if (result.cancelled) {
            status->setText(tr("Export cancelled."));
        } else if (!result.error.isEmpty()) {
            QMessageBox::warning(this, tr("Export failed"), result.error);
        } else {
            accept();
        }
    });
    connect(&watcher, &QFutureWatcher<QVExport::Result>::started, original, [original] { original->setEnabled(false); });
    connect(&previewWatcher, &QFutureWatcher<QVExport::Result>::finished, this, [this] {
        if (runningPreviewGeneration != previewGeneration) {
            previewTimer->start();
            return;
        }
        playPreview->setEnabled(!busy && !QVExport::findFFmpeg().isEmpty());
        const auto result = previewWatcher.result();
        if (result.cancelled) return;
        if (!result.error.isEmpty()) {
            preview->setText(tr("Preview unavailable"));
            previewStatus->setText(result.error);
            previewStatus->setToolTip(result.error);
            return;
        }
        if (runningWholePreview && (QFileInfo(previewPath).suffix() == "mp4"
                                    || QFileInfo(previewPath).suffix() == "webm")) {
            if (!previewPlayer) {
                previewPlayer = new QMediaPlayer(this);
                previewVideo = new QVideoWidget(this);
                previewStack->addWidget(previewVideo);
                previewPlayer->setVideoOutput(previewVideo);
                connect(previewPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
                    if (duration > 0 && runningPreviewGeneration == previewGeneration && runningWholePreview)
                        updatePreviewDetails(previewBytes, duration);
                });
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                previewPlayer->setMuted(true);
                connect(previewPlayer, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
                        [this] { previewStatus->setText(previewPlayer->errorString()); });
#else
                // No QAudioOutput: preview stays silent and avoids cold audio initialization.
                connect(previewPlayer, &QMediaPlayer::errorOccurred, this,
                        [this] { previewStatus->setText(previewPlayer->errorString()); });
#endif
            }
            previewStack->setCurrentWidget(previewVideo);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            previewPlayer->setSource(QUrl::fromLocalFile(previewPath));
#else
            previewPlayer->setMedia(QUrl::fromLocalFile(previewPath));
#endif
            previewPlayer->play();
        } else if (runningWholePreview) {
            previewMovie->setFileName(previewPath);
            previewMovie->setScaledSize(selectedOptions().size.scaled(preview->size(), Qt::KeepAspectRatio));
            if (!previewMovie->isValid()) {
                previewStatus->setText(tr("This format cannot be previewed with the installed image plugins."));
                return;
            }
            preview->setMovie(previewMovie);
            previewMovie->start();
        } else {
            const QImage image(previewPath);
            if (image.isNull()) {
                previewStatus->setText(tr("The encoded image could not be previewed."));
                return;
            }
            preview->setPixmap(QPixmap::fromImage(image).scaled(preview->size(), Qt::KeepAspectRatio,
                                                               Qt::SmoothTransformation));
        }
        if (!runningWholePreview && result.durationMs > 0 && this->source.durationMs < 0)
            this->source.durationMs = result.durationMs;
        updatePreviewDetails(runningWholePreview || scope->currentIndex() == 0
                                     ? QFileInfo(previewPath).size() : -1,
                             runningWholePreview ? result.durationMs : -1);
    });
    updateFormats();
    qApp->installEventFilter(this);
}

QVExportDialog::~QVExportDialog()
{
    qApp->removeEventFilter(this);
    if (cancellation)
        cancellation->store(true);
    if (previewCancellation) previewCancellation->store(true);
    stopPreviewPlayback();
    delete previewPlayer;
    previewPlayer = nullptr;
    previewWatcher.waitForFinished();
    watcher.waitForFinished();
}

void QVExportDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    scope->setFocus(Qt::OtherFocusReason);
    fileName->deselect();
}

bool QVExportDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && (widget == this || isAncestorOf(widget))
            && widget != fileName && !fileName->isAncestorOf(widget)) {
            fileName->deselect();
            if (fileName->hasFocus()) fileName->clearFocus();
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QVExportDialog::reject()
{
    if (busy) {
        cancellation->store(true);
        status->setText(tr("Cancelling…"));
        cancelButton->setEnabled(false);
        return;
    }
    previewTimer->stop();
    ++previewGeneration;
    if (previewCancellation) previewCancellation->store(true);
    stopPreviewPlayback();
    QDialog::reject();
}

void QVExportDialog::updateFormats()
{
    const QSignalBlocker blocker(format);
    format->clear();
    const QStringList formats = scope->currentIndex() == 1
            ? (source.video ? QStringList{ "mp4", "webm", "gif", "webp" }
                            : QStringList{ "gif", "webp", "mp4", "webm" })
            : QVExport::imageFormats();
    for (const auto &name : formats)
        format->addItem(name.toUpper(), name);
    updateControls();
}

void QVExportDialog::updateControls()
{
    const bool whole = scope->currentIndex() == 1;
    const QString name = format->currentData().toString();
    const bool movie = whole && (name == "mp4" || name == "webm");
    const bool needsFFmpeg = whole || (source.video && source.frame.isNull());
    const bool missing = needsFFmpeg && QVExport::findFFmpeg().isEmpty();
    for (QWidget *widget : QList<QWidget *>{ scope, format, width, height, aspect, rotate,
                                             mirror, flip, applyFilters, fileName, sizeMode })
        widget->setEnabled(!busy);
    const bool percentMode = sizeMode->currentIndex() == 1;
    percentage->setVisible(percentMode);
    percentage->setEnabled(!busy && percentMode);
    width->setEnabled(!busy && !percentMode);
    height->setEnabled(!busy && !percentMode);
    aspect->setEnabled(!busy && !percentMode);
    speed->setVisible(source.video);
    speedLabel->setVisible(source.video);
    speed->setEnabled(!busy && whole && source.video);
    updateFileName();
    quality->setEnabled(!busy && (movie || name == "jpeg" || name == "webp"));
    audio->setVisible(movie && source.video);
    audio->setEnabled(!busy);
    loop->setVisible(whole && !movie);
    loop->setEnabled(!busy);
    rotation->setEnabled(!busy && rotate->isChecked());
    reverse->setVisible(source.video || source.animated);
    reverse->setEnabled(!busy && whole);
    playPreview->setVisible(whole);
    playPreview->setEnabled(!busy && !missing);
    if (!busy) requestPreview();
    ffmpegButton->setVisible(needsFFmpeg);
    ffmpegButton->setEnabled(!busy);
    exportButton->setEnabled(!busy && !missing && format->count() > 0);
    if (!busy)
        status->setText(missing ? tr("Whole-media conversion needs FFmpeg. Select an installed executable to continue.")
                               : movie ? tr("Video dimensions must be even. Audio is re-encoded when included.")
                                       : QString());
}

void QVExportDialog::startExport()
{
    if (busy)
        return;
    const auto options = selectedOptions();
    if (options.wholeMedia && (options.format == "mp4" || options.format == "webm")
        && (options.size.width() % 2 || options.size.height() % 2)) {
        QMessageBox::information(this, tr("Export"), tr("Choose an even width and height for this video format."));
        return;
    }
    QSettings settings;
    const QString directory = settings.value("export/directory", QFileInfo(source.path).absolutePath()).toString();
    const QString suggested = exportFileName();
    if (fileName->text().trimmed().isEmpty() || suggested.contains('/') || suggested.contains('\\')
        || suggested.contains(':') || suggested == "." || suggested == "..") {
        QMessageBox::warning(this, tr("Export"), tr("Enter a file name without a folder path."));
        return;
    }
    QFileDialog picker(this, tr("Export to"), QDir(directory).filePath(suggested));
    picker.setAcceptMode(QFileDialog::AcceptSave);
    picker.setNameFilter(options.format.toUpper() + " (*." + options.format + ")");
    picker.setDefaultSuffix(options.format);
    if (picker.exec() != QDialog::Accepted)
        return;
    const QString destination = picker.selectedFiles().first();
    if (QVExport::sameFile(source.path, destination)) {
        QMessageBox::warning(this, tr("Export"), tr("Choose a different filename to preserve the original."));
        return;
    }
    const QString suffix = QFileInfo(destination).suffix().toLower();
    if (suffix != options.format && !(options.format == "jpeg" && suffix == "jpg")
        && !(options.format == "tiff" && suffix == "tif")) {
        QMessageBox::warning(this, tr("Export"), tr("The filename extension must match the selected format."));
        return;
    }
    settings.setValue("export/directory", QFileInfo(destination).absolutePath());
    cancellation = std::make_shared<std::atomic_bool>(false);
    busy = true;
    previewTimer->stop();
    ++previewGeneration;
    if (previewCancellation) previewCancellation->store(true);
    stopPreviewPlayback();
    updateControls();
    status->setText(tr("Exporting…"));
    cancelButton->setText(tr("Cancel export"));
    progress->show();
    const auto capturedSource = source;
    const auto cancel = cancellation;
    const QString ffmpeg = QVExport::findFFmpeg();
    watcher.setFuture(QtConcurrent::run([capturedSource, options, destination, ffmpeg, cancel] {
        return QVExport::run(capturedSource, options, destination, ffmpeg, cancel);
    }));
}

QVExport::Options QVExportDialog::selectedOptions() const
{
    QVExport::Options options;
    options.wholeMedia = scope->currentIndex() == 1;
    options.format = format->currentData().toString();
    options.size = QSize(width->value(), height->value());
    options.quality = quality->value();
    options.audio = source.video && audio->isChecked();
    options.loop = loop->isChecked();
    options.rotation = rotate->isChecked() ? rotation->currentData().toInt() : 0;
    options.mirrored = mirror->isChecked();
    options.flipped = flip->isChecked();
    options.reverse = options.wholeMedia && reverse->isChecked();
    options.speed = options.wholeMedia && source.video ? speed->value() : 1.0;
    if (applyFilters->isChecked())
        options.layers = source.layers;
    return options;
}

QSize QVExportDialog::orientedSize() const
{
    QSize size = source.size;
    if (applyFilters && applyFilters->isChecked() && source.layers.hasCanvas())
        size = QVLayers::canvasPixels(size, source.layers.canvas).size();
    return rotate->isChecked() && rotation->currentData().toInt() % 180
            ? size.transposed() : size;
}

void QVExportDialog::rotationChanged()
{
    const int angle = rotate->isChecked() ? rotation->currentData().toInt() : 0;
    if ((angle % 180) != (lastRotation % 180)) {
        const QSignalBlocker blockWidth(width), blockHeight(height);
        const int previousWidth = width->value();
        width->setValue(height->value());
        height->setValue(previousWidth);
    }
    lastRotation = angle;
    rotation->setEnabled(rotate->isChecked() && !busy);
    requestPreview();
}

void QVExportDialog::stopPreviewPlayback()
{
    previewMovie->stop();
    previewMovie->setFileName(QString());
    preview->clear();
    if (previewPlayer) {
        previewPlayer->stop();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        previewPlayer->setSource(QUrl());
#else
        previewPlayer->setMedia(QMediaContent());
#endif
    }
    previewStack->setCurrentWidget(preview);
}

void QVExportDialog::requestPreview(bool whole)
{
    if (busy) return;
    ++previewGeneration;
    previewWholeRequested = whole;
    if (previewCancellation) previewCancellation->store(true);
    stopPreviewPlayback();
    preview->setText(tr("Preparing preview…"));
    updatePreviewDetails();
    previewStatus->setToolTip(QString());
    previewTimer->start();
}

void QVExportDialog::startPreview()
{
    if (busy || previewWatcher.isRunning()) return;
    auto options = selectedOptions();
    runningWholePreview = previewWholeRequested;
    if (!runningWholePreview) {
        options.wholeMedia = false;
        options.reverse = false;
        if (scope->currentIndex() == 1) options.format = "png";
    }
    if (runningWholePreview && (options.format == "mp4" || options.format == "webm")
        && (options.size.width() % 2 || options.size.height() % 2)) {
        preview->setText(tr("Choose even video dimensions to preview."));
        return;
    }
    previewDirectory = std::make_shared<QTemporaryDir>();
    if (!previewDirectory->isValid()) {
        preview->setText(tr("Could not create preview files."));
        return;
    }
    previewPath = previewDirectory->filePath("preview." + options.format);
    runningPreviewGeneration = previewGeneration;
    previewCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancel = previewCancellation;
    const auto directory = previewDirectory;
    const auto capturedSource = source;
    const auto output = previewPath;
    const auto ffmpeg = QVExport::findFFmpeg();
    const bool inspectDuration = scope->currentIndex() == 1 && source.animated && source.durationMs < 0;
    preview->setText(runningWholePreview ? tr("Encoding preview…") : tr("Rendering image…"));
    updatePreviewDetails();
    playPreview->setEnabled(false);
    previewWatcher.setFuture(QtConcurrent::run([capturedSource, options, output, ffmpeg, cancel, directory, inspectDuration] {
        auto result = QVExport::run(capturedSource, options, output, ffmpeg, cancel);
        if (result.error.isEmpty() && !result.cancelled && inspectDuration && !options.wholeMedia)
            result.durationMs = QVExport::mediaDuration(capturedSource, cancel);
        if (result.error.isEmpty() && !result.cancelled && options.wholeMedia
            && (options.format == "gif" || options.format == "webp")) {
            QVExport::Source encoded;
            encoded.path = output;
            encoded.animated = true;
            result.durationMs = QVExport::mediaDuration(encoded, cancel);
        }
        return result;
    }));
}

void QVExportDialog::applyPercentage()
{
    const QSize original = orientedSize();
    const QSignalBlocker blockWidth(width), blockHeight(height);
    width->setValue(qMax(1, qRound(original.width() * percentage->value() / 100.0)));
    height->setValue(qMax(1, qRound(original.height() * percentage->value() / 100.0)));
    requestPreview();
}

QString QVExportDialog::exportFileName() const
{
    return fileName->text().trimmed() + "." + format->currentData().toString();
}

void QVExportDialog::updateFileName()
{
    const bool edited = fileName->isModified();
    if (!edited) {
        const QString base = QFileInfo(source.path).completeBaseName();
        const QString suggested = (base.isEmpty() ? tr("Untitled") : base)
                + (scope->currentIndex() == 1 || (!source.video && !source.animated) ? "-export" : "-frame");
        if (fileName->text() != suggested) fileName->setText(suggested);
    }
    fileName->setModified(edited);
}

void QVExportDialog::updatePreviewDetails(qint64 bytes, qint64 durationMs)
{
    previewBytes = bytes;
    const auto options = selectedOptions();
    int divisor = options.size.width(), remainder = options.size.height();
    while (remainder != 0) { const int next = divisor % remainder; divisor = remainder; remainder = next; }
    divisor = qMax(1, divisor);
    QStringList lines;
    lines << tr("%1 x %2 px | %3:%4").arg(options.size.width()).arg(options.size.height())
                     .arg(options.size.width() / divisor).arg(options.size.height() / divisor);
    if (options.wholeMedia) {
        if (durationMs < 0 && source.durationMs > 0) durationMs = qRound64(source.durationMs / options.speed);
        lines << (durationMs >= 0 ? tr("Duration: %1 s").arg(durationMs / 1000.0, 0, 'f', 2)
                                 : tr("Duration: calculating..."));
    }
    if (bytes >= 0) {
        const bool megabytes = bytes >= 1024 * 1024;
        lines << tr("File size: %1 %2").arg(bytes / (megabytes ? 1048576.0 : 1024.0), 0, 'f', 1)
                       .arg(megabytes ? "MiB" : "KiB");
    } else {
        lines << (options.wholeMedia ? tr("File size: available after Play preview") : tr("File size: calculating..."));
    }
    previewStatus->setText(lines.join('\n'));
}
