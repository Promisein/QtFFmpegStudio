//
// Created by Jianing on 2025/12/22.
//
#include "videoencoder.h"
#include "pipeline.h"
#include "ffmpeg_raii.h"
#include "video_rotate.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <limits>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

// SwsContext RAII 包装
struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) sws_freeContext(ctx);
    }
};
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

void video_encode_thread(AVCodecParameters* src_codec_par, AVRational output_time_base, AVRational input_frame_rate,
                         RingBuffer<AVFrame*>& in_rb, DeepCopyPacketQueue& out_q,
                         const ProcessingConfig& config,
                         Pipeline* pipeline) {
    if (!src_codec_par) {
        if (pipeline) pipeline->report_error("[VideoEncoder] 输入编码器参数为空指针");
        return;
    }

    // 编码器选择
    AVCodecID codec_id;
    const char* codec_name = "";
    switch (config.video_codec) {
        case VIDEO_CODEC_H264:
            codec_id = AV_CODEC_ID_H264;
            codec_name = "H264";
            break;
        case VIDEO_CODEC_MPEG4:
        default:
            codec_id = AV_CODEC_ID_MPEG4;
            codec_name = "MPEG4";
            break;
    }

    const bool use_nvenc = config.video_backend == VIDEO_BACKEND_NVENC;
    if (use_nvenc && codec_id != AV_CODEC_ID_H264) {
        if (pipeline) pipeline->report_error("[VideoEncoder] NVENC 后端仅支持 H.264");
        return;
    }
    const AVCodec* encoder = use_nvenc
        ? avcodec_find_encoder_by_name("h264_nvenc") : avcodec_find_encoder(codec_id);
    if (use_nvenc) codec_name = "h264_nvenc";
    if (!encoder) {
        if (pipeline) pipeline->report_error(std::string("[VideoEncoder] 找不到") + codec_name + "编码器");
        return;
    }

    // 旋转处理器
    VideoRotateProcessor rotator;
    bool need_rotate = false;
    if (config.rotate != ROTATE_NONE) {
        rotator = VideoRotateProcessor(config.rotate);
        need_rotate = true;
        Logger::info("VideoEncoder", std::string("旋转已启用: ") + std::to_string(config.rotate) + "度");
    }

    // 确定编码器输出分辨率（90/270旋转时交换宽高）
    int enc_width = src_codec_par->width;
    int enc_height = src_codec_par->height;
    if (config.rotate == ROTATE_90_CW || config.rotate == ROTATE_270_CW) {
        std::swap(enc_width, enc_height);
    }
    if (config.max_short_edge > 0 && std::min(enc_width, enc_height) > config.max_short_edge) {
        const double scale = static_cast<double>(config.max_short_edge) / std::min(enc_width, enc_height);
        enc_width = std::max(2, static_cast<int>(enc_width * scale) / 2 * 2);
        enc_height = std::max(2, static_cast<int>(enc_height * scale) / 2 * 2);
    }
    Logger::info("VideoEncoder", std::string("编码输出分辨率: ")
                 + std::to_string(enc_width) + "x" + std::to_string(enc_height));

    double speed = config.speed_ratio;
    if (speed != 1.0) {
        Logger::info("VideoEncoder", std::string("倍速: ") + std::to_string(speed) + "x");
    }

    CodecContextPtr enc_ctx(avcodec_alloc_context3(encoder));
    if (!enc_ctx) {
        if (pipeline) pipeline->report_error("[VideoEncoder] 分配视频编码器上下文失败");
        return;
    }

    enc_ctx->codec_id = codec_id;
    enc_ctx->width = enc_width;
    enc_ctx->height = enc_height;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->time_base = output_time_base;
    enc_ctx->framerate = av_inv_q(output_time_base);
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 动态码率: 根据分辨率×帧率计算，避免画质损失
    double fps_val = av_q2d(av_inv_q(output_time_base));
    double pixels_per_sec = enc_width * enc_height * fps_val;
    int64_t dynamic_bitrate = static_cast<int64_t>(pixels_per_sec * 0.12);
    if (dynamic_bitrate < 2000000) dynamic_bitrate = 2000000;

    if (config.quality == 1) dynamic_bitrate = dynamic_bitrate * 3 / 2;
    if (config.quality == 3) dynamic_bitrate = dynamic_bitrate / 2;
    if (config.quality == 4) dynamic_bitrate = static_cast<int64_t>(config.video_bitrate_kbps) * 1000;
    const char* quality_value = config.quality == 1 ? "18" : config.quality == 3 ? "28" : "23";

    // GOP: 每秒一个关键帧（比固定10更合理）
    enc_ctx->gop_size = static_cast<int>(fps_val);
    if (enc_ctx->gop_size < 1) enc_ctx->gop_size = 10;

    if (use_nvenc) {
        enc_ctx->codec_tag = 0;
        enc_ctx->max_b_frames = 0;
        enc_ctx->bit_rate = dynamic_bitrate;
        enc_ctx->rc_max_rate = dynamic_bitrate * 2;
        enc_ctx->rc_buffer_size = dynamic_bitrate * 2;
        if (av_opt_set(enc_ctx->priv_data, "preset", "p4", 0) < 0
            || av_opt_set(enc_ctx->priv_data, "rc", "vbr", 0) < 0
            || (config.quality != 4 && av_opt_set(enc_ctx->priv_data, "cq", quality_value, 0) < 0)) {
            if (pipeline) pipeline->report_error("[VideoEncoder] 当前 FFmpeg 不支持 NVENC 参数");
            return;
        }
    } else if (config.video_codec == VIDEO_CODEC_H264) {
        enc_ctx->codec_tag = 0;
        enc_ctx->max_b_frames = 2;
        av_opt_set(enc_ctx->priv_data, "preset", "medium", 0);
        if (config.quality == 4) enc_ctx->bit_rate = dynamic_bitrate;
        else av_opt_set(enc_ctx->priv_data, "crf", quality_value, 0);
        av_opt_set(enc_ctx->priv_data, "threads", "auto", 0);
    } else {
        enc_ctx->codec_tag = 0x7634706d;  // 'mp4v'
        enc_ctx->max_b_frames = 0;
        enc_ctx->bit_rate = dynamic_bitrate;
    }

    const bool bitrate_mode = config.quality == 4 || config.video_codec == VIDEO_CODEC_MPEG4;
    Logger::info("VideoEncoder", std::string("编码参数: ") + codec_name
                 + (bitrate_mode ? ", 目标视频码率=" + std::to_string(dynamic_bitrate / 1000) + "kbps"
                                 : std::string(use_nvenc ? ", CQ=" : ", CRF=") + quality_value)
                 + ", 帧率=" + std::to_string(fps_val)
                 + ", 分辨率=" + std::to_string(enc_width) + "x" + std::to_string(enc_height));

    char err_buf[1024];
    int ret = avcodec_open2(enc_ctx.get(), encoder, nullptr);
    if (ret < 0) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[VideoEncoder] 打开") + codec_name + "编码器失败：" + err_buf);
        return;
    }

    if (pipeline && !pipeline->publish_encoder_parameters(true, enc_ctx.get())) return;

    Logger::info("VideoEncoder", std::string(codec_name) + "编码器打开成功");

    FramePtr local_frame(av_frame_alloc());
    PacketPtr pkt(av_packet_alloc());
    if (!local_frame || !pkt) {
        if (pipeline) pipeline->report_error("[VideoEncoder] 分配Frame/Packet失败");
        return;
    }

    // 像素格式转换上下文（懒初始化）
    SwsContextPtr sws_ctx;
    FramePtr converted_frame;

    int input_frame_count = 0;
    int output_frame_count = 0;

    while (true) {
        AVFrame* raw_frame = local_frame.get();
        bool success = in_rb.pop(raw_frame);
        if (!success) {
            Logger::info("VideoEncoder", "环形缓冲区已空，停止接收帧");
            break;
        }

        input_frame_count++;

        if (!local_frame->data[0]) {
            Logger::warn("VideoEncoder", std::string("无效视频Frame（第")
                         + std::to_string(input_frame_count) + "帧），跳过");
            av_frame_unref(local_frame.get());
            continue;
        }

        // ========== 步骤1: 旋转 ==========
        AVFrame* frame_to_encode = local_frame.get();
        FramePtr rotated_frame;
        if (need_rotate) {
            rotated_frame.reset(rotator.rotateFrame(local_frame.get()));
            if (rotated_frame) {
                frame_to_encode = rotated_frame.get();
            } else {
                if (pipeline) pipeline->report_error("[VideoEncoder] 旋转视频帧失败");
                return;
            }
        }

        // ========== 步骤2: 像素格式转换 (任意格式 → YUV420P) ==========
        bool need_convert = frame_to_encode->format != AV_PIX_FMT_YUV420P
            || frame_to_encode->width != enc_width || frame_to_encode->height != enc_height;
        if (need_convert) {
            if (!sws_ctx) {
                sws_ctx.reset(sws_getContext(
                    frame_to_encode->width, frame_to_encode->height,
                    static_cast<AVPixelFormat>(frame_to_encode->format),
                    enc_width, enc_height, AV_PIX_FMT_YUV420P,
                    SWS_LANCZOS, nullptr, nullptr, nullptr));
                if (!sws_ctx) {
                    if (pipeline) pipeline->report_error("[VideoEncoder] 创建缩放转换器失败");
                    return;
                }
                Logger::info("VideoEncoder",
                    std::string("像素格式转换已启用: ")
                    + av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame_to_encode->format))
                    + " → YUV420P");
            }

            // 复用转换缓冲区（只分配一次，减少 malloc/free）
            if (!converted_frame) {
                converted_frame.reset(av_frame_alloc());
                if (!converted_frame) {
                    if (pipeline) pipeline->report_error("[VideoEncoder] 分配转换帧失败");
                    return;
                }
                converted_frame->format = AV_PIX_FMT_YUV420P;
                converted_frame->width = enc_width;
                converted_frame->height = enc_height;
                ret = av_frame_get_buffer(converted_frame.get(), 0);
                if (ret < 0) {
                    if (pipeline) pipeline->report_error("[VideoEncoder] 分配转换帧缓冲区失败");
                    return;
                }
            }

            if (av_frame_make_writable(converted_frame.get()) < 0) {
                if (pipeline) pipeline->report_error("[VideoEncoder] 转换帧不可写");
                return;
            }
            sws_scale(sws_ctx.get(),
                frame_to_encode->data, frame_to_encode->linesize,
                0, frame_to_encode->height,
                converted_frame->data, converted_frame->linesize);
            converted_frame->pts = frame_to_encode->pts;
            frame_to_encode = converted_frame.get();
        }

        // 按输出帧率和倍速选取或复制帧，PTS 使用输出帧时间基。
        if (config.output_fps > 0) {
            const double ratio = config.output_fps / (av_q2d(input_frame_rate) * speed);
            const double desired_count = std::floor(input_frame_count * ratio + 1e-9);
            if (!std::isfinite(desired_count) || desired_count > std::numeric_limits<int>::max()) {
                if (pipeline) pipeline->report_error("[VideoEncoder] 帧率和倍速组合超出处理范围");
                return;
            }
            const int desired = std::max(1, static_cast<int>(desired_count));
            while (output_frame_count < desired) {
                frame_to_encode->pts = output_frame_count;
                ret = avcodec_send_frame(enc_ctx.get(), frame_to_encode);
                if (ret < 0) {
                    if (pipeline) pipeline->report_error("[VideoEncoder] 发送帧失败");
                    return;
                }
                ++output_frame_count;
                while ((ret = avcodec_receive_packet(enc_ctx.get(), pkt.get())) >= 0) {
                    pkt->stream_index = 0;
                    av_packet_rescale_ts(pkt.get(), enc_ctx->time_base, output_time_base);
                    if (!out_q.push(*pkt)) {
                        if (pipeline && !pipeline->has_error()) pipeline->report_error("[VideoEncoder] 编码包入队失败");
                        return;
                    }
                    av_packet_unref(pkt.get());
                }
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                    if (pipeline) pipeline->report_error("[VideoEncoder] 接收编码包失败");
                    return;
                }
            }
        } else {
            // ========== 步骤3: 倍速处理 ==========
            if (speed > 1.0) {
                int input_idx = input_frame_count - 1;
                double expected_output = input_idx / speed;
                int should_encode = static_cast<int>(expected_output);
                if (should_encode < output_frame_count) {
                    av_frame_unref(local_frame.get());
                    continue;
                }
            }

            // 设置PTS（基于输出帧计数）
            frame_to_encode->pts = output_frame_count;

            // 发送到编码器
            ret = avcodec_send_frame(enc_ctx.get(), frame_to_encode);
            if (ret < 0) {
                av_strerror(ret, err_buf, sizeof(err_buf));
                if (pipeline) pipeline->report_error(std::string("[VideoEncoder] 发送编码帧失败：") + err_buf);
                return;
            }

            output_frame_count++;

            // 接收编码包
            while (true) {
                ret = avcodec_receive_packet(enc_ctx.get(), pkt.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    av_strerror(ret, err_buf, sizeof(err_buf));
                    if (pipeline) pipeline->report_error(std::string("[VideoEncoder] 接收编码包失败：") + err_buf);
                    return;
                }

                pkt->stream_index = 0;
                av_packet_rescale_ts(pkt.get(), enc_ctx->time_base, output_time_base);

                if (output_frame_count % 50 == 0) {
                    Logger::debug("VideoEncoder", std::string("编码") + codec_name + " Packet: pts="
                                  + std::to_string(pkt->pts) + " size=" + std::to_string(pkt->size)
                                  + "（第" + std::to_string(output_frame_count) + "帧）");
                }

                if (!out_q.push(*pkt)) {
                    if (pipeline && !pipeline->has_error()) pipeline->report_error("[VideoEncoder] 编码包入队失败");
                    return;
                }
                av_packet_unref(pkt.get());
            }

            // ========== 慢放: 复制帧 ==========
            if (speed < 1.0) {
                double repeats = 1.0 / speed;
                int repeat_count = static_cast<int>(std::round(repeats));
                if (repeat_count < 1) repeat_count = 1;

                for (int r = 1; r < repeat_count; r++) {
                    frame_to_encode->pts = output_frame_count;

                    ret = avcodec_send_frame(enc_ctx.get(), frame_to_encode);
                    if (ret < 0) {
                        if (pipeline) pipeline->report_error("[VideoEncoder] 发送重复帧失败");
                        return;
                    }

                    output_frame_count++;

                    while (true) {
                        ret = avcodec_receive_packet(enc_ctx.get(), pkt.get());
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                        if (ret < 0) {
                            if (pipeline) pipeline->report_error("[VideoEncoder] 接收重复帧编码包失败");
                            return;
                        }

                        pkt->stream_index = 0;
                        av_packet_rescale_ts(pkt.get(), enc_ctx->time_base, output_time_base);
                        if (!out_q.push(*pkt)) {
                            if (pipeline && !pipeline->has_error()) pipeline->report_error("[VideoEncoder] 编码包入队失败");
                            return;
                        }
                        av_packet_unref(pkt.get());
                    }
                }
            }
        }

        av_frame_unref(local_frame.get());
    }

    Logger::info("VideoEncoder", std::string("开始刷新编码器剩余数据（输入")
                 + std::to_string(input_frame_count) + "帧，输出"
                 + std::to_string(output_frame_count) + "帧）");
    ret = avcodec_send_frame(enc_ctx.get(), nullptr);
    if (ret < 0) {
        av_strerror(ret, err_buf, sizeof(err_buf));
        if (pipeline) pipeline->report_error(std::string("[VideoEncoder] 刷新编码器失败：") + err_buf);
        return;
    }

    while (true) {
        ret = avcodec_receive_packet(enc_ctx.get(), pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            av_strerror(ret, err_buf, sizeof(err_buf));
            if (pipeline) pipeline->report_error(std::string("[VideoEncoder] 刷新时接收编码包失败：") + err_buf);
            return;
        }

        av_packet_rescale_ts(pkt.get(), enc_ctx->time_base, output_time_base);
        pkt->stream_index = 0;
        if (!out_q.push(*pkt)) {
            if (pipeline && !pipeline->has_error()) pipeline->report_error("[VideoEncoder] 编码包入队失败");
            return;
        }
        av_packet_unref(pkt.get());
    }

    out_q.mark_done();
    if (pipeline) pipeline->video_encoded_frames.store(output_frame_count, std::memory_order_release);
    Logger::info("VideoEncoder", std::string("视频编码线程退出，输入")
                 + std::to_string(input_frame_count) + "帧，输出"
                 + std::to_string(output_frame_count) + "帧");
}
