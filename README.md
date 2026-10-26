# QtFFmpegStudio

将现有 C++/FFmpeg 转码项目逐步演化为 Qt 桌面转码工具，同时学习 Qt。

当前阶段：**第十五课增加实际转码耗时**。单文件任务完成后显示耗时；批量列表在每项结果前显示耗时，启动前被拒绝的任务不显示虚构的计时。顶部导航、按任务日志和 1～3 项批量并发继续可用。

## 目标

选择一个 MP4 文件 → 选择输出位置 → 设置旋转、倍速与编码器 → 后台转码 → 查看日志与结果。

用户已确认先做转码工具。当前使用 C++17 + Qt 6.8.3 Widgets + CMake + Qt 配套 MinGW 13.1，并手写控件与布局以学习信号、槽和对象关系。批量队列由多个独立 `QProcess` 有界并发执行；核心库直接接入仍是后续课程。

## 从这里开始

1. [学习路线](docs/01-roadmap.md)：每一阶段学什么、做什么、如何验收。
2. [第一课：主窗口与信号槽](docs/lessons/01-first-window.md)：已完成，保留作复习。
3. [第二课：MainWindow 类与文件选择](docs/lessons/02-file-selection.md)：复习文件选择与路径检查。
4. [第三课：从控件得到转码配置](docs/lessons/03-parameter-controls.md)：复习参数预览。
5. [第四课：用 QTimer 认识任务状态](docs/lessons/04-simulated-task.md)：复习模拟任务与窗口响应。
6. [第五课：用 QProcess 调用自己的 FFmpeg 后端](docs/lessons/05-real-transcode.md)：实机操作、代码导读和练习。
7. [第六课：保存设置、任务状态与打包](docs/lessons/06-engineering-polish.md)：CLion 操作和练习。
8. [第七课：批量转码队列](docs/lessons/07-batch-queue.md)：任务表格、顺序执行和练习。
9. [第八课：解封装进度](docs/lessons/08-demux-progress.md)：本课操作、代码导读、验证和练习。
10. [第九课：取消与临时输出清理](docs/lessons/09-cancel-cleanup.md)：取消流程、文件所有权和练习。
11. [第十课：有界批量并发](docs/lessons/10-bounded-concurrency.md)：进程调度、任务归属和练习。
12. [第十一课：结构化进度协议](docs/lessons/11-structured-progress.md)：后端事件、Qt JSON 解析和练习。
13. [第十二课：批量任务行内进度条](docs/lessons/12-batch-progress-bars.md)：模型角色、绘制代理和验收练习。
14. [第十三课：按任务查看批量日志](docs/lessons/13-per-task-batch-logs.md)：任务选择、日志筛选与批量页布局。
15. [第十三课后修订：顶部导航与标题](docs/updates/top-navigation.md)：固定模式切换位置、页面堆叠与界面验收。
16. [第十四课：日志复制与清空](docs/lessons/14-log-tools.md)：操作当前日志、Qt 剪贴板与信号槽。
17. [第十五课：任务耗时与结果摘要](docs/lessons/15-task-elapsed-time.md)：独立任务计时、结果展示和练习。

## 构建

需要 Windows、CMake 3.27+、Qt 6.8.3 Widgets、Qt 配套 MinGW 13.1，以及 FFmpeg 4.4 的 Windows shared SDK（含头文件、导入库和 DLL；运行真实转码测试还需要其 `bin/ffmpeg.exe`）。项目保留本机开发时的 Qt 默认路径 `D:/Qt/6.8.3/mingw_64`；其他安装位置请在 CMake 配置中设置 `CMAKE_PREFIX_PATH`。后端所需 SDK 由 `FFMPEG_ROOT` 指定，默认查找兄弟目录 `../FFmpegProject/ffmpeg-4.4-full_build-shared`。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/mingw_64 -DFFMPEG_ROOT=D:/path/to/ffmpeg-4.4-full_build-shared
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

`input/`、`output/`、`dist/` 和构建目录是本地数据或产物，不属于源码仓库。可用 `powershell -File .\scripts\package.ps1 -BuildDirectory .\build` 从已经构建的目录生成 Windows 分发目录。

## 目录

```text
QtFFmpegStudio/
  README.md
  CMakeLists.txt
  main.cpp               保留在根目录，作为唯一应用入口
  docs/                  学习路线与课程文档
  src/                   MainWindow、转码选项、后端进程、进度解析、批量模型与队列界面
  backend/               从原项目复制的流水线快照与单文件后端入口
  tests/                 参数预览、模拟任务、单文件转码和批量队列测试
  scripts/               Windows 打包脚本
  dist/                  本机打包输出；已加入 .gitignore
```

兄弟目录 `FFmpegProject` 是已有转码项目，`QtHelloWorld` 是已跑通的 Qt 入门项目；当前课程只修改 QtFFmpegStudio。

## 学习方式

每次推进一个可验收的小功能：先说明目标与涉及的 Qt 概念，再解释要改的文件，然后运行验证，最后用一两个小练习检查理解。遇到错误时保留完整错误信息和当前步骤，先定位原因再继续。

现在先关闭 CLion 中仍在运行的旧 QtFFmpegStudio 窗口，再重新加载 CMake、构建 GUI。按第十五课文档检查单文件和批量任务的实际耗时。
