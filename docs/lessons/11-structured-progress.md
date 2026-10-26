# 第十一课：为后端与 Qt 界面建立结构化进度协议

第八课从中文 `Demux 进度: N%` 日志提取百分比。后来进度改为依据封装器已写入的音视频时间戳估算整体转码，后端通过独立机器事件通知 GUI；原中文日志继续显示。

## 一、在 CLion 中操作

1. 关闭旧窗口，重新加载 CMake 并构建 `QtFFmpegStudio`。打开单文件页，选一个带音视频的 MP4 转码。
2. 观察“转码进度”随任务变化。日志框应继续显示 `Demux 进度` 等人类日志，**不会显示** `QFS_PROGRESS` 机器行。
3. 在批量页选并发数 2，加入两个 MP4。两行进度各自变化；一项失败或被取消时，不会把另一项的百分比带过去。
4. 用输出文件播放检查结果。转码期间进度最多 99%；只有后端成功退出且确认输出文件非空才显示 100% 和“转码完成”。

可运行自动测试：

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.8.3\mingw_64\bin;' + $env:PATH
cmake --build .\cmake-build-debug-mingw_qt_683 --parallel 4
ctest --test-dir .\cmake-build-debug-mingw_qt_683 --output-on-failure
```

若你的 CLion 使用其他构建目录，请替换命令中的目录。课程验证使用 `cmake-build-codex-lesson4`。

## 二、认识协议

后端仍输出原来的中文日志，同时在标准输出额外写一行 UTF-8 事件：

```text
QFS_PROGRESS {"version":1,"stage":"transcode","percent":40}
```

`QFS_PROGRESS ` 是与普通日志区分的固定前缀；`version` 固定为 1；`stage` 目前为 `transcode`；`percent` 是 0～99 的整数。每条事件以换行结束并立即刷新输出。100% 由 GUI 在后端成功结束且输出文件非空时设置。

[Logger](../../backend/src/logger.cpp)在写日志的同一把锁下输出机器事件；[mux.cpp](../../backend/src/mux.cpp)在成功提交音视频包给封装器后上报估算进度。[demux.cpp](../../backend/src/demux.cpp)继续输出中文日志。旧 CLI 参数仍可用。

## 三、读 Qt 代码

[BackendProcess](../../src/backendprocess.cpp)通过 `QProcess::readyReadStandardOutput` 收到字节块，用 `QStringDecoder` 增量解码 UTF-8，再交给 [TranscodeProgressParser](../../src/transcodeprogressparser.h) 按行处理。一次读取不保证恰好是一行，所以解析器会保留尚未收到换行的部分。它使用 [Qt 6.8 的 QJsonDocument](https://doc.qt.io/qt-6.8/qjsondocument.html)解析事件，只接受正确版本、`transcode` 阶段、0～99 的整数且比上次更大的百分比。

解析器把普通日志原样交给日志框，把机器事件转成进度信号。事件格式错误或没有事件时，它不会从中文日志猜数字；日志仍可读，任务最终成败仍由进程退出和输出文件检查决定。每个批量任务有独立 `BackendProcess` 和解析器，因此并发任务不会共用进度状态。

```text
后端中文日志 ───────────────▶ GUI 日志框
后端 QFS_PROGRESS 事件 ───▶ 按行与 JSON 解析 ─▶ 转码估算进度
后端退出 + 输出文件检查 ───▶ 转码成功 / 失败 / 取消
```

估算器以输入时长和倍速推算输出时长，以已经写入的音视频时间戳中较慢的一路计算比例。它反映编码与封装的处理位置，不是精确的剩余时间预测；未知时长会保持“估算中”，最终成功后仍能显示 100%。

## 四、测试与练习

[解析器测试](../../tests/test_transcode_progress_parser.cpp)覆盖中文 UTF-8 被拆开、机器事件跨读取、错误 JSON、错误版本或阶段、小数和越界值、重复或倒退值、重置及日志过滤。[真实转码测试](../../tests/test_real_transcode.cpp)检查后端同时输出两类信息，GUI 进度变化且日志框不出现机器行。批量、取消和输出保护也由全部 CTest 回归。

请自己完成两项练习：

1. 在 `TranscodeProgressParser::append()` 中找出检查 `version`、`stage` 和 `percent` 的位置。解释为什么收到 `101` 或 `10.5` 时不能更新进度条。
2. 比较 `BackendProcess::readOutput()` 的 `logReceived` 与 `transcodeProgressChanged`。解释同一段标准输出怎样被分成“给人看”和“给程序处理”两条路径。
