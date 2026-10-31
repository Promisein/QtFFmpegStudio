#include "mainwindow.h"
#include "batchwidget.h"

#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QLabel>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QtTest>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>
#include <QTableView>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>

class BatchIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void batchCardsShowRealCovers();
    void sidebarNavigationStaysStable();
    void refreshedPagesUseRealResultsAndResize();
    void batchTabHidesSingleFileControls();
    void batchDisablesOtherTasksAndRecoversAfterCancel();
    void closeAfterQueueFinishesDuringConfirmation();
    void closeCancelsAllConcurrentTasks();
};

void BatchIntegrationTest::sidebarNavigationStaysStable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.show();
    auto* title = window.findChild<QLabel*>(QStringLiteral("appTitle"));
    auto* single = window.findChild<QPushButton*>("navigation0");
    auto* batchNav = window.findChild<QPushButton*>("navigation1");
    auto* settingsNav = window.findChild<QPushButton*>("navigation2");
    auto* pages = window.findChild<QStackedWidget*>("taskPages");
    auto* batch = window.findChild<BatchWidget*>();
    QVERIFY(title && single && batchNav && settingsNav && pages && batch);
    QCOMPARE(title->text(), QStringLiteral("QtFFmpegStudio"));
    QCOMPARE(pages->currentIndex(), 0);
    const QPoint position=single->mapTo(&window,QPoint());
    batchNav->click(); QCoreApplication::processEvents();
    QCOMPARE(pages->currentWidget(),static_cast<QWidget*>(batch));
    QCOMPARE(single->mapTo(&window,QPoint()),position);
    settingsNav->click(); QCOMPARE(pages->currentIndex(),2);
    single->click(); QCOMPARE(pages->currentIndex(),0);
    QCOMPARE(single->mapTo(&window,QPoint()),position);
}

void BatchIntegrationTest::batchTabHidesSingleFileControls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.show();
    auto* tabs = window.findChild<QStackedWidget*>(QStringLiteral("taskPages"));
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
    QVERIFY(!options->isVisible());
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
        auto* tabs = window.findChild<QStackedWidget*>(QStringLiteral("taskPages"));
        tabs->setCurrentIndex(1);
        window.findChild<QLineEdit*>(QStringLiteral("batchOutputDirectoryEdit"))->clear();
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

void BatchIntegrationTest::refreshedPagesUseRealResultsAndResize()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString input=directory.filePath("sample.mp4");
    QCOMPARE(QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL), {"-hide_banner","-loglevel","error","-f","lavfi","-i","testsrc2=size=160x120:rate=25:duration=1","-f","lavfi","-i","sine=sample_rate=48000:duration=1","-c:v","mpeg4","-c:a","aac","-shortest",input}),0);
    const QString broken=directory.filePath("broken.mp4"); QFile bad(broken); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("invalid media"); bad.close();
    MainWindow window(nullptr,directory.filePath("settings.ini")); window.show();
    QMimeData mime; mime.setUrls({QUrl::fromLocalFile(input)});
    QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(&window,&enter); QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(&window,&drop); QVERIFY(drop.isAccepted());
    auto* start=window.findChild<QPushButton*>("startRealButton");
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(),10000);
    auto* pages=window.findChild<QStackedWidget*>("taskPages");
    auto* batch=window.findChild<BatchWidget*>(); auto* table=window.findChild<QTableView*>("batchTable");
    QCOMPARE(batch->enqueueFiles({input,broken},directory.path()),2);
    batch->startQueue(); QTRY_VERIFY_WITH_TIMEOUT(!batch->active(),10000);
    QCOMPARE(batch->model()->task(0).status,BatchTask::Status::Succeeded);
    QCOMPARE(batch->model()->task(1).status,BatchTask::Status::Failed);
    window.findChild<QPushButton*>("navigation1")->click(); QCOMPARE(pages->currentIndex(),1);
    QVERIFY(window.findChild<QPushButton*>("navigation1")->isChecked());
    for(auto* button:batch->findChildren<QPushButton*>()) {
        if(button->property("filter").toBool() && button->text().startsWith(QStringLiteral("已完成"))) {
            QCOMPARE(button->text(),QStringLiteral("已完成 (1)")); button->click();
            QVERIFY(!table->isRowHidden(0)); QVERIFY(table->isRowHidden(1));
        }
    }
    for(auto* button:batch->findChildren<QPushButton*>()) if(button->property("filter").toBool() && button->text().startsWith(QStringLiteral("全部"))) button->click();
    // The card menu exposes real actions for the selected completed task.
    table->setCurrentIndex(batch->model()->index(0,0));
    QTimer::singleShot(50,&window,[] { auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()); QVERIFY(menu); QVERIFY(menu->actions().last()->text().contains(QStringLiteral("输出文件夹"))); menu->close(); });
    QVERIFY(QMetaObject::invokeMethod(table,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,table->visualRect(batch->model()->index(0,0)).center())));
    const QString captures=qEnvironmentVariable("QTFFMPEG_UI_CAPTURE_DIR");
    if(!captures.isEmpty()) { QTest::qWait(100); QVERIFY(window.grab().save(captures+"/batch-results.png")); }
    const QString pending=directory.filePath("pending.mp4"); QVERIFY(QFile::copy(input,pending));
    QCOMPARE(batch->enqueueFiles({pending},directory.path()),1);
    QTimer::singleShot(50,&window,[] { auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()); QVERIFY(menu); menu->actions().last()->trigger(); menu->close(); });
    QVERIFY(QMetaObject::invokeMethod(table,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,table->visualRect(batch->model()->index(2,0)).center())));
    QCOMPARE(batch->model()->task(2).status,BatchTask::Status::Cancelled);
    QCOMPARE(batch->model()->task(0).status,BatchTask::Status::Succeeded);
    window.findChild<QPushButton*>("navigation0")->click();
    for(const QSize& size:{QSize(900,680),QSize(1240,860)}) {
        window.resize(size); QTest::qWait(60); QCOMPARE(window.size(),size);
        for(const char* name:{"startRealButton","outputButton"}) {
            auto* button=window.findChild<QPushButton*>(name); QVERIFY(button); QVERIFY(button->isVisible());
            QVERIFY(window.rect().contains(QRect(button->mapTo(&window,QPoint()),button->size())));
        }
        auto* workspace=window.findChild<QWidget*>("singlePage")->findChild<QScrollArea*>();
        QCOMPARE(workspace->horizontalScrollBar()->maximum(),0);
        if(!captures.isEmpty()) QVERIFY(window.grab().save(captures+QString("/single-%1.png").arg(size.width())));
    }
}

void BatchIntegrationTest::batchCardsShowRealCovers()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString red=directory.filePath(QStringLiteral("红色视频.mp4"));
    const QString blue=directory.filePath(QStringLiteral("蓝色视频.mp4"));
    for(const auto& sample:QList<QPair<QString,QString>>{{red,"red"},{blue,"blue"}}) {
        QCOMPARE(QProcess::execute(QString::fromUtf8(FFMPEG_TEST_TOOL),{"-hide_banner","-loglevel","error","-f","lavfi","-i","color=c="+sample.second+":size=160x120:rate=25:duration=1","-f","lavfi","-i","sine=sample_rate=48000:duration=1","-c:v","mpeg4","-c:a","aac","-shortest",sample.first}),0);
    }
    const QString bad=directory.filePath("broken.mp4"); QFile invalid(bad); QVERIFY(invalid.open(QIODevice::WriteOnly)); invalid.write("not a video"); invalid.close();
    MainWindow window(nullptr,directory.filePath("settings.ini")); window.show();
    window.findChild<QPushButton*>("navigation1")->click();
    auto* batch=window.findChild<BatchWidget*>(); auto* table=window.findChild<QTableView*>("batchTable");
    QCOMPARE(batch->enqueueFiles({red,blue,bad},directory.path()),3);
    auto coverPixel=[table,batch](int row) {
        const QRect rect=table->visualRect(batch->model()->index(row,0));
        const QImage image=table->viewport()->grab().toImage();
        return image.pixelColor(qRound((rect.left()+28)*image.devicePixelRatio()),qRound(rect.center().y()*image.devicePixelRatio()));
    };
    QTRY_VERIFY_WITH_TIMEOUT(coverPixel(0).red()>180 && coverPixel(0).blue()<80,15000);
    QTRY_VERIFY_WITH_TIMEOUT(coverPixel(1).blue()>180 && coverPixel(1).red()<80,15000);
    // Invalid media keeps the neutral file placeholder and cannot stop the queue.
    QVERIFY(coverPixel(2).red()>200 && coverPixel(2).blue()>200);
    if(qEnvironmentVariableIsSet("QTFFMPEG_BATCH_COVERS_SCREENSHOT")) QVERIFY(window.grab().save(qEnvironmentVariable("QTFFMPEG_BATCH_COVERS_SCREENSHOT")));
    window.findChild<QPushButton*>("batchClearButton")->click(); QCOMPARE(batch->model()->rowCount(),0);
    QCOMPARE(batch->enqueueFiles({blue,red},directory.path()),2);
    QTRY_VERIFY_WITH_TIMEOUT(coverPixel(0).blue()>180 && coverPixel(0).red()<80,15000);
    QTRY_VERIFY_WITH_TIMEOUT(coverPixel(1).red()>180 && coverPixel(1).blue()<80,15000);
    // Completed extraction must release Windows input handles.
    QVERIFY(QFile::remove(red)); QVERIFY(QFile::remove(blue));
}

QTEST_MAIN(BatchIntegrationTest)
#include "test_batch_integration.moc"
