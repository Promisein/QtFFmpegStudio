#include "mainwindow.h"
#include "batchwidget.h"

#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QStackedWidget>
#include <QLabel>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QtTest>

class BatchIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void modeNavigationStaysAtTop();
    void batchTabHidesSingleFileControls();
    void batchDisablesOtherTasksAndRecoversAfterCancel();
    void closeAfterQueueFinishesDuringConfirmation();
    void closeCancelsAllConcurrentTasks();
};

void BatchIntegrationTest::modeNavigationStaysAtTop()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.show();
    auto* title = window.findChild<QLabel*>(QStringLiteral("appTitle"));
    auto* navigation = window.findChild<QTabBar*>(QStringLiteral("modeNavigation"));
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("taskPages"));
    auto* batch = window.findChild<BatchWidget*>();
    QVERIFY(title && navigation && pages && batch);
    QCOMPARE(title->text(), QStringLiteral("QtFFmpegStudio"));
    QCOMPARE(navigation->count(), 3);
    QCOMPARE(navigation->currentIndex(), 0);
    const int topY = navigation->mapTo(&window, QPoint(0, 0)).y();
    QVERIFY(topY < window.findChild<QWidget*>(QStringLiteral("singleFileControls"))
                     ->mapTo(&window, QPoint(0, 0)).y());
    navigation->setCurrentIndex(1);
    QCoreApplication::processEvents();
    QCOMPARE(pages->currentWidget(), static_cast<QWidget*>(batch));
    QCOMPARE(navigation->mapTo(&window, QPoint(0, 0)).y(), topY);
    navigation->setCurrentIndex(2);
    QCoreApplication::processEvents();
    QCOMPARE(pages->currentIndex(), 2);
    QCOMPARE(navigation->mapTo(&window, QPoint(0, 0)).y(), topY);
    navigation->setCurrentIndex(0);
    QCoreApplication::processEvents();
    QCOMPARE(pages->currentIndex(), 0);
    QCOMPARE(navigation->mapTo(&window, QPoint(0, 0)).y(), topY);
}

void BatchIntegrationTest::batchTabHidesSingleFileControls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.show();
    auto* tabs = window.findChild<QTabBar*>(QStringLiteral("modeNavigation"));
    auto* batch = window.findChild<BatchWidget*>();
    auto* singleControls = window.findChild<QWidget*>(QStringLiteral("singleFileControls"));
    auto* options = window.findChild<QGroupBox*>(QStringLiteral("optionsGroup"));
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("inputPathEdit"));
    auto* output = window.findChild<QLineEdit*>(QStringLiteral("outputPathEdit"));
    QVERIFY(tabs && batch && singleControls && options && input && output);
    input->setText(QStringLiteral("C:/video/input.mp4"));
    output->setText(QStringLiteral("C:/video/output.mp4"));
    QVERIFY(singleControls->isVisible());
    tabs->setCurrentIndex(1);
    QVERIFY(!singleControls->isVisible());
    QVERIFY(options->isVisible());
    QVERIFY(!window.findChild<QPlainTextEdit*>(QStringLiteral("optionsPreview"))->isVisible());
    tabs->setCurrentIndex(0);
    QVERIFY(singleControls->isVisible());
    QCOMPARE(input->text(), QStringLiteral("C:/video/input.mp4"));
    QCOMPARE(output->text(), QStringLiteral("C:/video/output.mp4"));
}

void BatchIntegrationTest::batchDisablesOtherTasksAndRecoversAfterCancel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("input.mp4");
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("broken but valid path");
    file.close();
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* batch = window.findChild<BatchWidget*>();
    QVERIFY(batch != nullptr);
    QCOMPARE(batch->enqueueFiles({input}, directory.path()), 1);
    if (qEnvironmentVariableIsSet("QTFFMPEG_SCREENSHOT")) {
        auto* tabs = window.findChild<QTabBar*>(QStringLiteral("modeNavigation"));
        tabs->setCurrentIndex(1);
        window.show();
        QTest::qWait(150);
        QVERIFY(window.grab().save(qEnvironmentVariable("QTFFMPEG_SCREENSHOT")));
    }
    auto* realStart = window.findChild<QPushButton*>("startRealButton");
    auto* simulationStart = window.findChild<QPushButton*>("startSimulationButton");
    auto* options = window.findChild<QGroupBox*>("optionsGroup");
    QVERIFY(realStart && simulationStart && options);
    batch->startQueue();
    QVERIFY(batch->active());
    QVERIFY(!realStart->isEnabled());
    QVERIFY(!simulationStart->isEnabled());
    QVERIFY(!options->isEnabled());
    batch->cancelQueue();
    QTRY_VERIFY_WITH_TIMEOUT(!batch->active(), 8000);
    QVERIFY(!realStart->isEnabled()); // 尚未选择单文件输入；媒体分析门槛仍然生效。
    QVERIFY(simulationStart->isEnabled());
    QVERIFY(options->isEnabled());
}

void BatchIntegrationTest::closeAfterQueueFinishesDuringConfirmation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.filePath("broken.mp4");
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("broken media");
    file.close();
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.show();
    auto* batch = window.findChild<BatchWidget*>();
    QVERIFY(batch != nullptr);
    QCOMPARE(batch->enqueueFiles({input}, directory.path()), 1);
    batch->startQueue();
    QVERIFY(batch->active());
    QTimer::singleShot(1500, &window, [&]() {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        dialog->button(QMessageBox::Yes)->click();
    });
    window.close();
    QVERIFY(!batch->active());
    QVERIFY(!window.isVisible());
}

void BatchIntegrationTest::closeCancelsAllConcurrentTasks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sample = directory.filePath("source.mp4");
    const QStringList arguments{
        "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
        "testsrc=size=160x120:rate=25:duration=12", "-f", "lavfi", "-i",
        "sine=frequency=440:sample_rate=44100:duration=12", "-c:v", "mpeg4",
        "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", sample
    };
    QCOMPARE(QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL), arguments), 0);
    const QString first = directory.filePath("first.mp4");
    const QString second = directory.filePath("second.mp4");
    QVERIFY(QFile::copy(sample, first));
    QVERIFY(QFile::copy(sample, second));

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.show();
    auto* batch = window.findChild<BatchWidget*>();
    QVERIFY(batch != nullptr);
    batch->setMaxConcurrent(2);
    QCOMPARE(batch->enqueueFiles({first, second}, directory.path()), 2);
    batch->startQueue();
    QTRY_COMPARE_WITH_TIMEOUT(batch->model()->task(0).status, BatchTask::Status::Running, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(batch->model()->task(1).status, BatchTask::Status::Running, 5000);
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        dialog->button(QMessageBox::Yes)->click();
    });
    window.close();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 10000);
    QVERIFY(!batch->active());
    for (int row = 0; row < 2; ++row) {
        QCOMPARE(batch->model()->task(row).status, BatchTask::Status::Cancelled);
        QVERIFY(!QFileInfo::exists(batch->model()->task(row).options.outputPath));
    }
    QVERIFY(QDir(directory.path()).entryList({"QFS-*.tmp"}, QDir::Files).isEmpty());
}

QTEST_MAIN(BatchIntegrationTest)
#include "test_batch_integration.moc"
