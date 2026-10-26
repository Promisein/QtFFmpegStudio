//
// Created by Jianing on 2025/12/22.
//
#include "audioencoder.h"
#include "pipeline.h"
#include "ffmpeg_raii.h"
#include "logger.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <memory>
#include <limits>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/error.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

// frame_size 在开编码器后动态读取（AC3=1536, AAC=1024）

// SwrContext RAII 包装
struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const {
        if (ctx) swr_free(&ctx);
    }
};
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

void audio_encode_thread(AVCodecParameters* src_codec_par, AVRational output_time_base,
                         RingBuffer<AVFrame*>& in_rb, DeepCopyPacketQueue& out_q,
                         const ProcessingConfig& config,
                         Pipeline* pipeline) {
    if (!src_codec_par) {
        if (pipeline) pipeline->report_error("[AudioEncoder] 输入编码器参数为空指针");
        return;
    }

    double speed = config.speed_ratio;
    if (speed != 1.0) {
        Logger::info("AudioEncoder", std::string("倍速: ") + std::to_string(speed) + "x");
    }

    // 编码器选择
    AVCodecID codec_id;
    const char* codec_name = "";
    switch (config.audio_codec) {
        case AUDIO_CODEC_AAC:
            codec_id = AV_CODEC_ID_AAC;
            codec_name = "AAC";
            break;
        case AUDIO_CODEC_AC3:
        default:
            codec_id = AV_CODEC_ID_AC3;
            codec_name = "AC3";
            break;
    }

    const AVCodec* encoder = avcodec_find_encoder(codec_id);
    if (!encoder) {
        if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 找不到") + codec_name + "编码器");
        return;
    }

    CodecContextPtr enc_ctx(avcodec_alloc_context3(encoder));
    if (!enc_ctx) {
        if (pipeline) pipeline->report_error("[AudioEncoder] 分配音频编码器上下文失败");
        return;
    }

    enc_ctx->codec_id = codec_id;
    enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc_ctx->sample_rate = src_codec_par->sample_rate;
    enc_ctx->channel_layout = av_get_default_channel_layout(src_codec_par->channels);
    enc_ctx->channels = src_codec_par->channels;
    enc_ctx->bit_rate = 128000;
    enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
    // frame_size 在 avcodec_open2 后动态读取

    char err_buf[1024];
    int ret = avcodec_open2(enc_ctx.get(), encoder, nullptr);
    if (ret < 0) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 打开AC3编码器失败：") + err_buf);
        return;
    }

    // 读取编码器实际 frame_size
    int required_nb_samples = enc_ctx->frame_size;
    if (required_nb_samples <= 0) {
        required_nb_samples = (config.audio_codec == AUDIO_CODEC_AC3) ? 1536 : 1024;
        Logger::warn("AudioEncoder", std::string("编码器未报告frame_size，使用默认值: ")
                     + std::to_string(required_nb_samples));
    }

    Logger::info("AudioEncoder", std::string(codec_name) + "编码器打开成功（采样率："
                 + std::to_string(enc_ctx->sample_rate)
                 + "，声道数：" + std::to_string(enc_ctx->channels)
                 + "，帧大小：" + std::to_string(required_nb_samples) + "）");

    FramePtr input_frame(av_frame_alloc());
    FramePtr encode_frame(av_frame_alloc());
    PacketPtr pkt(av_packet_alloc());

    if (!input_frame || !encode_frame || !pkt) {
        if (pipeline) pipeline->report_error("[AudioEncoder] 分配Frame/Packet失败");
        return;
    }

    encode_frame->format = enc_ctx->sample_fmt;
    encode_frame->sample_rate = enc_ctx->sample_rate;
    encode_frame->channel_layout = enc_ctx->channel_layout;
    encode_frame->channels = enc_ctx->channels;
    encode_frame->nb_samples = required_nb_samples;

    ret = av_frame_get_buffer(encode_frame.get(), 0);
    if (ret < 0) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 分配编码帧缓冲区失败：") + err_buf);
        return;
    }

    // 重采样上下文（懒初始化）
    SwrContextPtr swr_ctx;
    bool need_resample = false;
    FramePtr resampled_frame(av_frame_alloc());
    using AudioFifoPtr = std::unique_ptr<AVAudioFifo, decltype(&av_audio_fifo_free)>;
    AudioFifoPtr fifo(av_audio_fifo_alloc(enc_ctx->sample_fmt, enc_ctx->channels,
                                         required_nb_samples * 2), &av_audio_fifo_free);
    if (!resampled_frame || !fifo) {
        if (pipeline) pipeline->report_error("[AudioEncoder] 分配音频缓冲区失败");
        return;
    }

    int input_frame_count = 0;
    int output_frame_count = 0;
    int64_t sample_counter = 0;
    int64_t input_sample_total = 0;
    int64_t selected_sample_total = 0;

    auto drainPackets = [&]() -> bool {
        while (true) {
            ret = avcodec_receive_packet(enc_ctx.get(), pkt.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
            if (ret < 0) {
                av_strerror(ret, err_buf, sizeof(err_buf));
                if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 接收编码包失败：") + err_buf);
                return false;
            }
            pkt->stream_index = 1;
            out_q.push(*pkt);
            av_packet_unref(pkt.get());
        }
    };

    auto encodeFromFifo = [&](int samples) -> bool {
        ret = av_frame_make_writable(encode_frame.get());
        if (ret < 0) {
            if (pipeline) pipeline->report_error("[AudioEncoder] 编码帧不可写");
            return false;
        }
        ret = av_samples_set_silence(encode_frame->extended_data, 0, required_nb_samples,
                                     enc_ctx->channels, enc_ctx->sample_fmt);
        if (ret < 0 || av_audio_fifo_read(fifo.get(),
                reinterpret_cast<void**>(encode_frame->extended_data), samples) != samples) {
            if (pipeline) pipeline->report_error("[AudioEncoder] 读取音频缓冲区失败");
            return false;
        }
        encode_frame->pts = sample_counter;
        sample_counter += required_nb_samples;
        ret = avcodec_send_frame(enc_ctx.get(), encode_frame.get());
        if (ret < 0) {
            av_strerror(ret, err_buf, sizeof(err_buf));
            if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 发送编码帧失败：") + err_buf);
            return false;
        }
        ++output_frame_count;
        return drainPackets();
    };

    while (true) {
        AVFrame* raw_frame = input_frame.get();
        bool success = in_rb.pop(raw_frame);
        if (!success) {
            Logger::info("AudioEncoder", "环形缓冲区已空，停止接收帧");
            break;
        }

        input_frame_count++;

        if (!input_frame->data[0] || input_frame->nb_samples <= 0) {
            Logger::warn("AudioEncoder", std::string("无效音频Frame（第")
                         + std::to_string(input_frame_count) + "帧），跳过");
            av_frame_unref(input_frame.get());
            continue;
        }

        // ========== 音频重采样: 解码格式 → FLTP (必须在倍速之前, 获取准确采样数) ==========
        AVFrame* source_frame = input_frame.get();
        bool src_is_fltp = (input_frame->format == AV_SAMPLE_FMT_FLTP);
        bool src_matches_layout = (input_frame->channel_layout == enc_ctx->channel_layout
                                   || input_frame->channels == enc_ctx->channels);
        bool src_matches_rate = (input_frame->sample_rate == enc_ctx->sample_rate);
        need_resample = !src_is_fltp || !src_matches_layout || !src_matches_rate;

        if (need_resample) {
            if (!swr_ctx) {
                int64_t in_ch_layout = input_frame->channel_layout;
                if (in_ch_layout == 0) {
                    in_ch_layout = av_get_default_channel_layout(input_frame->channels);
                }
                swr_ctx.reset(swr_alloc_set_opts(nullptr,
                    enc_ctx->channel_layout, enc_ctx->sample_fmt, enc_ctx->sample_rate,
                    in_ch_layout, static_cast<AVSampleFormat>(input_frame->format), input_frame->sample_rate,
                    0, nullptr));
                if (!swr_ctx) {
                    if (pipeline) pipeline->report_error("[AudioEncoder] 创建SwrContext失败");
                    return;
                }
                ret = swr_init(swr_ctx.get());
                if (ret < 0) {
                    av_strerror(ret, err_buf, sizeof(err_buf));
                    if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 初始化SwrContext失败：") + err_buf);
                    return;
                }
                Logger::info("AudioEncoder",
                    std::string("音频重采样已启用: ") + av_get_sample_fmt_name(static_cast<AVSampleFormat>(input_frame->format))
                    + " → FLTP, " + std::to_string(input_frame->sample_rate) + "Hz"
                    + ", ch_layout=" + std::to_string(in_ch_layout));
            }

            // 计算输出采样数
            int64_t delay = swr_get_delay(swr_ctx.get(), input_frame->sample_rate);
            int dst_nb = static_cast<int>(av_rescale_rnd(
                delay + input_frame->nb_samples,
                enc_ctx->sample_rate, input_frame->sample_rate, AV_ROUND_UP));

            av_frame_unref(resampled_frame.get());
            resampled_frame->format = enc_ctx->sample_fmt;
            resampled_frame->sample_rate = enc_ctx->sample_rate;
            resampled_frame->channel_layout = enc_ctx->channel_layout;
            resampled_frame->channels = enc_ctx->channels;
            resampled_frame->nb_samples = dst_nb;

            ret = av_frame_get_buffer(resampled_frame.get(), 0);
            if (ret < 0) {
                if (pipeline) pipeline->report_error("[AudioEncoder] 分配重采样帧缓冲区失败");
                return;
            }

            ret = swr_convert_frame(swr_ctx.get(), resampled_frame.get(), input_frame.get());
            if (ret < 0) {
                av_strerror(ret, err_buf, sizeof(err_buf));
                if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 音频重采样失败：") + err_buf);
                return;
            }
            source_frame = resampled_frame.get();
        }

        // 按样本选择快放/慢放数据，再把任意大小的解码帧拼成编码器要求的帧。
        const int64_t first_input_sample = input_sample_total;
        input_sample_total += source_frame->nb_samples;
        const double wanted = input_sample_total / speed;
        if (wanted > static_cast<double>(std::numeric_limits<int64_t>::max())) {
            if (pipeline) pipeline->report_error("[AudioEncoder] 倍速产生的采样数超出范围");
            return;
        }
        const int64_t target_samples = static_cast<int64_t>(wanted);
        const int64_t selection_count = target_samples - selected_sample_total;
        if (selection_count < 0 || selection_count > std::numeric_limits<int>::max()) {
            if (pipeline) pipeline->report_error("[AudioEncoder] 单帧采样数超出范围");
            return;
        }
        if (selection_count > 0) {
            AVFrame* selected = source_frame;
            FramePtr speed_frame;
            if (speed != 1.0) {
                speed_frame.reset(av_frame_alloc());
                if (!speed_frame) {
                    if (pipeline) pipeline->report_error("[AudioEncoder] 分配倍速帧失败");
                    return;
                }
                speed_frame->format = enc_ctx->sample_fmt;
                speed_frame->sample_rate = enc_ctx->sample_rate;
                speed_frame->channel_layout = enc_ctx->channel_layout;
                speed_frame->channels = enc_ctx->channels;
                speed_frame->nb_samples = static_cast<int>(selection_count);
                if (av_frame_get_buffer(speed_frame.get(), 0) < 0) {
                    if (pipeline) pipeline->report_error("[AudioEncoder] 分配倍速采样缓冲区失败");
                    return;
                }
                for (int ch = 0; ch < enc_ctx->channels; ++ch) {
                    const auto* src = reinterpret_cast<const float*>(source_frame->extended_data[ch]);
                    auto* dst = reinterpret_cast<float*>(speed_frame->extended_data[ch]);
                    for (int64_t index = 0; index < selection_count; ++index) {
                        int64_t source_index = static_cast<int64_t>(
                            (selected_sample_total + index) * speed) - first_input_sample;
                        source_index = std::max<int64_t>(0,
                            std::min<int64_t>(source_index, source_frame->nb_samples - 1));
                        dst[index] = src[source_index];
                    }
                }
                selected = speed_frame.get();
            }
            const int count = static_cast<int>(selection_count);
            if (av_audio_fifo_realloc(fifo.get(), av_audio_fifo_size(fifo.get()) + count) < 0
                || av_audio_fifo_write(fifo.get(),
                    reinterpret_cast<void**>(selected->extended_data), count) != count) {
                if (pipeline) pipeline->report_error("[AudioEncoder] 写入音频缓冲区失败");
                return;
            }
            selected_sample_total += selection_count;
            while (av_audio_fifo_size(fifo.get()) >= required_nb_samples) {
                if (!encodeFromFifo(required_nb_samples)) return;
            }
        }

        av_frame_unref(input_frame.get());
    }

    Logger::info("AudioEncoder", std::string("开始刷新编码器剩余数据（输入")
                 + std::to_string(input_frame_count) + "帧，输出"
                 + std::to_string(output_frame_count) + "帧）");
    const int remaining = av_audio_fifo_size(fifo.get());
    if (remaining > 0 && !encodeFromFifo(remaining)) return;
    ret = avcodec_send_frame(enc_ctx.get(), nullptr);
    if (ret < 0) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[AudioEncoder] 刷新编码器失败：") + err_buf);
        return;
    }
    if (!drainPackets()) return;

    out_q.mark_done();
    if (pipeline) pipeline->audio_encoded_frames.store(output_frame_count, std::memory_order_release);
    Logger::info("AudioEncoder", std::string("音频编码线程退出，输入")
                 + std::to_string(input_frame_count) + "帧，输出"
                 + std::to_string(output_frame_count) + "帧");
}
