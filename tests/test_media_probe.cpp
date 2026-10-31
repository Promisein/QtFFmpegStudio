#include "mediaprobe.h"
#include "mainwindow.h"

#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>
#include <QMediaPlayer>
#include <QSlider>

class MediaProbeTest : public QObject
{
    Q_OBJECT
private slots:
    void embeddedPreviewFollowsInputAndPlays();
    void parsesTheStreamsUsedByTheBackend();
    void distinguishesMissingAudioAndMalformedJson();
    void ignoresSupersededInput();
    void mainWindowShowsAndClearsMediaInfo();
    void singleStartRequiresCurrentSupportedMedia();
};

void MediaProbeTest::parsesTheStreamsUsedByTheBackend()
{
    const QByteArray json = R"({"format":{"format_name":"mov,mp4,m4a,3gp,3g2,mj2","duration":"4.5"},"streams":[{"index":0,"codec_type":"video","codec_name":"mpeg4","width":320,"height":180,"avg_frame_rate":"25/1"},{"index":1,"codec_type":"audio","codec_name":"aac","sample_rate":"48000","channels":2},{"index":2,"codec_type":"video","codec_name":"h264","width":640,"height":360,"avg_frame_rate":"30/1"}]})";
    MediaInfo info;
    QString error;
    QVERIFY(MediaProbe::parseJson(json, &info, &error));
    QCOMPARE(info.container, QStringLiteral("MP4"));
    QCOMPARE(info.durationSeconds, 4.5);
    QCOMPARE(info.videoCodec, QStringLiteral("h264"));
    QCOMPARE(info.width, 640);
    QCOMPARE(info.height, 360);
    QCOMPARE(info.frameRate, 30.0);
    QCOMPARE(info.audioCodec, QStringLiteral("aac"));
    QVERIFY(info.supported);
}

void MediaProbeTest::distinguishesMissingAudioAndMalformedJson()
{
    MediaInfo info;
    QString error;
    QVERIFY(MediaProbe::parseJson(R"({"format":{"format_name":"mov,mp4","duration":"N/A"},"streams":[{"codec_type":"video","codec_name":"h264","width":100,"height":100,"avg_frame_rate":"0/0"}]})", &info, &error));
    QVERIFY(info.hasVideo);
    QVERIFY(!info.hasAudio);
    QVERIFY(!info.supported);
    QCOMPARE(info.durationSeconds, -1.0);
    QCOMPARE(info.frameRate, -1.0);
    QVERIFY(!MediaProbe::parseJson("broken", &info, &error));
    QVERIFY(!error.isEmpty());
}

void MediaProbeTest::ignoresSupersededInput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath(QStringLiteral("输入 视频.mp4"));
    const QString ffmpeg = QString::fromUtf8(FFMPEG_TEST_TOOL);
    QCOMPARE(QProcess::execute(ffmpeg, {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                                "testsrc2=size=160x120:rate=25:duration=1", "-f", "lavfi", "-i",
                                "sine=sample_rate=48000:duration=1", "-c:v", "mpeg4", "-c:a", "aac",
                                "-shortest", input}), 0);
    MediaProbe probe;
    QStringList readyPaths;
    QStringList failedPaths;
    connect(&probe, &MediaProbe::ready, &probe, [&](const QString& path, const MediaInfo&) {
        readyPaths.append(path);
    });
    connect(&probe, &MediaProbe::failed, &probe, [&](const QString& path, const QString&) {
        failedPaths.append(path);
    });
    probe.inspect(input);
    QTRY_COMPARE_WITH_TIMEOUT(readyPaths.size(), 1, 10000);
    QCOMPARE(readyPaths.first(), input);
    probe.inspect(input);
    const QString missing = dir.filePath("missing.mp4");
    probe.inspect(missing);
    QTRY_COMPARE_WITH_TIMEOUT(failedPaths.size(), 1, 10000);
    QCOMPARE(failedPaths.first(), missing);
    QCOMPARE(readyPaths.size(), 1);
}

void MediaProbeTest::mainWindowShowsAndClearsMediaInfo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath("source.mp4");
    QCOMPARE(QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL),
            {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
             "testsrc2=size=160x120:rate=25:duration=1", "-f", "lavfi", "-i",
             "sine=sample_rate=48000:duration=1", "-c:v", "mpeg4", "-c:a", "aac",
             "-shortest", input}), 0);
    MainWindow window(nullptr, dir.filePath("settings.ini"));
    window.show();
    auto* path = window.findChild<QLineEdit*>("inputPathEdit");
    auto* details = window.findChild<QLabel*>("mediaDetails");
    auto* state = window.findChild<QLabel*>("mediaState");
    QVERIFY(path && details && state);
    path->setText(input);
    QTRY_VERIFY_WITH_TIMEOUT(details->text().contains(QStringLiteral("160 × 120")), 10000);
    QVERIFY(state->text().contains(QStringLiteral("可尝试转码")));
    auto* retry = window.findChild<QPushButton*>("mediaRetryButton");
    QVERIFY(retry);
    if (qEnvironmentVariableIsSet("QTFFMPEG_SINGLE_SCREENSHOT")) {
        window.findChild<QLineEdit*>("outputPathEdit")
            ->setText(dir.filePath(QStringLiteral("转码结果.mp4")));
        QVERIFY(QMetaObject::invokeMethod(&window, "checkPaths"));
        QVERIFY(window.grab().save(qEnvironmentVariable("QTFFMPEG_SINGLE_SCREENSHOT")));
    }
    path->clear();
    QCOMPARE(details->text(), QStringLiteral("选择 MP4 文件后显示视频信息"));
    QVERIFY(!retry->isEnabled());
    path->setText(input);
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("可尝试转码")), 10000);
    // Release the player before replacing a currently selected file on Windows.
    window.findChild<QMediaPlayer*>("previewPlayer")->setSource(QUrl());
    QVERIFY(QFile::remove(input));
    QFile damaged(input);
    QVERIFY(damaged.open(QIODevice::WriteOnly));
    damaged.write("damaged");
    damaged.close();
    retry->click();
    QTRY_COMPARE_WITH_TIMEOUT(state->text(), QStringLiteral("分析失败"), 10000);
}

void MediaProbeTest::singleStartRequiresCurrentSupportedMedia()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString valid = dir.filePath(QStringLiteral("有效 输入.mp4"));
    const QString silent = dir.filePath(QStringLiteral("无音轨.mp4"));
    const QString damaged = dir.filePath(QStringLiteral("损坏.mp4"));
    const QString ffmpeg = QString::fromUtf8(FFMPEG_TEST_TOOL);
    QCOMPARE(QProcess::execute(ffmpeg,
        {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
         "testsrc2=size=160x120:rate=25:duration=1", "-f", "lavfi", "-i",
         "sine=sample_rate=48000:duration=1", "-c:v", "mpeg4", "-c:a", "aac",
         "-shortest", valid}), 0);
    QCOMPARE(QProcess::execute(ffmpeg,
        {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
         "testsrc2=size=160x120:rate=25:duration=1", "-c:v", "mpeg4", silent}), 0);
    QFile bad(damaged);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("not media");
    bad.close();

    MainWindow window(nullptr, dir.filePath("settings.ini"));
    window.show();
    auto* input = window.findChild<QLineEdit*>("inputPathEdit");
    auto* start = window.findChild<QPushButton*>("startRealButton");
    auto* state = window.findChild<QLabel*>("mediaState");
    auto* retry = window.findChild<QPushButton*>("mediaRetryButton");
    QVERIFY(input && start && state && retry);
    QVERIFY(!start->isEnabled());

    input->setText(valid);
    QVERIFY(!start->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);

    input->setText(silent);
    QVERIFY(!start->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(state->text().contains(QStringLiteral("要求同时包含视频和音频")), 10000);
    QVERIFY(!start->isEnabled());

    input->setText(damaged);
    QVERIFY(!start->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(state->text(), QStringLiteral("分析失败"), 10000);
    QVERIFY(!start->isEnabled());

    input->setText(valid);
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
    retry->click();
    QVERIFY(!start->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
    input->clear();
    QVERIFY(!start->isEnabled());
}

void MediaProbeTest::embeddedPreviewFollowsInputAndPlays()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString input=dir.filePath(QStringLiteral("预览 视频.mp4"));
    QCOMPARE(QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL),
        {"-hide_banner","-loglevel","error","-f","lavfi","-i","testsrc2=size=160x120:rate=25:duration=4","-f","lavfi","-i","sine=sample_rate=48000:duration=4","-c:v","mpeg4","-c:a","aac","-shortest",input}),0);
    MainWindow window(nullptr,dir.filePath("settings.ini")); window.show();
    auto* preview=window.findChild<QWidget*>("videoPreview"); QVERIFY(preview);
    auto* play=preview->findChild<QPushButton*>("previewPlayButton"); QVERIFY(play); QVERIFY(!play->isEnabled());
    auto* path=window.findChild<QLineEdit*>("inputPathEdit"); path->setText(input);
    QTRY_VERIFY_WITH_TIMEOUT(play->isEnabled(),10000);
    // A real decoded first frame enables playback, without continuing to autoplay.
    QTRY_COMPARE_WITH_TIMEOUT(play->text(),QStringLiteral("播放"),10000);
    play->click(); QTRY_COMPARE_WITH_TIMEOUT(play->text(),QStringLiteral("暂停"),5000);
    auto* player=preview->findChild<QMediaPlayer*>("previewPlayer"); QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->position()>0,5000);
    auto* seek=preview->findChild<QSlider*>("previewSeekSlider"); QVERIFY(seek); QTRY_VERIFY(seek->isEnabled());
    seek->setValue(500); QVERIFY(QMetaObject::invokeMethod(seek,"sliderReleased",Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(player->position()>=1900,5000);
    QTest::qWait(180); play->click(); QTRY_COMPARE(play->text(),QStringLiteral("播放"));
    play->click(); window.findChild<QPushButton*>("navigation1")->click(); QTRY_COMPARE(play->text(),QStringLiteral("播放"));
    window.findChild<QPushButton*>("navigation0")->click();
    if(qEnvironmentVariableIsSet("QTFFMPEG_PREVIEW_SCREENSHOT")) QVERIFY(window.grab().save(qEnvironmentVariable("QTFFMPEG_PREVIEW_SCREENSHOT")));
    path->clear(); QVERIFY(!play->isEnabled());
    const QString broken=dir.filePath("broken.mp4"); QFile bad(broken); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("invalid video"); bad.close();
    path->setText(broken);
    auto* status=preview->findChild<QLabel*>("previewStatus"); QVERIFY(status);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("无法预览")),10000);
    QVERIFY(!play->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QLabel*>("mediaState")->text()==QStringLiteral("分析失败"),10000);
}

QTEST_MAIN(MediaProbeTest)
#include "test_media_probe.moc"
