#ifndef QVVIDEOVIEW_H
#define QVVIDEOVIEW_H

#include <QMediaPlayer>
#include <QVideoWidget>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QAudioOutput>
#endif

class QVVideoView : public QVideoWidget
{
    Q_OBJECT

public:
    explicit QVVideoView(QWidget *parent = nullptr);

    void loadFile(const QString &fileName);
    void reloadFile();
    void closeVideo();
    void togglePaused();

    bool isLoaded() const { return videoLoaded; }
    bool isPlaying() const;
    QString errorString() const { return player.errorString(); }

signals:
    void playbackStateChanged();
    void videoLoadedChanged();
    void errorOccurred();

    void previousFileRequested();
    void nextFileRequested();
    void fullscreenRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);

    QMediaPlayer player;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioOutput audioOutput;
#endif
    QString currentFilePath;
    bool videoLoaded = false;
};

#endif // QVVIDEOVIEW_H
