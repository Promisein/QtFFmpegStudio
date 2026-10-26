# 第十二课：批量任务行内进度条

## 这一课要看到什么

批量表格的“转码进度”列在收到有效进度后显示进度条及百分比。等待中的任务显示“—”；运行但还没有估算值时显示“估算中”；失败或取消后保留已处理百分比文字，状态列继续说明结果。运行中最高为 99%，成功完成后才显示 100%。

## 在 CLion 中操作

1. 关闭旧窗口，重新加载 CMake，构建并运行 `QtFFmpegStudio`。
2. 打开“批量队列”，添加两段较长的 MP4，设置“同时转码任务数”为 2，点击“开始队列”。观察两行进度条分别变化，状态列保持“运行中”。
3. 再试一项损坏的 MP4，观察它显示“失败”，不会变为 100%。尝试停止队列，观察运行项变为“已取消”、等待项保持没有进度条。
4. 窗口缩窄或拖动“转码进度”列宽，检查百分比可读性。播放成功输出，确认内容正常。

PowerShell 验证命令；若 CLion 使用别的构建目录，请替换目录名：

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.8.3\mingw_64\bin;' + $env:PATH
cmake --build .\cmake-build-codex-lesson4 --parallel 4
ctest --test-dir .\cmake-build-codex-lesson4 --output-on-failure
```

## 本课的 Qt 知识

[BatchTaskModel](../../src/batchtaskmodel.cpp)仍负责保存各项数据，`Qt::DisplayRole` 给出“20%”等文字；新增 `ProgressValueRole` 只在运行中有估算值或成功完成时返回 0～100 的整数。这样绘制逻辑不需要从中文文字中解析数字。状态变化和进度变化都会发出 `dataChanged()`，通知表格重绘。

[BatchProgressDelegate](../../src/batchprogressdelegate.h)继承 `QStyledItemDelegate`。它读取模型角色：有数值时用当前 Qt 样式绘制进度条和百分比，没有数值时沿用默认文字绘制。[BatchWidget](../../src/batchwidget.cpp)只把它安装在第 3 列（从 0 开始计数），其他列仍由 Qt 默认绘制。进度列可拖动调宽，初始宽度为 150 像素。

这里的“进度条”是表格单元格中的绘制结果，并没有为每个任务创建一个 `QProgressBar` 控件。模型保存数据、视图安排表格、代理负责画单元格，这就是本课要理解的 Model/View 分工。

## 自己练习

1. 找到 `BatchTaskModel::data()` 中 `Qt::DisplayRole` 与 `ProgressValueRole` 的分支。解释为什么等待项没有进度条，失败项仍能显示“20%”文字。
2. 在 `BatchProgressDelegate::paint()` 中找到有数值和没有数值的两条路径。试着调整进度列宽，观察文字是否仍清楚；暂时不要改变后端进度算法。

本课只改进批量列表的呈现；每项进度仍是整体转码位置的估算值，不是剩余时间预测。
