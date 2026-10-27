#ifndef QTFFMPEGSTUDIO_MAINWINDOW_H
#define QTFFMPEGSTUDIO_MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>
#include <QString>

#include "transcodeoptions.h"
#include "muxperformance.h"

// 头文件里只保存指针，因此先声明类，具体头文件放在 cpp 中。
class QLabel;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QGroupBox;
class QTimer;
class QCloseEvent;
class QShowEvent;
class BackendProcess;
class BatchWidget;
class MediaProbe;
class QSettings;
class QProcess;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, const QString& settingsFile = {});
    ~MainWindow() override;
    TranscodeOptions readOptions() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void selectInputFile();
    void selectOutputFile();
    void checkPaths();
    void showOptionsPreview();
    void startSimulation();
    void stopSimulation();
    void advanceSimulation();
    void startRealTranscode();
    void cancelRealTranscode();
    void openOutputFolder();
    void checkNvencAvailability();

private:
    bool validatePaths();
    enum class TaskState { Idle, Simulating, Transcoding, Cancelling,
                           BatchRunning, BatchCancelling };
    void setTaskState(TaskState state);
    void refreshRealStartAvailability();
    void loadSettings();
    void saveSettings();

    QLineEdit* inputPathEdit_ = nullptr;
    QWidget* singleFileControls_ = nullptr;
    QWidget* singleFileTools_ = nullptr;
    QLineEdit* outputPathEdit_ = nullptr;
    QLabel* mediaState_ = nullptr;
    QLabel* mediaDetails_ = nullptr;
    QPushButton* mediaRetryButton_ = nullptr;
    MediaProbe* mediaProbe_ = nullptr;
    QString analyzedInputPath_;
    bool analyzedInputSupported_ = false;
    double analyzedDurationSeconds_ = -1.0;
    QPushButton* inputButton_ = nullptr;
    QPushButton* outputButton_ = nullptr;
    QPushButton* checkButton_ = nullptr;
    QLabel* feedbackLabel_ = nullptr;
    QGroupBox* optionsGroup_ = nullptr;
    QComboBox* rotationCombo_ = nullptr;
    QDoubleSpinBox* speedSpin_ = nullptr;
    QComboBox* videoCodecCombo_ = nullptr;
    QComboBox* videoBackendCombo_ = nullptr;
    QLabel* nvencStatusLabel_ = nullptr;
    QPushButton* nvencRefreshButton_ = nullptr;
    QProcess* nvencProbeProcess_ = nullptr;
    bool nvencProbeStarted_ = false;
    QComboBox* audioCodecCombo_ = nullptr;
    QComboBox* qualityCombo_ = nullptr;
    QSpinBox* bitrateSpin_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QComboBox* frameRateCombo_ = nullptr;
    QPlainTextEdit* optionsPreview_ = nullptr;
    QPushButton* startSimulationButton_ = nullptr;
    QPushButton* stopSimulationButton_ = nullptr;
    QProgressBar* simulationProgress_ = nullptr;
    QLabel* simulationStateLabel_ = nullptr;
    QTimer* simulationTimer_ = nullptr;
    BackendProcess* backend_ = nullptr;
    BatchWidget* batchWidget_ = nullptr;
    QPushButton* startRealButton_ = nullptr;
    QPushButton* cancelRealButton_ = nullptr;
    QPushButton* openOutputFolderButton_ = nullptr;
    QPlainTextEdit* realLog_ = nullptr;
    QProgressBar* realTranscodeProgress_ = nullptr;
    QLabel* realStateLabel_ = nullptr;
    QLabel* realElapsedLabel_ = nullptr;
    QLabel* realResultSummary_ = nullptr;
    QElapsedTimer realElapsedTimer_;
    TranscodeOptions activeRealOptions_;
    double activeInputDurationSeconds_ = -1.0;
    MuxPerformance realMuxPerformance_;
    bool hasRealMuxPerformance_ = false;
    bool closeAfterCancel_ = false;
    TaskState taskState_ = TaskState::Idle;
    QSettings* settings_ = nullptr;
    QString lastSuccessfulOutput_;
};

#endif
