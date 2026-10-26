#include "mainwindow.h"

#include <QFile>
#include <QCoreApplication>
#include <QClipboard>
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
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <windows.h>

class RealTranscodeTest : public QObject
{
    Q_OBJECT

private slots:
    void validMediaProducesFileAndUtf8Log();
    void damagedMediaReportsFailureAndRecoversControls();
    void cancellingMarksTaskAsCancelled();
    void cancelRemovesOnlyOwnedTemporaryOutput();
    void backendAcceptsReservedTemporaryOutput();
    void backendEmitsStructuredProgressAndHumanLog();
    void backendRejectsUnownedTemporaryOutput();
    void backendRejectsHardlinkAlias();
    void aacInputToAc3KeepsAudioAndVideoDurationsAligned();
};

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
    auto* log = window.findChild<QPlainTextEdit*>("realLog");
    auto* progress = window.findChild<QProgressBar*>("realTranscodeProgress");
    auto* copyLog = window.findChild<QPushButton*>("realCopyLogButton");
    auto* clearLog = window.findChild<QPushButton*>("realClearLogButton");
    QVERIFY(start && cancel && openFolder && state && elapsed && log && progress && copyLog && clearLog);
    QCOMPARE(elapsed->text(), QStringLiteral("耗时：—"));
    QVERIFY(!copyLog->isEnabled());
    QVERIFY(!clearLog->isEnabled());
    window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(output);

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
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("转码完成")), 15000);
    QVERIFY(elapsed->text().contains(QStringLiteral("秒")));
    QVERIFY(QFileInfo(directory.filePath("other.mp4")).size() > 0);
}

void RealTranscodeTest::damagedMediaReportsFailureAndRecoversControls()
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
    QVERIFY(start && cancel && state && elapsed && log && progress);
    window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(output);

    start->click();
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("失败")), 10000);
    QVERIFY(start->isEnabled());
    QVERIFY(!cancel->isEnabled());
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(QDir(directory.path()).entryList({"QFS-*.tmp"}, QDir::Files).isEmpty());
    QVERIFY(progress->value() < 100);
    QVERIFY(elapsed->text().contains(QStringLiteral("秒")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("打开输入文件失败")));
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
