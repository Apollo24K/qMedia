#include "qvvideoview.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsVideoItem>
#include <QPointer>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantAnimation>
#include <QtMath>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QAudioDevice>
#  include <QMediaMetaData>
#  include <QVideoFrame>
#  include <QVideoSink>
#endif

namespace {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool audioBackendReady = false;
bool audioBackendWarmupStarted = false;
QList<QPointer<QVVideoView>> audioBackendWaiters;
#endif

QString mediaStatusName(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::NoMedia:
        return QStringLiteral("NoMedia");
    case QMediaPlayer::LoadingMedia:
        return QStringLiteral("LoadingMedia");
    case QMediaPlayer::LoadedMedia:
        return QStringLiteral("LoadedMedia");
    case QMediaPlayer::StalledMedia:
        return QStringLiteral("StalledMedia");
    case QMediaPlayer::BufferingMedia:
        return QStringLiteral("BufferingMedia");
    case QMediaPlayer::BufferedMedia:
        return QStringLiteral("BufferedMedia");
    case QMediaPlayer::EndOfMedia:
        return QStringLiteral("EndOfMedia");
    case QMediaPlayer::InvalidMedia:
        return QStringLiteral("InvalidMedia");
    }
    return QStringLiteral("UnknownMediaStatus");
}
}

QVVideoView::QVVideoView(QGraphicsScene *scene, QObject *parent)
    : QObject(parent)
    , player(this)
    , videoItem(new QGraphicsVideoItem())
{
    profileFilePath = QString::fromLocal8Bit(qgetenv("QMEDIA_VIDEO_PROFILE"));
    scene->addItem(videoItem);
    videoItem->hide();
    videoItem->setAcceptedMouseButtons(Qt::NoButton);

    player.setVideoOutput(videoItem);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    endFrameItem = new QGraphicsPixmapItem(videoItem);
    endFrameItem->hide();
#endif
    connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, this,
            [this](const QSizeF &size) {
                profileEvent(QStringLiteral("native video size"),
                             QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
                if (!size.isEmpty())
                    videoItem->setSize(size);
                emit nativeSizeChanged(size);
            });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioBackendReady)
        initializeAudioOutput();
    else {
        audioBackendWaiters.append(this);
        startAudioBackendWarmup();
    }
    connect(&player, &QMediaPlayer::playbackStateChanged, this,
            &QVVideoView::playbackStateChanged);
    connect(&player, &QMediaPlayer::errorOccurred, this, [this]() {
        profileEvent(QStringLiteral("playback error"), player.errorString());
        emit errorOccurred();
    });
    connect(videoItem->videoSink(), &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame &frame) {
                if (!frame.isValid())
                    return;

                lastVideoFrame = frame;
                if (frame.startTime() >= 0)
                    displayedFrameStartMs = frame.startTime() / 1000;
                if (frame.endTime() > frame.startTime()) {
                    displayedFrameDurationMs = qMax<qint64>(
                            1, (frame.endTime() - frame.startTime()) / 1000);
                }
                if (profileFirstFramePending) {
                    profileFirstFramePending = false;
                    profileEvent(QStringLiteral("first valid video frame"));
                }
                endFrameItem->hide();
                if (pauseOnNextVideoFrame) {
                    pauseOnNextVideoFrame = false;
                    player.pause();
                    if (audioPlayer)
                        audioPlayer->pause();
                }
            });
    connect(&player, &QMediaPlayer::metaDataChanged, this, [this]() {
        const qreal frameRate = player.metaData().value(QMediaMetaData::VideoFrameRate).toReal();
        if (frameRate > 0.0)
            displayedFrameDurationMs = qMax<qint64>(1, qRound64(1000.0 / frameRate));
        profileEvent(QStringLiteral("metadata available"));
    });
#else
    player.setVolume(100);
    connect(&player, &QMediaPlayer::stateChanged, this, &QVVideoView::playbackStateChanged);
    connect(&player,
            QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
            this,
            [this]() {
                profileEvent(QStringLiteral("playback error"), player.errorString());
                emit errorOccurred();
            });
#endif
    connect(&player, &QMediaPlayer::mediaStatusChanged, this, &QVVideoView::mediaStatusChanged);
}

void QVVideoView::recordInitializationDuration(qint64 milliseconds)
{
    QString backend = QString::fromLocal8Bit(qgetenv("QT_MEDIA_BACKEND"));
    if (backend.isEmpty())
        backend = QStringLiteral("default");
    profileEvent(QStringLiteral("video player constructed"),
                 QStringLiteral("duration=%1ms backend=%2").arg(milliseconds).arg(backend));
}

QVVideoView::~QVVideoView()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioPlayer)
        audioPlayer->setAudioOutput(nullptr);
#endif
    player.setVideoOutput(static_cast<QGraphicsVideoItem *>(nullptr));
    if (videoItem->scene())
        videoItem->scene()->removeItem(videoItem);
    delete videoItem;
}

void QVVideoView::startAudioBackendWarmup()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioBackendReady || audioBackendWarmupStarted)
        return;

    audioBackendWarmupStarted = true;
    QThread *warmupThread = QThread::create([]() {
        // The first QAudioOutput initializes Qt's audio backend and enumerates
        // devices. On some Windows systems this takes several seconds, but the
        // cost is process-wide and subsequent outputs are effectively instant.
        QAudioOutput temporaryOutput;
    });
    connect(warmupThread, &QThread::finished, qApp, []() {
        audioBackendReady = true;
        for (const QPointer<QVVideoView> &videoView : qAsConst(audioBackendWaiters)) {
            if (videoView)
                videoView->initializeAudioOutput();
        }
        audioBackendWaiters.clear();
    });
    connect(warmupThread, &QThread::finished, warmupThread, &QObject::deleteLater);
    warmupThread->start();
#endif
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void QVVideoView::initializeAudioOutput()
{
    if (audioOutput)
        return;

    audioPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    audioPlayerVideoSink = new QVideoSink(this);
    audioOutput->setVolume(0.0);
    audioOutput->setMuted(muted);
    audioPlayer->setAudioOutput(audioOutput);
    audioPlayer->setPlaybackRate(playbackSpeedPercent / 100.0);
    // The Windows backend can report successful audio-only playback without
    // rendering an embedded audio track unless it builds the complete media
    // topology. A private sink satisfies that requirement while discarding the
    // companion player's video frames.
    audioPlayer->setVideoSink(audioPlayerVideoSink);
    audioFadeAnimation = new QVariantAnimation(this);
    audioFadeAnimation->setDuration(250);
    audioFadeAnimation->setStartValue(0.0);
    audioFadeAnimation->setEndValue(1.0);
    connect(audioFadeAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                audioOutput->setVolume(static_cast<float>(value.toDouble()));
            });
    connect(audioFadeAnimation, &QVariantAnimation::finished, this, [this]() {
        audioOutput->setVolume(1.0f);
        profileEvent(QStringLiteral("audio fade completed"),
                     QStringLiteral("volume=%1 muted=%2 device=%3")
                             .arg(audioOutput->volume())
                             .arg(audioOutput->isMuted())
                             .arg(audioOutput->device().description()));
    });
    connect(audioPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                profileEvent(QStringLiteral("audio-only media status"),
                             QStringLiteral("%1 has-audio=%2 seekable=%3")
                                     .arg(mediaStatusName(status))
                                     .arg(audioPlayer->hasAudio())
                                     .arg(audioPlayer->isSeekable()));
                trySynchronizeAudioPlayback();
            });
    connect(audioPlayer, &QMediaPlayer::seekableChanged, this,
            [this]() { trySynchronizeAudioPlayback(); });
    connect(audioPlayer, &QMediaPlayer::errorOccurred, this, [this]() {
        profileEvent(QStringLiteral("audio-only playback error"), audioPlayer->errorString());
    });
    connect(audioPlayer, &QMediaPlayer::hasAudioChanged, this, [this](bool available) {
        profileEvent(QStringLiteral("audio-only has-audio changed"),
                     QStringLiteral("available=%1").arg(available));
    });
    connect(audioPlayer, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                profileEvent(QStringLiteral("audio-only playback state"),
                             QStringLiteral("state=%1 position=%2ms")
                                     .arg(static_cast<int>(state))
                                     .arg(audioPlayer->position()));
            });
    profileEvent(QStringLiteral("audio output attached"));

    if (!currentFilePath.isEmpty())
        startAudioPlayback();
}

void QVVideoView::startAudioPlayback()
{
    if (!audioPlayer || currentFilePath.isEmpty())
        return;

    ++audioSyncGeneration;
    audioSynchronizationPending = true;
    audioSyncAttemptScheduled = false;
    audioFadeAnimation->stop();
    audioOutput->setVolume(0.0);
    audioPlayer->stop();
    audioPlayer->setSource(QUrl::fromLocalFile(currentFilePath));
    audioPlayer->play();
    profileEvent(QStringLiteral("audio-only source requested"),
                 QFileInfo(currentFilePath).fileName());
    trySynchronizeAudioPlayback();
}

void QVVideoView::trySynchronizeAudioPlayback()
{
    if (!audioSynchronizationPending || audioSyncAttemptScheduled || !audioPlayer || !videoLoaded)
        return;

    const QMediaPlayer::MediaStatus audioStatus = audioPlayer->mediaStatus();
    const bool audioSourceReady = audioStatus == QMediaPlayer::LoadedMedia
            || audioStatus == QMediaPlayer::BufferingMedia
            || audioStatus == QMediaPlayer::BufferedMedia;
    if (!audioSourceReady || !audioPlayer->isSeekable())
        return;

    const quint64 generation = audioSyncGeneration;
    const qint64 targetPosition = player.position();
    audioSyncAttemptScheduled = true;
    audioPlayer->pause();
    audioPlayer->setPosition(targetPosition);
    profileEvent(QStringLiteral("audio synchronization requested"),
                 QStringLiteral("target=%1ms").arg(targetPosition));

    // Keep the audio muted until the backend has had a chance to apply the
    // seek, then correct once more against the still-running video clock.
    QTimer::singleShot(75, this, [this, generation]() {
        if (generation != audioSyncGeneration)
            return;

        audioSyncAttemptScheduled = false;
        if (!audioSynchronizationPending || !audioPlayer)
            return;

        const qint64 correctedPosition = player.position();
        if (qAbs(audioPlayer->position() - correctedPosition) > 100) {
            audioPlayer->setPosition(correctedPosition);
            profileEvent(QStringLiteral("audio synchronization retry"),
                         QStringLiteral("video=%1ms audio=%2ms")
                                 .arg(correctedPosition)
                                 .arg(audioPlayer->position()));
            trySynchronizeAudioPlayback();
            return;
        }

        if (isPlaying())
            audioPlayer->play();
        else
            audioPlayer->pause();
        audioSynchronizationPending = false;
        fadeInAudio();
        profileEvent(QStringLiteral("audio synchronized"),
                     QStringLiteral("video=%1ms audio=%2ms")
                             .arg(player.position())
                             .arg(audioPlayer->position()));
    });
}

void QVVideoView::fadeInAudio()
{
    if (!audioFadeAnimation)
        return;
    audioFadeAnimation->stop();
    audioFadeAnimation->setStartValue(static_cast<double>(audioOutput->volume()));
    audioFadeAnimation->setEndValue(1.0);
    audioFadeAnimation->start();
}
#endif

void QVVideoView::loadFile(const QString &fileName)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    ++audioSyncGeneration;
    audioSynchronizationPending = false;
    audioSyncAttemptScheduled = false;
    if (audioFadeAnimation)
        audioFadeAnimation->stop();
    if (audioOutput)
        audioOutput->setVolume(0.0);
    if (audioPlayer)
        audioPlayer->stop();
#endif
    ++profileLoadId;
    profileLoadTimer.start();
    profileFirstFramePending = true;
    displayedFrameStartMs = 0;
    displayedFrameDurationMs = 40;
    pauseOnNextVideoFrame = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    lastVideoFrame = QVideoFrame();
    endFrameItem->hide();
#endif
    currentFilePath = fileName;
    profileEvent(QStringLiteral("load requested"), QFileInfo(fileName).fileName());
    videoLoaded = false;
    emit videoLoadedChanged();

    const QUrl source = QUrl::fromLocalFile(fileName);
    QElapsedTimer callTimer;
    callTimer.start();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player.setSource(source);
#else
    player.setMedia(source);
#endif
    profileEvent(QStringLiteral("source assigned"),
                 QStringLiteral("call-duration=%1ms").arg(callTimer.elapsed()));
    callTimer.restart();
    player.play();
    profileEvent(QStringLiteral("play requested"),
                 QStringLiteral("call-duration=%1ms").arg(callTimer.elapsed()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioPlayer)
        startAudioPlayback();
#endif
}

void QVVideoView::reloadFile()
{
    if (!currentFilePath.isEmpty())
        loadFile(currentFilePath);
}

void QVVideoView::closeVideo()
{
    profileFirstFramePending = false;
    pauseOnNextVideoFrame = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    lastVideoFrame = QVideoFrame();
    endFrameItem->hide();
    ++audioSyncGeneration;
    audioSynchronizationPending = false;
    audioSyncAttemptScheduled = false;
    if (audioFadeAnimation)
        audioFadeAnimation->stop();
    if (audioPlayer) {
        audioPlayer->stop();
        audioPlayer->setSource(QUrl());
    }
#endif
    player.stop();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player.setSource(QUrl());
#else
    player.setMedia(QMediaContent());
#endif
    currentFilePath.clear();
    if (videoLoaded) {
        videoLoaded = false;
        emit videoLoadedChanged();
    }
}

void QVVideoView::togglePaused()
{
    if (!videoLoaded)
        return;

    if (isPlaying())
        player.pause();
    else
        player.play();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioPlayer && !audioSynchronizationPending) {
        audioPlayer->setPosition(player.position());
        if (isPlaying())
            audioPlayer->play();
        else
            audioPlayer->pause();
    }
#endif
}

void QVVideoView::toggleMuted()
{
    muted = !muted;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioOutput)
        audioOutput->setMuted(muted);
#else
    player.setMuted(muted);
#endif
}

bool QVVideoView::isMuted() const
{
    return muted;
}

void QVVideoView::stepFrame(int direction)
{
    if (!videoLoaded || direction == 0)
        return;

    player.pause();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioPlayer)
        audioPlayer->pause();
#endif
    const qint64 frameDuration = qMax<qint64>(1, displayedFrameDurationMs);
    const qint64 targetPosition = direction > 0
            ? displayedFrameStartMs + frameDuration
            : qMax<qint64>(0, displayedFrameStartMs - frameDuration);
    setSynchronizedPosition(targetPosition);
}

void QVVideoView::seekToPercent(int percent)
{
    if (!videoLoaded || player.duration() <= 0)
        return;

    percent = qBound(0, percent, 100);
    setSynchronizedPosition(qRound64(player.duration() * (percent / 100.0)));
}

void QVVideoView::setPlaybackSpeed(int percent)
{
    playbackSpeedPercent = qBound(25, percent, 400);
    player.setPlaybackRate(playbackSpeedPercent / 100.0);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioPlayer) {
        audioPlayer->setPlaybackRate(playbackSpeedPercent / 100.0);
        if (!audioSynchronizationPending)
            setSynchronizedPosition(player.position());
    }
#endif
}

void QVVideoView::setSynchronizedPosition(qint64 position)
{
    position = qBound<qint64>(0, position, qMax<qint64>(0, player.duration()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const bool needsPlaybackToRender = videoLoaded
            && player.playbackState() == QMediaPlayer::StoppedState;
    if (needsPlaybackToRender)
        pauseOnNextVideoFrame = true;
#endif
    player.setPosition(position);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (needsPlaybackToRender)
        player.play();
    if (audioPlayer && !audioSynchronizationPending) {
        ++audioSyncGeneration;
        const quint64 generation = audioSyncGeneration;
        audioPlayer->setPosition(position);
        QTimer::singleShot(50, this, [this, generation]() {
            if (generation != audioSyncGeneration || !audioPlayer)
                return;
            if (qAbs(audioPlayer->position() - player.position()) > 75)
                audioPlayer->setPosition(player.position());
        });
    }
#endif
}

bool QVVideoView::isPlaying() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return player.playbackState() == QMediaPlayer::PlayingState;
#else
    return player.state() == QMediaPlayer::PlayingState;
#endif
}

void QVVideoView::mediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    profileEvent(QStringLiteral("media status"), mediaStatusName(status));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (status == QMediaPlayer::EndOfMedia && lastVideoFrame.isValid()) {
        const QImage finalImage = lastVideoFrame.toImage();
        if (!finalImage.isNull()) {
            endFrameItem->setPixmap(QPixmap::fromImage(finalImage));
            const QSizeF videoSize = videoItem->size();
            endFrameItem->setTransform(QTransform::fromScale(
                    videoSize.width() / finalImage.width(),
                    videoSize.height() / finalImage.height()));
            endFrameItem->show();
        }
    }
#endif
    const bool isNowLoaded = status == QMediaPlayer::LoadedMedia
            || status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::BufferingMedia
            || status == QMediaPlayer::StalledMedia || status == QMediaPlayer::EndOfMedia;
    if (videoLoaded != isNowLoaded) {
        videoLoaded = isNowLoaded;
        emit videoLoadedChanged();
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    trySynchronizeAudioPlayback();
#endif
}

void QVVideoView::profileEvent(const QString &event, const QString &details)
{
    if (profileFilePath.isEmpty())
        return;

    QFile profileFile(profileFilePath);
    if (!profileFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&profileFile);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " pid=" << QCoreApplication::applicationPid()
           << " player=" << reinterpret_cast<quintptr>(this)
           << " load=" << profileLoadId;
    if (profileLoadTimer.isValid())
        stream << " elapsed=" << profileLoadTimer.elapsed() << "ms";
    stream << " event=\"" << event << '"';
    if (!details.isEmpty())
        stream << " details=\"" << details << '"';
    stream << '\n';
}
