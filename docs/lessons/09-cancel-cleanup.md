# 第九课：取消任务并清理本次临时输出

第八课能看到解封装进度；本课解决点击“取消转码”或“停止队列”后，后端进程和临时文件怎样收尾。现有架构仍是 Qt Widgets → `QProcess` → `FFmpegProjectBackend.exe`。第九课完成时批量队列仍按顺序运行；第十课新增 1～3 项有界并发。

## 一、在 CLion 中操作

1. 关闭旧窗口，重新加载 CMake，构建 `QtFFmpegStudio`。根目录的 `main.cpp` 是唯一应用入口。
2. 选一个稍长的 MP4，设置新的输出路径，点击“开始真实转码”。转码运行时点击“取消转码”：界面应恢复可启动状态，输出文件不能被当作成功结果。
3. 在批量页加入两个 MP4，开始队列，等第一项进入“运行中”再点“停止队列”。第一项标为“已取消”，第二项尚未运行，也标为“已取消”；汇总数与表格一致。
4. 打开输出目录观察：本次任务使用 `QFS-随机字符.tmp` 作为临时文件。取消完成后，它应消失。不要手工删除目录中来源不明的其他 `QFS*.tmp`，因为本程序只会自动清理它明确记录的本次路径。
5. 再用损坏的 MP4 试一次失败路径，然后用正常 MP4 完成一次成功转码；确认已有输出的覆盖保护及播放结果。

可以在项目根目录运行：

```powershell
cmake --build .\cmake-build-debug-mingw_qt_683 --parallel 4
ctest --test-dir .\cmake-build-debug-mingw_qt_683 --output-on-failure
```

若 CLion 使用另一构建目录，把命令中的目录替换为实际目录。课程验证使用 `cmake-build-codex-lesson4`，不会覆盖你正在运行的旧窗口。

## 二、为什么 Windows 上直接结束子进程

[Qt 6.8 的 QProcess 文档](https://doc.qt.io/qt-6.8/qprocess.html)说明：Windows 上 `terminate()` 发送 `WM_CLOSE`；没有处理该消息的控制台程序可能不会退出。本项目后端没有 Windows 消息循环，因此 [BackendProcess::cancel()](../../src/backendprocess.cpp) 在 Windows 上使用 `kill()`，再等待 `QProcess::finished` 信号，不在界面线程调用阻塞式等待。

`kill()` 会立即结束进程，后端 C++ 析构函数可能来不及运行。因此，单靠后端的 `TemporaryOutputCleanup` 无法保证取消后删除临时文件。本课把文件路径的所有权交给 GUI：

```text
GUI 在输出目录用 QTemporaryFile 预留唯一文件
  → 通过 --temp-output 把确切路径传给后端
  → 后端验证路径、转码并尝试发布最终文件
  → QProcess 退出后，GUI 只清理自己记录的路径
```

后端单独作为 CLI 使用时，仍可自行创建临时文件；这保持了旧命令的用法。GUI 传入路径时，[后端入口](../../backend/main.cpp)和[安全输出代码](../../backend/src/safe_output.cpp)会检查它在输出目录内、是空的常规文件、没有额外硬链接，也不是输入或最终输出。清理逻辑只针对这一次预留的路径，不遍历整个目录。

## 三、读状态变化

| 操作或事件 | 当前任务 | 尚未开始的批量任务 | 下一步 |
| --- | --- | --- | --- |
| 正常完成 | 成功，临时文件发布为最终 MP4 | 保持等待 | 启动下一项 |
| 转码失败 | 失败，临时文件清理 | 保持等待 | 启动下一项 |
| 单文件取消 | 已取消，进程退出后清理临时文件 | 不涉及 | 恢复控件 |
| 停止批量队列 | 运行项取消 | 全部标为已取消 | 不再启动下一项，汇总结果 |
| 确认关闭运行中的窗口 | 请求取消 | 全部标为已取消 | 进程退出后关闭窗口 |

[BatchWidget](../../src/batchwidget.cpp)的 `stopRequested_` 阻止后续调度，`cancelPending()` 更新等待行。[BackendProcess](../../src/backendprocess.cpp)在子进程退出后清理本次临时文件，再发出 `finished`，所以界面看到终态时，正常情况下清理也已经完成。

## 四、验证与练习

[单文件测试](../../tests/test_real_transcode.cpp)覆盖预留文件可供后端使用、不同目录的文件被拒绝、真实取消只清理本次文件而保留旁边的文件，以及失败路径。[批量测试](../../tests/test_batch_widget.cpp)会等运行项的临时文件出现后再停止，检查运行项、等待项与清理结果。原有[安全发布测试](../../tests/test_safe_output.cpp)继续检查已有最终输出不会被无意覆盖。

请自己完成两个练习：

1. 找到 `BackendProcess::start()` 中的 `QTemporaryFile`。解释为什么要先取得 `fileName()` 并关闭文件，再把路径传给后端。
2. 找到 `BatchWidget::cancelQueue()` 和 `startNext()`。解释“停止队列”为什么要同时取消正在运行的进程、标记等待项，并阻止下一项启动。

如果整个 GUI 进程被强制结束或电脑断电，GUI 来不及收到子进程退出信号，本课无法保证清理；这类异常恢复可作为后续独立功能。协作式取消也需要后端六线程流水线共同处理停止信号，本课先保持现有架构。
