/* Compiled as part of raylib, with the SAME flags as rcore/rtextures.
 * The CPU-only JPEG probe cannot detect a broken window/frame loop. */
#include "config.h"
#if defined(EXTERNAL_CONFIG_FLAGS)
#error "Spaceship requires raylib numeric defaults, not custom CMake flag parsing"
#endif
#if SUPPORT_CUSTOM_FRAME_CONTROL
#error "EndDrawing must swap buffers, pace frames and poll input"
#endif
#if SUPPORT_BUSY_WAIT_LOOP
#error "Spaceship must not enable full-frame CPU busy waiting"
#endif
#if !SUPPORT_FILEFORMAT_JPG
#error "The shipped title JPEG must be decodable"
#endif
#if !SUPPORT_FILEFORMAT_PNG
#error "The supplied tutorial cards and Wren exterior must be decodable"
#endif
#if !SUPPORT_MODULE_RTEXTURES || !SUPPORT_MODULE_RTEXT || !SUPPORT_MODULE_RSHAPES
#error "Required drawing modules disabled"
#endif
