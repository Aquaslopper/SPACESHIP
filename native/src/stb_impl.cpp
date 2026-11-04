// Single translation unit that instantiates the vendored stb implementations.
// stb_image is used for the embedded JPEG/PNG art, stb_truetype for glyph
// rasterisation. stb_image_write lives in png.cpp next to its only caller.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
