#ifndef QVPLAYBACKLOOPMODE_H
#define QVPLAYBACKLOOPMODE_H

enum class QVPlaybackLoopMode { Default, ForceLoop, ForceStop };

inline QVPlaybackLoopMode nextManualLoopMode(QVPlaybackLoopMode currentMode,
                                             bool mediaLoopsByDefault)
{
    if (currentMode == QVPlaybackLoopMode::Default) {
        return mediaLoopsByDefault ? QVPlaybackLoopMode::ForceStop
                                   : QVPlaybackLoopMode::ForceLoop;
    }

    return currentMode == QVPlaybackLoopMode::ForceLoop ? QVPlaybackLoopMode::ForceStop
                                                        : QVPlaybackLoopMode::ForceLoop;
}

#endif // QVPLAYBACKLOOPMODE_H
