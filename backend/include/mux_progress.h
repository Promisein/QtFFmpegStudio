#ifndef QTFFMPEGSTUDIO_MUX_PROGRESS_H
#define QTFFMPEGSTUDIO_MUX_PROGRESS_H

#include <algorithm>
#include <cmath>
#include <optional>

// 已交给封装器的两路媒体时间估算；99% 留给文件尾、发布和进程确认。
class MuxProgress
{
public:
    MuxProgress(double expectedDurationSeconds, bool hasAudio)
        : expectedDurationSeconds_(expectedDurationSeconds), hasAudio_(hasAudio) {}

    std::optional<int> observe(bool isVideo, double endSeconds)
    {
        if (!std::isfinite(expectedDurationSeconds_) || expectedDurationSeconds_ <= 0
            || !std::isfinite(endSeconds) || endSeconds < 0) return std::nullopt;
        if (isVideo) {
            sawVideo_ = true;
            videoSeconds_ = std::max(videoSeconds_, endSeconds);
        } else {
            sawAudio_ = true;
            audioSeconds_ = std::max(audioSeconds_, endSeconds);
        }
        if (!sawVideo_ || (hasAudio_ && !sawAudio_)) return std::nullopt;
        const double completedSeconds = hasAudio_
            ? std::min(videoSeconds_, audioSeconds_) : videoSeconds_;
        const double estimate = completedSeconds / expectedDurationSeconds_ * 100.0;
        const int percent = estimate >= 99.0 ? 99 : static_cast<int>(std::floor(estimate));
        if (percent <= lastPercent_) return std::nullopt;
        lastPercent_ = percent;
        return percent;
    }

private:
    double expectedDurationSeconds_ = 0;
    bool hasAudio_ = false;
    bool sawVideo_ = false;
    bool sawAudio_ = false;
    double videoSeconds_ = 0;
    double audioSeconds_ = 0;
    int lastPercent_ = -1;
};

#endif
