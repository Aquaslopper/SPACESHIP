// ============================================================================
//  Audio.cpp — every number the reference's soundscape is made of.
// ============================================================================
#include "audio/Audio.h"

#include "core/JsNumber.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <cmath>
#include <functional>

namespace ship {
namespace audio {

namespace {

constexpr double kTau = 6.28318;   // the reference's own approximation of 2*pi

double Clamp(double value, double low, double high) {
  return value < low ? low : (value > high ? high : value);
}

bool Contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Levels and gating
// ---------------------------------------------------------------------------
double LevelFromValue(double raw) {
  if (!std::isfinite(raw)) { return 0.4; }
  return Clamp(raw, 0.0, 1.0);
}

double Level(const Options& options) { return LevelFromValue(options.audioLevel); }

Gains GainsFor(const Options& options) {
  const double level = Level(options);
  Gains gains;
  gains.effectsOn = options.soundEffects;
  gains.musicOn = options.music;
  gains.master = level * 0.62;
  gains.effects = gains.effectsOn ? 0.8 : 0.0;
  gains.music = gains.musicOn ? 0.18 : 0.0;
  return gains;
}

// ---------------------------------------------------------------------------
//  The drone and the phrase
// ---------------------------------------------------------------------------
const std::vector<double>& DroneFreqs() {
  static const std::vector<double> freqs = {55.0, 82.41, 110.0};
  return freqs;
}

const std::vector<double>& DroneLevels() {
  static const std::vector<double> levels = {0.10, 0.055, 0.022};
  return levels;
}

const std::vector<double>& MusicNotes() {
  static const std::vector<double> notes = {220.0, 261.63, 196.0, 164.81, 246.94, 196.0};
  return notes;
}

double MusicNoteIntervalMs() { return 2450.0; }
double MusicNoteLengthSeconds() { return 2.3; }
double MusicNotePeak() { return 0.09; }

Note ScheduleNote(int index) {
  const std::vector<double>& notes = MusicNotes();
  const int at = index < 0 ? 0 : index;
  Note note;
  note.atMs = static_cast<double>(at) * MusicNoteIntervalMs();
  note.freq = notes[static_cast<std::size_t>(at) % notes.size()];
  note.durationMs = MusicNoteLengthSeconds() * 1000.0;
  note.peak = MusicNotePeak();
  return note;
}

std::vector<Note> ScheduleFrom(int firstIndex, int count) {
  std::vector<Note> schedule;
  for (int i = 0; i < count; ++i) {
    schedule.push_back(ScheduleNote(firstIndex + i));
  }
  return schedule;
}

void Sequencer::Reset() {
  noteIndex_ = 0;
  nextDueMs_ = 0.0;
  started_ = false;
}

void Sequencer::Configure(const Options& options) { musicOn_ = options.music; }

std::vector<Note> Sequencer::Advance(double nowMs) {
  std::vector<Note> due;
  if (!started_) {
    // startDrone() plays the first note immediately, then every 2450 ms.
    started_ = true;
    nextDueMs_ = nowMs;
  }
  if (!musicOn_) {
    // Silence is still time passing: the phrase keeps its place, so unmuting
    // does not replay a burst of missed notes.
    if (nextDueMs_ <= nowMs) {
      nextDueMs_ = nowMs + MusicNoteIntervalMs();
    }
    return due;
  }
  while (nextDueMs_ <= nowMs) {
    Note note = ScheduleNote(noteIndex_);
    note.atMs = nextDueMs_;
    due.push_back(note);
    noteIndex_ += 1;
    nextDueMs_ += MusicNoteIntervalMs();
  }
  return due;
}

// ---------------------------------------------------------------------------
//  Cues
// ---------------------------------------------------------------------------
std::string CueKindFor(const std::string& detail) {
  std::string text = detail;
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (Contains(text, "scan") || Contains(text, "sensor") || Contains(text, "signal") ||
      Contains(text, "comms")) {
    return "ping";
  }
  if (Contains(text, "course") || Contains(text, "travel") || Contains(text, "jump") ||
      Contains(text, "crossing")) {
    return "thrust";
  }
  if (Contains(text, "complete") || Contains(text, "arriv") || Contains(text, "paid")) {
    return "chime";
  }
  return "click";
}

CueSpec CueSpecFor(const std::string& kind) {
  CueSpec spec;
  spec.kind = kind;
  if (kind == "ping") {
    spec.duration = 0.62;
    spec.startHz = 570.0;
    spec.wave = "sine";
    spec.peak = 0.16;
  } else if (kind == "chime") {
    spec.duration = 0.8;
    spec.startHz = 760.0;
    spec.wave = "sine";
    spec.peak = 0.16;
  } else if (kind == "thrust") {
    spec.duration = 0.24;
    spec.startHz = 142.0;
    spec.wave = "sawtooth";
    spec.peak = 0.16;
  } else {  // click is the fallback, and the only triangle
    spec.kind = "click";
    spec.duration = 0.24;
    spec.startHz = 650.0;
    spec.wave = "triangle";
    spec.peak = 0.12;
  }
  return spec;
}

double CueFrequencyAt(const CueSpec& spec, double t) {
  const double duration = spec.duration <= 0.0 ? 0.0001 : spec.duration;
  const double progress = t / duration;
  if (spec.kind == "ping") {
    return spec.startHz + (1120.0 - spec.startHz) * progress;  // linear rise
  }
  if (spec.kind == "thrust") {
    // exponentialRampToValueAtTime(58): a geometric fall, clamped to the start.
    return spec.startHz * std::pow(58.0 / spec.startHz, progress);
  }
  const double target = spec.startHz * 0.72;
  return spec.startHz * std::pow(target / spec.startHz, progress);
}

double CueEnvelopeAt(const CueSpec& spec, double t) {
  const double duration = spec.duration <= 0.0 ? 0.0001 : spec.duration;
  if (t <= 0.0) { return 0.0001; }
  if (t < 0.025) { return 0.0001 + (spec.peak - 0.0001) * (t / 0.025); }
  // exponentialRampToValueAtTime(0.0001, now + duration) from the peak.
  const double span = duration - 0.025;
  if (span <= 0.0) { return 0.0001; }
  const double progress = (t - 0.025) / span;
  if (progress >= 1.0) { return 0.0001; }
  return spec.peak * std::pow(0.0001 / spec.peak, progress);
}

// ---------------------------------------------------------------------------
//  The fallback PCM
// ---------------------------------------------------------------------------
double MusicSampleAt(double t) {
  const int chord = static_cast<int>(std::floor(t / 2.0)) % 4;
  const double roots[4] = {55.0, 49.0, 65.41, 58.27};
  const double tones[4] = {220.0, 246.94, 196.0, 174.61};
  const double root = roots[chord < 0 ? chord + 4 : chord];
  const double tone = tones[chord < 0 ? chord + 4 : chord];
  const double phase = std::fmod(t, 2.0);
  const double edge = std::min({1.0, t / 0.12, (kMusicSeconds - t) / 0.12});
  const double env = std::min(1.0, phase / 0.18) * std::min(1.0, (2.0 - phase) / 0.35) *
                     std::max(0.0, edge);
  return std::sin(kTau * root * t) * 0.18 + std::sin(kTau * root * 1.5 * t) * 0.07 +
         std::sin(kTau * tone * t) * 0.10 * env;
}

double CueSampleAt(const std::string& kind, double t) {
  const std::string cue = kind.empty() ? "click" : kind;
  const double env = std::exp(-t * (cue == "ping" ? 4.5 : 12.0));
  double freq = 640.0 - 300.0 * (t / kCueSeconds);
  if (cue == "ping") {
    freq = 540.0 + 800.0 * (t / kCueSeconds);
  } else if (cue == "thrust") {
    freq = 145.0 - 85.0 * (t / kCueSeconds);
  } else if (cue == "chime") {
    freq = 760.0 - 230.0 * (t / kCueSeconds);
  }
  return std::sin(kTau * freq * t) * env * (cue == "thrust" ? 0.18 : 0.12);
}

namespace {

std::int16_t Quantise(double value) {
  const double clamped = Clamp(value, -0.85, 0.85);
  const double scaled = std::round(clamped * 32767.0);
  // Signed 16-bit, two's complement, exactly as the reference's b = a<0? a+65536 : a.
  const long long wrapped = scaled < 0.0 ? static_cast<long long>(scaled) + 65536
                                         : static_cast<long long>(scaled);
  return static_cast<std::int16_t>(wrapped >= 32768 ? wrapped - 65536 : wrapped);
}

Buffer Render(double seconds, int sampleRate, const std::function<double(double)>& sample) {
  Buffer buffer;
  buffer.sampleRate = sampleRate > 0 ? sampleRate : kSampleRate;
  const long long count = std::llround(static_cast<double>(buffer.sampleRate) * seconds);
  buffer.samples.reserve(static_cast<std::size_t>(std::max(0LL, count)));
  for (long long i = 0; i < count; ++i) {
    buffer.samples.push_back(Quantise(sample(static_cast<double>(i) / buffer.sampleRate)));
  }
  return buffer;
}

}  // namespace

Buffer RenderMusic(int sampleRate) {
  return Render(kMusicSeconds, sampleRate, [](double t) { return MusicSampleAt(t); });
}

Buffer RenderCue(const std::string& kind, int sampleRate) {
  return Render(kCueSeconds, sampleRate,
                [&kind](double t) { return CueSampleAt(kind, t); });
}

const std::vector<std::string>& SfxFileNames(const std::string& kind) {
  // The names the README promises, then the port's own aliases, so a file
  // dropped in by hand is found even if it is named after the synth cue.
  static const std::vector<std::string> click = {"click.wav"};
  static const std::vector<std::string> alert = {"alert.wav", "ping.wav"};
  static const std::vector<std::string> action = {"action.wav", "thrust.wav"};
  static const std::vector<std::string> complete = {"complete.wav", "arrive.wav",
                                                    "chime.wav"};
  static const std::vector<std::string> none;
  if (kind == "click") { return click; }
  if (kind == "ping") { return alert; }
  if (kind == "thrust") { return action; }
  if (kind == "chime") { return complete; }
  return none;
}

namespace {

bool HasMusicSuffix(const std::string& name) {
  const auto ends = [&name](const char* suffix) {
    const std::string tail(suffix);
    return name.size() >= tail.size() &&
           name.compare(name.size() - tail.size(), tail.size(), tail) == 0;
  };
  return ends(".ogg") || ends(".mp3") || ends(".wav");
}

}  // namespace

std::vector<unsigned char> ToWav(const Buffer& buffer) {
  const std::uint32_t rate = static_cast<std::uint32_t>(buffer.sampleRate);
  const std::uint32_t dataSize = static_cast<std::uint32_t>(buffer.samples.size() * 2);
  const std::uint32_t riffSize = 36 + dataSize;

  std::vector<unsigned char> bytes;
  bytes.reserve(static_cast<std::size_t>(dataSize) + 44);
  const auto put32 = [&bytes](std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 255));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 255));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 255));
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 255));
  };
  const auto put16 = [&bytes](std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 255));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 255));
  };
  const auto text = [&bytes](const char* four) {
    for (int i = 0; i < 4; ++i) { bytes.push_back(static_cast<unsigned char>(four[i])); }
  };

  text("RIFF");
  put32(riffSize);
  text("WAVE");
  text("fmt ");
  put32(16);          // PCM chunk size
  put16(1);           // format: PCM
  put16(1);           // channels: mono
  put32(rate);
  put32(rate * 2);    // byte rate
  put16(2);           // block align
  put16(16);          // bits per sample
  text("data");
  put32(dataSize);
  for (const std::int16_t sample : buffer.samples) {
    const std::uint16_t raw = static_cast<std::uint16_t>(sample);
    put16(raw);
  }
  return bytes;
}

ExternalAudio FindExternalAudio(const std::string& assetsDir) {
  namespace fs = std::filesystem;
  ExternalAudio found;
  if (assetsDir.empty()) { return found; }
  const fs::path audioDir = fs::path(assetsDir) / "audio";
  std::error_code ec;

  const fs::path musicDir = audioDir / "music";
  if (fs::is_directory(musicDir, ec)) {
    for (const fs::directory_entry& entry : fs::directory_iterator(musicDir, ec)) {
      if (!entry.is_regular_file(ec)) { continue; }
      const std::string name = entry.path().filename().string();
      if (!HasMusicSuffix(name)) { continue; }
      found.music.push_back(fs::absolute(entry.path()).string());
    }
    std::sort(found.music.begin(), found.music.end());
  }

  const fs::path sfxDir = audioDir / "sfx";
  if (fs::is_directory(sfxDir, ec)) {
    for (const char* kind : {"click", "ping", "thrust", "chime"}) {
      for (const std::string& name : SfxFileNames(kind)) {
        const fs::path candidate = sfxDir / name;
        if (fs::is_regular_file(candidate, ec)) {
          found.sfx[kind] = fs::absolute(candidate).string();
          break;
        }
      }
    }
  }
  return found;
}

}  // namespace audio
}  // namespace ship
