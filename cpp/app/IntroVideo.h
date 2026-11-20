#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "raylib.h"

// This header is included only by the client entry point.
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg.h"
#undef PL_MPEG_IMPLEMENTATION

class IntroVideo {
 public:
  IntroVideo(const std::filesystem::path& directory, bool audioReady) {
    decoder_ = plm_create_with_filename((directory / "intro.mpg").string().c_str());
    if (!decoder_) { return; }
    plm_set_audio_enabled(decoder_, false);
    const double fps = plm_get_framerate(decoder_);
    const double duration = plm_get_duration(decoder_);
    auto* first = plm_decode_video(decoder_);
    if (!first || !std::isfinite(duration) || duration <= 0.0 || fps <= 0.0) {
      Close();
      return;
    }
    frameStep_ = 1.0 / fps;
    duration_ = duration + frameStep_;
    // PL_MPEG writes RGB but leaves alpha untouched.
    pixels_.assign(static_cast<std::size_t>(first->width) * first->height * 4, 255);
    Convert(first);
    Image image{pixels_.data(), static_cast<int>(first->width), static_cast<int>(first->height),
                1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    texture_ = LoadTextureFromImage(image);
    if (!texture_.id) { Close(); return; }
    SetTextureFilter(texture_, TEXTURE_FILTER_BILINEAR);
    if (audioReady) {
      music_ = LoadMusicStream((directory / "intro.ogg").string().c_str());
      music_.looping = false;
    }
  }

  IntroVideo(const IntroVideo&) = delete;
  IntroVideo& operator=(const IntroVideo&) = delete;
  ~IntroVideo() { Close(); }

  bool Pending() const { return decoder_ != nullptr; }

  void Update(bool splash, bool skip, float gain) {
    if (!Pending() || splash) { return; }
    if (skip) { Close(); return; }
    const double now = GetTime();
    if (start_ < 0.0) {
      start_ = now;
      if (music_.stream.buffer) {
        SetMusicVolume(music_, gain);
        PlayMusicStream(music_);
      }
    }
    const double elapsed = now - start_;
    if (elapsed >= duration_) { Close(); return; }
    const float remaining = static_cast<float>(std::clamp(duration_ - elapsed, 0.0, 1.0));
    opacity_ = remaining * remaining * (3.0f - 2.0f * remaining);
    if (music_.stream.buffer) {
      SetMusicVolume(music_, gain * opacity_);
      UpdateMusicStream(music_);
    }
    plm_frame_t* latest = nullptr;
    while (!plm_has_ended(decoder_) && frameTime_ + frameStep_ <= elapsed) {
      auto* frame = plm_decode_video(decoder_);
      if (!frame) { break; }
      frameTime_ = frame->time;
      latest = frame;
    }
    if (latest) {
      Convert(latest);
      UpdateTexture(texture_, pixels_.data());
    }
  }

  void Draw() const {
    if (!Pending() || start_ < 0.0) { return; }
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    const float tw = static_cast<float>(texture_.width);
    const float th = static_cast<float>(texture_.height);
    const float scale = std::min(width / tw, height / th);
    const Rectangle dest{(width - tw * scale) / 2.0f, (height - th * scale) / 2.0f,
                         tw * scale, th * scale};
    const Color bars = Fade(BLACK, opacity_);
    DrawRectangleRec({0, 0, width, dest.y}, bars);
    DrawRectangleRec({0, dest.y + dest.height, width, dest.y}, bars);
    DrawRectangleRec({0, dest.y, dest.x, dest.height}, bars);
    DrawRectangleRec({dest.x + dest.width, dest.y, dest.x, dest.height}, bars);
    DrawTexturePro(texture_, {0, 0, tw, th}, dest, {0, 0}, 0, Fade(WHITE, opacity_));
  }

  void Close() {
    if (music_.stream.buffer) { UnloadMusicStream(music_); music_ = {}; }
    if (texture_.id) { UnloadTexture(texture_); texture_ = {}; }
    if (decoder_) { plm_destroy(decoder_); decoder_ = nullptr; }
  }

 private:
  void Convert(plm_frame_t* frame) {
    plm_frame_to_rgba(frame, pixels_.data(), static_cast<int>(frame->width * 4));
  }

  plm_t* decoder_ = nullptr;
  Texture2D texture_{};
  Music music_{};
  std::vector<unsigned char> pixels_;
  double start_ = -1.0;
  double duration_ = 0.0;
  double frameStep_ = 0.0;
  double frameTime_ = 0.0;
  float opacity_ = 1.0f;
};
