//
// Pipeline: owns all inter-thread queues and error propagation state
//
#ifndef FFMPEGPROJECT_PIPELINE_H
#define FFMPEGPROJECT_PIPELINE_H

#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include "ffmpeg_raii.h"
#include "packet_queue.h"
#include "deep_copy_packey_queue.h"
#include "ring_buffer.h"

class Pipeline {
public:
    // --- Queues ---
    PacketQueue<AVPacket> video_pkt_queue;
    PacketQueue<AVPacket> audio_pkt_queue;
    RingBuffer<AVFrame*> video_frame_ringbuf{30};
    RingBuffer<AVFrame*> audio_frame_ringbuf{30};
    DeepCopyPacketQueue en_video_pkt_queue;
    DeepCopyPacketQueue en_audio_pkt_queue;

    // --- Error propagation ---
    void report_error(const std::string& msg);
    bool has_error() const;
    std::string get_error() const;
    bool publish_encoder_parameters(bool video, const AVCodecContext* context);
    bool wait_for_encoder_parameters(AVCodecParameters*& video, AVCodecParameters*& audio);

    // --- FPS / frame counters (atomic, written by worker threads) ---
    std::atomic<int64_t> demux_video_packets{0};
    std::atomic<int64_t> demux_audio_packets{0};
    std::atomic<int64_t> video_decoded_frames{0};
    std::atomic<int64_t> audio_decoded_frames{0};
    std::atomic<int64_t> video_encoded_frames{0};
    std::atomic<int64_t> audio_encoded_frames{0};

    // --- Progress (0.0 ~ 1.0, written by demuxer) ---
    std::atomic<double> progress{0.0};

private:
    void stop_all_queues();

    std::atomic<bool> error_flag_{false};
    mutable std::mutex error_mtx_;
    std::string error_msg_;
    std::mutex encoder_params_mtx_;
    std::condition_variable encoder_params_cv_;
    CodecParametersPtr video_encoder_params_;
    CodecParametersPtr audio_encoder_params_;
};

#endif // FFMPEGPROJECT_PIPELINE_H
