//
// Pipeline implementation
//
#include "pipeline.h"

void Pipeline::report_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(error_mtx_);
    error_msg_ = msg;
    error_flag_.store(true, std::memory_order_release);
    stop_all_queues();
}

void Pipeline::stop_all_queues() {
    video_pkt_queue.stop();
    audio_pkt_queue.stop();
    video_frame_ringbuf.stop();
    audio_frame_ringbuf.stop();
    en_video_pkt_queue.stop();
    en_audio_pkt_queue.stop();
}

bool Pipeline::has_error() const {
    return error_flag_.load(std::memory_order_acquire);
}

std::string Pipeline::get_error() const {
    std::lock_guard<std::mutex> lock(error_mtx_);
    return error_msg_;
}
