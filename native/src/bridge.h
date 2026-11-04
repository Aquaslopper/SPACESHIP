// JavaScript <-> DOM bridge.
//
// The reference script targets Internet Explorer's Trident engine and uses a
// small, quirky API surface: getElementById, createElement, appendChild and
// friends, className/innerHTML/innerText/value/checked/disabled, direct
// style.camelCase assignments, offset*/client*/scroll* metrics, inline
// on<event> attributes, attachEvent, window.event, and stopping an event by
// returning false or setting cancelBubble. All of it is implemented here on
// top of the engine's own DOM, so the original script runs completely
// unmodified.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "dom.h"

namespace hta {

class StyleEngine;

// What the bridge needs from the host. Implemented by Engine::Impl.
class Host {
 public:
  virtual ~Host() = default;
  virtual Document& document() = 0;
  virtual StyleEngine& styleEngine() = 0;
  // Re-runs style resolution + layout if the DOM changed since the last frame.
  virtual void ensureLayout() = 0;
  virtual void markDirty() = 0;
  virtual void markRepaint() = 0;
  // Milliseconds since engine start; the reference script reads no clock of its
  // own except through timers, which are routed through here.
  virtual double nowMs() const = 0;
  virtual int viewportWidth() const = 0;
  virtual int viewportHeight() const = 0;

  // Modal dialogs. The reference uses window.confirm for destructive actions
  // and window.alert for validation complaints.
  virtual bool confirm(const std::string& message) = 0;
  virtual void alert(const std::string& message) = 0;

  // Dispatches a DOM event to the script's handler for `type` on `target`,
  // bubbling to ancestors and stopping early if the handler returns false or
  // sets cancelBubble. Returns true when the event was not cancelled
  // (i.e. the default action may still run), matching IE semantics.
  virtual bool dispatch(Node* target, const std::string& type, int clientX, int clientY,
                        int button, int keyCode, int charCode) = 0;

  // CSS selector query, used by the one querySelectorAll call in the script.
  virtual std::vector<Node*> queryAll(const std::string& selector) = 0;

  virtual void consoleLog(const std::string& line) = 0;
  virtual std::string readTextFile(const std::string& path) = 0;
  virtual bool writeTextFile(const std::string& path, const std::string& content) = 0;
};

struct TimerInfo {
  std::string callbackRef;  // MuJS registry reference to the callback
  double dueMs = 0;
  double intervalMs = 0;
  bool repeating = false;
};

// Installs every browser global the script expects into the MuJS state.
bool installBridge(Host& host, void* jsState, std::string& error);

// Timer helpers shared with the engine's frame pump.
void bridgeTimersClear(void* jsState);
bool bridgeTimersNext(void* jsState, double now, double& dueOut);
void bridgeTimersFire(void* jsState, double now);

}  // namespace hta
