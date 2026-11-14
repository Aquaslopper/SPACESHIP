// ============================================================================
//  Draw.cpp — the skin.
//  ---------------------------------------------------------------------------
//  Raylib paints the ship; ImGui paints the cockpit around it. Both are told
//  what to paint by the view-models, and neither decides anything: a click is
//  turned into a (kind, id) pair here and handed to the shell, which is where
//  tabs open and briefings are chosen.
//
//  The palette is the stylesheet's: parchment panels, coffee ink, and one warm
//  cream for a tab that has just announced itself. There is no default ImGui
//  theme anywhere in this file - a stock blue-dark window in a parchment
//  cockpit would be the loudest possible lie about what this port is.
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
#include <unordered_map>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "rlgl.h"

#include "audio/Audio.h"
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

void PaintChamferDoubleLinesRay(const Area& box, float chamfer, ::Color outerLine,
                                ::Color innerLine) {
  if (box.w <= 2.0f || box.h <= 2.0f) { return; }
  StrokeChamferRay(box.x, box.y, box.w, box.h, chamfer, outerLine);
  if (box.w >= 14.0f && box.h >= 14.0f) {
    const float ci = InnerChamfer(chamfer, box.w, box.h, kDoubleInset);
    StrokeChamferRay(box.x + kDoubleInset, box.y + kDoubleInset,
                     box.w - 2.0f * kDoubleInset, box.h - 2.0f * kDoubleInset, ci, innerLine);
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
}

// Spaceship.hta .pbtn-fill: #dff0cc, with a #7ec63f right edge.
// Share this across machine controls, generic bands, chart and quarters.
void PaintProgressFill(const Area& box, double percent, float chamfer = kChamferCard) {
  const float inset = (box.w >= 14.0f && box.h >= 14.0f) ? kDoubleInset : 1.0f;
  const float ix = box.x + inset;
  const float iy = box.y + inset;
  const float iw = std::max(0.0f, box.w - 2.0f * inset);
  const float ih = std::max(0.0f, box.h - 2.0f * inset);
  const float width = iw * static_cast<float>(std::clamp(percent, 0.0, 100.0)) / 100.0f;
  if (width <= 0.0f || iw <= 0.0f || ih <= 0.0f) { return; }
  const float ci = InnerChamfer(chamfer, box.w, box.h, inset);
  const auto poly = ClipFillAgainstChamfer(ix, ix + width, iy, ih, ix, iw, ci);
  if (poly.size() >= 3) { FillPolyRay(poly.data(), poly.size(), ::Color{223, 240, 204, 255}); }
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
  if (!blocked) { PaintProgressFill(box, button.fillPercent, chamfer); }
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

bool PaintMessStockIcon(const MachinePart& part, const Area& origin) {
  const bool cookedFood = part.cls == "mess-food-bottle";
  const bool dryRation = part.cls == "mess-ration-pack";
  if (!cookedFood && !dryRation) { return false; }

  const float x = origin.x + part.box.x;
  const float y = origin.y + part.box.y;
  const float w = part.box.w;
  const float h = part.box.h;
  if (w <= 0.0f || h <= 0.0f) { return true; }

  const ::Color outline = cookedFood ? ::Color{107, 125, 80, 255}
                                      : ::Color{140, 111, 78, 255};
  const ::Color shell = cookedFood ? ::Color{223, 236, 205, 255}
                                    : ::Color{243, 230, 207, 255};
  const ::Color label = ::Color{250, 247, 242, 255};
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
    DrawRectangleRec(label, ::Color{228, 219, 203, 255});
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

ShellView::~ShellView() { ReleaseWrenMesh(); }

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
  const float contentH = height_ - Metrics::kContentTop - Metrics::kStatusH;
  layout_ = ComputeLayout(width_, height_, contentH, others_, theme_.stageCompact);

  if (scrollTabKey_ != sim.State().activeTab) {
    scrollTabKey_ = sim.State().activeTab;
    ResetContentScroll();
    if (sim.State().activeTab == "home") { feedScroll_ = 0; draggingFeed_ = false; }
    draggingChart_ = false;
  }

  // Only Wren has supplied exterior artwork. The Kestrel and a missing asset
  // keep the existing fully interactive deck plan, without invented hull art.
  if (sim.State().activeTab == "home" && HasWrenExterior(sim)) {
    if (exteriorView_) {
      layout_.stage = {layout_.content.x + 8.0f, layout_.content.y,
                       std::max(1.0f, layout_.content.w - 16.0f),
                       Metrics::kHomeStageMinH};
      layout_.stage.h = BuildExteriorLayout(sim, layout_.stage).height;
    }
  }

  // Always build Home, including when reduced motion is enabled. Keep drift
  // state across frames and advance it at the reference cadence; rebuilding
  // random dots every frame caused teleporting and a blank reduced-motion view.
  if (sim.State().activeTab == "home") {
    FxRng seeded = rng_;
    idle_ = BuildIdleScreen(sim, layout_, theme_, theme_.hideCrewLabels, seeded);
    if (HasWrenExterior(sim)) {
      exterior_ = BuildExteriorLayout(sim, idle_.stageArea);
      // No tutorial copy or view-switch row is drawn above the ship.
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
    rng_ = seeded;
    SyncIdleDots(sim, nowMs);
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

  if (titleActive_) {
    cargoAnims_.clear();
    lastCargoAnimMs_ = 0.0;
    PaintTitle(sim, nowMs);
    return;
  }

  // Main-content scroll: the whole content column moves with contentScroll_,
  // matching the HTA's #content { overflow:auto }. Every screen paints inside
  // this transform, and Click() offsets the cursor by the same amount so
  // obscured content can never be clicked.
  Camera2D scrollCam{{0.0f, 0.0f}, {0.0f, contentScroll_}, 0.0f, 1.0f};
  contentPaintBottom_ = layout_.content.y;
  paintingContent_ = true;
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
      taskScreen_ = BuildTabScreen(sim, *activeTab, wiki_, &stationSelection_);
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
      } else {
        PaintTaskScreen(sim);
      }
    }
  }
  EndMode2D();
  paintingContent_ = false;
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

}

void ShellView::SyncIdleDots(const Simulation& sim, double nowMs) {
  const bool compact = sim.State().shipClass == "wren";
  // Reconcile by identity, not just count. Retain cosmetic movement while
  // refreshing its effective room (job, medical, social or corridor) every frame.
  std::vector<DriftDot> current;
  for (const DiagramCrew& crew : idle_.diagram.crew) {
    const auto old = std::find_if(dots_.begin(), dots_.end(),
                                [&crew](const DriftDot& d) { return d.id == crew.id; });
    DriftDot dot;
    if (old != dots_.end()) { dot = *old; }
    else {
      dot.id = crew.id;
      dot.x = dot.tx = crew.dot.x;
      dot.y = dot.ty = crew.dot.y;
    }
    dot.roomId = crew.roomId;
    current.push_back(dot);
  }
  dots_ = std::move(current);
  if (!sim.State().reducedMotion && lastDriftMs_ <= 0.0) { lastDriftMs_ = nowMs; }
  const int ticks = sim.State().reducedMotion || lastDriftMs_ <= 0.0
                        ? 0
                        : std::min(8, static_cast<int>((nowMs - lastDriftMs_) / FxTiming::kDriftMs));
  if (ticks > 0) {
    for (DriftDot& dot : dots_) {
      const auto room = std::find_if(idle_.diagram.rooms.begin(), idle_.diagram.rooms.end(),
                                     [&dot](const Room& r) { return r.id == dot.roomId; });
      if (room == idle_.diagram.rooms.end() && dot.roomId != "corridor") { continue; }
      const Area roomBox = dot.roomId == "corridor" ? idle_.diagram.corridor :
          Area{static_cast<float>(room->box.x), static_cast<float>(room->box.y),
               static_cast<float>(room->box.w), static_cast<float>(room->box.h)};
      for (int i = 0; i < ticks; ++i) { DriftStep(dot, roomBox, compact, driftRng_); }
    }
    lastDriftMs_ += static_cast<double>(ticks * FxTiming::kDriftMs);
  }
  for (DiagramCrew& crew : idle_.diagram.crew) {
    const auto dot = std::find_if(dots_.begin(), dots_.end(),
                                  [&crew](const DriftDot& d) { return d.id == crew.id; });
    if (dot == dots_.end()) { continue; }
    const float dx = dot->x - crew.dot.x;
    const float dy = dot->y - crew.dot.y;
    crew.dot.x = dot->x;
    crew.dot.y = dot->y;
    crew.label.x += dx;
    crew.label.y += dy;
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
  const std::vector<SideHit>& hits = sideHits_;
  const std::vector<SideBlock> blocks = SideBlocks(sim);
  draw->PushClipRect(ImVec2(layout_.sidepanel.x, layout_.sidepanel.y),
                     ImVec2(layout_.sidepanel.Right(), layout_.sidepanel.Bottom()), true);
  for (const SideHit& hit : hits) {
    if (hit.pinned) { continue; } // footer labels are tooltips, never sidebar text
    const Area& box = hit.paintBox;
    auto wrapped = [&](const std::string& text, float y, float width, ImU32 ink) {
      draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(box.x, y), ink,
                    text.c_str(), nullptr, width);
    };
    if (hit.header) {
      draw->AddText(ImVec2(box.x, box.y), ToIm(p.coffee), hit.text.c_str());
      bool open = true;
      for (const auto& block : blocks) { if (block.key == hit.key) { open = block.open; } }
      if (hit.key != "indicators") {
        const float x = box.Right() - 9, y = box.y + 5;
        if (open) {
          draw->AddTriangleFilled(ImVec2(x, y), ImVec2(x + 8, y),
                                  ImVec2(x + 4, y + 5), IM_COL32(140,111,78,255));
        } else {
          draw->AddTriangleFilled(ImVec2(x, y - 1), ImVec2(x, y + 7),
                                  ImVec2(x + 5, y + 3), IM_COL32(162,148,127,255));
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
  for (const SideHit& hit : hits) {
    if (!hit.pinned) { continue; }
    if (hit.action == SideAction::Manifest || hit.action == SideAction::Log ||
        hit.action == SideAction::Files || hit.action == SideAction::Settings) {
      PaintChamferBoxIm(draw, hit.box.x + 3.0f, hit.box.y + 1.0f, hit.box.w - 6.0f, 26.0f,
                        kChamferSmall, ToIm(p.panel2), ToIm(p.borderDark), ToIm(p.border));
      const ImVec2 iconCenter(hit.box.x + hit.box.w * 0.5f,
                             hit.box.y + 14.0f);
      const ImU32 iconInk = ToIm(p.coffee);
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
        assetRoot / "art" / "ships" / "wren-exterior.png",
        std::filesystem::path("assets") / "art" / "ships" / "wren-exterior.png",
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
  if (sim.State().shipClass != "wren" || texture == nullptr || texture->id == 0 ||
      texture->width <= 0 || texture->height <= 0) return false;
  // Wren exterior artwork remains visible throughout the voyage. The callouts
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
    const float v = 0.30f + 0.42f * ny;
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
        yawDeg = 10.5f * std::sin(t * 0.68f) + 2.6f * std::sin(t * 0.31f);
        pitchDeg = 5.2f * std::sin(t * 0.95f + 0.6f) + 1.6f;
        rollDeg = 4.6f * std::cos(t * 0.57f) + 1.3f * std::sin(t * 1.4f);
        bobX = 6.5f * std::sin(t * 0.48f);
        bobY = 5.0f * std::sin(t * 1.15f) + 0.40f * std::sin(t * 18.0f);
      } else {
        yawDeg = 8.4f * std::sin(t * 0.52f) + 2.0f * std::sin(t * 0.23f);
        pitchDeg = 4.2f * std::sin(t * 0.71f + 0.9f);
        rollDeg = 2.9f * std::cos(t * 0.43f + 0.4f);
        bobX = 3.8f * std::sin(t * 0.37f);
        bobY = 4.4f * std::sin(t * 0.89f + 0.5f);
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
  } else {
    DrawTexturePro(*texture,
                   Rectangle{0, 0, static_cast<float>(texture->width), static_cast<float>(texture->height)},
                   Rectangle{image.x, image.y, image.w, image.h}, Vector2{0, 0}, 0, WHITE);
    for (const ExteriorCallout& item : exterior_.callouts) {
      liveAnchors.push_back(item.anchor);
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

  if (HasWrenExterior(sim) && exteriorView_) {
    PaintWrenExterior(sim);
  } else {
    PaintDiagram(idle_.diagram, idle_.stageArea, idleDiagramScale_);
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
  if (!*style.cls) return false;
  const float x = origin.x + part.box.x, y = origin.y + part.box.y;
  const float w = std::max(0.0f, part.box.w), h = std::max(0.0f, part.box.h);
  const Rectangle box{x, y, w, h};
  const bool tonedFill = part.text.empty() && !part.tone.empty();
  const std::string background = tonedFill ? part.tone : style.background;
  const float rounding = std::min(1.0f, 2.0f * std::max(0, style.radius) / std::max(1.0f, std::min(w, h)));
  if (!background.empty() && w > 0 && h > 0) {
    const Color ink = HexColor(background, ToRay(theme_.palette.panel));
    if (style.radius == -1) DrawEllipse(static_cast<int>(x + w / 2), static_cast<int>(y + h / 2), w / 2, h / 2, ink);
    else if (style.radius > 0) DrawRectangleRounded(box, rounding, 12, ink);
    else DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), ink);
  }
  if (style.width > 0 && *style.border && w > 0 && h > 0) {
    const Color ink = HexColor(style.border, ToRay(theme_.palette.border));
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
           HexColor(ink, ToRay(theme_.palette.ink)), style.mono);
      Text(value.c_str(),
           static_cast<int>(x + w - TextWidth(value.c_str(), valuePx, reading.mono)),
           static_cast<int>(y), valuePx,
           HexColor(reading.ink, ToRay(theme_.palette.ink)), reading.mono);
      return true;
    }
    float tx = x;
    if (style.align) {
      const float space = w - TextWidth(part.text.c_str(), size, style.mono);
      tx += style.align == 1 ? space / 2 : space;
    }
    Text(part.text.c_str(), static_cast<int>(tx), static_cast<int>(y), size,
         HexColor(ink, ToRay(theme_.palette.ink)), style.mono);
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

void ShellView::PaintStage(const Simulation& sim, const TaskStage& stage, const Area& origin) {
  const Palette& p = theme_.palette;
  // HTA #machine is not overridden by the dark palette; the filter and
  // galley housings are. Keep the frame behind (not over) the scenery.
  const bool darkHousing = theme_.id != "classic" && stage.id != "machine";
  const Color housing = darkHousing ? ToRay(p.chrome) : HexColor("#fdfbf7", ToRay(p.panel));
  const Color edge = darkHousing ? ToRay(p.borderDark) : HexColor("#8c6f4e", ToRay(p.borderDark));
  const float scale = StageScale(stage, theme_);
  const Area stageFrame{origin.x, origin.y, stage.width * scale, stage.height * scale};
  PaintChamferBoxRay(stageFrame, kChamferPanel, housing, edge, ToRay(p.border));

  const bool reduced = sim.State().reducedMotion;
  const float tSec = reduced ? 0.0f : static_cast<float>(frameTimeMs_ * 0.001);
  const bool engineStage = stage.id == "machine";
  const bool o2Stage = stage.id == "o2mach";

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
        const Color coreCol = condRatio < 0.50f ? HexColor("#b05a42", ToRay(p.terracotta))
                                                : HexColor("#c08a3e", ToRay(p.amber));
        DrawCircleV(Vector2{cx, cy}, r * (1.28f + 0.18f * pulse), Fade(coreCol, 0.25f + 0.20f * pulse));
        DrawCircleV(Vector2{cx, cy}, r, StageMix(HexColor("#fdfbf7", WHITE), coreCol, 0.18f + 0.32f * pulse));
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
                               HexColor("#8c6f4e", ToRay(p.coffeeDark)));
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
          const Color hot = HexColor("#c08a3e", ToRay(p.amber));
          const Color cool = HexColor("#7f9161", ToRay(p.sage));
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
        const Color greenCol = HexColor("#7f9161", ToRay(p.sage));
        const Color greenEdge = HexColor("#6b7d50", ToRay(p.sage));
        const Color ambCol = HexColor("#c08a3e", ToRay(p.amber));
        const Color warnCol = HexColor("#b05a42", ToRay(p.terracotta));
        const Color offCol = HexColor("#efe8dc", ToRay(p.panel));
        const Color offEdge = HexColor("#cbb89c", ToRay(p.border));
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
                             std::max(1.0f, scale), Fade(HexColor("#ddd2bf", ToRay(p.border)), 0.75f));
        const Color baseFill = HexColor("#efe8dc", ToRay(p.panel));
        const Color hotFill = condRatio < 0.50f ? HexColor("#b05a42", ToRay(p.terracotta))
                                                : HexColor("#c08a3e", ToRay(p.amber));
        const Color cylFill = StageMix(baseFill, hotFill, phase * 0.28f);
        DrawRectangleRec(Rectangle{cx, cy, cw, ch}, cylFill);
        DrawRectangleLinesEx(Rectangle{cx, cy, cw, ch}, std::max(1.0f, scale),
                             HexColor("#cbb89c", ToRay(p.borderDark)));
        DrawRectangleRec(Rectangle{cx + scale, cy + scale, std::max(1.0f, cw - 2.0f * scale),
                                   std::max(1.0f, 3.0f * scale)},
                         StageMix(HexColor("#8c6f4e", ToRay(p.coffeeDark)), hotFill, phase * 0.45f));
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
        const Color slatCol = StageMix(HexColor("#cbb89c", ToRay(p.borderDark)),
                                       o2Running ? HexColor("#7f9161", ToRay(p.sage))
                                                 : HexColor("#8c6f4e", ToRay(p.coffee)),
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
        const Color streakCol = HexColor("#cfdcc0", ToRay(p.sage));
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
        const Color baseBg = HexColor("#e4dbcb", ToRay(p.panel3));
        const Color baseRim = HexColor("#cbb89c", ToRay(p.borderDark));
        const Color activeBg = HexColor("#7f9161", ToRay(p.sage));
        const Color standBg = HexColor("#c08a3e", ToRay(p.amber));
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

    if (PaintMessStockIcon(part, origin)) continue;
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

  // --- Priority 2 Overlay: Spinning turbines, rotating core encoder, sweeping needles & coolant loop ---
  if (engineStage) {
    const Color spokeCol = HexColor("#cbb89c", ToRay(p.borderDark));
    const Color wheelRimCol = HexColor("#8c6f4e", ToRay(p.coffeeDark));
    const Color hubFillCol = HexColor("#cbb89c", ToRay(p.panel2));
    const Color needleCol = HexColor("#8c6f4e", ToRay(p.coffeeDark));
    const Color hotFlowCol = HexColor("#c08a3e", ToRay(p.amber));
    const Color coolFlowCol = HexColor("#7f9161", ToRay(p.sage));

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
      const Color ndTint = (gi == 1 && tmpVal > 68.0f) ? HexColor("#b05a42", ToRay(p.terracotta)) : needleCol;
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
    const Color sageCol = HexColor("#7f9161", ToRay(p.sage));
    const Color ambCol = HexColor("#c08a3e", ToRay(p.amber));
    const Color bareCol = HexColor("#cbb89c", ToRay(p.borderDark));
    const Color bubbleFill = HexColor("#f5faee", WHITE);

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
        ledCol = HexColor("#b05a42", ToRay(p.terracotta));
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
      DrawCircleLinesV(Vector2{ledX, ledY}, 3.2f * scale, HexColor("#8c6f4e", ToRay(p.borderDark)));

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
      PaintProgressFill(box, control.button.fillPercent, kChamferCard);
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
  switch (look) {
    case ChartLook::Star:
    case ChartLook::Origin: return 0.0f;
    case ChartLook::Station: return 0.16f + posU * 0.24f;
    case ChartLook::Site: return (signedU >= 0.0f ? 0.10f : -0.10f) + signedU * 0.22f;
    case ChartLook::Belt: return signedU * 0.22f;
    case ChartLook::Moon: return signedU * 0.36f;
    case ChartLook::Gas:
    case ChartLook::Ice:
    case ChartLook::Ocean:
    case ChartLook::Rock: return signedU * 0.48f;
    case ChartLook::System:
    case ChartLook::Region: return signedU * 0.40f;
  }
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
  return std::clamp(base, 0.042f, 0.26f);
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
            PaintOrbitTrack3D(Vector3{systemStarPos.x, systemStarPos.y, systemStarPos.z},
                              rOrb, rOrb, tilt * 0.45f, -tilt * 0.35f, 40, orbCol);
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
          rlVertex3f(v0.x, v0.y, v0.z); rlVertex3f(v2.x, v2.y, v2.z); rlVertex3f(v3.x, v3.y, v3.z);
        };
        if (bright) { emitQuad(sz * 1.9f, Fade(amberCol, 0.24f)); }
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
            const ::Color gasCol = ChartLift(ToRay(p.amber), 0.38f);
            PaintShadedSphere3D(wp, r3, 8, 14, gasCol, lightRef, pointLight, 1, seed, p, alpha);
            const float ringTiltX = 0.42f + static_cast<float>(seed % 7u) * 0.04f;
            const float ringTiltZ = -0.22f + static_cast<float>((seed / 7u) % 5u) * 0.06f;
            PaintRing3D(wp, r3 * 1.38f, r3 * 1.72f, ringTiltX, ringTiltZ, 28,
                        ChartFade(ChartLift(ToRay(p.coffeeLight), 0.25f), 0.78f * alpha));
            PaintRing3D(wp, r3 * 1.82f, r3 * 2.08f, ringTiltX, ringTiltZ, 28,
                        ChartFade(ToRay(p.coffeeLight), 0.48f * alpha));
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

            // Orbiting 3D telemetry satellites flying continuously around every station
            const float satOrbitR = r3 * 2.65f;
            const float satAngle = tSec * 0.85f + static_cast<float>(seed % 31u) * 0.2f;
            const float satTiltX = 0.32f + static_cast<float>(seed % 5u) * 0.10f;
            const float satTiltZ = -0.25f;
            PaintOrbitTrack3D(wp, satOrbitR, satOrbitR * 0.88f, satTiltX, satTiltZ, 28,
                              Fade(ToRay(p.coffeeLight), 0.42f * alpha));
            const float lx = std::cos(satAngle) * satOrbitR;
            const float lz = std::sin(satAngle) * satOrbitR * 0.88f;
            const float cxT = std::cos(satTiltX), sxT = std::sin(satTiltX);
            const float czT = std::cos(satTiltZ), szT = std::sin(satTiltZ);
            const float ly1 = -lz * sxT, lz1 = lz * cxT;
            const Vector3 satPos{wp.x + (lx * czT - ly1 * szT),
                                 wp.y + (lx * szT + ly1 * czT),
                                 wp.z + lz1};
            const float tx = -std::sin(satAngle);
            const float tz = std::cos(satAngle) * 0.88f;
            PaintSatellite3D(satPos, r3 * 0.44f, tx, tz, p,
                             reduced ? 1.0f : (0.5f + 0.5f * std::sin(tSec * 6.0f + satAngle)));
            break;
          }
          case ChartLook::Site: {
            const float spin = tSec * 0.75f + static_cast<float>(seed % 17u);
            PaintAsteroid3D(wp, r3 * 0.68f, 0.35f, spin, 0.2f, ToRay(p.terracotta),
                            fallbackKey, seed);
            PaintOrbitTrack3D(wp, r3 * 1.25f, r3 * 1.25f, 0.5f, 0.0f, 18,
                              Fade(ToRay(p.terracotta), 0.55f * alpha));
            break;
          }
          case ChartLook::Belt: {
            const ::Color beltCol = ChartMix(ToRay(p.coffeeLight), ToRay(p.coffeeDark), 0.25f);
            PaintOrbitTrack3D(wp, r3 * 1.7f, r3 * 1.15f, 0.22f, -0.15f, 24,
                              Fade(beltCol, 0.42f * alpha));
            for (int bi = 0; bi < 6; ++bi) {
              const float a = static_cast<float>(bi) / 6.0f * 2.0f * PI + tSec * 0.22f;
              const Vector3 bp{
                  wp.x + std::cos(a) * r3 * 1.65f,
                  wp.y + std::sin(a * 2.0f) * r3 * 0.28f,
                  wp.z + std::sin(a) * r3 * 1.12f,
              };
              PaintAsteroid3D(bp, r3 * (0.20f + 0.04f * static_cast<float>(bi % 3)),
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

  float ty = std::max({ly, rightBottom, uy + underH, leftButtonBottom, rightButtonBottom}) + 12;
  for (const CommandRowView& row : screen.commands) {
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
    if (row.charged) { PaintProgressFill(box, row.chargePercent, kChamferCard); }
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
      DrawTexturePro(*portrait,
                     Rectangle{0, 0, static_cast<float>(portrait->width),
                               static_cast<float>(portrait->height)},
                     Rectangle{std::round(card.x + (card.w - w) / 2.0f),
                               std::round(card.Bottom() - h), w, h},
                     Vector2{0, 0}, 0.0f, WHITE);
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
    if (cloth != nullptr) {
      PaintRiggedChibi(*cloth, drawShown, hall, stageScale, pose,
                       HexColor(shown->cloth, Color{162, 148, 127, 255}));
    }
    PaintRiggedChibi(*figure, drawShown, hall, stageScale, pose, WHITE);

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
  cargoModel_ = BuildCargoRoom(sim, &cargoSelection_);
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
    if (live && hasId) { PaintProgressFill(btnBox, chargePct, kChamferCard); }
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
      ImGui::EndDisabled();
      if (displayAdjustmentAvailable_ && button("Reset display adjustment", "display-reset")) {
        action("display-gamma", "1"); action("display-brightness", "1");
        action("radiation-wave", "1");
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
  setupPreview_ = BuildChartPreview(sim.SourceWorld(), setupSettings_);
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
    DrawTexturePro(*texture,
                   Rectangle{0, 0, static_cast<float>(texture->width), static_cast<float>(texture->height)},
                   Rectangle{card.x, card.y, card.w, card.h}, Vector2{0, 0}, 0, ToRay(p.white));
  } else {
    PaintChamferBoxRay(card, kChamferPanel, ToRay(p.panel),
                       ToRay(p.borderDark), ToRay(p.border));
  }

  const float pad = card.w * 0.035f;
  const float leftW = card.w * 0.42f;
  const float mapX = card.x + leftW;
  const float mapY = card.y + pad;
  const float mapW = card.w - leftW - pad;
  const float mapH = card.h * 0.72f;
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
         static_cast<int>(barY + 10), TypePx(10), ToRay(p.titleInk));
  } else {
    Text("Choose a starting system on the map.", static_cast<int>(launch.x + launch.w + 12),
         static_cast<int>(barY + 10), TypePx(10), ToRay(p.muted));
  }
}

void ShellView::PaintTitle(Simulation& sim, double nowMs) {
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
  const Area menu{card.x + card.w * 0.02f, card.Bottom() - card.h * (0.0405f + 0.0565f),
                  card.w * 0.818f, std::max(card.h * 0.0565f, theme_.LineHeight() + 12)};
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
        if (row.charged) { PaintProgressFill(box, row.chargePercent, kChamferCard); }
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

void ShellView::PaintDiagram(const DiagramModel& diagram, const Area& stageBox, float scale) {
  (void)scale;
  const ::Color frame = ToRay(theme_.palette.coffee);
  const ::Color hull = ToRay(theme_.palette.white);
  const ::Color spine = ToRay(theme_.palette.panel);
  const float ox = diagram.stage.x;
  const float oy = diagram.stage.y;
  const bool overStage = layout_.content.Contains(mouseX_, mouseY_) &&
                         stageBox.Contains(mouseX_, mouseY_ + contentScroll_);
  const float localX = mouseX_ - ox;
  const float localY = mouseY_ + contentScroll_ - oy;
  const auto crewHovered = [&](const DiagramCrew& crew) {
    const Area dot{crew.dot.x - 6.0f, crew.dot.y - 6.0f, 27.0f, 27.0f};
    const Area label{crew.label.x, crew.label.y,
                     crew.labelled ? TextWidth(crew.firstName.c_str(), TypePx(9)) : 0.0f,
                     crew.labelled ? TypeStep(12.0f) : 0.0f};
    return overStage && (dot.Contains(localX, localY) || label.Contains(localX, localY));
  };
  const auto hoveredCrew = std::find_if(diagram.crew.begin(), diagram.crew.end(), crewHovered);
  const bool overCrew = hoveredCrew != diagram.crew.end();

  for (const HullPart& part : diagram.hull) {
    const int x = static_cast<int>(ox + part.box.x);
    const int y = static_cast<int>(oy + part.box.y);
    const int w = static_cast<int>(part.box.w);
    const int h = static_cast<int>(part.box.h);
    const float radius = HullRadius(part.cls);
    if (part.cls == "hp-lens") {
      DrawCircle(static_cast<int>(x + w / 2.0f), static_cast<int>(y + h / 2.0f),
                 static_cast<float>(w) / 2.0f, hull);
      DrawRing(Vector2{x + w / 2.0f, y + h / 2.0f}, static_cast<float>(w) / 2.0f - 2.0f,
               static_cast<float>(w) / 2.0f, 0.0f, 360.0f, 64, frame);
    } else if (radius > 0.0f) {
      DrawRectangleRounded(Rectangle{static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(w), static_cast<float>(h)},
                           radius / static_cast<float>(std::max(w, h)),
                           part.cls == "hp-nacelle" ? 6 : 12,
                           part.cls == "hp-spine" ? spine : hull);
      DrawRectangleRoundedLinesEx(
          Rectangle{static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h)},
          radius / static_cast<float>(std::max(w, h)),
          part.cls == "hp-nacelle" ? 6 : 12, 2.0f, frame);
    } else {
      DrawRectangle(x, y, w, h, hull);
      DrawRectangleLinesEx(Rectangle{static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(w), static_cast<float>(h)},
                           2.0f, frame);
    }
  }

  for (const Area& bar : diagram.nacBars) {
    DrawRectangle(static_cast<int>(ox + bar.x), static_cast<int>(oy + bar.y),
                  static_cast<int>(bar.w), static_cast<int>(bar.h),
                  ToRay(theme_.palette.borderDark));
  }

  DrawRectangleLines(static_cast<int>(ox + diagram.corridor.x),
                     static_cast<int>(oy + diagram.corridor.y),
                     static_cast<int>(diagram.corridor.w),
                     static_cast<int>(diagram.corridor.h),
                     ToRay(theme_.palette.borderDark));
  Text(diagram.corridorLabel.c_str(),
           static_cast<int>(ox + diagram.corridorLabelBox.x),
           static_cast<int>(oy + diagram.corridorLabelBox.y), TypePx(9) ,
           ToRay(theme_.palette.muted));

  DrawRectangle(static_cast<int>(ox + diagram.conduit.x),
                static_cast<int>(oy + diagram.conduit.y),
                static_cast<int>(diagram.conduit.w),
                static_cast<int>(diagram.conduit.h), ToRay(theme_.palette.panel3));

  for (const Room& room : diagram.rooms) {
    const int x = static_cast<int>(ox + room.box.x);
    const int y = static_cast<int>(oy + room.box.y);
    const int w = static_cast<int>(room.box.w);
    const int h = static_cast<int>(room.box.h);
    const bool hover = overStage && !overCrew && localX >= room.box.x && localY >= room.box.y &&
                       localX < room.box.x + room.box.w && localY < room.box.y + room.box.h;
    DrawRectangle(x, y, w, h, hover ? ToRay(theme_.palette.panel3)
                                   : (theme_.id == "classic" ? HexColor(room.tone, ToRay(theme_.palette.panel2))
                                                              : ToRay(theme_.palette.panel2)));
    DrawRectangleLines(x, y, w, h, ToRay(theme_.palette.borderDark));
    const auto accent = diagram.roomAccents.find(room.id);
    DrawRectangle(x, y, 3, h, accent == diagram.roomAccents.end() ? ToRay(theme_.palette.coffee) : HexColor(accent->second, ToRay(theme_.palette.coffee)));
    // Room names are always shown: in the reference the compact deck keeps
    // names at 10 px and hides only tags/sys codes. The 18 px floor applies,
    // so names may be large in compact rooms but are never hidden.
    Text(room.name.c_str(), x + 6, y + 4, TypePx(10), ToRay(theme_.palette.ink));
    if (!diagram.compact && !room.small) {
      Text(room.tag.c_str(), x + w - 30, y + h - 14, TypePx(8),
           ToRay(theme_.palette.muted));
    }
  }

  for (const Area& divider : diagram.dividers) {
    DrawLine(static_cast<int>(ox + divider.x),
             static_cast<int>(oy + divider.y),
             static_cast<int>(ox + divider.x),
             static_cast<int>(oy + divider.Bottom()),
             ToRay(theme_.palette.border));
  }

  for (const Area& nozzle : diagram.nozzles) {
    DrawRectangle(static_cast<int>(ox + nozzle.x), static_cast<int>(oy + nozzle.y),
                  static_cast<int>(nozzle.w), static_cast<int>(nozzle.h),
                  ToRay(theme_.palette.panel3));
    DrawRectangleLines(static_cast<int>(ox + nozzle.x), static_cast<int>(oy + nozzle.y),
                       static_cast<int>(nozzle.w), static_cast<int>(nozzle.h), frame);
  }

  for (const DiagramCrew& crew : diagram.crew) {
    if (overCrew && &crew == &*hoveredCrew) {
      DrawRectangle(static_cast<int>(ox + crew.dot.x - 3), static_cast<int>(oy + crew.dot.y - 3),
                    21, 21, ToRay(theme_.palette.panel3));
      if (crew.labelled) {
        DrawRectangle(static_cast<int>(ox + crew.label.x), static_cast<int>(oy + crew.label.y),
                      static_cast<int>(TextWidth(crew.firstName.c_str(), TypePx(9))),
                      static_cast<int>(TypeStep(12.0f)), ToRay(theme_.palette.panel3));
      }
    }
    DrawRectangle(static_cast<int>(ox + crew.dot.x), static_cast<int>(oy + crew.dot.y),
                  15, 15, HexColor(crew.colour, ToRay(theme_.palette.coffeeDark)));
    if (crew.labelled) {
      Text(crew.firstName.c_str(), static_cast<int>(ox + crew.label.x),
               static_cast<int>(oy + crew.label.y), TypePx(9) , ToRay(theme_.palette.muted));
    }
  }
}

void ShellView::Move(float x, float y) {
  mouseX_ = x;
  mouseY_ = y;
}

void ShellView::Drag(Simulation& sim, float x, float y, float dx, float dy, bool down, bool pressed) {
  Move(x, y);
  if (!down) { draggingChart_ = false; draggingFeed_ = false; return; }
  if (floating_.HasConfirmation()) { return; }
  if (titleActive_ || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (feedActive_ && down && (draggingFeed_ ||
      (pressed && layout_.content.Contains(x, y) && feedThumb_.Contains(x, y + contentScroll_)))) {
    if (!draggingFeed_) { draggingFeed_ = true; feedDragOffset_ = y + contentScroll_ - feedThumb_.y; }
    const float feedTrackTop = feedBox_.y + static_cast<float>(TypePx(11)) + 12.0f;
    const float travel = feedBox_.h - (static_cast<float>(TypePx(11)) + 16.0f) - feedThumb_.h;
    if (travel > 0) feedScroll_ = std::clamp((y + contentScroll_ - feedTrackTop - feedDragOffset_) / travel, 0.0f, 1.0f) * feedMaxScroll_;
    return;
  }
  const float localY = y + contentScroll_;
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
  ShowTitle(true);
}

void ShellView::Click(Simulation& sim, float x, float y, HitButton button,
                      double nowMs) {
  if (button == HitButton::Left) {
    const bool moved = draggingChart_ && chartDragMoved_;
    draggingChart_ = false;
    chartDragMoved_ = false;
    if (moved) { return; }
  }
  if (floating_.HasConfirmation() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (button == HitButton::None) { return; }

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
      if (hit.action == "resume") {
        if (!fromVoyage_) { quitRequested_ = true; return; }
        PressTitleAction(sim, "resume", "", &fromVoyage_, &titlePanel_, &titleSlot_,
                         saveDir_, &titleMessage_, &titleActive_);
        HideTitle();
        return;
      }
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
  const float scrollY = contentScroll_;

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
                       action.rfind("upgrade:", 0) == 0 || action == "hull-service" ||
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
           hit.arg.rfind("upgrade:", 0) == 0 || hit.arg == "hull-service" ||
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
    // The view switch and room callouts are presentation only. They route to
    // exactly the same room handlers as the original deck plan.
    if (HasWrenExterior(sim) && exteriorView_) {
      for (const ExteriorCallout& item : exterior_.callouts) {
        if (!item.box.Contains(x, y + scrollY)) continue;
        if (button == HitButton::Right) { OpenTask(sim, "room", item.id, false); }
        else { OpenBriefing(sim, "room", item.id); }
        return;
      }
      if (exterior_.image.Contains(x, y + scrollY)) {
        exteriorView_ = false;
        return;
      }
    } else if (idle_.stageArea.Contains(x, y + scrollY)) {
      const float localX = x - idle_.diagram.stage.x;
      const float localY = y + scrollY - idle_.diagram.stage.y;
      for (const DiagramCrew& crew : idle_.diagram.crew) {
        const Area hit{crew.dot.x - 6.0f, crew.dot.y - 6.0f, 27.0f, 27.0f};
        const Area labelHit{crew.label.x, crew.label.y,
                            crew.labelled ? TextWidth(crew.firstName.c_str(), TypePx(9)) : 0.0f,
                            crew.labelled ? TypeStep(12.0f) : 0.0f};
        if (!hit.Contains(localX, localY) && !labelHit.Contains(localX, localY)) { continue; }
        if (button == HitButton::Right) {
          OpenTask(sim, "crew", crew.id, false);
        } else {
          OpenBriefing(sim, "crew", crew.id);
        }
        return;
      }
      for (const Room& room : idle_.diagram.rooms) {
        if (localX < room.box.x || localY < room.box.y ||
            localX >= room.box.x + room.box.w || localY >= room.box.y + room.box.h) {
          continue;
        }
        if (button == HitButton::Right) {
          OpenTask(sim, "room", room.id, false);
        } else {
          OpenBriefing(sim, "room", room.id);
        }
        return;
      }
      if (HasWrenExterior(sim)) {
        exteriorView_ = true;
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
