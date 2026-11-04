// ============================================================================
//  Widgets.cpp — the frame, the button and the lock.
//  ---------------------------------------------------------------------------
//  Everything here is a direct reading of Section D-5 and the frame builders at
//  the end of Section I. Where the reference uses a regular expression, this
//  file uses the same expressions, spelled out as ordered substring tests so a
//  reader can compare them line for line with the HTA.
// ============================================================================
#include "ui/Widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>

#include "core/JsNumber.h"
#include "ui/EngineScreen.h"
#include "ui/MessScreen.h"
#include "ui/NavScreen.h"
#include "ui/O2Screen.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

std::string Lower(const std::string& text) {
  std::string out = text;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool ContainsAny(const std::string& haystack,
                 const std::vector<std::string>& needles) {
  for (const std::string& needle : needles) {
    if (haystack.find(needle) != std::string::npos) { return true; }
  }
  return false;
}

// /\baction(?:s)?\b/i
bool MentionsAction(const std::string& text) {
  const std::string lower = Lower(text);
  std::size_t at = lower.find("action");
  while (at != std::string::npos) {
    std::size_t end = at + 6;
    if (end < lower.size() && lower[end] == 's') { ++end; }
    const bool startOk =
        at == 0 || !(std::isalnum(static_cast<unsigned char>(lower[at - 1])) ||
                     lower[at - 1] == '_');
    const bool endOk = end >= lower.size() ||
                       !(std::isalnum(static_cast<unsigned char>(lower[end])) ||
                         lower[end] == '_');
    if (startOk && endOk) { return true; }
    at = lower.find("action", at + 1);
  }
  return false;
}

std::string RuleLabel(const Simulation& sim, const std::string& category) {
  auto it = sim.GameRules().cooldowns.find(category);
  return it != sim.GameRules().cooldowns.end() ? it->second.label : "Action";
}

double RuleActions(const Simulation& sim, const std::string& category) {
  auto it = sim.GameRules().cooldowns.find(category);
  return it != sim.GameRules().cooldowns.end() ? it->second.actions : 5.0;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Cooldowns
// ---------------------------------------------------------------------------
std::string ActionCooldownKey(const Simulation& sim, const std::string& id) {
  const std::string tab = sim.State().activeTab.empty() ? "home" : sim.State().activeTab;
  return "cd|" + tab + "|" + (id.empty() ? std::string("action") : id);
}

std::string ActionCooldownCategory(const std::string& subject,
                                   const std::string& preferred) {
  if (!preferred.empty()) {
    // The four categories are the only legal values; anything else is guessed.
    if (preferred == "routine" || preferred == "systems" || preferred == "space" ||
        preferred == "critical") {
      return preferred;
    }
  }
  const std::string s = Lower(subject);
  if (ContainsAny(s, {"signal", "archive", "buy larger ship", "commissioned sc",
                      "permanent", "irreversible", "jettison", "abandon"})) {
    return "critical";
  }
  if (ContainsAny(s, {"navigation", "travel", "jump", "course", "dock", "undock",
                      "watch", "crossing", "salvage", "scan", "route", "space",
                      "traffic report"})) {
    return "space";
  }
  const bool startsBuy = s.compare(0, 4, "buy ") == 0;
  const bool startsSell = s.compare(0, 5, "sell ") == 0;
  const bool startsAccept = s.compare(0, 7, "accept ") == 0;
  const bool startsCollect = s.compare(0, 8, "collect ") == 0;
  const bool startsHire = s.compare(0, 5, "hire ") == 0;
  if (startsBuy || startsSell || startsAccept || startsCollect || startsHire) {
    return "routine";
  }
  if (ContainsAny(s, {"o2", "oxygen", "filter", "engine", "maintenance", "cook",
                      "project", "medical", "treat", "incident", "resolve", "assign",
                      "sensor", "shield", "power", "recycl", "workshop", "hydropon",
                      "fuel reserve", "damage control", "thermal", "package", "hull",
                      "repair", "service", "recharge", "transfer", "split", "upgrade",
                      "expand", "cargo hold", "efficient drive", "rack"})) {
    return "systems";
  }
  return "routine";
}

double CooldownRemaining(const Simulation& sim, const std::string& key) {
  auto it = sim.State().actionCooldowns.find(key);
  return it == sim.State().actionCooldowns.end() ? 0.0 : it->second.remaining;
}

bool IsActionCost(const std::string& cost, bool hasCost) {
  if (!hasCost || cost.empty()) { return true; }
  return MentionsAction(cost);
}

bool CommitActionButton(Simulation& sim, const std::string& key,
                        const std::string& category,
                        const std::function<bool()>& action) {
  if (CooldownRemaining(sim, key) > 0.0) { return false; }

  const double before = sim.ActionCount();
  bool result = false;
  try {
    result = action();
  } catch (...) {
    // A callback that throws must still leave the clock consistent, exactly as
    // the reference's finally block does.
    if (sim.ActionCount() > before) {
      const std::string chosen = ActionCooldownCategory(key, category);
      sim.MutableState().actionCooldowns[key] =
          Cooldown{chosen, RuleActions(sim, chosen)};
    }
    throw;
  }
  if (sim.ActionCount() > before) {
    const std::string chosen = ActionCooldownCategory(key, category);
    sim.MutableState().actionCooldowns[key] =
        Cooldown{chosen, RuleActions(sim, chosen)};
  }
  return result;
}

// ---------------------------------------------------------------------------
//  The progress button
// ---------------------------------------------------------------------------
double OngoingPercent(const Task& task) {
  if (task.total <= 0.0) { return 0.0; }
  return std::floor(((task.total - task.left) / task.total) * 100.0);
}

ProgressButton BuildProgressButton(const Simulation& sim, const std::string& taskId,
                                   const std::string& idleLabel, bool enabled,
                                   const std::string& doneLabel,
                                   const std::string& category) {
  ProgressButton button;
  button.taskId = taskId;
  button.idleLabel = idleLabel;
  button.doneLabel = doneLabel.empty() ? "Complete" : doneLabel;
  button.enabled = enabled;
  button.cooldownKey = ActionCooldownKey(sim, "progress:" + taskId);
  button.category =
      ActionCooldownCategory(taskId + " " + idleLabel, category);

  const Task* task = sim.FindOngoing(taskId);
  const double remaining = CooldownRemaining(sim, button.cooldownKey);

  if (task == nullptr) {
    button.fillPercent = 0.0;
    if (remaining > 0.0) {
      button.state = ButtonState::Cooldown;
      button.text = "Cooldown: " + Num(remaining) + " actions";
      button.title = RuleLabel(sim, sim.State().actionCooldowns.at(button.cooldownKey).category) +
                     " cooldown. Other committed actions advance it; waiting does not.";
    } else {
      button.state = enabled ? ButtonState::Idle : ButtonState::Off;
      button.text = idleLabel;
    }
    return button;
  }

  if (task->state == "done") {
    button.state = ButtonState::Done;
    button.fillPercent = 100.0;
    button.text = button.doneLabel;
    button.acknowledge = true;
    return button;
  }

  button.state = ButtonState::Running;
  button.fillPercent = OngoingPercent(*task);
  button.text = task->label;
  button.inert = true;
  return button;
}

bool PressProgressButton(Simulation& sim, const std::string& taskId) {
  const Task* task = sim.FindOngoing(taskId);
  if (task != nullptr && task->state == "running") { return false; }  // inert
  if (task != nullptr && task->state == "done") { return sim.Acknowledge(taskId); }

  const ProgressButton button =
      BuildProgressButton(sim, taskId, "action", true);
  if (button.state == ButtonState::Cooldown) { return false; }
  if (button.state == ButtonState::Off) { return false; }
  if (button.state == ButtonState::Running) { return false; }

  // One task id per control, and the control's category is the one the button
  // reported, so a lock and the thing that caused it cannot disagree.
  const std::string category = button.category;
  return CommitActionButton(sim, ActionCooldownKey(sim, "progress:" + taskId),
                            category, [&sim, &taskId]() {
                              if (taskId.compare(0, 3, "o2s") == 0 && taskId.size() == 4) {
                                return sim.StartCanisterInstall(taskId[3] - '0');
                              }
                              if (taskId == "o2-run") { return sim.StartFilterBank(); }
                              if (taskId == "maint") { return sim.StartMaintenanceSelf(); }
                              if (taskId == "cook") { return sim.StartCookingSelf(); }
                              if (taskId == "travel") { return sim.StartTravel(); }
                              if (taskId == "scan") { return sim.StartScan(); }
                              return false;
                            });
}

// ---------------------------------------------------------------------------
//  Command rows
// ---------------------------------------------------------------------------
std::string CommandSlug(const std::string& label) {
  const std::string source = label.empty() ? "action" : label;
  std::string slug;
  bool pendingDash = false;
  for (char c : Lower(source)) {
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) {
      if (pendingDash && !slug.empty()) { slug.push_back('-'); }
      slug.push_back(c);
      pendingDash = false;
    } else {
      pendingDash = true;
    }
  }
  return slug;
}

CommandRowView BuildCommandRow(const Simulation& sim, const std::string& label,
                               bool enabled, const std::string& cost,
                               const std::string& category, int occurrence) {
  CommandRowView row;
  row.label = label;
  row.cost = cost;
  row.actionCost = IsActionCost(cost, !cost.empty());
  row.displayCost = cost.empty() ? "1 action" : cost;
  row.enabled = enabled;
  row.category = ActionCooldownCategory(label, category);
  row.cooldownKey =
      ActionCooldownKey(sim, "command:" + CommandSlug(label) + ":" + Num(occurrence));

  row.remaining = CooldownRemaining(sim, row.cooldownKey);
  if (row.remaining > 0.0) {
    row.coolingDown = true;
    row.displayCost = "COOLDOWN " + Num(row.remaining);
    const std::string tier =
        RuleLabel(sim, sim.State().actionCooldowns.at(row.cooldownKey).category);
    row.title = tier + " cooldown: " + Num(row.remaining) +
                " more actions. Other committed actions advance it; waiting does "
                "not.";
  }
  return row;
}

// ---------------------------------------------------------------------------
//  Panels
// ---------------------------------------------------------------------------
NavPanel& NavPanel::Name(const std::string& text, const std::string& tone) {
  lines.push_back(PanelLine{PanelLine::Kind::Name, std::string(), text, tone, 0.0});
  return *this;
}

NavPanel& NavPanel::Sub(const std::string& text) {
  lines.push_back(PanelLine{PanelLine::Kind::Sub, std::string(), text, std::string(), 0.0});
  return *this;
}

NavPanel& NavPanel::Field(const std::string& key, const std::string& value) {
  lines.push_back(PanelLine{PanelLine::Kind::Field, key, value, std::string(), 0.0});
  return *this;
}

NavPanel& NavPanel::Dim(const std::string& text) {
  lines.push_back(PanelLine{PanelLine::Kind::Dim, std::string(), text, std::string(), 0.0});
  return *this;
}

NavPanel& NavPanel::Note(const std::string& text) {
  lines.push_back(PanelLine{PanelLine::Kind::Note, std::string(), text, std::string(), 0.0});
  return *this;
}

NavPanel& NavPanel::Bar(double pct, const std::string& tone) {
  lines.push_back(PanelLine{PanelLine::Kind::Bar, std::string(), std::string(), tone, pct});
  return *this;
}

// ---------------------------------------------------------------------------
//  The generic frame
// ---------------------------------------------------------------------------
TaskScreenModel BuildDescribedScreen(const Simulation& sim,
                                     const Description& described) {
  TaskScreenModel screen;
  screen.head.kind = described.kindLabel;
  screen.head.title = described.title;
  screen.head.sub = described.sub;

  // A window with no actions at all gets no Available Actions column.
  screen.head.kind = described.kindLabel;
  NavPanel status;
  status.id = "status";
  status.title = "Status";
  for (const DescribeRow& row : described.rows) { status.Field(row.key, row.value); }
  screen.left.push_back(status);

  if (described.acts.empty()) { return screen; }

  NavPanel actions;
  actions.id = "actions";
  actions.title = "Available Actions";
  int occurrence = 0;
  int dead = 0;
  for (const DescribeAct& act : described.acts) {
    CommandRowView row = BuildCommandRow(
        sim, act.label, act.live, act.cost, std::string(), occurrence);
    ++occurrence;
    row.live = act.live;
    if (!act.live) { ++dead; }
    screen.commands.push_back(row);
  }
  if (dead > 0) {
    screen.bandNote =
        "Greyed interactions are not wired yet. Costs shown are the intended "
        "design values.";
  }
  screen.band.push_back(actions);
  return screen;
}

// ---------------------------------------------------------------------------
//  Routing
// ---------------------------------------------------------------------------
ScreenKind ScreenKindFor(const Tab& tab) {
  // The chart is not a room: it is its own window, and it is the first one the
  // player can open from the taskbar.
  if (tab.kind == "nav") { return ScreenKind::Nav; }
  if (tab.kind != "room") { return ScreenKind::Described; }
  if (tab.id == "engine") { return ScreenKind::Engine; }
  if (tab.id == "o2") { return ScreenKind::O2; }
  if (tab.id == "mess") { return ScreenKind::Mess; }
  // Cargo, science, the fitted systems, the bridge and the quarters each have a
  // window of their own in a later phase; until then they open on the house
  // frame rather than on nothing.
  if (tab.id == "cargo" || tab.id == "science" || tab.id == "quarters") {
    return ScreenKind::Deferred;
  }
  for (const std::string& glyph : SceneGlyphs()) { (void)glyph; }
  if (tab.id == "bridge") { return ScreenKind::Deferred; }
  return ScreenKind::Described;
}

TaskScreenModel BuildTabScreen(const Simulation& sim, const Tab& tab) {
  switch (ScreenKindFor(tab)) {
    case ScreenKind::Nav: return BuildNavScreen(sim).screen;
    case ScreenKind::Engine: return BuildEngineScreen(sim).screen;
    case ScreenKind::O2: return BuildO2Screen(sim).screen;
    case ScreenKind::Mess: return BuildMessScreen(sim).screen;
    case ScreenKind::Deferred:
    case ScreenKind::Described:
    default:
      break;
  }
  return BuildDescribedScreen(sim, DescribeTarget(sim, tab.kind, tab.id));
}

// ---------------------------------------------------------------------------
//  ROOM SCENES
// ---------------------------------------------------------------------------
const std::vector<std::string>& SceneGlyphs() {
  static const std::vector<std::string> glyphs = {
      "reactor", "filter", "habitat", "medical", "cargo",
      "orbit",   "core",   "network", "garden"};
  return glyphs;
}

Vignette BuildVignette(const Simulation& sim, const std::string& roomId) {
  static const RoomScene kFallback{"network", "SHIP SYSTEM", "Systems console",
                                   "Local status and controls.", "#8c6f4e"};
  Vignette v;
  v.roomId = roomId;
  auto it = sim.World().roomScenes.find(roomId);
  const RoomScene& scene = it != sim.World().roomScenes.end() ? it->second : kFallback;
  v.glyph = scene.glyph;
  v.code = scene.code;
  v.label = scene.label;
  v.description = scene.description;
  v.accent = scene.accent;

  v.hero = Area{0.0f, 0.0f, 0.0f, 132.0f};
  v.scope = Area{17.0f, 14.0f, 100.0f, 100.0f};
  v.orbitOne = Area{11.0f, 11.0f, 76.0f, 76.0f};
  v.orbitTwo = Area{23.0f, 23.0f, 52.0f, 52.0f};
  v.core = Area{43.0f, 43.0f, 14.0f, 14.0f};
  v.beaconOne = Area{21.0f, 27.0f, 5.0f, 5.0f};
  v.beaconTwo = Area{75.0f, 69.0f, 4.0f, 4.0f};
  v.readout = Area{143.0f, 18.0f, 0.0f, 0.0f};

  // Eight bars, 8% of the readout each, 3px apart; the first five are inked.
  for (int i = 0; i < 8; ++i) {
    v.meter.push_back(Area{static_cast<float>(i) * 11.0f, 0.0f, 8.0f, 4.0f});
  }
  v.squareCore = v.glyph == "reactor";
  return v;
}

}  // namespace ship
