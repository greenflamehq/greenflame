#pragma once

#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

// Encodes a run vector to a Windows "HTML Format" clipboard payload (UTF-8).
// Preserves bold, italic, underline, and strikethrough via inline CSS.
// All other style attributes (font, color, size) use browser defaults.
// Returns a complete "HTML Format" byte string ready for SetClipboardData.
[[nodiscard]] std::string Encode_html_clipboard(std::span<const TextRun> runs);

// Decodes an HTML clipboard payload (Windows "HTML Format" or "text/html"),
// importing ONLY bold/italic/underline/strikethrough from inline CSS styles.
// All other attributes (font face, size, color) are discarded.
// Paragraph breaks and explicit line breaks become LF characters.
// Returns empty vector on empty or unparseable input.
[[nodiscard]] std::vector<TextRun> Decode_html_clipboard(std::string_view html);

} // namespace greenflame::core
