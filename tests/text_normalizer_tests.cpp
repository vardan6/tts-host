#include "tts_host/text_normalizer.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect(const std::string &markdown, const std::string &expected, const std::string &message) {
  const auto actual = tts_host::normalize_markdown(markdown);
  if (actual != expected) {
    throw std::runtime_error(message + "\n  expected: [" + expected + "]\n  actual:   [" + actual +
                             "]");
  }
}

void expect_html(const std::string &html, const std::string &expected, const std::string &message) {
  const auto actual = tts_host::normalize_html(html);
  if (actual != expected) {
    throw std::runtime_error(message + "\n  expected: [" + expected + "]\n  actual:   [" + actual +
                             "]");
  }
}

// docs/requirements/product.md, "Speech behaviour": code blocks are announced
// and skipped.
void code_blocks_are_announced_and_skipped() {
  expect("Before\n\n```cpp\nint main() { return 0; }\n```\n\nAfter",
         "Before\n\nCode block.\n\nAfter", "a fenced code block was not announced and skipped");
  expect("~~~\nnot read\n~~~", "Code block.", "a tilde fence was not announced and skipped");
  // Markup inside a code block must not be interpreted.
  expect("```\n# Heading\n[link](url)\n```", "Code block.",
         "markup inside a code block leaked into the output");
  // An unterminated fence still swallows the rest of the input.
  expect("```\ndangling", "Code block.", "an unterminated fence was not skipped");
}

// Inline code is read.
void inline_code_is_read() {
  expect("Call `make_runner_load_request` first.", "Call make_runner_load_request first.",
         "inline code was not read as its contents");
  expect("Use ``a ` b`` here.", "Use a ` b here.", "a multi-backtick code span was not handled");
  expect("A `*` is not emphasis.", "A * is not emphasis.",
         "emphasis stripping leaked into a code span");
  expect("An unmatched ` backtick.", "An unmatched ` backtick.",
         "an unmatched backtick was not left alone");
}

// Links are read as their text.
void links_are_read_as_their_text() {
  expect("See [the design](docs/design/architecture.md).", "See the design.",
         "an inline link was not reduced to its text");
  expect("See [the design][design].", "See the design.",
         "a reference link was not reduced to its text");
  expect("An ![alt text](image.png) here.", "An alt text here.",
         "an image was not reduced to its alt text");
  expect("A [bracketed] aside.", "A [bracketed] aside.",
         "literal brackets were treated as a link");
  expect("Nested [a [b](c) d](e).", "Nested a b d.", "a nested link label was not rendered");
}

// Tables are skipped with a marker.
void tables_are_skipped_with_a_marker() {
  expect("Before\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |\n\nAfter",
         "Before\n\nTable skipped.\n\nAfter", "a table was not skipped with a marker");
  expect("| a | b |\n|:--|--:|\n| 1 | 2 |", "Table skipped.",
         "an aligned delimiter row was not recognized");
  // A pipe without a delimiter row is ordinary text, not a table.
  expect("a | b is a choice", "a | b is a choice", "a bare pipe was mistaken for a table");
}

// Headings are read with a leading pause, which normalization expresses as a
// block boundary; converting that to silence belongs to the split stage.
void headings_get_a_leading_pause() {
  expect("Intro text.\n## Section\nBody text.", "Intro text.\n\nSection\n\nBody text.",
         "a heading did not become its own block");
  expect("### Closed heading ###", "Closed heading", "a closing heading sequence was not stripped");
  expect("#hashtag", "#hashtag", "a bare hash was mistaken for a heading");
  expect("####### seven", "####### seven", "a seven-hash line was mistaken for a heading");
  expect("Title\n=====\nBody.", "Title\n\nBody.", "a setext underline was not dropped");
}

// The remaining markup that would otherwise be read literally.
void other_markup_is_not_read_literally() {
  expect("Some **bold** and _italic_ text.", "Some bold and italic text.",
         "emphasis markers were read literally");
  expect("Keep snake_case and 2 * 3 intact.", "Keep snake_case and 2 * 3 intact.",
         "emphasis stripping mangled an identifier or arithmetic");
  expect("- first\n- second", "first\n\nsecond", "list items were not separated into blocks");
  expect("1. first\n2) second", "first\n\nsecond", "ordered list markers were not stripped");
  expect("> quoted text", "quoted text", "a blockquote marker was read literally");
  expect("> ## quoted heading", "quoted heading", "a heading inside a blockquote was not handled");
  expect("Text\n\n---\n\nMore", "Text\n\nMore", "a thematic break was not dropped");
  expect("Escaped \\*not emphasis\\*.", "Escaped *not emphasis*.",
         "an escaped character was not unescaped");
}

// Wrapping and whitespace, since the runner receives one string per request.
void paragraphs_are_joined_and_trimmed() {
  expect("A line\nwrapped here.\n\nSecond paragraph.",
         "A line wrapped here.\n\nSecond paragraph.", "a wrapped paragraph was not joined");
  expect("", "", "empty input did not normalize to empty output");
  expect("   \n\n  \n", "", "whitespace-only input did not normalize to empty output");
  expect("Windows\r\nnewlines.\r\n", "Windows newlines.", "CRLF input was not handled");
  expect("  \n\nPlain text.\n\n\n", "Plain text.", "surrounding blank lines were not dropped");
  // Plain text must survive untouched; it is the overwhelmingly common case.
  expect("Just a sentence.", "Just a sentence.", "plain text was altered");
}

// The same five rules, but applied to HTML instead of Markdown
// (docs/design/architecture.md#speech-pipeline).

void html_code_blocks_are_announced_and_skipped() {
  expect_html("<p>Before</p><pre>int main() { return 0; }</pre><p>After</p>",
              "Before\n\nCode block.\n\nAfter", "a <pre> block was not announced and skipped");
  expect_html("<pre><code>not read</code></pre>", "Code block.",
              "<pre><code> was not announced and skipped");
  // Markup inside a code block must not be interpreted, nor its entities decoded.
  expect_html("<pre>&lt;b&gt;not bold&lt;/b&gt;</pre>", "Code block.",
              "markup inside a code block leaked into the output");
  // An unterminated <pre> still swallows the rest of the input.
  expect_html("<pre>dangling", "Code block.", "an unterminated <pre> was not skipped");
}

void html_inline_code_is_read() {
  expect_html("<p>Call <code>make_runner_load_request</code> first.</p>",
              "Call make_runner_load_request first.", "inline <code> was not read as its contents");
  expect_html("<code>a &lt; b</code>", "a < b", "entities inside inline code were not decoded");
}

void html_links_are_read_as_their_text() {
  expect_html("<p>See <a href=\"docs/design/architecture.md\">the design</a>.</p>", "See the design.",
              "a link was not reduced to its text");
  expect_html("<p>An <img src=\"image.png\" alt=\"alt text\"> here.</p>", "An alt text here.",
              "an image was not reduced to its alt text");
  expect_html("<img alt='self closing' />", "self closing",
              "a self-closing image was not reduced to its alt text");
  expect_html("<img src=\"x.png\">", "", "an image with no alt text produced stray output");
}

void html_tables_are_skipped_with_a_marker() {
  expect_html("<p>Before</p><table><tr><td>a</td></tr><tr><td>b</td></tr></table><p>After</p>",
              "Before\n\nTable skipped.\n\nAfter", "a table was not skipped with a marker");
}

void html_headings_get_a_leading_pause() {
  expect_html("<p>Intro text.</p><h2>Section</h2><p>Body text.</p>",
              "Intro text.\n\nSection\n\nBody text.", "a heading did not become its own block");
  expect_html("<h1>Closed <b>heading</b></h1>", "Closed heading",
              "inline markup inside a heading was not stripped");
}

void html_inline_formatting_is_stripped() {
  expect_html("<p>Some <b>bold</b> and <em>italic</em> text.</p>", "Some bold and italic text.",
              "inline formatting tags were read literally");
  expect_html("<a href=\"x\">Some <b>bold</b> text</a>", "Some bold text",
              "nested inline formatting inside a link was not stripped");
}

void html_entities_are_decoded() {
  expect_html(
      "<p>Fish &amp; Chips &lt;3 &quot;great&quot; &#39;really&#39;&nbsp;great.</p>",
      "Fish & Chips <3 \"great\" 'really' great.", "named and numeric entities were not decoded");
  expect_html("&#x27;quoted&#x27;", "'quoted'", "a hex numeric entity was not decoded");
}

void html_comments_are_stripped() {
  expect_html("<p>Before<!-- hidden --> After</p>", "Before After", "a comment was read literally");
  expect_html("<!-- just a comment -->Visible", "Visible", "a leading comment left a stray block");
}

void html_blocks_and_whitespace() {
  expect_html("<p>\n  Line one\n  Line two\n</p>", "Line one Line two",
              "whitespace inside a block was not collapsed");
  expect_html("<p>Line one<br>Line two</p>", "Line one\n\nLine two",
              "<br> did not introduce a pause");
  expect_html("<p>Unclosed paragraph", "Unclosed paragraph",
              "an unclosed paragraph tag was not handled");
  expect_html("   \n\t  ", "", "whitespace-only HTML did not normalize to empty output");
  // Plain text with no markup must survive untouched.
  expect_html("Just a sentence.", "Just a sentence.", "plain text was altered");
  expect_html("<p>First.</p><p>Second.</p>", "First.\n\nSecond.",
              "sibling paragraphs were not separated into blocks");
}

}  // namespace

int main() {
  try {
    code_blocks_are_announced_and_skipped();
    inline_code_is_read();
    links_are_read_as_their_text();
    tables_are_skipped_with_a_marker();
    headings_get_a_leading_pause();
    other_markup_is_not_read_literally();
    paragraphs_are_joined_and_trimmed();

    require(tts_host::normalize_markdown("# Only a heading") == "Only a heading",
            "a leading heading produced a stray block boundary");

    html_code_blocks_are_announced_and_skipped();
    html_inline_code_is_read();
    html_links_are_read_as_their_text();
    html_tables_are_skipped_with_a_marker();
    html_headings_get_a_leading_pause();
    html_inline_formatting_is_stripped();
    html_entities_are_decoded();
    html_comments_are_stripped();
    html_blocks_and_whitespace();

    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
