#include "dom.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "htautil.h"

namespace hta {

namespace {

bool isVoidTag(const std::string& t) {
  static const char* voids[] = {"area", "base", "br",   "col",    "embed", "hr",   "img",
                                "input", "link", "meta", "param", "source", "track", "wbr"};
  for (const char* v : voids)
    if (t == v) return true;
  return false;
}

void decodeEntities(std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '&') {
      size_t semi = text.find(';', i);
      if (semi != std::string::npos && semi - i <= 10) {
        std::string ent = text.substr(i + 1, semi - i - 1);
        bool handled = true;
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (ent == "nbsp") out += "\xC2\xA0";
        else if (ent.size() > 1 && ent[0] == '#') {
          int code = (ent[1] == 'x' || ent[1] == 'X')
                         ? static_cast<int>(std::strtol(ent.c_str() + 2, nullptr, 16))
                         : std::atoi(ent.c_str() + 1);
          std::string utf8;
          if (code < 0x80) {
            utf8.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (code >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            utf8.push_back(static_cast<char>(0xE0 | (code >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          out += utf8;
        } else {
          handled = false;
        }
        if (handled) {
          i = semi + 1;
          continue;
        }
      }
    }
    out.push_back(text[i++]);
  }
  text.swap(out);
}

struct Parser {
  Document& doc;
  const std::string& html;
  size_t i = 0;
  explicit Parser(Document& d, const std::string& h) : doc(d), html(h) {}
  bool eof() const { return i >= html.size(); }
  bool at(const char* t) const { return html.compare(i, std::strlen(t), t) == 0; }
  void skipSpace() {
    while (i < html.size() && std::isspace(static_cast<unsigned char>(html[i]))) ++i;
  }
  std::string readTagName() {
    size_t start = i;
    while (i < html.size() && (std::isalnum(static_cast<unsigned char>(html[i])) || html[i] == '-' ||
                               html[i] == '_' || html[i] == ':'))
      ++i;
    std::string t = html.substr(start, i - start);
    return lowerAscii(t);
  }
  std::string readAttr() {
    if (i >= html.size()) return std::string();
    if (html[i] == '"' || html[i] == '\'') {
      char q = html[i++];
      size_t start = i;
      while (i < html.size() && html[i] != q) ++i;
      std::string v = html.substr(start, i - start);
      if (i < html.size()) ++i;
      decodeEntities(v);
      return v;
    }
    size_t start = i;
    while (i < html.size() && !std::isspace(static_cast<unsigned char>(html[i])) && html[i] != '>' &&
           html[i] != '/')
      ++i;
    std::string v = html.substr(start, i - start);
    decodeEntities(v);
    return v;
  }
  bool readAttrs(Node* node) {
    while (!eof()) {
      skipSpace();
      if (eof()) return false;
      if (html[i] == '>') {
        ++i;
        return false;
      }
      if (html[i] == '/') {
        ++i;
        if (!eof() && html[i] == '>') {
          ++i;
          return true;
        }
        continue;
      }
      size_t start = i;
      while (i < html.size() && !std::isspace(static_cast<unsigned char>(html[i])) && html[i] != '=' &&
             html[i] != '>' && html[i] != '/')
        ++i;
      std::string name = lowerAscii(html.substr(start, i - start));
      if (name.empty()) {
        ++i;
        continue;
      }
      skipSpace();
      std::string value;
      if (i < html.size() && html[i] == '=') {
        ++i;
        skipSpace();
        value = readAttr();
      } else {
        value = name;
      }
      node->attrs[name] = value;
      if (name == "id") node->id = value;
      if (name == "class") node->setClassName(value);
      if (name == "value" && (node->tag == "input" || node->tag == "select")) node->inputValue = value;
      if (node->tag == "option" && name == "value") { /* selection value resolved later */ }
    }
    return false;
  }
  bool skipMisc() {
    if (at("<!--")) {
      size_t end = html.find("-->", i + 4);
      i = end == std::string::npos ? html.size() : end + 3;
      return true;
    }
    if (at("<?")) {
      size_t end = html.find("?>", i + 2);
      i = end == std::string::npos ? html.size() : end + 2;
      return true;
    }
    if (at("<!")) {
      size_t end = html.find('>', i);
      i = end == std::string::npos ? html.size() : end + 1;
      return true;
    }
    return false;
  }
  void parseChildren(Node* parent, const std::string& stop) {
    while (!eof()) {
      if (!stop.empty() && at(("</" + stop).c_str())) {
        size_t end = html.find('>', i);
        i = end == std::string::npos ? html.size() : end + 1;
        return;
      }
      if (skipMisc()) continue;
      if (html[i] == '<' && i + 1 < html.size() && html[i + 1] == '/') {
        size_t end = html.find('>', i);
        std::string name = html.substr(i + 2, (end == std::string::npos ? html.size() : end) - i - 2);
        i = end == std::string::npos ? html.size() : end + 1;
        if (lowerAscii(trim(name)) == parent->tag) return;
        continue;
      }
      if (html[i] == '<') {
        parseElement(parent);
        continue;
      }
      size_t start = i;
      while (i < html.size() && html[i] != '<') ++i;
      std::string text = html.substr(start, i - start);
      if (parent->tag != "script" && parent->tag != "style") decodeEntities(text);
      if (!text.empty()) parent->appendChild(doc.createText(text));
    }
  }
  void parseElement(Node* parent) {
    ++i;
    std::string tag = readTagName();
    if (tag.empty()) {
      size_t start = i;
      while (i < html.size() && html[i] != '>') ++i;
      if (i < html.size()) ++i;
      parent->appendChild(doc.createText("<" + html.substr(start, i - start)));
      return;
    }
    Node* node = doc.createElement(tag);
    bool selfClose = readAttrs(node);
    if (!selfClose && !isVoidTag(tag)) {
      if (tag == "script" || tag == "style") {
        size_t end = html.find("</" + tag, i);
        if (end == std::string::npos) end = html.size();
        std::string content = html.substr(i, end - i);
        if (!content.empty()) node->appendChild(doc.createText(content));
        size_t close = html.find('>', end);
        i = close == std::string::npos ? html.size() : close + 1;
      } else {
        // <option> may omit a closing tag; treat a new <option> as implicit end.
        parseChildren(node, tag);
      }
    }
    parent->appendChild(node);
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------
bool Node::hasClass(const std::string& c) const {
  for (const std::string& k : classes)
    if (k == c) return true;
  return false;
}

void Node::addClass(const std::string& c) {
  if (!hasClass(c)) classes.push_back(c);
}

void Node::removeClass(const std::string& c) {
  for (size_t i = 0; i < classes.size(); ++i) {
    if (classes[i] == c) {
      classes.erase(classes.begin() + static_cast<long>(i));
      return;
    }
  }
}

std::string Node::className() const {
  std::string out;
  for (size_t i = 0; i < classes.size(); ++i) {
    if (i) out.push_back(' ');
    out += classes[i];
  }
  return out;
}

void Node::setClassName(const std::string& value) {
  classes.clear();
  std::string cur;
  for (char c : value) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) classes.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) classes.push_back(cur);
}

std::string Node::textContent() const {
  std::string out;
  std::function<void(const Node*)> walk = [&](const Node* n) {
    if (n->type == NodeType::Text) {
      out += n->text;
      return;
    }
    if (n->tag == "br") {
      out.push_back('\n');
      return;
    }
    if (n->tag == "input" || n->tag == "textarea") {
      if (!n->inputValue.empty()) out += n->inputValue;
      return;
    }
    for (const Node* c : n->children) walk(c);
  };
  walk(this);
  return out;
}

void Node::appendChild(Node* child) {
  if (!child || child == this) return;
  if (child->parent) child->parent->removeChild(child);
  child->parent = this;
  children.push_back(child);
}

void Node::insertBefore(Node* child, Node* ref) {
  if (!child || child == this) return;
  if (child->parent) child->parent->removeChild(child);
  child->parent = this;
  if (!ref) {
    children.push_back(child);
    return;
  }
  for (size_t i = 0; i < children.size(); ++i) {
    if (children[i] == ref) {
      children.insert(children.begin() + static_cast<long>(i), child);
      return;
    }
  }
  children.push_back(child);
}

void Node::removeChild(Node* child) {
  for (size_t i = 0; i < children.size(); ++i) {
    if (children[i] == child) {
      children[i]->parent = nullptr;
      children.erase(children.begin() + static_cast<long>(i));
      return;
    }
  }
}

Node* Node::nextSiblingNode() const {
  if (!parent) return nullptr;
  bool found = false;
  for (Node* c : parent->children) {
    if (found) return c;
    if (c == this) found = true;
  }
  return nullptr;
}

int Node::elementIndexAmongSiblings() const {
  if (!parent) return 0;
  int idx = 0;
  for (Node* c : parent->children) {
    if (c->type != NodeType::Element) continue;
    if (c == this) return idx;
    ++idx;
  }
  return idx;
}

int Node::elementCountAmongSiblings() const {
  if (!parent) return 1;
  int count = 0;
  for (Node* c : parent->children)
    if (c->type == NodeType::Element) ++count;
  return count;
}

void Node::setInnerHTML(Document& doc, const std::string& html) {
  children.clear();
  std::vector<Node*> nodes = parseFragment(doc, html);
  for (Node* n : nodes) {
    n->parent = this;
    children.push_back(n);
  }
}

void Node::setTextContent(Document& doc, const std::string& value) {
  children.clear();
  Node* text = doc.createText(value);
  text->parent = this;
  children.push_back(text);
}

Node* Node::deepClone(Document& doc) {
  Node* copy = type == NodeType::Text ? doc.createText(text) : doc.createElement(tag);
  copy->attrs = attrs;
  copy->id = id;
  copy->classes = classes;
  copy->inlineStyle = inlineStyle;
  copy->inputValue = inputValue;
  for (Node* c : children) {
    Node* childCopy = c->deepClone(doc);
    childCopy->parent = copy;
    copy->children.push_back(childCopy);
  }
  return copy;
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------
Document::Document() {
  auto html = std::make_unique<Node>();
  html->type = NodeType::Element;
  html->tag = "html";
  auto headNode = std::make_unique<Node>();
  headNode->type = NodeType::Element;
  headNode->tag = "head";
  auto bodyNode = std::make_unique<Node>();
  bodyNode->type = NodeType::Element;
  bodyNode->tag = "body";
  Node* htmlPtr = html.get();
  Node* headPtr = headNode.get();
  Node* bodyPtr = bodyNode.get();
  headPtr->parent = htmlPtr;
  bodyPtr->parent = htmlPtr;
  htmlPtr->children.push_back(headPtr);
  htmlPtr->children.push_back(bodyPtr);
  root = htmlPtr;
  head = headPtr;
  body = bodyPtr;
  pool_.push_back(std::move(html));
  pool_.push_back(std::move(headNode));
  pool_.push_back(std::move(bodyNode));
}

Node* Document::byId(const std::string& id) const {
  auto it = ids_.find(id);
  return it == ids_.end() ? nullptr : it->second;
}

void Document::reindex() {
  ids_.clear();
  allElements.clear();
  std::function<void(Node*)> walk = [&](Node* n) {
    if (n->type == NodeType::Element) {
      allElements.push_back(n);
      if (!n->id.empty()) ids_[n->id] = n;
    }
    for (Node* c : n->children) walk(c);
  };
  walk(root);
}

Node* Document::createElement(const std::string& tag) {
  auto node = std::make_unique<Node>();
  node->type = NodeType::Element;
  node->tag = lowerAscii(tag);
  Node* ptr = node.get();
  pool_.push_back(std::move(node));
  return ptr;
}

Node* Document::createText(const std::string& text) {
  auto node = std::make_unique<Node>();
  node->type = NodeType::Text;
  node->tag = "#text";
  node->text = text;
  Node* ptr = node.get();
  pool_.push_back(std::move(node));
  return ptr;
}

std::vector<Node*> parseFragment(Document& doc, const std::string& html) {
  std::vector<Node*> out;
  Node* holder = doc.createText("");
  Parser parser(doc, html);
  parser.parseChildren(holder, "");
  for (Node* child : holder->children) {
    child->parent = nullptr;
    out.push_back(child);
  }
  holder->children.clear();
  return out;
}

bool parseDocument(Document& doc, const std::string& html, std::string& cssOut, std::string& error) {
  size_t styleStart = html.find("<style");
  if (styleStart != std::string::npos) {
    size_t open = html.find('>', styleStart);
    size_t close = html.find("</style", open);
    if (open != std::string::npos && close != std::string::npos && close > open) {
      cssOut = html.substr(open + 1, close - open - 1);
    }
  }
  size_t bodyStart = html.find("<body");
  if (bodyStart == std::string::npos) {
    error = "no body element";
    return false;
  }
  size_t bodyOpen = html.find('>', bodyStart);
  if (bodyOpen == std::string::npos) {
    error = "malformed body tag";
    return false;
  }
  size_t bodyEnd = html.rfind("</body>");
  if (bodyEnd == std::string::npos || bodyEnd < bodyOpen) bodyEnd = html.size();
  std::string bodyContent = html.substr(bodyOpen + 1, bodyEnd - bodyOpen - 1);
  std::string bodyAttrs = html.substr(bodyStart + 5, bodyOpen - bodyStart - 5);
  Parser attrParser(doc, bodyAttrs);
  attrParser.readAttrs(doc.body);
  Parser parser(doc, bodyContent);
  parser.parseChildren(doc.body, "body");
  doc.reindex();
  return true;
}

std::string serializeChildren(const Node* node) {
  std::string out;
  std::function<void(const Node*)> walk = [&](const Node* n) {
    if (n->type == NodeType::Text) {
      std::string esc;
      for (char c : n->text) {
        if (c == '&') esc += "&amp;";
        else if (c == '<') esc += "&lt;";
        else if (c == '>') esc += "&gt;";
        else esc.push_back(c);
      }
      out += esc;
      return;
    }
    out += "<" + n->tag;
    for (const auto& kv : n->attrs) out += " " + kv.first + "=\"" + kv.second + "\"";
    out += ">";
    if (n->tag == "input" || n->tag == "br" || n->tag == "img" || n->tag == "meta") return;
    for (const Node* c : n->children) walk(c);
    out += "</" + n->tag + ">";
  };
  for (const Node* c : node->children) walk(c);
  return out;
}

}  // namespace hta
