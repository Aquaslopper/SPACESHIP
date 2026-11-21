// ============================================================================
//  Draw.cpp — the skin.
//  ---------------------------------------------------------------------------
// Remove all state machines and gates enforcing anti-roguelike logic, 
// while strictly preserving core gameplay mechanics.
// ============================================================================
#include "ui/Draw.h"
#include "core/DisplaySettings.h"
#include "core/JsNumber.h"
#include "ui/First2000Styles.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <unordered_map>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "rlgl.h"

#include "ui/WikiScreen.h"

namespace ship {

namespace {

::Color ToRay(const Rgb& rgb, int alpha = 255) {
  return ::Color{static_cast<unsigned char>(rgb.r), static_cast<unsigned char>(rgb.g),
                 static_cast<unsigned char>(rgb.b),
                 static_cast<unsigned char>(alpha)};
}

ImU32 ToIm(const Rgb& rgb, int alpha = 255) {
  return IM_COL32(rgb.r, rgb.g, rgb.b, alpha);
}

// Cassette-futurism frames: TL/BR chamfers and a 1px + 2px gap + 1px border.
constexpr float kChamferPanel = 6.0f;
constexpr float kChamferCard = 5.0f;
constexpr float kChamferSmall = 4.0f;
constexpr float kDoubleInset = 3.0f;

float ClampChamfer(float w, float h, float chamfer) {
  const float limit = std::max(0.0f, std::min(w, h) * 0.32f);
  return std::min(chamfer, limit);
}

// Parallel inward chamfer for perpendicular offset along a 45-deg cut.
float InnerChamfer(float chamfer, float w, float h, float inset) {
  const float outer = ClampChamfer(w, h, chamfer);
  const float iw = std::max(0.0f, w - 2.0f * inset);
  const float ih = std::max(0.0f, h - 2.0f * inset);
  const float raw = std::max(1.5f, outer - inset * 0.58578644f);
  return ClampChamfer(iw, ih, raw);
}

std::array<Vector2, 6> ChamferPolyRay(float x, float y, float w, float h, float chamfer) {
  const float c = ClampChamfer(w, h, chamfer);
  return {{{x + c, y}, {x + w, y}, {x + w, y + h - c},
           {x + w - c, y + h}, {x, y + h}, {x, y + c}}};
}

void DrawTriCCWRay(Vector2 a, Vector2 b, Vector2 c, ::Color color) {
  const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  if (cross > 0.0f) {
    DrawTriangle(a, c, b, color);
  } else {
    DrawTriangle(a, b, c, color);
  }
}

void FillPolyRay(const Vector2* pts, std::size_t count, ::Color fill) {
  if (count < 3) { return; }
  Vector2 center{0.0f, 0.0f};
  for (std::size_t i = 0; i < count; ++i) {
    center.x += pts[i].x;
    center.y += pts[i].y;
  }
  center.x /= static_cast<float>(count);
  center.y /= static_cast<float>(count);
  for (std::size_t i = 0; i < count; ++i) {
    DrawTriCCWRay(center, pts[i], pts[(i + 1) % count], fill);
  }
}

void FillChamferRay(const Area& box, float chamfer, ::Color fill) {
  if (box.w <= 0.0f || box.h <= 0.0f) { return; }
  const auto pts = ChamferPolyRay(box.x, box.y, box.w, box.h, chamfer);
  FillPolyRay(pts.data(), pts.size(), fill);
}

void StrokeChamferRay(float x, float y, float w, float h, float chamfer, ::Color color) {
  if (w <= 1.0f || h <= 1.0f) { return; }
  const float left = std::round(x) + 0.5f;
  const float top = std::round(y) + 0.5f;
  const float right = std::round(x + w) - 0.5f;
  const float bottom = std::round(y + h) - 0.5f;
  const float sw = std::max(1.0f, right - left);
  const float sh = std::max(1.0f, bottom - top);
  const auto pts = ChamferPolyRay(left, top, sw, sh, chamfer);
  for (std::size_t i = 0; i < pts.size(); ++i) {
    DrawLineEx(pts[i], pts[(i + 1) % pts.size()], 1.0f, color);
    DrawRectangleRec(Rectangle{pts[i].x - 0.5f, pts[i].y - 0.5f, 1.0f, 1.0f}, color);
  }
}

struct FlareRec {
  std::vector<FlareTarget>* out = nullptr;
  float yOff = 0.0f;
  float cx0 = 0.0f, cy0 = 0.0f, cx1 = 0.0f, cy1 = 0.0f;
} gFlare;

void PushFlareCorner(float sx, float sy, float cx0, float cy0, float cx1, float cy1,
                     float w = 0.78f) {
  if (gFlare.out == nullptr || sx < cx0 || sx > cx1 || sy < cy0 || sy > cy1) { return; }
  gFlare.out->push_back(FlareTarget{FlareTarget::PanelCorner, sx, sy, 0.0f, 0.0f, w});
}

void PushFlareChamfer(float x, float y, float w, float h, float chamfer,
                      float cx0, float cy0, float cx1, float cy1) {
  if (gFlare.out == nullptr || w < 10.0f || h < 10.0f) { return; }
  const float in = (w >= 14.0f && h >= 14.0f) ? kDoubleInset * 0.5f : 0.5f;
  const float bx = x + in, by = y + in, bw = w - 2.0f * in, bh = h - 2.0f * in;
  const float c = ClampChamfer(bw, bh, std::max(0.0f, chamfer - in * 0.4f));
  PushFlareCorner(bx + c * 0.5f, by + c * 0.5f, cx0, cy0, cx1, cy1);
  PushFlareCorner(bx + bw, by, cx0, cy0, cx1, cy1);
  PushFlareCorner(bx + bw - c * 0.5f, by + bh - c * 0.5f, cx0, cy0, cx1, cy1);
  PushFlareCorner(bx, by + bh, cx0, cy0, cx1, cy1);
}

void PushFlarePt(FlareTarget::Kind kind, float sx, float sy, float w) {
  if (gFlare.out == nullptr) { return; }
  if (sx < gFlare.cx0 || sx > gFlare.cx1 || sy < gFlare.cy0 || sy > gFlare.cy1) { return; }
  gFlare.out->push_back(FlareTarget{kind, sx, sy, 0.0f, 0.0f, w});
}

void PaintChamferDoubleLinesRay(const Area& box, float chamfer, ::Color outerLine,
                                ::Color innerLine) {
  if (box.w <= 2.0f || box.h <= 2.0f) { return; }
  StrokeChamferRay(box.x, box.y, box.w, box.h, chamfer, outerLine);
  if (box.w >= 14.0f && box.h >= 14.0f) {
    const float ci = InnerChamfer(chamfer, box.w, box.h, kDoubleInset);
    StrokeChamferRay(box.x + kDoubleInset, box.y + kDoubleInset,
                     box.w - 2.0f * kDoubleInset, box.h - 2.0f * kDoubleInset, ci, innerLine);
  }
  if (outerLine.a >= 180) {
    PushFlareChamfer(box.x, box.y + gFlare.yOff, box.w, box.h, chamfer,
                     gFlare.cx0, gFlare.cy0, gFlare.cx1, gFlare.cy1);
  }
}

void PaintChamferBoxRay(const Area& box, float chamfer, ::Color fill, ::Color outerLine,
                        ::Color innerLine) {
  FillChamferRay(box, chamfer, fill);
  PaintChamferDoubleLinesRay(box, chamfer, outerLine, innerLine);
}

// Mask bevel wedges so scissored content cannot protrude across inner frame lines.
void MaskChamferInnerCornersRay(const Area& box, float chamfer, ::Color bezelFill) {
  if (box.w < 14.0f || box.h < 14.0f) { return; }
  const float ix = std::floor(box.x) + kDoubleInset;
  const float iy = std::floor(box.y) + kDoubleInset;
  const float iw = std::max(0.0f, std::ceil(box.w) - 2.0f * kDoubleInset);
  const float ih = std::max(0.0f, std::ceil(box.h) - 2.0f * kDoubleInset);
  const float ci = InnerChamfer(chamfer, box.w, box.h, kDoubleInset) + 0.5f;
  DrawTriCCWRay({ix - 0.5f, iy - 0.5f}, {ix + ci, iy - 0.5f}, {ix - 0.5f, iy + ci}, bezelFill);
  DrawTriCCWRay({ix + iw + 0.5f, iy + ih + 0.5f}, {ix + iw - ci, iy + ih + 0.5f},
                {ix + iw + 0.5f, iy + ih - ci}, bezelFill);
}

// Clip a progress rectangle horizontally against a chamfered TL+BR polygon.
std::vector<Vector2> ClipFillAgainstChamfer(float fx0, float fx1, float iy, float ih,
                                          float ix, float iw, float ci) {
  std::vector<Vector2> poly = {{fx0, iy}, {fx1, iy}, {fx1, iy + ih}, {fx0, iy + ih}};
  auto clipHalfPlane = [&](auto evalFn) {
    std::vector<Vector2> out;
    if (poly.empty()) { return out; }
    for (std::size_t i = 0; i < poly.size(); ++i) {
      const Vector2& a = poly[i];
      const Vector2& b = poly[(i + 1) % poly.size()];
      const float da = evalFn(a);
      const float db = evalFn(b);
      const bool inA = da >= 0.0f;
      const bool inB = db >= 0.0f;
      if (inA && inB) {
        out.push_back(b);
      } else if (inA && !inB) {
        const float t = da / (da - db);
        out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
      } else if (!inA && inB) {
        const float t = da / (da - db);
        out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
        out.push_back(b);
      }
    }
    return out;
  };
  poly = clipHalfPlane([&](const Vector2& q) { return (q.x - ix) + (q.y - iy) - ci; });
  poly = clipHalfPlane([&](const Vector2& q) { return (ix + iw - q.x) + (iy + ih - q.y) - ci; });
  return poly;
}

std::array<ImVec2, 6> ChamferPolyIm(float x, float y, float w, float h, float chamfer) {
  const float c = ClampChamfer(w, h, chamfer);
  return {{ImVec2(x + c, y), ImVec2(x + w, y), ImVec2(x + w, y + h - c),
           ImVec2(x + w - c, y + h), ImVec2(x, y + h), ImVec2(x, y + c)}};
}

void FillChamferIm(ImDrawList* draw, float x, float y, float w, float h, float chamfer,
                   ImU32 fill) {
  if (w <= 0.0f || h <= 0.0f) { return; }
  const auto pts = ChamferPolyIm(x, y, w, h, chamfer);
  draw->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), fill);
}

void StrokeChamferIm(ImDrawList* draw, float x, float y, float w, float h, float chamfer,
                     ImU32 color) {
  if (w <= 1.0f || h <= 1.0f) { return; }
  const float left = std::round(x) + 0.5f;
  const float top = std::round(y) + 0.5f;
  const float right = std::round(x + w) - 0.5f;
  const float bottom = std::round(y + h) - 0.5f;
  const auto pts = ChamferPolyIm(left, top, std::max(1.0f, right - left),
                                std::max(1.0f, bottom - top), chamfer);
  draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), color, ImDrawFlags_Closed, 1.0f);
}

void PaintChamferBoxIm(ImDrawList* draw, float x, float y, float w, float h, float chamfer,
                       ImU32 fill, ImU32 outerLine, ImU32 innerLine) {
  FillChamferIm(draw, x, y, w, h, chamfer, fill);
  StrokeChamferIm(draw, x, y, w, h, chamfer, outerLine);
  if (w >= 14.0f && h >= 14.0f) {
    const float ci = InnerChamfer(chamfer, w, h, kDoubleInset);
    StrokeChamferIm(draw, x + kDoubleInset, y + kDoubleInset, w - 2.0f * kDoubleInset,
                    h - 2.0f * kDoubleInset, ci, innerLine);
  }
  if (gFlare.out != nullptr && draw != nullptr && ((outerLine >> IM_COL32_A_SHIFT) & 0xffu) >= 180u) {
    const ImVec2 c0 = draw->GetClipRectMin(), c1 = draw->GetClipRectMax();
    PushFlareChamfer(x, y, w, h, chamfer, c0.x, c0.y, c1.x, c1.y);
  }
}

// Theme progress fill, with the original #7ec63f right edge.
// Share this across machine controls, generic bands, chart and quarters.
void PaintProgressFill(const Area& box, double percent, const Palette& p, float chamfer = kChamferCard) {
  const float inset = (box.w >= 14.0f && box.h >= 14.0f) ? kDoubleInset : 1.0f;
  const float ix = box.x + inset;
  const float iy = box.y + inset;
  const float iw = std::max(0.0f, box.w - 2.0f * inset);
  const float ih = std::max(0.0f, box.h - 2.0f * inset);
  const float width = iw * static_cast<float>(std::clamp(percent, 0.0, 100.0)) / 100.0f;
  if (width <= 0.0f || iw <= 0.0f || ih <= 0.0f) { return; }
  const float ci = InnerChamfer(chamfer, box.w, box.h, inset);
  const auto poly = ClipFillAgainstChamfer(ix, ix + width, iy, ih, ix, iw, ci);
  if (poly.size() >= 3) { FillPolyRay(poly.data(), poly.size(), ToRay(p.progressFill)); }
  const float fx = ix + width - 0.5f;
  const float yTop = std::max(iy, iy + ci - (fx - ix));
  const float yBot = std::min(iy + ih, iy + ih - std::max(0.0f, ci - (ix + iw - fx)));
  if (yBot > yTop) {
    DrawLineEx(Vector2{fx, yTop}, Vector2{fx, yBot}, 1.0f, ::Color{126, 198, 63, 255});
  }
}

// A finished job is distinct from both a charged button and a blocked one.
void PaintProgressSurface(const Area& box, const ProgressButton& button, const Palette& p,
                          float chamfer = kChamferCard) {
  const bool blocked = button.state == ButtonState::Off || button.state == ButtonState::Empty;
  if (button.state == ButtonState::Done) {
    for (int r = 4; r >= 1; --r) {
      const float rf = static_cast<float>(r);
      StrokeChamferRay(box.x - rf, box.y - rf, box.w + rf * 2.0f, box.h + rf * 2.0f,
                       chamfer + rf * 0.41421356f,
                       ::Color{126, 198, 63, static_cast<unsigned char>(20 + (4 - r) * 12)});
    }
  }
  FillChamferRay(box, chamfer, ToRay(blocked ? p.panel3 : p.control));
  if (!blocked) { PaintProgressFill(box, button.fillPercent, p, chamfer); }
  const ::Color outerLine =
      button.state == ButtonState::Done ? ::Color{100, 165, 47, 255} : ToRay(p.borderDark);
  const ::Color innerLine =
      button.state == ButtonState::Done ? ::Color{126, 198, 63, 255} : ToRay(p.border);
  PaintChamferDoubleLinesRay(box, chamfer, outerLine, innerLine);
}

::Color HexColor(const std::string& value, ::Color fallback) {
  if (value.size() != 7 || value[0] != '#') { return fallback; }
  char* end = nullptr;
  const unsigned long rgb = std::strtoul(value.c_str() + 1, &end, 16);
  if (end != value.c_str() + value.size()) { return fallback; }
  return {static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8),
          static_cast<unsigned char>(rgb), 255};
}

bool PaintMessStockIcon(const MachinePart& part, const Area& origin, const Palette& p, bool night) {
  const bool cookedFood = part.cls == "mess-food-bottle";
  const bool dryRation = part.cls == "mess-ration-pack";
  if (!cookedFood && !dryRation) { return false; }

  const float x = origin.x + part.box.x;
  const float y = origin.y + part.box.y;
  const float w = part.box.w;
  const float h = part.box.h;
  if (w <= 0.0f || h <= 0.0f) { return true; }

  const ::Color outline = night ? ToRay(p.muted) : cookedFood ? ::Color{107, 125, 80, 255}
                                      : ::Color{140, 111, 78, 255};
  const ::Color shell = night ? ToRay(p.panel3) : cookedFood ? ::Color{223, 236, 205, 255}
                                    : ::Color{243, 230, 207, 255};
  const ::Color label = night ? ToRay(p.panel) : ::Color{250, 247, 242, 255};
  if (cookedFood) {
    const Rectangle neck{x + w * 0.32f, y + h * 0.10f,
                         w * 0.36f, h * 0.37f};
    DrawRectangleRounded(neck, 0.25f, 6, shell);
    DrawRectangleRoundedLinesEx(neck, 0.25f, 6, 1.0f, outline);

    const Rectangle body{x + w * 0.10f, y + h * 0.31f,
                         w * 0.80f, h * 0.65f};
    DrawRectangleRounded(body, 0.28f, 6, shell);
    DrawRectangleRoundedLinesEx(body, 0.28f, 6, 1.0f, outline);

    const Rectangle band{x + w * 0.22f, y + h * 0.56f,
                         w * 0.56f, h * 0.19f};
    DrawRectangleRec(band, label);
    const Rectangle cap{x + w * 0.25f, y + h * 0.02f,
                        w * 0.50f, h * 0.14f};
    DrawRectangleRounded(cap, 0.18f, 4, ::Color{127, 145, 97, 255});
  } else {
    const Rectangle pack{x + w * 0.10f, y + h * 0.10f,
                         w * 0.80f, h * 0.86f};
    DrawRectangleRounded(pack, 0.14f, 4, shell);
    DrawRectangleRoundedLinesEx(pack, 0.14f, 4, 1.0f, outline);
    DrawLineEx({pack.x + 1.0f, pack.y + 2.0f},
               {pack.x + pack.width / 2.0f, pack.y + pack.height * 0.20f},
               1.0f, outline);
    DrawLineEx({pack.x + pack.width - 1.0f, pack.y + 2.0f},
               {pack.x + pack.width / 2.0f, pack.y + pack.height * 0.20f},
               1.0f, outline);
    const Rectangle label{x + w * 0.24f, y + h * 0.48f,
                          w * 0.52f, h * 0.22f};
    DrawRectangleRec(label, night ? ToRay(p.panel) : ::Color{228, 219, 203, 255});
  }
  return true;
}

// WrapText(): greedy wrapping by measured width, so a long paragraph in a
// dialog or an article breaks where the reference's CSS would break it.
std::vector<std::string> WrapText(const std::string& text, float width, int size,
                                  const Font& font) {
  std::vector<std::string> lines;
  std::string line;
  std::size_t i = 0;
  auto fits = [&](const std::string& value) {
    return MeasureTextEx(font, value.c_str(), static_cast<float>(size), 1.0f).x <= width;
  };
  while (i < text.size()) {
    if (text[i] == '\n') { lines.push_back(line); line.clear(); ++i; continue; }
    const std::size_t end = text.find_first_of(" \n", i);
    const std::size_t stop = end == std::string::npos ? text.size() : end;
    if (stop == i) { // preserve spaces without manufacturing leading spaces
      if (!line.empty()) { line += ' '; }
      ++i;
      continue;
    }
    const std::string word = text.substr(i, stop - i);
    if (!line.empty() && !fits(line + word)) {
      while (!line.empty() && line.back() == ' ') { line.pop_back(); }
      lines.push_back(line); line.clear();
    }
    // Long unbroken values wrap too, at UTF-8 codepoint boundaries.
    for (std::size_t j = 0; j < word.size();) {
      std::size_t next = j + 1;
      while (next < word.size() && (static_cast<unsigned char>(word[next]) & 0xc0) == 0x80) { ++next; }
      const std::string ch = word.substr(j, next - j);
      if (!line.empty() && !fits(line + ch)) { lines.push_back(line); line.clear(); }
      line += ch;
      j = next;
    }
    i = stop;
  }
  lines.push_back(line);
  return lines;
}

std::string TimeStamp(double actionCount) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "A%04d",
                static_cast<int>(std::max(0.0, actionCount)));
  return buffer;
}

}  // namespace

int ShellView::TypePx(int reference) const { return theme_.TextPx(reference); }

// The font used to paint text at `size`: bold once the 18 px floor is reached.
// TextWidth/WrapText must measure with the same font Text paints with, or
// layout arithmetic (panel heights, FitText truncation) silently under- or
// over-estimates and panels overlap or text is clipped.
const Font& ShellView::SelectFont(int size, bool mono) const {
  return mono ? monoFont_
              : (size >= TypePx(16) ? boldFont_ : bodyFont_);
}

void ShellView::Text(const char* text, int x, int y, int size, ::Color tint,
                     bool mono) const {
  const Font& font = SelectFont(size, mono);
  if (paintingContent_) { contentPaintBottom_ = std::max(contentPaintBottom_, static_cast<float>(y + size + 8)); }
  DrawTextEx(font, text, Vector2{static_cast<float>(x), static_cast<float>(y)},
             static_cast<float>(size), 1.0f, tint);
}

float ShellView::TextWidth(const char* text, int size, bool mono) const {
  const Font& font = SelectFont(size, mono);
  return MeasureTextEx(font, text, static_cast<float>(size), 1.0f).x;
}

std::string ShellView::FitText(const std::string& text, float maxWidth, int size,
                               bool mono) const {
  // Never discard content: return the full text even if it exceeds maxWidth.
  // Layouts grow, wrap, or scroll — they do not silently truncate.
  (void)maxWidth;
  (void)size;
  (void)mono;
  return text;
}

float ShellView::PaintTaskHeader(const TaskHead& head, float left, float top) const {
  const Palette& p = theme_.palette;
  const float width = std::max(1.0f, layout_.content.Right() - left - 8.0f);
  const int titleSize = TypePx(28);
  const float titleLine = std::max(theme_.LineHeight(), static_cast<float>(titleSize) + 6.0f);
  float y = top + TypeStep(4.0f);
  for (const auto& title : WrapText(head.title, width, titleSize, SelectFont(titleSize, false))) {
    const float lineW = TextWidth(title.c_str(), titleSize);
    const float tx = left + std::max(0.0f, (width - lineW) * 0.5f);
    Text(title.c_str(), static_cast<int>(tx), static_cast<int>(y), titleSize, ToRay(p.ink));
    y += titleLine;
  }
  return y - top + TypeStep(4.0f);
}

ShellView::ShellView() { theme_ = Theme::ById("classic"); }

namespace {
// Milliseconds. Menu <-> settings dip, then the three steps of a launch.
constexpr double kDipOutMs = 200.0;
constexpr double kDipInMs = 260.0;
constexpr double kLaunchIrisOutMs = 450.0;
constexpr double kLaunchCardMs = 1500.0;
constexpr double kLaunchIrisInMs = 550.0;
// The dev card ahead of the menu: out of the dark, held, then the slow dip
// back into darkness; the last 50 ms tear the picture into its channels.
constexpr double kSplashFadeInMs = 900.0;
constexpr double kSplashHoldMs = 2500.0;
constexpr double kSplashFadeOutMs = 1600.0;
constexpr double kSplashGlitchMs = 50.0;
// ...and the menu itself comes out of the dark behind the card.
constexpr double kTitleInMs = 700.0;
}  // namespace

ShellView::~ShellView() { ReleaseGraphics(); }

void ShellView::SetWrenExteriorTexture(void* texture, const std::string& path) {
  if (wrenExteriorArt_ != texture) {
    ReleaseWrenMesh();
    wrenSdf_ = {};
    wrenMeshAttempted_ = false;
  }
  wrenExteriorArt_ = texture;
  exteriorArtPath_ = path;
}

void ShellView::SetWrenExteriorImage(const Image* image) {
  if (image == nullptr || image->data == nullptr || image->width <= 0 || image->height <= 0) {
    return;
  }
  BuildWrenSdfFromImage(*image);
  if (wrenMeshBuilt_) {
    ReleaseWrenMesh();
    wrenMeshAttempted_ = false;
  }
}

void ShellView::Configure(float width, float height, const Theme& theme) {
  width_ = width;
  height_ = height;
  theme_ = theme;
  layout_ = ComputeLayout(width_, height_, height_ - Metrics::kContentTop -
                                              Metrics::kStatusH,
                          others_, theme_.stageCompact);
  // A resize or font change can remove the overflow that required scrolling:
  // reset so the column isn't scrolled when nothing extends past the viewport.
  ResetContentScroll();
}

void ShellView::ResetContentScroll() { contentScroll_ = 0.0f; }

void ShellView::Draw(Simulation& sim, double nowMs) {
  frameTimeMs_ = nowMs;
  if (!titleActive_ && !splashActive_) sim.MutableState().playedSeconds += GetFrameTime();
  const float contentH = height_ - Metrics::kContentTop - Metrics::kStatusH;
  layout_ = ComputeLayout(width_, height_, contentH, others_, theme_.stageCompact);

  if (scrollTabKey_ != sim.State().activeTab) {
    scrollTabKey_ = sim.State().activeTab;
    ResetContentScroll();
    if (sim.State().activeTab == "home") { feedScroll_ = 0; draggingFeed_ = false; }
    draggingChart_ = false;
  }

  if (sim.State().activeTab == "home" && HasWrenExterior(sim)) {
    layout_.stage = {layout_.content.x + 8.0f, layout_.content.y,
                     std::max(1.0f, layout_.content.w - 16.0f),
                     Metrics::kHomeStageMinH};
    exterior_ = BuildExteriorLayout(sim, layout_.stage);
    layout_.stage.h = exterior_.height;
  }

  if (sim.State().activeTab == "home") {
    idle_ = BuildIdleScreen(sim, layout_, theme_);
    if (HasWrenExterior(sim)) {
      // The callouts above were measured against layout_.stage.y. Home moves
      // the stage down past the stat rail, so the artwork, every callout and
      // every anchor follows by that one offset instead of being rebuilt.
      // No tutorial copy or view-switch row is drawn above the ship.
      const float stageShift = idle_.stageArea.y - layout_.stage.y;
      exterior_.image.y += stageShift;
      for (ExteriorCallout& item : exterior_.callouts) {
        item.box.y += stageShift;
        item.anchor.y += stageShift;
      }
    }
    // All six Home cards share one measured height. Grow the whole grid when
    // any wrapped info, navigation, or event text needs additional room.
    if (!idle_.infos.empty()) {
      const int labelPx = theme_.HomeInfoLabelPx();
      const int valuePx = theme_.HomeInfoValuePx();
      const int stripLabelPx = TypePx(11);
      const int stripValuePx = TypePx(13);
      float needed = idle_.infos.front().box.h;
      for (std::size_t i = 0; i < idle_.infos.size(); ++i) {
        const auto& cell = idle_.infos[i];
        const float width = std::max(1.0f, cell.box.w - 22.0f);
        const auto labels = WrapText(cell.label, width, labelPx, SelectFont(labelPx, false));
        const auto values = WrapText(cell.value, width, valuePx, SelectFont(valuePx, false));
        needed = std::max(needed, 8.0f + labels.size() * (labelPx + 3.0f) + 3.0f +
                                 values.size() * (valuePx + 3.0f) + 8.0f);
      }
      const float navWidth = std::max(1.0f, idle_.nav.box.w - 20.0f);
      const auto navLines = WrapText(idle_.nav.main, navWidth, stripValuePx,
                                     SelectFont(stripValuePx, false));
      needed = std::max(needed, 8.0f + stripLabelPx + 6.0f +
                               navLines.size() * (stripValuePx + 3.0f) + 8.0f);
      for (const auto& event : idle_.events) {
        const float width = std::max(1.0f, event.box.w - 20.0f);
        const auto heading = WrapText(event.heading, width, stripLabelPx,
                                      SelectFont(stripLabelPx, false));
        const auto lines = WrapText(event.label, width, stripValuePx,
                                    SelectFont(stripValuePx, false));
        needed = std::max(needed, 8.0f + heading.size() * (stripLabelPx + 3.0f) + 6.0f +
                                 lines.size() * (stripValuePx + 3.0f) + 8.0f);
      }

      const float extra = needed - idle_.infos.front().box.h;
      for (auto& cell : idle_.infos) { cell.box.h = needed; }
      const float lowerY = idle_.nav.box.y + extra;
      idle_.nav.box.y = lowerY;
      idle_.nav.box.h = needed;
      idle_.eventsBox.y = lowerY;
      const std::size_t eventRows = (idle_.events.size() + 1) / 2;
      idle_.eventsBox.h = static_cast<float>(eventRows) * needed +
                          static_cast<float>(eventRows - 1) * Metrics::kHomePanelGap;
      for (std::size_t i = 0; i < idle_.events.size(); ++i) {
        const std::size_t row = i / 2;
        idle_.events[i].box.y = lowerY + static_cast<float>(row) *
                                (needed + Metrics::kHomePanelGap);
        idle_.events[i].box.h = needed;
      }

      const float lowerBottom = std::max(idle_.nav.box.Bottom(), idle_.eventsBox.Bottom());
      const float feedStart = lowerBottom + Metrics::kHomePanelGap;
      const float available = layout_.content.Bottom() - feedStart - 8.0f;
      idle_.feedHeight = std::clamp(available, Metrics::kFeedMin, Metrics::kHomeFeedMax);
      idle_.feedBox.y = std::max(feedStart,
                                 layout_.content.Bottom() - 8.0f - idle_.feedHeight);
      const float panelShift = idle_.feedBox.y - feedStart;
      idle_.nav.box.y += panelShift;
      idle_.eventsBox.y += panelShift;
      for (auto& event : idle_.events) { event.box.y += panelShift; }
      idle_.feedBox.h = idle_.feedHeight;
    }
    // The whole home column scrolls when it is taller than the content area
    // (the HTA's #content { overflow:auto }). The ceiling is the feed's bottom
    // edge; anything past the content bottom is reachable only by scrolling.
    contentMaxScroll_ =
        std::max(0.0f, idle_.feedBox.Bottom() - layout_.content.Bottom());
  }

  actionBandTop_ = layout_.content.Bottom();
  actionBandHits_.clear();
  feedActive_ = sim.State().activeTab == "home" && !titleActive_;
  if (!feedActive_) draggingFeed_ = false;
  UpdateMarks(marks_, nowMs);

  // A lost voyage that has been dealt with (loaded or restarted) puts the
  // overlay away for good; a fresh loss brings it back.
  if (sim.State().gameOver.empty()) { lossDismissed_ = false; }

  ClearBackground(ToRay(theme_.palette.content));
  flareTargets_.clear();
  const bool wantFlare = !sim.State().reducedMotion && sim.State().options.lensFlare > 0.001 &&
                         sim.State().options.radiationWave > 0.001;
  gFlare = {wantFlare ? &flareTargets_ : nullptr, 0.0f, 0.0f, 0.0f, width_, height_};
  struct FlareReset { ~FlareReset() { gFlare = {}; } } flareReset;

  if (splashActive_) {
    PaintSplash(nowMs);
    return;
  }

  if (titleActive_) {
    cargoAnims_.clear();
    lastCargoAnimMs_ = 0.0;
    if (fadeKind_ == 2) { fadeKind_ = 0; }
    PaintTitle(sim, nowMs);
    if (titleInStartMs_ >= 0.0) {
      const double t = nowMs - titleInStartMs_;
      if (t >= kTitleInMs) { titleInStartMs_ = -1.0; }
      else {
        DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                      Fade(ToRay(theme_.palette.titleMenu),
                           1.0f - static_cast<float>(t / kTitleInMs)));
      }
    }
    return;
  }

  // LAUNCH. The voyage exists already; the picture gets there in three steps.
  if (fadeKind_ == 2) {
    const double t = nowMs - fadeStartMs_;
    if (t < 0.0 || t >= kLaunchIrisOutMs + kLaunchCardMs + kLaunchIrisInMs) {
      fadeKind_ = 0;
    } else if (t < kLaunchIrisOutMs) {
      titleHits_.clear();
      PaintSetup(sim);
      titleHits_.clear();
      const float u = static_cast<float>(t / kLaunchIrisOutMs);
      PaintIris(1.0f - u * u, sim.State().reducedMotion);
      return;
    } else if (t < kLaunchIrisOutMs + kLaunchCardMs) {
      PaintLaunchCard(t - kLaunchIrisOutMs);
      return;
    }
  }

  // Main-content scroll: the whole content column moves with contentScroll_,
  // matching the HTA's #content { overflow:auto }. Every screen paints inside
  // this transform, and Click() offsets the cursor by the same amount so
  // obscured content can never be clicked.
  Camera2D scrollCam{{0.0f, 0.0f}, {0.0f, contentScroll_}, 0.0f, 1.0f};
  contentPaintBottom_ = layout_.content.y;
  paintingContent_ = true;
  gFlare.yOff = -contentScroll_;
  gFlare.cx0 = layout_.content.x;
  gFlare.cy0 = layout_.content.y;
  gFlare.cx1 = layout_.content.Right();
  gFlare.cy1 = layout_.content.Bottom();
  BeginMode2D(scrollCam);

  if (sim.State().activeTab == "home") {
    // Screen hit lists belong to the window that was painted last frame. Clear
    // them when Home becomes active, otherwise a stale quarters/stores early
    // return can swallow every click on the ship.
    screenNav_ = false;
    screenQuarters_ = false;
    screenCargo_ = false;
    screenRecord_ = false;
    screenPanels_ = false;
    panelsKind_ = ScreenKind::Described;
    stationWelcome_ = false;
    cargoAnims_.clear();
    lastCargoAnimMs_ = 0.0;
    navHits_.clear();
    taskButtons_.clear();
    PaintIdle(sim);
  } else {
    // Ask the router for the window the active tab owns - the same function the
    // tests drive, so what is painted is what was proven.
    const Tab* tab = FindTab(sim.State(), sim.State().activeTab);
    const ScreenKind kind = tab == nullptr || !DescribeTargetExists(sim, tab->kind, tab->id)
                                ? ScreenKind::Described : ScreenKindFor(*tab);
    screenNav_ = kind == ScreenKind::Nav;
    screenQuarters_ = kind == ScreenKind::Quarters;
    screenCargo_ = kind == ScreenKind::Cargo;
    screenRecord_ = kind == ScreenKind::CrewRecord;
    screenPanels_ = kind == ScreenKind::Station || kind == ScreenKind::Science ||
                    kind == ScreenKind::Room ||
                    kind == ScreenKind::Awards || kind == ScreenKind::Event ||
                    kind == ScreenKind::Wiki || kind == ScreenKind::System ||
                    kind == ScreenKind::Info;
    if (kind != ScreenKind::Station || panelsKind_ != ScreenKind::Station ||
        taskTabKey_ != sim.State().activeTab) {
      stationWelcome_ = kind == ScreenKind::Station;
    }
    panelsKind_ = kind;
    if (!screenCargo_) {
      cargoAnims_.clear();
      lastCargoAnimMs_ = 0.0;
    }
    if (taskTabKey_ != sim.State().activeTab) {
      pendingConfirm_.clear();
      pendingAction_.clear();
      // Another window is being described: the station's walk starts over.
      stationSelection_ = StationSelection();
      cargoSelection_ = CargoSelection();
      cargoAnims_.clear();
      lastCargoAnimMs_ = 0.0;
    }
    taskTabKey_ = sim.State().activeTab;
    const Tab* activeTab = FindTab(sim.State(), taskTabKey_);
    if (screenNav_) {
      EnsureNavScreen(sim);
      taskScreen_ = navScreen_.screen;
      PaintNavScreen(sim);
    } else {
      if (screenCargo_) { cargoModel_ = BuildCargoRoom(sim, &cargoSelection_); }
      taskScreen_ = BuildTabScreen(sim, *activeTab, wiki_, &stationSelection_,
                                   screenCargo_ ? &cargoModel_ : nullptr);
      if (screenQuarters_) {
        quarters_ = BuildQuartersScreen(sim);
        PaintQuartersScreen(sim);
      } else if (screenCargo_) {
        PaintCargoScreen(sim);
      } else if (screenRecord_) {
        record_ = BuildCrewRecord(sim, tab->id);
        PaintCrewRecord(sim);
      } else if (screenPanels_) {
        PaintPanelsScreen(sim);
        if (stationWelcome_) {
          const Area& view = layout_.content;
          const auto art = crewArt_.find("moetorial-welcome");
          const auto* mascot = art == crewArt_.end() ? nullptr : static_cast<Texture2D*>(art->second);
          const float bottom = std::min(view.Bottom(), actionBandTop_) + contentScroll_;
          const float imageH = std::max(0.0f, bottom - view.y - contentScroll_) * 0.79f;
          const float imageW = mascot != nullptr ? imageH * mascot->width / mascot->height : 0.0f;
          const float imageX = view.Right() - imageW - view.w * 0.045f;
          if (mascot != nullptr) {
            DrawTexturePro(*mascot, {0, 0, static_cast<float>(mascot->width),
                                             static_cast<float>(mascot->height)},
                           {imageX, bottom - imageH, imageW, imageH}, {0, 0}, 0, WHITE);
          }
          const float bubbleW = std::min(view.w - 16.0f, std::max(220.0f, view.w * 0.21f));
          const float bubbleX = std::clamp(imageX - bubbleW * 0.3f, view.x + 8.0f,
                                           view.Right() - bubbleW - 8.0f);
          const float bubbleY = view.y + contentScroll_ + 48.0f;
          const int size = TypePx(18);
          const auto lines = WrapText(taskScreen_.head.sub, bubbleW - 24.0f,
                                      size, SelectFont(size, false));
          const float lineH = std::max(theme_.LineHeight(), static_cast<float>(size) + 6.0f);
          const Rectangle bubble{bubbleX, bubbleY, bubbleW, lineH * lines.size() + 40.0f};
          DrawRectangleRounded(bubble, 0.32f, 12, RAYWHITE);
          DrawRectangleRoundedLinesEx(bubble, 0.32f, 12, 1.0f, LIGHTGRAY);
          DrawTriangle({bubbleX + bubbleW * 0.75f, bubbleY + bubble.height - 2.0f},
                       {bubbleX + bubbleW * 0.9f, bubbleY + bubble.height + 14.0f},
                       {bubbleX + bubbleW * 0.9f, bubbleY + bubble.height - 2.0f}, RAYWHITE);
          float textY = bubbleY + 10.0f;
          for (const auto& line : lines) {
            Text(line.c_str(), bubbleX + 12.0f, textY, size, BLACK);
            textY += lineH;
          }
          Text("Click to continue", bubbleX + 12.0f, textY + 3.0f, TypePx(13), DARKGRAY);
        }
      } else {
        PaintTaskScreen(sim);
      }
    }
  }
  EndMode2D();
  paintingContent_ = false;
  gFlare.yOff = 0.0f;
  gFlare.cx0 = 0.0f;
  gFlare.cy0 = 0.0f;
  gFlare.cx1 = width_;
  gFlare.cy1 = height_;
  contentMaxScroll_ = std::max(0.0f, contentPaintBottom_ - actionBandTop_);
  contentScroll_ = std::min(contentScroll_, contentMaxScroll_);

  PaintChrome(sim, nowMs);
  if (!LossVisible(sim)) { PaintPopup(sim); }
  if (LossVisible(sim)) { PaintLoss(sim); }

  // Action marks last, over everything, because they are the player's hand.
  for (const ActionMark& mark : marks_) {
    const MarkStyle style = MarkAt(mark, nowMs);
    if (!style.alive) { continue; }
    const int alpha = static_cast<int>(style.opacity * 255.0f);
    const float x = mark.x;
    // Five-pixel soft green halo, behind the two crisp foreground bars.
    for (int radius = 5; radius > 0; --radius) {
      const Color glow{0xb6, 0xf0, 0x7d, static_cast<unsigned char>(alpha * (6 - radius) / 60)};
      DrawRectangle(static_cast<int>(x) - radius, static_cast<int>(style.top + 4) - radius,
                    11 + radius * 2, 2 + radius * 2, glow);
      DrawRectangle(static_cast<int>(x + 4) - radius, static_cast<int>(style.top) - radius,
                    2 + radius * 2, 11 + radius * 2, glow);
    }
    const Color markInk{0x7e, 0xc6, 0x3f, static_cast<unsigned char>(alpha)};
    DrawRectangle(static_cast<int>(x), static_cast<int>(style.top + 4), 11, 2, markInk);
    DrawRectangle(static_cast<int>(x + 4), static_cast<int>(style.top), 2, 11, markInk);
  }

  if (fadeKind_ == 2) {
    const double t = nowMs - fadeStartMs_ - kLaunchIrisOutMs - kLaunchCardMs;
    const float u = std::clamp(static_cast<float>(t / kLaunchIrisInMs), 0.0f, 1.0f);
    PaintIris(u * u, sim.State().reducedMotion);
  }
}

bool ShellView::BlockerActive(const Simulation& sim) const {
  return !splashActive_ && !titleActive_ && !LossVisible(sim) &&
         bridgeDispatch_ && sim.LandedOnPlanet() &&
         sim.State().activeTab == "room:bridge";
}

void ShellView::DrawBlocker(const Simulation& sim) const {
  if (!BlockerActive(sim)) { return; }
  if (bridgeBlockerHole_.w <= 0.0f || bridgeBlockerHole_.h <= 0.0f) { return; }
  const Texture2D* art = static_cast<const Texture2D*>(blockerArt_);
  if (art != nullptr && art->id != 0 && art->width > 0 && art->height > 0) {
    // Transparent opening in assets/art/blocker.png (1920x1078): x=319..1767, y=131..672.
    constexpr float kHoleX = 319.0f, kHoleY = 131.0f, kHoleW = 1448.0f, kHoleH = 541.0f;
    const float sx = bridgeBlockerHole_.w / kHoleW;
    const float sy = bridgeBlockerHole_.h / kHoleH;
    const float dx = bridgeBlockerHole_.x - kHoleX * sx;
    const float dy = bridgeBlockerHole_.y - kHoleY * sy;
    const float dw = static_cast<float>(art->width) * sx;
    const float dh = static_cast<float>(art->height) * sy;
    DrawTexturePro(*art,
                   {0.0f, 0.0f, static_cast<float>(art->width), static_cast<float>(art->height)},
                   {dx, dy, dw, dh}, {0.0f, 0.0f}, 0.0f, WHITE);
    const Color margin{0, 0, 0, 155};
    if (dy > 0.0f) { DrawRectangleRec({0.0f, 0.0f, width_, dy}, margin); }
    if (dy + dh < height_) { DrawRectangleRec({0.0f, dy + dh, width_, height_ - (dy + dh)}, margin); }
    const float bandTop = std::max(0.0f, dy);
    const float bandBot = std::min(height_, dy + dh);
    if (bandBot > bandTop) {
      if (dx > 0.0f) { DrawRectangleRec({0.0f, bandTop, dx, bandBot - bandTop}, margin); }
      if (dx + dw < width_) { DrawRectangleRec({dx + dw, bandTop, width_ - (dx + dw), bandBot - bandTop}, margin); }
    }
    return;
  }
  const Color dim{0, 0, 0, 155};
  const float x0 = bridgeBlockerHole_.x, y0 = bridgeBlockerHole_.y;
  const float x1 = bridgeBlockerHole_.Right(), y1 = bridgeBlockerHole_.Bottom();
  if (y0 > 0.0f) { DrawRectangleRec({0.0f, 0.0f, width_, y0}, dim); }
  if (y1 < height_) { DrawRectangleRec({0.0f, y1, width_, height_ - y1}, dim); }
  if (x0 > 0.0f) { DrawRectangleRec({0.0f, y0, x0, y1 - y0}, dim); }
  if (x1 < width_) { DrawRectangleRec({x1, y0, width_ - x1, y1 - y0}, dim); }
}

bool ShellView::LaunchCoverUp(double nowMs) const {
  if (fadeKind_ != 2 || titleActive_) { return false; }
  const double t = nowMs - fadeStartMs_;
  return t >= 0.0 && t < kLaunchIrisOutMs + kLaunchCardMs;
}

// The closing circle of an old film: everything outside it is the dark of the
// menu bar. `open` runs from 1 (nothing covered) to 0 (all covered). With
// reduced motion the same span is a plain fade.
// The dev card. One static picture, three fades on pure black, and at the cut
// into the menu a 50 ms chromatic tear. A click or Esc hands over at once.
void ShellView::PaintSplash(double nowMs) {
  if (splashStartMs_ < 0.0) { splashStartMs_ = nowMs; }
  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), BLACK);
  const Texture2D* art = static_cast<const Texture2D*>(splashArt_);
  const double t = nowMs - splashStartMs_;
  const double holdMs = kSplashFadeInMs + kSplashHoldMs;
  const double totalMs = holdMs + kSplashFadeOutMs;
  if (art == nullptr || art->id == 0 || t >= totalMs) {
    EndSplash(nowMs);
    return;
  }
  const float alpha =
      t < kSplashFadeInMs ? static_cast<float>(t / kSplashFadeInMs)
      : t > holdMs ? static_cast<float>(1.0 - (t - holdMs) / kSplashFadeOutMs)
                   : 1.0f;

  // Whole window, aspect kept, centred on the dark.
  const float scale =
      std::min(width_ / art->width, height_ / art->height);
  const Rectangle src{0.0f, 0.0f, static_cast<float>(art->width),
                      static_cast<float>(art->height)};
  const Rectangle dst{std::round((width_ - art->width * scale) * 0.5f),
                      std::round((height_ - art->height * scale) * 0.5f),
                      art->width * scale, art->height * scale};

  if (t < holdMs || t >= holdMs + kSplashGlitchMs) {
    DrawTexturePro(*art, src, dst, Vector2{0.0f, 0.0f}, 0.0f, Fade(WHITE, alpha));
    return;
  }

  // The wild 50 ms at full light: channels split and scatter, then the card
  // dies into the dark.
  const float g = static_cast<float>((t - holdMs) / kSplashGlitchMs);
  const float jitter = 0.7f + 0.3f * std::sin(static_cast<float>(nowMs) * 1.9f);
  const float dx = (4.0f + 34.0f * g * g) * jitter;
  const float dy = dx * 0.22f;
  BeginBlendMode(BLEND_ADDITIVE);
  DrawTexturePro(*art, src, Rectangle{dst.x - dx, dst.y + dy, dst.width, dst.height},
                 Vector2{0.0f, 0.0f}, 0.0f, Fade(Color{255, 0, 0, 255}, alpha));
  DrawTexturePro(*art, src, dst, Vector2{0.0f, 0.0f}, 0.0f,
                 Fade(Color{0, 255, 0, 255}, alpha));
  DrawTexturePro(*art, src, Rectangle{dst.x + dx, dst.y - dy, dst.width, dst.height},
                 Vector2{0.0f, 0.0f}, 0.0f, Fade(Color{0, 0, 255, 255}, alpha));
  EndBlendMode();
}

void ShellView::PaintIris(float open, bool reduced) {
  const Color dark = ToRay(theme_.palette.titleMenu);
  open = std::clamp(open, 0.0f, 1.0f);
  if (open >= 1.0f) { return; }
  if (reduced) {
    DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                  Fade(dark, 1.0f - open));
    return;
  }
  const Vector2 centre{width_ * 0.5f, height_ * 0.5f};
  const float reach = std::hypot(width_, height_) * 0.5f + 4.0f;
  const float radius = reach * open;
  if (radius < 1.0f) {
    DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), dark);
    return;
  }
  DrawRing(centre, radius, reach + 2.0f, 0.0f, 360.0f, 72, dark);
  DrawRing(centre, std::max(0.0f, radius - 3.0f), radius, 0.0f, 360.0f, 72,
           ToRay(theme_.palette.titleMenuBorder));
}

// The card between the settings and the ship. Nothing is loading: the voyage
// was built on the press. It is the pause a launch should have.
void ShellView::PaintLaunchCard(double elapsedMs) {
  const Palette& p = theme_.palette;
  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), ToRay(p.titleMenu));
  const float u = std::clamp(static_cast<float>(elapsedMs / kLaunchCardMs), 0.0f, 1.0f);
  const int headPx = TypePx(16);
  const int linePx = TypePx(11);
  const float barW = std::min(420.0f, width_ * 0.6f);
  const float barX = std::round((width_ - barW) * 0.5f);
  const float midY = std::round(height_ * 0.5f);

  const char* head = "PREPARING VOYAGE";
  Text(head, static_cast<int>((width_ - TextWidth(head, headPx, true)) * 0.5f),
       static_cast<int>(midY - 34.0f - headPx), headPx, ToRay(p.titleMenuInk), true);

  // Twenty-four blocks, filled left to right in whole steps.
  const int cells = 24;
  const int lit = std::min(cells, static_cast<int>(u * 1.08f * cells));
  const float gap = 3.0f;
  const float cellW = (barW - gap * (cells - 1)) / cells;
  for (int i = 0; i < cells; ++i) {
    const Rectangle cell{barX + i * (cellW + gap), midY - 8.0f, cellW, 16.0f};
    if (i < lit) { DrawRectangleRec(cell, ToRay(p.titleMenuInk)); }
    else { DrawRectangleLinesEx(cell, 1.0f, ToRay(p.titleMenuBorder, 150)); }
  }

  const char* step = u < 0.30f ? "Reading the chart" : u < 0.62f ? "Placing the ship"
                   : u < 0.92f ? "Opening the deck" : "Ready";
  Text(step, static_cast<int>(barX), static_cast<int>(midY + 22.0f), linePx,
       ToRay(p.titleMenuBorder), true);
  if (!fadeLabel_.empty()) {
    const float lw = TextWidth(fadeLabel_.c_str(), linePx, true);
    Text(fadeLabel_.c_str(), static_cast<int>(barX + barW - lw), static_cast<int>(midY + 22.0f),
         linePx, ToRay(p.titleMenuBorder), true);
  }
}

void ShellView::PaintChrome(Simulation& sim, double nowMs) {
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(width_, height_));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
  ImGui::Begin("##spaceship", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
  ImDrawList* draw = ImGui::GetWindowDrawList();

  // ---- taskbar -----------------------------------------------------------
  const ship::Palette& p = theme_.palette;
  draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width_, Metrics::kTaskbarH),
                      ToIm(theme_.id == "classic" ? p.panel2 : p.chrome));
  draw->AddLine(ImVec2(0.0f, Metrics::kTaskbarH - 1.0f),
                ImVec2(width_, Metrics::kTaskbarH - 1.0f), ToIm(p.borderDark));

  std::vector<TabView> tabs = TabBar(sim);
  tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [](const TabView& tab) {
    return tab.home;
  }), tabs.end());
  std::vector<float> tabWidths;
  float tabsWidth = 4.0f;
  for (const TabView& tab : tabs) {
    const std::string& label = tab.title;
    const float tabW = ImGui::CalcTextSize(label.c_str()).x + (tab.closable ? 34.0f : 22.0f);
    tabWidths.push_back(tabW);
    tabsWidth += tabW + 4.0f;
  }
  maxTabScroll_ = std::max(0.0f, tabsWidth - layout_.tabStrip.w);
  tabScroll_ = std::max(0.0f, std::min(maxTabScroll_, tabScroll_));
  const auto active = std::find_if(tabs.begin(), tabs.end(), [](const TabView& t) { return t.active; });
  const std::string requested = sim.State().tabRevealKey;
  const auto reveal = requested.empty() ? active :
      std::find_if(tabs.begin(), tabs.end(), [&requested](const TabView& t) { return t.key == requested; });
  if (reveal != tabs.end() && (!requested.empty() || reveal->key != lastRevealedTab_)) {
    const std::size_t index = static_cast<std::size_t>(reveal - tabs.begin());
    float start = 4.0f;
    for (std::size_t i = 0; i < index; ++i) { start += tabWidths[i] + 4.0f; }
    const float end = start + tabWidths[index];
    if (end - tabScroll_ > layout_.tabStrip.w) { tabScroll_ = std::min(maxTabScroll_, end - layout_.tabStrip.w); }
    if (start - tabScroll_ < 0.0f) { tabScroll_ = start; }
    lastRevealedTab_ = active == tabs.end() ? "" : active->key;
  }
  sim.MutableState().tabRevealKey.clear();
  for (auto it = tabPulseStarts_.begin(); it != tabPulseStarts_.end();) {
    if (std::none_of(tabs.begin(), tabs.end(), [&](const TabView& t) { return t.key == it->first; }))
      it = tabPulseStarts_.erase(it);
    else ++it;
  }
  tabHits_.clear();
  draw->PushClipRect(ImVec2(layout_.tabStrip.x, 0.0f),
                     ImVec2(layout_.tabStrip.Right(), Metrics::kTaskbarH), true);
  float x = layout_.tabStrip.x + 4.0f - tabScroll_;
  for (std::size_t i = 0; i < tabs.size(); ++i) {
    const TabView& tab = tabs[i];
    const float w = tabWidths[i];
    const Area tabBox{x, 3.0f, w, Metrics::kTaskbarH - 5.0f};
    const Area closeBox{tabBox.Right() - 19.0f, tabBox.y, 18.0f, tabBox.h};
    tabHits_.push_back(TabHit{tab.key, tabBox, closeBox, tab.closable});
    ImU32 fill = tab.active ? ToIm(p.content) : ToIm(p.panel3);
    if (tab.fresh && !sim.State().reducedMotion) { tabPulseStarts_[tab.key] = nowMs; }
    const auto pulse = tabPulseStarts_.find(tab.key);
    if (pulse != tabPulseStarts_.end()) {
      const double elapsed = std::floor(std::max(0.0, nowMs - pulse->second) / 50.0) * 50.0;
      if (sim.State().reducedMotion || elapsed >= 1660.0) { tabPulseStarts_.erase(pulse); }
      else {
        const double t = std::clamp((elapsed - 160.0) / 1500.0, 0.0, 1.0);
        const double k = t * t * (3.0 - 2.0 * t);
        const int base[3] = {tab.active ? 255 : 239, tab.active ? 255 : 232, tab.active ? 255 : 220};
        const int warm[3] = {233, 214, 184};
        int c[3];
        for (int q = 0; q < 3; ++q) c[q] = static_cast<int>(std::round(warm[q] + (base[q] - warm[q]) * k));
        fill = IM_COL32(c[0], c[1], c[2], 255);
      }
    }
    const ImU32 outerLine = ToIm(p.borderDark);
    const ImU32 innerLine = ToIm(tab.active ? p.border : p.muted);
    PaintChamferBoxIm(draw, x, 3.0f, w, Metrics::kTaskbarH - 5.0f, kChamferSmall, fill,
                      outerLine, innerLine);
    const ImU32 ink = ToIm(tab.active ? p.activeInk : p.tabInk);
    float titleX = x + 9.0f;
    draw->AddText(ImVec2(titleX, 9.0f), ink, tab.title.c_str());
    if (tab.closable) { draw->AddText(ImVec2(x + w - 15.0f, 9.0f), ToIm(p.muted), "x"); }
    x += w + 4.0f;
  }
  draw->PopClipRect();

  ImFont* bezelFont = io.Fonts->Fonts.Size > 6 ? io.Fonts->Fonts[io.Fonts->Fonts.Size - 1]
                                               : ImGui::GetFont();
  constexpr float kBezelPx = 10.5f;
  const ImU32 bezelInk = ToIm(p.muted, 122);
  const ImU32 tickInk = ToIm(p.borderDark, 88);
  {
    const char* topBezel = "CRT-84 // CH-04";
    const ImVec2 topSz = bezelFont->CalcTextSizeA(kBezelPx, FLT_MAX, 0.0f, topBezel);
    const float topX = layout_.controls.x - topSz.x - 10.0f;
    if (x + 14.0f < topX && topX > layout_.tabStrip.x + 8.0f) {
      draw->AddText(bezelFont, kBezelPx,
                    ImVec2(topX, std::round((Metrics::kTaskbarH - topSz.y) * 0.5f)),
                    bezelInk, topBezel);
    }
  }

  // The ship identity is a permanent home control in the fixed sidebar slot.
  PaintChamferBoxIm(draw, layout_.home.x + 2.0f, layout_.home.y + 2.0f, layout_.home.w - 4.0f,
                    layout_.home.h - 4.0f, kChamferSmall, ToIm(p.panel2), ToIm(p.borderDark),
                    ToIm(p.border));
  draw->AddText(ImVec2(layout_.home.x + 10.0f, 9.0f), ToIm(p.coffee), "SHIP");
  draw->AddText(ImVec2(layout_.home.x + 52.0f, 9.0f), ToIm(p.activeInk),
                sim.Ship().Get("name").c_str());

  // Only the tab-scroll arrows remain in the top-right control slot.
  draw->AddRectFilled(ImVec2(layout_.controls.x, 0.0f),
                      ImVec2(layout_.controls.Right(), Metrics::kTaskbarH), ToIm(p.panel2));
  PaintChamferBoxIm(draw, layout_.scrollLeft.x + 1.0f, 3.0f, layout_.scrollLeft.w - 2.0f,
                    Metrics::kTaskbarH - 6.0f, kChamferSmall, ToIm(p.panel),
                    ToIm(p.borderDark), ToIm(p.border));
  PaintChamferBoxIm(draw, layout_.scrollRight.x + 1.0f, 3.0f, layout_.scrollRight.w - 2.0f,
                    Metrics::kTaskbarH - 6.0f, kChamferSmall, ToIm(p.panel),
                    ToIm(p.borderDark), ToIm(p.border));
  const float textY = std::max(0.0f, (Metrics::kTaskbarH - ImGui::GetFontSize()) * 0.5f);
  const float leftW = ImGui::CalcTextSize("<").x;
  const float rightW = ImGui::CalcTextSize(">").x;
  draw->AddText(ImVec2(layout_.scrollLeft.x + (layout_.scrollLeft.w - leftW) * 0.5f, textY), ToIm(p.muted), "<");
  draw->AddText(ImVec2(layout_.scrollRight.x + (layout_.scrollRight.w - rightW) * 0.5f, textY), ToIm(p.muted), ">");
  (void)tabScroll_;

  // ---- sidebar -----------------------------------------------------------
  // Sidebar text is compact enough for the narrow column, while tracking the
  // same reading-size preference as the central labels and other controls.
  ImGui::PushFont(nullptr, theme_.SidebarPx());
  const float sidebarLine = Theme::LineHeightForPx(ImGui::GetFontSize());
  draw->AddRectFilled(ImVec2(layout_.sidepanel.x, layout_.sidepanel.y),
                      ImVec2(layout_.sidepanel.Right(), layout_.sidepanel.Bottom()),
                      ToIm(p.chrome));

  const auto measureSide = [&](const std::string& text, float width) {
    const float h = ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y;
    return std::max(1.0f, std::ceil(h / ImGui::GetFontSize())) * sidebarLine;
  };
  sideHits_ = SideHits(sim, layout_.sidepanel, layout_.sidefoot, sidebarLine,
                       sideScroll_, &sideContentH_, measureSide);
  // A browser clamps scrollTop when a section folds or a new voyage replaces
  // its contents. Do the same here; an old offset must not erase Vessel and
  // the other collapsed headings after the sidebar becomes shorter.
  const float boundedSideScroll = std::clamp(sideScroll_, 0.0f,
      std::max(0.0f, sideContentH_ - layout_.sidepanel.h));
  if (boundedSideScroll != sideScroll_) {
    sideScroll_ = boundedSideScroll;
    sideHits_ = SideHits(sim, layout_.sidepanel, layout_.sidefoot, sidebarLine,
                         sideScroll_, &sideContentH_, measureSide);
  }
  const std::vector<SideBlock> blocks = SideBlocks(sim);
  draw->PushClipRect(ImVec2(layout_.sidepanel.x, layout_.sidepanel.y),
                     ImVec2(layout_.sidepanel.Right(), layout_.sidepanel.Bottom()), true);
  for (SideHit& hit : sideHits_) {
    if (hit.pinned) { continue; } // footer labels are tooltips, never sidebar text
    const Area& box = hit.paintBox;
    auto wrapped = [&](const std::string& text, float y, float width, ImU32 ink) {
      draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(box.x, y), ink,
                    text.c_str(), nullptr, width);
    };
    if (hit.header) {
      draw->AddText(ImVec2(box.x, box.y),
                    ToIm(theme_.id.find("-night") != std::string::npos ? p.ink : p.coffee),
                    hit.text.c_str());
      bool open = true;
      for (const auto& block : blocks) { if (block.key == hit.key) { open = block.open; } }
      if (hit.key != "indicators") {
        const float x = box.Right() - 9, y = box.y + 5;
        if (open) {
          draw->AddTriangleFilled(ImVec2(x, y), ImVec2(x + 8, y),
                                  ImVec2(x + 4, y + 5),
                                  theme_.id.find("-night") != std::string::npos ? ToIm(p.ink) : IM_COL32(140,111,78,255));
        } else {
          draw->AddTriangleFilled(ImVec2(x, y - 1), ImVec2(x, y + 7),
                                  ImVec2(x + 5, y + 3),
                                  theme_.id.find("-night") != std::string::npos ? ToIm(p.ink) : IM_COL32(162,148,127,255));
        }
      } else {
        for (const auto& line : PlaceIndicators(IndicatorLines(sim))) {
          const float base = box.Bottom() + 4;
          draw->AddRectFilled(ImVec2(box.x + line.ghost.x, base + line.ghost.y),
                              ImVec2(box.x + line.ghost.x + 3, base + line.ghost.Bottom()), IM_COL32(236,229,216,255));
          draw->AddRectFilled(ImVec2(box.x + line.line.x, base + line.line.y),
                              ImVec2(box.x + line.line.x + 3, base + line.line.Bottom()),
                              ToIm(line.grow ? p.terracotta : p.sage));
        }
      }
      continue;
    }
    if (hit.action == SideAction::OpenEvent) {
      wrapped(hit.text, box.y, box.w - 60, ToIm(p.ink));
      const Area button{box.Right() - 52, box.y, 52, sidebarLine + 4};
      PaintChamferBoxIm(draw, button.x, button.y, button.w, button.h, kChamferSmall,
                        ToIm(p.control), ToIm(p.borderDark), ToIm(p.border));
      const float openW = ImGui::CalcTextSize("Open").x;
      draw->AddText(ImVec2(button.x + (button.w - openW) * 0.5f, button.y + 2),
                    ToIm(p.muted), "Open");
      continue;
    }
    if (hit.action == SideAction::OpenCrew) {
      const ImVec2 name = ImGui::CalcTextSize(hit.text.c_str(), nullptr, false, box.w);
      hit.box.w = std::min(hit.box.w, name.x);
      hit.box.h = std::max(0.0f, std::min(hit.box.Bottom(), box.y + name.y) - hit.box.y);
    }
    wrapped(hit.text, box.y, box.w, ToIm(p.ink));
    if (!hit.rightValue.empty()) {
      if (hit.stackValue) {
        wrapped(hit.rightValue, box.y + hit.valueOffset, box.w, ToIm(hit.dim ? p.muted : p.ink));
      } else {
        const float valueW = ImGui::CalcTextSize(hit.rightValue.c_str()).x;
        draw->AddText(ImVec2(box.Right() - valueW, box.y), ToIm(hit.dim ? p.muted : p.ink), hit.rightValue.c_str());
      }
    }
    if (!hit.detail.empty()) { wrapped(hit.detail, box.y + hit.valueOffset, box.w, ToIm(p.muted)); }
  }
  draw->PopClipRect();

  // ---- pinned foot -------------------------------------------------------
  draw->AddRectFilled(ImVec2(layout_.sidefoot.x, layout_.sidefoot.y),
                      ImVec2(layout_.sidefoot.Right(), layout_.sidefoot.Bottom()), ToIm(p.chrome));
  draw->AddLine(ImVec2(0.0f, layout_.sidefoot.y),
                ImVec2(layout_.sidefoot.Right(), layout_.sidefoot.y), ToIm(p.border));
  for (const SideHit& hit : sideHits_) {
    if (!hit.pinned) { continue; }
    if (hit.action == SideAction::Manifest || hit.action == SideAction::Log ||
        hit.action == SideAction::Files || hit.action == SideAction::Settings) {
      PaintChamferBoxIm(draw, hit.box.x + 3.0f, hit.box.y + 1.0f, hit.box.w - 6.0f, 26.0f,
                        kChamferSmall, ToIm(p.panel2), ToIm(p.borderDark), ToIm(p.border));
      const ImVec2 iconCenter(hit.box.x + hit.box.w * 0.5f,
                             hit.box.y + 14.0f);
      const ImU32 iconInk = ToIm(theme_.id.find("-night") != std::string::npos ? p.ink : p.coffee);
      if (hit.action == SideAction::Manifest) {
        draw->AddLine(ImVec2(iconCenter.x, iconCenter.y - 8.0f),
                      ImVec2(iconCenter.x, iconCenter.y + 4.0f), iconInk, 2.0f);
        draw->AddTriangleFilled(ImVec2(iconCenter.x + 1.0f, iconCenter.y - 6.0f),
                                ImVec2(iconCenter.x + 8.0f, iconCenter.y - 2.0f),
                                ImVec2(iconCenter.x + 1.0f, iconCenter.y + 1.0f),
                                ToIm(p.muted));
        draw->AddTriangleFilled(ImVec2(iconCenter.x - 8.0f, iconCenter.y + 4.0f),
                                ImVec2(iconCenter.x + 8.0f, iconCenter.y + 4.0f),
                                ImVec2(iconCenter.x, iconCenter.y + 9.0f), iconInk);
      } else if (hit.action == SideAction::Log) {
        for (int line = 0; line < 3; ++line) {
          const float yy = iconCenter.y - 5.0f + static_cast<float>(line) * 5.0f;
          draw->AddLine(ImVec2(iconCenter.x - 8.0f, yy),
                        ImVec2(iconCenter.x + (line == 2 ? 3.0f : 8.0f), yy),
                        iconInk, 2.0f);
        }
      } else if (hit.action == SideAction::Files) {
        draw->AddRectFilled(ImVec2(iconCenter.x - 7.0f, iconCenter.y - 8.0f),
                            ImVec2(iconCenter.x + 7.0f, iconCenter.y + 8.0f),
                            iconInk, 1.0f);
        draw->AddRectFilled(ImVec2(iconCenter.x - 2.0f, iconCenter.y - 7.0f),
                            ImVec2(iconCenter.x + 4.0f, iconCenter.y - 3.0f),
                            ToIm(p.panel));
        draw->AddRectFilled(ImVec2(iconCenter.x - 4.0f, iconCenter.y + 2.0f),
                            ImVec2(iconCenter.x + 5.0f, iconCenter.y + 6.0f),
                            ToIm(p.coffeeLight));
      } else {
        draw->AddCircle(iconCenter, 6.0f, iconInk, 0, 2.0f);
        draw->AddCircleFilled(iconCenter, 2.0f, iconInk);
        draw->AddLine(ImVec2(iconCenter.x, iconCenter.y - 10.0f),
                      ImVec2(iconCenter.x, iconCenter.y - 6.0f), iconInk, 2.0f);
        draw->AddLine(ImVec2(iconCenter.x, iconCenter.y + 6.0f),
                      ImVec2(iconCenter.x, iconCenter.y + 10.0f), iconInk, 2.0f);
        draw->AddLine(ImVec2(iconCenter.x - 10.0f, iconCenter.y),
                      ImVec2(iconCenter.x - 6.0f, iconCenter.y), iconInk, 2.0f);
        draw->AddLine(ImVec2(iconCenter.x + 6.0f, iconCenter.y),
                      ImVec2(iconCenter.x + 10.0f, iconCenter.y), iconInk, 2.0f);
      }
      if (ImGui::IsMouseHoveringRect(ImVec2(hit.box.x, hit.box.y),
                                     ImVec2(hit.box.Right(), hit.box.Bottom()))) {
        ImGui::SetTooltip("%s", hit.text.c_str());
      }
      continue;
    }
    draw->AddText(ImVec2(hit.box.x, hit.box.y), ToIm(p.ink), hit.text.c_str());
  }
  draw->AddText(ImVec2(8.0f, layout_.sidefoot.y + 63.0f), ToIm(p.muted),
                "Spaceship Command");
  draw->AddText(bezelFont, kBezelPx, ImVec2(8.0f, layout_.sidefoot.y + 81.0f),
                bezelInk, "SYS-01 // REV.84");
  ImGui::PopFont();

  // Subtle corner chassis registration ticks tucked inside the outer frame gutter.
  if (layout_.content.w > 24.0f && layout_.content.h > 24.0f) {
    const float cl = layout_.content.x + 2.0f;
    const float ct = layout_.content.y + 2.0f;
    const float cr = layout_.content.Right() - 3.0f;
    const float cb = layout_.content.Bottom() - 3.0f;
    constexpr float kTick = 5.0f;
    draw->AddLine(ImVec2(cl, ct), ImVec2(cl + kTick, ct), tickInk, 1.0f);
    draw->AddLine(ImVec2(cl, ct), ImVec2(cl, ct + kTick), tickInk, 1.0f);
    draw->AddLine(ImVec2(cr - kTick, ct), ImVec2(cr, ct), tickInk, 1.0f);
    draw->AddLine(ImVec2(cr, ct), ImVec2(cr, ct + kTick), tickInk, 1.0f);
    draw->AddLine(ImVec2(cl, cb - kTick), ImVec2(cl, cb), tickInk, 1.0f);
    draw->AddLine(ImVec2(cl, cb), ImVec2(cl + kTick, cb), tickInk, 1.0f);
    draw->AddLine(ImVec2(cr - kTick, cb), ImVec2(cr, cb), tickInk, 1.0f);
    draw->AddLine(ImVec2(cr, cb - kTick), ImVec2(cr, cb), tickInk, 1.0f);

    if (sim.State().activeTab != "home" &&
        contentPaintBottom_ < layout_.content.Bottom() - 24.0f) {
      const char* brBezel = "CAL 60HZ // MK.IV";
      const ImVec2 brSz = bezelFont->CalcTextSizeA(kBezelPx, FLT_MAX, 0.0f, brBezel);
      draw->AddText(bezelFont, kBezelPx,
                    ImVec2(cr - brSz.x - 4.0f, cb - brSz.y - 1.0f),
                    bezelInk, brBezel);
    }
  }

  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

namespace {

constexpr float kWrenHalfDepth = 0.25f;

float SmoothStep01(float edge0, float edge1, float x) {
  const float t = std::clamp((x - edge0) / std::max(1e-5f, edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

void Edt1D(const float* f, int n, float* d, std::vector<int>& v, std::vector<float>& z) {
  if (n <= 0) return;
  int k = 0;
  v[0] = 0;
  z[0] = -1e20f;
  z[1] = 1e20f;
  for (int q = 1; q < n; ++q) {
    float s = ((f[q] + static_cast<float>(q * q)) -
               (f[v[k]] + static_cast<float>(v[k] * v[k]))) /
              static_cast<float>(2 * q - 2 * v[k]);
    while (s <= z[k]) {
      --k;
      s = ((f[q] + static_cast<float>(q * q)) -
           (f[v[k]] + static_cast<float>(v[k] * v[k]))) /
          static_cast<float>(2 * q - 2 * v[k]);
    }
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = 1e20f;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < static_cast<float>(q)) { ++k; }
    const float dx = static_cast<float>(q - v[k]);
    d[q] = dx * dx + f[v[k]];
  }
}

std::vector<float> ComputeEuclideanDistance2D(const std::vector<uint8_t>& zeroSet, int w, int h) {
  constexpr float kInf = 1e12f;
  std::vector<float> grid(static_cast<std::size_t>(w) * h, kInf);
  for (std::size_t i = 0; i < grid.size(); ++i) {
    if (zeroSet[i] != 0) grid[i] = 0.0f;
  }
  const int maxDim = std::max(w, h);
  std::vector<float> f(static_cast<std::size_t>(maxDim));
  std::vector<float> d(static_cast<std::size_t>(maxDim));
  std::vector<int> v(static_cast<std::size_t>(maxDim));
  std::vector<float> z(static_cast<std::size_t>(maxDim) + 1);

  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) { f[y] = grid[static_cast<std::size_t>(y) * w + x]; }
    Edt1D(f.data(), h, d.data(), v, z);
    for (int y = 0; y < h; ++y) { grid[static_cast<std::size_t>(y) * w + x] = d[y]; }
  }
  for (int y = 0; y < h; ++y) {
    float* row = &grid[static_cast<std::size_t>(y) * w];
    Edt1D(row, w, d.data(), v, z);
    for (int x = 0; x < w; ++x) { row[x] = std::sqrt(std::max(0.0f, d[x])); }
  }
  return grid;
}

Matrix BuildOffAxisFrustumMatrix(double left, double right, double bottom, double top,
                                 double znear, double zfar) {
  Matrix m{};
  const float rl = static_cast<float>(right - left);
  const float tb = static_cast<float>(top - bottom);
  const float fn = static_cast<float>(zfar - znear);
  m.m0 = (2.0f * static_cast<float>(znear)) / rl;
  m.m5 = (2.0f * static_cast<float>(znear)) / tb;
  m.m8 = static_cast<float>(right + left) / rl;
  m.m9 = static_cast<float>(top + bottom) / tb;
  m.m10 = -static_cast<float>(zfar + znear) / fn;
  m.m11 = -1.0f;
  m.m14 = -(2.0f * static_cast<float>(zfar) * static_cast<float>(znear)) / fn;
  return m;
}

Matrix BuildScaleRotateXYZ(float scale, float pitchRad, float yawRad, float rollRad) {
  const float cosz = std::cos(-rollRad);
  const float sinz = std::sin(-rollRad);
  const float cosy = std::cos(-yawRad);
  const float siny = std::sin(-yawRad);
  const float cosx = std::cos(-pitchRad);
  const float sinx = std::sin(-pitchRad);
  Matrix m{};
  m.m0 = scale * (cosz * cosy);
  m.m1 = scale * ((cosz * siny * sinx) - (sinz * cosx));
  m.m2 = scale * ((cosz * siny * cosx) + (sinz * sinx));
  m.m4 = scale * (sinz * cosy);
  m.m5 = scale * ((sinz * siny * sinx) + (cosz * cosx));
  m.m6 = scale * ((sinz * siny * cosx) - (cosz * sinx));
  m.m8 = scale * (-siny);
  m.m9 = scale * (cosy * sinx);
  m.m10 = scale * (cosy * cosx);
  m.m15 = 1.0f;
  return m;
}

Matrix BuildTranslateZ(float tz) {
  Matrix m{};
  m.m0 = 1.0f;
  m.m5 = 1.0f;
  m.m10 = 1.0f;
  m.m14 = tz;
  m.m15 = 1.0f;
  return m;
}

// Column-vector product C = A * B (B applies first, then A).
Matrix MulColMajor(const Matrix& a, const Matrix& b) {
  Matrix r{};
  r.m0  = a.m0*b.m0 + a.m4*b.m1 + a.m8*b.m2  + a.m12*b.m3;
  r.m1  = a.m1*b.m0 + a.m5*b.m1 + a.m9*b.m2  + a.m13*b.m3;
  r.m2  = a.m2*b.m0 + a.m6*b.m1 + a.m10*b.m2 + a.m14*b.m3;
  r.m3  = a.m3*b.m0 + a.m7*b.m1 + a.m11*b.m2 + a.m15*b.m3;
  r.m4  = a.m0*b.m4 + a.m4*b.m5 + a.m8*b.m6  + a.m12*b.m7;
  r.m5  = a.m1*b.m4 + a.m5*b.m5 + a.m9*b.m6  + a.m13*b.m7;
  r.m6  = a.m2*b.m4 + a.m6*b.m5 + a.m10*b.m6 + a.m14*b.m7;
  r.m7  = a.m3*b.m4 + a.m7*b.m5 + a.m11*b.m6 + a.m15*b.m7;
  r.m8  = a.m0*b.m8 + a.m4*b.m9 + a.m8*b.m10 + a.m12*b.m11;
  r.m9  = a.m1*b.m8 + a.m5*b.m9 + a.m9*b.m10 + a.m13*b.m11;
  r.m10 = a.m2*b.m8 + a.m6*b.m9 + a.m10*b.m10 + a.m14*b.m11;
  r.m11 = a.m3*b.m8 + a.m7*b.m9 + a.m11*b.m10 + a.m15*b.m11;
  r.m12 = a.m0*b.m12 + a.m4*b.m13 + a.m8*b.m14 + a.m12*b.m15;
  r.m13 = a.m1*b.m12 + a.m5*b.m13 + a.m9*b.m14 + a.m13*b.m15;
  r.m14 = a.m2*b.m12 + a.m6*b.m13 + a.m10*b.m14 + a.m14*b.m15;
  r.m15 = a.m3*b.m12 + a.m7*b.m13 + a.m11*b.m14 + a.m15*b.m15;
  return r;
}

Vector2 ProjectPointMvp(const Vector3& p, const Matrix& mvp, float viewW, float viewH) {
  const float x = mvp.m0 * p.x + mvp.m4 * p.y + mvp.m8 * p.z + mvp.m12;
  const float y = mvp.m1 * p.x + mvp.m5 * p.y + mvp.m9 * p.z + mvp.m13;
  const float w = mvp.m3 * p.x + mvp.m7 * p.y + mvp.m11 * p.z + mvp.m15;
  const float invW = std::fabs(w) > 1e-5f ? 1.0f / w : 1.0f;
  const float ndcX = x * invW;
  const float ndcY = y * invW;
  return Vector2{(ndcX * 0.5f + 0.5f) * viewW, (0.5f - ndcY * 0.5f) * viewH};
}

Matrix BuildIdentityMatrix() {
  Matrix m{};
  m.m0 = 1.0f;
  m.m5 = 1.0f;
  m.m10 = 1.0f;
  m.m15 = 1.0f;
  return m;
}

Matrix BuildLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up) {
  float zx = eye.x - target.x;
  float zy = eye.y - target.y;
  float zz = eye.z - target.z;
  const float zLen = std::sqrt(zx * zx + zy * zy + zz * zz);
  if (zLen > 1e-6f) { zx /= zLen; zy /= zLen; zz /= zLen; }
  float xx = up.y * zz - up.z * zy;
  float xy = up.z * zx - up.x * zz;
  float xz = up.x * zy - up.y * zx;
  const float xLen = std::sqrt(xx * xx + xy * xy + xz * xz);
  if (xLen > 1e-6f) { xx /= xLen; xy /= xLen; xz /= xLen; }
  const float yx = zy * xz - zz * xy;
  const float yy = zz * xx - zx * xz;
  const float yz = zx * xy - zy * xx;
  Matrix m{};
  m.m0 = xx;  m.m4 = xy;  m.m8  = xz;  m.m12 = -(xx * eye.x + xy * eye.y + xz * eye.z);
  m.m1 = yx;  m.m5 = yy;  m.m9  = yz;  m.m13 = -(yx * eye.x + yy * eye.y + yz * eye.z);
  m.m2 = zx;  m.m6 = zy;  m.m10 = zz;  m.m14 = -(zx * eye.x + zy * eye.y + zz * eye.z);
  m.m3 = 0.0f; m.m7 = 0.0f; m.m11 = 0.0f; m.m15 = 1.0f;
  return m;
}

bool ProjectWorld3D(const Vector3& p, const Matrix& mvp, float viewW, float viewH,
                    Vector2* outScreen, float* outNdcZ = nullptr) {
  const float x = mvp.m0 * p.x + mvp.m4 * p.y + mvp.m8 * p.z + mvp.m12;
  const float y = mvp.m1 * p.x + mvp.m5 * p.y + mvp.m9 * p.z + mvp.m13;
  const float z = mvp.m2 * p.x + mvp.m6 * p.y + mvp.m10 * p.z + mvp.m14;
  const float w = mvp.m3 * p.x + mvp.m7 * p.y + mvp.m11 * p.z + mvp.m15;
  if (w <= 0.05f) { return false; }
  const float invW = 1.0f / w;
  const float ndcX = x * invW;
  const float ndcY = y * invW;
  const float ndcZ = z * invW;
  if (ndcZ < -1.0f || ndcZ > 1.0f) { return false; }
  if (outScreen != nullptr) {
    *outScreen = Vector2{(ndcX * 0.5f + 0.5f) * viewW, (0.5f - ndcY * 0.5f) * viewH};
  }
  if (outNdcZ != nullptr) { *outNdcZ = ndcZ; }
  return true;
}

constexpr const char* kWren3DVertexShader = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec2 fragTexCoord;
out vec3 fragNormal;
out vec3 fragWorldPos;
out vec4 fragColor;

void main() {
  fragTexCoord = vertexTexCoord;
  fragColor = vertexColor;
  vec4 worldPos = matModel * vec4(vertexPosition, 1.0);
  fragWorldPos = worldPos.xyz;
  fragNormal = normalize(mat3(matNormal) * vertexNormal);
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kWren3DFragmentShader = R"(
#version 330
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragWorldPos;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float uWarmBias;
uniform vec3 uKeyDir;

out vec4 finalColor;

void main() {
  vec4 tex = texture(texture0, fragTexCoord);
  if (tex.a < 0.08) discard;

  vec3 N = normalize(fragNormal);
  vec3 L = normalize(uKeyDir);
  vec3 V = normalize(vec3(0.0, 0.0, 12.0) - fragWorldPos);

  float ndotl = max(dot(N, L), 0.0);
  float bounce = max(-N.y, 0.0) * 0.25;
  float fresnel = pow(1.0 - clamp(abs(dot(N, V)), 0.0, 1.0), 2.3);
  vec3 H = normalize(L + V);
  float spec = pow(max(dot(N, H), 0.0), 26.0) * 0.09;

  vec3 base = tex.rgb * colDiffuse.rgb * mix(vec3(1.0), fragColor.rgb, 0.45);
  float shade = 0.89 + 0.14 * ndotl + 0.03 * bounce;
  vec3 rimTint = mix(vec3(0.76, 0.86, 0.98), vec3(1.00, 0.90, 0.72), clamp(uWarmBias, 0.0, 1.0));
  vec3 lit = base * shade + rimTint * (fresnel * 0.09 + spec);
  finalColor = vec4(clamp(lit, 0.0, 1.0), tex.a * colDiffuse.a);
}
)";

}  // namespace

void ShellView::BuildWrenSdfFromImage(const Image& image) {
  if (image.data == nullptr || image.width <= 0 || image.height <= 0) { return; }
  Image copy = ImageCopy(image);
  if (copy.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
    ImageFormat(&copy, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  }
  const int gw = 670;
  const int gh = 210;
  const int srcW = copy.width;
  const int srcH = copy.height;
  const uint8_t* px = static_cast<const uint8_t*>(copy.data);

  std::vector<uint8_t> solid(static_cast<std::size_t>(gw) * gh, 0);
  std::vector<uint8_t> empty(static_cast<std::size_t>(gw) * gh, 1);
  std::vector<float> luma(static_cast<std::size_t>(gw) * gh, 0.5f);

  for (int gy = 0; gy < gh; ++gy) {
    const int y0 = gy * srcH / gh;
    const int y1 = std::max(y0 + 1, (gy + 1) * srcH / gh);
    for (int gx = 0; gx < gw; ++gx) {
      const int x0 = gx * srcW / gw;
      const int x1 = std::max(x0 + 1, (gx + 1) * srcW / gw);
      uint8_t maxA = 0;
      float lSum = 0.0f;
      int opaqueCnt = 0;
      for (int y = y0; y < y1 && y < srcH; ++y) {
        for (int x = x0; x < x1 && x < srcW; ++x) {
          const uint8_t* c = &px[(static_cast<std::size_t>(y) * srcW + x) * 4];
          if (c[3] > maxA) maxA = c[3];
          if (c[3] >= 64) {
            lSum += (0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]) / 255.0f;
            ++opaqueCnt;
          }
        }
      }
      const std::size_t idx = static_cast<std::size_t>(gy) * gw + gx;
      const bool isBorder = (gx == 0 || gy == 0 || gx == gw - 1 || gy == gh - 1);
      if (!isBorder && maxA >= 96) {
        solid[idx] = 1;
        empty[idx] = 0;
        luma[idx] = opaqueCnt > 0 ? (lSum / static_cast<float>(opaqueCnt)) : 0.75f;
      }
    }
  }
  UnloadImage(copy);

  const std::vector<float> distIn = ComputeEuclideanDistance2D(empty, gw, gh);
  const std::vector<float> distOut = ComputeEuclideanDistance2D(solid, gw, gh);
  float maxDist = 1.0f;
  std::vector<float> sdf(static_cast<std::size_t>(gw) * gh, 0.0f);
  for (std::size_t i = 0; i < sdf.size(); ++i) {
    sdf[i] = solid[i] != 0 ? distIn[i] : -distOut[i];
    if (sdf[i] > maxDist) maxDist = sdf[i];
  }

  // Smooth the luma slightly so micro-relief emphasizes armor bays without jitter.
  std::vector<float> smoothLuma = luma;
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<float> next = smoothLuma;
    for (int gy = 1; gy < gh - 1; ++gy) {
      for (int gx = 1; gx < gw - 1; ++gx) {
        const std::size_t idx = static_cast<std::size_t>(gy) * gw + gx;
        if (solid[idx] == 0) continue;
        float acc = 0.0f;
        float wsum = 0.0f;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const std::size_t nidx = static_cast<std::size_t>(gy + dy) * gw + (gx + dx);
            if (solid[nidx] == 0) continue;
            acc += smoothLuma[nidx];
            wsum += 1.0f;
          }
        }
        if (wsum > 0.0f) next[idx] = acc / wsum;
      }
    }
    smoothLuma = std::move(next);
  }

  wrenSdf_.w = gw;
  wrenSdf_.h = gh;
  wrenSdf_.maxDist = maxDist;
  wrenSdf_.sdf = std::move(sdf);
  wrenSdf_.luma = std::move(smoothLuma);
}

float ShellView::SampleWrenHullZ(float u, float v) const {
  if (wrenSdf_.w <= 1 || wrenSdf_.h <= 1 || wrenSdf_.sdf.empty()) { return 0.0f; }
  const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(wrenSdf_.w - 1);
  const float fy = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(wrenSdf_.h - 1);
  const int x0 = static_cast<int>(std::floor(fx));
  const int y0 = static_cast<int>(std::floor(fy));
  const int x1 = std::min(wrenSdf_.w - 1, x0 + 1);
  const int y1 = std::min(wrenSdf_.h - 1, y0 + 1);
  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);
  auto sampleField = [&](const std::vector<float>& f) {
    const float a = f[static_cast<std::size_t>(y0) * wrenSdf_.w + x0] * (1.0f - tx) +
                    f[static_cast<std::size_t>(y0) * wrenSdf_.w + x1] * tx;
    const float b = f[static_cast<std::size_t>(y1) * wrenSdf_.w + x0] * (1.0f - tx) +
                    f[static_cast<std::size_t>(y1) * wrenSdf_.w + x1] * tx;
    return a * (1.0f - ty) + b * ty;
  };
  const float s = sampleField(wrenSdf_.sdf);
  if (s <= 0.0f) { return 0.0f; }
  const float t = std::clamp(s / wrenSdf_.maxDist, 0.0f, 1.0f);
  const float dome = std::sqrt(std::clamp(1.0f - std::pow(1.0f - std::min(1.0f, t * 1.15f), 2.15f),
                                          0.0f, 1.0f));
  const float lum = sampleField(wrenSdf_.luma);
  const float relief = 0.89f + 0.11f * lum;
  return kWrenHalfDepth * dome * (1.0f + (relief - 1.0f) * SmoothStep01(0.0f, 0.18f, t));
}

void ShellView::ReleaseWrenMesh() {
  if (wrenMeshBuilt_) {
    UnloadMesh(wrenMesh_);
    wrenMesh_ = {};
    wrenMeshBuilt_ = false;
  }
  if (wrenMat_.maps != nullptr) {
    // Keep external wrenExteriorArt_ alive (owned by caller); detach before unload.
    wrenMat_.maps[MATERIAL_MAP_DIFFUSE].texture.id = rlGetTextureIdDefault();
    wrenMat_.shader.id = rlGetShaderIdDefault();
    wrenMat_.shader.locs = rlGetShaderLocsDefault();
    UnloadMaterial(wrenMat_);
    wrenMat_ = {};
  }
  if (wrenShader_.id != 0 && wrenShader_.id != rlGetShaderIdDefault()) {
    UnloadShader(wrenShader_);
  }
  wrenShader_ = {};
  wrenShaderWarmLoc_ = -1;
  wrenShaderLightLoc_ = -1;
  wrenMeshTextureId_ = 0;
}

void ShellView::EnsureWrenMesh() {
  const Texture2D* texture = static_cast<const Texture2D*>(wrenExteriorArt_);
  if (texture == nullptr || texture->id == 0 || texture->width <= 0 || texture->height <= 0) {
    return;
  }
  if (wrenMeshBuilt_ && wrenMeshTextureId_ == texture->id) { return; }
  if (wrenMeshAttempted_ && wrenMeshTextureId_ == texture->id) { return; }

  ReleaseWrenMesh();
  wrenMeshAttempted_ = true;
  wrenMeshTextureId_ = texture->id;

  if (wrenSdf_.sdf.empty()) {
    Image gpuImg = LoadImageFromTexture(*texture);
    bool hasOpaque = false;
    if (gpuImg.data != nullptr && gpuImg.width > 0 && gpuImg.height > 0) {
      Image check = ImageCopy(gpuImg);
      if (check.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        ImageFormat(&check, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
      }
      const uint8_t* p = static_cast<const uint8_t*>(check.data);
      const std::size_t total = static_cast<std::size_t>(check.width) * check.height;
      for (std::size_t i = 0; i < total; i += 37) {
        if (p[i * 4 + 3] > 20) { hasOpaque = true; break; }
      }
      if (hasOpaque) { BuildWrenSdfFromImage(check); }
      UnloadImage(check);
    }
    if (gpuImg.data != nullptr) { UnloadImage(gpuImg); }
  }
  if (wrenSdf_.sdf.empty()) {
#ifdef SPACESHIP_ASSET_DIR
    const std::filesystem::path assetRoot(SPACESHIP_ASSET_DIR);
#else
    const std::filesystem::path assetRoot("assets");
#endif
    const std::array<std::filesystem::path, 2> candidates{
        assetRoot / exteriorArtPath_,
        std::filesystem::path("assets") / exteriorArtPath_,
    };
    for (const auto& path : candidates) {
      std::error_code ec;
      if (!std::filesystem::exists(path, ec)) { continue; }
      Image diskImg = LoadImage(path.string().c_str());
      if (diskImg.data != nullptr && diskImg.width > 0 && diskImg.height > 0) {
        BuildWrenSdfFromImage(diskImg);
        UnloadImage(diskImg);
        break;
      }
      if (diskImg.data != nullptr) { UnloadImage(diskImg); }
    }
  }
  if (wrenSdf_.sdf.empty()) { return; }

  const float aspect = static_cast<float>(texture->width) / static_cast<float>(texture->height);
  constexpr int kGridW = 240;
  constexpr int kGridH = 76;

  auto sampleSdf = [&](float u, float v) {
    const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(wrenSdf_.w - 1);
    const float fy = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(wrenSdf_.h - 1);
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(wrenSdf_.w - 1, x0 + 1);
    const int y1 = std::min(wrenSdf_.h - 1, y0 + 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float a = wrenSdf_.sdf[static_cast<std::size_t>(y0) * wrenSdf_.w + x0] * (1.0f - tx) +
                    wrenSdf_.sdf[static_cast<std::size_t>(y0) * wrenSdf_.w + x1] * tx;
    const float b = wrenSdf_.sdf[static_cast<std::size_t>(y1) * wrenSdf_.w + x0] * (1.0f - tx) +
                    wrenSdf_.sdf[static_cast<std::size_t>(y1) * wrenSdf_.w + x1] * tx;
    return a * (1.0f - ty) + b * ty;
  };

  struct MeshVert {
    Vector3 pos;
    Vector2 uv;
    Vector3 nrm;
    ::Color col;
  };
  std::vector<MeshVert> verts;
  std::vector<unsigned short> indices;
  verts.reserve(18000);
  indices.reserve(54000);

  auto evalVert = [&](float u, float v, float sideSign, bool onContour) -> MeshVert {
    const float x = (u - 0.5f) * aspect;
    const float y = 0.5f - v;
    const float z = onContour ? 0.0f : sideSign * SampleWrenHullZ(u, v);
    const float du = 1.0f / static_cast<float>(kGridW - 1);
    const float dv = 1.0f / static_cast<float>(kGridH - 1);
    const float dzdx = (SampleWrenHullZ(u + du, v) - SampleWrenHullZ(u - du, v)) / (2.0f * du * aspect);
    const float dzdy = (SampleWrenHullZ(u, v - dv) - SampleWrenHullZ(u, v + dv)) / (2.0f * dv);
    Vector3 n{-sideSign * dzdx, -sideSign * dzdy, sideSign};
    const float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (nLen > 1e-5f) { n.x /= nLen; n.y /= nLen; n.z /= nLen; }
    // Subtle baked studio illumination (visible on both software renderer & GPU).
    const Vector3 key{-0.45f, 0.52f, 0.72f};
    const float keyLen = std::sqrt(key.x * key.x + key.y * key.y + key.z * key.z);
    const float ndotl = std::max(0.0f, (n.x * key.x + n.y * key.y + n.z * key.z) / keyLen);
    const float shade = std::clamp(0.88f + 0.14f * ndotl + 0.03f * std::max(0.0f, n.y), 0.85f, 1.04f);
    const unsigned char c = static_cast<unsigned char>(std::round(std::min(1.0f, shade) * 255.0f));
    return MeshVert{{x, y, z}, {u, v}, n, ::Color{c, c, c, 255}};
  };

  auto emitIdxTri = [&](unsigned short i0, unsigned short i1, unsigned short i2, float expectedZSign) {
    const Vector3& a = verts[i0].pos;
    const Vector3& b = verts[i1].pos;
    const Vector3& c = verts[i2].pos;
    const Vector3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vector3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const float crossZ = ab.x * ac.y - ab.y * ac.x;
    if (std::fabs(crossZ) < 1e-8f) return;
    indices.push_back(i0);
    if (crossZ * expectedZSign >= 0.0f) {
      indices.push_back(i1);
      indices.push_back(i2);
    } else {
      indices.push_back(i2);
      indices.push_back(i1);
    }
  };

  // Precompute grid SDF values and shared interior vertex indices (keeps count ~16k).
  std::vector<float> gridSdf(static_cast<std::size_t>(kGridW) * kGridH, 0.0f);
  constexpr unsigned short kInvalidIdx = 0xFFFFu;
  std::vector<unsigned short> frontGridIdx(static_cast<std::size_t>(kGridW) * kGridH, kInvalidIdx);
  std::vector<unsigned short> backGridIdx(static_cast<std::size_t>(kGridW) * kGridH, kInvalidIdx);

  for (int gy = 0; gy < kGridH; ++gy) {
    const float v = static_cast<float>(gy) / static_cast<float>(kGridH - 1);
    for (int gx = 0; gx < kGridW; ++gx) {
      const float u = static_cast<float>(gx) / static_cast<float>(kGridW - 1);
      const float s = sampleSdf(u, v);
      const std::size_t idx = static_cast<std::size_t>(gy) * kGridW + gx;
      gridSdf[idx] = s;
      if (s >= 0.0f && verts.size() + 2 <= 65000) {
        frontGridIdx[idx] = static_cast<unsigned short>(verts.size());
        verts.push_back(evalVert(u, v, 1.0f, false));
        backGridIdx[idx] = static_cast<unsigned short>(verts.size());
        verts.push_back(evalVert(u, v, -1.0f, false));
      }
    }
  }

  for (int gy = 0; gy < kGridH - 1; ++gy) {
    const float v0 = static_cast<float>(gy) / static_cast<float>(kGridH - 1);
    const float v1 = static_cast<float>(gy + 1) / static_cast<float>(kGridH - 1);
    for (int gx = 0; gx < kGridW - 1; ++gx) {
      const float u0 = static_cast<float>(gx) / static_cast<float>(kGridW - 1);
      const float u1 = static_cast<float>(gx + 1) / static_cast<float>(kGridW - 1);
      struct Corner { float u, v, s; int gx, gy; };
      const Corner corners[4]{
          {u0, v0, gridSdf[static_cast<std::size_t>(gy) * kGridW + gx], gx, gy},
          {u1, v0, gridSdf[static_cast<std::size_t>(gy) * kGridW + gx + 1], gx + 1, gy},
          {u1, v1, gridSdf[static_cast<std::size_t>(gy + 1) * kGridW + gx + 1], gx + 1, gy + 1},
          {u0, v1, gridSdf[static_cast<std::size_t>(gy + 1) * kGridW + gx], gx, gy + 1},
      };
      // Clip cell polygon against s >= 0 (Sutherland-Hodgman on a quad -> 3..5 vertices).
      struct PolyPt { float u, v; bool contour; int gx, gy; };
      std::array<PolyPt, 6> poly{};
      int count = 0;
      for (int i = 0; i < 4; ++i) {
        const Corner& cur = corners[i];
        const Corner& nxt = corners[(i + 1) % 4];
        const bool curIn = cur.s >= 0.0f;
        const bool nxtIn = nxt.s >= 0.0f;
        if (curIn) { poly[static_cast<std::size_t>(count++)] = {cur.u, cur.v, false, cur.gx, cur.gy}; }
        if (curIn != nxtIn) {
          const float denom = cur.s - nxt.s;
          const float t = std::fabs(denom) > 1e-6f ? std::clamp(cur.s / denom, 0.0f, 1.0f) : 0.5f;
          poly[static_cast<std::size_t>(count++)] = {
              cur.u + (nxt.u - cur.u) * t,
              cur.v + (nxt.v - cur.v) * t,
              true, -1, -1,
          };
        }
      }
      if (count < 3) { continue; }
      for (const float side : {1.0f, -1.0f}) {
        const auto& gridIdx = side > 0.0f ? frontGridIdx : backGridIdx;
        std::array<unsigned short, 6> polyIdx{};
        bool ok = true;
        for (int k = 0; k < count; ++k) {
          const PolyPt& pt = poly[static_cast<std::size_t>(k)];
          if (!pt.contour && pt.gx >= 0) {
            const unsigned short shared = gridIdx[static_cast<std::size_t>(pt.gy) * kGridW + pt.gx];
            if (shared != kInvalidIdx) {
              polyIdx[static_cast<std::size_t>(k)] = shared;
              continue;
            }
          }
          if (verts.size() >= 65000) { ok = false; break; }
          polyIdx[static_cast<std::size_t>(k)] = static_cast<unsigned short>(verts.size());
          verts.push_back(evalVert(pt.u, pt.v, side, pt.contour));
        }
        if (!ok) { continue; }
        for (int k = 1; k < count - 1; ++k) {
          emitIdxTri(polyIdx[0], polyIdx[static_cast<std::size_t>(k)],
                     polyIdx[static_cast<std::size_t>(k + 1)], side);
        }
      }
    }
  }

  if (verts.empty() || indices.empty()) { return; }

  wrenMesh_.vertexCount = static_cast<int>(verts.size());
  wrenMesh_.triangleCount = static_cast<int>(indices.size() / 3);
  wrenMesh_.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(verts.size() * 3 * sizeof(float))));
  wrenMesh_.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned int>(verts.size() * 2 * sizeof(float))));
  wrenMesh_.normals = static_cast<float*>(MemAlloc(static_cast<unsigned int>(verts.size() * 3 * sizeof(float))));
  wrenMesh_.colors = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(verts.size() * 4 * sizeof(unsigned char))));
  wrenMesh_.indices = static_cast<unsigned short*>(MemAlloc(static_cast<unsigned int>(indices.size() * sizeof(unsigned short))));

  for (std::size_t i = 0; i < verts.size(); ++i) {
    wrenMesh_.vertices[i * 3 + 0] = verts[i].pos.x;
    wrenMesh_.vertices[i * 3 + 1] = verts[i].pos.y;
    wrenMesh_.vertices[i * 3 + 2] = verts[i].pos.z;
    wrenMesh_.texcoords[i * 2 + 0] = verts[i].uv.x;
    wrenMesh_.texcoords[i * 2 + 1] = verts[i].uv.y;
    wrenMesh_.normals[i * 3 + 0] = verts[i].nrm.x;
    wrenMesh_.normals[i * 3 + 1] = verts[i].nrm.y;
    wrenMesh_.normals[i * 3 + 2] = verts[i].nrm.z;
    wrenMesh_.colors[i * 4 + 0] = verts[i].col.r;
    wrenMesh_.colors[i * 4 + 1] = verts[i].col.g;
    wrenMesh_.colors[i * 4 + 2] = verts[i].col.b;
    wrenMesh_.colors[i * 4 + 3] = verts[i].col.a;
  }
  std::memcpy(wrenMesh_.indices, indices.data(), indices.size() * sizeof(unsigned short));
  UploadMesh(&wrenMesh_, false);

  wrenMat_ = LoadMaterialDefault();
  wrenMat_.maps[MATERIAL_MAP_DIFFUSE].texture = *texture;
  wrenMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

  Shader custom = LoadShaderFromMemory(kWren3DVertexShader, kWren3DFragmentShader);
  if (custom.id != 0 && custom.id != rlGetShaderIdDefault()) {
    wrenShader_ = custom;
    wrenShaderWarmLoc_ = GetShaderLocation(wrenShader_, "uWarmBias");
    wrenShaderLightLoc_ = GetShaderLocation(wrenShader_, "uKeyDir");
    wrenMat_.shader = wrenShader_;
  } else {
    if (custom.locs != nullptr && custom.id == 0) { UnloadShader(custom); }
    wrenShader_ = {};
  }
  wrenMeshBuilt_ = true;
}

bool ShellView::HasWrenExterior(const Simulation& sim) const {
  const Texture2D* texture = static_cast<const Texture2D*>(wrenExteriorArt_);
  if ((sim.State().shipClass != "wren" && sim.State().shipClass != "cargo-spine" &&
       sim.State().shipClass != "cargo-bulk" && sim.State().shipClass != "explorer-needle" &&
       sim.State().shipClass != "explorer-manta" && sim.State().shipClass != "yamato" && sim.State().shipClass != "arcadia") || texture == nullptr || texture->id == 0 ||
      texture->width <= 0 || texture->height <= 0) return false;
  // Selected exterior artwork remains visible throughout the voyage. The callouts
  // are built from sim.Rooms(), so only installed rooms are usable.
  return true;
}

ShellView::ExteriorLayout ShellView::BuildExteriorLayout(const Simulation& sim, const Area& stage) const {
  ExteriorLayout out;
  const int textSize = TypePx(10);
  out.lineHeight = std::max(static_cast<float>(textSize) + 3.0f, theme_.LineHeight());
  const float sideW = std::max(100.0f, std::min(245.0f, stage.w * 0.235f));
  std::vector<Room> rooms = sim.Rooms();
  std::stable_sort(rooms.begin(), rooms.end(), [](const Room& a, const Room& b) {
    return a.box.x < b.box.x;
  });
  // Keep the approximate fore/aft split, then order each side top-to-bottom
  // so leader lines do not needlessly cross one another.
  const auto middle = rooms.begin() + static_cast<std::ptrdiff_t>((rooms.size() + 1) / 2);
  const auto byHeight = [](const Room& a, const Room& b) {
    return a.box.y + a.box.h / 2.0 < b.box.y + b.box.h / 2.0;
  };
  std::stable_sort(rooms.begin(), middle, byHeight);
  std::stable_sort(middle, rooms.end(), byHeight);
  const std::size_t rows = (rooms.size() + 1) / 2;
  struct CalloutPlacement {
    bool left = false;
    std::size_t row = 0;
    float xInset = 0.0f;
    float width = 0.0f;
    float rowFraction = 0.0f;
  };
  std::vector<CalloutPlacement> placements;
  std::vector<std::vector<std::string>> labels;
  placements.reserve(rooms.size());
  labels.reserve(rooms.size());
  std::size_t maxLines = 1;
  for (std::size_t i = 0; i < rooms.size(); ++i) {
    const bool left = i < rows;
    const std::size_t row = left ? i : i - rows;
    const float rowT = rows > 1 ? static_cast<float>(row) / static_cast<float>(rows - 1) : 0.0f;
    CalloutPlacement placement{left, row, 0.0f, sideW, 0.69f * rowT};
    if (rows == 3) {
      // Match the reference composition: each lower callout steps inward by
      // a different amount on each side instead of forming two rigid columns.
      static constexpr float leftY[] = {0.0f, 0.354f, 0.542f};
      static constexpr float rightY[] = {0.0f, 0.306f, 0.527f};
      static constexpr float leftWidth[] = {1.0f, 0.90f, 0.78f};
      static constexpr float rightWidth[] = {1.0f, 0.93f, 0.80f};
      const std::size_t slot = std::min<std::size_t>(row, 2);
      const float baseWidth = std::max(1.0f, sideW - 8.0f);
      placement.width = baseWidth * (left ? leftWidth[slot] : rightWidth[slot]);
      placement.xInset = baseWidth - placement.width;
      placement.rowFraction = left ? leftY[slot] : rightY[slot];
    }
    placements.push_back(placement);
    labels.push_back(WrapText(rooms[i].name, std::max(1.0f, placement.width - 16.0f),
                              textSize, SelectFont(textSize, false)));
    maxLines = std::max(maxLines, labels.back().size());
  }
  const float rowH = static_cast<float>(maxLines) * out.lineHeight + 12.0f;
  float bodyH = std::max(Metrics::kHomeStageMinH, stage.h);
  if (rows > 3) {
    bodyH = std::max(bodyH, (rowH + 8.0f) * static_cast<float>(rows - 1) / 0.69f);
  }
  const float bodyY = stage.y;
  out.height = bodyH;
  const Texture2D* texture = static_cast<const Texture2D*>(wrenExteriorArt_);
  const float aspect = texture && texture->width > 0 ? static_cast<float>(texture->height) / texture->width : 420.0f / 1340.0f;
  const float imageW = std::max(1.0f, stage.w - sideW * 2.0f - 32.0f);
  const float imageH = std::min(imageW * aspect, bodyH * 0.75f);
  const float fittedW = imageH / aspect;
  out.image = {stage.x + (stage.w - fittedW) / 2.0f,
               bodyY + bodyH * 0.40f - imageH / 2.0f, fittedW, imageH};
  for (std::size_t i = 0; i < rooms.size(); ++i) {
    const CalloutPlacement& placement = placements[i];
    ExteriorCallout item;
    item.id = rooms[i].id;
    item.lines = std::move(labels[i]);
    item.left = placement.left;
    const float boxX = placement.left
        ? stage.x + 2.0f + placement.xInset
        : stage.Right() - 2.0f - placement.width;
    item.box = {boxX, bodyY + bodyH * placement.rowFraction,
                placement.width, rowH - 4.0f};
    // Conceptual anchor only: it ties a callout to the exterior visually and
    // does not claim the supplied sketch is an engineering deck layout.
    const float nx = std::clamp(static_cast<float>((rooms[i].box.x + rooms[i].box.w * 0.5) / 600.0), 0.0f, 1.0f);
    const float ny = std::clamp(static_cast<float>((rooms[i].box.y + rooms[i].box.h * 0.5) / 365.0), 0.0f, 1.0f);
    const float u = 0.07f + 0.86f * nx;
    const float v = sim.State().shipClass == "yamato" || sim.State().shipClass == "arcadia"
        ? 0.68f + 0.18f * ny : 0.30f + 0.42f * ny;
    item.normUV = Vector2{u, v};
    item.localZ = SampleWrenHullZ(u, v);
    item.anchor = {out.image.x + out.image.w * u,
                   out.image.y + out.image.h * v};
    out.callouts.push_back(std::move(item));
  }
  return out;
}

void ShellView::PaintWrenExterior(const Simulation& sim) {
  const Texture2D* texture = static_cast<const Texture2D*>(wrenExteriorArt_);
  const Palette& p = theme_.palette;
  const Area image = exterior_.image;

  EnsureWrenMesh();

  std::vector<Vector2> liveAnchors;
  liveAnchors.reserve(exterior_.callouts.size());

  const bool reduced = sim.State().reducedMotion;
  const Task* travel = sim.FindOngoing("travel");
  const bool inFlight = travel != nullptr && travel->state == "running";

  if (wrenMeshBuilt_ && width_ > 1.0f && height_ > 1.0f) {
    const float t = reduced ? 0.0f : static_cast<float>(frameTimeMs_ * 0.001);
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    float bobX = 0.0f;
    float bobY = 0.0f;
    if (!reduced) {
      if (inFlight) {
        yawDeg = 6.5f * std::sin(t * 0.34f) + 1.2f * std::sin(t * 0.16f);
        pitchDeg = 3.2f * std::sin(t * 0.45f + 0.6f) + 0.8f;
        rollDeg = 2.9f * std::cos(t * 0.28f) + 0.5f * std::sin(t * 0.65f);
        bobX = 2.8f * std::sin(t * 0.24f);
        bobY = 2.2f * std::sin(t * 0.55f) + 0.15f * std::sin(t * 14.0f);
      } else {
        yawDeg = 4.5f * std::sin(t * 0.25f) + 0.9f * std::sin(t * 0.12f);
        pitchDeg = 2.8f * std::sin(t * 0.34f + 0.9f);
        rollDeg = 1.8f * std::cos(t * 0.21f + 0.4f);
        bobX = 1.9f * std::sin(t * 0.18f);
        bobY = 1.8f * std::sin(t * 0.42f + 0.5f);
      }
    }

    const float texAspect = static_cast<float>(texture->width) / static_cast<float>(texture->height);
    constexpr float kCamDist = 12.0f;
    constexpr float kFovDeg = 26.0f;
    constexpr double kZNear = 0.5;
    constexpr double kZFar = 40.0;
    const float fovRad = kFovDeg * DEG2RAD;
    const float halfViewH = kCamDist * std::tan(fovRad * 0.5f);
    const float viewAspect = width_ / height_;
    const float shipWorldH = (image.h / height_) * 2.0f * halfViewH;

    const float screenCX = image.x + image.w * 0.5f + bobX;
    const float screenCY = image.y + image.h * 0.5f - contentScroll_ + bobY;
    const float ndcX = (2.0f * screenCX / width_) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenCY / height_);

    const double top0 = kZNear * std::tan(static_cast<double>(fovRad) * 0.5);
    const double right0 = top0 * static_cast<double>(viewAspect);
    const double shiftX = -static_cast<double>(ndcX) * right0;
    const double shiftY = -static_cast<double>(ndcY) * top0;
    const double left = -right0 + shiftX;
    const double right = right0 + shiftX;
    const double bottom = -top0 + shiftY;
    const double top = top0 + shiftY;

    Camera3D cam{};
    cam.position = Vector3{0.0f, 0.0f, kCamDist};
    cam.target = Vector3{0.0f, 0.0f, 0.0f};
    cam.up = Vector3{0.0f, 1.0f, 0.0f};
    cam.fovy = kFovDeg;
    cam.projection = CAMERA_PERSPECTIVE;

    const Matrix modelMat = BuildScaleRotateXYZ(
        shipWorldH, pitchDeg * DEG2RAD, yawDeg * DEG2RAD, rollDeg * DEG2RAD);
    const Matrix viewMat = BuildTranslateZ(-kCamDist);
    const Matrix projMat = BuildOffAxisFrustumMatrix(left, right, bottom, top, kZNear, kZFar);
    const Matrix mvp = MulColMajor(projMat, MulColMajor(viewMat, modelMat));

    auto projectUv = [&](float u, float v, float z) -> Vector2 {
      const Vector3 local{(u - 0.5f) * texAspect, 0.5f - v, z};
      Vector2 sp = ProjectPointMvp(local, mvp, width_, height_);
      sp.y += contentScroll_;
      return sp;
    };

    if (inFlight && !reduced) {
      // Soft thruster exhaust aura behind the three aft nozzles while traveling.
      const float pulse = 0.65f + 0.35f * std::sin(t * 9.5f);
      static constexpr float kNozzleU[3] = {0.055f, 0.018f, 0.055f};
      static constexpr float kNozzleV[3] = {0.28f, 0.49f, 0.69f};
      static constexpr float kNozzleR[3] = {11.0f, 16.0f, 11.0f};
      for (int i = 0; i < 3; ++i) {
        const Vector2 aft = projectUv(kNozzleU[i], kNozzleV[i], 0.0f);
        const Vector2 plume{aft.x - 10.0f, aft.y};
        DrawCircleGradient(plume, kNozzleR[i] * (0.90f + 0.18f * pulse),
                           Fade(::Color{245, 188, 92, 255}, 0.32f * pulse),
                           Fade(::Color{255, 224, 156, 255}, 0.0f));
        DrawCircleGradient(Vector2{aft.x - 5.0f, aft.y},
                           kNozzleR[i] * 0.48f * (0.90f + 0.14f * pulse),
                           Fade(::Color{255, 238, 194, 255}, 0.38f * pulse),
                           Fade(::Color{255, 224, 156, 255}, 0.0f));
      }
    }

    if (wrenShader_.id != 0) {
      const RadiationTelemetry rad = ComputeRadiationTelemetry(sim);
      if (wrenShaderWarmLoc_ >= 0) {
        SetShaderValue(wrenShader_, wrenShaderWarmLoc_, &rad.warmBias, SHADER_UNIFORM_FLOAT);
      }
      if (wrenShaderLightLoc_ >= 0) {
        const float keyDir[3]{-0.45f + 0.25f * rad.waveDirX,
                              0.55f - 0.15f * rad.waveDirY,
                              0.75f};
        SetShaderValue(wrenShader_, wrenShaderLightLoc_, keyDir, SHADER_UNIFORM_VEC3);
      }
    }

    EndMode2D();
    BeginScissorMode(static_cast<int>(layout_.content.x), static_cast<int>(layout_.content.y),
                     std::max(0, static_cast<int>(layout_.content.w)),
                     std::max(0, static_cast<int>(layout_.content.h)));
    BeginMode3D(cam);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlFrustum(left, right, bottom, top, kZNear, kZFar);
    rlMatrixMode(RL_MODELVIEW);
    rlEnableBackfaceCulling();
    DrawMesh(wrenMesh_, wrenMat_, modelMat);
    EndMode3D();
    EndScissorMode();
    Camera2D scrollCam{{0.0f, 0.0f}, {0.0f, contentScroll_}, 0.0f, 1.0f};
    BeginMode2D(scrollCam);

    for (const ExteriorCallout& item : exterior_.callouts) {
      const float z = item.localZ > 0.0f ? item.localZ : SampleWrenHullZ(item.normUV.x, item.normUV.y);
      liveAnchors.push_back(projectUv(item.normUV.x, item.normUV.y, z));
    }
    static constexpr float kHullUV[][2] = {
        {0.03f, 0.49f}, {0.07f, 0.30f}, {0.07f, 0.68f}, {0.23f, 0.26f},
        {0.25f, 0.72f}, {0.43f, 0.16f}, {0.43f, 0.81f}, {0.57f, 0.30f},
        {0.58f, 0.68f}, {0.77f, 0.34f}, {0.77f, 0.63f}, {0.93f, 0.49f}};
    for (const auto& uv : kHullUV) {
      const Vector2 sp = projectUv(uv[0], uv[1], SampleWrenHullZ(uv[0], uv[1]));
      PushFlarePt(FlareTarget::ShipPoint, sp.x, sp.y - contentScroll_, 0.95f);
    }
  } else {
    DrawTexturePro(*texture,
                   Rectangle{0, 0, static_cast<float>(texture->width), static_cast<float>(texture->height)},
                   Rectangle{image.x, image.y, image.w, image.h}, Vector2{0, 0}, 0, WHITE);
    for (const ExteriorCallout& item : exterior_.callouts) {
      liveAnchors.push_back(item.anchor);
    }
    static constexpr float kHullUV[][2] = {
        {0.03f, 0.49f}, {0.07f, 0.30f}, {0.07f, 0.68f}, {0.23f, 0.26f},
        {0.25f, 0.72f}, {0.43f, 0.16f}, {0.43f, 0.81f}, {0.57f, 0.30f},
        {0.58f, 0.68f}, {0.77f, 0.34f}, {0.77f, 0.63f}, {0.93f, 0.49f}};
    for (const auto& uv : kHullUV) {
      PushFlarePt(FlareTarget::ShipPoint, image.x + uv[0] * image.w,
                  image.y + uv[1] * image.h - contentScroll_, 0.95f);
    }
  }

  const auto hovered = [&](const ExteriorCallout& item) {
    return layout_.content.Contains(mouseX_, mouseY_) && item.box.Contains(mouseX_, mouseY_ + contentScroll_);
  };
  // Lines first and labels last: another room's leader can never paint over
  // a label. The highlights are decorative; neither they nor hover tick time.
  for (std::size_t i = 0; i < exterior_.callouts.size(); ++i) {
    const ExteriorCallout& item = exterior_.callouts[i];
    const Vector2 anchor = i < liveAnchors.size() ? liveAnchors[i] : item.anchor;
    const bool hover = hovered(item);
    const Color line = ToRay(hover ? p.coffeeDark : p.borderDark);
    const float edgeX = item.left ? item.box.Right() : item.box.x;
    const float elbowX = item.left ? image.x - 8.0f : image.Right() + 8.0f;
    const Vector2 from{edgeX, item.box.y + item.box.h * 0.5f};
    const Vector2 elbow{elbowX, from.y};
    DrawLineEx(from, elbow, 1.0f, line);
    DrawLineEx(elbow, anchor, 1.0f, line);
    DrawCircleV(anchor, hover ? 4.0f : 2.5f, line);
  }
  for (std::size_t i = 0; i < exterior_.callouts.size(); ++i) {
    const ExteriorCallout& item = exterior_.callouts[i];
    const Vector2 anchor = i < liveAnchors.size() ? liveAnchors[i] : item.anchor;
    const bool hover = hovered(item);
    if (hover) {
      const float pulse = reduced ? 0.20f :
          0.15f + 0.05f * static_cast<float>(0.5 + 0.5 * std::sin(frameTimeMs_ / 650.0));
      DrawEllipse(static_cast<int>(anchor.x), static_cast<int>(anchor.y),
                  std::max(12.0f, image.w * 0.045f), std::max(8.0f, image.h * 0.09f),
                  Fade(::Color{255, 224, 156, 255}, pulse));
    }
    PaintChamferBoxRay(item.box, kChamferSmall, ToRay(hover ? p.panel3 : p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
    float y = item.box.y + 6.0f;
    for (const auto& lineText : item.lines) {
      Text(lineText.c_str(), static_cast<int>(item.box.x + 9), static_cast<int>(y), TypePx(10), ToRay(p.ink));
      y += exterior_.lineHeight;
    }
  }
}

void ShellView::PaintIdle(Simulation& sim) {
  const Theme& theme = theme_;
  const auto hovered = [&](const Area& box) {
    return layout_.content.Contains(mouseX_, mouseY_) &&
           box.Contains(mouseX_, mouseY_ + contentScroll_);
  };

  // ---- stat and info rails ----------------------------------------------
  for (const RailCell& cell : idle_.stats) {
    PaintChamferBoxRay(cell.box, kChamferCard, ToRay(hovered(cell.box) ? theme.palette.panel3 : theme.palette.panel),
                       ToRay(theme.palette.borderDark), ToRay(theme.palette.border));
    // Tighter spacing keeps the four gauges shallower without shrinking type.
    const int labelSize = TypePx(11);
    const int valueSize = TypePx(20);
    const int unitSize = TypePx(10);
    const float labelY = cell.box.y + 5.0f;
    const float valueY = cell.box.y + 5.0f + labelSize;
    Text(cell.label.c_str(), static_cast<int>(cell.box.x + 8.0f),
         static_cast<int>(labelY), labelSize, ToRay(theme.palette.muted));
    Text(cell.value.c_str(), static_cast<int>(cell.box.x + 8.0f),
         static_cast<int>(valueY), valueSize, ToRay(theme.palette.ink), true);
    if (!cell.unit.empty()) {
      const float valueW = TextWidth(cell.value.c_str(), valueSize, true);
      const float unitW = TextWidth(cell.unit.c_str(), unitSize, false);
      const float unitX = cell.box.x + 8.0f + valueW + 4.0f;
      if (unitX + unitW <= cell.box.Right() - 8.0f) {
        Text(cell.unit.c_str(), static_cast<int>(unitX), static_cast<int>(valueY),
             unitSize, ToRay(theme.palette.muted));
      }
    }
    const float bar = std::max(1.0f, cell.box.w - 16.0f);
    DrawRectangle(static_cast<int>(cell.box.x + 8.0f),
                  static_cast<int>(cell.box.Bottom() - 9.0f), static_cast<int>(bar), 4,
                  ToRay(theme.palette.panel3));
    DrawRectangle(static_cast<int>(cell.box.x + 8.0f),
                  static_cast<int>(cell.box.Bottom() - 9.0f),
                  static_cast<int>(bar * std::max(0.0, std::min(100.0, cell.reading)) / 100.0),
                  4, HexColor(cell.colour, ToRay(theme.palette.coffee)));
  }

  if (HasWrenExterior(sim)) {
    PaintWrenExterior(sim);
  }
  // ---- info rail, navigation and dock strip -----------------------------
  auto paintStrip = [this, &theme, &hovered](const Strip& strip) {
    PaintChamferBoxRay(strip.box, kChamferCard, ToRay(hovered(strip.box) ? theme.palette.panel3 : theme.palette.panel),
                       ToRay(theme.palette.borderDark), ToRay(theme.palette.border));
    const float mainY = strip.box.y + 8.0f + TypePx(11) + 6.0f;
    Text(strip.label.c_str(), static_cast<int>(strip.box.x + 10.0f),
         static_cast<int>(strip.box.y + 8.0f), TypePx(11), ToRay(theme.palette.muted));
    const auto mainLines = WrapText(strip.main, std::max(1.0f, strip.box.w - 20.0f),
                                    TypePx(13), SelectFont(TypePx(13), false));
    float y = mainY;
    for (const auto& line : mainLines) {
      Text(line.c_str(), static_cast<int>(strip.box.x + 10.0f), static_cast<int>(y),
           TypePx(13), ToRay(theme.palette.ink));
      y += TypePx(13) + 3.0f;
    }
  };

  const int labelPx = theme.HomeInfoLabelPx();
  const int valuePx = theme.HomeInfoValuePx();
  for (std::size_t i = 0; i < idle_.infos.size(); ++i) {
    const RailCell& cell = idle_.infos[i];
    const float width = std::max(0.0f, cell.box.w);
    PaintChamferBoxRay(Area{cell.box.x, cell.box.y, width, cell.box.h}, kChamferCard,
                       ToRay(hovered(cell.box) ? theme.palette.panel3 : theme.palette.panel),
                       ToRay(theme.palette.borderDark), ToRay(theme.palette.border));
    const float textWidth = std::max(1.0f, width - 22.0f);
    float y = cell.box.y + 8.0f;
    for (const auto& line : WrapText(cell.label, textWidth, labelPx, SelectFont(labelPx, false))) {
      Text(line.c_str(), static_cast<int>(cell.box.x + 11.0f), static_cast<int>(y),
           labelPx, ToRay(theme.palette.muted));
      y += labelPx + 3.0f;
    }
    y += 3.0f;
    for (const auto& line : WrapText(cell.value, textWidth, valuePx, SelectFont(valuePx, false))) {
      Text(line.c_str(), static_cast<int>(cell.box.x + 11.0f), static_cast<int>(y),
           valuePx, ToRay(theme.palette.ink));
      y += valuePx + 3.0f;
    }
  }
  paintStrip(idle_.nav);
  for (const HomeEventCard& event : idle_.events) {
    const Area& box = event.box;
    const bool actionable = event.station || !event.id.empty();
    PaintChamferBoxRay(box, kChamferCard, ToRay(actionable && hovered(box) ? theme.palette.panel3 : theme.palette.panel),
                       ToRay(theme.palette.borderDark), ToRay(theme.palette.border));
    Text(event.heading.c_str(), static_cast<int>(box.x + 10.0f),
         static_cast<int>(box.y + 8.0f), TypePx(11), ToRay(theme.palette.muted));
    const auto lines = WrapText(event.label, std::max(1.0f, box.w - 20.0f),
                                TypePx(13), SelectFont(TypePx(13), false));
    float eventY = box.y + 8.0f + TypePx(11) + 6.0f;
    for (const std::string& line : lines) {
      Text(line.c_str(), static_cast<int>(box.x + 10.0f), static_cast<int>(eventY),
           TypePx(13), ToRay(theme.palette.ink));
      eventY += TypePx(13) + 3.0f;
    }
  }

  // ---- feed --------------------------------------------------------------
  const float feedHeaderHeight = static_cast<float>(TypePx(11)) + 14.0f;
  PaintChamferBoxRay(idle_.feedBox, kChamferPanel, ToRay(theme.palette.panel),
                       ToRay(theme.palette.borderDark), ToRay(theme.palette.border));
  Text(idle_.feedHead.c_str(), static_cast<int>(idle_.feedBox.x + 10.0f),
       static_cast<int>(idle_.feedBox.y + 8.0f), TypePx(11), ToRay(theme.palette.coffee));
  const bool trackFeed = paintingContent_;
  paintingContent_ = false;
  feedBox_ = idle_.feedBox;
  std::vector<std::string> log;
  for (auto it = sim.State().feed.rbegin(); it != sim.State().feed.rend(); ++it)
    log.push_back(it->stamp.empty() ? it->text : it->stamp + "  " + it->text);
  if (feedRevision_ != sim.FeedRevision() || feedSeen_ != log) {
    feedScroll_ = 0;
    draggingFeed_ = false;
    feedRevision_ = sim.FeedRevision();
    feedSeen_ = log;
  }
  const int size = TypePx(12);
  const float lineH = std::max(theme.LineHeight(), static_cast<float>(size) + 4);
  std::vector<std::string> rows;
  for (const auto& entry : log) {
    const auto wrapped = WrapText(entry, std::max(1.0f, feedBox_.w - 32), size, SelectFont(size, false));
    rows.insert(rows.end(), wrapped.begin(), wrapped.end());
  }
  const float feedContentTop = feedBox_.y + feedHeaderHeight;
  const float feedContentHeight = std::max(1.0f, feedBox_.h - feedHeaderHeight - 5.0f);
  const float visibleH = feedContentHeight;
  feedMaxScroll_ = std::max(0.0f, rows.size() * lineH - visibleH);
  feedScroll_ = std::clamp(feedScroll_, 0.0f, feedMaxScroll_);
  BeginScissorMode(static_cast<int>(feedBox_.x + kDoubleInset),
                   static_cast<int>(feedContentTop - contentScroll_),
                   std::max(0, static_cast<int>(feedBox_.w - 2.0f * kDoubleInset)),
                   std::max(0, static_cast<int>(feedContentHeight)));
  float y = feedContentTop + 2.0f - feedScroll_;
  for (const auto& row : rows) {
    if (y + lineH > feedContentTop && y < feedContentTop + feedContentHeight) {
      Text(row.c_str(), static_cast<int>(feedBox_.x + 10), static_cast<int>(y), size, ToRay(theme.palette.ink));
    }
    y += lineH;
  }
  feedThumb_ = {};
  if (feedMaxScroll_ > 0) {
    const float trackH = feedContentHeight;
    const float thumbH = std::min(trackH, std::max(18.0f, trackH * visibleH / (visibleH + feedMaxScroll_)));
    feedThumb_ = {feedBox_.Right() - 13, feedContentTop + (trackH - thumbH) * feedScroll_ / feedMaxScroll_, 8, thumbH};
    DrawRectangle(static_cast<int>(feedThumb_.x), static_cast<int>(feedContentTop), 8,
                  static_cast<int>(trackH), ToRay(theme.palette.panel3));
    DrawRectangle(static_cast<int>(feedThumb_.x), static_cast<int>(feedThumb_.y), 8,
                  static_cast<int>(thumbH), ToRay(theme.palette.borderDark));
  }
  EndScissorMode();
  MaskChamferInnerCornersRay(idle_.feedBox, kChamferPanel, ToRay(theme.palette.panel));
  PaintChamferDoubleLinesRay(idle_.feedBox, kChamferPanel, ToRay(theme.palette.borderDark), ToRay(theme.palette.border));
  paintingContent_ = trackFeed;
  contentPaintBottom_ = std::max(contentPaintBottom_, feedBox_.Bottom());
  (void)sim;
}

void ShellView::PaintPanel(const NavPanel& panel, const Area& box) {
  const Palette& p = theme_.palette;
  PaintChamferBoxRay(box, kChamferPanel, ToRay(p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
  BeginScissorMode(static_cast<int>(box.x + kDoubleInset),
                   static_cast<int>(box.y + kDoubleInset - contentScroll_),
                   std::max(0, static_cast<int>(box.w - 2.0f * kDoubleInset)),
                   std::max(0, static_cast<int>(box.h - 2.0f * kDoubleInset)));
  const int small = TypePx(10);
  const float line = TypeStep(14.0f);
  Text(panel.title.c_str(), static_cast<int>(box.x + 10.0f),
       static_cast<int>(box.y + 7.0f), TypePx(11), ToRay(p.coffee));
  float y = box.y + TypeStep(26.0f);
  const float textW = std::max(1.0f, box.w - 20.0f);
    auto paintWrapped = [&](const std::string& value, float x, float width,
                            ::Color color) {
      const std::vector<std::string> rows =
          WrapText(value, std::max(1.0f, width), small, SelectFont(small, false));
      for (const std::string& row : rows) {
      if (y + line > box.Bottom() - 5.0f) { break; }
      Text(row.c_str(), static_cast<int>(x), static_cast<int>(y), small, color);
      y += line;
    }
  };
  for (const PanelLine& item : panel.lines) {
    if (y >= box.Bottom() - 5.0f) { break; }
    switch (item.kind) {
      case PanelLine::Kind::Name:
        for (const auto& row : WrapText(item.value, textW, TypePx(18), SelectFont(TypePx(18), false))) {
          Text(row.c_str(), static_cast<int>(box.x + 10.0f), static_cast<int>(y), TypePx(18), ToRay(p.ink));
          y += std::max(TypeStep(23.0f), static_cast<float>(TypePx(18)) + 4.0f);
        }
        break;
      case PanelLine::Kind::Sub:
        paintWrapped(item.value, box.x + 10.0f, textW, ToRay(p.muted));
        y += TypeStep(2.0f);
        break;
      case PanelLine::Kind::Bar: {
        const float w = textW;
        DrawRectangle(static_cast<int>(box.x + 10.0f), static_cast<int>(y),
                      static_cast<int>(w), 5, ToRay(p.panel3));
        DrawRectangle(static_cast<int>(box.x + 10.0f), static_cast<int>(y),
          static_cast<int>(w * std::max(0.0, std::min(100.0, item.pct)) / 100.0),
          5, HexColor(item.tone, ToRay(p.coffee)));
        y += TypeStep(11.0f);
        break;
      }
      case PanelLine::Kind::Dim:
      case PanelLine::Kind::Note:
        paintWrapped(item.value, box.x + 10.0f, textW, ToRay(p.muted));
        y += TypeStep(2.0f);
        break;
      case PanelLine::Kind::Field: {
        const float x = box.x + 10.0f;
        const float keyW = TextWidth(item.key.c_str(), small);
        const float valueW = TextWidth(item.value.c_str(), small);
        const float roomW = textW - keyW - TypeStep(8.0f);
        Text(item.key.c_str(), static_cast<int>(x), static_cast<int>(y), small, ToRay(p.muted));
        if (valueW <= roomW) {
          Text(item.value.c_str(), static_cast<int>(box.Right() - 10.0f - valueW),
               static_cast<int>(y), small, ToRay(p.ink));
          y += line;
        } else {
          y += line;
          paintWrapped(item.value, x + TypeStep(6.0f), textW - TypeStep(6.0f), ToRay(p.ink));
        }
        break;
      }
    }
  }
  for (const ProgressButton& button : panel.buttons) {
    y += TypeStep(4.0f);
    const auto btnRows = WrapText(button.text, std::max(1.0f, textW - 18.0f),
                                  small, SelectFont(small, false));
    const float btnH = std::max(TypeStep(26.0f),
                                static_cast<float>(btnRows.size()) * line + TypeStep(10.0f));
    const Area btnBox{box.x + 10.0f, y, textW, btnH};
    if (btnBox.Bottom() <= box.Bottom() - 4.0f) {
      PaintProgressSurface(btnBox, button, p);
      float textY = btnBox.y + (btnH - static_cast<float>(btnRows.size()) * line) * 0.5f;
      const ::Color ink = ToRay(button.state == ButtonState::Empty || button.state == ButtonState::Off
                                ? p.muted : p.ink);
      for (const std::string& row : btnRows) {
        Text(row.c_str(),
             static_cast<int>(btnBox.x + (btnBox.w - TextWidth(row.c_str(), small)) * 0.5f),
             static_cast<int>(textY), small, ink);
        textY += line;
      }
      taskButtons_.push_back({btnBox, button.taskId});
    }
    y = btnBox.Bottom() + TypeStep(4.0f);
  }
  EndScissorMode();
  MaskChamferInnerCornersRay(box, kChamferPanel, ToRay(p.panel));
  PaintChamferDoubleLinesRay(box, kChamferPanel, ToRay(p.borderDark), ToRay(p.border));
}

float ShellView::MeasurePanelHeight(const NavPanel& panel, float width) const {
  const float textWidth = std::max(1.0f, width - 20.0f);
  const int textPx = TypePx(10);
  const float lineHeight = TypeStep(14.0f);
  const auto wrappedRows = [this, textPx](const std::string& text, float available) {
    return static_cast<float>(WrapText(text, std::max(1.0f, available), textPx,
                                       SelectFont(textPx, false)).size());
  };
  float y = TypeStep(26.0f);
  for (const PanelLine& item : panel.lines) {
    switch (item.kind) {
      case PanelLine::Kind::Name:
        y += WrapText(item.value, textWidth, TypePx(18), SelectFont(TypePx(18), false)).size() *
             std::max(TypeStep(23.0f), static_cast<float>(TypePx(18)) + 4.0f);
        break;
      case PanelLine::Kind::Sub:
      case PanelLine::Kind::Dim:
      case PanelLine::Kind::Note:
        y += wrappedRows(item.value, textWidth) * lineHeight + TypeStep(2.0f);
        break;
      case PanelLine::Kind::Bar:
        y += TypeStep(11.0f);
        break;
      case PanelLine::Kind::Field: {
        const float keyWidth = TextWidth(item.key.c_str(), textPx);
        const float valueWidth = TextWidth(item.value.c_str(), textPx);
        const float valueRoom = textWidth - keyWidth - TypeStep(8.0f);
        y += lineHeight;
        if (valueWidth > valueRoom) {
          y += wrappedRows(item.value, textWidth - TypeStep(6.0f)) * lineHeight;
        }
        break;
      }
    }
  }
  for (const ProgressButton& button : panel.buttons) {
    const auto btnRows = WrapText(button.text, std::max(1.0f, textWidth - 18.0f),
                                  textPx, SelectFont(textPx, false));
    const float btnH = std::max(TypeStep(26.0f),
                                static_cast<float>(btnRows.size()) * lineHeight + TypeStep(10.0f));
    y += TypeStep(4.0f) + btnH + TypeStep(4.0f);
  }
  return y + lineHeight + 6.0f;
}

// Native painting of the literal scene styles in HTA lines 918–1199.
// Geometry stays in the existing part models; CSS borders are inside the box.
bool ShellView::PaintScenePart(const MachinePart& part, const Area& origin) {
  const SceneStyle style = SceneStyleFor(part.cls);
  const bool night = theme_.id.find("-night") != std::string::npos &&
      (part.cls.rfind("mach-", 0) == 0 || part.cls.rfind("o2-", 0) == 0 ||
       part.cls.rfind("mess-", 0) == 0);
  if (!*style.cls) return false;
  const float x = origin.x + part.box.x, y = origin.y + part.box.y;
  const float w = std::max(0.0f, part.box.w), h = std::max(0.0f, part.box.h);
  const Rectangle box{x, y, w, h};
  const bool tonedFill = part.text.empty() && !part.tone.empty();
  const std::string background = tonedFill ? part.tone : style.background;
  const float rounding = std::min(1.0f, 2.0f * std::max(0, style.radius) / std::max(1.0f, std::min(w, h)));
  if (!background.empty() && w > 0 && h > 0) {
    const Color ink = night
        ? (tonedFill ? ColorBrightness(HexColor(background, ToRay(theme_.palette.sage)), -0.35f)
                     : ColorTint(HexColor(background, ToRay(theme_.palette.panel)),
                                 ToRay(theme_.palette.panel3)))
        : HexColor(background, ToRay(theme_.palette.panel));
    if (style.radius == -1) DrawEllipse(static_cast<int>(x + w / 2), static_cast<int>(y + h / 2), w / 2, h / 2, ink);
    else if (style.radius > 0) DrawRectangleRounded(box, rounding, 12, ink);
    else DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), ink);
  }
  if (style.width > 0 && *style.border && w > 0 && h > 0) {
    const Color ink = night ? ToRay(theme_.palette.border)
                            : HexColor(style.border, ToRay(theme_.palette.border));
    const float thick = static_cast<float>(style.width);
    if (style.radius == -1 && w == h) {
      const Vector2 center{x + w / 2, y + h / 2};
      if (style.dashed) {
        for (float angle = 0; angle < 360; angle += 12) DrawRing(center, std::max(0.0f, w / 2 - thick), w / 2, angle, angle + 6, 3, ink);
      } else DrawRing(center, std::max(0.0f, w / 2 - thick), w / 2, 0, 360, 64, ink);
    } else if (style.radius > 0 && !style.dashed && style.sides == 15) {
      DrawRectangleRoundedLinesEx(box, rounding, 12, thick, ink);
    } else {
      auto edge = [&](float ax, float ay, float bx, float by) {
        const float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0) return;
        if (!style.dashed) { DrawLineEx({ax, ay}, {bx, by}, thick, ink); return; }
        for (float pos = 0; pos < length; pos += 6) {
          const float end = std::min(pos + 3, length);
          DrawLineEx({ax + dx * pos / length, ay + dy * pos / length},
                     {ax + dx * end / length, ay + dy * end / length}, thick, ink);
        }
      };
      const float d = thick / 2;
      if (style.sides & 1) edge(x + d, y + d, x + w - d, y + d);
      if (style.sides & 2) edge(x + w - d, y + d, x + w - d, y + h - d);
      if (style.sides & 4) edge(x + d, y + h - d, x + w - d, y + h - d);
      if (style.sides & 8) edge(x + d, y + d, x + d, y + h - d);
    }
  }
  if (!part.text.empty()) {
    const int size = TypePx(style.fontPx);
    const std::string ink = part.tone.empty() ? style.ink : part.tone;
    const std::size_t bar = part.text.find('|');
    if (bar != std::string::npos && std::string(style.cls) == "mach-stat") {
      // A statistics row is written "label|value". The label is set at the left
      // edge in the row's own style, the value at the right edge in mach-sv.
      const SceneStyle reading = SceneStyleFor("mach-sv");
      const std::string label = part.text.substr(0, bar);
      const std::string value = part.text.substr(bar + 1);
      const int valuePx = TypePx(reading.fontPx);
      Text(label.c_str(), static_cast<int>(x), static_cast<int>(y), size,
           (night ? (part.tone.empty() ? ToRay(theme_.palette.ink)
                                    : ColorBrightness(HexColor(ink, ToRay(theme_.palette.ink)), 0.35f))
                  : HexColor(ink, ToRay(theme_.palette.ink))), style.mono);
      Text(value.c_str(),
           static_cast<int>(x + w - TextWidth(value.c_str(), valuePx, reading.mono)),
           static_cast<int>(y), valuePx,
           (night ? ToRay(theme_.palette.ink) : HexColor(reading.ink, ToRay(theme_.palette.ink))), reading.mono);
      return true;
    }
    float tx = x;
    if (style.align) {
      const float space = w - TextWidth(part.text.c_str(), size, style.mono);
      tx += style.align == 1 ? space / 2 : space;
    }
    Text(part.text.c_str(), static_cast<int>(tx), static_cast<int>(y), size,
         (night ? (part.tone.empty() ? ToRay(theme_.palette.ink)
                                    : ColorBrightness(HexColor(ink, ToRay(theme_.palette.ink)), 0.35f))
                  : HexColor(ink, ToRay(theme_.palette.ink))), style.mono);
  }
  return true;
}

namespace {
// How much larger than drawn a stage is painted. A stage laid out for one
// reading size grows with the size in use; every other stage is painted 1:1.
float StageScale(const TaskStage& stage, const Theme& theme) {
  return stage.typePx > 0.0f ? theme.FontPx() / stage.typePx : 1.0f;
}

Color StageMix(Color a, Color b, float t) {
  const float u = std::clamp(t, 0.0f, 1.0f);
  const auto ch = [u](unsigned char x, unsigned char y) {
    return static_cast<unsigned char>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * u));
  };
  return Color{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}
}  // namespace

void ShellView::LoadPlanetLayer(const Simulation& sim, std::uint8_t targetInMine, bool placeOres) {
  const Body* here = sim.FindBodyById(sim.Map().here);
  if (here == nullptr) { return; }
  constexpr int N = PlanetSurfaceState::kSize;
  static constexpr int kCardDx[4] = {0, 1, 0, -1};
  static constexpr int kCardDy[4] = {-1, 0, 1, 0};

  std::uint32_t baseSeed = 2166136261u;
  for (unsigned char c : here->id) {
    baseSeed ^= c;
    baseSeed *= 16777619u;
  }
  baseSeed ^= static_cast<std::uint32_t>(sim.World().chartSettings.seed * 2654435761u);
  if (baseSeed == 0u) { baseSeed = 1u; }

  std::uint32_t oreHash = baseSeed ^ 0x9E3779B9u;
  oreHash ^= oreHash << 13;
  oreHash ^= oreHash >> 17;
  oreHash ^= oreHash << 5;
  const int totalOreGoal = 3 + static_cast<int>(oreHash % 3u);  // 3..5 total per planet
  const int mineOreGoal = (totalOreGoal >= 4) ? 2 : 1;          // 1..2 in underground mine
  const int surfOreGoal = totalOreGoal - mineOreGoal;           // 2..3 on planet surface

  // Compact 288 B stack BFS (32 B row bitset + 256 B packed (y << 4) | x queue)
  auto connectedFrom = [&](int startX, int startY, std::uint8_t layerBit, bool checkLayerOres) -> bool {
    auto isBlocked = [&](int x, int y) -> bool {
      const std::uint8_t t = planetSurface_.Tile(x, y);
      if (t == 1u || (layerBit == 0u && t == 2u)) { return true; }
      if (checkLayerOres) {
        for (int oi = 0; oi < static_cast<int>(planetSurface_.oreCount); ++oi) {
          if (((planetSurface_.oreLayerMask >> oi) & 1u) == layerBit &&
              planetSurface_.oreX[oi] == x && planetSurface_.oreY[oi] == y) {
            return true;
          }
        }
      }
      return false;
    };

    std::uint16_t visRows[N]{};
    std::uint8_t queue[N * N];
    int head = 0, tail = 0;
    if (!isBlocked(startX, startY)) {
      visRows[startY] = static_cast<std::uint16_t>(1u << startX);
      queue[tail++] = static_cast<std::uint8_t>((startY << PlanetSurfaceState::kSizeShift) | startX);
    }
    int openTotal = 0;
    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        if (!isBlocked(x, y)) { ++openTotal; }
      }
    }
    while (head < tail) {
      const std::uint8_t packed = queue[head++];
      const int cx = static_cast<int>(packed & (N - 1));
      const int cy = static_cast<int>(packed >> PlanetSurfaceState::kSizeShift);
      for (int d = 0; d < 4; ++d) {
        const int nx = cx + kCardDx[d];
        const int ny = cy + kCardDy[d];
        if (static_cast<unsigned>(nx) >= static_cast<unsigned>(N) ||
            static_cast<unsigned>(ny) >= static_cast<unsigned>(N)) {
          continue;
        }
        const std::uint16_t bit = static_cast<std::uint16_t>(1u << nx);
        if ((visRows[ny] & bit) != 0u || isBlocked(nx, ny)) { continue; }
        visRows[ny] |= bit;
        queue[tail++] = static_cast<std::uint8_t>((ny << PlanetSurfaceState::kSizeShift) | nx);
      }
    }
    if (tail != openTotal) { return false; }
    if (checkLayerOres) {
      for (int oi = 0; oi < static_cast<int>(planetSurface_.oreCount); ++oi) {
        if (((planetSurface_.oreLayerMask >> oi) & 1u) != layerBit) { continue; }
        bool adjVis = false;
        for (int d = 0; d < 4; ++d) {
          const int nx = static_cast<int>(planetSurface_.oreX[oi]) + kCardDx[d];
          const int ny = static_cast<int>(planetSurface_.oreY[oi]) + kCardDy[d];
          if (static_cast<unsigned>(nx) < static_cast<unsigned>(N) &&
              static_cast<unsigned>(ny) < static_cast<unsigned>(N) &&
              (visRows[ny] & static_cast<std::uint16_t>(1u << nx)) != 0u) {
            adjVis = true;
            break;
          }
        }
        if (!adjVis) { return false; }
      }
    }
    return true;
  };

  if (targetInMine == 0u) {
    std::uint32_t h = baseSeed;
    auto nextRand = [&h]() -> std::uint32_t {
      h ^= h << 13;
      h ^= h >> 17;
      h ^= h << 5;
      return h ? h : 1u;
    };

    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        planetSurface_.cells[(y << PlanetSurfaceState::kSizeShift) | x] =
            (x == 0 || x == N - 1 || y == 0 || y == N - 1) ? 1u : 0u;
      }
    }
    constexpr int kSpawnX = N / 2;
    constexpr int kShipY = N - 2;
    constexpr int kSpawnY = N - 3;
    planetSurface_.shipX = static_cast<std::uint8_t>(kSpawnX);
    planetSurface_.shipY = static_cast<std::uint8_t>(kShipY);
    planetSurface_.SetTile(kSpawnX, kShipY, 2u);

    const int obstacleGoal = 24 + static_cast<int>(nextRand() % 9u);
    int placed = 0;
    for (int attempt = 0; attempt < 256 && placed < obstacleGoal; ++attempt) {
      const int rx = 1 + static_cast<int>(nextRand() % (N - 2));
      const int ry = 1 + static_cast<int>(nextRand() % (N - 2));
      if ((rx == kSpawnX && (ry == kShipY || ry == kSpawnY || ry == kSpawnY - 1)) ||
          planetSurface_.Tile(rx, ry) != 0u) {
        continue;
      }
      planetSurface_.SetTile(rx, ry, 1u);
      if (connectedFrom(kSpawnX, kSpawnY, 0u, /*checkLayerOres=*/false)) {
        ++placed;
      } else {
        planetSurface_.SetTile(rx, ry, 0u);
      }
    }

    // Carve a winding trail network (3) with natural bends and branches across the 16x16 surface
    std::uint8_t trailPos[N * N];
    int trailLen = 0;
    auto markTrail = [&](int x, int y) {
      if (planetSurface_.Tile(x, y) == 0u) {
        planetSurface_.SetTile(x, y, 3u);
        if (trailLen < N * N) {
          trailPos[trailLen++] = static_cast<std::uint8_t>((y << PlanetSurfaceState::kSizeShift) | x);
        }
      }
    };
    auto countAdjTrail = [&](int x, int y) -> int {
      int c = 0;
      for (int d = 0; d < 4; ++d) {
        const int nx = x + kCardDx[d];
        const int ny = y + kCardDy[d];
        if (nx > 0 && nx < N - 1 && ny > 0 && ny < N - 1 &&
            planetSurface_.Tile(nx, ny) == 3u) {
          ++c;
        }
      }
      return c;
    };

    markTrail(kSpawnX, kSpawnY);
    markTrail(kSpawnX, kSpawnY - 1);
    int wx = kSpawnX, wy = kSpawnY - 1, wDir = 0;
    const int mainGoal = 22 + static_cast<int>(nextRand() % 9u);
    for (int step = 0; step < mainGoal; ++step) {
      int cands[8];
      int nCand = 0;
      for (int d = 0; d < 4; ++d) {
        if (d == (wDir + 2) % 4) { continue; }
        const int nx = wx + kCardDx[d];
        const int ny = wy + kCardDy[d];
        if (nx <= 0 || nx >= N - 1 || ny <= 0 || ny >= N - 1) { continue; }
        if (planetSurface_.Tile(nx, ny) != 0u) { continue; }
        if (countAdjTrail(nx, ny) > 1) { continue; }
        const int weight = (d == wDir) ? ((step % 3 == 0) ? 1 : 2) : 2;
        for (int w = 0; w < weight && nCand < 8; ++w) { cands[nCand++] = d; }
      }
      if (nCand == 0) { break; }
      wDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
      wx += kCardDx[wDir];
      wy += kCardDy[wDir];
      markTrail(wx, wy);
    }

    // Add 3-5 curving side branches off the main trail across the 16x16 world
    const int branches = 3 + static_cast<int>(nextRand() % 3u);
    for (int b = 0; b < branches && trailLen > 2; ++b) {
      const int seedIdx = 1 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(trailLen - 1));
      int bx = static_cast<int>(trailPos[seedIdx] & (N - 1));
      int by = static_cast<int>(trailPos[seedIdx] >> PlanetSurfaceState::kSizeShift);
      int bDir = static_cast<int>(nextRand() % 4u);
      const int branchSteps = 5 + static_cast<int>(nextRand() % 5u);
      for (int step = 0; step < branchSteps; ++step) {
        int cands[4];
        int nCand = 0;
        for (int d = 0; d < 4; ++d) {
          if (d == (bDir + 2) % 4) { continue; }
          const int nx = bx + kCardDx[d];
          const int ny = by + kCardDy[d];
          if (nx <= 0 || nx >= N - 1 || ny <= 0 || ny >= N - 1) { continue; }
          if (planetSurface_.Tile(nx, ny) != 0u) { continue; }
          if (countAdjTrail(nx, ny) > 1) { continue; }
          cands[nCand++] = d;
        }
        if (nCand == 0) { break; }
        bDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
        bx += kCardDx[bDir];
        by += kCardDy[bDir];
        markTrail(bx, by);
      }
    }

    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        planetSurface_.SetVariant(x, y, static_cast<std::uint8_t>(nextRand() & 7u));
      }
    }

    // Pick 1 enterable mine portal cut into a cliff cell facing a reachable approach tile
    struct PortalCand {
      std::uint8_t x;
      std::uint8_t y;
      std::uint8_t outDir;
    };
    PortalCand tier1[128], tier2[128], tier3[64];
    int nT1 = 0, nT2 = 0, nT3 = 0;
    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        if (planetSurface_.Tile(x, y) != 1u) { continue; }
        const bool interior = (x > 0 && x < N - 1 && y > 0 && y < N - 1);
        for (int d = 0; d < 4; ++d) {
          const int ax = x + kCardDx[d];
          const int ay = y + kCardDy[d];
          if (ax <= 0 || ax >= N - 1 || ay <= 0 || ay >= N - 1) { continue; }
          const std::uint8_t at = planetSurface_.Tile(ax, ay);
          if (at != 0u && at != 3u) { continue; }
          if (ax == kSpawnX && (ay == kSpawnY || ay == kShipY)) { continue; }
          const int shipDist = std::abs(ax - kSpawnX) + std::abs(ay - kShipY);
          if (shipDist < 4) { continue; }
          if (interior) {
            const int bx = x - kCardDx[d];
            const int by = y - kCardDy[d];
            const bool backed = (bx <= 0 || bx >= N - 1 || by <= 0 || by >= N - 1 ||
                                 planetSurface_.Tile(bx, by) == 1u);
            const bool nearTrail = (at == 3u) || (countAdjTrail(ax, ay) > 0);
            if ((backed || nearTrail) && nT1 < 128) {
              tier1[nT1++] = PortalCand{static_cast<std::uint8_t>(x),
                                        static_cast<std::uint8_t>(y),
                                        static_cast<std::uint8_t>(d)};
            } else if (nT2 < 128) {
              tier2[nT2++] = PortalCand{static_cast<std::uint8_t>(x),
                                        static_cast<std::uint8_t>(y),
                                        static_cast<std::uint8_t>(d)};
            }
          } else if (nT3 < 64) {
            tier3[nT3++] = PortalCand{static_cast<std::uint8_t>(x),
                                      static_cast<std::uint8_t>(y),
                                      static_cast<std::uint8_t>(d)};
          }
        }
      }
    }
    PortalCand chosen{static_cast<std::uint8_t>(kSpawnX), 0u, 2u};
    if (nT1 > 0) {
      chosen = tier1[nextRand() % static_cast<std::uint32_t>(nT1)];
    } else if (nT2 > 0) {
      chosen = tier2[nextRand() % static_cast<std::uint32_t>(nT2)];
    } else if (nT3 > 0) {
      chosen = tier3[nextRand() % static_cast<std::uint32_t>(nT3)];
    }
    planetSurface_.portalX = chosen.x;
    planetSurface_.portalY = chosen.y;
    planetSurface_.portalOutDir = chosen.outDir;
    planetSurface_.mineInDir = static_cast<std::uint8_t>((chosen.outDir + 2u) & 3u);
    planetSurface_.SetVariant(chosen.x, chosen.y, 7u);
    const int portalApronX = static_cast<int>(chosen.x) + kCardDx[chosen.outDir & 3u];
    const int portalApronY = static_cast<int>(chosen.y) + kCardDy[chosen.outDir & 3u];
    if (planetSurface_.Tile(portalApronX, portalApronY) == 0u) {
      planetSurface_.SetTile(portalApronX, portalApronY, 3u);
    }

    if (placeOres) {
      std::uint8_t candPos[N * N];
      int candCount = 0;
      for (int y = 1; y < N - 1; ++y) {
        for (int x = 1; x < N - 1; ++x) {
          const std::uint8_t t = planetSurface_.Tile(x, y);
          if (t != 0u && t != 3u) { continue; }
          if (x == kSpawnX && y == kSpawnY) { continue; }
          if (x == portalApronX && y == portalApronY) { continue; }
          const int dist = std::abs(x - kSpawnX) + std::abs(y - kShipY);
          if (dist >= 3) {
            candPos[candCount++] = static_cast<std::uint8_t>((y << PlanetSurfaceState::kSizeShift) | x);
          }
        }
      }
      const int wantSurf = std::min({candCount, PlanetSurfaceState::kMaxOre, surfOreGoal});
      for (int i = 0; i < candCount && static_cast<int>(planetSurface_.oreCount) < wantSurf; ++i) {
        const int pick = i + static_cast<int>(nextRand() % static_cast<std::uint32_t>(candCount - i));
        std::swap(candPos[i], candPos[pick]);
        const int slot = static_cast<int>(planetSurface_.oreCount);
        planetSurface_.oreX[slot] = static_cast<std::uint8_t>(candPos[i] & (N - 1));
        planetSurface_.oreY[slot] = static_cast<std::uint8_t>(candPos[i] >> PlanetSurfaceState::kSizeShift);
        planetSurface_.oreLayerMask = static_cast<std::uint8_t>(planetSurface_.oreLayerMask & ~(1u << slot));
        ++planetSurface_.oreCount;
        if (!connectedFrom(kSpawnX, kSpawnY, 0u, /*checkLayerOres=*/true)) {
          --planetSurface_.oreCount;
        }
      }
    }
    return;
  }

  // targetInMine == 1: carve deterministic underground mine sub-map into cells[256] in place
  std::uint32_t h = baseSeed ^ 0x85EBCA6Bu;
  if (h == 0u) { h = 1u; }
  auto nextRand = [&h]() -> std::uint32_t {
    h ^= h << 13;
    h ^= h >> 17;
    h ^= h << 5;
    return h ? h : 1u;
  };

  std::uint8_t mTile[N * N];
  std::memset(mTile, 1, sizeof(mTile));
  auto mAt = [&](int u, int v) -> std::uint8_t& {
    return mTile[(v << PlanetSurfaceState::kSizeShift) | u];
  };
  auto countOpenAdj = [&](int u, int v) -> int {
    int c = 0;
    for (int d = 0; d < 4; ++d) {
      const int nu = u + kCardDx[d];
      const int nv = v + kCardDy[d];
      if (nu > 0 && nu < N - 1 && nv > 0 && nv < N - 1 && mAt(nu, nv) != 1u) {
        ++c;
      }
    }
    return c;
  };

  std::uint8_t spine[N * N];
  int spineLen = 0;
  auto carveCell = [&](int u, int v, std::uint8_t kind) {
    if (mAt(u, v) == 1u) {
      mAt(u, v) = kind;
      if (spineLen < N * N) {
        spine[spineLen++] = static_cast<std::uint8_t>((v << PlanetSurfaceState::kSizeShift) | u);
      }
    } else if (kind == 3u && mAt(u, v) == 0u) {
      mAt(u, v) = 3u;
    }
  };

  // Canonical entrance shaft at (8,14) -> (8,13..11) heading North; rotated to match mineInDir
  carveCell(8, 13, 3u);
  carveCell(8, 12, 3u);
  carveCell(8, 11, 3u);

  int cu = 8, cv = 11, cDir = 0;
  const int aditSteps = 20 + static_cast<int>(nextRand() % 7u);
  for (int step = 0; step < aditSteps; ++step) {
    int cands[8];
    int nCand = 0;
    for (int pass = 0; pass < 2 && nCand == 0; ++pass) {
      const int maxAdj = (pass == 0) ? 1 : 2;
      for (int d = 0; d < 4; ++d) {
        if (d == (cDir + 2) % 4) { continue; }
        if (step < 3 && d == 2) { continue; }
        const int nu = cu + kCardDx[d];
        const int nv = cv + kCardDy[d];
        if (nu <= 0 || nu >= N - 1 || nv <= 0 || nv >= N - 2) { continue; }
        if (mAt(nu, nv) != 1u || countOpenAdj(nu, nv) > maxAdj) { continue; }
        const int weight = (d == cDir) ? ((step % 3 == 0) ? 1 : 2) : 2;
        for (int w = 0; w < weight && nCand < 8; ++w) { cands[nCand++] = d; }
      }
    }
    if (nCand == 0) { break; }
    cDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
    cu += kCardDx[cDir];
    cv += kCardDy[cDir];
    carveCell(cu, cv, 3u);
  }

  // Carve 3-4 branching side drifts off the main adit
  const int driftCount = 3 + static_cast<int>(nextRand() % 2u);
  for (int b = 0; b < driftCount && spineLen > 3; ++b) {
    const int seedIdx = 2 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(spineLen - 2));
    int bu = static_cast<int>(spine[seedIdx] & (N - 1));
    int bv = static_cast<int>(spine[seedIdx] >> PlanetSurfaceState::kSizeShift);
    int bDir = static_cast<int>(nextRand() % 4u);
    const int driftSteps = 5 + static_cast<int>(nextRand() % 5u);
    for (int step = 0; step < driftSteps; ++step) {
      int cands[4];
      int nCand = 0;
      const int maxAdj = (b == 0 && step + 1 == driftSteps) ? 2 : 1;
      for (int d = 0; d < 4; ++d) {
        if (d == (bDir + 2) % 4) { continue; }
        const int nu = bu + kCardDx[d];
        const int nv = bv + kCardDy[d];
        if (nu <= 0 || nu >= N - 1 || nv <= 0 || nv >= N - 2) { continue; }
        if (mAt(nu, nv) != 1u || countOpenAdj(nu, nv) > maxAdj) { continue; }
        cands[nCand++] = d;
      }
      if (nCand == 0) { break; }
      bDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
      bu += kCardDx[bDir];
      bv += kCardDy[bDir];
      carveCell(bu, bv, (step < 3) ? 3u : 0u);
    }
  }

  // Excavate 2-3 small 2x2 subterranean pockets anchored on carved drifts for alcoves/chambers
  const int pocketCount = 2 + static_cast<int>(nextRand() % 2u);
  for (int p = 0; p < pocketCount && spineLen > 4; ++p) {
    const int seedIdx = 3 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(spineLen - 3));
    const int pu = std::clamp(static_cast<int>(spine[seedIdx] & (N - 1)), 1, N - 3);
    const int pv = std::clamp(static_cast<int>(spine[seedIdx] >> PlanetSurfaceState::kSizeShift), 1, N - 4);
    for (int dv = 0; dv < 2; ++dv) {
      for (int du = 0; du < 2; ++du) {
        carveCell(pu + du, pv + dv, 0u);
      }
    }
  }

  // Ensure every planet's underground mine has a generous network (>= 32 walkable cells)
  for (int extra = 0; extra < 32 && spineLen < 34 && spineLen > 2; ++extra) {
    const int seedIdx = 1 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(spineLen - 1));
    int eu = static_cast<int>(spine[seedIdx] & (N - 1));
    int ev = static_cast<int>(spine[seedIdx] >> PlanetSurfaceState::kSizeShift);
    int eDir = static_cast<int>(nextRand() % 4u);
    for (int step = 0; step < 5 && spineLen < 34; ++step) {
      int cands[4];
      int nCand = 0;
      for (int d = 0; d < 4; ++d) {
        const int nu = eu + kCardDx[d];
        const int nv = ev + kCardDy[d];
        if (nu <= 0 || nu >= N - 1 || nv <= 0 || nv >= N - 2) { continue; }
        if (mAt(nu, nv) != 1u || countOpenAdj(nu, nv) > 1) { continue; }
        cands[nCand++] = d;
      }
      if (nCand == 0) { break; }
      eDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
      eu += kCardDx[eDir];
      ev += kCardDy[eDir];
      carveCell(eu, ev, (step < 2) ? 3u : 0u);
    }
  }

  const int rot = static_cast<int>(planetSurface_.mineInDir & 3u);
  auto mapCoord = [rot](int u, int v, int& xOut, int& yOut) {
    if (rot == 0) {
      xOut = u;
      yOut = v;
    } else if (rot == 1) {
      xOut = (N - 1) - v;
      yOut = u;
    } else if (rot == 2) {
      xOut = (N - 1) - u;
      yOut = (N - 1) - v;
    } else {
      xOut = v;
      yOut = (N - 1) - u;
    }
  };

  for (int v = 0; v < N; ++v) {
    for (int u = 0; u < N; ++u) {
      int x = 0, y = 0;
      mapCoord(u, v, x, y);
      const std::uint8_t t = mAt(u, v);
      planetSurface_.SetTile(x, y, t);
      if (t == 1u) {
        // 100% of underground walls are 3D rock/vein/shored-timber faces (variants 4..7)
        planetSurface_.SetVariant(x, y, static_cast<std::uint8_t>(4u + (nextRand() & 3u)));
      } else {
        planetSurface_.SetVariant(x, y, static_cast<std::uint8_t>(nextRand() & 7u));
      }
    }
  }

  int mPortalX = 0, mPortalY = 0, mSpawnX = 0, mSpawnY = 0, mShaftX = 0, mShaftY = 0;
  mapCoord(8, 14, mPortalX, mPortalY);
  mapCoord(8, 13, mSpawnX, mSpawnY);
  mapCoord(8, 12, mShaftX, mShaftY);
  planetSurface_.portalX = static_cast<std::uint8_t>(mPortalX);
  planetSurface_.portalY = static_cast<std::uint8_t>(mPortalY);
  planetSurface_.portalOutDir = static_cast<std::uint8_t>(rot);
  planetSurface_.SetVariant(mPortalX, mPortalY, 7u);

  if (placeOres) {
    std::uint8_t tierA[N * N], tierB[N * N];
    int nA = 0, nB = 0;
    for (int y = 1; y < N - 1; ++y) {
      for (int x = 1; x < N - 1; ++x) {
        const std::uint8_t t = planetSurface_.Tile(x, y);
        if (t != 0u && t != 3u) { continue; }
        if ((x == mSpawnX && y == mSpawnY) || (x == mShaftX && y == mShaftY)) { continue; }
        const int dist = std::abs(x - mSpawnX) + std::abs(y - mSpawnY);
        int openDeg = 0;
        for (int d = 0; d < 4; ++d) {
          const std::uint8_t nt = planetSurface_.Tile(x + kCardDx[d], y + kCardDy[d]);
          if (nt == 0u || nt == 3u) { ++openDeg; }
        }
        const std::uint8_t packed = static_cast<std::uint8_t>((y << PlanetSurfaceState::kSizeShift) | x);
        if (dist >= 5 && (openDeg == 1 || t == 0u)) {
          tierA[nA++] = packed;
        } else if (dist >= 2) {
          tierB[nB++] = packed;
        }
      }
    }
    for (int i = 0; i < nA; ++i) {
      const int pick = i + static_cast<int>(nextRand() % static_cast<std::uint32_t>(nA - i));
      std::swap(tierA[i], tierA[pick]);
    }
    for (int i = 0; i < nB; ++i) {
      const int pick = i + static_cast<int>(nextRand() % static_cast<std::uint32_t>(nB - i));
      std::swap(tierB[i], tierB[pick]);
    }
    std::uint8_t candPos[N * N];
    int candCount = 0;
    for (int i = 0; i < nA; ++i) { candPos[candCount++] = tierA[i]; }
    for (int i = 0; i < nB; ++i) { candPos[candCount++] = tierB[i]; }

    int placedMineOres = 0;
    for (int i = 0; i < candCount &&
                    placedMineOres < mineOreGoal &&
                    static_cast<int>(planetSurface_.oreCount) < PlanetSurfaceState::kMaxOre; ++i) {
      const int slot = static_cast<int>(planetSurface_.oreCount);
      planetSurface_.oreX[slot] = static_cast<std::uint8_t>(candPos[i] & (N - 1));
      planetSurface_.oreY[slot] = static_cast<std::uint8_t>(candPos[i] >> PlanetSurfaceState::kSizeShift);
      planetSurface_.oreLayerMask = static_cast<std::uint8_t>(planetSurface_.oreLayerMask | (1u << slot));
      ++planetSurface_.oreCount;
      if (connectedFrom(mSpawnX, mSpawnY, 1u, /*checkLayerOres=*/true)) {
        ++placedMineOres;
      } else {
        planetSurface_.oreLayerMask = static_cast<std::uint8_t>(planetSurface_.oreLayerMask & ~(1u << slot));
        --planetSurface_.oreCount;
      }
    }
  }
}

void ShellView::EnsurePlanetSurface(const Simulation& sim) {
  if (!sim.LandedOnPlanet()) { return; }
  const Body* here = sim.FindBodyById(sim.Map().here);
  if (here == nullptr) { return; }
  if (planetSurface_.planetId == here->id) { return; }

  planetSurface_ = PlanetSurfaceState{};
  planetSurface_.planetId = here->id;
  constexpr int N = PlanetSurfaceState::kSize;

  // Build surface & place surface ores, then build mine once to place underground ores,
  // then restore surface cells in-place (0 B extra persistent map buffer).
  LoadPlanetLayer(sim, 0u, /*placeOres=*/true);
  std::uint8_t surfCells[N * N];
  std::memcpy(surfCells, planetSurface_.cells, sizeof(surfCells));
  const std::uint8_t sPortalX = planetSurface_.portalX;
  const std::uint8_t sPortalY = planetSurface_.portalY;
  const std::uint8_t sPortalOut = planetSurface_.portalOutDir;

  LoadPlanetLayer(sim, 1u, /*placeOres=*/true);

  std::memcpy(planetSurface_.cells, surfCells, sizeof(surfCells));
  planetSurface_.portalX = sPortalX;
  planetSurface_.portalY = sPortalY;
  planetSurface_.portalOutDir = sPortalOut;
  planetSurface_.inMine = 0u;
  planetSurface_.px = static_cast<std::uint8_t>(N / 2);
  planetSurface_.py = static_cast<std::uint8_t>(N - 3);
  planetSurface_.dir = 0u;
}

void ShellView::StepPlanetSurface(const Simulation& sim, int actionKind) {
  constexpr int N = PlanetSurfaceState::kSize;
  static constexpr int kDx[4] = {0, 1, 0, -1};
  static constexpr int kDy[4] = {-1, 0, 1, 0};

  auto canTraversePortal = [&](int fromX, int fromY, int toX, int toY) -> bool {
    const int pOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
    return toX == static_cast<int>(planetSurface_.portalX) &&
           toY == static_cast<int>(planetSurface_.portalY) &&
           fromX == static_cast<int>(planetSurface_.portalX) + kDx[pOut] &&
           fromY == static_cast<int>(planetSurface_.portalY) + kDy[pOut];
  };
  auto traversePortal = [&]() {
    if (planetSurface_.inMine == 0u) {
      planetSurface_.surfPx = planetSurface_.px;
      planetSurface_.surfPy = planetSurface_.py;
      planetSurface_.surfDir = static_cast<std::uint8_t>(planetSurface_.portalOutDir & 3u);
      LoadPlanetLayer(sim, 1u, /*placeOres=*/false);
      planetSurface_.inMine = 1u;
      const int mOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
      planetSurface_.px = static_cast<std::uint8_t>(static_cast<int>(planetSurface_.portalX) + kDx[mOut]);
      planetSurface_.py = static_cast<std::uint8_t>(static_cast<int>(planetSurface_.portalY) + kDy[mOut]);
      planetSurface_.dir = static_cast<std::uint8_t>(mOut);
    } else {
      LoadPlanetLayer(sim, 0u, /*placeOres=*/false);
      planetSurface_.inMine = 0u;
      planetSurface_.px = planetSurface_.surfPx;
      planetSurface_.py = planetSurface_.surfPy;
      planetSurface_.dir = static_cast<std::uint8_t>(planetSurface_.surfDir & 3u);
    }
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  };

  if (actionKind == 8) {
    const int fx = static_cast<int>(planetSurface_.px) + kDx[planetSurface_.dir & 3u];
    const int fy = static_cast<int>(planetSurface_.py) + kDy[planetSurface_.dir & 3u];
    if (canTraversePortal(planetSurface_.px, planetSurface_.py, fx, fy)) {
      traversePortal();
    }
    return;
  }

  if (actionKind == 1 || actionKind == 2) {
    const int sign = actionKind == 1 ? 1 : -1;
    const int nx = static_cast<int>(planetSurface_.px) + kDx[planetSurface_.dir & 3u] * sign;
    const int ny = static_cast<int>(planetSurface_.py) + kDy[planetSurface_.dir & 3u] * sign;
    if (actionKind == 1 && canTraversePortal(planetSurface_.px, planetSurface_.py, nx, ny)) {
      traversePortal();
      return;
    }
    std::uint8_t blk = 0;
    if (static_cast<unsigned>(nx) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(ny) >= static_cast<unsigned>(N)) {
      blk = (planetSurface_.inMine != 0u) ? 4u : 1u;
    } else if (planetSurface_.Tile(nx, ny) == 1u) {
      blk = ((planetSurface_.inMine != 0u) || ((planetSurface_.Variant(nx, ny) & 4) != 0)) ? 4u : 1u;
    } else if (planetSurface_.inMine == 0u &&
               nx == planetSurface_.shipX && ny == planetSurface_.shipY) {
      blk = 2u;
    } else {
      const std::string& hereId = sim.Map().here;
      for (int i = 0; i < static_cast<int>(planetSurface_.oreCount); ++i) {
        if (((planetSurface_.oreLayerMask >> i) & 1u) == planetSurface_.inMine &&
            planetSurface_.oreX[i] == nx && planetSurface_.oreY[i] == ny) {
          const std::string key = "planet-ore:" + hereId + ":" + std::to_string(i);
          auto it = sim.State().mapMarks.find(key);
          if (it == sim.State().mapMarks.end() || !it->second) {
            blk = 3u;
            break;
          }
        }
      }
    }
    if (blk == 0) {
      planetSurface_.px = static_cast<std::uint8_t>(nx);
      planetSurface_.py = static_cast<std::uint8_t>(ny);
      planetSurface_.bumpKind = 0;
      planetSurface_.bumpBack = false;
    } else {
      planetSurface_.bumpKind = blk;
      planetSurface_.bumpBack = (actionKind == 2);
    }
  } else if (actionKind == 3) {
    planetSurface_.dir = static_cast<std::uint8_t>((planetSurface_.dir + 3u) & 3u);
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  } else if (actionKind == 4) {
    planetSurface_.dir = static_cast<std::uint8_t>((planetSurface_.dir + 1u) & 3u);
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  } else if (actionKind == 7) {
    planetSurface_.dir = static_cast<std::uint8_t>((planetSurface_.dir + 2u) & 3u);
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  }
}

void ShellView::RenderPlanetSurfacePixels(const Simulation& sim) {
  constexpr int W = PlanetSurfaceState::kTexW;
  constexpr int H = PlanetSurfaceState::kTexH;
  constexpr int N = PlanetSurfaceState::kSize;
  const Body* planet = sim.FindBodyById(sim.Map().here);
  if (planet == nullptr) { return; }

  std::uint64_t pHash = 14695981039346656037ULL;
  for (unsigned char ch : planet->id) {
    pHash ^= static_cast<std::uint64_t>(ch);
    pHash *= 1099511628211ULL;
  }
  if (pHash == 0ULL) { pHash = 1ULL; }

  const std::size_t marksCount = sim.State().mapMarks.size();
  const int lotSerial = static_cast<int>(sim.State().lotSerial);
  std::uint8_t oreMask = planetSurface_.renderOreMask;
  if (planetSurface_.renderPlanetHash != pHash ||
      planetSurface_.renderMarksCount != marksCount ||
      planetSurface_.renderLotSerial != lotSerial ||
      oreMask == 0xFFu) {
    oreMask = 0u;
    char keyBuf[96];
    for (int i = 0; i < static_cast<int>(planetSurface_.oreCount); ++i) {
      const int n = std::snprintf(keyBuf, sizeof(keyBuf), "planet-ore:%s:%d",
                                  planet->id.c_str(), i);
      bool collected = false;
      if (n > 0 && static_cast<std::size_t>(n) < sizeof(keyBuf)) {
        auto it = sim.State().mapMarks.find(std::string(keyBuf, static_cast<std::size_t>(n)));
        collected = (it != sim.State().mapMarks.end() && it->second);
      } else {
        const std::string key = "planet-ore:" + planet->id + ":" + std::to_string(i);
        auto it = sim.State().mapMarks.find(key);
        collected = (it != sim.State().mapMarks.end() && it->second);
      }
      if (!collected) {
        oreMask = static_cast<std::uint8_t>(oreMask | (1u << i));
      }
    }
    planetSurface_.renderMarksCount = marksCount;
    planetSurface_.renderLotSerial = lotSerial;
  }

  if (planetTex_.id != 0 &&
      planetSurface_.renderPlanetHash == pHash &&
      planetSurface_.renderPx == static_cast<std::int8_t>(planetSurface_.px) &&
      planetSurface_.renderPy == static_cast<std::int8_t>(planetSurface_.py) &&
      planetSurface_.renderDir == static_cast<std::int8_t>(planetSurface_.dir) &&
      planetSurface_.renderInMine == static_cast<std::int8_t>(planetSurface_.inMine) &&
      planetSurface_.renderOreMask == oreMask) {
    return;
  }

  planetSurface_.renderPlanetHash = pHash;
  planetSurface_.renderPx = static_cast<std::int8_t>(planetSurface_.px);
  planetSurface_.renderPy = static_cast<std::int8_t>(planetSurface_.py);
  planetSurface_.renderDir = static_cast<std::int8_t>(planetSurface_.dir);
  planetSurface_.renderInMine = static_cast<std::int8_t>(planetSurface_.inMine);
  planetSurface_.renderOreMask = oreMask;
  planetSurface_.frontKind = 0;
  planetSurface_.frontArg = 0;
  planetSurface_.frontUv = Area{};

  const std::string& subtype = planet->subtype;
  const bool lavaWorld =
      subtype == "lava" ||
      planet->klass.find("Lava") != std::string::npos ||
      planet->klass.find("Volcanic") != std::string::npos ||
      planet->klass.find("lava") != std::string::npos;
  const bool iceWorld = !lavaWorld && subtype == "ice";
  const bool oceanWorld = !lavaWorld && subtype == "ocean";
  const bool rockWorld = !lavaWorld && !iceWorld && !oceanWorld &&
      (subtype == "rock" ||
       planet->klass.find("Rugged") != std::string::npos ||
       planet->klass.find("Barren") != std::string::npos ||
       planet->klass.find("Crater") != std::string::npos);

  const int biomeIdx = iceWorld ? 1 : oceanWorld ? 2 : lavaWorld ? 3 : rockWorld ? 4 : 0;

  // Deterministic per-planet visual DNA (0 B struct increase):
  // 4 distinct sub-biome palettes & moods per biome (20 total) + sky, horizon, road & prop variations.
  const std::uint32_t pSeed =
      static_cast<std::uint32_t>(pHash ^ (pHash >> 32)) ^
      (static_cast<std::uint32_t>(sim.World().chartSettings.seed) * 2654435761u);
  const int subBiome      = static_cast<int>((pSeed >> 2) & 3u);
  const int celestialKind = static_cast<int>((pSeed >> 5) & 3u);
  const int cloudStyle    = static_cast<int>((pSeed >> 7) & 3u);
  const int horizonStyle  = static_cast<int>((pSeed >> 9) & 3u);
  const int roadStyle     = static_cast<int>((pSeed >> 11) & 1u);
  const int propBias      = static_cast<int>((pSeed >> 12) & 3u);
  const int palIdx        = biomeIdx * 4 + subBiome;
  const bool airlessWorld = (biomeIdx == 4 && subBiome == 1);
  const bool nightSky     = airlessWorld ||
                            (biomeIdx == 1 && subBiome == 1) ||
                            (biomeIdx == 2 && subBiome == 3) ||
                            (biomeIdx == 3 && subBiome == 3);
  const bool duskSky      = (biomeIdx == 0 && (subBiome == 1 || subBiome == 2)) ||
                            (biomeIdx == 1 && subBiome == 2) ||
                            (biomeIdx == 2 && (subBiome == 1 || subBiome == 2)) ||
                            (biomeIdx == 4 && subBiome == 2);

  // PC-98 .GTA-style 12-bit RGB4 (4-4-4, 0..15 * 17) nibble-packed biome palettes:
  // 16 colors * 12 bits = 24 B per sub-biome, 480 B total across 20 sub-biomes (5 biomes x 4 moods).
  struct PackedBiomePalettes {
    std::uint8_t bytes[20][24]{};
  };
  static constexpr PackedBiomePalettes kBiomePalettes = []() constexpr {
    PackedBiomePalettes p{};
    auto setPair = [&](int b, int pairIdx,
                       int r0, int g0, int b0,
                       int r1, int g1, int b1) constexpr {
      const int o = pairIdx * 3;
      p.bytes[b][o + 0] = static_cast<std::uint8_t>(((r0 & 0xF) << 4) | (g0 & 0xF));
      p.bytes[b][o + 1] = static_cast<std::uint8_t>(((b0 & 0xF) << 4) | (r1 & 0xF));
      p.bytes[b][o + 2] = static_cast<std::uint8_t>(((g1 & 0xF) << 4) | (b1 & 0xF));
    };
    // 0: Temperate 0 — Emerald Noon Meadow
    setPair(0, 0,  1, 1, 1,   1, 2, 4);   // 0:ink,      1:sky0
    setPair(0, 1,  2, 4, 7,   4, 7,10);   // 2:sky1,     3:sky2
    setPair(0, 2,  7,11,12,   2, 4, 2);   // 4:sky3,     5:gnd0
    setPair(0, 3,  3, 6, 3,   5, 8, 3);   // 6:gnd1,     7:gnd2
    setPair(0, 4,  4, 3, 2,   7, 5, 3);   // 8:pathDark, 9:pathMid
    setPair(0, 5, 10, 8, 5,   5, 6, 5);   // 10:pathHi,  11:rockMid
    setPair(0, 6,  9,10, 8,   1, 4, 2);   // 12:rockHi,  13:leaf0
    setPair(0, 7,  3, 8, 3,   7,11, 4);   // 14:leaf1,   15:leaf2
    // 1: Temperate 1 — Golden Autumn Highlands
    setPair(1, 0,  1, 1, 1,   2, 2, 4);
    setPair(1, 1,  5, 3, 5,   9, 5, 4);
    setPair(1, 2, 13, 9, 6,   3, 3, 1);
    setPair(1, 3,  5, 5, 2,   7, 6, 2);
    setPair(1, 4,  4, 3, 3,   7, 6, 5);
    setPair(1, 5, 10, 9, 7,   7, 5, 3);
    setPair(1, 6, 11, 8, 5,   6, 2, 1);
    setPair(1, 7, 11, 4, 2,  14, 9, 3);
    // 2: Temperate 2 — Misty Pine Twilight
    setPair(2, 0,  0, 1, 1,   1, 1, 3);
    setPair(2, 1,  2, 2, 6,   5, 4, 8);
    setPair(2, 2,  8, 7,10,   1, 3, 3);
    setPair(2, 3,  2, 5, 4,   4, 6, 5);
    setPair(2, 4,  3, 2, 2,   5, 4, 3);
    setPair(2, 5,  8, 7, 5,   4, 5, 6);
    setPair(2, 6,  8, 9,10,   1, 3, 3);
    setPair(2, 7,  2, 6, 5,   5, 9, 7);
    // 3: Temperate 3 — Alien Teal-Amber Biosphere
    setPair(3, 0,  1, 0, 1,   1, 3, 4);
    setPair(3, 1,  2, 6, 7,   4, 9, 9);
    setPair(3, 2,  8,12,11,   1, 4, 4);
    setPair(3, 3,  2, 6, 5,   4, 8, 6);
    setPair(3, 4,  5, 4, 5,   8, 7, 7);
    setPair(3, 5, 11,10, 9,   5, 4, 6);
    setPair(3, 6,  9, 7, 9,   7, 2, 5);
    setPair(3, 7, 11, 4, 6,  14, 9, 7);

    // 4: Ice 0 — Glacial Daylight
    setPair(4, 0,  1, 1, 2,   1, 2, 4);
    setPair(4, 1,  2, 4, 8,   4, 8,11);
    setPair(4, 2,  8,12,14,   4, 6, 8);
    setPair(4, 3,  6, 9,11,   9,12,13);
    setPair(4, 4,  2, 4, 6,   5, 7, 9);
    setPair(4, 5,  9,11,13,   5, 7, 9);
    setPair(4, 6, 10,13,15,   2, 5, 7);
    setPair(4, 7,  5,10,12,  12,15,15);
    // 5: Ice 1 — Aurora Borealis Night
    setPair(5, 0,  0, 0, 1,   0, 1, 2);
    setPair(5, 1,  1, 2, 4,   1, 4, 6);
    setPair(5, 2,  2, 7, 8,   2, 3, 5);
    setPair(5, 3,  3, 5, 8,   5, 7,10);
    setPair(5, 4,  1, 2, 4,   3, 5, 7);
    setPair(5, 5,  6, 9,11,   4, 5, 8);
    setPair(5, 6,  8,10,13,   1, 6, 5);
    setPair(5, 7,  3,12, 9,   9,15,13);
    // 6: Ice 2 — Polar Rose Dusk
    setPair(6, 0,  1, 0, 1,   2, 1, 3);
    setPair(6, 1,  5, 2, 5,   9, 4, 6);
    setPair(6, 2, 13, 8, 8,   4, 4, 6);
    setPair(6, 3,  6, 6, 8,   9, 8,10);
    setPair(6, 4,  3, 3, 4,   6, 5, 7);
    setPair(6, 5, 10, 9,10,   6, 5, 7);
    setPair(6, 6, 11,10,12,   4, 4, 7);
    setPair(6, 7,  8, 7,10,  14,12,13);
    // 7: Ice 3 — Glacier Jade Crevasse
    setPair(7, 0,  0, 1, 1,   2, 3, 4);
    setPair(7, 1,  4, 6, 7,   7, 9,10);
    setPair(7, 2, 10,12,12,   2, 5, 6);
    setPair(7, 3,  4, 8, 8,   6,10,10);
    setPair(7, 4,  2, 3, 4,   4, 5, 6);
    setPair(7, 5,  8, 9, 9,   4, 7, 8);
    setPair(7, 6,  9,12,13,   1, 5, 5);
    setPair(7, 7,  3, 9, 9,  10,14,13);

    // 8: Ocean 0 — Tropical Turquoise Reef
    setPair(8, 0,  1, 1, 1,   1, 3, 6);
    setPair(8, 1,  2, 6, 9,   5, 9,12);
    setPair(8, 2,  8,13,14,   1, 5, 6);
    setPair(8, 3,  2, 7, 8,   4,10,10);
    setPair(8, 4,  6, 5, 4,  10, 9, 6);
    setPair(8, 5, 13,12, 9,   4, 5, 6);
    setPair(8, 6,  7, 9,10,   1, 5, 5);
    setPair(8, 7,  3, 9, 8,  14, 6, 5);
    // 9: Ocean 1 — Stormy Sea-Stack Dusk
    setPair(9, 0,  0, 1, 1,   1, 1, 2);
    setPair(9, 1,  2, 3, 4,   4, 5, 6);
    setPair(9, 2,  7, 8, 9,   1, 3, 4);
    setPair(9, 3,  2, 4, 5,   3, 6, 7);
    setPair(9, 4,  3, 3, 4,   5, 6, 6);
    setPair(9, 5,  8, 9, 9,   3, 4, 5);
    setPair(9, 6,  6, 7, 8,   2, 4, 3);
    setPair(9, 7,  4, 7, 5,  13, 9, 4);
    // 10: Ocean 2 — Golden Sunset Atoll
    setPair(10, 0, 1, 0, 1,   2, 1, 4);
    setPair(10, 1, 6, 3, 5,  10, 5, 5);
    setPair(10, 2,14, 9, 6,   2, 3, 5);
    setPair(10, 3, 3, 5, 7,   5, 7, 8);
    setPair(10, 4, 7, 5, 3,  10, 8, 5);
    setPair(10, 5,14,11, 7,   5, 4, 5);
    setPair(10, 6, 9, 7, 7,   2, 5, 5);
    setPair(10, 7, 5, 8, 7,  15, 7, 6);
    // 11: Ocean 3 — Bioluminescent Lagoon
    setPair(11, 0, 0, 0, 1,   0, 1, 2);
    setPair(11, 1, 1, 2, 4,   1, 4, 6);
    setPair(11, 2, 2, 7, 9,   1, 2, 4);
    setPair(11, 3, 1, 4, 6,   2, 6, 8);
    setPair(11, 4, 4, 5, 6,   7, 8, 9);
    setPair(11, 5,10,11,12,   3, 4, 6);
    setPair(11, 6, 6, 8,10,   1, 5, 6);
    setPair(11, 7, 2,11,11,   6,15,14);

    // 12: Lava 0 — Crimson Magma Inferno
    setPair(12, 0, 1, 0, 0,   2, 0, 1);
    setPair(12, 1, 5, 1, 1,   8, 2, 1);
    setPair(12, 2,12, 5, 2,   2, 1, 1);
    setPair(12, 3, 3, 2, 1,   5, 3, 2);
    setPair(12, 4, 2, 1, 2,   4, 3, 3);
    setPair(12, 5, 6, 5, 4,   5, 3, 3);
    setPair(12, 6, 9, 5, 4,   5, 2, 1);
    setPair(12, 7,14, 5, 1,  15,12, 4);
    // 13: Lava 1 — Ash-Choked Violet Caldera
    setPair(13, 0, 1, 0, 1,   2, 1, 3);
    setPair(13, 1, 4, 2, 5,   7, 3, 5);
    setPair(13, 2,10, 5, 5,   2, 1, 2);
    setPair(13, 3, 3, 2, 3,   5, 3, 4);
    setPair(13, 4, 4, 3, 4,   6, 5, 6);
    setPair(13, 5, 9, 8, 8,   5, 3, 5);
    setPair(13, 6, 8, 6, 7,   6, 2, 4);
    setPair(13, 7,13, 4, 6,  15,10, 5);
    // 14: Lava 2 — Sulfur Yellow Badlands
    setPair(14, 0, 1, 1, 0,   2, 2, 1);
    setPair(14, 1, 4, 4, 1,   8, 7, 2);
    setPair(14, 2,12,10, 3,   2, 2, 1);
    setPair(14, 3, 4, 3, 1,   5, 4, 2);
    setPair(14, 4, 4, 3, 1,   6, 5, 2);
    setPair(14, 5, 9, 8, 4,   5, 4, 2);
    setPair(14, 6, 8, 7, 4,   5, 4, 1);
    setPair(14, 7,13,11, 1,  15,15, 6);
    // 15: Lava 3 — Blue-Core Obsidian Rift
    setPair(15, 0, 0, 0, 1,   1, 1, 2);
    setPair(15, 1, 1, 2, 4,   2, 3, 6);
    setPair(15, 2, 4, 6, 9,   1, 1, 2);
    setPair(15, 3, 2, 2, 3,   3, 3, 5);
    setPair(15, 4, 3, 3, 4,   5, 5, 6);
    setPair(15, 5, 8, 8, 9,   4, 4, 6);
    setPair(15, 6, 7, 8,10,   2, 5, 8);
    setPair(15, 7, 4,11,14,  12,15,15);

    // 16: Rock 0 — Red Sandstone Canyon
    setPair(16, 0, 1, 0, 0,   3, 1, 2);
    setPair(16, 1, 6, 3, 3,  10, 5, 4);
    setPair(16, 2,13, 9, 6,   5, 2, 1);
    setPair(16, 3, 7, 4, 2,  10, 5, 3);
    setPair(16, 4, 4, 2, 2,   7, 5, 3);
    setPair(16, 5,11, 8, 5,   8, 4, 2);
    setPair(16, 6,12, 7, 4,   4, 4, 2);
    setPair(16, 7, 6, 7, 3,  10,10, 5);
    // 17: Rock 1 — Airless Starlit Crater World
    setPair(17, 0, 0, 0, 1,   0, 0, 1);
    setPair(17, 1, 0, 0, 1,   1, 1, 2);
    setPair(17, 2, 2, 3, 4,   3, 3, 4);
    setPair(17, 3, 5, 5, 6,   7, 7, 8);
    setPair(17, 4, 4, 4, 5,   7, 7, 8);
    setPair(17, 5,10,10,11,   6, 6, 7);
    setPair(17, 6,10,10,11,   3, 4, 5);
    setPair(17, 7, 6, 7, 8,  11,12,13);
    // 18: Rock 2 — Copper-Rust Badlands
    setPair(18, 0, 1, 0, 1,   3, 2, 3);
    setPair(18, 1, 6, 3, 4,   9, 5, 4);
    setPair(18, 2,12, 8, 6,   4, 2, 2);
    setPair(18, 3, 6, 3, 2,   8, 5, 3);
    setPair(18, 4, 5, 4, 3,   8, 7, 5);
    setPair(18, 5,11,10, 8,   7, 4, 3);
    setPair(18, 6,10, 7, 5,   2, 5, 4);
    setPair(18, 7, 4, 9, 7,   8,12,10);
    // 19: Rock 3 — Golden Steppe Monoliths
    setPair(19, 0, 1, 1, 1,   2, 4, 6);
    setPair(19, 1, 4, 6, 8,   7, 9,10);
    setPair(19, 2,11,12,11,   6, 5, 3);
    setPair(19, 3, 8, 7, 4,  11, 9, 5);
    setPair(19, 4, 5, 4, 3,   8, 7, 5);
    setPair(19, 5,12,11, 8,   8, 7, 5);
    setPair(19, 6,12,11, 8,   4, 5, 2);
    setPair(19, 7, 6, 7, 3,  10,10, 4);
    return p;
  }();
  static_assert(sizeof(kBiomePalettes) == 480, "20 * 24 B packed PC-98 RGB4 sub-biome palettes");

  // Shared 12-bit RGB4 sprite bank (indices 16..25: stars/clouds, ship hull/visor, 3-tone ore crate)
  struct PackedSpritePalette {
    std::uint8_t bytes[15]{};
  };
  static constexpr PackedSpritePalette kSharedSpritePalette = []() constexpr {
    PackedSpritePalette p{};
    auto setPair = [&](int pairIdx, int r0, int g0, int b0, int r1, int g1, int b1) constexpr {
      const int o = pairIdx * 3;
      p.bytes[o + 0] = static_cast<std::uint8_t>(((r0 & 0xF) << 4) | (g0 & 0xF));
      p.bytes[o + 1] = static_cast<std::uint8_t>(((b0 & 0xF) << 4) | (r1 & 0xF));
      p.bytes[o + 2] = static_cast<std::uint8_t>(((g1 & 0xF) << 4) | (b1 & 0xF));
    };
    setPair(0, 14,14,15,  10,12,13);  // 16:starCool,  17:cloudCool
    setPair(1, 15,13, 9,   6, 6, 7);  // 18:warmGleam, 19:shipDark
    setPair(2,  9, 9,10,  13,13,13);  // 20:shipMid,   21:shipLight
    setPair(3,  4,12,14,  10, 6, 2);  // 22:shipVisor, 23:oreBody
    setPair(4, 14,10, 3,  14, 8, 5);  // 24:oreGold,   25:lavaCloudRim
    return p;
  }();

  constexpr std::uint8_t inkOut   = 0u;
  constexpr std::uint8_t sky0     = 1u;
  constexpr std::uint8_t sky1     = 2u;
  constexpr std::uint8_t sky2     = 3u;
  constexpr std::uint8_t sky3     = 4u;
  constexpr std::uint8_t gnd0     = 5u;
  constexpr std::uint8_t gnd1     = 6u;
  constexpr std::uint8_t gnd2     = 7u;
  constexpr std::uint8_t pathDark = 8u;
  constexpr std::uint8_t pathMid  = 9u;
  constexpr std::uint8_t pathHi   = 10u;
  const std::uint8_t rockDark     = oceanWorld ? gnd0 : pathDark;
  constexpr std::uint8_t rockMid  = 11u;
  constexpr std::uint8_t rockHi   = 12u;
  const std::uint8_t trunkCol     = oceanWorld ? pathDark : pathMid;
  constexpr std::uint8_t leaf0    = 13u;
  constexpr std::uint8_t leaf1    = 14u;
  const std::uint8_t leaf2        = oceanWorld ? sky3 : static_cast<std::uint8_t>(15u);
  const std::uint8_t accent0      = iceWorld   ? leaf1
                                  : oceanWorld ? static_cast<std::uint8_t>(15u)
                                  : lavaWorld  ? leaf1
                                  : (rockWorld && subBiome == 2) ? leaf1
                                               : rockHi;
  const std::uint8_t accent1      = iceWorld   ? static_cast<std::uint8_t>(15u)
                                  : oceanWorld ? static_cast<std::uint8_t>(18u)
                                  : lavaWorld  ? static_cast<std::uint8_t>(15u)
                                  : (rockWorld && subBiome == 2) ? static_cast<std::uint8_t>(15u)
                                               : static_cast<std::uint8_t>(18u);
  constexpr std::uint8_t kIdxStarCool  = 16u;
  constexpr std::uint8_t kIdxCloudCool = 17u;
  constexpr std::uint8_t kIdxWarmGleam = 18u;
  constexpr std::uint8_t kIdxShipDark  = 19u;
  constexpr std::uint8_t kIdxShipMid   = 20u;
  constexpr std::uint8_t kIdxShipLight = 21u;
  constexpr std::uint8_t kIdxShipVisor = 22u;
  constexpr std::uint8_t kIdxOreBody   = 23u;
  constexpr std::uint8_t kIdxOreGold   = 24u;
  constexpr std::uint8_t kIdxLavaRim   = 25u;
  constexpr int kBaseColorCount = 26;
  constexpr std::uint8_t kMidFogBit = 32u;  // 32..57: RGB4 midpoint blend toward sky3
  constexpr std::uint8_t kInkFogBase = 64u; // 64..68: discrete RGB4 atmospheric silhouette inks

  // Unpack active 4-4-4 RGB4 palette + pre-quantized RGB4 mid-haze and silhouette entries
  auto unpackNibbleRgb = [](const std::uint8_t* packed, int slot, int& r4, int& g4, int& b4) {
    const int base = (slot >> 1) * 3;
    if ((slot & 1) == 0) {
      r4 = (packed[base + 0] >> 4) & 0x0F;
      g4 =  packed[base + 0]       & 0x0F;
      b4 = (packed[base + 1] >> 4) & 0x0F;
    } else {
      r4 =  packed[base + 1]       & 0x0F;
      g4 = (packed[base + 2] >> 4) & 0x0F;
      b4 =  packed[base + 2]       & 0x0F;
    }
  };
  auto rgb4ToColor = [](int r4, int g4, int b4) -> Color {
    return Color{static_cast<unsigned char>(std::clamp(r4, 0, 15) * 17),
                 static_cast<unsigned char>(std::clamp(g4, 0, 15) * 17),
                 static_cast<unsigned char>(std::clamp(b4, 0, 15) * 17),
                 255};
  };

  int baseR4[kBaseColorCount]{};
  int baseG4[kBaseColorCount]{};
  int baseB4[kBaseColorCount]{};
  for (int i = 0; i < 16; ++i) {
    unpackNibbleRgb(kBiomePalettes.bytes[palIdx], i, baseR4[i], baseG4[i], baseB4[i]);
  }
  for (int i = 0; i < 10; ++i) {
    unpackNibbleRgb(kSharedSpritePalette.bytes, i, baseR4[16 + i], baseG4[16 + i], baseB4[16 + i]);
  }

  const bool inMine = (planetSurface_.inMine != 0u);
  const std::uint8_t fogTargetIdx = inMine ? inkOut : sky3;

  Color resolvedLut[72]{};
  const int fogR4 = baseR4[fogTargetIdx];
  const int fogG4 = baseG4[fogTargetIdx];
  const int fogB4 = baseG4[fogTargetIdx];
  for (int i = 0; i < kBaseColorCount; ++i) {
    resolvedLut[i] = rgb4ToColor(baseR4[i], baseG4[i], baseB4[i]);
    resolvedLut[kMidFogBit + i] = rgb4ToColor((baseR4[i] + fogR4 + 1) >> 1,
                                              (baseG4[i] + fogG4 + 1) >> 1,
                                              (baseB4[i] + fogB4 + 1) >> 1);
  }
  // 4 stepped RGB4 silhouette inks toward sky2 (d=4..6) + 1 horizon ridge crest toward sky3
  static constexpr int kInkWeights[4] = {4, 7, 9, 12};  // out of 16
  for (int k = 0; k < 4; ++k) {
    const int w = kInkWeights[k];
    resolvedLut[kInkFogBase + k] = rgb4ToColor(
        (baseR4[inkOut] * (16 - w) + baseR4[sky2] * w + 8) >> 4,
        (baseG4[inkOut] * (16 - w) + baseG4[sky2] * w + 8) >> 4,
        (baseB4[inkOut] * (16 - w) + baseB4[sky2] * w + 8) >> 4);
  }
  resolvedLut[kInkFogBase + 4] = rgb4ToColor(
      (baseR4[inkOut] * 7 + fogR4 * 9 + 8) >> 4,
      (baseG4[inkOut] * 7 + fogG4 * 9 + 8) >> 4,
      (baseB4[inkOut] * 7 + fogB4 * 9 + 8) >> 4);

  // Stack-allocated 240x144 8-bit indexed framebuffer (34,560 B scratch, 0 B heap)
  std::uint8_t idxBuf[W * H]{};

  constexpr int hy = 69;
  constexpr int kGndH = H - hy;
  constexpr int cxMid = W / 2;
  static constexpr float kProjY = 33.0f;
  static constexpr float kProjX = 126.0f;
  static constexpr float kHalfRoad = 0.44f;

  static constexpr int kBayer4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  struct FogMaskLut {
    std::uint8_t row[17][4]{};
  };
  static constexpr FogMaskLut kFogMasks = []() constexpr {
    FogMaskLut m{};
    for (int thr = 0; thr <= 16; ++thr) {
      for (int ry = 0; ry < 4; ++ry) {
        std::uint8_t bits = 0u;
        for (int rx = 0; rx < 4; ++rx) {
          if (kBayer4[ry][rx] < thr) {
            bits = static_cast<std::uint8_t>(bits | (1u << rx));
          }
        }
        m.row[thr][ry] = bits;
      }
    }
    return m;
  }();
  struct ScanlineProjLut {
    float z[kGndH]{};
    float invScaleX[kGndH]{};
    std::uint8_t fogStep[kGndH]{};  // 0..32 two-band 4x4 Bayer mist (0..16 -> midHaze, 16..32 -> sky3)
  };
  static constexpr ScanlineProjLut kScanLut = []() constexpr {
    ScanlineProjLut lut{};
    for (int gy = 0; gy < kGndH; ++gy) {
      const float denom = (static_cast<float>(gy) + 0.5f) < 1.125f
                              ? 1.125f
                              : (static_cast<float>(gy) + 0.5f);
      const float z = kProjY / denom;
      lut.z[gy] = z;
      lut.invScaleX[gy] = z / kProjX;
      if (z <= 2.40f) {
        lut.fogStep[gy] = 0u;
      } else {
        const float x = (z - 2.40f) * 0.34f;
        const float expPos = 1.0f + x + 0.5f * x * x + (x * x * x) * (1.0f / 6.0f);
        const float fog = 1.0f - (1.0f / expPos);
        const int st = static_cast<int>(fog * 32.0f + 0.5f);
        lut.fogStep[gy] = static_cast<std::uint8_t>(st > 31 ? 31 : (st < 0 ? 0 : st));
      }
    }
    return lut;
  }();
  // Discrete 0..32 mist/torchlight steps for painter depths d = 0..6
  static constexpr std::uint8_t kDepthFog[7] = {0u, 0u, 0u, 6u, 13u, 20u, 26u};
  static constexpr std::uint8_t kMineDepthFog[7] = {0u, 0u, 5u, 12u, 19u, 25u, 30u};

  // Hand-authored 32x32 PC-98 Might & Magic tileable pixel-art textures (3 KB ROM, 0 B heap):
  // '.'=0 (ink/mortar), ':'=1 (dark bevel/shadow), '+'=2 (mid stone), '#'=3 (sunlit highlight)
  struct TileTex32 {
    std::uint8_t px[32][32]{};
    static constexpr TileTex32 FromArt(const char (&art)[32][33]) {
      TileTex32 t{};
      for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
          const char c = art[y][x];
          t.px[y][x] = (c == '#') ? 3u : (c == '+') ? 2u : (c == ':') ? 1u : 0u;
        }
      }
      return t;
    }
  };
  // World of Xeen chunky rounded beveled dungeon / castle ashlar stone blocks
  static constexpr char kStoneWallArt[32][33] = {
      "................................",
      ".#########.#######..##########..",
      ".#++++++:#.#+++++:#.#++++++++:#.",
      ".#+:+++:+:.#++#++::.#+++#++:++:.",
      ".#+++++:::.#++:+++:.#+++++::++:.",
      ".#++:++:::.#+++++::.#++:++++++:.",
      ".::::::::...::::::..::::::::::..",
      "................................",
      "#####..###########..#########.##",
      "++++:#.#+++++++++:#.#+++++++:.#+",
      "++:++:.#++#+++:++::.#++#+++::.#+",
      "++++::.#++++:++++::.#++++++::.#+",
      "++:+::.#++:++++++::.#++:+:+::.#+",
      "+++:::..#++++++++:.##++++++::.#:",
      ":::::...::::::::::..:::::::::.::",
      "................................",
      ".##########..########..########.",
      ".#++++++++:#.#++++++:#.#++++++:.",
      ".#++#+++++::.#+++#++::.#++#+++:.",
      ".#++++:+++::.#+++++:++.#++++:+:.",
      ".#++:+++++::.#++:++++:.#++:+++:.",
      ".#+++++++:::.:++++++::.#++++++:.",
      ".::::::::::..::::::::..::::::::.",
      "................................",
      "#######..#########..##########.#",
      "++++++:#.#+++++++:#.#++++++++:.#",
      "++#+++::.#++#+++:+:.#+++#+++::.#",
      "++++:+::.#++++:++::.#++++:++::.#",
      "++:+++::.#++:++++::.#++:++++::.#",
      "+++++:::.#+++++++::.#+++++++::.#",
      ":::::::..:::::::::..::::::::::.:",
      "................................",
  };
  static constexpr TileTex32 kStoneWallTex = TileTex32::FromArt(kStoneWallArt);
  // Clouds of Xeen irregular rounded riverstone / cobblestone road tiles
  // ('.'=crisp dark stone outline, ':'=warm earth mortar, '+'=stone face, '#'=sunlit stone crown)
  static constexpr char kCobbleRoadArt[32][33] = {
      "::.#####.:::::.######.::::.###.:",
      ":.#######.:::.########.::.#####.",
      ":.#++++++.:.:.#+++++++.::.#++++.",
      ":.#++#+++.:::.#+++#+++.::.#++++.",
      "::.++++++.::::.+++++++.:::.++++.",
      ":::......::::::.......:::::....:",
      "::::::::::::::::::::::::::::::::",
      "####.:::::.########.::::.#######",
      "++++.::::.##########.::.########",
      "+++..::::.#+++++++++.::.#+++++++",
      "++..:::::.#+++#+++++.::.#++#++++",
      "...:::::::.+++++++++.:::.+++++++",
      ":::::::::::.........:::::.......",
      "::::::::::::::::::::::::::::::::",
      "::.######.::::.#######.::::.###.",
      ":.########.::.#########.::.#####",
      ":.#+++++++.::.#++++++++.::.#++++",
      ":.#++#++++.::.#+++#++++.::.#++++",
      "::.+++++++.:::.++++++++.:::.++++",
      ":::.......:::::........:::::....",
      "::::::::::::::::::::::::::::::::",
      "#####.::::.######.::::.#######.:",
      "+++++.:::.########.::.#########.",
      "++++..:::.#+++++++.::.#++++++++.",
      "+++..::::.#++#++++.::.#+++#++++.",
      "....::::::.+++++++.:::.++++++++.",
      ":::::::::::.......:::::........:",
      "::::::::::::::::::::::::::::::::",
      "::.#######.::::.######.::::.###.",
      ":.#########.::.########.::.#####",
      ":.#++++++++.::.#+++++++.::.#++++",
      "::.++++++++.:::.+++++++.:::.++++",
  };
  static constexpr TileTex32 kCobbleRoadTex = TileTex32::FromArt(kCobbleRoadArt);
  // Clouds of Xeen jagged faceted craggy mountain / boulder rock texture
  static constexpr char kCragRockArt[32][33] = {
      "##++::..###++:..##+++::.###+::..",
      "###++:..##+++::.###++:..##+++::.",
      "+##+++:.+##++::.++##++:.+##+++:.",
      ":+##++:::+##+::..+##++:::+#++::.",
      ".:++#++:.:++::..::++#++:.:++::..",
      "..:++:::..:::...:.::++::..:::...",
      "...::::...::.###:...:::...::.##.",
      ".##:::..:##+####++:..::..:##+###",
      "####+::.####++++++::.##+.####+++",
      "###++::.+###+++#++:..###++###+++",
      "+##+++:.+##+++++++::.+##+++##++:",
      ":+##++:::+##++:+++::.:+##+++#+::",
      ".:++++::.:++::.:++::..:++++++::.",
      "..:::....::.....:::....::::::...",
      "...:..####+::....:..####+::.....",
      ".##+:######++::.##+:######++::..",
      "####+###+++++::####+###++++++::.",
      "###++##++++++:.###++##+++#++++:.",
      "+##+++#+++#++:.+##+++#++++++++::",
      ":+##++#+++++::.:+##++#++++:+++:.",
      ".:+++++++:++::..:++++++::.:++::.",
      "..::+++:::++:...::+++::...:++:..",
      "...::::..:::.....::::.....:::...",
      "###+::...:..####+::...:..####+:.",
      "####++:.##+:#####++:.##+:#####+:",
      "+###++:####+##+++++:####+##++++:",
      ":+##++:+###+++#++++:+###+++++++:",
      ".:+##+::+##+++++++:::+##+++:++::",
      "..:+++:::+##++:+++::.:+++::.::..",
      "...::::..:++::::++:...::::......",
      "..::......::....::....::...###..",
      ".##++::..####+:..##++::..####+:.",
  };
  static constexpr TileTex32 kCragRockTex = TileTex32::FromArt(kCragRockArt);

  int activeFog = 0;  // 0..32 (two 16-step 4x4 Bayer bands in index space)
  std::uint8_t activeInk = inkOut;
  auto inkForFog = [&](int fog32) -> std::uint8_t {
    if (inMine || fog32 <= 6) { return inkOut; }
    if (fog32 <= 13) { return kInkFogBase + 0u; }
    if (fog32 <= 19) { return kInkFogBase + 1u; }
    if (fog32 <= 24) { return kInkFogBase + 2u; }
    return kInkFogBase + 3u;
  };
  auto fogIndexPair = [&](std::uint8_t idx, int fog32,
                          std::uint8_t& idxA, std::uint8_t& idxB, int& thr16) {
    const std::uint8_t mid = static_cast<std::uint8_t>(idx | kMidFogBit);
    if (fog32 <= 16) {
      idxA = idx;
      idxB = mid;
      thr16 = fog32;
    } else {
      idxA = mid;
      idxB = fogTargetIdx;
      thr16 = std::min(16, fog32 - 16);
    }
  };

  auto putPx = [&](int x, int y, std::uint8_t idx) {
    if (x >= 0 && x < W && y >= 0 && y < H) {
      if (idx == inkOut) {
        idxBuf[y * W + x] = activeInk;
        return;
      }
      if (activeFog > 0) {
        std::uint8_t idxA = idx, idxB = idx;
        int thr16 = 0;
        fogIndexPair(idx, activeFog, idxA, idxB, thr16);
        idx = (((kFogMasks.row[thr16][y & 3] >> (x & 3)) & 1u) != 0u) ? idxB : idxA;
      }
      idxBuf[y * W + x] = idx;
    }
  };
  auto bayerPx = [&](int x, int y, float t, std::uint8_t a, std::uint8_t b) {
    const int thr = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 16.0f);
    putPx(x, y, thr > kBayer4[y & 3][x & 3] ? b : a);
  };
  auto fillRect = [&](int x, int y, int w, int h, std::uint8_t idx) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(W, x + w);
    const int y1 = std::min(H, y + h);
    if (idx == inkOut) {
      for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
          idxBuf[py * W + px] = activeInk;
        }
      }
      return;
    }
    if (activeFog <= 0) {
      for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
          idxBuf[py * W + px] = idx;
        }
      }
    } else {
      std::uint8_t idxA = idx, idxB = idx;
      int thr16 = 0;
      fogIndexPair(idx, activeFog, idxA, idxB, thr16);
      for (int py = y0; py < y1; ++py) {
        const std::uint8_t rowMask = kFogMasks.row[thr16][py & 3];
        for (int px = x0; px < x1; ++px) {
          idxBuf[py * W + px] = (((rowMask >> (px & 3)) & 1u) != 0u) ? idxB : idxA;
        }
      }
    }
  };
  auto fillOutlineRect = [&](int x, int y, int w, int h, std::uint8_t fill, std::uint8_t rim) {
    fillRect(x, y, w, h, rim);
    if (w > 2 && h > 2) {
      fillRect(x + 1, y + 1, w - 2, h - 2, fill);
    }
  };
  // Fast integer organic ellipse/line helpers (zero sqrt/trig, tight bounds)
  auto fillDisc = [&](int cx, int cy, int rx, int ry, std::uint8_t col) {
    if (rx <= 0 || ry <= 0) { putPx(cx, cy, col); return; }
    const int y0 = std::max(0, cy - ry);
    const int y1 = std::min(H - 1, cy + ry);
    const int x0 = std::max(0, cx - rx);
    const int x1 = std::min(W - 1, cx + rx);
    if (x0 > x1 || y0 > y1) { return; }
    const int rx2 = rx * rx;
    const int ry2 = ry * ry;
    const int lim = rx2 * ry2;
    for (int py = y0; py <= y1; ++py) {
      const int dy = py - cy;
      const int dyTerm = dy * dy * rx2;
      for (int px = x0; px <= x1; ++px) {
        const int dx = px - cx;
        if (dx * dx * ry2 + dyTerm <= lim) {
          putPx(px, py, col);
        }
      }
    }
  };
  auto fillDitherDisc = [&](int cx, int cy, int rx, int ry,
                            std::uint8_t colA, std::uint8_t colB, int thr16) {
    if (rx <= 0 || ry <= 0) { putPx(cx, cy, colA); return; }
    const int y0 = std::max(0, cy - ry);
    const int y1 = std::min(H - 1, cy + ry);
    const int x0 = std::max(0, cx - rx);
    const int x1 = std::min(W - 1, cx + rx);
    if (x0 > x1 || y0 > y1) { return; }
    const int rx2 = rx * rx;
    const int ry2 = ry * ry;
    const int lim = rx2 * ry2;
    const int clampedThr = std::clamp(thr16, 0, 16);
    for (int py = y0; py <= y1; ++py) {
      const int dy = py - cy;
      const int dyTerm = dy * dy * rx2;
      const std::uint8_t rMask = kFogMasks.row[clampedThr][py & 3];
      for (int px = x0; px <= x1; ++px) {
        const int dx = px - cx;
        if (dx * dx * ry2 + dyTerm <= lim) {
          putPx(px, py, (((rMask >> (px & 3)) & 1u) != 0u) ? colB : colA);
        }
      }
    }
  };
  auto fillOutlinedDisc = [&](int cx, int cy, int rx, int ry,
                              std::uint8_t fill, std::uint8_t rim) {
    const int orx = std::max(1, rx + 1);
    const int ory = std::max(1, ry + 1);
    const int y0 = std::max(0, cy - ory);
    const int y1 = std::min(H - 1, cy + ory);
    const int x0 = std::max(0, cx - orx);
    const int x1 = std::min(W - 1, cx + orx);
    if (x0 > x1 || y0 > y1) { return; }
    const int orx2 = orx * orx, ory2 = ory * ory, olim = orx2 * ory2;
    const int irx = std::max(0, rx), iry = std::max(0, ry);
    const int irx2 = irx * irx, iry2 = iry * iry, ilim = irx2 * iry2;
    for (int py = y0; py <= y1; ++py) {
      const int dy = py - cy;
      const int ody = dy * dy * orx2;
      const int idy = dy * dy * irx2;
      for (int px = x0; px <= x1; ++px) {
        const int dx = px - cx;
        const int dx2 = dx * dx;
        if (dx2 * ory2 + ody <= olim) {
          const bool inside = (irx > 0 && iry > 0) && (dx2 * iry2 + idy <= ilim);
          putPx(px, py, inside ? fill : rim);
        }
      }
    }
  };
  auto drawHLine = [&](int x0, int x1, int y, std::uint8_t col) {
    if (y < 0 || y >= H) { return; }
    if (x0 > x1) { std::swap(x0, x1); }
    const int cx0 = std::max(0, x0);
    const int cx1 = std::min(W - 1, x1);
    if (cx0 <= cx1) { fillRect(cx0, y, cx1 - cx0 + 1, 1, col); }
  };

  static constexpr int kDx[4] = {0, 1, 0, -1};
  static constexpr int kDy[4] = {-1, 0, 1, 0};
  const int fdx = kDx[planetSurface_.dir];
  const int fdy = kDy[planetSurface_.dir];
  const int rdx = kDx[(planetSurface_.dir + 1) % 4];
  const int rdy = kDy[(planetSurface_.dir + 1) % 4];
  const int ldx = -rdx;
  const int ldy = -rdy;

  auto tileAt = [&](int tx, int ty) -> std::uint8_t {
    if (static_cast<unsigned>(tx) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(ty) >= static_cast<unsigned>(N)) {
      return 1u;
    }
    return planetSurface_.Tile(tx, ty);
  };
  auto variantAt = [&](int tx, int ty) -> int {
    if (static_cast<unsigned>(tx) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(ty) >= static_cast<unsigned>(N)) {
      return static_cast<int>((static_cast<unsigned>(tx + 32) * 7u +
                               static_cast<unsigned>(ty + 32) * 13u) & 7u);
    }
    return planetSurface_.Variant(tx, ty);
  };
  auto isBorderTile = [&](int tx, int ty) -> bool {
    return tx <= 0 || tx >= N - 1 || ty <= 0 || ty >= N - 1;
  };
  auto isPathTile = [&](int tx, int ty) -> bool {
    const std::uint8_t t = tileAt(tx, ty);
    return t == 2 || t == 3;
  };
  auto oreAt = [&](int tx, int ty) -> int {
    for (int i = 0; i < static_cast<int>(planetSurface_.oreCount); ++i) {
      if ((oreMask & (1u << i)) != 0u &&
          ((planetSurface_.oreLayerMask >> i) & 1u) == planetSurface_.inMine &&
          planetSurface_.oreX[i] == tx && planetSurface_.oreY[i] == ty) {
        return i;
      }
    }
    return -1;
  };
  auto isCliffCell = [&](int x, int y) -> bool {
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(y) >= static_cast<unsigned>(N)) {
      return false;
    }
    if (tileAt(x, y) != 1) { return false; }
    const int v = variantAt(x, y);
    if (inMine) { return (v & 4) != 0; }
    // On surface, ONLY ancient fortress/ruin masonry (var == 7, non-portal) forms vertical walls;
    // var 4..6 & surface mine portal are sculpted sloped organic mountains & domed cave mounds!
    if (x == static_cast<int>(planetSurface_.portalX) &&
        y == static_cast<int>(planetSurface_.portalY)) {
      return false;
    }
    return v == 7;
  };

  // True 1/z first-person pinhole projection (camera eye inside d=0, ~88 deg FOV)
  auto yAtZ = [&](float z) -> int {
    return hy + static_cast<int>(std::lround(kProjY / std::max(0.35f, z) - 0.5f));
  };
  auto halfTileAtZ = [&](float z) -> float {
    return (0.5f * kProjX) / std::max(0.35f, z);
  };
  auto tileCenterAtZ = [&](int s, float z) -> float {
    return static_cast<float>(cxMid) + static_cast<float>(s) * (kProjX / std::max(0.35f, z));
  };
  // Per-cell wall height for Might & Magic-style silhouette variety (low ruins, mid bluffs, towering crags)
  auto cliffTopScaleFor = [&](int tx, int ty, int var) -> float {
    if (inMine) { return 1.00f; }
    if (tx == static_cast<int>(planetSurface_.portalX) &&
        ty == static_cast<int>(planetSurface_.portalY)) {
      return 0.48f;
    }
    const unsigned h = (static_cast<unsigned>(tx) * 73u) ^
                       (static_cast<unsigned>(ty) * 151u) ^
                       (pSeed >> 5) ^ (static_cast<unsigned>(var) * 29u);
    const bool border = isBorderTile(tx, ty);
    if (var == 5) {
      // Towering crags, glacial peaks, basalt organ-pipes, sandstone hoodoo mesas
      const float base = border ? 0.68f : 0.56f;
      return base + static_cast<float>(h % 4u) * 0.09f;  // 0.56 .. 0.95
    }
    if (var == 7) {
      // Ancient keeps, ruined parapets & temple walls (low crumbled half-walls to tall battlements)
      static constexpr float kRuinH[4] = {0.26f, 0.42f, 0.54f, 0.66f};
      return kRuinH[h & 3u] + (border ? 0.10f : 0.0f);
    }
    if (var == 6) {
      // Mineral-veined / overgrown cliff faces
      return 0.36f + static_cast<float>(h % 3u) * 0.11f + (border ? 0.10f : 0.0f);
    }
    // var == 4: low weathered outcrops & rolling bluffs
    static constexpr float kBluffH[4] = {0.22f, 0.34f, 0.46f, 0.58f};
    return kBluffH[h & 3u] + (border ? 0.12f : 0.0f);
  };
  auto cliffX = [&](float z, float lat) -> int {
    return cxMid + static_cast<int>(std::lround(lat * (kProjX / z)));
  };
  auto cliffYBot = [&](float z) -> int {
    return hy + static_cast<int>(std::lround(kProjY / z - 0.5f));
  };
  auto cliffYTopScaled = [&](float z, float topScale) -> int {
    return hy - static_cast<int>(std::lround(kProjY * topScale / z - (inMine ? 0.5f : 0.0f)));
  };
  auto cliffYTop = [&](float z) -> int {
    return cliffYTopScaled(z, inMine ? 1.00f : 0.42f);
  };

  // Front-to-back 1D horizontal occlusion ([xlower, xupper] + 32 B 240-bit column mask)
  // and 16 B stack-bitset LOS (uint64_t vis[2] covering all 77 frustum cells, 0 B heap).
  constexpr int kOccWords = (W + 31) / 32;  // 8 words for 240 columns
  std::uint64_t vis[2]{};         // 16 B stack bitset: frustum slots d=0..6, s=-(d+2)..+(d+2)
  std::uint32_t colOcc[kOccWords]{};
  std::uint32_t depthColOcc[7][kOccWords]{};
  std::uint8_t rowX0[7]{};
  std::uint8_t rowX1[7]{};
  int xlower = 0;
  int xupper = W - 1;
  int maxVisD = 0;
  bool viewSealed = false;
  float maxSealZ = 0.50f;

  auto frustumBit = [](int d, int s) -> int {
    return d * d + 4 * d + (s + d + 2);
  };
  auto setVis = [&](int d, int s) {
    const int b = frustumBit(d, s);
    vis[b >> 6] |= (std::uint64_t{1} << (b & 63));
  };
  auto isVis = [&](int d, int s) -> bool {
    const int b = frustumBit(d, s);
    return ((vis[b >> 6] >> (b & 63)) & 1ULL) != 0ULL;
  };
  auto hasUnoccludedCol = [&](const std::uint32_t occ[kOccWords], int clipL, int clipR,
                              int x0, int x1) -> bool {
    x0 = std::max(clipL, x0);
    x1 = std::min(clipR, x1);
    if (x0 > x1) { return false; }
    const int w0 = x0 >> 5;
    const int w1 = x1 >> 5;
    const std::uint32_t m0 = ~0u << (x0 & 31);
    const std::uint32_t m1 = ~0u >> (31 - (x1 & 31));
    if (w0 == w1) {
      return ((~occ[w0]) & (m0 & m1)) != 0u;
    }
    if (((~occ[w0]) & m0) != 0u) { return true; }
    for (int w = w0 + 1; w < w1; ++w) {
      if (~occ[w] != 0u) { return true; }
    }
    return ((~occ[w1]) & m1) != 0u;
  };
  auto markColSpan = [&](int x0, int x1, float zDepth) {
    x0 = std::max(0, x0);
    x1 = std::min(W - 1, x1);
    if (x0 > x1) { return; }
    const int w0 = x0 >> 5;
    const int w1 = x1 >> 5;
    const std::uint32_t m0 = ~0u << (x0 & 31);
    const std::uint32_t m1 = ~0u >> (31 - (x1 & 31));
    bool addedNew = false;
    if (w0 == w1) {
      const std::uint32_t mask = m0 & m1;
      addedNew = ((~colOcc[w0]) & mask) != 0u;
      colOcc[w0] |= mask;
    } else {
      if (((~colOcc[w0]) & m0) != 0u) { addedNew = true; }
      colOcc[w0] |= m0;
      for (int w = w0 + 1; w < w1; ++w) {
        if (~colOcc[w] != 0u) { addedNew = true; }
        colOcc[w] = ~0u;
      }
      if (((~colOcc[w1]) & m1) != 0u) { addedNew = true; }
      colOcc[w1] |= m1;
    }
    if (addedNew && zDepth > maxSealZ) {
      maxSealZ = zDepth;
    }
    while (xlower <= xupper && ((colOcc[xlower >> 5] >> (xlower & 31)) & 1u) != 0u) {
      ++xlower;
    }
    while (xlower <= xupper && ((colOcc[xupper >> 5] >> (xupper & 31)) & 1u) != 0u) {
      --xupper;
    }
  };

  for (int d = 0; d <= 6; ++d) {
    if (xlower > xupper) {
      viewSealed = true;
      break;
    }
    for (int w = 0; w < kOccWords; ++w) {
      depthColOcc[d][w] = colOcc[w];
    }
    rowX0[d] = static_cast<std::uint8_t>(xlower);
    rowX1[d] = static_cast<std::uint8_t>(xupper);

    const int maxSRoad = d + 2;
    if (d == 0) {
      for (int s = -maxSRoad; s <= maxSRoad; ++s) {
        setVis(0, s);
      }
      maxVisD = 0;
    } else {
      const float zNearCell = static_cast<float>(d) - 0.50f;
      const float zFarCell  = static_cast<float>(d) + 0.50f;
      for (int s = -maxSRoad; s <= maxSRoad; ++s) {
        const float latL = static_cast<float>(s) - 0.50f;
        const float latR = static_cast<float>(s) + 0.50f;
        const int xMin = std::min(cliffX(zNearCell, latL), cliffX(zFarCell, latL)) - 9;
        const int xMax = std::max(cliffX(zNearCell, latR), cliffX(zFarCell, latR)) + 9;
        if (hasUnoccludedCol(depthColOcc[d], xlower, xupper, xMin, xMax)) {
          setVis(d, s);
          maxVisD = d;
        }
      }
    }

    // Full-height wall occluders narrow [xlower, xupper] and set colOcc bits for deeper layers.
    // On the surface, walls vary in height so only full sky-blocking heights occlude columns
    // (letting towering crags & keeps peek cleanly above shorter foreground ruins).
    const bool canOccludeDeeper = inMine ? (d <= 5) : (d >= 1 && d <= 2);
    if (canOccludeDeeper) {
      const int maxSOcc = d + 1;
      for (int s = -maxSOcc; s <= maxSOcc; ++s) {
        if (d == 0 && s == 0) { continue; }
        if (!isVis(d, s)) { continue; }
        const int tx = planetSurface_.px + fdx * d + rdx * s;
        const int ty = planetSurface_.py + fdy * d + rdy * s;
        if (!isCliffCell(tx, ty)) { continue; }
        const int occVar = variantAt(tx, ty);
        const float occTopScale = cliffTopScaleFor(tx, ty, occVar);
        if (!inMine && occTopScale < 0.85f) { continue; }
        if (d == 0) {
          if (std::abs(s) == 1) {
            markColSpan((s < 0) ? 0 : (W - 9), (s < 0) ? 8 : (W - 1), 0.50f);
          }
          continue;
        }
        const float zNearW = static_cast<float>(d) - 0.50f;
        const float zFarW  = static_cast<float>(d) + 0.50f;
        if (!isCliffCell(tx - fdx, ty - fdy)) {
          const int xL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
          const int xR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
          const int yTBase = cliffYTopScaled(zNearW, occTopScale);
          const int yB = std::clamp(cliffYBot(zNearW), 0, H - 1);
          if (xR >= 0 && xL < W && xR >= xL && yTBase < yB) {
            markColSpan(xL, xR, zNearW);
          }
        }
        if (inMine) {
          if (s < 0 && !isCliffCell(tx + rdx, ty + rdy)) {
            const int xNear = cliffX(zNearW, static_cast<float>(s) + 0.50f);
            const int xFar  = cliffX(zFarW, static_cast<float>(s) + 0.50f);
            markColSpan(xNear, xFar - 1, zFarW);
          } else if (s > 0 && !isCliffCell(tx + ldx, ty + ldy)) {
            const int xNear = cliffX(zNearW, static_cast<float>(s) - 0.50f);
            const int xFar  = cliffX(zFarW, static_cast<float>(s) - 0.50f);
            markColSpan(xFar + 1, xNear, zFarW);
          }
        }
      }
      if (xlower > xupper) {
        viewSealed = true;
        break;
      }
    }
  }

  // When full-height mine walls seal all 240 columns at maxSealZ, skip 100%-overwritten ceiling/floor rows
  const int ceilEndY = (inMine && viewSealed)
                           ? std::clamp(cliffYTop(maxSealZ) + 1, 0, hy)
                           : hy;
  const int floorStartY = (inMine && viewSealed)
                              ? std::clamp(cliffYBot(maxSealZ), hy, H)
                              : hy;

  if (!inMine) {
    // Authentic PC-98 sky: 4x4 Bayer bands accented with classic horizontal interlace transitions
    for (int y = 0; y < hy; ++y) {
      const float u = static_cast<float>(y) / static_cast<float>(hy);
      std::uint8_t cA = sky0, cB = sky1;
      float bandT = 0.0f;
      if (u < 0.34f) {
        cA = sky0; cB = sky1; bandT = u / 0.34f;
      } else if (u < 0.68f) {
        cA = sky1; cB = sky2; bandT = (u - 0.34f) / 0.34f;
      } else {
        cA = sky2; cB = sky3; bandT = (u - 0.68f) / 0.32f;
      }
      // Subtle PC-98 horizontal line-interlace modulation at mid-band boundaries
      const int baseThr = static_cast<int>(std::clamp(bandT, 0.0f, 1.0f) * 16.0f);
      const int thr = (baseThr >= 5 && baseThr <= 11)
                          ? std::clamp(baseThr + ((y & 1) ? 2 : -2), 0, 16)
                          : baseThr;
      const std::uint8_t rowMask = kFogMasks.row[thr][y & 3];
      for (int x = 0; x < W; ++x) {
        idxBuf[y * W + x] = (((rowMask >> (x & 3)) & 1u) != 0u) ? cB : cA;
      }
    }

    // Starfield (dense 2-tone + twinkling crosses on airless/night worlds, subtle upper-sky stars by day)
    const int starCount = airlessWorld ? 72 : nightSky ? 46 : duskSky ? 26 : 14;
    const int starMaxY  = airlessWorld ? (hy - 6) : nightSky ? (hy - 14) : duskSky ? 34 : 22;
    for (int st = 0; st < starCount; ++st) {
      const unsigned sh = static_cast<unsigned>(st) * 2654435761u ^ pSeed;
      const int sx = 3 + static_cast<int>((sh + static_cast<unsigned>(planetSurface_.dir) * 59u) %
                                          static_cast<unsigned>(W - 6));
      const int sy = 2 + static_cast<int>((sh >> 8) % static_cast<unsigned>(std::max(8, starMaxY)));
      const std::uint8_t sCol =
          ((sh & 3u) == 0u) ? kIdxWarmGleam
                            : ((sh & 1u) == 0u) ? kIdxStarCool : kIdxCloudCool;
      putPx(sx, sy, sCol);
      if ((nightSky || airlessWorld) && (sh % 11u) == 0u && sy > 3 && sy < starMaxY - 2) {
        putPx(sx - 1, sy, sky2);
        putPx(sx + 1, sy, sky2);
        putPx(sx, sy - 1, sky2);
        putPx(sx, sy + 1, sky2);
        putPx(sx, sy, kIdxStarCool);
      }
    }

    // Ice sub-biome 1: sweeping PC-98 Aurora Borealis vertical curtains across night sky
    if (iceWorld && subBiome == 1) {
      for (int x = 6; x < W - 6; ++x) {
        const float af = static_cast<float>(x + planetSurface_.dir * 52 + static_cast<int>(pSeed & 31u));
        const float w1 = std::sin(af * 0.042f);
        const float w2 = std::sin(af * 0.095f + 1.4f);
        const float env = 0.55f + 0.45f * std::sin(af * 0.021f + 0.6f);
        const int baseY = 9 + static_cast<int>(std::lround((w1 * 5.0f + w2 * 3.0f) + 7.0f));
        const int ribbonH = 8 + static_cast<int>(std::lround(env * 14.0f));
        for (int k = 0; k < ribbonH; ++k) {
          const int py = baseY + k;
          if (py < 2 || py >= hy - 14) { continue; }
          const int distEdge = std::min(k + 1, ribbonH - k);
          const int density = std::min(15, (distEdge * 22) / std::max(1, ribbonH));
          if (kBayer4[py & 3][x & 3] < density && ((x + (py >> 2)) & 1) == 0) {
            putPx(x, py, (k < 4) ? leaf2 : (k < ribbonH * 2 / 3) ? leaf1 : accent1);
          }
        }
      }
    }

    // Celestial landmarks with compass parallax (ringed gas giant, cratered moon, coronal sun, twin moons)
    {
      const int celX = 28 + ((static_cast<int>(pSeed >> 14) + (3 - planetSurface_.dir) * 56) % (W - 56));
      const int celY = 16 + static_cast<int>((pSeed >> 18) % 14u);
      const std::uint8_t celHi  = (lavaWorld || duskSky) ? kIdxWarmGleam : kIdxStarCool;
      const std::uint8_t celMid = (lavaWorld || duskSky) ? sky3 : kIdxCloudCool;
      const std::uint8_t celShd = sky0;

      if (celestialKind == 0) {
        // Ringed gas giant: back ring -> full sphere (lit bands + shaded night hemisphere) -> front ring
        const int r = 11 + static_cast<int>((pSeed >> 20) & 3u);
        const int ringRx = r * 2 + 5;
        for (int rx = -ringRx; rx <= ringRx; ++rx) {
          const int ryBack = celY - (rx * 3) / 10 - 2;
          if (rx * rx > (r - 1) * (r - 1) && ryBack >= 1 && ryBack < hy - 6) {
            putPx(celX + rx, ryBack, celMid);
            putPx(celX + rx, ryBack - 1, ((rx & 1) == 0) ? sky2 : celMid);
          }
        }
        const int r2 = r * r;
        for (int dy = -r; dy <= r; ++dy) {
          const int py = celY + dy;
          if (py < 1 || py >= hy - 8) { continue; }
          for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r2) { continue; }
            const int px = celX + dx;
            const int lit = (-dx * 10 - dy * 3) + r * 4;  // upper-left sunlight
            const int band = ((dy + r) / 3) & 1;
            if (lit < 0) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < 4) ? sky1 : celShd);
            } else if (lit < r * 4) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < (lit * 16) / (r * 4)) ? sky2 : celShd);
            } else if (lit < r * 8) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < (band ? 11 : 7)) ? celMid : sky2);
            } else {
              putPx(px, py, (band && (py & 1) == 0) ? celMid : celHi);
            }
          }
        }
        for (int rx = -ringRx; rx <= ringRx; ++rx) {
          const int ryFront = celY - (rx * 3) / 10 + 1;
          if (ryFront >= 1 && ryFront < hy - 6) {
            putPx(celX + rx, ryFront, celHi);
            putPx(celX + rx, ryFront + 1, ((rx & 1) == 0) ? celMid : sky2);
          }
        }
      } else if (celestialKind == 1) {
        // Curved crescent / gibbous moon with offset-circle shadow cutout + crater detail
        const int r = 10 + static_cast<int>((pSeed >> 20) & 3u);
        const int r2 = r * r;
        const int shOffX = r * 55 / 100;
        const int shOffY = -r / 6;
        const int shR2 = (r * 92 / 100) * (r * 92 / 100);
        for (int dy = -r; dy <= r; ++dy) {
          const int py = celY + dy;
          if (py < 1 || py >= hy - 8) { continue; }
          for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r2) { continue; }
            const int px = celX + dx;
            const int sdx = dx - shOffX;
            const int sdy = dy - shOffY;
            const int dSh = sdx * sdx + sdy * sdy;
            if (dSh < shR2 - r * 3) {
              if (airlessWorld && kBayer4[py & 3][px & 3] < 4) { putPx(px, py, sky1); }
              continue;
            }
            if (dSh < shR2 + r * 2) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < 9) ? celMid : sky1);
              continue;
            }
            const bool crater = ((dx + 4) * (dx + 4) + (dy - 2) * (dy - 2) <= 4) ||
                                ((dx + 2) * (dx + 2) + (dy + 4) * (dy + 4) <= 3);
            putPx(px, py, crater ? celMid : celHi);
          }
        }
      } else if (celestialKind == 2) {
        // Low sun with horizontal PC-98 interlace corona (plus binary companion on odd seeds)
        const int sunY = std::min(hy - 18, celY + 10);
        const int r = 7;
        fillDitherDisc(celX, sunY, r + 7, r + 5, sky2, sky3, 9);
        fillDitherDisc(celX, sunY, r + 3, r + 2, sky3, celHi, 10);
        fillDisc(celX, sunY, r, r, celHi);
        if ((pSeed & 1u) != 0u) {
          fillDitherDisc(celX + 16, sunY - 7, 5, 5, sky3, kIdxWarmGleam, 8);
          fillDisc(celX + 16, sunY - 7, 3, 3, kIdxWarmGleam);
        }
      } else {
        // Twin moons (primary cratered disc + distant companion moonlet)
        const int r = 8;
        fillDitherDisc(celX, celY, r, r, celMid, celHi, 10);
        fillDisc(celX - 2, celY - 2, r - 3, r - 3, celHi);
        fillDisc(celX + 2, celY + 2, 2, 2, celMid);
        fillDisc(celX + 18, celY - 5, 3, 3, celMid);
        putPx(celX + 17, celY - 6, celHi);
        putPx(celX + 18, celY - 6, celHi);
      }
    }

    // Expansive, sweeping PC-98 Clouds of Xeen cumulus cloud banks with flat wispy undersides
    if (!airlessWorld) {
      const std::uint8_t cRim = lavaWorld ? kIdxLavaRim
                              : duskSky   ? kIdxWarmGleam
                              : nightSky  ? sky3
                                          : kIdxStarCool;
      const std::uint8_t cMid = lavaWorld ? sky3
                              : duskSky   ? sky3
                              : nightSky  ? sky2
                                          : kIdxCloudCool;
      const std::uint8_t cBot = nightSky ? sky1 : sky2;
      auto drawCloudBank = [&](int cx, int cy, int cw, int ch, unsigned seed) {
        // Flat horizontal wispy cloud-bank base + asymmetric multi-lobe billowing cumulus tops
        // (cloudStyle == 1 produces sleeker stretched wind-swept cumulus banks, never thin bars)
        const int effCh = (cloudStyle == 1) ? std::max(3, ch * 65 / 100) : ch;
        const int effCw = (cloudStyle == 1) ? (cw + 14) : cw;
        const int baseY = cy + effCh;
        const int lobes = 5;
        for (int i = 0; i < lobes; ++i) {
          const int lx = cx - effCw * 38 / 100 + (i * effCw * 76 / 100) / (lobes - 1)
                       + static_cast<int>(((seed + static_cast<unsigned>(i) * 13u) % 5u)) - 2;
          const int distEdge = std::min(i + 1, lobes - i);
          const int rx = std::max(5, effCw / 5 + distEdge * 2);
          const int ry = std::max(2, (effCh * (distEdge + 1)) / 3 + static_cast<int>((seed >> (i * 2)) & 3u));
          const int ly = baseY - ry * 7 / 10;
          fillDisc(lx, ly + 1, rx + 1, ry, cBot);
          fillDisc(lx, ly, rx, ry, cMid);
          fillDisc(lx - 1, ly - 1, std::max(3, rx - 2), std::max(2, ry - 1), cRim);
        }
        // Crisp flat cumulus underside trimmed with horizontal PC-98 interlace dither
        for (int py = baseY + 1; py <= baseY + 3; ++py) {
          if (py >= hy - 6) { break; }
          const int shrink = (py - baseY) * 4;
          for (int px = cx - effCw / 2 - 4 + shrink; px <= cx + effCw / 2 + 4 - shrink; ++px) {
            if (py == baseY + 1) {
              putPx(px, py, (((px + py) & 1) == 0) ? cMid : cBot);
            } else if (((px + py) & 3) == 0) {
              putPx(px, py, cBot);
            }
          }
        }
        // Trailing wispy PC-98 cloud tails extending left/right along the underside
        drawHLine(cx - effCw / 2 - 14, cx - effCw / 3, baseY, cRim);
        drawHLine(cx - effCw / 2 - 10, cx - effCw / 4, baseY + 1, cMid);
        drawHLine(cx + effCw / 3, cx + effCw / 2 + 12, baseY, cRim);
        drawHLine(cx + effCw / 4, cx + effCw / 2 + 8, baseY + 1, cMid);
      };
      const int cOff = (3 - planetSurface_.dir) * 52;
      drawCloudBank(38 + (cOff + static_cast<int>(pSeed & 31u)) % 120, 12, 68, 7, pSeed);
      drawCloudBank(182 - ((planetSurface_.dir * 37 + static_cast<int>((pSeed >> 4) & 31u)) % 96), 21, 56, 6, pSeed >> 3);
      drawCloudBank(96 + ((planetSurface_.dir * 43 + static_cast<int>((pSeed >> 7) & 31u)) % 88), 33, 44, 5, pSeed >> 7);
    }

    // Two-layer parallax horizon silhouettes varied by biome & planet horizonStyle
    // Uses incommensurate integer triangle waves (no repeating shark-tooth pattern)
    auto triWave = [](int v, int period) -> int {
      const int m = ((v % period) + period) % period;
      const int half = period / 2;
      return half - std::abs(m - half);
    };
    // Layer 1: Far atmospheric range blended softly toward sky3
    activeFog = 24;
    activeInk = kInkFogBase + 4u;
    int prevFarH = 6;
    for (int x = 0; x < W; ++x) {
      const int ph = x + planetSurface_.dir * 64 + static_cast<int>((pSeed >> 6) & 127u);
      int farH = 6;
      if (horizonStyle == 1 || (rockWorld && horizonStyle != 2)) {
        // Irregular flat-topped mesas & broad canyon gaps
        const int seg = (ph / 46);
        const int r = ph % 46;
        const int mesaW = 18 + ((seg * 7 + static_cast<int>(pSeed & 7u)) % 14);
        const int mesaH = 10 + ((seg * 5) % 7);
        if (r >= 6 && r <= 6 + mesaW) {
          farH = (r < 9 || r > 3 + mesaW) ? (mesaH - 3) : mesaH;
        } else {
          farH = 4 + (triWave(ph, 37) * 3) / 18;
        }
      } else if (horizonStyle == 2 || lavaWorld || iceWorld) {
        // Sweeping asymmetric alpine peaks & volcanic calderas
        const int w1 = triWave(ph, 86);
        const int w2 = triWave(ph + 29, 54);
        const int w3 = triWave(ph * 2 + 13, 38);
        farH = 4 + (w1 * 12) / 43 + (w2 * 6) / 27 + (w3 * 3) / 19;
      } else {
        // Broad rolling highlands / coastal ridges
        const int w1 = triWave(ph, 98);
        const int w2 = triWave(ph + 41, 62);
        const int w3 = triWave(ph + 19, 34);
        farH = 4 + (w1 * 9) / 49 + (w2 * 5) / 31 + (w3 * 2) / 17;
      }
      if (oceanWorld) { farH = std::max(2, farH - 5); }
      const int yTop = std::max(8, hy - farH);
      const bool litFar = (farH >= prevFarH);
      prevFarH = farH;
      putPx(x, yTop - 1, sky2);
      for (int y = yTop; y < hy; ++y) {
        putPx(x, y, (y == yTop && litFar) ? rockHi : rockMid);
      }
    }

    // Distant Might & Magic-style overworld horizon landmarks (compass-anchored per planet)
    {
      const int lmX = 36 + ((static_cast<int>(pSeed >> 9) + (3 - planetSurface_.dir) * 62) % (W - 72));
      activeFog = 22;
      activeInk = kInkFogBase + 4u;
      if (lavaWorld) {
        // Smoking caldera cone + distant obsidian spire
        for (int k = 0; k < 16; ++k) {
          const int py = hy - 14 - k;
          const int drift = (k * k) / 14;
          const int rw = 2 + k / 3;
          fillDitherDisc(lmX + drift, py, rw, 2, sky1, rockDark, 8);
        }
        // Jagged obsidian citadel spire beside the ridge
        const int sx = (lmX > W / 2) ? (lmX - 42) : (lmX + 42);
        fillRect(sx - 4, hy - 16, 9, 16, rockDark);
        fillRect(sx - 2, hy - 23, 5, 8, rockDark);
        fillRect(sx - 1, hy - 28, 3, 6, rockMid);
        putPx(sx, hy - 29, accent0);
        putPx(sx, hy - 19, kIdxWarmGleam);  // distant beacon window
      } else if (biomeIdx == 0) {
        // Distant ruined castle keep & watchtower perched on a hilltop
        fillRect(lmX - 10, hy - 14, 21, 14, rockDark);
        fillRect(lmX - 9, hy - 13, 8, 12, rockMid);
        // Left keep tower + crenels
        fillRect(lmX - 10, hy - 22, 7, 9, rockDark);
        fillRect(lmX - 9, hy - 21, 3, 7, rockMid);
        putPx(lmX - 10, hy - 23, rockHi);
        putPx(lmX - 7, hy - 23, rockHi);
        putPx(lmX - 4, hy - 23, rockHi);
        // Right broken watchtower + glowing window
        fillRect(lmX + 4, hy - 19, 6, 6, rockDark);
        putPx(lmX + 5, hy - 20, rockHi);
        putPx(lmX + 8, hy - 20, rockHi);
        putPx(lmX - 7, hy - 18, kIdxWarmGleam);
        putPx(lmX + 6, hy - 16, kIdxWarmGleam);
      } else if (iceWorld) {
        // Towering glacial crystal citadel spire on the distant horizon
        for (int row = 0; row < 22; ++row) {
          const int hw = std::max(1, (22 - row) / 3);
          drawHLine(lmX - hw, lmX + hw, hy - 4 - row, (row > 14) ? rockHi : rockMid);
          drawHLine(lmX, lmX + hw, hy - 4 - row, rockDark);
        }
        putPx(lmX, hy - 27, accent1);
        fillRect(lmX - 11, hy - 15, 4, 15, rockMid);
        fillRect(lmX + 8, hy - 13, 4, 13, rockDark);
        putPx(lmX - 10, hy - 16, accent1);
      } else if (oceanWorld) {
        // Distant sunken coastal aqueduct arches & sea-beacon lighthouse
        for (int a = 0; a < 3; ++a) {
          const int ax = lmX - 14 + a * 9;
          fillRect(ax, hy - 11, 9, 3, rockMid);
          fillRect(ax, hy - 8, 2, 8, rockDark);
          fillRect(ax + 7, hy - 8, 2, 8, rockDark);
        }
        fillRect(lmX + 15, hy - 18, 4, 18, rockMid);
        fillRect(lmX + 17, hy - 18, 2, 18, rockDark);
        putPx(lmX + 16, hy - 19, kIdxWarmGleam);
        putPx(lmX + 17, hy - 19, kIdxWarmGleam);
      } else {
        // Rock / Badlands: colossal stepped desert ziggurat / alien obelisk silhouette
        if ((pSeed & 1u) == 0u) {
          fillRect(lmX - 15, hy - 7, 31, 7, rockDark);
          fillRect(lmX - 11, hy - 12, 23, 5, rockMid);
          fillRect(lmX - 7, hy - 17, 15, 5, rockDark);
          fillRect(lmX - 3, hy - 21, 7, 4, rockHi);
          putPx(lmX, hy - 22, accent1);
        } else {
          // Tall needle monolith obelisk
          fillRect(lmX - 3, hy - 24, 7, 24, rockDark);
          fillRect(lmX - 3, hy - 24, 3, 24, rockMid);
          drawHLine(lmX - 2, lmX + 2, hy - 25, rockHi);
          drawHLine(lmX - 1, lmX + 1, hy - 26, accent1);
          putPx(lmX, hy - 27, accent1);
        }
      }
    }

    // Layer 2: Nearer detailed horizon ridge with sunlit/shadowed facets & 1px atmospheric crest
    activeFog = 18;
    activeInk = kInkFogBase + 4u;
    int prevNearH = 4;
    for (int x = 0; x < W; ++x) {
      const int ph = x + planetSurface_.dir * 84 + static_cast<int>((pSeed >> 11) & 63u);
      int nearH = 4;
      if (rockWorld && horizonStyle == 1) {
        const int blk = (ph / 38);
        const int rem = ph % 38;
        nearH = ((blk % 3) == 0 && rem > 5 && rem < 28) ? 9 : 3 + (triWave(ph, 29) * 3) / 14;
      } else if (oceanWorld) {
        const int w1 = triWave(ph, 72);
        const int w2 = triWave(ph + 19, 36);
        nearH = (w1 > 24) ? (3 + (w1 - 24) / 2 + w2 / 9) : 2;
      } else {
        const int p1 = triWave(ph, 68);
        const int p2 = triWave(ph + 23, 42);
        const int p3 = triWave(ph + 7, 22);
        nearH = 3 + (p1 * 6) / 34 + (p2 * 4) / 21 + (p3 * 2) / 11;
      }
      const int yTop = std::max(14, hy - nearH);
      putPx(x, yTop - 1, inkOut);
      const bool litSlope = (nearH >= prevNearH);
      prevNearH = nearH;
      for (int k = 0; k < nearH; ++k) {
        std::uint8_t col = rockDark;
        if (k == 0 && litSlope) {
          col = (iceWorld || (biomeIdx == 0 && subBiome == 1)) ? leaf2 : rockHi;
        } else if (k <= 2 && litSlope && ((x + k) & 1) == 0) {
          col = rockMid;
        }
        putPx(x, yTop + k, col);
      }
    }
    activeFog = 0;
    activeInk = inkOut;
  } else {
    // Underground foreshortened World of Xeen timber-raftered stone ceiling (0 B extra LUTs)
    const std::uint8_t ceilDarkMid = static_cast<std::uint8_t>(rockDark | kMidFogBit);
    const std::uint8_t ceilLitMid = static_cast<std::uint8_t>(rockMid | kMidFogBit);
    const std::uint8_t dripCol = iceWorld ? accent1
                               : lavaWorld ? accent0
                               : (biomeIdx == 0) ? leaf0
                                                 : rockMid;
    for (int y = 0; y < ceilEndY; ++y) {
      const int cy = (hy - 1) - y;
      const int baseFog = static_cast<int>(kScanLut.fogStep[cy]);
      const int f32 = (baseFog > 0) ? std::min(31, baseFog + 5) : 0;
      activeFog = f32;
      activeInk = inkOut;
      const std::uint8_t fogMaskRow =
          (f32 > 0) ? kFogMasks.row[(f32 <= 16) ? f32 : std::min(16, f32 - 16)][y & 3] : 0u;
      const float u = static_cast<float>(cy) / static_cast<float>(hy);
      const float zCeil = kScanLut.z[cy];
      const float zCellFrac = zCeil - std::floor(zCeil + 0.5f) + 0.5f;  // 0..1 within cell depth
      const bool isCrossBeam = (zCeil < 4.5f) && (zCellFrac < 0.16f);
      const bool isCrossEdge = (zCeil < 4.5f) && (zCellFrac < 0.04f || (zCellFrac >= 0.13f && zCellFrac < 0.17f));
      const bool isCrossHi   = (zCeil < 4.5f) && (zCellFrac >= 0.04f && zCellFrac < 0.08f);
      const float invScaleX = kScanLut.invScaleX[cy];
      const int gradThr = static_cast<int>(std::clamp((u - 0.12f) * 0.82f, 0.0f, 1.0f) * 16.0f);
      for (int x = 0; x < W; ++x) {
        std::uint8_t cIdx = rockDark;
        if (isCrossEdge) {
          cIdx = inkOut;
        } else if (isCrossBeam) {
          // Perspective overhead wooden dungeon ceiling rafter beam
          cIdx = isCrossHi ? pathHi : ((((x + y) & 3) == 0) ? pathDark : trunkCol);
        } else {
          // Check longitudinal side-beam timber runners along corridor edges (lat ~ +-0.46)
          const float absLat = std::abs((static_cast<float>(x) + 0.5f - static_cast<float>(cxMid)) * invScaleX);
          const float latMod = absLat - std::floor(absLat);
          if (zCeil < 4.2f && (latMod > 0.41f && latMod < 0.53f)) {
            cIdx = (latMod < 0.43f || latMod > 0.51f) ? inkOut
                 : (latMod < 0.46f)                   ? pathHi
                                                      : trunkCol;
          } else {
            const bool pickB = gradThr > kBayer4[(y + 2) & 3][(x + 1) & 3];
            cIdx = pickB ? rockMid : rockDark;
          }
        }
        if (f32 > 0) {
          const std::uint8_t cMid = (cIdx == rockDark) ? ceilDarkMid
                                  : (cIdx == rockMid)  ? ceilLitMid
                                                       : static_cast<std::uint8_t>(cIdx | kMidFogBit);
          const bool mBit = ((fogMaskRow >> (x & 3)) & 1u) != 0u;
          cIdx = (cIdx == inkOut) ? inkOut
               : (f32 <= 16)      ? (mBit ? cMid : cIdx)
                                  : (mBit ? inkOut : cMid);
        }
        idxBuf[y * W + x] = cIdx;
      }
    }
    // Subtle corner stalactites / damp mineral drips tucked between rafters
    activeFog = 0;
    activeInk = inkOut;
    if (ceilEndY > 14) {
      for (int st = 0; st < 7; ++st) {
        const unsigned sh = static_cast<unsigned>(st) * 374761393u ^
                            (static_cast<unsigned>(planetSurface_.px) * 668265263u) ^
                            (static_cast<unsigned>(planetSurface_.py) * 2147483647u) ^
                            (static_cast<unsigned>(planetSurface_.dir) * 1274126177u);
        const int sx = 14 + static_cast<int>(sh % static_cast<unsigned>(W - 28));
        const int len = 4 + static_cast<int>((sh >> 8) % 6u);
        const int halfW = 1 + static_cast<int>((sh >> 12) % 2u);
        if (len >= ceilEndY - 4) { continue; }
        for (int dy = 0; dy < len; ++dy) {
          const int hw = std::max(0, halfW - (dy * (halfW + 1)) / len);
          putPx(sx - hw - 1, dy, inkOut);
          putPx(sx + hw + 1, dy, inkOut);
          for (int px = sx - hw; px <= sx + hw; ++px) {
            putPx(px, dy, (px == sx - hw) ? dripCol : rockDark);
          }
        }
        putPx(sx, len, inkOut);
      }
    }
  }

  // World-anchored PC-98 4x4 Bayer dithered ground + biome-specific perspective surface textures
  // (pure 16.16 fixed-point horizontal stepping — zero inner-loop float math or division)
  const std::uint8_t floorA = inMine ? rockDark : gnd0;
  const std::uint8_t floorB = inMine ? rockMid  : gnd1;
  const std::uint8_t floorC = inMine ? rockMid  : gnd2;
  const float gndBaseWx = static_cast<float>(planetSurface_.px) + 0.5f;
  const float gndBaseWy = static_cast<float>(planetSurface_.py) + 0.5f;
  for (int y = floorStartY; y < H; ++y) {
    const int gy = y - hy;
    const int baseFog = static_cast<int>(kScanLut.fogStep[gy]);
    const int f32 = (inMine && baseFog > 0) ? std::min(31, baseFog + 4) : baseFog;
    activeFog = f32;
    activeInk = inkForFog(f32);
    const std::uint8_t fogMaskRow =
        (f32 > 0) ? kFogMasks.row[(f32 <= 16) ? f32 : std::min(16, f32 - 16)][y & 3] : 0u;
    const float u = static_cast<float>(gy) / static_cast<float>(kGndH);
    const int gradThr = static_cast<int>(
        std::clamp((u < 0.45f) ? (u / 0.45f) : ((u - 0.45f) / 0.55f), 0.0f, 1.0f) * 16.0f);
    const float z = kScanLut.z[gy];
    const bool detailRow = (z < 3.5f);
    std::int32_t wx_fp = 0, wy_fp = 0, dwx_fp = 0, dwy_fp = 0;
    if (detailRow) {
      const float invScaleX = kScanLut.invScaleX[gy];
      const float lat0 = (0.5f - static_cast<float>(cxMid)) * invScaleX;
      const float wx0 = (gndBaseWx + static_cast<float>(fdx) * z + static_cast<float>(rdx) * lat0) * 16.0f;
      const float wy0 = (gndBaseWy + static_cast<float>(fdy) * z + static_cast<float>(rdy) * lat0) * 16.0f;
      wx_fp  = static_cast<std::int32_t>(std::lround(wx0 * 65536.0f));
      wy_fp  = static_cast<std::int32_t>(std::lround(wy0 * 65536.0f));
      dwx_fp = static_cast<std::int32_t>(std::lround(static_cast<float>(rdx) * invScaleX * 16.0f * 65536.0f));
      dwy_fp = static_cast<std::int32_t>(std::lround(static_cast<float>(rdy) * invScaleX * 16.0f * 65536.0f));
    }

    for (int x = 0; x < W; ++x, wx_fp += dwx_fp, wy_fp += dwy_fp) {
      const bool pickB = gradThr > kBayer4[(y + (inMine ? 2 : 0)) & 3][(x + (inMine ? 1 : 0)) & 3];
      std::uint8_t gIdx = (u < 0.45f) ? (pickB ? floorB : floorA) : (pickB ? floorC : floorB);

      if (detailRow) {
        const int gx = wx_fp >> 16;
        const int gcy = wy_fp >> 16;
        const int fx = (wx_fp >> 12) & 15;
        const int fy = (wy_fp >> 12) & 15;
        const unsigned cellH = static_cast<unsigned>(gx) * 73856093u ^
                               static_cast<unsigned>(gcy) * 19349663u;
        if (inMine) {
          // World of Xeen irregular rounded dungeon floor flagstones
          const std::uint8_t st = kCobbleRoadTex.px[(gcy * 2 + (fy >> 3)) & 31][(gx * 2 + (fx >> 3)) & 31];
          gIdx = (st == 0u) ? inkOut : (st == 1u) ? rockDark : (st == 2u) ? rockMid : rockHi;
        } else if (lavaWorld) {
          // Cracked basalt crust with glowing magma seams near camera
          const int seam = (gx * 3 + gcy * 5) & 7;
          if (seam == 0 && (fx == 6 || fx == 7) && (cellH & 3u) != 0u) {
            gIdx = (z < 1.85f && (cellH & 1u) == 0u) ? accent1 : accent0;
          } else if ((cellH % 19u) == 0u && fx >= 5 && fx <= 9 && fy == 6) {
            gIdx = inkOut;
          }
        } else if (oceanWorld) {
          // Horizontal tidal shimmer ripples + dark sea-foam streaks
          if (((gcy + (gx >> 2)) & 3) == 0 && fy == 5 && (fx >= 2 && fx <= 12)) {
            gIdx = ((cellH & 1u) == 0u) ? sky3 : gnd2;
          } else if ((cellH % 29u) == 0u && fy == 9 && fx >= 5 && fx <= 10) {
            gIdx = rockDark;
          }
        } else if (iceWorld) {
          // Wind-carved snowdrift bands + sparkling ice glints
          if (((gx + gcy) & 3) == 0 && fy <= 2) {
            gIdx = rockHi;
          } else if ((cellH % 31u) == 0u && fx == 6 && fy == 6) {
            gIdx = accent1;
          } else if ((cellH % 23u) == 0u && fy == 11 && fx >= 4 && fx <= 10) {
            gIdx = gnd0;
          }
        } else if (rockWorld) {
          // Clouds of Xeen stippled desert sand & scattered ochre pebbles
          if (((cellH + static_cast<unsigned>(fx * 3 + fy * 5)) % 7u) == 0u) {
            gIdx = ((cellH & 1u) == 0u) ? gnd2 : gnd0;
          } else if ((cellH % 17u) == 0u && fx >= 5 && fx <= 9 && fy >= 6 && fy <= 8) {
            gIdx = (fy == 6) ? rockHi : rockDark;
          }
        } else {
          // Clouds of Xeen lush dappled meadow grass tufts & wildflowers
          if ((cellH % 5u) == 0u && fy >= 4 && fy <= 9 && fx >= 4 && fx <= 10) {
            gIdx = (fy <= 5) ? leaf2 : (fy <= 7 ? leaf1 : leaf0);
          } else if ((cellH % 29u) == 0u && fx == 7 && fy == 6) {
            gIdx = (subBiome == 1 || subBiome == 3) ? leaf2 : kIdxWarmGleam;
          }
        }
      }

      if (f32 > 0) {
        const std::uint8_t gMid = static_cast<std::uint8_t>(gIdx | kMidFogBit);
        const bool mBit = ((fogMaskRow >> (x & 3)) & 1u) != 0u;
        gIdx = (f32 <= 16) ? (mBit ? gMid : gIdx) : (mBit ? fogTargetIdx : gMid);
      }
      idxBuf[y * W + x] = gIdx;
    }
  }
  activeFog = 0;
  activeInk = inkOut;

  // Integer 16.16 fixed-point DDA trapezoid road rasterizer (rec98 make_linework / draw_trapezoid style).
  // Replaces 8,000 B roadBuf + 8,000 per-pixel float isRoadAtSample evaluations with exact projected
  // world-space road spans for visible path cells only.
  struct RoadSpan {
    float z0;
    float z1;
    float lat0;
    float lat1;
    std::uint8_t bevelMask;  // bit0=BL, bit1=BR, bit2=FL, bit3=FR outer dead-end/convex corner bevel
  };
  RoadSpan roadSpans[64]{};
  int roadSpanCount = 0;
  auto pushRoadSpan = [&](float z0, float z1, float lat0, float lat1, std::uint8_t bevel) {
    if (z1 <= 0.42f || z0 >= 6.60f || roadSpanCount >= 64) { return; }
    roadSpans[roadSpanCount++] = RoadSpan{
        std::max(0.42f, z0), std::min(6.60f, z1), lat0, lat1, bevel};
  };

  for (int d = 0; d <= maxVisD; ++d) {
    const int maxSRoad = d + 2;
    for (int s = -maxSRoad; s <= maxSRoad; ++s) {
      if (!isVis(d, s) &&
          (s == -maxSRoad || !isVis(d, s - 1)) &&
          (s == maxSRoad || !isVis(d, s + 1)) &&
          (d == 0 || std::abs(s) > d + 1 || !isVis(d - 1, s)) &&
          (d == maxVisD || !isVis(d + 1, s))) {
        continue;
      }
      const int tx = planetSurface_.px + fdx * d + rdx * s;
      const int ty = planetSurface_.py + fdy * d + rdy * s;
      if (!isPathTile(tx, ty)) { continue; }
      const bool nF = isPathTile(tx + fdx, ty + fdy);
      const bool nB = isPathTile(tx - fdx, ty - fdy);
      const bool nL = isPathTile(tx + ldx, ty + ldy);
      const bool nR = isPathTile(tx + rdx, ty + rdy);
      const float dc = static_cast<float>(d);
      const float sc = static_cast<float>(s);

      // Lateral pad + optional left/right arms across [d - kHalfRoad, d + kHalfRoad]
      std::uint8_t bevH = 0u;
      if (!nB && !nL) { bevH |= 1u; }
      if (!nB && !nR) { bevH |= 2u; }
      if (!nF && !nL) { bevH |= 4u; }
      if (!nF && !nR) { bevH |= 8u; }
      pushRoadSpan(dc - kHalfRoad, dc + kHalfRoad,
                   nL ? (sc - 0.50f) : (sc - kHalfRoad),
                   nR ? (sc + 0.50f) : (sc + kHalfRoad),
                   bevH);

      // Forward/backward connector arms across [s - kHalfRoad, s + kHalfRoad]
      if (nB || nF) {
        std::uint8_t bevV = 0u;
        if (!nB && !nL) { bevV |= 1u; }
        if (!nB && !nR) { bevV |= 2u; }
        if (!nF && !nL) { bevV |= 4u; }
        if (!nF && !nR) { bevV |= 8u; }
        pushRoadSpan(nB ? (dc - 0.50f) : (dc - kHalfRoad),
                     nF ? (dc + 0.50f) : (dc + kHalfRoad),
                     sc - kHalfRoad, sc + kHalfRoad,
                     bevV);
      }
    }
  }

  constexpr std::uint8_t kRoadSentinel = 254u;
  constexpr float kSlopeScale = (kProjX / kProjY) * 65536.0f;
  std::int16_t roadRowMinX[kGndH];
  std::int16_t roadRowMaxX[kGndH];
  for (int gy = 0; gy < kGndH; ++gy) {
    roadRowMinX[gy] = static_cast<std::int16_t>(W);
    roadRowMaxX[gy] = -1;
  }

  // Pass 1 (curb) & Pass 2 (sentinel interior): pure 16.16 fixed-point scanline edge walking
  for (int pass = 0; pass < 2; ++pass) {
    const int pad = (pass == 0) ? 1 : 0;
    for (int i = 0; i < roadSpanCount; ++i) {
      const RoadSpan& sp = roadSpans[i];
      const int gyTopRaw = static_cast<int>(std::ceil(kProjY / sp.z1 - 0.5f));
      const int gyBotRaw = static_cast<int>(std::floor(kProjY / sp.z0 - 0.5f));
      if (gyTopRaw > gyBotRaw) { continue; }
      const int gy0 = std::max(0, gyTopRaw - pad);
      const int gy1 = std::min(kGndH - 1, gyBotRaw + pad);
      if (gy0 > gy1) { continue; }

      const std::int32_t dXL_fp = static_cast<std::int32_t>(std::lround(sp.lat0 * kSlopeScale));
      const std::int32_t dXR_fp = static_cast<std::int32_t>(std::lround(sp.lat1 * kSlopeScale));
      std::int32_t xL_fp = (cxMid << 16) +
          static_cast<std::int32_t>(std::lround(sp.lat0 * kSlopeScale * (static_cast<float>(gy0) + 0.5f)));
      std::int32_t xR_fp = (cxMid << 16) +
          static_cast<std::int32_t>(std::lround(sp.lat1 * kSlopeScale * (static_cast<float>(gy0) + 0.5f)));

      for (int gy = gy0; gy <= gy1; ++gy) {
        int x0 = ((xL_fp + 32768) >> 16) - pad;
        int x1 = ((xR_fp + 32768) >> 16) - 1 + pad;
        xL_fp += dXL_fp;
        xR_fp += dXR_fp;

        // Subtle 1px outer convex corner bevel on road dead-ends / L-turns
        if (x1 - x0 >= 4 && (gy1 - gy0) >= 2) {
          if (gy == gy0) {
            if ((sp.bevelMask & 4u) != 0u) { ++x0; }
            if ((sp.bevelMask & 8u) != 0u) { --x1; }
          } else if (gy == gy1) {
            if ((sp.bevelMask & 1u) != 0u) { ++x0; }
            if ((sp.bevelMask & 2u) != 0u) { --x1; }
          }
        }

        const int cx0 = std::max(0, x0);
        const int cx1 = std::min(W - 1, x1);
        if (cx0 > cx1) { continue; }
        const int y = hy + gy;
        if (pass == 0) {
          const std::uint8_t curbInk = inkForFog(static_cast<int>(kScanLut.fogStep[gy]));
          for (int x = cx0; x <= cx1; ++x) {
            idxBuf[y * W + x] = curbInk;
          }
        } else {
          if (cx0 < roadRowMinX[gy]) { roadRowMinX[gy] = static_cast<std::int16_t>(cx0); }
          if (cx1 > roadRowMaxX[gy]) { roadRowMaxX[gy] = static_cast<std::int16_t>(cx1); }
          for (int x = cx0; x <= cx1; ++x) {
            idxBuf[y * W + x] = kRoadSentinel;
          }
        }
      }
    }
  }

  // Pass 3: Shade road bevel rims + world-anchored pebbles/dither in 16.16 fixed-point along active spans
  auto isCurbInk = [&](std::uint8_t v) -> bool {
    return v == inkOut || (v >= kInkFogBase && v <= kInkFogBase + 4u);
  };
  const int stepParity = (planetSurface_.px + planetSurface_.py + planetSurface_.dir) & 1;
  const float baseWx = static_cast<float>(planetSurface_.px) + 0.5f;
  const float baseWy = static_cast<float>(planetSurface_.py) + 0.5f;
  for (int gy = 0; gy < kGndH; ++gy) {
    const int xStart = roadRowMinX[gy];
    const int xEnd = roadRowMaxX[gy];
    if (xStart > xEnd) { continue; }
    const int y = hy + gy;
    const float z = kScanLut.z[gy];
    const float invScaleX = kScanLut.invScaleX[gy];
    activeFog = static_cast<int>(kScanLut.fogStep[gy]);
    activeInk = inkForFog(activeFog);
    const bool allowPebble = (z < 3.4f);

    const float lat0 = (static_cast<float>(xStart) + 0.5f - static_cast<float>(cxMid)) * invScaleX;
    // 64 texels/tile (2x2 cobble repeats per cell -> authentic Clouds of Xeen cobblestone density!)
    const float wx0 = (baseWx + static_cast<float>(fdx) * z + static_cast<float>(rdx) * lat0) * 64.0f;
    const float wy0 = (baseWy + static_cast<float>(fdy) * z + static_cast<float>(rdy) * lat0) * 64.0f;
    std::int32_t sx32_fp = static_cast<std::int32_t>(std::lround(wx0 * 65536.0f));
    std::int32_t sy32_fp = static_cast<std::int32_t>(std::lround(wy0 * 65536.0f));
    const std::int32_t dsx32_fp =
        static_cast<std::int32_t>(std::lround(static_cast<float>(rdx) * invScaleX * 64.0f * 65536.0f));
    const std::int32_t dsy32_fp =
        static_cast<std::int32_t>(std::lround(static_cast<float>(rdy) * invScaleX * 64.0f * 65536.0f));

    for (int x = xStart; x <= xEnd; ++x, sx32_fp += dsx32_fp, sy32_fp += dsy32_fp) {
      if (idxBuf[y * W + x] != kRoadSentinel) { continue; }
      const bool upRim = (gy > 0) && isCurbInk(idxBuf[(y - 1) * W + x]);
      const bool lfRim = (x > 0) && isCurbInk(idxBuf[y * W + (x - 1)]);
      const bool rtRim = (x + 1 < W) && isCurbInk(idxBuf[y * W + (x + 1)]);
      if (upRim || lfRim) {
        putPx(x, y, (lavaWorld && !inMine) ? accent0 : pathHi);
      } else if (rtRim) {
        putPx(x, y, pathDark);
      } else {
        const int gx = sx32_fp >> 16;
        const int gcy = sy32_fp >> 16;
        // Power-of-two cobble coords (0..31) & rail coords (0..31 per world tile): zero divisions
        const int cobX = gx & 31;
        const int cobY = gcy & 31;
        const int tx32 = (gx >> 1) & 31;
        const int ty32 = (gcy >> 1) & 31;
        const unsigned h = static_cast<unsigned>(gx) * 73856093u ^
                           static_cast<unsigned>(gcy) * 19349663u;
        const std::uint8_t cob = kCobbleRoadTex.px[cobY][cobX];
        if (inMine) {
          // Underground minecart tracks laid over chunky World of Xeen dungeon flagstones
          const int cellX = gx >> 6;
          const int cellY = gcy >> 6;
          const bool connNS = isPathTile(cellX, cellY - 1) || isPathTile(cellX, cellY + 1);
          const bool connEW = isPathTile(cellX - 1, cellY) || isPathTile(cellX + 1, cellY);
          const bool doNS = connNS || !connEW;
          const bool doEW = connEW;
          const bool railNS = doNS && (tx32 == 10 || tx32 == 11 || tx32 == 20 || tx32 == 21);
          const bool railEW = doEW && (ty32 == 10 || ty32 == 11 || ty32 == 20 || ty32 == 21);
          const bool tieNS  = doNS && (tx32 >= 8 && tx32 <= 23) && ((ty32 & 7) == 1 || (ty32 & 7) == 2);
          const bool tieEW  = doEW && (ty32 >= 8 && ty32 <= 23) && ((tx32 & 7) == 1 || (tx32 & 7) == 2);
          if (railNS || railEW) {
            const bool railLit = (railNS && (tx32 == 10 || tx32 == 20)) ||
                                 (railEW && (ty32 == 10 || ty32 == 20));
            putPx(x, y, railLit ? kIdxShipLight : kIdxShipDark);
          } else if (tieNS || tieEW) {
            const bool tieLit = (tieNS && (ty32 & 7) == 1) || (tieEW && (tx32 & 7) == 1);
            putPx(x, y, tieLit ? pathHi : pathMid);
          } else {
            putPx(x, y, (cob == 0u) ? inkOut : (cob == 1u) ? rockDark : (cob == 2u) ? rockMid : rockHi);
          }
        } else if (lavaWorld) {
          // Dark rounded volcanic flagstones with glowing ember mortar seams
          if (cob == 0u) {
            putPx(x, y, (z < 2.2f && (h & 7u) == 0u) ? accent0 : inkOut);
          } else if (cob == 1u) {
            putPx(x, y, rockDark);
          } else if (cob == 2u) {
            putPx(x, y, pathMid);
          } else {
            putPx(x, y, pathHi);
          }
        } else if (biomeIdx == 0 || oceanWorld) {
          // Classic PC-98 Clouds of Xeen rounded stone cobblestones set in rich earthy mortar!
          if (cob == 0u) {
            putPx(x, y, (z < 2.6f) ? inkOut : pathDark);
          } else if (cob == 1u) {
            putPx(x, y, (biomeIdx == 0) ? pathDark : rockDark);
          } else if (cob == 2u) {
            putPx(x, y, (roadStyle == 0) ? rockMid : pathMid);
          } else {
            putPx(x, y, (roadStyle == 0) ? rockHi : pathHi);
          }
        } else if (iceWorld) {
          // Frost-rimmed glacier cobblestones
          if (cob == 0u) {
            putPx(x, y, (z < 2.5f) ? inkOut : pathDark);
          } else if (cob == 1u) {
            putPx(x, y, pathDark);
          } else if (cob == 2u) {
            putPx(x, y, pathMid);
          } else {
            putPx(x, y, accent1);
          }
        } else {
          // Rock / Badlands: sun-baked rounded desert flagstones set in warm canyon dust
          if (cob == 0u) {
            putPx(x, y, (z < 2.5f) ? inkOut : pathDark);
          } else if (cob == 1u) {
            putPx(x, y, pathDark);
          } else if (cob == 2u) {
            putPx(x, y, (allowPebble || ((x + y + stepParity) & 1) == 0) ? pathMid : rockMid);
          } else {
            putPx(x, y, rockHi);
          }
        }
      }
    }
  }
  activeFog = 0;
  activeInk = inkOut;

  // Handcrafted PC-98 Clouds of Xeen towering multi-trunk leafy forest grove
  auto drawForestGrove = [&](int tx, int gy, float s) {
    const int th = std::max(16, static_cast<int>(std::lround(52.0f * s)));
    const int tw = std::max(16, static_cast<int>(std::lround(46.0f * s)));
    const int trunkH = std::max(7, th * 38 / 100);
    // Lush dark undergrowth shadow + mossy grass bank at grove base
    fillDisc(tx, gy, std::max(5, tw * 48 / 100), std::max(2, static_cast<int>(std::lround(3.0f * s))), inkOut);
    fillOutlinedDisc(tx - tw / 5, gy - std::max(2, th / 14), std::max(4, tw * 32 / 100), std::max(2, th / 11), leaf0, inkOut);
    fillOutlinedDisc(tx + tw / 5, gy - std::max(2, th / 14), std::max(4, tw * 32 / 100), std::max(2, th / 11), leaf0, inkOut);
    fillDitherDisc(tx, gy - std::max(2, th / 12), std::max(4, tw * 34 / 100), std::max(2, th / 12), leaf0, leaf1, 10);
    // 4 tall overlapping tree trunks (birch/oak mix like PC-98 Clouds of Xeen!)
    static constexpr int kTrunkOff[4] = {-32, -10, 12, 31};
    for (int t = 0; t < 4; ++t) {
      const int bx = tx + (tw * kTrunkOff[t]) / 100;
      const int bH = trunkH + (((t + tx) & 1) ? th / 12 : 0);
      const int halfW = std::max(1, static_cast<int>(std::lround(((t == 1 || t == 2) ? 2.4f : 1.8f) * s)));
      const bool birch = ((t + (tx & 3)) & 1) == 0 && subBiome != 3;
      const std::uint8_t barkLit = birch ? rockHi : pathHi;
      const std::uint8_t barkMid = birch ? rockMid : trunkCol;
      for (int row = 0; row < bH; ++row) {
        const int py = gy - row;
        const int hw = std::max(1, halfW - (row > bH * 2 / 3 ? 1 : 0));
        putPx(bx - hw - 1, py, inkOut);
        putPx(bx + hw + 1, py, inkOut);
        for (int px = bx - hw; px <= bx + hw; ++px) {
          std::uint8_t col = (px <= bx - hw / 2) ? barkLit : (px == bx + hw ? pathDark : barkMid);
          if (birch && ((row + t * 3) % 5) == 0 && px == bx) { col = inkOut; }
          putPx(px, py, col);
        }
      }
    }
    // Towering, billowing multi-lobe PC-98 leafy canopy with crisp ink outlines & crunchy leaf stipple
    const int canBaseY = gy - trunkH;
    const int canH = std::max(10, th - trunkH);
    struct Lobe { int dxP; int dyP; int rxP; int ryP; };
    static constexpr Lobe kLobes[7] = {
        {-34, -22, 24, 30}, { 34, -20, 24, 30},
        {-16, -42, 28, 34}, { 18, -40, 28, 34},
        {  0, -56, 30, 34}, {-26, -60, 22, 26}, { 26, -58, 22, 26},
    };
    // Back dark-outlined foliage mass
    for (int i = 0; i < 7; ++i) {
      const int lx = tx + (tw * kLobes[i].dxP) / 100;
      const int ly = canBaseY + (canH * kLobes[i].dyP) / 100;
      const int rx = std::max(4, (tw * kLobes[i].rxP) / 100);
      const int ry = std::max(3, (canH * kLobes[i].ryP) / 100);
      fillOutlinedDisc(lx, ly, rx, ry, leaf0, inkOut);
    }
    // Mid emerald leafy billow clusters (upper-left offset for crisp foliage volume)
    for (int i = 0; i < 7; ++i) {
      const int lx = tx + (tw * kLobes[i].dxP) / 100 - std::max(1, tw / 24);
      const int ly = canBaseY + (canH * kLobes[i].dyP) / 100 - std::max(1, canH / 16);
      const int rx = std::max(3, (tw * kLobes[i].rxP) / 122);
      const int ry = std::max(2, (canH * kLobes[i].ryP) / 122);
      fillDitherDisc(lx, ly, rx, ry, leaf0, leaf1, (i >= 2) ? 13 : 9);
    }
    // Sunlit crown highlights + crunchy PC-98 dark ink leaf-crevice clusters
    for (int i = 2; i < 7; ++i) {
      const int lx = tx + (tw * kLobes[i].dxP) / 100 - std::max(1, tw / 18);
      const int ly = canBaseY + (canH * kLobes[i].dyP) / 100 - std::max(2, canH / 9);
      const int rx = std::max(2, (tw * kLobes[i].rxP) / 170);
      const int ry = std::max(2, (canH * kLobes[i].ryP) / 170);
      fillDitherDisc(lx, ly, rx, ry, leaf1, leaf2, 11);
    }
    // Authentic PC-98 speckled leaf texture (leaf2 tips + inkOut leaf-shadow pockets)
    const int yTopCan = std::max(0, canBaseY - canH);
    const int yBotCan = std::min(H - 1, canBaseY + 2);
    const int x0Can = std::max(0, tx - tw / 2);
    const int x1Can = std::min(W - 1, tx + tw / 2);
    for (int py = yTopCan; py <= yBotCan; py += 2) {
      for (int px = x0Can; px <= x1Can; px += 2) {
        const std::uint8_t cur = idxBuf[py * W + px];
        if (cur == leaf1 || cur == leaf0) {
          const unsigned lh = static_cast<unsigned>(px) * 37u ^ static_cast<unsigned>(py) * 91u;
          if ((lh % 7u) == 0u && py < canBaseY - canH / 3 && px < tx + tw / 4) {
            putPx(px, py, leaf2);
          } else if ((lh % 9u) == 0u && py > canBaseY - canH * 2 / 3) {
            putPx(px, py, inkOut);
          }
        }
      }
    }
  };

  // Handcrafted PC-98 organic & architectural biome prop families (240x144 scale, zero boxy blobs)
  auto drawBroadleafTree = [&](int tx, int gy, float s) {
    drawForestGrove(tx, gy, s * 0.92f);
  };

  auto drawPineTree = [&](int tx, int gy, float s) {
    const int th = std::max(12, static_cast<int>(std::lround(40.0f * s)));
    const int tw = std::max(9, static_cast<int>(std::lround(26.0f * s)));
    const int trunkW = std::max(2, static_cast<int>(std::lround(4.5f * s)));
    const int trunkH = std::max(3, th * 20 / 100);
    fillDisc(tx, gy, std::max(3, tw * 38 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    fillOutlineRect(tx - trunkW / 2 - 1, gy - trunkH, trunkW + 2, trunkH + 1, trunkCol, inkOut);
    const int coneBase = gy - trunkH + 1;
    const int coneH = std::max(9, th - trunkH);
    const int tiers = (coneH >= 16) ? 4 : 3;
    for (int row = 0; row < coneH; ++row) {
      const int tier = (row * tiers) / coneH;
      const int tierStart = (tier * coneH) / tiers;
      const int tierSpan = std::max(2, ((tier + 1) * coneH) / tiers - tierStart);
      const int tierRow = row - tierStart;
      const float frac = static_cast<float>(tierRow + 1) / static_cast<float>(tierSpan);
      const int baseHalf = (tw / 2) * (tier + 2) / (tiers + 1);
      const int droop = (tierRow == tierSpan - 1 && baseHalf >= 4) ? 1 : 0;
      const int hw = std::max(1, static_cast<int>(std::lround(static_cast<float>(baseHalf) * (0.28f + 0.72f * frac)))) + droop;
      const int py = coneBase - coneH + row;
      putPx(tx - hw - 1, py, inkOut);
      putPx(tx + hw + 1, py, inkOut);
      if (row == 0) { putPx(tx, py - 1, inkOut); }
      const bool snowCap = iceWorld && (tierRow <= tierSpan / 2);
      for (int px = tx - hw; px <= tx + hw; ++px) {
        std::uint8_t col = leaf0;
        if (snowCap) {
          col = (px < tx + hw / 4) ? leaf2 : (tierRow == 0 ? rockHi : leaf1);
        } else if (tierRow == tierSpan - 1 && ((px + tier) & 1) == 0) {
          col = inkOut;  // shadowed bough underside notches
        } else if (px < tx - hw / 5) {
          col = (tierRow < tierSpan / 2) ? leaf2 : leaf1;
        } else if (px <= tx + hw / 4) {
          col = (kBayer4[py & 3][px & 3] < 9) ? leaf1 : leaf0;
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - tw / 2, tx + tw / 2, coneBase, inkOut);
  };

  auto drawExoticCanopy = [&](int tx, int gy, float s) {
    const int th = std::max(10, static_cast<int>(std::lround(34.0f * s)));
    const int tw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int trunkH = std::max(5, th * 54 / 100);
    const int lean = ((tx & 1) ? 1 : -1) * std::max(1, tw / 7);
    fillDisc(tx, gy, std::max(3, tw * 38 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Curved organic stem
    for (int row = 0; row < trunkH; ++row) {
      const int py = gy - row;
      const int cx = tx + (lean * row * row) / std::max(1, trunkH * trunkH);
      const int hw = std::max(1, static_cast<int>(std::lround((2.6f - 1.0f * static_cast<float>(row) / static_cast<float>(trunkH)) * s)));
      putPx(cx - hw - 1, py, inkOut);
      putPx(cx + hw + 1, py, inkOut);
      for (int px = cx - hw; px <= cx + hw; ++px) {
        putPx(px, py, (px < cx) ? pathHi : trunkCol);
      }
    }
    const int cx = tx + lean;
    const int cy = gy - trunkH;
    const int capRx = std::max(5, tw / 2);
    const int capRy = std::max(3, th * 20 / 100);
    if (biomeIdx == 0 && subBiome == 3) {
      // Alien Biosphere giant glowing mushroom canopy with gills
      fillOutlinedDisc(cx, cy, capRx, capRy, leaf1, inkOut);
      fillDisc(cx, cy + capRy / 2, std::max(3, capRx * 7 / 10), std::max(2, capRy / 2), pathDark);
      for (int px = cx - capRx * 6 / 10; px <= cx + capRx * 6 / 10; px += 2) {
        putPx(px, cy + capRy / 2, pathHi);
      }
      fillDisc(cx - capRx / 4, cy - capRy / 3, std::max(2, capRx / 2), std::max(2, capRy / 2), leaf2);
      putPx(cx + capRx / 3, cy - capRy / 4, kIdxWarmGleam);
    } else {
      // Wide sculpted umbrella / cypress / willow canopy with hanging tendrils
      fillOutlinedDisc(cx, cy - capRy / 3, capRx, capRy, leaf0, inkOut);
      fillDitherDisc(cx - capRx / 5, cy - capRy / 2, std::max(3, capRx * 7 / 10), std::max(2, capRy * 7 / 10), leaf1, leaf2, 8);
      fillDisc(cx - capRx / 4, cy - capRy * 2 / 3, std::max(2, capRx / 2), std::max(1, capRy / 2), leaf2);
      if (capRx >= 6) {
        for (int dx = -capRx + 2; dx <= capRx - 2; dx += 4) {
          const int drop = 2 + ((std::abs(dx) + tx) % 4);
          fillRect(cx + dx, cy + capRy / 2, 1, drop, (dx < 0) ? leaf1 : leaf0);
        }
      }
    }
  };

  auto drawMossyMonolith = [&](int tx, int gy, float s) {
    const int mh = std::max(11, static_cast<int>(std::lround(33.0f * s)));
    const int mw = std::max(7, static_cast<int>(std::lround(17.0f * s)));
    fillDisc(tx, gy, std::max(3, mw * 65 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Tapering weathered standing stone with broken top bevel
    for (int row = 0; row < mh; ++row) {
      const int py = gy - row;
      const int taper = (row * (mw / 4)) / std::max(1, mh);
      const int hwL = std::max(2, mw / 2 - taper);
      const int hwR = std::max(2, mw / 2 - taper - ((row > mh * 3 / 4) ? (row - mh * 3 / 4) / 2 : 0));
      putPx(tx - hwL - 1, py, inkOut);
      putPx(tx + hwR + 1, py, inkOut);
      for (int px = tx - hwL; px <= tx + hwR; ++px) {
        std::uint8_t col = rockMid;
        if (row >= mh - std::max(2, mh / 6)) {
          col = (px < tx) ? leaf2 : leaf1;  // moss cap
        } else if (px <= tx - hwL / 2) {
          col = rockHi;
        } else if (px >= tx + hwR / 2) {
          col = rockDark;
        } else if (mh >= 16 && (row == mh / 2 || row == mh / 3) && std::abs(px - tx) <= hwL / 2) {
          col = inkOut;  // carved ancient rune band
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - mw / 3, tx + mw / 4, gy - mh, inkOut);
    // Flanking mossy boulder at foot
    fillOutlinedDisc(tx + mw / 2, gy - std::max(2, mh / 8), std::max(2, mw / 3), std::max(2, mh / 8), rockDark, inkOut);
    fillDisc(tx + mw / 2 - 1, gy - std::max(2, mh / 8) - 1, std::max(1, mw / 5), std::max(1, mh / 12), rockMid);
  };

  auto drawCrystalCluster = [&](int tx, int gy, float s, bool obsidian) {
    const int cw = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    const int ch = std::max(12, static_cast<int>(std::lround(36.0f * s)));
    fillDisc(tx, gy, std::max(3, cw / 2), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    const std::uint8_t cDark = obsidian ? rockDark : (iceWorld ? leaf0 : rockDark);
    const std::uint8_t cMid  = obsidian ? rockMid  : accent0;
    const std::uint8_t cHi   = obsidian ? accent0  : accent1;
    auto drawShard = [&](int baseX, int h, int halfW, int leanPx) {
      if (h < 4) { return; }
      for (int row = 0; row < h; ++row) {
        const int py = gy - row;
        const int cx = baseX + (leanPx * row) / h;
        const int topSpan = std::max(2, h / 4);
        int hw = halfW;
        if (row > h - topSpan) {
          hw = std::max(0, (halfW * (h - row)) / topSpan);
        }
        putPx(cx - hw - 1, py, inkOut);
        putPx(cx + hw + 1, py, inkOut);
        for (int px = cx - hw; px <= cx + hw; ++px) {
          std::uint8_t col = cMid;
          if (px <= cx - hw / 3) {
            col = cHi;
          } else if (px >= cx + hw / 2) {
            col = cDark;
          } else if (((row + px) & 3) == 0 && row > h / 3) {
            col = cHi;
          }
          putPx(px, py, col);
        }
      }
      putPx(baseX + leanPx, gy - h, inkOut);
    };
    drawShard(tx - cw * 3 / 10, ch * 62 / 100, std::max(2, cw / 6), -std::max(2, cw / 4));
    drawShard(tx + cw * 3 / 10, ch * 55 / 100, std::max(2, cw / 6),  std::max(2, cw / 4));
    drawShard(tx, ch, std::max(2, cw * 24 / 100), ((tx & 1) ? 1 : -1) * std::max(1, cw / 10));
  };

  auto drawGlacialSerac = [&](int tx, int gy, float s) {
    const int sw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int sh = std::max(9, static_cast<int>(std::lround(26.0f * s)));
    fillDisc(tx, gy, sw / 2, std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Curved wind-carved ice arch with shadowed tunnel opening
    fillOutlinedDisc(tx, gy - sh / 2, sw / 2, sh / 2, rockMid, inkOut);
    fillDitherDisc(tx - sw / 6, gy - sh * 2 / 3, std::max(3, sw / 3), std::max(2, sh / 4), rockHi, accent1, 9);
    // Shadowed arch hollow
    if (sw >= 12 && sh >= 10) {
      fillOutlinedDisc(tx, gy - sh / 5, std::max(2, sw / 5), std::max(2, sh / 4), sky2, inkOut);
      fillDisc(tx, gy - sh / 6, std::max(1, sw / 6), std::max(1, sh / 5), rockDark);
    }
  };

  auto drawSeaStack = [&](int tx, int gy, float s) {
    const int sw = std::max(10, static_cast<int>(std::lround(28.0f * s)));
    const int sh = std::max(11, static_cast<int>(std::lround(34.0f * s)));
    // White surf-foam collar at base
    fillOutlinedDisc(tx, gy, std::max(4, sw * 52 / 100), std::max(1, static_cast<int>(std::lround(2.4f * s))), sky3, inkOut);
    // Undercut tide-eroded basalt stack (narrower at wave-cut notch, wider above)
    for (int row = 1; row < sh; ++row) {
      const int py = gy - row;
      const int u16 = (row * 16) / sh;
      int hw = (sw / 2);
      if (u16 < 5) {
        hw = sw * 34 / 100 + (u16 * sw) / 48;
      } else if (u16 > 12) {
        hw = std::max(2, (sw * (17 - u16)) / 10);
      } else {
        hw = sw * 46 / 100 + ((u16 == 8) ? 1 : 0);
      }
      putPx(tx - hw - 1, py, inkOut);
      putPx(tx + hw + 1, py, inkOut);
      for (int px = tx - hw; px <= tx + hw; ++px) {
        std::uint8_t col = rockMid;
        if (u16 >= 13) {
          col = (px < tx) ? leaf1 : leaf0;  // sea-moss crown
        } else if (px <= tx - hw / 2) {
          col = rockHi;
        } else if (px >= tx + hw / 3 || u16 < 3) {
          col = rockDark;
        } else if (u16 == 6 || u16 == 10) {
          col = rockDark;
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - sw / 5, tx + sw / 5, gy - sh, inkOut);
  };

  auto drawBranchingCoral = [&](int tx, int gy, float s) {
    const int cw = std::max(9, static_cast<int>(std::lround(26.0f * s)));
    const int ch = std::max(9, static_cast<int>(std::lround(26.0f * s)));
    fillDisc(tx, gy, std::max(3, cw * 42 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Basal rock mound + forked staghorn coral limbs with bright polyp tips
    fillOutlinedDisc(tx, gy - std::max(2, ch / 6), std::max(3, cw / 3), std::max(2, ch / 6), rockDark, inkOut);
    auto drawLimb = [&](int x0, int y0, int x1, int y1, int rad) {
      const int steps = std::max(4, std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
      for (int i = 0; i <= steps; ++i) {
        const int px = x0 + ((x1 - x0) * i) / steps;
        const int py = y0 + ((y1 - y0) * i) / steps;
        fillOutlinedDisc(px, py, rad, rad, (i > steps * 2 / 3) ? accent1 : accent0, inkOut);
      }
    };
    const int r = std::max(1, static_cast<int>(std::lround(1.6f * s)));
    drawLimb(tx, gy - ch / 6, tx - cw / 3, gy - ch * 7 / 10, r);
    drawLimb(tx - cw / 6, gy - ch * 4 / 10, tx - cw * 4 / 10, gy - ch * 9 / 10, std::max(1, r));
    drawLimb(tx, gy - ch / 6, tx, gy - ch, r);
    drawLimb(tx, gy - ch / 4, tx + cw / 3, gy - ch * 75 / 100, r);
    drawLimb(tx + cw / 6, gy - ch * 5 / 10, tx + cw * 4 / 10, gy - ch * 92 / 100, std::max(1, r));
  };

  auto drawHydroVent = [&](int tx, int gy, float s) {
    const int vw = std::max(10, static_cast<int>(std::lround(28.0f * s)));
    const int vh = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    fillDisc(tx, gy, vw / 2, std::max(1, static_cast<int>(std::lround(2.2f * s))), inkOut);
    // Sloped volcanic/hydrothermal cone with glowing crater lip & magma cracks
    for (int row = 0; row < vh; ++row) {
      const int py = gy - row;
      const int hw = std::max(2, (vw / 2) - (row * (vw / 3)) / std::max(1, vh));
      putPx(tx - hw - 1, py, inkOut);
      putPx(tx + hw + 1, py, inkOut);
      for (int px = tx - hw; px <= tx + hw; ++px) {
        std::uint8_t col = (px < tx - hw / 3) ? rockMid : rockDark;
        if (row >= vh - std::max(2, vh / 5) && std::abs(px - tx) <= hw * 6 / 10) {
          col = (row == vh - 1) ? accent1 : accent0;
        } else if (lavaWorld && (px == tx - row / 4 || px == tx + row / 3) && row > vh / 3) {
          col = accent0;  // lava rivulets running down cone
        } else if (px == tx - hw + 1) {
          col = rockHi;
        }
        putPx(px, py, col);
      }
    }
    // Glowing crater mouth + rising ember/bubble wisps
    fillDisc(tx, gy - vh, std::max(2, vw / 6), std::max(1, vh / 10), accent1);
    if (vh >= 12) {
      const std::uint8_t wispCol = lavaWorld ? accent0 : sky3;
      putPx(tx - 1, gy - vh - 3, wispCol);
      putPx(tx + 2, gy - vh - 5, lavaWorld ? kIdxWarmGleam : kIdxStarCool);
      putPx(tx, gy - vh - 7, wispCol);
    }
  };

  auto drawBasaltArch = [&](int tx, int gy, float s) {
    const int aw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int ah = std::max(10, static_cast<int>(std::lround(30.0f * s)));
    fillDisc(tx, gy, aw / 2, std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Dramatic skeletal volcanic arch / charred spire
    for (int row = 0; row < ah; ++row) {
      const int py = gy - row;
      const int u16 = (row * 16) / ah;
      const int outerHw = std::max(3, aw / 2 - (u16 * aw) / 48);
      if (u16 < 11) {
        const int legW = std::max(2, aw / 5 - u16 / 6);
        fillOutlineRect(tx - outerHw, py, legW + 2, 1, (u16 & 2) ? rockMid : rockDark, inkOut);
        fillOutlineRect(tx + outerHw - legW - 1, py, legW + 2, 1, rockDark, inkOut);
        if (lavaWorld && u16 == 4) { putPx(tx - outerHw + 1, py, accent0); }
      } else {
        putPx(tx - outerHw - 1, py, inkOut);
        putPx(tx + outerHw + 1, py, inkOut);
        for (int px = tx - outerHw; px <= tx + outerHw; ++px) {
          putPx(px, py, (u16 >= 14 && px < tx) ? rockHi : ((px < tx) ? rockMid : rockDark));
        }
      }
    }
    drawHLine(tx - aw / 3, tx + aw / 3, gy - ah, inkOut);
  };

  auto drawSandstoneHoodoo = [&](int tx, int gy, float s) {
    const int hw = std::max(10, static_cast<int>(std::lround(30.0f * s)));
    const int hh = std::max(11, static_cast<int>(std::lround(34.0f * s)));
    fillDisc(tx, gy, hw * 44 / 100, std::max(1, static_cast<int>(std::lround(2.2f * s))), inkOut);
    // Wind-eroded narrow pedestal supporting a wide stratified caprock!
    for (int row = 0; row < hh; ++row) {
      const int py = gy - row;
      const int u16 = (row * 16) / hh;
      int halfW = hw / 4;
      if (u16 < 3) {
        halfW = hw * 38 / 100 - (u16 * hw) / 24;
      } else if (u16 < 9) {
        halfW = std::max(2, hw * 19 / 100);  // slender neck
      } else if (u16 < 14) {
        halfW = hw * 48 / 100;               // overhanging stratified caprock
      } else {
        halfW = std::max(3, hw * (17 - u16) / 10);
      }
      putPx(tx - halfW - 1, py, inkOut);
      putPx(tx + halfW + 1, py, inkOut);
      for (int px = tx - halfW; px <= tx + halfW; ++px) {
        std::uint8_t col = rockMid;
        if (u16 == 9 || u16 == 8) {
          col = rockDark;  // deep shadow notch under overhang
        } else if (u16 == 12 || u16 == 5) {
          col = rockHi;    // bright sedimentary stratum
        } else if (px <= tx - halfW / 3) {
          col = rockHi;
        } else if (px >= tx + halfW / 3) {
          col = rockDark;
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - hw / 4, tx + hw / 4, gy - hh, inkOut);
  };

  auto drawBandedButte = [&](int tx, int gy, float s) {
    const int rw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int rh = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    fillDisc(tx, gy, rw / 2, std::max(1, static_cast<int>(std::lround(2.2f * s))), inkOut);
    // Multi-tier sculpted boulder / stepped rock mesa with diagonal strata
    fillOutlinedDisc(tx - rw / 6, gy - rh * 4 / 10, rw * 38 / 100, rh * 42 / 100, rockDark, inkOut);
    fillOutlinedDisc(tx + rw / 4, gy - rh * 3 / 10, rw * 28 / 100, rh * 34 / 100, rockMid, inkOut);
    fillOutlinedDisc(tx - rw / 10, gy - rh * 65 / 100, rw * 32 / 100, rh * 36 / 100, rockMid, inkOut);
    fillDitherDisc(tx - rw / 6, gy - rh * 78 / 100, std::max(2, rw * 22 / 100), std::max(2, rh * 20 / 100), rockMid, rockHi, 11);
    fillDisc(tx + rw / 5, gy - rh * 45 / 100, std::max(2, rw * 14 / 100), std::max(1, rh * 12 / 100), rockHi);
  };

  auto drawDesertSucculent = [&](int tx, int gy, float s) {
    if (airlessWorld) {
      drawCrystalCluster(tx, gy, s * 0.88f, true);
      return;
    }
    const int ch = std::max(10, static_cast<int>(std::lround(30.0f * s)));
    const int cw = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    const int stemW = std::max(3, static_cast<int>(std::lround(5.0f * s)));
    fillDisc(tx, gy, std::max(3, cw / 3), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Ribbed branching saguaro / desert succulent
    fillOutlineRect(tx - stemW / 2 - 1, gy - ch, stemW + 2, ch + 1, leaf1, inkOut);
    fillRect(tx - stemW / 2, gy - ch + 1, std::max(1, stemW / 3), ch - 1, leaf2);
    fillRect(tx + stemW / 2 - 1, gy - ch + 1, 1, ch - 1, leaf0);
    // Left & right arms
    const int armW = std::max(2, stemW * 3 / 4);
    const int lArmY = gy - ch * 55 / 100;
    const int rArmY = gy - ch * 42 / 100;
    fillOutlineRect(tx - cw * 42 / 100, lArmY, cw * 42 / 100, armW + 1, leaf0, inkOut);
    fillOutlineRect(tx - cw * 42 / 100, lArmY - ch * 28 / 100, armW + 2, ch * 28 / 100 + armW, leaf1, inkOut);
    fillRect(tx - cw * 42 / 100 + 1, lArmY - ch * 28 / 100 + 1, 1, ch * 24 / 100, leaf2);
    fillOutlineRect(tx + stemW / 2, rArmY, cw * 36 / 100, armW + 1, leaf0, inkOut);
    fillOutlineRect(tx + stemW / 2 + cw * 36 / 100 - armW - 1, rArmY - ch * 24 / 100, armW + 2, ch * 24 / 100 + armW, leaf1, inkOut);
  };

  auto drawScrubBush = [&](int tx, int gy, float s) {
    const int bw = std::max(6, static_cast<int>(std::lround(17.0f * s)));
    const int bh = std::max(4, static_cast<int>(std::lround(10.0f * s)));
    fillOutlinedDisc(tx - bw / 4, gy - bh / 3, std::max(2, bw * 28 / 100), std::max(2, bh * 36 / 100), leaf0, inkOut);
    fillOutlinedDisc(tx + bw / 4, gy - bh / 3, std::max(2, bw * 28 / 100), std::max(2, bh * 36 / 100), leaf0, inkOut);
    fillOutlinedDisc(tx, gy - bh * 55 / 100, std::max(2, bw * 34 / 100), std::max(2, bh * 42 / 100), leaf1, inkOut);
    fillDisc(tx - bw / 6, gy - bh * 65 / 100, std::max(1, bw / 5), std::max(1, bh / 4), leaf2);
    if (biomeIdx == 0 && bw >= 10) {
      putPx(tx + bw / 5, gy - bh / 2, kIdxWarmGleam);
    }
  };

  auto drawGroundScatter = [&](int tx, int gy, float s, int var) {
    const int w = std::max(3, static_cast<int>(std::lround(8.0f * s)));
    if (inMine) {
      fillOutlinedDisc(tx, gy - 1, std::max(1, w / 2), 1, (var & 2) ? accent0 : rockMid, inkOut);
      if ((var & 1) != 0) { putPx(tx - 1, gy - 2, rockHi); }
    } else if (iceWorld) {
      fillRect(tx - w / 2, gy - 1, w, 1, inkOut);
      fillRect(tx - w / 2 + 1, gy - 2, std::max(2, w - 2), 1, rockHi);
      putPx(tx, gy - 3, accent1);
    } else if (oceanWorld) {
      fillOutlinedDisc(tx, gy - 1, std::max(2, w / 2), 1, sky3, inkOut);
      if ((var & 1) == 0) { putPx(tx, gy - 2, accent0); }
    } else if (lavaWorld) {
      fillRect(tx - w / 2, gy - 1, w, 1, inkOut);
      putPx(tx - 1, gy - 2, accent0);
      putPx(tx, gy - 2, accent1);
      putPx(tx + 1, gy - 1, accent0);
    } else if (rockWorld) {
      fillOutlinedDisc(tx, gy - 1, std::max(2, w / 2), std::max(1, w / 4), rockMid, inkOut);
      putPx(tx - 1, gy - 2, rockHi);
    } else {
      // Temperate grass tuft + flower/autumn leaf
      putPx(tx - 2, gy - 1, leaf0);
      putPx(tx - 1, gy - 2, leaf1);
      putPx(tx, gy - 3, leaf2);
      putPx(tx + 1, gy - 2, leaf1);
      putPx(tx + 2, gy - 1, leaf0);
      if ((var & 1) != 0) { putPx(tx + 1, gy - 3, kIdxWarmGleam); }
    }
  };

  auto drawBiomeProp = [&](int model, int tx, int gy, float s) {
    const int m = (model + propBias) & 3;
    if (iceWorld) {
      if (m == 0) { drawPineTree(tx, gy, s); }
      else if (m == 1) { drawCrystalCluster(tx, gy, s, false); }
      else if (m == 2) { drawGlacialSerac(tx, gy, s); }
      else { drawBandedButte(tx, gy, s); }
    } else if (oceanWorld) {
      if (m == 0) { drawSeaStack(tx, gy, s); }
      else if (m == 1) { drawBranchingCoral(tx, gy, s); }
      else if (m == 2) { drawHydroVent(tx, gy, s); }
      else { drawExoticCanopy(tx, gy, s * 0.90f); }
    } else if (lavaWorld) {
      if (m == 0) { drawCrystalCluster(tx, gy, s, true); }
      else if (m == 1) { drawHydroVent(tx, gy, s); }
      else if (m == 2) { drawBasaltArch(tx, gy, s); }
      else { drawBandedButte(tx, gy, s); }
    } else if (rockWorld) {
      if (m == 0) { drawSandstoneHoodoo(tx, gy, s); }
      else if (m == 1) { drawBandedButte(tx, gy, s); }
      else if (m == 2) { drawBasaltArch(tx, gy, s); }
      else { drawDesertSucculent(tx, gy, s); }
    } else {
      if (m == 0) { drawBroadleafTree(tx, gy, s); }
      else if (m == 1) { drawPineTree(tx, gy, s); }
      else if (m == 2) { drawExoticCanopy(tx, gy, s); }
      else { drawMossyMonolith(tx, gy, s); }
    }
  };

  auto cliffCrownLift = [&](int uPermille, int spanW, int var, unsigned cellH,
                            bool adjStart, bool adjEnd) -> int {
    if (inMine || spanW < 7) { return 0; }
    // Taper exposed outer corners on natural cliffs so they never read like flat concrete boxes
    int edgeDrop = 0;
    if (var != 7) {
      if (!adjStart && uPermille < 170) {
        edgeDrop = ((170 - uPermille) * std::clamp(spanW / 8, 2, 7)) / 170;
      } else if (!adjEnd && uPermille > 830) {
        edgeDrop = ((uPermille - 830) * std::clamp(spanW / 8, 2, 7)) / 170;
      }
    } else if ((cellH & 2u) != 0u) {
      // Crumbled ruin corner decay
      if (!adjStart && uPermille < 200) {
        edgeDrop = ((200 - uPermille) * std::clamp(spanW / 9, 2, 6)) / 200;
      }
    }
    const int maxLift = std::clamp(spanW / 6, 2, 11);
    int lift = 0;
    if (var == 7) {
      // Might & Magic castle merlons, broken parapets, temple pediments & corner turrets
      const unsigned style = cellH & 3u;
      if (style == 0u) {
        // Bold fortress battlements (merlons & embrasures)
        lift = (((uPermille / 167) & 1) == 0) ? std::max(2, maxLift * 2 / 3) : 0;
      } else if (style == 1u) {
        // Weathered broken ruin wall (stepped missing blocks)
        const int blk = uPermille / 180;
        lift = ((blk + static_cast<int>(cellH >> 2)) % 3 == 0) ? maxLift
             : ((blk & 1) ? std::max(1, maxLift / 3) : 0);
      } else if (style == 2u) {
        // Stepped temple gable / ziggurat pediment rising toward center
        const int distMid = std::abs(uPermille - 500);
        lift = (distMid < 140) ? maxLift : (distMid < 310) ? (maxLift * 2 / 3) : (maxLift / 3);
      } else {
        // Twin corner watch-turrets framing a dipped parapet
        lift = (uPermille < 240 || uPermille > 760) ? maxLift : std::max(1, maxLift / 4);
      }
    } else if (var == 5) {
      if (lavaWorld) {
        // Stepped hexagonal basalt organ-pipe columns
        const unsigned col = static_cast<unsigned>(uPermille / 142) + (cellH & 3u);
        lift = static_cast<int>(((col * 5u + (cellH >> 2)) % 5u) * static_cast<unsigned>(maxLift)) / 4;
      } else if (iceWorld) {
        // Sharp asymmetric glacial crystal peak
        const int peakU = ((cellH & 1u) != 0u) ? 340 : 660;
        const int dPeak = std::abs(uPermille - peakU);
        lift = std::max(0, maxLift - (dPeak * maxLift) / 480);
      } else if (rockWorld) {
        // Flat-topped canyon mesa with overhanging caprock step
        lift = (uPermille >= 180 && uPermille <= 820) ? maxLift
             : (uPermille >= 90 && uPermille <= 910) ? (maxLift / 2) : 0;
      } else {
        // Organic rocky crag / twin-peaked bluff
        const int p1 = std::max(0, 320 - std::abs(uPermille - 330));
        const int p2 = std::max(0, 260 - std::abs(uPermille - 700));
        lift = ((p1 + p2 / 2) * maxLift) / 360;
      }
    } else if (var == 6) {
      // Sloped mineral ridge or stepped outcrop
      const int peakU = ((cellH & 1u) != 0u) ? 280 : 720;
      const int dPeak = std::abs(uPermille - peakU);
      lift = std::max(0, (maxLift * 3 / 4) - (dPeak * maxLift) / 620);
    } else {
      // var == 4: organic rolling rock bluff
      const int dMid = std::abs(uPermille - (420 + static_cast<int>((cellH & 3u) * 50u)));
      lift = std::max(0, (maxLift * 2 / 3) - (dMid * maxLift) / 560);
    }
    return lift - edgeDrop;
  };

  auto drawCliffCell = [&](int tx, int ty, int d, int s, int var) {
    const unsigned cellH = static_cast<unsigned>(tx * 37 + ty * 91 + var * 13);
    const bool adjNear  = isCliffCell(tx - fdx, ty - fdy);
    const bool adjFar   = isCliffCell(tx + fdx, ty + fdy);
    const bool adjLeft  = isCliffCell(tx + ldx, ty + ldy);
    const bool adjRight = isCliffCell(tx + rdx, ty + rdy);
    const float cellTopScale = cliffTopScaleFor(tx, ty, var);

    const bool fogActive = (activeFog > 0);
    const bool fogHighBand = (activeFog > 16);
    const int wallThr16 = fogHighBand ? std::min(16, activeFog - 16) : activeFog;
    auto colFogBitsForX = [&](int x) -> std::uint8_t {
      if (!fogActive) { return 0u; }
      const int rx = x & 3;
      return static_cast<std::uint8_t>(
          (((kFogMasks.row[wallThr16][0] >> rx) & 1u) << 0) |
          (((kFogMasks.row[wallThr16][1] >> rx) & 1u) << 1) |
          (((kFogMasks.row[wallThr16][2] >> rx) & 1u) << 2) |
          (((kFogMasks.row[wallThr16][3] >> rx) & 1u) << 3));
    };

    // Peripheral d == 0 cheek wall when standing directly beside a cliff (s == -1 or s == +1)
    if (d == 0) {
      if (std::abs(s) != 1) { return; }
      const int x0 = (s < 0) ? 0 : (W - 9);
      const int x1 = (s < 0) ? 8 : (W - 1);
      const int yTopInner = cliffYTopScaled(0.50f, cellTopScale);
      const int yTopOuter = inMine ? 0 : std::max(0, yTopInner - 10);
      const int yBotOuter = H - 1;
      const int yBotInner = cliffYBot(0.50f);
      const int yT0 = (s < 0) ? yTopOuter : yTopInner;
      const int yT1 = (s < 0) ? yTopInner : yTopOuter;
      const int yB0 = (s < 0) ? yBotOuter : yBotInner;
      const int yB1 = (s < 0) ? yBotInner : yBotOuter;
      const int dx = std::max(1, x1 - x0);
      std::int32_t yT_fp = yT0 << 16;
      std::int32_t yB_fp = yB0 << 16;
      const std::int32_t dYT_fp = ((yT1 - yT0) << 16) / dx;
      const std::int32_t dYB_fp = ((yB1 - yB0) << 16) / dx;
      const int innerEdgeX = (s < 0) ? x1 : x0;
      for (int x = x0; x <= x1; ++x, yT_fp += dYT_fp, yB_fp += dYB_fp) {
        const int yT = std::max(0, yT_fp >> 16);
        const int yB = std::min(H - 1, yB_fp >> 16);
        if (yT > yB) { continue; }
        if (x == innerEdgeX && !adjFar) {
          fillRect(x, yT, 1, yB - yT + 1, inkOut);
          continue;
        }
        idxBuf[yT * W + x] = inkOut;
        idxBuf[yB * W + x] = inkOut;
        const int colH = std::max(1, yB - yT);
        const std::uint32_t invColH =
            ((1u << 20) + static_cast<std::uint32_t>(colH) - 1u) / static_cast<std::uint32_t>(colH);
        const int uTex = ((x - x0) * 3) & 31;
        for (int y = yT + 1; y < yB; ++y) {
          const int v32 = static_cast<int>((static_cast<std::uint32_t>((y - yT) << 5) * invColH) >> 20) & 31;
          const std::uint8_t st = kStoneWallTex.px[v32][uTex];
          std::uint8_t cIdx = (st == 0u) ? inkOut : (st == 3u) ? rockMid : rockDark;
          if (!inMine && biomeIdx == 0 && v32 <= 4 && st > 0u) { cIdx = leaf0; }
          else if (!inMine && lavaWorld && st == 0u && v32 >= 14 && v32 <= 18) { cIdx = accent0; }
          idxBuf[y * W + x] = cIdx;
        }
      }
      return;
    }

    const float zNearW = static_cast<float>(d) - 0.50f;
    const float zFarW  = static_cast<float>(d) + 0.50f;
    const int clipL = std::max(0, static_cast<int>(rowX0[d]));
    const int clipR = std::min(W - 1, static_cast<int>(rowX1[d]));

    // 1. Quadrant-culled receding side face (defold sign rule: s < 0 -> right face, s > 0 -> left face)
    bool drawSide = false;
    float sideLat = 0.0f;
    if (s < 0 && !adjRight) {
      drawSide = true;
      sideLat = static_cast<float>(s) + 0.50f;
    } else if (s > 0 && !adjLeft) {
      drawSide = true;
      sideLat = static_cast<float>(s) - 0.50f;
    }

    if (drawSide) {
      const int xNear = cliffX(zNearW, sideLat);
      const int xFar  = cliffX(zFarW, sideLat);
      const int yTopN = cliffYTopScaled(zNearW, cellTopScale);
      const int yBotN = cliffYBot(zNearW);
      const int yTopF = cliffYTopScaled(zFarW, cellTopScale);
      const int yBotF = cliffYBot(zFarW);

      const bool nearLeft = (s < 0);
      const int x0 = nearLeft ? xNear : xFar;
      const int x1 = nearLeft ? xFar  : xNear;
      const int yT0 = nearLeft ? yTopN : yTopF;
      const int yT1 = nearLeft ? yTopF : yTopN;
      const int yB0 = nearLeft ? yBotN : yBotF;
      const int yB1 = nearLeft ? yBotF : yBotN;
      const bool adjSideStart = nearLeft ? adjNear : adjFar;
      const bool adjSideEnd   = nearLeft ? adjFar  : adjNear;

      if (x1 >= clipL && x0 <= clipR && x1 >= x0) {
        const int spanW = std::max(1, x1 - x0);
        std::int32_t yT_fp = yT0 << 16;
        std::int32_t yB_fp = yB0 << 16;
        const std::int32_t dYT_fp = ((yT1 - yT0) << 16) / spanW;
        const std::int32_t dYB_fp = ((yB1 - yB0) << 16) / spanW;
        int startX = x0;
        if (startX < clipL) {
          const int skip = clipL - startX;
          yT_fp += dYT_fp * skip;
          yB_fp += dYB_fp * skip;
          startX = clipL;
        }
        const int endX = std::min(clipR, x1);
        for (int x = startX; x <= endX; ++x, yT_fp += dYT_fp, yB_fp += dYB_fp) {
          if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { continue; }
          const int uPerm = ((x - x0) * 1000) / spanW;
          const int lift = cliffCrownLift(uPerm, spanW, var, cellH, adjSideStart, adjSideEnd);
          const int yT = std::clamp((yT_fp >> 16) - lift, 0, H - 1);
          const int yB = std::clamp(yB_fp >> 16, 0, H - 1);
          if (yT > yB) { continue; }
          if ((x == xNear && !adjNear) || (x == xFar && !adjFar)) {
            fillRect(x, yT, 1, yB - yT + 1, inkOut);
            continue;
          }
          idxBuf[yT * W + x] = activeInk;
          idxBuf[yB * W + x] = activeInk;
          const int colH = std::max(1, yB - yT);
          const std::uint32_t invColH =
              ((1u << 20) + static_cast<std::uint32_t>(colH) - 1u) / static_cast<std::uint32_t>(colH);
          const std::uint8_t colFogMask = colFogBitsForX(x);
          const bool sidePilaster = (var == 7 && spanW >= 12 && (uPerm < 110 || uPerm > 890));
          const int vineLen = (!inMine && biomeIdx == 0 && colH >= 12)
                                  ? static_cast<int>(((static_cast<unsigned>(x) * 13u + cellH) % 7u) == 0u
                                                         ? (colH * (25 + static_cast<int>((x * 7 + cellH) % 30u)) / 100)
                                                         : 2)
                                  : 0;
          const int uTexSide = ((uPerm * 48) / 1000 + static_cast<int>(cellH & 15u)) & 31;
          for (int y = yT + 1; y < yB; ++y) {
            const int v32 = static_cast<int>((static_cast<std::uint32_t>((y - yT) << 5) * invColH) >> 20) & 31;
            const int v16 = v32 >> 1;
            const std::uint8_t st = kStoneWallTex.px[v32][uTexSide];
            std::uint8_t cIdx = (st == 0u) ? inkOut : (st == 3u) ? rockMid : rockDark;
            if (sidePilaster) {
              // Vertical stone pilaster / mine timber post framing receding bay edges
              cIdx = inMine ? ((uPerm < 60 || uPerm > 940) ? trunkCol : pathHi)
                            : ((v16 == 1 || v16 == 14) ? rockHi : rockMid);
            } else if (!inMine && oceanWorld && y >= yB - 2 && ((x + y) & 1) == 0) {
              cIdx = sky3;  // surf foam collar along sea-cliff side
            } else if (!inMine && biomeIdx == 0 && y <= yT + vineLen) {
              cIdx = (y == yT + 1) ? leaf1 : leaf0;  // hanging forest moss/ivy tendrils
            } else if (!inMine && iceWorld && y <= yT + 2) {
              cIdx = (y == yT + 1) ? accent1 : rockMid;
            } else if (var == 6 && inMine && st == 3u && std::abs((v16 * 55) + (uPerm / 4) - 560) < 38) {
              cIdx = (((x + y) & 1) == 0) ? accent0 : accent1;  // diagonal glowing ore vein in mine stone
            } else if (st == 0u) {
              if ((inMine || biomeIdx == 0) && !lavaWorld && !iceWorld &&
                  ((uTexSide + v32 + static_cast<int>(cellH)) % 9) == 0) {
                cIdx = leaf0;  // World of Xeen damp mossy mortar pockets
              } else if (lavaWorld && !inMine && v16 == 8) {
                cIdx = accent0;
              } else {
                cIdx = inkOut;
              }
            } else if (st == 2u && kBayer4[y & 3][x & 3] < 5) {
              cIdx = rockMid;
            } else if (st == 3u && !inMine && iceWorld) {
              cIdx = accent1;
            }
            if (!fogActive) {
              idxBuf[y * W + x] = cIdx;
            } else if (cIdx == inkOut) {
              idxBuf[y * W + x] = activeInk;
            } else {
              const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
              const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
              idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
            }
          }
        }

        // Might & Magic corridor side-wall dressing (underground torches, barred alcoves, surface runes)
        const int visSpanW = endX - startX + 1;
        if (visSpanW >= 10 && d <= 4) {
          const int smx = (x0 + x1) / 2;
          if (smx >= startX + 3 && smx <= endX - 3 &&
              ((depthColOcc[d][smx >> 5] >> (smx & 31)) & 1u) == 0u) {
            const int syT = (yT0 + yT1) / 2;
            const int syB = (yB0 + yB1) / 2;
            const int sH = syB - syT;
            if (inMine && sH >= 16) {
              if ((cellH % 3u) == 0u) {
                // Foreshortened side-wall glowing torch sconce lining the dark mine corridor
                const int tyC = syT + sH * 42 / 100;
                const int haloR = std::clamp(std::min(visSpanW / 3, sH / 6), 3, 9);
                const int savedFog = activeFog;
                activeFog = std::max(0, savedFog - 14);
                fillDitherDisc(smx, tyC - 1, haloR, haloR, rockDark, pathHi, 7);
                fillDitherDisc(smx, tyC - 1, std::max(2, haloR / 2), std::max(2, haloR / 2),
                               pathHi, kIdxOreGold, 6);
                fillRect(smx - 1, tyC + 1, 2, std::max(3, sH / 10), inkOut);
                putPx(smx, tyC + 2, trunkCol);
                fillDisc(smx, tyC - 1, std::max(1, haloR / 5), std::max(2, haloR / 4), kIdxOreGold);
                putPx(smx, tyC - 1, kIdxWarmGleam);
                putPx(smx, tyC - 2, kIdxWarmGleam);
                activeFog = savedFog;
              } else if (var == 7 && (cellH % 3u) == 1u && visSpanW >= 14 && sH >= 22) {
                // Recessed dungeon iron-barred alcove on corridor side wall
                const int aw = std::clamp(visSpanW / 3, 4, 10);
                const int ah = std::clamp(sH * 36 / 100, 7, 20);
                const int ax = smx - aw / 2;
                const int ay = syT + sH * 30 / 100;
                fillOutlineRect(ax, ay, aw, ah, inkOut, rockMid);
                for (int bx = ax + 2; bx < ax + aw - 1; bx += 2) {
                  fillRect(bx, ay + 1, 1, ah - 2, kIdxShipDark);
                }
                if (ah >= 10) {
                  drawHLine(ax + 1, ax + aw - 2, ay + ah / 2, kIdxShipDark);
                }
              }
            } else if (!inMine && var == 7 && visSpanW >= 14 && sH >= 18 && (cellH & 1u) == 0u) {
              // Recessed gothic arrow-slit / dark window slit on surface ruin side wall
              const int sw = std::clamp(visSpanW / 5, 3, 6);
              const int sh = std::clamp(sH / 3, 5, 14);
              fillOutlineRect(smx - sw / 2, syT + sH * 28 / 100, sw, sh, inkOut, rockMid);
              if (lavaWorld) {
                putPx(smx, syT + sH * 28 / 100 + sh / 2, accent0);
              }
            }
          }
        }
      }
    }

    // 2. Front-facing cliff / canyon / ruin-mine facade at zNearW (culled if nearer cell is also cliff)
    if (!adjNear) {
      const int xL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
      const int xR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
      const int yTBase = cliffYTopScaled(zNearW, cellTopScale);
      const int yB = std::clamp(cliffYBot(zNearW), 0, H - 1);
      if (xR >= 0 && xL < W && xR >= xL && yTBase < yB) {
        const int faceW = std::max(1, xR - xL);
        const int baseH = std::max(1, yB - yTBase);
        const int cx0 = std::max(clipL, xL);
        const int cx1 = std::min(clipR, xR);

        if (cx0 <= cx1) {
          const int maxLiftBound = std::clamp(faceW / 6, 2, 11) + 2;
          const int yRowStart = std::max(0, yTBase - maxLiftBound);
          std::int8_t rowV32[H]{};
          for (int y = yRowStart; y <= yB; ++y) {
            const int dy = y - yTBase;
            rowV32[y] = static_cast<std::int8_t>(std::clamp((dy * 32) / baseH, 0, 31));
          }

          const int leftNeighYT = adjLeft
                                      ? cliffYTopScaled(zNearW, cliffTopScaleFor(tx + ldx, ty + ldy,
                                                                                 variantAt(tx + ldx, ty + ldy)))
                                      : yB + 1;
          const int rightNeighYT = adjRight
                                       ? cliffYTopScaled(zNearW, cliffTopScaleFor(tx + rdx, ty + rdy,
                                                                                  variantAt(tx + rdx, ty + rdy)))
                                       : yB + 1;
          for (int x = cx0; x <= cx1; ++x) {
            if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { continue; }
            const int uPerm = ((x - xL) * 1000) / faceW;
            const int lift = cliffCrownLift(uPerm, faceW, var, cellH, adjLeft, adjRight);
            const int yT = std::clamp(yTBase - lift, 0, H - 1);
            if (yT > yB) { continue; }
            if ((x == xL && !adjLeft) || (x == xR && !adjRight)) {
              fillRect(x, yT, 1, yB - yT + 1, inkOut);
              continue;
            }
            const int edgeOutlineBot =
                (x == xL && yT < leftNeighYT) ? std::min(yB, leftNeighYT)
              : (x == xR && yT < rightNeighYT) ? std::min(yB, rightNeighYT)
                                               : -1;
            if (edgeOutlineBot >= yT) {
              fillRect(x, yT, 1, edgeOutlineBot - yT + 1, inkOut);
            } else {
              idxBuf[yT * W + x] = activeInk;
            }
            idxBuf[yB * W + x] = activeInk;
            const std::uint8_t colFogMask = colFogBitsForX(x);
            const int uTex = ((uPerm * 64) / 1000 + static_cast<int>((cellH & 3u) * 8u)) & 31;
            const int capDrop = (!inMine && baseH >= 14)
                                    ? (1 + static_cast<int>((static_cast<unsigned>(x) * 3u + cellH) % 3u))
                                    : 0;
            const bool pilasterL = (!inMine && var == 7 && faceW >= 24 && uPerm > 15 && uPerm < 105);
            const bool pilasterR = (!inMine && var == 7 && faceW >= 24 && uPerm > 895 && uPerm < 985);
            const int vineDrop = (!inMine && biomeIdx == 0 && baseH >= 16)
                                     ? (((static_cast<unsigned>(x) * 11u + cellH) % 9u) == 0u
                                            ? (baseH * (24 + static_cast<int>((static_cast<unsigned>(x) * 7u + cellH) % 34u)) / 100)
                                            : 0)
                                     : 0;
            const int icicleDrop = (!inMine && iceWorld && baseH >= 16)
                                       ? (((static_cast<unsigned>(x) * 7u + cellH) % 11u) == 0u
                                              ? (3 + static_cast<int>((static_cast<unsigned>(x) * 5u + cellH) % 7u))
                                              : 0)
                                       : 0;

            for (int y = std::max(yT + 1, edgeOutlineBot + 1); y < yB; ++y) {
              const int v32 = rowV32[y];
              const int v16 = v32 >> 1;
              const std::uint8_t st = kStoneWallTex.px[v32][uTex];
              std::uint8_t cIdx = (st == 0u) ? inkOut
                                : (st == 1u) ? rockDark
                                : (st == 2u) ? rockMid
                                             : rockHi;

              if (pilasterL || pilasterR) {
                const bool edgeShadow = (pilasterL && uPerm >= 85) || (pilasterR && uPerm <= 915);
                const bool edgeLit    = (pilasterL && uPerm <= 42) || (pilasterR && uPerm >= 958);
                if (y <= yT + 2 || y >= yB - 2) {
                  cIdx = rockHi;
                } else if (edgeShadow) {
                  cIdx = rockDark;
                } else if (edgeLit) {
                  cIdx = rockHi;
                } else {
                  cIdx = ((y & 3) == 0) ? rockDark : rockMid;
                }
              } else if (!inMine && y <= yT + capDrop) {
                if (iceWorld) {
                  cIdx = (y == yT + 1) ? accent1 : rockHi;
                } else if (biomeIdx == 0) {
                  cIdx = (y == yT + 1) ? leaf2 : leaf1;
                } else if (lavaWorld) {
                  cIdx = (y == yT + 1) ? rockHi : rockDark;
                } else {
                  cIdx = rockHi;
                }
              } else if (!inMine && biomeIdx == 0 && vineDrop > 0 && y <= yT + vineDrop) {
                cIdx = (y == yT + vineDrop) ? leaf0 : (((x + y) & 1) ? leaf2 : leaf1);
              } else if (!inMine && iceWorld && icicleDrop > 0 && y <= yT + capDrop + icicleDrop) {
                cIdx = (y == yT + capDrop + icicleDrop) ? accent1 : rockHi;
              } else if (!inMine && oceanWorld && y >= yB - 3 && ((x + y) & 1) == 0) {
                cIdx = sky3;
              } else if (st == 0u) {
                // World of Xeen chunky rounded mortar joints with biome moss / forge embers
                if ((biomeIdx == 0 || inMine) && !lavaWorld && !iceWorld &&
                    ((uTex * 3 + v32 * 5 + static_cast<int>(cellH)) % 7) == 0) {
                  cIdx = leaf0;
                } else if (lavaWorld && v16 >= 6 && v16 <= 10 && ((uTex + v32) & 3) == 0) {
                  cIdx = accent0;
                } else {
                  cIdx = inkOut;
                }
              } else if (st == 1u && (biomeIdx == 0 || inMine) && !lavaWorld && !iceWorld && !rockWorld &&
                         ((uTex + v32 * 3 + static_cast<int>(cellH)) % 11) == 0) {
                // Dappled green dungeon moss clinging to stone block edges (just like World of Xeen!)
                cIdx = (v32 & 1) ? leaf1 : leaf0;
              } else if (inMine && var == 6 && st == 3u && std::abs((v16 * 55) + (uPerm / 4) - 560) < 46) {
                cIdx = ((x + y) & 1) ? accent0 : accent1;  // sparkling mineral vein in dungeon stone
              } else if (!inMine && rockWorld && baseH >= 16 && v16 == 3) {
                cIdx = (((uPerm / 65) & 1) == 0) ? rockHi : rockDark;  // carved sandstone temple frieze
              } else if (!inMine && iceWorld && st == 3u) {
                cIdx = accent1;
              }

              if (!fogActive) {
                idxBuf[y * W + x] = cIdx;
              } else if (cIdx == inkOut) {
                idxBuf[y * W + x] = activeInk;
              } else {
                const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
                const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
                idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
              }
            }
          }
        }

        const int pOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
        const bool isPortalFront =
            (tx == static_cast<int>(planetSurface_.portalX) &&
             ty == static_cast<int>(planetSurface_.portalY) &&
             fdx == -kDx[pOut] && fdy == -kDy[pOut]);

        // Might & Magic storytelling front-wall dressing (recessed arches, iron grates, glowing rune tablets, vents)
        if (!inMine && !isPortalFront && faceW >= 20 && baseH >= 16 && d <= 4) {
          const int fcx = (xL + xR) / 2;
          const int fcy = yTBase + baseH * 52 / 100;
          if (var == 7) {
            const unsigned dress = (cellH >> 1) & 3u;
            if (dress == 0u) {
              // Recessed arched dungeon/ruin alcove with iron grate bars & faint inner gleam
              const int aw = std::clamp(faceW * 32 / 100, 7, 22);
              const int ah = std::clamp(baseH * 46 / 100, 8, 26);
              const int ax = fcx - aw / 2;
              const int ay = std::clamp(yB - ah - 1, yTBase + 4, yB - 6);
              // Stone arch surround + dark interior
              fillOutlineRect(ax - 1, ay + 2, aw + 2, ah - 1, inkOut, rockHi);
              fillOutlineRect(ax + 1, ay, aw - 2, 4, inkOut, rockHi);
              fillRect(ax, ay + 2, aw, ah - 2, inkOut);
              // Vertical wrought-iron grate bars + cross-bar
              const int stepB = std::max(2, aw / 4);
              for (int bx = ax + 2; bx <= ax + aw - 3; bx += stepB) {
                fillRect(bx, ay + 2, 1, ah - 3, kIdxShipDark);
                if (ah >= 12) { putPx(bx, ay + 4, kIdxShipLight); }
              }
              drawHLine(ax + 1, ax + aw - 2, ay + ah / 2, kIdxShipDark);
              // Faint mysterious gleam inside the barred recess
              if (aw >= 10 && ah >= 11) {
                const std::uint8_t glint = lavaWorld ? accent0 : (iceWorld ? accent1 : kIdxWarmGleam);
                putPx(fcx - 1, ay + ah * 3 / 5, glint);
                putPx(fcx + 1, ay + ah * 3 / 5, glint);
              }
            } else if (dress == 1u) {
              // Carved glowing ancient rune plaque / alien monolith tablet
              const int pw = std::clamp(faceW * 34 / 100, 8, 20);
              const int ph = std::clamp(baseH * 34 / 100, 7, 16);
              const int px = fcx - pw / 2;
              const int py = fcy - ph / 2;
              fillOutlineRect(px - 1, py - 1, pw + 2, ph + 2, rockDark, rockHi);
              fillOutlineRect(px, py, pw, ph, inkOut, rockDark);
              const std::uint8_t runeCol = (biomeIdx == 0) ? leaf2
                                         : iceWorld        ? accent1
                                         : lavaWorld       ? accent0
                                                           : kIdxShipVisor;
              // Crisp geometric PC-98 glyphs inside plaque
              drawHLine(px + 2, px + pw - 3, py + 2, runeCol);
              fillRect(fcx, py + 2, 1, ph - 4, runeCol);
              if (pw >= 12) {
                fillRect(px + 3, py + 3, 1, std::max(2, ph - 6), runeCol);
                fillRect(px + pw - 4, py + 3, 1, std::max(2, ph - 6), runeCol);
                drawHLine(px + 3, px + pw - 4, py + ph - 3, accent1);
              }
            } else if (dress == 2u) {
              // Biome-themed wall emblem / banner / brazier grate / drainage arch
              const int bw = std::clamp(faceW * 22 / 100, 5, 13);
              const int bh = std::clamp(baseH * 42 / 100, 7, 22);
              const int bx = fcx - bw / 2;
              const int by = yTBase + std::max(3, baseH * 22 / 100);
              if (lavaWorld) {
                // Glowing recessed forge brazier grate
                fillOutlineRect(bx, by + bh / 3, bw, std::max(4, bh / 2), accent0, inkOut);
                fillRect(bx + 1, by + bh / 3 + 1, std::max(1, bw - 2), std::max(2, bh / 4), kIdxWarmGleam);
                for (int gx = bx + 2; gx < bx + bw - 1; gx += 2) {
                  fillRect(gx, by + bh / 3, 1, std::max(4, bh / 2), inkOut);
                }
              } else if (oceanWorld) {
                // Carved sea-drainage arch spilling foaming water down the wall
                fillOutlineRect(bx, by, bw, std::max(5, bh / 2), inkOut, rockHi);
                fillRect(fcx - 1, by + bh / 3, 3, std::max(3, yB - (by + bh / 3) - 1), sky3);
                fillRect(fcx, by + bh / 3, 1, std::max(3, yB - (by + bh / 3) - 1), kIdxStarCool);
              } else {
                // Weathered wall tapestry / guild banner / carved crest
                const std::uint8_t banCol = (biomeIdx == 0) ? accent0
                                          : iceWorld        ? accent1
                                                            : kIdxOreBody;
                drawHLine(bx - 1, bx + bw, by, kIdxOreGold);
                fillOutlineRect(bx, by + 1, bw, bh, banCol, inkOut);
                fillRect(fcx, by + 3, 1, std::max(2, bh - 6), kIdxWarmGleam);
                if (bw >= 7) { drawHLine(bx + 2, bx + bw - 3, by + bh / 2, kIdxWarmGleam); }
              }
            }
          } else if (var == 4 || var == 5) {
            // Natural cliff storytelling accents (grotto hollow, lava weep, frost crystal pocket)
            if (lavaWorld && (cellH & 1u) == 0u && faceW >= 24) {
              // Glowing weeping magma vent trickling down basalt columns
              const int vx = fcx + (((cellH & 2u) != 0u) ? -faceW / 6 : faceW / 6);
              const int vy = yTBase + baseH * 38 / 100;
              fillDisc(vx, vy, 2, 2, accent0);
              putPx(vx, vy, kIdxWarmGleam);
              for (int py = vy + 1; py < yB - 1; ++py) {
                const int jx = vx + (((py / 3) & 1) ? 1 : 0);
                putPx(jx, py, ((py & 1) == 0) ? accent0 : accent1);
              }
            } else if (oceanWorld && (cellH & 1u) == 0u && faceW >= 24 && baseH >= 18) {
              // Wave-carved dark sea-grotto arch at the cliff waterline
              const int gw = std::clamp(faceW * 28 / 100, 6, 16);
              const int gh = std::clamp(baseH * 34 / 100, 5, 14);
              fillOutlineRect(fcx - gw / 2, yB - gh, gw, gh, inkOut, rockDark);
              drawHLine(fcx - gw / 2 + 1, fcx + gw / 2 - 1, yB - 1, sky3);
            } else if (rockWorld && (cellH % 3u) == 0u && faceW >= 24 && baseH >= 18) {
              // Wind-carved stepped sandstone cave hollow at the base of the canyon wall
              const int aw = std::clamp(faceW * 28 / 100, 7, 16);
              const int ah = std::clamp(baseH * 36 / 100, 6, 15);
              const int ax = fcx - aw / 2;
              const int ay = yB - ah;
              fillRect(ax + 2, ay, aw - 4, 2, inkOut);
              fillRect(ax + 1, ay + 2, aw - 2, 2, inkOut);
              fillRect(ax, ay + 4, aw, std::max(2, ah - 4), inkOut);
              drawHLine(ax + 2, ax + aw - 3, ay - 1, rockHi);
              drawHLine(ax + 1, ax + aw - 2, ay + 1, rockDark);
              if (aw >= 10 && ah >= 9) {
                putPx(fcx, ay + ah / 2, accent0);
              }
            }
          }
        }

        // Shored wooden mine-shaft timbers + arched vault header + glowing wall torches on underground var == 7 bays
        if (inMine && var == 7 && !isPortalFront && faceW >= 12 && baseH >= 12) {
          const int postW = std::clamp(faceW / 11, 2, 4);
          const int beamH = std::clamp(baseH / 13, 2, 4);
          fillOutlineRect(xL + 1, yTBase + 1, postW + 2, baseH - 1, trunkCol, inkOut);
          fillOutlineRect(xR - postW - 2, yTBase + 1, postW + 2, baseH - 1, trunkCol, inkOut);
          fillOutlineRect(xL + 1, yTBase + 1, faceW - 2, beamH + 2, pathHi, inkOut);
          // Diagonal timber knee-braces + iron corner plates
          if (faceW >= 22) {
            for (int kb = 0; kb < std::min(5, faceW / 8); ++kb) {
              putPx(xL + postW + 2 + kb, yTBase + beamH + 5 - kb, trunkCol);
              putPx(xR - postW - 3 - kb, yTBase + beamH + 5 - kb, trunkCol);
            }
            putPx(xL + postW + 1, yTBase + beamH + 1, kIdxShipDark);
            putPx(xR - postW - 2, yTBase + beamH + 1, kIdxShipDark);
          }
          // Glowing wall torch sconce with warm dithered radial halo contained inside timber bay
          if ((cellH & 1u) == 0u && faceW >= 18 && baseH >= 18 && d <= 4) {
            const int tcx = (xL + xR) / 2;
            const int tcy = yTBase + baseH * 48 / 100;
            const int maxR = std::max(2, tcy - (yTBase + beamH + 4));
            const int haloR = std::min(std::clamp(faceW / 5, 3, 10), maxR);
            const int savedFog = activeFog;
            activeFog = std::max(0, savedFog - 12);
            fillDitherDisc(tcx, tcy - 1, haloR, haloR, rockMid, pathHi, 7);
            fillDitherDisc(tcx, tcy - 1, std::max(2, haloR / 2), std::max(2, haloR / 2), pathHi, kIdxOreGold, 6);
            // Iron sconce bracket + wooden handle + multi-tone flame
            fillRect(tcx - 1, tcy + 1, 3, std::max(3, baseH / 8), inkOut);
            fillRect(tcx, tcy + 2, 1, std::max(2, baseH / 10), trunkCol);
            fillDisc(tcx, tcy - 1, std::max(1, haloR / 5), std::max(2, haloR / 4), kIdxOreGold);
            putPx(tcx, tcy - 1, kIdxWarmGleam);
            putPx(tcx, tcy - 2, kIdxWarmGleam);
            activeFog = savedFog;
          } else if ((cellH & 1u) != 0u && faceW >= 24 && baseH >= 20 && d <= 4) {
            // Alternate mine bay: barred dungeon niche / recessed drainage grate
            const int aw = std::clamp(faceW * 30 / 100, 7, 18);
            const int ah = std::clamp(baseH * 36 / 100, 7, 18);
            const int ax = (xL + xR) / 2 - aw / 2;
            const int ay = yTBase + baseH * 34 / 100;
            fillOutlineRect(ax, ay, aw, ah, inkOut, rockHi);
            for (int bx = ax + 2; bx < ax + aw - 1; bx += 3) {
              fillRect(bx, ay + 1, 1, ah - 2, kIdxShipDark);
            }
            drawHLine(ax + 1, ax + aw - 2, ay + ah / 2, kIdxShipDark);
          }
        }

        // Enterable PC-98 timbered mine entrance (surface) or daylight exit shaft (mine)
        if (isPortalFront && faceW >= 9 && baseH >= 8) {
          const int pW = std::max(8, faceW * 48 / 100);
          const int pTopTarget = hy - static_cast<int>(std::lround(kProjY * 0.28f / zNearW));
          const int pY = std::clamp(pTopTarget, yTBase + 3, yB - 6);
          const int pH = std::max(6, yB - pY);
          const int pX = xL + (faceW - pW) / 2;
          const int postW = std::max(2, std::min(5, pW / 6));
          const int lintelH = std::max(2, std::min(5, pH / 6));
          fillOutlineRect(pX - 2, pY - lintelH, pW + 4, lintelH + 2, pathHi, inkOut);
          if (!inMine) {
            fillRect(pX, pY, pW, pH, inkOut);
            // Deep recessed timber struts inside the mine tunnel
            if (pW >= 14 && pH >= 12) {
              const int inW = pW - postW * 2 - 4;
              const int inH = pH - 4;
              if (inW >= 6 && inH >= 6) {
                fillOutlineRect(pX + postW + 2, pY + 2, inW, inH, inkOut, rockDark);
              }
            }
            fillOutlineRect(pX, pY, postW + 1, pH, trunkCol, inkOut);
            fillOutlineRect(pX + pW - postW - 1, pY, postW + 1, pH, trunkCol, inkOut);
            // Diagonal timber knee-braces + warm hanging brass mine lantern
            if (pW >= 12 && pH >= 10) {
              putPx(pX + postW + 1, pY + 2, pathHi);
              putPx(pX + pW - postW - 2, pY + 2, pathHi);
              const int lx = pX + pW / 2;
              const int ly = pY + std::max(2, std::min(12, pH / 5));
              const int lHalo = std::clamp(pW / 11, 2, 7);
              fillRect(lx, pY, 1, ly - pY, kIdxShipDark);
              fillDitherDisc(lx, ly + 2, lHalo, lHalo, inkOut, kIdxOreBody, 6);
              fillOutlineRect(lx - 2, ly, 5, 5, kIdxOreGold, inkOut);
              fillRect(lx - 1, ly + 1, 3, 3, kIdxWarmGleam);
              drawHLine(lx - 2, lx + 2, ly - 1, kIdxShipDark);
            }
          } else {
            // Glowing surface daylight shining down the exit adit through cavern gloom
            const int savedFog = activeFog;
            activeFog = std::max(0, savedFog - 18);
            const int inX0 = pX + postW + 1;
            const int inX1 = pX + pW - postW - 1;
            for (int py = pY; py < yB; ++py) {
              const float vFrac = static_cast<float>(py - pY) / static_cast<float>(std::max(1, pH));
              for (int px = inX0; px < inX1; ++px) {
                if (py >= yB - std::max(2, pH / 6)) {
                  putPx(px, py, kIdxWarmGleam);
                } else {
                  bayerPx(px, py, vFrac, sky2, sky3);
                }
              }
            }
            activeFog = savedFog;
            fillOutlineRect(pX, pY, postW + 1, pH, trunkCol, inkOut);
            fillOutlineRect(pX + pW - postW - 1, pY, postW + 1, pH, trunkCol, inkOut);
            putPx(pX, yB, inkOut);
          }
          if (d == 1 && s == 0) {
            const int ux0 = std::clamp(pX - 1, 0, W);
            const int ux1 = std::clamp(pX + pW + 1, 0, W);
            const int uy0 = std::clamp(pY - lintelH, 0, H);
            const int uy1 = std::clamp(yB, 0, H);
            if (ux1 > ux0 && uy1 > uy0) {
              planetSurface_.frontKind = 8;
              planetSurface_.frontArg = inMine ? 0 : 1;
              planetSurface_.frontUv = Area{
                  static_cast<float>(ux0) / static_cast<float>(W),
                  static_cast<float>(uy0) / static_cast<float>(H),
                  static_cast<float>(ux1 - ux0) / static_cast<float>(W),
                  static_cast<float>(uy1 - uy0) / static_cast<float>(H)};
            }
          }
        }
      }
    }
  };

  // Authentic PC-98 Clouds of Xeen / Might & Magic III sloped craggy mountain & geological mass
  // (replaces rectangular outdoor cliff boxes for var 4, 5, 6 — wide rocky base, tapered jagged summit,
  // sunlit left ridge slope vs. deep craggy shadowed right slope using kCragRockTex!)
  auto drawMountainCell = [&](int tx, int ty, int d, int s, int var) {
    const unsigned cellH = static_cast<unsigned>(tx) * 73u ^
                           static_cast<unsigned>(ty) * 151u ^
                           (pSeed >> 5) ^ (static_cast<unsigned>(var) * 29u);
    const bool fogActive = (activeFog > 0);
    const bool fogHighBand = (activeFog > 16);
    const int mtnThr16 = fogHighBand ? std::min(16, activeFog - 16) : activeFog;
    auto colFogBitsForX = [&](int x) -> std::uint8_t {
      if (!fogActive) { return 0u; }
      const int rx = x & 3;
      return static_cast<std::uint8_t>(
          (((kFogMasks.row[mtnThr16][0] >> rx) & 1u) << 0) |
          (((kFogMasks.row[mtnThr16][1] >> rx) & 1u) << 1) |
          (((kFogMasks.row[mtnThr16][2] >> rx) & 1u) << 2) |
          (((kFogMasks.row[mtnThr16][3] >> rx) & 1u) << 3));
    };

    // Peripheral d == 0 looming sloped mountain flank at left/right edge (Might & Magic III style!)
    if (d == 0) {
      if (std::abs(s) != 1) { return; }
      const bool onLeft = (s < 0);
      const int flankW = 30;
      const int xStart = onLeft ? 0 : (W - flankW);
      const int xEnd   = onLeft ? (flankW - 1) : (W - 1);
      int prevYT = -1;
      for (int x = xStart; x <= xEnd; ++x) {
        const int frac = onLeft ? ((x * 1000) / flankW) : (((xEnd - x) * 1000) / flankW);
        // Organic craggy stepped mountain shoulder (not a straight ruler diagonal!)
        const int cragLedge = (frac > 300 && frac < 520) ? -7 : (frac > 680 ? 5 : 0);
        const int jag = static_cast<int>(((static_cast<unsigned>(x) * 7u + cellH) % 5u)) - 2;
        const int yT = std::clamp(8 + (frac * (hy + 22)) / 1000 + cragLedge + jag, 0, H - 1);
        const int yB = std::clamp(H - 1 - (frac * 18) / 1000, yT, H - 1);
        const int outlineBot = (prevYT >= 0 && yT > prevYT + 1) ? yT : yT;
        const int outlineTop = (prevYT >= 0 && prevYT > yT + 1) ? yT : ((prevYT >= 0) ? std::min(yT, prevYT) : yT);
        for (int oy = outlineTop; oy <= outlineBot; ++oy) {
          putPx(x, oy, inkOut);
        }
        prevYT = yT;
        const int uTex = (x + static_cast<int>(cellH & 31u)) & 31;
        const bool shadowSide = !onLeft ? (frac < 580) : (frac > 480);
        for (int y = yT + 1; y <= yB; ++y) {
          const int vTex = (y + static_cast<int>((cellH >> 4) & 31u)) & 31;
          const std::uint8_t cr = kCragRockTex.px[vTex][uTex];
          std::uint8_t cIdx = shadowSide
                                  ? ((cr <= 1u) ? inkOut : (cr == 2u) ? rockDark : rockMid)
                                  : ((cr == 0u) ? inkOut : (cr == 1u) ? rockDark : (cr == 2u) ? rockMid : rockHi);
          if (y == yB) { cIdx = inkOut; }
          else if (iceWorld && !shadowSide && cr == 3u) { cIdx = accent1; }
          idxBuf[y * W + x] = cIdx;
        }
      }
      return;
    }

    const float zNearW = std::max(0.50f, static_cast<float>(d) - 0.36f);
    const int clipL = std::max(0, static_cast<int>(rowX0[d]));
    const int clipR = std::min(W - 1, static_cast<int>(rowX1[d]));
    const int baseL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
    const int baseR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
    const int tileW = std::max(6, baseR - baseL);
    // Wide overlapping rocky skirt so neighboring mountain tiles merge into continuous ridges!
    const int skirt = (tileW * 28) / 100 + 2;
    const int xL = baseL - skirt;
    const int xR = baseR + skirt;
    const int spanW = std::max(6, xR - xL);
    const int yB = std::clamp(cliffYBot(zNearW) + 1, hy + 2, H - 1);

    // Tall dramatic Clouds of Xeen mountain height reaching boldly into the bright sky
    const bool border = isBorderTile(tx, ty);
    float peakScale = (var == 5) ? (0.94f + static_cast<float>(cellH % 4u) * 0.08f)
                    : (var == 6) ? (0.76f + static_cast<float>(cellH % 3u) * 0.09f)
                                 : (0.64f + static_cast<float>(cellH % 4u) * 0.08f);
    if (border) { peakScale += 0.14f; }
    if (oceanWorld) { peakScale *= 0.86f; }
    const int yPeak = std::clamp(cliffYTopScaled(zNearW, peakScale), 3, yB - 6);
    const int peakH = std::max(6, yB - yPeak);

    // Asymmetric primary summit + secondary rocky shoulder
    const int peakU = 380 + static_cast<int>((cellH % 5u) * 60u);  // 380..620
    const int shldU = (peakU < 500) ? (peakU + 230) : (peakU - 230);
    const int shldH = 560 + static_cast<int>(((cellH >> 3) % 3u) * 75u);  // permille of peakH

    auto profilePerm = [&](int u) -> int {
      if (u <= 0 || u >= 1000) { return 0; }
      int h = 0;
      if (rockWorld) {
        // Monument Valley stepped flat-topped canyon mesa & overhanging caprock plateau
        if (u < 190) {
          h = (u * 520) / 190;
        } else if (u < 290) {
          h = 520 + ((u - 190) * 440) / 100;
        } else if (u <= 710) {
          const int jag = static_cast<int>(((static_cast<unsigned>(u / 48) * 7u + cellH) % 5u)) * 18 - 30;
          h = std::clamp(960 + jag, 850, 1000);
        } else if (u < 810) {
          h = 520 + ((810 - u) * 440) / 100;
        } else {
          h = ((1000 - u) * 520) / 190;
        }
      } else if (iceWorld) {
        // Razor-sharp jagged twin-crested glacial peak / iceberg
        const int d1 = std::abs(u - peakU);
        const int span1 = (u < peakU) ? peakU : (1000 - peakU);
        const int h1 = std::max(0, 1000 - (d1 * 1000) / std::max(1, span1));
        const int d2 = std::abs(u - shldU);
        const int h2 = std::max(0, shldH - (d2 * shldH) / 340);
        const int notch = ((u / 55 + static_cast<int>(cellH)) & 1) ? 35 : -20;
        h = std::clamp(std::max(h1, h2) + (h1 > 200 ? notch : 0), 0, 1000);
      } else if (lavaWorld) {
        // Steep volcanic caldera cone with craggy summit rim notch
        const int d1 = std::abs(u - peakU);
        const int span1 = (u < peakU) ? peakU : (1000 - peakU);
        int h1 = std::max(0, 1000 - (d1 * 1020) / std::max(1, span1));
        if (d1 < 85 && var == 5) { h1 -= (85 - d1) * 2; }  // volcanic crater rim dip
        const int d2 = std::abs(u - shldU);
        const int h2 = std::max(0, (shldH - 60) - (d2 * shldH) / 360);
        h = std::max(h1, h2);
      } else {
        // Classic PC-98 Clouds of Xeen craggy Alpine mountain / coastal crag with stepped shoulders
        const int d1 = std::abs(u - peakU);
        const int span1 = (u < peakU) ? peakU : (1000 - peakU);
        // Slightly convex craggy slope (broader upper shoulder like PC-98 Clouds of Xeen!)
        const int lin1 = std::max(0, 1000 - (d1 * 1000) / std::max(1, span1));
        const int h1 = std::min(1000, (lin1 * 1160) / 1000 - (lin1 * lin1) / 6200);
        const int d2 = std::abs(u - shldU);
        const int h2 = std::max(0, shldH - (d2 * shldH) / 390);
        const int cragStep = static_cast<int>(((static_cast<unsigned>(u / 62) * 11u + cellH) % 5u)) * 22 - 40;
        h = std::clamp(std::max(h1, h2) + ((std::max(h1, h2) > 180) ? cragStep : 0), 0, 1000);
      }
      return h;
    };

    const int cx0 = std::max(clipL, xL);
    const int cx1 = std::min(clipR, xR);
    if (cx0 > cx1) { return; }

    int prevYT = -1;
    const int texUOff = static_cast<int>((cellH * 7u) & 31u);
    const int texVOff = static_cast<int>((cellH * 13u) & 31u);
    for (int x = cx0; x <= cx1; ++x) {
      if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { prevYT = -1; continue; }
      const int uPerm = ((x - xL) * 1000) / spanW;
      const int hPerm = profilePerm(uPerm);
      if (hPerm < 35) { prevYT = -1; continue; }
      const int colH = std::max(2, (peakH * hPerm) / 1000);
      // Slight rocky foot arch so outer edges tuck softly into terrain
      const int footLift = (uPerm < 120 || uPerm > 880) ? 1 : 0;
      const int yColB = std::min(H - 1, yB - footLift);
      const int yT = std::clamp(yColB - colH, 0, yColB);
      if (yT >= yColB) { prevYT = -1; continue; }

      // Crisp unbroken PC-98 black ink contour along steep jagged mountain slopes
      const int stepBot = (prevYT >= 0 && yT > prevYT + 1) ? yT : (yT + 1);
      const int stepTop = (prevYT >= 0 && prevYT > yT + 1) ? yT : ((prevYT >= 0) ? std::min(yT, prevYT) : yT);
      for (int oy = stepTop; oy <= std::min(yColB, stepBot); ++oy) {
        idxBuf[oy * W + x] = activeInk;
      }
      prevYT = yT;
      idxBuf[yColB * W + x] = activeInk;

      const std::uint8_t colFogMask = colFogBitsForX(x);
      // Scale crag texture density proportionally so close d=1 mountains stay crisp 1:1 PC-98 pixel art!
      const int uScale = std::clamp(spanW, 32, 112);
      const int uTex = ((uPerm * uScale) / 1000 + texUOff) & 31;
      const int vScale = std::clamp(peakH, 28, 64);
      const std::uint32_t invPeakH =
          ((1u << 20) + static_cast<std::uint32_t>(peakH) - 1u) / static_cast<std::uint32_t>(peakH);

      for (int y = yT + 1; y < yColB; ++y) {
        const int vFromPeak = static_cast<int>((static_cast<std::uint32_t>((y - yPeak) << 5) * invPeakH) >> 20);
        const int vTex = (((y - yPeak) * vScale) / std::max(1, peakH) + texVOff) & 31;
        const int v16 = std::clamp(vFromPeak >> 1, 0, 15);
        // Winding 3D diagonal mountain spine ridge splitting sunlit left face from shadowed right face!
        const int ridgeWobble = (((vFromPeak / 4 + static_cast<int>(cellH & 3u)) & 1) ? 35 : -30);
        const int ridgeU = peakU + ((vFromPeak - 4) * ((cellH & 1u) ? 6 : -4)) + ridgeWobble;
        const bool isRidgeLine = (std::abs(uPerm - ridgeU) < 22) && (vFromPeak >= 2);
        const bool shadowSide = (uPerm > ridgeU);
        const std::uint8_t cr = kCragRockTex.px[vTex][uTex];

        std::uint8_t cIdx = rockMid;
        if (isRidgeLine) {
          cIdx = (cr == 0u || (vFromPeak & 1)) ? inkOut : rockDark;
        } else if (rockWorld) {
          // Terracotta canyon strata warped across sunlit/shadowed mesa faces
          const int sv = (v16 + ((uPerm / 140) & 1)) & 15;
          if (shadowSide) {
            cIdx = (cr == 0u) ? inkOut : (sv == 3 || sv == 9) ? rockMid : rockDark;
          } else {
            if (cr == 0u) { cIdx = rockDark; }
            else if (sv == 2 || sv == 3 || sv == 8 || cr == 3u) { cIdx = rockHi; }
            else if (sv == 6 || sv == 11) { cIdx = pathMid; }
            else { cIdx = (cr == 1u) ? rockDark : rockMid; }
          }
        } else if (iceWorld) {
          // Razor crystalline glacial peak: bright snow/cyan left slope, cobalt dark right slope
          if (shadowSide) {
            cIdx = (cr == 0u) ? inkOut : (cr == 3u && v16 < 10) ? rockMid : rockDark;
          } else {
            cIdx = (cr == 0u) ? rockDark : (cr == 3u || v16 < 4) ? accent1 : (cr == 2u ? rockHi : rockMid);
          }
        } else if (lavaWorld) {
          // Dark volcanic basalt crag with glowing molten magma river winding near the ridge
          const bool magmaVein = (std::abs(uPerm - (ridgeU + 30)) < 26) && (vFromPeak >= 5);
          if (magmaVein && ((y + x) & 1) == 0) {
            cIdx = (vFromPeak < 12) ? kIdxWarmGleam : ((y & 2) ? accent1 : accent0);
          } else if (shadowSide) {
            cIdx = (cr <= 1u) ? inkOut : (cr == 3u ? rockMid : rockDark);
          } else {
            cIdx = (cr == 0u) ? inkOut : (cr == 1u) ? rockDark : (cr == 2u) ? rockMid : rockHi;
          }
        } else {
          // Classic PC-98 Clouds of Xeen high-contrast craggy Alpine / Sea mountain!
          if (!shadowSide) {
            cIdx = (cr == 0u) ? inkOut
                 : (cr == 1u) ? rockDark
                 : (cr == 2u) ? rockMid
                              : rockHi;
            if (y <= yT + 2 && cr > 0u) { cIdx = rockHi; }
          } else {
            cIdx = (cr <= 1u) ? inkOut
                 : (cr == 2u) ? rockDark
                              : ((kBayer4[y & 3][x & 3] < 10) ? rockMid : rockDark);
          }
          // Biome organic footing accents (surf foam on Ocean, mossy clumps on Temperate lower slope)
          if (oceanWorld && y >= yColB - 3 && ((x + y) & 1) == 0) {
            cIdx = sky3;
          } else if (biomeIdx == 0 && y >= yColB - std::max(2, peakH / 6) && cr >= 2u && ((x + y) & 1) == 0) {
            cIdx = shadowSide ? leaf0 : leaf1;
          }
        }
        // Mineral vein sparkle on var == 6 mountains
        if (var == 6 && !shadowSide && cr == 3u && v16 >= 6 && v16 <= 11 && ((x + y) & 1) == 0) {
          cIdx = accent0;
        }

        if (!fogActive) {
          idxBuf[y * W + x] = cIdx;
        } else if (cIdx == inkOut) {
          idxBuf[y * W + x] = activeInk;
        } else {
          const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
          const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
          idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
        }
      }
    }

    // Grounded base scrub/boulders nestled at foot of Temperate / Rock mountains
    if (!oceanWorld && spanW >= 24 && d <= 4) {
      if (biomeIdx == 0) {
        drawScrubBush(xL + spanW * 24 / 100, yB, std::min(1.0f, static_cast<float>(tileW) / 85.0f));
        drawScrubBush(xL + spanW * 78 / 100, yB, std::min(0.9f, static_cast<float>(tileW) / 95.0f));
      }
    }
  };

  // Authentic PC-98 Clouds of Xeen domed craggy rock cave entrance (replaces rectangular portal box on surface!)
  auto drawCaveMoundPortal = [&](int tx, int ty, int d, int s) {
    if (d == 0) {
      drawMountainCell(tx, ty, d, s, 5);
      return;
    }
    const float zNearW = std::max(0.50f, static_cast<float>(d) - 0.42f);
    const int clipL = std::max(0, static_cast<int>(rowX0[d]));
    const int clipR = std::min(W - 1, static_cast<int>(rowX1[d]));
    const int baseL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
    const int baseR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
    const int tileW = std::max(8, baseR - baseL);
    const int skirt = tileW * 16 / 100;
    const int xL = baseL - skirt;
    const int xR = baseR + skirt;
    const int spanW = std::max(8, xR - xL);
    const int yB = std::clamp(cliffYBot(zNearW) + 1, hy + 2, H - 1);
    const int yPeak = std::clamp(cliffYTopScaled(zNearW, 0.70f), 6, yB - 10);
    const int peakH = std::max(10, yB - yPeak);

    const bool fogActive = (activeFog > 0);
    const bool fogHighBand = (activeFog > 16);
    const int thr16 = fogHighBand ? std::min(16, activeFog - 16) : activeFog;
    auto colFogBitsForX = [&](int x) -> std::uint8_t {
      if (!fogActive) { return 0u; }
      const int rx = x & 3;
      return static_cast<std::uint8_t>(
          (((kFogMasks.row[thr16][0] >> rx) & 1u) << 0) |
          (((kFogMasks.row[thr16][1] >> rx) & 1u) << 1) |
          (((kFogMasks.row[thr16][2] >> rx) & 1u) << 2) |
          (((kFogMasks.row[thr16][3] >> rx) & 1u) << 3));
    };

    // Draw broad craggy domed boulder hill (Clouds of Xeen PC-98 cave mound silhouette!)
    const int cx0 = std::max(clipL, xL);
    const int cx1 = std::min(clipR, xR);
    int prevYT = -1;
    for (int x = cx0; x <= cx1; ++x) {
      if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { prevYT = -1; continue; }
      const int uPerm = ((x - xL) * 1000) / spanW;
      const int du = std::abs(uPerm - 500);
      // Rounded craggy dome profile (steep sides, broad boulder crown)
      const int domeSq = std::max(0, 250000 - du * du);
      int hPerm = (domeSq * 1000) / 250000;
      const int bump = static_cast<int>(((static_cast<unsigned>(uPerm / 70) * 9u) % 5u)) * 22 - 35;
      hPerm = std::clamp(hPerm + ((hPerm > 220) ? bump : 0), 0, 1000);
      if (hPerm < 45) { prevYT = -1; continue; }
      const int yT = std::clamp(yB - std::max(3, (peakH * hPerm) / 1000), 0, yB - 1);
      const int stepTop = (prevYT >= 0) ? std::min(yT, prevYT) : yT;
      const int stepBot = (prevYT >= 0 && yT > prevYT + 1) ? yT : (yT + 1);
      for (int oy = stepTop; oy <= std::min(yB, stepBot); ++oy) {
        idxBuf[oy * W + x] = activeInk;
      }
      prevYT = yT;
      idxBuf[yB * W + x] = activeInk;
      const std::uint8_t colFogMask = colFogBitsForX(x);
      const int uScale = std::clamp(spanW, 32, 104);
      const int uTex = ((uPerm * uScale) / 1000) & 31;
      const int vScale = std::clamp(peakH, 28, 56);
      for (int y = yT + 1; y < yB; ++y) {
        const int vTex = (((y - yPeak) * vScale) / std::max(1, peakH)) & 31;
        const std::uint8_t cr = kCragRockTex.px[vTex][uTex];
        const bool shadeRight = (uPerm > 580);
        std::uint8_t cIdx = shadeRight
                                ? ((cr <= 1u) ? inkOut : (cr == 2u) ? rockDark : rockMid)
                                : ((cr == 0u) ? inkOut : (cr == 1u) ? rockDark : (cr == 2u) ? rockMid : rockHi);
        if (!fogActive) {
          idxBuf[y * W + x] = cIdx;
        } else if (cIdx == inkOut) {
          idxBuf[y * W + x] = activeInk;
        } else {
          const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
          const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
          idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
        }
      }
    }

    // Timber-framed dark cave entrance carved into the front face of the craggy mound!
    const int pOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
    const bool isPortalFront = (fdx == -kDx[pOut] && fdy == -kDy[pOut]);
    if (isPortalFront && tileW >= 10 && peakH >= 10) {
      const int pW = std::max(8, tileW * 44 / 100);
      const int pTopTarget = hy - static_cast<int>(std::lround(kProjY * 0.28f / zNearW));
      const int pY = std::clamp(pTopTarget, yPeak + std::max(4, peakH / 4), yB - 6);
      const int pH = std::max(6, yB - pY);
      const int pX = (xL + xR - pW) / 2;
      const int postW = std::max(2, std::min(5, pW / 6));
      const int lintelH = std::max(2, std::min(5, pH / 6));
      // Heavy rough-hewn timber crossbeam lintel + black cave maw + interior support frame
      fillOutlineRect(pX - 2, pY - lintelH, pW + 4, lintelH + 2, pathHi, inkOut);
      fillRect(pX, pY, pW, pH, inkOut);
      if (pW >= 14 && pH >= 12) {
        const int inW = pW - postW * 2 - 4;
        const int inH = pH - 4;
        if (inW >= 6 && inH >= 6) {
          fillOutlineRect(pX + postW + 2, pY + 2, inW, inH, inkOut, rockDark);
        }
      }
      fillOutlineRect(pX, pY, postW + 1, pH, trunkCol, inkOut);
      fillOutlineRect(pX + pW - postW - 1, pY, postW + 1, pH, trunkCol, inkOut);
      if (pW >= 12 && pH >= 10) {
        putPx(pX + postW + 1, pY + 2, pathHi);
        putPx(pX + pW - postW - 2, pY + 2, pathHi);
        const int lx = pX + pW / 2;
        const int ly = pY + std::max(2, std::min(12, pH / 5));
        const int lHalo = std::clamp(pW / 11, 2, 7);
        fillRect(lx, pY, 1, ly - pY, kIdxShipDark);
        fillDitherDisc(lx, ly + 2, lHalo, lHalo, inkOut, kIdxOreBody, 6);
        fillOutlineRect(lx - 2, ly, 5, 5, kIdxOreGold, inkOut);
        fillRect(lx - 1, ly + 1, 3, 3, kIdxWarmGleam);
        drawHLine(lx - 2, lx + 2, ly - 1, kIdxShipDark);
      }
      if (d == 1 && s == 0) {
        const int ux0 = std::clamp(pX - 1, 0, W);
        const int ux1 = std::clamp(pX + pW + 1, 0, W);
        const int uy0 = std::clamp(pY - lintelH, 0, H);
        const int uy1 = std::clamp(yB, 0, H);
        if (ux1 > ux0 && uy1 > uy0) {
          planetSurface_.frontKind = 8;
          planetSurface_.frontArg = 1;
          planetSurface_.frontUv = Area{
              static_cast<float>(ux0) / static_cast<float>(W),
              static_cast<float>(uy0) / static_cast<float>(H),
              static_cast<float>(ux1 - ux0) / static_cast<float>(W),
              static_cast<float>(uy1 - uy0) / static_cast<float>(H)};
        }
      }
    }
  };

  // Back-to-front painter's algorithm across visible cells (culled by vis[2] + depthColOcc)
  for (int d = maxVisD; d >= 0; --d) {
    activeFog = static_cast<int>(inMine ? kMineDepthFog[d] : kDepthFog[d]);
    activeInk = inkForFog(activeFog);
    const float zMid = (d == 0) ? 0.54f : static_cast<float>(d);
    const float zFar = (d == 0) ? 0.62f : static_cast<float>(d) + 0.24f;
    const float zNear = (d == 0) ? 0.48f : std::max(0.54f, static_cast<float>(d) - 0.24f);
    const int gyFar = yAtZ(zFar);
    const int gyMid = yAtZ(zMid);
    const int gyNear = yAtZ(zNear);
    const float sTile = (d == 0) ? 1.35f : (1.02f / zMid);
    const float htMid = halfTileAtZ(zMid);
    const float htNear = halfTileAtZ(zNear);
    const int maxS = d + 1;

    for (int k = maxS; k >= 0; --k) {
      for (int sidePass = 0; sidePass < (k == 0 ? 1 : 2); ++sidePass) {
        const int s = (k == 0) ? 0 : (sidePass == 0 ? -k : k);
        if (d == 0 && s == 0) { continue; }
        if (!isVis(d, s)) { continue; }
        const int tx = planetSurface_.px + fdx * d + rdx * s;
        const int ty = planetSurface_.py + fdy * d + rdy * s;
        if (tx < 0 || tx >= N || ty < 0 || ty >= N) { continue; }
        const std::uint8_t tile = tileAt(tx, ty);
        const int var = variantAt(tx, ty);
        const float tcxMid = tileCenterAtZ(s, zMid);
        const float tcxNear = tileCenterAtZ(s, zNear);

        if (tile == 1) {
          // Surface: portal = craggy cave mound; var 7 = ancient stone fortress ruin;
          // var 4..6 = sloped craggy Clouds of Xeen mountains; var 0..3 = towering forests/groves.
          // Underground mine: all var 4..7 = full-height beveled World of Xeen dungeon walls.
          if (!inMine && tx == static_cast<int>(planetSurface_.portalX) &&
              ty == static_cast<int>(planetSurface_.portalY)) {
            drawCaveMoundPortal(tx, ty, d, s);
            continue;
          }
          if ((var & 4) != 0) {
            if (inMine || var == 7) {
              drawCliffCell(tx, ty, d, s, var);
            } else {
              drawMountainCell(tx, ty, d, s, var);
            }
            continue;
          }
          const float groveBoost = (biomeIdx == 0) ? 1.05f : 1.24f;
          if (d == 0) {
            // Peripheral l0/r0 foliage/rock peeking in at extreme left/right edge only
            const int edgeX = (s < 0)
                ? static_cast<int>(std::lround(tcxMid + htMid * 0.62f))
                : static_cast<int>(std::lround(tcxMid - htMid * 0.62f));
            drawBiomeProp(var % 4, edgeX, gyMid, sTile * 0.96f * groveBoost);
            continue;
          }
          if (isBorderTile(tx, ty)) {
            // Monumental perimeter forest wall / biome crag grove
            const int lx = static_cast<int>(std::lround(tcxMid - htMid * 0.42f));
            const int rx = static_cast<int>(std::lround(tcxMid + htMid * 0.42f));
            drawBiomeProp((var + 0) % 4, lx, gyMid, sTile * 1.04f * groveBoost);
            drawBiomeProp((var + 1) % 4, rx, gyMid + 1, sTile * 1.02f * groveBoost);
            drawScrubBush(static_cast<int>(std::lround(tcxNear + ((var & 1) ? -0.15f : 0.15f) * htNear)),
                          gyNear, sTile * 0.80f);
          } else {
            // Dense towering forest grove / monumental biome obstacle cluster
            const float sign = (var & 1) ? -1.0f : 1.0f;
            const int xMain = static_cast<int>(std::lround(tcxMid + sign * htMid * 0.28f));
            const int xSec = static_cast<int>(std::lround(tcxMid - sign * htMid * 0.36f));
            const int xAcc = static_cast<int>(std::lround(tcxNear + sign * htNear * 0.12f));
            drawBiomeProp((var + 1) % 4, xSec, gyFar + 1, sTile * 0.92f * groveBoost);
            if (s == 0 && d == 1) {
              drawBiomeProp((var + 2) % 4, static_cast<int>(std::lround(tcxMid)), gyMid, sTile * 1.02f * groveBoost);
            }
            drawBiomeProp(var % 4, xMain, gyMid + 1, sTile * 1.10f * groveBoost);
            if (var & 2) {
              drawScrubBush(xAcc, gyNear, sTile * 0.82f);
            } else {
              drawBiomeProp(3, xAcc, gyNear, sTile * 0.74f * groveBoost);
            }
          }
          continue;
        }

        // Non-blocking low ground accents on open tiles (0)
        if (tile == 0 && (var % 3) == 0 && d >= 1 && d <= 4) {
          const float off = ((var & 1) ? -0.44f : 0.44f) * htMid;
          drawGroundScatter(static_cast<int>(std::lround(tcxMid + off)), gyMid, sTile, var);
        }

        // Ship & Ore sprites across forward view cone (clickable only at d==1 && s==0)
        if (d >= 1 && d <= 6 && std::abs(s) <= d) {
          const int objCx = static_cast<int>(std::lround(tcxMid));
          if (!inMine && tx == planetSurface_.shipX && ty == planetSurface_.shipY) {
            const int sw = std::max(10, static_cast<int>(std::lround(44.0f * sTile)));
            const int sh = std::max(6, static_cast<int>(std::lround(26.0f * sTile)));
            const int sx = objCx - sw / 2;
            const int sy = gyMid - sh;
            if (sx + sw > 2 && sx < W - 2) {
              // Elliptical ground shadow + thruster scorch
              fillDisc(objCx, gyMid, std::max(4, sw * 46 / 100), std::max(1, sh / 8), inkOut);
              // Splayed landing struts & footpads
              const int strutH = std::max(2, sh * 28 / 100);
              const int footW = std::max(2, sw * 14 / 100);
              fillOutlineRect(objCx - sw * 46 / 100, gyMid - strutH, footW, strutH + 1, kIdxShipDark, inkOut);
              fillOutlineRect(objCx + sw * 46 / 100 - footW, gyMid - strutH, footW, strutH + 1, kIdxShipDark, inkOut);
              // Side stabilizer wings / pods
              const int wingH = std::max(3, sh * 32 / 100);
              fillOutlineRect(objCx - sw * 48 / 100, gyMid - sh * 58 / 100, sw * 96 / 100, wingH, kIdxShipDark, inkOut);
              fillRect(objCx - sw * 46 / 100, gyMid - sh * 56 / 100, sw * 92 / 100, std::max(1, wingH / 3), kIdxShipMid);
              // Main tapered hull fuselage with upper specular bevel & panel seams
              const int hullW = std::max(6, sw * 74 / 100);
              const int hullH = std::max(4, sh * 46 / 100);
              const int hullY = gyMid - sh * 72 / 100;
              fillOutlineRect(objCx - hullW / 2, hullY, hullW, hullH, kIdxShipMid, inkOut);
              fillRect(objCx - hullW / 2 + 1, hullY + 1, std::max(2, hullW - 2), std::max(1, hullH * 4 / 10), kIdxShipLight);
              fillRect(objCx - hullW / 2 + 1, hullY + hullH - std::max(2, hullH / 4), std::max(2, hullW - 2),
                       std::max(1, hullH / 5), kIdxShipDark);
              // Twin ventral thruster bells
              if (sw >= 18) {
                fillOutlineRect(objCx - hullW / 4 - 2, hullY + hullH - 2, 4, std::max(2, sh / 7), kIdxShipDark, inkOut);
                fillOutlineRect(objCx + hullW / 4 - 2, hullY + hullH - 2, 4, std::max(2, sh / 7), kIdxShipDark, inkOut);
              }
              // Upper bridge superstructure + glowing cyan multi-pane cockpit canopy + beacon mast
              const int topW = std::max(4, sw * 44 / 100);
              const int topH = std::max(3, sh * 32 / 100);
              const int topY = gyMid - sh * 96 / 100;
              fillOutlineRect(objCx - topW / 2, topY, topW, topH, kIdxShipMid, inkOut);
              fillRect(objCx - topW / 2 + 1, topY + 1, std::max(2, topW - 2), std::max(1, topH / 3), kIdxShipLight);
              const int visW = std::max(2, sw * 28 / 100);
              const int visH = std::max(1, sh * 16 / 100);
              fillOutlineRect(objCx - visW / 2 - 1, topY + std::max(1, topH / 3), visW + 2, visH + 2, kIdxShipVisor, inkOut);
              if (visW >= 5) {
                putPx(objCx - visW / 3, topY + std::max(1, topH / 3) + 1, kIdxStarCool);
              }
              if (sh >= 14) {
                fillRect(objCx + topW / 4, topY - std::max(2, sh / 7), 1, std::max(2, sh / 7), kIdxShipLight);
                putPx(objCx + topW / 4, topY - std::max(2, sh / 7) - 1, kIdxWarmGleam);
              }
              if (d == 1 && s == 0) {
                const int cx0 = std::clamp(sx, 0, W);
                const int cx1 = std::clamp(sx + sw, 0, W);
                if (cx1 > cx0) {
                  planetSurface_.frontKind = 6;
                  planetSurface_.frontArg = 0;
                  planetSurface_.frontUv = Area{
                      static_cast<float>(cx0) / static_cast<float>(W),
                      static_cast<float>(std::max(0, sy)) / static_cast<float>(H),
                      static_cast<float>(cx1 - cx0) / static_cast<float>(W),
                      static_cast<float>(sh) / static_cast<float>(H)};
                }
              }
            }
          }
          const int oreIdx = (d <= 5) ? oreAt(tx, ty) : -1;
          if (oreIdx >= 0) {
            const int ow = std::max(7, static_cast<int>(std::lround(21.0f * sTile)));
            const int oh = std::max(5, static_cast<int>(std::lround(15.0f * sTile)));
            const int ox = objCx - ow / 2;
            const int oy = gyMid - oh;
            if (ox + ow > 2 && ox < W - 2) {
              // Cast shadow + riveted PC-98 sci-fi ore container with beveled sunlit lid & glowing core
              fillDisc(objCx, gyMid, std::max(3, ow * 48 / 100), std::max(1, oh / 6), inkOut);
              fillOutlineRect(ox, oy, ow, oh, kIdxOreBody, inkOut);
              // Sunlit top beveled lid
              const int lidH = std::max(2, oh * 30 / 100);
              fillRect(ox + 1, oy + 1, std::max(2, ow - 2), lidH, kIdxOreGold);
              drawHLine(ox + 1, ox + ow - 2, oy + 1, kIdxWarmGleam);
              drawHLine(ox + 1, ox + ow - 2, oy + lidH + 1, inkOut);
              // Dark metal corner brackets & vertical ribs
              if (ow >= 11 && oh >= 9) {
                fillRect(ox + 1, oy + 1, 2, oh - 2, kIdxShipDark);
                fillRect(ox + ow - 3, oy + 1, 2, oh - 2, kIdxShipDark);
                putPx(ox + 1, oy + 2, kIdxShipLight);
                putPx(ox + ow - 2, oy + 2, kIdxShipLight);
              }
              // Recessed glowing ore core window + specular gleam
              const int coreW = std::max(3, ow * 40 / 100);
              const int coreH = std::max(2, oh * 34 / 100);
              const int coreX = objCx - coreW / 2;
              const int coreY = oy + lidH + std::max(1, (oh - lidH - coreH) / 2);
              fillOutlineRect(coreX - 1, coreY - 1, coreW + 2, coreH + 2, kIdxOreGold, inkOut);
              fillRect(coreX, coreY, std::max(1, coreW - 1), std::max(1, coreH - 1), kIdxWarmGleam);
              if (coreW >= 4) { putPx(coreX, coreY, kIdxStarCool); }
              if (d == 1 && s == 0) {
                const int cx0 = std::clamp(ox, 0, W);
                const int cx1 = std::clamp(ox + ow, 0, W);
                if (cx1 > cx0) {
                  planetSurface_.frontKind = 5;
                  planetSurface_.frontArg = oreIdx;
                  planetSurface_.frontUv = Area{
                      static_cast<float>(cx0) / static_cast<float>(W),
                      static_cast<float>(std::max(0, oy)) / static_cast<float>(H),
                      static_cast<float>(cx1 - cx0) / static_cast<float>(W),
                      static_cast<float>(oh) / static_cast<float>(H)};
                }
              }
            }
          }
        }
      }
    }
  }
  activeFog = 0;
  activeInk = inkOut;

  // 1px retro PC-98 bezel inner shadow
  fillRect(0, 0, W, 1, inkOut);
  fillRect(0, 0, 1, H, inkOut);

  // Single linear RGB4 (4-4-4) palette resolve pass from L1-resident idxBuf into transient stack RGBA8 buffer (0 B heap)
  Color rgbaBuf[W * H];
  for (int i = 0; i < W * H; ++i) {
    rgbaBuf[i] = resolvedLut[idxBuf[i]];
  }

  if (planetTex_.id == 0) {
    Image img{};
    img.data = rgbaBuf;
    img.width = W;
    img.height = H;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    planetTex_ = LoadTextureFromImage(img);
    if (planetTex_.id != 0) {
      SetTextureFilter(planetTex_, TEXTURE_FILTER_POINT);
    }
  } else {
    UpdateTexture(planetTex_, rgbaBuf);
  }
  if (planetScanlineTex_.id == 0) {
    // True PC-98 CRT scanline mask: 1 x (H * 4) bilinear beam-gap profile locked 1:1 to all 144 pixel rows.
    // Built once (2.3 KB VRAM, 0 B persistent heap, 1 DrawTexturePro/frame, moire-free at arbitrary scales).
    constexpr int kScanSub = 4;
    constexpr int kScanH = H * kScanSub;
    static constexpr unsigned char kRowAlpha[kScanSub] = {28u, 0u, 22u, 118u};
    Color scanBuf[kScanH];
    for (int y = 0; y < H; ++y) {
      for (int s = 0; s < kScanSub; ++s) {
        scanBuf[y * kScanSub + s] = Color{0u, 0u, 0u, kRowAlpha[s]};
      }
    }
    Image scanImg{};
    scanImg.data = scanBuf;
    scanImg.width = 1;
    scanImg.height = kScanH;
    scanImg.mipmaps = 1;
    scanImg.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    planetScanlineTex_ = LoadTextureFromImage(scanImg);
    if (planetScanlineTex_.id != 0) {
      SetTextureFilter(planetScanlineTex_, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(planetScanlineTex_, TEXTURE_WRAP_CLAMP);
    }
  }
}

void ShellView::PaintStage(const Simulation& sim, const TaskStage& stage, const Area& origin) {
  const Palette& p = theme_.palette;
  const bool night = theme_.id.find("-night") != std::string::npos;
  const bool darkHousing = night || (theme_.id != "classic" && stage.id != "machine");
  const Color housing = darkHousing ? ToRay(p.chrome) : (night ? ToRay(p.panel) : HexColor("#fdfbf7", ToRay(p.panel)));
  const Color edge = darkHousing ? ToRay(p.borderDark) : (night ? ToRay(p.borderDark) : HexColor("#8c6f4e", ToRay(p.borderDark)));
  const float scale = StageScale(stage, theme_);
  const Area stageFrame{origin.x, origin.y, stage.width * scale, stage.height * scale};
  PaintChamferBoxRay(stageFrame, kChamferPanel, housing, edge, ToRay(p.border));

  const bool reduced = sim.State().reducedMotion;
  const float tSec = reduced ? 0.0f : static_cast<float>(frameTimeMs_ * 0.001);
  const bool engineStage = stage.id == "machine";
  const bool o2Stage = stage.id == "o2mach";
  const bool bridgeStage = stage.id == "bridge";

  // Engine drive telemetry derived from simulation state
  const float condRatio = std::clamp(static_cast<float>(sim.State().engineCondition) / 100.0f, 0.0f, 1.0f);
  const bool underway = sim.FindOngoing("travel") != nullptr;
  const float rpmFactor =
      (reduced || sim.State().engineCondition <= 0.0)
          ? 0.0f
          : ((0.50f + 0.55f * condRatio) * (underway ? 1.85f : 1.0f));
  const float turbinePhase1 = tSec * 2.55f * rpmFactor;
  const float turbinePhase2 = tSec * 2.55f * (76.0f / 52.0f) * rpmFactor;
  const float corePhase = -tSec * 0.62f * rpmFactor;

  // O2 filter telemetry derived from simulation state
  const bool o2Running = sim.BankRunning();
  int o2Seated = 0;
  for (const CanisterSlot& s : sim.Life().slots) {
    if (s.state == "active" || s.state == "installed") { ++o2Seated; }
  }

  int machLampIdx = 0;
  int machCylIdx = 0;
  int machFinIdx = 0;
  int o2LouvreIdx = 0;
  int o2FlowIdx = 0;
  int o2StreakIdx = 0;
  int o2DustIdx = 0;

  MachinePart grown;
  for (const MachinePart& drawn : stage.parts) {
    if (scale != 1.0f) {
      grown = drawn;
      grown.box = Area{drawn.box.x * scale, drawn.box.y * scale,
                       drawn.box.w * scale, drawn.box.h * scale};
    }
    MachinePart part = scale != 1.0f ? grown : drawn;

    // --- Priority 2: Engine Room dynamic stage replacement/modulation ---
    if (engineStage) {
      if (drawn.cls == "mach-flow" || drawn.cls == "mach-spoke" || drawn.cls == "mach-nd") {
        continue;  // Drawn smoothly by animated Engine Room pass below
      }
      if (drawn.cls == "mach-tick" &&
          ((drawn.box.w == 3.0f && drawn.box.h == 3.0f) ||
           (drawn.box.w == 6.0f && drawn.box.h == 6.0f) ||
           (drawn.box.w == 5.0f && drawn.box.h == 5.0f))) {
        continue;  // Core encoder ticks & turbine blades rotate continuously below
      }
      if (drawn.cls == "mach-hub2" && drawn.box.x > 400.0f) {
        continue;  // Drawn on top of rotating turbine spokes below
      }
      if (drawn.cls == "mach-hub2" && drawn.box.x < 120.0f) {
        continue;  // Drawn on top of sweeping gauge needles below
      }
      if (drawn.cls == "mach-core") {
        const float pulse = reduced ? 0.5f : (0.5f + 0.5f * std::sin(tSec * 3.4f));
        const float cx = origin.x + (drawn.box.x + drawn.box.w * 0.5f) * scale;
        const float cy = origin.y + (drawn.box.y + drawn.box.h * 0.5f) * scale;
        const float r = drawn.box.w * 0.5f * scale;
        const Color coreCol = condRatio < 0.50f ? (night ? ToRay(p.terracotta) : HexColor("#b05a42", ToRay(p.terracotta)))
                                                : (night ? ToRay(p.amber) : HexColor("#c08a3e", ToRay(p.amber)));
        DrawCircleV(Vector2{cx, cy}, r * (1.28f + 0.18f * pulse), Fade(coreCol, 0.25f + 0.20f * pulse));
        DrawCircleV(Vector2{cx, cy}, r, StageMix((night ? ToRay(p.panel2) : HexColor("#fdfbf7", WHITE)), coreCol, 0.18f + 0.32f * pulse));
        continue;
      }
      if (drawn.cls == "mach-rod") {
        if (drawn.box.w < 10.0f) {
          // Vertical drive belt between upper & lower turbines: draw belt + traveling seam ticks
          PaintScenePart(part, origin);
          if (rpmFactor > 0.0f) {
            const float bx = origin.x + (drawn.box.x + drawn.box.w * 0.5f) * scale;
            const float byTop = origin.y + drawn.box.y * scale;
            const float bh = drawn.box.h * scale;
            for (int bi = 0; bi < 3; ++bi) {
              const float u = std::fmod(tSec * 1.4f * rpmFactor + static_cast<float>(bi) / 3.0f, 1.0f);
              const float ty = byTop + u * bh;
              DrawRectangleRec(Rectangle{bx - 2.0f * scale, ty - 1.0f * scale,
                                         4.0f * scale, 2.0f * scale},
                               (night ? ToRay(p.coffeeDark) : HexColor("#8c6f4e", ToRay(p.coffeeDark))));
            }
          }
          continue;
        }
        // Reciprocating horizontal linkage rods synchronized with primary turbine phase
        const float sign = (drawn.box.y < 200.0f) ? 1.0f : -1.0f;
        const float shift = reduced ? 0.0f : (sign * 2.4f * std::sin(turbinePhase1) * scale);
        part.box.x += shift;
      }
      if (drawn.cls == "mach-fin") {
        // Subtle heat-exchanger thermal shimmer from left (hot) to right (cool)
        const float wave = reduced ? 0.0f : (0.5f + 0.5f * std::sin(tSec * 2.8f - static_cast<float>(machFinIdx) * 0.55f));
        ++machFinIdx;
        PaintScenePart(part, origin);
        if (wave > 0.05f) {
          const Color hot = (night ? ToRay(p.amber) : HexColor("#c08a3e", ToRay(p.amber)));
          const Color cool = (night ? ToRay(p.sage) : HexColor("#7f9161", ToRay(p.sage)));
          const Color finTint = StageMix(hot, cool, static_cast<float>(machFinIdx) / 9.0f);
          DrawRectangleRec(Rectangle{origin.x + (drawn.box.x + 1.5f) * scale,
                                     origin.y + (drawn.box.y + 4.0f) * scale,
                                     std::max(1.0f, (drawn.box.w - 3.0f) * scale),
                                     std::max(1.0f, (drawn.box.h - 8.0f) * scale)},
                           Fade(finTint, 0.14f + 0.22f * wave));
        }
        continue;
      }
      if (drawn.cls.rfind("mach-lamp", 0) == 0) {
        const int idx = machLampIdx++;
        const float lx = origin.x + (drawn.box.x + drawn.box.w * 0.5f) * scale;
        const float ly = origin.y + (drawn.box.y + drawn.box.h * 0.5f) * scale;
        const float lr = drawn.box.w * 0.5f * scale;
        const Color greenCol = (night ? ToRay(p.sage) : HexColor("#7f9161", ToRay(p.sage)));
        const Color greenEdge = (night ? ToRay(p.sage) : HexColor("#6b7d50", ToRay(p.sage)));
        const Color ambCol = (night ? ToRay(p.amber) : HexColor("#c08a3e", ToRay(p.amber)));
        const Color warnCol = (night ? ToRay(p.terracotta) : HexColor("#b05a42", ToRay(p.terracotta)));
        const Color offCol = (night ? ToRay(p.panel) : HexColor("#efe8dc", ToRay(p.panel)));
        const Color offEdge = (night ? ToRay(p.border) : HexColor("#cbb89c", ToRay(p.border)));
        Color fill = offCol;
        Color rim = offEdge;
        float glow = 0.0f;
        const bool isAmb = drawn.cls.find("amb") != std::string::npos;
        const bool isOn = drawn.cls.find("-on") != std::string::npos;
        if (idx == 5) {
          // Telemetry bus heartbeat lamp
          const float beat = reduced ? 0.0f : std::sin(tSec * (underway ? 8.5f : 4.2f));
          if (beat > 0.15f) {
            fill = underway ? ambCol : greenCol;
            rim = greenEdge;
            glow = beat * 0.55f;
          }
        } else if (isAmb || (condRatio < 0.50f && (idx == 3 || idx == 6))) {
          const float blink = reduced ? 0.85f : (0.5f + 0.5f * std::sin(tSec * 4.8f + static_cast<float>(idx) * 1.4f));
          const Color base = condRatio < 0.30f ? warnCol : ambCol;
          fill = StageMix(offCol, base, 0.25f + 0.75f * blink);
          rim = base;
          glow = blink * 0.5f;
        } else if (isOn) {
          const float shimmer = reduced ? 1.0f : (0.72f + 0.28f * std::sin(tSec * 2.6f + static_cast<float>(idx) * 1.7f));
          fill = StageMix(offCol, greenCol, shimmer);
          rim = greenEdge;
          glow = shimmer * 0.35f;
        }
        if (glow > 0.05f) {
          DrawCircleV(Vector2{lx, ly}, lr * 1.75f, Fade(fill, glow * 0.55f));
        }
        DrawCircleV(Vector2{lx, ly}, lr, fill);
        DrawCircleLinesV(Vector2{lx, ly}, lr, rim);
        continue;
      }
      if (drawn.cls == "mach-cyl") {
        const int k = machCylIdx++;
        const float bottomY = 402.0f;
        const float minH = 16.0f;
        const float maxH = 44.0f;
        const float phase = reduced
                                ? (k % 2 ? 0.55f : 1.0f)
                                : (0.5f + 0.5f * std::sin(tSec * (3.2f * std::max(0.25f, rpmFactor)) -
                                                          static_cast<float>(k) * 0.58f));
        const float cyH = minH + (maxH - minH) * phase;
        const float cx = origin.x + drawn.box.x * scale;
        const float cy = origin.y + (bottomY - cyH) * scale;
        const float cw = drawn.box.w * scale;
        const float ch = cyH * scale;
        // Faint cylinder guide track
        DrawRectangleLinesEx(Rectangle{cx, origin.y + (bottomY - maxH) * scale, cw, maxH * scale},
                             std::max(1.0f, scale), Fade((night ? ToRay(p.border) : HexColor("#ddd2bf", ToRay(p.border))), 0.75f));
        const Color baseFill = (night ? ToRay(p.panel) : HexColor("#efe8dc", ToRay(p.panel)));
        const Color hotFill = condRatio < 0.50f ? (night ? ToRay(p.terracotta) : HexColor("#b05a42", ToRay(p.terracotta)))
                                                : (night ? ToRay(p.amber) : HexColor("#c08a3e", ToRay(p.amber)));
        const Color cylFill = StageMix(baseFill, hotFill, phase * 0.28f);
        DrawRectangleRec(Rectangle{cx, cy, cw, ch}, cylFill);
        DrawRectangleLinesEx(Rectangle{cx, cy, cw, ch}, std::max(1.0f, scale),
                             (night ? ToRay(p.borderDark) : HexColor("#cbb89c", ToRay(p.borderDark))));
        DrawRectangleRec(Rectangle{cx + scale, cy + scale, std::max(1.0f, cw - 2.0f * scale),
                                   std::max(1.0f, 3.0f * scale)},
                         StageMix((night ? ToRay(p.coffeeDark) : HexColor("#8c6f4e", ToRay(p.coffeeDark))), hotFill, phase * 0.45f));
        continue;
      }
    }

    // --- Priority 3: O2 Filter Room dynamic stage modulation ---
    if (o2Stage) {
      if (drawn.cls == "o2-louvre") {
        const int li = o2LouvreIdx++;
        const float wave = reduced ? 0.35f : (0.5f + 0.5f * std::sin(tSec * (o2Running ? 4.4f : 1.5f) - static_cast<float>(li) * 0.55f));
        const float lx = origin.x + drawn.box.x * scale;
        const float ly = origin.y + drawn.box.y * scale;
        const float lw = drawn.box.w * scale;
        const float lh = drawn.box.h * scale;
        const Color slatCol = StageMix((night ? ToRay(p.borderDark) : HexColor("#cbb89c", ToRay(p.borderDark))),
                                       o2Running ? (night ? ToRay(p.sage) : HexColor("#7f9161", ToRay(p.sage)))
                                                 : (night ? ToRay(p.coffee) : HexColor("#8c6f4e", ToRay(p.coffee))),
                                       wave * (o2Running ? 0.55f : 0.25f));
        const float openW = std::max(2.0f, lw * (0.65f + 0.45f * wave));
        DrawRectangleRec(Rectangle{lx + (lw - openW) * 0.5f, ly, openW, lh}, slatCol);
        continue;
      }
      if (drawn.cls == "o2-streak") {
        const int si = o2StreakIdx++;
        const float minX = 24.0f;
        const float span = 550.0f - drawn.box.w;
        const float speed = o2Running ? (85.0f + static_cast<float>(si % 2) * 32.0f) : 22.0f;
        const float baseOff = drawn.box.x - minX;
        const float animX = reduced ? drawn.box.x
                                    : (minX + std::fmod(baseOff + tSec * speed, std::max(1.0f, span)));
        const float animY = reduced ? drawn.box.y
                                    : (78.0f + static_cast<float>(si) * 6.5f +
                                       1.8f * std::sin(tSec * 2.4f + static_cast<float>(si)));
        const Color streakCol = (night ? ToRay(p.sage) : HexColor("#cfdcc0", ToRay(p.sage)));
        DrawRectangleRec(Rectangle{origin.x + animX * scale, origin.y + animY * scale,
                                   drawn.box.w * scale, drawn.box.h * scale},
                         Fade(streakCol, 0.85f));
        continue;
      }
      if (drawn.cls == "o2-dust") {
        const int di = o2DustIdx++;
        const float dx = reduced ? 0.0f : (8.5f * std::sin(tSec * (0.75f + 0.12f * static_cast<float>(di % 4)) + static_cast<float>(di) * 1.9f));
        const float dy = reduced ? 0.0f : (3.5f * std::cos(tSec * (0.95f + 0.15f * static_cast<float>(di % 3)) + static_cast<float>(di) * 1.1f));
        part.box.x = std::clamp(drawn.box.x + dx, 24.0f, 572.0f);
        part.box.y = std::clamp(drawn.box.y + dy, 96.0f, 113.0f);
      }
      if (drawn.cls == "o2-flow") {
        const int fi = o2FlowIdx++;
        const float lx = origin.x + drawn.box.x * scale;
        const float ly = origin.y + drawn.box.y * scale;
        const float lw = drawn.box.w * scale;
        const float lh = drawn.box.h * scale;
        const Color baseBg = (night ? ToRay(p.panel3) : HexColor("#e4dbcb", ToRay(p.panel3)));
        const Color baseRim = (night ? ToRay(p.borderDark) : HexColor("#cbb89c", ToRay(p.borderDark)));
        const Color activeBg = (night ? ToRay(p.sage) : HexColor("#7f9161", ToRay(p.sage)));
        const Color standBg = (night ? ToRay(p.amber) : HexColor("#c08a3e", ToRay(p.amber)));
        float pulse = 0.0f;
        if (!reduced) {
          if (o2Running) {
            const float head = std::fmod(tSec * 5.2f, 8.0f);
            float d = std::fabs(head - static_cast<float>(fi));
            d = std::min(d, 8.0f - d);
            pulse = std::clamp(1.0f - d * 0.55f, 0.0f, 1.0f);
          } else if (o2Seated > 0) {
            pulse = 0.25f * (0.5f + 0.5f * std::sin(tSec * 2.0f - static_cast<float>(fi) * 0.65f));
          } else {
            pulse = 0.12f * (0.5f + 0.5f * std::sin(tSec * 1.3f - static_cast<float>(fi) * 0.5f));
          }
        }
        const Color fill = o2Running ? StageMix(baseBg, activeBg, 0.22f + 0.78f * pulse)
                                     : StageMix(baseBg, standBg, pulse);
        DrawRectangleRec(Rectangle{lx, ly, lw, lh}, fill);
        DrawRectangleLinesEx(Rectangle{lx, ly, lw, lh}, std::max(1.0f, scale),
                             o2Running && pulse > 0.45f ? activeBg : baseRim);
        continue;
      }
    }

    if (PaintMessStockIcon(part, origin, p, night)) continue;
    if (PaintScenePart(part, origin)) continue;
    const int x = static_cast<int>(origin.x + part.box.x);
    const int y = static_cast<int>(origin.y + part.box.y);
    const int w = std::max(1, static_cast<int>(part.box.w));
    const int h = std::max(1, static_cast<int>(part.box.h));
    if (!part.tone.empty()) {
      const Color tone = HexColor(part.tone, ToRay(p.muted));
      if (!part.text.empty()) {
        Text(part.text.c_str(), x, y, TypePx(8), tone);
      } else if (part.box.w > 0 && part.box.h > 0) {
        DrawRectangle(x, y, w, h, tone);
      }
      continue;
    }
    if (part.cls.find("pipe") != std::string::npos ||
        part.cls.find("duct") != std::string::npos ||
        part.cls.find("counter") != std::string::npos) {
      DrawRectangle(x, y, w, h, ToRay(p.panel3));
      DrawRectangleLines(x, y, w, h, ToRay(p.border));
    } else if (part.cls.find("ring") != std::string::npos ||
               part.cls.find("wheel") != std::string::npos ||
               part.cls.find("dial") != std::string::npos) {
      DrawCircle(x + w / 2, y + h / 2, static_cast<float>(std::max(w, h)) / 2.0f,
                 ToRay(p.panel2));
      DrawCircleLines(x + w / 2, y + h / 2,
                      static_cast<float>(std::max(w, h)) / 2.0f, ToRay(p.coffee));
    } else if (part.cls.find("fill") != std::string::npos) {
      DrawRectangle(x, y, w, h, ToRay(p.sage));
    } else if (part.cls.find("track") != std::string::npos ||
               part.cls.find("box") != std::string::npos ||
               part.cls.find("slot") != std::string::npos ||
               part.cls.find("plate") != std::string::npos ||
               part.cls.find("rack") != std::string::npos) {
      DrawRectangle(x, y, w, h, ToRay(p.panel));
      DrawRectangleLines(x, y, w, h, ToRay(p.borderDark));
    } else {
      DrawRectangle(x, y, w, h, ToRay(p.coffeeLight));
    }
    if (!part.text.empty() && part.text[0] != 'm') {
      Text(part.text.c_str(), x + 2, y + 1, TypePx(8) , ToRay(p.muted));
    }
  }

  if (bridgeStage) {
    const auto V = [&](float x, float y) {
      return Vector2{origin.x + x * scale, origin.y + y * scale};
    };
    const float pen = std::max(1.0f, scale);
    const Color paper = darkHousing ? ToRay(p.panel) : Color{251, 250, 247, 255};
    const Color ink = darkHousing ? ToRay(p.ink) : Color{104, 94, 80, 255};
    const Color rule = darkHousing ? ToRay(p.border) : Color{190, 182, 168, 255};
    const Color glass{39, 40, 41, 255};
    const auto line = [&](float x, float y, float u, float v, Color col) {
      DrawLineEx(V(x, y), V(u, v), pen, col);
    };
    const auto plate = [&](float x, float y, float w, float h, Color fill) {
      PaintChamferBoxRay({V(x,y).x, V(x,y).y, w * scale, h * scale},
                         5 * scale, fill, rule, ink);
    };
    plate(10, 10, 940, 352, paper);
    plate(77, 26, 806, 276, paper);
    plate(85, 34, 790, 260, glass);
    bridgeBlockerHole_ = {V(10.0f, 10.0f).x - 1.5f * scale,
                          V(10.0f, 10.0f).y - contentScroll_ - 1.5f * scale,
                          943.0f * scale, 355.0f * scale};

    if (!sim.LandedOnPlanet()) {
      bridgeDispatch_ = false;
      planetSurface_.planetId.clear();
    }
    planetHitCount_ = 0;

    const float targetOpen = bridgeDispatch_ ? 1.0f : 0.0f;
    if (sim.State().reducedMotion) {
      bridgeDispatchOpen_ = targetOpen;
    } else {
      const float step = std::clamp(GetFrameTime(), 0.0f, 0.05f) * 3.6f;
      if (bridgeDispatchOpen_ < targetOpen) {
        bridgeDispatchOpen_ = std::min(targetOpen, bridgeDispatchOpen_ + step);
      } else if (bridgeDispatchOpen_ > targetOpen) {
        bridgeDispatchOpen_ = std::max(targetOpen, bridgeDispatchOpen_ - step);
      }
    }

    if (bridgeDispatchOpen_ < 0.999f) {
    const Catalog& catalog = sim.World();
    const Task* trip = sim.FindOngoing("travel");
    const Body* dest = trip ? sim.FindBodyById(trip->target) : nullptr;
    const bool sector = dest && dest->type == "system";
    NavPoint eye = TravelMarker(sim);
    if (sector) {
      const auto start = sim.Map().flightOrigin.at("sector");
      const double part = OngoingPercent(*trip) / 100.0;
      eye = {start.first + (dest->x - start.first) * part,
             start.second + (dest->y - start.second) * part};
    }
    const auto& local = catalog.BodiesInSystem(sim.CurrentSystemId());
    const Body* forward = dest;
    if (!forward && sim.State().navTarget) {
      const Body* plotted = sim.FindBodyById(*sim.State().navTarget);
      if (plotted && plotted->type != "system" && catalog.BodySystem(*plotted) == sim.CurrentSystemId()) forward = plotted;
    }
    if (!forward || (forward->x == eye.x && forward->y == eye.y)) {
      double nearest = HUGE_VAL;
      for (auto index : local) {
        const Body& body = catalog.bodies[index];
        const double d = std::hypot(body.x - eye.x, body.y - eye.y);
        if (body.type == "star" && d > 0 && d < nearest) { nearest = d; forward = &body; }
      }
    }
    const double heading = forward ? std::atan2(forward->y - eye.y, forward->x - eye.x) : 0.0;
    const double yaw = heading + bridgeLook_ * 2 * PI;
    const float left = V(91, 0).x, top = std::max(layout_.content.y, V(0, 38).y - contentScroll_);
    const float bottom = std::min(layout_.content.Bottom(), V(0, 290).y - contentScroll_);
    BeginScissorMode(static_cast<int>(left), static_cast<int>(top), static_cast<int>(778 * scale),
                     static_cast<int>(std::max(0.0f, bottom - top)));
    const Vector2 pivot = V(480, 164);
    float sunGlassX = 0.0f, sunGlassY = 0.0f, sunGlassR = 0.0f;
    bool sunOnGlass = false;
    rlPushMatrix();
    rlTranslatef(pivot.x, pivot.y, 0);
    rlRotatef(std::fmod(tSec * 0.30f, 360.0f), 0, 0, 1);
    rlTranslatef(-pivot.x, -pivot.y, 0);
    // A fixed procedural sky, and near particles passing the bow only in flight.
    for (int i = 1; i <= 360; ++i) {
      const double az = i * 2.39996323 - yaw;
      const double z = std::cos(az);
      if (z < 0.60) continue;
      const float x = 480 + 330 * std::sin(az) / z;
      const float y = 164 + ((i*i*37+i*23)%827 - 413) / z;
      DrawCircleV(V(x,y), std::max(0.7f, (i%7 ? 0.8f : 1.4f)*scale), Fade(WHITE, 0.4f + 0.08f*(i%6)));
      if (i % 19 == 0) {
        const float tick = 2.6f * scale;
        line(x - tick, y, x + tick, y, Fade(WHITE, 0.14f));
        line(x, y - tick, x, y + tick, Fade(WHITE, 0.14f));
      } else if (i % 47 == 0) {
        DrawCircleLinesV(V(x, y), 3.2f * scale, Fade(WHITE, 0.10f));
      }
    }
    if (trip && !reduced) {
      const float look = bridgeLook_ * 2 * PI;
      for (int i = 1; i <= 72; ++i) {
        const float z = 1.0f - std::fmod(i * 0.618034f + tSec * 0.38f, 1.0f);
        const float dx = ((i*97)%201 - 100)*0.012f, dy = ((i*61)%137 - 68)*0.006f;
        const float depth = z*std::cos(look) + dx*std::sin(look);
        const float trail = 0.052f;
        const float tail = depth + trail*std::cos(look);
        if (depth < 0.08f || tail < 0.08f) continue;
        const float side = dx*std::cos(look) - z*std::sin(look);
        line(480+330*side/depth, 164+330*dy/depth,
             480+330*(side-trail*std::sin(look))/tail, 164+330*dy/tail, rule);
      }
    }
    const auto drawBody = [&](const Body& body) {
      const double dx = body.x-eye.x, dy = body.y-eye.y;
      const double depth = dx*std::cos(yaw) + dy*std::sin(yaw);
      if (depth <= 0.01) return;
      const float x = 480 + 330*(dy*std::cos(yaw) - dx*std::sin(yaw))/depth;
      if (x < 40 || x > 920) return;
      const float y = 154 + 1100/(30+depth);
      const bool sun = body.type == "star" || body.type == "system";
      const float r = std::clamp(static_cast<float>((sun ? 2800 : body.type == "moon" ? 650 : 1500)/(28+depth)), 2.0f, 40.0f);
      const float haze = static_cast<float>(depth / (depth + 200.0));
      const float opacity = 0.70f - 0.45f*haze;
      if (body.type == "station" || body.type == "site") {
        DrawRectangleV(V(x-r,y-r*0.35f), {2*r*scale,0.7f*r*scale}, Fade(rule, opacity));
        line(x,y-r,x,y+r,Fade(WHITE, opacity));
      } else if (body.type == "belt") {
        for (int i=0; i<11; ++i) DrawCircleV(V(x+(i-5)*r*0.35f,y+((i*7)%5-2)*r*0.18f), pen, Fade(rule, opacity));
      } else {
        const float visibility = body.type == "planet" || body.type == "moon"
            ? static_cast<float>(1.0 / (1.0 + (dx*dx + dy*dy) / (150.0*150.0))) : 1.0f;
        const Color light = sun ? Color{244,229,198,255} : Color{164,161,152,255};
        const Vector2 center = V(x,y);
        DrawCircleGradient(center,
                           (sun ? 4.8f*r : r+2.5f*haze)*scale,
                           Fade(light, sun ? 0.84f : 0.32f * visibility), Fade(light, 0));
        DrawCircleV(center, (r-0.7f*haze)*scale, Fade(light, opacity * visibility));
        if (sun) {
          DrawCircleLinesV(V(x,y),(r+2)*scale,Fade(rule, opacity*0.6f));
          const unsigned marks = static_cast<unsigned>(static_cast<int>(body.x)) ^
                                 static_cast<unsigned>(static_cast<int>(body.y));
          for (int spot = 0; spot < 5; ++spot) {
            const float a = static_cast<float>((marks * (spot + 3u)) % 628u) * 0.01f;
            const float d = r * (0.10f + 0.50f * static_cast<float>((marks >> (spot * 2)) & 3u) / 3.0f);
            const float sr = r * (0.07f + 0.09f * static_cast<float>((marks >> spot) & 1u));
            DrawCircleV(V(x + d*std::cos(a), y + d*std::sin(a)), sr*scale, Fade(ink, 0.50f * opacity));
          }
          for (int ray = 0; ray < 6; ++ray) {
            const float a = static_cast<float>(marks % 360u) * DEG2RAD +
                            ray * (PI / 3.0f) + tSec * 0.05f;
            const float reach = r * (1.9f + 0.35f * (ray % 3));
            line(x + r*0.9f*std::cos(a), y + r*0.9f*std::sin(a),
                 x + reach*std::cos(a), y + reach*std::sin(a),
                 Fade(light, (ray % 2 ? 0.07f : 0.13f) * opacity));
          }
          const float roll = std::fmod(tSec * 0.30f, 360.0f) * DEG2RAD;
          const float ox = V(x,y).x - pivot.x, oy = V(x,y).y - pivot.y;
          sunGlassX = pivot.x + ox*std::cos(roll) - oy*std::sin(roll);
          sunGlassY = pivot.y + ox*std::sin(roll) + oy*std::cos(roll);
          sunGlassR = r*scale;
          sunOnGlass = true;
        }
        if (body.subtype == "gas") line(x-r*1.4f,y+r*0.35f,x+r*1.4f,y-r*0.35f,Fade(rule, opacity * visibility));
      }
    };
    if (sector) {
      for (const Body& body : catalog.systems) if (body.region == dest->region || body.region == sim.CurrentSystem()->region) drawBody(body);
    } else {
      for (auto index : local) drawBody(catalog.bodies[index]);
    }
    rlPopMatrix();
    debrisHits_ = {};
    const int click = static_cast<int>(sim.ActionCount());
    const int cycle = click / 10;
    if (trip && click % 10 >= 5) {
      // Reconstruct this window from the voyage seed, never roll on a render frame.
      std::minstd_rand dice(static_cast<unsigned>(catalog.chartSettings.seed) ^
                            (static_cast<unsigned>(cycle) * 2654435761u));
      const int count = 1 + dice() % 2;
      for (int slot = 0; slot < count; ++slot) {
        const int appears = 5 + dice() % 5;
        const int reward = dice() % 10;
        const int coins = 5 + dice() % 16;
        const float phase = (dice() % 628) * 0.01f;
        const float bearing = (static_cast<int>(dice() % 37) - 18) * DEG2RAD;
        if (click % 10 < appears || sim.State().debrisClaimedCycle[slot] == cycle) continue;
        const float az = bearing + 0.035f * std::sin(phase + tSec * 0.12f) - bridgeLook_ * 2 * PI;
        if (std::cos(az) < 0.60f) continue;
        const float dx = 330 * std::tan(az);
        const float dy = (36 * std::sin(phase) + 8 * std::sin(phase + tSec * 0.16f)) / std::cos(az);
        const float roll = std::fmod(tSec * 0.30f, 360.0f) * DEG2RAD;
        const float x = 480 + dx * std::cos(roll) - dy * std::sin(roll);
        const float y = 164 + dx * std::sin(roll) + dy * std::cos(roll);
        if (x < 100 || x > 860 || y < 48 || y > 280) continue;
        const Vector2 center = V(x, y);
        debrisHits_[slot] = {{center.x-9*scale, center.y-9*scale, 18*scale, 18*scale}, click, reward, coins};
        rlPushMatrix();
        rlTranslatef(center.x, center.y, 0);
        rlRotatef(phase * RAD2DEG + tSec * 6, 0, 0, 1);
        const Rectangle crate{-5*scale,-4*scale,10*scale,8*scale};
        DrawRectangleRec(crate, ink);
        DrawRectangleLinesEx(crate, pen, rule);
        DrawLineEx({-2*scale,-3*scale},{-2*scale,3*scale},pen,rule);
        DrawLineEx({2*scale,-3*scale},{2*scale,3*scale},pen,rule);
        rlPopMatrix();
      }
    }
    // The glass answers the sky: a soft sun smear, a sheen across the pane, the sill.
    if (sunOnGlass) {
      DrawRectangleGradientV(static_cast<int>(sunGlassX - sunGlassR),
                             static_cast<int>(sunGlassY + sunGlassR),
                             static_cast<int>(sunGlassR * 2.0f),
                             static_cast<int>(78.0f * scale),
                             Fade(paper, 0.18f), Fade(paper, 0));
    }
    rlPushMatrix();
    rlTranslatef(pivot.x, pivot.y, 0);
    rlRotatef(-22.0f, 0, 0, 1);
    rlTranslatef(-pivot.x, -pivot.y, 0);
    DrawRectangleGradientH(static_cast<int>(V(610, 20).x), static_cast<int>(V(0, 20).y),
                           static_cast<int>(64.0f * scale), static_cast<int>(300.0f * scale),
                           Fade(paper, 0), Fade(paper, 0.035f));
    rlPopMatrix();
    DrawRectangleGradientV(static_cast<int>(left), static_cast<int>(V(0,254).y),
                           static_cast<int>(778*scale), static_cast<int>(45*scale),
                           Fade(paper, 0), Fade(paper, 0.26f));
    for (float x : {109.0f, 851.0f}) {
      for (int k = 0; k < 5; ++k) line(x-6, 275+2*k, x+6, 275+2*k, Fade(rule, 0.32f));
    }
    EndScissorMode();
    }

    if (bridgeDispatchOpen_ > 0.001f) {
      if (bridgeDispatch_ && !titleActive_ && !floating_.HasConfirmation() &&
          !ImGui::GetIO().WantTextInput) {
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) { StepPlanetSurface(sim, 1); }
        else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) { StepPlanetSurface(sim, 2); }
        else if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) { StepPlanetSurface(sim, 3); }
        else if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) { StepPlanetSurface(sim, 4); }
        else if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_X)) { StepPlanetSurface(sim, 7); }
      }
      EnsurePlanetSurface(sim);
      RenderPlanetSurfacePixels(sim);

      const float ease =
          bridgeDispatchOpen_ * bridgeDispatchOpen_ * (3.0f - 2.0f * bridgeDispatchOpen_);
      const float halfH = std::max(2.0f, 126.0f * ease);
      const float openTop = 164.0f - halfH;
      const float openBot = 164.0f + halfH;
      const float left = V(91, 0).x;
      const float top = std::max(layout_.content.y, V(0, std::max(38.0f, openTop)).y - contentScroll_);
      const float bottom =
          std::min(layout_.content.Bottom(), V(0, std::min(290.0f, openBot)).y - contentScroll_);
      BeginScissorMode(static_cast<int>(left), static_cast<int>(top), static_cast<int>(778 * scale),
                       static_cast<int>(std::max(0.0f, bottom - top)));
      DrawRectangle(static_cast<int>(V(85, 34).x), static_cast<int>(V(85, 34).y),
                    static_cast<int>(790 * scale), static_cast<int>(260 * scale),
                    Color{12, 13, 16, 255});
      for (int s = 0; s < 26; ++s) {
        const float sx = 96.0f + static_cast<float>((s * 97) % 165) + (s % 2 ? 590.0f : 0.0f);
        const float sy = 44.0f + static_cast<float>((s * 61) % 236);
        DrawRectangle(static_cast<int>(V(sx, sy).x), static_cast<int>(V(sx, sy).y),
                      std::max(1, static_cast<int>(1.5f * scale)),
                      std::max(1, static_cast<int>(1.5f * scale)), Fade(WHITE, 0.35f));
      }

      const float vx = 275.0f, vy = 42.0f, vw = 410.0f, vh = 244.0f;
      const float cxMid = vx + vw * 0.5f;
      const auto rectS = [&](float x, float y, float w, float h, Color col) {
        DrawRectangle(static_cast<int>(V(x, y).x), static_cast<int>(V(x, y).y),
                      std::max(1, static_cast<int>(std::ceil(w * scale))),
                      std::max(1, static_cast<int>(std::ceil(h * scale))), col);
      };
      if (planetTex_.id != 0) {
        const Rectangle src{0.0f, 0.0f, static_cast<float>(PlanetSurfaceState::kTexW),
                            static_cast<float>(PlanetSurfaceState::kTexH)};
        const Rectangle dst{V(vx, vy).x, V(vx, vy).y, vw * scale, vh * scale};
        DrawTexturePro(planetTex_, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        if (planetScanlineTex_.id != 0) {
          const Rectangle scanSrc{0.0f, 0.0f, 1.0f, static_cast<float>(planetScanlineTex_.height)};
          DrawTexturePro(planetScanlineTex_, scanSrc, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        }
      }

      const Body* planet = sim.FindBodyById(sim.Map().here);
      static const std::string kEmptySubtype;
      const std::string& subtype = planet ? planet->subtype : kEmptySubtype;
      const bool lavaWorld = planet != nullptr &&
          (subtype == "lava" ||
           planet->klass.find("Lava") != std::string::npos ||
           planet->klass.find("Volcanic") != std::string::npos ||
           planet->klass.find("lava") != std::string::npos);
      const bool iceWorld = !lavaWorld && subtype == "ice";
      const bool oceanWorld = !lavaWorld && subtype == "ocean";
      const bool rockWorld = !lavaWorld && !iceWorld && !oceanWorld && planet != nullptr &&
          (subtype == "rock" ||
           planet->klass.find("Rugged") != std::string::npos ||
           planet->klass.find("Barren") != std::string::npos ||
           planet->klass.find("Crater") != std::string::npos);

      const auto addHit = [&](const Area& box, int kind, int arg) {
        if (planetHitCount_ < static_cast<int>(planetHits_.size())) {
          planetHits_[planetHitCount_++] = PlanetClickHit{box, kind, arg};
        }
      };
      const auto uvToArea = [&](const Area& uv) -> Area {
        return Area{V(vx + uv.x * vw, vy + uv.y * vh).x,
                    V(vx + uv.x * vw, vy + uv.y * vh).y,
                    uv.w * vw * scale, uv.h * vh * scale};
      };

      const bool inMine = (planetSurface_.inMine != 0u);

      if (planetSurface_.frontKind != 0 && planetSurface_.frontUv.w > 0.0f) {
        const Area frontBox = uvToArea(planetSurface_.frontUv);
        const bool hov = layout_.content.Contains(mouseX_, mouseY_) &&
                         frontBox.Contains(mouseX_, mouseY_ + contentScroll_);
        const bool isShip = (planetSurface_.frontKind == 6);
        const bool isPortal = (planetSurface_.frontKind == 8);
        if (hov) {
          DrawRectangleLinesEx({frontBox.x, frontBox.y, frontBox.w, frontBox.h}, pen,
                               (isShip || isPortal) ? Color{255, 220, 110, 255}
                                                    : Color{255, 240, 170, 255});
        }
        const int tagPx = std::max(9, static_cast<int>(std::lround(10.0f * scale)));
        const char* tag = isShip ? "SHIP" : isPortal ? (inMine ? "EXIT" : "MINE") : "ORE";
        const float tw = TextWidth(tag, tagPx, true);
        const int tx = static_cast<int>(frontBox.x + (frontBox.w - tw) * 0.5f);
        const int ty = static_cast<int>(frontBox.y - 13.0f * scale);
        DrawRectangle(tx - 3, ty - 1, static_cast<int>(std::ceil(tw)) + 6, tagPx + 2,
                      Color{10, 12, 16, 224});
        const Color tagCol = isShip
            ? (hov ? Color{255, 232, 140, 255} : Color{180, 236, 246, 255})
            : isPortal
                ? (hov ? Color{255, 236, 152, 255}
                       : (inMine ? Color{176, 238, 248, 255} : Color{252, 188, 96, 255}))
                : (hov ? Color{255, 236, 148, 255} : Color{248, 208, 104, 255});
        Text(tag, tx, ty, tagPx, tagCol, true);
        addHit(frontBox, planetSurface_.frontKind, planetSurface_.frontArg);
      }

      if (planetSurface_.bumpKind != 0) {
        const char* bumpTxt = "";
        if (planetSurface_.bumpBack) {
          bumpTxt = (planetSurface_.bumpKind == 2) ? "BLOCKED - SHIP BEHIND"
                  : (planetSurface_.bumpKind == 3) ? "BLOCKED - ORE BEHIND"
                  : (planetSurface_.bumpKind == 4) ? (inMine ? "BLOCKED - WALL BEHIND"
                                                             : "BLOCKED - CLIFF BEHIND")
                                                   : "BLOCKED BEHIND";
        } else if (planetSurface_.bumpKind == 2) {
          bumpTxt = "BLOCKED - SHIP AHEAD";
        } else if (planetSurface_.bumpKind == 3) {
          bumpTxt = "BLOCKED - ORE AHEAD";
        } else if (planetSurface_.bumpKind == 4) {
          bumpTxt = inMine ? "BLOCKED - WALL AHEAD" : "BLOCKED - CLIFF AHEAD";
        } else {
          bumpTxt = iceWorld   ? "BLOCKED - ICE AHEAD"
                  : oceanWorld ? "BLOCKED - REEF AHEAD"
                  : lavaWorld  ? "BLOCKED - VENT AHEAD"
                  : rockWorld  ? "BLOCKED - ROCK AHEAD"
                               : "BLOCKED - TREES AHEAD";
        }
        const int bumpPx = std::max(9, static_cast<int>(std::lround(9.5f * scale)));
        const float tw = TextWidth(bumpTxt, bumpPx, true);
        const float padX = 8.0f * scale;
        const float bh = static_cast<float>(bumpPx) + 6.0f * scale;
        const float bw = tw + 2.0f * padX;
        const float bx = V(cxMid, 0.0f).x - bw * 0.5f;
        const float by = V(0.0f, vy + vh - 6.0f).y - bh;
        DrawRectangleRec({bx, by, bw, bh}, Color{10, 12, 16, 234});
        DrawRectangleLinesEx({bx, by, bw, bh}, pen, Color{236, 164, 92, 255});
        Text(bumpTxt, static_cast<int>(bx + padX),
             static_cast<int>(by + (bh - static_cast<float>(bumpPx)) * 0.5f),
             bumpPx, Color{255, 214, 152, 255}, true);
      }

      DrawRectangleLinesEx({V(vx, vy).x, V(vx, vy).y, vw * scale, vh * scale},
                           pen, Color{196, 198, 194, 255});

      const Color mmObstFill = inMine     ? Color{92, 86, 80, 255}
                             : iceWorld   ? Color{84, 138, 250, 255}
                             : oceanWorld ? Color{42, 212, 148, 255}
                             : lavaWorld  ? Color{250, 96, 52, 255}
                             : rockWorld  ? Color{208, 112, 58, 255}
                                          : Color{58, 192, 76, 255};
      const Color mmObstHi   = inMine     ? Color{148, 140, 132, 255}
                             : iceWorld   ? Color{208, 230, 255, 255}
                             : oceanWorld ? Color{176, 250, 220, 255}
                             : lavaWorld  ? Color{255, 196, 112, 255}
                             : rockWorld  ? Color{246, 186, 132, 255}
                                          : Color{164, 242, 152, 255};
      const Color mmBorderStud = inMine     ? Color{56, 54, 52, 255}
                               : iceWorld   ? Color{56, 78, 122, 255}
                               : oceanWorld ? Color{42, 92, 82, 255}
                               : lavaWorld  ? Color{92, 52, 46, 255}
                               : rockWorld  ? Color{88, 66, 52, 255}
                                            : Color{52, 82, 56, 255};
      const Color mmFloor = inMine ? Color{202, 206, 212, 255} : Color{236, 238, 230, 255};
      const Color mmWall{14, 17, 22, 255};
      const Color mmGrid{34, 38, 46, 255};
      const Color mmRoadFill{248, 190, 56, 255};
      const Color mmRoadCurb{28, 22, 14, 255};
      const Color mmShipFill{28, 220, 236, 255};

      constexpr int N = PlanetSurfaceState::kSize;
      constexpr int kView = PlanetSurfaceState::kViewSize;
      auto tileAt = [&](int tx, int ty) -> std::uint8_t {
        if (static_cast<unsigned>(tx) >= static_cast<unsigned>(N) ||
            static_cast<unsigned>(ty) >= static_cast<unsigned>(N)) {
          return 1u;
        }
        return planetSurface_.Tile(tx, ty);
      };
      auto isRoadCell = [&](int tx, int ty) -> bool {
        const std::uint8_t v = tileAt(tx, ty);
        return v == 3u || v == 2u;
      };

      // Edge-clamped 9x9 scrolling camera window over the 16x16 planet/mine grid
      const int camX = std::clamp(static_cast<int>(planetSurface_.px) - kView / 2, 0, N - kView);
      const int camY = std::clamp(static_cast<int>(planetSurface_.py) - kView / 2, 0, N - kView);

      // Left side inside glass: legendless high-contrast 9x9 PC-98 minimap (no ore hints)
      const int infoPx = std::max(9, static_cast<int>(std::lround(10.0f * scale)));
      const float mmCell = 14.0f;
      const float mmX = 118.0f;
      const float mmY = 98.0f;
      Text(inMine ? "MINE" : "MAP",
           static_cast<int>(V(mmX - 2.0f, 78.0f).x), static_cast<int>(V(mmX - 2.0f, 78.0f).y),
           infoPx, Color{240, 242, 236, 255}, true);

      int mmColX[kView + 1];
      int mmRowY[kView + 1];
      for (int i = 0; i <= kView; ++i) {
        mmColX[i] = static_cast<int>(std::lround(V(mmX + static_cast<float>(i) * mmCell, 0.0f).x));
        mmRowY[i] = static_cast<int>(std::lround(V(0.0f, mmY + static_cast<float>(i) * mmCell).y));
      }
      const int gx0 = mmColX[0];
      const int gy0 = mmRowY[0];
      const int gx1 = mmColX[kView];
      const int gy1 = mmRowY[kView];
      const int framePad = std::max(2, static_cast<int>(std::lround(2.5f * scale)));
      const int rimPx = std::max(1, static_cast<int>(std::lround(scale)));
      DrawRectangle(gx0 - framePad - rimPx, gy0 - framePad - rimPx,
                    (gx1 - gx0) + 2 * (framePad + rimPx), (gy1 - gy0) + 2 * (framePad + rimPx),
                    Color{232, 236, 228, 255});
      DrawRectangle(gx0 - framePad, gy0 - framePad,
                    (gx1 - gx0) + 2 * framePad, (gy1 - gy0) + 2 * framePad, mmWall);
      DrawRectangle(gx0, gy0, gx1 - gx0, gy1 - gy0, mmGrid);

      // Pass 1: high-contrast floor vs wall/obstacle tiles across 9x9 camera window
      for (int my = 0; my < kView; ++my) {
        const int wy = camY + my;
        const int cy0 = mmRowY[my];
        const int cy1 = mmRowY[my + 1] - 1;
        const int ch = std::max(2, cy1 - cy0);
        for (int mx = 0; mx < kView; ++mx) {
          const int wx = camX + mx;
          const int cx0 = mmColX[mx];
          const int cx1 = mmColX[mx + 1] - 1;
          const int cw = std::max(2, cx1 - cx0);
          const std::uint8_t t = tileAt(wx, wy);
          if (t == 1u) {
            DrawRectangle(cx0, cy0, cw, ch, mmWall);
            const bool isEdge = (wx == 0 || wx == N - 1 || wy == 0 || wy == N - 1);
            if (isEdge) {
              const int pad = std::max(2, cw / 3);
              if (cw > 2 * pad && ch > 2 * pad) {
                DrawRectangle(cx0 + pad, cy0 + pad, cw - 2 * pad, ch - 2 * pad, mmBorderStud);
              }
            } else {
              const bool isCliffObst = inMine ||
                  (wx >= 0 && wx < N && wy >= 0 && wy < N && (planetSurface_.Variant(wx, wy) & 4) != 0);
              const Color cFill = isCliffObst ? Color{122, 114, 102, 255} : mmObstFill;
              const Color cHi   = isCliffObst ? Color{186, 178, 164, 255} : mmObstHi;
              const int ins = std::max(2, cw / 5);
              const int iw = std::max(2, cw - 2 * ins);
              const int ih = std::max(2, ch - 2 * ins);
              DrawRectangle(cx0 + ins, cy0 + ins, iw, ih, cFill);
              DrawRectangle(cx0 + ins, cy0 + ins, std::max(1, iw / 2), std::max(1, ih / 3), cHi);
            }
          } else {
            DrawRectangle(cx0, cy0, cw, ch, mmFloor);
          }
        }
      }

      // Pass 2: connected dark-curbed golden trail ribbon across road tiles in 9x9 window
      for (int roadPass = 0; roadPass < 2; ++roadPass) {
        const Color rCol = (roadPass == 0) ? mmRoadCurb : mmRoadFill;
        for (int my = 0; my < kView; ++my) {
          const int wy = camY + my;
          const int cy0 = mmRowY[my];
          const int cy1 = mmRowY[my + 1] - 1;
          const int ch = std::max(2, cy1 - cy0);
          for (int mx = 0; mx < kView; ++mx) {
            const int wx = camX + mx;
            if (tileAt(wx, wy) != 3u) { continue; }
            const int cx0 = mmColX[mx];
            const int cx1 = mmColX[mx + 1] - 1;
            const int cw = std::max(2, cx1 - cx0);
            const int mxC = cx0 + cw / 2;
            const int myC = cy0 + ch / 2;
            const int curb = (roadPass == 0) ? std::max(1, rimPx) : 0;
            const int rw = std::max(2, cw * 24 / 100) + curb;
            const int rh = std::max(2, ch * 24 / 100) + curb;
            DrawRectangle(mxC - rw, myC - rh, 2 * rw, 2 * rh, rCol);
            if (isRoadCell(wx, wy - 1)) {
              const int yTop = std::max(gy0, cy0 - 1);
              DrawRectangle(mxC - rw, yTop, 2 * rw, (myC + rh) - yTop, rCol);
            }
            if (isRoadCell(wx, wy + 1)) {
              const int yBot = std::min(gy1, cy1 + 1);
              DrawRectangle(mxC - rw, myC - rh, 2 * rw, yBot - (myC - rh), rCol);
            }
            if (isRoadCell(wx - 1, wy)) {
              const int xLft = std::max(gx0, cx0 - 1);
              DrawRectangle(xLft, myC - rh, (mxC + rw) - xLft, 2 * rh, rCol);
            }
            if (isRoadCell(wx + 1, wy)) {
              const int xRgt = std::min(gx1, cx1 + 1);
              DrawRectangle(mxC - rw, myC - rh, xRgt - (mxC - rw), 2 * rh, rCol);
            }
          }
        }
      }

      // Pass 3: ship/portal markers (strictly inside 9x9 window) and player heading arrow
      {
        if (!inMine) {
          const int sLocalX = static_cast<int>(planetSurface_.shipX) - camX;
          const int sLocalY = static_cast<int>(planetSurface_.shipY) - camY;
          if (sLocalX >= 0 && sLocalX < kView && sLocalY >= 0 && sLocalY < kView) {
            const int sx0 = mmColX[sLocalX];
            const int sx1 = mmColX[sLocalX + 1] - 1;
            const int sy0 = mmRowY[sLocalY];
            const int sy1 = mmRowY[sLocalY + 1] - 1;
            const int sw = std::max(4, sx1 - sx0);
            const int sh = std::max(4, sy1 - sy0);
            const int sRim = std::max(1, rimPx);
            const int sPad = std::max(2, sw / 5);
            DrawRectangle(sx0 + sRim, sy0 + sRim, sw - 2 * sRim, sh - 2 * sRim, mmWall);
            DrawRectangle(sx0 + sPad, sy0 + sPad, std::max(2, sw - 2 * sPad), std::max(2, sh - 2 * sPad), mmShipFill);
            DrawRectangle(sx0 + sw / 2 - sRim, sy0 + sh / 2 - sRim, 2 * sRim, 2 * sRim, Color{252, 254, 255, 255});
          }
        }

        const int mLocalX = static_cast<int>(planetSurface_.portalX) - camX;
        const int mLocalY = static_cast<int>(planetSurface_.portalY) - camY;
        if (mLocalX >= 0 && mLocalX < kView && mLocalY >= 0 && mLocalY < kView) {
          const int mx0 = mmColX[mLocalX];
          const int mx1 = mmColX[mLocalX + 1] - 1;
          const int my0 = mmRowY[mLocalY];
          const int my1 = mmRowY[mLocalY + 1] - 1;
          const int mw = std::max(4, mx1 - mx0);
          const int mh = std::max(4, my1 - my0);
          const int mRim = std::max(1, rimPx);
          const int mPad = std::max(2, mw / 5);
          DrawRectangle(mx0 + mRim, my0 + mRim, mw - 2 * mRim, mh - 2 * mRim, mmWall);
          DrawRectangle(mx0 + mPad, my0 + mPad, std::max(2, mw - 2 * mPad), std::max(2, mh - 2 * mPad),
                        inMine ? Color{144, 228, 248, 255} : Color{242, 162, 72, 255});
          DrawRectangle(mx0 + mw / 2 - mRim, my0 + mh / 2, 2 * mRim, std::max(2, mh / 2 - mPad + mRim),
                        inMine ? Color{248, 252, 255, 255} : mmWall);
        }

        const int pLocalX = static_cast<int>(planetSurface_.px) - camX;
        const int pLocalY = static_cast<int>(planetSurface_.py) - camY;
        const int px0 = mmColX[pLocalX];
        const int px1 = mmColX[pLocalX + 1] - 1;
        const int py0 = mmRowY[pLocalY];
        const int py1 = mmRowY[pLocalY + 1] - 1;
        const float pxC = (static_cast<float>(px0 + px1)) * 0.5f;
        const float pyC = (static_cast<float>(py0 + py1)) * 0.5f;
        const float cellPx = static_cast<float>(std::max(4, px1 - px0));
        auto drawDirTri = [&](float tip, float base, float wing, Color col) {
          if (planetSurface_.dir == 0) {
            DrawTriangle(Vector2{pxC, pyC - tip}, Vector2{pxC - wing, pyC + base}, Vector2{pxC + wing, pyC + base}, col);
          } else if (planetSurface_.dir == 1) {
            DrawTriangle(Vector2{pxC + tip, pyC}, Vector2{pxC - base, pyC - wing}, Vector2{pxC - base, pyC + wing}, col);
          } else if (planetSurface_.dir == 2) {
            DrawTriangle(Vector2{pxC - wing, pyC - base}, Vector2{pxC, pyC + tip}, Vector2{pxC + wing, pyC - base}, col);
          } else {
            DrawTriangle(Vector2{pxC - tip, pyC}, Vector2{pxC + base, pyC + wing}, Vector2{pxC + base, pyC - wing}, col);
          }
        };
        drawDirTri(cellPx * 0.48f, cellPx * 0.38f, cellPx * 0.42f, mmWall);
        drawDirTri(cellPx * 0.27f, cellPx * 0.22f, cellPx * 0.23f, Color{255, 74, 58, 255});
      }

      // Right side inside glass: tiny heading/ore/blocked readout + 4-way arrow pad
      static const char* kDirNames[4] = {"N", "E", "S", "W"};
      static constexpr int kFwdDx[4] = {0, 1, 0, -1};
      static constexpr int kFwdDy[4] = {-1, 0, 1, 0};
      int oreLeft = 0;
      const int fx = planetSurface_.px + kFwdDx[planetSurface_.dir & 3];
      const int fy = planetSurface_.py + kFwdDy[planetSurface_.dir & 3];
      const int pOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
      const bool frontIsOpenPortal =
          (fx == static_cast<int>(planetSurface_.portalX) &&
           fy == static_cast<int>(planetSurface_.portalY) &&
           static_cast<int>(planetSurface_.px) == static_cast<int>(planetSurface_.portalX) + kFwdDx[pOut] &&
           static_cast<int>(planetSurface_.py) == static_cast<int>(planetSurface_.portalY) + kFwdDy[pOut]);
      std::uint8_t frontBlk = 0;
      if (!frontIsOpenPortal) {
        if (tileAt(fx, fy) == 1) {
          const bool cliffAhead = inMine ||
              (fx >= 0 && fx < N && fy >= 0 && fy < N && (planetSurface_.Variant(fx, fy) & 4) != 0);
          frontBlk = cliffAhead ? 4u : 1u;
        } else if (!inMine && fx == planetSurface_.shipX && fy == planetSurface_.shipY) {
          frontBlk = 2u;
        }
      }
      if (planet != nullptr) {
        const unsigned activeOres = planetSurface_.renderOreMask;
        for (int i = 0; i < planetSurface_.oreCount; ++i) {
          if ((activeOres & (1u << i)) != 0u) {
            ++oreLeft;
            if (((planetSurface_.oreLayerMask >> i) & 1u) == planetSurface_.inMine &&
                planetSurface_.oreX[i] == fx && planetSurface_.oreY[i] == fy) {
              frontBlk = 3u;
            }
          }
        }
      }
      const char* pTitle = planet ? planet->name.c_str() : "Surface";
      Text(pTitle, static_cast<int>(V(700, 48).x), static_cast<int>(V(700, 48).y),
           infoPx, Color{220, 224, 220, 255}, true);
      char posBuf[32];
      std::snprintf(posBuf, sizeof(posBuf), "%s %s (%u,%u)",
                    inMine ? "MINE" : "DIR",
                    kDirNames[planetSurface_.dir & 3u],
                    static_cast<unsigned>(planetSurface_.px),
                    static_cast<unsigned>(planetSurface_.py));
      Text(posBuf, static_cast<int>(V(700, 66).x), static_cast<int>(V(700, 66).y),
           infoPx, Color{168, 176, 172, 255});
      char oreBuf[16];
      std::snprintf(oreBuf, sizeof(oreBuf), "ORE %d", oreLeft);
      Text(oreBuf, static_cast<int>(V(700, 84).x), static_cast<int>(V(700, 84).y),
           infoPx, oreLeft > 0 ? Color{236, 184, 82, 255} : Color{130, 140, 134, 255}, true);
      if (frontBlk != 0 || planetSurface_.bumpKind != 0) {
        const bool bumped = (planetSurface_.bumpKind != 0);
        const char* blkLine = "";
        if (bumped && planetSurface_.bumpBack) {
          blkLine = (planetSurface_.bumpKind == 2) ? "BACK: SHIP"
                  : (planetSurface_.bumpKind == 3) ? "BACK: ORE"
                  : (planetSurface_.bumpKind == 4) ? (inMine ? "BACK: WALL" : "BACK: CLIFF")
                                                   : "BACK: BLOCKED";
        } else {
          const std::uint8_t shown = bumped ? planetSurface_.bumpKind : frontBlk;
          if (shown == 2) {
            blkLine = "BLOCKED: SHIP";
          } else if (shown == 3) {
            blkLine = "BLOCKED: ORE";
          } else if (shown == 4) {
            blkLine = inMine ? "BLOCKED: WALL" : "BLOCKED: CLIFF";
          } else {
            blkLine = iceWorld   ? "BLOCKED: ICE"
                    : oceanWorld ? "BLOCKED: REEF"
                    : lavaWorld  ? "BLOCKED: VENT"
                    : rockWorld  ? "BLOCKED: ROCK"
                                 : "BLOCKED: TREE";
          }
        }
        Text(blkLine, static_cast<int>(V(700, 102).x), static_cast<int>(V(700, 102).y),
             infoPx, bumped ? Color{255, 142, 96, 255} : Color{232, 168, 98, 255}, true);
      } else if (frontIsOpenPortal) {
        Text(inMine ? "AHEAD: EXIT" : "AHEAD: MINE",
             static_cast<int>(V(700, 102).x), static_cast<int>(V(700, 102).y),
             infoPx, inMine ? Color{164, 228, 242, 255} : Color{248, 192, 104, 255}, true);
      }

      // D-pad inset box on right side
      rectS(718.0f, 136.0f, 122.0f, 122.0f, Color{6, 7, 9, 255});
      DrawRectangleLinesEx({V(718, 136).x, V(718, 136).y, 122.0f * scale, 122.0f * scale},
                           pen, Color{42, 44, 48, 255});
      const float btnS = 26.0f;
      struct PadBtn { float x; float y; int kind; };
      const PadBtn pads[5] = {
          {766.0f, 152.0f, 1},
          {766.0f, 216.0f, 2},
          {734.0f, 184.0f, 3},
          {798.0f, 184.0f, 4},
          {766.0f, 184.0f, 7},
      };
      for (const PadBtn& b : pads) {
        const Area box{V(b.x, b.y).x, V(b.x, b.y).y, btnS * scale, btnS * scale};
        const bool hov = layout_.content.Contains(mouseX_, mouseY_) &&
                         box.Contains(mouseX_, mouseY_ + contentScroll_);
        const Color btnBg = b.kind == 7
            ? (hov ? Color{255, 226, 148, 255} : Color{208, 212, 206, 255})
            : (hov ? Color{255, 226, 148, 255} : Color{244, 244, 240, 255});
        DrawRectangleRec({box.x, box.y, box.w, box.h}, btnBg);
        DrawRectangleLinesEx({box.x, box.y, box.w, box.h}, pen, Color{40, 42, 46, 255});
        const float cx = b.x + btnS * 0.5f;
        const float cy = b.y + btnS * 0.5f;
        const Color arr{28, 30, 34, 255};
        if (b.kind == 1) {
          DrawTriangle(V(cx, cy - 6.0f), V(cx - 6.0f, cy + 5.0f), V(cx + 6.0f, cy + 5.0f), arr);
        } else if (b.kind == 2) {
          DrawTriangle(V(cx - 6.0f, cy - 5.0f), V(cx, cy + 6.0f), V(cx + 6.0f, cy - 5.0f), arr);
        } else if (b.kind == 3) {
          DrawTriangle(V(cx - 6.0f, cy), V(cx + 5.0f, cy + 6.0f), V(cx + 5.0f, cy - 6.0f), arr);
        } else if (b.kind == 4) {
          DrawTriangle(V(cx + 6.0f, cy), V(cx - 5.0f, cy - 6.0f), V(cx - 5.0f, cy + 6.0f), arr);
        } else {
          const int turnPx = std::max(8, static_cast<int>(std::lround(9.0f * scale)));
          const char* turnTxt = "180";
          const float tw = TextWidth(turnTxt, turnPx, true);
          Text(turnTxt, static_cast<int>(box.x + (box.w - tw) * 0.5f),
               static_cast<int>(box.y + (box.h - static_cast<float>(turnPx)) * 0.5f),
               turnPx, arr, true);
        }
        addHit(box, b.kind, 0);
      }

      EndScissorMode();

      if (bridgeDispatchOpen_ < 0.99f) {
        const Color beam{188, 224, 210, 210};
        line(92.0f, std::max(38.0f, openTop), 868.0f, std::max(38.0f, openTop), beam);
        line(92.0f, std::min(290.0f, openBot), 868.0f, std::min(290.0f, openBot), beam);
      }
    }
    for (int i = 0; i < 2; ++i) {
      const float cx = i ? 914 : 46;
      plate(cx - 23, 26, 46, 276, paper);
      for (int j = 0; j < 3; ++j) {
        const float cy = 62 + 68 * j;
        DrawCircleLinesV(V(cx, cy), 16 * scale, ink);
        for (int k = 0; k <= 8; ++k) {
          const float a = (135 + 33.75f * k) * DEG2RAD;
          line(cx + 12 * std::cos(a), cy + 12 * std::sin(a),
               cx + 15 * std::cos(a), cy + 15 * std::sin(a), rule);
        }
        const float a = -1.5f + 0.85f * std::sin(tSec * 0.8f + j + i);
        line(cx, cy, cx + 11 * std::cos(a), cy + 11 * std::sin(a), ink);
        DrawCircleV(V(cx, cy), 2 * scale, ink);
        line(cx - 11, cy + 24, cx + 11, cy + 24, rule);
      }
      for (int k = 0; k < 8; ++k) line(cx - 12, 250 + 5*k, cx + 12, 250 + 5*k, rule);
    }
    // Instrument sill: two strip charts either side of the engraved bearing scale.
    for (int i = 0; i < 2; ++i) {
      const float x = i ? 724 : 23;
      plate(x, 312, 213, 38, paper);
      for (int k = 0; k <= 12; ++k) line(x+10+k*16, 318, x+10+k*16, 344, rule);
      for (int k = 0; k < 96; ++k) {
        const float phase = k * 0.18f + tSec + i;
        line(x+10+k*2, 331+8*std::sin(phase)*std::cos(phase*0.5f),
             x+12+k*2, 331+8*std::sin(phase+0.18f)*std::cos((phase+0.18f)*0.5f), ink);
      }
    }
    line(256, 335, 704, 335, rule);
    for (int k = 0; k <= 56; ++k) line(256+k*8, 335, 256+k*8, k%7 ? 330 : 321, ink);
    bridgeBearingBar_ = {V(256,318).x, V(256,318).y, 448*scale, 32*scale};
    const float pointer = 480 + 448*bridgeLook_;
    DrawTriangle(V(pointer, 337), V(pointer-6, 346), V(pointer+6, 346), ink);
  }

  // --- Priority 2 Overlay: Spinning turbines, rotating core encoder, sweeping needles & coolant loop ---
  if (engineStage) {
    const Color spokeCol = (night ? ToRay(p.borderDark) : HexColor("#cbb89c", ToRay(p.borderDark)));
    const Color wheelRimCol = (night ? ToRay(p.coffeeDark) : HexColor("#8c6f4e", ToRay(p.coffeeDark)));
    const Color hubFillCol = (night ? ToRay(p.panel2) : HexColor("#cbb89c", ToRay(p.panel2)));
    const Color needleCol = (night ? ToRay(p.coffeeDark) : HexColor("#8c6f4e", ToRay(p.coffeeDark)));
    const Color hotFlowCol = (night ? ToRay(p.amber) : HexColor("#c08a3e", ToRay(p.amber)));
    const Color coolFlowCol = (night ? ToRay(p.sage) : HexColor("#7f9161", ToRay(p.sage)));

    // 1) Rotating 24-tick primary core encoder ring around (270, 188)
    {
      const float cx = origin.x + 270.0f * scale;
      const float cy = origin.y + 188.0f * scale;
      const float r = 52.0f * scale;
      for (int k = 0; k < 24; ++k) {
        const float a = static_cast<float>(k) * (PI / 12.0f) + corePhase;
        const bool major = (k % 6 == 0);
        const float tx = cx + r * std::cos(a);
        const float ty = cy + r * std::sin(a);
        const float sz = (major ? 4.0f : 3.0f) * scale;
        DrawRectangleRec(Rectangle{tx - sz * 0.5f, ty - sz * 0.5f, sz, sz},
                         major ? wheelRimCol : spokeCol);
      }
    }

    // 2) Upper & lower spinning turbine spokes + blades + center hubs
    auto paintTurbine = [&](float sx, float sy, float spokeR, float bladeR, float hubR,
                            int bladeCount, float bladeSize, float phase) {
      const float cx = origin.x + sx * scale;
      const float cy = origin.y + sy * scale;
      for (int s = 0; s < 2; ++s) {
        const float a = phase + static_cast<float>(s) * (PI * 0.5f);
        const float dx = std::cos(a) * spokeR * scale;
        const float dy = std::sin(a) * spokeR * scale;
        DrawLineEx(Vector2{cx - dx, cy - dy}, Vector2{cx + dx, cy + dy},
                   std::max(1.5f, 2.0f * scale), spokeCol);
      }
      for (int k = 0; k < bladeCount; ++k) {
        const float a = phase + static_cast<float>(k) * (2.0f * PI / static_cast<float>(bladeCount)) +
                        (bladeCount == 8 ? PI / 8.0f : 0.0f);
        const float bx = cx + std::cos(a) * bladeR * scale;
        const float by = cy + std::sin(a) * bladeR * scale;
        const float bs = bladeSize * scale;
        DrawRectangleRec(Rectangle{bx - bs * 0.5f, by - bs * 0.5f, bs, bs},
                         (k % 2 == 0) ? wheelRimCol : spokeCol);
      }
      DrawCircleV(Vector2{cx, cy}, hubR * scale, hubFillCol);
      DrawCircleLinesV(Vector2{cx, cy}, hubR * scale, wheelRimCol);
    };
    paintTurbine(424.0f, 158.0f, 36.5f, 29.0f, 8.0f, 8, 5.5f, turbinePhase1);
    paintTurbine(424.0f, 240.0f, 24.5f, 19.0f, 6.0f, 6, 4.5f, turbinePhase2);

    // 3) Sweeping PRS & TMP gauge needles
    const float prsVal = reduced ? 68.0f
                                 : std::clamp(68.0f + (underway ? 11.0f : 0.0f) +
                                                  5.5f * std::sin(tSec * 1.7f) +
                                                  2.0f * std::sin(tSec * 4.6f),
                                              12.0f, 96.0f);
    const float tmpVal = reduced ? 41.0f
                                 : std::clamp(41.0f + (1.0f - condRatio) * 18.0f +
                                                  (underway ? 10.0f : 0.0f) +
                                                  4.2f * std::sin(tSec * 1.15f + 1.2f),
                                              10.0f, 96.0f);
    const float gaugeVals[2] = {prsVal, tmpVal};
    for (int gi = 0; gi < 2; ++gi) {
      const float gx = origin.x + 96.0f * scale;
      const float gy = origin.y + (109.0f + static_cast<float>(gi) * 104.0f) * scale;
      const float deg = 135.0f + gaugeVals[gi] * 2.7f;
      const float rad = deg * DEG2RAD;
      const float cs = std::cos(rad), sn = std::sin(rad);
      const Color ndTint = (gi == 1 && tmpVal > 68.0f) ? (night ? ToRay(p.terracotta) : HexColor("#b05a42", ToRay(p.terracotta))) : needleCol;
      DrawLineEx(Vector2{gx - cs * 4.0f * scale, gy - sn * 4.0f * scale},
                 Vector2{gx + cs * 18.5f * scale, gy + sn * 18.5f * scale},
                 std::max(1.8f, 2.2f * scale), ndTint);
      DrawCircleV(Vector2{gx + cs * 17.5f * scale, gy + sn * 17.5f * scale}, 2.2f * scale, ndTint);
      DrawCircleV(Vector2{gx, gy}, 4.0f * scale, hubFillCol);
      DrawCircleLinesV(Vector2{gx, gy}, 4.0f * scale, wheelRimCol);
    }

    // 4) Continuous coolant loop circulation pulses around main circuit + exchanger loop
    // Main loop path: (30,39) -> (607,39) -> (607,305) -> (193,305) -> (193,262)
    constexpr float kL0 = 577.0f;
    constexpr float kL1 = 266.0f;
    constexpr float kL2 = 414.0f;
    constexpr float kL3 = 43.0f;
    constexpr float kTotalLoop = kL0 + kL1 + kL2 + kL3;
    const float loopSpeed = (reduced || sim.State().engineCondition <= 0.0)
                                ? 0.0f
                                : (115.0f * (underway ? 1.6f : 1.0f));
    constexpr int kLoopPulses = 10;
    for (int pi = 0; pi < kLoopPulses; ++pi) {
      const float d = std::fmod(tSec * loopSpeed +
                                    static_cast<float>(pi) * (kTotalLoop / static_cast<float>(kLoopPulses)),
                                kTotalLoop);
      float px = 30.0f, py = 39.0f;
      float heat = 1.0f;
      if (d < kL0) {
        px = 30.0f + d;
        py = 39.0f;
        heat = 1.0f - (d / kL0) * 0.25f;
      } else if (d < kL0 + kL1) {
        const float s = d - kL0;
        px = 607.0f;
        py = 39.0f + s;
        heat = 0.75f - (s / kL1) * 0.55f;
      } else if (d < kL0 + kL1 + kL2) {
        const float s = d - (kL0 + kL1);
        px = 607.0f - s;
        py = 305.0f;
        heat = 0.20f - (s / kL2) * 0.20f;
      } else {
        const float s = d - (kL0 + kL1 + kL2);
        px = 193.0f;
        py = 305.0f - s;
        heat = 0.0f;
      }
      const Color pCol = StageMix(coolFlowCol, hotFlowCol, std::clamp(heat, 0.0f, 1.0f));
      DrawCircleV(Vector2{origin.x + px * scale, origin.y + py * scale}, 2.8f * scale,
                  Fade(pCol, 0.88f));
    }
    // Exchanger vertical feed & drain pulses (x = 538, y = 46..120 and y = 224..298)
    for (int ei = 0; ei < 4; ++ei) {
      const float u = std::fmod(tSec * (loopSpeed / 120.0f) + static_cast<float>(ei) * 0.25f, 1.0f);
      const float feedY = 46.0f + u * 74.0f;
      const float drainY = 224.0f + u * 74.0f;
      DrawCircleV(Vector2{origin.x + 538.0f * scale, origin.y + feedY * scale},
                  2.4f * scale, Fade(hotFlowCol, 0.82f));
      DrawCircleV(Vector2{origin.x + 538.0f * scale, origin.y + drainY * scale},
                  2.4f * scale, Fade(coolFlowCol, 0.85f));
    }
  }

  // --- Priority 3 Overlay: O2 Intake Airflow, Canister Rising Bubbles & Status LEDs ---
  if (o2Stage) {
    const Color sageCol = (night ? ToRay(p.sage) : HexColor("#7f9161", ToRay(p.sage)));
    const Color ambCol = (night ? ToRay(p.amber) : HexColor("#c08a3e", ToRay(p.amber)));
    const Color bareCol = (night ? ToRay(p.borderDark) : HexColor("#cbb89c", ToRay(p.borderDark)));
    const Color bubbleFill = (night ? ToRay(p.muted) : HexColor("#f5faee", WHITE));

    // 1) Intake duct live indicator dot + traveling intake airflow packets
    {
      const float dotPulse = reduced ? 0.85f : (0.5f + 0.5f * std::sin(tSec * (o2Running ? 4.5f : 1.8f)));
      const Color led = o2Running ? sageCol : ambCol;
      const float lx = origin.x + 88.0f * scale;
      const float ly = origin.y + 28.0f * scale;
      DrawCircleV(Vector2{lx, ly}, 4.5f * scale, Fade(led, 0.25f * dotPulse));
      DrawCircleV(Vector2{lx, ly}, 2.6f * scale, StageMix(bareCol, led, 0.45f + 0.55f * dotPulse));
      if (!reduced && (o2Running || o2Seated > 0)) {
        const float spd = o2Running ? 140.0f : 42.0f;
        for (int bi = 0; bi < 6; ++bi) {
          const float u = std::fmod(tSec * (spd / 470.0f) + static_cast<float>(bi) / 6.0f, 1.0f);
          const float ax = 98.0f + u * 472.0f;
          const float ay = 29.0f + ((bi % 2 == 0) ? -3.0f : 3.0f);
          DrawRectangleRec(Rectangle{origin.x + ax * scale, origin.y + ay * scale,
                                     14.0f * scale, 2.0f * scale},
                           Fade(o2Running ? sageCol : ambCol, o2Running ? 0.50f : 0.25f));
        }
      }
    }

    // 2) CABIN AIR chamber pulsing indicator LED + exhaust TO DECKS indicator LED
    {
      const float airPulse = reduced ? 0.8f : (0.5f + 0.5f * std::sin(tSec * (o2Running ? 3.8f : 1.4f)));
      const Color airLed = o2Running ? sageCol : HexColor(sim.AirSpec().tone, ToRay(p.amber));
      DrawCircleV(Vector2{origin.x + 113.0f * scale, origin.y + 68.0f * scale},
                  4.5f * scale, Fade(airLed, 0.24f * airPulse));
      DrawCircleV(Vector2{origin.x + 113.0f * scale, origin.y + 68.0f * scale},
                  2.5f * scale, StageMix(bareCol, airLed, 0.4f + 0.6f * airPulse));

      const float exPulse = reduced ? 0.8f : (0.5f + 0.5f * std::sin(tSec * 4.2f + 1.0f));
      const Color exLed = o2Running ? sageCol : (o2Seated > 0 ? ambCol : bareCol);
      if (o2Running || o2Seated > 0) {
        DrawCircleV(Vector2{origin.x + 109.0f * scale, origin.y + 337.0f * scale},
                    4.5f * scale, Fade(exLed, 0.26f * exPulse));
      }
      DrawCircleV(Vector2{origin.x + 109.0f * scale, origin.y + 337.0f * scale},
                  2.5f * scale, exLed);
    }

    // 3) Per-slot bay status LED + tiny rising bubbles inside seated / active filter canisters
    for (int k = 0; k < 3; ++k) {
      const float sx = 20.0f + static_cast<float>(k) * 190.0f;
      const auto& slot = sim.Life().slots[static_cast<std::size_t>(k)];
      const bool seatingNow = sim.FindOngoing("o2s" + std::to_string(k)) != nullptr;
      const bool active = slot.state == "active";
      const bool installed = slot.state == "installed";

      // Bay status LED in upper-right header of SLOT frame
      const float ledX = origin.x + (sx + 162.0f) * scale;
      const float ledY = origin.y + 137.0f * scale;
      Color ledCol = bareCol;
      float ledGlow = 0.0f;
      if (active && o2Running) {
        const float pGlow = reduced ? 0.9f : (0.55f + 0.45f * std::sin(tSec * 4.4f + static_cast<float>(k) * 1.1f));
        ledCol = sageCol;
        ledGlow = pGlow * 0.45f;
      } else if (active && !o2Running) {
        const float pGlow = reduced ? 0.8f : (0.35f + 0.65f * std::sin(tSec * 3.0f + static_cast<float>(k)));
        ledCol = (night ? ToRay(p.terracotta) : HexColor("#b05a42", ToRay(p.terracotta)));
        ledGlow = pGlow * 0.35f;
      } else if (installed || seatingNow) {
        const float rate = seatingNow ? 6.5f : 2.1f;
        const float pGlow = reduced ? 0.85f : (0.35f + 0.65f * std::sin(tSec * rate + static_cast<float>(k) * 1.5f));
        ledCol = ambCol;
        ledGlow = pGlow * 0.35f;
      }
      if (ledGlow > 0.05f) {
        DrawCircleV(Vector2{ledX, ledY}, 6.0f * scale, Fade(ledCol, ledGlow));
      }
      DrawCircleV(Vector2{ledX, ledY}, 3.2f * scale, ledCol);
      DrawCircleLinesV(Vector2{ledX, ledY}, 3.2f * scale, (night ? ToRay(p.borderDark) : HexColor("#8c6f4e", ToRay(p.borderDark))));

      // Rising O2 filtration bubbles inside any seated or active canister!
      if (active || installed) {
        const Color fluidCol = active ? sageCol : ambCol;
        const float riseRate = (active && o2Running) ? 0.56f : (active ? 0.14f : 0.22f);
        // Canister interior bounds: x in [sx+52 .. sx+128], y in [168 .. 221]
        if (!reduced) {
          const float shimmer = 0.5f + 0.5f * std::sin(tSec * (active ? 3.2f : 1.4f) + static_cast<float>(k));
          DrawRectangleRec(Rectangle{origin.x + (sx + 52.0f) * scale, origin.y + 170.0f * scale,
                                     76.0f * scale, 50.0f * scale},
                           Fade(fluidCol, active ? (0.06f + 0.06f * shimmer) : 0.04f));
        }
        constexpr int kBubbles = 11;
        for (int bi = 0; bi < kBubbles; ++bi) {
          const unsigned h = static_cast<unsigned>((k + 1) * 977 + bi * 353);
          const float xFrac = static_cast<float>(h % 100u) / 99.0f;
          const float phase = std::fmod(static_cast<float>(bi) / static_cast<float>(kBubbles) +
                                            tSec * (riseRate + 0.06f * static_cast<float>(bi % 3)),
                                        1.0f);
          const float by = 220.0f - phase * 50.0f;  // Rises bottom (220) -> neck (170)
          const float wobble = reduced ? 0.0f
                                       : (3.2f * std::sin(tSec * 2.8f + static_cast<float>(bi) * 1.4f +
                                                          static_cast<float>(k)));
          const float bx = std::clamp(sx + 54.0f + xFrac * 72.0f + wobble,
                                      sx + 52.0f, sx + 128.0f);
          const float edgeFade = std::min(std::clamp(phase * 5.0f, 0.0f, 1.0f),
                                          std::clamp((1.0f - phase) * 5.0f, 0.0f, 1.0f));
          const float r = (1.5f + static_cast<float>(bi % 3) * 0.75f) * scale;
          const float a = edgeFade * (active ? 0.85f : 0.58f);
          if (a <= 0.02f) { continue; }
          const float px = origin.x + bx * scale;
          const float py = origin.y + by * scale;
          DrawCircleV(Vector2{px, py}, r, Fade(bubbleFill, a));
          DrawCircleLinesV(Vector2{px, py}, r, Fade(fluidCol, a * 0.90f));
          DrawCircleV(Vector2{px - r * 0.32f, py - r * 0.32f}, std::max(0.7f, r * 0.28f),
                      Fade(WHITE, a));
        }
      }
    }
  }

  // A stage laid out for a reading size gives each control its whole box and
  // sets the label in the middle of it. The other stages keep the 26 px strip.
  const bool fitted = stage.typePx > 0.0f;
  const int labelPx = TypePx(10);
  for (const LiveControl& control : stage.controls) {
    const Area box{origin.x + control.box.x * scale, origin.y + control.box.y * scale,
                   control.box.w * scale, fitted ? control.box.h * scale : 26.0f};
    if (control.button.taskId == "engine-click") {
      // The engine's battery remains visible even when the drive is full and
      // there is nothing to maintain. Its charge is not the drive condition.
      FillChamferRay(box, kChamferCard, ToRay(control.button.enabled ? p.panel2 : p.panel3));
      PaintProgressFill(box, control.button.fillPercent, p, kChamferCard);
      PaintChamferDoubleLinesRay(box, kChamferCard, ToRay(p.borderDark), ToRay(p.border));
    } else {
      PaintProgressSurface(box, control.button, p);
    }
    Text(control.button.text.c_str(), static_cast<int>(box.x + 8.0f * scale),
             static_cast<int>(fitted ? box.y + (box.h - labelPx) / 2.0f : box.y + 7.0f), labelPx,
             ToRay(control.button.state == ButtonState::Empty || control.button.state == ButtonState::Off ? p.muted : p.ink));
    taskButtons_.push_back({box, control.button.taskId});
  }
}

float ShellView::PaintActionBand(const std::string& title, const std::string& note,
                                const std::vector<ProgressButton>& buttons, const std::string& warning, bool measureOnly) {
  const auto& p = theme_.palette;
  const float left = layout_.content.x + 8, width = layout_.content.w - 16;
  const float inner = std::max(1.0f, width - 24), gap = 10;
  const int size = TypePx(10);
  const float line = std::max(theme_.LineHeight(), static_cast<float>(size) + 4);
  struct ButtonBox { std::size_t index; float width, height; std::vector<std::string> text; };
  std::vector<std::vector<ButtonBox>> rows(1);
  float used = 0;
  for (std::size_t i = 0; i < buttons.size(); ++i) {
    const float w = std::min(inner, std::max(200.0f, TextWidth(buttons[i].text.c_str(), size) + 24));
    auto text = WrapText(buttons[i].text, std::max(1.0f, w - 20), size, SelectFont(size, false));
    if (!rows.back().empty() && used + gap + w > inner) { rows.emplace_back(); used = 0; }
    if (!rows.back().empty()) used += gap;
    used += w;
    rows.back().push_back({i, w, text.size() * line + 12, std::move(text)});
  }
  const auto notes = note.empty() ? std::vector<std::string>{} : WrapText(note, inner, size, SelectFont(size, false));
  const auto warnings = warning.empty() ? std::vector<std::string>{} : WrapText(warning, inner, size, SelectFont(size, false));
  float buttonsH = 0;
  for (const auto& row : rows) {
    float h = 0; for (const auto& button : row) h = std::max(h, button.height);
    if (h) buttonsH += h + gap;
  }
  const float h = 12 + line + 10 + buttonsH + (notes.size() + warnings.size()) * line + 12;
  if (measureOnly) return h;
  actionBandTop_ = layout_.content.Bottom() - 8 - h;
  const float top = actionBandTop_ + contentScroll_;
  // A pinned overlay must not inflate the scroll extent of the body beneath it.
  const bool tracked = paintingContent_; paintingContent_ = false;
  PaintChamferBoxRay(Area{left, top, width, h}, kChamferPanel, ToRay(p.panel),
                     ToRay(p.borderDark), ToRay(p.border));
  Text(title.c_str(), static_cast<int>(left + (width - TextWidth(title.c_str(), size)) / 2), static_cast<int>(top + 8), size, ToRay(p.coffee));
  float y = top + 12 + line + 10;
  for (const auto& row : rows) {
    float rowW = 0, rowH = 0;
    for (const auto& button : row) { rowW += button.width; rowH = std::max(rowH, button.height); }
    if (!row.empty()) rowW += gap * (row.size() - 1);
    float x = left + (width - rowW) / 2;
    for (const auto& cell : row) {
      const auto& button = buttons[cell.index];
      const Area box{x, y, cell.width, rowH};
      PaintProgressSurface(box, button, p);
      float textY = y + (rowH - cell.text.size() * line) / 2;
      for (const auto& text : cell.text) {
        Text(text.c_str(), static_cast<int>(x + (box.w - TextWidth(text.c_str(), size)) / 2), static_cast<int>(textY), size,
             ToRay(button.state == ButtonState::Empty || button.state == ButtonState::Off ? p.muted : p.ink));
        textY += line;
      }
      actionBandHits_.push_back({box, button.taskId, ""});
      x += box.w + gap;
    }
    y += rowH + gap;
  }
  for (const auto& text : notes) {
    Text(text.c_str(), static_cast<int>(left + (width - TextWidth(text.c_str(), size)) / 2), static_cast<int>(y), size, ToRay(p.muted));
    y += line;
  }
  for (const auto& text : warnings) {
    Text(text.c_str(), static_cast<int>(left + (width - TextWidth(text.c_str(), size)) / 2), static_cast<int>(y), size, ToRay(p.terracotta));
    y += line;
  }
  paintingContent_ = tracked;
  return h;
}

void ShellView::PaintTaskScreen(Simulation& sim) {
  taskButtons_.clear();
  const TaskScreenModel& screen = taskScreen_;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float column = 250.0f;

  const float headerH = PaintTaskHeader(screen.head, left, layout_.content.y);

  const float bodyTop = layout_.content.y + headerH + 8;
  const float stageScale = StageScale(screen.stage, theme_);
  const float stageW = screen.stage.width * stageScale;
  const float stageH = screen.stage.height * stageScale;
  const float stageX = left + (right - left - stageW) / 2;
  const bool beside = !screen.hasStage ||
      (stageX >= left + column + 16 && stageX + stageW <= right - column - 16);
  const float panelTop = screen.hasStage && !beside ? bodyTop + stageH + 12 : bodyTop;
  float y = panelTop;
  for (const NavPanel& panel : screen.left) {
    const float panelW = panel.placement == "np-full" ? right - left : column;
    const float panelH = std::max(118.0f, MeasurePanelHeight(panel, panelW));
    PaintPanel(panel, Area{left, y, panelW, panelH});
    y += panelH + 6;
  }
  float ry = panelTop;
  for (const NavPanel& panel : screen.right) {
    const float panelH = std::max(150.0f, MeasurePanelHeight(panel, column));
    PaintPanel(panel, Area{right - column, ry, column, panelH});
    ry += panelH + 6;
  }
  float stageBottom = bodyTop;
  if (screen.hasStage) {
    // The instrument keeps its own size, centered independently of viewport
    // width. When the side panels cannot fit beside it, they flow beneath,
    // never over it.
    const Area origin{stageX, bodyTop, stageW, stageH};
    PaintStage(sim, screen.stage, origin);
    const float uy = beside ? origin.Bottom() + 4 : std::max({origin.Bottom(), y, ry}) + 12;
    const float gap = 6;
    const float panelW = (origin.w - gap * std::max(0, static_cast<int>(screen.under.size()) - 1)) /
                         std::max(1, static_cast<int>(screen.under.size()));
    float underH = 0;
    for (const auto& panel : screen.under) underH = std::max(underH, MeasurePanelHeight(panel, panelW));
    stageBottom = uy + underH;
    for (std::size_t i = 0; i < screen.under.size(); ++i)
      PaintPanel(screen.under[i], {origin.x + i * (panelW + gap), uy, panelW, underH});
  }

  contentPaintBottom_ = std::max({contentPaintBottom_, y, ry, stageBottom});
  if (!screen.progress.empty()) { PaintActionBand("Actions", screen.bandNote, screen.progress); }
  (void)sim;
}

// The chart's celestial bodies: stars, worlds, moons, stations and dust.
namespace {
#include "ChartArt.inc"

float NavBodyWorldY(const NavBodyView& body, const std::string& layer) {
  const ChartLook look = ChartLookOf(body);
  const unsigned h = ChartSeed(body.id);
  const float signedU = (static_cast<float>(h % 2001u) / 1000.0f) - 1.0f;
  const float posU = static_cast<float>((h / 2003u) % 1000u) / 1000.0f;
  if (layer == "galaxy") {
    if (look == ChartLook::Region) { return signedU * 0.34f; }
    return signedU * 0.72f;
  }
  if (layer == "sector") {
    return signedU * 0.62f;
  }
  // System scale: every body lies on the ecliptic. Seeded heights stacked the
  // worlds above and below one another on their stems; the chart's own x/y is
  // the horizontal plane and is the only placement a system has.
  (void)look;
  (void)posU;
  return 0.0f;
}

float NavBodyScale3D(const NavBodyView& body, float tableScale, const std::string& layer) {
  const ChartLook look = ChartLookOf(body);
  float base = static_cast<float>(body.size) * 0.0085f * tableScale;
  switch (look) {
    case ChartLook::Star: base = std::max(base, 0.155f); break;
    case ChartLook::System: base = std::max(base, layer == "galaxy" ? 0.068f : 0.105f); break;
    case ChartLook::Region: base = std::max(base, 0.140f); break;
    case ChartLook::Gas: base = std::max(base, 0.125f); break;
    case ChartLook::Ice:
    case ChartLook::Ocean:
    case ChartLook::Rock: base = std::max(base, 0.095f); break;
    case ChartLook::Moon: base = std::max(base, 0.056f); break;
    case ChartLook::Station: base = std::max(base, 0.085f); break;
    case ChartLook::Site: base = std::max(base, 0.072f); break;
    case ChartLook::Belt: base = std::max(base, 0.088f); break;
    case ChartLook::Origin: base = std::max(base, 0.088f); break;
  }
  // System scale shows worlds, moons and stations side by side on one plane;
  // at full size they crowd the orbits and each other's labels.
  const float shrink = layer == "system" ? 0.62f : 1.0f;
  return std::clamp(base, 0.042f, 0.26f) * shrink;
}

void PaintShadedSphere3D(const Vector3& center, float radius, int rings, int slices,
                         const ::Color& base, const Vector3& lightRef, bool isPointLight,
                         int styleMode, unsigned seed, const Palette& p, float alpha) {
  if (radius <= 1e-4f || alpha <= 0.01f) { return; }
  Vector3 lightDir = lightRef;
  if (isPointLight) {
    lightDir = Vector3{lightRef.x - center.x, lightRef.y - center.y, lightRef.z - center.z};
  }
  const float lLen = std::sqrt(lightDir.x * lightDir.x + lightDir.y * lightDir.y +
                               lightDir.z * lightDir.z);
  if (lLen > 1e-5f) {
    lightDir.x /= lLen;
    lightDir.y /= lLen;
    lightDir.z /= lLen;
  } else {
    lightDir = Vector3{-0.45f, 0.72f, 0.52f};
  }

  std::array<Vector3, 3> spots{};
  for (int k = 0; k < 3; ++k) {
    const float a = static_cast<float>((seed >> (k * 7)) % 360u) * DEG2RAD;
    const float b = (static_cast<float>((seed >> (k * 5 + 3)) % 140u) - 70.0f) * DEG2RAD;
    spots[static_cast<std::size_t>(k)] = Vector3{
        std::cos(b) * std::cos(a), std::sin(b), std::cos(b) * std::sin(a)};
  }

  const ::Color shadowCol = ChartDeep(base, 0.54f);
  const ::Color whiteCol = ToRay(p.white);
  const ::Color inkCol = ToRay(p.coffeeDark);

  auto evalVertex = [&](float nx, float ny, float nz, ::Color* outCol) {
    ::Color albedo = base;
    if (styleMode == 1) {
      // Gas giant latitude banding
      const float band = 0.5f + 0.5f * std::sin(ny * 15.0f + static_cast<float>(seed % 17u));
      albedo = ChartMix(ChartDeep(base, 0.24f), ChartLift(base, 0.22f), band);
    } else if (styleMode == 2) {
      // Ice world frost polar caps + pale glacial core
      if (std::fabs(ny) > 0.58f) {
        albedo = ChartMix(base, whiteCol, 0.78f);
      } else {
        const float patch = nx * spots[0].x + ny * spots[0].y + nz * spots[0].z;
        if (patch > 0.55f) { albedo = ChartMix(base, whiteCol, 0.50f); }
      }
    } else if (styleMode == 3) {
      // Ocean world with continental landmasses
      bool land = false;
      for (int k = 0; k < 3; ++k) {
        const float d = nx * spots[static_cast<std::size_t>(k)].x +
                        ny * spots[static_cast<std::size_t>(k)].y +
                        nz * spots[static_cast<std::size_t>(k)].z;
        if (d > 0.56f) { land = true; break; }
      }
      if (land) {
        albedo = ChartLift(ToRay(p.coffeeLight), 0.14f);
      }
    } else if (styleMode == 4) {
      // Cratered rock / moon
      for (int k = 0; k < 3; ++k) {
        const float d = nx * spots[static_cast<std::size_t>(k)].x +
                        ny * spots[static_cast<std::size_t>(k)].y +
                        nz * spots[static_cast<std::size_t>(k)].z;
        if (d > 0.82f) {
          albedo = ChartDeep(base, 0.36f);
          break;
        }
      }
    } else if (styleMode == 5) {
      // Emissive star core
      const float core = std::clamp(0.55f + 0.45f * ny, 0.0f, 1.0f);
      *outCol = ChartFade(ChartMix(base, whiteCol, 0.48f * core), alpha);
      return;
    }

    const float ndotl = std::max(0.0f, nx * lightDir.x + ny * lightDir.y + nz * lightDir.z);
    const float hemi = 0.28f + 0.16f * std::max(0.0f, ny);
    const float lit = std::clamp(hemi + 0.72f * ndotl, 0.0f, 1.0f);
    ::Color c = ChartMix(shadowCol, albedo, lit);
    if (ndotl > 0.62f) {
      c = ChartMix(c, ChartLift(albedo, 0.45f), (ndotl - 0.62f) / 0.38f * 0.55f);
    }
    // Crisp subtle dark terminator contour so pale spheres stay clear on parchment
    const float edgeTone = 1.0f - std::fabs(ny) * 0.08f;
    c = ChartMix(inkCol, c, std::clamp(edgeTone, 0.80f, 1.0f));
    *outCol = ChartFade(c, alpha);
  };

  rlCheckRenderBatchLimit(rings * slices * 6);
  rlBegin(RL_TRIANGLES);
  for (int i = 0; i < rings; ++i) {
    const float lat0 = (0.5f - static_cast<float>(i) / static_cast<float>(rings)) * PI;
    const float lat1 = (0.5f - static_cast<float>(i + 1) / static_cast<float>(rings)) * PI;
    const float cl0 = std::cos(lat0), sl0 = std::sin(lat0);
    const float cl1 = std::cos(lat1), sl1 = std::sin(lat1);
    for (int j = 0; j < slices; ++j) {
      const float lon0 = static_cast<float>(j) / static_cast<float>(slices) * 2.0f * PI;
      const float lon1 = static_cast<float>(j + 1) / static_cast<float>(slices) * 2.0f * PI;
      const float cx0 = std::cos(lon0), sx0 = std::sin(lon0);
      const float cx1 = std::cos(lon1), sx1 = std::sin(lon1);

      const Vector3 n00{cl0 * cx0, sl0, cl0 * sx0};
      const Vector3 n01{cl0 * cx1, sl0, cl0 * sx1};
      const Vector3 n10{cl1 * cx0, sl1, cl1 * sx0};
      const Vector3 n11{cl1 * cx1, sl1, cl1 * sx1};
      ::Color c00{}, c01{}, c10{}, c11{};
      evalVertex(n00.x, n00.y, n00.z, &c00);
      evalVertex(n01.x, n01.y, n01.z, &c01);
      evalVertex(n10.x, n10.y, n10.z, &c10);
      evalVertex(n11.x, n11.y, n11.z, &c11);

      const auto vtx = [&](const Vector3& n, const ::Color& col) {
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlVertex3f(center.x + n.x * radius, center.y + n.y * radius, center.z + n.z * radius);
      };
      // Outward CCW winding verified for (p00, p01, p10) & (p01, p11, p10)
      vtx(n00, c00); vtx(n01, c01); vtx(n10, c10);
      vtx(n01, c01); vtx(n11, c11); vtx(n10, c10);
    }
  }
  rlEnd();
}

void PaintTranslucentSphere3D(const Vector3& center, float radius, int rings, int slices,
                                const ::Color& col) {
  if (rings < 2 || slices < 4 || radius <= 0.0f || col.a == 0) { return; }
  rlCheckRenderBatchLimit(rings * slices * 6);
  rlBegin(RL_TRIANGLES);
  for (int i = 0; i < rings; ++i) {
    const float lat0 = (0.5f - static_cast<float>(i) / static_cast<float>(rings)) * PI;
    const float lat1 = (0.5f - static_cast<float>(i + 1) / static_cast<float>(rings)) * PI;
    const float cl0 = std::cos(lat0), sl0 = std::sin(lat0);
    const float cl1 = std::cos(lat1), sl1 = std::sin(lat1);
    for (int j = 0; j < slices; ++j) {
      const float lon0 = static_cast<float>(j) / static_cast<float>(slices) * 2.0f * PI;
      const float lon1 = static_cast<float>(j + 1) / static_cast<float>(slices) * 2.0f * PI;
      const float cx0 = std::cos(lon0), sx0 = std::sin(lon0);
      const float cx1 = std::cos(lon1), sx1 = std::sin(lon1);
      const auto emitV = [&](float x, float y, float z) {
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlVertex3f(center.x + x * radius, center.y + y * radius, center.z + z * radius);
      };
      emitV(cl0 * cx0, sl0, cl0 * sx0);
      emitV(cl0 * cx1, sl0, cl0 * sx1);
      emitV(cl1 * cx0, sl1, cl1 * sx0);
      emitV(cl0 * cx1, sl0, cl0 * sx1);
      emitV(cl1 * cx1, sl1, cl1 * sx1);
      emitV(cl1 * cx0, sl1, cl1 * sx0);
    }
  }
  rlEnd();
}

void PaintRing3D(const Vector3& center, float innerR, float outerR,
                 float tiltX, float tiltZ, int segs, const ::Color& col) {
  if (segs < 6 || outerR <= innerR) { return; }
  const float cx = std::cos(tiltX), sx = std::sin(tiltX);
  const float cz = std::cos(tiltZ), sz = std::sin(tiltZ);
  auto xform = [&](float lx, float lz) -> Vector3 {
    // Rotate around X then Z
    const float y1 = -lz * sx;
    const float z1 = lz * cx;
    const float x2 = lx * cz - y1 * sz;
    const float y2 = lx * sz + y1 * cz;
    return Vector3{center.x + x2, center.y + y2, center.z + z1};
  };
  rlDisableBackfaceCulling();
  rlCheckRenderBatchLimit(segs * 6);
  rlBegin(RL_TRIANGLES);
  for (int i = 0; i < segs; ++i) {
    const float a0 = static_cast<float>(i) / static_cast<float>(segs) * 2.0f * PI;
    const float a1 = static_cast<float>(i + 1) / static_cast<float>(segs) * 2.0f * PI;
    const float c0 = std::cos(a0), s0 = std::sin(a0);
    const float c1 = std::cos(a1), s1 = std::sin(a1);
    const Vector3 i0 = xform(c0 * innerR, s0 * innerR);
    const Vector3 o0 = xform(c0 * outerR, s0 * outerR);
    const Vector3 i1 = xform(c1 * innerR, s1 * innerR);
    const Vector3 o1 = xform(c1 * outerR, s1 * outerR);
    // Repeat rlColor4ub per triangle so software rasterizers keep alpha blending active.
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(i0.x, i0.y, i0.z);
    rlVertex3f(o0.x, o0.y, o0.z);
    rlVertex3f(o1.x, o1.y, o1.z);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(i0.x, i0.y, i0.z);
    rlVertex3f(o1.x, o1.y, o1.z);
    rlVertex3f(i1.x, i1.y, i1.z);
  }
  rlEnd();
  rlEnableBackfaceCulling();
}

void PaintAsteroid3D(const Vector3& center, float radius, float rx, float ry, float rz,
                     const ::Color& base, const Vector3& lightDir, unsigned seed) {
  static constexpr float kPhi = 1.61803398875f;
  static constexpr float kRaw[12][3] = {
      {-1,  kPhi, 0}, { 1,  kPhi, 0}, {-1, -kPhi, 0}, { 1, -kPhi, 0},
      { 0, -1,  kPhi}, { 0,  1,  kPhi}, { 0, -1, -kPhi}, { 0,  1, -kPhi},
      { kPhi, 0, -1}, { kPhi, 0,  1}, {-kPhi, 0, -1}, {-kPhi, 0,  1},
  };
  static constexpr int kFaces[20][3] = {
      {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
      {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
      {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
      {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
  };
  const float crx = std::cos(rx), srx = std::sin(rx);
  const float cry = std::cos(ry), sry = std::sin(ry);
  const float crz = std::cos(rz), srz = std::sin(rz);
  std::array<Vector3, 12> v{};
  for (int i = 0; i < 12; ++i) {
    const unsigned h = seed ^ static_cast<unsigned>(i * 2654435761u);
    const float jitter = 0.72f + 0.50f * (static_cast<float>(h % 100u) / 99.0f);
    const float len = std::sqrt(kRaw[i][0] * kRaw[i][0] + kRaw[i][1] * kRaw[i][1] +
                                kRaw[i][2] * kRaw[i][2]);
    float x = kRaw[i][0] / len * radius * jitter;
    float y = kRaw[i][1] / len * radius * jitter * 0.82f;
    float z = kRaw[i][2] / len * radius * jitter;
    // Rotate X -> Y -> Z
    const float y1 = y * crx - z * srx;
    const float z1 = y * srx + z * crx;
    const float x2 = x * cry + z1 * sry;
    const float z2 = -x * sry + z1 * cry;
    const float x3 = x2 * crz - y1 * srz;
    const float y3 = x2 * srz + y1 * crz;
    v[static_cast<std::size_t>(i)] = Vector3{center.x + x3, center.y + y3, center.z + z2};
  }
  rlCheckRenderBatchLimit(60);
  rlBegin(RL_TRIANGLES);
  for (int f = 0; f < 20; ++f) {
    const Vector3& a = v[static_cast<std::size_t>(kFaces[f][0])];
    const Vector3& b = v[static_cast<std::size_t>(kFaces[f][1])];
    const Vector3& c = v[static_cast<std::size_t>(kFaces[f][2])];
    const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nLen > 1e-6f) { nx /= nLen; ny /= nLen; nz /= nLen; }
    const float ndotl = std::max(0.0f, nx * lightDir.x + ny * lightDir.y + nz * lightDir.z);
    const ::Color col = ChartMix(ChartDeep(base, 0.56f), ChartLift(base, 0.24f),
                                 std::clamp(0.24f + 0.76f * ndotl, 0.0f, 1.0f));
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
  }
  rlEnd();
}

void PaintOrbitTrack3D(const Vector3& center, float rx, float rz, float tiltX, float tiltZ,
                       int segs, const ::Color& col) {
  if (segs < 8) { return; }
  const float cx = std::cos(tiltX), sx = std::sin(tiltX);
  const float cz = std::cos(tiltZ), sz = std::sin(tiltZ);
  auto pt = [&](int i) -> Vector3 {
    const float a = static_cast<float>(i) / static_cast<float>(segs) * 2.0f * PI;
    const float lx = std::cos(a) * rx;
    const float lz = std::sin(a) * rz;
    const float y1 = -lz * sx;
    const float z1 = lz * cx;
    const float x2 = lx * cz - y1 * sz;
    const float y2 = lx * sz + y1 * cz;
    return Vector3{center.x + x2, center.y + y2, center.z + z1};
  };
  rlCheckRenderBatchLimit(segs * 2);
  rlBegin(RL_LINES);
  Vector3 prev = pt(0);
  for (int i = 1; i <= segs; ++i) {
    const Vector3 cur = pt(i);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(prev.x, prev.y, prev.z);
    rlVertex3f(cur.x, cur.y, cur.z);
    prev = cur;
  }
  rlEnd();
}

void PaintSatellite3D(const Vector3& center, float scale, float tangentX, float tangentZ,
                      const Palette& p, float beaconPulse) {
  const float tLen = std::hypot(tangentX, tangentZ);
  const float fx = tLen > 1e-4f ? tangentX / tLen : 1.0f;
  const float fz = tLen > 1e-4f ? tangentZ / tLen : 0.0f;
  const float rx = -fz;
  const float rz = fx;
  // Tiny spacecraft bus + twin angled solar wings + antenna beacon
  const Vector3 busSize{scale * 0.85f, scale * 0.72f, scale * 0.85f};
  DrawCube(center, busSize.x, busSize.y, busSize.z, ChartLift(ToRay(p.coffeeLight), 0.15f));
  DrawCubeWires(center, busSize.x, busSize.y, busSize.z, ToRay(p.coffeeDark));

  const ::Color wingCol{66, 98, 138, 255};
  const ::Color wingEdge{42, 64, 92, 255};
  const float wOff = scale * 1.35f;
  const float wSpan = scale * 1.45f;
  const float wChord = scale * 0.62f;
  const float wTilt = scale * 0.24f;
  rlDisableBackfaceCulling();
  rlCheckRenderBatchLimit(12);
  rlBegin(RL_TRIANGLES);
  for (const float side : {-1.0f, 1.0f}) {
    const Vector3 inner{center.x + rx * side * (scale * 0.45f), center.y,
                        center.z + rz * side * (scale * 0.45f)};
    const Vector3 outer{center.x + rx * side * (wOff + wSpan * 0.5f), center.y + wTilt * 0.35f,
                        center.z + rz * side * (wOff + wSpan * 0.5f)};
    const Vector3 a{inner.x - fx * wChord, inner.y - wTilt, inner.z - fz * wChord};
    const Vector3 b{inner.x + fx * wChord, inner.y + wTilt, inner.z + fz * wChord};
    const Vector3 c{outer.x + fx * wChord, outer.y + wTilt, outer.z + fz * wChord};
    const Vector3 d{outer.x - fx * wChord, outer.y - wTilt, outer.z - fz * wChord};
    rlColor4ub(wingCol.r, wingCol.g, wingCol.b, 255);
    rlVertex3f(a.x, a.y, a.z); rlVertex3f(b.x, b.y, b.z); rlVertex3f(c.x, c.y, c.z);
    rlVertex3f(a.x, a.y, a.z); rlVertex3f(c.x, c.y, c.z); rlVertex3f(d.x, d.y, d.z);
  }
  rlEnd();
  rlEnableBackfaceCulling();

  DrawLine3D(Vector3{center.x - rx * (wOff + wSpan * 0.5f), center.y,
                     center.z - rz * (wOff + wSpan * 0.5f)},
             Vector3{center.x + rx * (wOff + wSpan * 0.5f), center.y,
                     center.z + rz * (wOff + wSpan * 0.5f)},
             wingEdge);
  const Vector3 tip{center.x, center.y + scale * 1.15f, center.z};
  DrawLine3D(center, tip, ToRay(p.coffeeDark));
  PaintTranslucentSphere3D(tip, scale * 0.28f, 4, 6,
                           Fade(ToRay(p.terracotta), 0.35f + 0.65f * beaconPulse));
}

void PaintStation3D(const Vector3& center, float scale, float yawRad,
                    const Palette& p, float alpha, float beaconOpacity) {
  const float cs = std::cos(yawRad), sn = std::sin(yawRad);
  const ::Color hub = ChartFade(ToRay(p.coffee), alpha);
  const ::Color trim = ChartFade(ToRay(p.coffeeLight), alpha);
  const ::Color dark = ChartFade(ToRay(p.coffeeDark), alpha);
  PaintShadedSphere3D(center, scale * 0.52f, 6, 10, hub,
                      Vector3{-0.4f, 0.75f, 0.5f}, false, 0, 7u, p, alpha);
  // Equatorial orbital ring + twin orthogonal truss arms + antenna mast
  PaintRing3D(center, scale * 0.72f, scale * 0.94f, 0.0f, 0.0f, 16, trim);
  const float arm = scale * 1.55f;
  DrawLine3D(Vector3{center.x - arm * cs, center.y, center.z - arm * sn},
             Vector3{center.x + arm * cs, center.y, center.z + arm * sn}, dark);
  DrawLine3D(Vector3{center.x + arm * sn, center.y, center.z - arm * cs},
             Vector3{center.x - arm * sn, center.y, center.z + arm * cs}, dark);
  const Vector3 mastTop{center.x, center.y + scale * 1.85f, center.z};
  DrawLine3D(center, mastTop, trim);
  PaintTranslucentSphere3D(mastTop, scale * 0.26f, 5, 6,
                           ChartFade(ToRay(p.terracotta), beaconOpacity * alpha));
}

}  // namespace

void ShellView::EnsureNavScreen(const Simulation& sim) {
  NavCacheKey key;
  key.valid = true;
  key.actions = sim.ActionCount();
  key.layer = sim.Map().layer;
  key.sector = sim.Map().sector;
  key.here = sim.Map().here;
  key.zoom = sim.Map().zoom;
  auto viewIt = sim.Views().find(key.layer);
  if (viewIt != sim.Views().end()) {
    key.panX = viewIt->second.px;
    key.panY = viewIt->second.py;
  }
  key.target = sim.State().navTarget.value_or(std::string());
  key.docked = sim.State().docked;
  key.reducedMotion = sim.State().reducedMotion;
  key.scannedCount = sim.State().scanned.size();
  key.fuel = sim.StatValue("fuel");
  key.energy = sim.StatValue("energy");
  key.drive = sim.State().engineCondition;
  key.sensorTier = sim.SensorTier();
  key.scanCost = sim.ScanCost();
  key.mapMarksCount = sim.State().mapMarks.size();
  key.cargoUsed = sim.CargoUsed();
  key.cargoCap = sim.CargoStore().capacity;

  std::size_t taskSig = sim.State().ongoing.size();
  for (const Task& task : sim.State().ongoing) {
    taskSig ^= std::hash<std::string>{}(task.id) + 0x9e3779b9 + (taskSig << 6) + (taskSig >> 2);
    taskSig ^= std::hash<double>{}(task.left) + 0x9e3779b9 + (taskSig << 6) + (taskSig >> 2);
    taskSig ^= std::hash<std::string>{}(task.state) + 0x9e3779b9 + (taskSig << 6) + (taskSig >> 2);
  }
  key.ongoingSig = taskSig;

  std::size_t crewSig = sim.Crew().size();
  for (const CrewMember& member : sim.Crew()) {
    crewSig ^= std::hash<std::string>{}(member.job) + 0x9e3779b9 + (crewSig << 6) + (crewSig >> 2);
    crewSig ^= std::hash<double>{}(member.jobAt) + 0x9e3779b9 + (crewSig << 6) + (crewSig >> 2);
  }
  key.crewSig = crewSig;

  if (key == navCacheKey_) { return; }
  navCacheKey_ = std::move(key);
  navScreen_ = BuildNavScreen(sim);
  navRaster_ = BuildNavRaster(sim);
}

void ShellView::PaintNavScreen(Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = navScreen_.screen;
  const NavChartView& chart = navScreen_.chart;

  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float leftColumn = 214.0f;
  const float rightColumn = 252.0f;
  const float middleLeft = left + leftColumn + 12.0f;
  const float middleRight = right - rightColumn - 12.0f;

  const float headerH = PaintTaskHeader(screen.head, left, layout_.content.y);

  // HTA #map fills the middle column up to 1100px. Its height is limited by
  // the remaining content height, 500px and half its width, with a 240px floor.
  const float spareW = std::max(1.0f, middleRight - middleLeft);
  const float chartW = std::min(1100.0f, spareW);
  const float chartTop = layout_.content.y + headerH + 8.0f;
  // Park at 240 and measure every other drawn item, as HTA layoutNav does.
  const float halfW = (chartW - 6) / 2;
  float underH = 112;
  for (const auto& panel : screen.under) underH = std::max(underH, MeasurePanelHeight(panel, halfW));
  float leftH = 0;
  for (const auto& panel : screen.left) leftH += std::max(96.0f, MeasurePanelHeight(panel, leftColumn)) + 4;
  const float rightH = screen.right.empty() ? 0 : std::max(190.0f, MeasurePanelHeight(screen.right[0], rightColumn));
  float tailH = screen.commands.size() * (theme_.LineHeight() + 12);
  for (const auto& note : navScreen_.notes)
    tailH += WrapText(note, right - left - 16, TypePx(9), SelectFont(TypePx(9), false)).size() * theme_.LineHeight() + 4;
  const float parkedBody = std::max({240 + 22 + underH, leftH, rightH}) + 12 + tailH;
  const float otherH = headerH + 8 + parkedBody - 240 + 8;
  const float availableH = layout_.content.h - otherH - 4;
  // Preserve the existing logical viewport and cap/floor ordering.
  const float chartH = std::min({500.0f, chartW * 0.5f, std::max(240.0f, availableH)});
  const Area box{middleLeft + (spareW - chartW) / 2, chartTop, chartW, chartH};
  navChartBox_ = box;
  const ship::NavRaster& raster = navRaster_;
  const float sx = box.w / static_cast<float>(raster.width == 0 ? 1.0 : raster.width);
  const float sy = box.h / static_cast<float>(raster.height == 0 ? 1.0 : raster.height);
  navChartScaleX_ = sx;
  navChartScaleY_ = sy;
  const float bodyScale = std::min(sx, sy);
  const float clipMinX = box.x - 18.0f;
  const float clipMaxX = box.Right() + 18.0f;
  const float clipMinY = box.y - 18.0f;
  const float clipMaxY = box.Bottom() + 18.0f;

  const ::Color kPlayerBeaconRed{220, 38, 38, 255};
  const bool reduced = sim.State().reducedMotion;
  const float tSec = reduced ? 0.0f : static_cast<float>(frameTimeMs_ * 0.001);
  navFxTick_ = reduced ? 0.0 : frameTimeMs_ / 60.0;
  const NavFxFrame liveFx = BuildNavFx(sim, navFxTick_, reduced);

  PaintChamferBoxRay(box, kChamferPanel, ToRay(p.chart),
                       ToRay(p.borderDark), ToRay(p.border));

  struct ProjectedBody {
    Vector3 worldPos{};
    Vector2 docPos{};
    float ndcZ = 0.0f;
    float screenR = 6.0f;
    bool onScreen = false;
  };
  struct SectorTerritory {
    std::string id;
    std::string name;
    Vector3 worldCenter{};
    float rx = 0.0f;
    float rz = 0.0f;
    ::Color tint{};
    bool active = false;
  };
  std::vector<SectorTerritory> sectorTerritories;
  std::unordered_map<std::string, Vector3> worldAllById;
  worldAllById.reserve(chart.bodies.size() * 2);

  const float tableHalfD = 3.15f;
  const float tableHalfW = tableHalfD * (box.w / std::max(1.0f, box.h));
  const float invRasterW = 1.0f / static_cast<float>(raster.width == 0 ? 1.0 : raster.width);
  const float invRasterH = 1.0f / static_cast<float>(raster.height == 0 ? 1.0 : raster.height);
  auto chartToXZ = [&](double cx, double cy) -> std::pair<float, float> {
    const float nx = (static_cast<float>(cx) * invRasterW - 0.5f) * 2.0f;
    const float nz = (static_cast<float>(cy) * invRasterH - 0.5f) * 2.0f;
    return {nx * tableHalfW, nz * tableHalfD};
  };

  // Locate primary system star (if any) for real directional planetary lighting and orbit rings.
  Vector3 systemStarPos{-tableHalfW * 0.6f, 2.5f, -tableHalfD * 0.6f};
  bool hasPointStarLight = false;
  for (const NavBodyView& body : chart.bodies) {
    const auto xz = chartToXZ(body.at.x, body.at.y);
    const float wy = NavBodyWorldY(body, chart.layer);
    const Vector3 wp{xz.first, wy, xz.second};
    worldAllById.emplace(body.id, wp);
    if (chart.layer == "system" && body.type == "star" && !hasPointStarLight) {
      systemStarPos = wp;
      hasPointStarLight = true;
    }
  }

  // Visible bodies inside generous 3D horizon margin around the chart table.
  std::vector<const NavBodyView*> ordered;
  ordered.reserve(chart.bodies.size());
  for (const NavBodyView& body : chart.bodies) {
    const float nx = (static_cast<float>(body.at.x) * invRasterW - 0.5f) * 2.0f;
    const float nz = (static_cast<float>(body.at.y) * invRasterH - 0.5f) * 2.0f;
    if (std::fabs(nx) > 1.55f || std::fabs(nz) > 1.55f) { continue; }
    ordered.push_back(&body);
  }
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const NavBodyView* a, const NavBodyView* b) {
                     return ChartPaintRank(ChartLookOf(*a)) < ChartPaintRank(ChartLookOf(*b));
                   });

  if (chart.layer == "sector") {
    static constexpr ::Color kSectorPalette[6] = {
        ::Color{146, 98, 42, 255},   ::Color{62, 108, 142, 255},
        ::Color{74, 122, 84, 255},   ::Color{154, 78, 68, 255},
        ::Color{110, 84, 138, 255},  ::Color{56, 124, 118, 255},
    };
    struct BoundsAcc {
      float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
      int count = 0;
    };
    std::unordered_map<std::string, BoundsAcc> boundsByRegion;
    boundsByRegion.reserve(sim.World().regions.size() * 2);
    for (const NavBodyView& body : chart.bodies) {
      if (body.region.empty()) { continue; }
      const auto itPos = worldAllById.find(body.id);
      if (itPos == worldAllById.end()) { continue; }
      BoundsAcc& acc = boundsByRegion[body.region];
      acc.minX = std::min(acc.minX, itPos->second.x);
      acc.maxX = std::max(acc.maxX, itPos->second.x);
      acc.minZ = std::min(acc.minZ, itPos->second.z);
      acc.maxZ = std::max(acc.maxZ, itPos->second.z);
      ++acc.count;
    }
    const Body* curSys = sim.CurrentSystem();
    const std::string shipRegion = curSys == nullptr ? std::string() : curSys->region;
    sectorTerritories.reserve(sim.World().regions.size());
    for (std::size_t ri = 0; ri < sim.World().regions.size(); ++ri) {
      const Body& reg = sim.World().regions[ri];
      const auto it = boundsByRegion.find(reg.id);
      if (it == boundsByRegion.end() || it->second.count == 0) { continue; }
      const BoundsAcc& acc = it->second;
      const float pad = std::clamp(0.32f * static_cast<float>(sim.Map().zoom * 0.55), 0.24f, 0.72f);
      SectorTerritory terr;
      terr.id = reg.id;
      terr.name = reg.name;
      terr.worldCenter = Vector3{0.5f * (acc.minX + acc.maxX), 0.0f, 0.5f * (acc.minZ + acc.maxZ)};
      terr.rx = std::max(0.45f, 0.5f * (acc.maxX - acc.minX) + pad);
      terr.rz = std::max(0.35f, 0.5f * (acc.maxZ - acc.minZ) + pad * 0.85f);
      terr.tint = kSectorPalette[ri % 6];
      terr.active = (reg.id == sim.Map().sector || reg.id == shipRegion);
      sectorTerritories.push_back(std::move(terr));
    }
  }

  std::unordered_map<std::string, ProjectedBody> projById;
  projById.reserve(ordered.size() * 2);
  Vector2 markerDocPos{};
  bool markerOnScreen = false;

  if (width_ > 1.0f && height_ > 1.0f) {
    constexpr float kCamDist = 13.4f;
    constexpr float kNavFovDeg = 32.0f;
    constexpr double kZNear = 0.5;
    constexpr double kZFar = 80.0;
    const float yawRad = navOrbitYaw_ * DEG2RAD;
    const float pitchRad = navOrbitPitch_ * DEG2RAD;
    const float cosPitch = std::cos(pitchRad);
    const Vector3 camPos{
        kCamDist * cosPitch * std::sin(yawRad),
        kCamDist * std::sin(pitchRad),
        kCamDist * cosPitch * std::cos(yawRad),
    };
    const float fovRad = kNavFovDeg * DEG2RAD;
    const float viewAspect = width_ / height_;
    const float screenCX = box.x + box.w * 0.5f;
    const float screenCY = box.y + box.h * 0.5f - contentScroll_;
    const float ndcX = (2.0f * screenCX / width_) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenCY / height_);

    const double top0 = kZNear * std::tan(static_cast<double>(fovRad) * 0.5);
    const double right0 = top0 * static_cast<double>(viewAspect);
    const double shiftX = -static_cast<double>(ndcX) * right0;
    const double shiftY = -static_cast<double>(ndcY) * top0;
    const double fLeft = -right0 + shiftX;
    const double fRight = right0 + shiftX;
    const double fBottom = -top0 + shiftY;
    const double fTop = top0 + shiftY;

    Camera3D cam{};
    cam.position = camPos;
    cam.target = Vector3{0.0f, 0.0f, 0.0f};
    cam.up = Vector3{0.0f, 1.0f, 0.0f};
    cam.fovy = kNavFovDeg;
    cam.projection = CAMERA_PERSPECTIVE;

    const Matrix viewMat = BuildLookAtMatrix(cam.position, cam.target, cam.up);
    const Matrix projMat = BuildOffAxisFrustumMatrix(fLeft, fRight, fBottom, fTop, kZNear, kZFar);
    const Matrix mvp = MulColMajor(projMat, viewMat);

    auto projectDoc = [&](const Vector3& wp, Vector2* outDoc, float* outNdcZ = nullptr) -> bool {
      Vector2 sp{};
      if (!ProjectWorld3D(wp, mvp, width_, height_, &sp, outNdcZ)) { return false; }
      sp.y += contentScroll_;
      if (outDoc != nullptr) { *outDoc = sp; }
      return true;
    };

    // Camera right & up vectors for crisp 3D billboarded deep-field star sprites.
    const Vector3 camRight{std::cos(yawRad), 0.0f, -std::sin(yawRad)};
    const Vector3 camForward{-cosPitch * std::sin(yawRad), -std::sin(pitchRad),
                             -cosPitch * std::cos(yawRad)};
    const Vector3 camUp{
        camRight.y * camForward.z - camRight.z * camForward.y,
        camRight.z * camForward.x - camRight.x * camForward.z,
        camRight.x * camForward.y - camRight.y * camForward.x,
    };

    const std::string layer = chart.layer;
    (void)layer;
    EndMode2D();
    BeginScissorMode(static_cast<int>(box.x + kDoubleInset),
                     static_cast<int>(box.y + kDoubleInset - contentScroll_),
                     std::max(0, static_cast<int>(box.w - 2.0f * kDoubleInset)),
                     std::max(0, static_cast<int>(box.h - 2.0f * kDoubleInset)));
    BeginMode3D(cam);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlFrustum(fLeft, fRight, fBottom, fTop, kZNear, kZFar);
    rlMatrixMode(RL_MODELVIEW);
    rlEnableDepthTest();
    rlEnableBackfaceCulling();

    // --- Pass A (DepthMask OFF): Hologram reference grid, orbits, lanes & starfield
    rlDisableDepthMask();
    {
      const ::Color gridCol = Fade(ToRay(p.ink), 0.11f);
      const ::Color axisCol = Fade(ToRay(p.ink), 0.19f);
      const float gSpanW = tableHalfW * 1.18f;
      const float gSpanD = tableHalfD * 1.18f;
      rlBegin(RL_LINES);
      for (int gx = -5; gx <= 5; ++gx) {
        const float x = (static_cast<float>(gx) / 5.0f) * gSpanW;
        const ::Color c = (gx == 0) ? axisCol : gridCol;
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlVertex3f(x, 0.0f, -gSpanD);
        rlVertex3f(x, 0.0f, gSpanD);
      }
      for (int gz = -3; gz <= 3; ++gz) {
        const float z = (static_cast<float>(gz) / 3.0f) * gSpanD;
        const ::Color c = (gz == 0) ? axisCol : gridCol;
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlVertex3f(-gSpanW, 0.0f, z);
        rlVertex3f(gSpanW, 0.0f, z);
      }
      rlEnd();

      const ::Color ringGridCol = Fade(ToRay(p.ink), 0.13f);
      for (const float rad : {1.2f, 2.4f, 3.6f, 5.0f}) {
        PaintOrbitTrack3D(Vector3{0.0f, 0.0f, 0.0f}, rad, rad, 0.0f, 0.0f, 44, ringGridCol);
      }

      if (chart.layer == "system" && hasPointStarLight) {
        const ::Color orbCol = Fade(ToRay(p.coffeeLight), 0.24f);
        for (const NavBodyView* body : ordered) {
          if (body->type != "planet" && body->type != "belt" && body->type != "station") {
            continue;
          }
          const auto itP = worldAllById.find(body->id);
          if (itP == worldAllById.end()) { continue; }
          const float dx = itP->second.x - systemStarPos.x;
          const float dz = itP->second.z - systemStarPos.z;
          const float rOrb = std::hypot(dx, dz);
          if (rOrb > 0.25f && rOrb < tableHalfW * 2.2f) {
            const float tilt = std::clamp(itP->second.y / rOrb, -0.22f, 0.22f);
            const float orbTilt = layer == "system" ? 0.0f : tilt * 0.45f;
            PaintOrbitTrack3D(Vector3{systemStarPos.x, systemStarPos.y, systemStarPos.z},
                              rOrb, rOrb, orbTilt, -orbTilt * 0.78f, 40, orbCol);
          }
        }
      }

      if (chart.layer == "sector") {
        for (const SectorTerritory& terr : sectorTerritories) {
          PaintOrbitTrack3D(terr.worldCenter, terr.rx, terr.rz, 0.0f, 0.0f, 36,
                            Fade(terr.tint, terr.active ? 0.48f : 0.24f));
        }
      }

      if (chart.layer == "sector" || chart.layer == "galaxy") {
        const std::vector<Lane>& lanes =
            chart.layer == "sector" ? sim.World().sectorLanes : sim.World().galaxyLanes;
        const std::string homeId = sim.HomeBody() == nullptr ? std::string() : sim.HomeBody()->id;
        const std::string targetId = sim.State().navTarget.value_or(std::string());
        for (const Lane& lane : lanes) {
          const auto a = worldAllById.find(lane.from);
          const auto b = worldAllById.find(lane.to);
          if (a == worldAllById.end() || b == worldAllById.end()) { continue; }
          const bool lit = lane.from == homeId || lane.to == homeId || lane.from == targetId ||
                           lane.to == targetId;
          const ::Color laneCol = lit ? Fade(ToRay(p.coffee), 0.62f)
                                      : Fade(ToRay(p.coffee), 0.20f);
          const float dist = std::hypot(b->second.x - a->second.x, b->second.z - a->second.z);
          const float archH = std::min(0.45f, dist * 0.12f);
          constexpr int kSegs = 6;
          rlCheckRenderBatchLimit(kSegs * 2);
          rlBegin(RL_LINES);
          Vector3 prev = a->second;
          for (int s = 1; s <= kSegs; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(kSegs);
            const float arc = 4.0f * u * (1.0f - u) * archH;
            const Vector3 cur{
                a->second.x + (b->second.x - a->second.x) * u,
                a->second.y + (b->second.y - a->second.y) * u + arc,
                a->second.z + (b->second.z - a->second.z) * u,
            };
            rlColor4ub(laneCol.r, laneCol.g, laneCol.b, laneCol.a);
            rlVertex3f(prev.x, prev.y, prev.z);
            rlVertex3f(cur.x, cur.y, cur.z);
            prev = cur;
          }
          rlEnd();
        }
      }

      // 3D Starfield clusters (distributed in depth across Y for crisp orbital parallax)
      const ::Color amberCol = ToRay(p.amber);
      const ::Color coffeeCol = ToRay(p.coffee);
      const ::Color mutedCol = ToRay(p.muted);
      rlDisableBackfaceCulling();
      rlBegin(RL_TRIANGLES);
      for (std::size_t ci = 0; ci < chart.clusters.size(); ++ci) {
        const NavDot& dot = chart.clusters[ci];
        const auto xz = chartToXZ(dot.x, dot.y);
        const bool isRegionCloud = (dot.cls == "cdot");
        const float hHash = static_cast<float>((ci * 2654435761u) % 2001u) / 1000.0f - 1.0f;
        const float wy = isRegionCloud ? hHash * 0.42f : hHash * 2.35f;
        const Vector3 sc{xz.first, wy, xz.second};
        const bool bright = dot.cls.find("bright") != std::string::npos;
        const float sz = bright ? 0.014f : (isRegionCloud ? 0.011f : 0.008f);
        const ::Color c = bright ? Fade(coffeeCol, 0.85f)
                                 : (isRegionCloud ? Fade(amberCol, 0.62f) : Fade(mutedCol, 0.55f));
        rlCheckRenderBatchLimit(bright ? 12 : 6);
        const auto emitQuad = [&](float r, const ::Color& qc) {
          const Vector3 rx{camRight.x * r, camRight.y * r, camRight.z * r};
          const Vector3 uy{camUp.x * r, camUp.y * r, camUp.z * r};
          const Vector3 v0{sc.x - rx.x - uy.x, sc.y - rx.y - uy.y, sc.z - rx.z - uy.z};
          const Vector3 v1{sc.x + rx.x - uy.x, sc.y + rx.y - uy.y, sc.z + rx.z - uy.z};
          const Vector3 v2{sc.x + rx.x + uy.x, sc.y + rx.y + uy.y, sc.z + rx.z + uy.z};
          const Vector3 v3{sc.x - rx.x + uy.x, sc.y - rx.y + uy.y, sc.z - rx.z + uy.z};
          rlColor4ub(qc.r, qc.g, qc.b, qc.a);
          rlVertex3f(v0.x, v0.y, v0.z); rlVertex3f(v1.x, v1.y, v1.z); rlVertex3f(v2.x, v2.y, v2.z);
          rlColor4ub(qc.r, qc.g, qc.b, qc.a);
  const std::string layer = chart.layer;
          rlVertex3f(v0.x, v0.y, v0.z); rlVertex3f(v2.x, v2.y, v2.z); rlVertex3f(v3.x, v3.y, v3.z);
        };
        if (bright) {
          emitQuad(sz * 1.9f, Fade(amberCol, 0.24f));
          Vector2 starSp{};
          if (gFlare.out != nullptr && ProjectWorld3D(sc, mvp, width_, height_, &starSp, nullptr) &&
              starSp.x >= box.x + kDoubleInset && starSp.x <= box.Right() - kDoubleInset &&
              starSp.y >= box.y + kDoubleInset - contentScroll_ &&
              starSp.y <= box.Bottom() - kDoubleInset - contentScroll_) {
            PushFlarePt(FlareTarget::StarPoint, starSp.x, starSp.y, 0.65f);
          }
        }
        emitQuad(sz, c);
      }
      rlEnd();
      rlEnableBackfaceCulling();
    }

    // --- Pass B (DepthMask ON): Drop-stems, lit 3D worlds, stations, satellites & asteroids
    rlDrawRenderBatchActive();
    rlEnableDepthMask();

    const Vector3 fallbackKey{-0.45f, 0.75f, 0.50f};
    const float tableScale = std::clamp(bodyScale, 0.65f, 1.55f);
    const bool compactGalaxyStars = (chart.layer == "galaxy");

    for (const NavBodyView* body : ordered) {
      const auto itW = worldAllById.find(body->id);
      if (itW == worldAllById.end()) { continue; }
      const Vector3 wp = itW->second;
      const ChartLook look = ChartLookOf(*body);
      const float r3 = NavBodyScale3D(*body, tableScale, chart.layer);
      const unsigned seed = ChartSeed(body->id);

      float alpha = body->inRange || body->here ? 1.0f : 0.78f;
      if (chart.layer == "sector" && !body->region.empty() && !sim.Map().sector.empty() &&
          body->region != sim.Map().sector) {
        alpha = std::min(alpha, 0.86f);
      }

      // Drop-stem + ecliptic footprint tick for elevated bodies (makes 3D depth unmistakable)
      if (std::fabs(wp.y) > 0.03f &&
          !(compactGalaxyStars && body->type == "system" && !body->selected && !body->here)) {
        const ::Color stemCol = Fade(ToRay(p.coffee), 0.34f * alpha);
        DrawLine3D(wp, Vector3{wp.x, 0.0f, wp.z}, stemCol);
        const float tick = std::min(0.055f, r3 * 0.48f);
        DrawLine3D(Vector3{wp.x - tick, 0.0f, wp.z}, Vector3{wp.x + tick, 0.0f, wp.z}, stemCol);
        DrawLine3D(Vector3{wp.x, 0.0f, wp.z - tick}, Vector3{wp.x, 0.0f, wp.z + tick}, stemCol);
      }

      const Vector3 lightRef = (chart.layer == "system" && hasPointStarLight && look != ChartLook::Star)
                                   ? systemStarPos
                                   : fallbackKey;
      const bool pointLight = (chart.layer == "system" && hasPointStarLight && look != ChartLook::Star);

      if (compactGalaxyStars && body->type == "system" && !body->selected && !body->here) {
        PaintShadedSphere3D(wp, r3 * 0.44f, 4, 6, ToRay(p.amber),
                            fallbackKey, false, 5, seed, p, 0.76f * alpha);
      } else {
        switch (look) {
          case ChartLook::Star:
            PaintShadedSphere3D(wp, r3 * 0.92f, 8, 14, ToRay(p.amber),
                                fallbackKey, false, 5, seed, p, alpha);
            break;
          case ChartLook::System:
            PaintShadedSphere3D(wp, r3 * 0.64f, 6, 10, ToRay(p.amber),
                                fallbackKey, false, 5, seed, p, alpha);
            PaintOrbitTrack3D(wp, r3 * 1.15f, r3 * 1.15f, 0.18f, 0.12f, 18,
                              Fade(ToRay(p.coffeeLight), 0.36f * alpha));
            break;
          case ChartLook::Gas: {
            const bool flatLayer = (layer == "system");
            const ::Color gasCol = ChartLift(ToRay(p.amber), 0.38f);
            PaintShadedSphere3D(wp, r3, 8, 14, gasCol, lightRef, pointLight, 1, seed, p, alpha);
            const float ringTiltX = flatLayer ? 0.0f : 0.42f + static_cast<float>(seed % 7u) * 0.04f;
            const float ringTiltZ = flatLayer ? 0.0f : -0.22f + static_cast<float>((seed / 7u) % 5u) * 0.06f;
            const float ringIn = r3 * (flatLayer ? 1.18f : 1.38f);
            const float ringMid = r3 * (flatLayer ? 1.46f : 1.72f);
            const float ringOut = r3 * (flatLayer ? 1.55f : 2.08f);
            PaintRing3D(wp, ringIn, ringMid, ringTiltX, ringTiltZ, 28,
                        ChartFade(ChartLift(ToRay(p.coffeeLight), 0.25f), 0.78f * alpha));
            if (!flatLayer) {
              PaintRing3D(wp, r3 * 1.82f, ringOut, ringTiltX, ringTiltZ, 28,
                          ChartFade(ToRay(p.coffeeLight), 0.48f * alpha));
            }
            break;
          }
          case ChartLook::Ice:
            PaintShadedSphere3D(wp, r3, 8, 14, ChartMix(ToRay(p.white), ToRay(p.sage), 0.28f),
                                lightRef, pointLight, 2, seed, p, alpha);
            break;
          case ChartLook::Ocean:
            PaintShadedSphere3D(wp, r3, 8, 14, ChartMix(ToRay(p.sage), ToRay(p.coffeeDark), 0.18f),
                                lightRef, pointLight, 3, seed, p, alpha);
            break;
          case ChartLook::Rock:
            PaintShadedSphere3D(wp, r3, 8, 14, ChartMix(ToRay(p.coffee), ToRay(p.terracotta), 0.34f),
                                lightRef, pointLight, 4, seed, p, alpha);
            break;
          case ChartLook::Moon:
            PaintShadedSphere3D(wp, r3, 6, 10, ChartLift(ToRay(p.muted), 0.38f),
                                lightRef, pointLight, 4, seed, p, alpha);
            break;
          case ChartLook::Station: {
            const float spin = tSec * 0.28f + static_cast<float>(seed % 100u) * 0.06f;
            const float beaconOp = (liveFx.hasStation && body->id == liveFx.stationId)
                                       ? static_cast<float>(liveFx.beaconOpacity)
                                       : (reduced ? 1.0f : (0.35f + 0.65f * (0.5f + 0.5f * std::sin(tSec * 4.5f))));
            PaintStation3D(wp, r3, spin, p, alpha, beaconOp);

            // The telemetry satellite keeps its orbit in the chart plane so the
            // system layer reads horizontally. Sector and galaxy keep their tilt.
            const bool flatLayer = (layer == "system");
            const float satOrbitR = r3 * (flatLayer ? 2.20f : 2.65f);
            const float satAngle = tSec * 0.85f + static_cast<float>(seed % 31u) * 0.2f;
            const float satTiltX = flatLayer ? 0.0f : 0.32f + static_cast<float>(seed % 5u) * 0.10f;
            const float satTiltZ = flatLayer ? 0.0f : -0.25f;
            PaintOrbitTrack3D(wp, satOrbitR, flatLayer ? satOrbitR : satOrbitR * 0.88f,
                              satTiltX, satTiltZ, 28,
                              Fade(ToRay(p.coffeeLight), 0.42f * alpha));
            const float lx = std::cos(satAngle) * satOrbitR;
            const float lz = std::sin(satAngle) * (flatLayer ? satOrbitR : satOrbitR * 0.88f);
            const float cxT = std::cos(satTiltX), sxT = std::sin(satTiltX);
            const float czT = std::cos(satTiltZ), szT = std::sin(satTiltZ);
            const float ly1 = -lz * sxT, lz1 = lz * cxT;
            const Vector3 satPos{wp.x + (lx * czT - ly1 * szT),
                                 wp.y + (flatLayer ? 0.0f : (lx * szT + ly1 * czT)),
                                 wp.z + lz1};
            const float tx = -std::sin(satAngle);
            const float tz = std::cos(satAngle) * (flatLayer ? 1.0f : 0.88f);
            PaintSatellite3D(satPos, r3 * 0.44f, tx, tz, p,
                             reduced ? 1.0f : (0.5f + 0.5f * std::sin(tSec * 6.0f + satAngle)));
            break;
          }
          case ChartLook::Site: {
            const bool flatLayer = (layer == "system");
            const float spin = tSec * 0.75f + static_cast<float>(seed % 17u);
            PaintAsteroid3D(wp, r3 * 0.68f, 0.35f, spin, 0.2f, ToRay(p.terracotta),
                            fallbackKey, seed);
            PaintOrbitTrack3D(wp, r3 * (flatLayer ? 1.15f : 1.25f),
                              flatLayer ? r3 * 1.15f : r3 * 1.25f,
                              flatLayer ? 0.0f : 0.5f, 0.0f, 18,
                              Fade(ToRay(p.terracotta), 0.55f * alpha));
            break;
          }
          case ChartLook::Belt: {
            const bool flatLayer = (layer == "system");
            const ::Color beltCol = ChartMix(ToRay(p.coffeeLight), ToRay(p.coffeeDark), 0.25f);
            const float beltR = r3 * (flatLayer ? 1.45f : 1.7f);
            PaintOrbitTrack3D(wp, beltR, flatLayer ? beltR : r3 * 1.15f,
                              flatLayer ? 0.0f : 0.22f, flatLayer ? 0.0f : -0.15f, 24,
                              Fade(beltCol, 0.42f * alpha));
            for (int bi = 0; bi < 6; ++bi) {
              const float a = static_cast<float>(bi) / 6.0f * 2.0f * PI + tSec * 0.22f;
              const float rr = flatLayer ? beltR : r3 * 1.65f;
              const Vector3 bp{
                  wp.x + std::cos(a) * rr,
                  wp.y + (flatLayer ? 0.0f : std::sin(a * 2.0f) * r3 * 0.28f),
                  wp.z + std::sin(a) * (flatLayer ? beltR : r3 * 1.12f),
              };
              PaintAsteroid3D(bp, r3 * (flatLayer ? 0.18f : 0.20f + 0.04f * static_cast<float>(bi % 3)),
                              a * 1.7f, a * 2.3f, a * 0.9f, beltCol, lightRef,
                              seed + static_cast<unsigned>(bi) * 97u);
            }
            break;
          }
          case ChartLook::Region:
            PaintShadedSphere3D(wp, r3 * 0.50f, 6, 10, ToRay(p.amber),
                                fallbackKey, false, 5, seed, p, alpha);
            PaintOrbitTrack3D(wp, r3 * 1.35f, r3 * 1.35f, 0.12f, -0.10f, 24,
                              Fade(ToRay(p.coffeeLight), 0.52f * alpha));
            for (int ri = 0; ri < 7; ++ri) {
              const float a = 0.8975979f * static_cast<float>(ri + 1) + tSec * 0.09f;
              const float reach = r3 * (1.25f + 0.22f * static_cast<float>(ri % 3));
              const Vector3 rp{
                  wp.x + std::cos(a) * reach,
                  wp.y + std::sin(a * 1.7f) * r3 * 0.36f,
                  wp.z + std::sin(a) * reach * 0.72f,
              };
              PaintShadedSphere3D(rp, r3 * 0.16f, 4, 6, ToRay(p.coffeeLight),
                                  fallbackKey, false, 5, seed + static_cast<unsigned>(ri), p, alpha);
            }
            break;
          case ChartLook::Origin:
            PaintShadedSphere3D(wp, r3 * 0.72f, 6, 10, ToRay(p.sage),
                                fallbackKey, false, 0, seed, p, alpha);
            PaintOrbitTrack3D(wp, r3 * 1.55f, r3 * 1.55f, 0.0f, 0.0f, 24,
                              Fade(ToRay(p.sage), 0.65f * alpha));
            break;
        }
      }

      ProjectedBody pb;
      pb.worldPos = wp;
      pb.onScreen = projectDoc(wp, &pb.docPos, &pb.ndcZ);
      Vector3 edgePos{wp.x + camRight.x * r3, wp.y + camRight.y * r3, wp.z + camRight.z * r3};
      Vector2 edgeDoc{};
      if (pb.onScreen && projectDoc(edgePos, &edgeDoc)) {
        pb.screenR = std::max(4.5f, std::hypot(edgeDoc.x - pb.docPos.x, edgeDoc.y - pb.docPos.y));
      } else {
        pb.screenR = std::max(4.5f, ChartMinimumRadius(look, bodyScale));
      }
      projById.emplace(body->id, pb);
    }

    // --- Live Flying 3D Asteroids & Deep-System Survey Satellite (System scale) ---
    if (chart.layer == "system") {
      const ::Color rockCol = ChartMix(ToRay(p.coffee), ToRay(p.coffeeDark), 0.24f);
      for (std::size_t ri = 0; ri < liveFx.rocks.size(); ++ri) {
        const NavFxRock& rock = liveFx.rocks[ri];
        const auto xz = chartToXZ(rock.at.x, rock.at.y);
        const float hHash = static_cast<float>((ri * 1337u + 17u) % 200u) / 100.0f - 1.0f;
        const float wy = hHash * 0.56f + 0.06f * std::sin(tSec * 0.6f + static_cast<float>(ri));
        const Vector3 rp{xz.first, wy, xz.second};
        const float rad = 0.036f + 0.013f * static_cast<float>(rock.size);
        const float tumble = tSec * (0.9f + 0.35f * static_cast<float>(ri % 4));
        PaintAsteroid3D(rp, rad, tumble * 0.8f, tumble * 1.2f, tumble * 0.5f,
                        rockCol, fallbackKey, 4242u + static_cast<unsigned>(ri) * 131u);
        // Subtle dust wake trailing behind moving asteroid
        const float wakeLen = std::clamp(static_cast<float>(rock.vx) * 0.85f, 0.10f, 0.28f);
        DrawLine3D(rp, Vector3{rp.x - wakeLen, rp.y, rp.z},
                   Fade(ToRay(p.coffeeLight), 0.42f));
      }

      // Free-roaming reconnaissance satellite orbiting around system center / local star
      if (hasPointStarLight) {
        const float probeR = 1.45f;
        const float probeAng = tSec * 0.34f + 1.1f;
        const Vector3 probePos{
            systemStarPos.x + std::cos(probeAng) * probeR,
            systemStarPos.y + std::sin(probeAng) * 0.38f,
            systemStarPos.z + std::sin(probeAng) * probeR * 0.82f,
        };
        PaintOrbitTrack3D(systemStarPos, probeR, probeR * 0.82f, 0.26f, 0.0f, 36,
                          Fade(ToRay(p.coffeeLight), 0.22f));
        PaintSatellite3D(probePos, 0.048f, -std::sin(probeAng), std::cos(probeAng) * 0.82f,
                         p, reduced ? 1.0f : (0.5f + 0.5f * std::sin(tSec * 5.0f)));
      }
    } else if (!reduced && (chart.layer == "sector" || chart.layer == "galaxy")) {
      // Live 3D courier traffic blips cruising along hyperspace lanes
      const std::vector<Lane>& lanes =
          chart.layer == "sector" ? sim.World().sectorLanes : sim.World().galaxyLanes;
      const std::string homeId = sim.HomeBody() == nullptr ? std::string() : sim.HomeBody()->id;
      int courierCount = 0;
      for (std::size_t li = 0; li < lanes.size() && courierCount < 14; ++li) {
        const Lane& lane = lanes[li];
        const bool lit = (lane.from == homeId || lane.to == homeId);
        if (!lit && (li % 5 != 0)) { continue; }
        const auto a = worldAllById.find(lane.from);
        const auto b = worldAllById.find(lane.to);
        if (a == worldAllById.end() || b == worldAllById.end()) { continue; }
        const float dist = std::hypot(b->second.x - a->second.x, b->second.z - a->second.z);
        const float archH = std::min(0.45f, dist * 0.12f);
        const float u = std::fmod(tSec * (0.11f + 0.02f * static_cast<float>(li % 3)) +
                                      static_cast<float>(li) * 0.23f,
                                  1.0f);
        const float arc = 4.0f * u * (1.0f - u) * archH;
        const Vector3 cp{
            a->second.x + (b->second.x - a->second.x) * u,
            a->second.y + (b->second.y - a->second.y) * u + arc,
            a->second.z + (b->second.z - a->second.z) * u,
        };
        PaintTranslucentSphere3D(cp, lit ? 0.024f : 0.017f, 4, 6,
                                 lit ? ToRay(p.sage) : ToRay(p.amber));
        ++courierCount;
      }
    }

    // 3D Route trajectory dots
    for (std::size_t ri = 0; ri < chart.routes.size(); ++ri) {
      const NavDot& dot = chart.routes[ri];
      const auto xz = chartToXZ(dot.x, dot.y);
      const float frac = chart.routes.size() > 1
                             ? static_cast<float>(ri) / static_cast<float>(chart.routes.size() - 1)
                             : 0.5f;
      const float archY = 0.08f + 0.22f * 4.0f * frac * (1.0f - frac);
      Color ink = Fade(ToRay(p.coffee), 0.78f);
      float sz = 0.0085f;
      if (dot.cls.find("faint") != std::string::npos) {
        ink = Fade(ToRay(p.coffeeLight), 0.55f);
        sz = 0.0070f;
      }
      if (dot.cls.find("sel") != std::string::npos) {
        ink = ToRay(p.sage);
        sz = 0.0115f;
      }
      if (dot.cls.find("trade") != std::string::npos) {
        ink = ::Color{126, 198, 63, 255};
        sz = 0.0145f;
      }
      PaintTranslucentSphere3D(Vector3{xz.first, archY, xz.second}, sz, 4, 6, ink);
    }

    // 3D Ship marker elevation & ring
    Vector3 markerWorld{};
    if (chart.hasMarker) {
      const auto mxz = chartToXZ(chart.marker.x, chart.marker.y);
      float my = 0.05f;
      if (sim.HomeBody() != nullptr) {
        const auto itHome = worldAllById.find(sim.HomeBody()->id);
        if (itHome != worldAllById.end()) { my = itHome->second.y; }
      }
      markerWorld = Vector3{mxz.first, my, mxz.second};
      PaintOrbitTrack3D(markerWorld, 0.16f, 0.16f, 0.0f, 0.0f, 24,
                        Fade(kPlayerBeaconRed, 0.85f));
      if (std::fabs(my) > 0.02f) {
        DrawLine3D(markerWorld, Vector3{markerWorld.x, 0.0f, markerWorld.z},
                   Fade(kPlayerBeaconRed, 0.55f));
      }
      markerOnScreen = projectDoc(markerWorld, &markerDocPos);
    }

    // --- Pass C (DepthMask OFF): Soft luminous coronas, halos & star rays
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (const NavBodyView* body : ordered) {
      const ChartLook look = ChartLookOf(*body);
      if (look != ChartLook::Star && look != ChartLook::System && look != ChartLook::Region &&
          look != ChartLook::Origin) {
        continue;
      }
      if (compactGalaxyStars && look == ChartLook::System && !body->selected && !body->here) {
        continue;
      }
      const auto itW = worldAllById.find(body->id);
      if (itW == worldAllById.end()) { continue; }
      const Vector3 wp = itW->second;
      const float r3 = NavBodyScale3D(*body, tableScale, chart.layer);
      const float pulse = reduced ? 1.0f : (0.92f + 0.08f * std::sin(tSec * 2.1f + wp.x));
      if (look == ChartLook::Star) {
        const ::Color haloCol = ToRay(p.amber);
        PaintTranslucentSphere3D(wp, r3 * 1.48f * pulse, 6, 10, Fade(haloCol, 0.24f));
        PaintTranslucentSphere3D(wp, r3 * 2.10f * pulse, 6, 10, Fade(haloCol, 0.10f));
        const float reach = r3 * 2.35f * pulse;
        const ::Color rayCol = Fade(ChartLift(haloCol, 0.35f), 0.45f);
        for (int ray = 0; ray < 4; ++ray) {
          const float ang = 0.785398f + static_cast<float>(ray) * 1.570796f;
          DrawLine3D(Vector3{wp.x + std::cos(ang) * r3 * 1.05f, wp.y,
                             wp.z + std::sin(ang) * r3 * 1.05f},
                     Vector3{wp.x + std::cos(ang) * reach, wp.y,
                             wp.z + std::sin(ang) * reach},
                     rayCol);
        }
      } else if (look == ChartLook::System) {
        const ::Color haloCol = ToRay(p.amber);
        PaintTranslucentSphere3D(wp, r3 * 1.24f * pulse, 5, 8, Fade(haloCol, 0.17f));
      } else if (look == ChartLook::Region) {
        PaintTranslucentSphere3D(wp, r3 * 1.65f * pulse, 6, 10, Fade(ToRay(p.amber), 0.14f));
        PaintOrbitTrack3D(wp, r3 * 1.68f * pulse, r3 * 1.68f * pulse, 0.08f, -0.06f, 24,
                          Fade(ToRay(p.coffeeLight), 0.35f));
      } else if (look == ChartLook::Origin) {
        PaintTranslucentSphere3D(wp, r3 * 1.75f * pulse, 6, 10, Fade(ToRay(p.sage), 0.18f));
      }
    }
    if (chart.hasMarker) {
      PaintTranslucentSphere3D(markerWorld, 0.14f, 5, 8, Fade(kPlayerBeaconRed, 0.18f));
    }

    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndMode3D();
    EndScissorMode();

    Camera2D scrollCam{{0.0f, 0.0f}, {0.0f, contentScroll_}, 0.0f, 1.0f};
    BeginMode2D(scrollCam);
    BeginScissorMode(static_cast<int>(box.x + kDoubleInset),
                     static_cast<int>(box.y + kDoubleInset - contentScroll_),
                     std::max(0, static_cast<int>(box.w - 2.0f * kDoubleInset)),
                     std::max(0, static_cast<int>(box.h - 2.0f * kDoubleInset)));
  } else {
    BeginScissorMode(static_cast<int>(box.x + kDoubleInset),
                     static_cast<int>(box.y + kDoubleInset - contentScroll_),
                     std::max(0, static_cast<int>(box.w - 2.0f * kDoubleInset)),
                     std::max(0, static_cast<int>(box.h - 2.0f * kDoubleInset)));
    for (const NavBodyView* body : ordered) {
      const float cx = box.x + static_cast<float>(body->at.x) * sx;
      const float cy = box.y + static_cast<float>(body->at.y) * sy;
      const float radius = static_cast<float>(body->size) * 0.42f * bodyScale;
      PaintChartBody(*body, cx, cy, radius, bodyScale, p, 1.0f);
      projById.emplace(body->id, ProjectedBody{{}, Vector2{cx, cy}, 0.0f, std::max(6.0f, radius), true});
    }
    if (chart.hasMarker) {
      markerDocPos = Vector2{box.x + static_cast<float>(chart.marker.x) * sx,
                             box.y + static_cast<float>(chart.marker.y) * sy};
      markerOnScreen = true;
    }
  }

  // Hit-test front to back (last entry in navHits_ wins clicks).
  std::vector<const NavBodyView*> hitOrder = ordered;
  std::stable_sort(hitOrder.begin(), hitOrder.end(),
                   [&projById](const NavBodyView* a, const NavBodyView* b) {
                     const int ra = ChartHitRank(*a);
                     const int rb = ChartHitRank(*b);
                     if (ra != rb) { return ra < rb; }
                     const auto ia = projById.find(a->id);
                     const auto ib = projById.find(b->id);
                     const float za = ia != projById.end() ? ia->second.ndcZ : 1.0f;
                     const float zb = ib != projById.end() ? ib->second.ndcZ : 1.0f;
                     return za > zb;  // Larger ndcZ (farther) first so closer wins last
                   });
  for (const NavBodyView* body : hitOrder) {
    const auto itP = projById.find(body->id);
    if (itP == projById.end() || !itP->second.onScreen) { continue; }
    const float cx = itP->second.docPos.x;
    const float cy = itP->second.docPos.y;
    if (cx < box.x || cx > box.Right() || cy < box.y || cy > box.Bottom()) { continue; }
    const float reach = itP->second.screenR + 6.0f;
    navHits_.push_back(NavHit{Area{cx - reach, cy - reach, reach * 2.0f, reach * 2.0f},
                              "select", body->id});
    if (gFlare.out != nullptr && cx >= box.x + kDoubleInset && cx <= box.Right() - kDoubleInset &&
        cy >= box.y + kDoubleInset && cy <= box.Bottom() - kDoubleInset) {
      const ChartLook lk = ChartLookOf(*body);
      const float sw = (lk == ChartLook::Star || lk == ChartLook::System) ? 1.15f : 0.85f;
      PushFlarePt(FlareTarget::StarPoint, cx, cy - contentScroll_, sw);
    }
  }

  for (const NavBodyView* body : ordered) {
    if (!body->selected) { continue; }
    const auto itP = projById.find(body->id);
    if (itP == projById.end() || !itP->second.onScreen) { continue; }
    const float cx = itP->second.docPos.x;
    const float cy = itP->second.docPos.y;
    const float drawn = itP->second.screenR;
    DrawCircleLinesV(Vector2{cx, cy}, drawn + 4.0f, ToRay(p.terracotta));
    DrawCircleLinesV(Vector2{cx, cy}, drawn + 6.0f, Fade(ToRay(p.terracotta), 0.45f));
  }

  // Top-pass vivid red player position reticle projected from 3D ship location.
  if (chart.hasMarker && markerOnScreen) {
    const float px = markerDocPos.x;
    const float py = markerDocPos.y;
    if (px >= clipMinX && px <= clipMaxX && py >= clipMinY && py <= clipMaxY) {
      const float markerR = std::max(10.0f, 13.5f * bodyScale);
      DrawCircleLinesV(Vector2{px, py}, markerR, kPlayerBeaconRed);
      DrawCircleLinesV(Vector2{px, py}, markerR + 1.6f, Fade(kPlayerBeaconRed, 0.82f));
      const float tickIn = markerR + 1.5f;
      const float tickOut = markerR + 5.5f;
      DrawLineEx(Vector2{px - tickOut, py}, Vector2{px - tickIn, py}, 1.8f, kPlayerBeaconRed);
      DrawLineEx(Vector2{px + tickIn, py}, Vector2{px + tickOut, py}, 1.8f, kPlayerBeaconRed);
      DrawLineEx(Vector2{px, py - tickOut}, Vector2{px, py - tickIn}, 1.8f, kPlayerBeaconRed);
      DrawLineEx(Vector2{px, py + tickIn}, Vector2{px, py + tickOut}, 1.8f, kPlayerBeaconRed);
      if (chart.layer != "system" || sim.FindOngoing("travel") != nullptr) {
        DrawCircleV(Vector2{px, py}, 2.6f, kPlayerBeaconRed);
      }
    }
  }

  // --- labels (projected 3D sector headers + collision-free body labels) ---
  std::vector<Area> takenLabels;
  // Reserve caption, zoom/layer/reset buttons, and bottom orbit hint so chart text never collides.
  takenLabels.push_back(Area{box.x + 6.0f, box.y + 4.0f, std::min(box.w * 0.48f, 340.0f),
                             theme_.LineHeight() + 10.0f});
  takenLabels.push_back(Area{box.Right() - 96.0f, box.y + 4.0f, 92.0f, 180.0f});
  takenLabels.push_back(Area{box.x + 6.0f, box.Bottom() - theme_.LineHeight() - 10.0f,
                             std::min(box.w * 0.60f, 360.0f), theme_.LineHeight() + 8.0f});
  if (chart.layer == "sector") {
    const int hdrSize = TypePx(9);
    const float hdrH = theme_.LineHeight() + 2.0f;
    // Re-derive live 2D screen anchor of each sector from projected visible systems
    struct ScreenBoundsAcc {
      float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
      int count = 0;
    };
    std::unordered_map<std::string, ScreenBoundsAcc> sBounds;
    for (const NavBodyView* body : ordered) {
      if (body->region.empty()) { continue; }
      const auto itP = projById.find(body->id);
      if (itP == projById.end() || !itP->second.onScreen) { continue; }
      ScreenBoundsAcc& sa = sBounds[body->region];
      sa.minX = std::min(sa.minX, itP->second.docPos.x);
      sa.maxX = std::max(sa.maxX, itP->second.docPos.x);
      sa.minY = std::min(sa.minY, itP->second.docPos.y);
      sa.maxY = std::max(sa.maxY, itP->second.docPos.y);
      ++sa.count;
    }
    for (const SectorTerritory& terr : sectorTerritories) {
      const auto itS = sBounds.find(terr.id);
      if (itS == sBounds.end() || itS->second.count == 0) { continue; }
      const ScreenBoundsAcc& sa = itS->second;
      const float cx = 0.5f * (sa.minX + sa.maxX);
      const float topY = sa.minY - 18.0f;
      if (sa.maxX < box.x + 20.0f || sa.minX > box.Right() - 20.0f ||
          sa.maxY < box.y + 20.0f || sa.minY > box.Bottom() - 20.0f) {
        continue;
      }
      const float textW = TextWidth(terr.name.c_str(), hdrSize);
      const float pillW = textW + 10.0f;
      const float pillX = std::clamp(cx - pillW * 0.5f, box.x + 4.0f, box.Right() - pillW - 36.0f);
      const float pillY = std::clamp(topY, box.y + 4.0f, box.Bottom() - hdrH - 4.0f);
      const Area pill{pillX, pillY, pillW + 4.0f, hdrH + 4.0f};
      bool overlaps = false;
      for (const Area& taken : takenLabels) {
        if (ChartRectsOverlap(pill, taken)) { overlaps = true; break; }
      }
      if (overlaps) { continue; }
      PaintChamferBoxRay(pill, kChamferSmall,
                         Fade(ToRay(p.panel), terr.active ? 0.90f : 0.78f),
                         Fade(terr.tint, terr.active ? 0.85f : 0.52f),
                         Fade(terr.tint, 0.35f));
      Text(terr.name.c_str(), static_cast<int>(pill.x + 7.0f), static_cast<int>(pill.y + 3.0f),
           hdrSize, terr.active ? ToRay(p.ink) : terr.tint);
      takenLabels.push_back(pill);
    }
  }

  std::vector<const NavBodyView*> labelled;
  labelled.reserve(ordered.size());
  for (const NavBodyView* body : ordered) {
    if (chart.layer == "galaxy" && body->type == "system") { continue; }
    labelled.push_back(body);
  }
  const NavPoint labelAnchor = chart.hasMarker ? chart.marker : NavPoint{0.0, 0.0};
  const std::string browsed = sim.Map().sector;
  std::stable_sort(labelled.begin(), labelled.end(),
                   [&labelAnchor, &browsed](const NavBodyView* a, const NavBodyView* b) {
                     const int pa = ChartLabelPriority(*a);
                     const int pb = ChartLabelPriority(*b);
                     if (pa != pb) { return pa < pb; }
                     const bool ain = !browsed.empty() && a->region == browsed;
                     const bool bin = !browsed.empty() && b->region == browsed;
                     if (ain != bin) { return ain; }
                     const double da = (a->at.x - labelAnchor.x) * (a->at.x - labelAnchor.x) +
                                       (a->at.y - labelAnchor.y) * (a->at.y - labelAnchor.y);
                     const double db = (b->at.x - labelAnchor.x) * (b->at.x - labelAnchor.x) +
                                       (b->at.y - labelAnchor.y) * (b->at.y - labelAnchor.y);
                     if (da != db) { return da < db; }
                     return a->id < b->id;
                   });
  const std::size_t sectorZoomBudget = static_cast<std::size_t>(
      std::clamp(26.0 + (sim.Map().zoom - 1.0) * 8.0, 22.0, 96.0));
  const std::size_t labelBudget =
      chart.layer == "sector" ? sectorZoomBudget : (chart.layer == "galaxy" ? 24u : 48u);
  std::size_t labelsPainted = 0;
  takenLabels.reserve(takenLabels.size() + labelBudget);
  for (const NavBodyView* body : labelled) {
    if (labelsPainted >= labelBudget) { break; }
    const auto itP = projById.find(body->id);
    if (itP == projById.end() || !itP->second.onScreen) { continue; }
    const float bodyX = itP->second.docPos.x;
    const float bodyY = itP->second.docPos.y;
    if (bodyX < box.x + 4.0f || bodyY < box.y + 4.0f ||
        bodyX > box.Right() - 4.0f || bodyY > box.Bottom() - 4.0f) {
      continue;
    }
    float longestWord = 0.0f;
    {
      std::string word;
      for (const char c : body->name + " ") {
        if (c == ' ') {
          if (!word.empty()) {
            longestWord = std::max(longestWord, TextWidth(word.c_str(), TypePx(9)));
            word.clear();
          }
        } else {
          word.push_back(c);
        }
      }
    }
    const float labelW = std::max(92.0f * sx, longestWord + 6.0f);
    const auto labelLines =
        WrapText(body->name, labelW, TypePx(9), SelectFont(TypePx(9), false));
    const float textHeight =
        theme_.LineHeight() * static_cast<float>(labelLines.size());
    const bool flipLeft = body->labelRight || (bodyX + itP->second.screenR + labelW + 10.0f > box.Right() - 78.0f);
    const float labelX = flipLeft
                             ? (bodyX - itP->second.screenR - labelW - 5.0f)
                             : (bodyX + itP->second.screenR + 5.0f);
    const float labelY = bodyY - textHeight * 0.5f;
    const Area rect{labelX, labelY, labelW, textHeight};
    if (rect.x < box.x + 4.0f || rect.y < box.y + 20.0f || rect.Right() > box.Right() - 4.0f ||
        rect.Bottom() > box.Bottom() - 18.0f) {
      continue;
    }
    bool clashes = false;
    for (const Area& taken : takenLabels) {
      if (ChartRectsOverlap(rect, taken)) { clashes = true; break; }
    }
    if (clashes) { continue; }
    takenLabels.push_back(rect);
    ++labelsPainted;

    float textY = labelY;
    const ::Color ink = body->selected ? ToRay(p.ink)
                                       : (body->scanned ? ToRay(p.ink) : ToRay(p.muted));
    for (const auto& line : labelLines) {
      const float textX =
          flipLeft ? labelX + labelW - TextWidth(line.c_str(), TypePx(9)) : labelX;
      Text(line.c_str(), static_cast<int>(textX), static_cast<int>(textY), TypePx(9), ink);
      textY += theme_.LineHeight();
    }
    navHits_.push_back(NavHit{rect, "select", body->id});
  }
  if (chart.hasMarker && markerOnScreen && sim.HomeBody() != nullptr) {
    const float px = markerDocPos.x;
    const float py = markerDocPos.y;
    const float r = std::max(10.0f, 13.5f * bodyScale);
    navHits_.push_back(NavHit{Area{std::max(box.x, px - r), std::max(box.y, py - r),
        std::max(0.0f, std::min(box.Right(), px + r) - std::max(box.x, px - r)),
        std::max(0.0f, std::min(box.Bottom(), py + r) - std::max(box.y, py - r))}, "select", sim.HomeBody()->id});
  }
  EndScissorMode();
  MaskChamferInnerCornersRay(box, kChamferPanel, ToRay(p.chart));
  PaintChamferDoubleLinesRay(box, kChamferPanel, ToRay(p.borderDark), ToRay(p.border));

  // Match the HTA controls: a vertical zoom pair, then vertical layer buttons
  // pinned at the chart's upper-right edge.
  const float zoomSize = std::max(21.0f, theme_.LineHeight() + 2.0f);
  const float zoomX = box.Right() - zoomSize - 7.0f;
  const Area zoomIn{zoomX, box.y + 7.0f, zoomSize, zoomSize};
  const Area zoomOut{zoomX, zoomIn.Bottom() + 4.0f, zoomSize, zoomSize};
  for (const Area& z : {zoomIn, zoomOut}) {
    PaintChamferBoxRay(z, kChamferSmall, ToRay(p.panel2),
                       ToRay(p.borderDark), ToRay(p.border));
  }
  const float plusW = TextWidth("+", TypePx(11));
  const float minusW = TextWidth("-", TypePx(11));
  Text("+", static_cast<int>(zoomIn.x + (zoomSize - plusW) * 0.5f),
       static_cast<int>(zoomIn.y + (zoomSize - TypePx(11)) * 0.5f),
       TypePx(11), ToRay(p.ink));
  Text("-", static_cast<int>(zoomOut.x + (zoomSize - minusW) * 0.5f),
       static_cast<int>(zoomOut.y + (zoomSize - TypePx(11)) * 0.5f),
       TypePx(11), ToRay(p.ink));
  navHits_.push_back(NavHit{zoomIn, "zoom-in", ""});
  navHits_.push_back(NavHit{zoomOut, "zoom-out", ""});

  float layerY = zoomOut.Bottom() + 6.0f;
  for (const std::string& key : NavLayerOrder()) {
    const bool on = key == chart.layer;
    const std::string label = NavLayerLabel(key);
    const float layerW = std::max(64.0f, TextWidth(label.c_str(), TypePx(9)) + 18.0f);
    const float layerH = std::max(19.0f, theme_.LineHeight() + 4.0f);
    const Area layerBox{box.Right() - layerW - 7.0f, layerY, layerW, layerH};
    PaintChamferBoxRay(layerBox, kChamferSmall, ToRay(on ? p.panel3 : p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
    Text(label.c_str(), static_cast<int>(layerBox.x + 8.0f),
         static_cast<int>(layerBox.y + (layerH - TypePx(9)) * 0.5f), TypePx(9),
         ToRay(on ? p.ink : p.muted));
    navHits_.push_back(NavHit{layerBox, "layer", key});
    layerY += layerH + 4.0f;
  }
  const bool orbited = std::fabs(navOrbitYaw_ - kDefaultNavOrbitYaw) > 0.5f ||
                       std::fabs(navOrbitPitch_ - kDefaultNavOrbitPitch) > 0.5f;
  if (orbited) {
    const char* resetLabel = "RESET 3D";
    const float rstW = std::max(64.0f, TextWidth(resetLabel, TypePx(8)) + 16.0f);
    const float rstH = std::max(18.0f, theme_.LineHeight() + 2.0f);
    const Area rstBox{box.Right() - rstW - 7.0f, layerY + 2.0f, rstW, rstH};
    PaintChamferBoxRay(rstBox, kChamferSmall, ToRay(p.panel2),
                       ToRay(p.borderDark), ToRay(p.border));
    Text(resetLabel, static_cast<int>(rstBox.x + 8.0f),
         static_cast<int>(rstBox.y + (rstH - TypePx(8)) * 0.5f), TypePx(8), ToRay(p.ink));
    navHits_.push_back(NavHit{rstBox, "reset-3d", ""});
  }

  std::string caption = chart.scaleText;
  if (orbited) {
    char orbBuf[48];
    std::snprintf(orbBuf, sizeof(orbBuf), "  [3D %.0f/%.0f deg]", navOrbitYaw_, navOrbitPitch_);
    caption += orbBuf;
  }
  Text(caption.c_str(), static_cast<int>(box.x + 10.0f),
       static_cast<int>(box.y + 9.0f), TypePx(9), ToRay(p.chartInk));
  Text("Left-drag pan  |  Wheel zoom  |  Middle-drag orbit",
       static_cast<int>(box.x + 10.0f),
       static_cast<int>(box.Bottom() - static_cast<float>(TypePx(8)) - 7.0f),
       TypePx(8), Fade(ToRay(p.muted), 0.72f));

  // --- panels: identity left, sensors right, the leg underneath the chart
  float ly = layout_.content.y + headerH + 8.0f;
  for (const NavPanel& panel : screen.left) {
    const float panelH = std::max(96.0f, MeasurePanelHeight(panel, leftColumn));
    PaintPanel(panel, Area{left, ly, leftColumn, panelH});
    ly += panelH + 4.0f;
  }
  float rightBottom = chartTop;
  if (!screen.right.empty()) {
    const float panelH = std::max(190.0f,
                                  MeasurePanelHeight(screen.right[0], rightColumn));
    rightBottom = chartTop + panelH;
    PaintPanel(screen.right[0],
               Area{right - rightColumn, layout_.content.y + headerH + 8.0f,
                    rightColumn, panelH});
  }

  const float uy = box.Bottom() + 22.0f;
  for (std::size_t i = 0; i < screen.under.size(); ++i) {
    PaintPanel(screen.under[i],
               Area{box.x + static_cast<float>(i) * (halfW + 6.0f), uy, halfW,
                    underH});
  }

  // Place travel alongside the details, and sensors and extraction on the
  // opposite side of the chart. These are body controls, not a pinned banner.
  auto paintNavButton = [&](const ProgressButton& button, float bx, float by, float bw) {
    const int size = TypePx(10);
    const float line = std::max(theme_.LineHeight(), static_cast<float>(size) + 4.0f);
    const bool blocked = button.state == ButtonState::Off || button.state == ButtonState::Empty;
    const std::string& labelText = blocked && !button.title.empty() ? button.title : button.text;
    const auto label = WrapText(labelText, bw - 20.0f, size, SelectFont(size, false));
    const float height = std::max(48.0f, static_cast<float>(label.size()) * line + 16.0f);
    const Area control{bx, by, bw, height};
    PaintProgressSurface(control, button, p);
    float textY = by + (height - label.size() * line) / 2.0f;
    for (const auto& text : label) {
      Text(text.c_str(), static_cast<int>(bx + (bw - TextWidth(text.c_str(), size)) / 2),
           static_cast<int>(textY), size,
           ToRay(button.state == ButtonState::Off || button.state == ButtonState::Empty ? p.muted : p.ink));
      textY += line;
    }
    if (button.state != ButtonState::Off && button.state != ButtonState::Empty) {
      navHits_.push_back({control, button.taskId, ""});
    }
    return control.Bottom();
  };
  const float leftButtonTop = std::max(uy, ly + 8.0f);
  float leftButtonBottom = leftButtonTop;
  float rightButtonBottom = std::max(uy, rightBottom + 8.0f);
  for (const ProgressButton& button : screen.progress) {
    if (button.taskId == "travel") {
      leftButtonBottom = paintNavButton(button, left, leftButtonTop, leftColumn);
    } else {
      rightButtonBottom = paintNavButton(button, right - rightColumn, rightButtonBottom, rightColumn) + 12.0f;
    }
  }

  for (const auto& row : screen.commands) {
    if (row.label != "HOLD" && row.label != "Change Course") { continue; }
    const int size = TypePx(10);
    const float line = std::max(theme_.LineHeight(), static_cast<float>(size) + 4.0f);
    const auto label = WrapText(row.label, leftColumn - 20.0f, size, SelectFont(size, false));
    const float height = std::max(48.0f, static_cast<float>(label.size()) * line + 16.0f);
    const Area control{left, leftButtonBottom + 12.0f, leftColumn, height};
    FillChamferRay(control, kChamferCard, ToRay(row.enabled ? p.control : p.panel3));
    PaintProgressFill(control, row.chargePercent, p, kChamferCard);
    PaintChamferDoubleLinesRay(control, kChamferCard, ToRay(p.borderDark), ToRay(p.border));
    float textY = control.y + (height - label.size() * line) / 2.0f;
    for (const auto& text : label) {
      Text(text.c_str(), static_cast<int>(left + (leftColumn - TextWidth(text.c_str(), size)) / 2),
           static_cast<int>(textY), size, ToRay(row.enabled ? p.ink : p.muted));
      textY += line;
    }
    if (row.enabled) {
      navHits_.push_back({control, row.label == "HOLD" ? "hold" : "change-course", ""});
    }
    leftButtonBottom = control.Bottom();
  }

  float ty = std::max({ly, rightBottom, uy + underH, leftButtonBottom, rightButtonBottom}) + 12;
  for (const CommandRowView& row : screen.commands) {
    if (row.label == "HOLD" || row.label == "Change Course") { continue; }
    const float costW = TextWidth(row.displayCost.c_str(), TypePx(9)) + 12.0f;
    const Area rowBox{left + 8.0f, ty, right - left - 16.0f, theme_.LineHeight() + 8.0f};
    const std::string& rowText = !row.enabled && !row.title.empty() ? row.title : row.label;
    Text(rowText.c_str(), static_cast<int>(rowBox.x + costW),
             static_cast<int>(rowBox.y + 4.0f), TypePx(10) ,
             ToRay(row.enabled ? p.ink : p.muted));
    Text((row.displayCost + "  ").c_str(), static_cast<int>(rowBox.x),
             static_cast<int>(rowBox.y + 4.0f), TypePx(9) , ToRay(p.coffee));
    if (row.charged && row.live) {
      const float barW = rowBox.w * static_cast<float>(std::clamp(row.chargePercent, 0.0, 100.0)) / 100.0f;
      DrawRectangle(static_cast<int>(rowBox.x), static_cast<int>(rowBox.Bottom() - 2.0f),
                    static_cast<int>(barW), 2, ::Color{126, 198, 63, 255});
    }
    std::string action;
    if (row.label == "HOLD") { action = "hold"; }
    if (row.label == "Change Course") { action = "change-course"; }
    if (row.label.find("Stand watch") == 0) { action = "stand-watch"; }
    if (row.label.find("Dock at ") == 0) { action = "dock"; }
    if (row.label.find("View ") == 0) { action = "view-sector"; }
    if (!action.empty() && row.enabled) {
      const std::string arg =
          action == "view-sector"
              ? sim.State().navTarget.value_or(std::string())
              : std::string();
      navHits_.push_back(NavHit{rowBox, action, arg});
    }
    ty += theme_.LineHeight() + 12.0f;
  }
  for (const std::string& note : navScreen_.notes) {
    for (const auto& line : WrapText(note, right - left - 16.0f, TypePx(9), SelectFont(TypePx(9), false))) {
      Text(line.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(ty), TypePx(9), ToRay(p.muted));
      ty += theme_.LineHeight();
    }
    ty += 4.0f;
  }
  contentPaintBottom_ = std::max(contentPaintBottom_, ty);

}

void ShellView::PaintCrewRecord(const Simulation& sim) {
  navHits_.clear();
  taskButtons_.clear();
  const Palette& p = theme_.palette;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  const float headerH = PaintTaskHeader(record_.head, left, layout_.content.y);

  // The record is free to open; talking is a separate, clearly priced action.
  float y = layout_.content.y + headerH + 8.0f;
  if (!record_.crewId.empty()) {
    const auto& row = record_.conversation;
    std::string label = row.label;
    if (row.spent) { label += " - out of clicks"; }
    else if (!row.enabled) { label += " - unavailable"; }
    const int size = TypePx(10);
    const float line = theme_.LineHeight();
    const auto lines = WrapText(label, right - left - 20.0f, size, SelectFont(size, false));
    const Area box{left, y, right - left, lines.size() * line + 16.0f};
    FillChamferRay(box, kChamferCard, ToRay(row.enabled ? p.panel : p.panel3));
    if (row.charged) { PaintProgressFill(box, row.chargePercent, p, kChamferCard); }
    PaintChamferDoubleLinesRay(box, kChamferCard, ToRay(p.borderDark), ToRay(p.border));
    float textY = box.y + 8.0f;
    for (const auto& text : lines) {
      Text(text.c_str(), static_cast<int>(left + 10), static_cast<int>(textY), size,
           ToRay(row.enabled ? p.ink : p.muted));
      textY += line;
    }
    if (row.enabled) { navHits_.push_back({box, "talk", record_.crewId}); }
    y = box.Bottom() + 6.0f;
    if (!record_.said.empty()) {
      for (const auto& text : WrapText("\"" + record_.said + "\"", right - left - 16.0f,
                                       size, SelectFont(size, false))) {
        Text(text.c_str(), static_cast<int>(left + 8), static_cast<int>(y), size, ToRay(p.muted));
        y += line;
      }
      y += 6.0f;
    }
  }
  // Status across the top, then the three short shelves left to right.
  if (!record_.shelves.empty()) {
    const float panelH = std::max(96.0f,
                                  MeasurePanelHeight(record_.shelves[0], right - left));
    PaintPanel(record_.shelves[0], Area{left, y, right - left, panelH});
    y += panelH + 6.0f;
  }
  const float column = (right - left - 12.0f) / 3.0f;
  for (std::size_t i = 1; i < record_.shelves.size() && i <= 3; ++i) {
    const float panelH = std::max(180.0f,
                                  MeasurePanelHeight(record_.shelves[i], column));
    PaintPanel(record_.shelves[i],
               Area{left + static_cast<float>(i - 1) * (column + 6.0f), y, column,
                    panelH});
  }
  (void)sim;
}

namespace {
// A picture from assets/art/crew by name, or null when no such file was loaded.
const Texture2D* CrewTexture(const std::map<std::string, void*>& art, const std::string& name) {
  const auto found = art.find(name);
  if (found == art.end() || found->second == nullptr) { return nullptr; }
  const Texture2D* texture = static_cast<const Texture2D*>(found->second);
  return texture->id != 0 ? texture : nullptr;
}

// One colour moved towards another: 0 leaves `from`, 1 gives `to`.
Color Blend(Color from, Color to, float amount) {
  const auto channel = [amount](unsigned char a, unsigned char b) {
    return static_cast<unsigned char>(std::lround(a + (b - a) * amount));
  };
  return Color{channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b), 255};
}

float CrewSeedPhase(const std::string& id) {
  unsigned h = 2166136261u;
  for (unsigned char ch : id) {
    h ^= ch;
    h *= 16777619u;
  }
  return static_cast<float>(h % 628u) * 0.01f;
}

bool HasCrewProp(const CrewScene& scene, const std::string& propName) {
  return std::find(scene.propNames.begin(), scene.propNames.end(), propName) !=
         scene.propNames.end();
}

bool IsVectorCrewProp(const std::string& name) {
  return name == "cross" || name == "desk" || name == "crate" || name == "zzz" ||
         name == "mop" || name == "chart" || name == "wrench" || name == "tank" ||
         name == "ladle";
}

struct ChibiPose {
  float breath = 0.0f;         // 0..1 inhale expansion
  float breathLiftY = 0.0f;    // vertical chest/torso lift (stage px)
  float hipShiftX = 0.0f;      // horizontal weight shift (stage px)
  float hipDipY = 0.0f;        // knee/crouch settle downward (stage px)
  float torsoTiltRad = 0.0f;   // upper-body lean around waist pivot
  float armFloatY = 0.0f;      // subtle shoulder/sleeve vertical float
  float armSwayX = 0.0f;       // subtle arm outward/horizontal drift
  float armSpreadX = 0.0f;     // arm outward posture offset
  float armCounterY = 0.0f;    // opposite-phase left/right arm counterbalance
  float headTiltRad = 0.0f;    // rigid neck rotation (keeps face/glasses crisp)
  float headNodY = 0.0f;       // vertical head nod/droop (stage px)
  float headTurnX = 0.0f;      // horizontal look shift (stage px)
  float headScaleX = 1.0f;     // subtle 2.5D perspective compression on head turn
  float shadowScale = 1.0f;    // dynamic ground contact shadow width factor
  bool isSleeping = false;
  bool isMedical = false;
  bool hasDesk = false;
  bool hasCrate = false;
  bool perked = false;
};

ChibiPose ComputeChibiPose(const Simulation& sim, const CrewMember* crew, const CrewScene& scene,
                           const std::string& crewId, bool hasSaid, bool hovered,
                           float tSec, float stageScale) {
  ChibiPose pose;
  pose.isSleeping = (scene.pose == "sit");
  pose.isMedical = (scene.pose == "lie");
  pose.hasDesk = HasCrewProp(scene, "desk");
  pose.hasCrate = HasCrewProp(scene, "crate");
  pose.perked = hovered || hasSaid;

  const bool reduced = sim.State().reducedMotion;
  const bool travel = sim.FindOngoing("travel") != nullptr;
  const std::string needKey = crew != nullptr ? sim.NeedKeyFor(*crew) : std::string();
  const bool tired = (needKey == "tired" || needKey == "ill" || needKey == "morale");

  const float phase = CrewSeedPhase(crewId);
  const float cadence = 0.94f + 0.12f * (0.5f + 0.5f * std::sin(phase));

  // Static posture offsets remain legible even under reducedMotion while all
  // time-varying motion zeroes cleanly when reducedMotion is enabled.
  if (pose.isMedical) {
    pose.hipDipY = 1.5f * stageScale;
    pose.torsoTiltRad = -0.11f;
    pose.headTiltRad = -0.045f;
    pose.headNodY = 0.8f * stageScale;
    pose.shadowScale = 1.15f;
    if (!reduced) {
      const float br = 0.5f + 0.5f * std::sin(tSec * 1.45f * cadence + phase);
      pose.breath = br;
      pose.breathLiftY = -1.75f * br * stageScale;
      pose.armFloatY = 0.60f * std::sin(tSec * 1.45f * cadence + phase - 0.6f) * stageScale;
      pose.headTiltRad += 0.018f * std::sin(tSec * 0.9f + phase);
      pose.headNodY += 0.60f * br * stageScale;
    }
    return pose;
  }

  if (pose.isSleeping) {
    pose.hipDipY = 3.2f * stageScale;
    pose.torsoTiltRad = 0.026f;
    pose.headTiltRad = 0.045f;
    pose.headNodY = 1.0f * stageScale;
    pose.shadowScale = 1.05f;
    if (!reduced) {
      const float br = 0.5f + 0.5f * std::sin(tSec * 1.55f * cadence + phase);
      const float sway = std::sin(tSec * 0.85f * cadence + phase);
      // Periodic 4.6 s head-nod droop followed by a soft wake-up catch.
      const float loopT = std::fmod(tSec * 0.217f * cadence + phase * 0.15f, 1.0f);
      float nodCycle = 0.0f;
      if (loopT < 0.74f) {
        nodCycle = SmoothStep01(0.10f, 0.74f, loopT);
      } else {
        const float u = (loopT - 0.74f) / 0.26f;
        nodCycle = std::max(0.0f, (1.0f - SmoothStep01(0.0f, 0.45f, u)) - 0.22f * std::sin(u * 6.28318f) * (1.0f - u));
      }
      pose.breath = br;
      pose.breathLiftY = -1.55f * br * stageScale;
      pose.hipShiftX = 1.1f * sway * stageScale;
      pose.hipDipY += 0.65f * br * stageScale;
      pose.torsoTiltRad += 0.016f * nodCycle + 0.012f * sway;
      pose.armFloatY = (0.7f * br + 0.4f * nodCycle) * stageScale;
      pose.headTiltRad = 0.025f + 0.068f * nodCycle + (hovered ? -0.045f : 0.0f);
      pose.headNodY = (0.5f + 2.1f * nodCycle + (hovered ? -1.2f : 0.0f)) * stageScale;
      pose.headTurnX = 0.5f * sway * stageScale;
      pose.shadowScale += 0.04f * br;
    }
    return pose;
  }

  // Active / Standing Duty pose (with desk, crate, fatigue, perk-up, and travel nuances)
  if (pose.hasDesk) {
    pose.hipShiftX = -1.6f * stageScale;
    pose.torsoTiltRad = -0.022f;
    pose.headTiltRad = -0.026f;
    pose.headTurnX = -1.1f * stageScale;
  } else if (pose.hasCrate) {
    pose.headTiltRad = -0.024f;
    pose.headTurnX = -0.9f * stageScale;
  }
  if (tired) {
    pose.hipDipY = 2.0f * stageScale;
    pose.torsoTiltRad += 0.022f;
    pose.headTiltRad += 0.032f;
    pose.headNodY += 0.8f * stageScale;
  }

  if (!reduced) {
    const float brFreq = (tired ? 1.75f : (hovered ? 2.55f : 2.15f)) * cadence;
    const float br = 0.5f + 0.5f * std::sin(tSec * brFreq + phase);
    const float weightShift = std::sin(tSec * (tired ? 0.92f : 1.22f) * cadence + phase);
    const float lookWave = std::sin(tSec * 0.68f * cadence + phase * 1.7f);
    // Shape lookWave into gentle held glances (left -> center -> right).
    const float glance = std::clamp(lookWave * 1.45f, -1.0f, 1.0f);

    pose.breath = br;
    pose.breathLiftY = -(tired ? 1.80f : 1.50f) * br * stageScale;
    pose.hipShiftX += (tired ? 1.45f : 2.40f) * weightShift * stageScale;
    pose.hipDipY += (0.70f * (1.0f - std::cos(tSec * 2.44f * cadence + 2.0f * phase)) *
                     (tired ? 0.50f : 0.95f)) * stageScale;
    pose.torsoTiltRad += -0.030f * weightShift;
    pose.armFloatY = (-1.00f * br + 0.65f * std::sin(tSec * brFreq + phase - 0.8f)) * stageScale;
    pose.armSwayX = -0.55f * weightShift * stageScale;
    pose.armSpreadX = (hovered ? 1.00f : 0.42f * br) * stageScale;
    pose.armCounterY = 0.80f * weightShift * stageScale;

    if (pose.hasDesk) {
      const float studyNod = 0.5f + 0.5f * std::sin(tSec * 1.9f * cadence + phase);
      pose.headTiltRad += -0.026f * studyNod + 0.020f * weightShift;
      pose.headNodY += 1.25f * studyNod * stageScale;
      pose.headTurnX += 0.65f * glance * stageScale;
    } else {
      const float curiosity = std::sin(tSec * 0.95f * cadence + phase * 0.9f);
      pose.headTiltRad += 0.048f * curiosity + (hovered ? -0.038f : 0.0f);
      pose.headNodY += ((hovered ? -1.05f * std::fabs(std::sin(tSec * 4.2f)) : 0.0f) +
                        0.55f * br) * stageScale;
      pose.headTurnX += 1.65f * glance * stageScale;
    }

    if (pose.perked && !pose.hasDesk) {
      const float perkNod = std::sin(tSec * 3.4f + phase);
      pose.headTiltRad += 0.024f * perkNod;
      pose.hipDipY = std::max(0.0f, pose.hipDipY - 0.5f * stageScale);
    }
    if (travel) {
      const float microVib = std::sin(tSec * 26.0f + phase) * 0.18f * stageScale;
      pose.hipShiftX += microVib;
      pose.headNodY += std::cos(tSec * 21.0f) * 0.14f * stageScale;
    }
    pose.headScaleX = 1.0f - 0.020f * std::min(1.0f, std::fabs(pose.headTurnX) / std::max(0.5f, 1.8f * stageScale));
    pose.shadowScale = 1.0f - 0.035f * br + 0.025f * std::fabs(weightShift);
  }
  return pose;
}

void PaintRiggedChibi(const Texture2D& tex, const ScenePicture& shown, const Area& hall,
                      float stageScale, const ChibiPose& pose, Color tint) {
  if (tex.id == 0 || tex.width <= 0 || tex.height <= 0) { return; }
  const float tw = static_cast<float>(tex.width);
  const float th = static_cast<float>(tex.height);
  const float w = tw * 0.5f * stageScale;
  const float h = th * 0.5f * stageScale;
  const float cx = hall.x + static_cast<float>(shown.x) * stageScale;
  const float baseY = hall.y + static_cast<float>(shown.base) * stageScale;

  // Consistent chibi anatomical heights measured up from the boot soles (2px margin):
  // soles = 0, boot cuff ~ 24, belt/waist pivot ~ 86, collar/neck pivot ~ 160.
  const float waistStageY = -86.0f * 0.5f * stageScale;
  const float neckStageY = -160.0f * 0.5f * stageScale;

  const float cosT = std::cos(pose.torsoTiltRad);
  const float sinT = std::sin(pose.torsoTiltRad);
  const float cosH = std::cos(pose.headTiltRad);
  const float sinH = std::sin(pose.headTiltRad);

  const float waistPivotX = pose.hipShiftX * 0.85f;
  const float waistPivotY = waistStageY + pose.hipDipY * 0.85f;

  // Transform the exact collar center through the waist->torso transform so
  // the rigid head rotates seamlessly around the live moving neck joint.
  const float neckRawX = pose.hipShiftX;
  const float neckRawY = neckStageY + pose.hipDipY + pose.breathLiftY;
  const float ndxW = neckRawX - waistPivotX;
  const float ndyW = neckRawY - waistPivotY;
  const float neckPivotX = waistPivotX + ndxW * cosT - ndyW * sinT;
  const float neckPivotY = waistPivotY + ndxW * sinT + ndyW * cosT;

  const auto evalVertex = [&](float u, float v) -> Vector2 {
    const float srcFromBottom = (1.0f - v) * th;
    const float wAnkle = SmoothStep01(14.0f, 36.0f, srcFromBottom);
    const float wTorso = SmoothStep01(68.0f, 102.0f, srcFromBottom);
    const float wHead = SmoothStep01(146.0f, 168.0f, srcFromBottom);
    const float wBodyOnly = wTorso * (1.0f - wHead);

    const float sideAbs = std::fabs(u - 0.5f);
    const float sideSign = (u >= 0.5f) ? 1.0f : -1.0f;
    const float armMask = SmoothStep01(0.27f, 0.36f, sideAbs) *
                          SmoothStep01(56.0f, 74.0f, srcFromBottom) *
                          (1.0f - SmoothStep01(138.0f, 158.0f, srcFromBottom));

    float px = (u - 0.5f) * w;
    float py = (v - 1.0f) * h;

    // 1) Grounded weight shift + crouch settle
    px += pose.hipShiftX * wAnkle;
    py += pose.hipDipY * wAnkle;

    // 2) Chest breathing + arm counterbalance
    px += px * (0.018f * pose.breath * wBodyOnly);
    py += pose.breathLiftY * wTorso;
    py += (pose.armFloatY + sideSign * pose.armCounterY) * armMask;
    px += (sideSign * pose.armSpreadX + pose.armSwayX) * armMask;

    // 3) Torso rotation around waist pivot weighted by wTorso
    const float dxW = px - waistPivotX;
    const float dyW = py - waistPivotY;
    const float aT = pose.torsoTiltRad * wTorso;
    const float cT = std::cos(aT);
    const float sT = std::sin(aT);
    const float tx = waistPivotX + dxW * cT - dyW * sT;
    const float ty = waistPivotY + dxW * sT + dyW * cT;

    // 4) Rigid head rotation + 2.5D glance around neck pivot weighted by wHead
    const float dxN = (tx - neckPivotX) * (1.0f - (1.0f - pose.headScaleX) * wHead);
    const float dyN = ty - neckPivotY;
    const float aH = pose.headTiltRad * wHead;
    const float cH = std::cos(aH);
    const float sH = std::sin(aH);
    const float hx = neckPivotX + dxN * cH - dyN * sH + pose.headTurnX * wHead;
    const float hy = neckPivotY + dxN * sH + dyN * cH + pose.headNodY * wHead;

    return Vector2{cx + hx, baseY + hy};
  };
  (void)cosH;
  (void)sinH;

  constexpr int kCols = 14;
  constexpr int kRows = 36;
  Vector2 grid[(kRows + 1) * (kCols + 1)];
  for (int r = 0; r <= kRows; ++r) {
    const float v = static_cast<float>(r) / static_cast<float>(kRows);
    for (int c = 0; c <= kCols; ++c) {
      const float u = static_cast<float>(c) / static_cast<float>(kCols);
      grid[r * (kCols + 1) + c] = evalVertex(u, v);
    }
  }

  rlSetTexture(tex.id);
  rlBegin(RL_QUADS);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);
  rlNormal3f(0.0f, 0.0f, 1.0f);
  for (int r = 0; r < kRows; ++r) {
    const float v0 = static_cast<float>(r) / static_cast<float>(kRows);
    const float v1 = static_cast<float>(r + 1) / static_cast<float>(kRows);
    for (int c = 0; c < kCols; ++c) {
      const float u0 = static_cast<float>(c) / static_cast<float>(kCols);
      const float u1 = static_cast<float>(c + 1) / static_cast<float>(kCols);
      const Vector2& tl = grid[r * (kCols + 1) + c];
      const Vector2& tr = grid[r * (kCols + 1) + c + 1];
      const Vector2& bl = grid[(r + 1) * (kCols + 1) + c];
      const Vector2& br = grid[(r + 1) * (kCols + 1) + c + 1];

      rlTexCoord2f(u0, v0);
      rlVertex2f(tl.x, tl.y);
      rlTexCoord2f(u0, v1);
      rlVertex2f(bl.x, bl.y);
      rlTexCoord2f(u1, v1);
      rlVertex2f(br.x, br.y);
      rlTexCoord2f(u1, v0);
      rlVertex2f(tr.x, tr.y);
    }
  }
  rlEnd();
  rlSetTexture(0);
}

void PaintQuartersCorridorFx(const Simulation& sim, const Palette& p, const Area& hall,
                             float stageScale, const CrewScene& scene, bool hasCrew,
                             const ChibiPose& pose, float tSec, float contentScroll) {
  const bool reduced = sim.State().reducedMotion;
  const bool travel = sim.FindOngoing("travel") != nullptr;
  const float doorX = kCrewFigureX - kCrewDoorW * 0.5f;
  const float paneX = hall.x + (doorX + 15.0f) * stageScale;
  const float paneY = hall.y + 82.0f * stageScale;
  const float paneW = 66.0f * stageScale;
  const float paneH = 56.0f * stageScale;

  // 1) Door porthole glass: star drift & diagonal reflection glint inside cq-dpane
  BeginScissorMode(static_cast<int>(std::floor(paneX)),
                   static_cast<int>(std::floor(paneY - contentScroll)),
                   std::max(1, static_cast<int>(std::ceil(paneW))),
                   std::max(1, static_cast<int>(std::ceil(paneH))));
  const Color spaceBg = Blend(ToRay(p.panel3), ToRay(p.ink), 0.78f);
  DrawRectangle(static_cast<int>(paneX), static_cast<int>(paneY),
                static_cast<int>(std::ceil(paneW)), static_cast<int>(std::ceil(paneH)), spaceBg);
  const float driftSpeed = reduced ? 0.0f : (travel ? 0.28f : 0.025f);
  for (int si = 0; si < 9; ++si) {
    const float sx0 = std::fmod(0.11f * static_cast<float>(si) + 0.37f * static_cast<float>((si * 3) % 7), 1.0f);
    const float sy0 = std::fmod(0.19f * static_cast<float>(si) + 0.23f * static_cast<float>((si * 5) % 9), 1.0f);
    const float sx = paneX + std::fmod(sx0 + 1.0f - std::fmod(tSec * driftSpeed * (0.65f + 0.12f * static_cast<float>(si % 4)), 1.0f), 1.0f) * paneW;
    const float sy = paneY + sy0 * paneH;
    const float tw = reduced ? 0.8f : (0.55f + 0.45f * std::sin(tSec * (2.2f + 0.5f * static_cast<float>(si)) + static_cast<float>(si)));
    if (travel && !reduced) {
      DrawLineEx(Vector2{sx, sy}, Vector2{sx + 7.0f * stageScale, sy}, 1.2f * stageScale,
                 Fade(ToRay(p.white), 0.45f * tw));
    } else {
      DrawCircleV(Vector2{sx, sy}, ((si % 3 == 0) ? 1.25f : 0.85f) * stageScale,
                  Fade(ToRay(p.white), 0.35f + 0.45f * tw));
    }
  }
  // Subtle glass reflection diagonal bands
  DrawLineEx(Vector2{paneX + 8.0f * stageScale, paneY + 4.0f * stageScale},
             Vector2{paneX + 26.0f * stageScale, paneY + 22.0f * stageScale},
             3.0f * stageScale, Fade(ToRay(p.white), 0.14f));
  DrawLineEx(Vector2{paneX + 16.0f * stageScale, paneY + 4.0f * stageScale},
             Vector2{paneX + 28.0f * stageScale, paneY + 16.0f * stageScale},
             1.5f * stageScale, Fade(ToRay(p.white), 0.10f));
  EndScissorMode();
  // Restore hallway scissor
  BeginScissorMode(static_cast<int>(hall.x + kDoubleInset),
                   static_cast<int>(hall.y + kDoubleInset - contentScroll),
                   std::max(0, static_cast<int>(hall.w - 2.0f * kDoubleInset)),
                   std::max(0, static_cast<int>(hall.h - 2.0f * kDoubleInset)));

  // 2) Soft overhead ceiling light wash down into the corridor + floating dust specks
  {
    const float pulse = reduced ? 1.0f : (0.94f + 0.06f * std::sin(tSec * 2.3f));
    const Color beamCol = Fade(ToRay(p.white), 0.055f * pulse);
    const float topL = hall.x + doorX * stageScale;
    const float topR = hall.x + (doorX + kCrewDoorW) * stageScale;
    const float topY = hall.y + 9.0f * stageScale;
    const float botL = hall.x + (doorX - 42.0f) * stageScale;
    const float botR = hall.x + (doorX + kCrewDoorW + 42.0f) * stageScale;
    const float botY = hall.y + 266.0f * stageScale;
    rlBegin(RL_QUADS);
    rlColor4ub(beamCol.r, beamCol.g, beamCol.b, beamCol.a);
    rlVertex2f(topL, topY);
    rlVertex2f(botL, botY);
    rlVertex2f(botR, botY);
    rlVertex2f(topR, topY);
    rlEnd();

    if (!reduced) {
      for (int di = 0; di < 7; ++di) {
        const float u = static_cast<float>(di) / 7.0f;
        const float dx = 342.0f + 118.0f * std::fmod(0.29f * static_cast<float>(di) + 0.03f * tSec * (di % 2 == 0 ? 1.0f : -0.7f) + 10.0f, 1.0f);
        const float dy = 38.0f + 210.0f * std::fmod(0.41f * static_cast<float>(di) + 0.055f * tSec + 0.15f * std::sin(tSec * 0.7f + u * 6.0f), 1.0f);
        const float alpha = 0.16f + 0.14f * std::sin(tSec * 1.9f + static_cast<float>(di) * 1.3f);
        DrawCircleV(Vector2{hall.x + dx * stageScale, hall.y + dy * stageScale},
                    1.1f * stageScale, Fade(ToRay(p.amber), alpha));
      }
    }
  }

  // 3) Animated doorway status beacon right beside DECK A frame
  {
    Color led = ToRay(p.borderDark);
    float ledPulse = 0.75f;
    if (hasCrew) {
      if (scene.pose == "lie") {
        led = ToRay(p.terracotta);
        ledPulse = reduced ? 0.85f : (0.5f + 0.5f * std::sin(tSec * 3.8f));
      } else if (scene.pose == "sit") {
        led = ToRay(p.amber);
        ledPulse = reduced ? 0.75f : (0.4f + 0.6f * std::sin(tSec * 1.8f));
      } else {
        led = ToRay(p.sage);
        ledPulse = reduced ? 0.85f : (0.55f + 0.45f * std::sin(tSec * 3.0f));
      }
    }
    const float lx = hall.x + (doorX + kCrewDoorW - 9.0f) * stageScale;
    const float ly = hall.y + 49.0f * stageScale;
    DrawCircleV(Vector2{lx, ly}, 4.5f * stageScale, Fade(led, 0.25f * ledPulse));
    DrawCircleV(Vector2{lx, ly}, 2.2f * stageScale, StageMix(ToRay(p.panel), led, 0.45f + 0.55f * ledPulse));
  }

  // 4) Synced soft contact shadow under the figure
  if (hasCrew) {
    const float sx = hall.x + kCrewFigureX * stageScale + pose.hipShiftX * 0.55f;
    const float sy = hall.y + kCrewDeckY * stageScale;
    const float rx = 38.0f * pose.shadowScale * stageScale;
    const float ry = 5.5f * stageScale;
    const Color shad = Fade(ToRay(p.coffeeDark), 0.22f);
    DrawEllipse(static_cast<int>(std::round(sx)), static_cast<int>(std::round(sy)),
                rx, ry, shad);
    DrawEllipse(static_cast<int>(std::round(sx)), static_cast<int>(std::round(sy)),
                rx * 0.68f, ry * 0.65f, Fade(ToRay(p.coffeeDark), 0.18f));
  }
}

void PaintQuartersSchematicProps(const Simulation& sim, const Palette& p, const Area& hall,
                                 float stageScale, const CrewScene& scene,
                                 const ChibiPose& pose, float tSec,
                                 const Font& textFont, int smallPx) {
  const bool reduced = sim.State().reducedMotion;
  const float doorX = kCrewFigureX - kCrewDoorW * 0.5f;
  const float wallLeftCenter = kCrewFigureX - (kCrewDoorW + kCrewBareWall) * 0.5f;   // 327
  const float wallRightCenter = kCrewFigureX + (kCrewDoorW + kCrewBareWall) * 0.5f;  // 475
  const Color edge = ToRay(p.borderDark);
  const Color cardBg = ToRay(p.panel);
  const Color cardHi = ToRay(p.panel3);
  const Color coffee = ToRay(p.coffee);
  const Color dark = ToRay(p.coffeeDark);

  const auto propArea = [&](float sx, float sy, float sw, float sh) {
    return Area{hall.x + sx * stageScale, hall.y + sy * stageScale,
                sw * stageScale, sh * stageScale};
  };

  for (const std::string& propName : scene.propNames) {
    if (propName == "cross") {
      // Wall medical placard + scrolling cardiac telemetry line
      const Area pl = propArea(wallLeftCenter - 20.0f, 118.0f, 40.0f, 52.0f);
      PaintChamferBoxRay(pl, 3.0f * stageScale, cardBg, edge, ToRay(p.border));
      const float cx = pl.x + pl.w * 0.5f;
      const float cy = pl.y + 15.0f * stageScale;
      const Color redCol = ToRay(p.terracotta);
      DrawRectangle(static_cast<int>(cx - 3.0f * stageScale),
                    static_cast<int>(cy - 8.5f * stageScale),
                    std::max(2, static_cast<int>(6.0f * stageScale)),
                    std::max(4, static_cast<int>(17.0f * stageScale)), redCol);
      DrawRectangle(static_cast<int>(cx - 8.5f * stageScale),
                    static_cast<int>(cy - 3.0f * stageScale),
                    std::max(4, static_cast<int>(17.0f * stageScale)),
                    std::max(2, static_cast<int>(6.0f * stageScale)), redCol);
      // Mini EKG glass display at bottom of placard
      const Area scr{pl.x + 5.0f * stageScale, pl.y + 30.0f * stageScale,
                     pl.w - 10.0f * stageScale, 15.0f * stageScale};
      DrawRectangleRec(Rectangle{scr.x, scr.y, scr.w, scr.h},
                       Blend(cardBg, ToRay(p.ink), 0.78f));
      DrawRectangleLinesEx(Rectangle{scr.x, scr.y, scr.w, scr.h}, 1.0f, edge);
      const float midY = scr.y + scr.h * 0.55f;
      const float phase = reduced ? 0.5f : std::fmod(tSec * 0.85f, 1.0f);
      Vector2 prev{scr.x + 2.0f * stageScale, midY};
      for (int i = 1; i <= 14; ++i) {
        const float u = static_cast<float>(i) / 14.0f;
        const float d = std::fabs(u - phase);
        float spike = 0.0f;
        if (d < 0.08f) {
          const float k = (u - phase) / 0.08f;
          spike = k < 0.0f ? 2.5f * (1.0f + k) : (k < 0.45f ? -5.0f * (1.0f - k * 2.2f) : 2.2f * (1.0f - (k - 0.45f) * 1.8f));
        }
        const Vector2 cur{scr.x + (2.0f + u * (pl.w / stageScale - 14.0f)) * stageScale,
                          midY + spike * stageScale};
        DrawLineEx(prev, cur, 1.3f * stageScale, ToRay(p.sage));
        prev = cur;
      }
    } else if (propName == "desk") {
      // Study terminal console left of the figure, staying cleanly clear of Locker 01
      const Area desk = propArea(298.0f, 218.0f, 52.0f, 60.0f);
      // Legs & tabletop
      DrawRectangle(static_cast<int>(desk.x + 4.0f * stageScale),
                    static_cast<int>(desk.y + 24.0f * stageScale),
                    std::max(2, static_cast<int>(4.0f * stageScale)),
                    std::max(4, static_cast<int>(36.0f * stageScale)), dark);
      DrawRectangle(static_cast<int>(desk.Right() - 8.0f * stageScale),
                    static_cast<int>(desk.y + 24.0f * stageScale),
                    std::max(2, static_cast<int>(4.0f * stageScale)),
                    std::max(4, static_cast<int>(36.0f * stageScale)), dark);
      PaintChamferBoxRay(Area{desk.x, desk.y + 20.0f * stageScale, desk.w, 6.0f * stageScale},
                         1.5f * stageScale, coffee, dark, edge);
      // Study terminal monitor angled on tabletop
      const Area mon{desk.x + 8.0f * stageScale, desk.y, 30.0f * stageScale, 19.0f * stageScale};
      PaintChamferBoxRay(mon, 2.0f * stageScale, cardHi, dark, edge);
      const Area glass{mon.x + 3.0f * stageScale, mon.y + 3.0f * stageScale,
                       mon.w - 6.0f * stageScale, mon.h - 6.0f * stageScale};
      DrawRectangleRec(Rectangle{glass.x, glass.y, glass.w, glass.h},
                       Blend(cardBg, ToRay(p.ink), 0.72f));
      const float pulse = reduced ? 0.8f : (0.5f + 0.5f * std::sin(tSec * 3.6f));
      DrawRectangleRec(Rectangle{glass.x + 3.0f * stageScale, glass.y + 3.0f * stageScale,
                                 14.0f * stageScale, 2.0f * stageScale},
                       Fade(ToRay(p.sage), 0.85f));
      DrawRectangleRec(Rectangle{glass.x + 3.0f * stageScale, glass.y + 7.0f * stageScale,
                                 10.0f * stageScale, 2.0f * stageScale},
                       Fade(ToRay(p.amber), 0.65f + 0.3f * pulse));
    } else if (propName == "crate") {
      // Compact gear transfer crate sitting cleanly between Locker 01 and figure
      const Area cr = propArea(307.0f, 240.0f, 46.0f, 38.0f);
      PaintChamferBoxRay(cr, 3.0f * stageScale, cardHi, edge, ToRay(p.border));
      DrawRectangleRec(Rectangle{cr.x + 2.0f * stageScale, cr.y + 8.0f * stageScale,
                                 cr.w - 4.0f * stageScale, 5.0f * stageScale},
                       Blend(cardBg, HexColor(scene.figureArt.empty() ? std::string() : scene.figureArt.front().cloth, ToRay(p.sage)), 0.75f));
      DrawLineEx(Vector2{cr.x + 2.0f * stageScale, cr.y + 13.0f * stageScale},
                 Vector2{cr.Right() - 2.0f * stageScale, cr.y + 13.0f * stageScale},
                 1.2f * stageScale, edge);
      DrawRectangleRec(Rectangle{cr.x + cr.w * 0.5f - 5.0f * stageScale, cr.y + 16.0f * stageScale,
                                 10.0f * stageScale, 4.0f * stageScale}, dark);
    } else if (propName == "mop") {
      // Lean broom/mop against the bare wall right of the door
      const float bx = hall.x + wallRightCenter * stageScale;
      const float by = hall.y + 276.0f * stageScale;
      DrawLineEx(Vector2{bx + 6.0f * stageScale, by - 76.0f * stageScale},
                 Vector2{bx - 5.0f * stageScale, by - 6.0f * stageScale},
                 2.6f * stageScale, coffee);
      PaintChamferBoxRay(Area{bx - 16.0f * stageScale, by - 8.0f * stageScale,
                              22.0f * stageScale, 7.0f * stageScale},
                         1.5f * stageScale, ToRay(p.sage), dark, edge);
    } else if (propName == "chart") {
      // Rolled navigation chart tube & plotting slate beside door
      const Area slate = propArea(wallRightCenter - 16.0f, 210.0f, 32.0f, 40.0f);
      PaintChamferBoxRay(slate, 2.5f * stageScale, cardBg, edge, ToRay(p.border));
      const float sx = slate.x + 5.0f * stageScale;
      const float sy = slate.y + 6.0f * stageScale;
      DrawRectangleRec(Rectangle{sx, sy, slate.w - 10.0f * stageScale, slate.h - 14.0f * stageScale},
                       Blend(cardBg, ToRay(p.ink), 0.75f));
      DrawLineEx(Vector2{sx + 4.0f * stageScale, sy + 18.0f * stageScale},
                 Vector2{sx + 17.0f * stageScale, sy + 6.0f * stageScale},
                 1.4f * stageScale, ToRay(p.amber));
      const float blip = reduced ? 0.85f : (0.45f + 0.55f * std::sin(tSec * 3.4f));
      DrawCircleV(Vector2{sx + 17.0f * stageScale, sy + 6.0f * stageScale},
                  2.1f * stageScale, Fade(ToRay(p.amber), blip));
    } else if (propName == "wrench") {
      // Heavy engineering wrench mounted cleanly beside door
      const float wx = hall.x + wallRightCenter * stageScale;
      const float wy = hall.y + 228.0f * stageScale;
      DrawLineEx(Vector2{wx, wy - 20.0f * stageScale}, Vector2{wx, wy + 24.0f * stageScale},
                 5.0f * stageScale, dark);
      DrawCircleV(Vector2{wx, wy - 24.0f * stageScale}, 8.5f * stageScale, dark);
      DrawCircleV(Vector2{wx, wy - 24.0f * stageScale}, 4.5f * stageScale, cardBg);
      DrawRectangle(static_cast<int>(wx - 2.5f * stageScale),
                    static_cast<int>(wy - 33.0f * stageScale),
                    std::max(2, static_cast<int>(5.0f * stageScale)),
                    std::max(3, static_cast<int>(8.0f * stageScale)), cardBg);
      DrawCircleV(Vector2{wx, wy + 24.0f * stageScale}, 5.0f * stageScale, coffee);
    } else if (propName == "tank") {
      // Miniature O2 filter canister with live sight-glass & tiny bubble
      const Area tk = propArea(wallRightCenter - 13.0f, 224.0f, 26.0f, 52.0f);
      DrawRectangle(static_cast<int>(tk.x + 8.0f * stageScale),
                    static_cast<int>(tk.y - 4.0f * stageScale),
                    std::max(3, static_cast<int>(10.0f * stageScale)),
                    std::max(2, static_cast<int>(5.0f * stageScale)), dark);
      PaintChamferBoxRay(tk, 3.5f * stageScale, Blend(cardBg, ToRay(p.sage), 0.55f),
                         dark, edge);
      const Area win{tk.x + 6.0f * stageScale, tk.y + 12.0f * stageScale,
                     tk.w - 12.0f * stageScale, 24.0f * stageScale};
      DrawRectangleRec(Rectangle{win.x, win.y, win.w, win.h}, cardBg);
      DrawRectangleLinesEx(Rectangle{win.x, win.y, win.w, win.h}, 1.0f, edge);
      for (int bi = 0; bi < 3; ++bi) {
        const float u = reduced ? (0.25f + 0.25f * static_cast<float>(bi))
                                : std::fmod(tSec * 0.42f + static_cast<float>(bi) * 0.33f, 1.0f);
        const float bx = win.x + (3.5f + static_cast<float>((bi * 3) % 7)) * stageScale;
        const float by = win.Bottom() - 3.0f * stageScale - u * (win.h - 6.0f * stageScale);
        DrawCircleV(Vector2{bx, by}, 1.5f * stageScale, Fade(ToRay(p.sage), 0.78f));
      }
    } else if (propName == "ladle") {
      // Galley utensil rack beside door
      const float lx = hall.x + wallRightCenter * stageScale;
      const float ly = hall.y + 220.0f * stageScale;
      DrawLineEx(Vector2{lx, ly - 18.0f * stageScale}, Vector2{lx, ly + 32.0f * stageScale},
                 3.2f * stageScale, dark);
      DrawCircleV(Vector2{lx, ly + 34.0f * stageScale}, 8.0f * stageScale, coffee);
      DrawCircleV(Vector2{lx, ly + 33.0f * stageScale}, 4.5f * stageScale, cardHi);
    } else if (propName == "zzz") {
      // Smoothly drifting typographic Z motifs floating clearly beside the right brow
      const char* glyphs[3] = {"z", "z", "Z"};
      const float baseX = hall.x + (kCrewFigureX + 41.0f) * stageScale + pose.headTurnX;
      const float baseY = hall.y + (kCrewDeckY - 138.0f) * stageScale + pose.headNodY * 0.5f;
      for (int zi = 0; zi < 3; ++zi) {
        const float phase = reduced ? (0.20f + 0.28f * static_cast<float>(zi))
                                    : std::fmod(tSec * 0.34f + static_cast<float>(zi) / 3.0f, 1.0f);
        const float alpha = reduced ? (0.86f - 0.16f * static_cast<float>(zi))
                                    : (SmoothStep01(0.0f, 0.22f, phase) *
                                       (1.0f - SmoothStep01(0.68f, 1.0f, phase)));
        const float zx = baseX + (static_cast<float>(zi) * 7.5f + 5.0f * std::sin(phase * 6.28318f + static_cast<float>(zi))) * stageScale;
        const float zy = baseY - (phase * 40.0f + static_cast<float>(zi) * 5.0f) * stageScale;
        const int sz = std::max(8, static_cast<int>(static_cast<float>(smallPx) * (0.74f + 0.24f * phase)));
        DrawTextEx(textFont, glyphs[zi], Vector2{zx, zy}, static_cast<float>(sz), 0.5f,
                   Fade(dark, std::clamp(alpha, 0.0f, 0.94f)));
      }
    }
  }
  (void)doorX;
}
}  // namespace

void ShellView::PaintQuartersScreen(Simulation& sim) {
  navHits_.clear();
  taskButtons_.clear();
  const auto& p = theme_.palette;
  const auto& screen = quarters_.screen;
  const float left = layout_.content.x + 8;
  const float right = layout_.content.Right() - 8;
  const float column = 240;
  const float line = theme_.LineHeight();
  const float panelTop = layout_.content.y + PaintTaskHeader(screen.head, left, layout_.content.y) + 8;
  auto wrapped = [&](const std::string& value, float x, float& y, float width, Color color) {
    for (const auto& row : WrapText(value, width, TypePx(10), SelectFont(TypePx(10), false))) {
      Text(row.c_str(), static_cast<int>(x), static_cast<int>(y), TypePx(10), color);
      y += line;
    }
  };
  // Payroll and roster are separate in paintRoster(). Do not paint the full
  // roster as a table and then overlay clickable copies over its payroll rows.
  // The stage is laid out for one reading size and grows with the size in use.
  const float stageScale = StageScale(screen.stage, theme_);
  const Area hall{(left + right - screen.stage.width * stageScale) / 2, panelTop,
                  screen.stage.width * stageScale, screen.stage.height * stageScale};
  const bool stacked = hall.x < left + column + 10 || hall.Right() > right - column - 10;
  float rowY = stacked ? hall.Bottom() + 10 : panelTop;
  if (!screen.left.empty()) {
    auto payroll = screen.left.front();
    if (payroll.lines.size() > 4) { payroll.lines.resize(4); }
    const float h = MeasurePanelHeight(payroll, column);
    PaintPanel(payroll, {left, rowY, column, h});
    rowY += h + 6;
  }
  for (const auto& row : quarters_.roster) {
    const float nameH = WrapText(row.name, column - 26, TypePx(10), SelectFont(TypePx(10), false)).size() * line;
    const float roleH = WrapText(row.role, column - 26, TypePx(10), SelectFont(TypePx(10), false)).size() * line;
    const float h = nameH + roleH + (row.flag.empty() ? 0 : line) + 14;
    const Area box{left, rowY, column, h};
    PaintChamferBoxRay(box, kChamferCard, ToRay(row.selected ? p.panel3 : p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
    float y = rowY + 7;
    const auto& color = row.colour;
    if (color.size() == 7 && color[0] == '#') {
      const unsigned rgb = static_cast<unsigned>(std::stoul(color.substr(1), nullptr, 16));
      DrawRectangle(static_cast<int>(left + 8), static_cast<int>(rowY + 13), 4, 4,
                    Color{static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8), static_cast<unsigned char>(rgb), 255});
    }
    wrapped(row.name, left + 15, y, column - 26, ToRay(p.ink));
    wrapped(row.role, left + 15, y, column - 26, ToRay(p.muted));
    if (!row.flag.empty()) { wrapped(row.flag, left + 15, y, column - 26, ToRay(p.terracotta)); }
    navHits_.push_back({box, "select", row.id});
    rowY += h + 3;
  }
  const Color hallBg =
      theme_.id == "classic" ? HexColor("#fdfbf7", ToRay(p.panel)) : ToRay(p.chrome);
  PaintChamferBoxRay(hall, kChamferPanel, hallBg, ToRay(p.borderDark), ToRay(p.border));
  BeginScissorMode(static_cast<int>(hall.x + kDoubleInset),
                   static_cast<int>(hall.y + kDoubleInset - contentScroll_),
                   std::max(0, static_cast<int>(hall.w - 2.0f * kDoubleInset)),
                   std::max(0, static_cast<int>(hall.h - 2.0f * kDoubleInset)));
  MachinePart grown;
  for (const MachinePart& drawn : quarters_.hall) {
    if (quarters_.hasCrew && drawn.cls.find("cq-shadow") != std::string::npos) { continue; }
    if (stageScale != 1.0f) {
      grown = drawn;
      grown.box = Area{drawn.box.x * stageScale, drawn.box.y * stageScale,
                       drawn.box.w * stageScale, drawn.box.h * stageScale};
    }
    const MachinePart& part = stageScale != 1.0f ? grown : drawn;
    if (PaintScenePart(part, hall)) continue;
    const int x = static_cast<int>(hall.x + part.box.x);
    const int y = static_cast<int>(hall.y + part.box.y);
    const int w = std::max(1, static_cast<int>(part.box.w));
    const int h = std::max(1, static_cast<int>(part.box.h));
    Color ink = ToRay(p.panel3);
    if (part.cls.find("cq-wall") != std::string::npos) { ink = ToRay(p.panel); }
    if (part.cls.find("cq-floor") != std::string::npos) { ink = ToRay(p.coffeeLight); }
    if (part.cls.find("cq-light") != std::string::npos) { ink = ToRay(p.white); }
    if (part.cls.find("cq-door") != std::string::npos) { ink = ToRay(p.coffeeDark); }
    if (part.cls.find("cq-lock") != std::string::npos) { ink = ToRay(p.coffee); }
    DrawRectangle(x, y, w, h, ink);
    if (!part.text.empty()) {
      Text(part.text.c_str(), x + 2, y + 1, TypePx(8) , ToRay(p.muted));
    }
  }

  // The portrait card: the selected hand in front of their roster colour, with
  // name and role on a plate across the foot of the picture.
  {
    const Area card{hall.x + quarters_.card.x * stageScale, hall.y + quarters_.card.y * stageScale,
                    quarters_.card.w * stageScale, quarters_.card.h * stageScale};
    const Color edge = ToRay(p.borderDark);
    const int size = TypePx(10);
    const float pad = 8.0f * stageScale;
    const float textW = std::max(1.0f, card.w - 2.0f * pad);
    DrawRectangle(static_cast<int>(card.x), static_cast<int>(card.y), static_cast<int>(card.w),
                  static_cast<int>(card.h), Blend(hallBg, HexColor(quarters_.cardColour, hallBg), 0.38f));
    const Texture2D* portrait = CrewTexture(crewArt_, quarters_.scene.portrait);
    if (portrait != nullptr) {
      // The whole picture, as large as the card takes it, standing on the lower edge.
      const float fit = std::min(card.w / static_cast<float>(portrait->width),
                                 card.h / static_cast<float>(portrait->height));
      const float w = static_cast<float>(portrait->width) * fit;
      const float h = static_cast<float>(portrait->height) * fit;
      if (displayAdjustmentAvailable_) {
        // Keep RGB source-over, but remove artwork coverage from the hue mask.
        rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                                  RL_ZERO, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
      }
      DrawTexturePro(*portrait,
                     Rectangle{0, 0, static_cast<float>(portrait->width),
                               static_cast<float>(portrait->height)},
                     Rectangle{std::round(card.x + (card.w - w) / 2.0f),
                               std::round(card.Bottom() - h), w, h},
                     Vector2{0, 0}, 0.0f, WHITE);
      if (displayAdjustmentAvailable_) {
        rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                                  RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
      }
    } else {
      const std::string none = quarters_.hasCrew ? "No portrait on file." : "No crew aboard.";
      float ty = card.y + card.h * 0.36f;
      for (const auto& row : WrapText(none, textW, size, SelectFont(size, false))) {
        Text(row.c_str(), static_cast<int>(card.x + (card.w - TextWidth(row.c_str(), size)) / 2.0f),
             static_cast<int>(ty), size, ToRay(p.muted));
        ty += line;
      }
    }
    if (quarters_.hasCrew) {
      const auto nameRows = WrapText(quarters_.cardName, textW, size, SelectFont(size, false));
      const auto roleRows = WrapText(quarters_.cardRole, textW, size, SelectFont(size, false));
      const float plateH = static_cast<float>(nameRows.size() + roleRows.size()) * line + pad;
      const float plateY = card.Bottom() - plateH;
      DrawRectangle(static_cast<int>(card.x), static_cast<int>(plateY), static_cast<int>(card.w),
                    static_cast<int>(plateH) + 1, Fade(hallBg, 0.9f));
      DrawRectangle(static_cast<int>(card.x), static_cast<int>(plateY), static_cast<int>(card.w), 1, edge);
      float ty = plateY + pad / 2.0f;
      for (const auto& row : nameRows) {
        Text(row.c_str(), static_cast<int>(card.x + pad), static_cast<int>(ty), size, ToRay(p.ink));
        ty += line;
      }
      for (const auto& row : roleRows) {
        Text(row.c_str(), static_cast<int>(card.x + pad), static_cast<int>(ty), size, ToRay(p.muted));
        ty += line;
      }
    }
    DrawRectangleLinesEx(Rectangle{card.x, card.y, card.w, card.h}, 2.0f, edge);
  }

  // Evaluate crew persona pose & corridor atmospheric fx
  const CrewScene& scene = quarters_.scene;
  const ScenePicture* shown = nullptr;
  ScenePicture medicalStandFallback{};
  const Texture2D* figure = nullptr;
  for (const ScenePicture& candidate : scene.figureArt) {
    figure = CrewTexture(crewArt_, candidate.name);
    if (figure != nullptr) { shown = &candidate; break; }
  }
  if (figure == nullptr && scene.pose == "lie" && !scene.figureArt.empty()) {
    const ScenePicture& orig = scene.figureArt.front();
    const std::size_t pos = orig.name.rfind("-lie");
    if (pos != std::string::npos) {
      medicalStandFallback = orig;
      medicalStandFallback.name = orig.name.substr(0, pos) + "-stand";
      figure = CrewTexture(crewArt_, medicalStandFallback.name);
      if (figure != nullptr) { shown = &medicalStandFallback; }
    }
  }
  const CrewMember* selectedCrew = quarters_.hasCrew ? sim.FindCrew(quarters_.selectedId) : nullptr;
  const Area talkBtn{hall.x + quarters_.talk.x * stageScale, hall.y + quarters_.talk.y * stageScale,
                     quarters_.talk.w * stageScale, quarters_.talk.h * stageScale};
  const Area figureHoverBox{hall.x + (kCrewFigureX - 42.0f) * stageScale,
                            hall.y + (kCrewDeckY - 165.0f) * stageScale,
                            84.0f * stageScale, 170.0f * stageScale};
  const bool figureHovered = quarters_.hasCrew &&
                             (figureHoverBox.Contains(mouseX_, mouseY_ + contentScroll_) ||
                              (quarters_.conversation.enabled &&
                               talkBtn.Contains(mouseX_, mouseY_ + contentScroll_)));
  const float tSec = static_cast<float>(frameTimeMs_ * 0.001);
  const ChibiPose pose = ComputeChibiPose(sim, selectedCrew, scene, quarters_.selectedId,
                                          !quarters_.said.empty(), figureHovered, tSec, stageScale);

  PaintQuartersCorridorFx(sim, p, hall, stageScale, scene, quarters_.hasCrew, pose, tSec,
                          contentScroll_);

  if (figure != nullptr) {
    // Crisp vector schematic corridor props replace blocky sprite-table placeholders
    PaintQuartersSchematicProps(sim, p, hall, stageScale, scene, pose, tSec,
                                SelectFont(TypePx(10), true), TypePx(10));
    // Also render any unrecognised custom props below figureFrom if present
    const std::size_t cells = std::min(scene.figureFrom, scene.pixels.size());
    bool hasCustomProp = false;
    for (const std::string& propName : scene.propNames) {
      if (!IsVectorCrewProp(propName)) { hasCustomProp = true; break; }
    }
    if (hasCustomProp) {
      for (std::size_t i = 0; i < cells; ++i) {
        const Area& box = scene.pixels[i].box;
        const int x0 = static_cast<int>(std::floor(hall.x + box.x * stageScale));
        const int y0 = static_cast<int>(std::floor(hall.y + box.y * stageScale));
        const int x1 = static_cast<int>(std::floor(hall.x + (box.x + box.w) * stageScale));
        const int y1 = static_cast<int>(std::floor(hall.y + (box.y + box.h) * stageScale));
        DrawRectangle(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0),
                      HexColor(scene.pixels[i].colour, ToRay(p.coffee)));
      }
    }

    if (pose.isMedical) {
      // Medical bay headboard + pillow behind the patient chibi
      const float cotL = hall.x + (kCrewFigureX - 46.0f) * stageScale;
      const float cotW = 92.0f * stageScale;
      const Color medEdge = ToRay(p.borderDark);
      const Color frameCol = ToRay(p.coffeeDark);
      PaintChamferBoxRay(Area{cotL, hall.y + 190.0f * stageScale, 12.0f * stageScale, 88.0f * stageScale},
                         2.5f * stageScale, frameCol, medEdge, ToRay(p.border));
      PaintChamferBoxRay(Area{cotL + cotW - 10.0f * stageScale, hall.y + 220.0f * stageScale,
                              10.0f * stageScale, 58.0f * stageScale},
                         2.0f * stageScale, frameCol, medEdge, ToRay(p.border));
      // Soft pillow propped against the headboard behind head/shoulders
      PaintChamferBoxRay(Area{cotL + 10.0f * stageScale, hall.y + 194.0f * stageScale,
                              32.0f * stageScale, 32.0f * stageScale},
                         6.0f * stageScale, ToRay(p.white), medEdge, ToRay(p.border));
    }

    // 2.5D articulated chibi render: uniform layer underneath tinted by job color,
    // then character ink & features seamlessly stitched on top.
    ScenePicture drawShown = *shown;
    if (pose.isMedical) { drawShown.base -= 10.0; }
    const Texture2D* cloth = CrewTexture(crewArt_, shown->name + "-cloth");
    if (displayAdjustmentAvailable_) {
      rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                                RL_ZERO, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
      BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    }
    if (cloth != nullptr) {
      PaintRiggedChibi(*cloth, drawShown, hall, stageScale, pose,
                       HexColor(shown->cloth, Color{162, 148, 127, 255}));
    }
    PaintRiggedChibi(*figure, drawShown, hall, stageScale, pose, WHITE);
    if (displayAdjustmentAvailable_) {
      rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                                RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
      BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    }

    if (pose.isMedical) {
      // Medical mattress & folded recovery blanket tucked cleanly over waist & legs
      const float cotL = hall.x + (kCrewFigureX - 44.0f) * stageScale;
      const float cotW = 88.0f * stageScale;
      const Color medEdge = ToRay(p.borderDark);
      PaintChamferBoxRay(Area{cotL + 2.0f * stageScale, hall.y + 226.0f * stageScale,
                              cotW - 4.0f * stageScale, 42.0f * stageScale},
                         4.0f * stageScale, Blend(ToRay(p.panel), ToRay(p.sage), 0.34f),
                         medEdge, ToRay(p.border));
      // Folded crisp white top cuff of the blanket
      DrawRectangleRec(Rectangle{cotL + 4.0f * stageScale, hall.y + 226.0f * stageScale,
                                 cotW - 8.0f * stageScale, 7.0f * stageScale},
                       ToRay(p.white));
      DrawLineEx(Vector2{cotL + 4.0f * stageScale, hall.y + 233.0f * stageScale},
                 Vector2{cotL + cotW - 4.0f * stageScale, hall.y + 233.0f * stageScale},
                 1.2f * stageScale, medEdge);
      // Bed frame & rolling legs down to deck
      DrawRectangleRec(Rectangle{cotL + 4.0f * stageScale, hall.y + 266.0f * stageScale,
                                 cotW - 8.0f * stageScale, 4.0f * stageScale},
                       ToRay(p.coffeeDark));
      DrawRectangleRec(Rectangle{cotL + 6.0f * stageScale, hall.y + 270.0f * stageScale,
                                 5.0f * stageScale, 8.0f * stageScale},
                       ToRay(p.coffeeDark));
      DrawRectangleRec(Rectangle{cotL + cotW - 11.0f * stageScale, hall.y + 270.0f * stageScale,
                                 5.0f * stageScale, 8.0f * stageScale},
                       ToRay(p.coffeeDark));
    }
  } else {
    // Procedural pixel-grid crew fallback with gentle idle breathing & sway
    const std::size_t cells = scene.pixels.size();
    for (std::size_t i = 0; i < cells; ++i) {
      const bool isBody = (i >= scene.figureFrom);
      const float dx = isBody ? pose.hipShiftX * 0.45f : 0.0f;
      const float dy = isBody ? (pose.breathLiftY * 0.7f + pose.hipDipY * 0.35f) : 0.0f;
      const Area& box = scene.pixels[i].box;
      const int x0 = static_cast<int>(std::floor(hall.x + box.x * stageScale + dx));
      const int y0 = static_cast<int>(std::floor(hall.y + box.y * stageScale + dy));
      const int x1 = static_cast<int>(std::floor(hall.x + (box.x + box.w) * stageScale + dx));
      const int y1 = static_cast<int>(std::floor(hall.y + (box.y + box.h) * stageScale + dy));
      DrawRectangle(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0),
                    HexColor(scene.pixels[i].colour, ToRay(p.coffee)));
    }
  }
  if (quarters_.hasCrew) {
    // The speech bubble beside the figure: the conversation control.
    const bool enabled = quarters_.conversation.enabled;
    const bool hovered = enabled && talkBtn.Contains(mouseX_, mouseY_ + contentScroll_);
    const Color btnBg = ToRay(hovered || !enabled ? p.panel3 : p.panel);
    PaintChamferBoxRay(talkBtn, kChamferSmall, btnBg,
                       ToRay(enabled ? p.borderDark : p.border), ToRay(p.border));
    const Color mark = ToRay(enabled ? p.ink : p.muted);
    // A 20x18 bubble with three animated waiting/speaking dots
    const int bx = static_cast<int>(talkBtn.x + (talkBtn.w - 20.0f) / 2.0f);
    const int by = static_cast<int>(talkBtn.y + (talkBtn.h - 18.0f) / 2.0f);
    DrawRectangleLines(bx, by, 20, 14, mark);
    DrawRectangle(bx + 2, by + 13, 4, 4, btnBg);
    DrawRectangle(bx + 2, by + 14, 2, 4, mark);
    DrawRectangle(bx + 4, by + 14, 2, 2, mark);
    for (int di = 0; di < 3; ++di) {
      const float wave = (enabled && !sim.State().reducedMotion)
                             ? std::sin(tSec * (hovered || !quarters_.said.empty() ? 5.2f : 2.4f) -
                                        static_cast<float>(di) * 0.9f)
                             : 0.0f;
      const int lift = wave > 0.35f ? -1 : 0;
      const Color dotCol = (hovered && wave > 0.35f) ? ToRay(p.sage) : mark;
      DrawRectangle(bx + 5 + di * 4, by + 6 + lift, 2, 2, dotCol);
    }
    if (enabled) { navHits_.push_back({talkBtn, "talk", ""}); }
  }
  EndScissorMode();
  MaskChamferInnerCornersRay(hall, kChamferPanel, hallBg);
  PaintChamferDoubleLinesRay(hall, kChamferPanel, ToRay(p.borderDark), ToRay(p.border));


  // Orders retain the HTA dropdown and every note/cost. At narrow widths the
  // whole column moves below the hall instead of painting through its figure.
  const float orderX = right - column;
  float oy = stacked ? hall.Bottom() + 10 : panelTop;
  for (const auto& panel : screen.right) {
    const float h = MeasurePanelHeight(panel, column);
    PaintPanel(panel, {orderX, oy, column, h});
    oy += h + 6;
    if (panel.id != "cq-orders" || !quarters_.hasCrew) { continue; }
    // A real select, not an invisible/unwired label. Only this small overlay
    // owns ImGui input; the rest of the screen uses the measured Raylib hits.
    const float comboH = line + 12;
    const float screenY = oy - contentScroll_;
    if (screenY >= layout_.content.y && screenY + comboH < actionBandTop_) {
      ImGui::SetNextWindowPos(ImVec2(orderX, screenY), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(column, comboH), ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      ImGui::Begin("##quarters-job", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
      std::string preview;
      for (const auto& option : quarters_.jobOptions) {
        const auto split = option.find('\t');
        if (option.substr(0, split) == quarters_.selectedJob) { preview = option.substr(split + 1); }
      }
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##standing-job", preview.c_str())) {
        for (const auto& option : quarters_.jobOptions) {
          const auto split = option.find('\t');
          const auto id = option.substr(0, split);
          const auto label = option.substr(split + 1);
          if (ImGui::Selectable(label.c_str(), id == quarters_.selectedJob)) { PressQuartersAction(sim, "job", id); }
        }
        ImGui::EndCombo();
      }
      ImGui::End(); ImGui::PopStyleVar();
    }
    oy += comboH + 6;
    for (const auto& order : quarters_.orders) {
      const auto label = std::string(order.on ? "[x] " : "[ ] ") + order.label;
      const float h = (WrapText(label, column - 20, TypePx(10), SelectFont(TypePx(10), false)).size() +
          WrapText(order.note, column - 20, TypePx(10), SelectFont(TypePx(10), false)).size() +
          WrapText(order.cost, column - 20, TypePx(10), SelectFont(TypePx(10), false)).size()) * line + 16;
      const Area box{orderX, oy, column, h};
      PaintChamferBoxRay(box, kChamferCard, ToRay(order.on ? p.panel3 : p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
      float y = oy + 8;
      wrapped(label, orderX + 10, y, column - 20, ToRay(order.locked ? p.muted : p.ink));
      wrapped(order.note, orderX + 10, y, column - 20, ToRay(p.muted));
      wrapped(order.cost, orderX + 10, y, column - 20, ToRay(p.coffee));
      if (!order.locked) { navHits_.push_back({box, "order", order.id}); }
      oy += h + 4;
    }
  }
  float uy = stacked ? std::max(rowY, oy) + 10 : hall.Bottom() + 10;
  if (quarters_.hasCrew) {
    const std::string lineText =
        !quarters_.conversation.enabled && quarters_.said.empty()
            ? (quarters_.conversation.title.empty() ? "Crew member is in the medical bay."
                                                    : quarters_.conversation.title)
            : (quarters_.said.empty()
                   ? quarters_.conversation.label + " (click the speech bubble beside them)."
                   : "\"" + quarters_.said + "\"");
    const auto speechRows =
        WrapText(lineText, hall.w - 20, TypePx(10), SelectFont(TypePx(10), false));
    const float speechH = static_cast<float>(speechRows.size()) * line + 16.0f;
    PaintChamferBoxRay(Area{hall.x, uy, hall.w, speechH}, kChamferCard, ToRay(p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
    float sy = uy + 8.0f;
    const Color speechColor =
        ToRay(quarters_.said.empty() ? p.muted : p.ink);
    for (const auto& text : speechRows) {
      Text(text.c_str(), static_cast<int>(hall.x + 10), static_cast<int>(sy), TypePx(10), speechColor);
      sy += line;
    }
    uy += speechH + 6.0f;
  }
  for (const auto& panel : screen.under) {
    const float h = MeasurePanelHeight(panel, hall.w);
    PaintPanel(panel, {hall.x, uy, hall.w, h}); uy += h + 6;
  }
  contentPaintBottom_ = std::max(contentPaintBottom_, std::max({rowY, oy, uy}) + 10);
}

void ShellView::PaintCargoScreen(Simulation& sim) {
  navHits_.clear();
  taskButtons_.clear();
  const CargoRoomModel& cargo = cargoModel_;
  const Palette& p = theme_.palette;

  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float headerH = PaintTaskHeader(cargo.head, left, layout_.content.y);
  const float top = layout_.content.y + headerH + 8.0f;
  const float gap = 10.0f;
  const float totalW = std::max(360.0f, right - left);
  const float leftW = std::floor((totalW - 2.0f * gap) * 0.27f);
  const float midW = std::floor((totalW - 2.0f * gap) * 0.43f);
  const float rightW = std::max(140.0f, totalW - 2.0f * gap - leftW - midW);
  const float leftX = left;
  const float midX = leftX + leftW + gap;
  const float rightX = midX + midW + gap;

  const int titlePx = TypePx(11);
  const int bodyPx = TypePx(10);
  const int smallPx = TypePx(9);
  const int microPx = TypePx(8);
  const float line = theme_.LineHeight();
  const float smallLine = std::max(13.0f, line - 2.0f);

  // Measure left inventory cards so the three column panels share an even floor.
  struct CardMeasure {
    std::vector<std::string> titleLines;
    std::vector<std::string> noteLines;
    std::string volText;
    std::string statusText;
    std::string qtyText;
    float height = 0.0f;
  };
  const float cardW = std::max(80.0f, leftW - 16.0f);
  std::vector<CardMeasure> cards;
  cards.reserve(cargo.lots.size());
  float leftContentH = TypeStep(28.0f) + smallLine + 8.0f;
  if (cargo.lots.empty()) {
    leftContentH += line + 16.0f;
  } else {
    for (const CargoLotView& lot : cargo.lots) {
      CardMeasure cm;
      cm.volText = js::NumToString(lot.vol) + " m3";
      const float volW = TextWidth(cm.volText.c_str(), bodyPx);
      cm.titleLines = WrapText(lot.name, std::max(40.0f, cardW - 26.0f - volW),
                               bodyPx, SelectFont(bodyPx, false));
      std::string sub = lot.note;
      if (!lot.detail.empty()) {
        sub += sub.empty() ? lot.detail : (" - " + lot.detail);
      }
      cm.noteLines = WrapText(sub, std::max(40.0f, cardW - 24.0f),
                              smallPx, SelectFont(smallPx, false));
      cm.statusText = lot.stowed ? "Sorted" : std::string();
      cm.qtyText = "QTY: " + std::to_string(lot.qty);
      cm.height = 8.0f + static_cast<float>(cm.titleLines.size()) * line + 2.0f +
                  static_cast<float>(cm.noteLines.size()) * smallLine + 6.0f + smallLine + 8.0f;
      cards.push_back(std::move(cm));
      leftContentH += cards.back().height + 6.0f;
    }
  }

  // Measure middle hold section height.
  const float bayH = std::clamp(midW * 0.68f, 236.0f, 336.0f);
  const float midContentH =
      TypeStep(28.0f) + 6.0f + bayH + 14.0f + line + 20.0f + smallLine + 14.0f;

  // Measure right context panel height.
  const auto sideNotes = WrapText(cargo.sideNote, std::max(40.0f, rightW - 22.0f),
                                  smallPx, SelectFont(smallPx, false));
  float rightContentH = TypeStep(30.0f) + static_cast<float>(sideNotes.size()) * smallLine + 10.0f;
  if (cargo.runTabs.size() > 1) {
    rightContentH += static_cast<float>(cargo.runTabs.size()) * (smallLine + 12.0f) + 6.0f;
  }
  if (!cargo.contextDetails.empty()) {
    float detH = 14.0f;
    for (const std::string& d : cargo.contextDetails) {
      detH += static_cast<float>(WrapText(d, std::max(40.0f, rightW - 40.0f),
                                          smallPx, SelectFont(smallPx, false)).size()) *
              smallLine;
    }
    rightContentH += detH + 8.0f;
  }
  struct SideBtnMeasure {
    std::vector<std::string> labelLines;
    std::vector<std::string> noteLines;
    float height = 0.0f;
  };
  std::vector<SideBtnMeasure> sideBtns;
  sideBtns.reserve(cargo.sideRows.size());
  const float btnW = std::max(80.0f, rightW - 20.0f);
  for (const CargoSideRow& row : cargo.sideRows) {
    SideBtnMeasure bm;
    bm.labelLines = WrapText(row.label, std::max(40.0f, btnW - 20.0f),
                             bodyPx, SelectFont(bodyPx, false));
    if (!row.note.empty()) {
      bm.noteLines = WrapText(row.note, std::max(40.0f, btnW - 20.0f),
                              smallPx, SelectFont(smallPx, false));
    }
    bm.height = 8.0f + static_cast<float>(bm.labelLines.size()) * line +
                (bm.noteLines.empty() ? 0.0f
                                      : 2.0f + static_cast<float>(bm.noteLines.size()) * smallLine) +
                8.0f;
    sideBtns.push_back(std::move(bm));
    rightContentH += sideBtns.back().height + 6.0f;
  }
  rightContentH += 10.0f;

  const float viewportH = std::max(340.0f, layout_.content.Bottom() - top - 14.0f);
  const float panelH = std::max({viewportH, leftContentH, midContentH, rightContentH});

  // Paint outer column frames.
  const Area leftBox{leftX, top, leftW, panelH};
  const Area midBox{midX, top, midW, panelH};
  const Area rightBox{rightX, top, rightW, panelH};
  for (const Area& col : {leftBox, midBox, rightBox}) {
    PaintChamferBoxRay(col, kChamferPanel, ToRay(p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
  }

  // ---- Left column: Cargo Inventory cards ---------------------------------
  Text("Cargo Inventory", static_cast<int>(leftX + 11.0f), static_cast<int>(top + 9.0f),
       titlePx, ToRay(p.coffee));
  float ly = top + TypeStep(28.0f) + 4.0f;
  if (cargo.lots.empty()) {
    Text("The hold is empty.", static_cast<int>(leftX + 11.0f), static_cast<int>(ly),
         bodyPx, ToRay(p.muted));
  } else {
    for (std::size_t i = 0; i < cargo.lots.size(); ++i) {
      const CargoLotView& lot = cargo.lots[i];
      const CardMeasure& cm = cards[i];
      const Area cardBox{leftX + 8.0f, ly, cardW, cm.height};

      const ::Color cardBg = lot.stowed
                                 ? ToRay(p.panel3)
                                 : (lot.selected
                                        ? (theme_.id == "classic"
                                               ? ::Color{233, 244, 219, 255}
                                               : Fade(::Color{126, 198, 63, 255}, 0.18f))
                                        : ToRay(p.panel2));
      const ::Color cardEdge = lot.stowed
                                   ? ToRay(p.border)
                                   : (lot.selected ? ::Color{100, 165, 47, 255}
                                                   : ToRay(p.borderDark));
      const ::Color cardInner = (lot.selected && !lot.stowed)
                                    ? ::Color{126, 198, 63, 255}
                                    : ToRay(p.border);
      PaintChamferBoxRay(cardBox, kChamferCard, cardBg, cardEdge, cardInner);
      if (lot.selected && !lot.stowed) {
        DrawRectangle(static_cast<int>(cardBox.x + 5.0f), static_cast<int>(cardBox.y + 6.0f), 3,
                      static_cast<int>(std::max(1.0f, cardBox.h - 12.0f)),
                      ::Color{126, 198, 63, 255});
      }

      const ::Color titleInk = lot.stowed ? ToRay(p.muted) : ToRay(p.ink);
      const ::Color subInk = ToRay(p.muted);
      const float volW = TextWidth(cm.volText.c_str(), bodyPx);
      Text(cm.volText.c_str(), static_cast<int>(cardBox.Right() - 10.0f - volW),
           static_cast<int>(cardBox.y + 8.0f), bodyPx, titleInk);

      float cy = cardBox.y + 8.0f;
      for (const std::string& row : cm.titleLines) {
        Text(row.c_str(), static_cast<int>(cardBox.x + 10.0f), static_cast<int>(cy),
             bodyPx, titleInk);
        cy += line;
      }
      cy += 2.0f;
      for (const std::string& row : cm.noteLines) {
        Text(row.c_str(), static_cast<int>(cardBox.x + 10.0f), static_cast<int>(cy),
             smallPx, subInk);
        cy += smallLine;
      }
      cy += 5.0f;
      if (!cm.statusText.empty()) {
        Text(cm.statusText.c_str(), static_cast<int>(cardBox.x + 10.0f), static_cast<int>(cy),
             microPx, subInk);
      }
      const float qtyW = TextWidth(cm.qtyText.c_str(), microPx);
      Text(cm.qtyText.c_str(), static_cast<int>(cardBox.Right() - 10.0f - qtyW),
           static_cast<int>(cy), microPx, titleInk);

      if (!lot.stowed) {
        navHits_.push_back(NavHit{cardBox, "row", "cargo-select:" + lot.id});
      }
      ly += cm.height + 6.0f;
    }
  }

  // ---- Middle column: 3D Visual Cargo Hold + Capacity Bar -----------------
  Text("Cargo Hold View", static_cast<int>(midX + 11.0f), static_cast<int>(top + 9.0f),
       titlePx, ToRay(p.coffee));
  const std::string shipCapTag =
      cargo.shipLabel + " - " + js::NumToString(cargo.capacity) + " m3";
  const float tagW = TextWidth(shipCapTag.c_str(), smallPx);
  Text(shipCapTag.c_str(), static_cast<int>(midX + midW - 11.0f - tagW),
       static_cast<int>(top + 11.0f), smallPx, ToRay(p.muted));

  const float my = top + TypeStep(28.0f) + 4.0f;
  const Area bayOuter{midX + 10.0f, my, midW - 20.0f, bayH};
  const bool classic = theme_.id == "classic";
  const ::Color wallTopCol = classic ? ::Color{252, 249, 243, 255} : ToRay(p.content);
  const ::Color wallSideCol = classic ? ::Color{244, 238, 227, 255} : ToRay(p.panel2);
  const ::Color wallFloorCol = classic ? ::Color{240, 232, 217, 255} : ToRay(p.panel3);

  auto drawTriCCW = [](Vector2 a, Vector2 b, Vector2 c, ::Color color) {
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0.0f) {
      DrawTriangle(a, c, b, color);
    } else {
      DrawTriangle(a, b, c, color);
    }
  };
  auto fillQuad = [&](Vector2 q0, Vector2 q1, Vector2 q2, Vector2 q3, ::Color color) {
    drawTriCCW(q0, q1, q2, color);
    drawTriCCW(q0, q2, q3, color);
  };

  // Scale inner back-wall footprint by ship capacity while keeping crisp 3D
  // ceiling, side walls, and floor perspective.
  float wallScaleW = 0.80f;
  float wallScaleH = 0.77f;
  int bays = 2;
  int gridCols = 6;
  int gridRows = 4;
  if (cargo.capacity > 95.0) {
    wallScaleW = 0.94f; wallScaleH = 0.85f; bays = 4; gridCols = 12; gridRows = 6;
  } else if (cargo.capacity > 70.0) {
    wallScaleW = 0.91f; wallScaleH = 0.83f; bays = 3; gridCols = 12; gridRows = 5;
  } else if (cargo.capacity > 45.0) {
    wallScaleW = 0.88f; wallScaleH = 0.81f; bays = 2; gridCols = 10; gridRows = 5;
  } else if (cargo.capacity > 25.0) {
    wallScaleW = 0.84f; wallScaleH = 0.79f; bays = 2; gridCols = 8; gridRows = 4;
  }
  const float backW = std::floor(bayOuter.w * wallScaleW);
  const float backH = std::floor(bayOuter.h * wallScaleH);
  const float sideDrop = std::floor((bayOuter.w - backW) * 0.5f);
  const float topDrop = std::max(8.0f, std::floor((bayOuter.h - backH) * 0.28f));
  const Area backWall{bayOuter.x + sideDrop, bayOuter.y + topDrop, backW, backH};
  const float floorDepth = std::max(14.0f, bayOuter.Bottom() - backWall.Bottom());

  const Vector2 oTL{bayOuter.x, bayOuter.y};
  const Vector2 oTR{bayOuter.Right(), bayOuter.y};
  const Vector2 oBR{bayOuter.Right(), bayOuter.Bottom()};
  const Vector2 oBL{bayOuter.x, bayOuter.Bottom()};
  const Vector2 bTL{backWall.x, backWall.y};
  const Vector2 bTR{backWall.Right(), backWall.y};
  const Vector2 bBR{backWall.Right(), backWall.Bottom()};
  const Vector2 bBL{backWall.x, backWall.Bottom()};

  fillQuad(oTL, oTR, bTR, bTL, wallTopCol);
  fillQuad(oTL, bTL, bBL, oBL, wallSideCol);
  fillQuad(bTR, oTR, oBR, bBR, wallSideCol);
  fillQuad(bBL, bBR, oBR, oBL, wallFloorCol);

  DrawRectangle(static_cast<int>(backWall.x), static_cast<int>(backWall.y),
                static_cast<int>(backWall.w), static_cast<int>(backWall.h), ToRay(p.panel));

  // Back-wall grid and vertical bulkhead seam(s).
  for (int c = 1; c < gridCols; ++c) {
    const float gx = backWall.x + backWall.w * static_cast<float>(c) / static_cast<float>(gridCols);
    DrawLine(static_cast<int>(gx), static_cast<int>(backWall.y + 1.0f),
             static_cast<int>(gx), static_cast<int>(backWall.Bottom() - 1.0f),
             Fade(ToRay(p.border), 0.48f));
    const float fgx =
        bayOuter.x + bayOuter.w * static_cast<float>(c) / static_cast<float>(gridCols);
    DrawLineEx({gx, backWall.Bottom()}, {fgx, bayOuter.Bottom()}, 1.0f,
               Fade(ToRay(p.border), 0.30f));
  }
  for (int r = 1; r < gridRows; ++r) {
    const float gy = backWall.y + backWall.h * static_cast<float>(r) / static_cast<float>(gridRows);
    DrawLine(static_cast<int>(backWall.x + 1.0f), static_cast<int>(gy),
             static_cast<int>(backWall.Right() - 1.0f), static_cast<int>(gy),
             Fade(ToRay(p.border), 0.48f));
  }
  for (int b = 1; b < bays; ++b) {
    const float bx = backWall.x + backWall.w * static_cast<float>(b) / static_cast<float>(bays);
    DrawLineEx({bx, backWall.y + 1.0f}, {bx, backWall.Bottom() - 1.0f}, 1.5f,
               Fade(ToRay(p.coffee), 0.55f));
  }

  // Perspective corner lines + room borders.
  const ::Color cornerLine = Fade(ToRay(p.borderDark), 0.72f);
  DrawLineEx(oTL, bTL, 1.0f, cornerLine);
  DrawLineEx(oTR, bTR, 1.0f, cornerLine);
  DrawLineEx(oBL, bBL, 1.0f, cornerLine);
  DrawLineEx(oBR, bBR, 1.0f, cornerLine);
  DrawRectangleLines(static_cast<int>(backWall.x), static_cast<int>(backWall.y),
                     static_cast<int>(backWall.w), static_cast<int>(backWall.h),
                     ToRay(p.coffeeDark));
  DrawRectangleLines(static_cast<int>(bayOuter.x), static_cast<int>(bayOuter.y),
                     static_cast<int>(bayOuter.w), static_cast<int>(bayOuter.h),
                     ToRay(p.borderDark));

  // Advance smooth crate animation delta once per frame and prune removed lots.
  double animDt = 0.0;
  if (lastCargoAnimMs_ > 0.0 && frameTimeMs_ > lastCargoAnimMs_) {
    animDt = std::clamp((frameTimeMs_ - lastCargoAnimMs_) * 0.001, 0.0, 0.08);
  }
  lastCargoAnimMs_ = frameTimeMs_;
  for (auto it = cargoAnims_.begin(); it != cargoAnims_.end();) {
    const bool keep = std::any_of(cargo.hold.begin(), cargo.hold.end(),
                                  [&](const CargoHoldLot& b) { return b.id == it->first; });
    if (!keep) {
      it = cargoAnims_.erase(it);
    } else {
      ++it;
    }
  }

  const float sizeBoost = std::clamp(backWall.w / 440.0f, 0.85f, 1.15f);
  const auto crateBoxSize = [&](const CargoHoldLot* block) {
    const float rootVol =
        std::clamp(static_cast<float>(std::sqrt(std::max(1.0, block->vol))), 1.2f, 7.0f);
    float w = std::round((22.0f + rootVol * 5.6f) * sizeBoost);
    float h = std::round((22.0f + rootVol * 5.6f) * sizeBoost);
    const std::string& lbl = block->shortLabel.empty() ? block->label : block->shortLabel;
    const int labelPx = std::clamp(static_cast<int>(std::round(h * 0.40f)), 12, microPx);
    const float needW = TextWidth(lbl.c_str(), labelPx) + 8.0f;
    w = std::max(w, std::min(backWall.w * 0.36f, needW));
    return std::pair<float, float>{w, h};
  };

  std::vector<const CargoHoldLot*> sortedCrates;
  std::vector<const CargoHoldLot*> unsortedCrates;
  for (const CargoHoldLot& block : cargo.hold) {
    if (block.stowed) {
      sortedCrates.push_back(&block);
    } else {
      unsortedCrates.push_back(&block);
    }
  }
  std::stable_sort(sortedCrates.begin(), sortedCrates.end(),
                   [](const CargoHoldLot* a, const CargoHoldLot* b) {
                     const int ao = a->sortOrder > 0 ? a->sortOrder : 100000;
                     const int bo = b->sortOrder > 0 ? b->sortOrder : 100000;
                     return ao < bo;
                   });

  struct CrateTarget {
    const CargoHoldLot* block = nullptr;
    float tx = 0.0f;
    float ty = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    int tier = 0;
  };
  std::vector<CrateTarget> targets;
  targets.reserve(cargo.hold.size());

  // Sorted crates: neatly arranged along the 3D floor from left to right.
  const float floorLeft = backWall.x + 12.0f;
  const float floorRight = backWall.Right() - 10.0f;
  const float frontGroundY = backWall.Bottom() + std::max(8.0f, floorDepth * 0.48f);
  float fx = floorLeft;
  float groundY = frontGroundY;
  float rowMaxH = 0.0f;
  int rowTier = 0;
  const float crateGap = 8.0f;
  for (const CargoHoldLot* block : sortedCrates) {
    const auto sz = crateBoxSize(block);
    if (fx > floorLeft && fx + sz.first > floorRight) {
      ++rowTier;
      fx = floorLeft + static_cast<float>(rowTier) * 6.0f;
      groundY = std::max(backWall.y + sz.second + 12.0f,
                         groundY - std::max(14.0f, rowMaxH * 0.52f));
      rowMaxH = 0.0f;
    }
    targets.push_back(CrateTarget{block, fx, groundY - sz.second, sz.first, sz.second, rowTier});
    fx += sz.first + crateGap;
    rowMaxH = std::max(rowMaxH, sz.second);
  }

  // Unsorted crates: randomly distributed through the open room space above the floor.
  std::vector<CrateTarget> airTargets;
  airTargets.reserve(unsortedCrates.size());
  const float airMinX = backWall.x + 14.0f;
  const float airMaxX = std::max(airMinX + 8.0f, backWall.Right() - 14.0f);
  const float airMinY = backWall.y + 12.0f;
  const float airMaxY = std::max(airMinY + 12.0f, backWall.Bottom() - 22.0f);
  for (const CargoHoldLot* block : unsortedCrates) {
    const auto sz = crateBoxSize(block);
    const float rx = std::clamp((block->jitterX + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float ry = std::clamp((block->jitterY + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float maxX = std::max(airMinX, airMaxX - sz.first);
    const float maxY = std::max(airMinY, airMaxY - sz.second);
    const float ux = airMinX + rx * (maxX - airMinX);
    const float uy = airMinY + ry * (maxY - airMinY);
    airTargets.push_back(CrateTarget{block, ux, uy, sz.first, sz.second, 0});
  }
  for (int iter = 0; iter < 10; ++iter) {
    for (std::size_t i = 0; i < airTargets.size(); ++i) {
      for (std::size_t j = i + 1; j < airTargets.size(); ++j) {
        CrateTarget& a = airTargets[i];
        CrateTarget& b = airTargets[j];
        const float acx = a.tx + a.w * 0.5f;
        const float acy = a.ty + a.h * 0.5f;
        const float bcx = b.tx + b.w * 0.5f;
        const float bcy = b.ty + b.h * 0.5f;
        const float dx = bcx - acx;
        const float dy = bcy - acy;
        const float needX = (a.w + b.w) * 0.5f + 8.0f;
        const float needY = (a.h + b.h) * 0.5f + 8.0f;
        if (std::abs(dx) < needX && std::abs(dy) < needY) {
          const float pushX = (needX - std::abs(dx)) * 0.5f;
          const float pushY = (needY - std::abs(dy)) * 0.5f;
          if (pushX < pushY) {
            const float dir = dx >= 0.0f ? 1.0f : -1.0f;
            a.tx -= dir * pushX;
            b.tx += dir * pushX;
          } else {
            const float dir = dy >= 0.0f ? 1.0f : -1.0f;
            a.ty -= dir * pushY;
            b.ty += dir * pushY;
          }
          a.tx = std::clamp(a.tx, airMinX, std::max(airMinX, airMaxX - a.w));
          a.ty = std::clamp(a.ty, airMinY, std::max(airMinY, airMaxY - a.h));
          b.tx = std::clamp(b.tx, airMinX, std::max(airMinX, airMaxX - b.w));
          b.ty = std::clamp(b.ty, airMinY, std::max(airMinY, airMaxY - b.h));
        }
      }
    }
  }
  targets.insert(targets.end(), airTargets.begin(), airTargets.end());

  struct RenderedCrate {
    const CargoHoldLot* block = nullptr;
    Area box{};
    bool hovered = false;
    bool settleFlash = false;
  };
  std::vector<RenderedCrate> rendered;
  rendered.reserve(targets.size());
  for (const CrateTarget& tgt : targets) {
    CargoCrateAnim& anim = cargoAnims_[tgt.block->id];
    if (!anim.initialized || sim.State().reducedMotion) {
      anim.x = tgt.tx;
      anim.y = tgt.ty;
      anim.hop = 0.0f;
    } else {
      if (!anim.wasStowed && tgt.block->stowed) {
        anim.hop = 1.0f;
        anim.settleFxUntilMs = frameTimeMs_ + 550.0;
      }
      const float k = 1.0f - std::exp(-12.0f * static_cast<float>(animDt));
      anim.x += (tgt.tx - anim.x) * k;
      anim.y += (tgt.ty - anim.y) * k;
      anim.hop = std::max(0.0f, anim.hop - static_cast<float>(animDt) * 2.8f);
    }
    anim.wasStowed = tgt.block->stowed;
    anim.initialized = true;

    float drawX = anim.x;
    float drawY = anim.y - std::sin(anim.hop * 3.14159265f) * 14.0f;
    if (!tgt.block->stowed && !sim.State().reducedMotion) {
      const float phase = (tgt.block->jitterX + 1.5f) * 3.1f +
                          static_cast<float>(tgt.block->mixEpoch) * 1.7f;
      const float tSec = static_cast<float>(frameTimeMs_ * 0.001);
      drawX += std::sin(tSec * 1.35f + phase) * 2.5f;
      drawY += std::cos(tSec * 1.85f + phase * 1.4f) * 3.0f;
    }
    const Area box{drawX, drawY, tgt.w, tgt.h};
    const bool hovered =
        !tgt.block->stowed && box.Contains(mouseX_, mouseY_ + contentScroll_);
    const bool settleFlash = anim.settleFxUntilMs > frameTimeMs_;
    rendered.push_back(RenderedCrate{tgt.block, box, hovered, settleFlash});
  }

  // Painter's order: upper/back boxes first, lower/front and highlighted boxes on top.
  std::stable_sort(rendered.begin(), rendered.end(),
                   [](const RenderedCrate& a, const RenderedCrate& b) {
                     const int ap = a.block->selected ? 2 : (a.hovered ? 1 : 0);
                     const int bp = b.block->selected ? 2 : (b.hovered ? 1 : 0);
                     if (ap != bp) { return ap < bp; }
                     return (a.box.y + a.box.h) < (b.box.y + b.box.h);
                   });

  for (const RenderedCrate& item : rendered) {
    const CargoHoldLot* block = item.block;
    const Area& box = item.box;
    const float depthX = std::clamp(box.w * 0.16f, 4.0f, 7.0f);
    const float depthY = std::clamp(box.h * 0.14f, 3.0f, 6.0f);
    const bool green = block->selected || item.settleFlash;

    // Subtle shadow underneath the box.
    const ::Color shadowCol = Fade(ToRay(p.coffeeDark), block->stowed ? 0.18f : 0.10f);
    DrawRectangle(static_cast<int>(box.x - depthX * 0.5f),
                  static_cast<int>(box.Bottom() - 1.0f),
                  static_cast<int>(box.w + depthX * 0.7f),
                  block->stowed ? 5 : 4, shadowCol);

    if (block->selected) {
      const float pulse =
          sim.State().reducedMotion
              ? 1.0f
              : static_cast<float>(0.80 + 0.20 * std::sin(frameTimeMs_ * 0.007));
      for (int r = 4; r >= 1; --r) {
        const unsigned char alpha =
            static_cast<unsigned char>((28 + (4 - r) * 18) * pulse);
        DrawRectangleLines(static_cast<int>(box.x - depthX) - r,
                           static_cast<int>(box.y - depthY) - r,
                           static_cast<int>(box.w + depthX) + r * 2,
                           static_cast<int>(box.h + depthY) + r * 2,
                           ::Color{126, 198, 63, alpha});
      }
    }

    const ::Color frontCol = green ? ::Color{202, 236, 168, 255}
                                   : (item.hovered
                                          ? (classic ? ::Color{225, 198, 160, 255} : ToRay(p.hover))
                                          : (classic ? ::Color{204, 170, 130, 255}
                                                     : ToRay(block->stowed ? p.panel3 : p.coffeeLight)));
    const ::Color topCol = green ? ::Color{232, 249, 214, 255}
                                 : (classic ? ::Color{252, 248, 241, 255} : ToRay(p.content));
    const ::Color sideCol = green ? ::Color{216, 242, 190, 255}
                                  : (classic ? ::Color{240, 233, 222, 255} : ToRay(p.panel2));
    const ::Color edgeCol = green ? ::Color{82, 150, 36, 255}
                                  : (item.hovered ? ToRay(p.coffeeDark) : ToRay(p.borderDark));

    const Vector2 fTL{box.x, box.y};
    const Vector2 fTR{box.Right(), box.y};
    const Vector2 fBL{box.x, box.Bottom()};
    const Vector2 bTL{box.x - depthX, box.y - depthY};
    const Vector2 bTR{box.Right() - depthX, box.y - depthY};
    const Vector2 bBL{box.x - depthX, box.Bottom() - depthY};

    fillQuad(bTL, bTR, fTR, fTL, topCol);
    fillQuad(bTL, fTL, fBL, bBL, sideCol);
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h), frontCol);
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h), edgeCol);
    DrawLineEx(bTL, bTR, 1.0f, edgeCol);
    DrawLineEx(bTR, fTR, 1.0f, edgeCol);
    DrawLineEx(bTL, fTL, 1.0f, edgeCol);
    DrawLineEx(bTL, bBL, 1.0f, edgeCol);
    DrawLineEx(bBL, fBL, 1.0f, edgeCol);

    const std::string& crateText =
        block->shortLabel.empty() ? block->label : block->shortLabel;
    const int boxPx = std::clamp(static_cast<int>(std::round(box.h * 0.40f)), 12, microPx);
    const float txtW = TextWidth(crateText.c_str(), boxPx);
    const ::Color labelInk = green ? ::Color{38, 66, 20, 255} : ToRay(p.ink);
    Text(crateText.c_str(),
         static_cast<int>(box.x + std::max(2.0f, (box.w - txtW) * 0.5f)),
         static_cast<int>(box.y + std::max(2.0f, (box.h - static_cast<float>(boxPx)) * 0.5f)),
         boxPx, labelInk);
  }

  // Register clicks on every unsorted crate (front-most drawn crate wins first).
  for (auto it = rendered.rbegin(); it != rendered.rend(); ++it) {
    if (!it->block->stowed) {
      const float depthX = std::clamp(it->box.w * 0.16f, 4.0f, 7.0f);
      const float depthY = std::clamp(it->box.h * 0.14f, 3.0f, 6.0f);
      const Area hitBox{it->box.x - depthX, it->box.y - depthY,
                        it->box.w + depthX, it->box.h + depthY};
      navHits_.push_back(NavHit{hitBox, "row", "cargo-stow:" + it->block->id});
    }
  }

  // Capacity bar & legend below visual bay.
  float capY = bayOuter.Bottom() + 10.0f;
  DrawLine(static_cast<int>(midX + 10.0f), static_cast<int>(capY),
           static_cast<int>(midX + midW - 10.0f), static_cast<int>(capY), ToRay(p.border));
  capY += 8.0f;
  Text("Capacity", static_cast<int>(midX + 10.0f), static_cast<int>(capY), bodyPx, ToRay(p.ink));
  const float capTxtW = TextWidth(cargo.capacityLine.c_str(), smallPx);
  Text(cargo.capacityLine.c_str(), static_cast<int>(midX + midW - 10.0f - capTxtW),
       static_cast<int>(capY + 1.0f), smallPx, ToRay(p.ink));
  capY += line + 4.0f;

  const float barW = midW - 20.0f;
  const int segments = 12;
  const float segGap = 2.0f;
  const float segW = std::max(4.0f, (barW - segGap * (segments - 1)) / segments);
  const double pct = cargo.capacity > 0 ? std::clamp(cargo.used / cargo.capacity, 0.0, 1.0) : 0.0;
  const int filledSegs = static_cast<int>(std::round(pct * segments));
  const ::Color usedColor = pct > 0.90 ? HexColor("#c08a3e", ToRay(p.amber))
                                       : ToRay(p.coffeeDark);
  for (int s = 0; s < segments; ++s) {
    const float sxPos = midX + 10.0f + static_cast<float>(s) * (segW + segGap);
    const bool isUsed = s < filledSegs;
    DrawRectangle(static_cast<int>(sxPos), static_cast<int>(capY),
                  static_cast<int>(segW), 8, isUsed ? usedColor : ToRay(p.panel2));
    DrawRectangleLines(static_cast<int>(sxPos), static_cast<int>(capY),
                       static_cast<int>(segW), 8, ToRay(p.borderDark));
  }
  capY += 14.0f;
  DrawRectangle(static_cast<int>(midX + 10.0f), static_cast<int>(capY + 2.0f), 9, 9, usedColor);
  Text("Used", static_cast<int>(midX + 23.0f), static_cast<int>(capY), microPx, ToRay(p.muted));
  DrawRectangle(static_cast<int>(midX + 68.0f), static_cast<int>(capY + 2.0f), 9, 9,
                ToRay(p.panel2));
  DrawRectangleLines(static_cast<int>(midX + 68.0f), static_cast<int>(capY + 2.0f), 9, 9,
                     ToRay(p.borderDark));
  Text("Free", static_cast<int>(midX + 81.0f), static_cast<int>(capY), microPx, ToRay(p.muted));

  // ---- Right column: Context-sensitive finalizer --------------------------
  Text(cargo.sideTitle.c_str(), static_cast<int>(rightX + 11.0f), static_cast<int>(top + 9.0f),
       titlePx, ToRay(p.coffee));
  const std::string ctxBadge = cargo.contextKind == "delivery"    ? "DELIVERY"
                               : cargo.contextKind == "freelance" ? "FREELANCE"
                               : cargo.contextKind == "station"   ? "STATION"
                                                                  : "JETTISON";
  const float badgeTextW = TextWidth(ctxBadge.c_str(), microPx);
  const float badgeW = badgeTextW + 16.0f;
  const Area badgeBox{rightX + rightW - 10.0f - badgeW, top + 6.0f, badgeW, smallLine + 5.0f};
  PaintChamferBoxRay(badgeBox, kChamferSmall, ToRay(p.panel2),
                       ToRay(p.borderDark), ToRay(p.border));
  Text(ctxBadge.c_str(), static_cast<int>(badgeBox.x + 8.0f), static_cast<int>(badgeBox.y + 3.0f),
       microPx, ToRay(p.coffee));

  float ry = top + TypeStep(28.0f) + 2.0f;
  for (const std::string& row : sideNotes) {
    Text(row.c_str(), static_cast<int>(rightX + 11.0f), static_cast<int>(ry),
         smallPx, ToRay(p.muted));
    ry += smallLine;
  }
  ry += 6.0f;

  if (cargo.runTabs.size() > 1) {
    for (const CargoRunTab& tab : cargo.runTabs) {
      const Area tabBox{rightX + 10.0f, ry, btnW, smallLine + 10.0f};
      PaintChamferBoxRay(tabBox, kChamferSmall, ToRay(tab.selected ? p.panel3 : p.panel2),
                         ToRay(tab.selected ? p.coffee : p.borderDark),
                         ToRay(tab.selected ? p.coffeeLight : p.border));
      if (tab.selected) {
        DrawRectangle(static_cast<int>(tabBox.x + 5.0f), static_cast<int>(tabBox.y + 5.0f), 3,
                      static_cast<int>(std::max(1.0f, tabBox.h - 10.0f)), ToRay(p.coffee));
      }
      const std::string tabTxt = FitText(tab.label, tabBox.w - 18.0f, smallPx);
      Text(tabTxt.c_str(), static_cast<int>(tabBox.x + 11.0f), static_cast<int>(tabBox.y + 5.0f),
           smallPx, ToRay(tab.selected ? p.ink : p.muted));
      navHits_.push_back(NavHit{tabBox, "row", "cargo-trade:" + tab.key});
      ry += tabBox.h + 4.0f;
    }
    ry += 4.0f;
  }

  if (!cargo.contextDetails.empty()) {
    std::vector<std::string> detailLines;
    for (const std::string& d : cargo.contextDetails) {
      const auto wrapped = WrapText(d, std::max(40.0f, btnW - 20.0f),
                                    smallPx, SelectFont(smallPx, false));
      detailLines.insert(detailLines.end(), wrapped.begin(), wrapped.end());
    }
    const float detH = static_cast<float>(detailLines.size()) * smallLine + 14.0f;
    const Area detBox{rightX + 10.0f, ry, btnW, detH};
    PaintChamferBoxRay(detBox, kChamferCard, ToRay(p.panel2),
                       ToRay(p.borderDark), ToRay(p.border));
    float dy = detBox.y + 7.0f;
    for (const std::string& dLine : detailLines) {
      Text(dLine.c_str(), static_cast<int>(detBox.x + 10.0f), static_cast<int>(dy),
           smallPx, ToRay(p.ink));
      dy += smallLine;
    }
    ry += detH + 8.0f;
  }

  for (std::size_t i = 0; i < cargo.sideRows.size(); ++i) {
    const CargoSideRow& row = cargo.sideRows[i];
    const SideBtnMeasure& bm = sideBtns[i];
    const Area btnBox{rightX + 10.0f, ry, btnW, bm.height};
    const bool hasId = !row.id.empty();
    const std::string tierSubject =
        row.id.compare(0, 13, "cargo-unload:") == 0 ? ("jettison " + row.id) : ("sell " + row.id);
    const std::string chargeKey = ButtonChargeKey(sim, "press:cargo:" + row.id + ":");
    const std::string chargeTier = ButtonChargeTier(tierSubject);
    const double chargePct = hasId ? ChargePercent(sim, chargeKey, chargeTier) : 0.0;
    const bool live = row.enabled && hasId && ChargeLeft(sim, chargeKey, chargeTier) > 0.0;

    FillChamferRay(btnBox, kChamferCard, ToRay(live ? p.control : p.panel3));
    if (live && hasId) { PaintProgressFill(btnBox, chargePct, p, kChamferCard); }
    PaintChamferDoubleLinesRay(btnBox, kChamferCard, ToRay(live ? p.borderDark : p.border), ToRay(p.border));

    float ty = btnBox.y + 7.0f;
    for (const std::string& lRow : bm.labelLines) {
      Text(lRow.c_str(), static_cast<int>(btnBox.x + 10.0f), static_cast<int>(ty),
           bodyPx, ToRay(live ? p.ink : p.muted));
      ty += line;
    }
    if (!bm.noteLines.empty()) {
      ty += 2.0f;
      for (const std::string& nRow : bm.noteLines) {
        Text(nRow.c_str(), static_cast<int>(btnBox.x + 10.0f), static_cast<int>(ty),
             smallPx, ToRay(live ? p.coffee : p.muted));
        ty += smallLine;
      }
    }
    if (live) {
      navHits_.push_back(NavHit{btnBox, "row", row.id});
    }
    ry += bm.height + 6.0f;
  }

  contentPaintBottom_ = std::max(contentPaintBottom_, top + panelH + 8.0f);
  if (!pendingConfirm_.empty()) {
    std::vector<ProgressButton> buttons;
    ProgressButton yes;
    yes.taskId = "confirm-yes";
    yes.text = "Yes, do it";
    ProgressButton no;
    no.taskId = "confirm-no";
    no.text = "Keep as it is";
    buttons.push_back(yes);
    buttons.push_back(no);
    PaintActionBand("Confirm action", "", buttons, pendingConfirm_);
  }
}

bool ShellView::PressPanelRow(Simulation& sim, ScreenKind kind, const std::string& id) {
  switch (kind) {
    case ScreenKind::System: return PressSystemAction(sim, id);
    case ScreenKind::Info: return PressInfoAction(sim, id);
    case ScreenKind::Station: {
      // The first two columns only choose; the third column commits. Choosing
      // costs nothing, so it neither spends an action nor a click.
      if (id.compare(0, 12, "station-cat:") == 0) {
        const std::string card = id.substr(12);
        if (stationSelection_.category != card) {
          stationSelection_.category = card;
          stationSelection_.item.clear();
        }
        return true;
      }
      if (id.compare(0, 13, "station-item:") == 0) {
        stationSelection_.item = id.substr(13);
        return true;
      }
      return PressStationAction(sim, id, "");
    }
    case ScreenKind::Cargo: {
      if (id.compare(0, 13, "cargo-select:") == 0) {
        const std::string lotId = id.substr(13);
        cargoSelection_.lotId = (cargoSelection_.lotId == lotId) ? std::string() : lotId;
        for (const CargoLotView& lot : cargoModel_.lots) {
          if (lot.id == cargoSelection_.lotId && !lot.contractKey.empty()) {
            for (const CargoRunTab& tab : cargoModel_.runTabs) {
              if (tab.key == lot.contractKey) {
                cargoSelection_.tradeKey = tab.key;
                break;
              }
            }
          }
        }
        return true;
      }
      if (id.compare(0, 12, "cargo-trade:") == 0) {
        cargoSelection_.tradeKey = id.substr(12);
        return true;
      }
      if (id.compare(0, 11, "cargo-stow:") == 0) {
        const bool ok = PressCargoRoom(sim, id, "");
        if (ok && cargoSelection_.lotId == id.substr(11)) { cargoSelection_.lotId.clear(); }
        return ok;
      }
      return PressCargoRoom(sim, id, "");
    }
    case ScreenKind::Science: return PressStoresAction(sim, id, "");
    case ScreenKind::Room: return PressRoomAction(sim, id, "");
    case ScreenKind::Awards: return PressAwardsAction(sim, id, "");
    case ScreenKind::Event: return PressEventAction(sim, id, "");
    case ScreenKind::Wiki: {
      const std::size_t cut = id.find(':');
      const std::string verb = cut == std::string::npos ? id : id.substr(0, cut);
      const std::string arg = cut == std::string::npos ? std::string() : id.substr(cut + 1);
      return PressWikiAction(sim, verb, arg, &wiki_);
    }
    default: return false;
  }
}

// ============================================================================
//  The room hero: the vignette from ROOM_SCENES — a scope, two orbits,
//  a core, two beacons, the code and label, and an eight-bar gauge. Every
//  rectangle is read from the model; none of it is measured here.
// ============================================================================
void ShellView::PaintHero(const Vignette& vignette, const Area& box) {
  const Palette& p = theme_.palette;
  const Rgb accent = p.coffee;
  PaintChamferBoxRay(box, kChamferPanel, ToRay(p.panel),
                       ToRay(p.borderDark), ToRay(p.border));

  const auto cx = [&box](const Area& a) {
    return box.x + a.x + a.w / 2.0f;
  };
  const auto cy = [&box](const Area& a) {
    return box.y + a.y + a.h / 2.0f;
  };
  DrawCircleLines(static_cast<int>(cx(vignette.scope)), static_cast<int>(cy(vignette.scope)),
                  vignette.scope.w / 2.0f, ToRay(p.border));
  DrawCircleLines(static_cast<int>(cx(vignette.orbitOne)),
                  static_cast<int>(cy(vignette.orbitOne)), vignette.orbitOne.w / 2.0f,
                  ToRay(p.borderDark));
  DrawCircleLines(static_cast<int>(cx(vignette.orbitTwo)),
                  static_cast<int>(cy(vignette.orbitTwo)), vignette.orbitTwo.w / 2.0f,
                  ToRay(p.borderDark));
  DrawRectangle(static_cast<int>(box.x + vignette.core.x),
                static_cast<int>(box.y + vignette.core.y),
                static_cast<int>(vignette.core.w), static_cast<int>(vignette.core.h),
                ToRay(accent));
  DrawRectangle(static_cast<int>(box.x + vignette.beaconOne.x),
                static_cast<int>(box.y + vignette.beaconOne.y),
                static_cast<int>(vignette.beaconOne.w),
                static_cast<int>(vignette.beaconOne.h), ToRay(accent));
  DrawRectangle(static_cast<int>(box.x + vignette.beaconTwo.x),
                static_cast<int>(box.y + vignette.beaconTwo.y),
                static_cast<int>(vignette.beaconTwo.w),
                static_cast<int>(vignette.beaconTwo.h), ToRay(p.muted));

  Text(vignette.code.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y), TypePx(9) , ToRay(p.coffee));
  Text(vignette.label.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y + 14.0f), TypePx(13) , ToRay(p.ink));
  Text(vignette.description.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y + 32.0f), TypePx(9) , ToRay(p.muted));
  Text(vignette.statusTop.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + box.h - 34.0f), TypePx(8) , ToRay(p.muted));
  Text(vignette.statusValue.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + box.h - 22.0f), TypePx(9) , ToRay(p.coffee));

  for (std::size_t i = 0; i < vignette.meter.size(); ++i) {
    const Area& bar = vignette.meter[i];
    DrawRectangle(static_cast<int>(box.x + bar.x), static_cast<int>(box.y + bar.y),
                  static_cast<int>(bar.w), static_cast<int>(bar.h),
                  ToRay(i < 5 ? p.coffee : p.panel3));
  }
}


// ============================================================================
//  The main menu (Section N). The card is the reference's: a 3:2 crop of the
//  1672x941 art, five free commands down the left, and one dialog beside them.
//  Nothing here can advance the clock: the menu is not a place in the voyage.
// ============================================================================
void ShellView::OpenPopup(const std::string& key) {
  if (std::find(PopupKeys().begin(), PopupKeys().end(), key) == PopupKeys().end()) { return; }
  auto& panel = floating_.Open(key);
  focusedPanel_ = panel.serial;
  popupKey_ = key;
  popupCount_ = floating_.OpenedCount();
}

void ShellView::OpenBriefing(const Simulation& sim, const std::string& kind, const std::string& id) {
  selection_ = DescribeTarget(sim, kind, id);
  auto& panel = floating_.Open("", BuildBriefPopup(sim, kind, id));
  focusedPanel_ = panel.serial;
  popupCount_ = floating_.OpenedCount();
}

void ShellView::ClosePopup() {
  if (std::none_of(floating_.Panels().begin(), floating_.Panels().end(),
                   [&](const auto& panel) { return panel.serial == focusedPanel_; }) &&
      !floating_.Panels().empty()) { focusedPanel_ = floating_.Panels().back().serial; }
  floating_.Close(focusedPanel_);
  focusedPanel_ = floating_.Panels().empty() ? 0 : floating_.Panels().back().serial;
  popupKey_ = floating_.Panels().empty() ? "" : floating_.Panels().back().key;
}

void ShellView::HideTitle() {
  titleActive_ = false;
  titleConfirm_ = false;
  titlePanel_ = TitlePanel::None;
  titleHits_.clear();
  titleMessage_.clear();
}

void ShellView::ShowSplash(bool fromVoyage) {
  splashActive_ = true;
  splashFromVoyage_ = fromVoyage;
  splashStartMs_ = -1.0;
  titleInStartMs_ = -1.0;
}

void ShellView::ShowTitle(bool fromVoyage) {
  titleActive_ = true;
  fromVoyage_ = fromVoyage;
  titlePanel_ = TitlePanel::None;
  titleCard_ = 0;
  titleMessage_.clear();
  titleConfirm_ = false;
  titleHits_.clear();
  popupKey_.clear();
  floating_.Clear();
  focusedPanel_ = 0;
}

void ShellView::EndSplash(double nowMs) {
  splashActive_ = false;
  ShowTitle(splashFromVoyage_);
  titleInStartMs_ = nowMs;
}

void ShellView::PaintSettingsControls(Simulation& sim, const PopupModel& model) {
  auto text = [](const std::string& value) { ImGui::TextWrapped("%s", value.c_str()); };
  auto heading = [&](const std::string& value) { ImGui::Spacing(); text(value); };
  auto action = [&](const std::string& cmd, const std::string& arg = "") {
    std::string message;
    PressPopupAction(sim, "settings", cmd, arg, saveDir_, &message);
  };
  auto button = [&](const std::string& label, const std::string& id) {
    ImGui::PushID(id.c_str());
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const auto size = ImGui::CalcTextSize(label.c_str(), nullptr, false, std::max(1.0f, width - 20.0f));
    const float h = size.y + 14.0f;
    const auto at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("button", ImVec2(width, h));
    const auto& p = theme_.palette;
    auto* draw = ImGui::GetWindowDrawList();
    PaintChamferBoxIm(draw, at.x, at.y, width, h, kChamferSmall,
                      ToIm(ImGui::IsItemHovered() ? p.panel3 : p.panel2),
                      ToIm(p.borderDark), ToIm(p.border));
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + 10.0f, at.y + 7.0f),
                  ToIm(p.ink), label.c_str(), nullptr, std::max(1.0f, width - 20.0f));
    ImGui::PopID(); return clicked;
  };
      if (!model.themes.empty()) {
        heading("Colour palette"); text("Change the bridge and instrument colours.");
        const auto it = std::find(model.themes.begin(), model.themes.end(), model.theme);
        const int selected = it == model.themes.end() ? 0 : static_cast<int>(it - model.themes.begin());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##palette", model.themeLabels[selected].c_str())) {
          for (std::size_t i = 0; i < model.themes.size(); ++i) {
            if (ImGui::Selectable(model.themeLabels[i].c_str(), static_cast<int>(i) == selected)) { action("theme", model.themes[i]); }
          }
          ImGui::EndCombo();
        }
      }
      if (!audioStatus_.empty()) { heading("Soundtrack"); text(audioStatus_); }
      heading("Display adjustment");
      text("Higher gamma darkens midtones; brightness adjusts the white level. Radiation wave shifts sub-pixel R/B channels along stellar/nebular drift (pauses under reduced motion).");
      if (!displayAdjustmentAvailable_) { text("Display adjustment is unavailable; using the unadjusted display."); }
      ImGui::BeginDisabled(!displayAdjustmentAvailable_);
      float gamma = model.displayGamma;
      float brightness = model.displayBrightness;
      float wave = model.radiationWave;
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##display-gamma", &gamma, static_cast<float>(kDisplayGammaMin),
                             static_cast<float>(kDisplayGammaMax), "Gamma: %.2f")) {
        action("display-gamma", std::to_string(gamma));
      }
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##display-brightness", &brightness, 0.75f, 1.15f, "Brightness: %.2f")) {
        action("display-brightness", std::to_string(brightness));
      }
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##radiation-wave", &wave, static_cast<float>(kRadiationWaveMin),
                             static_cast<float>(kRadiationWaveMax),
                             wave <= 0.01f ? "Radiation wave: Off" : "Radiation wave: %.2f")) {
        action("radiation-wave", std::to_string(wave));
      }
      float colorShift = model.colorShift;
      float glow = model.glow;
      float lensFlare = model.lensFlare;
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##color-shift", &colorShift, -180.0f, 180.0f, "Colorshift: %.0f degrees")) {
        action("color-shift", std::to_string(colorShift));
      }
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##glow", &glow, 0.0f, 2.0f, "Glow: %.2f")) {
        action("glow", std::to_string(glow));
      }
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##lens-flare", &lensFlare, 0.0f, 2.0f,
                             lensFlare <= 0.01f ? "Lens flare: Off" : "Lens flare: %.2f")) {
        action("lens-flare", std::to_string(lensFlare));
      }
      ImGui::EndDisabled();
      if (displayAdjustmentAvailable_ && button("Reset display adjustment", "display-reset")) {
        action("display-gamma", "1"); action("display-brightness", "1");
        action("radiation-wave", "1");
        action("color-shift", "0"); action("glow", "1"); action("lens-flare", "1");
      }
      for (const auto& toggle : model.toggles) {
        heading(toggle.heading); text(toggle.help);
        if (button(toggle.label, toggle.id)) { action("toggle", toggle.id); }
        // HTA places volume after sound effects, before decorative motion.
        if (toggle.id == "soundEffects") {
          heading("Audio level"); text("Overall music and effects volume.");
          std::string current = model.audioLevel;
          for (const auto& choice : model.audioChoices) {
            if (choice.value == model.audioLevel) { current = choice.label; }
          }
          ImGui::SetNextItemWidth(-1);
          if (ImGui::BeginCombo("##audio", current.c_str())) {
            for (const auto& choice : model.audioChoices) {
              if (ImGui::Selectable(choice.label.c_str(), model.audioLevel == choice.value)) { action("audio", choice.value); }
            }
            ImGui::EndCombo();
          }
        }
      }
      if (!model.fontStepLabels.empty()) {
        heading(model.fontStepLine);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##reading-size", model.fontStepLabels[model.fontStep].c_str())) {
          for (std::size_t i = 0; i < model.fontStepLabels.size(); ++i) {
            if (ImGui::Selectable(model.fontStepLabels[i].c_str(), static_cast<int>(i) == model.fontStep)) { action("font", std::to_string(i)); }
          }
          ImGui::EndCombo();
        }
        if (button(model.fullscreenLabel, "fullscreen")) { action("fullscreen"); }
      }
}

void ShellView::PageTutorial(int direction, double nowMs) {
  if (!titleActive_ || (titlePanel_ != TitlePanel::Guide && titlePanel_ != TitlePanel::Tutorial) ||
      direction == 0) return;
  const int next = std::clamp(titleCard_ + (direction > 0 ? 1 : -1),
                              0, static_cast<int>(TutorialCards().size()) - 1);
  if (next != titleCard_) {
    titleCard_ = next;
    tutorialDirection_ = direction > 0 ? 1 : -1;
    tutorialTransitionMs_ = nowMs;
  }
}

void ShellView::PaintTutorial(const Simulation& sim, double nowMs) {
  const Palette& p = theme_.palette;
  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), ToRay(p.titleBackground));
  const float innerW = std::min(width_ - 80.0f, 1200.0f);
  const float left = (width_ - innerW) * 0.5f;
  const int textPx = TypePx(11);
  const float lineH = static_cast<float>(textPx) + 5.0f;
  const std::string heading = "HOW TO PLAY   " + std::to_string(title_.tutorialCard + 1) +
                              " / " + std::to_string(title_.tutorialCards);
  Text(heading.c_str(), static_cast<int>(left), 18, textPx, ToRay(p.titleInk));
  const char* keyboardHint = "LEFT / RIGHT KEYS";
  Text(keyboardHint, static_cast<int>(left + innerW - TextWidth(keyboardHint, textPx, false)),
       18, textPx, ToRay(p.muted));
  const auto lines = WrapText(title_.cardBody, innerW, textPx, SelectFont(textPx, false));
  const auto habits = WrapText(title_.cardHabit, innerW, textPx, SelectFont(textPx, false));
  const float controlsY = height_ - 58.0f;
  const float notesY = controlsY - 12.0f - (lines.size() + habits.size()) * lineH;
  const float artTop = 54.0f;
  const float artH = std::max(40.0f, notesY - artTop - 12.0f);
  const Texture2D* art = static_cast<const Texture2D*>(tutorialArt_[static_cast<std::size_t>(title_.tutorialCard)]);
  if (art != nullptr && art->id != 0 && art->width > 0 && art->height > 0) {
    const float scale = std::min(innerW / art->width, artH / art->height);
    const float w = art->width * scale, h = art->height * scale;
    const float progress = sim.State().reducedMotion || theme_.reducedMotion ? 1.0f :
        std::clamp(static_cast<float>((nowMs - tutorialTransitionMs_) / 320.0), 0.0f, 1.0f);
    const float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
    const float x = (width_ - w) * 0.5f + tutorialDirection_ * (1.0f - eased) * 48.0f;
    const float y = artTop + (artH - h) * 0.5f;
    DrawTexturePro(*art, Rectangle{0, 0, static_cast<float>(art->width), static_cast<float>(art->height)},
                   Rectangle{x, y, w, h}, Vector2{0, 0}, 0, ToRay(p.white, static_cast<int>(255 * eased)));
  } else {
    // Keep the guide usable even if a packaged artwork is missing.
    Text(title_.cardTitle.c_str(), static_cast<int>(left), static_cast<int>(artTop + artH * 0.4f),
         TypePx(18), ToRay(p.titleInk));
    Text("Illustration unavailable", static_cast<int>(left), static_cast<int>(artTop + artH * 0.4f + 40),
         textPx, ToRay(p.muted));
  }
  float y = notesY;
  for (const auto& line : lines) {
    Text(line.c_str(), static_cast<int>(left), static_cast<int>(y), textPx, ToRay(p.titleInk));
    y += lineH;
  }
  for (const auto& line : habits) {
    Text(line.c_str(), static_cast<int>(left), static_cast<int>(y), textPx, ToRay(p.coffee));
    y += lineH;
  }
  const float gap = 12.0f, buttonW = std::min(210.0f, (innerW - 2 * gap) / 3.0f);
  const float startX = (width_ - (buttonW * 3 + gap * 2)) * 0.5f;
  const char* labels[] = {"PREVIOUS", "BACK TO MENU", "NEXT"};
  const char* actions[] = {"card:prev", "back", "card:next"};
  for (int i = 0; i < 3; ++i) {
    const bool enabled = i == 1 || (i == 0 ? title_.tutorialCard > 0 : title_.tutorialCard + 1 < title_.tutorialCards);
    const Area box{startX + i * (buttonW + gap), controlsY, buttonW, 40.0f};
    const bool hovered = enabled && box.Contains(mouseX_, mouseY_);
    PaintChamferBoxRay(box, kChamferCard, ToRay(hovered ? p.titleMenuHover : p.titleMenu),
                       ToRay(p.titleMenuBorder), ToRay(p.border));
    const float tw = TextWidth(labels[i], textPx, false);
    Text(labels[i], static_cast<int>(box.x + (box.w - tw) * 0.5f),
         static_cast<int>(box.y + (box.h - textPx) * 0.5f), textPx,
         ToRay(enabled ? p.titleMenuInk : p.muted));
    if (enabled) titleHits_.push_back({box, actions[i], ""});
  }
}

void ShellView::RebuildSetupPreview(const Simulation& sim) {
  setupPreview_ = BuildChartPreview(sim.World(), setupSettings_);
  setupPreviewDirty_ = false;
}

void ShellView::PaintSetup(Simulation& sim) {
  const Palette& p = theme_.palette;
  if (setupPreviewDirty_) { RebuildSetupPreview(sim); }

  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), ToRay(p.titleBackground));
  const float ratio = 1672.0f / 941.0f;
  float cardW = std::min(width_ - 24.0f, 1672.0f);
  float cardH = cardW / ratio;
  if (cardH > height_ - 24.0f) { cardH = height_ - 24.0f; cardW = cardH * ratio; }
  const Area card{(width_ - cardW) * 0.5f, (height_ - cardH) * 0.5f, cardW, cardH};
  const Texture2D* texture = static_cast<const Texture2D*>(setupArt_);
  const bool hasArt = texture != nullptr && texture->id != 0 && texture->width > 0;
  if (hasArt) {
    // setupscreen.png is opaque: outside its chamfered frame it is black, a
    // 4 px rim and four 36 px corners (measured on the 1867x842 file). Leave
    // the rim out of the source and lay the page colour over the corners.
    const float tw = static_cast<float>(texture->width);
    const float th = static_cast<float>(texture->height);
    const float rimX = tw * (4.0f / 1867.0f);
    const float rimY = th * (4.0f / 842.0f);
    DrawTexturePro(*texture, Rectangle{rimX, rimY, tw - 2.0f * rimX, th - 2.0f * rimY},
                   Rectangle{card.x, card.y, card.w, card.h}, Vector2{0, 0}, 0, ToRay(p.white));
    const float cx = card.w * (38.0f / 1859.0f) + 1.0f;
    const float cy = card.h * (38.0f / 834.0f) + 1.0f;
    const Color page = ToRay(p.titleBackground);
    const float l = card.x, r = card.Right(), t = card.y, b = card.Bottom();
    DrawTriangle(Vector2{l, t}, Vector2{l, t + cy}, Vector2{l + cx, t}, page);
    DrawTriangle(Vector2{r, t}, Vector2{r - cx, t}, Vector2{r, t + cy}, page);
    DrawTriangle(Vector2{l, b}, Vector2{l + cx, b}, Vector2{l, b - cy}, page);
    DrawTriangle(Vector2{r, b}, Vector2{r, b - cy}, Vector2{r - cx, b}, page);
  } else {
    PaintChamferBoxRay(card, kChamferPanel, ToRay(p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
  }

  const float pad = card.w * 0.035f;
  const float leftW = card.w * 0.42f;
  const float mapX = card.x + leftW;
  const float mapY = card.y + pad;
  const float mapH = card.h * 0.72f;
  // The art's lower right corner is cut by a diagonal that runs, in fractions
  // of the card, along x + 0.451 y = 1.275. The preview keeps its height and
  // gives up width, so its own lower right corner stays inside that line.
  const float diagonalX = card.w * (1.275f - 0.451f * ((mapY + mapH - card.y) / card.h));
  const float mapW = std::min(card.w - leftW - pad, diagonalX - card.w * 0.017f - leftW);
  const float barY = card.Bottom() - card.h * 0.11f;
  const int textPx = TypePx(12);

  Text("VOYAGE SETTINGS", static_cast<int>(card.x + pad), static_cast<int>(card.y + pad),
       TypePx(16), ToRay(p.titleInk));

  float y = card.y + pad + 36.0f;
  auto row = [&](const char* label, const std::string& value, const char* dec, const char* inc) {
    Text(label, static_cast<int>(card.x + pad), static_cast<int>(y), textPx, ToRay(p.muted));
    y += 18.0f;
    const Area minus{card.x + pad, y, 28.0f, 28.0f};
    const Area plus{card.x + pad + leftW - pad * 2.0f - 28.0f, y, 28.0f, 28.0f};
    PaintChamferBoxRay(minus, kChamferSmall, ToRay(p.titleMenu),
                       ToRay(p.titleMenuBorder), ToRay(p.border));
    PaintChamferBoxRay(plus, kChamferSmall, ToRay(p.titleMenu),
                       ToRay(p.titleMenuBorder), ToRay(p.border));
    Text("-", static_cast<int>(minus.x + 10), static_cast<int>(minus.y + 6), textPx, ToRay(p.titleMenuInk));
    Text("+", static_cast<int>(plus.x + 8), static_cast<int>(plus.y + 6), textPx, ToRay(p.titleMenuInk));
    const float tw = TextWidth(value.c_str(), textPx, false);
    Text(value.c_str(), static_cast<int>(minus.x + 36.0f + (plus.x - minus.x - 64.0f - tw) * 0.5f),
         static_cast<int>(y + 6), textPx, ToRay(p.titleInk));
    titleHits_.push_back({minus, dec, ""});
    titleHits_.push_back({plus, inc, ""});
    y += 40.0f;
  };

  char seedBuf[32];
  std::snprintf(seedBuf, sizeof(seedBuf), "%016llX", static_cast<unsigned long long>(setupSettings_.seed));
  row("Seed", seedBuf, "setup-seed-dec", "setup-seed-inc");
  titleHits_.push_back({Area{card.x + pad + 36.0f, y - 40.0f, leftW - 100.0f, 28.0f}, "setup-seed-rand", ""});
  row("Galaxies (1-100)", std::to_string(setupSettings_.galaxies), "setup-gal-dec", "setup-gal-inc");
  if (setupSettings_.galaxies >= kGalaxyRamWarning) {
    for (const auto& line : WrapText("Warning: 25+ galaxies exhaust RAM and CPU.",
                                     leftW - pad, TypePx(10), SelectFont(TypePx(10), false))) {
      Text(line.c_str(), static_cast<int>(card.x + pad), static_cast<int>(y), TypePx(10), ToRay(p.terracotta));
      y += 14.0f;
    }
  }
  row("Map size", MapSizeLabel(setupSettings_.mapSize), "setup-scale-dec", "setup-scale-inc");
  char spreadBuf[32];
  std::snprintf(spreadBuf, sizeof(spreadBuf), "%.2f", setupSettings_.spread);
  row("Spread (distance)", spreadBuf, "setup-spread-dec", "setup-spread-inc");

  Text("Layer", static_cast<int>(card.x + pad), static_cast<int>(y), textPx, ToRay(p.muted));
  y += 20.0f;
  const char* layers[] = {"galaxy", "sector", "system"};
  const char* layerLabels[] = {"GALAXY", "SECTOR", "SYSTEM"};
  const float lw = (leftW - pad * 2.0f - 16.0f) / 3.0f;
  for (int i = 0; i < 3; ++i) {
    const Area box{card.x + pad + i * (lw + 8.0f), y, lw, 28.0f};
    const bool on = setupLayer_ == layers[i];
    PaintChamferBoxRay(box, kChamferSmall, ToRay(on ? p.titleMenuHover : p.titleMenu),
                       ToRay(p.titleMenuBorder), ToRay(p.border));
    const float tw = TextWidth(layerLabels[i], TypePx(10), false);
    Text(layerLabels[i], static_cast<int>(box.x + (box.w - tw) * 0.5f), static_cast<int>(box.y + 7),
         TypePx(10), ToRay(p.titleMenuInk));
    titleHits_.push_back({box, std::string("setup-layer:") + layers[i], ""});
  }

  PaintChamferBoxRay(Area{mapX, mapY, mapW, mapH}, kChamferPanel, ToRay(p.ink, 18),
                     ToRay(p.titleBorder), ToRay(p.border));
  const float side = std::min(mapW, mapH) - 8.0f;
  const float discX = mapX + (mapW - side) * 0.5f;
  const float discY = mapY + (mapH - side) * 0.5f;
  DrawCircle(static_cast<int>(discX + side * 0.5f), static_cast<int>(discY + side * 0.5f),
             side * 0.5f, ToRay(p.ink, 28));
  DrawCircleLines(static_cast<int>(discX + side * 0.5f), static_cast<int>(discY + side * 0.5f),
                  side * 0.5f, ToRay(p.titleBorder));

  auto plot = [&](double x, double yv, double bw, double bh) {
    const float px = static_cast<float>(discX + (x / bw) * side);
    const float py = static_cast<float>(discY + (yv / bh) * side);
    return Vector2{px, py};
  };

  if (setupLayer_ == "galaxy") {
    for (const auto& dot : setupPreview_.galaxies) {
      const Vector2 pt = plot(dot.x, dot.y, setupPreview_.galaxyW, setupPreview_.galaxyH);
      const bool sel = dot.id == setupGalaxyId_;
      DrawCircle(static_cast<int>(pt.x), static_cast<int>(pt.y), sel ? 5.0f : 3.0f,
                 ToRay(sel ? p.terracotta : p.coffee));
      PushFlarePt(FlareTarget::StarPoint, pt.x, pt.y, sel ? 1.1f : 0.85f);
      titleHits_.push_back({Area{pt.x - 8, pt.y - 8, 16, 16}, "setup-pick-galaxy", dot.id});
    }
  } else {
    const double bw = setupPreview_.sectorW;
    const double bh = setupPreview_.sectorH;
    for (const auto& dot : setupPreview_.systems) {
      if (!setupGalaxyId_.empty() && dot.parent != setupGalaxyId_) { continue; }
      if (setupLayer_ == "system" && !setupSystemId_.empty() && dot.id != setupSystemId_) { continue; }
      const Vector2 pt = plot(dot.x, dot.y, bw, bh);
      const bool sel = dot.id == setupSystemId_;
      DrawCircle(static_cast<int>(pt.x), static_cast<int>(pt.y), sel ? 4.5f : 2.5f,
                 ToRay(sel ? p.terracotta : p.coffee));
      PushFlarePt(FlareTarget::StarPoint, pt.x, pt.y, sel ? 1.1f : 0.85f);
      titleHits_.push_back({Area{pt.x - 7, pt.y - 7, 14, 14}, "setup-pick-system", dot.id});
    }
  }

  const bool canLaunch = !setupSystemId_.empty();
  const Area launch{mapX, barY, std::min(180.0f, mapW * 0.4f), 36.0f};
  PaintChamferBoxRay(launch, kChamferCard, ToRay(canLaunch ? p.titleMenuHover : p.titleMenu, canLaunch ? 255 : 140),
                       ToRay(p.titleMenuBorder), ToRay(p.border));
  const float ltw = TextWidth("LAUNCH", TypePx(14), false);
  Text("LAUNCH", static_cast<int>(launch.x + (launch.w - ltw) * 0.5f),
       static_cast<int>(launch.y + 8), TypePx(14),
       ToRay(canLaunch ? p.titleMenuInk : p.muted));
  if (canLaunch) { titleHits_.push_back({launch, "setup-launch", ""}); }

  const Area back{card.x + pad, barY, 120.0f, 36.0f};
  PaintChamferBoxRay(back, kChamferCard, ToRay(p.titleMenu),
                       ToRay(p.titleMenuBorder), ToRay(p.border));
  const float btw = TextWidth("BACK", textPx, false);
  Text("BACK", static_cast<int>(back.x + (120 - btw) * 0.5f), static_cast<int>(back.y + 10),
       textPx, ToRay(p.titleMenuInk));
  titleHits_.push_back({back, "back", ""});

  if (!setupSystemId_.empty()) {
    Text(("Start: " + setupSystemId_).c_str(), static_cast<int>(launch.x + launch.w + 12),
         static_cast<int>(barY + 10), TypePx(10), ToRay(p.titleMenuInk));
  } else {
    Text("Choose a starting system on the map.", static_cast<int>(launch.x + launch.w + 12),
         static_cast<int>(barY + 10), TypePx(10), ToRay(p.titleMenuBorder));
  }
}

void ShellView::PaintTitle(Simulation& sim, double nowMs) {
  // Menu <-> voyage settings: the screen that was up darkens, the new one
  // comes out of the dark. Only when the menu was on screen the frame before.
  const bool setupNow = !titleConfirm_ && titlePanel_ == TitlePanel::Setup;
  const bool continuous = nowMs - titlePaintMs_ < 250.0;
  titlePaintMs_ = nowMs;
  if (continuous && setupNow != titleWasSetup_ &&
      (titlePanel_ == TitlePanel::Setup || titlePanel_ == TitlePanel::None)) {
    fadeKind_ = 1;
    fadeStartMs_ = nowMs;
    fadeFromSetup_ = titleWasSetup_;
  }
  titleWasSetup_ = setupNow;

  PaintTitleBody(sim, nowMs);
  if (fadeKind_ != 1) { return; }
  const double t = nowMs - fadeStartMs_;
  if (t < 0.0 || t >= kDipOutMs + kDipInMs || titleConfirm_ ||
      (titlePanel_ != TitlePanel::Setup && titlePanel_ != TitlePanel::None)) {
    fadeKind_ = 0;
    return;
  }
  const Color dark = ToRay(theme_.palette.titleMenu);
  if (t < kDipOutMs) {
    // The new screen's controls are already live; keep them, show the old one.
    const std::vector<NavHit> live = titleHits_;
    const TitlePanel panel = titlePanel_;
    titlePanel_ = fadeFromSetup_ ? TitlePanel::Setup : TitlePanel::None;
    PaintTitleBody(sim, nowMs);
    titlePanel_ = panel;
    titleHits_ = live;
    DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                  Fade(dark, static_cast<float>(t / kDipOutMs)));
  } else {
    DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                  Fade(dark, 1.0f - static_cast<float>((t - kDipOutMs) / kDipInMs)));
  }
}

void ShellView::PaintTitleBody(Simulation& sim, double nowMs) {
  const Palette& p = theme_.palette;
  titleHits_.clear();
  title_ = BuildTitleScreen(sim, fromVoyage_, titlePanel_, saveDir_, titleSlot_,
                            titleMessage_, titleCard_);
  if (!titleConfirm_ && titlePanel_ == TitlePanel::Setup) {
    PaintSetup(sim);
    return;
  }
  if (!titleConfirm_ && (titlePanel_ == TitlePanel::Guide || titlePanel_ == TitlePanel::Tutorial)) {
    PaintTutorial(sim, nowMs);
    return;
  }

  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                ToRay(p.panel));

  // title.jpg is the complete menu card, not a hero image to decorate with a
  // second, invented menu. Fit the entire 1672x941 artwork without cropping.
  const float ratio = 1672.0f / 941.0f;
  float cardW = std::min(width_ - 24.0f, 1672.0f);
  float cardH = cardW / ratio;
  if (cardH > height_ - 24.0f) { cardH = height_ - 24.0f; cardW = cardH * ratio; }
  const Area card{(width_ - cardW) * 0.5f, (height_ - cardH) * 0.5f, cardW, cardH};
  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), ToRay(p.titleBackground));
  const Texture2D* texture = static_cast<const Texture2D*>(titleArt_);
  const bool hasTitleArt = texture != nullptr && texture->id != 0 &&
                           texture->width > 0 && texture->height > 0;
  if (hasTitleArt) {
    DrawTexturePro(*texture,
                   Rectangle{0.0f, 0.0f, static_cast<float>(texture->width),
                             static_cast<float>(texture->height)},
                   Rectangle{card.x, card.y, card.w, card.h}, Vector2{0.0f, 0.0f}, 0.0f,
                   ToRay(p.white));
    static constexpr float kTitleCorners[][2] = {
        {39.0f, 46.0f}, {1632.0f, 46.0f}, {1644.0f, 640.0f},
        {1379.0f, 905.0f}, {39.0f, 894.0f}};
    for (const auto& c : kTitleCorners) {
      PushFlareCorner(card.x + card.w * (c[0] / 1672.0f), card.y + card.h * (c[1] / 941.0f),
                      0.0f, 0.0f, width_, height_);
    }
  }

  // A missing optional title image must never leave a blank window. Keep the
  // title and all menu actions usable, with explicit native controls as a
  // fallback for packaged builds where an asset might be absent or unreadable.
  if (!hasTitleArt && titlePanel_ == TitlePanel::None && !titleConfirm_) {
    Text("SPACESHIP", static_cast<int>(card.x + 42.0f),
         static_cast<int>(card.y + 48.0f), TypePx(40), ToRay(p.ink));
    Text("A SIMULATION GAME OF DEPTH, PATIENCE AND EVERY DECISION MATTERS",
         static_cast<int>(card.x + 46.0f), static_cast<int>(card.y + 106.0f),
         TypePx(12), ToRay(p.muted));
    float y = card.y + 190.0f;
    for (const ActionRowView& action : title_.actions) {
      const Area box{card.x + 46.0f, y, std::min(360.0f, card.w - 92.0f), 42.0f};
      PaintChamferBoxRay(box, kChamferCard, ToRay(action.enabled ? p.white : p.panel3),
                       ToRay(action.enabled ? p.borderDark : p.border), ToRay(p.border));
      Text(action.label.c_str(), static_cast<int>(box.x + 14.0f),
           static_cast<int>(box.y + 12.0f), TypePx(12),
           ToRay(action.enabled ? p.ink : p.muted));
      if (action.enabled) { titleHits_.push_back(NavHit{box, action.id, ""}); }
      y += 52.0f;
    }
    return;
  }

  // The art says DESIGN / MANAGE / ...; these are decorative, not menu
  // controls. HTA #title-actions paints its own five commands over that strip.
  // The strip painted into title.jpg runs from x = 36 to 1397 of 1672, short of
  // the frame line on the left and the diagonal on the right.
  const Area menu{card.x + card.w * 0.0204f, card.Bottom() - card.h * (0.0405f + 0.0565f),
                  card.w * 0.8128f, std::max(card.h * 0.0565f, theme_.LineHeight() + 12)};
  const Color menuBg = ToRay(p.titleMenu), menuText = ToRay(p.titleMenuInk);
  const float cell = menu.w / static_cast<float>(title_.actions.size());
  for (std::size_t i = 0; i < title_.actions.size(); ++i) {
    const Area box{menu.x + cell * i + 2.0f, menu.y, cell - 4.0f, menu.h};
    const bool hover = box.Contains(mouseX_, mouseY_);
    PaintChamferBoxRay(box, kChamferSmall, hover ? ToRay(p.titleMenuHover) : menuBg,
                       ToRay(p.titleMenuBorder), ToRay(p.border));
    const auto& action = title_.actions[i];
    const float tw = TextWidth(action.label.c_str(), TypePx(12), true);
    Text(action.label.c_str(), static_cast<int>(box.x + (box.w - tw) / 2),
         static_cast<int>(box.y + (box.h - TypePx(12)) / 2), TypePx(12), menuText, true);
    if (titlePanel_ == TitlePanel::None && !titleConfirm_ && action.enabled) { titleHits_.push_back({box, action.id, ""}); }
  }
  if (titlePanel_ == TitlePanel::None && !titleConfirm_) { return; }

  // HTA #title-dialog is centred, parchment, vertically scrollable. There is
  // no second title or left-hand command column on top of the supplied art.
  const float w = std::min(560.0f, width_ * 0.9f);
  const float maxSheetH = std::max(120.0f, height_ * 0.86f);
  ImGui::SetNextWindowPos(ImVec2(width_ / 2, height_ / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, maxSheetH));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, ToIm(p.titleInk));
  ImGui::PushStyleColor(ImGuiCol_Border, ToIm(p.titleBorder));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 24));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  if (ImGui::Begin("##title-sheet", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground)) {
    const ImVec2 winPos = ImGui::GetWindowPos();
    const std::size_t flareMark = flareTargets_.size();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);

    const float bodyW = std::max(1.0f, w - 52.0f);
    const float bodyMaxH = std::max(60.0f, maxSheetH - 48.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, bodyMaxH));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::BeginChild("##title-body", ImVec2(bodyW, 0.0f), ImGuiChildFlags_AutoResizeY)) {
      auto sheetButton = [&](const std::string& label, const std::string& id, float reqWidth,
                             bool enabled = true) {
        ImGui::PushID(id.c_str());
        const float bw = reqWidth > 0.0f ? reqWidth
                                         : std::max(64.0f, ImGui::CalcTextSize(label.c_str()).x + 24.0f);
        const auto textSz = ImGui::CalcTextSize(label.c_str(), nullptr, false, std::max(1.0f, bw - 20.0f));
        const float bh = textSz.y + 14.0f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("sheetbtn", ImVec2(bw, bh));
        ImDrawList* bdraw = ImGui::GetWindowDrawList();
        const ImU32 fill = ToIm(enabled && ImGui::IsItemHovered() ? p.panel3 : p.panel2);
        PaintChamferBoxIm(bdraw, at.x, at.y, bw, bh, kChamferSmall, fill,
                          ToIm(p.titleBorder), ToIm(p.border));
        const float tx = reqWidth > 0.0f ? at.x + 10.0f : at.x + (bw - textSz.x) * 0.5f;
        bdraw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, at.y + 7.0f),
                       ToIm(enabled ? p.titleInk : p.muted), label.c_str(), nullptr,
                       std::max(1.0f, bw - 20.0f));
        ImGui::PopID();
        return pressed;
      };

      ImGui::TextWrapped("%s", titleConfirm_ ? "New voyage" : title_.panelTitle.c_str());
      ImGui::Spacing();
      if (titleConfirm_) {
        ImGui::TextWrapped("%s", titleMessage_.c_str());
        ImGui::Spacing();
        if (sheetButton("OK", "confirm-ok", 0.0f)) {
          PressTitleAction(sim, "new-voyage", "", &fromVoyage_, &titlePanel_, &titleSlot_, saveDir_, &titleMessage_, &titleActive_);
          titleConfirm_ = false;
          setupPreviewDirty_ = true;
          setupSystemId_.clear();
          setupGalaxyId_.clear();
          setupLayer_ = "galaxy";
        }
        ImGui::SameLine();
        if (sheetButton("Cancel", "confirm-cancel", 0.0f)) { titleConfirm_ = false; titleMessage_.clear(); }
      } else if (titlePanel_ == TitlePanel::Settings) {
        ImGui::TextWrapped("%s", title_.settings.notice.c_str());
        PaintSettingsControls(sim, title_.settings);
        ImGui::Spacing();
        if (sheetButton("BACK", "settings-back", 0.0f)) { titlePanel_ = TitlePanel::None; }
      } else {
        ImGui::TextWrapped("%s", title_.panelNote.c_str());
        const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        for (std::size_t idx = 0; idx < title_.panelRows.size(); ++idx) {
          const auto& row = title_.panelRows[idx];
          ImGui::BeginDisabled(!row.enabled);
          if (sheetButton(row.label, row.id + "#" + std::to_string(idx), avail, row.enabled)) {
            PressTitleAction(sim, row.id, "", &fromVoyage_, &titlePanel_, &titleSlot_, saveDir_, &titleMessage_, &titleActive_);
          }
          ImGui::EndDisabled();
        }
        if (!titleMessage_.empty()) { ImGui::TextWrapped("%s", titleMessage_.c_str()); }
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    const float finalH =
        std::clamp(ImGui::GetItemRectMax().y - winPos.y + 24.0f, 64.0f, maxSheetH);
    if (gFlare.out != nullptr) {
      flareTargets_.insert(flareTargets_.begin() + static_cast<std::ptrdiff_t>(flareMark),
                           FlareTarget{FlareTarget::Occlude, winPos.x, winPos.y,
                                       winPos.x + w, winPos.y + finalH, 0.0f});
    }
    draw->ChannelsSetCurrent(0);
    draw->PushClipRect(ImVec2(winPos.x - 2.0f, winPos.y - 2.0f),
                       ImVec2(winPos.x + w + 2.0f, winPos.y + finalH + 2.0f), false);
    PaintChamferBoxIm(draw, winPos.x, winPos.y, w, finalH, kChamferPanel,
                      ToIm(p.titleDialog), ToIm(p.titleBorder), ToIm(p.border));
    draw->PopClipRect();
    draw->ChannelsMerge();
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(3);
}

// ============================================================================
//  The loss overlay: the voyage ended, and the run's own record with it.
// ============================================================================
void ShellView::PaintLoss(Simulation& sim) {
  const Palette& p = theme_.palette;
  lossHits_.clear();
  loss_ = BuildLossScreen(sim, lossSlot_, lossMessage_);

  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                ToRay(p.panel, 226));
  if (gFlare.out != nullptr) {
    flareTargets_.push_back(FlareTarget{FlareTarget::Occlude, 0.0f, 0.0f, width_, height_, 0.0f});
  }
  const float cardW = std::min(420.0f, width_ - 60.0f);
  const Area card{(width_ - cardW) * 0.5f, 90.0f, cardW, std::min(560.0f, height_ - 120.0f)};
  PaintChamferBoxRay(card, kChamferPanel, ToRay(p.panel),
                       ToRay(p.borderDark), ToRay(p.border));

  float y = card.y + 18.0f;
  Text(loss_.heading.c_str(), static_cast<int>(card.x + 20.0f),
           static_cast<int>(y), TypePx(24) , ToRay(p.terracotta));
  y += 34.0f;
  for (const std::string& line : WrapText(loss_.cause, card.w - 40.0f, TypePx(12), SelectFont(TypePx(12), false))) {
    Text(line.c_str(), static_cast<int>(card.x + 20.0f), static_cast<int>(y), TypePx(12) ,
             ToRay(p.ink));
    y += 16.0f;
  }
  y += 4.0f;
  for (const std::string& line : WrapText(loss_.note, card.w - 40.0f, TypePx(10), SelectFont(TypePx(10), false))) {
    Text(line.c_str(), static_cast<int>(card.x + 20.0f), static_cast<int>(y), TypePx(10) ,
             ToRay(p.muted));
    y += 13.0f;
  }
  y += 10.0f;
  for (const PopupRow& row : loss_.summary) {
    Text(row.key.c_str(), static_cast<int>(card.x + 20.0f), static_cast<int>(y), TypePx(10) ,
             ToRay(p.muted));
    const float valueW = static_cast<float>(TextWidth(row.value.c_str(), TypePx(10) ));
    Text(row.value.c_str(), static_cast<int>(card.Right() - 20.0f - valueW),
             static_cast<int>(y), TypePx(10) , ToRay(p.ink));
    y += 14.0f;
  }
  y += 10.0f;
  for (const ActionRowView& action : loss_.actions) {
    if (y + 28.0f > card.Bottom() - 12.0f) { break; }
    const Area box{card.x + 20.0f, y, card.w - 40.0f, 26.0f};
    PaintChamferBoxRay(box, kChamferSmall, ToRay(p.panel2),
                       ToRay(p.borderDark), ToRay(p.border));
    Text(action.label.c_str(), static_cast<int>(box.x + 10.0f),
             static_cast<int>(box.y + 7.0f), TypePx(10) , ToRay(p.ink));
    lossHits_.push_back(NavHit{box, action.id, ""});
    y += 30.0f;
  }
  if (!lossMessage_.empty()) {
    Text(lossMessage_.c_str(), static_cast<int>(card.x + 20.0f),
             static_cast<int>(card.Bottom() - 18.0f), TypePx(9) , ToRay(p.coffee));
  }
}

// The handbook's search box, drawn as a real field over the index column.
void ShellView::PaintWikiSearch(const Area& viewport) {
  if (panelsKind_ != ScreenKind::Wiki) { return; }
  const Area box{layout_.content.x + 8.0f, viewport.y, 260.0f, 28.0f};
  ImGui::SetNextWindowPos(ImVec2(box.x - 4.0f, box.y - 4.0f));
  ImGui::SetNextWindowSize(ImVec2(box.w + 8.0f, box.h + 8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
  const bool host = ImGui::Begin("##wiki-search-host", nullptr,
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBackground);
  if (!host) { ImGui::End(); ImGui::PopStyleVar(); return; }
  ImDrawList* draw = ImGui::GetWindowDrawList();
  PaintChamferBoxIm(draw, box.x, box.y, box.w, box.h, kChamferSmall,
                    ToIm(theme_.palette.panel2), ToIm(theme_.palette.borderDark),
                    ToIm(theme_.palette.border));
  ImGui::SetNextItemWidth(box.w);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, ToIm(theme_.palette.ink));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));
  if (ImGui::InputTextWithHint("##wiki-search", "Search the handbook", wikiQuery_,
                               sizeof(wikiQuery_))) {
    wiki_.query = wikiQuery_;
  }
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
  ImGui::End();
  ImGui::PopStyleVar();
}

void ShellView::PaintPanelsScreen(Simulation& sim) {
  navHits_.clear();
  taskButtons_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = taskScreen_;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  const float headerH = PaintTaskHeader(screen.head, left, layout_.content.y);

  const float searchTop = layout_.content.y + headerH + 8.0f;
  PaintWikiSearch(Area{layout_.content.x, searchTop, layout_.content.w, 0.0f});
  float top = searchTop + (panelsKind_ == ScreenKind::Wiki ? 38.0f : 0.0f);
  if (screen.panels.size() == 3 && screen.panels.front().id == "bridge-controls") {
    NavPanel voyagePanel;
    voyagePanel.id = screen.panels[1].id;
    voyagePanel.title = screen.panels[1].title;
    NavPanel coursePanel;
    coursePanel.id = "bridge-course";
    coursePanel.title = "Current location / course and alerts";
    bool inCourse = false;
    for (const PanelLine& item : screen.panels[1].lines) {
      if (!inCourse && item.kind == PanelLine::Kind::Sub &&
          item.value == "Current location / course and alerts") {
        inCourse = true;
        continue;
      }
      if (!inCourse) {
        if (item.kind == PanelLine::Kind::Sub && item.value.empty()) { continue; }
        voyagePanel.lines.push_back(item);
      } else {
        coursePanel.lines.push_back(item);
      }
    }

    const float availW = std::max(1.0f, right - left);
    const float colGap = 12.0f;
    const float innerW = std::max(1.0f, availW - 2.0f * colGap);
    const float voyageW = std::max(275.0f, std::floor(innerW * 0.235f));
    const float courseW =
        std::max({304.0f, std::floor(innerW * 0.31f),
                  TextWidth(coursePanel.title.c_str(), TypePx(11)) + 24.0f});
    const float feedW = std::max(1.0f, innerW - voyageW - courseW);

    const float panelH = std::max({MeasurePanelHeight(screen.panels[2], feedW),
                                   MeasurePanelHeight(voyagePanel, voyageW),
                                   MeasurePanelHeight(coursePanel, courseW)});
    const float imageTop = top + 4.0f;
    const float bottomGap = 10.0f;
    const float bottomPad = 6.0f;
    const float availStageH =
        std::max(160.0f, layout_.content.Bottom() - bottomPad - panelH - bottomGap - imageTop);

    TaskStage bridgeStage = screen.stage;
    bridgeStage.width = 960.0f;
    bridgeStage.height = 460.0f;
    const float fitScale =
        std::min(availW / bridgeStage.width, availStageH / bridgeStage.height);
    bridgeStage.typePx = theme_.FontPx() / fitScale;
    const float scale = StageScale(bridgeStage, theme_);
    const float stageW = bridgeStage.width * scale;
    const float stageH = bridgeStage.height * scale;
    const float stageX = left + (availW - stageW) * 0.5f;

    PaintStage(sim, bridgeStage, Area{stageX, imageTop, stageW, stageH});

    // Embed the two live gameplay controls inside the lower Command Bay sockets.
    // The controls live inside the scaled stage image, so their text scales with
    // the image and each box stays clamped inside its socket at every resolution.
    if (!screen.panelRows.empty()) {
      const bool hasDispatch = screen.panelRows[0].size() == 3;
      const float sideBtnW = (hasDispatch ? 404.0f : 422.0f) * scale;
      const float midBtnW = 82.0f * scale;
      const int lblPx = std::max(11, static_cast<int>(std::lround(TypePx(10) * scale)));
      const int badPx = std::max(10, static_cast<int>(std::lround(TypePx(9) * scale)));
      const float btnLine = std::max(14.0f, static_cast<float>(std::lround(lblPx * 1.32f)));
      const float btnPad = 11.0f * scale;
      float buttonH = 48.0f * scale;
      for (std::size_t r = 0; r < screen.panelRows[0].size(); ++r) {
        const CommandRowView& row = screen.panelRows[0][r];
        const bool isMid = hasDispatch && r == 1;
        if (isMid) { continue; }
        const float rowW = sideBtnW;
        const std::string cost =
            row.hideCost ? std::string() : (row.displayCost.empty() ? "1 click" : row.displayCost);
        const float badgeW = cost.empty() ? 0.0f : (TextWidth(cost.c_str(), badPx) + 14.0f * scale);
        const float labelW = std::max(60.0f * scale,
                                      rowW - 2.0f * btnPad - (badgeW > 0.0f ? badgeW + 10.0f * scale : 0.0f));
        const auto lines = WrapText(row.label, labelW, lblPx, SelectFont(lblPx, false));
        buttonH = std::max(buttonH,
                           static_cast<float>(lines.size()) * btnLine + 14.0f * scale);
      }
      // The sockets run 375..451 in stage space; keep the controls inside them.
      const float socketTop = imageTop + 375.0f * scale;
      const float socketBottom = imageTop + 451.0f * scale;
      float btnY = imageTop + 413.0f * scale - buttonH * 0.5f;
      if (btnY < socketTop) { btnY = socketTop; }
      const float socketMaxY = socketBottom - buttonH;
      if (btnY > socketMaxY) { btnY = socketMaxY; }

      for (std::size_t r = 0; r < screen.panelRows[0].size(); ++r) {
        const CommandRowView& row = screen.panelRows[0][r];
        const bool isMid = hasDispatch && r == 1;
        const float bx = stageX + (hasDispatch ? (r == 0 ? 23.0f : (r == 1 ? 439.0f : 533.0f))
                                               : (r == 0 ? 23.0f : 515.0f)) * scale;
        const float rowW = isMid ? midBtnW : sideBtnW;
        const float rowH = isMid ? std::max(28.0f * scale, buttonH * 0.72f) : buttonH;
        const float rowY = isMid ? (btnY + (buttonH - rowH) * 0.5f) : btnY;
        const Area box{bx, rowY, rowW, rowH};
        const bool hovered = row.enabled && layout_.content.Contains(mouseX_, mouseY_) &&
                             box.Contains(mouseX_, mouseY_ + contentScroll_);

        if (isMid) {
          const Color fillCol = bridgeDispatch_ ? ToRay(p.amber) : ToRay(p.terracotta);
          FillChamferRay(box, kChamferCard, fillCol);
          PaintChamferDoubleLinesRay(box, kChamferCard,
                                     ToRay(hovered ? p.coffeeDark : p.borderDark),
                                     ToRay(hovered ? p.amber : p.border));
        } else {
          FillChamferRay(box, kChamferCard, ToRay(row.enabled ? p.control : p.panel3));
          if (row.charged) { PaintProgressFill(box, row.chargePercent, p, kChamferCard); }
          PaintChamferDoubleLinesRay(box, kChamferCard,
                                     ToRay(hovered ? p.coffeeDark : p.borderDark),
                                     ToRay(hovered ? p.amber : p.border));
        }

        const std::string cost =
            row.hideCost ? std::string() : (row.displayCost.empty() ? "1 click" : row.displayCost);
        const float badgeW = cost.empty() ? 0.0f : (TextWidth(cost.c_str(), badPx) + 14.0f * scale);
        if (isMid) {
          int fitPx = std::max(7, static_cast<int>(std::lround(TypePx(7) * scale)));
          const float maxMidW = std::max(24.0f, box.w - 10.0f * scale);
          while (fitPx > 6 && TextWidth(row.label.c_str(), fitPx, false) > maxMidW) {
            --fitPx;
          }
          const float tw = TextWidth(row.label.c_str(), fitPx, false);
          const float lineX = box.x + (box.w - tw) * 0.5f;
          const float textY = box.y + (box.h - static_cast<float>(fitPx)) * 0.5f - 0.5f;
          Text(row.label.c_str(), static_cast<int>(lineX), static_cast<int>(textY), fitPx,
               Color{252, 248, 240, 255}, false);
        } else {
          const float labelW = std::max(60.0f * scale,
                                        box.w - 2.0f * btnPad - (badgeW > 0.0f ? badgeW + 10.0f * scale : 0.0f));
          const auto lines = WrapText(row.label, labelW, lblPx, SelectFont(lblPx, false));
          const float totalTextH = static_cast<float>(lines.size()) * btnLine;
          float textY = box.y + std::max(4.0f * scale, (box.h - totalTextH) * 0.5f);
          for (const std::string& line : lines) {
            Text(line.c_str(), static_cast<int>(box.x + btnPad), static_cast<int>(textY), lblPx,
                 ToRay(row.enabled ? p.ink : p.muted), false);
            textY += btnLine;
          }
        }
        if (!cost.empty()) {
          const float badgeH = std::max(20.0f * scale, static_cast<float>(badPx) + 10.0f * scale);
          const Area badge{box.Right() - badgeW - 9.0f * scale, box.y + (box.h - badgeH) * 0.5f,
                           badgeW, badgeH};
          PaintChamferBoxRay(badge, kChamferSmall, ToRay(p.panel2),
                             ToRay(p.borderDark), ToRay(p.border));
          Text(cost.c_str(), static_cast<int>(badge.x + 7.0f * scale),
               static_cast<int>(badge.y + (badge.h - static_cast<float>(badPx)) * 0.5f - 1.0f),
               badPx, ToRay(row.enabled ? p.coffee : p.muted));
        }
        if (row.enabled && r < screen.panelRowIds[0].size()) {
          navHits_.push_back(NavHit{box, "row", screen.panelRowIds[0][r]});
        }
      }
    }

    const float bottomTop = imageTop + stageH + bottomGap;
    PaintPanel(screen.panels[2], Area{left, bottomTop, feedW, panelH});
    PaintPanel(voyagePanel, Area{left + feedW + colGap, bottomTop, voyageW, panelH});
    PaintPanel(coursePanel, Area{left + feedW + voyageW + 2.0f * colGap, bottomTop, courseW, panelH});
    contentPaintBottom_ = std::max(contentPaintBottom_, bottomTop + panelH);
    return;
  }
  if (screen.hasHero) {
    PaintHero(screen.hero, Area{left, top, right - left, screen.hero.hero.h});
    top += screen.hero.hero.h + 8.0f;
  }
  float panelsBottom = top;
  const bool grouped = screen.panelColumns.size() == screen.panels.size() && !screen.panels.empty();
  std::size_t count = screen.panels.empty() ? 1 : screen.panels.size();
  if (grouped) {
    count = 1;
    for (int column : screen.panelColumns) {
      count = std::max(count, static_cast<std::size_t>(column + 1));
    }
  }
  const float gap = 8.0f;
  const float columnW = (right - left - gap * static_cast<float>(count - 1)) /
                        static_cast<float>(count);
  std::vector<float> columnY(count, top);
  struct RowMetrics {
    std::vector<std::string> lines;
    std::string cost;
    float badgeW = 0;
    float height = 0;
    bool inlineCost = false;
  };
  const auto measureRow = [&](const CommandRowView& row, float rowW) {
    RowMetrics m;
    m.cost = row.hideCost ? std::string()
                          : (row.displayCost.empty() ? "free" : row.displayCost);
    m.badgeW = m.cost.empty() ? 0.0f : TextWidth(m.cost.c_str(), TypePx(9)) + 16.0f;
    m.inlineCost = grouped && !row.reading && (m.cost.empty() || rowW >= m.badgeW + 118.0f);
    const float labelW = std::max(1.0f, rowW - 22.0f - (m.inlineCost ? m.badgeW + 10.0f : 0.0f));
    m.lines = WrapText(row.label, labelW, TypePx(10), SelectFont(TypePx(10), false));
    m.height = row.reading ? m.lines.size() * theme_.LineHeight() + 6.0f
        : (m.lines.size() + (m.inlineCost ? 0 : 1)) * theme_.LineHeight() + 16.0f;
    return m;
  };
  for (std::size_t i = 0; i < screen.panels.size(); ++i) {
    const std::size_t column = grouped ? static_cast<std::size_t>(screen.panelColumns[i]) : i;
    const float x = left + static_cast<float>(column) * (columnW + gap);
    const float originalTop = columnY[column];
    const NavPanel& panel = screen.panels[i];
    const float noteH = MeasurePanelHeight(panel, columnW);

    // The station's panels contain their rows, as in the HTA, and the sales /
    // board and shipyard / tavern panels stack in their shared columns.
    float rowsH = 0;
    if (i < screen.panelRows.size()) {
      for (const CommandRowView& row : screen.panelRows[i]) {
        const RowMetrics m = measureRow(row, columnW - (grouped ? 16.0f : 0.0f));
        rowsH += m.height + (row.reading ? 0.0f : 3.0f);
      }
    }
    // Keep the selected handbook article alongside the scrolling index. For
    // a tall article, let it scroll until its bottom is visible before pinning
    // it; never make the lower text unreachable. Its sticky offset must not
    // grow the document's scroll extent or its click coordinates will drift.
    const bool stickyWiki = panelsKind_ == ScreenKind::Wiki && panel.id == "wiki-article";
    const float panelHeight = noteH + 6.0f + rowsH;
    const float anchor = std::min(layout_.content.y + 8.0f,
                                  layout_.content.Bottom() - 8.0f - panelHeight);
    const float stickyShift = stickyWiki ? std::max(0.0f, contentScroll_ + anchor - originalTop) : 0.0f;
    const float panelTop = originalTop + stickyShift;
    const bool tracked = paintingContent_;
    if (stickyWiki) { paintingContent_ = false; }
    if (grouped && rowsH > 0) {
      PaintChamferBoxRay(Area{x, panelTop, columnW, noteH + rowsH + 14.0f}, kChamferPanel,
                         ToRay(p.panel), ToRay(p.borderDark), ToRay(p.border));
    }
    PaintPanel(panel, Area{x, panelTop, columnW, noteH});

    float y = panelTop + noteH + 6.0f;
    if (i < screen.panelRows.size()) {
      for (std::size_t r = 0; r < screen.panelRows[i].size(); ++r) {
        const CommandRowView& row = screen.panelRows[i][r];
        const float inset = grouped ? 8.0f : 0.0f;
        const float rowW = columnW - 2 * inset;
        const RowMetrics m = measureRow(row, rowW);
        if (row.reading) {
          for (const auto& line : m.lines) {
            Text(line.c_str(), static_cast<int>(x + inset + 6.0f), static_cast<int>(y), TypePx(10), ToRay(p.muted));
            y += theme_.LineHeight();
          }
          y += 6.0f;
          continue;
        }
        const float rowH = m.height;
        const Area box{x + inset, y, rowW, rowH};
        if (row.plain) {
          if (row.selected) {
            PaintChamferBoxRay(box, kChamferSmall, ToRay(p.panel3), ToRay(p.borderDark),
                               ToRay(p.border));
            DrawRectangle(static_cast<int>(box.x + 5.0f), static_cast<int>(box.y + 5.0f), 3,
                          static_cast<int>(std::max(1.0f, box.h - 10.0f)), ToRay(p.coffee));
          }
          float lineTop = box.y + 8.0f;
          for (const auto& line : m.lines) {
            Text(line.c_str(), static_cast<int>(box.x + 11.0f), static_cast<int>(lineTop),
                 TypePx(10), ToRay(row.live ? p.ink : p.muted));
            lineTop += theme_.LineHeight();
          }
          if (row.enabled && i < screen.panelRowIds.size() && r < screen.panelRowIds[i].size()) {
            navHits_.push_back(NavHit{box, "row", screen.panelRowIds[i][r]});
          }
          y += rowH + 3.0f;
          continue;
        }
        FillChamferRay(box, kChamferCard,
                       ToRay(row.selected ? p.panel3
                                          : (row.enabled ? (grouped ? p.control : p.panel2)
                                                         : p.panel3)));
        if (row.charged) { PaintProgressFill(box, row.chargePercent, p, kChamferCard); }
        PaintChamferDoubleLinesRay(
            box, kChamferCard,
            ToRay(row.selected ? p.coffee : (row.enabled ? p.borderDark : p.border)),
            ToRay(row.selected ? p.coffeeLight : p.border));
        if (row.selected) {
          DrawRectangle(static_cast<int>(box.x + 5.0f), static_cast<int>(box.y + 5.0f), 3,
                        static_cast<int>(std::max(1.0f, box.h - 10.0f)), ToRay(p.coffee));
        }
        float textTop = box.y + 8.0f;
        const float textLeft = box.x + (row.selected ? 11.0f : 10.0f);
        for (const auto& line : m.lines) {
          Text(line.c_str(), static_cast<int>(textLeft), static_cast<int>(textTop), TypePx(10), ToRay(row.enabled ? p.ink : p.muted));
          textTop += theme_.LineHeight();
        }
        if (m.inlineCost) {
          if (!m.cost.empty()) {
            const float badgeX = box.Right() - m.badgeW - 7.0f;
            const Area badgeBox{badgeX, box.y + 5.0f, m.badgeW, theme_.LineHeight() + 4.0f};
            PaintChamferBoxRay(badgeBox, kChamferSmall, ToRay(p.panel2),
                               ToRay(p.borderDark), ToRay(p.border));
            Text(m.cost.c_str(), static_cast<int>(badgeX + 8.0f), static_cast<int>(box.y + 7.0f),
                 TypePx(9), ToRay(row.enabled ? p.coffee : p.muted));
          }
        } else if (!m.cost.empty()) {
          Text(m.cost.c_str(), static_cast<int>(textLeft), static_cast<int>(textTop), TypePx(9), ToRay(row.enabled ? p.coffee : p.muted));
        }
        if (row.enabled && i < screen.panelRowIds.size() && r < screen.panelRowIds[i].size()) {
          navHits_.push_back(NavHit{box, "row", screen.panelRowIds[i][r]});
        }
        y += rowH + 3.0f;
      }
    }
    if (grouped && rowsH > 0) { y += 6.0f; }
    columnY[column] = y - stickyShift + gap;
    panelsBottom = std::max(panelsBottom, y - stickyShift);
    paintingContent_ = tracked;
  }

  contentPaintBottom_ = std::max(contentPaintBottom_, panelsBottom);
  auto buttons = screen.progress;
  std::string note = screen.bandNote;
  if (!pendingConfirm_.empty()) {
    ProgressButton yes; yes.taskId = "confirm-yes"; yes.text = "Yes, do it";
    ProgressButton no; no.taskId = "confirm-no"; no.text = "Keep as it is";
    buttons.push_back(yes); buttons.push_back(no);
  }
  if (!buttons.empty() || !note.empty()) PaintActionBand("Work in progress", note, buttons, pendingConfirm_);
  (void)sim;
}

// ============================================================================
//  The floating popups (Section K). They never replace the window: a veil and
//  a card float over whatever is behind, and every control inside is free.
//  Successive popups step down and right by (count % 5) * 22, as in the
//  reference, so the reader can see one behind another.
// ============================================================================
void ShellView::PaintPopup(Simulation& sim) {
  const auto& p = theme_.palette;
  // One standard movable chrome for every HTA showPopup window. Fixed width,
  // growing body up to viewport height, then native scrolling; no text cutoffs.
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, ToIm(p.ink));
  ImGui::PushStyleColor(ImGuiCol_Border, ToIm(p.borderDark));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_Button, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToIm(p.panel3));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToIm(p.coffeeLight));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_Header, ToIm(p.panel3));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  std::vector<int> closed;
  bool showMenu = false;
  bool restarted = false;
  for (auto& panel : floating_.Panels()) {
    // Briefings and manifest are opening-time snapshots. The log, settings
    // and file controls refresh in place, as their HTA handlers require.
    if (panel.key == "manifest" && panel.brief.title.empty()) {
      panel.brief = BuildPopup(sim, panel.key, saveDir_);
    }
    const auto model = panel.key.empty() || panel.key == "manifest"
        ? panel.brief : BuildPopup(sim, panel.key, saveDir_);
    const std::string name = model.title + "###ship-popup-" + std::to_string(panel.serial);
    const float w = std::min(520.0f, std::max(220.0f, width_ - 32.0f));
    const float maxH = std::max(100.0f, height_ - 48.0f);
    ImGui::SetNextWindowPos(ImVec2(std::max(0.0f, std::min(panel.x, width_ - w - 12.0f)),
                                std::max(0.0f, std::min(panel.y, height_ - 100.0f))), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, maxH));
    bool open = true;
    const bool visible = ImGui::Begin(name.c_str(), nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
      focusedPanel_ = panel.serial;
    }
    if (visible) {
      const ImVec2 winPos = ImGui::GetWindowPos();
      const std::size_t flareMark = flareTargets_.size();
      ImDrawList* winDraw = ImGui::GetWindowDrawList();
      const float headerH = std::max(26.0f, ImGui::GetFontSize() + 10.0f);
      const float hdrX = winPos.x + kDoubleInset + 1.0f;
      const float hdrY = winPos.y + kDoubleInset + 1.0f;
      const float hdrW = std::max(1.0f, w - 2.0f * (kDoubleInset + 1.0f));
      const float closeW = 24.0f;
      const float closeH = std::max(18.0f, headerH - 6.0f);
      const float dragW = std::max(24.0f, hdrW - closeW - 14.0f);

      winDraw->ChannelsSplit(2);
      winDraw->ChannelsSetCurrent(1);

      ImGui::SetCursorScreenPos(ImVec2(hdrX + 6.0f, hdrY + 2.0f));
      ImGui::InvisibleButton("##popup-drag", ImVec2(dragW, headerH - 4.0f));
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        const ImVec2 winSz = ImGui::GetWindowSize();
        ImGui::SetWindowPos(
            ImVec2(std::max(0.0f, std::min(width_ - w, winPos.x + delta.x)),
                   std::max(0.0f, std::min(height_ - std::max(40.0f, winSz.y), winPos.y + delta.y))));
      }
      winDraw->PushClipRect(ImVec2(hdrX + 8.0f, hdrY), ImVec2(hdrX + dragW, hdrY + headerH), true);
      winDraw->AddText(ImVec2(hdrX + 10.0f, hdrY + (headerH - ImGui::GetFontSize()) * 0.5f),
                       ToIm(p.ink), model.title.c_str());
      winDraw->PopClipRect();

      ImGui::SetCursorScreenPos(ImVec2(hdrX + hdrW - closeW - 6.0f, hdrY + (headerH - closeH) * 0.5f));
      if (ImGui::InvisibleButton("##popup-close", ImVec2(closeW, closeH))) {
        open = false;
      }
      const ImVec2 closeAt = ImGui::GetItemRectMin();
      PaintChamferBoxIm(winDraw, closeAt.x, closeAt.y, closeW, closeH, 3.0f,
                        ToIm(ImGui::IsItemHovered() ? p.panel3 : p.panel),
                        ToIm(p.borderDark), ToIm(p.border));
      const ImVec2 xSz = ImGui::CalcTextSize("x");
      winDraw->AddText(ImVec2(closeAt.x + (closeW - xSz.x) * 0.5f,
                              closeAt.y + (closeH - xSz.y) * 0.5f),
                       ToIm(p.ink), "x");

      ImGui::SetCursorScreenPos(ImVec2(winPos.x + 14.0f, hdrY + headerH + 8.0f));
      const float bodyW = std::max(1.0f, w - 28.0f);
      const float bodyMaxH = std::max(60.0f, maxH - headerH - 28.0f);
      ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, bodyMaxH));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
      if (ImGui::BeginChild("##popup-body", ImVec2(bodyW, 0.0f), ImGuiChildFlags_AutoResizeY)) {
        ImGui::BeginDisabled(floating_.HasConfirmation() && panel.pendingAction.empty());
        auto text = [](const std::string& value) { ImGui::TextWrapped("%s", value.c_str()); };
        auto heading = [&](const std::string& value) { ImGui::Spacing(); text(value); };
        auto action = [&](const std::string& command, const std::string& arg = "") {
          PressPopupAction(sim, panel.key, command, arg, saveDir_, &panel.message);
        };
        // Wrap labels as well as paragraphs. InvisibleButton creates a complete,
        // measured hitbox for each multiline command instead of clipping Button.
        auto button = [&](const std::string& label, const std::string& id) {
          ImGui::PushID(id.c_str());
          const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
          const auto size = ImGui::CalcTextSize(label.c_str(), nullptr, false, std::max(1.0f, width - 20.0f));
          const float bh = size.y + 14.0f;
          const ImVec2 at = ImGui::GetCursorScreenPos();
          const bool pressed = ImGui::InvisibleButton("control", ImVec2(width, bh));
          auto* draw = ImGui::GetWindowDrawList();
          PaintChamferBoxIm(draw, at.x, at.y, width, bh, kChamferSmall,
                            ToIm(ImGui::IsItemHovered() ? p.panel3 : p.panel2),
                            ToIm(p.borderDark), ToIm(p.border));
          draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + 10.0f, at.y + 7.0f),
                        ToIm(p.ink), label.c_str(), nullptr, std::max(1.0f, width - 20.0f));
          ImGui::PopID();
          return pressed;
        };
        if (!model.notice.empty()) { text(model.notice); ImGui::Spacing(); }
        for (const auto& section : model.sections) {
          if (!section.title.empty()) { heading(section.title); ImGui::Separator(); }
          for (const auto& row : section.rows) {
            text(row.key);
            ImGui::Indent(12); text(row.value); ImGui::Unindent(12);
            ImGui::Spacing();
          }
        }
        for (const auto& line : model.lines) { text(line); ImGui::Spacing(); }
        if (!model.acts.empty()) {
          heading("Actions in this window"); ImGui::Separator();
          for (const auto& act : model.acts) {
            text(act.cost + "  " + act.label + (act.live ? " - wired" : ""));
          }
        }
        for (const auto& line : model.footer) { ImGui::Spacing(); text(line); }
        if (panel.key == "settings") { PaintSettingsControls(sim, model); }
        if (!model.slots.empty()) {
          ImGui::BeginDisabled(!panel.pendingAction.empty());
          heading("Save slot");
          ImGui::SetNextItemWidth(-1);
          if (ImGui::BeginCombo("##slot", model.slots[panel.slot - 1].label.c_str())) {
            for (const auto& slot : model.slots) {
              if (ImGui::Selectable(slot.label.c_str(), slot.slot == panel.slot)) { panel.slot = slot.slot; }
            }
            ImGui::EndCombo();
          }
          const struct { const char* command; const char* label; } commands[] = {
              {"save", "Save voyage"}, {"load", "Load voyage"},
              {"new-voyage", "Start a new voyage"}, {"main-menu", "Return to main menu"}};
          for (const auto& command : commands) {
            if (!button(command.label, command.command)) { continue; }
            const std::string cmd = command.command;
            if (cmd == "main-menu") { showMenu = true; }
            else if (cmd == "new-voyage" || sim.State().options.confirmRisk) {
              panel.pendingAction = cmd;
              panel.pendingSlot = panel.slot;
              panel.question = cmd == "new-voyage"
                  ? "Start a new voyage? Unsaved progress will be lost; saved slots are kept."
                  : (cmd == "save" ? "Overwrite save slot " : "Replace the current voyage with slot ") + std::to_string(panel.slot) + "?";
            } else { action(cmd, std::to_string(panel.slot)); }
          }
          ImGui::EndDisabled();
          if (!panel.pendingAction.empty()) {
            ImGui::Separator(); text(panel.question);
            if (button("OK", "confirm")) {
              const auto cmd = panel.pendingAction;
              panel.pendingAction.clear(); panel.question.clear();
              action(cmd, std::to_string(panel.pendingSlot));
              if (cmd == "new-voyage") { restarted = true; }
            }
            if (button("Cancel", "cancel")) { panel.pendingAction.clear(); panel.question.clear(); }
          }
        }
        if (!panel.message.empty()) { ImGui::Spacing(); text(panel.message); }
        ImGui::EndDisabled();
      }
      ImGui::EndChild();
      ImGui::PopStyleColor();
      const float finalH =
          std::clamp(ImGui::GetItemRectMax().y - winPos.y + 12.0f, 40.0f, maxH);

      winDraw->ChannelsSetCurrent(0);
      winDraw->PushClipRect(ImVec2(winPos.x - 2.0f, winPos.y - 2.0f),
                            ImVec2(winPos.x + w + 2.0f, winPos.y + finalH + 2.0f), false);
      if (gFlare.out != nullptr) {
        flareTargets_.insert(flareTargets_.begin() + static_cast<std::ptrdiff_t>(flareMark),
                             FlareTarget{FlareTarget::Occlude, winPos.x, winPos.y,
                                         winPos.x + w, winPos.y + finalH, 0.0f});
      }
      PaintChamferBoxIm(winDraw, winPos.x, winPos.y, w, finalH, kChamferPanel,
                        ToIm(p.panel), ToIm(p.borderDark), ToIm(p.border));
      const float hdrCi = InnerChamfer(kChamferPanel, w, finalH, kDoubleInset + 1.0f);
      const std::array<ImVec2, 5> hdrPts = {{
          ImVec2(hdrX + hdrCi, hdrY),
          ImVec2(hdrX + hdrW, hdrY),
          ImVec2(hdrX + hdrW, hdrY + headerH),
          ImVec2(hdrX, hdrY + headerH),
          ImVec2(hdrX, hdrY + hdrCi),
      }};
      winDraw->AddConvexPolyFilled(hdrPts.data(), static_cast<int>(hdrPts.size()),
                                   ToIm(p.panel2));
      winDraw->AddLine(ImVec2(hdrX, hdrY + headerH), ImVec2(hdrX + hdrW, hdrY + headerH),
                       ToIm(p.borderDark));
      winDraw->PopClipRect();
      winDraw->ChannelsMerge();
    }
    ImGui::End();
    if (!open) { closed.push_back(panel.serial); }
    if (restarted || showMenu) { break; }
  }
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(10);
  for (int serial : closed) { floating_.Close(serial); }
  if (restarted) {
    floating_.Clear();
    ResetContentScroll();
    ShowTitle(false);
    titlePanel_ = TitlePanel::Setup;
    setupPreviewDirty_ = true;
    setupSystemId_.clear();
    setupGalaxyId_.clear();
    setupLayer_ = "galaxy";
  }
  if (showMenu) { ShowTitle(true); }
  popupKey_ = floating_.Panels().empty() ? "" : floating_.Panels().back().key;
}

void ShellView::Move(float x, float y) {
  mouseX_ = x;
  mouseY_ = y;
}

void ShellView::Drag(Simulation& sim, float x, float y, float dx, float dy, bool down, bool pressed) {
  Move(x, y);
  if (!down) { draggingChart_ = false; draggingFeed_ = false; draggingBearing_ = false; return; }
  if (floating_.HasConfirmation()) { return; }
  if (titleActive_ || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (BlockerActive(sim) && !bridgeBlockerHole_.Contains(x, y)) { return; }
  if (feedActive_ && down && (draggingFeed_ ||
      (pressed && layout_.content.Contains(x, y) && feedThumb_.Contains(x, y + contentScroll_)))) {
    if (!draggingFeed_) { draggingFeed_ = true; feedDragOffset_ = y + contentScroll_ - feedThumb_.y; }
    const float feedTrackTop = feedBox_.y + static_cast<float>(TypePx(11)) + 12.0f;
    const float travel = feedBox_.h - (static_cast<float>(TypePx(11)) + 16.0f) - feedThumb_.h;
    if (travel > 0) feedScroll_ = std::clamp((y + contentScroll_ - feedTrackTop - feedDragOffset_) / travel, 0.0f, 1.0f) * feedMaxScroll_;
    return;
  }
  const float localY = y + contentScroll_;
  if (sim.State().activeTab == "room:bridge" && layout_.content.Contains(x,y) &&
      (draggingBearing_ || (pressed && bridgeBearingBar_.Contains(x,localY)))) {
    draggingBearing_ = true;
    bridgeLook_ = std::clamp((x-bridgeBearingBar_.x)/bridgeBearingBar_.w - 0.5f,-0.5f,0.5f);
    return;
  }
  if (!draggingChart_) {
    if (!pressed) { return; }
    if (!screenNav_ || !layout_.content.Contains(x, y) ||
        !navChartBox_.Contains(x, localY)) { return; }
    // Layer and zoom controls must not also pan the chart underneath them.
    for (const auto& hit : navHits_) {
      if (hit.action != "select" && hit.box.Contains(x, localY)) { return; }
    }
    chartDragAccumX_ = chartDragAccumY_ = 0.0;
    chartDragMoved_ = false;
    draggingChart_ = true;
    return;
  }
  chartDragAccumX_ += dx;
  chartDragAccumY_ += dy;
  chartDragMoved_ = std::abs(chartDragAccumX_) + std::abs(chartDragAccumY_) > 4.0;
  // Rotate pan delta into the current 3D orbit yaw so dragging left/right/up/down
  // always pans smoothly along the screen axes regardless of camera azimuth.
  const float yawRad = navOrbitYaw_ * DEG2RAD;
  const float cosY = std::cos(yawRad);
  const float sinY = std::sin(yawRad);
  const float worldDx = dx * cosY + dy * sinY;
  const float worldDy = -dx * sinY + dy * cosY;
  sim.PanMap(worldDx / std::max(0.01f, navChartScaleX_),
             worldDy / std::max(0.01f, navChartScaleY_));
}

void ShellView::OrbitDrag(Simulation& sim, float x, float y, float dx, float dy,
                          bool down, bool pressed) {
  (void)sim;
  Move(x, y);
  if (!down) {
    if (orbitingChart_ && !orbitDragMoved_) {
      navOrbitYaw_ = kDefaultNavOrbitYaw;
      navOrbitPitch_ = kDefaultNavOrbitPitch;
    }
    orbitingChart_ = false;
    orbitDragMoved_ = false;
    return;
  }
  if (floating_.HasConfirmation()) { return; }
  if (titleActive_ || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (BlockerActive(sim) && !bridgeBlockerHole_.Contains(x, y)) { return; }
  const float localY = y + contentScroll_;
  if (!orbitingChart_) {
    if (!pressed) { return; }
    if (!screenNav_ || !layout_.content.Contains(x, y) ||
        !navChartBox_.Contains(x, localY)) {
      return;
    }
    for (const auto& hit : navHits_) {
      if (hit.action != "select" && hit.box.Contains(x, localY)) { return; }
    }
    orbitDragAccumX_ = 0.0;
    orbitDragAccumY_ = 0.0;
    orbitDragMoved_ = false;
    orbitingChart_ = true;
    return;
  }
  orbitDragAccumX_ += dx;
  orbitDragAccumY_ += dy;
  if (std::abs(orbitDragAccumX_) + std::abs(orbitDragAccumY_) > 4.0) {
    orbitDragMoved_ = true;
  }
  navOrbitYaw_ -= dx * 0.42f;
  while (navOrbitYaw_ > 180.0f) { navOrbitYaw_ -= 360.0f; }
  while (navOrbitYaw_ < -180.0f) { navOrbitYaw_ += 360.0f; }
  navOrbitPitch_ = std::clamp(navOrbitPitch_ + dy * 0.36f, -82.0f, 86.0f);
}

void ShellView::Scroll(Simulation& sim, float x, float y, float wheelDelta) {
  Move(x, y);
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (floating_.HasConfirmation()) { return; }
  if (BlockerActive(sim)) { return; }
  if (!titleActive_ && screenNav_ && navChartBox_.Contains(x, y + contentScroll_)) {
    const int dir = wheelDelta > 0.0f ? 1 : -1;
    sim.ZoomMap(dir);
    return;
  }
  const float step = wheelDelta * TypeStep(40.0f);
  if (titleActive_) { titleScroll_ = std::max(0.0f, std::min(titleMaxScroll_, titleScroll_ - step)); }
  else if (feedActive_ && layout_.content.Contains(x, y) && feedBox_.Contains(x, y + contentScroll_)) {
    feedScroll_ = std::clamp(feedScroll_ - step, 0.0f, feedMaxScroll_);
  }
  else if (wikiIndexBox_.Contains(x, y + contentScroll_)) { wikiIndexScroll_ = std::max(0.0f, std::min(wikiIndexMaxScroll_, wikiIndexScroll_ - step)); }
  else if (wikiBodyBox_.Contains(x, y + contentScroll_)) { wikiBodyScroll_ = std::max(0.0f, std::min(wikiBodyMaxScroll_, wikiBodyScroll_ - step)); }
  else if (layout_.sidepanel.Contains(x, y)) { sideScroll_ = std::max(0.0f, std::min(std::max(0.0f, sideContentH_ - layout_.sidepanel.h), sideScroll_ - step)); }
  else { contentScroll_ = std::max(0.0f, std::min(contentMaxScroll_, contentScroll_ - step)); }
}

void ShellView::Escape(Simulation& sim) {
  if (splashActive_) { EndSplash(frameTimeMs_); return; }
  if (LaunchCoverUp(frameTimeMs_)) { return; }
  if (LossVisible(sim)) { lossDismissed_ = true; return; }
  if (titleActive_) {
    if (titleConfirm_) { titleConfirm_ = false; }
    else if (titlePanel_ != TitlePanel::None) { titlePanel_ = TitlePanel::None; }
    else { HideTitle(); }
    return;
  }
  if (floating_.HasConfirmation()) {
    for (auto& panel : floating_.Panels()) { panel.pendingAction.clear(); panel.question.clear(); }
    return;
  }
  if (!floating_.Panels().empty()) { ClosePopup(); return; }
  if (!pendingConfirm_.empty()) { pendingConfirm_.clear(); pendingAction_.clear(); return; }
  if (stationWelcome_) { stationWelcome_ = false; return; }
  ShowTitle(true);
}

void ShellView::Click(Simulation& sim, float x, float y, HitButton button,
                      double nowMs) {
  if (splashActive_) { EndSplash(nowMs); return; }
  if (button == HitButton::Left) {
    if (draggingBearing_) { draggingBearing_ = false; return; }
    const bool moved = draggingChart_ && chartDragMoved_;
    draggingChart_ = false;
    chartDragMoved_ = false;
    if (moved) { return; }
  }
  if (floating_.HasConfirmation() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (button == HitButton::None) { return; }
  // Nothing of the ship is on screen yet; a press must not land on it blind.
  if (LaunchCoverUp(nowMs)) { return; }

  // The menu covers everything below it, and the loss overlay covers the ship.
  if (titleActive_) {
    for (const NavHit& hit : titleHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      if (hit.action == "confirm-new-voyage") {
        titleConfirm_ = false;
        PressTitleAction(sim, "new-voyage", "", &fromVoyage_, &titlePanel_, &titleSlot_,
                         saveDir_, &titleMessage_, &titleActive_);
        setupPreviewDirty_ = true;
        setupSystemId_.clear();
        setupGalaxyId_.clear();
        setupLayer_ = "galaxy";
        return;
      }
      if (hit.action == "cancel-new-voyage") {
        titleConfirm_ = false;
        titleMessage_.clear();
        return;
      }
      if (hit.action == "new-voyage" || hit.action == "start-voyage") {
        if (TitleNeedsConfirmation(sim, fromVoyage_)) {
          titleConfirm_ = true;
          titleMessage_ =
              "Start a new voyage? Unsaved progress will be lost; saved slots are kept.";
          return;
        }
        titleConfirm_ = false;
        PressTitleAction(sim, hit.action, "", &fromVoyage_, &titlePanel_, &titleSlot_,
                         saveDir_, &titleMessage_, &titleActive_);
        setupPreviewDirty_ = true;
        setupSystemId_.clear();
        setupGalaxyId_.clear();
        setupLayer_ = "galaxy";
        return;
      }
      if (hit.action == "load-slot") {
        const bool loaded = PressTitleAction(sim, "load-slot", "", &fromVoyage_,
                                             &titlePanel_, &titleSlot_, saveDir_,
                                             &titleMessage_, &titleActive_);
        if (loaded) { HideTitle(); }
        return;
      }
      if (hit.action == "card:next" || hit.action == "card:prev") {
        PageTutorial(hit.action == "card:next" ? 1 : -1, nowMs);
        return;
      }
      if (hit.action == "exit") { quitRequested_ = true; return; }
      if (hit.action == "guide" || hit.action == "tutorial") {
        titleCard_ = 0;
        tutorialDirection_ = 1;
        tutorialTransitionMs_ = nowMs;
      }
      if (hit.action == "setup-seed-inc") {
        setupSettings_.seed += 1;
        setupPreviewDirty_ = true;
        setupSystemId_.clear();
        return;
      }
      if (hit.action == "setup-seed-dec") {
        setupSettings_.seed -= 1;
        setupPreviewDirty_ = true;
        setupSystemId_.clear();
        return;
      }
      if (hit.action == "setup-seed-rand") {
        setupSettings_.seed = setupSettings_.seed * 6364136223846793005ULL + 1ULL;
        setupPreviewDirty_ = true;
        setupSystemId_.clear();
        return;
      }
      if (hit.action == "setup-gal-inc") {
        setupSettings_.galaxies = ClampGalaxies(setupSettings_.galaxies + 1);
        setupPreviewDirty_ = true;
        setupSystemId_.clear();
        setupGalaxyId_.clear();
        return;
      }
      if (hit.action == "setup-gal-dec") {
        setupSettings_.galaxies = ClampGalaxies(setupSettings_.galaxies - 1);
        setupPreviewDirty_ = true;
        setupSystemId_.clear();
        setupGalaxyId_.clear();
        return;
      }
      if (hit.action == "setup-scale-inc") {
        setupSettings_.mapSize = ClampMapSize(setupSettings_.mapSize + 1);
        setupSettings_.mapScale = MapSizeScale(setupSettings_.mapSize);
        setupPreviewDirty_ = true;
        return;
      }
      if (hit.action == "setup-scale-dec") {
        setupSettings_.mapSize = ClampMapSize(setupSettings_.mapSize - 1);
        setupSettings_.mapScale = MapSizeScale(setupSettings_.mapSize);
        setupPreviewDirty_ = true;
        return;
      }
      if (hit.action == "setup-spread-inc") {
        setupSettings_.spread = std::min(3.0, setupSettings_.spread + 0.1);
        setupPreviewDirty_ = true;
        return;
      }
      if (hit.action == "setup-spread-dec") {
        setupSettings_.spread = std::max(0.4, setupSettings_.spread - 0.1);
        setupPreviewDirty_ = true;
        return;
      }
      if (hit.action.rfind("setup-layer:", 0) == 0) {
        setupLayer_ = hit.action.substr(12);
        return;
      }
      if (hit.action == "setup-pick-galaxy") {
        setupGalaxyId_ = hit.arg;
        setupLayer_ = "sector";
        setupSystemId_.clear();
        return;
      }
      if (hit.action == "setup-pick-system") {
        setupSystemId_ = hit.arg;
        for (const auto& dot : setupPreview_.systems) {
          if (dot.id == setupSystemId_) { setupGalaxyId_ = dot.parent; break; }
        }
        return;
      }
      if (hit.action == "setup-launch") {
        if (setupSystemId_.empty()) { return; }
        sim.BeginVoyage(setupSettings_, setupSystemId_);
        fromVoyage_ = false;
        HideTitle();
        fadeKind_ = 2;
        fadeStartMs_ = nowMs;
        fadeLabel_ = setupSystemId_;
        return;
      }
      PressTitleAction(sim, hit.action, "", &fromVoyage_, &titlePanel_, &titleSlot_,
                       saveDir_, &titleMessage_, &titleActive_);
      return;
    }
    return;
  }

  if (LossVisible(sim)) {
    for (const NavHit& hit : lossHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      bool toTitle = false;
      const bool changed = PressLossAction(sim, hit.action, "", &lossSlot_, &toTitle,
                                          saveDir_, &lossMessage_);
      if (hit.action == "main-menu" || toTitle) {
        lossDismissed_ = true;
        ShowTitle(false);
        if (hit.action == "new-voyage") {
          titlePanel_ = TitlePanel::Setup;
          setupPreviewDirty_ = true;
          setupSystemId_.clear();
          setupGalaxyId_.clear();
          setupLayer_ = "galaxy";
        }
        return;
      } else if (changed && sim.State().gameOver.empty()) {
        lossDismissed_ = true;
      }
      return;
    }
    return;
  }

  if (BlockerActive(sim) && !bridgeBlockerHole_.Contains(x, y)) { return; }

  // Floating windows are nonmodal. ImGui owns input only over their actual
  // bounds; exposed ship/chrome controls remain usable, as in the HTA.

  // Chrome hit-testing follows paint order: taskbar controls and tabs, then
  // sidebar and pinned foot, then the screen behind them.
  if (layout_.scrollLeft.Contains(x, y)) {
    tabScroll_ = std::max(0.0f, tabScroll_ - layout_.tabStrip.w * 0.65f);
    return;
  }
  if (layout_.scrollRight.Contains(x, y)) {
    tabScroll_ = std::min(maxTabScroll_, tabScroll_ + layout_.tabStrip.w * 0.65f);
    return;
  }
  if (layout_.home.Contains(x, y)) { SwitchTab(sim, "home"); return; }
  for (const TabHit& hit : tabHits_) {
    if (hit.closable && hit.closeBox.Contains(x, y)) { CloseTab(sim, hit.key); return; }
    if (hit.box.Contains(x, y)) { SwitchTab(sim, hit.key); return; }
  }
  for (const SideHit& hit : sideHits_) {
    if (!hit.box.Contains(x, y)) { continue; }
    switch (hit.action) {
      case SideAction::Toggle:
        if (hit.key != "indicators") { sim.MutableState().spOpen[hit.key] = !sim.MutableState().spOpen[hit.key]; }
        return;
      case SideAction::OpenCrew:
        if (button == HitButton::Left) { OpenTask(sim, "crew", hit.key, true); }
        return;
      case SideAction::OpenEvent: OpenTask(sim, "event", hit.key, false); return;
      case SideAction::Wiki: OpenTask(sim, "wiki", "main", true); return;
      case SideAction::Rewards: OpenTask(sim, "awards", "main", true); return;
      case SideAction::Manifest: OpenPopup("manifest"); return;
      case SideAction::Log: OpenPopup("log"); return;
      case SideAction::Files: OpenPopup("files"); return;
      case SideAction::Settings: OpenPopup("settings"); return;
      case SideAction::None: break;
    }
  }

  // Screen content is painted shifted up by contentScroll_; shift the cursor
  // down by the same amount so hit testing matches what is painted, and
  // content scrolled out of view can never be clicked.
  if (!layout_.content.Contains(x, y)) { return; }
  if (stationWelcome_) {
    if (button == HitButton::Left) { stationWelcome_ = false; }
    return;
  }
  const float scrollY = contentScroll_;

  if (button == HitButton::Left && sim.State().activeTab == "room:bridge" &&
      bridgeDispatch_ && sim.LandedOnPlanet()) {
    for (int i = planetHitCount_ - 1; i >= 0; --i) {
      const PlanetClickHit& hit = planetHits_[i];
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if ((hit.kind >= 1 && hit.kind <= 4) || hit.kind == 7 || hit.kind == 8) {
        StepPlanetSurface(sim, hit.kind);
      } else if (hit.kind == 5) {
        if (sim.CollectPlanetOre(sim.Map().here, hit.arg)) {
          planetSurface_.bumpKind = 0;
          planetSurface_.bumpBack = false;
        }
      } else if (hit.kind == 6) {
        planetSurface_.bumpKind = 0;
        planetSurface_.bumpBack = false;
        bridgeDispatch_ = false;
      }
      return;
    }
  }
  if (button == HitButton::Left && sim.State().activeTab == "room:bridge" && sim.FindOngoing("travel")) {
    for (int slot = 0; slot < static_cast<int>(debrisHits_.size()); ++slot) {
      const auto& hit = debrisHits_[slot];
      if (hit.click != sim.ActionCount() || !hit.box.Contains(x, y + scrollY)) continue;
      auto& state = sim.MutableState();
      if (state.debrisClaimedCycle[slot] == hit.click / 10) continue;
      std::string item;
      if (hit.reward < 4) { ++state.repairKits; item = "a repair kit"; }
      else if (hit.reward < 8) { state.shiplets += hit.shiplets; item = std::to_string(hit.shiplets) + " shiplets"; }
      else if (hit.reward == 8) { ++state.fuelCells; item = "a fuel cell"; }
      else {
        if (sim.Life().canisters >= sim.Life().capacity) { sim.LogLine("Debris: O2 canister rack full."); return; }
        ++sim.MutableLife().canisters;
        item = "an O2 canister";
      }
      state.debrisClaimedCycle[slot] = hit.click / 10;
      sim.FreeCommit("Recovered " + item + " from space debris");
      return;
    }
  }
  if (button == HitButton::Left && sim.State().activeTab == "room:bridge" &&
      bridgeBearingBar_.Contains(x, y + scrollY)) {
    bridgeLook_ = std::clamp((x-bridgeBearingBar_.x)/bridgeBearingBar_.w - 0.5f,-0.5f,0.5f);
    return;
  }
  if (y >= actionBandTop_) {
    for (const auto& hit : actionBandHits_) {
      if (!hit.box.Contains(x, y + scrollY)) continue;
      if (hit.action == "confirm-no") { pendingConfirm_.clear(); pendingAction_.clear(); return; }
      if (hit.action == "confirm-yes") {
        const std::string action = pendingAction_;
        pendingConfirm_.clear(); pendingAction_.clear();
        bool mark = false;
        const TaskScreenModel& screen = taskScreen_;
        for (std::size_t c = 0; c < screen.panelRowIds.size(); ++c) {
          for (std::size_t r = 0; r < screen.panelRowIds[c].size(); ++r) {
            if (screen.panelRowIds[c][r] != action) { continue; }
            if (c < screen.panelRows.size() && r < screen.panelRows[c].size()) {
              const auto& row = screen.panelRows[c][r];
              mark = row.charged ||
                     (panelsKind_ == ScreenKind::Station && !row.plain &&
                      (action.rfind("buy-", 0) == 0 || action == "ship-buy" ||
                       action.rfind("upgrade:", 0) == 0 ||
                       action.rfind("room-buy:", 0) == 0 || action.rfind("hire:", 0) == 0 ||
                       action.rfind("trade-accept:", 0) == 0));
            }
          }
        }
        if (PressPanelRow(sim, panelsKind_, action) &&
            (mark || panelsKind_ == ScreenKind::Cargo)) {
          SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
        }
        return;
      }
      const bool positive = screenNav_ ? PressNavAction(sim, hit.action, hit.arg)
                                       : PressProgressButton(sim, hit.action);
      if (positive) SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      return;
    }
    return; // Never activate a body control hidden behind the band.
  }

  // The quarters: Assign Tasks button (green), roster row, order, or speech bubble (free).
  if (screenQuarters_) {
    for (const auto& entry : taskButtons_) {
      if (!entry.first.Contains(x, y + scrollY)) { continue; }
      if (PressProgressButton(sim, entry.second)) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      PressQuartersAction(sim, hit.action, hit.arg);
      return;
    }
    return;
  }
  if (screenRecord_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (hit.action == "talk") { PressConversation(sim, hit.arg); }
      return;
    }
    return;
  }

  if (screenCargo_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      std::string question;
      for (const CargoSideRow& row : cargoModel_.sideRows) {
        if (row.id == hit.arg) { question = row.confirm; break; }
      }
      if (!question.empty() && sim.State().options.confirmRisk) {
        pendingConfirm_ = question;
        pendingAction_ = hit.arg;
        return;
      }
      const bool acted = PressPanelRow(sim, ScreenKind::Cargo, hit.arg);
      const bool greenPing =
          acted && (hit.arg.rfind("cargo-stow:", 0) == 0 ||
                    hit.arg.rfind("cargo-settle:", 0) == 0 ||
                    hit.arg.rfind("cargo-unload:", 0) == 0);
      if (greenPing) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }

  // Station and stores: each command row carries its own action id.
  if (screenPanels_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (panelsKind_ == ScreenKind::Room && hit.arg == "dispatch" && sim.LandedOnPlanet()) {
        if (!bridgeDispatch_) {
          bridgeDispatch_ = true;
          contentScroll_ = 0.0f;
          floating_.Panels().clear();
          EnsurePlanetSurface(sim);
        }
        return;
      }
      // Read the row's own question before acting: a purchase that cannot be
      // undone asks first when the reader keeps confirmations on.
      const CommandRowView* clickedRow = nullptr;
      std::string question;
      const TaskScreenModel& screen = taskScreen_;
      for (std::size_t c = 0; c < screen.panelRowIds.size(); ++c) {
        for (std::size_t r = 0; r < screen.panelRowIds[c].size(); ++r) {
          if (screen.panelRowIds[c][r] != hit.arg) { continue; }
          if (c < screen.panelRows.size() && r < screen.panelRows[c].size()) {
            clickedRow = &screen.panelRows[c][r];
            question = clickedRow->confirm;
          }
        }
      }
      // The ship purchase always asks; other risk prompts follow the option.
      if (!question.empty() && (sim.State().options.confirmRisk ||
                                (panelsKind_ == ScreenKind::Station && hit.arg == "ship-buy"))) {
        pendingConfirm_ = question;
        pendingAction_ = hit.arg;
        return;
      }
      const bool rowCharged = clickedRow != nullptr && clickedRow->charged;
      const bool stationPurchaseOrTrade =
          panelsKind_ == ScreenKind::Station && clickedRow != nullptr && !clickedRow->plain &&
          (hit.arg.rfind("buy-", 0) == 0 || hit.arg == "ship-buy" ||
           hit.arg.rfind("upgrade:", 0) == 0 ||
           hit.arg.rfind("room-buy:", 0) == 0 || hit.arg.rfind("hire:", 0) == 0 ||
           hit.arg.rfind("trade-accept:", 0) == 0);
      const bool acted = PressPanelRow(sim, panelsKind_, hit.arg);
      if (acted && (rowCharged || stationPurchaseOrTrade)) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
  }

  // The chart's own controls: bodies, the zoom pair, the layers, the actions.
  if (screenNav_) {
    for (auto it = navHits_.rbegin(); it != navHits_.rend(); ++it) {
      const NavHit& hit = *it;
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (hit.action == "reset-3d") {
        navOrbitYaw_ = kDefaultNavOrbitYaw;
        navOrbitPitch_ = kDefaultNavOrbitPitch;
        return;
      }
      const double actionsBefore = sim.ActionCount();
      PressNavAction(sim, hit.action, hit.arg);
      if (sim.ActionCount() > actionsBefore) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }

  // A task window's own controls come first.
  for (const auto& entry : taskButtons_) {
    if (!entry.first.Contains(x, y + scrollY)) { continue; }
    if (PressProgressButton(sim, entry.second)) {
      SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
    }
    return;
  }

  if (layout_.home.Contains(x, y)) {
    SwitchTab(sim, "home");
    return;
  }

  if (sim.State().activeTab == "home") {
    // The whole home column is painted shifted up by contentScroll_; shift the
    // cursor down by the same amount so a click lands on what is actually
    // painted, and never on content scrolled out of view.
    const float scrollY = contentScroll_;
    if (HasWrenExterior(sim)) {
      for (const ExteriorCallout& item : exterior_.callouts) {
        if (!item.box.Contains(x, y + scrollY)) continue;
        if (button == HitButton::Right) { OpenTask(sim, "room", item.id, false); }
        else { OpenBriefing(sim, "room", item.id); }
        return;
      }
    }
    for (const RailCell& cell : idle_.stats) {
      if (!cell.box.Contains(x, y + scrollY)) { continue; }
      if (button == HitButton::Right) {
        OpenTask(sim, "system", cell.id, false);
      } else {
        OpenBriefing(sim, "system", cell.id);
      }
      return;
    }
    for (const RailCell& cell : idle_.infos) {
      if (!cell.box.Contains(x, y + scrollY)) { continue; }
      if (button == HitButton::Right) {
        OpenTask(sim, "info", cell.id, false);
      } else {
        OpenBriefing(sim, "info", cell.id);
      }
      return;
    }
    if (idle_.nav.box.Contains(x, y + scrollY)) {
      if (button == HitButton::Right) {
        OpenTask(sim, "nav", "main", false);
      } else {
        OpenBriefing(sim, "nav", "main");
      }
      return;
    }
    for (const HomeEventCard& event : idle_.events) {
      if (!event.box.Contains(x, y + scrollY)) { continue; }
      if (event.station) {
        if (button == HitButton::Right) { OpenTask(sim, "station", event.id, false); }
        else { OpenBriefing(sim, "station", event.id); }
      } else if (!event.id.empty()) {
        OpenTask(sim, "event", event.id, false);
      }
      return;
    }
  }
}

}  // namespace ship
