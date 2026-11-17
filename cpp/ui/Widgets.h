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

std::string ButtonChargeKey(const Simulation& sim, const std::string& id);
std::string ButtonChargeTier(const std::string& subject,
                             const std::string& preferred = std::string());

double ChargeLeft(const Simulation& sim, const std::string& key, const std::string& tier);
double ChargeVolume(const Simulation& sim, const std::string& key, const std::string& tier);
double ChargePercent(const Simulation& sim, const std::string& key, const std::string& tier);

double TierVolume(const Simulation& sim, const std::string& tier);
std::string TierLabel(const Simulation& sim, const std::string& tier);

bool IsActionCost(const std::string& cost, bool hasCost = true);

bool PressWithoutCharge(Simulation& sim, const std::function<bool()>& action);

bool CommitActionButton(Simulation& sim, const std::string& key,
                        const std::string& tier,
                        const std::function<bool()>& action);

bool PressProgressButton(Simulation& sim, const std::string& taskId);

enum class ButtonState {
  Idle,
  Off,
  Empty,
  Running,
  Done,
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
  bool inert = false;
  bool acknowledge = false;
};

bool PressProgressButton(Simulation& sim, const ProgressButton& button);

ProgressButton BuildProgressButton(const Simulation& sim, const std::string& taskId,
                                   const std::string& idleLabel, bool enabled,
                                   const std::string& doneLabel = "Complete",
                                   const std::string& tier = std::string());

double OngoingPercent(const Task& task);

struct CommandRowView {
  std::string label;
  std::string cost;
  std::string displayCost;
  std::string title;
  std::string chargeKey;
  std::string tier;
  bool enabled = false;
  bool live = false;
  std::string confirm;
  bool actionCost = true;
  bool reading = false;
  bool selected = false;
  bool charged = false;
  bool spent = false;
  double chargePercent = 100.0;
  bool plain = false;
  bool hideCost = false;
};

std::string ConversationChargeKey(const std::string& crewId);
CommandRowView BuildConversationRow(const Simulation& sim, const std::string& crewId);
bool PressConversation(Simulation& sim, const std::string& crewId);

struct ActionRowView {
  std::string id;
  std::string label;
  std::string cost;
  std::string note;
  std::string confirm;
  bool enabled = false;
  bool reading = false;
};

std::string CommandSlug(const std::string& label);
CommandRowView BuildCommandRow(const Simulation& sim, const std::string& label,
                               bool enabled, const std::string& cost,
                               const std::string& category, int occurrence);

void BindCommandCharge(const Simulation& sim, CommandRowView& row,
                         const std::string& key, const std::string& category);

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
  std::string placement;
  std::vector<PanelLine> lines;
  std::vector<ProgressButton> buttons;

  NavPanel& Name(const std::string& text, const std::string& tone = std::string());
  NavPanel& Sub(const std::string& text);
  NavPanel& Field(const std::string& key, const std::string& value);
  NavPanel& Dim(const std::string& text);
  NavPanel& Note(const std::string& text);
  NavPanel& Bar(double pct, const std::string& tone);
};

struct MachinePart {
  std::string cls;
  Area box;
  std::string text;
  std::string tone = "";
};

struct LiveControl {
  std::string id;
  Area box;
  ProgressButton button;
};

struct TaskStage {
  std::string id;
  float width = 0.0f;
  float height = 0.0f;
  float typePx = 0.0f;
  std::vector<MachinePart> parts;
  std::vector<LiveControl> controls;
};

struct Vignette {
  std::string roomId;
  std::string glyph;
  std::string code;
  std::string label;
  std::string description;
  std::string accent;

  Area hero;
  Area scope;
  Area orbitOne;
  Area orbitTwo;
  Area core;
  Area beaconOne;
  Area beaconTwo;
  Area readout;
  std::vector<Area> meter;
  std::string statusTop = "SHIP SYSTEM";
  std::string statusValue = "ONLINE";
  bool squareCore = false;
};

struct TaskScreenModel {
  TaskHead head;
  std::vector<NavPanel> left;
  std::vector<NavPanel> under;
  std::vector<NavPanel> right;
  std::vector<NavPanel> band;
  TaskStage stage;
  std::vector<ProgressButton> progress;
  std::vector<CommandRowView> commands;
  std::string bandNote;
  bool hasStage = false;

  Vignette hero;
  bool hasHero = false;

  std::vector<NavPanel> panels;
  std::vector<int> panelColumns;
  std::vector<std::vector<CommandRowView>> panelRows;
  std::vector<std::vector<std::string>> panelRowIds;
};

TaskScreenModel BuildDescribedScreen(const Simulation& sim, const Description& described);

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
  Awards,
  Event,
  Wiki,
  System,
  Info
};
ScreenKind ScreenKindFor(const Tab& tab);

bool PressSystemAction(Simulation& sim, const std::string& statId);
bool PressInfoAction(Simulation& sim, const std::string& target);

struct StationSelection;
TaskScreenModel BuildTabScreen(const Simulation& sim, const Tab& tab,
                               const WikiView& wiki = WikiView(),
                               StationSelection* station = nullptr);

Vignette BuildVignette(const Simulation& sim, const std::string& roomId);

const std::vector<std::string>& SceneGlyphs();

}  // namespace ship
