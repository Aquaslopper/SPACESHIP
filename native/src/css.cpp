#include "css.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "htautil.h"

namespace hta {
namespace {

struct Reader {
  const std::string& s;
  size_t i = 0;
  explicit Reader(const std::string& text) : s(text) {}
  bool eof() const { return i >= s.size(); }
  char peek() const { return i < s.size() ? s[i] : '\0'; }
  char next() { return i < s.size() ? s[i++] : '\0'; }
  bool startsWith(const char* t) const { return s.compare(i, std::strlen(t), t) == 0; }
  void skipSpace() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }
};

std::string readIdent(Reader& r) {
  size_t start = r.i;
  while (r.i < r.s.size()) {
    char c = r.s[r.i];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '\\' ||
        static_cast<unsigned char>(c) >= 0x80)
      ++r.i;
    else
      break;
  }
  return r.s.substr(start, r.i - start);
}

std::string readString(Reader& r) {
  char quote = r.next();
  std::string out;
  while (r.i < r.s.size()) {
    char c = r.next();
    if (c == quote) break;
    if (c == '\\' && r.i < r.s.size()) out.push_back(r.next());
    else out.push_back(c);
  }
  return out;
}

// Strips comments while preserving strings.
std::string stripComments(const std::string& css) {
  std::string out;
  out.reserve(css.size());
  for (size_t i = 0; i < css.size();) {
    if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*') {
      size_t end = css.find("*/", i + 2);
      i = (end == std::string::npos) ? css.size() : end + 2;
      out.push_back(' ');
      continue;
    }
    if (css[i] == '"' || css[i] == '\'') {
      char q = css[i];
      out.push_back(css[i++]);
      while (i < css.size()) {
        char c = css[i++];
        out.push_back(c);
        if (c == '\\' && i < css.size()) {
          out.push_back(css[i++]);
          continue;
        }
        if (c == q) break;
      }
      continue;
    }
    out.push_back(css[i++]);
  }
  return out;
}

bool readCompound(Reader& r, CompoundSel& out, int& spec);

// Parses one simple selector token (used by :not(...)).
bool readCompound(Reader& r, CompoundSel& out, int& spec) {
  bool any = false;
  if (r.peek() == '*') {
    r.next();
    out.tag = "*";
    any = true;
  } else if (std::isalpha(static_cast<unsigned char>(r.peek())) || r.peek() == '_') {
    out.tag = readIdent(r);
    spec += 1;
    any = true;
  }
  while (!r.eof()) {
    char c = r.peek();
    if (c == '#') {
      r.next();
      out.id = readIdent(r);
      spec += 10000;
      any = true;
    } else if (c == '.') {
      r.next();
      out.classes.push_back(readIdent(r));
      spec += 100;
      any = true;
    } else if (c == '[') {
      r.next();
      CompoundSel::Attr attr;
      r.skipSpace();
      attr.name = readIdent(r);
      r.skipSpace();
      if (r.peek() == '=') {
        r.next();
        r.skipSpace();
        if (r.peek() == '"' || r.peek() == '\'') attr.value = readString(r);
        else attr.value = readIdent(r);
        attr.op = "=";
      }
      r.skipSpace();
      if (r.peek() == ']') r.next();
      out.attrs.push_back(attr);
      spec += 100;
      any = true;
    } else if (c == ':') {
      r.next();
      if (r.peek() == ':') {
        r.next();
        out.pseudoElement = readIdent(r);
        spec += 1;
      } else {
        std::string name = readIdent(r);
        if (name == "first-child" || name == "last-child" || name == "only-child") {
          CompoundSel::Structural st;
          st.kind = name;
          out.structural.push_back(st);
          spec += 100;
        } else if (name == "nth-child" || name == "nth-last-child") {
          CompoundSel::Structural st;
          st.kind = "nth-child";
          if (r.peek() == '(') {
            r.next();
            std::string arg;
            while (!r.eof() && r.peek() != ')') arg.push_back(r.next());
            if (r.peek() == ')') r.next();
            arg = lowerAscii(trim(arg));
            if (arg == "odd") {
              st.a = 2;
              st.b = 1;
            } else if (arg == "even") {
              st.a = 2;
              st.b = 0;
            } else {
              size_t npos = arg.find('n');
              if (npos != std::string::npos) {
                std::string av = arg.substr(0, npos);
                st.a = av.empty() || av == "+" ? 1 : (av == "-" ? -1 : std::atoi(av.c_str()));
                std::string bv = arg.substr(npos + 1);
                st.b = bv.empty() ? 0 : std::atoi(bv.c_str());
              } else {
                st.a = 0;
                st.b = std::atoi(arg.c_str());
              }
            }
          }
          out.structural.push_back(st);
          spec += 100;
        } else if (name == "not") {
          if (r.peek() == '(') {
            r.next();
            CompoundSel neg;
            int sub = 0;
            readCompound(r, neg, sub);
            if (r.peek() == ')') r.next();
            out.negations.push_back(std::make_shared<CompoundSel>(neg));
            spec += sub;
          }
        } else {
          out.dynamic.push_back(name);
          spec += 100;
        }
      }
      any = true;
    } else {
      break;
    }
  }
  return any;
}

}  // namespace

bool parseSelectorList(const std::string& text, std::vector<Selector>& out) {
  size_t start = 0;
  int depth = 0;
  std::vector<std::string> pieces;
  for (size_t i = 0; i <= text.size(); ++i) {
    char c = i < text.size() ? text[i] : ',';
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (c == ',' && depth == 0) {
      pieces.push_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  for (const std::string& raw : pieces) {
    std::string piece = trim(raw);
    if (piece.empty()) continue;
    Selector sel;
    sel.text = piece;
    Reader r(piece);
    int spec = 0;
    SelectorPart part;
    bool have = false;
    while (!r.eof()) {
      r.skipSpace();
      if (r.eof()) break;
      if (r.peek() == '>') {
        r.next();
        if (have) {
          sel.parts.push_back(part);
          part = SelectorPart();
        }
        part.combinator = SelectorPart::Child;
        have = false;
        continue;
      }
      CompoundSel compound;
      int before = spec;
      if (!readCompound(r, compound, spec)) break;
      (void)before;
      if (have) {
        sel.parts.push_back(part);
        part = SelectorPart();
        part.combinator = SelectorPart::Descendant;
      }
      part.compound = compound;
      have = true;
    }
    if (have) sel.parts.push_back(part);
    sel.specificity = spec;
    if (!sel.parts.empty()) out.push_back(sel);
  }
  return !out.empty();
}

namespace {

void parseDeclarations(const std::string& body, std::vector<Declaration>& out) {
  size_t start = 0;
  int depth = 0;
  char quote = 0;
  for (size_t i = 0; i <= body.size(); ++i) {
    char c = i < body.size() ? body[i] : ';';
    if (quote) {
      if (c == quote && (i == 0 || body[i - 1] != '\\')) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (c == ';' && depth == 0) {
      std::string decl = body.substr(start, i - start);
      start = i + 1;
      size_t colon = decl.find(':');
      if (colon == std::string::npos) continue;
      Declaration d;
      d.prop = lowerAscii(trim(decl.substr(0, colon)));
      std::string value = trim(decl.substr(colon + 1));
      std::string low = lowerAscii(value);
      if (endsWith(low, "!important")) {
        d.important = true;
        value = trim(value.substr(0, value.size() - 10));
      }
      if (d.prop.empty()) continue;
      d.value = value;
      out.push_back(d);
    }
  }
}

}  // namespace

Style Style::withOnly(const std::vector<std::string>& props) const {
  Style out;
  for (const auto& p : props) {
    auto it = values.find(p);
    if (it != values.end()) out.values[p] = it->second;
  }
  return out;
}

bool StyleSheet::parse(const std::string& cssText, std::string& error) {
  std::string css = stripComments(cssText);
  Reader r(css);
  rules_.clear();
  int order = 0;
  while (!r.eof()) {
    r.skipSpace();
    if (r.eof()) break;
    if (r.peek() == '@') {
      // At-rules: @media is honoured, everything else is skipped.
      size_t atStart = r.i;
      std::string at = readIdent(r);
      while (!r.eof() && r.peek() != '{' && r.peek() != ';') r.next();
      if (r.peek() == ';') {
        r.next();
        continue;
      }
      if (r.peek() != '{') {
        error = "unterminated at-rule";
        return false;
      }
      r.next();
      std::string condition = css.substr(atStart, r.i - atStart - 1);
      int depth = 1;
      size_t bodyStart = r.i;
      while (!r.eof() && depth > 0) {
        char c = r.next();
        if (c == '{') ++depth;
        if (c == '}') --depth;
      }
      std::string body = css.substr(bodyStart, r.i - 1 - bodyStart);
      if (at != "media") continue;
      int maxw = 0, minw = 0;
      std::string cond = lowerAscii(condition);
      size_t mw = cond.find("max-width");
      if (mw != std::string::npos) {
        size_t colon = cond.find(':', mw);
        if (colon != std::string::npos) maxw = std::atoi(cond.c_str() + colon + 1);
      }
      size_t nw = cond.find("min-width");
      if (nw != std::string::npos) {
        size_t colon = cond.find(':', nw);
        if (colon != std::string::npos) minw = std::atoi(cond.c_str() + colon + 1);
      }
      // Recurse for nested media bodies.
      StyleSheet inner;
      std::string innerErr;
      if (inner.parse(body, innerErr)) {
        for (Rule rule : inner.rules()) {
          rule.mediaMaxWidth = maxw;
          rule.mediaMinWidth = minw;
          rule.order = order++;
          rules_.push_back(rule);
        }
      }
      continue;
    }
    // selector list followed by { declarations }
    size_t selStart = r.i;
    int depth = 0;
    char quote = 0;
    while (!r.eof()) {
      char c = r.peek();
      if (quote) {
        r.next();
        if (c == quote) quote = 0;
        continue;
      }
      if (c == '"' || c == '\'') {
        quote = c;
        r.next();
        continue;
      }
      if (c == '{') break;
      r.next();
    }
    if (r.eof()) break;
    std::string selectorText = css.substr(selStart, r.i - selStart);
    r.next();  // '{'
    int braceDepth = 1;
    size_t bodyStart = r.i;
    quote = 0;
    while (!r.eof() && braceDepth > 0) {
      char c = r.next();
      if (quote) {
        if (c == quote && css[r.i - 2] != '\\') quote = 0;
        continue;
      }
      if (c == '"' || c == '\'') {
        quote = c;
        continue;
      }
      if (c == '{') ++braceDepth;
      if (c == '}') --braceDepth;
    }
    std::string body = css.substr(bodyStart, r.i - 1 - bodyStart);
    Rule rule;
    rule.order = order++;
    if (!parseSelectorList(selectorText, rule.selectors)) continue;
    parseDeclarations(body, rule.decls);
    if (rule.decls.empty()) continue;
    rules_.push_back(std::move(rule));
  }
  return true;
}

void parseInlineStyle(const std::string& text, Style& out) {
  std::vector<Declaration> decls;
  parseDeclarations(text, decls);
  for (const Declaration& d : decls) out.set(d.prop, d.value);
}

bool parseColor(const std::string& raw, Color& out) {
  std::string v = lowerAscii(trim(raw));
  if (v.empty()) return false;
  if (v[0] == '#') {
    std::string hex = v.substr(1);
    auto hexVal = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
    };
    if (hex.size() == 3 || hex.size() == 4) {
      int r = hexVal(hex[0]) * 17, g = hexVal(hex[1]) * 17, b = hexVal(hex[2]) * 17;
      int a = hex.size() == 4 ? hexVal(hex[3]) * 17 : 255;
      out = Color(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b),
                  static_cast<uint8_t>(a));
      return true;
    }
    if (hex.size() >= 6) {
      int r = hexVal(hex[0]) * 16 + hexVal(hex[1]);
      int g = hexVal(hex[2]) * 16 + hexVal(hex[3]);
      int b = hexVal(hex[4]) * 16 + hexVal(hex[5]);
      int a = 255;
      if (hex.size() >= 8) a = hexVal(hex[6]) * 16 + hexVal(hex[7]);
      out = Color(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b),
                  static_cast<uint8_t>(a));
      return true;
    }
    return false;
  }
  if (startsWith(v, "rgb(") || startsWith(v, "rgba(")) {
    size_t open = v.find('('), close = v.find(')');
    if (close == std::string::npos) return false;
    std::string inner = v.substr(open + 1, close - open - 1);
    std::vector<std::string> parts = split(inner, ',');
    int comp[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < parts.size() && i < 4; ++i) {
      std::string p = trim(parts[i]);
      comp[i] = std::atoi(p.c_str());
      if (i == 3) {
        float f = static_cast<float>(std::atof(p.c_str()));
        comp[3] = static_cast<int>(f * 255.0f + 0.5f);
      }
    }
    out = Color(static_cast<uint8_t>(comp[0]), static_cast<uint8_t>(comp[1]),
                static_cast<uint8_t>(comp[2]), static_cast<uint8_t>(comp[3]));
    return true;
  }
  if (startsWith(v, "alpha(")) return false;
  struct Named {
    const char* name;
    uint32_t rgba;
  };
  static const Named named[] = {
      {"transparent", 0x00000000}, {"white", 0xFFFFFFFF}, {"black", 0xFF000000},
      {"red", 0xFFFF0000},         {"green", 0xFF008000}, {"blue", 0xFF0000FF},
      {"gray", 0xFF808080},        {"grey", 0xFF808080},  {"silver", 0xFFC0C0C0},
      {"maroon", 0xFF800000},      {"olive", 0xFF808000}, {"lime", 0xFF00FF00},
      {"teal", 0xFF008080},        {"navy", 0xFF000080},  {"purple", 0xFF800080},
      {"fuchsia", 0xFFFF00FF},     {"aqua", 0xFF00FFFF},  {"yellow", 0xFFFFFF00},
      {"orange", 0xFFFFA500},      {"inherit", 0x00000000}};
  for (const Named& n : named) {
    if (v == n.name) {
      uint32_t c = n.rgba;
      out = Color(static_cast<uint8_t>((c >> 16) & 0xFF), static_cast<uint8_t>((c >> 8) & 0xFF),
                  static_cast<uint8_t>(c & 0xFF), static_cast<uint8_t>(c >> 24));
      if (c == 0) out.a = 0;
      return true;
    }
  }
  return false;
}

bool parseLength(const std::string& value, float fontSize, float parentFontSize, float& px,
                 bool& percent, float* percentOf) {
  std::string v = trim(value);
  if (v.empty()) return false;
  char* end = nullptr;
  double n = std::strtod(v.c_str(), &end);
  if (end == v.c_str()) return false;
  std::string unit = lowerAscii(trim(std::string(end)));
  percent = false;
  if (unit == "%") {
    percent = true;
    px = percentOf ? static_cast<float>(n / 100.0 * *percentOf) : static_cast<float>(n);
    return true;
  }
  if (unit.empty() || unit == "px") {
    px = static_cast<float>(n);
    return true;
  }
  if (unit == "em") {
    px = static_cast<float>(n * fontSize);
    return true;
  }
  if (unit == "rem") {
    px = static_cast<float>(n * 12.0);
    return true;
  }
  if (unit == "pt") {
    px = static_cast<float>(n * 96.0 / 72.0);
    return true;
  }
  (void)parentFontSize;
  return false;
}

}  // namespace hta
