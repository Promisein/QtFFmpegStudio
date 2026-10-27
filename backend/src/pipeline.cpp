//
// Pipeline implementation
//
#include "pipeline.h"

void Pipeline::report_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(error_mtx_);
    error_msg_ = msg;
    error_flag_.store(true, std::memory_order_release);
    stop_all_queues();
    encoder_params_cv_.notify_all();
}

bool Pipeline::publish_encoder_parameters(bool video, const AVCodecContext* context) {
    CodecParametersPtr parameters(avcodec_parameters_alloc());
    if (!parameters || avcodec_parameters_from_context(parameters.get(), context) < 0) {
        report_error("[Pipeline] 获取编码器参数失败");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(encoder_params_mtx_);
        if (video) video_encoder_params_ = std::move(parameters);
        else audio_encoder_params_ = std::move(parameters);
    }
    encoder_params_cv_.notify_all();
    return true;
}

bool Pipeline::wait_for_encoder_parameters(AVCodecParameters*& video, AVCodecParameters*& audio) {
    std::unique_lock<std::mutex> lock(encoder_params_mtx_);
    encoder_params_cv_.wait(lock, [this] {
        return has_error() || (video_encoder_params_ && audio_encoder_params_);
    });
    if (has_error()) return false;
    video = video_encoder_params_.get();
    audio = audio_encoder_params_.get();
    return true;
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
