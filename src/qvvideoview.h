#ifndef QVVIDEOVIEW_H
#define QVVIDEOVIEW_H

#include <QMediaPlayer>
#include <QObject>
#include <QSizeF>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QAudioOutput>
#else
#  include <QMediaContent>
#endif

class QGraphicsScene;
class QGraphicsVideoItem;

class QVVideoView : public QObject
{
    Q_OBJECT

public:
    explicit QVVideoView(QGraphicsScene *scene, QObject *parent = nullptr);
    ~QVVideoView() override;

    void loadFile(const QString &fileName);
    void reloadFile();
    void closeVideo();
    void togglePaused();

    bool isLoaded() const { return videoLoaded; }
    bool isPlaying() const;
    QString errorString() const { return player.errorString(); }
    QGraphicsVideoItem *graphicsItem() const { return videoItem; }

signals:
    void playbackStateChanged();
    void videoLoadedChanged();
    void errorOccurred();
    void nativeSizeChanged(const QSizeF &size);

private:
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);

    QMediaPlayer player;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioOutput audioOutput;
#endif
    QGraphicsVideoItem *videoItem;
    QString currentFilePath;
    bool videoLoaded = false;
};

#endif // QVVIDEOVIEW_H
