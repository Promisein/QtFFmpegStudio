#ifndef QTFFMPEGSTUDIO_BACKEND_TRANSCODE_H
#define QTFFMPEGSTUDIO_BACKEND_TRANSCODE_H

#include <string>

#include "config.h"

struct TranscodeResult {
    std::string filename;
    bool success = false;
    std::string error;
    int video_frames = 0;
    int audio_frames = 0;
};

TranscodeResult transcode_single(const std::string& input_path,
                                 const std::string& output_path,
                                 const ProcessingConfig& config);

#endif
