#ifndef FFMPEGPROJECT_MUX_PACKET_QUEUE_H
#define FFMPEGPROJECT_MUX_PACKET_QUEUE_H

#include "ffmpeg_raii.h"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <algorithm>
extern "C" {
#include <libavutil/mathematics.h>
}

// 两路 FIFO 共用同一互斥锁和条件变量；只有 mux 消费。
class MuxPacketQueue {
public:
    enum class Result { Packet, End, Stopped };
    bool push(bool video, const AVPacket& packet) {
        PacketPtr copy(av_packet_clone(&packet));
        if (!copy) return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_ || (video ? video_done_ : audio_done_)) return false;
            auto& queue = video ? video_ : audio_;
            queue.push(std::move(copy));
            peak_ = std::max(peak_, video_.size() + audio_.size());
        }
        ready_.notify_one();
        return true;
    }
    void finish(bool video) {
        { std::lock_guard<std::mutex> lock(mutex_); (video ? video_done_ : audio_done_) = true; }
        ready_.notify_all();
    }
    void stop() {
        { std::lock_guard<std::mutex> lock(mutex_); stopped_ = true; }
        ready_.notify_all();
    }
    Result pop(AVPacket& packet, bool& video, AVRational video_tb, AVRational audio_tb, bool* waited = nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto ready = [this] {
            return stopped_ || ((video_done_ || !video_.empty()) && (audio_done_ || !audio_.empty()));
        };
        if (waited) *waited = !ready();
        ready_.wait(lock, ready);
        if (stopped_) return Result::Stopped;
        if (video_.empty() && audio_.empty()) return Result::End;
        if (video_.empty()) video = false;
        else if (audio_.empty()) video = true;
        else {
            const AVPacket* v = video_.front().get();
            const AVPacket* a = audio_.front().get();
            const int64_t vt = v->dts == AV_NOPTS_VALUE ? v->pts : v->dts;
            const int64_t at = a->dts == AV_NOPTS_VALUE ? a->pts : a->dts;
            video = vt == AV_NOPTS_VALUE || (at != AV_NOPTS_VALUE && av_compare_ts(vt, video_tb, at, audio_tb) <= 0);
        }
        auto& queue = video ? video_ : audio_;
        av_packet_move_ref(&packet, queue.front().get());
        queue.pop();
        return Result::Packet;
    }
    size_t peak() { std::lock_guard<std::mutex> lock(mutex_); return peak_; }
private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<PacketPtr> video_, audio_;
    bool video_done_ = false, audio_done_ = false, stopped_ = false;
    size_t peak_ = 0;
};

// 保留编码线程现有的每路生产接口。
class DeepCopyPacketQueue {
public:
    DeepCopyPacketQueue(MuxPacketQueue& queue, bool video) : queue_(queue), video_(video) {}
    bool push(const AVPacket& packet) { return queue_.push(video_, packet); }
    void mark_done() { queue_.finish(video_); }
    void stop() { queue_.stop(); }
private:
    MuxPacketQueue& queue_;
    bool video_;
};
#endif
