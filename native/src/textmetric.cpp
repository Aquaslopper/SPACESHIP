// Text metrics.
//
// Measurement drives both layout (where lines break) and the painter (where
// glyphs land), so there is exactly one implementation. Widths are expressed as
// per-character advance ratios of the em square: close to the reference's
// Segoe UI / Consolas / Georgia metrics, and identical for monospace faces.
#include <cctype>
#include <cstring>

#include "htautil.h"
#include "layout.h"

namespace hta {
namespace textmetric {

bool isMonospace(const std::string& family) {
  std::string low = lowerAscii(family);
  return low.find("consolas") != std::string::npos || low.find("courier") != std::string::npos ||
         low.find("monospace") != std::string::npos || low.find("lucida console") != std::string::npos;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

namespace {

bool isSerif(const std::string& family) {
  std::string low = lowerAscii(family);
  return low.find("georgia") != std::string::npos || low.find("serif") != std::string::npos;
}

float charRatio(unsigned cp, bool mono, bool serif) {
  if (mono) return 0.6f;
  if (cp == ' ') return 0.278f;
  if (cp == '\t') return 1.4f;
  if (cp >= 0xA0) return 0.58f;
  if (cp >= 0x80) return 0.58f;
  char ch = static_cast<char>(cp);
  switch (ch) {
    case '.': case ',': case ';': return 0.27f;
    case ':': return 0.33f;
    case '\'': case '`': return 0.2f;
    case '|': return 0.24f;
    case '!': case 'i': case 'j': case 'l': return 0.3f;
    case 'f': case 't': case 'r': return 0.36f;
    case 'I': case '(': case ')': case '[': case ']': return 0.33f;
    case 'm': return 0.87f;
    case 'w': return 0.82f;
    case 'M': return 0.86f;
    case 'W': return 0.89f;
    case '@': return 0.95f;
    default: break;
  }
  if (std::isdigit(static_cast<unsigned char>(ch))) return serif ? 0.5f : 0.556f;
  if (std::isupper(static_cast<unsigned char>(ch))) return serif ? 0.72f : 0.66f;
  return serif ? 0.5f : 0.53f;
}

}  // namespace

float advance(const std::string& text, const std::string& family, float size, int weight, bool italic,
              float letterSpacing) {
  (void)italic;
  bool mono = isMonospace(family);
  bool serif = isSerif(family);
  float boldFactor = weight >= 700 ? 1.045f : 1.0f;
  float total = 0;
  for (size_t i = 0; i < text.size();) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    unsigned cp = c;
    size_t len = 1;
    if (c >= 0xF0) {
      len = 4;
      cp = c & 0x07u;
    } else if (c >= 0xE0) {
      len = 3;
      cp = c & 0x0Fu;
    } else if (c >= 0xC0) {
      len = 2;
      cp = c & 0x1Fu;
    }
    for (size_t k = 1; k < len && i + k < text.size(); ++k)
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
    i += len;
    total += charRatio(cp, mono, serif) * boldFactor * size + letterSpacing;
  }
  return total;
}

}  // namespace textmetric

// ---------------------------------------------------------------------------
// Wrapping shared by layout and painting
// ---------------------------------------------------------------------------
std::vector<std::string> wrapText(const std::string& text, const Computed& style, float maxWidth) {
  std::vector<std::string> lines;
  if (text.empty()) return lines;
  if (maxWidth <= 1) maxWidth = 1;
  bool pre = style.whiteSpace == "pre" || style.whiteSpace == "pre-wrap";
  bool nowrap = style.whiteSpace == "nowrap" || style.whiteSpace == "pre";
  std::string source = text;
  if (!pre) {
    std::string collapsed;
    bool space = false;
    for (char c : source) {
      if (textmetric::isSpace(c)) {
        if (!space) collapsed.push_back(' ');
        space = true;
      } else {
        collapsed.push_back(c);
        space = false;
      }
    }
    source = trim(collapsed);
  }
  auto width = [&](const std::string& s) {
    return textmetric::advance(s, style.fontFamily, style.fontSize, style.weight, style.italic,
                               style.letterSpacing);
  };
  size_t start = 0;
  while (start <= source.size()) {
    size_t nl = source.find('\n', start);
    size_t end = nl == std::string::npos ? source.size() : nl;
    std::string paragraph = source.substr(start, end - start);
    if (nowrap) {
      lines.push_back(paragraph);
    } else {
      std::string current;
      size_t i = 0;
      while (i <= paragraph.size()) {
        size_t sp = paragraph.find(' ', i);
        std::string word = paragraph.substr(i, (sp == std::string::npos ? paragraph.size() : sp) - i);
        std::string candidate = current.empty() ? word : current + " " + word;
        if (width(candidate) <= maxWidth || current.empty()) {
          current = candidate;
          // A single word wider than the line is broken, like overflow-wrap.
          while (utf8Length(current) > 1 && width(current) > maxWidth) {
            size_t chars = utf8Length(current);
            size_t cut = chars;
            while (cut > 1 && width(utf8Substr(current, 0, cut)) > maxWidth) --cut;
            if (cut >= chars) break;
            lines.push_back(utf8Substr(current, 0, cut));
            current = utf8Substr(current, cut, chars - cut);
          }
        } else {
          lines.push_back(current);
          current = word;
        }
        if (sp == std::string::npos) break;
        i = sp + 1;
      }
      lines.push_back(current);
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  for (std::string& line : lines) line = trim(line);
  return lines;
}

}  // namespace hta
