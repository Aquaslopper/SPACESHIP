// Layout: turns the cascaded box model into geometry.
//
// The reference layout uses a small, well-defined vocabulary: absolutely
// positioned frames with left/right/top/bottom anchors, block flow, floats
// (the id rails) and inline-block rows. There is no flexbox or grid anywhere
// in the stylesheet, so this engine implements exactly that vocabulary.
#pragma once

#include <string>
#include <vector>

#include "canvas.h"
#include "css.h"
#include "dom.h"
#include "style.h"

namespace hta {

namespace textmetric {
float advance(const std::string& text, const std::string& family, float size, int weight, bool italic,
              float letterSpacing);
bool isMonospace(const std::string& family);
bool isSpace(char c);
}  // namespace textmetric

class LayoutEngine {
 public:
  void layout(Document& doc, StyleEngine& styles, int viewportW, int viewportH);
  // Deepest element whose border box contains the point; body as fallback.
  Node* hitTest(Document& doc, float x, float y) const;

  static bool isInlineLevel(const Computed& c) {
    return c.display == "inline" || c.display == "inline-block" || c.display == "inline-table";
  }
  static bool isReplaced(const Node* n) {
    return n->tag == "img" || n->tag == "input" || n->tag == "select" || n->tag == "textarea" ||
           n->tag == "button";
  }

 private:
  // Containing block handed down to in-flow children.
  struct Flow {
    float x = 0, y = 0;  // content origin (inside padding + border)
    float w = 0, h = 0;  // content size
    float blockW = 0;    // width used for percentages / auto blocks
    int depth = 0;
  };

  void layoutElement(Node* node, const Computed& style, const Flow& flow, bool shrinkToFit);
  void layoutChildren(Node* node, const Computed& style);
  void layoutInlineRun(Node* parent, const Computed& style, const Flow& flow, float startY,
                       float& usedHeight);

  StyleEngine* styles_ = nullptr;
  int viewportW_ = 1440;
  int viewportH_ = 940;
};

// Wraps text the way a browser does for the given computed style and returns
// the resulting lines (used by layout and by the painter).
std::vector<std::string> wrapText(const std::string& text, const Computed& style, float maxWidth);

}  // namespace hta
