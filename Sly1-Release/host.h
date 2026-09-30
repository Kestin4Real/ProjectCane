#pragma once

#include <filesystem>
#include <string>

std::filesystem::path ExecutableDirectory();
bool ReplaceFileAtomic(const std::filesystem::path& temporary, const std::filesystem::path& destination);

#ifndef _WIN32
void HostDebugBreak();
#endif
