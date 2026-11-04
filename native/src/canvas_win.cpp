// DIB-backed presentation surface (Windows).
#include <windows.h>

#include "canvas.h"

namespace hta {

struct Surface::Impl {
  HDC mem = nullptr;
  HBITMAP bitmap = nullptr;
  HBITMAP old = nullptr;
  void* bits = nullptr;
};

Surface::Surface() = default;

Surface::~Surface() {
  if (impl_) {
    if (impl_->mem) {
      if (impl_->old) SelectObject(impl_->mem, impl_->old);
      DeleteDC(impl_->mem);
    }
    if (impl_->bitmap) DeleteObject(impl_->bitmap);
    delete impl_;
  }
}

bool Surface::ensure(int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (impl_ && w == w_ && h == h_) {
    canvas_.resize(w, h);
    return true;
  }
  if (impl_) {
    if (impl_->mem) {
      if (impl_->old) SelectObject(impl_->mem, impl_->old);
      DeleteDC(impl_->mem);
    }
    if (impl_->bitmap) DeleteObject(impl_->bitmap);
    delete impl_;
    impl_ = nullptr;
  }
  Impl* impl = new Impl();
  HDC screen = GetDC(nullptr);
  impl->mem = CreateCompatibleDC(screen);
  ReleaseDC(nullptr, screen);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = -h;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  impl->bitmap = CreateDIBSection(impl->mem, &info, DIB_RGB_COLORS, &impl->bits, nullptr, 0);
  if (!impl->mem || !impl->bitmap) {
    if (impl->mem) DeleteDC(impl->mem);
    if (impl->bitmap) DeleteObject(impl->bitmap);
    delete impl;
    return false;
  }
  impl->old = static_cast<HBITMAP>(SelectObject(impl->mem, impl->bitmap));
  impl_ = impl;
  w_ = w;
  h_ = h;
  canvas_.resize(w, h);
  return true;
}

void Surface::present(void* hdc, int x, int y, int w, int h) {
  if (!hdc || !impl_ || w <= 0 || h <= 0) return;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = w_;
  info.bmiHeader.biHeight = -h_;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  SetStretchBltMode(static_cast<HDC>(hdc), COLORONCOLOR);
  StretchDIBits(static_cast<HDC>(hdc), x, y, w, h, 0, 0, w_, h_, canvas_.data(), &info, DIB_RGB_COLORS,
                SRCCOPY);
}

}  // namespace hta
