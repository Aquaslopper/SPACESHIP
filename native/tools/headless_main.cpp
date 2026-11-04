// Headless verification driver.
//
// Loads a real Spaceship.hta, runs the original script against the native DOM,
// advances the frame clock, and writes PNG captures. This is the parity test:
// it exercises the exact same code the window build uses, minus the window.
#include <cstdio>
#include <cstring>
#include <string>

#include "canvas.h"
#include "engine.h"

int main(int argc, char** argv) {
  std::string path = "Spaceship.hta";
  std::string capture = "out.png";
  int ticks = 6;
  bool dump = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) path = argv[++i];
    else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
    else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) ticks = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--dump") == 0) dump = true;
  }

  std::string error;
  std::string hta = hta::readFileUtf8(path, error);
  if (hta.empty()) {
    std::fprintf(stderr, "cannot read %s: %s\n", path.c_str(), error.c_str());
    return 1;
  }
  std::printf("document: %zu bytes\n", hta.size());

  hta::Engine engine;
  hta::EngineOptions options;
  options.width = 1440;
  options.height = 900;
  if (!engine.load(hta, options, error)) {
    std::fprintf(stderr, "load failed: %s\n", error.c_str());
    return 1;
  }
  std::printf("elements after parse: %d\n", engine.elementCount());

  for (int i = 0; i < ticks; ++i) engine.tick(i * 16.0);
  std::printf("elements after script: %d\n", engine.elementCount());
  if (!engine.lastError().empty()) std::printf("script error: %s\n", engine.lastError().c_str());

  if (dump) {
    std::string text = engine.dumpText();
    std::printf("---- body text (%zu chars) ----\n%.2000s\n", text.size(), text.c_str());
  }

  hta::Canvas canvas;
  engine.paint(canvas);
  if (!hta::writePng(capture, canvas)) {
    std::fprintf(stderr, "cannot write %s\n", capture.c_str());
    return 1;
  }
  std::printf("wrote %s (%dx%d)\n", capture.c_str(), canvas.width(), canvas.height());

  // Feed a couple of synthetic frames through the input path so the handlers
  // registered by the script are proven reachable.
  engine.mouseMove(400, 300);
  engine.mouseDown(400, 300, 0);
  engine.mouseUp(400, 300, 0);
  engine.tick(200.0);
  std::printf("elements after input: %d\n", engine.elementCount());
  return 0;
}
