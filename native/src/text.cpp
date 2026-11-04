// Text rasterisation.
//
// The reference is a Windows HTA, so its fonts are Segoe UI, Consolas and
// Georgia. Those files cannot be redistributed with this project, so the
// painter rasterises glyphs from whatever regular/bold face the host supplies
// (stb_truetype), and falls back to a compact built-in 6x12 bitmap face so the
// game is always readable even with no font files installed.
#include <cmath>
#include <cstdint>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "canvas.h"
#include "htautil.h"
#include "layout.h"
#include "paint.h"

#if defined(__has_include)
#if __has_include("stb_truetype.h")
#define HTA_HAS_STB 1
#endif
#endif

#ifdef HTA_HAS_STB
#include "stb_truetype.h"
#endif

namespace hta {
namespace {

// ---------------------------------------------------------------------------
// Built-in fallback glyphs: 5x7 bitmap stencil scaled to the requested size,
// drawn with the same coverage-blend path as the TrueType renderer.
// ---------------------------------------------------------------------------
struct FallbackGlyphs {
  std::map<char, std::vector<uint8_t>> map;  // each: 7 rows of 5-bit masks
};

const FallbackGlyphs& fallback() {
  static FallbackGlyphs f;
  static bool init = false;
  if (init) return f;
  init = true;
  auto def = [&](char c, const std::array<const char*, 7>& rows) {
    std::vector<uint8_t> bits(7, 0);
    for (int r = 0; r < 7; ++r) {
      uint8_t v = 0;
      for (int i = 0; i < 5; ++i)
        if (rows[r][i] == '#') v |= static_cast<uint8_t>(1u << (4 - i));
      bits[static_cast<size_t>(r)] = v;
    }
    f.map[c] = bits;
  };
  def(' ', std::array<const char*, 7>{{"     ", "     ", "     ", "     ", "     ", "     ", "     "}});
  def('A', std::array<const char*, 7>{{" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}});
  def('B', std::array<const char*, 7>{{"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}});
  def('C', std::array<const char*, 7>{{" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"}});
  def('D', std::array<const char*, 7>{{"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}});
  def('E', std::array<const char*, 7>{{"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}});
  def('F', std::array<const char*, 7>{{"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}});
  def('G', std::array<const char*, 7>{{" ####", "#    ", "#    ", "#  ##", "#   #", "#   #", " ####"}});
  def('H', std::array<const char*, 7>{{"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}});
  def('I', std::array<const char*, 7>{{"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"}});
  def('J', std::array<const char*, 7>{{"  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "}});
  def('K', std::array<const char*, 7>{{"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}});
  def('L', std::array<const char*, 7>{{"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}});
  def('M', std::array<const char*, 7>{{"#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #"}});
  def('N', std::array<const char*, 7>{{"#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"}});
  def('O', std::array<const char*, 7>{{" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}});
  def('P', std::array<const char*, 7>{{"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}});
  def('Q', std::array<const char*, 7>{{" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}});
  def('R', std::array<const char*, 7>{{"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}});
  def('S', std::array<const char*, 7>{{" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}});
  def('T', std::array<const char*, 7>{{"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}});
  def('U', std::array<const char*, 7>{{"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}});
  def('V', std::array<const char*, 7>{{"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}});
  def('W', std::array<const char*, 7>{{"#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"}});
  def('X', std::array<const char*, 7>{{"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}});
  def('Y', std::array<const char*, 7>{{"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}});
  def('Z', std::array<const char*, 7>{{"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}});
  def('a', std::array<const char*, 7>{{"     ", "     ", " ### ", "    #", " ####", "#   #", " ####"}});
  def('b', std::array<const char*, 7>{{"#    ", "#    ", "#### ", "#   #", "#   #", "#   #", "#### "}});
  def('c', std::array<const char*, 7>{{"     ", "     ", " ####", "#    ", "#    ", "#    ", " ####"}});
  def('d', std::array<const char*, 7>{{"    #", "    #", " ####", "#   #", "#   #", "#   #", " ####"}});
  def('e', std::array<const char*, 7>{{"     ", "     ", " ### ", "#   #", "#####", "#    ", " ####"}});
  def('f', std::array<const char*, 7>{{"  ###", " #   ", " #   ", "#### ", " #   ", " #   ", " #   "}});
  def('g', std::array<const char*, 7>{{"     ", "     ", " ####", "#   #", "#   #", " ####", "    #"}});
  def('h', std::array<const char*, 7>{{"#    ", "#    ", "#### ", "#   #", "#   #", "#   #", "#   #"}});
  def('i', std::array<const char*, 7>{{"  #  ", "     ", " ##  ", "  #  ", "  #  ", "  #  ", " ### "}});
  def('j', std::array<const char*, 7>{{"   # ", "     ", "  ## ", "   # ", "   # ", "   # ", " ##  "}});
  def('k', std::array<const char*, 7>{{"#    ", "#    ", "#  # ", "# #  ", "##   ", "# #  ", "#  # "}});
  def('l', std::array<const char*, 7>{{" ##  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}});
  def('m', std::array<const char*, 7>{{"     ", "     ", "## # ", "# # #", "# # #", "#   #", "#   #"}});
  def('n', std::array<const char*, 7>{{"     ", "     ", "#### ", "#   #", "#   #", "#   #", "#   #"}});
  def('o', std::array<const char*, 7>{{"     ", "     ", " ### ", "#   #", "#   #", "#   #", " ### "}});
  def('p', std::array<const char*, 7>{{"     ", "     ", "#### ", "#   #", "#### ", "#    ", "#    "}});
  def('q', std::array<const char*, 7>{{"     ", "     ", " ####", "#   #", " ####", "    #", "    #"}});
  def('r', std::array<const char*, 7>{{"     ", "     ", "# ###", "##   ", "#    ", "#    ", "#    "}});
  def('s', std::array<const char*, 7>{{"     ", "     ", " ####", "#    ", " ### ", "    #", "#### "}});
  def('t', std::array<const char*, 7>{{" #   ", " #   ", "#### ", " #   ", " #   ", " #   ", "  ###"}});
  def('u', std::array<const char*, 7>{{"     ", "     ", "#   #", "#   #", "#   #", "#   #", " ####"}});
  def('v', std::array<const char*, 7>{{"     ", "     ", "#   #", "#   #", "#   #", " # # ", "  #  "}});
  def('w', std::array<const char*, 7>{{"     ", "     ", "#   #", "#   #", "# # #", "# # #", " # # "}});
  def('x', std::array<const char*, 7>{{"     ", "     ", "#   #", " # # ", "  #  ", " # # ", "#   #"}});
  def('y', std::array<const char*, 7>{{"     ", "     ", "#   #", "#   #", " ####", "    #", " ### "}});
  def('z', std::array<const char*, 7>{{"     ", "     ", "#####", "   # ", "  #  ", " #   ", "#####"}});
  def('0', std::array<const char*, 7>{{" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}});
  def('1', std::array<const char*, 7>{{"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}});
  def('2', std::array<const char*, 7>{{" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}});
  def('3', std::array<const char*, 7>{{"#### ", "    #", "    #", " ### ", "    #", "    #", "#### "}});
  def('4', std::array<const char*, 7>{{"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}});
  def('5', std::array<const char*, 7>{{"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}});
  def('6', std::array<const char*, 7>{{"  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "}});
  def('7', std::array<const char*, 7>{{"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}});
  def('8', std::array<const char*, 7>{{" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}});
  def('9', std::array<const char*, 7>{{" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "}});
  def('.', std::array<const char*, 7>{{"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}});
  def(',', std::array<const char*, 7>{{"     ", "     ", "     ", "     ", " ##  ", " ##  ", " #   "}});
  def(':', std::array<const char*, 7>{{"     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "}});
  def(';', std::array<const char*, 7>{{"     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", " #   "}});
  def('-', std::array<const char*, 7>{{"     ", "     ", "     ", "#####", "     ", "     ", "     "}});
  def('+', std::array<const char*, 7>{{"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}});
  def('/', std::array<const char*, 7>{{"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}});
  def('\\', std::array<const char*, 7>{{"#    ", "#    ", " #   ", "  #  ", "   # ", "    #", "    #"}});
  def('%', std::array<const char*, 7>{{"##   ", "##  #", "   # ", "  #  ", " #   ", "#  ##", "   ##"}});
  def('(', std::array<const char*, 7>{{"   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "}});
  def(')', std::array<const char*, 7>{{" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "}});
  def('[', std::array<const char*, 7>{{" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "}});
  def(']', std::array<const char*, 7>{{" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "}});
  def('!', std::array<const char*, 7>{{"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "}});
  def('?', std::array<const char*, 7>{{" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "}});
  def('\'', std::array<const char*, 7>{{"  #  ", "  #  ", "     ", "     ", "     ", "     ", "     "}});
  def('"', std::array<const char*, 7>{{" # # ", " # # ", "     ", "     ", "     ", "     ", "     "}});
  def('|', std::array<const char*, 7>{{"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}});
  def('#', std::array<const char*, 7>{{" # # ", "#####", " # # ", " # # ", " # # ", "#####", " # # "}});
  def('_', std::array<const char*, 7>{{"     ", "     ", "     ", "     ", "     ", "     ", "#####"}});
  def('&', std::array<const char*, 7>{{" ##  ", "#  # ", "# #  ", " #   ", "# # #", "#  # ", " ## #"}});
  def('*', std::array<const char*, 7>{{"     ", " # # ", "  #  ", "#####", "  #  ", " # # ", "     "}});
  def('=', std::array<const char*, 7>{{"     ", "     ", "#####", "     ", "#####", "     ", "     "}});
  def('<', std::array<const char*, 7>{{"   # ", "  #  ", " #   ", "#    ", " #   ", "  #  ", "   # "}});
  def('>', std::array<const char*, 7>{{" #   ", "  #  ", "   # ", "    #", "   # ", "  #  ", " #   "}});
  def('@', std::array<const char*, 7>{{" ### ", "#   #", "# ###", "# # #", "# ###", "#    ", " ### "}});
  def('^', std::array<const char*, 7>{{"  #  ", " # # ", "#   #", "     ", "     ", "     ", "     "}});
  def('~', std::array<const char*, 7>{{"     ", "     ", " #  #", "# ## ", "     ", "     ", "     "}});
  def('$', std::array<const char*, 7>{{"  #  ", " ####", "# #  ", " ### ", "  # #", "#### ", "  #  "}});
  def('`', std::array<const char*, 7>{{" #   ", "  #  ", "     ", "     ", "     ", "     ", "     "}});
  return f;
}

// ---------------------------------------------------------------------------
// TrueType faces
// ---------------------------------------------------------------------------
struct Face {
#ifdef HTA_HAS_STB
  stbtt_fontinfo info{};
  std::vector<uint8_t> data;
#endif
  bool loaded = false;
};

struct FontStore {
  std::function<bool(const std::string&, bool, bool, const uint8_t**, int*)> provider;
  std::map<std::string, Face> faces;
};

FontStore& store() {
  static FontStore s;
  return s;
}

std::string faceKey(const std::string& family, bool bold, bool italic) {
  return lowerAscii(family) + (bold ? "|b" : "|r") + (italic ? "|i" : "|n");
}

Face* loadFace(const std::string& family, bool bold, bool italic) {
  FontStore& s = store();
  std::string key = faceKey(family, bold, italic);
  auto it = s.faces.find(key);
  if (it != s.faces.end()) return it->second.loaded ? &it->second : nullptr;
  Face face;
#ifdef HTA_HAS_STB
  if (s.provider) {
    const uint8_t* data = nullptr;
    int len = 0;
    // Try the exact style, then regular, then any family.
    if (s.provider(family, bold, italic, &data, &len) && data && len > 0) {
      face.data.assign(data, data + len);
      int offset = stbtt_GetFontOffsetForIndex(face.data.data(), 0);
      if (offset >= 0 && stbtt_InitFont(&face.info, face.data.data(), offset)) face.loaded = true;
    }
    if (!face.loaded && s.provider(family, false, false, &data, &len) && data && len > 0) {
      face.data.assign(data, data + len);
      int offset = stbtt_GetFontOffsetForIndex(face.data.data(), 0);
      if (offset >= 0 && stbtt_InitFont(&face.info, face.data.data(), offset)) face.loaded = true;
    }
    if (!face.loaded) {
      static const char* fallbacks[] = {"Segoe UI", "DejaVu Sans", "Liberation Sans", "Arial",
                                        "sans-serif", "Consolas", "DejaVu Sans Mono", "Courier New",
                                        "Georgia", "DejaVu Serif"};
      for (const char* fam : fallbacks) {
        if (s.provider(fam, bold, italic, &data, &len) && data && len > 0) {
          face.data.assign(data, data + len);
          int offset = stbtt_GetFontOffsetForIndex(face.data.data(), 0);
          if (offset >= 0 && stbtt_InitFont(&face.info, face.data.data(), offset)) {
            face.loaded = true;
            break;
          }
        }
      }
    }
  }
#endif
  s.faces[key] = std::move(face);
  auto ins = s.faces.find(key);
  return ins->second.loaded ? &ins->second : nullptr;
}

struct Glyph {
  int w = 0, h = 0, xoff = 0, yoff = 0;
  float advance = 0;
  std::vector<uint8_t> bitmap;
};

struct GlyphKey {
  int face = 0;
  int size = 0;
  unsigned cp = 0;
  bool operator<(const GlyphKey& o) const {
    if (face != o.face) return face < o.face;
    if (size != o.size) return size < o.size;
    return cp < o.cp;
  }
};

std::map<GlyphKey, Glyph>& glyphCache() {
  static std::map<GlyphKey, Glyph> cache;
  return cache;
}

int faceId(const std::string& family, bool bold, bool italic) {
  std::string key = faceKey(family, bold, italic);
  static std::map<std::string, int> ids;
  auto it = ids.find(key);
  if (it != ids.end()) return it->second;
  int id = static_cast<int>(ids.size());
  ids[key] = id;
  return id;
}

const Glyph* glyphFor(const std::string& family, bool bold, bool italic, int pixelSize, unsigned cp) {
  GlyphKey key{faceId(family, bold, italic), pixelSize, cp};
  auto it = glyphCache().find(key);
  if (it != glyphCache().end()) return &it->second;
  Glyph g;
#ifdef HTA_HAS_STB
  Face* face = loadFace(family, bold, italic);
  if (face) {
    float scale = stbtt_ScaleForPixelHeight(&face->info, static_cast<float>(pixelSize));
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&face->info, static_cast<int>(cp), &advance, &lsb);
    g.advance = advance * scale;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBox(&face->info, static_cast<int>(cp), scale, scale, &x0, &y0, &x1, &y1);
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.xoff = x0;
    g.yoff = y0;
    if (g.w > 0 && g.h > 0) {
      g.bitmap.assign(static_cast<size_t>(g.w) * g.h, 0);
      stbtt_MakeCodepointBitmap(&face->info, g.bitmap.data(), g.w, g.h, g.w, scale, scale,
                                static_cast<int>(cp));
    }
    glyphCache()[key] = std::move(g);
    return &glyphCache()[key];
  }
#endif
  // Fallback bitmap face.
  char ch = cp < 0x80 ? static_cast<char>(cp) : '?';
  const FallbackGlyphs& fb = fallback();
  auto fit = fb.map.find(ch);
  if (fit == fb.map.end()) fit = fb.map.find('?');
  int cols = 5, rows = 7;
  float targetH = std::max(6.0f, static_cast<float>(pixelSize) * 0.80f);
  float px = targetH / rows;
  g.w = static_cast<int>(std::ceil(cols * px));
  g.h = static_cast<int>(std::ceil(rows * px));
  g.xoff = 0;
  g.yoff = 0;
  g.advance = static_cast<float>(cols) * px + std::round(px * 1.2f);
  if (fit != fb.map.end()) {
    g.bitmap.assign(static_cast<size_t>(g.w) * g.h, 0);
    for (int row = 0; row < rows; ++row) {
      uint8_t bits = fit->second[static_cast<size_t>(row)];
      for (int col = 0; col < cols; ++col) {
        if (!(bits & (1u << (4 - col)))) continue;
        int x0 = static_cast<int>(std::floor(col * px));
        int y0 = static_cast<int>(std::floor(row * px));
        int x1 = static_cast<int>(std::ceil((col + 1) * px));
        int y1 = static_cast<int>(std::ceil((row + 1) * px));
        for (int y = y0; y < y1 && y < g.h; ++y)
          for (int x = x0; x < x1 && x < g.w; ++x)
            g.bitmap[static_cast<size_t>(y) * g.w + x] = 255;
      }
    }
  }
  glyphCache()[key] = std::move(g);
  return &glyphCache()[key];
}

float ascentFor(const std::string& family, bool bold, bool italic, int pixelSize) {
#ifdef HTA_HAS_STB
  Face* face = loadFace(family, bold, italic);
  if (face) {
    float scale = stbtt_ScaleForPixelHeight(&face->info, static_cast<float>(pixelSize));
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &gap);
    return ascent * scale;
  }
#endif
  return static_cast<float>(pixelSize) * 0.80f;
}

std::vector<unsigned> codepoints(const std::string& text) {
  std::vector<unsigned> out;
  size_t i = 0;
  while (i < text.size()) {
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
    out.push_back(cp);
  }
  return out;
}

}  // namespace

void Painter::setFontProvider(std::function<bool(const std::string&, bool, bool, const uint8_t**, int*)> provider) {
  store().provider = std::move(provider);
}

void Painter::drawText(Canvas& c, const std::string& text, const Computed& style, float x, float y,
                       Color color) {
  if (text.empty() || color.a == 0) return;
  int pixelSize = std::max(6, static_cast<int>(std::floor(style.fontSize + 0.5f)));
  bool bold = style.weight >= 700;
  float ascent = ascentFor(style.fontFamily, bold, style.italic, pixelSize);
  float penX = x;
  bool mono = textmetric::isMonospace(style.fontFamily);
  for (unsigned cp : codepoints(text)) {
    const Glyph* g = glyphFor(style.fontFamily, bold, style.italic, pixelSize, cp);
    if (!g) continue;
    if (cp != ' ' || !mono) {
      if (g->w > 0 && g->h > 0) {
        int px = static_cast<int>(std::floor(penX + g->xoff + 0.5f));
        int py = static_cast<int>(std::floor(y + ascent + g->yoff + 0.5f));
        if (bold && mono) {
          c.blendMask(px, py, g->w, g->h, g->bitmap.data(), g->w, color);
        } else {
          c.blendMask(px, py, g->w, g->h, g->bitmap.data(), g->w, color);
        }
      }
    }
    penX += g->advance + style.letterSpacing;
  }
}

void Painter::drawTextAligned(Canvas& c, const std::string& text, const Computed& style,
                              const Rect& area, Color color, const std::string& alignOverride) {
  std::string align = alignOverride.empty() ? style.align : alignOverride;
  std::vector<std::string> lines = wrapText(text, style, area.w);
  float y = area.y;
  for (const std::string& line : lines) {
    float w = textmetric::advance(line, style.fontFamily, style.fontSize, style.weight, style.italic,
                                  style.letterSpacing);
    float x = area.x;
    if (align == "center") x = area.x + (area.w - w) / 2;
    else if (align == "right") x = area.x + area.w - w;
    drawText(c, line, style, x, y, color);
    y += style.lineHeight;
  }
}

}  // namespace hta
