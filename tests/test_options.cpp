#include "mainwindow.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

class OptionsTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultValuesAppearInPreview();
    void changedValuesAndPathsAppearInPreview();
    void changingOptionsClearsOldPreview();
};

void OptionsTest::defaultValuesAppearInPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* preview = window.findChild<QPlainTextEdit*>("optionsPreview");
    auto* button = window.findChild<QPushButton*>("previewButton");
    QVERIFY(preview != nullptr);
    QVERIFY(button != nullptr);

    button->click();
    const QString text = preview->toPlainText();
    QVERIFY(text.contains(QStringLiteral("旋转：0°")));
    QVERIFY(text.contains(QStringLiteral("倍速：1.00x")));
    QVERIFY(text.contains(QStringLiteral("视频编码：mpeg4")));
    QVERIFY(text.contains(QStringLiteral("音频编码：ac3")));
    QVERIFY(text.contains(QStringLiteral("尚未选择")));
}

void OptionsTest::changedValuesAndPathsAppearInPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* rotation = window.findChild<QComboBox*>("rotationCombo");
    auto* speed = window.findChild<QDoubleSpinBox*>("speedSpin");
    auto* video = window.findChild<QComboBox*>("videoCodecCombo");
    auto* audio = window.findChild<QComboBox*>("audioCodecCombo");
    auto* input = window.findChild<QLineEdit*>("inputPathEdit");
    auto* output = window.findChild<QLineEdit*>("outputPathEdit");
    auto* preview = window.findChild<QPlainTextEdit*>("optionsPreview");
    auto* button = window.findChild<QPushButton*>("previewButton");
    QVERIFY(rotation && speed && video && audio && input && output && preview && button);

    rotation->setCurrentIndex(rotation->findData(270));
    speed->setValue(1.25);
    video->setCurrentIndex(video->findData(QStringLiteral("h264")));
    audio->setCurrentIndex(audio->findData(QStringLiteral("aac")));
    input->setText(QStringLiteral("C:/影片/示例 视频.mp4"));
    output->setText(QStringLiteral("D:/输出/完成.mp4"));
    button->click();

    const QString text = preview->toPlainText();
    QVERIFY(text.contains(QStringLiteral("旋转：270°")));
    QVERIFY(text.contains(QStringLiteral("倍速：1.25x")));
    QVERIFY(text.contains(QStringLiteral("视频编码：h264")));
    QVERIFY(text.contains(QStringLiteral("音频编码：aac")));
    QVERIFY(text.contains(QStringLiteral("C:/影片/示例 视频.mp4")));
    QVERIFY(text.contains(QStringLiteral("D:/输出/完成.mp4")));
}

void OptionsTest::changingOptionsClearsOldPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* speed = window.findChild<QDoubleSpinBox*>("speedSpin");
    auto* preview = window.findChild<QPlainTextEdit*>("optionsPreview");
    auto* button = window.findChild<QPushButton*>("previewButton");
    QVERIFY(speed && preview && button);

    button->click();
    QVERIFY(!preview->toPlainText().isEmpty());
    speed->setValue(0.75);
    QVERIFY(preview->toPlainText().isEmpty());
    button->click();
    QVERIFY(preview->toPlainText().contains(QStringLiteral("倍速：0.75x")));
}

QTEST_MAIN(OptionsTest)
#include "test_options.moc"
