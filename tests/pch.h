#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

// Include windef.h for HWND without pulling in all of windows.h.
// winnt.h (pulled by windef.h) requires a target-architecture define;
// set it from the MSVC predefined macros if not already set.
#if defined(_M_AMD64) && !defined(_AMD64_)
#define _AMD64_
#endif
#if defined(_M_IX86) && !defined(_X86_)
#define _X86_
#endif
#if defined(_M_ARM64) && !defined(_ARM64_)
#define _ARM64_
#endif
#if defined(_M_ARM) && !defined(_ARM_)
#define _ARM_
#endif
#include <windef.h>

// Define RGB macro for compatibility with Windows SDK.
#define RGB(r, g, b)                                                                   \
    ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
