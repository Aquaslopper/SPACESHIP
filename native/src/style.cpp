#include "style.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "htautil.h"

namespace hta {
namespace {

bool isElement(const Node* n) { return n && n->type == NodeType::Element; }

std::vector<std::string> valueTokens(const std::string& value) {
  std::vector<std::string> out;
  std::string cur;
  int depth = 0;
  for (char c : value) {
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool looksNumeric(const std::string& tok) {
  if (tok.empty()) return false;
  char c = tok[0];
  return std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+';
}

std::string firstOf(const std::string& value, const std::vector<std::string>& allowed) {
  for (const std::string& tok : valueTokens(value)) {
    for (const std::string& a : allowed)
      if (tok == a) return tok;
  }
  return std::string();
}

std::string firstColor(const std::string& value) {
  for (const std::string& tok : valueTokens(value)) {
    std::string low = lowerAscii(tok);
    if (!tok.empty() && (tok[0] == '#' || startsWith(low, "rgb"))) return tok;
    Color tmp;
    if (parseColor(tok, tmp)) return tok;
  }
  return std::string();
}

std::string firstLengthToken(const std::string& value) {
  for (const std::string& tok : valueTokens(value)) {
    if (looksNumeric(tok)) return tok;
    if (tok == "thin" || tok == "medium" || tok == "thick") return tok;
  }
  return std::string();
}

// ---------------------------------------------------------------------------
// Background layer parsing: "linear-gradient(...) , url(...) center center"
// Lazy tokenizer that respects nesting and quoted strings.
// ---------------------------------------------------------------------------
std::vector<std::string> splitTopLevel(const std::string& value, char sep) {
  std::vector<std::string> out;
  int depth = 0;
  char quote = 0;
  std::string cur;
  for (size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
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

// Splits gradient arguments at top-level commas.
std::vector<std::string> gradientStops(const std::string& body) { return splitTopLevel(body, ','); }

}  // namespace

bool isAuto(const std::string& value) { return lowerAscii(trim(value)) == "auto"; }

float parsePx(const std::string& value, float fontSize, float base, bool* wasPercent,
              float* percentValue) {
  if (wasPercent) *wasPercent = false;
  std::string v = trim(value);
  if (v.empty() || v == "auto" || v == "none" || v == "normal") return 0;
  char* end = nullptr;
  double n = std::strtod(v.c_str(), &end);
  if (end == v.c_str()) {
    std::string low = lowerAscii(v);
    if (low == "thin") return 1;
    if (low == "medium") return 3;
    if (low == "thick") return 5;
    if (low == "xx-small") return 9;
    if (low == "x-small") return 10;
    if (low == "small") return 11;
    if (low == "medium") return 12;
    if (low == "large") return 14;
    if (low == "x-large") return 18;
    if (low == "xx-large") return 24;
    return 0;
  }
  std::string unit = lowerAscii(trim(std::string(end)));
  if (unit == "%") {
    if (wasPercent) *wasPercent = true;
    if (percentValue) *percentValue = static_cast<float>(n);
    return static_cast<float>(n / 100.0 * base);
  }
  if (unit == "em") return static_cast<float>(n * fontSize);
  if (unit == "rem") return static_cast<float>(n * 12.0);
  if (unit == "pt") return static_cast<float>(n * 96.0 / 72.0);
  if (unit.empty() || unit == "px") return static_cast<float>(n);
  return static_cast<float>(n);
}

// ---------------------------------------------------------------------------
// Shorthands
// ---------------------------------------------------------------------------
void expandShorthand(const std::string& prop, const std::string& value, StyleDecl& target) {
  if (prop == "margin" || prop == "padding") {
    std::vector<std::string> tok = valueTokens(value);
    if (tok.empty()) return;
    std::string t = tok[0];
    std::string r = tok.size() > 1 ? tok[1] : t;
    std::string b = tok.size() > 2 ? tok[2] : t;
    std::string l = tok.size() > 3 ? tok[3] : r;
    target.set(prop + "-top", t);
    target.set(prop + "-right", r);
    target.set(prop + "-bottom", b);
    target.set(prop + "-left", l);
    return;
  }
  if (prop == "border-radius") {
    std::vector<std::string> tok = valueTokens(value);
    if (tok.empty()) return;
    std::string tl = tok[0];
    std::string tr = tok.size() > 1 ? tok[1] : tl;
    std::string br = tok.size() > 2 ? tok[2] : tl;
    std::string bl = tok.size() > 3 ? tok[3] : tr;
    target.set("border-top-left-radius", tl);
    target.set("border-top-right-radius", tr);
    target.set("border-bottom-right-radius", br);
    target.set("border-bottom-left-radius", bl);
    return;
  }
  if (prop == "border") {
    std::string w = firstLengthToken(value);
    std::string st = firstOf(value, {"solid", "dashed", "dotted", "none", "hidden", "double"});
    std::string col = firstColor(value);
    if (w.empty()) w = "medium";
    if (st.empty()) st = "none";
    if (col.empty()) col = "currentColor";
    for (const char* side : {"top", "right", "bottom", "left"}) {
      target.set(std::string("border-") + side + "-width", w);
      target.set(std::string("border-") + side + "-style", st);
      target.set(std::string("border-") + side + "-color", col);
    }
    return;
  }
  if (prop == "border-top" || prop == "border-right" || prop == "border-bottom" ||
      prop == "border-left") {
    std::string w = firstLengthToken(value);
    std::string st = firstOf(value, {"solid", "dashed", "dotted", "none", "hidden", "double"});
    std::string col = firstColor(value);
    if (w.empty()) w = "medium";
    if (st.empty()) st = "none";
    if (col.empty()) col = "currentColor";
    target.set(prop + "-width", w);
    target.set(prop + "-style", st);
    target.set(prop + "-color", col);
    return;
  }
  if (prop == "border-width" || prop == "border-style" || prop == "border-color") {
    std::string suffix = prop.substr(7);
    std::vector<std::string> tok = valueTokens(value);
    if (tok.empty()) return;
    std::string t = tok[0];
    std::string r = tok.size() > 1 ? tok[1] : t;
    std::string b = tok.size() > 2 ? tok[2] : t;
    std::string l = tok.size() > 3 ? tok[3] : r;
    target.set("border-top-" + suffix, t);
    target.set("border-right-" + suffix, r);
    target.set("border-bottom-" + suffix, b);
    target.set("border-left-" + suffix, l);
    return;
  }
  if (prop == "overflow") {
    std::vector<std::string> tok = valueTokens(value);
    std::string v = tok.empty() ? "visible" : tok[0];
    target.set("overflow-x", v);
    target.set("overflow-y", v);
    return;
  }
  if (prop == "outline") {
    std::string w = firstLengthToken(value);
    std::string st = firstOf(value, {"solid", "dashed", "dotted", "none", "hidden"});
    std::string col = firstColor(value);
    target.set("outline-width", w.empty() ? "medium" : w);
    target.set("outline-style", st.empty() ? "none" : st);
    target.set("outline-color", col.empty() ? "currentColor" : col);
    return;
  }
  if (prop == "box-shadow") {
    // Recorded as-is; parsed at compute time.
    target.set("box-shadow", value);
    return;
  }
  if (prop == "font") {
    std::string rest = trim(value);
    std::string style, weight;
    // Optional leading style/weight tokens.
    for (int guard = 0; guard < 3; ++guard) {
      size_t sp = rest.find(' ');
      if (sp == std::string::npos) break;
      std::string first = lowerAscii(rest.substr(0, sp));
      if (first == "italic" || first == "oblique" || first == "bold" || first == "normal" ||
          first == "bolder" || first == "lighter") {
        if (first == "italic" || first == "oblique") style = "italic";
        else if (first != "normal") weight = first;
        rest = trim(rest.substr(sp + 1));
        continue;
      }
      // Numeric weight only when the token before the size has no unit.
      size_t px = lowerAscii(rest).find("px");
      if (!first.empty() && std::isdigit(static_cast<unsigned char>(first[0])) && px != std::string::npos &&
          sp < px && rest.find_first_of(" \t", sp + 1) != std::string::npos) {
        weight = first;
        rest = trim(rest.substr(sp + 1));
        continue;
      }
      break;
    }
    size_t sp = rest.find(' ');
    std::string sizeToken = sp == std::string::npos ? rest : rest.substr(0, sp);
    std::string family = sp == std::string::npos ? std::string() : trim(rest.substr(sp + 1));
    if (!style.empty()) target.set("font-style", style);
    if (!weight.empty()) target.set("font-weight", weight);
    if (!sizeToken.empty()) target.set("font-size", sizeToken);
    if (!family.empty()) target.set("font-family", family);
    return;
  }
  if (prop == "background") {
    std::string low = lowerAscii(value);
    std::string col = firstColor(value);
    if (!col.empty()) target.set("background-color", col);
    if (low.find("url(") != std::string::npos || low.find("gradient(") != std::string::npos) {
      // Keep the image/size/position/repeat bundle together so the painter can
      // interpret the shorthand exactly as authored.
      std::string images;
      for (const std::string& layer : splitTopLevel(value, ',')) {
        std::string l = layer;
        size_t start = lowerAscii(l).find("url(");
        if (start != std::string::npos) {
          int depth = 0;
          size_t end = start;
          for (; end < l.size(); ++end) {
            if (l[end] == '(') ++depth;
            if (l[end] == ')') {
              --depth;
              if (depth == 0) break;
            }
          }
          std::string url = l.substr(start, end - start + 1);
          if (!images.empty()) images += ", ";
          images += url;
          std::string rest = trim(l.substr(end + 1));
          if (!rest.empty()) {
            // position / size / repeat live after the image in the layer
            std::string rlow = lowerAscii(rest);
            if (rlow.find("no-repeat") != std::string::npos)
              target.set("background-repeat", "no-repeat");
            else if (rlow.find("repeat-x") != std::string::npos)
              target.set("background-repeat", "repeat-x");
            if (rlow.find("center") != std::string::npos) target.set("background-position", "center");
            size_t slash = rest.find('/');
            if (slash != std::string::npos) target.set("background-size", trim(rest.substr(slash + 1)));
          }
        } else if (startsWith(low, "linear-gradient") || startsWith(l, "linear-gradient")) {
          if (!images.empty()) images += ", ";
          images += l;
        }
      }
      if (!images.empty()) target.set("background-image", images);
    }
    return;
  }
}

void parseInlineDeclarations(const std::string& text, StyleDecl& target) {
  size_t start = 0;
  int depth = 0;
  char quote = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    char c = i < text.size() ? text[i] : ';';
    if (quote) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (c == ';' && depth == 0) {
      std::string decl = text.substr(start, i - start);
      start = i + 1;
      size_t colon = decl.find(':');
      if (colon == std::string::npos) continue;
      std::string prop = lowerAscii(trim(decl.substr(0, colon)));
      std::string value = trim(decl.substr(colon + 1));
      if (prop.empty() || value.empty()) continue;
      std::string low = lowerAscii(value);
      if (endsWith(low, "!important")) value = trim(value.substr(0, value.size() - 10));
      target.set(prop, value);
      expandShorthand(prop, value, target);
    }
  }
}

// ---------------------------------------------------------------------------
// Selector matching
// ---------------------------------------------------------------------------
bool StyleEngine::matchesCompound(const CompoundSel& sel, const Node* node) {
  if (!isElement(node)) return false;
  if (!sel.tag.empty() && sel.tag != "*" && sel.tag != node->tag) return false;
  if (!sel.id.empty() && sel.id != node->id) return false;
  for (const std::string& cls : sel.classes) {
    if (!node->hasClass(cls)) return false;
  }
  for (const CompoundSel::Attr& attr : sel.attrs) {
    auto it = node->attrs.find(attr.name);
    if (it == node->attrs.end()) return false;
    if (attr.op == "=" && it->second != attr.value) return false;
  }
  for (const std::string& dyn : sel.dynamic) {
    if (dyn == "hover" && !node->hovered) return false;
    if (dyn == "focus" && !node->focused) return false;
    if (dyn == "active" && !node->activeState) return false;
    if (dyn == "disabled" && node->attrs.count("disabled") == 0) return false;
    if (dyn == "checked" && node->attrs.count("checked") == 0) return false;
    if (dyn == "link" || dyn == "visited") return false;
  }
  for (const CompoundSel::Structural& st : sel.structural) {
    int idx = node->elementIndexAmongSiblings() + 1;
    int count = node->elementCountAmongSiblings();
    if (st.kind == "first-child" && idx != 1) return false;
    if (st.kind == "last-child" && idx != count) return false;
    if (st.kind == "only-child" && count != 1) return false;
    if (st.kind == "nth-child") {
      if (st.a == 0) {
        if (idx != st.b) return false;
      } else {
        int diff = idx - st.b;
        if (diff % st.a != 0 || diff / st.a < 0) return false;
      }
    }
  }
  for (const auto& neg : sel.negations) {
    if (neg && matchesCompound(*neg, node)) return false;
  }
  return true;
}

bool StyleEngine::matches(const Selector& sel, const Node* node) {
  if (sel.parts.empty() || !isElement(node)) return false;
  int part = static_cast<int>(sel.parts.size()) - 1;
  if (!matchesCompound(sel.parts[static_cast<size_t>(part)].compound, node)) return false;
  --part;
  const Node* current = node;
  while (part >= 0) {
    SelectorPart::Combinator comb = sel.parts[static_cast<size_t>(part) + 1].combinator;
    if (comb == SelectorPart::Child) {
      current = current->parent;
      if (!isElement(current)) return false;
      if (!matchesCompound(sel.parts[static_cast<size_t>(part)].compound, current)) return false;
      --part;
    } else {
      bool found = false;
      const Node* p = current->parent;
      while (isElement(p)) {
        if (matchesCompound(sel.parts[static_cast<size_t>(part)].compound, p)) {
          found = true;
          current = p;
          break;
        }
        p = p->parent;
      }
      if (!found) return false;
      --part;
    }
  }
  return true;
}

bool StyleEngine::matchesSelectorText(const std::string& selector, const Node* node) const {
  std::vector<Selector> selectors;
  if (!parseSelectorList(selector, selectors)) return false;
  for (const Selector& sel : selectors)
    if (StyleEngine::matches(sel, node)) return true;
  return false;
}

const Computed& StyleEngine::style(const Node* node) const {
  static const Computed empty;
  auto it = styles_.find(node);
  return it == styles_.end() ? empty : it->second;
}

// ---------------------------------------------------------------------------
// Computed style construction
// ---------------------------------------------------------------------------
namespace {

std::string pick(const std::map<std::string, std::string>& props, const char* name,
                 const std::string& fallback) {
  auto it = props.find(name);
  return it == props.end() ? fallback : it->second;
}

float resolveFontSize(const std::string& value, float parentSize) {
  std::string v = lowerAscii(trim(value));
  if (v.empty() || v == "inherit") return parentSize;
  if (v == "xx-small") return 9;
  if (v == "x-small") return 10;
  if (v == "small") return 11;
  if (v == "medium") return 12;
  if (v == "large") return 14;
  if (v == "x-large") return 18;
  if (v == "xx-large") return 24;
  if (v == "smaller") return parentSize * 0.83f;
  if (v == "larger") return parentSize * 1.2f;
  bool pct = false;
  float pctValue = 0;
  if (endsWith(v, "%")) {
    float px = parsePx(v, parentSize, parentSize, &pct, &pctValue);
    (void)px;
    return parentSize * pctValue / 100.0f;
  }
  if (endsWith(v, "em")) return parentSize * static_cast<float>(std::atof(v.c_str()));
  if (endsWith(v, "rem")) return 12.0f * static_cast<float>(std::atof(v.c_str()));
  return parsePx(v, parentSize, parentSize);
}

float resolveLineHeight(const std::string& value, float fontSize, float inheritedLineHeight) {
  std::string v = lowerAscii(trim(value));
  if (v.empty()) return inheritedLineHeight;
  if (v == "normal") return fontSize * 1.22f;
  if (v == "inherit") return inheritedLineHeight;
  // Unitless factors are multiplied by the element font size.
  char* end = nullptr;
  double n = std::strtod(v.c_str(), &end);
  if (end && *end == '\0') return static_cast<float>(n * fontSize);
  return parsePx(v, fontSize, fontSize);
}

void parseShadow(const std::string& value, std::vector<Shadow>& out) {
  for (const std::string& layer : splitTopLevel(value, ',')) {
    Shadow s;
    bool haveColor = false;
    for (const std::string& tok : valueTokens(layer)) {
      std::string low = lowerAscii(tok);
      if (low == "inset") continue;
      Color c;
      if (tok[0] == '#' || startsWith(low, "rgb") || parseColor(tok, c)) {
        if (parseColor(tok, c)) {
          s.color = c;
          haveColor = true;
        }
        continue;
      }
      if (looksNumeric(tok)) {
        float px = parsePx(tok, 12, 0);
        if (s.dx == 0 && !haveColor) {
          s.dx = px;
        } else if (s.dy == 0) {
          s.dy = px;
        } else {
          s.blur = px;
        }
      }
    }
    if (!haveColor) s.color = Color(0, 0, 0, 100);
    out.push_back(s);
  }
}

}  // namespace

void StyleEngine::resolve(Document& doc) {
  styles_.clear();
  Computed rootStyle;
  rootStyle.display = "block";
  resolveNode(doc.root, rootStyle, true);
}

void StyleEngine::resolveNode(Node* node, const Computed& inherited, bool root) {
  if (!node) return;
  if (node->type != NodeType::Element) {
    for (Node* child : node->children) resolveNode(child, inherited, false);
    return;
  }

  std::map<std::string, std::string> props;
  std::string afterProps;
  if (sheet_) {
    for (const Rule& rule : sheet_->rules()) {
      if (rule.mediaMaxWidth && viewportW_ > rule.mediaMaxWidth) continue;
      if (rule.mediaMinWidth && viewportW_ < rule.mediaMinWidth) continue;
      int best = -1;
      bool anyAfter = false;
      for (const Selector& sel : rule.selectors) {
        if (matches(sel, node)) {
          best = std::max(best, sel.specificity);
        }
        if (!sel.parts.empty() && sel.parts.back().compound.pseudoElement == "after") {
          Selector plain = sel;
          plain.parts.back().compound.pseudoElement.clear();
          if (matches(plain, node)) anyAfter = true;
        }
      }
      if (best < 0 && !anyAfter) continue;
      for (const Declaration& d : rule.decls) {
        if (anyAfter && best < 0) {
          if (d.prop != "content") afterProps += d.prop + ":" + d.value + ";";
          continue;
        }
        props[d.prop] = d.value;
      }
    }
  }
  // Inline style wins over author rules.
  for (const auto& kv : node->inlineStyle.values) props[kv.first] = kv.second;

  Computed c;
  // Inherited values first.
  c.fontFamily = inherited.fontFamily;
  c.fontSize = inherited.fontSize;
  c.lineHeight = inherited.lineHeight;
  c.italic = inherited.italic;
  c.weight = inherited.weight;
  c.align = inherited.align;
  c.transform = inherited.transform;
  c.letterSpacing = inherited.letterSpacing;
  c.whiteSpace = inherited.whiteSpace;
  c.wordWrap = inherited.wordWrap;
  c.color = inherited.color;
  c.visibility = inherited.visibility;
  c.cursor = inherited.cursor;

  if (root) {
    c.fontFamily = "\"Segoe UI\", Tahoma, Geneva, Verdana, sans-serif";
    c.fontSize = 12;
    c.lineHeight = 12 * 1.22f;
    c.color = Color(59, 54, 48);
    c.display = "block";
  }

  // Default display per tag.
  static const char* inlineTags[] = {"span", "i", "b", "strong", "em", "label", "a", "small",
                                     "nobr", "s", "u"};
  c.display = "block";
  for (const char* t : inlineTags) {
    if (node->tag == t) {
      c.display = "inline";
      break;
    }
  }
  if (node->tag == "input" || node->tag == "select" || node->tag == "img" || node->tag == "button" ||
      node->tag == "textarea")
    c.display = "inline-block";

  if (auto it = props.find("font-size"); it != props.end()) c.fontSize = resolveFontSize(it->second, inherited.fontSize);
  if (auto it = props.find("font-family"); it != props.end()) c.fontFamily = it->second;
  if (auto it = props.find("font-style"); it != props.end())
    c.italic = lowerAscii(it->second) == "italic" || lowerAscii(it->second) == "oblique";
  if (auto it = props.find("font-weight"); it != props.end()) {
    std::string v = lowerAscii(it->second);
    if (v == "bold" || v == "bolder") c.weight = 700;
    else if (v == "lighter") c.weight = 300;
    else if (v == "normal") c.weight = 400;
    else c.weight = std::atoi(v.c_str());
  }
  if (auto it = props.find("line-height"); it != props.end())
    c.lineHeight = resolveLineHeight(it->second, c.fontSize, inherited.lineHeight);
  else
    c.lineHeight = c.fontSize * 1.22f;
  if (auto it = props.find("text-align"); it != props.end()) c.align = lowerAscii(it->second);
  if (auto it = props.find("text-transform"); it != props.end()) c.transform = lowerAscii(it->second);
  if (auto it = props.find("letter-spacing"); it != props.end())
    c.letterSpacing = parsePx(it->second, c.fontSize, c.fontSize);
  if (auto it = props.find("white-space"); it != props.end()) c.whiteSpace = lowerAscii(it->second);
  if (auto it = props.find("word-wrap"); it != props.end()) c.wordWrap = lowerAscii(it->second);
  if (auto it = props.find("overflow-wrap"); it != props.end()) c.wordWrap = lowerAscii(it->second);
  if (auto it = props.find("visibility"); it != props.end()) c.visibility = lowerAscii(it->second);
  if (auto it = props.find("color"); it != props.end()) {
    std::string v = lowerAscii(it->second);
    Color col;
    if (v == "inherit") c.color = inherited.color;
    else if (parseColor(it->second, col)) c.color = col;
  }
  if (auto it = props.find("display"); it != props.end()) c.display = lowerAscii(it->second);
  if (auto it = props.find("position"); it != props.end()) c.position = lowerAscii(it->second);
  if (auto it = props.find("float"); it != props.end()) c.floatDir = lowerAscii(it->second);
  if (auto it = props.find("clear"); it != props.end()) c.clear = lowerAscii(it->second);
  if (auto it = props.find("vertical-align"); it != props.end()) c.verticalAlign = lowerAscii(it->second);
  if (auto it = props.find("cursor"); it != props.end()) c.cursor = lowerAscii(it->second);
  if (auto it = props.find("overflow-x"); it != props.end()) c.overflowX = lowerAscii(it->second);
  if (auto it = props.find("overflow-y"); it != props.end()) c.overflowY = lowerAscii(it->second);
  if (auto it = props.find("pointer-events"); it != props.end())
    c.pointerEventsNone = lowerAscii(it->second) == "none";
  if (auto it = props.find("opacity"); it != props.end()) c.opacity = static_cast<float>(std::atof(it->second.c_str()));
  if (auto it = props.find("transform"); it != props.end()) c.transformCss = it->second;
  if (auto it = props.find("z-index"); it != props.end()) {
    std::string v = lowerAscii(it->second);
    c.zAuto = v == "auto";
    c.zIndex = c.zAuto ? 0 : std::atoi(it->second.c_str());
  }

  c.rawWidth = pick(props, "width", "auto");
  c.rawHeight = pick(props, "height", "auto");
  c.rawMinW = pick(props, "min-width", "0");
  c.rawMinH = pick(props, "min-height", "0");
  c.rawMaxW = pick(props, "max-width", "none");
  c.rawMaxH = pick(props, "max-height", "none");
  c.rawTop = pick(props, "top", "auto");
  c.rawRight = pick(props, "right", "auto");
  c.rawBottom = pick(props, "bottom", "auto");
  c.rawLeft = pick(props, "left", "auto");
  c.rawMarginT = pick(props, "margin-top", "0");
  c.rawMarginR = pick(props, "margin-right", "0");
  c.rawMarginB = pick(props, "margin-bottom", "0");
  c.rawMarginL = pick(props, "margin-left", "0");
  c.rawPadT = pick(props, "padding-top", "0");
  c.rawPadR = pick(props, "padding-right", "0");
  c.rawPadB = pick(props, "padding-bottom", "0");
  c.rawPadL = pick(props, "padding-left", "0");
  c.rawRadiusTL = pick(props, "border-top-left-radius", "0");
  c.rawRadiusTR = pick(props, "border-top-right-radius", "0");
  c.rawRadiusBR = pick(props, "border-bottom-right-radius", "0");
  c.rawRadiusBL = pick(props, "border-bottom-left-radius", "0");

  auto side = [&](const char* suffix, BorderSide& target) {
    std::string widthKey = std::string("border-") + suffix + "-width";
    std::string styleKey = std::string("border-") + suffix + "-style";
    std::string colorKey = std::string("border-") + suffix + "-color";
    std::string style = lowerAscii(pick(props, styleKey.c_str(), "none"));
    std::string widthRaw = pick(props, widthKey.c_str(), "medium");
    target.style = style;
    target.width = (style == "none" || style == "hidden") ? 0 : parsePx(widthRaw, c.fontSize, c.fontSize);
    std::string colorRaw = pick(props, colorKey.c_str(), "currentColor");
    Color col;
    if (lowerAscii(colorRaw) == "currentcolor" || lowerAscii(colorRaw) == "inherit") target.color = c.color;
    else if (parseColor(colorRaw, col)) target.color = col;
    else target.color = c.color;
  };
  side("top", c.borderT);
  side("right", c.borderR);
  side("bottom", c.borderB);
  side("left", c.borderL);

  if (auto it = props.find("background-color"); it != props.end()) {
    Color col;
    std::string v = lowerAscii(it->second);
    if (v == "inherit" || v == "currentcolor") c.background = c.color;
    else if (parseColor(it->second, col)) c.background = col;
  }
  if (auto it = props.find("background-image"); it != props.end()) {
    std::string v = lowerAscii(it->second);
    if (v != "none") {
      // Layers paint first-to-last as given; the painter iterates in order.
      for (const std::string& layer : splitTopLevel(it->second, ',')) c.backgroundImages.push_back(layer);
    }
  }
  if (auto it = props.find("background-size"); it != props.end()) c.backgroundSize = lowerAscii(it->second);
  if (auto it = props.find("background-position"); it != props.end())
    c.backgroundPosition = lowerAscii(it->second);
  if (auto it = props.find("background-repeat"); it != props.end())
    c.backgroundRepeat = lowerAscii(it->second);
  if (auto it = props.find("box-shadow"); it != props.end()) parseShadow(it->second, c.shadows);
  if (auto it = props.find("outline-style"); it != props.end()) {
    c.outlineStyle = lowerAscii(it->second);
    c.outlineWidth = parsePx(pick(props, "outline-width", "medium"), c.fontSize, c.fontSize);
    Color col;
    std::string colorRaw = pick(props, "outline-color", "currentColor");
    if (lowerAscii(colorRaw) == "currentcolor") c.outlineColor = c.color;
    else if (parseColor(colorRaw, col)) c.outlineColor = col;
    else c.outlineColor = c.color;
    if (auto off = props.find("outline-offset"); off != props.end())
      c.outlineOffset = parsePx(off->second, c.fontSize, c.fontSize);
  }

  if (!afterProps.empty()) {
    StyleDecl pseudo;
    parseInlineDeclarations(afterProps, pseudo);
    c.hasAfter = true;
    c.afterLeft = pseudo.values.count("left") ? pseudo.values["left"] : "auto";
    c.afterTop = pseudo.values.count("top") ? pseudo.values["top"] : "auto";
    c.afterWidth = pseudo.values.count("width") ? pseudo.values["width"] : "auto";
    c.afterHeight = pseudo.values.count("height") ? pseudo.values["height"] : "auto";
    std::string bg = pseudo.values.count("background") ? pseudo.values["background"]
                     : pseudo.values.count("background-color") ? pseudo.values["background-color"] : "";
    if (lowerAscii(bg) == "inherit") c.afterInheritBackground = true;
    else {
      Color col;
      if (parseColor(bg, col)) c.afterBackground = col;
    }
  }

  styles_[node] = c;
  for (Node* child : node->children) resolveNode(child, c, false);
}

}  // namespace hta
