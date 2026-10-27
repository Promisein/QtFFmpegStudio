#include "batchtaskmodel.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class BatchModelTest : public QObject
{
    Q_OBJECT

private slots:
    void createsDistinctOutputsAndSnapshotsOptions();
    void skipsInvalidAndDuplicateInputs();
    void updatesStatusAndClearsTasks();
    void progressIsPerTaskAndMonotonic();
    void taskIdsRemainUniqueAfterClear();
};

static QString createMp4(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write("path fixture");
    return path;
}

void BatchModelTest::createsDistinctOutputsAndSnapshotsOptions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = createMp4(directory.filePath(QStringLiteral("中文 视频.mp4")));
    QVERIFY(!input.isEmpty());
    const QString reserved = directory.filePath(QStringLiteral("中文 视频_batch.mp4"));
    QVERIFY(!createMp4(reserved).isEmpty());

    BatchTaskModel model;
    TranscodeOptions options;
    options.speedRatio = 1.5;
    options.videoBackend = QStringLiteral("nvenc");
    QCOMPARE(model.addFiles({input}, directory.path(), options), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.columnCount(), 5);
    const BatchTask task = model.task(0);
    QCOMPARE(task.options.inputPath, input);
    QCOMPARE(task.options.outputPath, directory.filePath(QStringLiteral("中文 视频_batch_2.mp4")));
    QCOMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("中文 视频.mp4"));
    QCOMPARE(model.data(model.index(0, 1)).toString(), QStringLiteral("中文 视频_batch_2.mp4"));
    QVERIFY(model.data(model.index(0, 1), Qt::ToolTipRole).toString()
            .contains(task.options.outputPath));
    QCOMPARE(task.options.speedRatio, 1.5);
    QCOMPARE(task.options.videoBackend, QStringLiteral("nvenc"));
    options.speedRatio = 2.0;
    options.videoBackend = QStringLiteral("software");
    QCOMPARE(model.task(0).options.speedRatio, 1.5);
    QCOMPARE(model.task(0).options.videoBackend, QStringLiteral("nvenc"));
    QCOMPARE(model.nextPendingIndex(), 0);
}

void BatchModelTest::progressIsPerTaskAndMonotonic()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = createMp4(directory.filePath("one.mp4"));
    const QString second = createMp4(directory.filePath("two.mp4"));
    BatchTaskModel model;
    QCOMPARE(model.addFiles({first, second}, directory.path(), {}), 2);
    QCOMPARE(model.data(model.index(0, 3)).toString(), QStringLiteral("—"));
    QVERIFY(!model.data(model.index(0, 3), Qt::UserRole + 1).isValid());
    model.setStatus(0, BatchTask::Status::Running);
    QCOMPARE(model.data(model.index(0, 3)).toString(), QStringLiteral("估算中"));
    QVERIFY(!model.data(model.index(0, 3), Qt::UserRole + 1).isValid());
    model.setTranscodeProgress(0, 20);
    model.setTranscodeProgress(0, 10);
    model.setTranscodeProgress(0, 101);
    QCOMPARE(model.task(0).transcodeProgress, 20);
    QCOMPARE(model.task(1).transcodeProgress, -1);
    QCOMPARE(model.data(model.index(0, 3)).toString(), QStringLiteral("20%"));
    QCOMPARE(model.data(model.index(0, 3), Qt::UserRole + 1).toInt(), 20);
    model.setTranscodeProgress(0, 100);
    QCOMPARE(model.data(model.index(0, 3)).toString(), QStringLiteral("20%"));
    model.setStatus(0, BatchTask::Status::Succeeded);
    QCOMPARE(model.data(model.index(0, 3)).toString(), QStringLiteral("100%"));
    QCOMPARE(model.data(model.index(0, 3), Qt::UserRole + 1).toInt(), 100);
    model.setStatus(1, BatchTask::Status::Running);
    QCOMPARE(model.task(1).transcodeProgress, -1);
    model.setStatus(1, BatchTask::Status::Failed);
    QVERIFY(!model.data(model.index(1, 3), Qt::UserRole + 1).isValid());
}

void BatchModelTest::skipsInvalidAndDuplicateInputs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString valid = createMp4(directory.filePath("valid.mp4"));
    const QString wrongExtension = createMp4(directory.filePath("wrong.txt"));
    BatchTaskModel model;
    QCOMPARE(model.addFiles({valid, valid, wrongExtension, directory.filePath("missing.mp4")},
                            directory.path(), {}), 1);
    QCOMPARE(model.addFiles({valid}, directory.path(), {}), 0);
    QCOMPARE(model.rowCount(), 1);
}

void BatchModelTest::updatesStatusAndClearsTasks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = createMp4(directory.filePath("one.mp4"));
    const QString second = createMp4(directory.filePath("two.mp4"));
    BatchTaskModel model;
    QCOMPARE(model.addFiles({first, second}, directory.path(), {}), 2);
    model.setStatus(0, BatchTask::Status::Running, QStringLiteral("正在转码"));
    QCOMPARE(model.task(0).status, BatchTask::Status::Running);
    model.setStatus(0, BatchTask::Status::Succeeded, QStringLiteral("完成"), 1234);
    QCOMPARE(model.data(model.index(0, 4)).toString(), QStringLiteral("耗时 1.2 秒｜完成"));
    QCOMPARE(model.nextPendingIndex(), 1);
    model.cancelPending();
    QCOMPARE(model.task(1).status, BatchTask::Status::Cancelled);
    QVERIFY(!model.data(model.index(1, 4)).toString().contains(QStringLiteral("耗时")));
    QCOMPARE(model.nextPendingIndex(), -1);
    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

void BatchModelTest::taskIdsRemainUniqueAfterClear()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = createMp4(directory.filePath("first.mp4"));
    const QString second = createMp4(directory.filePath("second.mp4"));
    BatchTaskModel model;
    QCOMPARE(model.addFiles({first, second}, directory.path(), {}), 2);
    const auto firstId = model.task(0).id;
    const auto secondId = model.task(1).id;
    QVERIFY(firstId != 0 && secondId != 0 && firstId != secondId);
    QCOMPARE(model.rowForId(secondId), 1);
    model.clear();
    QCOMPARE(model.addFiles({first}, directory.path(), {}), 1);
    QVERIFY(model.task(0).id != firstId && model.task(0).id != secondId);
    QCOMPARE(model.rowForId(secondId), -1);
}

QTEST_MAIN(BatchModelTest)
#include "test_batch_model.moc"
