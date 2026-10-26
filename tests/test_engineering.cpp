#include "mainwindow.h"

#include <QComboBox>
#include <QAbstractItemModel>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class EngineeringTest : public QObject
{
    Q_OBJECT

private slots:
    void restoresValidSettingsAfterRestart();
    void ignoresMissingInputOnRestart();
};

void EngineeringTest::restoresValidSettingsAfterRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsFile = directory.filePath("settings.ini");
    const QString input = directory.filePath(QStringLiteral("输入 空格.mp4"));
    const QString output = directory.filePath(QStringLiteral("输出 空格.mp4"));
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("fixture");
    file.close();

    {
        MainWindow window(nullptr, settingsFile);
        window.findChild<QLineEdit*>("inputPathEdit")->setText(input);
        window.findChild<QLineEdit*>("outputPathEdit")->setText(output);
        window.findChild<QComboBox*>("rotationCombo")->setCurrentIndex(1);
        window.findChild<QDoubleSpinBox*>("speedSpin")->setValue(1.5);
        window.findChild<QComboBox*>("videoCodecCombo")->setCurrentIndex(1);
        window.findChild<QComboBox*>("audioCodecCombo")->setCurrentIndex(1);
        window.findChild<QComboBox*>("batchConcurrencyCombo")->setCurrentIndex(2);
        window.resize(900, 720);
        window.close();
    }
    MainWindow restored(nullptr, settingsFile);
    QCOMPARE(restored.readOptions().inputPath, input);
    QCOMPARE(restored.readOptions().outputPath, output);
    QCOMPARE(restored.readOptions().rotationDegrees, 90);
    QCOMPARE(restored.readOptions().speedRatio, 1.5);
    QCOMPARE(restored.readOptions().videoCodec, QStringLiteral("h264"));
    QCOMPARE(restored.readOptions().audioCodec, QStringLiteral("aac"));
    QCOMPARE(restored.findChild<QComboBox*>("batchConcurrencyCombo")->currentData().toInt(), 3);
    QCOMPARE(restored.size(), QSize(900, 720));
    QVERIFY(!restored.findChild<QPushButton*>("openOutputFolderButton")->isEnabled());
}

void EngineeringTest::ignoresMissingInputOnRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsFile = directory.filePath("settings.ini");
    {
        QSettings settings(settingsFile, QSettings::IniFormat);
        settings.setValue("files/input", directory.filePath("missing.mp4"));
        settings.setValue("files/output", directory.filePath("output.mp4"));
        settings.setValue("options/speed", 100.0);
        settings.setValue("options/videoCodec", "unsupported");
        settings.setValue("batch/maxConcurrent", 99);
        settings.sync();
    }
    MainWindow window(nullptr, settingsFile);
    QVERIFY(window.readOptions().inputPath.isEmpty());
    QCOMPARE(window.readOptions().outputPath, directory.filePath("output.mp4"));
    QCOMPARE(window.readOptions().speedRatio, 1.0);
    QCOMPARE(window.readOptions().videoCodec, QStringLiteral("mpeg4"));
    QCOMPARE(window.findChild<QComboBox*>("batchConcurrencyCombo")->currentData().toInt(), 1);
}

QTEST_MAIN(EngineeringTest)
#include "test_engineering.moc"
