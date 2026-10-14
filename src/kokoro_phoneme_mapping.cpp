#include "tts_host/kokoro_phoneme_mapping.hpp"

#include <array>
#include <string>
#include <unordered_map>

namespace tts_host {
namespace {

// Kokoro-82M's ONNX phoneme vocabulary (id 0 is a padding token added by
// callers, not a phoneme, so it has no entry here). Ported from the "vocab"
// table in onnx-community/Kokoro-82M-v1.0-ONNX / thewh1teagle/kokoro-onnx's
// config.json.
const std::unordered_map<std::string, std::int64_t> &kokoro_vocab() {
  static const std::unordered_map<std::string, std::int64_t> vocab{
      {";", 1},         {":", 2},         {",", 3},         {".", 4},
      {"!", 5},         {"?", 6},         {"\u2014", 9},    {"\u2026", 10},
      {"\"", 11},       {"(", 12},        {")", 13},        {"\u201c", 14},
      {"\u201d", 15},   {" ", 16},        {"\u0303", 17},   {"\u02a3", 18},
      {"\u02a5", 19},   {"\u02a6", 20},   {"\u02a8", 21},   {"\u1d5d", 22},
      {"\uab67", 23},   {"A", 24},        {"I", 25},        {"O", 31},
      {"Q", 33},        {"S", 35},        {"T", 36},        {"W", 39},
      {"Y", 41},        {"\u1d4a", 42},   {"a", 43},        {"b", 44},
      {"c", 45},        {"d", 46},        {"e", 47},        {"f", 48},
      {"h", 50},        {"i", 51},        {"j", 52},        {"k", 53},
      {"l", 54},        {"m", 55},        {"n", 56},        {"o", 57},
      {"p", 58},        {"q", 59},        {"r", 60},        {"s", 61},
      {"t", 62},        {"u", 63},        {"v", 64},        {"w", 65},
      {"x", 66},        {"y", 67},        {"z", 68},        {"\u0251", 69},
      {"\u0250", 70},   {"\u0252", 71},   {"\u00e6", 72},   {"\u03b2", 75},
      {"\u0254", 76},   {"\u0255", 77},   {"\u00e7", 78},   {"\u0256", 80},
      {"\u00f0", 81},   {"\u02a4", 82},   {"\u0259", 83},   {"\u025a", 85},
      {"\u025b", 86},   {"\u025c", 87},   {"\u025f", 90},   {"\u0261", 92},
      {"\u0265", 99},   {"\u0268", 101},  {"\u026a", 102},  {"\u029d", 103},
      {"\u026f", 110},  {"\u0270", 111},  {"\u014b", 112},  {"\u0273", 113},
      {"\u0272", 114},  {"\u0274", 115},  {"\u00f8", 116},  {"\u0278", 118},
      {"\u03b8", 119},  {"\u0153", 120},  {"\u0279", 123},  {"\u027e", 125},
      {"\u027b", 126},  {"\u0281", 128},  {"\u027d", 129},  {"\u0282", 130},
      {"\u0283", 131},  {"\u0288", 132},  {"\u02a7", 133},  {"\u028a", 135},
      {"\u028b", 136},  {"\u028c", 138},  {"\u0263", 139},  {"\u0264", 140},
      {"\u03c7", 142},  {"\u028e", 143},  {"\u0292", 147},  {"\u0294", 148},
      {"\u02c8", 156},  {"\u02cc", 157},  {"\u02d0", 158},  {"\u02b0", 162},
      {"\u02b2", 164},  {"\u2193", 169},  {"\u2192", 171},  {"\u2197", 172},
      {"\u2198", 173},  {"\u1d7b", 177},
  };
  return vocab;
}

// Ties multi-letter espeak-ng phonemes (diphthongs, affricates); `--ipa=3`
// emits it as ZERO WIDTH JOINER, not the `^` misaki's own phonemizer
// configuration uses.
constexpr std::string_view kTie = "\u200d";
// COMBINING VERTICAL LINE BELOW: marks a syllabic consonant (e.g. bottle's
// second "l").
constexpr std::string_view kSyllabic = "\u0329";

// Ported verbatim from misaki's `EspeakFallback.E2M`, `^` replaced by
// `kTie`, already sorted longest-key-first so earlier entries never get
// pre-empted by a shorter one applied first (mirrors Python's
// `sorted(E2M.items(), key=lambda kv: -len(kv[0]))`).
constexpr std::array<std::pair<std::string_view, std::string_view>, 20> kDigraphReplacements{{
    {"\u0294\u02cc" "n" "\u0329", "\u0294n"},
    {"\u0294" "n" "\u0329", "\u0294n"},
    {"a\u200d\u026a", "I"},
    {"a\u200d\u028a", "W"},
    {"d\u200d\u0292", "\u02a4"},
    {"e\u200d\u026a", "A"},
    {"t\u200d\u0283", "\u02a7"},
    {"\u0254\u200d\u026a", "Y"},
    {"\u0259\u200dl", "\u1d4al"},
    {"\u02b2o", "jo"},
    {"\u02b2\u0259", "j\u0259"},
    {"e", "A"},
    {"\u02b2", ""},
    {"\u025a", "\u0259\u0279"},
    {"r", "\u0279"},
    {"x", "k"},
    {"\u00e7", "k"},
    {"\u0250", "\u0259"},
    {"\u026c", "l"},
    {"\u0303", ""},
}};

// American-English branch of misaki's `EspeakFallback.__call__` (the `else`
// side of `if self.british:`).
constexpr std::array<std::pair<std::string_view, std::string_view>, 5> kAmericanReplacements{{
    {"o\u200d\u028a", "O"},
    {"\u025c\u02d0\u0279", "\u025c\u0279"},
    {"\u025c\u02d0", "\u025c\u0279"},
    {"\u026a\u0259", "i\u0259"},
    {"\u02d0", ""},
}};

void replace_all(std::string &text, std::string_view from, std::string_view to) {
  if (from.empty()) {
    return;
  }
  std::size_t position = 0;
  while ((position = text.find(from, position)) != std::string::npos) {
    text.replace(position, from.size(), to);
    position += to.size();
  }
}

std::size_t utf8_codepoint_length(unsigned char lead_byte) {
  if ((lead_byte & 0x80) == 0x00) {
    return 1;
  }
  if ((lead_byte & 0xe0) == 0xc0) {
    return 2;
  }
  if ((lead_byte & 0xf0) == 0xe0) {
    return 3;
  }
  if ((lead_byte & 0xf8) == 0xf0) {
    return 4;
  }
  return 1;
}

// Mirrors `re.sub(r'(\S)̩', r'ᵊ\1', ps).replace(chr(809), '')`: folds a
// syllabic consonant marker onto the codepoint before it (dropping the
// marker), or drops it outright if nothing non-whitespace precedes it.
std::string apply_syllabic_marker(const std::string &input) {
  std::string result;
  result.reserve(input.size());
  std::string previous_codepoint;
  std::size_t index = 0;
  while (index < input.size()) {
    const auto length = utf8_codepoint_length(static_cast<unsigned char>(input[index]));
    const std::string codepoint = input.substr(index, length);
    if (codepoint == kSyllabic) {
      if (!previous_codepoint.empty() && previous_codepoint != " ") {
        result.erase(result.size() - previous_codepoint.size());
        result += "\u1d4a";
        result += previous_codepoint;
      }
      previous_codepoint.clear();
    } else {
      result += codepoint;
      previous_codepoint = codepoint;
    }
    index += length;
  }
  return result;
}

}  // namespace

std::vector<std::int64_t> translate_ipa_to_kokoro_token_ids(std::string_view ipa) {
  std::string phonemes(ipa);

  // espeak-ng starts a new line per clause; Kokoro's vocabulary has no
  // clause-boundary token, so treat clause breaks like word breaks.
  for (auto &character : phonemes) {
    if (character == '\n') {
      character = ' ';
    }
  }

  for (const auto &[from, to] : kDigraphReplacements) {
    replace_all(phonemes, from, to);
  }
  phonemes = apply_syllabic_marker(phonemes);
  for (const auto &[from, to] : kAmericanReplacements) {
    replace_all(phonemes, from, to);
  }
  replace_all(phonemes, "o", "\u0254");
  replace_all(phonemes, "\u027e", "T");
  replace_all(phonemes, "\u0294", "t");
  replace_all(phonemes, kTie, "");

  const auto &vocab = kokoro_vocab();
  std::vector<std::int64_t> token_ids;
  std::size_t index = 0;
  while (index < phonemes.size()) {
    const auto length = utf8_codepoint_length(static_cast<unsigned char>(phonemes[index]));
    const auto found = vocab.find(phonemes.substr(index, length));
    if (found != vocab.end()) {
      token_ids.push_back(found->second);
    }
    index += length;
  }
  return token_ids;
}

}  // namespace tts_host
