#pragma once

#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

// Encodes a run vector to a CF_RTF clipboard string (ASCII with Unicode escapes).
// Preserves bold, italic, underline, and strikethrough flags.
// Returns a null-terminated ANSI string suitable for SetClipboardData(CF_RTF).
[[nodiscard]] std::string Encode_rtf(std::span<const TextRun> runs);

// Decodes a CF_RTF clipboard string, importing ONLY
// bold/italic/underline/strikethrough. All other attributes (font face, size, color)
// are discarded. Newlines are normalized to LF. Returns empty vector on malformed or
// empty input.
[[nodiscard]] std::vector<TextRun> Decode_rtf(std::string_view rtf);

} // namespace greenflame::core
