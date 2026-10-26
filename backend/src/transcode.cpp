#include "transcode.h"
#include <iostream>
#include <thread>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <filesystem>
#include "demux.h"
#include "videodecoder.h"
#include "audiodecoder.h"
#include "videoencoder.h"
#include "audioencoder.h"
#include "mux.h"
#include "pipeline.h"
#include "ffmpeg_raii.h"
#include "config.h"
#include "thread_pool.h"
#include "logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#include <windows.h>

namespace fs = std::filesystem;

// ====================== 单文件转码任务 ======================


TranscodeResult transcode_single(const std::string& input_path,
                                  const std::string& output_path,
                                  const ProcessingConfig& config) {
    TranscodeResult result;
    result.filename = input_path;

    std::string output_file = output_path;

    Logger::info("main", std::string("开始转码: ") + input_path + " -> " + output_file);

    // 打开输入文件
    AVFormatContext* fmt_ctx_raw = nullptr;
    if (avformat_open_input(&fmt_ctx_raw, input_path.c_str(), nullptr, nullptr) < 0) {
        result.success = false;
        result.error = "打开输入文件失败";
        return result;
    }
    InputFormatContextPtr fmt_ctx(fmt_ctx_raw);
    if (avformat_find_stream_info(fmt_ctx.get(), nullptr) < 0) {
        result.success = false;
        result.error = "获取媒体流信息失败";
        return result;
    }

    // 查找流索引
    int video_stream_idx = -1, audio_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_idx = i;
        else if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            audio_stream_idx = i;
    }
    if (video_stream_idx == -1 || audio_stream_idx == -1) {
        result.success = false;
        result.error = "未找到视频/音频流";
        return result;
    }

    AVCodecParameters* video_dec_par = fmt_ctx->streams[video_stream_idx]->codecpar;
    AVCodecParameters* audio_dec_par = fmt_ctx->streams[audio_stream_idx]->codecpar;

    // 确定输出分辨率
    int out_width = video_dec_par->width;
    int out_height = video_dec_par->height;
    if (config.rotate == ROTATE_90_CW || config.rotate == ROTATE_270_CW) {
        std::swap(out_width, out_height);
    }

    // 视频编码参数（根据 codec 动态设置）
    CodecParametersPtr video_enc_params(avcodec_parameters_alloc());
    video_enc_params->codec_type = AVMEDIA_TYPE_VIDEO;
    video_enc_params->width = out_width;
    video_enc_params->height = out_height;
    video_enc_params->format = AV_PIX_FMT_YUV420P;
    video_enc_params->bit_rate = 1000000;

    if (config.video_codec == VIDEO_CODEC_H264) {
        video_enc_params->codec_id = AV_CODEC_ID_H264;
        video_enc_params->codec_tag = 0;
    } else {
        video_enc_params->codec_id = AV_CODEC_ID_MPEG4;
        video_enc_params->codec_tag = 0x7634706d;  // 'mp4v'
    }

    // 音频编码参数（根据 codec 动态设置）
    CodecParametersPtr audio_enc_params(avcodec_parameters_alloc());
    audio_enc_params->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_enc_params->sample_rate = audio_dec_par->sample_rate;
    audio_enc_params->channels = audio_dec_par->channels;
    audio_enc_params->channel_layout = av_get_default_channel_layout(audio_dec_par->channels);
    audio_enc_params->format = AV_SAMPLE_FMT_FLTP;
    audio_enc_params->bit_rate = 128000;
    audio_enc_params->codec_id = (config.audio_codec == AUDIO_CODEC_AAC)
        ? AV_CODEC_ID_AAC : AV_CODEC_ID_AC3;
    audio_enc_params->frame_size = (config.audio_codec == AUDIO_CODEC_AAC) ? 1024 : 1536;

    // 输出时间基
    AVRational input_frame_rate = fmt_ctx->streams[video_stream_idx]->r_frame_rate;
    if (input_frame_rate.num == 0 || input_frame_rate.den == 0) {
        input_frame_rate = (AVRational){25, 1};
    }
    AVRational output_time_base = av_inv_q(input_frame_rate);
    const double expected_output_duration_seconds = fmt_ctx->duration > 0
        ? (fmt_ctx->duration / static_cast<double>(AV_TIME_BASE)) / config.speed_ratio
        : 0.0;

    // 创建独立的 Pipeline（每个任务有自己的一套队列）
    Pipeline pipeline;

    // 启动计时
    auto start_time = std::chrono::steady_clock::now();

    // 启动 6 个转码线程
    std::thread demux_th(demux_thread,
                         fmt_ctx.get(), video_stream_idx, audio_stream_idx,
                         std::ref(pipeline.video_pkt_queue),
                         std::ref(pipeline.audio_pkt_queue),
                         &pipeline);

    std::thread video_dec_th(video_decode_thread, video_dec_par,
                             std::ref(pipeline.video_pkt_queue),
                             std::ref(pipeline.video_frame_ringbuf),
                             &pipeline);
    std::thread audio_dec_th(audio_decode_thread, audio_dec_par,
                             std::ref(pipeline.audio_pkt_queue),
                             std::ref(pipeline.audio_frame_ringbuf),
                             &pipeline);

    std::thread video_enc_th(video_encode_thread, video_dec_par, output_time_base,
                             std::ref(pipeline.video_frame_ringbuf),
                             std::ref(pipeline.en_video_pkt_queue),
                             config, &pipeline);
    std::thread audio_enc_th(audio_encode_thread, audio_dec_par, output_time_base,
                             std::ref(pipeline.audio_frame_ringbuf),
                             std::ref(pipeline.en_audio_pkt_queue),
                             config, &pipeline);

    std::thread mux_th(mux_thread, output_file,
                       video_enc_params.get(), audio_enc_params.get(), output_time_base,
                       expected_output_duration_seconds,
                       std::ref(pipeline.en_video_pkt_queue),
                       std::ref(pipeline.en_audio_pkt_queue),
                       config, &pipeline);

    // 等待当前任务的所有线程完成
    demux_th.join();
    video_dec_th.join();
    audio_dec_th.join();
    video_enc_th.join();
    audio_enc_th.join();
    mux_th.join();

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(end_time - start_time).count();

    // FPS 统计
    if (elapsed_s > 0) {
        int64_t vdec = pipeline.video_decoded_frames.load(std::memory_order_acquire);
        int64_t adec = pipeline.audio_decoded_frames.load(std::memory_order_acquire);
        int64_t venc = pipeline.video_encoded_frames.load(std::memory_order_acquire);
        int64_t aenc = pipeline.audio_encoded_frames.load(std::memory_order_acquire);
        Logger::info("main", std::string("性能统计 (耗时 ") + std::to_string(elapsed_s) + "s):");
        Logger::info("main", std::string("  视频解码=") + std::to_string(static_cast<int>(vdec / elapsed_s))
                     + " fps, 音频解码=" + std::to_string(static_cast<int>(adec / elapsed_s))
                     + " fps");
        Logger::info("main", std::string("  视频编码=") + std::to_string(static_cast<int>(venc / elapsed_s))
                     + " fps, 音频编码=" + std::to_string(static_cast<int>(aenc / elapsed_s)) + " fps");
    }

    if (pipeline.has_error()) {
        result.success = false;
        result.error = pipeline.get_error();
        return result;
    }

    result.success = true;
    Logger::info("main", std::string("完成: ") + input_path);
    return result;
}
