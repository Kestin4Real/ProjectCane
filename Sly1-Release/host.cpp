#include "host.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#include <csignal>
#include <climits>
#include <vector>
#endif

#include <system_error>

std::filesystem::path ExecutableDirectory()
{
#ifdef _WIN32
    wchar_t executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return std::filesystem::current_path();
    return std::filesystem::path(executablePath).parent_path();
#else
    std::vector<char> buffer(PATH_MAX);
    for (;;)
    {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0)
            return std::filesystem::current_path();
        if (static_cast<size_t>(length) < buffer.size())
        {
            buffer[static_cast<size_t>(length)] = '\0';
            return std::filesystem::path(buffer.data()).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
}

bool ReplaceFileAtomic(const std::filesystem::path& temporary, const std::filesystem::path& destination)
{
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error)
        return true;
    error.clear();
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

#ifndef _WIN32
void HostDebugBreak()
{
    raise(SIGTRAP);
}
#endif
