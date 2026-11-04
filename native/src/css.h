// CSS subset that the reference HTA stylesheet uses.
//
// The engine parses the real style sheet shipped inside Spaceship.hta, keeps
// the cascade (document order, specificity, !important), inheritance and the
// handful of presentational details the reference relies on. Values are kept
// as authored strings and resolved against the element when needed.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "canvas.h"

namespace hta {

struct Style {
  std::map<std::string, std::string> values;
  const std::string* get(const std::string& prop) const {
    auto it = values.find(prop);
    return it == values.end() ? nullptr : &it->second;
  }
  void set(const std::string& prop, const std::string& value) { values[prop] = value; }
  bool has(const std::string& prop) const { return values.count(prop) != 0; }
  void merge(const Style& o) {
    for (const auto& kv : o.values) values[kv.first] = kv.second;
  }
  Style withOnly(const std::vector<std::string>& props) const;
};

// ---------------------------------------------------------------------------
// Selectors
// ---------------------------------------------------------------------------
struct CompoundSel {
  std::string tag;                  // "*" or empty for any
  std::string id;                   // without '#'
  std::vector<std::string> classes; // without '.'
  struct Attr {
    std::string name, op, value;
  };
  std::vector<Attr> attrs;
  std::string pseudoElement;        // "after" / "before"
  std::vector<std::string> dynamic; // hover, focus, active, ...
  struct Structural {
    std::string kind;  // "first-child" / "last-child" / "nth-child"
    int a = 0, b = 0;
  };
  std::vector<Structural> structural;
  // :not(...) holds a nested compound selector. Indirected through
  // shared_ptr so CompoundSel itself stays copyable (Selector copies are
  // taken during matching, e.g. when stripping the ::after pseudo element).
  std::vector<std::shared_ptr<CompoundSel>> negations;
  bool empty() const {
    return tag.empty() && id.empty() && classes.empty() && attrs.empty() && structural.empty() &&
           dynamic.empty() && negations.empty();
  }
};

struct SelectorPart {
  enum Combinator { Descendant, Child } combinator = Descendant;
  CompoundSel compound;
};

struct Selector {
  std::vector<SelectorPart> parts;  // left to right (ancestor first)
  int specificity = 0;              // ids*10000 + classes/attrs/pseudo*100 + tags
  std::string text;
};

struct Declaration {
  std::string prop;
  std::string value;
  bool important = false;
};

struct Rule {
  std::vector<Selector> selectors;
  std::vector<Declaration> decls;
  int order = 0;
  int mediaMaxWidth = 0;  // 0 = always applies
  int mediaMinWidth = 0;
};

class StyleSheet {
 public:
  bool parse(const std::string& css, std::string& error);
  const std::vector<Rule>& rules() const { return rules_; }
  size_t ruleCount() const { return rules_.size(); }

 private:
  std::vector<Rule> rules_;
};

// Parses a comma separated selector list (used by querySelectorAll).
bool parseSelectorList(const std::string& text, std::vector<Selector>& out);

// Parses a style="" attribute into declarations.
void parseInlineStyle(const std::string& text, Style& out);

// Shorthand expansion, colour parsing, etc. shared by the style layer.
bool parseColor(const std::string& value, Color& out);
bool parseLength(const std::string& value, float fontSize, float parentFontSize, float& px,
                 bool& percent, float* percentOf = nullptr);

}  // namespace hta
