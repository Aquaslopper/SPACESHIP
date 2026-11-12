// ============================================================================
//  Popups.cpp — manifest, log, settings and the three save slots.
// ============================================================================
#include "ui/Popups.h"

#include "core/JsNumber.h"
#include "core/DisplaySettings.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace ship {

namespace {

// The same five themes the renderer knows, so the settings sheet can never
// offer a palette the ship has not got (Phase 9 replaced the literal table that
// used to drift from Theme::Specs()).
const std::vector<std::pair<std::string, std::string>>& ThemeTable() {
  static const std::vector<std::pair<std::string, std::string>> table = [] {
    std::vector<std::pair<std::string, std::string>> rows;
    for (const ThemeSpec& spec : Theme::Specs()) {
      rows.emplace_back(spec.id, spec.label);
    }
    return rows;
  }();
  return table;
}

// slotSummary(): a label, not a machine. Read by loading the file into a
// throw-away voyage, so a corrupt slot reports Empty rather than lying.
}  // namespace

std::string SaveSlotSummary(const Simulation& sim, int slot, const std::string& saveDir,
                            bool* written) {
  if (written != nullptr) { *written = false; }
  if (saveDir.empty()) { return "Empty"; }
  Simulation probe = sim;
  std::string error;
  if (!probe.LoadSlot(slot, saveDir, &error)) { return "Empty"; }
  if (written != nullptr) { *written = true; }
  return js::NumToString(probe.ActionCount()) + " clicks, " +
         js::NumToString(probe.State().shiplets) + " sh";
}

const std::vector<std::string>& PopupKeys() {
  static const std::vector<std::string> keys = {"manifest", "log", "files", "settings"};
  return keys;
}

std::string PopupTitle(const std::string& key) {
  if (key == "manifest") { return "Ship Manifest"; }
  if (key == "log") { return "Activity Log"; }
  if (key == "files") { return "Save / Load Game"; }
  if (key == "settings") { return "Settings"; }
  return std::string();
}

PopupModel BuildBriefPopup(const Simulation& sim, const std::string& kind,
                           const std::string& id) {
  const auto description = DescribeTarget(sim, kind, id);
  PopupModel model;
  model.title = description.kindLabel + " - " + description.title;
  model.notice = description.sub;
  PopupSection details;
  // briefSpec deliberately shows at most five rows; the task has the file.
  for (std::size_t i = 0; i < description.rows.size() && i < 5; ++i) {
    details.rows.push_back({description.rows[i].key, description.rows[i].value});
  }
  model.acts = description.acts;
  model.footer.push_back("Right-click to open the " + description.title +
      " window. It waits in the tab bar - you are not taken there.");
  if (kind == "nav") { // navBrief is separately from describeTarget
    model.notice = "The ship chart. Everything that is only LOOKING at it is free.";
    details.rows = {{"Scales", "System, Sector and Galaxy"},
        {"Switching", "The three buttons on the chart - free"},
        {"Panning", "Hold the left button and drag the chart"},
        {"Selecting", "Click any marker to read it - free"},
        {"Galaxy", "Select a region, then open its sector chart - free"},
        {"Sector", "Select a system and jump to its entry star"}};
    const double cost = std::max(1.0, sim.ScanCost());
    model.acts = {{"Start Travel", "varies", true},
                  {"Run Deep Scan", js::NumToString(cost) + " clicks", true}};
    model.footer = {
        "Travel costs one click per step of charted distance, so the price is the crossing itself. Both run in the background and drain from clicks spent anywhere on the ship.",
        "Travel also uses fuel; right-click the fuel gauge to transfer a cell. Right-click to open the Navigation window. It waits in the tab bar - you are not taken there."};
  }
  model.sections.push_back(std::move(details));
  return model;
}

PopupModel BuildPopup(const Simulation& sim, const std::string& key,
                      const std::string& saveDir) {
  PopupModel model;
  model.key = key;
  model.title = PopupTitle(key);

  if (key == "manifest") {
    PopupSection crew;
    crew.title = "Crew";
    if (sim.Crew().empty()) {
      crew.rows.push_back(PopupRow{"Roster", "No crew aboard"});
    }
    for (const CrewMember& member : sim.Crew()) {
      std::string room = member.room;
      for (const Room& r : sim.Rooms()) {
        if (r.id == member.room) { room = r.name; }
      }
      crew.rows.push_back(PopupRow{member.name, member.role + " - " + room});
    }
    PopupSection holds;
    holds.title = "Compartments";
    for (const Room& room : sim.Rooms()) {
      holds.rows.push_back(PopupRow{room.name, room.systems});
    }
    model.sections = {crew, holds};
    return model;
  }

  if (key == "log") {
    // Newest first: the last thing that happened is the thing being asked for.
    for (std::size_t i = sim.State().feed.size(); i-- > 0;) {
      const FeedLine& line = sim.State().feed[i];
      model.lines.push_back(line.stamp.empty() ? line.text
                                               : line.stamp + "  " + line.text);
    }
    return model;
  }

  if (key == "files") {
    model.notice =
        "Three independent slots. Slot 1 uses the original save. Loading replaces "
        "the current voyage; save first if needed.";
    for (int slot = 1; slot <= 3; ++slot) {
      PopupSlot entry;
      entry.slot = slot;
      entry.label = slot == 1 ? "Slot 1 (original)"
                              : "Slot " + js::NumToString(slot);
      entry.summary = SaveSlotSummary(sim, slot, saveDir, &entry.written);
      model.slots.push_back(entry);
    }
    return model;
  }

  // ---- settings ----------------------------------------------------------
  const GameState& state = sim.State();
  const Options& options = state.options;
  model.notice = "Preferences are free to change and are saved with the voyage.";
  model.toggles.push_back(PopupToggle{
      "music", options.music ? "Ambient soundtrack: On" : "Ambient soundtrack: Off",
      options.music});
  model.toggles.push_back(PopupToggle{
      "soundEffects", options.soundEffects ? "Sound effects: On" : "Sound effects: Off",
      options.soundEffects});
  model.audioLevel = js::NumToString(options.audioLevel);
  model.displayGamma = static_cast<float>(DisplayGamma(options.displayGamma));
  model.displayBrightness = static_cast<float>(DisplayBrightness(options.displayBrightness));
  model.audioChoices = {{"0.18", "Quiet"}, {"0.4", "Standard"},
                        {"0.65", "Loud"}, {"0.9", "Very loud"}};
  model.toggles.push_back(PopupToggle{
      "reducedMotion",
      state.reducedMotion ? "Enable decorative motion" : "Reduce decorative motion",
      state.reducedMotion});
  model.toggles.push_back(PopupToggle{
      "largeText", options.largeText ? "Use standard reading text"
                                     : "Use larger reading text",
      options.largeText});
  model.toggles.push_back(PopupToggle{
      "confirmRisk", options.confirmRisk ? "Disable confirmations"
                                         : "Enable confirmations",
      options.confirmRisk});
  model.toggles.push_back(PopupToggle{
      "hideCrewLabels", options.hideCrewLabels ? "Show crew labels" : "Hide crew labels",
      options.hideCrewLabels});
  const char* headings[] = {"Ambient soundtrack", "Sound effects", "Decorative motion", "Reading text", "Confirmations", "Crew labels"};
  const char* help[] = {
      "Play custom music from assets/audio/music, or the built-in shipboard theme.",
      "Soft console clicks, scan pings, drive cues and completion chimes.",
      "Pause map and crew drift, tab glows and click marks.",
      "Enlarge the main reading and click text.",
      "Ask before jettisoning, abandoning, story decisions, saving or loading.",
      "Show or hide names on the ship diagram."};
  for (std::size_t i = 0; i < model.toggles.size(); ++i) {
    model.toggles[i].heading = headings[i];
    model.toggles[i].help = help[i];
  }
  for (const auto& theme : ThemeTable()) {
    model.themes.push_back(theme.first);
    model.themeLabels.push_back(theme.second);
  }
  model.theme = options.theme.empty() ? "classic" : options.theme;

  // ---- typography floor and the window ----------------------------------
  // Four readable native sizes, starting at the required 18px floor.
  model.fontStep = options.fontStep < 0 ? 0 : (options.fontStep > 3 ? 3 : options.fontStep);
  const char* stepNames[] = {"Base", "+5%", "+10%", "+15%"};
  for (int step = 0; step < 4; ++step) {
    const int px = static_cast<int>(Theme::FontPxForStep(step));
    model.fontStepLabels.push_back(std::string(stepNames[step]) + " (" +
                                   js::NumToString(px) + "px)");
  }
  const double stepPx = static_cast<double>(Theme::FontPxForStep(model.fontStep));
  model.fontStepLine = "Reading size: " + std::string(stepNames[model.fontStep]) + " (" +
                       js::NumToString(stepPx) + " px)";
  model.fullscreen = options.fullscreen;
  model.fullscreenLabel = options.fullscreen ? "Window: Borderless fullscreen"
                                             : "Window: Windowed";
  return model;
}

bool PressPopupAction(Simulation& sim, const std::string& key,
                      const std::string& action, const std::string& arg,
                      const std::string& saveDir, std::string* message) {
  if (message != nullptr) { message->clear(); }

  if (key == "settings") {
    GameState& state = sim.MutableState();
    Options& options = state.options;
    if (action == "toggle") {
      // Free: a preference is not a decision the ship has to live with.
      if (arg == "music") { options.music = !options.music; return true; }
      if (arg == "soundEffects") { options.soundEffects = !options.soundEffects; return true; }
      if (arg == "largeText") { options.largeText = !options.largeText; return true; }
      if (arg == "confirmRisk") { options.confirmRisk = !options.confirmRisk; return true; }
      if (arg == "hideCrewLabels") {
        options.hideCrewLabels = !options.hideCrewLabels;
        return true;
      }
      if (arg == "reducedMotion") {
        state.reducedMotion = !state.reducedMotion;
        return true;
      }
      return false;
    }
    if (action == "theme") {
      for (const auto& theme : ThemeTable()) {
        if (theme.first == arg) { options.theme = arg; return true; }
      }
      return false;
    }
    if (action == "audio") {
      options.audioLevel = std::stod(arg);
      return true;
    }
    if (action == "display-gamma" || action == "display-brightness") {
      try {
        std::size_t parsed = 0;
        const double value = std::stod(arg, &parsed);
        if (parsed != arg.size() || !std::isfinite(value)) { return false; }
        if (action == "display-gamma") { options.displayGamma = DisplayGamma(value); }
        else { options.displayBrightness = DisplayBrightness(value); }
        return true;
      } catch (const std::exception&) { return false; }
    }
    if (action == "font") {
      // Free, and clamped to the four offered steps rather than trusted.
      const int step = std::stoi(arg.empty() ? "0" : arg);
      if (step < 0 || step > 3) { return false; }
      options.fontStep = step;
      return true;
    }
    if (action == "font-step") {
      // The keyboard's version of the ladder: one step up or down, stopping at
      // the ends instead of wrapping past them.
      const int delta = arg == "-1" ? -1 : 1;
      const int step = options.fontStep + delta;
      if (step < 0 || step > 3) { return false; }
      options.fontStep = step;
      return true;
    }
    if (action == "fullscreen") {
      options.fullscreen = !options.fullscreen;
      return true;
    }
    return false;
  }

  if (key == "files") {
    const int slot = std::stoi(arg.empty() ? "1" : arg);
    if (slot < 1 || slot > 3) { return false; }
    if (action == "save") {
      std::string error;
      std::string directory = saveDir.empty() ? std::string(".") : saveDir;
      if (!sim.SaveSlot(slot, directory, &error)) {
        if (message != nullptr) { *message = "Could not save: " + error; }
        return false;
      }
      if (message != nullptr) {
        *message = "Saved to " + (directory == "." ? std::string() : directory + "/") + Simulation::SlotFilename(slot) + ".";
      }
      return true;
    }
    if (action == "new-voyage") {
      if (message != nullptr) { *message = std::string(); }
      return true;
    }
    if (action == "main-menu") {
      // The shell hides the voyage and shows the menu; nothing here changes the
      // voyage, so the command is free like every other one on this sheet.
      if (message != nullptr) { *message = "Returning to the main menu."; }
      return true;
    }
    if (action == "load") {
      // LoadSlot validates and rolls back inside SaveSystem: a corrupt file
      // leaves the voyage exactly as it was.
      std::string error;
      std::string directory = saveDir.empty() ? std::string(".") : saveDir;
      if (!sim.LoadSlot(slot, directory, &error)) {
        if (message != nullptr) { *message = "Could not load: " + error; }
        return false;
      }
      if (message != nullptr) { *message = "Saved voyage restored."; }
      return true;
    }
    return false;
  }

  // The manifest and the log are read-only: there is nothing to press.
  return false;
}

}  // namespace ship
