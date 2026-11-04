// Headless presentation surface used by the Linux test build.
#include "canvas.h"

namespace hta {

struct Surface::Impl {};

Surface::Surface() = default;
Surface::~Surface() = default;

bool Surface::ensure(int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  w_ = w;
  h_ = h;
  canvas_.resize(w, h);
  return true;
}

void Surface::present(void*, int, int, int, int) {}

}  // namespace hta
