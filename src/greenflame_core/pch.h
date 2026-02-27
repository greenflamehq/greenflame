#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
