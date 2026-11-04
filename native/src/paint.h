// Painting: draws the laid-out tree into the canvas exactly as the reference
// stylesheet and markup describe it.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "canvas.h"
#include "css.h"
#include "dom.h"
#include "layout.h"
#include "style.h"

namespace hta {

// Text measurement lives in layout.h (textmetric namespace) and is defined once
// in textmetric.cpp, so wrapping decisions and painted glyphs always agree.

class Painter {
 public:
  void paint(Canvas& canvas, Document& doc, StyleEngine& styles, int viewportW, int viewportH);

  static void paintBox(Canvas& c, const Rect& box, const Computed& style, float radiusBase);
  static void paintBorders(Canvas& c, const Rect& box, const Computed& style, float radiusBase);
  static void drawGradient(Canvas& c, const Rect& box, const std::string& css);
  static void drawText(Canvas& c, const std::string& text, const Computed& style, float x, float y,
                       Color color);
  static void drawTextLine(Canvas& c, const std::string& text, const Computed& style, float baselineY,
                           Color color);
  static void drawTextAligned(Canvas& c, const std::string& text, const Computed& style,
                              const Rect& area, Color color,
                              const std::string& alignOverride = std::string());
  static void setImageResolver(std::function<const Image*(const std::string&)> resolver);
  static void setFontProvider(
      std::function<bool(const std::string& family, bool bold, bool italic, const uint8_t** data,
                         int* length)> provider);
  static std::string urlOf(const std::string& css);

 private:
  void paintElement(Canvas& c, Node* node, const Computed& style);
  void paintTextPass(Canvas& c, Node* node);

  StyleEngine* styles_ = nullptr;
  Canvas* canvas_ = nullptr;
};

// Useful shared helper: computes the content origin of a node.
inline void contentOrigin(const Node* n, float& x, float& y) {
  x = n->box.x + n->box.bdL + n->box.padL - n->scrollLeft;
  y = n->box.y + n->box.bdT + n->box.padT - n->scrollTop;
}

}  // namespace hta
