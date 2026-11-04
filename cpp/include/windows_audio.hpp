#pragma once
// Include after windows.h. Playback never calls the simulation or uses game timers.
#include <mmsystem.h>

#include <algorithm>

#include "runtime_files.hpp"

namespace spaceship {
class WavePlayer {
 public:
  WavePlayer() = default;
  WavePlayer(const WavePlayer&) = delete;
  WavePlayer& operator=(const WavePlayer&) = delete;
  ~WavePlayer() { stop(); }
  void stop() {
    if (!handle_) return;
    waveOutReset(handle_);
    waveOutUnprepareHeader(handle_, &header_, sizeof(header_));
    waveOutClose(handle_);
    handle_ = nullptr;
    header_ = {};
    clip_ = {};
  }
  bool playing() const { return handle_ != nullptr; }
  void volume(double level) {
    if (!handle_) return;
    const auto v = static_cast<DWORD>(std::clamp(level, 0.0, 1.0) * 65535);
    waveOutSetVolume(handle_, v | (v << 16));
  }
  bool play(const std::filesystem::path& path, bool loop, double level) {
    stop();
    clip_ = readWave(path);
    if (clip_.samples.empty()) return false;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(clip_.channels);
    format.nSamplesPerSec = clip_.rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * 2;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (waveOutOpen(&handle_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
      handle_ = nullptr;
      return false;
    }
    header_.lpData = clip_.samples.data();
    header_.dwBufferLength = static_cast<DWORD>(clip_.samples.size());
    if (loop) {
      header_.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
      header_.dwLoops = 0xffffffff;
    }
    if (waveOutPrepareHeader(handle_, &header_, sizeof(header_)) != MMSYSERR_NOERROR) {
      stop();
      return false;
    }
    volume(level);
    if (waveOutWrite(handle_, &header_, sizeof(header_)) != MMSYSERR_NOERROR) {
      stop();
      return false;
    }
    return true;
  }

 private:
  HWAVEOUT handle_ = nullptr;
  WAVEHDR header_{};
  WaveClip clip_;
};
}  // namespace spaceship
