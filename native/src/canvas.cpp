#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hta {
namespace {

inline uint32_t pack(Color c) {
  return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
         (static_cast<uint32_t>(c.g) << 8) | c.b;
}

inline Color unpack(uint32_t v) {
  Color c;
  c.a = static_cast<uint8_t>(v >> 24);
  c.r = static_cast<uint8_t>(v >> 16);
  c.g = static_cast<uint8_t>(v >> 8);
  c.b = static_cast<uint8_t>(v);
  return c;
}

inline int iround(float v) { return static_cast<int>(std::floor(v + 0.5f)); }

// Splits value into integer cell and coverage (approximate area rules with
// an analytic integral, which is stable for the sub-pixel widths here).
inline float coverage(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

}  // namespace

std::string formatColorTxt(Color c) {
  char buf[80];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x a=%.0f", c.r, c.g, c.b, 255.0 * c.a / 255.0);
  return buf;
}

Canvas::Canvas() = default;

void Canvas::resize(int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w == w_ && h == h_) return;
  w_ = w;
  h_ = h;
  pixels_.assign(static_cast<size_t>(w_) * h_, 0xFF000000u);
  clips_.clear();
}

void Canvas::clear(Color c) {
  uint32_t v = pack(c);
  std::fill(pixels_.begin(), pixels_.end(), v);
}

Color Canvas::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return Color();
  return unpack(pixels_[static_cast<size_t>(y) * w_ + x]);
}

void Canvas::pushClip(const Rect& r) {
  Rect c = r.intersect(currentClip());
  clips_.push_back(c);
}

void Canvas::popClip() {
  if (!clips_.empty()) clips_.pop_back();
}

void Canvas::blend(int x, int y, Color c, float cov) {
  if (cov <= 0 || c.a == 0) return;
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  for (const Rect& clip : clips_) {
    if (!clip.contains(x + 0.5f, y + 0.5f)) return;
  }
  float sa = (c.a / 255.0f) * cov;
  if (sa <= 0) return;
  if (sa > 1) sa = 1;
  size_t i = static_cast<size_t>(y) * w_ + x;
  uint32_t dst = pixels_[i];
  float dr = static_cast<float>((dst >> 16) & 0xFF);
  float dg = static_cast<float>((dst >> 8) & 0xFF);
  float db = static_cast<float>(dst & 0xFF);
  float outR = c.r * sa + dr * (1 - sa);
  float outG = c.g * sa + dg * (1 - sa);
  float outB = c.b * sa + db * (1 - sa);
  pixels_[i] = 0xFF000000u | (static_cast<uint32_t>(outR + 0.5f) << 16) |
               (static_cast<uint32_t>(outG + 0.5f) << 8) | static_cast<uint32_t>(outB + 0.5f);
}

void Canvas::blendPixel(int x, int y, Color c, float cov) { blend(x, y, c, cov); }

void Canvas::blendSpan(int x, int y, int w, Color c) {
  for (int i = 0; i < w; ++i) blend(x + i, y, c, 1.0f);
}

void Canvas::blendMask(int x, int y, int w, int h, const uint8_t* mask, int stride, Color c) {
  for (int row = 0; row < h; ++row) {
    const uint8_t* line = mask + static_cast<size_t>(row) * stride;
    for (int col = 0; col < w; ++col) {
      if (line[col]) blend(x + col, y + row, c, line[col] / 255.0f);
    }
  }
}

void Canvas::fillRect(const Rect& r, Color c) {
  Rect rc = r.intersect(currentClip());
  if (rc.empty() || c.a == 0) return;
  int x0 = static_cast<int>(std::floor(rc.x + 0.5f));
  int x1 = static_cast<int>(std::floor(rc.right() + 0.5f));
  int y0 = static_cast<int>(std::floor(rc.y + 0.5f));
  int y1 = static_cast<int>(std::floor(rc.bottom() + 0.5f));
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(w_, x1);
  y1 = std::min(h_, y1);
  if (x1 <= x0 || y1 <= y0) return;
  if (c.a == 255) {
    uint32_t v = pack(c);
    for (int y = y0; y < y1; ++y) {
      uint32_t* row = pixels_.data() + static_cast<size_t>(y) * w_;
      std::fill(row + x0, row + x1, v);
    }
    return;
  }
  float alpha = c.a / 255.0f;
  for (int y = y0; y < y1; ++y) {
    uint32_t* row = pixels_.data() + static_cast<size_t>(y) * w_;
    for (int x = x0; x < x1; ++x) {
      uint32_t dst = row[x];
      float dr = static_cast<float>((dst >> 16) & 0xFF), dg = static_cast<float>((dst >> 8) & 0xFF),
            db = static_cast<float>(dst & 0xFF);
      float outR = c.r * alpha + dr * (1 - alpha);
      float outG = c.g * alpha + dg * (1 - alpha);
      float outB = c.b * alpha + db * (1 - alpha);
      row[x] = 0xFF000000u | (static_cast<uint32_t>(outR + 0.5f) << 16) |
               (static_cast<uint32_t>(outG + 0.5f) << 8) | static_cast<uint32_t>(outB + 0.5f);
    }
  }
}

void Canvas::fillVerticalGradient(const Rect& r, Color top, Color bottom) {
  Rect rc = r.intersect(currentClip());
  if (rc.empty()) return;
  int y0 = std::max(0, iround(rc.y)), y1 = std::min(h_, iround(rc.bottom()));
  for (int y = y0; y < y1; ++y) {
    float t = r.h > 0 ? (y - r.y) / r.h : 0;
    t = std::min(1.0f, std::max(0.0f, t));
    Color c;
    c.a = static_cast<uint8_t>(top.a + (bottom.a - top.a) * t);
    c.r = static_cast<uint8_t>(top.r + (bottom.r - top.r) * t);
    c.g = static_cast<uint8_t>(top.g + (bottom.g - top.g) * t);
    c.b = static_cast<uint8_t>(top.b + (bottom.b - top.b) * t);
    fillRect(Rect{r.x, static_cast<float>(y), r.w, 1}, c);
  }
}

// Signed-distance based rounded rectangle fill with 1px anti-aliasing.
void Canvas::fillRoundedRect(const Rect& r, const Corners& corners, Color c) {
  Rect clip = currentClip();
  Rect rc = r.intersect(clip);
  if (rc.empty() || c.a == 0) return;
  int x0 = std::max(0, iround(rc.x) - 1), x1 = std::min(w_, iround(rc.right()) + 1);
  int y0 = std::max(0, iround(rc.y) - 1), y1 = std::min(h_, iround(rc.bottom()) + 1);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      float px = x + 0.5f, py = y + 0.5f;
      // Determine which corner region this pixel falls in.
      float dx = 0, dy = 0;
      bool corner = false;
      if (px < r.x + corners.tl && py < r.y + corners.tl) {
        dx = px - (r.x + corners.tl);
        dy = py - (r.y + corners.tl);
        corner = true;
      } else if (px > r.right() - corners.tr && py < r.y + corners.tr) {
        dx = px - (r.right() - corners.tr);
        dy = py - (r.y + corners.tr);
        corner = true;
      } else if (px > r.right() - corners.br && py > r.bottom() - corners.br) {
        dx = px - (r.right() - corners.br);
        dy = py - (r.bottom() - corners.br);
        corner = true;
      } else if (px < r.x + corners.bl && py > r.bottom() - corners.bl) {
        dx = px - (r.x + corners.bl);
        dy = py - (r.bottom() - corners.bl);
        corner = true;
      }
      float cov = 1.0f;
      if (corner) {
        float radius = (px < r.x + r.w / 2) ? (py < r.y + r.h / 2 ? corners.tl : corners.bl)
                                            : (py < r.y + r.h / 2 ? corners.tr : corners.br);
        float dist = std::sqrt(dx * dx + dy * dy) - radius;
        cov = coverage(0.5f - dist);
      }
      if (cov > 0) blend(x, y, c, cov);
    }
  }
}

void Canvas::strokeRect(const Rect& r, float width, Color c) {
  fillRect(Rect{r.x, r.y, r.w, width}, c);
  fillRect(Rect{r.x, r.bottom() - width, r.w, width}, c);
  fillRect(Rect{r.x, r.y + width, width, r.h - 2 * width}, c);
  fillRect(Rect{r.right() - width, r.y + width, width, r.h - 2 * width}, c);
}

void Canvas::strokeRoundedRect(const Rect& r, const Corners& corners, float width, Color c) {
  if (!corners.any()) {
    strokeRect(r, width, c);
    return;
  }
  fillRoundedRect(r, corners, c);
  Rect inner{r.x + width, r.y + width, r.w - 2 * width, r.h - 2 * width};
  Corners in = corners;
  in.tl = std::max(0.0f, in.tl - width);
  in.tr = std::max(0.0f, in.tr - width);
  in.br = std::max(0.0f, in.br - width);
  in.bl = std::max(0.0f, in.bl - width);
  // Erasing an inner rounded rect requires the shape below; fill it in the
  // caller-visible colour is wrong, so instead draw the ring analytically.
  (void)inner;
  (void)in;
  // Analytic ring: for every pixel inside r but not inside the inset shape.
  Rect clip = currentClip();
  Rect rc = r.intersect(clip);
  if (rc.empty()) return;
  int x0 = std::max(0, iround(rc.x) - 1), x1 = std::min(w_, iround(rc.right()) + 1);
  int y0 = std::max(0, iround(rc.y) - 1), y1 = std::min(h_, iround(rc.bottom()) + 1);
  auto sd = [&](float px, float py, float inset) {
    float l = r.x + inset, t = r.y + inset, rr = r.right() - inset, bb = r.bottom() - inset;
    float cxl = l, cxr = rr, cyt = t, cyb = bb, radius = 0;
    if (px < l + corners.tl - inset && py < t + corners.tl - inset) {
      radius = std::max(0.0f, corners.tl - inset);
      float dx = px - (l + radius), dy = py - (t + radius);
      return std::sqrt(dx * dx + dy * dy) - radius;
    }
    if (px > rr - corners.tr + inset && py < t + corners.tr - inset) {
      radius = std::max(0.0f, corners.tr - inset);
      float dx = px - (rr - radius), dy = py - (t + radius);
      return std::sqrt(dx * dx + dy * dy) - radius;
    }
    if (px > rr - corners.br + inset && py > bb - corners.br + inset) {
      radius = std::max(0.0f, corners.br - inset);
      float dx = px - (rr - radius), dy = py - (bb - radius);
      return std::sqrt(dx * dx + dy * dy) - radius;
    }
    if (px < l + corners.bl - inset && py > bb - corners.bl + inset) {
      radius = std::max(0.0f, corners.bl - inset);
      float dx = px - (l + radius), dy = py - (bb - radius);
      return std::sqrt(dx * dx + dy * dy) - radius;
    }
    (void)cxl;
    (void)cxr;
    (void)cyt;
    (void)cyb;
    float d = std::max(l - px, std::max(py - bb, std::max(px - rr, t - py)));
    return d;
  };
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      float px = x + 0.5f, py = y + 0.5f;
      float outer = sd(px, py, 0.0f);
      float inner = sd(px, py, width);
      float cov = coverage(0.5f - outer) * coverage(0.5f + inner);
      if (cov > 0) blend(x, y, c, cov);
    }
  }
}

void Canvas::fillEllipse(float cx, float cy, float rx, float ry, Color c) {
  if (rx <= 0 || ry <= 0) return;
  Rect clip = currentClip();
  int x0 = std::max(0, iround(cx - rx) - 1), x1 = std::min(w_, iround(cx + rx) + 1);
  int y0 = std::max(0, iround(cy - ry) - 1), y1 = std::min(h_, iround(cy + ry) + 1);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (!clip.contains(x + 0.5f, y + 0.5f)) continue;
      float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
      float d = std::sqrt(dx * dx + dy * dy);
      // Approximate distance in pixels: scale the normalised distance.
      float scale = std::min(rx, ry);
      float cov = coverage((1.0f - d) * scale + 0.5f);
      if (cov > 0) blend(x, y, c, cov);
    }
  }
}

void Canvas::strokeEllipse(float cx, float cy, float rx, float ry, float width, Color c) {
  Rect clip = currentClip();
  float outer = std::max(rx, ry) + width + 2;
  int x0 = std::max(0, iround(cx - outer)), x1 = std::min(w_, iround(cx + outer));
  int y0 = std::max(0, iround(cy - outer)), y1 = std::min(h_, iround(cy + outer));
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (!clip.contains(x + 0.5f, y + 0.5f)) continue;
      float dx = (x + 0.5f - cx), dy = (y + 0.5f - cy);
      // Distance to the ellipse boundary, normalised by the gradient.
      float nx = dx / (rx * rx), ny = dy / (ry * ry);
      float g = std::sqrt(nx * nx + ny * ny);
      if (g < 1e-6f) continue;
      float f = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
      float dist = (f - 1.0f) / (2.0f * g);
      float cov = coverage(width / 2 - std::fabs(dist) + 0.5f);
      if (cov > 0) blend(x, y, c, cov);
    }
  }
}

void Canvas::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
  std::vector<float> pts = {x0, y0, x1, y1, x2, y2};
  fillPolygon(pts, c);
}

// Scanline polygon fill with 4x vertical supersampling and analytic
// horizontal coverage. Non-zero winding like the browser's canvas fill.
void Canvas::fillPolygon(const std::vector<float>& points, Color c) {
  size_t n = points.size() / 2;
  if (n < 3 || c.a == 0) return;
  Rect clip = currentClip();
  if (clip.empty()) return;
  float minY = points[1], maxY = points[1];
  for (size_t i = 0; i < n; ++i) {
    minY = std::min(minY, points[i * 2 + 1]);
    maxY = std::max(maxY, points[i * 2 + 1]);
  }
  int y0 = std::max(0, iround(minY)), y1 = std::min(h_, iround(maxY) + 1);
  std::vector<float> xs;
  const int samples = 4;
  std::vector<float> cov(static_cast<size_t>(w_), 0.0f);
  for (int y = y0; y < y1; ++y) {
    std::fill(cov.begin(), cov.end(), 0.0f);
    for (int s = 0; s < samples; ++s) {
      float sy = y + (s + 0.5f) / samples;
      xs.clear();
      for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        float ax = points[i * 2], ay = points[i * 2 + 1];
        float bx = points[j * 2], by = points[j * 2 + 1];
        if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
          float t = (sy - ay) / (by - ay);
          xs.push_back(ax + t * (bx - ax));
        }
      }
      std::sort(xs.begin(), xs.end());
      for (size_t k = 0; k + 1 < xs.size(); k += 2) {
        float xa = xs[k], xb = xs[k + 1];
        if (xb <= xa) continue;
        int ia = std::max(0, static_cast<int>(std::floor(xa)));
        int ib = std::min(w_ - 1, static_cast<int>(std::ceil(xb)) - 1);
        for (int x = ia; x <= ib; ++x) {
          float covL = std::min(xb, (float)x + 1) - std::max(xa, (float)x);
          if (covL > 0) cov[x] += covL / samples;
        }
      }
    }
    for (int x = 0; x < w_; ++x) {
      if (cov[x] <= 0) continue;
      if (!clip.contains(x + 0.5f, y + 0.5f)) continue;
      float cvg = std::min(1.0f, cov[x]);
      if (cvg > 0.002f) blend(x, y, c, cvg);
    }
  }
}

void Canvas::drawLine(float x0, float y0, float x1, float y1, float width, Color c, LineCap cap) {
  if (width <= 0) return;
  float dx = x1 - x0, dy = y1 - y0;
  float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001f) {
    if (cap == LineCap::Round) fillEllipse(x0, y0, width / 2, width / 2, c);
    return;
  }
  float nx = -dy / len * (width / 2), ny = dx / len * (width / 2);
  std::vector<float> quad = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
  fillPolygon(quad, c);
  if (cap == LineCap::Round) {
    fillEllipse(x0, y0, width / 2, width / 2, c);
    fillEllipse(x1, y1, width / 2, width / 2, c);
  } else if (cap == LineCap::Square) {
    float ux = dx / len * (width / 2), uy = dy / len * (width / 2);
    std::vector<float> a = {x0 + nx - ux, y0 + ny - uy, x0 - nx - ux, y0 - ny - uy, x0 - nx + ux,
                            y0 - ny + uy, x0 + nx + ux, y0 + ny + uy};
    fillPolygon(a, c);
    std::vector<float> b = {x1 + nx - ux, y1 + ny - uy, x1 - nx - ux, y1 - ny - uy, x1 - nx + ux,
                            y1 - ny + uy, x1 + nx + ux, y1 + ny + uy};
    fillPolygon(b, c);
  }
}

void Canvas::drawDashedLine(float x0, float y0, float x1, float y1, float width, Color c, float dash) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.01f) return;
  if (dash <= 0) dash = 4;
  float ux = dx / len, uy = dy / len;
  for (float t = 0; t < len; t += dash * 2) {
    float e = std::min(len, t + dash);
    drawLine(x0 + ux * t, y0 + uy * t, x0 + ux * e, y0 + uy * e, width, c, LineCap::Butt);
  }
}

void Canvas::drawImage(const Image& image, const Rect& dst, float alpha) {
  drawImageRegion(image, Rect{0, 0, (float)image.w, (float)image.h}, dst, alpha);
}

void Canvas::drawImageRegion(const Image& image, const Rect& src, const Rect& dst, float alpha) {
  if (!image.valid() || dst.empty() || alpha <= 0) return;
  Rect clip = currentClip();
  Rect rc = dst.intersect(clip);
  if (rc.empty()) return;
  int x0 = std::max(0, iround(rc.x)), x1 = std::min(w_, iround(rc.right()));
  int y0 = std::max(0, iround(rc.y)), y1 = std::min(h_, iround(rc.bottom()));
  for (int y = y0; y < y1; ++y) {
    float ty = dst.h > 0 ? (y + 0.5f - dst.y) / dst.h : 0;
    float srcYf = src.y + ty * src.h;
    int sy = std::min(image.h - 1, std::max(0, static_cast<int>(srcYf)));
    if (sy < 0) continue;
    const Color* row = image.pixels.data() + static_cast<size_t>(sy) * image.w;
    for (int x = x0; x < x1; ++x) {
      float tx = dst.w > 0 ? (x + 0.5f - dst.x) / dst.w : 0;
      int sx = std::min(image.w - 1, std::max(0, static_cast<int>(src.x + tx * src.w)));
      Color px = row[sx];
      if (px.a == 0) continue;
      float scale = alpha * (px.a / 255.0f);
      uint8_t save = px.a;
      px.a = static_cast<uint8_t>(std::min(255.0f, save * alpha));
      (void)scale;
      blend(x, y, px, 1.0f);
    }
  }
}

}  // namespace hta
