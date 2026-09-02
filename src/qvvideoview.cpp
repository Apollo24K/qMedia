#include "qvvideoview.h"

#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QUrl>

QVVideoView::QVVideoView(QGraphicsScene *scene, QObject *parent)
    : QObject(parent)
    , player(this)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    , audioOutput(this)
#endif
    , videoItem(new QGraphicsVideoItem())
{
    scene->addItem(videoItem);
    videoItem->hide();
    videoItem->setAcceptedMouseButtons(Qt::NoButton);

    player.setVideoOutput(videoItem);
    connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, this,
            [this](const QSizeF &size) {
                if (!size.isEmpty())
                    videoItem->setSize(size);
                emit nativeSizeChanged(size);
            });
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

QVVideoView::~QVVideoView()
{
    player.setVideoOutput(static_cast<QGraphicsVideoItem *>(nullptr));
    if (videoItem->scene())
        videoItem->scene()->removeItem(videoItem);
    delete videoItem;
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
