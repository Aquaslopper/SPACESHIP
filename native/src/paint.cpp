#include "paint.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "htautil.h"

namespace hta {
namespace {

std::function<const Image*(const std::string&)> g_imageResolver;

size_t matchParen(const std::string& s, size_t open) {
  int depth = 0;
  for (size_t i = open; i < s.size(); ++i) {
    if (s[i] == '(') ++depth;
    if (s[i] == ')') {
      --depth;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

std::vector<std::string> splitTop(const std::string& value, char sep) {
  std::vector<std::string> out;
  int depth = 0;
  char quote = 0;
  std::string cur;
  for (char c : value) {
    if (quote) {
      cur.push_back(c);
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      cur.push_back(c);
      continue;
    }
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (c == sep && depth == 0) {
      out.push_back(trim(cur));
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  if (!trim(cur).empty()) out.push_back(trim(cur));
  return out;
}

struct Stop {
  Color color;
  float pos = 0;
};

// "transparent 12px, rgba(139,111,78,0.13) 12px, rgba(...) 13px, transparent 13px"
std::vector<Stop> parseStops(const std::vector<std::string>& tokens, float extent) {
  std::vector<Stop> stops;
  float last = 0;
  for (const std::string& token : tokens) {
    std::string t = trim(token);
    if (t.empty()) continue;
    std::string colorTok = t;
    std::string posTok;
    // The colour may itself be a function; find the token boundary after it.
    if (t[0] == '#') {
      size_t sp = t.find(' ');
      if (sp != std::string::npos) {
        colorTok = t.substr(0, sp);
        posTok = trim(t.substr(sp + 1));
      }
    } else {
      size_t open = t.find('(');
      if (open != std::string::npos) {
        size_t close = matchParen(t, open);
        if (close != std::string::npos) {
          colorTok = t.substr(0, close + 1);
          posTok = trim(t.substr(close + 1));
        }
      } else {
        size_t sp = t.find(' ');
        if (sp != std::string::npos) {
          colorTok = t.substr(0, sp);
          posTok = trim(t.substr(sp + 1));
        }
      }
    }
    Color col;
    if (!parseColor(colorTok, col)) continue;
    Stop s;
    s.color = col;
    if (posTok.empty()) {
      s.pos = last;
    } else {
      float v = 0;
      bool pct = false;
      if (parsePx(posTok, 12, extent, &pct, nullptr)) {
        if (endsWith(lowerAscii(posTok), "%")) v = extent * static_cast<float>(std::atof(posTok.c_str())) / 100.0f;
        else v = parsePx(posTok, 12, extent);
        s.pos = extent > 0 ? v / extent : 0;
      } else {
        s.pos = last;
      }
    }
    last = s.pos;
    stops.push_back(s);
  }
  return stops;
}

Color sampleStops(const std::vector<Stop>& stops, float t) {
  if (stops.empty()) return Color(0, 0, 0, 0);
  if (t <= stops.front().pos) return stops.front().color;
  if (t >= stops.back().pos) return stops.back().color;
  for (size_t i = 0; i + 1 < stops.size(); ++i) {
    const Stop& a = stops[i];
    const Stop& b = stops[i + 1];
    if (t >= a.pos && t <= b.pos) {
      float span = b.pos - a.pos;
      float f = span <= 0.000001f ? 0.0f : (t - a.pos) / span;
      Color out;
      out.a = static_cast<uint8_t>(a.color.a + (b.color.a - a.color.a) * f);
      out.r = static_cast<uint8_t>(a.color.r + (b.color.r - a.color.r) * f);
      out.g = static_cast<uint8_t>(a.color.g + (b.color.g - a.color.g) * f);
      out.b = static_cast<uint8_t>(a.color.b + (b.color.b - a.color.b) * f);
      return out;
    }
  }
  return stops.back().color;
}

float gradientAngle(const std::vector<std::string>& parts) {
  if (parts.empty()) return 180;
  std::string first = lowerAscii(trim(parts[0]));
  if (endsWith(first, "deg")) {
    return static_cast<float>(std::atof(parts[0].c_str()));
  }
  if (startsWith(first, "to ")) {
    bool top = first.find("top") != std::string::npos;
    bool bottom = first.find("bottom") != std::string::npos;
    bool left = first.find("left") != std::string::npos;
    bool right = first.find("right") != std::string::npos;
    if (bottom && right) return 135;
    if (bottom && left) return 225;
    if (top && right) return 45;
    if (top && left) return 315;
    if (top) return 0;
    if (right) return 90;
    if (left) return 270;
    return 180;
  }
  return 180;
}

}  // namespace

std::string Painter::urlOf(const std::string& css) {
  std::string low = lowerAscii(css);
  size_t start = low.find("url(");
  if (start == std::string::npos) return std::string();
  size_t close = matchParen(css, start + 3);
  if (close == std::string::npos) return std::string();
  std::string url = css.substr(start + 4, close - start - 4);
  if (!url.empty() && (url.front() == '"' || url.front() == '\'')) url = url.substr(1, url.size() - 2);
  return url;
}

void Painter::setImageResolver(std::function<const Image*(const std::string&)> resolver) {
  g_imageResolver = std::move(resolver);
}

void Painter::drawGradient(Canvas& c, const Rect& box, const std::string& css) {
  std::string low = lowerAscii(css);
  size_t open = low.find('(');
  if (open == std::string::npos) return;
  size_t close = matchParen(css, open);
  if (close == std::string::npos) return;
  std::string body = css.substr(open + 1, close - open - 1);
  std::vector<std::string> parts = splitTop(body, ',');
  if (parts.size() < 2) return;
  std::string first = lowerAscii(trim(parts[0]));
  bool hasDirection = endsWith(first, "deg") || startsWith(first, "to ");
  float angle = hasDirection ? gradientAngle(parts) : 180.0f;
  size_t startIndex = hasDirection ? 1 : 0;
  std::vector<std::string> stopTokens(parts.begin() + static_cast<long>(startIndex), parts.end());

  float rad = (angle - 90.0f) * 3.14159265f / 180.0f;
  float dx = std::cos(rad), dy = std::sin(rad);
  float lineLen = std::fabs(box.w * dx) + std::fabs(box.h * dy);
  if (lineLen < 1) lineLen = 1;
  std::vector<Stop> stops = parseStops(stopTokens, lineLen);
  if (stops.empty()) return;
  Rect clip = c.currentClip();
  Rect rc = box.intersect(clip);
  if (rc.empty()) return;
  int x0 = std::max(0, static_cast<int>(std::floor(rc.x)));
  int x1 = std::min(c.width(), static_cast<int>(std::ceil(rc.right())));
  int y0 = std::max(0, static_cast<int>(std::floor(rc.y)));
  int y1 = std::min(c.height(), static_cast<int>(std::ceil(rc.bottom())));
  float cx = box.x + box.w / 2, cy = box.y + box.h / 2;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      float px = x + 0.5f - cx, py = y + 0.5f - cy;
      float t = (px * dx + py * dy) / lineLen + 0.5f;
      Color col = sampleStops(stops, t < 0 ? 0 : (t > 1 ? 1 : t));
      if (col.a == 0) continue;
      c.blendPixel(x, y, col, 1.0f);
    }
  }
}

void Painter::paintBox(Canvas& c, const Rect& box, const Computed& style, float radiusBase) {
  if (box.empty()) return;
  Corners corners;
  float minSide = std::min(box.w, box.h) / 2;
  auto range = [&](const std::string& raw) -> float {
    std::string v = trim(raw);
    if (v.empty()) return 0;
    if (endsWith(lowerAscii(v), "%")) return minSide * static_cast<float>(std::atof(v.c_str())) / 100.0f;
    float px = parsePx(v, style.fontSize, radiusBase);
    return std::min(px, minSide);
  };
  corners.tl = range(style.rawRadiusTL);
  corners.tr = range(style.rawRadiusTR);
  corners.br = range(style.rawRadiusBR);
  corners.bl = range(style.rawRadiusBL);

  for (const Shadow& s : style.shadows) {
    // Approximation of a blurred shadow with concentric fading layers; the
    // reference uses it for popup cards and the title dialog.
    if (s.blur <= 0.5f) {
      Rect r{box.x + s.dx, box.y + s.dy, box.w, box.h};
      if (corners.any()) c.fillRoundedRect(r, corners, s.color);
      else c.fillRect(r, s.color);
      continue;
    }
    int steps = std::max(3, static_cast<int>(s.blur));
    for (int i = steps; i >= 1; --i) {
      float grow = s.blur * i / steps;
      Color layer = s.color;
      layer.a = static_cast<uint8_t>(std::max(1.0f, s.color.a / (steps * 1.6f)));
      Rect r{box.x + s.dx - grow, box.y + s.dy - grow, box.w + grow * 2, box.h + grow * 2};
      Corners k = corners;
      k.tl += grow;
      k.tr += grow;
      k.br += grow;
      k.bl += grow;
      c.fillRoundedRect(r, k, layer);
    }
  }

  if (style.background.a > 0) {
    if (corners.any()) c.fillRoundedRect(box, corners, style.background);
    else c.fillRect(box, style.background);
  }
  if (style.backgroundImages.empty()) return;
  bool needsClip = corners.any();
  if (needsClip) c.pushClip(box);
  for (auto it = style.backgroundImages.rbegin(); it != style.backgroundImages.rend(); ++it) {
    const std::string& layer = *it;
    std::string low = lowerAscii(layer);
    if (low.find("gradient(") != std::string::npos) {
      drawGradient(c, box, layer);
    } else if (low.find("url(") != std::string::npos) {
      std::string url = urlOf(layer);
      const Image* image = g_imageResolver ? g_imageResolver(url) : nullptr;
      if (!image || !image->valid()) continue;
      Rect dst = box;
      std::string size = style.backgroundSize;
      std::string pos = style.backgroundPosition;
      if (size.find("100%") == std::string::npos && size.find("cover") == std::string::npos &&
          size.find("contain") == std::string::npos) {
        dst.w = static_cast<float>(image->w);
        dst.h = static_cast<float>(image->h);
        if (pos.find("center") != std::string::npos) {
          dst.x = box.x + (box.w - dst.w) / 2;
          dst.y = box.y + (box.h - dst.h) / 2;
        }
      }
      if (style.backgroundRepeat == "no-repeat") {
        c.drawImageRegion(*image, Rect{0, 0, (float)image->w, (float)image->h}, dst, 1.0f);
      } else {
        for (float yy = box.y; yy < box.bottom(); yy += dst.h) {
          for (float xx = box.x; xx < box.right(); xx += dst.w) {
            Rect target{xx, yy, std::min(dst.w, box.right() - xx), std::min(dst.h, box.bottom() - yy)};
            Rect src{0, 0, target.w / dst.w * image->w, target.h / dst.h * image->h};
            c.drawImageRegion(*image, src, target, 1.0f);
          }
        }
      }
    }
  }
  if (needsClip) c.popClip();
}

void Painter::paintBorders(Canvas& c, const Rect& box, const Computed& style, float radiusBase) {
  if (box.empty()) return;
  float minSide = std::min(box.w, box.h) / 2;
  Corners corners;
  auto range = [&](const std::string& raw) -> float {
    std::string v = trim(raw);
    if (v.empty()) return 0;
    if (endsWith(lowerAscii(v), "%")) return minSide * static_cast<float>(std::atof(v.c_str())) / 100.0f;
    return std::min(parsePx(v, style.fontSize, radiusBase), minSide);
  };
  corners.tl = range(style.rawRadiusTL);
  corners.tr = range(style.rawRadiusTR);
  corners.br = range(style.rawRadiusBR);
  corners.bl = range(style.rawRadiusBL);

  auto dashDraw = [&](const Rect& r, Color color, bool dashed, bool vertical) {
    if (!dashed) {
      c.fillRect(r, color);
      return;
    }
    if (vertical) {
      for (float y = r.y; y < r.bottom(); y += 7)
        c.fillRect(Rect{r.x, y, r.w, std::min(4.0f, r.bottom() - y)}, color);
    } else {
      for (float x = r.x; x < r.right(); x += 7)
        c.fillRect(Rect{x, r.y, std::min(4.0f, r.right() - x), r.h}, color);
    }
  };

  if (corners.any()) {
    // Uniform-colour rounded outlines cover every authored rounded rule. When
    // sides differ, the widest/leftmost visible side colour wins, matching the
    // reference's accent-left panels.
    Color ring = style.borderT.color;
    float width = style.borderT.width;
    for (const BorderSide* side : {&style.borderL, &style.borderR, &style.borderB}) {
      if (side->visible() && side->width >= width) {
        ring = side->color;
        width = side->width;
      }
    }
    if (width > 0) c.strokeRoundedRect(box, corners, width, ring);
    // Distinct left accent drawn over the ring.
    if (style.borderL.visible() && !(style.borderL.color.r == ring.r && style.borderL.color.g == ring.g &&
                                     style.borderL.color.b == ring.b) &&
        style.borderL.width >= 2.0f) {
      float top = box.y + corners.tl, bottom = box.bottom() - corners.bl;
      if (bottom > top) c.fillRect(Rect{box.x, top, style.borderL.width, bottom - top}, style.borderL.color);
    }
    return;
  }
  if (style.borderT.visible())
    dashDraw(Rect{box.x, box.y, box.w, style.borderT.width}, style.borderT.color,
             style.borderT.style == "dashed" || style.borderT.style == "dotted", false);
  if (style.borderB.visible())
    dashDraw(Rect{box.x, box.bottom() - style.borderB.width, box.w, style.borderB.width},
             style.borderB.color, style.borderB.style == "dashed" || style.borderB.style == "dotted",
             false);
  if (style.borderL.visible())
    dashDraw(Rect{box.x, box.y + style.borderT.width, style.borderL.width,
                  box.h - style.borderT.width - style.borderB.width},
             style.borderL.color, style.borderL.style == "dashed" || style.borderL.style == "dotted",
             true);
  if (style.borderR.visible())
    dashDraw(Rect{box.right() - style.borderR.width, box.y + style.borderT.width, style.borderR.width,
                  box.h - style.borderT.width - style.borderB.width},
             style.borderR.color, style.borderR.style == "dashed" || style.borderR.style == "dotted",
             true);
}

// Paints element backgrounds recursively, then text in a second pass so text
// always sits above sibling backgrounds as the browser would order it.
void Painter::paintElement(Canvas& c, Node* node, const Computed& style) {
  Rect box{node->box.x, node->box.y, node->box.w, node->box.h};
  paintBox(c, box, style, box.w);
  bool clip = style.clips();
  if (clip) {
    c.pushClip(Rect{box.x + node->box.bdL, box.y + node->box.bdT,
                    std::max(0.0f, box.w - node->box.bdL - node->box.bdR),
                    std::max(0.0f, box.h - node->box.bdT - node->box.bdB)});
  }
  for (Node* child : node->children) {
    if (child->type != NodeType::Element) continue;
    const Computed& cs = styles_->style(child);
    if (cs.display == "none") continue;
    paintElement(c, child, cs);
  }
  paintBorders(c, box, style, box.w);
  // ::after decorations.
  if (style.hasAfter && !isAuto(style.afterWidth) && !isAuto(style.afterHeight)) {
    float x = isAuto(style.afterLeft) ? 0 : parsePx(style.afterLeft, style.fontSize, box.w);
    float y = isAuto(style.afterTop) ? 0 : parsePx(style.afterTop, style.fontSize, box.h);
    float w = parsePx(style.afterWidth, style.fontSize, box.w);
    float h = parsePx(style.afterHeight, style.fontSize, box.h);
    Color col = style.afterInheritBackground ? style.background : style.afterBackground;
    if (w > 0 && h > 0) c.fillRect(Rect{box.x + x, box.y + y, w, h}, col);
  }
  if (clip) c.popClip();
  (void)style.outlineStyle;
}

// Text pass: paints the text of `node` (when it has no element children that
// already carry it) and walks children in document order.
void Painter::paintTextPass(Canvas& c, Node* node) {
  const Computed& style = styles_->style(node);
  if (style.display == "none") return;
  bool hasElementChild = false;
  for (Node* child : node->children) {
    if (child->type == NodeType::Element) {
      hasElementChild = true;
      break;
    }
  }
  // Gather direct text-node content.
  std::string direct;
  for (Node* child : node->children) {
    if (child->type == NodeType::Text) direct += child->text;
  }
  if (!direct.empty() && !hasElementChild) {
    Rect content{node->box.x + node->box.bdL + node->box.padL - node->scrollLeft,
                 node->box.y + node->box.bdT + node->box.padT - node->scrollTop,
                 std::max(0.0f, node->box.w - node->box.bdL - node->box.bdR - node->box.padL - node->box.padR),
                 0};
    if (style.whiteSpace == "pre" && style.lineHeight > 0) {
      // fixed layout by the element itself (pre-formatted blocks)
      float y = content.y;
      size_t start = 0;
      while (start <= direct.size()) {
        size_t nl = direct.find('\n', start);
        std::string line = direct.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        line = trim(line);
        drawText(c, line, style, content.x, y, style.color);
        y += style.lineHeight;
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
    } else if (style.whiteSpace == "nowrap") {
      float y = content.y + (style.lineHeight - style.fontSize) * 0.5f;
      float x = content.x;
      if (style.align == "center") x = content.x + (content.w - textmetric::advance(direct, style.fontFamily, style.fontSize, style.weight, style.italic, style.letterSpacing)) / 2;
      else if (style.align == "right") x = content.x + content.w - textmetric::advance(direct, style.fontFamily, style.fontSize, style.weight, style.italic, style.letterSpacing);
      drawText(c, trim(direct), style, x, y, style.color);
    } else {
      float y = content.y;
      size_t start = 0;
      std::string collapsed;
      bool space = false;
      for (char ch : direct) {
        if (textmetric::isSpace(ch)) {
          if (!space) collapsed.push_back(' ');
          space = true;
        } else {
          collapsed.push_back(ch);
          space = false;
        }
      }
      std::string text = trim(collapsed);
      while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        float x = content.x;
        if (style.align == "center")
          x = content.x + (content.w - textmetric::advance(line, style.fontFamily, style.fontSize, style.weight, style.italic, style.letterSpacing)) / 2;
        else if (style.align == "right")
          x = content.x + content.w - textmetric::advance(line, style.fontFamily, style.fontSize, style.weight, style.italic, style.letterSpacing);
        drawText(c, line, style, x, y, style.color);
        y += style.lineHeight;
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
    }
  }
  // Replaced content.
  if (node->tag == "input") {
    Rect r{node->box.x, node->box.y, node->box.w, node->box.h};
    std::string value = node->inputValue;
    Computed s = style;
    s.align = "left";
    float ty = r.y + (r.h - style.fontSize) / 2 - 1;
    drawText(c, value, s, r.x + 4, ty, style.color);
  } else if (node->tag == "select") {
    Rect r{node->box.x, node->box.y, node->box.w, node->box.h};
    Computed s = style;
    std::string value = node->inputValue;
    drawText(c, value, s, r.x + 4, r.y + (r.h - style.fontSize) / 2 - 1, style.color);
    // dropdown arrow
    float ax = r.right() - 10, ay = r.y + r.h / 2 - 2;
    c.fillTriangle(ax - 4, ay, ax + 4, ay, ax, ay + 4, Color(90, 78, 64));
  }
  for (Node* child : node->children) {
    if (child->type != NodeType::Element) continue;
    const Computed& cs = styles_->style(child);
    if (cs.display == "none") continue;
    paintTextPass(c, child);
  }
}

void Painter::paint(Canvas& canvas, Document& doc, StyleEngine& styles, int viewportW, int viewportH) {
  (void)viewportW;
  (void)viewportH;
  styles_ = &styles;
  canvas_ = &canvas;
  canvas.clear(Color(255, 255, 255));
  Computed bodyStyle = styles.style(doc.body);
  canvas.clear(bodyStyle.background.a ? bodyStyle.background : Color(255, 255, 255));
  // Pass 1: all boxes, backgrounds, borders, images.
  for (Node* child : doc.body->children) {
    if (child->type != NodeType::Element) continue;
    const Computed& cs = styles.style(child);
    if (cs.display == "none") continue;
    paintElement(canvas, child, cs);
  }
  // Pass 2: text on top.
  for (Node* child : doc.body->children) {
    if (child->type != NodeType::Element) continue;
    paintTextPass(canvas, child);
  }
}

}  // namespace hta
