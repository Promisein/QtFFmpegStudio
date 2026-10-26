# 第八课：从后端日志显示解封装进度

> 历史课程说明：这一课记录第八课的实现过程。当前源码已改为结构化事件和整体转码估算进度；阅读下文代码导读时，请同时参考[第十一课](11-structured-progress.md)。

这一课给单文件转码加进度条，并在批量表格中为每项增加“解封装”列。数字来自后端已有的 `Demux 进度: N%` 日志，表示**读取输入的估算进度**。解封装到 100% 时，编码、封装和输出发布可能仍在进行；窗口只有收到后端成功退出、且确认输出文件存在后，才显示“转码完成”。

QtFFmpegStudio 继续作为 `FFmpegProject` 旁边的独立目录。这个课程不会初始化 Git 仓库，也不会修改旧项目；旧项目的 GitHub 地址仍是 [Promisein/FFmpegProject](https://github.com/Promisein/FFmpegProject)。

## 一、在 CLion 中一步步操作

1. 关闭仍在运行的旧 QtFFmpegStudio 窗口，打开 QtFFmpegStudio 项目，重新加载 CMake，构建 `QtFFmpegStudio`。根目录的 `main.cpp` 仍是应用入口，不需要在 `src` 再建一个 `main.cpp`。
2. 准备一个带视频和音轨的 MP4。在单文件页选择输入和输出，点击“开始真实转码”。观察“解封装进度”进度条和日志中的 `Demux 进度` 行。到 100% 后若任务尚未结束，状态会提示仍在等待编码、封装和输出发布。
3. 等状态显示“转码完成”，再检查输出文件并播放核对。打开输出目录按钮只有成功后才可用。
4. 在批量页加入两个 MP4。表格中的“解封装”列应分别更新；等待项显示 `—`。一项进入“运行中”时从 0% 开始，另一项仍保持等待。成功项最终显示 100%。
5. 用一个损坏的 MP4 或取消操作试失败路径：结果应是“失败”或“已取消”，不会仅因进度条曾到 100% 就显示成功。进度数值可以保留在失败行，供排查时参考。

CLion 中可以直接运行全部测试：

```powershell
cmake --build .\cmake-build-debug-mingw_qt_683 --parallel 4
ctest --test-dir .\cmake-build-debug-mingw_qt_683 --output-on-failure
```

若当前 CLion 配置与课程构建目录不同，把命令中的目录替换成你的实际构建目录。CTest 会在 Windows 构建目录内使用可写临时目录，并补齐测试程序所需的 Qt/MinGW DLL 搜索路径。

## 二、读代码：一条日志如何变成一个数字

1. [后端解封装代码](../../backend/src/demux.cpp)估算读取进度，Logger 输出文字行。它并不知道后续编码和封装什么时候结束。
2. [BackendProcess](../../src/backendprocess.cpp)接收 `QProcess::readyReadStandardOutput`，用 `QStringDecoder` 增量解码 UTF-8。原始文本仍通过 `logReceived` 送到日志框；同一份文本再交给 [TranscodeProgressParser](../../src/transcodeprogressparser.h)。
3. `TranscodeProgressParser::append()` 缓存不完整的一行，只识别后端当前格式的完整 `INFO` 行，忽略非法值、重复和倒退的百分比。每次 `BackendProcess::start()` 都调用 `reset()`，避免上一项的进度影响下一项。
4. 解析出新值后，`BackendProcess` 发出 `demuxProgressChanged(int)`。这是信号；[MainWindow](../../src/mainwindow.cpp)和[BatchWidget](../../src/batchwidget.cpp)分别连接它，更新进度条和当前批量行。这里没有阻塞等待子进程，Qt 事件循环仍可处理界面操作。
5. [BatchTaskModel](../../src/batchtaskmodel.cpp)把每项进度保存在自己的 `BatchTask` 中，状态和数值变化后发出 `dataChanged()` 通知表格刷新。运行项到 100% 时暂显示 `100%（收尾中）`，直到该项真正成功或失败。

```text
后端日志 → QProcess 标准输出 → UTF-8 解码 → 按行解析
         → demuxProgressChanged(int) → 单文件进度条 / 批量模型
后端 finished + 输出文件检查 → 转码结果
```

## 三、验证了什么

[解析器测试](../../tests/test_transcode_progress_parser.cpp)覆盖跨读取事件的半个中文字符、半行日志、无关/错误格式、重复/倒退值和任务重置。[单文件集成测试](../../tests/test_real_transcode.cpp)检查真实短视频会产生进度与输出，损坏输入不会显示成功。[批量测试](../../tests/test_batch_widget.cpp)检查每行独立更新和失败后继续；[模型测试](../../tests/test_batch_model.cpp)检查等待、运行、收尾和成功的表格文本。

第八课实现时使用已有人工日志格式，因此当时后端改动日志文字就需要同步调整解析器。第十一课已改为独立的结构化进度协议；当前百分比仍只描述解封装，不可用来估算整个转码还要多久。

## 四、两个练习

1. 在 `TranscodeProgressParser::append()` 中找到按换行切分的位置。解释为什么 `readyReadStandardOutput` 的一次回调不一定恰好对应一整行日志。
2. 在批量页让第一项解封装到 100% 后，观察其“运行中”和 `100%（收尾中）`。解释为什么此时第二项不会启动，以及哪一个信号触发下一项。
