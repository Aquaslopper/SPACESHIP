// The native HTA host.
//
// Spaceship.hta is a self-contained document: markup, stylesheet, game data and
// the entire game script in one file. This host parses that document, runs the
// original script unmodified on an embedded standards-compliant ES5 engine, and
// renders the resulting live DOM with its own layout/paint engine. No browser,
// no mshta, no WebView, no runtime JavaScript files: one native executable.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "canvas.h"

namespace hta {

struct EngineOptions {
  int width = 1440;
  int height = 940;
  // Directory that receives Spaceship_save*.txt, mirroring the HTA's behaviour
  // of saving beside the document.
  std::string saveDirectory;
  std::string executablePath;
  // Optional: TTF/OTF data used for text. Order matters (regular, bold).
  std::function<bool(const std::string& family, bool bold, bool italic, const uint8_t** data,
                     int* length)>
      fontProvider;
  bool enableAudio = true;
};

class Engine {
 public:
  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Parses and starts the document. Returns false with `error` populated.
  bool load(const std::string& htaText, const EngineOptions& options, std::string& error);

  void resize(int width, int height);
  // Runs due timers and lays out if the DOM changed.
  void tick(double nowMs);
  // Paints into the canvas at the current size.
  void paint(Canvas& canvas);

  bool wantsAnimation() const;
  double nextTimerDelay(double nowMs) const;

  // Input. Coordinates are in client (CSS) pixels.
  void mouseMove(float x, float y);
  void mouseDown(float x, float y, int button);
  void mouseUp(float x, float y, int button);
  void mouseWheel(float x, float y, float delta);
  void keyDown(int keyCode, int charCode);
  void keyUp(int keyCode);
  void mouseLeave();
  // Timing needed for gesture gating of audio.
  void setFocus(bool focused);

  // Diagnostics.
  int elementCount() const;
  int domNodeCount() const;
  std::string lastError() const;
  std::string dumpText() const;
  bool runScript(const std::string& script, std::string& error);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Loads a file as UTF-8 (handles a UTF-8 BOM, UTF-16LE/BE with BOM, and falls
// back to the system code page bytes unchanged).
std::string readFileUtf8(const std::string& path, std::string& error);
bool writeFileUtf8(const std::string& path, const std::string& data, std::string& error);

// PNG capture used by the Windows --capture mode and the test suite.
bool writePng(const std::string& path, const Canvas& canvas);
bool writePngRgba(const std::string& path, int width, int height, const uint32_t* pixels);

}  // namespace hta
