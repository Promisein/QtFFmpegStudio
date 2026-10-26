# 第十三课后修订：固定顶部导航与应用标题

## 本课要解决什么

之前“真实转码 / 批量队列 / 模拟任务”标签位于所有输入和参数控件下面。切到批量页时，单文件控件收起，标签随之上移，视觉上会闪跳。这次把模式选择放到标题下方，让它始终处于同一位置。

## 在 CLion 中操作

1. 关闭正在运行的旧窗口，重新加载 CMake，构建并运行 `QtFFmpegStudio`。
2. 看窗口上方：左边是应用名 **QtFFmpegStudio**，右边是“FFmpeg 图形化转码工具”；下一行是三个模式入口。
3. 依次点击“单文件转码”“批量队列”“模拟任务”。观察模式入口不再上下移动；批量页仍保留共用转码参数，但不显示单文件输入、输出、路径检查和预览。
4. 在单文件页选择输入、输出路径，切到批量页再切回，检查路径没有被清空。开始一次单文件或批量转码，检查按钮、日志、进度及输出文件。
5. 调整窗口宽度，检查标题、模式入口和当前页面仍可读。

项目验证命令；如果你的 CLion 使用其他构建目录，替换目录名即可：

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.8.3\mingw_64\bin;' + $env:PATH
cmake --build .\cmake-build-codex-lesson4 --parallel 4
ctest --test-dir .\cmake-build-codex-lesson4 --output-on-failure
```

## 读代码：导航和页面分工

[MainWindow](../../src/mainwindow.cpp)用 `QTabBar` 单独显示模式入口，固定在标题下方。下方的 `QStackedWidget` 保存三个页面，同一时刻只显示一个。连接 `QTabBar::currentChanged` 到页面索引切换，并按当前模式决定单文件控件是否可见。

```text
应用标题
模式导航 QTabBar             ← 位置固定
单文件路径（批量页隐藏）
共用转码参数
单文件工具（批量页隐藏）
当前页面 QStackedWidget      ← 内容切换
```

原来的 `QTabWidget` 同时管理标签和页面，放在下面时会随上方控件高度变化而移动。现在把导航和页面分开，是为了解决这个具体布局问题；转码仍由原来的 `QProcess` 后端执行。

## 自己练习

1. 在 `MainWindow` 构造函数中找到 `modeNavigation` 和 `taskPages`。解释二者为什么用相同的索引顺序：单文件、批量、模拟。
2. 找到 `currentChanged` 连接，说明切到批量页时哪些控件被隐藏、哪些控件仍显示，以及为什么隐藏不会清空路径。
