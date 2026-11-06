#pragma once

#include <string>

namespace ship {
class Simulation;

// Dated snapshots are separate from the three manual slots; neither kind is
// overwritten by autosave. LoadNewestSave also considers old/manual slots.
enum class AutoLoadResult { None, Loaded, Failed };
bool SaveDated(const Simulation& sim, const std::string& directory,
               std::string* savedPath, std::string* error);
AutoLoadResult LoadNewestSave(Simulation& sim, const std::string& directory,
                              std::string* loadedPath, std::string* warning);
}  // namespace ship
