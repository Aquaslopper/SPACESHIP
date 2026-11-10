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
//  which is why the frame, the button charges and the button's exact strings can
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
#include "ui/WikiView.h"

namespace ship {

// ---------------------------------------------------------------------------
//  Button charges (Section 9). A charge belongs to one control, is counted in
//  clicks, and is scoped to the tab that owns it. A control holds `volume`
//  presses; a press spends one of them and returns one to every other control.
// ---------------------------------------------------------------------------
std::string ButtonChargeKey(const Simulation& sim, const std::string& id);
std::string ButtonChargeTier(const std::string& subject,
                             const std::string& preferred = std::string());

// A control the player has not pressed yet is full, so the bar reads 100 and an
// absent entry means "untouched", not "empty".
double ChargeLeft(const Simulation& sim, const std::string& key, const std::string& tier);
double ChargeVolume(const Simulation& sim, const std::string& key, const std::string& tier);
double ChargePercent(const Simulation& sim, const std::string& key, const std::string& tier);

// The rule's own numbers, so a test can hold a charge to them instead of to a
// literal: routine 8, systems 4, space 2, critical 2 clicks.
double TierVolume(const Simulation& sim, const std::string& tier);
std::string TierLabel(const Simulation& sim, const std::string& tier);

// isActionCost(): undefined/null counts as an action cost, otherwise the cost
// string has to say "action" or "actions".
bool IsActionCost(const std::string& cost, bool hasCost = true);

// pressWithoutCharge(): the same press for a control that keeps its own mechanic
// or is paid in shiplets. It spends nothing itself but still winds the rest of
// the ship back up, because the player did click.
bool PressWithoutCharge(Simulation& sim, const std::function<bool()>& action);

// commitActionButton(): run the callback while the control still holds a click,
// and charge it only when the callback really spent an action.
bool CommitActionButton(Simulation& sim, const std::string& key,
                        const std::string& tier,
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
  Empty,     // out of clicks: the green bar is at zero
  Running,   // a task is under way; inert
  Done,      // finished, waiting to be acknowledged
};

struct ProgressButton {
  std::string taskId;
  std::string idleLabel;
  std::string doneLabel = "Complete";
  std::string text;
  std::string title;
  std::string chargeKey;
  std::string tier;
  ButtonState state = ButtonState::Idle;
  double fillPercent = 0.0;
  bool enabled = true;
  bool inert = false;     // running: the click does nothing
  bool acknowledge = false;
};

bool PressProgressButton(Simulation& sim, const ProgressButton& button);

ProgressButton BuildProgressButton(const Simulation& sim, const std::string& taskId,
                                   const std::string& idleLabel, bool enabled,
                                   const std::string& doneLabel = "Complete",
                                   const std::string& tier = std::string());

// ongoingPercent(): floor of the work done, as a whole percent.
double OngoingPercent(const Task& task);

// ---------------------------------------------------------------------------
//  A command row: a label, a cost badge, and its charge.
// ---------------------------------------------------------------------------
struct CommandRowView {
  std::string label;
  std::string cost;         // what the caller asked for
  std::string displayCost;  // "1 click" or "free"
  std::string title;        // the hover text
  std::string chargeKey;
  std::string tier;
  bool enabled = false;
  bool live = false;        // a `live()` action, wired to something
  std::string confirm;      // asked first when the reader keeps confirmations on
  bool actionCost = true;
  bool reading = false;     // non-interactive text interleaved with command rows
  bool selected = false;    // the station's open card / picked item: marked, not pressed
  bool charged = false;     // this row is paid in clicks, not in shiplets
  bool spent = false;       // no clicks left: the green bar is empty
  double chargePercent = 100.0;  // the green bar: the clicks this control still holds
  bool plain = false;     // a list line, not a button: no box, no border
  bool hideCost = false;  // no cost badge at all: the button is just a button
};

// A row the player can press, with the id the press path answers to. The
// station, the stores, the fitted rooms, the Simulacron, the logbook and the
// incident reports all speak this shape.
// Conversation locks are per crew member, shared across quarters and records.
std::string ConversationChargeKey(const std::string& crewId);
CommandRowView BuildConversationRow(const Simulation& sim, const std::string& crewId);
bool PressConversation(Simulation& sim, const std::string& crewId);

struct ActionRowView {
  std::string id;
  std::string label;
  std::string cost;      // "1 click", "free", "done", or empty
  std::string note;
  std::string confirm;   // asked first when confirmations are on
  bool enabled = false;
  bool reading = false;  // plain reading: nothing to press
};

// commandRow(): the same slug-and-occurrence identity the reference builds, so
// two rows with the same label in one window do not share a lock.
std::string CommandSlug(const std::string& label);
CommandRowView BuildCommandRow(const Simulation& sim, const std::string& label,
                               bool enabled, const std::string& cost,
                               const std::string& category, int occurrence);

// Use the dispatcher's persistent key, not a second label-derived lock.
void BindCommandCharge(const Simulation& sim, CommandRowView& row,
                         const std::string& key, const std::string& category);

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
  std::string tone = "";  // CSS colour, when a part has a state-specific fill
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

  // The hero vignette a fitted room paints above its lists: the
  // geometry, the readout and the eight-meter gauge come from ROOM_SCENES.
  Vignette hero;
  bool hasHero = false;

  // Free-standing columns: the station and the stores are lists of command
  // rows under a heading, not a stage with panels around it. The ids travel
  // beside the rows so a click can be dispatched without parsing a label.
  std::vector<NavPanel> panels;
  // Optional column index per panel; repeated indices stack panels vertically.
  std::vector<int> panelColumns;
  std::vector<std::vector<CommandRowView>> panelRows;
  std::vector<std::vector<std::string>> panelRowIds;
};

// The generic path: a described target that owns no bespoke screen.
TaskScreenModel BuildDescribedScreen(const Simulation& sim, const Description& described);

// Which builder a tab would use. Every window the ship can open has its own
// builder now: the three machine rooms, the chart, quarters and records, the
// station and the stores, the fitted bays with the Simulacron, the logbook, the
// incident reports and the handbook. `Described` is not a placeholder but the
// reference's own briefing frame, and it is what a hull room that owns no work
// still uses.
enum class ScreenKind {
  Described,
  Nav,
  Engine,
  O2,
  Mess,
  Quarters,
  CrewRecord,
  Station,
  Cargo,
  Science,
  Room,
  SimCore,
  Awards,
  Event,
  Wiki,
  System,
  Info
};
ScreenKind ScreenKindFor(const Tab& tab);

bool PressSystemAction(Simulation& sim, const std::string& statId);
bool PressInfoAction(Simulation& sim, const std::string& target);

// The handbook needs its session view (the search text and the selection), so
// the builder takes it as an argument rather than reaching into the shell.
// The station needs its own session view for the same reason: which card is
// open and which item is picked. It is a pointer because the builder writes
// the normalised selection back (an item that has just been sold cannot stay
// picked); callers that only read a screen leave it null and get the default.
struct StationSelection;  // ui/StationScreen.h
TaskScreenModel BuildTabScreen(const Simulation& sim, const Tab& tab,
                               const WikiView& wiki = WikiView(),
                               StationSelection* station = nullptr);

Vignette BuildVignette(const Simulation& sim, const std::string& roomId);

// The nine archetype glyphs, in the order the plan lists them.
const std::vector<std::string>& SceneGlyphs();

}  // namespace ship
