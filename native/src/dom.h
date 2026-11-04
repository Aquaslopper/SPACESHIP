// A tiny DOM with exactly the object surface the reference game script wants.
//
// Ownership is flat and deliberately never freed: the Document owns every node
// for its whole lifetime, parents hold raw pointers, and the JavaScript bridge
// hands out stable Node* handles. The reference script caches DOM references
// across its own innerHTML rebuilds, so a node it created must stay valid even
// after it stops being attached.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "canvas.h"
#include "css.h"

namespace hta {

enum class NodeType { Element, Text };

struct Document;

struct StyleDecl {
  std::map<std::string, std::string> values;
  void set(const std::string& prop, const std::string& value) { values[prop] = value; }
};

struct Node {
  NodeType type = NodeType::Element;
  Node* parent = nullptr;
  std::vector<Node*> children;

  std::string tag;
  std::map<std::string, std::string> attrs;
  std::string id;
  std::vector<std::string> classes;
  StyleDecl inlineStyle;
  std::string text;        // text nodes only
  std::string inputValue;  // input / select / textarea value

  // Geometry, filled in by the layout engine.
  struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    float padL = 0, padT = 0, padR = 0, padB = 0;
    float bdL = 0, bdT = 0, bdR = 0, bdB = 0;
    float scrollW = 0, scrollH = 0;
    float lineHeight = 0;
    bool laidOut = false;
    bool visible = false;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  } box;

  // Interaction state the stylesheet can select on (:hover, :focus, :active).
  bool hovered = false, focused = false, activeState = false;
  float scrollLeft = 0, scrollTop = 0;

  bool hasClass(const std::string& c) const;
  void addClass(const std::string& c);
  void removeClass(const std::string& c);
  std::string className() const;
  void setClassName(const std::string& value);

  std::string textContent() const;
  void appendChild(Node* child);
  void insertBefore(Node* child, Node* ref);
  void removeChild(Node* child);
  Node* nextSiblingNode() const;
  int elementIndexAmongSiblings() const;
  int elementCountAmongSiblings() const;

  void setInnerHTML(Document& doc, const std::string& html);
  void setTextContent(Document& doc, const std::string& value);
  Node* deepClone(Document& doc);
};

class Document {
 public:
  Document();
  Node* root = nullptr;
  Node* head = nullptr;
  Node* body = nullptr;

  Node* byId(const std::string& id) const;
  void reindex();
  std::vector<Node*> allElements;

  Node* createElement(const std::string& tag);
  Node* createText(const std::string& text);

 private:
  std::map<std::string, Node*> ids_;
  std::vector<std::unique_ptr<Node>> pool_;
};

std::vector<Node*> parseFragment(Document& doc, const std::string& html);
bool parseDocument(Document& doc, const std::string& html, std::string& cssOut, std::string& error);
std::string serializeChildren(const Node* node);

}  // namespace hta
