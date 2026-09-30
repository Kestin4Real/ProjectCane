#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else

#include <cstdint>
#include <cstring>
#include <strings.h>
#include <csignal>

using BYTE = std::uint8_t;
using DWORD = std::uint32_t;
using UINT = unsigned int;
using UINT32 = std::uint32_t;
using ULONG = unsigned long;
using LONG = std::int32_t;
using LONGLONG = std::int64_t;
using BOOL = int;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

#define _stricmp strcasecmp
#define _strnicmp strncasecmp

inline void __debugbreak()
{
    raise(SIGTRAP);
}

#if defined(__x86_64__) || defined(__i386__)
inline void YieldProcessor()
{
    __builtin_ia32_pause();
}
#else
inline void YieldProcessor()
{
}
#endif

#endif
