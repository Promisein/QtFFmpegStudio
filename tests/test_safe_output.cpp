#include "safe_output.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <windows.h>

namespace fs = std::filesystem;

static std::string readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static void writeFile(const fs::path& path, const std::string& data)
{
    std::ofstream file(path, std::ios::binary);
    file << data;
}

int main()
{
    wchar_t root[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, root)) return 1;
    const fs::path directory = fs::path(root) / (L"QtFFmpegStudio-safe-output-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory);
    const fs::path output = directory / L"输出.mp4";
    const fs::path temp = createTemporaryOutput(output);
    if (!fs::exists(temp) || temp.parent_path() != output.parent_path()) return 2;
    writeFile(temp, "new media");
    writeFile(output, "other process created this");
    DWORD error = ERROR_SUCCESS;
    if (publishOutput(temp, output, false, error)) return 3;
    if (readFile(output) != "other process created this") return 4;
    if (!fs::exists(temp)) return 5;
    if (!publishOutput(temp, output, true, error)) return 6;
    if (readFile(output) != "new media") return 7;
    fs::remove(output);
    fs::remove(directory);
    std::cout << "safe output PASS\n";
    return 0;
}
