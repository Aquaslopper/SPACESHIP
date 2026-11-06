#include "core/AutoSave.h"
#include "core/Simulation.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

namespace ship {
namespace fs = std::filesystem;
namespace {
std::string DateName() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch()).count() % 1000;
  std::ostringstream out;
  out << "Spaceship_" << std::put_time(&local, "%Y-%m-%d_%H-%M-%S") << '-'
      << std::setw(3) << std::setfill('0') << millis;
  return out.str();
}
bool IsSaveName(const std::string& name) {
  for (int slot = 1; slot <= 3; ++slot) if (name == Simulation::SlotFilename(slot)) return true;
  return name.rfind("Spaceship_", 0) == 0 && name.size() > 14 &&
         name.compare(name.size() - 4, 4, ".txt") == 0;
}
}  // namespace

bool SaveDated(const Simulation& sim, const std::string& directory,
               std::string* savedPath, std::string* error) {
  const fs::path folder = fs::u8path(directory);
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) {
    if (error) *error = "Could not create autosave folder " + directory + ": " + ec.message();
    return false;
  }
  const std::string stem = DateName();
  for (unsigned int n = 0; n < 10000; ++n) {
    const std::string name = stem + (n == 0 ? "" : "-" + std::to_string(n)) + ".txt";
    const fs::path path = folder / name;
    // A per-name directory reserves it across simultaneously running copies.
    const fs::path lock = folder / (name + ".lock");
    if (!fs::create_directory(lock, ec)) {
      if (ec) { if (error) *error = "Could not reserve autosave: " + ec.message(); return false; }
      continue;
    }
    if (fs::exists(path, ec)) { fs::remove(lock, ec); continue; }
    if (ec) {
      std::error_code ignored; fs::remove(lock, ignored);
      if (error) *error = "Could not check autosave path: " + ec.message();
      return false;
    }
    const bool ok = sim.SaveToFile(path.u8string(), false, error);
    fs::remove(lock, ec);
    if (ok) { if (savedPath) *savedPath = path.u8string(); return true; }
    // A partial save is not valid; leave existing snapshots untouched.
    fs::remove(path, ec);
    return false;
  }
  if (error) *error = "Could not find a unique dated save name in " + directory;
  return false;
}

AutoLoadResult LoadNewestSave(Simulation& sim, const std::string& directory,
                              std::string* loadedPath, std::string* warning) {
  if (loadedPath) loadedPath->clear();
  if (warning) warning->clear();
  std::error_code ec;
  const fs::path folder = fs::u8path(directory);
  if (!fs::exists(folder, ec) && !ec) return AutoLoadResult::None;
  if (ec) {
    if (warning) *warning = "Could not read the save folder " + directory + ": " + ec.message();
    return AutoLoadResult::Failed;
  }
  if (!fs::is_directory(folder, ec)) {
    if (warning) *warning = "Could not read the save folder " + directory +
        (ec ? ": " + ec.message() : ": not a directory");
    return AutoLoadResult::Failed;
  }
  struct Candidate { fs::path path; fs::file_time_type written; };
  std::vector<Candidate> files;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code entryError;
    if (!it->is_regular_file(entryError) || !IsSaveName(it->path().filename().u8string())) continue;
    const auto written = it->last_write_time(entryError);
    if (!entryError) files.push_back({it->path(), written});
  }
  if (ec) { if (warning) *warning = "Could not scan the save folder " + directory + ": " + ec.message(); return AutoLoadResult::Failed; }
  std::sort(files.begin(), files.end(), [](const Candidate& a, const Candidate& b) {
    return a.written == b.written ? a.path.u8string() > b.path.u8string() : a.written > b.written;
  });
  for (const Candidate& file : files) {
    std::string why;
    if (sim.LoadFromFile(file.path.u8string(), &why)) {
      if (loadedPath) *loadedPath = file.path.u8string();
      return AutoLoadResult::Loaded;
    }
    if (warning && warning->empty()) *warning = "Could not load " + file.path.u8string() + ": " + why;
  }
  return files.empty() ? AutoLoadResult::None : AutoLoadResult::Failed;
}
}  // namespace ship
