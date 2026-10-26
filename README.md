# QtFFmpegStudio

基于 Qt Widgets 和 FFmpeg 的 Windows 桌面视频转码工具。支持单文件转码与批量队列，转码任务由独立后端进程执行，界面保持响应。

## 功能

- 选择 MP4 输入和输出文件，设置旋转角度、播放倍速、视频编码器（MPEG-4 / H.264）与音频编码器（AC-3 / AAC）。
- 选择输入后异步分析视频信息，显示文件大小、时长、编码、分辨率、帧率和音轨信息；分析超时或失败会提示原因，并可点击“重新分析”。
- 批量添加任务，设置同时运行的任务数（1～3），分别查看任务状态、进度、耗时和日志。
- 显示整体转码进度；支持取消运行中的任务，并清理本次任务创建的临时输出。
- 复制、清空日志，保存常用设置；单文件成功后可打开输出目录。

目前处理带视频和音频流的 MP4 文件，输出为 MP4。进度依据后端封装阶段的时间戳估算；运行中最高显示 99%，成功完成后显示 100%。耗时是任务开始至后端结束的实际经过时间。

## 实现结构

```text
Qt Widgets GUI
  ├─ MainWindow：单文件操作与设置
  ├─ BatchWidget / BatchTaskModel：队列调度、任务状态与日志
  └─ BackendProcess：QProcess 启动、监听和取消后端
                     │
                     ▼
FFmpegProjectBackend.exe：解封装、解码、处理、编码、封装流水线
```

每个运行中的批量任务对应一个后端进程。后端通过标准输出发送日志与结构化进度消息；GUI 异步读取并更新界面。

## 构建与运行

当前项目面向 Windows，使用 C++17、CMake 3.27+、Qt 6.8.3 Widgets 和 Qt 配套的 MinGW 13.1。还需要 FFmpeg 4.4 的 Windows shared SDK，包含 `include/`、`lib/`、`bin/`；媒体分析和真实转码测试分别需要 `bin/ffprobe.exe` 与 `bin/ffmpeg.exe`。这些第三方文件不包含在仓库中。

以下命令中的路径请改成实际安装位置。`FFMPEG_ROOT` 未指定时默认查找 `../FFmpegProject/ffmpeg-4.4-full_build-shared`。

```powershell
$env:PATH = 'D:\Qt\Tools\mingw1310_64\bin;D:\Qt\6.8.3\mingw_64\bin;' + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=D:/Qt/Tools/mingw1310_64/bin/gcc.exe -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1310_64/bin/g++.exe -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/mingw_64 -DFFMPEG_ROOT=D:/path/to/ffmpeg-4.4-full_build-shared
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

构建后运行 `build/QtFFmpegStudio.exe`。`FFmpegProjectBackend.exe`、`ffprobe.exe` 和所需 DLL 会复制到构建目录。也可以在 CLion 中配置相同的 CMake 选项，构建并运行 `QtFFmpegStudio` 目标。

## 转码正确性验证

```powershell
powershell -File .\scripts\verify_transcode_correctness.ps1 -BuildDirectory .\build -FfmpegRoot D:\path\to\ffmpeg-4.4-full_build-shared
```

脚本在构建目录生成测试视频，检查普通转码、顺时针旋转、0.5x/2x 倍速和 60 秒素材的音视频时长、编码、完整解码、静音片段与旋转方向。结果保存在构建目录的 `correctness-日期时间/` 下。具体测试数据与范围见[验证记录](verification/2026-10-26-transcode-correctness.md)。

## 打包

```powershell
powershell -File .\scripts\package.ps1 -BuildDirectory .\build -QtBin D:\Qt\6.8.3\mingw_64\bin -MinGWBin D:\Qt\Tools\mingw1310_64\bin
```

脚本调用 `windeployqt`，将 GUI、后端、`ffprobe.exe` 及运行依赖放入 `dist/QtFFmpegStudio-日期时间/`。请在目标 Windows 机器上单独验证打包结果。

## 仓库目录

| 目录 | 内容 |
| --- | --- |
| `src/` | GUI、任务模型、进度解析和后端进程管理 |
| `backend/` | FFmpeg 转码流水线及独立进程入口 |
| `tests/` | Qt 界面、批量队列、真实转码与输出保护测试 |
| `scripts/` | Windows 打包脚本 |
| `verification/` | 可复现的输出正确性记录与已知边界 |

`input/`、`output/`、`dist/` 和构建目录属于本地数据或产物，不提交到仓库。
