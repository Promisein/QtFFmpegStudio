# 第六课：保存设置、任务状态与打包

本课运行后，关闭再打开程序仍能看到上次有效的文件路径、转码参数和窗口大小；真实转码成功后可以点击“打开输出目录”。你还可以把当前构建结果复制到独立目录，观察 Qt 程序运行需要哪些文件。

## 一、先在 CLion 操作

1. 关闭仍在运行的旧 QtFFmpegStudio 窗口，重新加载 CMake 并构建 `QtFFmpegStudio`。
2. 选择一个有视频和音轨的 MP4，设置输出路径，选 90°、1.50x、H.264 和 AAC，然后关闭窗口。
3. 再运行程序，确认输入、输出、参数和窗口大小恢复。如果输入文件已被移走，程序会忽略失效路径。
4. 执行一次真实转码，成功后点击“打开输出目录”；把输出路径改掉，按钮应重新禁用。
5. 试着在真实转码运行时关闭窗口，确认先出现“结束转码”提示；选择“否”继续运行，选择“是”则取消任务，待后端退出后关闭。

## 二、读代码：QSettings

从 [mainwindow.cpp](../../src/mainwindow.cpp) 的 `loadSettings()`、`saveSettings()` 开始。`QSettings` 按键值读写设置：这里保存 `files/input`、`files/output`、`options/*` 和 `window/size`。Windows 上的正式程序使用用户级原生设置；测试则向 `MainWindow` 传入独立的 INI 文件路径，所以不会改变你自己的设置。[Qt QSettings 文档](https://doc.qt.io/qt-6.8/qsettings.html)说明了 `value()`、`setValue()` 与 `sync()`。

恢复前逐项检查：输入必须仍是可读的 MP4 文件；输出目录必须存在且扩展名为 MP4；未知编码器、非法倍速和过小窗口尺寸都回到默认值。文件失效时仍保留有效的参数。这就是“保存有效设置”的含义。

你可以做一个实验：把输入视频移到别处后重新打开程序，观察输入栏清空；再放回原处并重新选择它。

## 三、读代码：任务状态

`TaskState` 有 `Idle`、`Simulating`、`Transcoding`、`Cancelling` 四个状态。`setTaskState()` 集中决定哪些按钮可点：模拟任务和真实转码不会同时启动；取消中的任务不能再次启动。状态描述的是**界面允许的操作**；后端是否仍在运行由 `BackendProcess::running()` 判断。

转码成功时，界面记录输出文件路径，只有文件仍存在且界面空闲，“打开输出目录”才可用。按钮调用 `QDesktopServices::openUrl(QUrl::fromLocalFile(...))`，让系统文件管理器打开目录。[Qt QDesktopServices 文档](https://doc.qt.io/qt-6.8/qdesktopservices.html)解释了本地文件 URL 的处理。

关闭窗口时，模拟任务直接停止；真实任务先询问，确认后等待后端退出。这个顺序让关闭动作与异步进程的生命周期一致。

## 四、打包练习

先构建当前项目，然后在项目根目录运行：

```powershell
& .\scripts\package.ps1
```

脚本默认读取 `cmake-build-codex-lesson4`，将两个 exe 和构建目录的 DLL 复制到新的 `dist/QtFFmpegStudio-日期时间` 目录，再调用 `windeployqt` 补齐 Qt 平台插件和其他 Qt 依赖。脚本会检查关键文件；目标目录已存在时会报错，以免覆盖旧打包结果。若你在 CLion 使用其他构建目录，传入 `-BuildDirectory`；Qt 或 MinGW 安装位置不同，可分别传 `-QtBin` 和 `-MinGWBin`。

打开生成目录，找出 `QtFFmpegStudio.exe`、`FFmpegProjectBackend.exe`、`Qt6Core.dll`、`avcodec-58.dll` 和 `platforms/qwindows.dll`。其中 `windeployqt` 负责 Qt 的运行依赖，FFmpeg 后端和它的 DLL 由本项目脚本复制。[Qt Windows 部署文档](https://doc.qt.io/qt-6.8/windows-deployment.html)说明了 `windeployqt` 的用途。

本机已检查打包目录中两个程序能在不额外添加 Qt、MinGW 目录到 PATH 时启动；跨电脑运行仍应在目标机器单独验证。

## 五、自己完成两个小练习

1. 在 `loadSettings()` 找到倍速范围检查。手动将测试 INI 的 `options/speed` 改为 `100`，解释程序为什么恢复为 `1.0x`。
2. 在 `setTaskState()` 中找出“取消转码”何时可点击。结合 `BackendProcess::finished` 信号，说出取消后按钮何时恢复。

项目测试命令：

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.8.3\mingw_64\bin;' + $env:PATH
cmake --build .\cmake-build-codex-lesson4
ctest --test-dir .\cmake-build-codex-lesson4 --output-on-failure
```

下一课按[学习路线](../01-roadmap.md)开始批量任务；届时需要为每个文件保存独立状态，不能把这节课单任务的状态直接当成批量队列。
