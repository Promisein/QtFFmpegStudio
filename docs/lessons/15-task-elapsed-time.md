# 第十五课：任务耗时与结果摘要

本课让转码工具显示实际花了多少时间。这个数字是任务开始后到后端结束之间经过的时间，**不是按进度推算的剩余时间**。

## 在 CLion 中逐步操作

1. 关闭旧窗口，重新加载 CMake，构建并运行 `QtFFmpegStudio`。
2. 在“单文件转码”页选一个 MP4 并开始。进度条下方先显示“耗时：计时中”；任务结束后显示例如“耗时：1.2 秒”。再用损坏的 MP4 试一次，失败后也会显示本次实际经过的时间。
3. 在“批量队列”页加入两个有效 MP4，把同时转码任务数设为 2。两项结束后，结果列各自以“耗时 N.N 秒｜…”开头；这个数字属于对应任务，不会共用计时器。
4. 尝试让一项在启动前就被拒绝，例如添加后再占用它预定的输出文件名。该项只显示失败原因，不显示没有发生过的转码耗时。
5. 停止一个已运行的批量任务：已启动项显示取消前经过的时间；未启动的等待项没有耗时。

项目验证命令；如果 CLion 使用其他构建目录，请替换目录名：

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.8.3\mingw_64\bin;' + $env:PATH
cmake --build .\cmake-build-codex-lesson4 --parallel 4
ctest --test-dir .\cmake-build-codex-lesson4 --output-on-failure
```

## 读代码

[MainWindow](../../src/mainwindow.cpp)在调用 `BackendProcess::start()` 前启动一个 `QElapsedTimer`，收到 `finished` 后读取耗时、更新标签并让计时器失效。下一次开始任务会重新启动计时器，旧结果不会混入新任务。

[BatchWidget](../../src/batchwidget.cpp)用任务 ID 保存各自的 `QElapsedTimer`。启动某项时创建计时器；该项后端结束时，以同一个 ID 取回计时器，并把毫秒数交给 [BatchTaskModel](../../src/batchtaskmodel.cpp)。模型在结果列把耗时放在结果文字前；未启动的项保留 `-1`，仅显示原来的原因。短任务以一位小数的秒数呈现，格式化方法位于 [elapsedformat.h](../../src/elapsedformat.h)。

这里测量的是 GUI 观察到的整体任务用时，包括启动后端和输出确认，可能与后端日志中的纯转码流水线耗时略有不同。进度百分比仍用于表示处理位置，不参与耗时计算。

## 自己练习

1. 找到 `BatchWidget` 中插入和取出计时器的位置。说明并发数为 2 时，为什么计时器必须按任务 ID 保存。
2. 找到 `BatchTaskModel::resultText()`。解释 `elapsedMilliseconds == -1` 时为何不显示“0.0 秒”。
