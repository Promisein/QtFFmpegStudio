#include "mainwindow.h"

#include <QFile>
#include <QCoreApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QFileInfo>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <windows.h>

class RealTranscodeTest : public QObject
{
    Q_OBJECT

private slots:
    void validMediaProducesFileAndUtf8Log();
    void damagedMediaIsRejectedBeforeBackendStarts();
    void cancellingMarksTaskAsCancelled();
    void cancelRemovesOnlyOwnedTemporaryOutput();
    void backendAcceptsReservedTemporaryOutput();
    void backendEmitsStructuredProgressAndHumanLog();
    void backendRejectsUnownedTemporaryOutput();
    void backendRejectsHardlinkAlias();
    void aacInputToAc3KeepsAudioAndVideoDurationsAligned();
    void backendNvencH264RotatesAndKeepsAudioInSync();
    void backendNvencDrainsDelayedH264InputFrames();
    void backendRejectsNvencForMpeg4();
    void guiGpuSelectionUsesNvencBackend();
    void backendReportsNvencAvailability();
    void backendControlsResolutionAndFrameRate_data();
    void backendControlsResolutionAndFrameRate();
};

void RealTranscodeTest::backendReportsNvencAvailability()
{
    const QString backend = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("FFmpegProjectBackend.exe"));
    QProcess process;
    process.start(backend, {QStringLiteral("--check-nvenc")});
    QVERIFY(process.waitForFinished(15000));
    const QByteArray output = process.readAllStandardOutput();
    QVERIFY2(output.contains("QFS_NVENC_CHECK status="), output.constData());
    const bool available = output.contains("status=available");
    QVERIFY(available || output.contains("status=missing_encoder")
            || output.contains("status=init_failed")
            || output.contains("status=unsupported_options"));
    QCOMPARE(process.exitCode() == 0, available);
}

static bool makeFixture(const QString& target, int duration = 1)
{
    const QStringList arguments{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("testsrc=size=160x120:rate=25:duration=%1").arg(duration),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:sample_rate=44100:duration=%1").arg(duration),
        QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"),
        QStringLiteral("-shortest"), target
    };
    return QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL), arguments) == 0
        && QFileInfo(target).size() > 0;
}

void RealTranscodeTest::backendControlsResolutionAndFrameRate_data()
{
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QString>("encoderBackend");
    QTest::addColumn<QString>("quality");
    QTest::addColumn<int>("fps");
    QTest::addColumn<double>("speed");
    QTest::newRow("cpu-h264-quality") << "h264" << "software" << "high" << 30 << 2.0;
    QTest::newRow("cpu-mpeg4-bitrate") << "mpeg4" << "software" << "bitrate" << 30 << 2.0;
    QTest::newRow("nvenc-bitrate") << "h264" << "nvenc" << "bitrate" << 30 << 2.0;
    QTest::newRow("cpu-duplicate-frames") << "h264" << "software" << "small" << 60 << 0.75;
}

void RealTranscodeTest::backendControlsResolutionAndFrameRate()
{
    QFETCH(QString, codec);
    QFETCH(QString, encoderBackend);
    QFETCH(QString, quality);
    QFETCH(int, fps);
    QFETCH(double, speed);
    if (encoderBackend == QStringLiteral("nvenc") && QProcess::execute("nvidia-smi", {"-L"}) != 0)
        QSKIP("NVIDIA GPU/driver is unavailable");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    const QString output = directory.filePath("output.mp4");
    QCOMPARE(QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL),
        {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc=size=640x480:rate=25:duration=4",
         "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=4",
         "-c:v", "mpeg4", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", input}), 0);
    const QString backend = QDir(QCoreApplication::applicationDirPath()).filePath("FFmpegProjectBackend.exe");
    QProcess process;
    process.start(backend, {"-i", input, "-o", output, "-vcodec", codec, "-vbackend", encoderBackend, "-acodec", "aac",
                            "-rotate", "90", "-speed", QString::number(speed), "-quality", quality, "-bitrate", "800", "-height", "240", "-fps", QString::number(fps)});
    QVERIFY(process.waitForFinished(30000));
    QVERIFY2(process.exitCode() == 0, (process.readAllStandardOutput() + process.readAllStandardError()).constData());
    QProcess probe;
    const QString ffprobe = QFileInfo(QString::fromUtf8(FFMPEG_TEST_TOOL)).dir().filePath("ffprobe.exe");
    probe.start(ffprobe, {"-v", "error", "-show_entries", "stream=codec_type,width,height,r_frame_rate,duration",
                          "-of", "json", output});
    QVERIFY(probe.waitForFinished(10000));
    const auto streams = QJsonDocument::fromJson(probe.readAllStandardOutput()).object().value("streams").toArray();
    double videoDuration = 0, audioDuration = 0;
    for (const auto& item : streams) {
        const auto stream = item.toObject();
        if (stream.value("codec_type") == "video") {
            QCOMPARE(stream.value("width").toInt(), 240);
            QCOMPARE(stream.value("height").toInt(), 320);
            QCOMPARE(stream.value("r_frame_rate").toString(), QStringLiteral("%1/1").arg(fps));
            videoDuration = stream.value("duration").toString().toDouble();
        } else if (stream.value("codec_type") == "audio") audioDuration = stream.value("duration").toString().toDouble();
    }
    QVERIFY(qAbs(videoDuration - 4.0 / speed) < 0.1);
    QVERIFY(qAbs(videoDuration - audioDuration) < 0.2);
}

void RealTranscodeTest::guiGpuSelectionUsesNvencBackend()
{
    if (QProcess::execute(QStringLiteral("nvidia-smi"), {QStringLiteral("-L")}) != 0)
        QSKIP("NVIDIA GPU/driver is unavailable");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("gui-gpu-input.mp4");
    const QString output = directory.filePath("gui-gpu-output.mp4");
    const QString ffmpeg = QString::fromUtf8(FFMPEG_TEST_TOOL);
    QCOMPARE(QProcess::execute(ffmpeg, {"-hide_banner", "-loglevel", "error",
                                        "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25:duration=2",
                                        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=2",
                                        "-c:v", "mpeg4", "-pix_fmt", "yuv420p",
                                        "-c:a", "aac", "-shortest", input}), 0);

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* device = window.findChild<QComboBox*>("videoBackendCombo");
    auto* codec = window.findChild<QComboBox*>("videoCodecCombo");
    auto* start = window.findChild<QPushButton*>("startRealButton");
    auto* state = window.findChild<QLabel*>("realStateLabel");
    auto* log = window.findChild<QPlainTextEdit*>("realLog");
    QVERIFY(device && codec && start && state && log);
    device->setCurrentIndex(device->findData(QStringLiteral("nvenc")));
    QCOMPARE(codec->currentData().toString(), QStringLiteral("h264"));
    window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(output);
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
    start->click();
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("转码完成")), 30000);
    QVERIFY2(log->toPlainText().contains(QStringLiteral("h264_nvenc")),
             qPrintable(log->toPlainText().right(1500)));
    QVERIFY(QFileInfo(output).size() > 0);
    const QString ffprobe = QFileInfo(ffmpeg).dir().filePath("ffprobe.exe");
    QProcess probe;
    probe.start(ffprobe, {"-v", "error", "-select_streams", "v:0",
                          "-show_entries", "stream=codec_name", "-of", "default=nw=1:nk=1", output});
    QVERIFY(probe.waitForFinished(10000));
    QCOMPARE(probe.exitCode(), 0);
    QCOMPARE(QString::fromUtf8(probe.readAllStandardOutput()).trimmed(), QStringLiteral("h264"));
}

void RealTranscodeTest::aacInputToAc3KeepsAudioAndVideoDurationsAligned()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("四秒输入.mp4"));
    const QString output = directory.filePath(QStringLiteral("ac3输出.mp4"));
    QVERIFY(makeFixture(input, 4));

    const QString backend = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("FFmpegProjectBackend.exe"));
    QCOMPARE(QProcess::execute(backend, {QStringLiteral("-i"), input,
                                         QStringLiteral("-o"), output,
                                         QStringLiteral("-vcodec"), QStringLiteral("mpeg4"),
                                         QStringLiteral("-acodec"), QStringLiteral("ac3")}), 0);

    const QString ffprobe = QFileInfo(QString::fromUtf8(FFMPEG_TEST_TOOL)).dir()
        .filePath(QStringLiteral("ffprobe.exe"));
    QProcess probe;
    probe.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-show_entries"), QStringLiteral("stream=codec_type,duration"),
                          QStringLiteral("-of"), QStringLiteral("json"), output});
    QVERIFY(probe.waitForFinished(10000));
    QCOMPARE(probe.exitCode(), 0);
    const QJsonArray streams = QJsonDocument::fromJson(probe.readAllStandardOutput())
        .object().value(QStringLiteral("streams")).toArray();
    double videoDuration = -1;
    double audioDuration = -1;
    for (const QJsonValue& value : streams) {
        const QJsonObject stream = value.toObject();
        const double duration = stream.value(QStringLiteral("duration")).toString().toDouble();
        if (stream.value(QStringLiteral("codec_type")) == QStringLiteral("video")) videoDuration = duration;
        if (stream.value(QStringLiteral("codec_type")) == QStringLiteral("audio")) audioDuration = duration;
    }
    QVERIFY(videoDuration > 3.8 && videoDuration < 4.2);
    QVERIFY2(audioDuration > 3.8 && audioDuration < 4.2,
             qPrintable(QStringLiteral("audio duration=%1 s").arg(audioDuration)));
    QVERIFY(qAbs(videoDuration - audioDuration) < 0.2);
}

void RealTranscodeTest::backendNvencH264RotatesAndKeepsAudioInSync()
{
    if (QProcess::execute(QStringLiteral("nvidia-smi"), {QStringLiteral("-L")}) != 0)
        QSKIP("NVIDIA GPU/driver is unavailable");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("nvenc-input.mp4"));
    const QString output = directory.filePath(QStringLiteral("nvenc-output.mp4"));
    const QString ffmpeg = QString::fromUtf8(FFMPEG_TEST_TOOL);
    QCOMPARE(QProcess::execute(ffmpeg, {"-hide_banner", "-loglevel", "error",
                                        "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25:duration=4",
                                        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=4",
                                        "-c:v", "mpeg4", "-pix_fmt", "yuv420p",
                                        "-c:a", "aac", "-shortest", input}), 0);

    const QString backend = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("FFmpegProjectBackend.exe"));
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(backend, {"-i", input, "-o", output, "-vcodec", "h264",
                            "-vbackend", "nvenc", "-acodec", "aac",
                            "-rotate", "90", "-speed", "2"});
    QVERIFY(process.waitForFinished(30000));
    const QByteArray log = process.readAll();
    QVERIFY2(process.exitCode() == 0, log.right(3000).constData());
    QVERIFY2(log.contains("h264_nvenc"), log.constData());
    QVERIFY(QFileInfo(output).size() > 0);

    const QString ffprobe = QFileInfo(ffmpeg).dir().filePath("ffprobe.exe");
    QProcess probe;
    probe.start(ffprobe, {"-v", "error", "-show_data_hash", "sha256", "-show_entries",
                          "stream=codec_name,codec_type,width,height,duration,extradata_hash:format=duration",
                          "-of", "json", output});
    QVERIFY(probe.waitForFinished(10000));
    QCOMPARE(probe.exitCode(), 0);
    const QJsonArray streams = QJsonDocument::fromJson(probe.readAllStandardOutput())
        .object().value("streams").toArray();
    QCOMPARE(streams.size(), 2);
    QJsonObject video;
    QJsonObject audio;
    for (const QJsonValue& value : streams) {
        const QJsonObject stream = value.toObject();
        if (stream.value("codec_type") == "video") video = stream;
        if (stream.value("codec_type") == "audio") audio = stream;
    }
    QCOMPARE(video.value("codec_name").toString(), QStringLiteral("h264"));
    QCOMPARE(video.value("width").toInt(), 240);
    QCOMPARE(video.value("height").toInt(), 320);
    QVERIFY(video.value("extradata_hash").toString().startsWith("SHA256:"));
    QCOMPARE(audio.value("codec_name").toString(), QStringLiteral("aac"));
    QVERIFY(audio.value("extradata_hash").toString().startsWith("SHA256:"));
    const double videoSeconds = video.value("duration").toString().toDouble();
    const double audioSeconds = audio.value("duration").toString().toDouble();
    QVERIFY(videoSeconds > 1.8 && videoSeconds < 2.2);
    QVERIFY(qAbs(videoSeconds - audioSeconds) < 0.2);
    QCOMPARE(QProcess::execute(ffmpeg, {"-v", "error", "-xerror", "-i", output,
                                        "-f", "null", "-"}), 0);
}

void RealTranscodeTest::backendNvencDrainsDelayedH264InputFrames()
{
    if (QProcess::execute(QStringLiteral("nvidia-smi"), {QStringLiteral("-L")}) != 0)
        QSKIP("NVIDIA GPU/driver is unavailable");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("h264-with-bframes.mp4");
    const QString output = directory.filePath("nvenc-drained.mp4");
    const QString ffmpeg = QString::fromUtf8(FFMPEG_TEST_TOOL);
    QCOMPARE(QProcess::execute(ffmpeg, {"-hide_banner", "-loglevel", "error",
                                        "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25:duration=3",
                                        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=3",
                                        "-c:v", "libx264", "-bf", "2", "-pix_fmt", "yuv420p",
                                        "-c:a", "aac", "-shortest", input}), 0);

    const QString backend = QDir(QCoreApplication::applicationDirPath())
        .filePath("FFmpegProjectBackend.exe");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(backend, {"-i", input, "-o", output,
                            "-vcodec", "h264", "-vbackend", "nvenc", "-acodec", "aac"});
    QVERIFY(process.waitForFinished(30000));
    QVERIFY2(process.exitCode() == 0, process.readAll().right(1500).constData());

    const QString ffprobe = QFileInfo(ffmpeg).dir().filePath("ffprobe.exe");
    auto inspect = [&](const QString& path) {
        QProcess probe;
        probe.start(ffprobe, {"-v", "error", "-show_entries",
                              "stream=codec_type,nb_frames,duration", "-of", "json", path});
        if (!probe.waitForFinished(10000) || probe.exitCode() != 0) return QJsonArray{};
        return QJsonDocument::fromJson(probe.readAllStandardOutput())
            .object().value("streams").toArray();
    };
    const QJsonArray inputStreams = inspect(input);
    const QJsonArray outputStreams = inspect(output);
    QCOMPARE(inputStreams.size(), 2);
    QCOMPARE(outputStreams.size(), 2);
    auto videoFrameCount = [](const QJsonArray& streams) {
        for (const QJsonValue& value : streams) {
            if (value.toObject().value("codec_type") == "video")
                return value.toObject().value("nb_frames").toString().toInt();
        }
        return 0;
    };
    QCOMPARE(videoFrameCount(inputStreams), 75);
    QCOMPARE(videoFrameCount(outputStreams), 75);
}

void RealTranscodeTest::backendRejectsNvencForMpeg4()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString output = directory.filePath("unsupported.mp4");
    const QString backend = QDir(QCoreApplication::applicationDirPath())
        .filePath("FFmpegProjectBackend.exe");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(backend, {"-i", directory.filePath("unused.mp4"), "-o", output,
                            "-vcodec", "mpeg4", "-vbackend", "nvenc"});
    QVERIFY(process.waitForFinished(10000));
    QCOMPARE(process.exitCode(), 2);
    QVERIFY(!QFileInfo::exists(output));
}

void RealTranscodeTest::validMediaProducesFileAndUtf8Log()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("中文 空格 输入.mp4"));
    const QString output = directory.filePath(QStringLiteral("输出 空格.mp4"));
    QVERIFY2(makeFixture(input), "Could not create a short video/audio fixture");

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* start = window.findChild<QPushButton*>("startRealButton");
    auto* cancel = window.findChild<QPushButton*>("cancelRealButton");
    auto* openFolder = window.findChild<QPushButton*>("openOutputFolderButton");
    auto* state = window.findChild<QLabel*>("realStateLabel");
    auto* elapsed = window.findChild<QLabel*>("realElapsedLabel");
    auto* resultSummary = window.findChild<QLabel*>("realResultSummary");
    auto* log = window.findChild<QPlainTextEdit*>("realLog");
    auto* progress = window.findChild<QProgressBar*>("realTranscodeProgress");
    auto* copyLog = window.findChild<QPushButton*>("realCopyLogButton");
    auto* clearLog = window.findChild<QPushButton*>("realClearLogButton");
    QVERIFY(start && cancel && openFolder && state && elapsed && resultSummary
            && log && progress && copyLog && clearLog);
    QCOMPARE(elapsed->text(), QStringLiteral("耗时：—"));
    QVERIFY(resultSummary->text().contains(QStringLiteral("完成后")));
    QVERIFY(!copyLog->isEnabled());
    QVERIFY(!clearLog->isEnabled());
    window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(output);
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);

    bool prematureCompletion = false;
    bool sawEstimatedProgress = false;
    QObject::connect(progress, &QProgressBar::valueChanged, &window, [&](int value) {
        if (value > 0 && value < 100) sawEstimatedProgress = true;
        if (value == 100 && !QFileInfo(output).isFile()) prematureCompletion = true;
    });
    QSignalSpy progressValues(progress, &QProgressBar::valueChanged);
    start->click();
    QCOMPARE(elapsed->text(), QStringLiteral("耗时：计时中"));
    QVERIFY(!start->isEnabled());
    QVERIFY(cancel->isEnabled());
    bool responsive = false;
    QTimer::singleShot(0, &window, [&] { responsive = true; });
    QTRY_VERIFY_WITH_TIMEOUT(responsive, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("转码完成")), 15000);
    QVERIFY(start->isEnabled());
    QVERIFY(!cancel->isEnabled());
    QVERIFY(openFolder->isEnabled());
    QCOMPARE(progress->value(), 100);
    QVERIFY(elapsed->text().startsWith(QStringLiteral("耗时：")));
    QVERIFY(elapsed->text().contains(QStringLiteral("秒")));
    QVERIFY(resultSummary->text().contains(QStringLiteral("CPU")));
    QVERIFY(resultSummary->text().contains(QStringLiteral("处理倍率")));
    QVERIFY(resultSummary->text().contains(QStringLiteral("固定休眠")));
    QVERIFY(resultSummary->text().contains(QStringLiteral("写包")));
    QVERIFY(!progressValues.isEmpty());
    QVERIFY(sawEstimatedProgress);
    QVERIFY(!prematureCompletion);
    QVERIFY(QFileInfo(output).size() > 0);
    QVERIFY(log->toPlainText().contains(QStringLiteral("中文 空格 输入.mp4")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("Demux 进度")));
    QVERIFY(!log->toPlainText().contains(QStringLiteral("QFS_PROGRESS")));
    QVERIFY(!log->toPlainText().contains(QChar::ReplacementCharacter));
    QVERIFY(copyLog->isEnabled());
    QVERIFY(clearLog->isEnabled());
    copyLog->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), log->toPlainText());
    clearLog->click();
    QVERIFY(log->toPlainText().isEmpty());
    QVERIFY(!copyLog->isEnabled());
    QVERIFY(!clearLog->isEnabled());
    window.findChild<QLineEdit*>("outputPathEdit")->setText(directory.filePath("other.mp4"));
    QVERIFY(!openFolder->isEnabled());
    start->click();
    QCOMPARE(elapsed->text(), QStringLiteral("耗时：计时中"));
    QVERIFY(resultSummary->text().contains(QStringLiteral("转码中")));
    QVERIFY(!resultSummary->text().contains(QStringLiteral("固定休眠")));
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("转码完成")), 15000);
    QVERIFY(resultSummary->text().contains(QStringLiteral("固定休眠")));
    QVERIFY(elapsed->text().contains(QStringLiteral("秒")));
    QVERIFY(QFileInfo(directory.filePath("other.mp4")).size() > 0);
}

void RealTranscodeTest::damagedMediaIsRejectedBeforeBackendStarts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("损坏输入.mp4"));
    const QString output = directory.filePath(QStringLiteral("不应生成.mp4"));
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a playable video");
    file.close();

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* start = window.findChild<QPushButton*>("startRealButton");
    auto* cancel = window.findChild<QPushButton*>("cancelRealButton");
    auto* state = window.findChild<QLabel*>("realStateLabel");
    auto* elapsed = window.findChild<QLabel*>("realElapsedLabel");
    auto* log = window.findChild<QPlainTextEdit*>("realLog");
    auto* progress = window.findChild<QProgressBar*>("realTranscodeProgress");
    auto* mediaState = window.findChild<QLabel*>("mediaState");
    QVERIFY(start && cancel && state && elapsed && log && progress && mediaState);
    window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(output);
    QTRY_COMPARE_WITH_TIMEOUT(mediaState->text(), QStringLiteral("分析失败"), 10000);
    QVERIFY(!start->isEnabled());
    start->click();
    QVERIFY(!cancel->isEnabled());
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(QDir(directory.path()).entryList({"QFS-*.tmp"}, QDir::Files).isEmpty());
    QVERIFY(progress->value() < 100);
    QCOMPARE(elapsed->text(), QStringLiteral("耗时：—"));
    QVERIFY(log->toPlainText().isEmpty());

    const QString backend = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("FFmpegProjectBackend.exe"));
    QProcess process;
    process.start(backend, {QStringLiteral("-i"), input, QStringLiteral("-o"), output});
    QVERIFY(process.waitForFinished(10000));
    QVERIFY(process.exitCode() != 0);
    QVERIFY(!QFileInfo::exists(output));
}

void RealTranscodeTest::cancellingMarksTaskAsCancelled()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("取消输入.mp4"));
    const QString output = directory.filePath(QStringLiteral("取消输出.mp4"));
    QVERIFY(makeFixture(input));

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* start = window.findChild<QPushButton*>("startRealButton");
    auto* cancel = window.findChild<QPushButton*>("cancelRealButton");
    auto* state = window.findChild<QLabel*>("realStateLabel");
    QVERIFY(start && cancel && state);
    window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(output);
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);

    start->click();
    QVERIFY(cancel->isEnabled());
    cancel->click();
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("转码已取消")), 8000);
    QVERIFY(start->isEnabled());
    QVERIFY(!cancel->isEnabled());
}

void RealTranscodeTest::cancelRemovesOnlyOwnedTemporaryOutput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("long.mp4");
    const QString output = directory.filePath("cancelled.mp4");
    const QString foreign = directory.filePath("QFS-foreign.tmp");
    QVERIFY(makeFixture(input, 8));
    QFile other(foreign);
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("keep this file");
    other.close();

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* start = window.findChild<QPushButton*>("startRealButton");
    auto* cancel = window.findChild<QPushButton*>("cancelRealButton");
    auto* state = window.findChild<QLabel*>("realStateLabel");
    QVERIFY(start && cancel && state);
    window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(output);
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
    start->click();
    auto owned = [&]() {
        QStringList paths = QDir(directory.path()).entryList({"QFS-*.tmp"}, QDir::Files);
        paths.removeAll("QFS-foreign.tmp");
        return paths;
    };
    QTRY_VERIFY_WITH_TIMEOUT(!owned().isEmpty(), 5000);
    cancel->click();
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("转码已取消")), 8000);
    QVERIFY(owned().isEmpty());
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(other.open(QIODevice::ReadOnly));
    QCOMPARE(other.readAll(), QByteArray("keep this file"));
}

void RealTranscodeTest::backendAcceptsReservedTemporaryOutput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    const QString output = directory.filePath("output.mp4");
    const QString temporary = directory.filePath("QFS-reserved.tmp");
    QVERIFY(makeFixture(input));
    QFile reservation(temporary);
    QVERIFY(reservation.open(QIODevice::WriteOnly));
    reservation.close();
    const QString backend = QCoreApplication::applicationDirPath()
        + QStringLiteral("/FFmpegProjectBackend.exe");
    QCOMPARE(QProcess::execute(backend, {"-i", input, "-o", output,
                                        "--temp-output", temporary}), 0);
    QVERIFY(QFileInfo(output).size() > 0);
    QVERIFY(!QFileInfo::exists(temporary));
}

void RealTranscodeTest::backendEmitsStructuredProgressAndHumanLog()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    const QString output = directory.filePath("output.mp4");
    QVERIFY(makeFixture(input));
    QProcess backend;
    backend.start(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/FFmpegProjectBackend.exe"),
                  {"-i", input, "-o", output});
    QVERIFY(backend.waitForFinished(15000));
    QCOMPARE(backend.exitCode(), 0);
    const QByteArray stdoutText = backend.readAllStandardOutput();
    QVERIFY(stdoutText.contains("Demux "));
    QVERIFY(stdoutText.contains("QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\","));
    QVERIFY(!stdoutText.contains("\"stage\":\"demux\""));
    QVERIFY(!stdoutText.contains("\"stage\":\"transcode\",\"percent\":100}"));
    const int muxStarted = stdoutText.indexOf("Mux 开始写入数据包");
    const int progressStarted = stdoutText.indexOf("QFS_PROGRESS ");
    QVERIFY(muxStarted >= 0 && progressStarted > muxStarted);
    const QString outputLog = QString::fromUtf8(stdoutText);
    QVERIFY(outputLog.contains(QStringLiteral("Mux 性能统计:")));
    auto measuredMilliseconds = [&](const QString& name) {
        const QRegularExpression expression(
            QRegularExpression::escape(name) + QStringLiteral("=(\\d+(?:\\.\\d+)?)"));
        const auto match = expression.match(outputLog);
        return match.hasMatch() ? match.captured(1).toDouble() : -1.0;
    };
    const double total = measuredMilliseconds(QStringLiteral("total_ms"));
    QVERIFY(total > 0);
    double accounted = 0;
    for (const QString& name : {QStringLiteral("params_wait_ms"),
                                QStringLiteral("open_ms"),
                                QStringLiteral("header_ms"),
                                QStringLiteral("video_pop_ms"),
                                QStringLiteral("audio_pop_ms"),
                                QStringLiteral("write_ms"),
                                QStringLiteral("trailer_ms"),
                                QStringLiteral("sleep_ms"),
                                QStringLiteral("cleanup_ms")}) {
        const double value = measuredMilliseconds(name);
        QVERIFY2(value >= 0, qPrintable(name));
        accounted += value;
    }
    QVERIFY(measuredMilliseconds(QStringLiteral("write_ms")) > 0);
    QVERIFY(accounted <= total + 0.01);
    QVERIFY(QFileInfo(output).size() > 0);
}

void RealTranscodeTest::backendRejectsUnownedTemporaryOutput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    const QString outputDirectory = directory.filePath("out");
    QVERIFY(QDir().mkpath(outputDirectory));
    const QString output = QDir(outputDirectory).filePath("output.mp4");
    const QString foreign = directory.filePath("QFS-foreign.tmp");
    QVERIFY(makeFixture(input));
    QFile other(foreign);
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.close();
    const QString backend = QCoreApplication::applicationDirPath()
        + QStringLiteral("/FFmpegProjectBackend.exe");
    QCOMPARE(QProcess::execute(backend, {"-i", input, "-o", output,
                                        "--temp-output", foreign}), 2);
    QVERIFY(QFileInfo::exists(foreign));
    QVERIFY(!QFileInfo::exists(output));
}

void RealTranscodeTest::backendRejectsHardlinkAlias()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath(QStringLiteral("原始.mp4"));
    const QString alias = directory.filePath(QStringLiteral("同文件别名.mp4"));
    QVERIFY(makeFixture(input));
    QVERIFY(CreateHardLinkW(alias.toStdWString().c_str(),
                            input.toStdWString().c_str(), nullptr));
    QFile source(input);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray before = source.readAll();
    source.close();

    const QString backend = QCoreApplication::applicationDirPath()
        + QStringLiteral("/FFmpegProjectBackend.exe");
    const int exitCode = QProcess::execute(backend, {
        QStringLiteral("-i"), input, QStringLiteral("-o"), alias,
        QStringLiteral("--overwrite")
    });
    QCOMPARE(exitCode, 2);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), before);
}

QTEST_MAIN(RealTranscodeTest)
#include "test_real_transcode.moc"
