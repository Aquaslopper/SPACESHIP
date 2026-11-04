// Software rasterizer used by the native HTA host.
//
// Everything is drawn into a 32-bit premultiplied-free ARGB buffer and
// presented through a single GDI StretchDIBits call. The rasterizer only
// needs what the reference stylesheet and markup actually use: filled
// rectangles, rounded rectangles, borders, anti-aliased polygons (for the
// rotated CSS transforms and the perspective chart raster), pixel-smooth
// text (drawn by the text layer) and images.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hta {

struct Color {
  uint8_t a = 0, r = 0, g = 0, b = 0;
  Color() = default;
  Color(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255) : a(aa), r(rr), g(gg), b(bb) {}
  bool visible() const { return a != 0; }
};

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  float right() const { return x + w; }
  float bottom() const { return y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
  Rect intersect(const Rect& o) const {
    float l = x > o.x ? x : o.x, t = y > o.y ? y : o.y;
    float r = right() < o.right() ? right() : o.right();
    float b = bottom() < o.bottom() ? bottom() : o.bottom();
    Rect out;
    out.x = l;
    out.y = t;
    out.w = r - l;
    out.h = b - t;
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
  }
  bool contains(float px, float py) const {
    return px >= x && px < right() && py >= y && py < bottom();
  }
};

// Axis-aligned rounded-rect corner radii. CSS border-radius shorthand is
// reduced to these; the reference uses uniform radii plus a few mixed forms.
struct Corners {
  float tl = 0, tr = 0, br = 0, bl = 0;
  bool any() const { return tl > 0 || tr > 0 || br > 0 || bl > 0; }
};

enum class LineCap { Butt, Round, Square };

struct FontMetricsCache;

// A decoded image, RGBA with straight alpha.
struct Image {
  int w = 0, h = 0;
  std::vector<Color> pixels;
  bool valid() const { return w > 0 && h > 0 && pixels.size() == static_cast<size_t>(w) * h; }
};

class Canvas {
 public:
  Canvas();
  void resize(int w, int h);
  int width() const { return w_; }
  int height() const { return h_; }
  const uint32_t* data() const { return pixels_.data(); }
  uint32_t* data() { return pixels_.data(); }

  void clear(Color c);
  Color pixel(int x, int y) const;

  // Clip stack. Coordinates in device pixels; the style layer works in
  // window client space which matches the canvas one to one.
  void pushClip(const Rect& r);
  void popClip();
  Rect currentClip() const { return clips_.empty() ? Rect{0, 0, (float)w_, (float)h_} : clips_.back(); }

  // Basic shapes. All are anti-aliased at the edges.
  void fillRect(const Rect& r, Color c);
  void fillRoundedRect(const Rect& r, const Corners& corners, Color c);
  void fillRoundRect(const Rect& r, float radius, Color c) {
    Corners k;
    k.tl = k.tr = k.br = k.bl = radius;
    fillRoundedRect(r, k, c);
  }
  void strokeRect(const Rect& r, float width, Color c);
  void strokeRoundedRect(const Rect& r, const Corners& corners, float width, Color c);
  void fillEllipse(float cx, float cy, float rx, float ry, Color c);
  void strokeEllipse(float cx, float cy, float rx, float ry, float width, Color c);
  void fillPolygon(const std::vector<float>& points, Color c);
  void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c);
  void drawLine(float x0, float y0, float x1, float y1, float width, Color c, LineCap cap = LineCap::Butt);
  void drawDashedLine(float x0, float y0, float x1, float y1, float width, Color c, float dash);
  void drawImage(const Image& image, const Rect& dst, float alpha = 1.0f);
  void drawImageRegion(const Image& image, const Rect& src, const Rect& dst, float alpha = 1.0f);

  // Used by the text layer: composite an 8-bit coverage mask.
  void blendMask(int x, int y, int w, int h, const uint8_t* mask, int stride, Color c);
  void blendSpan(int x, int y, int w, Color c);
  void blendPixel(int x, int y, Color c, float coverage);

  // Cheap vertical/horizontal gradients used by a handful of rules.
  void fillVerticalGradient(const Rect& r, Color top, Color bottom);

 private:
  void blend(int x, int y, Color c, float coverage);
  int w_ = 0, h_ = 0;
  std::vector<uint32_t> pixels_;
  std::vector<Rect> clips_;
};

// Presentation surface: wraps a DIB section plus a Canvas. Implemented in
// canvas_win.cpp on Windows and as a plain buffer elsewhere (the test suite
// renders headlessly).
class Surface {
 public:
  Surface();
  ~Surface();
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;
  bool ensure(int w, int h);
  Canvas& canvas() { return canvas_; }
  // Blits to any HDC (window DC or a memory DC used for PNG captures).
  void present(void* hdc, int x, int y, int w, int h);

 private:
  int w_ = 0, h_ = 0;
  Canvas canvas_;
  struct Impl;
  Impl* impl_ = nullptr;
};

std::string formatColorTxt(Color c);

}  // namespace hta
