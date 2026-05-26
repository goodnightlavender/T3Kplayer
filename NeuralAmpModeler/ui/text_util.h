// text_util.h — small UTF-8 sanitizer for redesign render sites.
//
// The vendored Inter subset doesn't include U+00B7 (middle dot),
// U+2014 (em dash), or U+2026 (horizontal ellipsis). Render sites that
// take data from LibraryDb / cloud catalog responses (creator strings,
// display names, descriptions) can hit any of those code points and
// paint a missing-glyph "tofu" box. Pipe the string through
// `toAsciiSafe()` immediately before the IGraphics::DrawText call to
// replace the offenders with ASCII-safe equivalents:
//
//   U+00B7 (middle dot, "\xC2\xB7")     -> " - "
//   U+2013 (en dash,    "\xE2\x80\x93") -> " - "
//   U+2014 (em dash,    "\xE2\x80\x94") -> " - "
//   U+2018/2019 curly apostrophes        -> "'"
//   U+201C/201D curly quotes             -> "\""
//   U+2026 (ellipsis,   "\xE2\x80\xA6") -> "..."
//
// Inputs are assumed to be UTF-8. Bytes that aren't part of one of the
// three target sequences are copied through unchanged, so ASCII text is
// untouched. The function is a value-returning copy, not in-place, so
// the caller can apply it inline.
//
// Confined to the redesigned Tone-tab surface for now; if the same
// tofu shows up in other views (Library / Cloud cards) we can expand
// the call sites without touching this header.

#pragma once

#include <string>

namespace t3k::text_util {

inline std::string toAsciiSafe(const std::string& in)
{
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();)
  {
    // U+00B7 middle dot — 0xC2 0xB7
    const unsigned char ch = static_cast<unsigned char>(in[i]);
    if (in[i] == '\r' || in[i] == '\n' || in[i] == '\t' || ch < 0x20)
    {
      if (out.empty() || out.back() != ' ') out += " ";
      ++i;
      continue;
    }
    if (i + 1 < in.size()
        && static_cast<unsigned char>(in[i])     == 0xC2
        && static_cast<unsigned char>(in[i + 1]) == 0xB7)
    {
      out += " - ";
      i += 2;
      continue;
    }
    // U+2014 em dash — 0xE2 0x80 0x94
    if (i + 2 < in.size()
        && static_cast<unsigned char>(in[i])     == 0xE2
        && static_cast<unsigned char>(in[i + 1]) == 0x80
        && (static_cast<unsigned char>(in[i + 2]) == 0x93 ||
            static_cast<unsigned char>(in[i + 2]) == 0x94))
    {
      out += " - ";
      i += 3;
      continue;
    }
    // U+2018 / U+2019 curly apostrophes
    if (i + 2 < in.size()
        && static_cast<unsigned char>(in[i])     == 0xE2
        && static_cast<unsigned char>(in[i + 1]) == 0x80
        && (static_cast<unsigned char>(in[i + 2]) == 0x98 ||
            static_cast<unsigned char>(in[i + 2]) == 0x99))
    {
      out += "'";
      i += 3;
      continue;
    }
    // U+201C / U+201D curly quotes
    if (i + 2 < in.size()
        && static_cast<unsigned char>(in[i])     == 0xE2
        && static_cast<unsigned char>(in[i + 1]) == 0x80
        && (static_cast<unsigned char>(in[i + 2]) == 0x9C ||
            static_cast<unsigned char>(in[i + 2]) == 0x9D))
    {
      out += "\"";
      i += 3;
      continue;
    }
    // U+2026 horizontal ellipsis — 0xE2 0x80 0xA6
    if (i + 2 < in.size()
        && static_cast<unsigned char>(in[i])     == 0xE2
        && static_cast<unsigned char>(in[i + 1]) == 0x80
        && static_cast<unsigned char>(in[i + 2]) == 0xA6)
    {
      out += "...";
      i += 3;
      continue;
    }
    // U+00A0 non-breaking space
    if (i + 1 < in.size()
        && static_cast<unsigned char>(in[i])     == 0xC2
        && static_cast<unsigned char>(in[i + 1]) == 0xA0)
    {
      out += " ";
      i += 2;
      continue;
    }
    if (static_cast<unsigned char>(in[i]) >= 0x80)
    {
      const unsigned char b = static_cast<unsigned char>(in[i]);
      if ((b & 0xE0) == 0xC0) i += 2;
      else if ((b & 0xF0) == 0xE0) i += 3;
      else if ((b & 0xF8) == 0xF0) i += 4;
      else ++i;
      out += " ";
      continue;
    }
    out += in[i];
    ++i;
  }
  return out;
}

}  // namespace t3k::text_util
