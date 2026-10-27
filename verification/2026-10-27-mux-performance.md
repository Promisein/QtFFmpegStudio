# Mux 性能测量（2026-10-27）

## 目的与口径

本次只增加计时，没有改变转码处理、队列或封装逻辑。`Mux 性能统计` 在每个成功的后端任务日志中输出：

- `total_ms`：mux 线程从入口到输出上下文清理结束的墙钟时间。
- `params_wait_ms`：等待编码器参数；`video_pop_ms` / `audio_pop_ms`：阻塞取包调用的时间，包含等待及出队。
- `open_ms`、`header_ms`、`write_ms`、`trailer_ms`、`cleanup_ms`：对应 FFmpeg/IO 调用的累计墙钟时间。`write_ms` 仅统计 `av_interleaved_write_frame()`。
- `sleep_ms` / `sleep_calls`：现有 `sleep_for(1ms)` 的实际累计时间与调用次数。

这些计时属于同一个 mux 线程；未列出的少量时间包括格式配置、时间戳处理、日志与循环开销。进程墙钟时间由外部秒表测量，包括启动、后端初始化、输出发布与退出。

## 环境与方法

- Windows 11；AMD Ryzen 9 8945HX；NVIDIA GeForce RTX 5060 Laptop GPU，驱动 582.05。
- 项目 Git 基线 `4a104c5`，加上工作区中尚未提交的 NVENC/UI 实现与本次计时改动。
- FFmpeg / ffprobe 4.4 full build；Qt 6.8.3；MinGW GCC 13.1.0。
- 输入由脚本用 FFmpeg 4.4 自动生成：`testsrc2=size=1080x1728:rate=30:duration=4` 与 48 kHz、440 Hz 正弦音频，MPEG-4/AAC，1080×1728，4.000 秒、120 帧，本次生成文件为 7,359,991 字节。生成命令在脚本中，可从新检出的仓库独立复现。
- 每次使用独立输出：`-vcodec h264 -vbackend software|nvenc -acodec aac -speed 1`，无旋转。CPU 使用现有 medium/CRF 23；NVENC 使用现有 p4/VBR/CQ 23；两者输出画质没有校准为相同水平。
- 每种模式先预热 1 次，再交替顺序测量 3 次。脚本：[measure_mux_performance.ps1](../scripts/measure_mux_performance.ps1)。原始数据：[CSV](2026-10-27-mux-performance-runs.csv)。单次任务顺序执行，未测批量并发。

复现命令（在仓库根目录运行）：

```powershell
.\scripts\measure_mux_performance.ps1 -BuildDirectory .\build-codex-nvenc-ui -FfmpegRoot ..\FFmpegProject\ffmpeg-4.4-full_build-shared -Runs 3
```

## 结果

单位均为毫秒。各列中位数分别计算，因此同一行的各项相加不一定等于总计。

| 编码后端 | 进程墙钟中位数 | mux 总计中位数 | 等参数 | 取视频包 | 取音频包 | 写包 | 写文件尾 | 固定休眠 | 休眠次数 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU H.264 | 3327.8 | 3250.1 | 9.8 | 266.1 | 0.6 | 12.5 | 0.4 | 2956.4 | 189 |
| NVENC H.264 | 3455.1 | 3365.8 | 301.3 | 64.8 | 0.6 | 16.6 | 0.6 | 2970.9 | 189 |

固定休眠占 mux 线程时间的约 **91.0%（CPU）/ 88.3%（NVENC）**；每次请求 1 ms 的休眠实际平均约 **15.6 / 15.7 ms**。`av_interleaved_write_frame()` 累计只占约 **0.38% / 0.49%**。这是在本机观察到的墙钟时间，不能直接推断移除休眠后端到端会缩短相同毫秒数；上游供包等待可能随之转移到 `pop()` 计时。

本次三轮 CPU 进程耗时为 3364.8、3324.8、3327.8 ms；NVENC 为 3427.8、3455.1、3580.6 ms。此合成素材与当前编码参数下，NVENC 的进程耗时中位数约高 3.8%，不代表真实视频、其他分辨率、时长或参数下的性能。

## 输出验证与结论

六个测量输出均为 H.264/AAC MP4，视频均保留输入的 120 帧；视频时长约 3.967 秒，音频时长约 4.032 秒，两路起始时间差为 0，结束时间差约 0.065 秒。每个输出均通过 ffprobe 检查和 FFmpeg 完整音视频解码。真实转码计时日志的自动测试以及项目完整 CTest 集合 11/11 通过。

当前可确认的主要 mux 线程耗时来自代码中的固定休眠，写包调用在本素材上不是主要耗时项。下一步应做受控对照：移除这处固定休眠，保持其余代码和同一输入不变，重新测量进程墙钟、队列等待与输出正确性。届时才能确定可获得的实际提速。
