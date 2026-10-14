#include "tts_host/text_normalizer.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tts_host {
namespace {

bool is_ascii_alnum(char character) {
  const auto value = static_cast<unsigned char>(character);
  return std::isalnum(value) != 0;
}

bool is_space(char character) {
  const auto value = static_cast<unsigned char>(character);
  return std::isspace(value) != 0;
}

std::string_view trim(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && is_space(text[begin])) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// A run of three or more of the same fence character, per CommonMark.
bool is_code_fence(std::string_view trimmed) {
  if (trimmed.size() < 3) {
    return false;
  }
  const char marker = trimmed.front();
  if (marker != '`' && marker != '~') {
    return false;
  }
  return trimmed[1] == marker && trimmed[2] == marker;
}

// Three or more of -, * or _, and nothing else. This also covers a setext
// h2 underline, which needs no marker of its own: the paragraph above it is
// already its own block, which is what gives a heading its leading pause.
bool is_thematic_break(std::string_view trimmed) {
  if (trimmed.size() < 3) {
    return false;
  }
  const char marker = trimmed.front();
  if (marker != '-' && marker != '*' && marker != '_') {
    return false;
  }
  for (const char character : trimmed) {
    if (character != marker && !is_space(character)) {
      return false;
    }
  }
  return true;
}

// A setext h1 underline. Dropped for the same reason as the h2 form above.
bool is_setext_underline(std::string_view trimmed) {
  if (trimmed.size() < 2 || trimmed.front() != '=') {
    return false;
  }
  return trimmed.find_first_not_of('=') == std::string_view::npos;
}

// The |---|:--:| row that turns the line above it into a table header.
bool is_table_delimiter(std::string_view trimmed) {
  if (trimmed.find('-') == std::string_view::npos) {
    return false;
  }
  for (const char character : trimmed) {
    if (character != '|' && character != '-' && character != ':' && !is_space(character)) {
      return false;
    }
  }
  return true;
}

// Returns the heading text for an ATX heading, or npos-equivalent absence.
bool parse_atx_heading(std::string_view trimmed, std::string_view &text) {
  std::size_t level = 0;
  while (level < trimmed.size() && trimmed[level] == '#') {
    ++level;
  }
  if (level == 0 || level > 6) {
    return false;
  }
  if (level < trimmed.size() && !is_space(trimmed[level])) {
    return false;
  }
  auto body = trim(trimmed.substr(level));
  // Closing sequence, as in "## Title ##".
  while (!body.empty() && body.back() == '#') {
    body.remove_suffix(1);
  }
  text = trim(body);
  return true;
}

// Strips a leading blockquote marker. Returns true if one was present, so the
// remainder can be reinterpreted as a block in its own right.
bool strip_blockquote(std::string_view &line) {
  auto rest = line;
  std::size_t indent = 0;
  while (indent < rest.size() && is_space(rest[indent])) {
    ++indent;
  }
  if (indent >= rest.size() || rest[indent] != '>') {
    return false;
  }
  rest.remove_prefix(indent + 1);
  if (!rest.empty() && rest.front() == ' ') {
    rest.remove_prefix(1);
  }
  line = rest;
  return true;
}

// Strips a bullet or ordered list marker. Each item becomes its own block, so
// items are separated by a pause instead of running together.
bool strip_list_marker(std::string_view &line) {
  auto rest = trim(line);
  if (rest.empty()) {
    return false;
  }
  if ((rest.front() == '-' || rest.front() == '*' || rest.front() == '+') && rest.size() > 1 &&
      is_space(rest[1])) {
    line = trim(rest.substr(2));
    return true;
  }
  std::size_t digits = 0;
  while (digits < rest.size() && std::isdigit(static_cast<unsigned char>(rest[digits])) != 0) {
    ++digits;
  }
  if (digits > 0 && digits + 1 < rest.size() && (rest[digits] == '.' || rest[digits] == ')') &&
      is_space(rest[digits + 1])) {
    line = trim(rest.substr(digits + 2));
    return true;
  }
  return false;
}

// Skips the (url) or [reference] that follows a link or image label. Returns
// false when neither is present, which means the brackets were literal text.
bool skip_link_target(std::string_view text, std::size_t &index) {
  if (index >= text.size()) {
    return false;
  }
  const char opener = text[index];
  const char closer = opener == '(' ? ')' : (opener == '[' ? ']' : '\0');
  if (closer == '\0') {
    return false;
  }
  const auto end = text.find(closer, index + 1);
  if (end == std::string_view::npos) {
    return false;
  }
  index = end + 1;
  return true;
}

std::string render_inline(std::string_view text);

// Renders a [label](target) link or ![alt](target) image starting at `index`,
// which points at the '['. On success `index` moves past the whole construct.
bool render_bracketed(std::string_view text, std::size_t &index, std::string &out) {
  std::size_t depth = 0;
  std::size_t cursor = index;
  std::size_t label_end = std::string_view::npos;
  for (; cursor < text.size(); ++cursor) {
    if (text[cursor] == '\\') {
      ++cursor;
      continue;
    }
    if (text[cursor] == '[') {
      ++depth;
    } else if (text[cursor] == ']') {
      --depth;
      if (depth == 0) {
        label_end = cursor;
        break;
      }
    }
  }
  if (label_end == std::string_view::npos) {
    return false;
  }

  std::size_t after = label_end + 1;
  if (!skip_link_target(text, after)) {
    return false;
  }

  out += render_inline(text.substr(index + 1, label_end - index - 1));
  index = after;
  return true;
}

// Applies the inline rules: inline code is read as its contents, links are
// read as their text, and emphasis markers are dropped.
std::string render_inline(std::string_view text) {
  std::string out;
  out.reserve(text.size());

  for (std::size_t index = 0; index < text.size();) {
    const char character = text[index];

    if (character == '\\' && index + 1 < text.size()) {
      out += text[index + 1];
      index += 2;
      continue;
    }

    // A code span ends at a backtick run of the same length as its opener.
    if (character == '`') {
      std::size_t run = 0;
      while (index + run < text.size() && text[index + run] == '`') {
        ++run;
      }
      const auto opener = text.substr(index, run);
      const auto close = text.find(opener, index + run);
      if (close != std::string_view::npos) {
        out += text.substr(index + run, close - index - run);
        index = close + run;
        continue;
      }
    }

    // An image is read as its alt text, which is the only human-readable part
    // of it. The requirement is silent on images; this is the screen-reader
    // convention.
    if (character == '!' && index + 1 < text.size() && text[index + 1] == '[') {
      std::size_t cursor = index + 1;
      if (render_bracketed(text, cursor, out)) {
        index = cursor;
        continue;
      }
    }

    if (character == '[') {
      std::size_t cursor = index;
      if (render_bracketed(text, cursor, out)) {
        index = cursor;
        continue;
      }
    }

    // Emphasis markers are dropped, but only where they are plausibly markup:
    // an underscore between two word characters is part of an identifier, and
    // an asterisk surrounded by spaces is more likely arithmetic.
    if (character == '_' || character == '*') {
      const bool alnum_before = index > 0 && is_ascii_alnum(text[index - 1]);
      const bool alnum_after = index + 1 < text.size() && is_ascii_alnum(text[index + 1]);
      const bool space_before = index == 0 || is_space(text[index - 1]);
      const bool space_after = index + 1 >= text.size() || is_space(text[index + 1]);
      const bool intraword_underscore = character == '_' && alnum_before && alnum_after;
      const bool isolated_asterisk = character == '*' && space_before && space_after;
      if (!intraword_underscore && !isolated_asterisk) {
        ++index;
        continue;
      }
    }

    out += character;
    ++index;
  }

  return out;
}

std::vector<std::string_view> split_lines(std::string_view text) {
  std::vector<std::string_view> lines;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    auto end = text.find('\n', begin);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    auto line = text.substr(begin, end - begin);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    lines.push_back(line);
    if (end == text.size()) {
      break;
    }
    begin = end + 1;
  }
  return lines;
}

// Accumulates rendered blocks, joining continuation lines into the block they
// belong to and dropping blocks that normalize to nothing.
class BlockBuilder {
 public:
  void append_line(std::string_view line) { append_rendered(render_inline(line)); }

  // Appends text that has already been rendered (no markdown inline rules
  // applied), joining it into the block currently being built. Used by the
  // HTML normalizer, whose inline rules are different from markdown's.
  void append_text(std::string_view rendered) { append_rendered(rendered); }

  void push_literal(std::string_view block) {
    flush();
    blocks_.emplace_back(block);
  }

  void flush() {
    if (!open_.empty()) {
      blocks_.push_back(open_);
      open_.clear();
    }
  }

  std::string result() {
    flush();
    std::string out;
    for (std::size_t index = 0; index < blocks_.size(); ++index) {
      if (index > 0) {
        out += "\n\n";
      }
      out += blocks_[index];
    }
    return out;
  }

 private:
  void append_rendered(std::string_view rendered) {
    const auto trimmed = trim(rendered);
    if (trimmed.empty()) {
      return;
    }
    if (open_.empty()) {
      open_ = std::string(trimmed);
    } else {
      open_ += ' ';
      open_ += trimmed;
    }
  }

  std::vector<std::string> blocks_;
  std::string open_;
};

// ---------------------------------------------------------------------------
// HTML normalization. HTML is not line-oriented like Markdown, so this is a
// separate character-level scan rather than a line-based one, but it shares
// BlockBuilder for block joining and applies the same five rules.

// Decodes a handful of named entities plus numeric character references.
// Unknown named entities are left as-is (better to read "&foo;" literally
// than to silently drop text).
void append_utf8(std::string &out, long code_point) {
  if (code_point < 0) {
    return;
  }
  if (code_point <= 0x7F) {
    out += static_cast<char>(code_point);
  } else if (code_point <= 0x7FF) {
    out += static_cast<char>(0xC0 | (code_point >> 6));
    out += static_cast<char>(0x80 | (code_point & 0x3F));
  } else if (code_point <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (code_point >> 12));
    out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code_point & 0x3F));
  } else if (code_point <= 0x10FFFF) {
    out += static_cast<char>(0xF0 | (code_point >> 18));
    out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code_point & 0x3F));
  }
}

std::string decode_entities(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '&') {
      const auto semi = text.find(';', index + 1);
      if (semi != std::string_view::npos && semi - index <= 10) {
        const auto entity = text.substr(index + 1, semi - index - 1);
        if (entity == "amp") {
          out += '&';
          index = semi + 1;
          continue;
        }
        if (entity == "lt") {
          out += '<';
          index = semi + 1;
          continue;
        }
        if (entity == "gt") {
          out += '>';
          index = semi + 1;
          continue;
        }
        if (entity == "quot") {
          out += '"';
          index = semi + 1;
          continue;
        }
        if (entity == "apos") {
          out += '\'';
          index = semi + 1;
          continue;
        }
        if (entity == "nbsp") {
          out += ' ';
          index = semi + 1;
          continue;
        }
        if (!entity.empty() && entity.front() == '#') {
          const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
          const auto digits = entity.substr(hex ? 2 : 1);
          if (!digits.empty()) {
            try {
              const long code_point = std::stol(std::string(digits), nullptr, hex ? 16 : 10);
              append_utf8(out, code_point);
              index = semi + 1;
              continue;
            } catch (const std::exception &) {
              // Fall through: not a valid numeric reference, keep it literal.
            }
          }
        }
      }
    }
    out += text[index];
    ++index;
  }
  return out;
}

// Collapses any run of whitespace (including newlines, which HTML does not
// treat as significant) to a single space, and trims the ends.
std::string collapse_whitespace(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool pending_space = false;
  for (const char character : text) {
    if (is_space(character)) {
      pending_space = true;
      continue;
    }
    if (pending_space && !out.empty()) {
      out += ' ';
    }
    pending_space = false;
    out += character;
  }
  return out;
}

struct HtmlTag {
  std::string name;
  bool closing = false;
  bool self_closing = false;
  std::size_t tag_start = 0;
  std::size_t tag_end = 0;  // index of the closing '>' of the tag.
};

// Finds the '>' that closes a tag starting at `start` (which must point at
// '<'), respecting quoted attribute values that may themselves contain '>'.
bool find_tag_end(std::string_view html, std::size_t start, std::size_t &end) {
  char quote = '\0';
  for (std::size_t index = start + 1; index < html.size(); ++index) {
    const char character = html[index];
    if (quote != '\0') {
      if (character == quote) {
        quote = '\0';
      }
      continue;
    }
    if (character == '"' || character == '\'') {
      quote = character;
    } else if (character == '>') {
      end = index;
      return true;
    }
  }
  return false;
}

bool parse_tag_at(std::string_view html, std::size_t pos, HtmlTag &tag) {
  std::size_t end = 0;
  if (!find_tag_end(html, pos, end)) {
    return false;
  }
  std::size_t index = pos + 1;
  bool closing = false;
  if (index < html.size() && html[index] == '/') {
    closing = true;
    ++index;
  }
  const std::size_t name_start = index;
  while (index < end && is_ascii_alnum(html[index])) {
    ++index;
  }
  if (index == name_start) {
    return false;
  }
  std::string name(html.substr(name_start, index - name_start));
  for (char &character : name) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  tag.name = std::move(name);
  tag.closing = closing;
  tag.self_closing = end > pos && html[end - 1] == '/';
  tag.tag_start = pos;
  tag.tag_end = end;
  return true;
}

// Case-insensitive search for `</name>` within [from, end).
std::size_t find_closing_tag(std::string_view html, std::size_t from, std::size_t end,
                              std::string_view name) {
  std::size_t pos = from;
  while (pos < end) {
    const auto less_than = html.find('<', pos);
    if (less_than == std::string_view::npos || less_than >= end) {
      return std::string_view::npos;
    }
    HtmlTag tag;
    if (parse_tag_at(html, less_than, tag) && tag.tag_end < end && tag.closing && tag.name == name) {
      return less_than;
    }
    pos = less_than + 1;
  }
  return std::string_view::npos;
}

// Returns the index just past a closing tag found at `close_pos`, or `end`
// if there was no closing tag (an unterminated element swallows the rest of
// the span, mirroring how an unterminated code fence is handled in Markdown).
std::size_t advance_past_closing(std::string_view html, std::size_t close_pos, std::size_t end) {
  if (close_pos == std::string_view::npos) {
    return end;
  }
  HtmlTag close_tag;
  if (!parse_tag_at(html, close_pos, close_tag)) {
    return end;
  }
  return close_tag.tag_end + 1;
}

std::size_t find_attribute(std::string_view tag, std::string_view name) {
  for (std::size_t index = 0; index + name.size() <= tag.size(); ++index) {
    bool match = true;
    for (std::size_t k = 0; k < name.size(); ++k) {
      if (std::tolower(static_cast<unsigned char>(tag[index + k])) !=
          std::tolower(static_cast<unsigned char>(name[k]))) {
        match = false;
        break;
      }
    }
    if (!match) {
      continue;
    }
    if (index > 0 && !is_space(tag[index - 1]) && tag[index - 1] != '<') {
      continue;
    }
    std::size_t after = index + name.size();
    while (after < tag.size() && is_space(tag[after])) {
      ++after;
    }
    if (after < tag.size() && tag[after] == '=') {
      return index;
    }
  }
  return std::string_view::npos;
}

// Extracts an attribute's raw (still entity-encoded) value from a tag's text,
// e.g. `get_attribute("<img alt=\"a cat\">", "alt")` returns "a cat".
std::optional<std::string> get_attribute(std::string_view tag, std::string_view name) {
  const auto start = find_attribute(tag, name);
  if (start == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t index = start + name.size();
  while (index < tag.size() && is_space(tag[index])) {
    ++index;
  }
  if (index >= tag.size() || tag[index] != '=') {
    return std::nullopt;
  }
  ++index;
  while (index < tag.size() && is_space(tag[index])) {
    ++index;
  }
  if (index < tag.size() && (tag[index] == '"' || tag[index] == '\'')) {
    const char quote = tag[index];
    ++index;
    const auto value_start = index;
    while (index < tag.size() && tag[index] != quote) {
      ++index;
    }
    return std::string(tag.substr(value_start, index - value_start));
  }
  const auto value_start = index;
  while (index < tag.size() && !is_space(tag[index]) && tag[index] != '>' && tag[index] != '/') {
    ++index;
  }
  return std::string(tag.substr(value_start, index - value_start));
}

void scan_html(std::string_view html, std::size_t &pos, std::size_t end, BlockBuilder &builder);

// Renders a self-contained span of HTML (the contents of a heading or a
// link) as a single piece of inline text: block boundaries that a malformed
// nested block tag might introduce are collapsed to spaces rather than kept,
// since a heading or link is read as one phrase.
std::string render_span(std::string_view html, std::size_t start, std::size_t end) {
  BlockBuilder local;
  std::size_t pos = start;
  scan_html(html, pos, end, local);
  const auto result = local.result();
  std::string joined;
  joined.reserve(result.size());
  bool in_gap = false;
  for (const char character : result) {
    if (character == '\n') {
      in_gap = true;
      continue;
    }
    if (in_gap && !joined.empty()) {
      joined += ' ';
    }
    in_gap = false;
    joined += character;
  }
  return joined;
}

// Scans html[pos, end), appending rendered blocks to `builder` and leaving
// `pos` at `end`. This is the single pass shared by the top-level call and
// by render_span for nested spans.
void scan_html(std::string_view html, std::size_t &pos, std::size_t end, BlockBuilder &builder) {
  std::string run;
  const auto flush_run = [&]() {
    if (!run.empty()) {
      builder.append_text(collapse_whitespace(run));
      run.clear();
    }
  };

  while (pos < end) {
    if (html[pos] != '<') {
      const auto next = html.find('<', pos);
      const auto text_end = (next == std::string_view::npos || next >= end) ? end : next;
      run += decode_entities(html.substr(pos, text_end - pos));
      pos = text_end;
      continue;
    }

    if (html.compare(pos, 4, "<!--") == 0) {
      const auto close = html.find("-->", pos + 4);
      pos = (close == std::string_view::npos || close >= end) ? end : close + 3;
      continue;
    }

    HtmlTag tag;
    if (!parse_tag_at(html, pos, tag) || tag.tag_end >= end) {
      // Not a recognizable tag (e.g. a bare '<'); read it literally.
      run += '<';
      ++pos;
      continue;
    }
    const auto &name = tag.name;

    // A <pre> block is announced and skipped whole, same as a fenced code
    // block in Markdown -- this also covers <pre><code>...</code></pre>,
    // since the inner <code> is never reached.
    if (!tag.closing && name == "pre") {
      flush_run();
      const auto close = find_closing_tag(html, tag.tag_end + 1, end, "pre");
      builder.push_literal(kCodeBlockMarker);
      pos = advance_past_closing(html, close, end);
      continue;
    }

    // A table is recognized by its tag and skipped whole, replaced with a
    // marker.
    if (!tag.closing && name == "table") {
      flush_run();
      const auto close = find_closing_tag(html, tag.tag_end + 1, end, "table");
      builder.push_literal(kTableMarker);
      pos = advance_past_closing(html, close, end);
      continue;
    }

    // A heading is its own block, giving it a leading pause the same way an
    // ATX heading does in Markdown.
    if (!tag.closing && name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
      flush_run();
      builder.flush();
      const auto close = find_closing_tag(html, tag.tag_end + 1, end, name);
      const auto content_end = (close == std::string_view::npos) ? end : close;
      run += render_span(html, tag.tag_end + 1, content_end);
      flush_run();
      builder.flush();
      pos = advance_past_closing(html, close, end);
      continue;
    }

    // Inline <code> (not inside a <pre>, which was already handled above) is
    // read as its own contents.
    if (!tag.closing && name == "code") {
      const auto close = find_closing_tag(html, tag.tag_end + 1, end, "code");
      const auto content_end = (close == std::string_view::npos) ? end : close;
      run += decode_entities(html.substr(tag.tag_end + 1, content_end - (tag.tag_end + 1)));
      pos = advance_past_closing(html, close, end);
      continue;
    }

    // A link is reduced to its text.
    if (!tag.closing && name == "a") {
      const auto close = find_closing_tag(html, tag.tag_end + 1, end, "a");
      const auto content_end = (close == std::string_view::npos) ? end : close;
      run += render_span(html, tag.tag_end + 1, content_end);
      pos = advance_past_closing(html, close, end);
      continue;
    }

    // An image is reduced to its alt text, the same convention used for a
    // Markdown image.
    if (!tag.closing && name == "img") {
      const auto tag_text = html.substr(tag.tag_start, tag.tag_end - tag.tag_start + 1);
      const auto alt = get_attribute(tag_text, "alt");
      if (alt.has_value()) {
        run += decode_entities(*alt);
      }
      pos = tag.tag_end + 1;
      continue;
    }

    // <br> and the common block containers give a pause the same way a
    // blank line does in Markdown.
    if (name == "br" || name == "p" || name == "div" || name == "li" || name == "blockquote") {
      flush_run();
      builder.flush();
      pos = tag.tag_end + 1;
      continue;
    }

    // Any other tag -- list containers, inline formatting such as <b>,
    // <strong>, <em>, <i>, <span>, <u>, and stray/unmatched closing tags --
    // is stripped, keeping its text content flowing inline.
    pos = tag.tag_end + 1;
  }

  flush_run();
}

}  // namespace

std::string normalize_markdown(std::string_view markdown) {
  const auto lines = split_lines(markdown);
  BlockBuilder builder;

  for (std::size_t index = 0; index < lines.size(); ++index) {
    auto line = lines[index];
    // A quoted block is spoken as its contents; the marker itself is markup.
    while (strip_blockquote(line)) {
    }
    const auto trimmed = trim(line);

    if (trimmed.empty()) {
      builder.flush();
      continue;
    }

    // A code block is announced and its contents skipped, so nothing inside
    // it is interpreted as markup.
    if (is_code_fence(trimmed)) {
      builder.push_literal(kCodeBlockMarker);
      const char marker = trimmed.front();
      ++index;
      while (index < lines.size()) {
        auto body = lines[index];
        while (strip_blockquote(body)) {
        }
        const auto body_trimmed = trim(body);
        if (is_code_fence(body_trimmed) && body_trimmed.front() == marker) {
          break;
        }
        ++index;
      }
      continue;
    }

    if (is_thematic_break(trimmed) || is_setext_underline(trimmed)) {
      builder.flush();
      continue;
    }

    // A table is recognized by its delimiter row, then skipped whole.
    if (trimmed.find('|') != std::string_view::npos && index + 1 < lines.size()) {
      auto next = lines[index + 1];
      while (strip_blockquote(next)) {
      }
      const auto next_trimmed = trim(next);
      if (next_trimmed.find('|') != std::string_view::npos && is_table_delimiter(next_trimmed)) {
        builder.push_literal(kTableMarker);
        index += 2;
        while (index < lines.size()) {
          auto row = lines[index];
          while (strip_blockquote(row)) {
          }
          const auto row_trimmed = trim(row);
          if (row_trimmed.empty() || row_trimmed.find('|') == std::string_view::npos) {
            break;
          }
          ++index;
        }
        --index;
        continue;
      }
    }

    // A heading is its own block, and the blank line that separates blocks is
    // what gives it the leading pause.
    std::string_view heading;
    if (parse_atx_heading(trimmed, heading)) {
      builder.flush();
      builder.append_line(heading);
      builder.flush();
      continue;
    }

    auto item = line;
    if (strip_list_marker(item)) {
      builder.flush();
      builder.append_line(item);
      continue;
    }

    builder.append_line(trimmed);
  }

  return builder.result();
}

std::string normalize_html(std::string_view html) {
  BlockBuilder builder;
  std::size_t pos = 0;
  scan_html(html, pos, html.size(), builder);
  return builder.result();
}

}  // namespace tts_host
