# 第四课：用 QTimer 认识任务状态

本课已加入“模拟任务”区域：进度条、开始、停止和状态文字。模拟会每 200 毫秒前进 10%，约两秒完成。**这不是 FFmpeg 的实际转码进度，也不会生成视频文件。** 这节课要弄清窗口为什么在任务运行期间仍然可以响应。

## 1. 先运行

如果 CLion 里还开着上一课的 QtFFmpegStudio 窗口，先关闭那个窗口；Windows 会阻止构建程序覆盖正在运行的 exe。然后在 CLion 中重新加载 CMake、构建并运行。

按下面的顺序操作：

1. 不选文件，直接点“开始模拟”：路径检查会要求先选择输入视频。
2. 选择一个 MP4 和不同的输出 MP4 路径，然后点“开始模拟”。
3. 观察进度条从 0% 到 100%；运行中试着拖动窗口、点击“停止模拟”。
4. 运行中“开始模拟”、文件选择和参数控件会暂时禁用。点击停止后，它们恢复可用。
5. 再点“开始模拟”，进度从 0% 重新开始；完成后可以再次模拟。
6. 检查输出路径：本课不会创建输出文件。

选输出时，如果目标文件已存在，界面会提示后续真实转码前需要确认覆盖；本课模拟不会改动它。

## 2. 从 QTimer 看事件循环

打开 [mainwindow.cpp](../../src/mainwindow.cpp)，找到构造函数中的计时器：

```cpp
simulationTimer_ = new QTimer(this);
simulationTimer_->setInterval(200);
connect(simulationTimer_, &QTimer::timeout,
        this, &MainWindow::advanceSimulation);
```

`app.exec()` 启动 Qt 事件循环。计时器启动后，每隔约 200 毫秒发出一次 `timeout` 信号，Qt 再调用 `advanceSimulation()`。这个函数只改一次进度值，很快就返回；事件循环因此还能处理鼠标、键盘和窗口重绘。

`QTimer` 不会自动创建后台线程。若把真实的耗时转码放进 `advanceSimulation()`，窗口照样会卡住。后续接入自己的 FFmpeg 转码器时，会使用异步进程调用；本课先练习事件和状态。[Qt 的 QTimer 文档](https://doc.qt.io/qt-6.8/qtimer.html)说明了它与事件循环的关系。

## 3. 阅读按钮与状态的变化

在 [mainwindow.h](../../src/mainwindow.h) 中找到 `startSimulation()`、`stopSimulation()`、`advanceSimulation()` 和 `setSimulationRunning(bool)`。

- `startSimulation()` 先调用 `validatePaths()`。路径无效时直接返回；有效时将进度清零，禁用会改变任务配置的控件，启动计时器。
- `advanceSimulation()` 每次把进度加 10；到 100 后停止计时器，并显示“模拟任务完成”。
- `stopSimulation()` 停止计时器，保留当前进度，恢复控件，显示“模拟任务已停止”。
- `setSimulationRunning()` 把运行中与空闲时的控件状态集中设置，避免各个函数漏改某个按钮。

模拟开始时，“开始模拟”按钮被禁用，代码还会检查计时器是否已运行，因此重复点击不会重新创建一个模拟任务。停止后再开始，先把进度清为 0，形成重试。

计时器是 `MainWindow` 的子对象；关闭窗口时由 Qt 的对象所有权机制释放。它没有后端进程需要等待，这一点将在真实转码课发生变化。

## 4. 路径检查为什么复用

第二课的“检查路径”按钮已有输入输出验证。本课把验证主体放进 `validatePaths()`，按钮槽 `checkPaths()` 和 `startSimulation()` 共用它。这样，单独按“检查路径”和开始任务执行的是同一套规则。原先关于空路径、同文件、扩展名、输出目录的提示继续保留。

这里的 `validatePaths()` 返回 `bool`：通过为 `true`，拒绝为 `false`，并同时更新界面提示。它只检查文件路径是否满足本课程的基础要求，不能证明输入是可解码的视频。

## 5. 看测试怎样证明行为

打开 [test_simulation.cpp](../../tests/test_simulation.cpp)。测试用临时目录放一个带 `.mp4` 扩展名的路径夹具，这只是验证路径，不是可播放视频。三个检查分别覆盖：

- 空路径、输入输出相同时，不启动模拟。
- 合法路径下，事件循环继续处理另一个 `QTimer` 的回调；进度最终到 100%，输出文件仍不存在。
- 运行中按钮不接受重复启动；停止后进度保持，重试从 0% 开始。

验证先在缺少新控件时得到 3 项预期失败，加入功能后通过。当前项目有两组测试：第三课参数预览、第四课模拟任务。你可以在 PowerShell 中从项目根目录运行：

```powershell
$env:PATH = 'D:\QT\6.8.3\mingw_64\bin;D:\QT\Tools\mingw1310_64\bin;' + $env:PATH
& 'D:\CLion 2023.3.6\bin\cmake\win\x64\bin\cmake.exe' --build .\cmake-build-codex-lesson4
& 'D:\CLion 2023.3.6\bin\cmake\win\x64\bin\ctest.exe' --test-dir .\cmake-build-codex-lesson4 --output-on-failure
```

如果要在 CLion 原来的构建目录重新构建，先关掉正在运行的旧程序，再让 CLion Build。上述 `cmake-build-codex-lesson4` 是本轮独立验证目录，使用同一份源码。

## 6. 两个小练习

1. 找到 `simulationTimer_->setInterval(200)`，改为 `400` 后运行，观察总用时变化。思考：进度数值为什么还是按 10 增长？做完恢复为 `200`。
2. 开始模拟，在 30%～70% 时点“停止模拟”，再启动一次。说出哪些控件在运行中被禁用，以及这些设置集中在哪个函数。

完成这一课后，第五课会讨论怎样给原 `FFmpegProject` 增加单文件输入输出接口，再由 Qt 的 `QProcess` 异步调用它。那个阶段才有真实转码和真实完成/失败结果。

参考：[QTimer](https://doc.qt.io/qt-6.8/qtimer.html)、[QProgressBar](https://doc.qt.io/qt-6.8/qprogressbar.html)、[Qt 对象与线程](https://doc.qt.io/qt-6.8/threads-qobject.html)。
