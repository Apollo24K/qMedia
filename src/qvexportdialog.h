#ifndef QVEXPORTDIALOG_H
#define QVEXPORTDIALOG_H
#include "qvexport.h"
#include <QDialog>
#include <QFutureWatcher>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class QTemporaryDir;
class QMovie;
class QMediaPlayer;
class QVideoWidget;
class QStackedWidget;

class QVExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QVExportDialog(const QVExport::Source &source, QWidget *parent = nullptr);
    ~QVExportDialog() override;
protected:
    void reject() override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void updateFormats();
    void updateControls();
    void startExport();
    QVExport::Options selectedOptions() const;
    QSize orientedSize() const;
    void rotationChanged();
    void requestPreview(bool whole = false);
    void startPreview();
    void stopPreviewPlayback();
    void applyPercentage();
    void updatePreviewDetails(qint64 bytes = -1, qint64 durationMs = -1);
    QString exportFileName() const;
    void updateFileName();
    QVExport::Source source;
    QLineEdit *fileName;
    QComboBox *sizeMode;
    QDoubleSpinBox *percentage;
    QDoubleSpinBox *speed;
    QLabel *speedLabel;
    qint64 previewBytes = -1;
    QComboBox *scope;
    QComboBox *format;
    QSpinBox *width;
    QSpinBox *height;
    QSpinBox *quality;
    QCheckBox *aspect;
    QCheckBox *audio;
    QCheckBox *loop;
    QCheckBox *rotate;
    QComboBox *rotation;
    QCheckBox *mirror;
    QCheckBox *flip;
    QCheckBox *reverse;
    QCheckBox *applyFilters;
    QLabel *preview;
    QLabel *previewStatus;
    QPushButton *playPreview;
    QStackedWidget *previewStack;
    QTimer *previewTimer;
    QMovie *previewMovie;
    QMediaPlayer *previewPlayer = nullptr;
    QVideoWidget *previewVideo = nullptr;
    QFutureWatcher<QVExport::Result> previewWatcher;
    std::shared_ptr<std::atomic_bool> previewCancellation;
    std::shared_ptr<QTemporaryDir> previewDirectory;
    QString previewPath;
    int previewGeneration = 0;
    int runningPreviewGeneration = 0;
    bool previewWholeRequested = false;
    bool runningWholePreview = false;
    int lastRotation = 0;
    QLabel *status;
    QPushButton *exportButton;
    QPushButton *ffmpegButton;
    QPushButton *cancelButton;
    QProgressBar *progress;
    QFutureWatcher<QVExport::Result> watcher;
    std::shared_ptr<std::atomic_bool> cancellation;
    bool busy = false;
};
#endif
