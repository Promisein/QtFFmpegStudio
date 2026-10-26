#include "safe_output.h"

#include <system_error>

namespace fs = std::filesystem;

fs::path createTemporaryOutput(const fs::path& destination)
{
    wchar_t buffer[MAX_PATH] = {};
    const std::wstring directory = destination.parent_path().wstring();
    if (GetTempFileNameW(directory.c_str(), L"QFS", 0, buffer) == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "无法在输出目录创建临时文件");
    }
    return fs::path(buffer);
}

bool isReservedTemporaryOutput(const fs::path& temporary,
                               const fs::path& destination,
                               const fs::path& input)
{
    std::error_code error;
    if (temporary.filename().wstring().rfind(L"QFS-", 0) != 0
        || temporary.extension() != L".tmp"
        || !fs::equivalent(temporary.parent_path(), destination.parent_path(), error)
        || error || fs::is_symlink(temporary, error) || error
        || !fs::is_regular_file(temporary, error) || error
        || fs::file_size(temporary, error) != 0 || error
        || fs::hard_link_count(temporary, error) != 1 || error
        || fs::equivalent(temporary, input, error) || error) return false;
    if (fs::exists(destination, error)) {
        if (error || fs::equivalent(temporary, destination, error) || error) return false;
    }
    return !error;
}

bool publishOutput(const fs::path& temporary, const fs::path& destination,
                   bool overwrite, DWORD& windowsError)
{
    const BOOL moved = overwrite
        ? MoveFileExW(temporary.c_str(), destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        : MoveFileW(temporary.c_str(), destination.c_str());
    windowsError = moved ? ERROR_SUCCESS : GetLastError();
    return moved != 0;
}
