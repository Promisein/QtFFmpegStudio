#include "batchwidget.h"
#include "batchcarddelegate.h"
#include "taskthumbnailcache.h"
#include "studiotheme.h"
#include <QButtonGroup>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include "batchprogressdelegate.h"
#include "backendprocess.h"
#include "elapsedformat.h"

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
    layout->setContentsMargins(28,24,28,20); layout->setSpacing(16);
    auto* header=new QHBoxLayout; auto* titles=new QVBoxLayout;
    auto* title=new QLabel(QStringLiteral("任务中心")); title->setObjectName("pageTitle"); titles->addWidget(title);
    auto* subtitle=new QLabel(QStringLiteral("管理和查看视频转码任务")); subtitle->setProperty("muted",true); titles->addWidget(subtitle);
    header->addLayout(titles,1); layout->addLayout(header);

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
    cancelButton_ = new QPushButton(QStringLiteral("取消队列"), this);
    cancelButton_->setObjectName(QStringLiteral("batchCancelButton"));
    clearButton_ = new QPushButton(QStringLiteral("清空任务"), this);
    clearButton_->setObjectName(QStringLiteral("batchClearButton"));
    header->addWidget(addFilesButton_);
    addFilesButton_->setText(QStringLiteral("＋ 添加任务"));
    buttons->addWidget(startButton_);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(clearButton_);

    auto* concurrencyRow = new QHBoxLayout;
    concurrencyRow->addWidget(new QLabel(QStringLiteral("同时转码任务数："), this));
    concurrencyCombo_ = new QComboBox(this);
    concurrencyCombo_->setObjectName(QStringLiteral("batchConcurrencyCombo"));
    for (int count = 1; count <= 3; ++count) {
        concurrencyCombo_->addItem(QString::number(count), count);
    }
    concurrencyRow->addWidget(concurrencyCombo_);
    concurrencyRow->addStretch();


    auto* tablePane = new QWidget(this);
    auto* tableLayout = new QVBoxLayout(tablePane);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    table_ = new QTableView(tablePane);
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
    table_->setShowGrid(false); table_->setAlternatingRowColors(false);
    table_->horizontalHeader()->hide(); table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(112);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* covers=new TaskThumbnailCache(this);
    covers->setObjectName("batchThumbnailCache");
    connect(model_,&QAbstractItemModel::modelReset,covers,&TaskThumbnailCache::clear);
    connect(covers,&TaskThumbnailCache::ready,this,[this](quint64 id){
        const int row=model_->rowForId(id);
        if(row>=0) table_->viewport()->update(table_->visualRect(model_->index(row,0)));
    });
    auto* cardDelegate=new BatchCardDelegate(table_);
    cardDelegate->thumbnail=[covers](quint64 id,const QString& path){return covers->image(id,path);};
    cardDelegate->action=[this](int row,const QPoint& position){showTaskMenu(row,position);};
    cardDelegate->runningElapsed=[this](quint64 id){return runningTimers_.value(id).elapsed();};
    table_->setItemDelegateForColumn(0,cardDelegate);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_,&QWidget::customContextMenuRequested,this,[this](const QPoint& point){showTaskMenu(table_->indexAt(point).row(),table_->viewport()->mapToGlobal(point));});
    auto* elapsedRefresh=new QTimer(this); elapsedRefresh->setInterval(1000);
    connect(elapsedRefresh,&QTimer::timeout,this,[this]{
        for(auto it=running_.cbegin();it!=running_.cend();++it) { int row=model_->rowForId(it.key()); if(row>=0) table_->viewport()->update(table_->visualRect(model_->index(row,0))); }
    }); elapsedRefresh->start();
    for(int c=1;c<5;++c) table_->hideColumn(c);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->setMinimumHeight(180);
    auto* filters=new QHBoxLayout; auto* filterGroup=new QButtonGroup(this);
    const QStringList labels{QStringLiteral("全部"),QStringLiteral("进行中"),QStringLiteral("已完成"),QStringLiteral("失败")};
    for(int f=0;f<4;++f) { auto* button=new QPushButton(labels[f]); button->setProperty("filter",true); button->setCheckable(true); filterGroup->addButton(button,f); filters->addWidget(button); }
    filterGroup->button(0)->setChecked(true); filters->addStretch(); layout->insertLayout(2,filters);
    auto updateFilters=[this,filterGroup,labels]() {
        int counts[4]={model_->rowCount(),0,0,0};
        for(int row=0;row<model_->rowCount();++row) {
            const auto status=model_->task(row).status;
            const int category=status==BatchTask::Status::Running?1:status==BatchTask::Status::Succeeded?2:status==BatchTask::Status::Failed?3:0;
            if(category) ++counts[category];
            const int filter=filterGroup->checkedId(); table_->setRowHidden(row,filter!=0 && category!=filter);
        }
        for(int f=0;f<4;++f) filterGroup->button(f)->setText(labels[f]+QStringLiteral(" (%1)").arg(counts[f]));
    };
    connect(filterGroup,&QButtonGroup::idClicked,this,[updateFilters](int){updateFilters();});
    connect(model_,&QAbstractItemModel::rowsInserted,this,[updateFilters]{updateFilters();});
    connect(model_,&QAbstractItemModel::modelReset,this,[updateFilters]{updateFilters();});
    connect(model_,&QAbstractItemModel::dataChanged,this,[this,updateFilters](const QModelIndex& first,const QModelIndex& last){
        if(first.column()!=3 || last.column()!=3) updateFilters();
        for(int row=first.row();row<=last.row();++row) table_->viewport()->update(table_->visualRect(model_->index(row,0)));
    }); updateFilters();

    tableLayout->addWidget(table_);
    auto* empty=new QLabel(QStringLiteral("队列还是空的\n\n点击“添加任务”，选择要转换的 MP4 视频。")); empty->setAlignment(Qt::AlignCenter); empty->setProperty("muted",true); empty->setMinimumHeight(160); tableLayout->addWidget(empty);
    auto updateEmpty=[this,empty]{empty->setVisible(model_->rowCount()==0); table_->setVisible(model_->rowCount()!=0);};
    connect(model_,&QAbstractItemModel::rowsInserted,this,[updateEmpty]{updateEmpty();}); connect(model_,&QAbstractItemModel::modelReset,this,[updateEmpty]{updateEmpty();}); updateEmpty();

    stateLabel_ = new QLabel(QStringLiteral("尚未添加批量任务。"), tablePane);
    stateLabel_->setObjectName(QStringLiteral("batchStateLabel"));
    tableLayout->addWidget(stateLabel_);
    taskDetails_ = new QLabel(QStringLiteral("选择表格中的任务查看编码设置与结果。"), tablePane);
    taskDetails_->setObjectName(QStringLiteral("batchTaskDetails"));
    taskDetails_->setTextFormat(Qt::PlainText);
    taskDetails_->setWordWrap(true);

    auto* logPane = new QWidget(this);
    auto* logLayout = new QVBoxLayout(logPane);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->addWidget(taskDetails_);
    auto* logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel(QStringLiteral("查看日志："), logPane));
    logSelector_ = new QComboBox(logPane);
    logSelector_->setObjectName(QStringLiteral("batchLogSelector"));
    logSelector_->setMinimumWidth(220);
    logSelector_->addItem(QStringLiteral("全部日志"), QVariant::fromValue<qulonglong>(0));
    logHeader->addWidget(logSelector_);
    logHeader->addStretch();
    auto* copyLogButton = new QPushButton(QStringLiteral("复制日志"), logPane);
    copyLogButton->setObjectName(QStringLiteral("batchCopyLogButton"));
    copyLogButton->setEnabled(false);
    auto* clearLogButton = new QPushButton(QStringLiteral("清空日志"), logPane);
    clearLogButton->setObjectName(QStringLiteral("batchClearLogButton"));
    clearLogButton->setEnabled(false);
    logHeader->addWidget(copyLogButton);
    logHeader->addWidget(clearLogButton);
    logLayout->addLayout(logHeader);
    log_ = new QPlainTextEdit(logPane);
    log_->setObjectName(QStringLiteral("batchLog"));
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setPlaceholderText(QStringLiteral("各任务的后端日志会按任务编号显示在这里"));
    logLayout->addWidget(log_);
    layout->addWidget(tablePane,1);
    auto* detailDisclosure=StudioTheme::disclosure(QStringLiteral("任务详情与日志"),logPane,this);
    log_->setFixedHeight(140); layout->addWidget(detailDisclosure);
    concurrencyRow->addLayout(buttons);
    layout->addLayout(concurrencyRow);
    connect(table_,&QTableView::clicked,this,[detailDisclosure](const QModelIndex&) {
        auto* toggle=detailDisclosure->findChild<QPushButton*>(); if(toggle) toggle->setChecked(true);
    });

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
    connect(model_, &QAbstractItemModel::dataChanged, this, [this]() {
        if (selectedLogTaskId_ != 0) showTaskDetails(selectedLogTaskId_);
    });

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
        individuallyCancelled_.clear();
        taskPerformance_.clear();
        pendingLogLines_.clear();
        logEntries_.clear();
        selectedLogTaskId_ = 0;
        logSelector_->clear();
        logSelector_->addItem(QStringLiteral("全部日志"), QVariant::fromValue<qulonglong>(0));
        log_->clear();
        showTaskDetails(0);
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
    connect(backend, &BackendProcess::muxPerformanceReady, this,
            [this, id = task.id, backend](const MuxPerformance& performance) {
        if (running_.value(id) != backend) return;
        taskPerformance_.insert(id, performance);
        if (selectedLogTaskId_ == id) showTaskDetails(id);
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
            const bool cancelled=individuallyCancelled_.remove(id) || stopRequested_;
            const auto status = success ? BatchTask::Status::Succeeded
                : cancelled ? BatchTask::Status::Cancelled : BatchTask::Status::Failed;
            model_->setStatus(taskRow, status, message, elapsedMilliseconds);
            appendLogLine(id, QStringLiteral("[#%1] %2：%3").arg(id)
                              .arg(success ? QStringLiteral("成功")
                                   : cancelled ? QStringLiteral("已取消")
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
    showTaskDetails(id);
    QStringList lines;
    for (const LogEntry& entry : logEntries_) {
        if (id == 0 || entry.taskId == id) lines.append(entry.line);
    }
    log_->setPlainText(lines.join(QLatin1Char('\n')));
    QTextCursor cursor = log_->textCursor();
    cursor.movePosition(QTextCursor::End);
    log_->setTextCursor(cursor);
}

void BatchWidget::showTaskDetails(quint64 id)
{
    const int row = id == 0 ? -1 : model_->rowForId(id);
    if (row < 0) {
        taskDetails_->setText(QStringLiteral("选择表格中的任务查看编码设置与结果。"));
        return;
    }
    const BatchTask task = model_->task(row);
    const QString device = task.options.videoBackend == QStringLiteral("nvenc")
        ? QStringLiteral("GPU（NVIDIA NVENC）") : QStringLiteral("CPU（软件编码）");
    const QString codec = task.options.videoCodec == QStringLiteral("h264")
        ? QStringLiteral("H.264") : QStringLiteral("MPEG-4");
    QString details = QStringLiteral("任务 #%1 · %2 · %3 / %4 · 旋转 %5° · 倍速 %6x")
        .arg(task.id).arg(device, codec, task.options.audioCodec)
        .arg(task.options.rotationDegrees)
        .arg(QString::number(task.options.speedRatio, 'f', 2));
    const QString quality = task.options.quality == QStringLiteral("high") ? QStringLiteral("高画质")
        : task.options.quality == QStringLiteral("balanced") ? QStringLiteral("均衡")
        : task.options.quality == QStringLiteral("small") ? QStringLiteral("小体积")
        : task.options.quality == QStringLiteral("bitrate") ? QStringLiteral("指定视频码率")
        : QStringLiteral("原有默认");
    details += QStringLiteral("\n画质：%1%2 · 分辨率：%3 · 帧率：%4")
        .arg(quality,
             task.options.quality == QStringLiteral("bitrate")
                ? QStringLiteral(" %1 kbps").arg(task.options.videoBitrateKbps) : QString(),
             task.options.maxShortEdge ? QStringLiteral("短边不超过 %1").arg(task.options.maxShortEdge) : QStringLiteral("原始"),
             task.options.outputFps ? QStringLiteral("%1 fps").arg(task.options.outputFps) : QStringLiteral("原始"));
    details += QStringLiteral("\n状态：%1").arg(model_->data(model_->index(row, 2)).toString());
    if (task.elapsedMilliseconds >= 0)
        details += QStringLiteral(" · 耗时 %1").arg(formatElapsed(task.elapsedMilliseconds));
    if (task.status == BatchTask::Status::Succeeded) {
        const qint64 bytes = QFileInfo(task.options.outputPath).size();
        details += QStringLiteral(" · 输出 %1 MiB")
            .arg(QString::number(bytes / 1048576.0, 'f', 2));
    } else if (!task.detail.isEmpty()) {
        details += QStringLiteral(" · %1").arg(task.detail);
    }
    if (taskPerformance_.contains(id))
        details += QLatin1Char('\n') + formatMuxPerformance(taskPerformance_.value(id));
    taskDetails_->setText(details);
    taskDetails_->setToolTip(task.options.inputPath + QLatin1Char('\n') + task.options.outputPath);
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

void BatchWidget::showTaskMenu(int row,const QPoint& position)
{
    if(row<0 || row>=model_->rowCount()) return;
    table_->setCurrentIndex(model_->index(row,0));
        const auto task=model_->task(row); QMenu menu(this);
        auto* details=menu.addAction(QStringLiteral("查看参数、结果与日志"));
        connect(details,&QAction::triggered,this,[this,task]{showLogForTask(task.id); auto* toggle=findChild<QPushButton*>(QStringLiteral("任务详情与日志Toggle")); if(toggle) toggle->setChecked(true);});
        if(task.status==BatchTask::Status::Running || task.status==BatchTask::Status::Pending) {
            auto* cancel=menu.addAction(QStringLiteral("取消此任务"));
            connect(cancel,&QAction::triggered,this,[this,task,row]{
                if(auto* process=running_.value(task.id,nullptr)) { individuallyCancelled_.insert(task.id); process->cancel(); }
                else if(model_->task(row).status==BatchTask::Status::Pending) { model_->setStatus(row,BatchTask::Status::Cancelled,QStringLiteral("用户取消")); refreshControls(); }
            });
        }
        if(task.status==BatchTask::Status::Succeeded) {
            auto* open=menu.addAction(QStringLiteral("打开输出文件夹"));
            connect(open,&QAction::triggered,this,[task]{ QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(task.options.outputPath).absolutePath())); });
        }
    menu.exec(position);
}
