// ============================================================================
//  Audio.h — the reference's self-contained sound (Section D-3).
//  ---------------------------------------------------------------------------
//  Spaceship.hta fetches nothing. It synthesises everything it plays: a drone
//  on three fixed partials, a six-note ambient phrase every 2.45 seconds, four
//  cues chosen by what the action said, and — where Web Audio is missing — a
//  16-bit mono PCM buffer built sample by sample.
//
//  This translation keeps the arithmetic, not the browser. Every frequency,
//  envelope, duration and gain below is the reference's number, and everything
//  that decides a sample is a pure function, so the tests can hold the
//  waveform, the schedule and the mute/volume gating without a sound card.
//
//  The Raylib layer (app/main.cpp) only turns these buffers into a Wave and
//  hands the cue and note events to the device; nothing about what to play is
//  decided there.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/GameState.h"

namespace ship {
namespace audio {

// ---- the reference's constants ---------------------------------------------
// audioLevel(): options.audioLevel, defaulted and clamped to 0..1.
double Level(const Options& options);
double LevelFromValue(double raw);

// syncAudioOptions(): three gains on one master. Master = level * 0.62,
// effects 0.8 of it, music 0.18 of it — each zeroed by its own switch.
struct Gains {
  double master = 0.0;
  double effects = 0.0;
  double music = 0.0;
  bool effectsOn = true;
  bool musicOn = true;
};
Gains GainsFor(const Options& options);

// startDrone(): the three partials and their levels, in Hz and unity gain.
const std::vector<double>& DroneFreqs();
const std::vector<double>& DroneLevels();

// playMusicNote(): the six-note phrase that walks around itself.
const std::vector<double>& MusicNotes();
double MusicNoteIntervalMs();   // 2450
double MusicNoteLengthSeconds();// 2.3, the stop time
double MusicNotePeak();         // 0.09, the linear ramp peak

// One scheduled note: when it starts, what it sounds, how long and how loud.
struct Note {
  double atMs = 0.0;
  double freq = 0.0;
  double durationMs = 0.0;
  double peak = 0.0;
};
// The phrase as the sequencer plays it: note i starts at i * 2450 ms and takes
// the i-th entry of MusicNotes(), wrapping. `index` keeps counting up forever,
// exactly as SOUND.musicIndex does.
Note ScheduleNote(int index);
std::vector<Note> ScheduleFrom(int firstIndex, int count);

// The live sequencer, kept headless: it owns the index, the next due time and
// the events the device has not consumed yet.
class Sequencer {
 public:
  void Configure(const Options& options);
  // Advance to `nowMs`; returns the notes that became due, in order. A clock
  // that jumps forward emits every note it passed, at its own scheduled time.
  std::vector<Note> Advance(double nowMs);
  bool MusicOn() const { return musicOn_; }
  int NoteIndex() const { return noteIndex_; }
  double NextDueMs() const { return nextDueMs_; }
  void Reset();

 private:
  bool musicOn_ = true;
  int noteIndex_ = 0;
  double nextDueMs_ = 0.0;
  bool started_ = false;
};

// ---- cues -------------------------------------------------------------------
// cueKind(detail): a committed action's sentence picks one of four cues.
std::string CueKindFor(const std::string& detail);

struct CueSpec {
  std::string kind;
  double duration = 0.24;   // seconds
  double startHz = 650.0;
  std::string wave;         // sine | triangle | sawtooth
  double peak = 0.12;       // the linear ramp's target
};
CueSpec CueSpecFor(const std::string& kind);

// The Web Audio shape, as arithmetic: the oscillator's frequency at time t and
// the envelope's gain at time t, both in seconds from the cue's start.
double CueFrequencyAt(const CueSpec& spec, double t);
double CueEnvelopeAt(const CueSpec& spec, double t);

// ---- the fallback PCM --------------------------------------------------------
// waveDataUri(): 8000 Hz, mono, 16-bit. Music is eight seconds of a four-chord
// bed; a cue is 0.42 seconds of a swept sine. Samples are clamped to ±0.85 and
// scaled by 32767, exactly as `waveDataUri` does.
constexpr int kSampleRate = 8000;
constexpr double kMusicSeconds = 8.0;
constexpr double kCueSeconds = 0.42;

struct Buffer {
  int sampleRate = kSampleRate;
  std::vector<std::int16_t> samples;

  std::size_t Frames() const { return samples.size(); }
  double Seconds() const {
    return sampleRate > 0 ? static_cast<double>(samples.size()) / sampleRate : 0.0;
  }
};

// The float sample before quantisation, so tests can compare the waveform.
double MusicSampleAt(double t);
double CueSampleAt(const std::string& kind, double t);

Buffer RenderMusic(int sampleRate = kSampleRate);
Buffer RenderCue(const std::string& kind, int sampleRate = kSampleRate);

// The 44-byte RIFF header the reference writes by hand, then the samples.
std::vector<unsigned char> ToWav(const Buffer& buffer);

// ---- the player's own audio (Phase 9) ---------------------------------------
// The soundtrack the port synthesises is a fallback, not a rule. If the player
// drops files into assets/audio, those win:
//
//   assets/audio/music/   every .ogg, .mp3 or .wav is streamed and cycled
//   assets/audio/sfx/     click.wav, action.wav, complete.wav, alert.wav,
//                         arrive.wav (and a few aliases) replace the synth cue
//
// Everything below is pure filesystem inspection so the headless tests can hold
// the search rules to their word; main.cpp is the only place that loads.
const std::vector<std::string>& SfxFileNames(const std::string& kind);

struct ExternalAudio {
  std::vector<std::string> music;                 // absolute paths, sorted
  std::map<std::string, std::string> sfx;         // cue kind -> the file found
  bool any() const { return !music.empty() || !sfx.empty(); }
};

// Scans `<assets>/audio`. A missing directory is an empty result, not an error.
ExternalAudio FindExternalAudio(const std::string& assetsDir);

}  // namespace audio
}  // namespace ship
