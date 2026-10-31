#include "mainwindow.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>
#include <QIcon>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class OptionsTest : public QObject
{
    Q_OBJECT

private slots:
    void windowHasEmbeddedBrandIcon();
    void defaultValuesAppearInPreview();
    void changedValuesAndPathsAppearInPreview();
    void changingOptionsClearsOldPreview();
    void gpuSelectionUsesH264AndPersists();
    void nvencStatusCanBeRefreshed();
    void qualitySizeAndFpsSettings();
};

void OptionsTest::windowHasEmbeddedBrandIcon()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    MainWindow window(nullptr,directory.filePath("settings.ini"));
    QVERIFY(!window.windowIcon().isNull());
    for(int size:{16,32,48,256}) {
        const QImage image=window.windowIcon().pixmap(size,size).toImage();
        QVERIFY(!image.isNull()); QVERIFY(image.hasAlphaChannel());
        QVERIFY(image.pixelColor(0,0).alpha()<20);
    }
    window.show(); QCoreApplication::processEvents();
#ifdef Q_OS_WIN
    const HWND handle=reinterpret_cast<HWND>(window.winId());
    QVERIFY(SendMessageW(handle,WM_GETICON,ICON_BIG,0)!=0);
    QVERIFY(SendMessageW(handle,WM_GETICON,ICON_SMALL,0)!=0);
#endif
}

void OptionsTest::nvencStatusCanBeRefreshed()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* status = window.findChild<QLabel*>("nvencStatusLabel");
    auto* refresh = window.findChild<QPushButton*>("nvencRefreshButton");
    QVERIFY(status && refresh);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("NVENC"))
                             && !status->text().contains(QStringLiteral("检测中")), 15000);
    QVERIFY(status->text().contains(QStringLiteral("可用"))
            || status->text().contains(QStringLiteral("不可用")));
    refresh->click();
    QTRY_VERIFY_WITH_TIMEOUT(refresh->isEnabled(), 15000);
}

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

void OptionsTest::gpuSelectionUsesH264AndPersists()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsFile = directory.filePath("settings.ini");
    {
        MainWindow window(nullptr, settingsFile);
        auto* backend = window.findChild<QComboBox*>("videoBackendCombo");
        auto* codec = window.findChild<QComboBox*>("videoCodecCombo");
        auto* preview = window.findChild<QPlainTextEdit*>("optionsPreview");
        auto* button = window.findChild<QPushButton*>("previewButton");
        QVERIFY(backend && codec && preview && button);
        QCOMPARE(backend->currentData().toString(), QStringLiteral("software"));

        backend->setCurrentIndex(backend->findData(QStringLiteral("nvenc")));
        QCOMPARE(codec->currentData().toString(), QStringLiteral("h264"));
        button->click();
        QVERIFY(preview->toPlainText().contains(QStringLiteral("GPU（NVIDIA NVENC）")));

        codec->setCurrentIndex(codec->findData(QStringLiteral("mpeg4")));
        QCOMPARE(backend->currentData().toString(), QStringLiteral("software"));
        backend->setCurrentIndex(backend->findData(QStringLiteral("nvenc")));
        window.close();
    }
    MainWindow restored(nullptr, settingsFile);
    QCOMPARE(restored.readOptions().videoCodec, QStringLiteral("h264"));
    auto* restoredBackend = restored.findChild<QComboBox*>("videoBackendCombo");
    QVERIFY(restoredBackend);
    QCOMPARE(restoredBackend->currentData().toString(), QStringLiteral("nvenc"));
}

QTEST_MAIN(OptionsTest)
#include "test_options.moc"

void OptionsTest::qualitySizeAndFpsSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(nullptr, directory.filePath("settings.ini"));
    auto* quality = window.findChild<QComboBox*>("qualityCombo");
    auto* size = window.findChild<QComboBox*>("resolutionCombo");
    auto* fps = window.findChild<QComboBox*>("frameRateCombo");
    QVERIFY(quality && size && fps);
    QCOMPARE(quality->currentData().toString(), QStringLiteral("default"));
    QCOMPARE(size->currentData().toInt(), 0);
    QCOMPARE(fps->currentData().toInt(), 0);
    quality->setCurrentIndex(quality->findData(QStringLiteral("high")));
    size->setCurrentIndex(size->findData(720));
    fps->setCurrentIndex(fps->findData(30));
    auto* preview = window.findChild<QPlainTextEdit*>("optionsPreview");
    window.findChild<QPushButton*>("previewButton")->click();
    QVERIFY(preview->toPlainText().contains(QStringLiteral("720")));
    QVERIFY(preview->toPlainText().contains(QStringLiteral("30")));
    auto* bitrate = window.findChild<QSpinBox*>("bitrateSpin");
    QVERIFY(bitrate && !bitrate->isEnabled());
    quality->setCurrentIndex(quality->findData(QStringLiteral("bitrate")));
    QVERIFY(bitrate->isEnabled());
    bitrate->setValue(3500);
    window.close();
    MainWindow restored(nullptr, directory.filePath("settings.ini"));
    QCOMPARE(restored.readOptions().quality, QStringLiteral("bitrate"));
    QCOMPARE(restored.readOptions().videoBitrateKbps, 3500);
    QCOMPARE(restored.readOptions().maxShortEdge, 720);
    QCOMPARE(restored.readOptions().outputFps, 30);
}
