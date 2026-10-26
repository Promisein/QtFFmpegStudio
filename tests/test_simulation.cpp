#include "mainwindow.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class SimulationTest : public QObject
{
    Q_OBJECT

private slots:
    void invalidPathsDoNotStart();
    void completesWhileEventLoopRemainsResponsive();
    void stoppingAllowsRetry();
};

static QString prepareInput(QTemporaryDir& directory)
{
    const QString path = directory.filePath(QStringLiteral("学习 视频.mp4"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write("path fixture only");
    file.close();
    return path;
}

void SimulationTest::invalidPathsDoNotStart()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    MainWindow window(nullptr, settingsDirectory.filePath("settings.ini"));
    auto* start = window.findChild<QPushButton*>("startSimulationButton");
    auto* stop = window.findChild<QPushButton*>("stopSimulationButton");
    auto* progress = window.findChild<QProgressBar*>("simulationProgress");
    auto* feedback = window.findChild<QLabel*>("feedbackLabel");
    auto* input = window.findChild<QLineEdit*>("inputPathEdit");
    auto* output = window.findChild<QLineEdit*>("outputPathEdit");
    QVERIFY(start && stop && progress && feedback && input && output);

    start->click();
    QCOMPARE(feedback->text(), QStringLiteral("请先选择输入视频。"));
    QCOMPARE(progress->value(), 0);
    QVERIFY(start->isEnabled());
    QVERIFY(!stop->isEnabled());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = prepareInput(directory);
    QVERIFY(!path.isEmpty());
    input->setText(path);
    output->setText(path);
    start->click();
    QCOMPARE(feedback->text(), QStringLiteral("输入和输出不能是同一个文件。"));
    QCOMPARE(progress->value(), 0);
}

void SimulationTest::completesWhileEventLoopRemainsResponsive()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = prepareInput(directory);
    QVERIFY(!inputPath.isEmpty());
    const QString outputPath = directory.filePath(QStringLiteral("结果.mp4"));

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.findChild<QLineEdit*>("inputPathEdit")->setText(inputPath);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(outputPath);
    auto* start = window.findChild<QPushButton*>("startSimulationButton");
    auto* stop = window.findChild<QPushButton*>("stopSimulationButton");
    auto* inputButton = window.findChild<QPushButton*>("inputButton");
    auto* rotation = window.findChild<QComboBox*>("rotationCombo");
    auto* progress = window.findChild<QProgressBar*>("simulationProgress");
    auto* state = window.findChild<QLabel*>("simulationStateLabel");
    QVERIFY(start && stop && inputButton && rotation && progress && state);

    start->click();
    QVERIFY(!start->isEnabled());
    QVERIFY(stop->isEnabled());
    QVERIFY(!inputButton->isEnabled());
    QVERIFY(!rotation->isEnabled());
    QCOMPARE(progress->value(), 0);
    bool eventLoopResponded = false;
    QTimer::singleShot(0, &window, [&]() { eventLoopResponded = true; });
    QTRY_VERIFY_WITH_TIMEOUT(eventLoopResponded, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(progress->value(), 100, 5000);
    QVERIFY(state->text().contains(QStringLiteral("模拟任务完成")));
    QVERIFY(start->isEnabled());
    QVERIFY(!stop->isEnabled());
    QVERIFY(inputButton->isEnabled());
    QVERIFY(rotation->isEnabled());
    QVERIFY(!QFileInfo::exists(outputPath));
}

void SimulationTest::stoppingAllowsRetry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = prepareInput(directory);
    QVERIFY(!inputPath.isEmpty());
    const QString outputPath = directory.filePath(QStringLiteral("结果.mp4"));

    MainWindow window(nullptr, directory.filePath("settings.ini"));
    window.findChild<QLineEdit*>("inputPathEdit")->setText(inputPath);
    window.findChild<QLineEdit*>("outputPathEdit")->setText(outputPath);
    auto* start = window.findChild<QPushButton*>("startSimulationButton");
    auto* stop = window.findChild<QPushButton*>("stopSimulationButton");
    auto* progress = window.findChild<QProgressBar*>("simulationProgress");
    auto* state = window.findChild<QLabel*>("simulationStateLabel");
    QVERIFY(start && stop && progress && state);

    start->click();
    QTRY_VERIFY_WITH_TIMEOUT(progress->value() >= 10, 1500);
    const int beforeDuplicateClick = progress->value();
    start->click();
    QVERIFY(progress->value() >= beforeDuplicateClick);
    stop->click();
    const int stoppedValue = progress->value();
    QTest::qWait(350);
    QCOMPARE(progress->value(), stoppedValue);
    QVERIFY(state->text().contains(QStringLiteral("模拟任务已停止")));
    QVERIFY(start->isEnabled());
    QVERIFY(!stop->isEnabled());

    start->click();
    QCOMPARE(progress->value(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(progress->value(), 100, 5000);
    QVERIFY(state->text().contains(QStringLiteral("模拟任务完成")));
    QVERIFY(!QFileInfo::exists(outputPath));
}

QTEST_MAIN(SimulationTest)
#include "test_simulation.moc"
