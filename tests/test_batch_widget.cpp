#include "batchwidget.h"

#include <QFile>
#include <QDir>
#include <QComboBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QFileInfo>
#include <QProcess>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTableView>
#include <QtTest>

class BatchWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void processesSequentiallyAndContinuesAfterFailure();
    void refusesOutputCreatedAfterEnqueue();
    void cancellationStopsCurrentAndPendingTasks();
    void runsTwoAtOnceAndKeepsResultsSeparate();
    void stopsThreeRunningTasksWithoutStartingPending();
};

static bool makeMedia(const QString& path, int duration = 1)
{
    const QStringList args{
        "-hide_banner", "-loglevel", "error",
        "-f", "lavfi", "-i", QStringLiteral("testsrc=size=160x120:rate=25:duration=%1").arg(duration),
        "-f", "lavfi", "-i", QStringLiteral("sine=frequency=440:sample_rate=44100:duration=%1").arg(duration),
        "-c:v", "mpeg4", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", path
    };
    return QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL), args) == 0
        && QFileInfo(path).size() > 0;
}

static bool hasAudioAndVideo(const QString& path)
{
    const QString ffprobe = QFileInfo(QString::fromUtf8(FFMPEG_TEST_TOOL)).dir()
        .filePath("ffprobe.exe");
    QProcess process;
    process.start(ffprobe, {"-v", "error", "-show_entries", "stream=codec_type",
                            "-of", "csv=p=0", path});
    if (!process.waitForFinished(10000) || process.exitCode() != 0) return false;
    const QByteArray streams = process.readAllStandardOutput();
    return streams.contains("video") && streams.contains("audio");
}

void BatchWidgetTest::processesSequentiallyAndContinuesAfterFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("中文 一.mp4"));
    const QString broken = directory.filePath("bad.mp4");
    const QString third = directory.filePath("third.mp4");
    QVERIFY(makeMedia(first));
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("broken media");
    file.close();
    QVERIFY(makeMedia(third));

    BatchWidget widget(nullptr, [] { return TranscodeOptions{}; });
    auto* table = widget.findChild<QTableView*>(QStringLiteral("batchTable"));
    QVERIFY(table);
    auto* progressDelegate = table->itemDelegateForColumn(3);
    QVERIFY(progressDelegate);
    QCOMPARE(progressDelegate->objectName(),
             QStringLiteral("batchProgressDelegate"));
    QCOMPARE(widget.enqueueFiles({first, broken, third}, directory.path()), 3);
    bool overlapped = false;
    bool sawRunningProgress = false;
    QObject::connect(widget.model(), &QAbstractItemModel::dataChanged, &widget,
                     [&]() {
        int running = 0;
        for (int row = 0; row < widget.model()->rowCount(); ++row) {
            if (widget.model()->task(row).status == BatchTask::Status::Running) ++running;
        }
        if (running > 1) overlapped = true;
        if (widget.model()->task(0).status == BatchTask::Status::Running
            && widget.model()->task(0).transcodeProgress > 0) sawRunningProgress = true;
        if (widget.model()->task(2).status == BatchTask::Status::Running
            && widget.model()->task(0).status != BatchTask::Status::Succeeded) overlapped = true;
    });
    widget.startQueue();
    QVERIFY(widget.active());
    QTRY_VERIFY_WITH_TIMEOUT(!widget.active(), 20000);
    QVERIFY(!overlapped);
    QVERIFY(sawRunningProgress);
    QCOMPARE(widget.model()->task(0).status, BatchTask::Status::Succeeded);
    QCOMPARE(widget.model()->task(1).status, BatchTask::Status::Failed);
    QCOMPARE(widget.model()->task(2).status, BatchTask::Status::Succeeded);
    for (const int row : {0, 1, 2}) {
        QVERIFY(widget.model()->data(widget.model()->index(row, 4)).toString()
                .contains(QStringLiteral("耗时")));
    }
    QCOMPARE(widget.model()->task(0).transcodeProgress, 100);
    QVERIFY(widget.model()->task(1).transcodeProgress < 100);
    QCOMPARE(widget.model()->task(2).transcodeProgress, 100);
    QVERIFY(QFileInfo(widget.model()->task(0).options.outputPath).size() > 0);
    QVERIFY(QFileInfo(widget.model()->task(2).options.outputPath).size() > 0);
    QVERIFY(hasAudioAndVideo(widget.model()->task(0).options.outputPath));
    QVERIFY(hasAudioAndVideo(widget.model()->task(2).options.outputPath));
}

void BatchWidgetTest::refusesOutputCreatedAfterEnqueue()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    QVERIFY(makeMedia(input));
    BatchWidget widget(nullptr, [] { return TranscodeOptions{}; });
    QCOMPARE(widget.enqueueFiles({input}, directory.path()), 1);
    QFile file(widget.model()->task(0).options.outputPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray original("existing output must stay intact");
    file.write(original);
    file.close();
    widget.startQueue();
    QTRY_VERIFY_WITH_TIMEOUT(!widget.active(), 5000);
    QCOMPARE(widget.model()->task(0).status, BatchTask::Status::Failed);
    QVERIFY(!widget.model()->data(widget.model()->index(0, 4)).toString()
            .contains(QStringLiteral("耗时")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
}

void BatchWidgetTest::cancellationStopsCurrentAndPendingTasks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath("first.mp4");
    const QString second = directory.filePath("second.mp4");
    QVERIFY(makeMedia(first, 8));
    QVERIFY(makeMedia(second));
    BatchWidget widget(nullptr, [] { return TranscodeOptions{}; });
    QCOMPARE(widget.enqueueFiles({first, second}, directory.path()), 2);
    widget.startQueue();
    QVERIFY(widget.active());
    QTRY_COMPARE_WITH_TIMEOUT(widget.model()->task(0).status, BatchTask::Status::Running, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!QDir(directory.path()).entryList({"QFS-*.tmp"}, QDir::Files).isEmpty(), 5000);
    widget.cancelQueue();
    QTRY_VERIFY_WITH_TIMEOUT(!widget.active(), 8000);
    QCOMPARE(widget.model()->task(0).status, BatchTask::Status::Cancelled);
    QCOMPARE(widget.model()->task(1).status, BatchTask::Status::Cancelled);
    QVERIFY(widget.model()->data(widget.model()->index(0, 4)).toString()
            .contains(QStringLiteral("耗时")));
    QVERIFY(!widget.model()->data(widget.model()->index(1, 4)).toString()
            .contains(QStringLiteral("耗时")));
    QVERIFY(QDir(directory.path()).entryList({"QFS-*.tmp"}, QDir::Files).isEmpty());
}

void BatchWidgetTest::runsTwoAtOnceAndKeepsResultsSeparate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("中文 一.mp4"));
    const QString broken = directory.filePath("broken.mp4");
    const QString third = directory.filePath("third.mp4");
    QVERIFY(makeMedia(first, 10));
    QFile bad(broken);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("broken");
    bad.close();
    QVERIFY(QFile::copy(first, third));

    BatchWidget widget(nullptr, [] { return TranscodeOptions{}; });
    widget.setMaxConcurrent(2);
    QCOMPARE(widget.maxConcurrent(), 2);
    QCOMPARE(widget.enqueueFiles({first, broken, third}, directory.path()), 3);
    int peakRunning = 0;
    QObject::connect(widget.model(), &QAbstractItemModel::dataChanged, &widget, [&] {
        int running = 0;
        for (int row = 0; row < widget.model()->rowCount(); ++row) {
            if (widget.model()->task(row).status == BatchTask::Status::Running) ++running;
        }
        peakRunning = qMax(peakRunning, running);
    });
    widget.startQueue();
    QTRY_VERIFY_WITH_TIMEOUT(!widget.active(), 20000);
    QCOMPARE(peakRunning, 2);
    QCOMPARE(widget.model()->task(0).status, BatchTask::Status::Succeeded);
    QCOMPARE(widget.model()->task(1).status, BatchTask::Status::Failed);
    QCOMPARE(widget.model()->task(2).status, BatchTask::Status::Succeeded);
    QCOMPARE(widget.model()->task(0).transcodeProgress, 100);
    QCOMPARE(widget.model()->task(2).transcodeProgress, 100);
    QVERIFY(QFileInfo(widget.model()->task(0).options.outputPath).size() > 0);
    QVERIFY(QFileInfo(widget.model()->task(2).options.outputPath).size() > 0);
    auto* log = widget.findChild<QPlainTextEdit*>("batchLog");
    auto* copyLog = widget.findChild<QPushButton*>("batchCopyLogButton");
    auto* clearLog = widget.findChild<QPushButton*>("batchClearLogButton");
    QVERIFY(copyLog && clearLog);
    QVERIFY(log != nullptr);
    QVERIFY(log->toPlainText().contains(QStringLiteral("中文 一.mp4")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("third.mp4")));
    const QStringList lines = log->toPlainText().split(QLatin1Char('\n'));
    for (const int row : {0, 2}) {
        const QString prefix = QStringLiteral("[#%1]").arg(widget.model()->task(row).id);
        const QString fileName = QFileInfo(widget.model()->task(row).options.inputPath).fileName();
        bool attributed = false;
        for (const QString& line : lines) {
            if (line.startsWith(prefix) && line.contains(fileName)) attributed = true;
        }
        QVERIFY(attributed);
    }

    auto* table = widget.findChild<QTableView*>(QStringLiteral("batchTable"));
    auto* selector = widget.findChild<QComboBox*>(QStringLiteral("batchLogSelector"));
    QVERIFY(table && selector);
    QCOMPARE(selector->currentIndex(), 0);
    table->setCurrentIndex(widget.model()->index(0, 0));
    QVERIFY(log->toPlainText().contains(QStringLiteral("中文 一.mp4")));
    QVERIFY(!log->toPlainText().contains(QStringLiteral("third.mp4")));
    copyLog->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), log->toPlainText());
    clearLog->click();
    QVERIFY(log->toPlainText().isEmpty());
    QVERIFY(!copyLog->isEnabled());
    QVERIFY(!clearLog->isEnabled());
    table->setCurrentIndex(widget.model()->index(2, 0));
    QVERIFY(log->toPlainText().contains(QStringLiteral("third.mp4")));
    QVERIFY(!log->toPlainText().contains(QStringLiteral("中文 一.mp4")));
    table->setCurrentIndex(widget.model()->index(1, 0));
    QVERIFY(log->toPlainText().contains(QStringLiteral("失败")));
    selector->setCurrentIndex(0);
    QVERIFY(!log->toPlainText().contains(QStringLiteral("中文 一.mp4")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("third.mp4")));
    clearLog->click();
    QVERIFY(log->toPlainText().isEmpty());
    table->setCurrentIndex(widget.model()->index(2, 0));
    QVERIFY(log->toPlainText().isEmpty());
    widget.findChild<QPushButton*>(QStringLiteral("batchClearButton"))->click();
    QCOMPARE(selector->count(), 1);
    QVERIFY(log->toPlainText().isEmpty());
}

void BatchWidgetTest::stopsThreeRunningTasksWithoutStartingPending()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sample = directory.filePath("source.mp4");
    QVERIFY(makeMedia(sample, 12));
    QStringList inputs;
    for (int i = 0; i < 4; ++i) {
        const QString path = directory.filePath(QStringLiteral("copy%1.mp4").arg(i));
        QVERIFY(QFile::copy(sample, path));
        inputs << path;
    }
    BatchWidget widget(nullptr, [] { return TranscodeOptions{}; });
    widget.setMaxConcurrent(3);
    QCOMPARE(widget.enqueueFiles(inputs, directory.path()), 4);
    int peakRunning = 0;
    QObject::connect(widget.model(), &QAbstractItemModel::dataChanged, &widget, [&] {
        int running = 0;
        for (int row = 0; row < widget.model()->rowCount(); ++row) {
            if (widget.model()->task(row).status == BatchTask::Status::Running) ++running;
        }
        peakRunning = qMax(peakRunning, running);
    });
    widget.startQueue();
    QTRY_COMPARE_WITH_TIMEOUT(peakRunning, 3, 5000);
    QCOMPARE(widget.model()->task(3).status, BatchTask::Status::Pending);
    widget.cancelQueue();
    QTRY_VERIFY_WITH_TIMEOUT(!widget.active(), 10000);
    for (int row = 0; row < 4; ++row) {
        QCOMPARE(widget.model()->task(row).status, BatchTask::Status::Cancelled);
        QVERIFY(!QFileInfo::exists(widget.model()->task(row).options.outputPath));
    }
    QVERIFY(QDir(directory.path()).entryList({"QFS-*.tmp"}, QDir::Files).isEmpty());
}

QTEST_MAIN(BatchWidgetTest)
#include "test_batch_widget.moc"
