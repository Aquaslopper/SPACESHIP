// ============================================================================
//  Shell.cpp — tabs, sidebar, foot, status line.
//  ---------------------------------------------------------------------------
//  Faithful to Section H (tab system), Section J (sidebar) and Section L
//  (status bar). The UI-only state kept here is the same state the reference
//  saves with the voyage: which tabs are open, which one is on screen, which
//  ones still owe the player a cream pulse, and which sidebar blocks are folded
//  open. Nothing else is retained between paints - the numbers are read fresh
//  from the voyage every time, which is what stops a tab showing a stale ship.
// ============================================================================
#include "ui/Shell.h"

#include "ui/Describe.h"

#include <algorithm>

#include "core/JsNumber.h"

namespace ship {

namespace {
std::string Num(double value) { return js::NumToString(value); }

std::string ShortRole(const std::string& role) {
  const std::size_t cut = role.find(" /");
  return cut != std::string::npos && cut > 0 ? role.substr(0, cut) : role;
}
}  // namespace

// ---------------------------------------------------------------------------
//  §H — the tab system.
// ---------------------------------------------------------------------------
std::string TaskKey(const std::string& kind, const std::string& id) {
  return kind + ":" + id;
}

const Tab* FindTab(const GameState& state, const std::string& key) {
  for (const Tab& tab : state.tabs) {
    if (tab.key == key) { return &tab; }
  }
  return nullptr;
}

std::string HomeTabTitle(const Simulation& sim) {
  return sim.Ship().Get("name") + " (Idle)";
}

namespace {

// Re-ask describeTarget every time a screen is built. Nav describes itself live
// and is left alone; a window whose subject has left the ship keeps its key and
// gains the GONE description.
void RefreshInPlace(Simulation& sim, Tab& tab) {
  if (tab.kind == "nav") { return; }
  const Description fresh = DescribeTarget(sim, tab.kind, tab.id);
  tab.title = fresh.title;
  tab.kindLabel = fresh.kindLabel;
  tab.sub = fresh.sub;
}

}  // namespace

bool RefreshTab(Simulation& sim, const std::string& key) {
  for (Tab& tab : sim.MutableState().tabs) {
    if (tab.key == key) {
      RefreshInPlace(sim, tab);
      return true;
    }
  }
  return false;
}

bool OpenTask(Simulation& sim, const std::string& kind, const std::string& id,
              bool focus) {
  const std::string key = TaskKey(kind, id);
  GameState& state = sim.MutableState();
  if (FindTab(state, key) == nullptr) {
    const Description described = DescribeTarget(sim, kind, id);
    Tab tab;
    tab.key = key;
    tab.id = id;
    tab.kind = kind;
    tab.title = described.title;
    tab.kindLabel = described.kindLabel;
    tab.sub = described.sub;
    state.tabs.push_back(tab);
    state.freshTabs[key] = true;
  }
  if (focus) { return SwitchTab(sim, key); }
  return true;  // opened in the background: the screen does not change
}

bool SwitchTab(Simulation& sim, const std::string& key) {
  GameState& state = sim.MutableState();
  if (key != "home" && FindTab(state, key) == nullptr) { return false; }
  state.activeTab = key;
  if (key != "home") { RefreshTab(sim, key); }
  return true;
}

bool CloseTab(Simulation& sim, const std::string& key) {
  GameState& state = sim.MutableState();
  std::vector<Tab> keep;
  keep.reserve(state.tabs.size());
  for (const Tab& tab : state.tabs) {
    if (tab.key != key) { keep.push_back(tab); }
  }
  if (keep.size() == state.tabs.size()) { return false; }
  state.tabs = keep;
  if (state.activeTab == key) { return SwitchTab(sim, "home"); }
  return true;
}

std::vector<TabView> TabBar(Simulation& sim) {
  GameState& state = sim.MutableState();
  std::vector<TabView> out;
  out.reserve(state.tabs.size() + 1);

  TabView home;
  home.key = "home";
  home.title = HomeTabTitle(sim);
  home.kindLabel = "SHIP";
  home.home = true;
  home.active = state.activeTab == "home";
  out.push_back(home);

  for (const Tab& tab : state.tabs) {
    TabView view;
    view.key = tab.key;
    view.title = tab.title;
    view.kindLabel = tab.kindLabel;
    view.active = state.activeTab == tab.key;
    view.closable = true;
    auto fresh = state.freshTabs.find(tab.key);
    if (fresh != state.freshTabs.end() && fresh->second) {
      view.fresh = true;
      // makeTab consumes the pulse the moment the tab is drawn.
      fresh->second = false;
    }
    out.push_back(view);
  }
  return out;
}

// ---------------------------------------------------------------------------
//  §L — the status bar.
// ---------------------------------------------------------------------------
std::string StatusText(const Simulation& sim) {
  const GameState& state = sim.State();
  std::string location;
  for (const InfoRow& info : sim.Infos()) {
    if (info.id == "location") { location = info.value; }
  }
  std::string text = sim.Ship().Get("name") + " - " + location + " | Actions " +
                     Num(sim.ActionCount()) + " | Open tasks " +
                     Num(static_cast<double>(state.tabs.size()));
  if (!state.feed.empty()) {
    text += " | " + state.feed.back().text;
  }
  return text;
}

// ---------------------------------------------------------------------------
//  §D — the activity feed.
// ---------------------------------------------------------------------------
std::vector<std::string> FeedLinesNewestFirst(const Simulation& sim,
                                             std::size_t limit) {
  const std::vector<FeedLine>& feed = sim.State().feed;
  const std::size_t shown = std::min(feed.size(), limit);
  std::vector<std::string> out;
  out.reserve(shown);
  for (std::size_t i = 0; i < shown; ++i) {
    out.push_back(feed[feed.size() - 1 - i].text);
  }
  return out;
}

// ---------------------------------------------------------------------------
//  §J — the sidebar.
// ---------------------------------------------------------------------------
std::vector<SideBlock> SideBlocks(const Simulation& sim) {
  const GameState& state = sim.State();
  std::vector<SideBlock> blocks;

  const ShipDef& ship = sim.Ship();
  auto open = [&state](const std::string& key) {
    auto it = state.spOpen.find(key);
    return it != state.spOpen.end() && it->second;
  };

  SideBlock vessel;
  vessel.key = "vessel";
  vessel.title = "Vessel";
  vessel.open = open("vessel");
  vessel.rows.push_back({"Built", ship.Get("built"), true, false, "pair", ""});
  vessel.rows.push_back({"Yard", ship.Get("yard"), true, false, "long", ""});
  vessel.rows.push_back({"Refit", ship.Get("refit"), true, false, "long", ""});
  vessel.rows.push_back({"Drive", ship.Get("drive"), true, false, "long", ""});
  vessel.rows.push_back({"Length", ship.Get("length"), true, false, "pair", ""});
  vessel.rows.push_back({"Beam", ship.Get("beam"), true, false, "pair", ""});
  vessel.rows.push_back({"Height", ship.Get("height"), true, false, "pair", ""});
  vessel.rows.push_back({"Mass", ship.Get("mass"), true, false, "pair", ""});
  vessel.rows.push_back({"Decks", ship.Get("decks"), true, false, "pair", ""});
  vessel.rows.push_back({"Berths", ship.Get("berths"), true, false, "pair", ""});
  vessel.rows.push_back(
      {"Rooms", Num(static_cast<double>(sim.Rooms().size())) + " aboard", true, false,
       "pair", ""});
  blocks.push_back(vessel);

  SideBlock resources;
  resources.key = "resources";
  resources.title = "Resources";
  resources.open = open("resources");
  const std::vector<std::pair<std::string, std::string>> res = {
      {"Shiplets", Num(state.shiplets)},
      {"Fuel cells", Num(state.fuelCells)},
      {"Rations", Num(sim.MessRack())},
      {"Repair kits", Num(state.repairKits)},
      {"Medkits", Num(state.medkits)},
      {"O2 tanks", Num(sim.Life().canisters)},
      {"Goods", Num(sim.CargoUsed()) + " m3"},
      {"Survey data", Num(static_cast<double>(sim.Survey().size())) + " logs"},
  };
  for (const auto& row : res) {
    resources.rows.push_back({row.first, row.second, false, true, "pair", ""});
  }
  blocks.push_back(resources);

  SideBlock crew;
  crew.key = "crew";
  crew.title = "Crew";
  crew.open = open("crew");
  for (const CrewMember& member : sim.Crew()) {
    crew.rows.push_back({member.name, ShortRole(member.role), true, false, "pair", ""});
  }
  if (!state.passengers.empty()) {
    crew.rows.push_back({"Passengers", Num(static_cast<double>(state.passengers.size())),
                         true, false, "pair", ""});
  }
  blocks.push_back(crew);

  SideBlock contracts;
  contracts.key = "contracts";
  contracts.title = "Contracts";
  contracts.open = open("contracts");
  contracts.empty = "No active contracts.";
  for (const Contract& contract : sim.World().contracts) {
    auto it = state.contracts.find(contract.id);
    if (it == state.contracts.end()) { continue; }
    if (it->second != "active" && it->second != "ready") { continue; }
    contracts.rows.push_back({contract.title,
                              contract.note +
                                  (it->second == "ready" ? " Ready to collect." : ""),
                              true, false, "long", ""});
  }
  if (state.commissions.active.has_value()) {
    const Commission& active = *state.commissions.active;
    const double goal = CommissionGoal(active.kind);
    std::string line = CommissionDescription(sim, active) + " - " + Num(active.progress) +
                       " / " + Num(goal);
    line += active.progress >= goal ? ". Ready to collect at a station." : ".";
    contracts.rows.push_back({"Commission", line, true, false, "long", ""});
  }
  blocks.push_back(contracts);

  SideBlock events;
  events.key = "events";
  events.title = "Events";
  events.open = open("events");
  events.empty = "Nothing reported.";
  for (const Incident& event : sim.Events()) {
    events.rows.push_back({event.kind, "", false, false, "event", event.id});
  }
  blocks.push_back(events);

  SideBlock indicators;
  indicators.key = "indicators";
  indicators.title = "Indicators";
  indicators.indicators = true;
  indicators.open = true;
  blocks.push_back(indicators);
  return blocks;
}

std::vector<IndicatorLine> IndicatorLines(const Simulation& sim) {
  std::vector<IndicatorLine> out;
  const GameState& state = sim.State();

  // The engine warning: silent above the threshold, then a red line climbing as
  // the condition falls, so the worse it gets the harder it is to miss.
  const double warnBelow = sim.GameRules().engine.warnBelow;
  if (state.engineCondition < warnBelow) {
    const double pct = js::Round(((warnBelow - state.engineCondition) / warnBelow) * 100.0);
    out.push_back({static_cast<int>(pct), true});
  }

  // The air, as a second red line when the bank is not holding the top step.
  const int stage = sim.AirStageIndex();
  if (stage < 4) {
    out.push_back({static_cast<int>(js::Round(((4.0 - stage) / 4.0) * 100.0)), true});
  }

  for (const Task& task : state.ongoing) {
    int pct = task.total > 0.0 ? static_cast<int>(js::Round((task.left / task.total) * 100.0)) : 0;
    if (task.state != "running") { pct = 0; }
    out.push_back({pct, false});
  }

  for (const Incident& event : sim.Events()) {
    const IncidentType* spec = nullptr;
    for (const IncidentType& type : sim.World().incidentTypes) {
      if (type.id == event.type) { spec = &type; }
    }
    if (spec == nullptr || spec->stages.empty()) { continue; }
    const double count = static_cast<double>(spec->stages.size());
    const IncidentType::Stage& stage = spec->stages[std::min<std::size_t>(
        static_cast<std::size_t>(event.stage), spec->stages.size() - 1)];
    const double ticks = stage.ticks;
    const double done = ticks > 0.0 ? (ticks - event.left) / ticks : 1.0;
    const double pct = js::Round(100.0 * ((event.stage + done) / (count - 1.0)));
    out.push_back({static_cast<int>(pct), true});
  }
  return out;
}

std::vector<SideHit> SideHits(const Simulation& sim, const Area& sidepanel,
                              const Area& foot, float lineHeight,
                              float scrollY, float* totalContentHeight) {
  std::vector<SideHit> hits;
  const float top = sidepanel.y;
  const float bottom = sidepanel.Bottom();
  float cursor = 8.0f;

  const auto pushInPanel = [&](SideHit hit, float rawTop, float h) {
    const float y = top + rawTop - scrollY;
    // Keep every emitted box strictly inside sidepanel [top, bottom) so folded
    // or scrolled rows never spill over the taskbar or the pinned foot.
    if (y + h <= top + 1.0f || y >= bottom - 1.0f) { return; }
    const float clippedTop = std::max(top, y);
    const float clippedBottom = std::min(bottom - 0.5f, y + h);
    if (clippedBottom <= clippedTop + 2.0f) { return; }
    hit.box = Area{hit.box.x, clippedTop, hit.box.w, clippedBottom - clippedTop};
    hits.push_back(std::move(hit));
  };

  for (const SideBlock& block : SideBlocks(sim)) {
    const float headH = lineHeight + 3.0f;
    SideHit head;
    head.action = SideAction::Toggle;
    head.key = block.key;
    head.text = block.title;
    head.box = Area{sidepanel.x + 6.0f, 0.0f, sidepanel.w - 12.0f, headH};
    head.header = true;
    pushInPanel(head, cursor, headH);
    cursor += headH;

    if (block.indicators) {
      cursor += Metrics::kIndH + 6.0f;   // the strip is drawn, never clicked
      continue;
    }
    if (!block.open) { continue; }
    if (block.rows.empty() && !block.empty.empty()) {
      SideHit dim;
      dim.text = block.empty;
      dim.dim = true;
      dim.box = Area{sidepanel.x + 12.0f, 0.0f, sidepanel.w - 18.0f, lineHeight};
      pushInPanel(dim, cursor, lineHeight);
      cursor += lineHeight;
      continue;
    }
    for (const SideRow& row : block.rows) {
      float height = lineHeight;
      // A long value gets its own line beneath its label.
      if (row.kind == "long" || row.kind == "event") { height += 14.0f; }
      SideHit hit;
      hit.text = row.key;
      hit.detail = row.kind == "long" ? row.value : std::string();
      hit.rightValue = row.kind == "pair" ? row.value : std::string();
      hit.dim = row.dim;
      hit.box = Area{sidepanel.x + 12.0f, 0.0f, sidepanel.w - 18.0f, height};
      if (row.kind == "event") {
        hit.action = SideAction::OpenEvent;
        hit.key = row.id;
      }
      pushInPanel(hit, cursor, height);
      cursor += height;
    }
  }
  if (totalContentHeight != nullptr) { *totalContentHeight = cursor + 8.0f; }

  // The pinned foot: four ship functions, then the two reference windows.
  // Always emitted regardless of how many sidebar blocks are unfolded above.
  const std::vector<FootButton> buttons = FootButtons();
  float fx = foot.x + std::max(0.0f, (foot.w - 128.0f) * 0.5f);
  const SideAction actions[4] = {SideAction::Manifest, SideAction::Log,
                                 SideAction::Files, SideAction::Settings};
  for (std::size_t i = 0; i < buttons.size() && i < 4; ++i) {
    SideHit hit;
    hit.action = actions[i];
    hit.key = buttons[i].key;
    hit.text = buttons[i].title;
    hit.box = Area{fx, foot.y + 8.0f, 32.0f, 28.0f};
    hits.push_back(hit);
    fx += 32.0f;
  }

  SideHit wiki;
  wiki.action = SideAction::Wiki;
  wiki.key = "wiki";
  wiki.text = "Wiki";
  wiki.box = Area{foot.x + 8.0f, foot.y + 42.0f, 52.0f, 24.0f};
  hits.push_back(wiki);

  SideHit rewards;
  rewards.action = SideAction::Rewards;
  wiki.dim = false;
  rewards.key = "awards";
  rewards.text = RewardsLabel(sim);
  rewards.box = Area{foot.x + 64.0f, foot.y + 42.0f, 93.0f, 24.0f};
  hits.push_back(rewards);
  return hits;
}

// ---------------------------------------------------------------------------
//  The pinned foot.
// ---------------------------------------------------------------------------
std::vector<FootButton> FootButtons() {
  return {
      {"manifest", "Ship Manifest"},
      {"log", "Activity Log"},
      {"files", "Save / Load Game"},
      {"settings", "Settings"},
  };
}

int ReadyCommendations(const Simulation& sim) {
  int ready = 0;
  for (const Commendation& commendation : sim.World().commendations) {
    const auto earned = sim.State().chronicle.earned.find(commendation.id);
    const auto claimed = sim.State().chronicle.claimed.find(commendation.id);
    const bool has = earned != sim.State().chronicle.earned.end() && earned->second;
    const bool took = claimed != sim.State().chronicle.claimed.end() && claimed->second;
    if (has && !took) { ++ready; }
  }
  return ready;
}

std::string RewardsLabel(const Simulation& sim) {
  const int ready = ReadyCommendations(sim);
  return "Rewards" + (ready > 0 ? " (" + Num(ready) + ")" : std::string());
}

std::string CaptainRank(const Simulation& sim) {
  int owned = 0;
  for (const auto& entry : sim.State().chronicle.earned) {
    if (entry.second) { ++owned; }
  }
  const std::size_t total = sim.World().commendations.size();
  if (static_cast<std::size_t>(owned) >= total) { return "Constellation"; }
  if (owned >= 10) { return "Voyager"; }
  if (owned >= 6) { return "Wayfinder"; }
  if (owned >= 3) { return "Keeper"; }
  return "New Watch";
}

}  // namespace ship
