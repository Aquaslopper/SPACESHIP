// PNG capture. Used by --capture on Windows and by the headless test driver.
#include <cstdio>
#include <vector>

#include "canvas.h"
#include "engine.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace hta {
namespace {

// The canvas stores premultiplied-free 0xAARRGGBB in host byte order.
std::vector<unsigned char> toRgba(const uint32_t* pixels, int width, int height) {
  std::vector<unsigned char> out(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
    uint32_t p = pixels[i];
    unsigned char a = static_cast<unsigned char>((p >> 24) & 0xFF);
    unsigned char r = static_cast<unsigned char>((p >> 16) & 0xFF);
    unsigned char g = static_cast<unsigned char>((p >> 8) & 0xFF);
    unsigned char b = static_cast<unsigned char>(p & 0xFF);
    out[i * 4 + 0] = r;
    out[i * 4 + 1] = g;
    out[i * 4 + 2] = b;
    out[i * 4 + 3] = a;
  }
  return out;
}

}  // namespace

bool writePngRgba(const std::string& path, int width, int height, const uint32_t* pixels) {
  if (width <= 0 || height <= 0 || !pixels) return false;
  std::vector<unsigned char> rgba = toRgba(pixels, width, height);
  return stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4) != 0;
}

bool writePng(const std::string& path, const Canvas& canvas) {
  return writePngRgba(path, canvas.width(), canvas.height(), canvas.data());
}

}  // namespace hta
