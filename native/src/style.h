// Cascade + computed values.
//
// The reference stylesheet sets geometry with the `* { box-sizing:border-box }`
// universe rule, so every authored width/height is an outer (border box) size.
// Geometry stays as raw strings here and is resolved during layout, where the
// containing block is finally known (percentages, auto margins, anchors).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "canvas.h"
#include "css.h"
#include "dom.h"

namespace hta {

struct BorderSide {
  float width = 0;
  std::string style;
  Color color;
  bool visible() const { return width > 0 && style != "none" && style != "hidden" && color.a > 0; }
};

struct Shadow {
  float dx = 0, dy = 0, blur = 0;
  Color color;
};

// Non-geometric computed values plus the raw geometry declarations.
struct Computed {
  // ---- inherited / text
  std::string fontFamily = "\"Segoe UI\", Tahoma, Geneva, Verdana, sans-serif";
  float fontSize = 12;
  float lineHeight = 14.64f;
  bool italic = false;
  int weight = 400;
  std::string align = "left";
  std::string transform = "none";
  float letterSpacing = 0;
  std::string whiteSpace = "normal";
  std::string wordWrap = "normal";
  Color color{59, 54, 48};
  std::string visibility = "visible";

  // ---- box model / positioning
  std::string position = "static";
  std::string display = "block";
  std::string floatDir = "none";
  std::string clear = "none";
  std::string overflowX = "visible", overflowY = "visible";
  std::string verticalAlign = "baseline";
  bool zAuto = true;
  int zIndex = 0;
  float opacity = 1;
  std::string cursor = "auto";
  bool pointerEventsNone = false;

  // Raw geometry declarations ("auto" when not authored).
  std::string rawWidth = "auto", rawHeight = "auto";
  std::string rawMinW = "0", rawMinH = "0", rawMaxW = "none", rawMaxH = "none";
  std::string rawTop = "auto", rawRight = "auto", rawBottom = "auto", rawLeft = "auto";
  std::string rawMarginT = "0", rawMarginR = "0", rawMarginB = "0", rawMarginL = "0";
  std::string rawPadT = "0", rawPadR = "0", rawPadB = "0", rawPadL = "0";

  BorderSide borderT, borderR, borderB, borderL;
  std::string rawRadiusTL = "0", rawRadiusTR = "0", rawRadiusBR = "0", rawRadiusBL = "0";

  Color background{0, 0, 0, 0};
  std::vector<std::string> backgroundImages;
  std::string backgroundSize = "auto";
  std::string backgroundPosition = "0% 0%";
  std::string backgroundRepeat = "repeat";

  std::vector<Shadow> shadows;
  std::string outlineStyle = "none";
  float outlineWidth = 0;
  Color outlineColor{0, 0, 0, 0};
  float outlineOffset = 0;
  bool outlineInset = false;
  std::string transformCss = "none";

  // ::after pseudo element (the reference uses one, for the medical cross).
  bool hasAfter = false;
  std::string afterContent;
  std::string afterLeft = "auto", afterTop = "auto", afterWidth = "auto", afterHeight = "auto";
  Color afterBackground{0, 0, 0, 0};
  bool afterInheritBackground = false;

  bool collapsed() const { return display == "none" || visibility == "hidden"; }
  bool clips() const { return overflowX != "visible" || overflowY != "visible"; }
  bool scrolls() const {
    return overflowX == "auto" || overflowY == "auto" || overflowX == "scroll" || overflowY == "scroll";
  }
  bool inlineLevel() const {
    return display == "inline" || display == "inline-block" || display == "inline-table";
  }
};

class StyleEngine {
 public:
  void setStylesheet(const StyleSheet* sheet) { sheet_ = sheet; }
  void setViewport(int w, int h) {
    viewportW_ = w;
    viewportH_ = h;
  }
  void resolve(Document& doc);
  const Computed& style(const Node* node) const;
  // Public selector matching, used by querySelectorAll in the script bridge.
  bool matchesSelectorText(const std::string& selector, const Node* node) const;

 private:
  void resolveNode(Node* node, const Computed& inherited, bool root);

 public:
  // Stateless matching helpers (also used by querySelectorAll).
  static bool matches(const Selector& sel, const Node* node);
  static bool matchesCompound(const CompoundSel& sel, const Node* node);

 private:

  const StyleSheet* sheet_ = nullptr;
  int viewportW_ = 1440, viewportH_ = 940;
  std::map<const Node*, Computed> styles_;
};

void parseInlineDeclarations(const std::string& text, StyleDecl& target);
void expandShorthand(const std::string& prop, const std::string& value, StyleDecl& target);

float parsePx(const std::string& value, float fontSize, float base, bool* wasPercent = nullptr,
              float* percentValue = nullptr);
bool isAuto(const std::string& value);

}  // namespace hta
