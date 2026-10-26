# 第三课：从控件得到转码配置

本课代码已经写入项目。运行后，原来的文件选择和路径检查仍可使用；新增旋转角度、播放倍速、视频编码器和音频编码器四项设置。点击“预览参数”会在下方显示当前选择。

## 第一步：运行并观察

1. 在 CLion 中重新加载 CMake，构建并运行 `QtFFmpegStudio`。
2. 不改控件，点击“预览参数”：应看到 **0°、1.00x、mpeg4、ac3**。尚未选择文件时，输入输出显示“尚未选择”。
3. 选择自己的 MP4 和输出路径，改成 **270°、1.25x、H.264、AAC**，再点击预览，确认六行内容与界面一致。
4. 再修改倍速：旧预览应被清空；重新点击预览后显示新值。文件路径改变时也一样。

预览只是读取界面状态，不执行转码，也不创建输出视频。H.264/AAC 是否能实际编码，需要接入现有后端后再验证。

## 第二步：理解配置结构

打开 [transcodeoptions.h](../../src/transcodeoptions.h)。`TranscodeOptions` 是普通 C++ 结构体，只保存当前输入、输出和四项参数：

```cpp
struct TranscodeOptions
{
    QString inputPath;
    QString outputPath;
    int rotationDegrees = 0;
    double speedRatio = 1.0;
    QString videoCodec = QStringLiteral("mpeg4");
    QString audioCodec = QStringLiteral("ac3");
};
```

默认值与原 FFmpeg 项目中的 `ProcessingConfig` 对齐。这里的 `QString` 方便 Qt 界面处理中文路径；第五课连接后端时，再处理 CLI 参数和编码问题。结构体不含控件指针，今后可以把一次选择当作一份任务配置传递。

## 第三步：QFormLayout 与控件

打开 [mainwindow.cpp](../../src/mainwindow.cpp)，找到 `optionsGroup`。`QFormLayout` 的 `addRow(标题, 控件)` 让四个标签和输入控件对齐，例如：

```cpp
auto* optionsForm = new QFormLayout(optionsGroup);
optionsForm->addRow(QStringLiteral("旋转角度"), rotationCombo_);
```

旋转和编码器使用 `QComboBox`。添加一项时有两个不同的值：

```cpp
videoCodecCombo_->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
```

“H.264”是用户看到的文字；“h264”是准备交给后端的参数标识。后续修改界面语言时，技术标识仍可保持一致。旋转角度也是同样的方法：显示“顺时针 90°”，内部保存整数 `90`。读取时用 `currentData()`，不解析 `currentText()`。

倍速使用 `QDoubleSpinBox`，范围为 `0.50` 到 `4.00`，显示两位小数，步进 `0.25`，后缀 `x`。`value()` 返回 `double`；后缀只是显示文本。你可以手动输入范围内的两位小数值，步进并不表示只能选择 0.25 的倍数。

## 第四步：界面值怎样变成配置

[mainwindow.h](../../src/mainwindow.h) 中新增 `readOptions() const`，实现位于 `mainwindow.cpp`。它逐一读取六个控件，并返回 `TranscodeOptions`：

```cpp
options.rotationDegrees = rotationCombo_->currentData().toInt();
options.speedRatio = speedSpin_->value();
options.videoCodec = videoCodecCombo_->currentData().toString();
```

这里的 `const` 表示读取配置不修改窗口状态。点击“预览参数”后，`showOptionsPreview()` 调用 `readOptions()`，将这份配置格式化显示到只读的 `QPlainTextEdit` 中。预览使用 `setPlainText()`，路径中的 `&`、`<` 等字符按普通文本显示。

预览代表点击按钮时的一次快照。修改文件或参数后，连接到 `textChanged`、`currentIndexChanged`、`valueChanged` 的回调会清空旧预览，防止它看起来仍是最新值。再点一次按钮即可得到新快照。

## 第五步：看验证过程

本课在 [test_options.cpp](../../tests/test_options.cpp) 中写了三个 Qt Test 行为检查：

- 默认配置会正确显示在预览中。
- 修改四项参数和中英文、空格路径后，预览显示当前值。
- 预览后修改倍速会清空旧结果，再预览显示新值。

这些测试先因缺少第三课控件而失败，功能完成后重新运行通过。它们检查窗口对用户可见的结果，不依赖模拟转码器。若你想自己运行，在 PowerShell 中进入本项目后执行：

```powershell
$env:PATH = 'D:\QT\6.8.3\mingw_64\bin;D:\QT\Tools\mingw1310_64\bin;' + $env:PATH
& 'D:\CLion 2023.3.6\bin\cmake\win\x64\bin\cmake.exe' --build .\cmake-build-debug-mingw_qt_683
& 'D:\CLion 2023.3.6\bin\cmake\win\x64\bin\ctest.exe' --test-dir .\cmake-build-debug-mingw_qt_683 --output-on-failure
```

实际测试由 CTest 设置 `QT_QPA_PLATFORM=windows`。这是因为当前构建目录随应用部署了 Windows 平台插件；Qt Test 创建 QApplication 时需要找到对应插件。本机 Qt Test 已安装在 Qt 6.8.3 SDK 中。若你使用其他 CLion 构建目录，替换上面两处目录名。

## 第六步：两个小练习

1. 只把视频编码选项的显示名 `H.264` 改成 `H.264 / AVC`，运行后预览中的技术标识应仍是 `h264`。说出原因。
2. 将倍速设为 `0.75x`，先点击预览，再改成 `1.50x`。指出哪条 `connect` 让旧预览消失，以及哪里把 `double` 格式化为两位小数。

完成本课后，下一课用 `QTimer` 模拟任务状态，让你先看清 Qt 事件循环与界面响应；真实 FFmpeg 调用会在后续课程接入。

参考：[QComboBox](https://doc.qt.io/qt-6.8/qcombobox.html)、[QDoubleSpinBox](https://doc.qt.io/qt-6.8/qdoublespinbox.html)、[QFormLayout](https://doc.qt.io/qt-6.8/qformlayout.html)、[QPlainTextEdit](https://doc.qt.io/qt-6.8/qplaintextedit.html)。
