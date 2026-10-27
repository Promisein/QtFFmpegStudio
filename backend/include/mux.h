//
// Created by Jianing on 2025/12/22.
//

#ifndef FFMPEGPROJECT_MUX_H
#define FFMPEGPROJECT_MUX_H

#include <string>
#include "common.h"
#include "config.h"

// 封装线程等待编码线程发布真实参数，再写入 MP4 文件头。
void mux_thread(const std::string& output_file,
                AVRational output_time_base,
                double expected_output_duration_seconds,
                Pipeline* pipeline);

#endif //FFMPEGPROJECT_MUX_H
