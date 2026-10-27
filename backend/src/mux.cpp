//
// Created by Jianing on 2025/12/22.
//
#include "mux.h"
#include "pipeline.h"
#include "ffmpeg_raii.h"
#include "logger.h"
#include "mux_progress.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

struct MuxTiming {
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;

    Clock::time_point started = Clock::now();
    Duration params_wait{};
    Duration open{};
    Duration header{};
    Duration video_pop{};
    Duration audio_pop{};
    Duration write{};
    Duration trailer{};
    Duration sleep{};
    Duration packet_wait{};
    int wait_calls = 0;
    size_t queue_peak = 0;
    Duration cleanup{};
    int sleep_calls = 0;

    void report(int video_packets, int audio_packets) const {
        const auto ms = [](Duration value) {
            return std::chrono::duration<double, std::milli>(value).count();
        };
        Logger::info("Mux", "性能统计: total_ms=" + std::to_string(ms(Clock::now() - started))
                     + " params_wait_ms=" + std::to_string(ms(params_wait))
                     + " open_ms=" + std::to_string(ms(open))
                     + " header_ms=" + std::to_string(ms(header))
                     + " video_pop_ms=" + std::to_string(ms(video_pop))
                     + " audio_pop_ms=" + std::to_string(ms(audio_pop))
                     + " write_ms=" + std::to_string(ms(write))
                     + " trailer_ms=" + std::to_string(ms(trailer))
                     + " wait_ms=" + std::to_string(ms(packet_wait))
                     + " wait_calls=" + std::to_string(wait_calls)
                     + " queue_peak=" + std::to_string(queue_peak)
                     + " sleep_ms=" + std::to_string(ms(sleep))
                     + " cleanup_ms=" + std::to_string(ms(cleanup))
                     + " sleep_calls=" + std::to_string(sleep_calls)
                     + " video_packets=" + std::to_string(video_packets)
                     + " audio_packets=" + std::to_string(audio_packets));
    }
};

void mux_thread(const std::string& output_file,
                AVRational output_time_base,
                double expected_output_duration_seconds,
                Pipeline* pipeline) {
    MuxTiming timing;
    Logger::info("Mux", std::string("开始创建输出文件: ") + output_file);

    AVCodecParameters* video_enc_par = nullptr;
    AVCodecParameters* audio_enc_par = nullptr;
    if (!pipeline) return;
    const auto params_wait_started = MuxTiming::Clock::now();
    const bool parameters_ready = pipeline->wait_for_encoder_parameters(video_enc_par, audio_enc_par);
    timing.params_wait += MuxTiming::Clock::now() - params_wait_started;
    if (!parameters_ready) return;

    AVFormatContext* out_fmt_ctx_raw = nullptr;
    char err_buf[1024];
    int ret = avformat_alloc_output_context2(&out_fmt_ctx_raw, nullptr, "mp4", output_file.c_str());
    if (ret < 0 || !out_fmt_ctx_raw) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[Mux] 分配输出上下文失败: ") + err_buf);
        return;
    }

    auto cleanup_output = [&](AVFormatContext* ctx) {
        if (ctx) {
            if (!(ctx->oformat->flags & AVFMT_NOFILE) && ctx->pb) {
                avio_closep(&ctx->pb);
            }
            avformat_free_context(ctx);
        }
    };

    AVStream* video_stream = avformat_new_stream(out_fmt_ctx_raw, nullptr);
    if (!video_stream) {
        if (pipeline) pipeline->report_error("[Mux] 创建视频流失败");
        cleanup_output(out_fmt_ctx_raw);
        return;
    }

    ret = avcodec_parameters_copy(video_stream->codecpar, video_enc_par);
    if (ret < 0) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[Mux] 复制视频编码参数失败: ") + err_buf);
        cleanup_output(out_fmt_ctx_raw);
        return;
    }

    video_stream->time_base = output_time_base;

    Logger::info("Mux", std::string("视频流配置: 分辨率=")
                 + std::to_string(video_stream->codecpar->width) + "x"
                 + std::to_string(video_stream->codecpar->height)
                 + ", 时间基=" + std::to_string(video_stream->time_base.num)
                 + "/" + std::to_string(video_stream->time_base.den));

    AVStream* audio_stream = nullptr;
    if (audio_enc_par && audio_enc_par->codec_type == AVMEDIA_TYPE_AUDIO) {
        audio_stream = avformat_new_stream(out_fmt_ctx_raw, nullptr);
        if (audio_stream) {
            ret = avcodec_parameters_copy(audio_stream->codecpar, audio_enc_par);
            if (ret < 0) {
                av_strerror(ret, err_buf, sizeof(err_buf));
                Logger::warn("Mux", std::string("复制音频编码参数失败: ") + err_buf);
                audio_stream = nullptr;
            } else {
                audio_stream->time_base = (AVRational){1, audio_enc_par->sample_rate};
                Logger::info("Mux", std::string("音频流配置: 采样率=")
                             + std::to_string(audio_stream->codecpar->sample_rate)
                             + ", 声道数=" + std::to_string(audio_stream->codecpar->channels));
            }
        }
    } else {
        Logger::info("Mux", "无音频流参数，仅处理视频");
    }

    if (!audio_stream) {
        pipeline->report_error("[Mux] 创建音频输出流失败");
        cleanup_output(out_fmt_ctx_raw);
        return;
    }
    av_dump_format(out_fmt_ctx_raw, 0, output_file.c_str(), 1);

    if (!(out_fmt_ctx_raw->oformat->flags & AVFMT_NOFILE)) {
        const auto open_started = MuxTiming::Clock::now();
        ret = avio_open(&out_fmt_ctx_raw->pb, output_file.c_str(), AVIO_FLAG_WRITE);
        timing.open += MuxTiming::Clock::now() - open_started;
        if (ret < 0) {
            av_strerror(ret, err_buf, sizeof(err_buf));
            if (pipeline) pipeline->report_error(std::string("[Mux] 打开输出文件失败: ") + err_buf);
            cleanup_output(out_fmt_ctx_raw);
            return;
        }
    }

    const auto header_started = MuxTiming::Clock::now();
    ret = avformat_write_header(out_fmt_ctx_raw, nullptr);
    timing.header += MuxTiming::Clock::now() - header_started;
    if (ret < 0) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[Mux] 写入文件头失败: ") + err_buf);
        cleanup_output(out_fmt_ctx_raw);
        return;
    }

    Logger::info("Mux", "开始写入数据包...");

    int video_packet_count = 0;
    int audio_packet_count = 0;
    int64_t audio_accumulated_samples = 0;
    const AVRational audio_encoder_time_base{1, audio_enc_par->sample_rate};
    MuxProgress overallProgress(expected_output_duration_seconds, true);
    PacketPtr pkt(av_packet_alloc());
    if (!pkt) {
        pipeline->report_error("[Mux] 分配数据包失败");
        cleanup_output(out_fmt_ctx_raw);
        return;
    }

    while (true) {
        bool video = false;
        const auto wait_started = MuxTiming::Clock::now();
        bool waited = false;
        const auto result = pipeline->mux_packets.pop(*pkt, video, output_time_base, audio_encoder_time_base, &waited);
        if (waited) {
            timing.packet_wait += MuxTiming::Clock::now() - wait_started;
            ++timing.wait_calls;
        }
        if (result == MuxPacketQueue::Result::Stopped) {
            if (!pipeline->has_error()) pipeline->report_error("[Mux] 处理已终止");
            break;
        }
        if (result == MuxPacketQueue::Result::End) break;
        pkt->stream_index = video ? video_stream->index : audio_stream->index;
        if (pkt->pts == AV_NOPTS_VALUE || pkt->dts == AV_NOPTS_VALUE) {
            pipeline->report_error("[Mux] 编码包缺少 PTS 或 DTS");
            av_packet_unref(pkt.get());
            break;
        }
        if (video) {
            av_packet_rescale_ts(pkt.get(), output_time_base, video_stream->time_base);
            ++video_packet_count;
        } else {
            if (pkt->duration <= 0) {
                pkt->duration = audio_stream->codecpar->frame_size > 0 ? audio_stream->codecpar->frame_size : 1024;
            }
            audio_accumulated_samples += pkt->duration;
            av_packet_rescale_ts(pkt.get(), audio_encoder_time_base, audio_stream->time_base);
            ++audio_packet_count;
        }
        const int stream_index = pkt->stream_index;
        const int64_t packet_pts = pkt->pts;
        const int64_t packet_duration = pkt->duration;
        const AVRational packet_time_base = out_fmt_ctx_raw->streams[stream_index]->time_base;
        const auto write_started = MuxTiming::Clock::now();
        ret = av_interleaved_write_frame(out_fmt_ctx_raw, pkt.get());
        timing.write += MuxTiming::Clock::now() - write_started;
        av_packet_unref(pkt.get());
        if (ret < 0) {
            av_strerror(ret, err_buf, sizeof(err_buf));
            pipeline->report_error(std::string("[Mux] 写入数据包失败: ") + err_buf);
            break;
        }
        if (packet_pts != AV_NOPTS_VALUE) {
            const double end_seconds = (packet_pts + std::max<int64_t>(packet_duration, 0)) * av_q2d(packet_time_base);
            if (const auto percent = overallProgress.observe(video, end_seconds)) Logger::progress("transcode", *percent);
        }
    }
    timing.queue_peak = pipeline->mux_packets.peak();

    // 错误终止 vs 正常收尾
    bool has_error = pipeline && pipeline->has_error();
    if (!has_error) {
        Logger::info("Mux", "写入文件尾...");
        const auto trailer_started = MuxTiming::Clock::now();
        ret = av_write_trailer(out_fmt_ctx_raw);
        timing.trailer += MuxTiming::Clock::now() - trailer_started;
        if (ret < 0) {
            av_strerror(ret, err_buf, sizeof(err_buf));
            Logger::warn("Mux", std::string("写入文件尾失败: ") + err_buf);
            if (pipeline) pipeline->report_error(std::string("[Mux] 写入文件尾失败: ") + err_buf);
            has_error = true;
        }
    }

    Logger::info("Mux", std::string("完成！文件: ") + output_file
                 + "，视频包: " + std::to_string(video_packet_count)
                 + "，音频包: " + std::to_string(audio_packet_count)
                 + "，音频累计: " + std::to_string(audio_accumulated_samples));

    const auto cleanup_started = MuxTiming::Clock::now();
    cleanup_output(out_fmt_ctx_raw);
    timing.cleanup += MuxTiming::Clock::now() - cleanup_started;
    timing.report(video_packet_count, audio_packet_count);

    // 错误时删除不完整的输出文件
    if (has_error) {
        std::remove(output_file.c_str());
        Logger::info("Mux", std::string("已删除不完整的输出文件: ") + output_file);
    }
}
