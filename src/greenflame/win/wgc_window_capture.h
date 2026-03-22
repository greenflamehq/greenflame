#pragma once

#include "greenflame_core/app_services.h"
#include "win/gdi_capture.h"

namespace greenflame {

[[nodiscard]] core::CaptureSaveResult
Capture_window_with_wgc(HWND hwnd, core::RectPx window_rect_screen,
                        GdiCaptureResult &capture_out);

} // namespace greenflame
