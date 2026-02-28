#pragma once

// Clang-only diagnostic push/pop macros. Expand to nothing on MSVC and other compilers.
#ifdef __clang__
#define CLANG_DIAGNOSTIC_PUSH() _Pragma("clang diagnostic push")
#define CLANG_DIAGNOSTIC_POP() _Pragma("clang diagnostic pop")
#define CLANG_DIAG_IGNORED_HELPER(x) _Pragma(#x)
#define CLANG_DIAGNOSTIC_IGNORED(w)                                                    \
    CLANG_DIAG_IGNORED_HELPER(clang diagnostic ignored w)
#define CLANG_WARN_IGNORE_PUSH(w) CLANG_DIAGNOSTIC_PUSH() CLANG_DIAGNOSTIC_IGNORED(w)
#define CLANG_WARN_IGNORE_POP() CLANG_DIAGNOSTIC_POP()

#else
#define CLANG_DIAGNOSTIC_PUSH()     /**/
#define CLANG_DIAGNOSTIC_POP()      /**/
#define CLANG_DIAGNOSTIC_IGNORED(w) /**/
#define CLANG_WARN_IGNORE_PUSH(w)   /**/
#define CLANG_WARN_IGNORE_POP()     /**/
#endif
