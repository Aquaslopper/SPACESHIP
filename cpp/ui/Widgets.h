// ============================================================================
//  Widgets.h — the house frame, and the three widgets every window uses.
//  ---------------------------------------------------------------------------
//  Every task window in the reference is built from the same few pieces:
//
//    contentScreen   the kind/title/sub header
//    navPanel        a titled box of readings
//    field           one key/value line inside it
//    commandRow      one line of committed work, with its cost badge
//    buildProgressButton  the three-state button (idle / running / done)
//
//  None of that is drawn here. This file produces the data the renderer walks,
//  which is why the frame, the cooldown locks and the button's exact strings can
//  all be checked without a window.
// ============================================================================
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Describe.h"
#include "ui/Layout.h"

namespace ship {

// ---------------------------------------------------------------------------
//  Action cooldowns (Section D-5). A lock belongs to one control, is counted in
//  actions, and is scoped to the tab that owns it.
// ---------------------------------------------------------------------------
std::string ActionCooldownKey(const Simulation& sim, const std::string& id);
std::string ActionCooldownCategory(const std::string& subject,
                                   const std::string& preferred = std::string());
double CooldownRemaining(const Simulation& sim, const std::string& key);

// isActionCost(): undefined/null counts as an action cost, otherwise the cost
// string has to say "action" or "actions".
bool IsActionCost(const std::string& cost, bool hasCost = true);

// commitActionButton(): refuse while locked, run the callback, and start the
// lock only when the callback really spent an action.
bool CommitActionButton(Simulation& sim, const std::string& key,
                        const std::string& category,
                        const std::function<bool()>& action);

// pressProgressButton(): what a click on a progress button does, in the
// reference's order - inert while running, acknowledge when done, commit the
// start otherwise (and refuse while locked). Headless, so the opening loop can
// be driven by a test without a window.
bool PressProgressButton(Simulation& sim, const std::string& taskId);

// ---------------------------------------------------------------------------
//  The three-state progress button.
// ---------------------------------------------------------------------------
enum class ButtonState {
  Idle,      // click to commit
  Off,       // nothing to commit, and the button says why
  Cooldown,  // locked by the action clock
  Running,   // a task is under way; inert
  Done,      // finished, waiting to be acknowledged
};

struct ProgressButton {
  std::string taskId;
  std::string idleLabel;
  std::string doneLabel = "Complete";
  std::string text;
  std::string title;
  std::string cooldownKey;
  std::string category;
  ButtonState state = ButtonState::Idle;
  double fillPercent = 0.0;
  bool enabled = true;
  bool inert = false;     // running: the click does nothing
  bool acknowledge = false;
};

ProgressButton BuildProgressButton(const Simulation& sim, const std::string& taskId,
                                   const std::string& idleLabel, bool enabled,
                                   const std::string& doneLabel = "Complete",
                                   const std::string& category = std::string());

// ongoingPercent(): floor of the work done, as a whole percent.
double OngoingPercent(const Task& task);

// ---------------------------------------------------------------------------
//  A command row: a label, a cost badge, and its lock.
// ---------------------------------------------------------------------------
struct CommandRowView {
  std::string label;
  std::string cost;         // what the caller asked for
  std::string displayCost;  // "1 action", "free", or "COOLDOWN 7"
  std::string title;        // the hover text
  std::string cooldownKey;
  std::string category;
  bool enabled = false;
  bool live = false;        // a `live()` action, wired to something
  bool actionCost = true;
  bool coolingDown = false;
  double remaining = 0.0;
};

// commandRow(): the same slug-and-occurrence identity the reference builds, so
// two rows with the same label in one window do not share a lock.
std::string CommandSlug(const std::string& label);
CommandRowView BuildCommandRow(const Simulation& sim, const std::string& label,
                               bool enabled, const std::string& cost,
                               const std::string& category, int occurrence);

// ---------------------------------------------------------------------------
//  The frame: header, panels, stage, band.
// ---------------------------------------------------------------------------
struct TaskHead {
  std::string kind;
  std::string title;
  std::string sub;
};

struct PanelLine {
  enum class Kind { Name, Sub, Field, Dim, Note, Bar };
  Kind kind = Kind::Field;
  std::string key;
  std::string value;
  std::string tone;
  double pct = 0.0;
};

struct NavPanel {
  std::string id;
  std::string title;
  std::string placement;  // "", "np-last", "np-half", "np-half-last", "np-full"
  std::vector<PanelLine> lines;

  NavPanel& Name(const std::string& text, const std::string& tone = std::string());
  NavPanel& Sub(const std::string& text);
  NavPanel& Field(const std::string& key, const std::string& value);
  NavPanel& Dim(const std::string& text);
  NavPanel& Note(const std::string& text);
  NavPanel& Bar(double pct, const std::string& tone);
};

// One absolutely positioned piece of an illustration. `html` is a text label.
struct MachinePart {
  std::string cls;
  Area box;
  std::string text;
};

// A live control drawn over the scenery: the O2 slot buttons.
struct LiveControl {
  std::string id;
  Area box;
  ProgressButton button;
};

struct TaskStage {
  std::string id;        // "machine", "o2mach", "messmach"
  float width = 0.0f;
  float height = 0.0f;
  std::vector<MachinePart> parts;
  std::vector<LiveControl> controls;
};

struct TaskScreenModel {
  TaskHead head;
  std::vector<NavPanel> left;
  std::vector<NavPanel> under;
  std::vector<NavPanel> right;
  std::vector<NavPanel> band;     // the full-width commitments panel
  TaskStage stage;
  std::vector<ProgressButton> progress;  // buttons in the band (and stage)
  std::vector<CommandRowView> commands;  // the generic frame's action rows
  std::string bandNote;
  bool hasStage = false;
};

// The generic path: a described target that owns no bespoke screen.
TaskScreenModel BuildDescribedScreen(const Simulation& sim, const Description& described);

// Which builder a tab would use. Phase 3 owns the three machine rooms; every
// other window still opens on the described frame until its own phase.
enum class ScreenKind { Described, Engine, O2, Mess, Deferred };
ScreenKind ScreenKindFor(const Tab& tab);
TaskScreenModel BuildTabScreen(const Simulation& sim, const Tab& tab);

// ---------------------------------------------------------------------------
//  ROOM SCENES — the nine vignettes.
// ---------------------------------------------------------------------------
struct Vignette {
  std::string roomId;
  std::string glyph;
  std::string code;
  std::string label;
  std::string description;
  std::string accent;

  // Geometry from .room-hero, so the renderer paints the same picture every
  // time without reading the stylesheet.
  Area hero;        // 0,0,100% x 132
  Area scope;       // 17,14 100x100
  Area orbitOne;    // 11,11 76x76
  Area orbitTwo;    // 23,23 52x52 (dashed)
  Area core;        // 43,43 14x14
  Area beaconOne;   // 21,27 5x5, accent
  Area beaconTwo;   // 75,69 4x4
  Area readout;     // 143,18
  std::vector<Area> meter;  // eight 8%-wide bars, the first five in coffee
  std::string statusTop = "SHIP SYSTEM";
  std::string statusValue = "ONLINE";
  bool squareCore = false;  // the reactor's core is a rotated square
};

Vignette BuildVignette(const Simulation& sim, const std::string& roomId);

// The nine archetype glyphs, in the order the plan lists them.
const std::vector<std::string>& SceneGlyphs();

}  // namespace ship
