#ifndef QVVIDEOVIEW_H
#define QVVIDEOVIEW_H

#include <QMediaPlayer>
#include <QElapsedTimer>
#include <QObject>
#include <QSizeF>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QAudioOutput>
#else
#  include <QMediaContent>
#endif

class QGraphicsScene;
class QGraphicsVideoItem;
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

    bool isLoaded() const { return videoLoaded; }
    bool isPlaying() const;
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
#endif

    QMediaPlayer player;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaPlayer *audioPlayer = nullptr;
    QAudioOutput *audioOutput = nullptr;
    QVideoSink *audioPlayerVideoSink = nullptr;
    QVariantAnimation *audioFadeAnimation = nullptr;
#endif
    QGraphicsVideoItem *videoItem;
    QString currentFilePath;
    QString profileFilePath;
    QElapsedTimer profileLoadTimer;
    quint64 profileLoadId = 0;
    bool profileFirstFramePending = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    quint64 audioSyncGeneration = 0;
    bool audioSynchronizationPending = false;
#endif
    bool videoLoaded = false;
};

#endif // QVVIDEOVIEW_H
