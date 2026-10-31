#include "mainwindow.h"
#include "appicon.h"
#include "studiotheme.h"
#include <QButtonGroup>
#include <QPalette>
#include <QStyle>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDropEvent>
#include "backendprocess.h"
#include "batchwidget.h"
#include "elapsedformat.h"
#include "mediaprobe.h"
#include "videopreview.h"

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
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSpinBox>
#include <QSettings>
#include <QStatusBar>
#include <QStackedWidget>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::~MainWindow()
{
    if (!nvencProbeProcess_) return;
    nvencProbeProcess_->disconnect(this);
    if (nvencProbeProcess_->state() != QProcess::NotRunning) {
        nvencProbeProcess_->kill();
        nvencProbeProcess_->waitForFinished(3000);
    }
    delete nvencProbeProcess_;
    nvencProbeProcess_ = nullptr;
}

MainWindow::MainWindow(QWidget* parent, const QString& settingsFile)
    : QMainWindow(parent)
{
    settings_ = settingsFile.isEmpty()
        ? new QSettings(QSettings::NativeFormat, QSettings::UserScope,
                        QStringLiteral("QtFFmpegStudio"), QStringLiteral("QtFFmpegStudio"), this)
        : new QSettings(settingsFile, QSettings::IniFormat, this);
    QPalette light;
    light.setColor(QPalette::Window,QColor("#F8FAFC")); light.setColor(QPalette::WindowText,QColor("#182235"));
    light.setColor(QPalette::Base,Qt::white); light.setColor(QPalette::Text,QColor("#182235"));
    light.setColor(QPalette::Button,Qt::white); light.setColor(QPalette::ButtonText,QColor("#182235"));
    light.setColor(QPalette::Highlight,QColor("#1677FF")); light.setColor(QPalette::HighlightedText,Qt::white); setPalette(light);
    setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    setWindowTitle(QStringLiteral("QtFFmpegStudio"));
    setWindowIcon(studioApplicationIcon());
    resize(1240, 860);
    setMinimumSize(900, 680);
    setAcceptDrops(true);

    auto* central = new QWidget(this);
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
    outputButton_->setObjectName(QStringLiteral("outputButton"));
    auto* outputRow = new QHBoxLayout;
    outputRow->addWidget(outputPathEdit_);
    outputRow->addWidget(outputButton_);
    singlePathsLayout->addWidget(new QLabel(QStringLiteral("输出文件"), singleFileControls_));
    singlePathsLayout->addLayout(outputRow);
    auto* mediaGroup = new QGroupBox(QStringLiteral("视频信息"), singleFileControls_);
    mediaGroup->setObjectName(QStringLiteral("mediaInfoGroup"));
    auto* mediaLayout = new QVBoxLayout(mediaGroup);
    mediaState_ = new QLabel(QStringLiteral("等待选择视频"), mediaGroup);
    mediaState_->setWordWrap(true);
    mediaState_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);
    mediaState_->setMinimumWidth(90);
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

    videoBackendCombo_ = new QComboBox(optionsGroup_);
    videoBackendCombo_->setObjectName(QStringLiteral("videoBackendCombo"));
    videoBackendCombo_->addItem(QStringLiteral("CPU（软件编码）"), QStringLiteral("software"));
    videoBackendCombo_->addItem(QStringLiteral("GPU（NVIDIA NVENC）"), QStringLiteral("nvenc"));
    videoBackendCombo_->setToolTip(QStringLiteral("GPU 仅加速 H.264 视频编码；解码、旋转、倍速和音频处理仍在 CPU 上执行。"));
    optionsForm->addWidget(new QLabel(QStringLiteral("视频编码设备"), optionsGroup_), 2, 0);
    optionsForm->addWidget(videoBackendCombo_, 2, 1);
    nvencStatusLabel_ = new QLabel(QStringLiteral("NVENC：检测中…"), optionsGroup_);
    nvencStatusLabel_->setObjectName(QStringLiteral("nvencStatusLabel"));
    nvencStatusLabel_->setWordWrap(true);
    nvencStatusLabel_->setToolTip(QStringLiteral("通过独立后端实际初始化 h264_nvenc 编码器进行检测。"));
    optionsForm->addWidget(nvencStatusLabel_, 2, 2);
    nvencRefreshButton_ = new QPushButton(QStringLiteral("重新检测"), optionsGroup_);
    nvencRefreshButton_->setObjectName(QStringLiteral("nvencRefreshButton"));
    optionsForm->addWidget(nvencRefreshButton_, 2, 3);
    qualityCombo_ = new QComboBox(optionsGroup_);
    qualityCombo_->setObjectName(QStringLiteral("qualityCombo"));
    qualityCombo_->addItem(QStringLiteral("原有默认"), QStringLiteral("default"));
    qualityCombo_->addItem(QStringLiteral("高画质"), QStringLiteral("high"));
    qualityCombo_->addItem(QStringLiteral("均衡"), QStringLiteral("balanced"));
    qualityCombo_->addItem(QStringLiteral("小体积"), QStringLiteral("small"));
    qualityCombo_->addItem(QStringLiteral("指定视频码率"), QStringLiteral("bitrate"));
    optionsForm->addWidget(new QLabel(QStringLiteral("画质"), optionsGroup_), 3, 0);
    optionsForm->addWidget(qualityCombo_, 3, 1);
    bitrateSpin_ = new QSpinBox(optionsGroup_);
    bitrateSpin_->setObjectName(QStringLiteral("bitrateSpin"));
    bitrateSpin_->setRange(100, 100000);
    bitrateSpin_->setSingleStep(500);
    bitrateSpin_->setValue(2000);
    bitrateSpin_->setSuffix(QStringLiteral(" kbps"));
    bitrateSpin_->setToolTip(QStringLiteral("视频平均目标码率，实际文件大小还受音频及内容影响。"));
    bitrateSpin_->setEnabled(false);
    optionsForm->addWidget(new QLabel(QStringLiteral("目标视频码率"), optionsGroup_), 3, 2);
    optionsForm->addWidget(bitrateSpin_, 3, 3);
    resolutionCombo_ = new QComboBox(optionsGroup_);
    resolutionCombo_->setObjectName(QStringLiteral("resolutionCombo"));
    resolutionCombo_->addItem(QStringLiteral("保持原始尺寸"), 0);
    resolutionCombo_->addItem(QStringLiteral("720p（短边不超过 720）"), 720);
    resolutionCombo_->addItem(QStringLiteral("1080p（短边不超过 1080）"), 1080);
    resolutionCombo_->setToolTip(QStringLiteral("旋转后按比例缩小，不放大小于目标的画面。"));
    optionsForm->addWidget(new QLabel(QStringLiteral("输出分辨率"), optionsGroup_), 4, 0);
    optionsForm->addWidget(resolutionCombo_, 4, 1);
    frameRateCombo_ = new QComboBox(optionsGroup_);
    frameRateCombo_->setObjectName(QStringLiteral("frameRateCombo"));
    frameRateCombo_->addItem(QStringLiteral("保持原始帧率"), 0);
    for (int fps : {24, 25, 30, 60}) frameRateCombo_->addItem(QStringLiteral("%1 fps").arg(fps), fps);
    optionsForm->addWidget(new QLabel(QStringLiteral("输出帧率"), optionsGroup_), 4, 2);
    optionsForm->addWidget(frameRateCombo_, 4, 3);
    optionsForm->setColumnStretch(1, 1);
    optionsForm->setColumnStretch(3, 1);

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
    optionsPreview_->setMaximumHeight(110);
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
    startRealButton_ = new QPushButton(QStringLiteral("开始转码"), realGroup);
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
    realStateLabel_ = new QLabel(QStringLiteral("选择输入视频并等待分析完成，再设置输出并开始转码。"), realGroup);
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
    auto* resultGroup = new QGroupBox(QStringLiteral("本次转码结果"), realGroup);
    resultGroup->setObjectName(QStringLiteral("realResultGroup"));
    auto* resultLayout = new QVBoxLayout(resultGroup);
    realResultSummary_ = new QLabel(QStringLiteral("完成后显示编码设备、处理速度和耗时分布。"), resultGroup);
    realResultSummary_->setObjectName(QStringLiteral("realResultSummary"));
    realResultSummary_->setTextFormat(Qt::PlainText);
    realResultSummary_->setWordWrap(true);
    resultLayout->addWidget(realResultSummary_);
    realLayout->addWidget(resultGroup);
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
    // Reuse the existing controls and signal endpoints; only their layout changes.
    auto* shell = new QHBoxLayout(central);
    shell->setContentsMargins(0, 0, 0, 0); shell->setSpacing(0);
    auto* sidebar = new QWidget(central); sidebar->setObjectName("sidebar"); sidebar->setFixedWidth(204);
    auto* navLayout = new QVBoxLayout(sidebar); navLayout->setContentsMargins(14,28,14,24); navLayout->setSpacing(10);
    auto* brand = new QLabel(QStringLiteral("QtFFmpegStudio"), sidebar); brand->setObjectName("appTitle");
    navLayout->addWidget(brand); navLayout->addSpacing(24);
    auto* navigation = new QButtonGroup(this); navigation->setExclusive(true);
    QStringList names{QStringLiteral("单文件转码"), QStringLiteral("批量任务"), QStringLiteral("设置")};

    for (int i=0; i<3; ++i) {
        auto* button = new QPushButton(names[i], sidebar); button->setProperty("nav", true);
        button->setObjectName(QStringLiteral("navigation%1").arg(i)); button->setCheckable(true);
        button->setIcon(StudioTheme::icon(i)); button->setIconSize(QSize(22,22));
        navigation->addButton(button, i); navLayout->addWidget(button);
        if (i==1) {
            auto* history = new QPushButton(QStringLiteral("历史记录 · 规划中"), sidebar);
            history->setProperty("nav",true); history->setEnabled(false);
            history->setToolTip(QStringLiteral("当前版本尚未保存历史任务")); navLayout->addWidget(history);
        }
    }
    navigation->button(0)->setChecked(true);
    navLayout->addStretch();
    auto* tagline = new QLabel(QStringLiteral("FFmpeg 视频处理工具\nC++ · Qt Widgets"),sidebar);
    tagline->setProperty("muted",true); navLayout->addWidget(tagline);
    shell->addWidget(sidebar); shell->addWidget(taskPages,1);
    auto* singlePage = new QWidget(taskPages); singlePage->setObjectName("singlePage");
    auto* pageLayout = new QVBoxLayout(singlePage); pageLayout->setContentsMargins(28,24,28,20); pageLayout->setSpacing(16);
    auto* pageTitle = new QLabel(QStringLiteral("视频转码")); pageTitle->setObjectName("pageTitle"); pageLayout->addWidget(pageTitle);
    auto* subtitle = new QLabel(QStringLiteral("选择视频，调整输出参数，开始一次新的转换。")); subtitle->setProperty("muted",true); pageLayout->addWidget(subtitle);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget; auto* columns = new QHBoxLayout(body); columns->setContentsMargins(0,0,0,0); columns->setSpacing(18);
    auto* left = new QWidget; auto* leftLayout = new QVBoxLayout(left); leftLayout->setContentsMargins(0,0,0,0); leftLayout->setSpacing(16);
    // Move media information to its own right-hand card.
    singlePathsLayout->removeWidget(mediaGroup); mediaGroup->setParent(body);
    singlePathsLayout->removeItem(outputRow);
    // Remove the old path labels; captions now belong to the cards/action bar.
    for (int i=singlePathsLayout->count()-1;i>=0;--i) {
        if (auto* label=qobject_cast<QLabel*>(singlePathsLayout->itemAt(i)->widget())) { singlePathsLayout->removeWidget(label); delete label; }
    }
    auto* inputHint = new QLabel(QStringLiteral("选择视频或拖拽至窗口")); inputHint->setAlignment(Qt::AlignCenter); inputHint->setMinimumHeight(74);
    inputHint->setObjectName("inputDropHint"); singlePathsLayout->insertWidget(0,inputHint);
    auto* inputHelp = new QLabel(QStringLiteral("支持带视频和音轨的 MP4 文件")); inputHelp->setProperty("muted",true); inputHelp->setAlignment(Qt::AlignCenter); singlePathsLayout->addWidget(inputHelp);
    connect(inputPathEdit_, &QLineEdit::textChanged, inputHint, [inputHint,inputHelp](const QString& path) {
        inputHint->setText(path.isEmpty()?QStringLiteral("选择视频或拖拽至窗口"):QFileInfo(path).fileName());
        inputHint->setMinimumHeight(path.isEmpty()?74:24); inputHelp->setVisible(path.isEmpty());
    });
    auto* fileCard=new QGroupBox(QStringLiteral("输入视频")); auto* fileCardLayout=new QVBoxLayout(fileCard); fileCardLayout->addWidget(singleFileControls_);
    fileCard->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    optionsGroup_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    leftLayout->addWidget(fileCard); leftLayout->addWidget(optionsGroup_); leftLayout->addStretch();
    optionsGroup_->setTitle(QStringLiteral("输出设置"));
    // Clear old grid items, keeping the actual option widgets.
    while(auto* item=optionsForm->takeAt(0)) { if(qobject_cast<QLabel*>(item->widget()) && item->widget()!=nvencStatusLabel_) delete item->widget(); delete item; }
    auto addOption=[optionsForm](int position,const QString& text,QWidget* control) {
        auto* cell=new QWidget; cell->setProperty("cardContent",true); auto* cellLayout=new QVBoxLayout(cell); cellLayout->setContentsMargins(0,0,0,0); cellLayout->setSpacing(6);
        auto* label=new QLabel(text); label->setProperty("muted",true); cellLayout->addWidget(label); cellLayout->addWidget(control);
        optionsForm->addWidget(cell,position/2,position%2);
    };
    addOption(0,QStringLiteral("视频编码"),videoCodecCombo_); addOption(1,QStringLiteral("编码设备"),videoBackendCombo_);
    addOption(2,QStringLiteral("画质预设"),qualityCombo_); addOption(3,QStringLiteral("音频编码"),audioCodecCombo_);
    addOption(4,QStringLiteral("分辨率"),resolutionCombo_); addOption(5,QStringLiteral("帧率"),frameRateCombo_);
    auto* bitrateRow=new QWidget; auto* bitrateLayout=new QHBoxLayout(bitrateRow); bitrateLayout->setContentsMargins(0,0,0,0);
    bitrateLayout->addWidget(new QLabel(QStringLiteral("视频目标码率"))); bitrateLayout->addWidget(bitrateSpin_);
    optionsForm->addWidget(bitrateRow,3,0,1,2); bitrateRow->hide();
    connect(qualityCombo_,&QComboBox::currentIndexChanged,bitrateRow,[this,bitrateRow]{bitrateRow->setVisible(qualityCombo_->currentData()==QStringLiteral("bitrate"));});
    optionsForm->setColumnStretch(0,1); optionsForm->setColumnStretch(1,1); optionsForm->setColumnStretch(3,0); optionsForm->setVerticalSpacing(12);
    auto* advanced=new QWidget; auto* advancedForm=new QFormLayout(advanced); advancedForm->setContentsMargins(0,0,0,0);
    advancedForm->addRow(QStringLiteral("旋转角度"),rotationCombo_); advancedForm->addRow(QStringLiteral("播放倍速"),speedSpin_);
    optionsForm->addWidget(StudioTheme::disclosure(QStringLiteral("高级设置"),advanced),4,0,1,2);
    videoPreview_=new VideoPreview(mediaGroup); mediaLayout->insertWidget(0,videoPreview_);
    connect(inputPathEdit_,&QLineEdit::textChanged,videoPreview_,&VideoPreview::setFile);
    mediaDetails_->setMinimumWidth(210); mediaDetails_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);
    mediaLayout->addStretch(); mediaGroup->setMinimumWidth(240);
    for(auto* combo:optionsGroup_->findChildren<QComboBox*>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); combo->setMinimumContentsLength(5);
        combo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed); combo->setMinimumWidth(0);
    }
    columns->addWidget(left,3); columns->addWidget(mediaGroup,2); scroll->setWidget(body); pageLayout->addWidget(scroll,1);
    // Keep progress visible; hide verbose result/log tools behind one disclosure.
    auto* details = new QWidget; auto* detailsLayout = new QVBoxLayout(details); detailsLayout->setContentsMargins(0,0,0,0);
    realLayout->removeItem(realLogTools);
    detailsLayout->addWidget(resultGroup); detailsLayout->addLayout(realLogTools); realLog_->setFixedHeight(130); detailsLayout->addWidget(realLog_);
    realLayout->removeWidget(realElapsedLabel_); realButtons->addWidget(realElapsedLabel_);
    realLayout->removeWidget(realStateLabel_); realButtons->insertWidget(0,realStateLabel_,1);
    realGroup->setTitle(QStringLiteral("转码状态"));
    realLayout->setContentsMargins(16,16,16,12); realLayout->setSpacing(6);
    auto* resultDisclosure=StudioTheme::disclosure(QStringLiteral("结果与运行日志"),details);
    realLayout->addWidget(resultDisclosure);
    auto* statusToggle=resultDisclosure->findChild<QPushButton*>();
    resultDisclosure->layout()->removeWidget(statusToggle); realButtons->addWidget(statusToggle);
    pageLayout->addWidget(realGroup);
    realButtons->removeWidget(startRealButton_); realButtons->removeWidget(cancelRealButton_); realButtons->removeWidget(openOutputFolderButton_);
    auto* actions = new QHBoxLayout; actions->addWidget(new QLabel(QStringLiteral("输出路径"))); actions->addLayout(outputRow,1);
    actions->addWidget(startRealButton_); actions->addWidget(cancelRealButton_); pageLayout->addLayout(actions);
    realButtons->addWidget(openOutputFolderButton_); realButtons->addStretch();
    outputButton_->setText(QStringLiteral("浏览…")); inputButton_->setText(QStringLiteral("选择文件…"));
    auto* settingsPage = new QWidget; auto* settingsLayout = new QVBoxLayout(settingsPage); settingsLayout->setContentsMargins(28,24,28,24); settingsLayout->setSpacing(18);
    auto* settingsTitle = new QLabel(QStringLiteral("设置")); settingsTitle->setObjectName("pageTitle"); settingsLayout->addWidget(settingsTitle);
    auto* settingsHelp = new QLabel(QStringLiteral("转码参数和并行数会自动保存；批量任务使用添加时的参数快照。")); settingsHelp->setWordWrap(true); settingsLayout->addWidget(settingsHelp);
    auto* deviceGroup=new QGroupBox(QStringLiteral("硬件编码")); auto* deviceLayout=new QVBoxLayout(deviceGroup); deviceLayout->addWidget(nvencStatusLabel_); deviceLayout->addWidget(nvencRefreshButton_); settingsLayout->addWidget(deviceGroup);
    auto* diagnostic = new QWidget; auto* diagnosticLayout = new QVBoxLayout(diagnostic); diagnosticLayout->addWidget(singleFileTools_); diagnosticLayout->addWidget(simulationGroup);
    auto* editDefaults=new QPushButton(QStringLiteral("编辑默认转码参数")); settingsLayout->addWidget(editDefaults);
    connect(editDefaults,&QPushButton::clicked,this,[taskPages,this]{taskPages->setCurrentIndex(0); videoCodecCombo_->setFocus();});
    settingsLayout->addWidget(StudioTheme::disclosure(QStringLiteral("诊断与模拟工具"),diagnostic)); settingsLayout->addStretch();
    auto* settingsScroll=new QScrollArea; settingsScroll->setWidgetResizable(true); settingsScroll->setWidget(settingsPage);
    taskPages->addWidget(singlePage); taskPages->addWidget(batchWidget_); taskPages->addWidget(settingsScroll);
    connect(navigation, &QButtonGroup::idClicked, taskPages, &QStackedWidget::setCurrentIndex);
    connect(taskPages,&QStackedWidget::currentChanged,this,[navigation](int index){if(auto* button=navigation->button(index)) button->setChecked(true);});
    taskPages->setCurrentIndex(0);
    setCentralWidget(central); central->setObjectName("appShell"); setStyleSheet(StudioTheme::styleSheet());
    statusBar()->showMessage(QStringLiteral("准备就绪"));

    backend_ = new BackendProcess(this);
    nvencProbeProcess_ = new QProcess(this);
    connect(nvencProbeProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray output = nvencProbeProcess_->readAllStandardOutput();
        QString status;
        if (exitStatus == QProcess::NormalExit && exitCode == 0
            && output.contains("QFS_NVENC_CHECK status=available")) {
            status = QStringLiteral("NVENC：可用");
        } else if (output.contains("status=missing_encoder")) {
            status = QStringLiteral("NVENC：不可用（FFmpeg 缺少 h264_nvenc）");
        } else if (output.contains("status=unsupported_options")) {
            status = QStringLiteral("NVENC：不可用（FFmpeg 不支持当前编码参数）");
        } else if (output.contains("status=init_failed")) {
            status = QStringLiteral("NVENC：不可用（显卡、驱动或编码会话无法初始化）");
        } else {
            status = QStringLiteral("NVENC：不可用（检测失败，退出码 %1）").arg(exitCode);
        }
        nvencStatusLabel_->setText(status);
        nvencRefreshButton_->setEnabled(true);
    });
    connect(nvencProbeProcess_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        nvencStatusLabel_->setText(QStringLiteral("NVENC：不可用（无法启动后端程序）"));
        nvencRefreshButton_->setEnabled(true);
    });
    connect(nvencRefreshButton_, &QPushButton::clicked,
            this, &MainWindow::checkNvencAvailability);
    mediaProbe_ = new MediaProbe(this);
    connect(mediaProbe_, &MediaProbe::ready, this,
            [this](const QString& path, const MediaInfo& info) {
        if (path != inputPathEdit_->text()) return;
        analyzedInputPath_ = path;
        analyzedInputSupported_ = info.supported;
        analyzedDurationSeconds_ = info.durationSeconds;
        const QString unknown = QStringLiteral("未知");
        const QString duration = info.durationSeconds > 0
            ? QStringLiteral("%1 秒").arg(QString::number(info.durationSeconds, 'f', 1)) : unknown;
        const QString size = info.fileSizeBytes >= 0
            ? QStringLiteral("%1 MiB").arg(QString::number(info.fileSizeBytes / 1048576.0, 'f', 2)) : unknown;
        const QString dimensions = info.width > 0 && info.height > 0
            ? QStringLiteral("%1 × %2").arg(info.width).arg(info.height) : unknown;
        const QString fps = info.frameRate > 0
            ? QStringLiteral("%1 fps").arg(QString::number(info.frameRate, 'f', 2)) : unknown;
        mediaDetails_->setText(QStringLiteral("文件名    ") + QFileInfo(path).fileName() + QStringLiteral("\n\n文件路径    ") + fontMetrics().elidedText(QDir::toNativeSeparators(path), Qt::ElideMiddle, 235) + QStringLiteral("\n\n") + QStringLiteral("文件大小    %1\n\n容器    %2\n\n时长    %3\n\n分辨率    %5\n\n帧率    %6\n\n视频编码    %4\n\n音频编码    %7\n\n采样率    %8 Hz · %9 声道")
            .arg(size, info.container.isEmpty() ? unknown : info.container, duration,
                 info.videoCodec.isEmpty() ? unknown : info.videoCodec, dimensions, fps,
                 info.hasAudio ? (info.audioCodec.isEmpty() ? unknown : info.audioCodec) : QStringLiteral("无音轨"),
                 info.sampleRate > 0 ? QString::number(info.sampleRate) : unknown,
                 info.channels > 0 ? QString::number(info.channels) : unknown));
        mediaDetails_->setToolTip(QDir::toNativeSeparators(path));
        mediaState_->setText(info.supported ? QStringLiteral("已分析 · 可尝试转码")
            : QStringLiteral("已分析 · 当前转码要求同时包含视频和音频流"));
        if (taskState_ == TaskState::Idle) {
            realStateLabel_->setText(info.supported
                ? QStringLiteral("视频信息已通过；选择输出文件后即可开始转码。")
                : QStringLiteral("当前输入缺少视频或音频流，请选择其他 MP4。"));
        }
        refreshRealStartAvailability();
    });
    connect(mediaProbe_, &MediaProbe::failed, this,
            [this](const QString& path, const QString& error) {
        if (path != inputPathEdit_->text()) return;
        analyzedInputPath_.clear();
        analyzedInputSupported_ = false;
        analyzedDurationSeconds_ = -1.0;
        mediaState_->setText(QStringLiteral("分析失败"));
        mediaDetails_->setText(error);
        if (taskState_ == TaskState::Idle)
            realStateLabel_->setText(QStringLiteral("输入视频分析失败，请检查文件或重新选择。"));
        refreshRealStartAvailability();
    });
    connect(inputPathEdit_, &QLineEdit::textChanged, this, [this](const QString& path) {
        analyzedInputPath_.clear();
        analyzedInputSupported_ = false;
        analyzedDurationSeconds_ = -1.0;
        inputPathEdit_->setToolTip(path);
        mediaRetryButton_->setEnabled(!path.isEmpty());
        mediaState_->setText(path.isEmpty() ? QStringLiteral("等待选择视频") : QStringLiteral("分析中…"));
        mediaDetails_->setText(QStringLiteral("选择 MP4 文件后显示视频信息"));
        if (taskState_ == TaskState::Idle)
            realStateLabel_->setText(path.isEmpty()
                ? QStringLiteral("请选择输入视频。") : QStringLiteral("正在分析输入视频…"));
        refreshRealStartAvailability();
        mediaProbe_->inspect(path);
    });
    connect(mediaRetryButton_, &QPushButton::clicked, this, [this]() {
        analyzedInputPath_.clear();
        analyzedInputSupported_ = false;
        analyzedDurationSeconds_ = -1.0;
        mediaState_->setText(QStringLiteral("分析中…"));
        mediaDetails_->setText(QStringLiteral("正在重新读取视频信息"));
        if (taskState_ == TaskState::Idle)
            realStateLabel_->setText(QStringLiteral("正在重新分析输入视频…"));
        refreshRealStartAvailability();
        mediaProbe_->inspect(inputPathEdit_->text());
    });
    connect(outputPathEdit_, &QLineEdit::textChanged, this, [this](const QString& path) {
        outputPathEdit_->setToolTip(path);
        if (taskState_ == TaskState::Idle && analyzedInputSupported_
            && analyzedInputPath_ == inputPathEdit_->text()) {
            realStateLabel_->setText(path.isEmpty()
                ? QStringLiteral("视频信息已通过；请选择输出文件。")
                : QStringLiteral("输入与输出已选择；可检查路径并开始转码。"));
        }
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
    connect(backend_, &BackendProcess::muxPerformanceReady, this,
            [this](const MuxPerformance& performance) {
        realMuxPerformance_ = performance;
        hasRealMuxPerformance_ = true;
    });
    connect(backend_, &BackendProcess::finished, this,
            [this](bool success, const QString& message) {
        const qint64 elapsedMs = realElapsedTimer_.isValid()
            ? realElapsedTimer_.elapsed() : -1;
        realElapsedLabel_->setText(elapsedMs >= 0
            ? QStringLiteral("耗时：%1").arg(formatElapsed(elapsedMs))
            : QStringLiteral("耗时：—"));
        realElapsedTimer_.invalidate();
        if (success) realTranscodeProgress_->setValue(100);
        realTranscodeProgress_->setFormat(success
            ? QStringLiteral("转码完成：100%")
            : QStringLiteral("转码未完成｜已处理约 %p%"));
        lastSuccessfulOutput_ = success ? outputPathEdit_->text() : QString{};
        if (success) {
            const QString device = activeRealOptions_.videoBackend == QStringLiteral("nvenc")
                ? QStringLiteral("GPU（NVIDIA NVENC）") : QStringLiteral("CPU（软件编码）");
            const qint64 bytes = QFileInfo(activeRealOptions_.outputPath).size();
            QString summary = QStringLiteral("编码设备：%1  ·  总耗时：%2  ·  输出：%3 MiB")
                .arg(device, elapsedMs >= 0 ? formatElapsed(elapsedMs) : QStringLiteral("未知"),
                     QString::number(bytes / 1048576.0, 'f', 2));
            if (activeInputDurationSeconds_ > 0 && elapsedMs > 0) {
                summary += QStringLiteral("\n处理倍率：%1x（输入时长 ÷ 实际耗时）")
                    .arg(QString::number(activeInputDurationSeconds_ * 1000.0 / elapsedMs, 'f', 2));
            }
            summary += hasRealMuxPerformance_
                ? QLatin1Char('\n') + formatMuxPerformance(realMuxPerformance_)
                : QStringLiteral("\nMux 计时未提供；详见日志。");
            realResultSummary_->setText(summary);
        } else {
            realResultSummary_->setText(QStringLiteral("本次转码未完成；详见运行日志。"));
        }
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
    connect(videoBackendCombo_, &QComboBox::currentIndexChanged, this, clearPreview);
    connect(audioCodecCombo_, &QComboBox::currentIndexChanged, this, clearPreview);
    connect(qualityCombo_, &QComboBox::currentIndexChanged, this, [this, clearPreview]() {
        bitrateSpin_->setEnabled(qualityCombo_->currentData() == QStringLiteral("bitrate"));
        clearPreview();
    });
    connect(bitrateSpin_, &QSpinBox::valueChanged, this, clearPreview);
    connect(resolutionCombo_, &QComboBox::currentIndexChanged, this, clearPreview);
    connect(frameRateCombo_, &QComboBox::currentIndexChanged, this, clearPreview);
    connect(videoBackendCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (videoBackendCombo_->currentData() == QStringLiteral("nvenc")
            && videoCodecCombo_->currentData() != QStringLiteral("h264")) {
            videoCodecCombo_->setCurrentIndex(videoCodecCombo_->findData(QStringLiteral("h264")));
        }
    });
    connect(videoCodecCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (videoCodecCombo_->currentData() == QStringLiteral("mpeg4")
            && videoBackendCombo_->currentData() == QStringLiteral("nvenc")) {
            videoBackendCombo_->setCurrentIndex(videoBackendCombo_->findData(QStringLiteral("software")));
        }
    });
    loadSettings();
    setTaskState(TaskState::Idle);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (nvencProbeStarted_) return;
    nvencProbeStarted_ = true;
    checkNvencAvailability();
}

void MainWindow::checkNvencAvailability()
{
    if (nvencProbeProcess_->state() != QProcess::NotRunning) return;
    const QString executable = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("FFmpegProjectBackend.exe"));
    if (!QFileInfo(executable).isFile()) {
        nvencStatusLabel_->setText(QStringLiteral("NVENC：不可用（后端程序缺失）"));
        nvencRefreshButton_->setEnabled(true);
        return;
    }
    nvencStatusLabel_->setText(QStringLiteral("NVENC：检测中…"));
    nvencRefreshButton_->setEnabled(false);
    nvencProbeProcess_->setProgram(executable);
    nvencProbeProcess_->setArguments({QStringLiteral("--check-nvenc")});
    nvencProbeProcess_->start();
}

TranscodeOptions MainWindow::readOptions() const
{
    TranscodeOptions options;
    options.inputPath = inputPathEdit_->text();
    options.outputPath = outputPathEdit_->text();
    options.rotationDegrees = rotationCombo_->currentData().toInt();
    options.speedRatio = speedSpin_->value();
    options.videoCodec = videoCodecCombo_->currentData().toString();
    options.videoBackend = videoBackendCombo_->currentData().toString();
    options.audioCodec = audioCodecCombo_->currentData().toString();
    options.quality = qualityCombo_->currentData().toString();
    options.videoBitrateKbps = bitrateSpin_->value();
    options.maxShortEdge = resolutionCombo_->currentData().toInt();
    options.outputFps = frameRateCombo_->currentData().toInt();
    return options;
}

void MainWindow::showOptionsPreview()
{
    const TranscodeOptions options = readOptions();
    const QString input = options.inputPath.isEmpty()
        ? QStringLiteral("尚未选择") : options.inputPath;
    const QString output = options.outputPath.isEmpty()
        ? QStringLiteral("尚未选择") : options.outputPath;

    const QString quality = qualityCombo_->currentText()
        + (options.quality == QStringLiteral("bitrate")
           ? QStringLiteral("（%1 kbps）").arg(options.videoBitrateKbps) : QString());
    optionsPreview_->setPlainText(
        QStringLiteral("输入：%1\n输出：%2\n旋转：%3°\n倍速：%4x\n视频编码：%5\n视频编码设备：%6\n音频编码：%7\n画质：%8\n分辨率：%9\n帧率：%10")
            .arg(input)
            .arg(output)
            .arg(options.rotationDegrees)
            .arg(QString::number(options.speedRatio, 'f', 2))
            .arg(options.videoCodec)
            .arg(options.videoBackend == QStringLiteral("nvenc")
                ? QStringLiteral("GPU（NVIDIA NVENC）") : QStringLiteral("CPU（软件编码）"))
            .arg(options.audioCodec)
            .arg(quality)
            .arg(resolutionCombo_->currentText())
            .arg(frameRateCombo_->currentText()));
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
        ? QStringLiteral("路径检查通过。目标文件已存在，转码前需确认覆盖。")
        : analyzedInputSupported_ && analyzedInputPath_ == inputPathEdit_->text()
            ? QStringLiteral("路径和视频信息均已通过，可以开始转码。")
            : QStringLiteral("路径检查通过；视频信息还需分析通过并包含音视频流。"));
    statusBar()->showMessage(QStringLiteral("路径检查通过"));
    return true;
}

void MainWindow::setTaskState(TaskState state)
{
    taskState_ = state;
    if(state != TaskState::Idle) videoPreview_->pause();
    const bool idle = state == TaskState::Idle;
    startSimulationButton_->setEnabled(idle);
    stopSimulationButton_->setEnabled(state == TaskState::Simulating);
    refreshRealStartAvailability();
    cancelRealButton_->setEnabled(state == TaskState::Transcoding);
    inputButton_->setEnabled(idle);
    outputButton_->setEnabled(idle);
    checkButton_->setEnabled(idle);
    optionsGroup_->setEnabled(idle);
    openOutputFolderButton_->setEnabled(idle && QFileInfo(lastSuccessfulOutput_).isFile());
    batchWidget_->setExternalAvailable(idle || state == TaskState::BatchRunning
                                      || state == TaskState::BatchCancelling);
}

void MainWindow::refreshRealStartAvailability()
{
    if (!startRealButton_) return;
    mediaRetryButton_->setEnabled(taskState_ == TaskState::Idle && !inputPathEdit_->text().isEmpty());
    const bool ready = analyzedInputSupported_
        && analyzedInputPath_ == inputPathEdit_->text();
    startRealButton_->setEnabled(taskState_ == TaskState::Idle && ready);
    if (taskState_ != TaskState::Idle) {
        startRealButton_->setToolTip(QStringLiteral("请等待当前任务结束。"));
    } else if (ready) {
        startRealButton_->setToolTip(QStringLiteral("视频信息已分析；开始转码前仍会检查路径。"));
    } else if (inputPathEdit_->text().isEmpty()) {
        startRealButton_->setToolTip(QStringLiteral("请先选择 MP4 输入视频。"));
    } else {
        startRealButton_->setToolTip(mediaState_->text() + QStringLiteral("：") + mediaDetails_->text());
    }
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
    if (taskState_ != TaskState::Idle) return;
    if (!analyzedInputSupported_ || analyzedInputPath_ != inputPathEdit_->text()) {
        realStateLabel_->setText(QStringLiteral("请等待当前输入视频分析通过，再开始转码。"));
        return;
    }
    if (!validatePaths()) return;

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
    activeRealOptions_ = options;
    activeInputDurationSeconds_ = analyzedDurationSeconds_;
    hasRealMuxPerformance_ = false;
    realResultSummary_->setText(QStringLiteral("转码中；完成后显示结果与性能摘要。"));
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
    const int backend = videoBackendCombo_->findData(settings_->value(QStringLiteral("options/videoBackend")));
    if (backend >= 0) videoBackendCombo_->setCurrentIndex(backend);
    const int audio = audioCodecCombo_->findData(settings_->value(QStringLiteral("options/audioCodec")));
    if (audio >= 0) audioCodecCombo_->setCurrentIndex(audio);
    const int quality = qualityCombo_->findData(settings_->value(QStringLiteral("options/quality"), QStringLiteral("default")));
    if (quality >= 0) qualityCombo_->setCurrentIndex(quality);
    bitrateSpin_->setValue(settings_->value(QStringLiteral("options/videoBitrateKbps"), 2000).toInt());
    const int resolution = resolutionCombo_->findData(settings_->value(QStringLiteral("options/maxShortEdge"), 0));
    if (resolution >= 0) resolutionCombo_->setCurrentIndex(resolution);
    const int frameRate = frameRateCombo_->findData(settings_->value(QStringLiteral("options/outputFps"), 0));
    if (frameRate >= 0) frameRateCombo_->setCurrentIndex(frameRate);
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
    settings_->setValue(QStringLiteral("options/videoBackend"), options.videoBackend);
    settings_->setValue(QStringLiteral("options/audioCodec"), options.audioCodec);
    settings_->setValue(QStringLiteral("options/quality"), options.quality);
    settings_->setValue(QStringLiteral("options/videoBitrateKbps"), options.videoBitrateKbps);
    settings_->setValue(QStringLiteral("options/maxShortEdge"), options.maxShortEdge);
    settings_->setValue(QStringLiteral("options/outputFps"), options.outputFps);
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

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    const auto urls=event->mimeData()->urls();
    if(taskState_==TaskState::Idle && urls.size()==1 && urls.first().isLocalFile()
       && QFileInfo(urls.first().toLocalFile()).suffix().compare("mp4",Qt::CaseInsensitive)==0)
        event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls=event->mimeData()->urls();
    if(taskState_!=TaskState::Idle || urls.size()!=1 || !urls.first().isLocalFile()) return;
    const QFileInfo file(urls.first().toLocalFile());
    if(!file.isFile() || file.suffix().compare("mp4",Qt::CaseInsensitive)!=0) return;
    inputPathEdit_->setText(QDir::toNativeSeparators(file.absoluteFilePath()));
    event->acceptProposedAction();
}
