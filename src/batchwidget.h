#ifndef QTFFMPEGSTUDIO_BATCHWIDGET_H
#define QTFFMPEGSTUDIO_BATCHWIDGET_H

#include <QWidget>
#include <QHash>
#include <QElapsedTimer>
#include <functional>

#include "batchtaskmodel.h"
#include "muxperformance.h"

class BackendProcess;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableView;

class BatchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BatchWidget(QWidget* parent, std::function<TranscodeOptions()> optionsSource);
    BatchTaskModel* model() const;
    int enqueueFiles(const QStringList& paths, const QString& outputDirectory);
    bool active() const;
    int maxConcurrent() const;
    void setMaxConcurrent(int count);
    void setExternalAvailable(bool available);

public slots:
    void startQueue();
    void cancelQueue();

signals:
    void activeChanged(bool active);

private:
    void schedulePump();
    void pumpQueue();
    void startTask(int row);
    void appendTaskLog(quint64 id, const QString& text);
    void appendLogLine(quint64 id, const QString& line);
    void showLogForTask(quint64 id);
    void showTaskDetails(quint64 id);
    void clearVisibleLog();
    void finishQueue();
    void refreshControls();

    std::function<TranscodeOptions()> optionsSource_;
    BatchTaskModel* model_ = nullptr;
    QHash<quint64, BackendProcess*> running_;
    QHash<quint64, QElapsedTimer> runningTimers_;
    QHash<quint64, MuxPerformance> taskPerformance_;
    QHash<quint64, QString> pendingLogLines_;
    struct LogEntry { quint64 taskId; QString line; };
    QVector<LogEntry> logEntries_;
    quint64 selectedLogTaskId_ = 0;
    QTableView* table_ = nullptr;
    QComboBox* logSelector_ = nullptr;
    QComboBox* concurrencyCombo_ = nullptr;
    QLineEdit* outputDirectoryEdit_ = nullptr;
    QPushButton* chooseDirectoryButton_ = nullptr;
    QPushButton* addFilesButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* taskDetails_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    bool externalAvailable_ = true;
    bool active_ = false;
    bool stopRequested_ = false;
    bool pumpScheduled_ = false;
};

#endif
