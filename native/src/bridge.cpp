// JavaScript <-> DOM bridge. See bridge.h for the API surface being emulated.
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "mujs.h"
}

#include "bridge.h"
#include "htautil.h"
#include "layout.h"
#include "style.h"

namespace hta {
namespace {

const char* kElementTag = "HTAElement";
const char* kStyleTag = "HTAStyle";
const char* kNodeListTag = "HTANodeList";
const char* kDocumentTag = "HTADocument";
const char* kWindowTag = "HTAWindow";

// js_try is a C macro (setjmp on a void*); give the C++ side a typed handle.
jmp_buf* trapBuffer(js_State* J) { return static_cast<jmp_buf*>(js_savetry(J)); }
#define HTA_TRY(J) setjmp(*trapBuffer(J))

Host* g_host = nullptr;
std::map<int, TimerInfo> g_timers;
int g_nextTimerId = 1;

void pushNode(js_State* J, Node* node);
int nodeGet(js_State* J, void* p, const char* name);
int nodePut(js_State* J, void* p, const char* name);
int nodeHas(js_State* J, void* p, const char* name);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

Node* self(js_State* J, int idx) {
  if (!js_isuserdata(J, idx, kElementTag)) {
    js_typeerror(J, "Node expected");
    return nullptr;
  }
  return static_cast<Node*>(js_touserdata(J, idx, kElementTag));
}

Node* selfOrNull(js_State* J, int idx) {
  if (!js_isuserdata(J, idx, kElementTag)) return nullptr;
  return static_cast<Node*>(js_touserdata(J, idx, kElementTag));
}

// Element.handler = fn  ->  stored as the global "__handler:<type>". This
// mirrors how the reference document assigns handlers, which is always via
// inline HTML attributes (onclick="foo()") or el.onclick = function.
std::string handlerSlot(const std::string& eventName) {
  return "__handler:" + eventName;
}

void storeHandler(js_State* J, const std::string& eventName, int valueIndex) {
  std::string slot = handlerSlot(eventName);
  if (js_isundefined(J, valueIndex) || js_isnull(J, valueIndex)) {
    js_delglobal(J, slot.c_str());
  } else {
    js_copy(J, valueIndex);
    js_setglobal(J, slot.c_str());
  }
}

std::string attrOr(const Node* n, const char* key) {
  auto it = n->attrs.find(key);
  return it == n->attrs.end() ? std::string() : it->second;
}

// ---------------------------------------------------------------------------
// NodeList
// ---------------------------------------------------------------------------
struct NodeListData {
  std::vector<Node*> items;
};

void nodeListFinalize(js_State* J, void* p) {
  (void)J;
  delete static_cast<NodeListData*>(p);
}

int nodeListGetProp(js_State* J, void* p, const char* name) {
  NodeListData* list = static_cast<NodeListData*>(p);
  if (std::strcmp(name, "length") == 0) {
    js_pushnumber(J, static_cast<double>(list->items.size()));
    return 1;
  }
  char* end = nullptr;
  long index = std::strtol(name, &end, 10);
  if (end != name && *end == '\0' && index >= 0 && static_cast<size_t>(index) < list->items.size()) {
    pushNode(J, list->items[static_cast<size_t>(index)]);
    return 1;
  }
  if (std::strcmp(name, "item") == 0) {
    js_newcfunction(J, [](js_State* J) {
      NodeListData* l = static_cast<NodeListData*>(js_touserdata(J, 0, kNodeListTag));
      double i = js_tonumber(J, 1);
      if (i < 0 || i >= static_cast<double>(l->items.size())) js_pushnull(J);
      else pushNode(J, l->items[static_cast<size_t>(i)]);
    }, "item", 1);
    return 1;
  }
  return 0;
}

void pushNodeList(js_State* J, std::vector<Node*> items) {
  js_getglobal(J, "__nodeListPrototype");
  js_newuserdatax(J, kNodeListTag, new NodeListData{std::move(items)}, nodeListGetProp, nullptr,
                  nullptr, nodeListFinalize);
}

// ---------------------------------------------------------------------------
// Style declarations
// ---------------------------------------------------------------------------
std::string cssNameFor(js_State* J, const std::string& name) {
  (void)J;
  static const struct {
    const char* js;
    const char* css;
  } kAliases[] = {{"cssFloat", "float"},
                  {"styleFloat", "float"},
                  {"backgroundColor", "background-color"},
                  {"fontFamily", "font-family"},
                  {"fontSize", "font-size"},
                  {"fontWeight", "font-weight"},
                  {"fontStyle", "font-style"},
                  {"lineHeight", "line-height"},
                  {"textAlign", "text-align"},
                  {"textTransform", "text-transform"},
                  {"letterSpacing", "letter-spacing"},
                  {"whiteSpace", "white-space"},
                  {"wordWrap", "word-wrap"},
                  {"borderRadius", "border-radius"},
                  {"borderColor", "border-color"},
                  {"borderWidth", "border-width"},
                  {"borderStyle", "border-style"},
                  {"marginLeft", "margin-left"},
                  {"marginRight", "margin-right"},
                  {"marginTop", "margin-top"},
                  {"marginBottom", "margin-bottom"},
                  {"paddingLeft", "padding-left"},
                  {"paddingRight", "padding-right"},
                  {"paddingTop", "padding-top"},
                  {"paddingBottom", "padding-bottom"},
                  {"backgroundImage", "background-image"},
                  {"backgroundSize", "background-size"},
                  {"backgroundPosition", "background-position"},
                  {"backgroundRepeat", "background-repeat"},
                  {"display", "display"},
                  {"visibility", "visibility"},
                  {"width", "width"},
                  {"height", "height"},
                  {"left", "left"},
                  {"top", "top"},
                  {"right", "right"},
                  {"bottom", "bottom"},
                  {"color", "color"},
                  {"opacity", "opacity"},
                  {"overflow", "overflow"},
                  {"cursor", "cursor"}};
  for (const auto& alias : kAliases)
    if (name == alias.js || name == alias.css) return alias.css;
  std::string css;
  for (char c : name) {
    if (c >= 'A' && c <= 'Z') {
      css.push_back('-');
      css.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      css.push_back(c);
    }
  }
  return css;
}

int styleGet(js_State* J, void* p, const char* name) {
  Node* node = static_cast<Node*>(p);
  std::string css = cssNameFor(J, name);
  auto it = node->inlineStyle.values.find(css);
  if (it != node->inlineStyle.values.end()) {
    js_pushstring(J, it->second.c_str());
    return 1;
  }
  js_pushstring(J, "");
  return 1;
}

int stylePut(js_State* J, void* p, const char* name) {
  Node* node = static_cast<Node*>(p);
  if (std::strcmp(name, "cssText") == 0) {
    node->inlineStyle.values.clear();
    parseInlineDeclarations(js_tostring(J, -1), node->inlineStyle);
  } else {
    std::string css = cssNameFor(J, name);
    std::string value = js_tostring(J, -1);
    if (value.empty()) {
      node->inlineStyle.values.erase(css);
    } else {
      node->inlineStyle.set(css, value);
      expandShorthand(css, value, node->inlineStyle);
    }
  }
  g_host->markDirty();
  return 1;
}

void pushStyle(js_State* J, Node* node) {
  js_getglobal(J, "__stylePrototype");
  js_newuserdatax(J, kStyleTag, node, styleGet, stylePut, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// Derived DOM properties
// ---------------------------------------------------------------------------
std::string selectValueText(Node* select) {
  if (!select->inputValue.empty()) return select->inputValue;
  for (Node* child : select->children) {
    if (child->tag != "option") continue;
    std::string value = attrOr(child, "value");
    if (value.empty()) value = child->textContent();
    select->inputValue = value;
    return value;
  }
  return std::string();
}

std::string elementValue(Node* node) {
  if (node->tag == "select") return selectValueText(node);
  if (!node->inputValue.empty()) return node->inputValue;
  return attrOr(node, "value");
}

double boxMetric(Node* node, const std::string& key) {
  g_host->ensureLayout();
  const Node::Box& b = node->box;
  if (key == "offsetWidth" || key == "clientWidth") return b.w;
  if (key == "offsetHeight" || key == "clientHeight") return b.h;
  if (key == "offsetLeft") return b.x;
  if (key == "offsetTop") return b.y;
  if (key == "scrollWidth") return b.scrollW > 0 ? b.scrollW : b.w;
  if (key == "scrollHeight") return b.scrollH > 0 ? b.scrollH : b.h;
  return 0;
}

bool isMetric(const std::string& key) {
  return key == "offsetWidth" || key == "offsetHeight" || key == "offsetLeft" || key == "offsetTop" ||
         key == "clientWidth" || key == "clientHeight" || key == "scrollWidth" ||
         key == "scrollHeight";
}

// ---------------------------------------------------------------------------
// Element property access
// ---------------------------------------------------------------------------
int nodeGet(js_State* J, void* p, const char* name) {
  Node* node = static_cast<Node*>(p);
  std::string key = name;

  if (key == "id") {
    js_pushstring(J, node->id.c_str());
    return 1;
  }
  if (key == "className") {
    js_pushstring(J, node->className().c_str());
    return 1;
  }
  if (key == "tagName" || key == "nodeName") {
    js_pushstring(J, upperAscii(node->tag).c_str());
    return 1;
  }
  if (key == "nodeType") {
    js_pushnumber(J, node->type == NodeType::Text ? 3 : 1);
    return 1;
  }
  if (key == "innerHTML") {
    js_pushstring(J, serializeChildren(node).c_str());
    return 1;
  }
  if (key == "outerHTML") {
    js_pushstring(J, serializeChildren(node->parent ? node->parent : node).c_str());
    return 1;
  }
  if (key == "innerText" || key == "textContent") {
    js_pushstring(J, node->textContent().c_str());
    return 1;
  }
  if (key == "value") {
    js_pushstring(J, elementValue(node).c_str());
    return 1;
  }
  if (key == "checked") {
    js_pushboolean(J, node->attrs.count("checked") != 0);
    return 1;
  }
  if (key == "disabled") {
    js_pushboolean(J, node->attrs.count("disabled") != 0);
    return 1;
  }
  if (key == "style") {
    pushStyle(J, node);
    return 1;
  }
  if (key == "parentNode") {
    if (node->parent && node->parent != g_host->document().root) {
      pushNode(J, node->parent);
    } else {
      js_pushnull(J);
    }
    return 1;
  }
  if (key == "firstChild") {
    if (!node->children.empty()) pushNode(J, node->children.front());
    else js_pushnull(J);
    return 1;
  }
  if (key == "lastChild") {
    if (!node->children.empty()) pushNode(J, node->children.back());
    else js_pushnull(J);
    return 1;
  }
  if (key == "childNodes" || key == "children") {
    pushNodeList(J, node->children);
    return 1;
  }
  if (key == "nextSibling") {
    Node* next = node->nextSiblingNode();
    if (next) pushNode(J, next);
    else js_pushnull(J);
    return 1;
  }
  if (key == "scrollLeft") {
    js_pushnumber(J, node->scrollLeft);
    return 1;
  }
  if (key == "scrollTop") {
    js_pushnumber(J, node->scrollTop);
    return 1;
  }
  if (isMetric(key)) {
    js_pushnumber(J, boxMetric(node, key));
    return 1;
  }
  if (key.size() > 2 && key[0] == 'o' && key[1] == 'n') {
    js_getglobal(J, handlerSlot(key).c_str());
    return 1;
  }
  // Inline element "methods" are resolved from the prototype (see
  // pushNode); anything else falls back to the matching HTML attribute, which
  // is how the reference reads data-id / data-kind / title.
  auto it = node->attrs.find(key);
  if (it != node->attrs.end()) {
    js_pushstring(J, it->second.c_str());
    return 1;
  }
  js_pushundefined(J);
  return 1;
}

int nodePut(js_State* J, void* p, const char* name) {
  Node* node = static_cast<Node*>(p);
  std::string key = name;

  if (key == "innerHTML") {
    node->setInnerHTML(g_host->document(), js_tostring(J, -1));
    g_host->document().reindex();
    g_host->markDirty();
    return 1;
  }
  if (key == "textContent") {
    node->setTextContent(g_host->document(), js_tostring(J, -1));
    g_host->markDirty();
    return 1;
  }
  if (key == "innerText") {
    node->setTextContent(g_host->document(), js_tostring(J, -1));
    g_host->markDirty();
    return 1;
  }
  if (key == "className") {
    node->setClassName(js_tostring(J, -1));
    g_host->markDirty();
    return 1;
  }
  if (key == "id") {
    node->id = js_tostring(J, -1);
    g_host->document().reindex();
    g_host->markDirty();
    return 1;
  }
  if (key == "value") {
    node->inputValue = js_tostring(J, -1);
    return 1;
  }
  if (key == "checked" || key == "disabled") {
    bool on = js_toboolean(J, -1);
    if (on) node->attrs[key] = key;
    else node->attrs.erase(key);
    g_host->markDirty();
    return 1;
  }
  if (key == "scrollLeft") {
    node->scrollLeft = static_cast<float>(js_tonumber(J, -1));
    g_host->markRepaint();
    return 1;
  }
  if (key == "scrollTop") {
    node->scrollTop = static_cast<float>(js_tonumber(J, -1));
    g_host->markRepaint();
    return 1;
  }
  if (key.size() > 2 && key[0] == 'o' && key[1] == 'n') {
    storeHandler(J, key, -1);
    return 1;
  }
  node->attrs[key] = js_tostring(J, -1);
  g_host->markDirty();
  return 1;
}

// IE exposed element properties only through their attribute names; the
// reference relies on both, so `in` must report both.
int nodeHas(js_State* J, void* p, const char* name) {
  Node* node = static_cast<Node*>(p);
  std::string key = name;
  if (node->attrs.count(key)) {
    js_pushboolean(J, 1);
    return 1;
  }
  js_getglobal(J, "__elementPrototype");
  js_pushstring(J, key.c_str());
  int found = js_hasproperty(J, -2, key.c_str());
  js_pop(J, 2);
  js_pushboolean(J, found);
  return 1;
}

void pushNode(js_State* J, Node* node) {
  js_getglobal(J, "__elementPrototype");
  js_newuserdatax(J, kElementTag, node, nodeGet, nodePut, nodeHas, nullptr);
}

// ---------------------------------------------------------------------------
// Element methods
// ---------------------------------------------------------------------------
void method_release(js_State* J) { js_pushundefined(J); }

void m_appendChild(js_State* J) {
  Node* parent = self(J, 0);
  Node* child = selfOrNull(J, 1);
  if (child) {
    parent->appendChild(child);
    g_host->markDirty();
  }
  js_copy(J, 1);
}

void m_removeChild(js_State* J) {
  Node* parent = self(J, 0);
  Node* child = selfOrNull(J, 1);
  if (child) {
    parent->removeChild(child);
    g_host->markDirty();
  }
  js_copy(J, 1);
}

void m_insertBefore(js_State* J) {
  Node* parent = self(J, 0);
  Node* child = selfOrNull(J, 1);
  Node* ref = selfOrNull(J, 2);
  if (child) {
    parent->insertBefore(child, ref);
    g_host->markDirty();
  }
  js_copy(J, 1);
}

void m_replaceChild(js_State* J) {
  Node* parent = self(J, 0);
  Node* fresh = selfOrNull(J, 1);
  Node* old = selfOrNull(J, 2);
  if (fresh && old && old->parent == parent) parent->insertBefore(fresh, old);
  if (old && old->parent == parent) parent->removeChild(old);
  js_copy(J, 1);
}

void m_setAttribute(js_State* J) {
  Node* node = self(J, 0);
  node->attrs[js_tostring(J, 1)] = js_tostring(J, 2);
  g_host->markDirty();
  js_pushundefined(J);
}

void m_getAttribute(js_State* J) {
  Node* node = self(J, 0);
  std::string key = js_tostring(J, 1);
  auto it = node->attrs.find(key);
  if (it == node->attrs.end()) js_pushnull(J);
  else js_pushstring(J, it->second.c_str());
}

void m_removeAttribute(js_State* J) {
  Node* node = self(J, 0);
  node->attrs.erase(js_tostring(J, 1));
  g_host->markDirty();
  js_pushundefined(J);
}

void m_hasAttribute(js_State* J) {
  Node* node = self(J, 0);
  js_pushboolean(J, node->attrs.count(js_tostring(J, 1)) != 0);
}

void m_focus(js_State* J) {
  Node* node = self(J, 0);
  node->focused = true;
  js_pushundefined(J);
}

void m_blur(js_State* J) {
  Node* node = self(J, 0);
  node->focused = false;
  js_pushundefined(J);
}

void m_click(js_State* J) {
  Node* node = self(J, 0);
  g_host->dispatch(node, "click", 0, 0, 0, 0, 0);
  js_pushundefined(J);
}

void m_getBoundingClientRect(js_State* J) {
  Node* node = self(J, 0);
  g_host->ensureLayout();
  js_newobject(J);
  js_pushnumber(J, node->box.x);
  js_setproperty(J, -2, "left");
  js_pushnumber(J, node->box.y);
  js_setproperty(J, -2, "top");
  js_pushnumber(J, node->box.x + node->box.w);
  js_setproperty(J, -2, "right");
  js_pushnumber(J, node->box.y + node->box.h);
  js_setproperty(J, -2, "bottom");
  js_pushnumber(J, node->box.w);
  js_setproperty(J, -2, "width");
  js_pushnumber(J, node->box.h);
  js_setproperty(J, -2, "height");
}

void m_cloneNode(js_State* J) {
  Node* node = self(J, 0);
  pushNode(J, node->deepClone(g_host->document()));
}

void m_hasChildNodes(js_State* J) {
  Node* node = self(J, 0);
  js_pushboolean(J, !node->children.empty());
}

// attachEvent("onclick", fn) and addEventListener("click", fn) both just park
// the handler where dispatch will find it.
void bindListener(js_State* J, int nameIndex, int fnIndex) {
  std::string type = js_tostring(J, nameIndex);
  if (type.size() > 2 && type[0] == 'o' && type[1] == 'n') type = type.substr(2);
  storeHandler(J, "on" + type, fnIndex);
}

void m_attachEvent(js_State* J) {
  bindListener(J, 1, 2);
  js_pushboolean(J, 1);
}

void m_addEventListener(js_State* J) {
  bindListener(J, 1, 2);
  js_pushundefined(J);
}

void m_detachEvent(js_State* J) { js_pushundefined(J); }
void m_removeEventListener(js_State* J) { js_pushundefined(J); }

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------
void d_getElementById(js_State* J) {
  Document& doc = g_host->document();
  Node* node = doc.byId(js_tostring(J, 0));
  if (node) pushNode(J, node);
  else js_pushnull(J);
}

void d_createElement(js_State* J) {
  pushNode(J, g_host->document().createElement(js_tostring(J, 0)));
}

void d_createTextNode(js_State* J) {
  pushNode(J, g_host->document().createText(js_tostring(J, 0)));
}

void d_querySelectorAll(js_State* J) {
  pushNodeList(J, g_host->queryAll(js_tostring(J, 0)));
}

void d_querySelector(js_State* J) {
  std::vector<Node*> found = g_host->queryAll(js_tostring(J, 0));
  if (found.empty()) js_pushnull(J);
  else pushNode(J, found.front());
}

void d_write(js_State* J) {
  // document.write is only used before the document finishes loading in the
  // reference; it appends to body.
  Document& doc = g_host->document();
  std::vector<Node*> nodes = parseFragment(doc, js_tostring(J, 0));
  for (Node* node : nodes) doc.body->appendChild(node);
  doc.reindex();
  g_host->markDirty();
  js_pushundefined(J);
}

int documentGet(js_State* J, void* p, const char* name) {
  (void)p;
  std::string key = name;
  Document& doc = g_host->document();
  if (key == "body") { pushNode(J, doc.body); return 1; }
  if (key == "documentElement") { pushNode(J, doc.root); return 1; }
  if (key == "getElementById") { js_newcfunction(J, d_getElementById, "getElementById", 1); return 1; }
  if (key == "createElement" || key == "createElementNS") {
    js_newcfunction(J, d_createElement, "createElement", 1);
    return 1;
  }
  if (key == "createTextNode") { js_newcfunction(J, d_createTextNode, "createTextNode", 1); return 1; }
  if (key == "querySelectorAll") { js_newcfunction(J, d_querySelectorAll, "querySelectorAll", 1); return 1; }
  if (key == "querySelector") { js_newcfunction(J, d_querySelector, "querySelector", 1); return 1; }
  if (key == "write") { js_newcfunction(J, d_write, "write", 1); return 1; }
  if (key == "addEventListener" || key == "attachEvent") {
    js_newcfunction(J, m_addEventListener, "addEventListener", 2);
    return 1;
  }
  if (key.size() > 2 && key[0] == 'o' && key[1] == 'n') {
    js_getglobal(J, handlerSlot(key).c_str());
    return 1;
  }
  js_pushundefined(J);
  return 1;
}

int documentPut(js_State* J, void* p, const char* name) {
  (void)p;
  std::string key = name;
  if (key.size() > 2 && key[0] == 'o' && key[1] == 'n') {
    storeHandler(J, key, -1);
    return 1;
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
double argNumber(js_State* J, int idx) { return js_isundefined(J, idx) ? 0 : js_tonumber(J, idx); }

void pushTimer(js_State* J, bool repeating, int defaultDelay) {
  if (!js_iscallable(J, 0)) {
    js_pushnumber(J, 0);
    return;
  }
  int id = g_nextTimerId++;
  TimerInfo info;
  info.callbackRef = std::string(js_ref(J));
  info.repeating = repeating;
  double delay = argNumber(J, 1);
  if (delay < 0) delay = 0;
  if (repeating && delay < 1) delay = defaultDelay;
  info.intervalMs = delay;
  info.dueMs = g_host->nowMs() + delay;
  g_timers[id] = info;
  js_pushnumber(J, id);
}

void t_setTimeout(js_State* J) { pushTimer(J, false, 0); }
void t_setInterval(js_State* J) { pushTimer(J, true, 55); }

void t_clearTimer(js_State* J) {
  int id = static_cast<int>(argNumber(J, 0));
  auto it = g_timers.find(id);
  if (it != g_timers.end()) {
    // The registry reference is released when the state shuts down; MuJS has
    // no cheap unref for a live stack, and timers are few.
    g_timers.erase(it);
  }
  js_pushundefined(J);
}

// ---------------------------------------------------------------------------
// window
// ---------------------------------------------------------------------------
void w_alert(js_State* J) {
  g_host->alert(js_tostring(J, 0));
  js_pushundefined(J);
}

void w_confirm(js_State* J) { js_pushboolean(J, g_host->confirm(js_tostring(J, 0))); }

void w_prompt(js_State* J) {
  g_host->alert(js_isdefined(J, 0) ? js_tostring(J, 0) : std::string());
  js_pushstring(J, js_isdefined(J, 1) ? js_tostring(J, 1) : "");
}

void w_noop(js_State* J) { js_pushundefined(J); }

void w_close(js_State* J) {
  js_getglobal(J, "__windowClosed");
  js_pushboolean(J, 1);
  js_setglobal(J, "__windowClosed");
  js_pushundefined(J);
}

void w_setInterval(js_State* J) { t_setInterval(J); }
void w_setTimeout(js_State* J) { t_setTimeout(J); }

int windowGet(js_State* J, void* p, const char* name) {
  (void)p;
  std::string key = name;
  if (key == "event") {
    js_getglobal(J, "event");
    return 1;
  }
  if (key == "innerWidth" || key == "outerWidth" || key == "clientWidth") {
    js_pushnumber(J, g_host->viewportWidth());
    return 1;
  }
  if (key == "innerHeight" || key == "outerHeight" || key == "clientHeight") {
    js_pushnumber(J, g_host->viewportHeight());
    return 1;
  }
  if (key == "alert") { js_newcfunction(J, w_alert, "alert", 1); return 1; }
  if (key == "confirm") { js_newcfunction(J, w_confirm, "confirm", 1); return 1; }
  if (key == "prompt") { js_newcfunction(J, w_prompt, "prompt", 2); return 1; }
  if (key == "close") { js_newcfunction(J, w_close, "close", 0); return 1; }
  if (key == "focus" || key == "blur" || key == "print" || key == "open" ||
      key == "moveTo" || key == "resizeTo" || key == "scrollTo" || key == "scrollBy") {
    js_newcfunction(J, w_noop, key.c_str(), 0);
    return 1;
  }
  if (key == "setInterval") { js_newcfunction(J, w_setInterval, "setInterval", 2); return 1; }
  if (key == "setTimeout") { js_newcfunction(J, w_setTimeout, "setTimeout", 2); return 1; }
  if (key == "clearInterval") { js_newcfunction(J, t_clearTimer, "clearInterval", 1); return 1; }
  if (key == "clearTimeout") { js_newcfunction(J, t_clearTimer, "clearTimeout", 1); return 1; }
  if (key == "location") {
    js_newobject(J);
    js_pushstring(J, "file:");
    js_setproperty(J, -2, "protocol");
    js_pushstring(J, "/Spaceship.hta");
    js_setproperty(J, -2, "pathname");
    js_pushstring(J, "");
    js_setproperty(J, -2, "search");
    js_pushstring(J, "file:///Spaceship.hta");
    js_setproperty(J, -2, "href");
    return 1;
  }
  if (key == "screen") {
    js_newobject(J);
    js_pushnumber(J, 1920);
    js_setproperty(J, -2, "availWidth");
    js_pushnumber(J, 1040);
    js_setproperty(J, -2, "availHeight");
    js_pushnumber(J, 1920);
    js_setproperty(J, -2, "width");
    js_pushnumber(J, 1080);
    js_setproperty(J, -2, "height");
    return 1;
  }
  // Anything else resolves against the real global object so that
  // window.SOMETHING assignments and reads keep working.
  js_getglobal(J, name);
  return 1;
}

int windowPut(js_State* J, void* p, const char* name) {
  (void)p;
  js_copy(J, -1);
  js_setglobal(J, name);
  return 1;
}

// ---------------------------------------------------------------------------
// console
// ---------------------------------------------------------------------------
void console_log(js_State* J) {
  std::string line;
  int n = js_gettop(J);
  for (int i = 1; i <= n; ++i) {
    if (i > 1) line.push_back(' ');
    line += js_isstring(J, i) || js_isnumber(J, i) ? js_tostring(J, i) : js_tostring(J, i);
  }
  g_host->consoleLog(line);
  js_pushundefined(J);
}

// ---------------------------------------------------------------------------
// Storage. The HTA writes saves to disk through ActiveXObject("Scripting.FileSystemObject");
// the browser branch of the script uses localStorage, so both must exist for
// the script to run without branching.
// ---------------------------------------------------------------------------
int storageGet(js_State* J, void* p, const char* name) {
  (void)p;
  js_getglobal(J, "localStorage");
  js_pushstring(J, name);
  js_getproperty(J, -2, name);
  return 1;
}

// ---------------------------------------------------------------------------
// Global installation
// ---------------------------------------------------------------------------
// js_setglobal pops, so keep a copy on the stack for the caller to decorate.
void pushPrototype(js_State* J, const char* globalName) {
  js_newobject(J);
  js_copy(J, -1);
  js_setglobal(J, globalName);
}

void defineMethod(js_State* J, const char* prototype, const char* name, js_CFunction fn,
                  int length) {
  js_getglobal(J, prototype);
  js_newcfunction(J, fn, name, length);
  js_defproperty(J, -2, name, JS_DONTENUM);
  js_pop(J, 1);
}

}  // namespace

bool installBridge(Host& host, void* jsState, std::string& error) {
  if (!jsState) {
    error = "no JavaScript state";
    return false;
  }
  g_host = &host;
  js_State* J = static_cast<js_State*>(jsState);

  // Everything below allocates JavaScript objects, and any allocation may
  // trigger a garbage collection, which may throw. Without a setjmp frame
  // MuJS treats that as an unhandled panic and aborts, so the whole setup
  // runs protected and turns a throw into an ordinary error string.
  if (HTA_TRY(J)) {
    error = js_isobject(J, -1) ? js_tostring(J, -1) : "out of memory while creating the DOM bridge";
    js_pop(J, 1);
    return false;
  }

  pushPrototype(J, "__elementPrototype");
  pushPrototype(J, "__stylePrototype");
  pushPrototype(J, "__nodeListPrototype");

  defineMethod(J, "__elementPrototype", "appendChild", m_appendChild, 1);
  defineMethod(J, "__elementPrototype", "removeChild", m_removeChild, 1);
  defineMethod(J, "__elementPrototype", "insertBefore", m_insertBefore, 2);
  defineMethod(J, "__elementPrototype", "replaceChild", m_replaceChild, 2);
  defineMethod(J, "__elementPrototype", "setAttribute", m_setAttribute, 2);
  defineMethod(J, "__elementPrototype", "getAttribute", m_getAttribute, 1);
  defineMethod(J, "__elementPrototype", "removeAttribute", m_removeAttribute, 1);
  defineMethod(J, "__elementPrototype", "hasAttribute", m_hasAttribute, 1);
  defineMethod(J, "__elementPrototype", "focus", m_focus, 0);
  defineMethod(J, "__elementPrototype", "blur", m_blur, 0);
  defineMethod(J, "__elementPrototype", "click", m_click, 0);
  defineMethod(J, "__elementPrototype", "attachEvent", m_attachEvent, 2);
  defineMethod(J, "__elementPrototype", "detachEvent", m_detachEvent, 2);
  defineMethod(J, "__elementPrototype", "addEventListener", m_addEventListener, 2);
  defineMethod(J, "__elementPrototype", "removeEventListener", m_removeEventListener, 2);
  defineMethod(J, "__elementPrototype", "cloneNode", m_cloneNode, 1);
  defineMethod(J, "__elementPrototype", "hasChildNodes", m_hasChildNodes, 0);
  defineMethod(J, "__elementPrototype", "getBoundingClientRect", m_getBoundingClientRect, 0);

  // pushPrototype leaves the new object on the stack, so decorate it in place.
  pushPrototype(J, "__stylePrototype");
  js_newcfunction(J, m_setAttribute, "setProperty", 2);
  js_defproperty(J, -2, "setProperty", JS_DONTENUM);
  js_newcfunction(J, m_removeAttribute, "removeProperty", 1);
  js_defproperty(J, -2, "removeProperty", JS_DONTENUM);
  js_pop(J, 1);

  js_newuserdatax(J, kDocumentTag, &host, documentGet, documentPut, nullptr, nullptr);
  js_setglobal(J, "document");

  js_newuserdatax(J, kWindowTag, &host, windowGet, windowPut, nullptr, nullptr);
  js_setglobal(J, "window");
  js_copy(J, -1);
  js_setglobal(J, "self");
  js_copy(J, -1);
  js_setglobal(J, "top");
  js_copy(J, -1);
  js_setglobal(J, "parent");
  js_pop(J, 1);

  js_newcfunction(J, t_setTimeout, "setTimeout", 2);
  js_setglobal(J, "setTimeout");
  js_newcfunction(J, t_setInterval, "setInterval", 2);
  js_setglobal(J, "setInterval");
  js_newcfunction(J, t_clearTimer, "clearTimeout", 1);
  js_setglobal(J, "clearTimeout");
  js_newcfunction(J, t_clearTimer, "clearInterval", 1);
  js_setglobal(J, "clearInterval");

  js_newcfunction(J, w_alert, "alert", 1);
  js_setglobal(J, "alert");
  js_newcfunction(J, w_confirm, "confirm", 1);
  js_setglobal(J, "confirm");

  js_newobject(J);
  js_newcfunction(J, console_log, "log", 1);
  js_setproperty(J, -2, "log");
  js_newcfunction(J, console_log, "warn", 1);
  js_setproperty(J, -2, "warn");
  js_newcfunction(J, console_log, "error", 1);
  js_setproperty(J, -2, "error");
  js_setglobal(J, "console");

  // window.event is replaced with a fresh object before every dispatch; this
  // is the initial (empty) one.
  js_pushnull(J);
  js_setglobal(J, "event");

  js_pushboolean(J, 0);
  js_setglobal(J, "__windowClosed");

  js_endtry(J);
  return true;
}

void bridgeTimersClear(void* jsState) {
  (void)jsState;
  g_timers.clear();
}

bool bridgeTimersNext(void* jsState, double now, double& dueOut) {
  (void)jsState;
  (void)now;
  bool any = false;
  double best = 0;
  for (const auto& kv : g_timers) {
    if (!any || kv.second.dueMs < best) {
      best = kv.second.dueMs;
      any = true;
    }
  }
  dueOut = best;
  return any;
}

void bridgeTimersFire(void* jsState, double now) {
  js_State* J = static_cast<js_State*>(jsState);
  if (!J) return;
  std::vector<int> due;
  for (const auto& kv : g_timers)
    if (kv.second.dueMs <= now) due.push_back(kv.first);
  for (int id : due) {
    auto it = g_timers.find(id);
    if (it == g_timers.end()) continue;
    TimerInfo info = it->second;
    if (info.repeating) {
      it->second.dueMs = now + info.intervalMs;
    } else {
      g_timers.erase(it);
    }
    js_getregistry(J, info.callbackRef.c_str());
    if (js_iscallable(J, -1)) {
      if (js_pcall(J, 0)) {
        std::string message = js_tostring(J, -1);
        g_host->consoleLog("timer callback threw: " + message);
      }
      js_pop(J, 1);
    } else {
      js_pop(J, 1);
    }
  }
}

}  // namespace hta
