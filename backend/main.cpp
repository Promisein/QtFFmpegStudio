#include "transcode.h"
#include "logger.h"
#include "safe_output.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <windows.h>

extern "C" {
#include <libavformat/avformat.h>
}

namespace fs = std::filesystem;

struct TemporaryOutputCleanup
{
    fs::path path;
    ~TemporaryOutputCleanup()
    {
        std::error_code ignored;
        fs::remove(path, ignored);
    }
};

static std::string toUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                          value.data(), static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes == 0) throw std::runtime_error("无法转换路径为 UTF-8");
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), bytes,
                        nullptr, nullptr);
    return result;
}

static void usage()
{
    Logger::info("usage", "用法: FFmpegProjectBackend.exe -i <输入.mp4> -o <输出.mp4> "
                          "[-rotate 90|180|270] [-speed 正数] "
                          "[-vcodec h264|mpeg4] [-acodec aac|ac3] [--overwrite] "
                          "[--temp-output <GUI预留临时文件>]");
}

static int parseInt(const std::wstring& text)
{
    size_t consumed = 0;
    int result = std::stoi(text, &consumed);
    if (consumed != text.size()) throw std::invalid_argument("整数参数含无效字符");
    return result;
}

static double parseDouble(const std::wstring& text)
{
    size_t consumed = 0;
    double result = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(result)) {
        throw std::invalid_argument("倍速参数无效");
    }
    return result;
}

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::wstring input;
    std::wstring output;
    std::wstring reservedTemporaryOutput;
    ProcessingConfig config;
    bool overwrite = false;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::wstring flag = argv[i];
            if (flag == L"-h" || flag == L"--help") {
                usage();
                return 0;
            }
            if (flag == L"--overwrite") {
                overwrite = true;
                continue;
            }
            if (i + 1 >= argc) throw std::invalid_argument("参数缺少值");
            const std::wstring value = argv[++i];
            if (flag == L"-i") input = value;
            else if (flag == L"-o") output = value;
            else if (flag == L"--temp-output") reservedTemporaryOutput = value;
            else if (flag == L"-rotate") {
                const int angle = parseInt(value);
                if (angle == 90) config.rotate = ROTATE_90_CW;
                else if (angle == 180) config.rotate = ROTATE_180;
                else if (angle == 270) config.rotate = ROTATE_270_CW;
                else throw std::invalid_argument("旋转角度必须是 90、180 或 270");
            } else if (flag == L"-speed") {
                config.speed_ratio = parseDouble(value);
                if (config.speed_ratio <= 0) throw std::invalid_argument("倍速必须大于 0");
            } else if (flag == L"-vcodec") {
                if (value == L"h264") config.video_codec = VIDEO_CODEC_H264;
                else if (value == L"mpeg4") config.video_codec = VIDEO_CODEC_MPEG4;
                else throw std::invalid_argument("不支持的视频编码器");
            } else if (flag == L"-acodec") {
                if (value == L"aac") config.audio_codec = AUDIO_CODEC_AAC;
                else if (value == L"ac3") config.audio_codec = AUDIO_CODEC_AC3;
                else throw std::invalid_argument("不支持的音频编码器");
            } else {
                throw std::invalid_argument("未知参数");
            }
        }

        if (input.empty() || output.empty()) throw std::invalid_argument("必须指定 -i 和 -o");

        const fs::path inputPath = fs::weakly_canonical(fs::path(input));
        const fs::path outputPath = fs::weakly_canonical(fs::path(output));
        if (!fs::is_regular_file(inputPath)) throw std::invalid_argument("输入文件不存在");
        if (!fs::exists(outputPath.parent_path())) throw std::invalid_argument("输出目录不存在");
        if (_wcsicmp(inputPath.c_str(), outputPath.c_str()) == 0
            || (fs::exists(outputPath) && fs::equivalent(inputPath, outputPath))) {
            throw std::invalid_argument("输入和输出不能是同一个文件");
        }
        if (fs::exists(outputPath) && !overwrite) {
            throw std::invalid_argument("输出文件已存在；需显式传入 --overwrite");
        }

        const fs::path temporaryPath = reservedTemporaryOutput.empty()
            ? createTemporaryOutput(outputPath)
            : fs::path(reservedTemporaryOutput);
        if (!reservedTemporaryOutput.empty()
            && !isReservedTemporaryOutput(temporaryPath, outputPath, inputPath)) {
            throw std::invalid_argument("GUI 预留临时文件无效");
        }
        TemporaryOutputCleanup temporary{temporaryPath};
        avformat_network_init();
        const auto result = transcode_single(toUtf8(inputPath.wstring()),
                                             toUtf8(temporary.path.wstring()), config);
        avformat_network_deinit();
        if (!result.success) {
            Logger::error("backend", result.error);
            return 1;
        }
        if (!fs::is_regular_file(temporary.path) || fs::file_size(temporary.path) == 0) {
            Logger::error("backend", "转码结果文件不存在或为空");
            return 1;
        }
        DWORD windowsError = ERROR_SUCCESS;
        if (!publishOutput(temporary.path, outputPath, overwrite, windowsError)) {
            Logger::error("backend", "发布输出文件失败，可能已有其他程序创建同名文件；Windows 错误码: "
                                     + std::to_string(windowsError));
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        Logger::error("backend", error.what());
        usage();
        return 2;
    }
}
