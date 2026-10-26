#include "mainwindow.h"
#include "backendprocess.h"
#include "batchwidget.h"
#include "elapsedformat.h"
#include "mediaprobe.h"

#include <QDir>
#include <QComboBox>
#include <QCloseEvent>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QFont>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QStackedWidget>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent, const QString& settingsFile)
    : QMainWindow(parent)
{
    settings_ = settingsFile.isEmpty()
        ? new QSettings(QSettings::NativeFormat, QSettings::UserScope,
                        QStringLiteral("QtFFmpegStudio"), QStringLiteral("QtFFmpegStudio"), this)
        : new QSettings(settingsFile, QSettings::IniFormat, this);
    setWindowTitle(QStringLiteral("QtFFmpegStudio"));
    resize(1000, 800);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("QtFFmpegStudio"), central);
    title->setObjectName(QStringLiteral("appTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(new QLabel(QStringLiteral("FFmpeg 图形化转码工具"), central));
    layout->addLayout(header);

    auto* modeNavigation = new QTabBar(central);
    modeNavigation->setObjectName(QStringLiteral("modeNavigation"));
    modeNavigation->setExpanding(true);
    modeNavigation->addTab(QStringLiteral("单文件转码"));
    modeNavigation->addTab(QStringLiteral("批量队列"));
    modeNavigation->addTab(QStringLiteral("模拟任务"));
    layout->addWidget(modeNavigation);

    singleFileControls_ = new QWidget(central);
    singleFileControls_->setObjectName(QStringLiteral("singleFileControls"));
    auto* singlePathsLayout = new QVBoxLayout(singleFileControls_);
    singlePathsLayout->setContentsMargins(0, 0, 0, 0);

    inputPathEdit_ = new QLineEdit(singleFileControls_);
    inputPathEdit_->setObjectName(QStringLiteral("inputPathEdit"));
    inputPathEdit_->setReadOnly(true);
    inputPathEdit_->setPlaceholderText(QStringLiteral("请选择一个 MP4 视频"));
    inputButton_ = new QPushButton(QStringLiteral("选择输入视频…"), singleFileControls_);
    inputButton_->setObjectName(QStringLiteral("inputButton"));
    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(inputPathEdit_);
    inputRow->addWidget(inputButton_);
    singlePathsLayout->addWidget(new QLabel(QStringLiteral("输入视频"), singleFileControls_));
    singlePathsLayout->addLayout(inputRow);

    outputPathEdit_ = new QLineEdit(singleFileControls_);
    outputPathEdit_->setObjectName(QStringLiteral("outputPathEdit"));
    outputPathEdit_->setReadOnly(true);
    outputPathEdit_->setPlaceholderText(QStringLiteral("请选择输出 MP4 文件的位置"));
    outputButton_ = new QPushButton(QStringLiteral("选择输出文件…"), singleFileControls_);
    auto* outputRow = new QHBoxLayout;
    outputRow->addWidget(outputPathEdit_);
    outputRow->addWidget(outputButton_);
    singlePathsLayout->addWidget(new QLabel(QStringLiteral("输出文件"), singleFileControls_));
    singlePathsLayout->addLayout(outputRow);
    auto* mediaGroup = new QGroupBox(QStringLiteral("视频信息"), singleFileControls_);
    mediaGroup->setObjectName(QStringLiteral("mediaInfoGroup"));
    auto* mediaLayout = new QVBoxLayout(mediaGroup);
    mediaState_ = new QLabel(QStringLiteral("等待选择视频"), mediaGroup);
    mediaState_->setObjectName(QStringLiteral("mediaState"));
    mediaRetryButton_ = new QPushButton(QStringLiteral("重新分析"), mediaGroup);
    mediaRetryButton_->setObjectName(QStringLiteral("mediaRetryButton"));
    mediaRetryButton_->setEnabled(false);
    mediaDetails_ = new QLabel(QStringLiteral("选择 MP4 文件后显示视频信息"), mediaGroup);
    mediaDetails_->setObjectName(QStringLiteral("mediaDetails"));
    mediaDetails_->setTextFormat(Qt::PlainText);
    mediaDetails_->setWordWrap(true);
    auto* mediaHeader = new QHBoxLayout;
    mediaHeader->addWidget(mediaState_);
    mediaHeader->addStretch();
    mediaHeader->addWidget(mediaRetryButton_);
    mediaLayout->addLayout(mediaHeader);
    mediaLayout->addWidget(mediaDetails_);
    singlePathsLayout->addWidget(mediaGroup);

    optionsGroup_ = new QGroupBox(QStringLiteral("转码参数"), central);
    optionsGroup_->setObjectName(QStringLiteral("optionsGroup"));
    auto* optionsForm = new QGridLayout(optionsGroup_);

    rotationCombo_ = new QComboBox(optionsGroup_);
    rotationCombo_->setObjectName(QStringLiteral("rotationCombo"));
    rotationCombo_->addItem(QStringLiteral("不旋转"), 0);
    rotationCombo_->addItem(QStringLiteral("顺时针 90°"), 90);
    rotationCombo_->addItem(QStringLiteral("旋转 180°"), 180);
    rotationCombo_->addItem(QStringLiteral("顺时针 270°"), 270);
    optionsForm->addWidget(new QLabel(QStringLiteral("旋转角度"), optionsGroup_), 0, 0);
    optionsForm->addWidget(rotationCombo_, 0, 1);

    speedSpin_ = new QDoubleSpinBox(optionsGroup_);
    speedSpin_->setObjectName(QStringLiteral("speedSpin"));
    speedSpin_->setRange(0.5, 4.0);
    speedSpin_->setDecimals(2);
    speedSpin_->setSingleStep(0.25);
    speedSpin_->setSuffix(QStringLiteral("x"));
    speedSpin_->setValue(1.0);
    optionsForm->addWidget(new QLabel(QStringLiteral("播放倍速"), optionsGroup_), 0, 2);
    optionsForm->addWidget(speedSpin_, 0, 3);

    videoCodecCombo_ = new QComboBox(optionsGroup_);
    videoCodecCombo_->setObjectName(QStringLiteral("videoCodecCombo"));
    videoCodecCombo_->addItem(QStringLiteral("MPEG-4"), QStringLiteral("mpeg4"));
    videoCodecCombo_->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
    optionsForm->addWidget(new QLabel(QStringLiteral("视频编码"), optionsGroup_), 1, 0);
    optionsForm->addWidget(videoCodecCombo_, 1, 1);

    audioCodecCombo_ = new QComboBox(optionsGroup_);
    audioCodecCombo_->setObjectName(QStringLiteral("audioCodecCombo"));
    audioCodecCombo_->addItem(QStringLiteral("AC-3"), QStringLiteral("ac3"));
    audioCodecCombo_->addItem(QStringLiteral("AAC"), QStringLiteral("aac"));
    optionsForm->addWidget(new QLabel(QStringLiteral("音频编码"), optionsGroup_), 1, 2);
    optionsForm->addWidget(audioCodecCombo_, 1, 3);
    optionsForm->setColumnStretch(1, 1);
    optionsForm->setColumnStretch(3, 1);
    layout->addWidget(optionsGroup_);

    singleFileTools_ = new QWidget(central);
    singleFileTools_->setObjectName(QStringLiteral("singleFileTools"));
    auto* singleToolsLayout = new QVBoxLayout(singleFileTools_);
    singleToolsLayout->setContentsMargins(0, 0, 0, 0);
    checkButton_ = new QPushButton(QStringLiteral("检查路径"), singleFileTools_);
    feedbackLabel_ = new QLabel(
        QStringLiteral("选择输入视频和输出文件后，点击“检查路径”。"), singleFileTools_);
    feedbackLabel_->setObjectName(QStringLiteral("feedbackLabel"));
    feedbackLabel_->setWordWrap(true);
    // 路径可能包含 & 或 < 等字符，反馈文字始终按纯文本显示。
    feedbackLabel_->setTextFormat(Qt::PlainText);
    singleToolsLayout->addWidget(checkButton_);
    singleToolsLayout->addWidget(feedbackLabel_);

    auto* previewButton = new QPushButton(QStringLiteral("预览参数"), singleFileTools_);
    previewButton->setObjectName(QStringLiteral("previewButton"));
    optionsPreview_ = new QPlainTextEdit(singleFileTools_);
    optionsPreview_->setObjectName(QStringLiteral("optionsPreview"));
    optionsPreview_->setReadOnly(true);
    optionsPreview_->setMaximumHeight(82);
    optionsPreview_->setPlaceholderText(QStringLiteral("点击“预览参数”查看当前配置"));
    singleToolsLayout->addWidget(previewButton);
    singleToolsLayout->addWidget(optionsPreview_);

    auto* taskPages = new QStackedWidget(central);
    taskPages->setObjectName(QStringLiteral("taskPages"));
    auto* simulationGroup = new QGroupBox(QStringLiteral("模拟任务（不执行转码）"), taskPages);
    auto* simulationLayout = new QVBoxLayout(simulationGroup);
    simulationProgress_ = new QProgressBar(simulationGroup);
    simulationProgress_->setObjectName(QStringLiteral("simulationProgress"));
    simulationProgress_->setRange(0, 100);
    simulationProgress_->setValue(0);
    simulationProgress_->setFormat(QStringLiteral("模拟进度：%p%"));
    simulationLayout->addWidget(simulationProgress_);

    auto* simulationButtons = new QHBoxLayout;
    startSimulationButton_ = new QPushButton(QStringLiteral("开始模拟"), simulationGroup);
    startSimulationButton_->setObjectName(QStringLiteral("startSimulationButton"));
    stopSimulationButton_ = new QPushButton(QStringLiteral("停止模拟"), simulationGroup);
    stopSimulationButton_->setObjectName(QStringLiteral("stopSimulationButton"));
    stopSimulationButton_->setEnabled(false);
    simulationButtons->addWidget(startSimulationButton_);
    simulationButtons->addWidget(stopSimulationButton_);
    simulationLayout->addLayout(simulationButtons);

    simulationStateLabel_ = new QLabel(QStringLiteral("尚未开始模拟任务。"), simulationGroup);
    simulationStateLabel_->setObjectName(QStringLiteral("simulationStateLabel"));
    simulationLayout->addWidget(simulationStateLabel_);
    auto* realGroup = new QGroupBox(QStringLiteral("单文件转码"), taskPages);
    auto* realLayout = new QVBoxLayout(realGroup);
    auto* realButtons = new QHBoxLayout;
    startRealButton_ = new QPushButton(QStringLiteral("开始真实转码"), realGroup);
    startRealButton_->setObjectName(QStringLiteral("startRealButton"));
    cancelRealButton_ = new QPushButton(QStringLiteral("取消转码"), realGroup);
    cancelRealButton_->setObjectName(QStringLiteral("cancelRealButton"));
    cancelRealButton_->setEnabled(false);
    openOutputFolderButton_ = new QPushButton(QStringLiteral("打开输出目录"), realGroup);
    openOutputFolderButton_->setObjectName(QStringLiteral("openOutputFolderButton"));
    openOutputFolderButton_->setEnabled(false);
    realButtons->addWidget(startRealButton_);
    realButtons->addWidget(cancelRealButton_);
    realButtons->addWidget(openOutputFolderButton_);
    realLayout->addLayout(realButtons);
    realStateLabel_ = new QLabel(QStringLiteral("请选择输入、输出和参数，然后开始真实转码。"), realGroup);
    realStateLabel_->setObjectName(QStringLiteral("realStateLabel"));
    realStateLabel_->setWordWrap(true);
    realLayout->addWidget(realStateLabel_);
    realTranscodeProgress_ = new QProgressBar(realGroup);
    realTranscodeProgress_->setObjectName(QStringLiteral("realTranscodeProgress"));
    realTranscodeProgress_->setRange(0, 100);
    realTranscodeProgress_->setValue(0);
    realTranscodeProgress_->setFormat(QStringLiteral("转码进度：估算中"));
    realLayout->addWidget(realTranscodeProgress_);
    realElapsedLabel_ = new QLabel(QStringLiteral("耗时：—"), realGroup);
    realElapsedLabel_->setObjectName(QStringLiteral("realElapsedLabel"));
    realLayout->addWidget(realElapsedLabel_);
    auto* realLogTools = new QHBoxLayout;
    realLogTools->addWidget(new QLabel(QStringLiteral("运行日志"), realGroup));
    realLogTools->addStretch();
    auto* copyRealLog = new QPushButton(QStringLiteral("复制日志"), realGroup);
    copyRealLog->setObjectName(QStringLiteral("realCopyLogButton"));
    copyRealLog->setEnabled(false);
    auto* clearRealLog = new QPushButton(QStringLiteral("清空日志"), realGroup);
    clearRealLog->setObjectName(QStringLiteral("realClearLogButton"));
    clearRealLog->setEnabled(false);
    realLogTools->addWidget(copyRealLog);
    realLogTools->addWidget(clearRealLog);
    realLayout->addLayout(realLogTools);
    realLog_ = new QPlainTextEdit(realGroup);
    realLog_->setObjectName(QStringLiteral("realLog"));
    realLog_->setReadOnly(true);
    realLog_->setMaximumBlockCount(2000);
    realLog_->setPlaceholderText(QStringLiteral("转码日志会显示在这里"));
    realLayout->addWidget(realLog_);
    connect(realLog_, &QPlainTextEdit::textChanged, this,
            [this, copyRealLog, clearRealLog]() {
        const bool hasLog = !realLog_->toPlainText().isEmpty();
        copyRealLog->setEnabled(hasLog);
        clearRealLog->setEnabled(hasLog);
    });
    connect(copyRealLog, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(realLog_->toPlainText());
    });
    connect(clearRealLog, &QPushButton::clicked, realLog_, &QPlainTextEdit::clear);
    batchWidget_ = new BatchWidget(taskPages, [this]() { return readOptions(); });
    batchWidget_->setObjectName(QStringLiteral("batchWidget"));
    taskPages->addWidget(realGroup);
    taskPages->addWidget(batchWidget_);
    taskPages->addWidget(simulationGroup);
    taskPages->setCurrentWidget(realGroup);
    auto* workspace = new QHBoxLayout;
    auto* singleSide = new QWidget(central);
    singleSide->setObjectName(QStringLiteral("singleSideContents"));
    auto* singleSideLayout = new QVBoxLayout(singleSide);
    singleSideLayout->setContentsMargins(0, 0, 4, 0);
    singleSideLayout->addWidget(singleFileControls_);
    singleSideLayout->addWidget(singleFileTools_);
    singleSideLayout->addStretch();
    auto* sideScroll = new QScrollArea(central);
    sideScroll->setObjectName(QStringLiteral("singleSidePanel"));
    sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setMinimumWidth(320);
    sideScroll->setMaximumWidth(410);
    sideScroll->setWidget(singleSide);
    workspace->addWidget(sideScroll);
    workspace->addWidget(taskPages, 1);
    layout->addLayout(workspace, 1);
    connect(modeNavigation, &QTabBar::currentChanged, this,
            [this, taskPages, sideScroll](int index) {
        taskPages->setCurrentIndex(index);
        const bool batchTab = index == 1;
        sideScroll->setVisible(!batchTab);
        singleFileControls_->setVisible(!batchTab);
        singleFileTools_->setVisible(!batchTab);
    });
    setCentralWidget(central);
    central->setObjectName(QStringLiteral("appShell"));
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#appShell { background: #101827; color: #e8eef7; }
        QScrollArea#singleSidePanel, QWidget#singleSideContents {
            background: #101827; border: 0; }
        QLabel, QGroupBox { color: #e8eef7; }
        QLabel#appTitle { color: #f8fafc; }
        QGroupBox { background: #192438; border: 1px solid #2d3c55; border-radius: 10px;
                    margin-top: 13px; padding: 10px 12px 8px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; }
        QLineEdit, QComboBox, QDoubleSpinBox, QPlainTextEdit, QTableView {
            background: #111b2c; color: #e8eef7; border: 1px solid #35455f;
            border-radius: 6px; padding: 5px; selection-background-color: #3167bb; }
        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus {
            border-color: #6da8ff; }
        QPushButton { background: #273a55; color: #e8eef7; border: 1px solid #395172;
                      border-radius: 6px; padding: 7px 13px; }
        QPushButton:hover { background: #345172; }
        QPushButton:disabled { color: #8291a6; background: #202c3e; border-color: #29394f; }
        QPushButton#startRealButton, QPushButton#batchStartButton {
            background: #2563bf; border-color: #4183df; color: white; font-weight: 600; }
        QPushButton#startRealButton:hover, QPushButton#batchStartButton:hover { background: #3477d5; }
        QTabBar::tab { background: #1a2940; color: #b9c8dc; padding: 10px 22px;
                       border: 1px solid #30415c; }
        QTabBar::tab:selected { background: #285493; color: white; }
        QProgressBar { background: #142034; color: #edf4ff; border: 1px solid #344968;
                       border-radius: 6px; text-align: center; min-height: 18px; }
        QProgressBar::chunk { background: #3986e5; border-radius: 5px; }
        QHeaderView::section { background: #243652; color: #e8eef7; padding: 6px;
                               border: 0; border-right: 1px solid #35455f; }
        QStatusBar { background: #172337; color: #cbd8e9; }
        QSplitter::handle { background: #35455f; }
    )"));
    statusBar()->showMessage(QStringLiteral("准备就绪"));

    backend_ = new BackendProcess(this);
    mediaProbe_ = new MediaProbe(this);
    connect(mediaProbe_, &MediaProbe::ready, this,
            [this](const QString& path, const MediaInfo& info) {
        if (path != inputPathEdit_->text()) return;
        const QString unknown = QStringLiteral("未知");
        const QString duration = info.durationSeconds > 0
            ? QStringLiteral("%1 秒").arg(QString::number(info.durationSeconds, 'f', 1)) : unknown;
        const QString size = info.fileSizeBytes >= 0
            ? QStringLiteral("%1 MiB").arg(QString::number(info.fileSizeBytes / 1048576.0, 'f', 2)) : unknown;
        const QString dimensions = info.width > 0 && info.height > 0
            ? QStringLiteral("%1 × %2").arg(info.width).arg(info.height) : unknown;
        const QString fps = info.frameRate > 0
            ? QStringLiteral("%1 fps").arg(QString::number(info.frameRate, 'f', 2)) : unknown;
        mediaDetails_->setText(QStringLiteral("大小 %1  ·  容器 %2  ·  时长 %3\n视频 %4  ·  %5  ·  %6\n音频 %7  ·  %8 Hz  ·  %9 声道")
            .arg(size, info.container.isEmpty() ? unknown : info.container, duration,
                 info.videoCodec.isEmpty() ? unknown : info.videoCodec, dimensions, fps,
                 info.hasAudio ? (info.audioCodec.isEmpty() ? unknown : info.audioCodec) : QStringLiteral("无音轨"),
                 info.sampleRate > 0 ? QString::number(info.sampleRate) : unknown,
                 info.channels > 0 ? QString::number(info.channels) : unknown));
        mediaState_->setText(info.supported ? QStringLiteral("已分析 · 可尝试转码")
            : QStringLiteral("已分析 · 当前转码要求同时包含视频和音频流"));
    });
    connect(mediaProbe_, &MediaProbe::failed, this,
            [this](const QString& path, const QString& error) {
        if (path != inputPathEdit_->text()) return;
        mediaState_->setText(QStringLiteral("分析失败"));
        mediaDetails_->setText(error);
    });
    connect(inputPathEdit_, &QLineEdit::textChanged, this, [this](const QString& path) {
        inputPathEdit_->setToolTip(path);
        mediaRetryButton_->setEnabled(!path.isEmpty());
        mediaState_->setText(path.isEmpty() ? QStringLiteral("等待选择视频") : QStringLiteral("分析中…"));
        mediaDetails_->setText(QStringLiteral("选择 MP4 文件后显示视频信息"));
        mediaProbe_->inspect(path);
    });
    connect(mediaRetryButton_, &QPushButton::clicked, this, [this]() {
        mediaState_->setText(QStringLiteral("分析中…"));
        mediaDetails_->setText(QStringLiteral("正在重新读取视频信息"));
        mediaProbe_->inspect(inputPathEdit_->text());
    });
    connect(outputPathEdit_, &QLineEdit::textChanged, this, [this](const QString& path) {
        outputPathEdit_->setToolTip(path);
    });
    simulationTimer_ = new QTimer(this);
    simulationTimer_->setInterval(200);
    connect(simulationTimer_, &QTimer::timeout, this, &MainWindow::advanceSimulation);

    connect(inputButton_, &QPushButton::clicked, this, &MainWindow::selectInputFile);
    connect(outputButton_, &QPushButton::clicked, this, &MainWindow::selectOutputFile);
    connect(checkButton_, &QPushButton::clicked, this, &MainWindow::checkPaths);
    connect(previewButton, &QPushButton::clicked, this, &MainWindow::showOptionsPreview);
    connect(startSimulationButton_, &QPushButton::clicked, this, &MainWindow::startSimulation);
    connect(stopSimulationButton_, &QPushButton::clicked, this, &MainWindow::stopSimulation);
    connect(startRealButton_, &QPushButton::clicked, this, &MainWindow::startRealTranscode);
    connect(cancelRealButton_, &QPushButton::clicked, this, &MainWindow::cancelRealTranscode);
    connect(openOutputFolderButton_, &QPushButton::clicked, this, &MainWindow::openOutputFolder);
    connect(batchWidget_, &BatchWidget::activeChanged, this, [this](bool active) {
        setTaskState(active ? TaskState::BatchRunning : TaskState::Idle);
        statusBar()->showMessage(active ? QStringLiteral("批量队列运行中")
                                        : QStringLiteral("批量队列已结束"));
        if (!active && closeAfterCancel_) {
            closeAfterCancel_ = false;
            QTimer::singleShot(0, this, &QWidget::close);
        }
    });
    connect(backend_, &BackendProcess::logReceived, this, [this](const QString& text) {
        QTextCursor cursor = realLog_->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text);
        realLog_->setTextCursor(cursor);
    });
    connect(backend_, &BackendProcess::transcodeProgressChanged, this, [this](int percent) {
        realTranscodeProgress_->setValue(percent);
        realTranscodeProgress_->setFormat(QStringLiteral("转码进度（估算）：%p%"));
        realStateLabel_->setText(QStringLiteral("正在转码；整体估算进度 %1%。").arg(percent));
    });
    connect(backend_, &BackendProcess::finished, this,
            [this](bool success, const QString& message) {
        realElapsedLabel_->setText(realElapsedTimer_.isValid()
            ? QStringLiteral("耗时：%1").arg(formatElapsed(realElapsedTimer_.elapsed()))
            : QStringLiteral("耗时：—"));
        realElapsedTimer_.invalidate();
        if (success) realTranscodeProgress_->setValue(100);
        realTranscodeProgress_->setFormat(success
            ? QStringLiteral("转码完成：100%")
            : QStringLiteral("转码未完成｜已处理约 %p%"));
        lastSuccessfulOutput_ = success ? outputPathEdit_->text() : QString{};
        setTaskState(TaskState::Idle);
        realStateLabel_->setText(message);
        statusBar()->showMessage(success ? QStringLiteral("真实转码完成")
                                         : QStringLiteral("真实转码失败或取消"));
        if (closeAfterCancel_) {
            closeAfterCancel_ = false;
            QTimer::singleShot(0, this, &QWidget::close);
        }
    });

    // 路径变化后，旧的检查结论不再有效。
    auto clearFeedback = [this]() {
        feedbackLabel_->setText(QStringLiteral("路径已更新，请重新检查。"));
        statusBar()->showMessage(QStringLiteral("待检查"));
    };
    connect(inputPathEdit_, &QLineEdit::textChanged, this, clearFeedback);
    connect(outputPathEdit_, &QLineEdit::textChanged, this, clearFeedback);
    connect(outputPathEdit_, &QLineEdit::textChanged, this, [this]() {
        lastSuccessfulOutput_.clear();
        openOutputFolderButton_->setEnabled(false);
    });

    // 预览是一次快照；选项改变后清除旧快照。
    auto clearPreview = [this]() { optionsPreview_->clear(); };
    connect(inputPathEdit_, &QLineEdit::textChanged, this, clearPreview);
    connect(outputPathEdit_, &QLineEdit::textChanged, this, clearPreview);
    connect(rotationCombo_, &QComboBox::currentIndexChanged, this, clearPreview);
    connect(speedSpin_, &QDoubleSpinBox::valueChanged, this, clearPreview);
    connect(videoCodecCombo_, &QComboBox::currentIndexChanged, this, clearPreview);
    connect(audioCodecCombo_, &QComboBox::currentIndexChanged, this, clearPreview);
    loadSettings();
}

TranscodeOptions MainWindow::readOptions() const
{
    TranscodeOptions options;
    options.inputPath = inputPathEdit_->text();
    options.outputPath = outputPathEdit_->text();
    options.rotationDegrees = rotationCombo_->currentData().toInt();
    options.speedRatio = speedSpin_->value();
    options.videoCodec = videoCodecCombo_->currentData().toString();
    options.audioCodec = audioCodecCombo_->currentData().toString();
    return options;
}

void MainWindow::showOptionsPreview()
{
    const TranscodeOptions options = readOptions();
    const QString input = options.inputPath.isEmpty()
        ? QStringLiteral("尚未选择") : options.inputPath;
    const QString output = options.outputPath.isEmpty()
        ? QStringLiteral("尚未选择") : options.outputPath;

    optionsPreview_->setPlainText(
        QStringLiteral("输入：%1\n输出：%2\n旋转：%3°\n倍速：%4x\n视频编码：%5\n音频编码：%6")
            .arg(input)
            .arg(output)
            .arg(options.rotationDegrees)
            .arg(QString::number(options.speedRatio, 'f', 2))
            .arg(options.videoCodec)
            .arg(options.audioCodec));
}

void MainWindow::selectInputFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择输入视频"),
        inputPathEdit_->text(),
        QStringLiteral("MP4 视频 (*.mp4 *.MP4)"));

    // 用户取消时返回空字符串，保留上次的选择。
    if (path.isEmpty()) {
        return;
    }
    inputPathEdit_->setText(QDir::toNativeSeparators(path));
}

void MainWindow::selectOutputFile()
{
    QString suggestedPath = outputPathEdit_->text();
    if (suggestedPath.isEmpty()) {
        if (inputPathEdit_->text().isEmpty()) {
            suggestedPath = QDir::home().filePath(QStringLiteral("output.mp4"));
        } else {
            const QFileInfo input(inputPathEdit_->text());
            suggestedPath = input.dir().filePath(
                input.completeBaseName() + QStringLiteral("_converted.mp4"));
        }
    }

    QFileDialog dialog(this, QStringLiteral("选择输出文件"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(QStringLiteral("MP4 视频 (*.mp4 *.MP4)"));
    dialog.setDefaultSuffix(QStringLiteral("mp4"));
    dialog.selectFile(suggestedPath);

    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }
    outputPathEdit_->setText(QDir::toNativeSeparators(dialog.selectedFiles().first()));
}

void MainWindow::checkPaths()
{
    validatePaths();
}

bool MainWindow::validatePaths()
{
    const QFileInfo input(inputPathEdit_->text());
    const QFileInfo output(outputPathEdit_->text());
    QString error;

    if (inputPathEdit_->text().isEmpty()) {
        error = QStringLiteral("请先选择输入视频。");
    } else if (!input.exists() || !input.isFile() || !input.isReadable()) {
        error = QStringLiteral("输入文件不存在或不可读取，请重新选择。");
    } else if (input.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) {
        error = QStringLiteral("本阶段只支持 MP4 输入文件。");
    } else if (outputPathEdit_->text().isEmpty()) {
        error = QStringLiteral("请选择输出文件。");
    } else if (output.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) {
        error = QStringLiteral("输出文件需要使用 .mp4 扩展名。");
    } else if (!output.dir().exists()) {
        error = QStringLiteral("输出目录不存在，请重新选择。");
    } else if (output.exists() && !output.isFile()) {
        error = QStringLiteral("输出位置不是普通文件，请重新选择。");
    } else {
        // canonicalFilePath 用于已存在的文件；未创建的输出则使用绝对路径。
        const QString outputPath = output.exists()
            ? output.canonicalFilePath() : output.absoluteFilePath();
#ifdef Q_OS_WIN
        const auto pathCase = Qt::CaseInsensitive;
#else
        const auto pathCase = Qt::CaseSensitive;
#endif
        if (input.canonicalFilePath().compare(outputPath, pathCase) == 0) {
            error = QStringLiteral("输入和输出不能是同一个文件。");
        }
    }

    if (!error.isEmpty()) {
        feedbackLabel_->setText(error);
        statusBar()->showMessage(QStringLiteral("路径检查未通过"));
        return false;
    }

    feedbackLabel_->setText(output.exists()
        ? QStringLiteral("路径检查通过。目标文件已存在，真实转码前需确认覆盖。")
        : QStringLiteral("路径检查通过。可以开始模拟任务或真实转码。"));
    statusBar()->showMessage(QStringLiteral("路径检查通过"));
    return true;
}

void MainWindow::setTaskState(TaskState state)
{
    taskState_ = state;
    const bool idle = state == TaskState::Idle;
    startSimulationButton_->setEnabled(idle);
    stopSimulationButton_->setEnabled(state == TaskState::Simulating);
    startRealButton_->setEnabled(idle);
    cancelRealButton_->setEnabled(state == TaskState::Transcoding);
    inputButton_->setEnabled(idle);
    outputButton_->setEnabled(idle);
    checkButton_->setEnabled(idle);
    optionsGroup_->setEnabled(idle);
    openOutputFolderButton_->setEnabled(idle && QFileInfo(lastSuccessfulOutput_).isFile());
    batchWidget_->setExternalAvailable(idle || state == TaskState::BatchRunning
                                      || state == TaskState::BatchCancelling);
}

void MainWindow::startSimulation()
{
    if (taskState_ != TaskState::Idle || !validatePaths()) {
        return;
    }

    showOptionsPreview();
    simulationProgress_->setValue(0);
    simulationStateLabel_->setText(QStringLiteral("模拟任务进行中；不会生成视频文件。"));
    setTaskState(TaskState::Simulating);
    statusBar()->showMessage(QStringLiteral("模拟任务运行中"));
    simulationTimer_->start();
}

void MainWindow::stopSimulation()
{
    if (!simulationTimer_->isActive()) {
        return;
    }

    simulationTimer_->stop();
    setTaskState(TaskState::Idle);
    simulationStateLabel_->setText(QStringLiteral("模拟任务已停止；可以重新开始。"));
    statusBar()->showMessage(QStringLiteral("模拟任务已停止"));
}

void MainWindow::advanceSimulation()
{
    const int nextValue = simulationProgress_->value() + 10;
    simulationProgress_->setValue(nextValue);
    if (nextValue >= 100) {
        simulationTimer_->stop();
        setTaskState(TaskState::Idle);
        simulationStateLabel_->setText(QStringLiteral("模拟任务完成；未生成视频文件。"));
        statusBar()->showMessage(QStringLiteral("模拟任务完成"));
    }
}

void MainWindow::startRealTranscode()
{
    if (taskState_ != TaskState::Idle || !validatePaths()) return;

    const TranscodeOptions options = readOptions();
    const bool outputExists = QFileInfo::exists(options.outputPath);
    if (outputExists) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("确认覆盖"),
            QStringLiteral("输出文件已经存在，确定覆盖吗？\n%1").arg(options.outputPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    realLog_->clear();
    realTranscodeProgress_->setFormat(QStringLiteral("转码进度：估算中"));
    realTranscodeProgress_->setValue(0);
    lastSuccessfulOutput_.clear();
    realStateLabel_->setText(QStringLiteral("正在启动后端程序…"));
    setTaskState(TaskState::Transcoding);
    realElapsedLabel_->setText(QStringLiteral("耗时：计时中"));
    realElapsedTimer_.start();
    backend_->start(options, outputExists);
}

void MainWindow::cancelRealTranscode()
{
    if (!backend_->running()) return;
    cancelRealButton_->setEnabled(false);
    realStateLabel_->setText(QStringLiteral("正在取消转码…"));
    setTaskState(TaskState::Cancelling);
    backend_->cancel();
}

void MainWindow::openOutputFolder()
{
    if (taskState_ != TaskState::Idle || !QFileInfo(lastSuccessfulOutput_).isFile()) return;
    const QString directory = QFileInfo(lastSuccessfulOutput_).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        QMessageBox::warning(this, QStringLiteral("打开目录失败"),
                             QStringLiteral("无法打开输出目录：\n%1").arg(directory));
    }
}

void MainWindow::loadSettings()
{
    const QSize storedSize = settings_->value(QStringLiteral("window/size")).toSize();
    if (storedSize.width() >= 600 && storedSize.height() >= 500) resize(storedSize);

    const QString input = settings_->value(QStringLiteral("files/input")).toString();
    const QFileInfo inputInfo(input);
    if (inputInfo.isFile() && inputInfo.isReadable()
        && inputInfo.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0) {
        inputPathEdit_->setText(input);
    }
    const QString output = settings_->value(QStringLiteral("files/output")).toString();
    const QFileInfo outputInfo(output);
    if (outputInfo.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0
        && outputInfo.dir().exists() && (!outputInfo.exists() || outputInfo.isFile())
        && inputInfo.absoluteFilePath() != outputInfo.absoluteFilePath()) {
        outputPathEdit_->setText(output);
    }

    const int rotation = rotationCombo_->findData(settings_->value(QStringLiteral("options/rotation")));
    if (rotation >= 0) rotationCombo_->setCurrentIndex(rotation);
    const double speed = settings_->value(QStringLiteral("options/speed"), 1.0).toDouble();
    if (speed >= speedSpin_->minimum() && speed <= speedSpin_->maximum()) speedSpin_->setValue(speed);
    const int video = videoCodecCombo_->findData(settings_->value(QStringLiteral("options/videoCodec")));
    if (video >= 0) videoCodecCombo_->setCurrentIndex(video);
    const int audio = audioCodecCombo_->findData(settings_->value(QStringLiteral("options/audioCodec")));
    if (audio >= 0) audioCodecCombo_->setCurrentIndex(audio);
    batchWidget_->setMaxConcurrent(settings_->value(QStringLiteral("batch/maxConcurrent"), 1).toInt());
}

void MainWindow::saveSettings()
{
    settings_->setValue(QStringLiteral("window/size"), size());
    const TranscodeOptions options = readOptions();
    const QFileInfo input(options.inputPath);
    if (input.isFile() && input.isReadable()
        && input.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0) {
        settings_->setValue(QStringLiteral("files/input"), options.inputPath);
    } else {
        settings_->remove(QStringLiteral("files/input"));
    }
    const QFileInfo output(options.outputPath);
    if (output.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0
        && output.dir().exists() && (!output.exists() || output.isFile())
        && input.absoluteFilePath() != output.absoluteFilePath()) {
        settings_->setValue(QStringLiteral("files/output"), options.outputPath);
    } else {
        settings_->remove(QStringLiteral("files/output"));
    }
    settings_->setValue(QStringLiteral("options/rotation"), options.rotationDegrees);
    settings_->setValue(QStringLiteral("options/speed"), options.speedRatio);
    settings_->setValue(QStringLiteral("options/videoCodec"), options.videoCodec);
    settings_->setValue(QStringLiteral("options/audioCodec"), options.audioCodec);
    settings_->setValue(QStringLiteral("batch/maxConcurrent"), batchWidget_->maxConcurrent());
    settings_->sync();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (backend_->running() || batchWidget_->active()) {
        if (closeAfterCancel_) {
            event->ignore();
            return;
        }
        const auto answer = QMessageBox::question(
            this, QStringLiteral("结束转码"),
            QStringLiteral("转码仍在运行。要结束任务并关闭窗口吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        // 对话框运行自己的事件循环；用户回答前，短任务可能已经结束。
        if (!backend_->running() && !batchWidget_->active()) {
            saveSettings();
            QMainWindow::closeEvent(event);
            return;
        }
        closeAfterCancel_ = true;
        if (batchWidget_->active()) {
            setTaskState(TaskState::BatchCancelling);
            batchWidget_->cancelQueue();
        } else {
            cancelRealTranscode();
        }
        event->ignore();
        return;
    }
    if (taskState_ == TaskState::Simulating) {
        simulationTimer_->stop();
        setTaskState(TaskState::Idle);
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}
