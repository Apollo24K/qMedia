#include "qvvideoview.h"

#include <QMouseEvent>
#include <QUrl>

QVVideoView::QVVideoView(QWidget *parent)
    : QVideoWidget(parent)
    , player(this)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    , audioOutput(this)
#endif
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background-color: black;");

    player.setVideoOutput(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player.setAudioOutput(&audioOutput);
    audioOutput.setVolume(1.0);
    connect(&player, &QMediaPlayer::playbackStateChanged, this,
            &QVVideoView::playbackStateChanged);
    connect(&player, &QMediaPlayer::errorOccurred, this, [this]() { emit errorOccurred(); });
#else
    player.setVolume(100);
    connect(&player, &QMediaPlayer::stateChanged, this, &QVVideoView::playbackStateChanged);
    connect(&player,
            QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
            this,
            [this]() { emit errorOccurred(); });
#endif
    connect(&player, &QMediaPlayer::mediaStatusChanged, this, &QVVideoView::mediaStatusChanged);
}

void QVVideoView::loadFile(const QString &fileName)
{
    currentFilePath = fileName;
    videoLoaded = false;
    emit videoLoadedChanged();

    const QUrl source = QUrl::fromLocalFile(fileName);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player.setSource(source);
#else
    player.setMedia(source);
#endif
    player.play();
}

void QVVideoView::reloadFile()
{
    if (!currentFilePath.isEmpty())
        loadFile(currentFilePath);
}

void QVVideoView::closeVideo()
{
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
}

bool QVVideoView::isPlaying() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return player.playbackState() == QMediaPlayer::PlayingState;
#else
    return player.state() == QMediaPlayer::PlayingState;
#endif
}

void QVVideoView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::BackButton)
        emit previousFileRequested();
    else if (event->button() == Qt::ForwardButton)
        emit nextFileRequested();
    else
        QVideoWidget::mousePressEvent(event);
}

void QVVideoView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit fullscreenRequested();
    else
        QVideoWidget::mouseDoubleClickEvent(event);
}

void QVVideoView::mediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    const bool isNowLoaded = status == QMediaPlayer::LoadedMedia
            || status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::BufferingMedia
            || status == QMediaPlayer::StalledMedia || status == QMediaPlayer::EndOfMedia;
    if (videoLoaded != isNowLoaded) {
        videoLoaded = isNowLoaded;
        emit videoLoadedChanged();
    }
}
