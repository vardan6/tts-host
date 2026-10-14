#pragma once

#include <string>
#include <string_view>

namespace tts_host {

// Converts Markdown to speakable text, the "normalize" stage of the speech
// pipeline (docs/design/architecture.md#speech-pipeline). Normalization lives
// in the host so every client benefits from it, and it is deliberately not
// user-configurable in the first release
// (docs/requirements/product.md, "Speech behaviour").
//
// Blocks in the returned text are separated by a blank line. Nothing here
// converts that boundary into actual silence; that belongs to the later
// "split" stage, which does not exist yet.
std::string normalize_markdown(std::string_view markdown);

// Converts HTML to speakable text, applying the same five rules as
// normalize_markdown over HTML syntax instead: code blocks (<pre>/<code>)
// announced and skipped, inline <code> read as its contents, <a>/<img>
// reduced to their text/alt, <table> skipped with a marker, and headings
// (<h1>-<h6>) given a leading pause via block boundary. Other block-level
// tags (<p>, <div>, <li>, <blockquote>, <br>, ...) produce block/pause
// boundaries; other inline tags have their markup stripped but text kept.
// HTML entities are decoded and comments are dropped.
std::string normalize_html(std::string_view html);

// Spoken in place of a construct that is announced rather than read.
inline constexpr std::string_view kCodeBlockMarker = "Code block.";
inline constexpr std::string_view kTableMarker = "Table skipped.";

}  // namespace tts_host
