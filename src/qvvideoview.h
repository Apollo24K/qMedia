#ifndef QVVIDEOVIEW_H
#define QVVIDEOVIEW_H

#include <QMediaPlayer>
#include <QElapsedTimer>
#include <QObject>
#include <QSizeF>

#include "qvplaybackloopmode.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QAudioOutput>
#  include <QVideoFrame>
#else
#  include <QMediaContent>
#endif

class QGraphicsScene;
class QGraphicsVideoItem;
class QGraphicsPixmapItem;
class QVariantAnimation;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QVideoSink;
#endif

class QVVideoView : public QObject
{
    Q_OBJECT

public:
    explicit QVVideoView(QGraphicsScene *scene, QObject *parent = nullptr);
    ~QVVideoView() override;

    static void startAudioBackendWarmup();

    void loadFile(const QString &fileName);
    void reloadFile();
    void closeVideo();
    void togglePaused();
    void toggleMuted();
    void stepFrame(int direction);
    void seekToPercent(int percent);
    void setPlaybackSpeed(int percent);
    void setLoopMode(QVPlaybackLoopMode mode);

    bool isLoaded() const { return videoLoaded; }
    bool isPlaying() const;
    bool isMuted() const;
    int playbackSpeed() const { return playbackSpeedPercent; }
    QString errorString() const { return player.errorString(); }
    QGraphicsVideoItem *graphicsItem() const { return videoItem; }
    void recordInitializationDuration(qint64 milliseconds);

signals:
    void playbackStateChanged();
    void videoLoadedChanged();
    void errorOccurred();
    void nativeSizeChanged(const QSizeF &size);

private:
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);
    void profileEvent(const QString &event, const QString &details = QString());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void initializeAudioOutput();
    void startAudioPlayback();
    void trySynchronizeAudioPlayback();
    void fadeInAudio();
    void showHeldVideoFrame(const QVideoFrame &frame);
#endif
    void setSynchronizedPosition(qint64 position);
    void restartPlayback();

    QMediaPlayer player;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaPlayer *audioPlayer = nullptr;
    QAudioOutput *audioOutput = nullptr;
    QVideoSink *audioPlayerVideoSink = nullptr;
    QVariantAnimation *audioFadeAnimation = nullptr;
    QGraphicsPixmapItem *endFrameItem = nullptr;
    QVideoFrame firstVideoFrame;
    QVideoFrame lastVideoFrame;
#endif
    QGraphicsVideoItem *videoItem;
    QString currentFilePath;
    QString profileFilePath;
    QElapsedTimer profileLoadTimer;
    quint64 profileLoadId = 0;
    bool profileFirstFramePending = false;
    qint64 displayedFrameStartMs = 0;
    qint64 displayedFrameDurationMs = 40;
    int playbackSpeedPercent = 100;
    bool muted = false;
    bool pauseOnNextVideoFrame = false;
    QVPlaybackLoopMode loopMode = QVPlaybackLoopMode::Default;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    quint64 audioSyncGeneration = 0;
    bool audioSynchronizationPending = false;
    bool audioSyncAttemptScheduled = false;
    bool loopRestartFramePending = false;
#endif
    bool videoLoaded = false;
};

#endif // QVVIDEOVIEW_H
