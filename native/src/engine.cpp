// The native HTA host engine.
//
// Spaceship.hta is a self-contained document: markup, stylesheet, game data and
// the entire game script in one file. This engine parses that document, runs
// the original script unmodified on an embedded ES5 interpreter, and renders
// the resulting live DOM with its own layout and paint engines. There is no
// browser, no mshta, no WebView and no external JavaScript file.
#include "engine.h"

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "mujs.h"
}

#include "bridge.h"
#include "canvas.h"
#include "css.h"
#include "dom.h"
#include "htautil.h"
#include "layout.h"
#include "paint.h"
#include "style.h"

namespace hta {
namespace {

// The reference document hardcodes a 1440x900 client area via resizeTo on
// load; the native window uses the same default so first paint matches.
const int kDefaultWidth = 1440;
const int kDefaultHeight = 900;

std::string jsString(js_State* J, int idx) {
  if (js_isundefined(J, idx)) return "undefined";
  if (js_isnull(J, idx)) return "null";
  return js_tostring(J, idx);
}

}  // namespace

struct Engine::Impl : Host {
  EngineOptions options;
  Document doc;
  Canvas canvas;
  std::unique_ptr<StyleSheet> sheet;
  StyleEngine styles;
  LayoutEngine layout;
  Painter painter;
  js_State* J = nullptr;

  bool loaded = false;
  bool dirty = true;
  bool needRepaint = true;
  bool scriptStarted = false;
  double nowMs_ = 0;
  double startMs_ = 0;
  int width = kDefaultWidth;
  int height = kDefaultHeight;
  bool confirmDefault = true;
  std::string lastErrorText;
  std::string scriptSource;

  // Map of images referenced by URL (the HTA embeds them as data: URIs; the
  // platform layer supplies decoders).
  std::map<std::string, Image> images;

  // ---- Host -------------------------------------------------------------
  Document& document() override { return doc; }
  StyleEngine& styleEngine() override { return styles; }

  void ensureLayout() override {
    if (!dirty) return;
    dirty = false;
    styles.setStylesheet(sheet.get());
    styles.setViewport(width, height);
    styles.resolve(doc);
    layout.layout(doc, styles, width, height);
    needRepaint = true;
  }

  void markDirty() override {
    dirty = true;
    needRepaint = true;
  }

  void markRepaint() override { needRepaint = true; }

  double nowMs() const override { return nowMs_; }
  int viewportWidth() const override { return width; }
  int viewportHeight() const override { return height; }

  bool confirm(const std::string& message) override {
    (void)message;
    return confirmDefault;
  }

  void alert(const std::string& message) override { consoleLog("[alert] " + message); }

  void consoleLog(const std::string& line) override {
    std::fprintf(stderr, "[console] %s\n", line.c_str());
  }

  std::string readTextFile(const std::string& path) override {
    std::string error;
    return readFileUtf8(path, error);
  }

  bool writeTextFile(const std::string& path, const std::string& content) override {
    std::string error;
    return writeFileUtf8(path, content, error);
  }

  // Dispatches an event the way IE did: build window.event, look up the
  // handler on the target, run it, then bubble up while nobody cancelled.
  bool dispatch(Node* target, const std::string& type, int clientX, int clientY, int button,
                int keyCode, int charCode) override {
    if (!J) return true;
    bool cancelled = false;
    for (Node* node = target; node && !cancelled; node = node->parent) {
      std::string slot = "__handler:on" + type;
      (void)slot;
      // The bridge stores one handler per event type globally, matching the
      // reference document's single-handler-per-event design.
      std::string globalName = "__handler:on" + type;
      js_getglobal(J, globalName.c_str());
      if (!js_iscallable(J, -1)) {
        js_pop(J, 1);
        continue;
      }
      // window.event
      js_newobject(J);
      js_pushstring(J, type.c_str());
      js_setproperty(J, -2, "type");
      js_pushnumber(J, clientX);
      js_setproperty(J, -2, "clientX");
      js_pushnumber(J, clientY);
      js_setproperty(J, -2, "clientY");
      js_pushnumber(J, button);
      js_setproperty(J, -2, "button");
      js_pushnumber(J, keyCode);
      js_setproperty(J, -2, "keyCode");
      js_pushnumber(J, charCode);
      js_setproperty(J, -2, "charCode");
      js_pushboolean(J, 0);
      js_setproperty(J, -2, "cancelBubble");
      js_pushboolean(J, 1);
      js_setproperty(J, -2, "returnValue");
      js_setglobal(J, "event");
      js_newcfunction(
          J,
          [](js_State* Js) {
            js_getglobal(Js, "event");
            js_pushboolean(Js, 1);
            js_setproperty(Js, -2, "cancelBubble");
            js_pushboolean(Js, 0);
            js_setproperty(Js, -2, "returnValue");
            js_pushundefined(Js);
          },
          "preventDefault", 0);
      js_getglobal(J, "event");
      js_setproperty(J, -2, "preventDefault");
      js_pushundefined(J);

      js_getglobal(J, globalName.c_str());
      int top = js_gettop(J);
      js_pushnull(J);  // `this`
      js_getglobal(J, "event");
      js_insert(J, top + 1);
      if (js_pcall(J, 1)) {
        std::string message = jsString(J, -1);
        lastErrorText = "event handler (" + type + "): " + message;
        consoleLog("event handler threw: " + message);
        js_pop(J, 1);
      } else {
        bool returnFalse = js_isboolean(J, -1) && !js_toboolean(J, -1);
        js_pop(J, 1);
        bool bubble = false;
        bool returnValue = true;
        js_getglobal(J, "event");
        if (js_isobject(J, -1)) {
          js_getproperty(J, -1, "cancelBubble");
          bubble = js_toboolean(J, -1) != 0;
          js_pop(J, 1);
          js_getproperty(J, -1, "returnValue");
          returnValue = js_toboolean(J, -1) != 0;
          js_pop(J, 1);
        }
        js_pop(J, 1);
        if (returnFalse || bubble || !returnValue) cancelled = true;
      }
      js_pop(J, 1);
    }
    dirty = true;
    return !cancelled;
  }

  std::vector<Node*> queryAll(const std::string& selector) override {
    std::vector<Node*> out;
    std::vector<Selector> selectors;
    if (!parseSelectorList(selector, selectors)) return out;
    for (Node* node : doc.allElements) {
      if (node == doc.root) continue;
      for (const Selector& sel : selectors) {
        if (StyleEngine::matches(sel, node)) {
          out.push_back(node);
          break;
        }
      }
    }
    return out;
  }

  // ---- script -----------------------------------------------------------
  bool runScript(const std::string& source, std::string& error) {
    if (!J) {
      error = "no script state";
      return false;
    }
    // Compiling and running both allocate, and any allocation can collect and
    // throw. Everything is done inside one protected frame so a failure comes
    // back as a message rather than an abort.
    if (js_ploadstring(J, "<spaceship>", source.c_str())) {
      error = jsString(J, -1);
      js_pop(J, 1);
      return false;
    }
    if (js_pcall(J, 0)) {
      std::string message = jsString(J, -1);
      if (js_isobject(J, -1)) {
        js_getproperty(J, -1, "stack");
        if (js_isstring(J, -1)) message = js_tostring(J, -1);
        js_pop(J, 1);
      }
      js_pop(J, 1);
      error = message;
      return false;
    }
    js_pop(J, 1);
    dirty = true;
    return true;
  }
};

Engine::Engine() : impl_(new Impl()) {}
Engine::~Engine() {
  if (impl_->J) {
    js_freestate(impl_->J);
    impl_->J = nullptr;
  }
}

bool Engine::load(const std::string& htaText, const EngineOptions& options, std::string& error) {
  Impl& e = *impl_;
  e.options = options;
  e.width = options.width > 0 ? options.width : kDefaultWidth;
  e.height = options.height > 0 ? options.height : kDefaultHeight;
  e.canvas.resize(e.width, e.height);

  std::string cssText;
  if (!parseDocument(e.doc, htaText, cssText, error)) return false;
  e.doc.reindex();

  // The document's own stylesheet, exactly as authored.
  e.sheet = std::make_unique<StyleSheet>();
  if (!cssText.empty()) {
    std::string cssError;
    if (!e.sheet->parse(cssText, cssError)) {
      std::fprintf(stderr, "[css] %s\n", cssError.c_str());
    }
  }
  e.styles.setStylesheet(e.sheet.get());

  // Extract the game script verbatim.
  size_t scriptStart = htaText.find("<script");
  while (scriptStart != std::string::npos) {
    size_t open = htaText.find('>', scriptStart);
    size_t close = htaText.find("</script", open);
    if (open == std::string::npos || close == std::string::npos) break;
    std::string body = htaText.substr(open + 1, close - open - 1);
    if (body.find("Spaceship") != std::string::npos || e.scriptSource.empty()) e.scriptSource = body;
    scriptStart = htaText.find("<script", close);
  }

  e.J = js_newstate(nullptr, nullptr, JS_STRICT);
  if (!e.J) {
    error = "could not create the JavaScript state";
    return false;
  }
  js_setreport(e.J, nullptr);
  if (!installBridge(e, e.J, error)) return false;

  Painter::setFontProvider(options.fontProvider);
  Painter::setImageResolver([&e](const std::string& url) -> const Image* {
    auto it = e.images.find(url);
    return it == e.images.end() ? nullptr : &it->second;
  });

  e.loaded = true;
  e.dirty = true;
  return true;
}

void Engine::resize(int width, int height) {
  if (width <= 0 || height <= 0) return;
  impl_->width = width;
  impl_->height = height;
  impl_->canvas.resize(width, height);
  impl_->dirty = true;
  impl_->needRepaint = true;
}

void Engine::tick(double nowMs) {
  Impl& e = *impl_;
  e.nowMs_ = nowMs;
  bridgeTimersFire(e.J, nowMs);
  if (!e.scriptStarted && e.J && !e.scriptSource.empty()) {
    e.scriptStarted = true;
    // The script runs with the document already parsed, which is what the HTA
    // sees too (it is at the end of the <body>).
    std::string error;
    if (!e.runScript(e.scriptSource, error)) {
      e.lastErrorText = error;
      std::fprintf(stderr, "[script] %s\n", error.c_str());
    }
  }
}

void Engine::paint(Canvas& canvas) {
  Impl& e = *impl_;
  if (!e.loaded) return;
  e.ensureLayout();
  if (&canvas != &e.canvas) canvas.resize(e.width, e.height);
  e.painter.paint(canvas, e.doc, e.styles, e.width, e.height);
}

bool Engine::wantsAnimation() const { return false; }

double Engine::nextTimerDelay(double nowMs) const {
  double due = 0;
  if (!bridgeTimersNext(impl_->J, nowMs, due)) return -1;
  return due - nowMs;
}

void Engine::mouseMove(float x, float y) {
  Impl& e = *impl_;
  e.ensureLayout();
  Node* hit = e.layout.hitTest(e.doc, x, y);
  // :hover is applied through the cascade's interaction state.
  std::vector<Node*> chain;
  for (Node* n = hit; n; n = n->parent) chain.push_back(n);
  bool changed = false;
  for (Node* n : e.doc.allElements) {
    bool shouldHover = std::find(chain.begin(), chain.end(), n) != chain.end();
    if (n->hovered != shouldHover) {
      n->hovered = shouldHover;
      changed = true;
    }
  }
  if (changed) e.markDirty();
}

void Engine::mouseDown(float x, float y, int button) {
  Impl& e = *impl_;
  e.ensureLayout();
  Node* hit = e.layout.hitTest(e.doc, x, y);
  if (hit) {
    hit->activeState = true;
    e.markDirty();
  }
  e.dispatch(hit, button == 2 ? "contextmenu" : "mousedown", static_cast<int>(x), static_cast<int>(y),
             button, 0, 0);
}

void Engine::mouseUp(float x, float y, int button) {
  Impl& e = *impl_;
  e.ensureLayout();
  Node* hit = e.layout.hitTest(e.doc, x, y);
  for (Node* n : e.doc.allElements) n->activeState = false;
  bool ok = e.dispatch(hit, "mouseup", static_cast<int>(x), static_cast<int>(y), button, 0, 0);
  if (ok && hit && button != 2) e.dispatch(hit, "click", static_cast<int>(x), static_cast<int>(y), button, 0, 0);
  e.markDirty();
}

void Engine::mouseWheel(float x, float y, float delta) {
  Impl& e = *impl_;
  e.ensureLayout();
  Node* hit = e.layout.hitTest(e.doc, x, y);
  for (Node* n = hit; n; n = n->parent) {
    const Computed& cs = e.styles.style(n);
    if (cs.scrolls()) {
      n->scrollTop += delta;
      if (n->scrollTop < 0) n->scrollTop = 0;
      float maxScroll = n->box.scrollH - n->box.h;
      if (maxScroll < 0) maxScroll = 0;
      if (n->scrollTop > maxScroll) n->scrollTop = maxScroll;
      e.markRepaint();
      return;
    }
  }
}

void Engine::keyDown(int keyCode, int charCode) {
  Impl& e = *impl_;
  e.dispatch(nullptr, "keydown", 0, 0, 0, keyCode, charCode);
}

void Engine::keyUp(int keyCode) {
  Impl& e = *impl_;
  e.dispatch(nullptr, "keyup", 0, 0, 0, keyCode, 0);
}

void Engine::mouseLeave() {
  Impl& e = *impl_;
  bool changed = false;
  for (Node* n : e.doc.allElements)
    if (n->hovered) {
      n->hovered = false;
      changed = true;
    }
  if (changed) e.markDirty();
}

void Engine::setFocus(bool focused) { (void)focused; }

int Engine::elementCount() const { return static_cast<int>(impl_->doc.allElements.size()); }
int Engine::domNodeCount() const { return static_cast<int>(impl_->doc.allElements.size()); }
std::string Engine::lastError() const { return impl_->lastErrorText; }
std::string Engine::dumpText() const { return impl_->doc.body ? impl_->doc.body->textContent() : std::string(); }

bool Engine::runScript(const std::string& script, std::string& error) {
  return impl_->runScript(script, error);
}

std::string readFileUtf8(const std::string& path, std::string& error) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    error = "cannot open " + path;
    return std::string();
  }
  std::string data;
  char buffer[65536];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) data.append(buffer, read);
  std::fclose(file);
  if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
      static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
    data.erase(0, 3);
  }
  return data;
}

bool writeFileUtf8(const std::string& path, const std::string& data, std::string& error) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) {
    error = "cannot write " + path;
    return false;
  }
  std::fwrite(data.data(), 1, data.size(), file);
  std::fclose(file);
  return true;
}

}  // namespace hta
