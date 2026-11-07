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
#include "ui/CrewRecord.h"
#include "ui/NavScreen.h"
#include "ui/AwardsScreen.h"
#include "ui/EventScreen.h"
#include "ui/Popups.h"
#include "ui/RoomsScreen.h"
#include "ui/SimCoreScreen.h"
#include "ui/WikiScreen.h"
#include "ui/QuartersScreen.h"
#include "ui/Shell.h"
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
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

std::string RuleLabel(const Simulation& sim, const std::string& category) {
  auto it = sim.World().rules.cooldowns.find(category);
  return it == sim.World().rules.cooldowns.end() ? std::string("Action") : it->second.label;
}

double RuleActions(const Simulation& sim, const std::string& category) {
  auto it = sim.World().rules.cooldowns.find(category);
  return it == sim.World().rules.cooldowns.end() ? 5.0 : it->second.actions;
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
                     " cooldown: " + Num(remaining) + " more actions. Other committed actions advance it; waiting does not.";
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
  // Resolve the actual rendered control, including its label/category and
  // enabled condition; a synthetic "action" label loses e.g. Maintenance's tier.
  const Tab* tab = FindTab(sim.State(), sim.State().activeTab);
  if (tab != nullptr) {
    const auto screen = BuildTabScreen(sim, *tab);
    for (const auto& button : screen.progress) {
      if (button.taskId == taskId) { return PressProgressButton(sim, button); }
    }
    for (const auto& control : screen.stage.controls) {
      if (control.button.taskId == taskId) { return PressProgressButton(sim, control.button); }
    }
  }
  return PressProgressButton(sim, BuildProgressButton(sim, taskId, "action", true));
}

bool PressProgressButton(Simulation& sim, const ProgressButton& button) {
  const std::string& taskId = button.taskId;
  const Task* task = sim.FindOngoing(taskId);
  if (task != nullptr && task->state == "running") { return false; }  // inert
  if (task != nullptr && task->state == "done") { return sim.Acknowledge(taskId); }

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
                              if (taskId == "extract") { return sim.StartExtraction(); }
                              if (taskId == "crew-assign") { return sim.CommitOrders(); }
                              // Phase 7: projects, the medical bay, the shared meal
                              // and the incident work all live behind this one
                              // button, exactly as they do in the reference.
                              if (taskId.compare(0, 8, "project-") == 0) {
                                const std::string id = taskId.substr(8);
                                if (id == "social") { return false; }  // needs a pair
                                return sim.StartProject(id);
                              }
                              // Science packaging: the Science Room's own
                              // progress button (Phase 9 audit found it had no
                              // dispatch at all, so the button was dead).
                              if (taskId.compare(0, 4, "pkg-") == 0) {
                                return sim.PackageSurvey(taskId.substr(4));
                              }
                              if (taskId.compare(0, 8, "salvage-") == 0) {
                                return sim.StartSalvage(std::stoi(taskId.substr(8)));
                              }
                              if (taskId.compare(0, 4, "doc-") == 0) {
                                return sim.StartMedical(taskId.substr(4));
                              }
                              if (taskId.compare(0, 9, "incident-") == 0) {
                                return sim.AssignIncidentCrew(taskId.substr(9));
                              }
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

  BindCommandCooldown(sim, row, row.cooldownKey, row.category);
  return row;
}

void BindCommandCooldown(const Simulation& sim, CommandRowView& row,
                         const std::string& key, const std::string& category) {
  row.cooldownKey = key;
  row.category = category;
  row.remaining = CooldownRemaining(sim, key);
  row.coolingDown = row.remaining > 0.0;
  if (row.coolingDown) {
    row.displayCost = "COOLDOWN " + Num(row.remaining);
    row.title = RuleLabel(sim, sim.State().actionCooldowns.at(key).category) +
        " cooldown: " + Num(row.remaining) +
        " more actions. Other committed actions advance it; waiting does not.";
    row.enabled = false;
    row.live = false;
  }
}

std::string ConversationCooldownKey(const std::string& crewId) {
  return "cd|crew-conversation|" + crewId;
}

CommandRowView BuildConversationRow(const Simulation& sim, const std::string& crewId) {
  const CrewMember* crew = sim.FindCrew(crewId);
  CommandRowView row;
  if (crew == nullptr) { return row; }
  const auto space = crew->name.find(' ');
  row.label = "Talk to " + crew->name.substr(0, space) + " - 1 action, up to +" +
              Num(Simulation::kConversationMorale) + " morale";
  row.cost = row.displayCost = "1 action";
  row.actionCost = true;
  row.enabled = crew->health > 0 && sim.State().gameOver.empty() &&
                sim.FindOngoing("doc-" + crewId) == nullptr;
  row.live = row.enabled;
  if (!row.enabled && sim.FindOngoing("doc-" + crewId) != nullptr) {
    row.title = "Crew member is in the medical bay.";
  }
  BindCommandCooldown(sim, row, ConversationCooldownKey(crewId), "routine");
  return row;
}

bool PressConversation(Simulation& sim, const std::string& crewId) {
  const CommandRowView row = BuildConversationRow(sim, crewId);
  if (!row.enabled) { return false; }
  return CommitActionButton(sim, row.cooldownKey, row.category,
                            [&]() { return sim.TalkToCrew(crewId); });
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
  status.placement = described.acts.empty() ? "np-full" : "";
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
  // A crew tab is a read-only record, and a room tab for the quarters is the
  // order sheet. Neither is a room screen in the machine sense.
  if (tab.kind == "crew") { return ScreenKind::CrewRecord; }
  if (tab.kind == "station") { return ScreenKind::Station; }
  if (tab.kind == "room" && tab.id == "cargo") { return ScreenKind::Cargo; }
  if (tab.kind == "room" && tab.id == "science") { return ScreenKind::Science; }
  if (tab.kind == "room" && tab.id == "quarters") { return ScreenKind::Quarters; }
  // Phase 7: the Simulacron, the fitted bays and the logbook.
  if (tab.kind == "awards") { return ScreenKind::Awards; }
  if (tab.kind == "wiki") { return ScreenKind::Wiki; }
  if (tab.kind == "event") { return ScreenKind::Event; }
  if (tab.kind == "system") { return ScreenKind::System; }
  if (tab.kind == "info") { return ScreenKind::Info; }
  if (tab.kind == "room" && tab.id == "simulacron") { return ScreenKind::SimCore; }
  if (tab.kind != "room") { return ScreenKind::Described; }
  if (tab.id == "engine") { return ScreenKind::Engine; }
  if (tab.id == "o2") { return ScreenKind::O2; }
  if (tab.id == "mess") { return ScreenKind::Mess; }
  // Cargo, science, the fitted systems, the bridge and the quarters each have a
  // window of their own in a later phase; until then they open on the house
  // frame rather than on nothing.

  // The fourteen fitted bays and the bridge: r1..r13 and "bridge", which is
  // exactly the set ROOM_FITS names (the Simulacron left above).
  if (tab.id == "bridge") { return ScreenKind::Room; }
  if (tab.id.size() > 1 && tab.id[0] == 'r') {
    bool digits = true;
    for (std::size_t i = 1; i < tab.id.size(); ++i) {
      if (tab.id[i] < '0' || tab.id[i] > '9') { digits = false; }
    }
    if (digits) {
      const int number = std::stoi(tab.id.substr(1));
      if (number >= 1 && number <= 13) { return ScreenKind::Room; }
    }
  }
  return ScreenKind::Described;
}

TaskScreenModel BuildTabScreen(const Simulation& sim, const Tab& tab,
                               const WikiView& wiki, StationSelection* station) {
  if (!DescribeTargetExists(sim, tab.kind, tab.id)) {
    return BuildDescribedScreen(sim, DescribeTarget(sim, tab.kind, tab.id));
  }
  switch (ScreenKindFor(tab)) {
    case ScreenKind::Nav: return BuildNavScreen(sim).screen;
    case ScreenKind::Quarters: return BuildQuartersScreen(sim).screen;
    case ScreenKind::System: {
      const Stat* found = nullptr;
      for (const Stat& s : sim.Stats()) {
        if (s.id == tab.id) { found = &s; break; }
      }
      if (found == nullptr) { break; }
      const bool useKit = (tab.id == "hull" || tab.id == "shield");
      const std::string label =
          tab.id == "hull" ? "Patch hull (+25%)"
          : tab.id == "shield" ? "Service shields (+25%)"
          : tab.id == "energy" ? "Recharge energy (+20%)"
                               : "Transfer fuel (+15%)";
      const double have = useKit ? sim.State().repairKits : sim.State().fuelCells;

      TaskScreenModel screen;
      screen.head.kind = "SYSTEM";
      screen.head.title = found->label;
      screen.head.sub = "Condition and onboard service";

      NavPanel status;
      status.id = "system-status";
      status.title = "Status";
      status.Field("Reading", Num(found->value) + "%");
      status.Field("Available",
                   Num(have) + (useKit ? " repair kits" : " fuel cells"));
      status.Note(std::string("Service uses one ") +
                  (useKit ? "repair kit" : "fuel cell") +
                  ". Supplies can be bought at a station.");
      screen.panels.push_back(status);

      const bool enabled =
          found->value < 100.0 && have > 0.0 && sim.State().gameOver.empty();
      CommandRowView row = BuildCommandRow(sim, label, enabled, std::string(),
                                           "systems", 0);
      row.live = enabled;
      screen.panelRows.push_back({row});
      screen.panelRowIds.push_back({tab.id});
      return screen;
    }
    case ScreenKind::Info: {
      const InfoRow* info = nullptr;
      for (const InfoRow& candidate : sim.Infos()) {
        if (candidate.id == tab.id) { info = &candidate; break; }
      }
      if (info == nullptr) { break; }
      const Body* sys = sim.CurrentSystem();
      std::string regionName;
      if (sys != nullptr) {
        const Body* region = sim.FindBodyById(sys->region);
        if (region != nullptr) { regionName = region->name; }
      }

      TaskScreenModel screen;
      screen.head.kind = "READOUT";
      screen.head.title = info->label;
      screen.head.sub = "Live ship information";

      NavPanel readout;
      readout.id = "info-readout";
      readout.title = "Readout";
      readout.Field("Reading", info->value);
      if (sys != nullptr) { readout.Field("System", sys->name); }
      if (!regionName.empty()) { readout.Field("Region", regionName); }
      screen.panels.push_back(readout);

      std::vector<CommandRowView> rows;
      std::vector<std::string> ids;
      if (tab.id == "life") {
        CommandRowView o2 =
            BuildCommandRow(sim, "Open O2 Filter Room", true, "free", std::string(), 0);
        o2.live = true;
        rows.push_back(o2);
        ids.push_back("room:o2");
      } else {
        CommandRowView nav =
            BuildCommandRow(sim, "Open Navigation", true, "free", std::string(), 0);
        nav.live = true;
        CommandRowView comms =
            BuildCommandRow(sim, "Open Comms Relay", true, "free", std::string(), 1);
        comms.live = true;
        rows.push_back(nav);
        ids.push_back("nav:main");
        rows.push_back(comms);
        ids.push_back("room:r4");
      }
      screen.panelRows.push_back(rows);
      screen.panelRowIds.push_back(ids);
      return screen;
    }
    case ScreenKind::Station: {
      // Three columns walked in one direction. Nothing here binds a cooldown:
      // the station's own state is what stops a second press.
      StationSelection picked;
      if (station != nullptr) { picked = *station; }
      const StationScreenModel model = BuildStationScreen(sim, tab.id);
      NormalizeStationSelection(model, picked);
      if (station != nullptr) { *station = picked; }

      TaskScreenModel screen;
      screen.head = model.head;
      screen.panelColumns = {0, 1, 2};

      // ---- left: the five category cards, free to open --------------------
      NavPanel cards;
      cards.id = "station-cards";
      cards.title = model.portName;
      cards.Dim("Choose a card, then an item, then commit. Reading and choosing are free.");
      cards.Dim(model.holdLine);
      screen.panels.push_back(cards);
      std::vector<CommandRowView> cardRows;
      std::vector<std::string> cardIds;
      for (const StationColumn& column : model.columns) {
        CommandRowView view;
        view.label = column.title;
        view.title = column.badge;
        view.enabled = true;
        view.live = true;
        view.plain = true;       // the cards are list lines, not buttons
        view.hideCost = true;    // and they carry no badge: choosing is free
        view.selected = column.id == picked.category;
        cardRows.push_back(view);
        cardIds.push_back("station-cat:" + column.id);
      }
      screen.panelRows.push_back(cardRows);
      screen.panelRowIds.push_back(cardIds);

      // ---- middle: only what the open card holds, free to pick ------------
      const StationColumn* open = FindStationCategory(model, picked.category);
      NavPanel items;
      items.id = "station-items";
      items.title = open == nullptr ? model.portName : open->title;
      if (open != nullptr) {
        if (!open->badge.empty()) { items.Dim(open->badge); }
        for (const std::string& note : open->notes) { items.Dim(note); }
      }
      screen.panels.push_back(items);
      std::vector<CommandRowView> itemRows;
      std::vector<std::string> itemIds;
      if (open != nullptr) {
        for (const StationRow& row : open->rows) {
          CommandRowView view;
          view.label = row.label;
          view.reading = row.reading;
          view.enabled = !row.reading;   // picking is free, even for a greyed commit
          view.live = row.enabled;       // a greyed commit reads muted in the list
          view.plain = !row.reading;     // list lines, not buttons
          view.hideCost = true;          // the cost lives in the inspector, not here
          view.selected = !row.reading && row.id == picked.item;
          itemRows.push_back(view);
          itemIds.push_back(row.reading ? std::string() : "station-item:" + row.id);
        }
      }
      screen.panelRows.push_back(itemRows);
      screen.panelRowIds.push_back(itemIds);

      // ---- right: the inspector, and the one commit button ----------------
      const StationRow* item =
          open == nullptr ? nullptr : FindStationItem(model, picked.category, picked.item);
      NavPanel inspector;
      inspector.id = "station-inspector";
      inspector.title = item == nullptr ? "Inspector" : "Inspector and commit";
      if (item == nullptr) {
        inspector.Dim("Pick an item in the middle column to read it and commit.");
      } else {
        inspector.Field("Item", item->label);
        inspector.Field("Cost", item->free ? "Shiplets only" : "One action");
        for (const std::string& line : item->details) { inspector.Note(line); }
      }
      screen.panels.push_back(inspector);
      std::vector<CommandRowView> commitRows;
      std::vector<std::string> commitIds;
      if (item != nullptr) {
        CommandRowView view;
        view.label = item->commit.empty() ? item->label : item->commit;
        view.cost = item->cost;
        view.hideCost = true;            // one simple button, no badge
        view.title = item->label;
        view.confirm = item->confirm;
        view.enabled = item->enabled;
        view.live = item->enabled;
        commitRows.push_back(view);
        commitIds.push_back(item->id);
      }
      screen.panelRows.push_back(commitRows);
      screen.panelRowIds.push_back(commitIds);
      return screen;
    }
    case ScreenKind::Cargo:
    case ScreenKind::Science: {
      const StoresScreenModel stores = BuildStoresScreen(sim, tab.kind == "room" ? tab.id : "cargo");
      TaskScreenModel screen;
      screen.head = stores.head;
      NavPanel panel;
      panel.id = "stores-panel";
      panel.title = stores.cargo ? "Cargo manifest" : "Survey records";
      panel.Note(stores.notice);
      if (!stores.hint.empty()) { panel.Note(stores.hint); }
      panel.Dim(stores.tally);
      screen.panels.push_back(panel);
      std::vector<CommandRowView> rows;
      std::vector<std::string> ids;
      for (const StoreRowView& row : stores.rows) {
        CommandRowView view;
        view.label = row.label;
        view.cost = row.cost;
        view.displayCost = row.cost.empty() ? "1 action" : row.cost;
        view.enabled = row.enabled;
        view.live = row.enabled;
        view.confirm = row.confirm;
        BindCommandCooldown(sim, view, ActionCooldownKey(sim, "press:stores:" + row.id + ":"),
                            ActionCooldownCategory(row.id + " "));
        rows.push_back(view);
        ids.push_back(row.id);
      }
      screen.panelRows.push_back(rows);
      screen.panelRowIds.push_back(ids);
      screen.progress = stores.buttons;
      screen.hasStage = false;
      return screen;
    }
    case ScreenKind::Room: {
      const RoomScreenModel room = BuildRoomScreen(sim, tab.id);
      TaskScreenModel screen;
      screen.head = room.head;
      screen.panels.push_back(room.body);
      std::vector<CommandRowView> rows;
      std::vector<std::string> ids;
      for (const ActionRowView& row : room.rows) {
        CommandRowView view;
        view.label = row.label;
        view.cost = row.cost;
        view.displayCost = row.cost.empty() ? "1 action" : row.cost;
        view.enabled = row.enabled;
        view.live = row.enabled;
        view.confirm = row.confirm;
        BindCommandCooldown(sim, view, ActionCooldownKey(sim, "press:room:" + row.id + ":"),
                            RoomActionCategory(row.id));
        rows.push_back(view);
        ids.push_back(row.id);
      }
      screen.panelRows.push_back(rows);
      screen.panelRowIds.push_back(ids);
      screen.progress = room.buttons;
      screen.hero = BuildVignette(sim, tab.id);
      screen.hasHero = true;
      return screen;
    }
    case ScreenKind::SimCore: {
      const SimScreenModel core = BuildSimCoreScreen(sim);
      TaskScreenModel screen;
      screen.head = core.head;
      NavPanel panel = core.body;
      for (const std::string& line : core.anchors) { panel.Note(line); }
      if (!core.latestDream.empty()) { panel.Note(core.latestDream); }
      panel.Sub("Memory mosaic - newest first");
      for (const std::string& line : core.memories) { panel.Note(line); }
      screen.panels.push_back(panel);
      std::vector<CommandRowView> rows;
      std::vector<std::string> ids;
      for (const ActionRowView& row : core.rows) {
        CommandRowView view;
        view.label = row.label;
        view.cost = row.cost;
        view.displayCost = row.cost.empty() ? "1 action" : row.cost;
        view.enabled = row.enabled;
        view.live = row.enabled;
        view.confirm = row.confirm;
        BindCommandCooldown(sim, view, ActionCooldownKey(sim, "press:simcore:" + row.id + ":"),
                            ActionCooldownCategory(row.id + " "));
        rows.push_back(view);
        ids.push_back(row.id);
      }
      screen.panelRows.push_back(rows);
      screen.panelRowIds.push_back(ids);
      screen.progress = core.buttons;
      screen.hero = BuildVignette(sim, "simulacron");
      screen.hasHero = true;
      return screen;
    }
    case ScreenKind::Wiki: {
      const WikiModel page = BuildWikiScreen(sim, wiki);
      TaskScreenModel screen;
      screen.head = page.head;
      NavPanel index;
      index.id = "wiki-index";
      index.title = "Index";
      if (!page.emptyNote.empty()) { index.Dim(page.emptyNote); }
      screen.panels.push_back(index);

      NavPanel article;
      article.id = "wiki-article";
      article.title = page.group.empty() ? "Article" : page.group;
      article.Sub(page.title);
      article.Note(page.body);
      screen.panels.push_back(article);

      std::vector<CommandRowView> indexRows;
      std::vector<std::string> indexIds;
      for (const WikiIndexEntry& entry : page.index) {
        CommandRowView view;
        view.label = entry.group.empty() ? entry.entry->title
                                         : entry.group + " - " + entry.entry->title;
        view.cost = entry.selected ? "reading" : "free";
        view.displayCost = view.cost;
        view.enabled = true;
        view.live = true;
        indexRows.push_back(view);
        indexIds.push_back("entry:" + entry.entry->id);
      }
      screen.panelRows.push_back(indexRows);
      screen.panelRowIds.push_back(indexIds);

      std::vector<CommandRowView> articleRows;
      std::vector<std::string> articleIds;
      for (const ActionRowView& row : page.rows) {
        CommandRowView view;
        view.label = row.label;
        view.cost = row.cost;
        view.displayCost = row.cost.empty() ? "free" : row.cost;
        view.enabled = row.enabled;
        view.live = row.enabled;
        articleRows.push_back(view);
        articleIds.push_back(row.id);
      }
      screen.panelRows.push_back(articleRows);
      screen.panelRowIds.push_back(articleIds);
      return screen;
    }
    case ScreenKind::Awards: {
      const AwardsScreenModel awards = BuildAwardsScreen(sim);
      TaskScreenModel screen;
      screen.head = awards.head;
      screen.panels.push_back(awards.summary);
      std::vector<CommandRowView> rows;
      std::vector<std::string> ids;
      for (const ActionRowView& row : awards.rows) {
        CommandRowView view;
        view.label = row.label;
        view.cost = row.cost;
        view.displayCost = row.cost.empty() ? "free" : row.cost;
        view.enabled = row.enabled;
        view.live = row.enabled;
        view.confirm = row.confirm;
        BindCommandCooldown(sim, view, ActionCooldownKey(sim, "press:awards:" + row.id + ":"),
                            ActionCooldownCategory(row.id + " "));
        rows.push_back(view);
        ids.push_back(row.id);
      }
      screen.panelRows.push_back(rows);
      screen.panelRowIds.push_back(ids);
      return screen;
    }
    case ScreenKind::Event: {
      const EventScreenModel event = BuildEventScreen(sim, tab.id);
      TaskScreenModel screen;
      screen.head = event.head;
      screen.panels.push_back(event.report);
      std::vector<CommandRowView> rows;
      std::vector<std::string> ids;
      for (const ActionRowView& row : event.rows) {
        CommandRowView view;
        view.label = row.label;
        view.cost = row.cost;
        view.displayCost = row.cost.empty() ? "1 action" : row.cost;
        view.enabled = row.enabled;
        view.live = row.enabled;
        view.confirm = row.confirm;
        BindCommandCooldown(sim, view, ActionCooldownKey(sim, "press:event:" + row.id + ":"),
                            "systems");
        rows.push_back(view);
        ids.push_back(row.id);
      }
      screen.panelRows.push_back(rows);
      screen.panelRowIds.push_back(ids);
      screen.progress = event.buttons;
      return screen;
    }
    case ScreenKind::CrewRecord: {
      // Reading the record is free; its conversation is an explicit action.
      const CrewRecordModel record = BuildCrewRecord(sim, tab.id);
      TaskScreenModel screen;
      screen.head = record.head;
      screen.left = record.shelves;
      if (!record.crewId.empty()) { screen.commands.push_back(record.conversation); }
      return screen;
    }
    case ScreenKind::Engine: return BuildEngineScreen(sim).screen;
    case ScreenKind::O2: return BuildO2Screen(sim).screen;
    case ScreenKind::Mess: return BuildMessScreen(sim).screen;
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

bool PressSystemAction(Simulation& sim, const std::string& statId) {
  std::string label;
  if (statId == "hull") {
    label = "Patch hull (+25%)";
  } else if (statId == "shield") {
    label = "Service shields (+25%)";
  } else if (statId == "energy") {
    label = "Recharge energy (+20%)";
  } else if (statId == "fuel") {
    label = "Transfer fuel (+15%)";
  } else {
    return false;
  }
  const CommandRowView row =
      BuildCommandRow(sim, label, true, std::string(), "systems", 0);
  return CommitActionButton(sim, row.cooldownKey, row.category,
                            [&sim, &statId]() { return sim.ServiceSystem(statId); });
}

bool PressInfoAction(Simulation& sim, const std::string& target) {
  const std::size_t cut = target.find(':');
  if (cut == std::string::npos) { return false; }
  return OpenTask(sim, target.substr(0, cut), target.substr(cut + 1), true);
}

}  // namespace ship
