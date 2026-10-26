#ifndef QTFFMPEGSTUDIO_SAFE_OUTPUT_H
#define QTFFMPEGSTUDIO_SAFE_OUTPUT_H

#include <filesystem>
#include <windows.h>

// 在目标目录创建一个独占名字的空临时文件；调用者负责删除未发布的文件。
std::filesystem::path createTemporaryOutput(const std::filesystem::path& destination);

// 仅接受同目录、空的独立常规文件；不能是输入、输出或它们的硬链接。
bool isReservedTemporaryOutput(const std::filesystem::path& temporary,
                               const std::filesystem::path& destination,
                               const std::filesystem::path& input);

// 不允许覆盖时由 MoveFileW 保证目标必须尚不存在。
bool publishOutput(const std::filesystem::path& temporary,
                   const std::filesystem::path& destination,
                   bool overwrite, DWORD& windowsError);

#endif
