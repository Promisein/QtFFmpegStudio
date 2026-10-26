# 第十四课：复制和清空当前日志

这一课为单文件和批量转码增加两个直接可用的日志操作：“复制日志”与“清空日志”。它们放在各自日志框上方，日志为空时不可点击，有新内容时自动启用。

## 在 CLion 中操作

1. 关闭旧窗口，重新加载 CMake 并构建、运行 `QtFFmpegStudio`。单文件页的日志框上方应有“复制日志”“清空日志”按钮。
2. 转码一个 MP4。日志出现后点击“复制日志”，粘贴到记事本，检查内容与当前日志框一致。点击“清空日志”，日志框变空，已完成的输出文件与任务结果不变。
3. 批量页加入两段 MP4，设置同时转码任务数为 2 并运行。点击第一行，使日志框只显示第一项；点击“复制日志”，粘贴后应没有第二项的日志。
4. 保持第一项选中，点击“清空日志”，第一项当前记录会被清空。切到“全部日志”应仍看得到第二项。再点击“清空日志”，全部保留记录会清空。
5. 若在任务运行时清空日志，后续新日志仍会出现；该按钮不会取消转码。批量页仍只保留最近 2000 行日志。

项目验证命令；若 CLion 用其他构建目录，请替换目录名：

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.8.3\mingw_64\bin;' + $env:PATH
cmake --build .\cmake-build-codex-lesson4 --parallel 4
ctest --test-dir .\cmake-build-codex-lesson4 --output-on-failure
```

## 读代码

[MainWindow](../../src/mainwindow.cpp)在单文件日志框上方放两个按钮。`QPlainTextEdit::textChanged` 更新按钮可用状态；复制按钮读取 `toPlainText()`，通过 `QGuiApplication::clipboard()->setText()` 写到系统剪贴板；清空按钮连接 `QPlainTextEdit::clear()`。每次启动新的单文件任务时，原有逻辑仍先清空日志。

[BatchWidget](../../src/batchwidget.cpp)的复制按钮读取当前日志框，所以选中任务时只复制该项，选“全部日志”时复制全部可见内容。批量日志不仅在文本框里，还保留带任务 ID 的记录；因此清空时必须同时删除匹配的记录。`clearVisibleLog()` 在任务视图删除该任务的记录，在“全部日志”视图删除所有记录，随后重建显示。任务的状态、进度和输出文件都不受影响。

## 自己练习

1. 比较两个“复制日志”按钮的连接，说明为什么用 `toPlainText()` 而不是文本框的 `copy()` 来复制整个当前视图。
2. 找到 `BatchWidget::clearVisibleLog()`。解释为什么只调用 `log_->clear()` 不够：切换“全部日志”后会发生什么？

顶部导航属于[第十三课后的界面修订](../updates/top-navigation.md)，本课正式编号为第十四课。
