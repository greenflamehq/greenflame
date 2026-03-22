#pragma once

namespace greenflame::core {

enum class WindowCaptureBackend : uint8_t {
    Auto = 0,
    Gdi = 1,
    Wgc = 2,
};

} // namespace greenflame::core
