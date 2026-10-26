#include "batchwidget.h"
#include "batchprogressdelegate.h"
#include "backendprocess.h"

#include <QDir>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QItemSelectionModel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

BatchWidget::BatchWidget(QWidget* parent, std::function<TranscodeOptions()> optionsSource)
    : QWidget(parent), optionsSource_(std::move(optionsSource))
{
    model_ = new BatchTaskModel(this);
    auto* layout = new QVBoxLayout(this);
    auto* directoryRow = new QHBoxLayout;
    outputDirectoryEdit_ = new QLineEdit(this);
    outputDirectoryEdit_->setObjectName(QStringLiteral("batchOutputDirectoryEdit"));
    outputDirectoryEdit_->setReadOnly(true);
    outputDirectoryEdit_->setPlaceholderText(QStringLiteral("选择批量输出目录；留空时使用第一个输入文件的目录"));
    chooseDirectoryButton_ = new QPushButton(QStringLiteral("选择输出目录…"), this);
    chooseDirectoryButton_->setObjectName(QStringLiteral("batchChooseDirectoryButton"));
    directoryRow->addWidget(outputDirectoryEdit_);
    directoryRow->addWidget(chooseDirectoryButton_);
    layout->addLayout(directoryRow);

    auto* buttons = new QHBoxLayout;
    addFilesButton_ = new QPushButton(QStringLiteral("添加 MP4 文件…"), this);
    addFilesButton_->setObjectName(QStringLiteral("batchAddFilesButton"));
    startButton_ = new QPushButton(QStringLiteral("开始队列"), this);
    startButton_->setObjectName(QStringLiteral("batchStartButton"));
    cancelButton_ = new QPushButton(QStringLiteral("停止队列"), this);
    cancelButton_->setObjectName(QStringLiteral("batchCancelButton"));
    clearButton_ = new QPushButton(QStringLiteral("清空任务"), this);
    clearButton_->setObjectName(QStringLiteral("batchClearButton"));
    buttons->addWidget(addFilesButton_);
    buttons->addWidget(startButton_);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(clearButton_);
    layout->addLayout(buttons);
    auto* concurrencyRow = new QHBoxLayout;
    concurrencyRow->addWidget(new QLabel(QStringLiteral("同时转码任务数："), this));
    concurrencyCombo_ = new QComboBox(this);
    concurrencyCombo_->setObjectName(QStringLiteral("batchConcurrencyCombo"));
    for (int count = 1; count <= 3; ++count) {
        concurrencyCombo_->addItem(QString::number(count), count);
    }
    concurrencyRow->addWidget(concurrencyCombo_);
    concurrencyRow->addStretch();
    layout->addLayout(concurrencyRow);

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("batchTable"));
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    auto* progressDelegate = new BatchProgressDelegate(table_);
    progressDelegate->setObjectName(QStringLiteral("batchProgressDelegate"));
    table_->setItemDelegateForColumn(3, progressDelegate);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->horizontalHeader()->resizeSection(3, 150);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->setMinimumHeight(180);
    layout->addWidget(table_);

    stateLabel_ = new QLabel(QStringLiteral("尚未添加批量任务。"), this);
    stateLabel_->setObjectName(QStringLiteral("batchStateLabel"));
    layout->addWidget(stateLabel_);
    auto* logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel(QStringLiteral("查看日志："), this));
    logSelector_ = new QComboBox(this);
    logSelector_->setObjectName(QStringLiteral("batchLogSelector"));
    logSelector_->setMinimumWidth(220);
    logSelector_->addItem(QStringLiteral("全部日志"), QVariant::fromValue<qulonglong>(0));
    logHeader->addWidget(logSelector_);
    logHeader->addStretch();
    auto* copyLogButton = new QPushButton(QStringLiteral("复制日志"), this);
    copyLogButton->setObjectName(QStringLiteral("batchCopyLogButton"));
    copyLogButton->setEnabled(false);
    auto* clearLogButton = new QPushButton(QStringLiteral("清空日志"), this);
    clearLogButton->setObjectName(QStringLiteral("batchClearLogButton"));
    clearLogButton->setEnabled(false);
    logHeader->addWidget(copyLogButton);
    logHeader->addWidget(clearLogButton);
    layout->addLayout(logHeader);
    log_ = new QPlainTextEdit(this);
    log_->setObjectName(QStringLiteral("batchLog"));
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setPlaceholderText(QStringLiteral("各任务的后端日志会按任务编号显示在这里"));
    layout->addWidget(log_);

    connect(log_, &QPlainTextEdit::textChanged, this,
            [this, copyLogButton, clearLogButton]() {
        const bool hasLog = !log_->toPlainText().isEmpty();
        copyLogButton->setEnabled(hasLog);
        clearLogButton->setEnabled(hasLog);
    });
    connect(copyLogButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(log_->toPlainText());
    });
    connect(clearLogButton, &QPushButton::clicked, this, &BatchWidget::clearVisibleLog);

    connect(logSelector_, &QComboBox::currentIndexChanged, this, [this]() {
        showLogForTask(logSelector_->currentData().toULongLong());
    });
    auto selectRowLog = [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        const quint64 id = model_->task(index.row()).id;
        const int selectorIndex = logSelector_->findData(QVariant::fromValue<qulonglong>(id));
        if (selectorIndex >= 0) logSelector_->setCurrentIndex(selectorIndex);
    };
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [selectRowLog](const QModelIndex& current) { selectRowLog(current); });
    connect(table_, &QTableView::clicked, this, selectRowLog);

    connect(chooseDirectoryButton_, &QPushButton::clicked, this, [this]() {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择批量输出目录"), outputDirectoryEdit_->text());
        if (!chosen.isEmpty()) outputDirectoryEdit_->setText(QDir::toNativeSeparators(chosen));
    });
    connect(addFilesButton_, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("添加 MP4 文件"), {}, QStringLiteral("MP4 视频 (*.mp4 *.MP4)"));
        if (files.isEmpty()) return;
        QString directory = outputDirectoryEdit_->text();
        if (directory.isEmpty()) directory = QFileInfo(files.first()).absolutePath();
        enqueueFiles(files, directory);
    });
    connect(startButton_, &QPushButton::clicked, this, &BatchWidget::startQueue);
    connect(cancelButton_, &QPushButton::clicked, this, &BatchWidget::cancelQueue);
    connect(clearButton_, &QPushButton::clicked, this, [this]() {
        if (active_) return;
        model_->clear();
        runningTimers_.clear();
        pendingLogLines_.clear();
        logEntries_.clear();
        selectedLogTaskId_ = 0;
        logSelector_->clear();
        logSelector_->addItem(QStringLiteral("全部日志"), QVariant::fromValue<qulonglong>(0));
        log_->clear();
        stateLabel_->setText(QStringLiteral("任务已清空。"));
        refreshControls();
    });
    refreshControls();
}

BatchTaskModel* BatchWidget::model() const { return model_; }
bool BatchWidget::active() const { return active_; }
int BatchWidget::maxConcurrent() const { return concurrencyCombo_->currentData().toInt(); }

void BatchWidget::setMaxConcurrent(int count)
{
    if (active_ || count < 1 || count > 3) return;
    concurrencyCombo_->setCurrentIndex(concurrencyCombo_->findData(count));
}

void BatchWidget::setExternalAvailable(bool available)
{
    externalAvailable_ = available;
    refreshControls();
}

int BatchWidget::enqueueFiles(const QStringList& paths, const QString& outputDirectory)
{
    if (active_ || !externalAvailable_ || paths.isEmpty()) return 0;
    const int count = model_->addFiles(paths, outputDirectory, optionsSource_());
    for (int row = model_->rowCount() - count; row < model_->rowCount(); ++row) {
        const BatchTask task = model_->task(row);
        logSelector_->addItem(QStringLiteral("#%1  %2").arg(task.id)
                                  .arg(QFileInfo(task.options.inputPath).fileName()),
                              QVariant::fromValue<qulonglong>(task.id));
    }
    if (count > 0) outputDirectoryEdit_->setText(QDir::toNativeSeparators(outputDirectory));
    stateLabel_->setText(QStringLiteral("已添加 %1 项；无效或重复文件 %2 项。")
                         .arg(count).arg(paths.size() - count));
    refreshControls();
    return count;
}

void BatchWidget::startQueue()
{
    if (active_ || !externalAvailable_ || model_->nextPendingIndex() < 0) return;
    active_ = true;
    stopRequested_ = false;
    logSelector_->setCurrentIndex(0);
    stateLabel_->setText(QStringLiteral("批量队列运行中；最多同时处理 %1 项。")
                         .arg(maxConcurrent()));
    refreshControls();
    emit activeChanged(true);
    schedulePump();
}

void BatchWidget::cancelQueue()
{
    if (!active_ || stopRequested_) return;
    stopRequested_ = true;
    model_->cancelPending();
    stateLabel_->setText(QStringLiteral("正在停止队列；等待所有运行中的任务退出。"));
    refreshControls();
    const auto processes = running_.values();
    for (BackendProcess* process : processes) process->cancel();
    if (running_.isEmpty()) schedulePump();
}

void BatchWidget::schedulePump()
{
    if (pumpScheduled_) return;
    pumpScheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        pumpScheduled_ = false;
        pumpQueue();
    });
}

void BatchWidget::pumpQueue()
{
    if (!active_) return;
    if (stopRequested_) {
        if (running_.isEmpty()) finishQueue();
        return;
    }
    while (running_.size() < maxConcurrent()) {
        const int row = model_->nextPendingIndex();
        if (row < 0) break;
        startTask(row);
    }
    if (running_.isEmpty() && model_->nextPendingIndex() < 0) finishQueue();
}

void BatchWidget::startTask(int row)
{
    const BatchTask task = model_->task(row);
    const QFileInfo input(task.options.inputPath);
    const QFileInfo output(task.options.outputPath);
    QString error;
    if (!input.isFile() || !input.isReadable()) error = QStringLiteral("输入文件已失效");
    else if (!output.dir().exists()) error = QStringLiteral("输出目录已失效");
    else if (output.exists()) error = QStringLiteral("输出文件已存在；为避免覆盖，跳过该项");
    if (!error.isEmpty()) {
        model_->setStatus(row, BatchTask::Status::Failed, error);
        appendLogLine(task.id, QStringLiteral("[#%1] 失败：%2").arg(task.id).arg(error));
        return;
    }
    auto* backend = new BackendProcess(this);
    running_.insert(task.id, backend);
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    runningTimers_.insert(task.id, elapsedTimer);
    model_->setStatus(row, BatchTask::Status::Running, QStringLiteral("正在转码"));
    appendLogLine(task.id, QStringLiteral("=== 任务 #%1：%2 ===")
                     .arg(task.id).arg(input.fileName()));
    connect(backend, &BackendProcess::logReceived, this,
            [this, id = task.id, backend](const QString& text) {
        if (running_.value(id) == backend) appendTaskLog(id, text);
    });
    connect(backend, &BackendProcess::transcodeProgressChanged, this,
            [this, id = task.id, backend](int percent) {
        if (running_.value(id) != backend) return;
        const int taskRow = model_->rowForId(id);
        if (taskRow >= 0) model_->setTranscodeProgress(taskRow, percent);
    });
    connect(backend, &BackendProcess::finished, this,
            [this, id = task.id, backend](bool success, const QString& message) {
        if (running_.value(id) != backend) return;
        const QString tail = pendingLogLines_.take(id);
        if (!tail.isEmpty()) appendTaskLog(id, tail + QLatin1Char('\n'));
        running_.remove(id);
        const QElapsedTimer timer = runningTimers_.take(id);
        const qint64 elapsedMilliseconds = timer.isValid() ? timer.elapsed() : -1;
        backend->deleteLater();
        const int taskRow = model_->rowForId(id);
        if (taskRow >= 0) {
            const auto status = success ? BatchTask::Status::Succeeded
                : stopRequested_ ? BatchTask::Status::Cancelled : BatchTask::Status::Failed;
            model_->setStatus(taskRow, status, message, elapsedMilliseconds);
            appendLogLine(id, QStringLiteral("[#%1] %2：%3").arg(id)
                              .arg(success ? QStringLiteral("成功")
                                   : stopRequested_ ? QStringLiteral("已取消")
                                                    : QStringLiteral("失败"), message));
        }
        schedulePump();
    });
    backend->start(task.options, false);
}

void BatchWidget::appendTaskLog(quint64 id, const QString& text)
{
    QString& pending = pendingLogLines_[id];
    pending += text;
    qsizetype newline = -1;
    while ((newline = pending.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = pending.left(newline);
        pending.remove(0, newline + 1);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (!line.isEmpty()) {
            appendLogLine(id, QStringLiteral("[#%1] %2").arg(id).arg(line));
        }
    }
    if (pending.size() > 16384) {
        appendLogLine(id, QStringLiteral("[#%1] 日志行过长，已截断。").arg(id));
        pending.clear();
    }
}

void BatchWidget::appendLogLine(quint64 id, const QString& line)
{
    constexpr qsizetype maximumEntries = 2000;
    logEntries_.append({id, line});
    if (logEntries_.size() > maximumEntries) logEntries_.remove(0, logEntries_.size() - maximumEntries);
    if (selectedLogTaskId_ == 0 || selectedLogTaskId_ == id) log_->appendPlainText(line);
}

void BatchWidget::showLogForTask(quint64 id)
{
    selectedLogTaskId_ = id;
    QStringList lines;
    for (const LogEntry& entry : logEntries_) {
        if (id == 0 || entry.taskId == id) lines.append(entry.line);
    }
    log_->setPlainText(lines.join(QLatin1Char('\n')));
    QTextCursor cursor = log_->textCursor();
    cursor.movePosition(QTextCursor::End);
    log_->setTextCursor(cursor);
}

void BatchWidget::clearVisibleLog()
{
    if (selectedLogTaskId_ == 0) {
        logEntries_.clear();
    } else {
        logEntries_.erase(std::remove_if(logEntries_.begin(), logEntries_.end(),
                                         [this](const LogEntry& entry) {
            return entry.taskId == selectedLogTaskId_;
        }), logEntries_.end());
    }
    showLogForTask(selectedLogTaskId_);
}

void BatchWidget::finishQueue()
{
    if (!active_) return;
    active_ = false;
    int success = 0;
    int failed = 0;
    int cancelled = 0;
    for (int row = 0; row < model_->rowCount(); ++row) {
        switch (model_->task(row).status) {
        case BatchTask::Status::Succeeded: ++success; break;
        case BatchTask::Status::Failed: ++failed; break;
        case BatchTask::Status::Cancelled: ++cancelled; break;
        default: break;
        }
    }
    stateLabel_->setText(QStringLiteral("队列结束：成功 %1，失败 %2，取消 %3。")
                         .arg(success).arg(failed).arg(cancelled));
    refreshControls();
    emit activeChanged(false);
}

void BatchWidget::refreshControls()
{
    const bool editable = externalAvailable_ && !active_;
    chooseDirectoryButton_->setEnabled(editable);
    addFilesButton_->setEnabled(editable);
    clearButton_->setEnabled(editable && model_->rowCount() > 0);
    startButton_->setEnabled(editable && model_->nextPendingIndex() >= 0);
    cancelButton_->setEnabled(active_ && !stopRequested_);
    concurrencyCombo_->setEnabled(editable);
}
