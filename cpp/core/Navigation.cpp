// ============================================================================
//  Navigation.cpp — the chart, the crossing and the sensors.
//  ---------------------------------------------------------------------------
//  Looking is free. Every scale of the chart, every pan, every selection and
//  every readout of a body's name, class and distance costs nothing. Two things
//  cost: plotting a crossing (one action now, then the distance in actions spent
//  elsewhere) and asking the sensors for more than the chart already shows.
//
//  Distances are DERIVED, never authored. They are measured from MAP.here at the
//  moment of the question, which is why the ship can relocate on arrival without
//  invalidating a single route.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace ship {

namespace {

std::string Plur(double n, const std::string& word) {
  return js::NumToString(n) + " " + word + (n == 1 ? "" : "s");
}

std::uint64_t MapEventHash(const std::string& key) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : key) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    h *= 0x100000001b3ULL;
  }
  return h;
}

std::string DetailValue(const Body& body, const std::string& label) {
  for (const auto& entry : body.details) {
    if (entry.first == label) { return entry.second; }
  }
  return std::string();
}

bool IsProceduralId(const std::string& id) {
  return id.rfind("sec-gen-", 0) == 0 || id.rfind("gal-gen-", 0) == 0 ||
         id.rfind("gen-", 0) == 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// The chart's own questions
// ---------------------------------------------------------------------------

const Body* Simulation::FindBodyById(const std::string& id) const {
  if (const Body* body = catalog_.FindBody(id)) { return body; }
  if (const Body* system = catalog_.FindSystem(id)) { return system; }
  if (const Body* region = catalog_.FindRegion(id)) { return region; }
  return nullptr;
}

std::string Simulation::CurrentSystemId() const {
  const Body* here = FindBodyById(map_.here);
  return here == nullptr ? std::string("sec-kessler") : catalog_.BodySystem(*here);
}

const Body* Simulation::CurrentSystem() const { return FindBodyById(CurrentSystemId()); }

bool Simulation::InRange(const Body& body) const {
  if (body.type == "region") { return false; }
  if (body.type == "system") { return !body.entry.empty(); }
  return catalog_.BodySystem(body) == CurrentSystemId();
}

int Simulation::SensorTier() const {
  auto has = [this](const char* key) {
    auto it = state_.upgrades.find(key);
    return it != state_.upgrades.end() && it->second;
  };
  if (has("scanner2")) { return 2; }
  if (has("scanner")) { return 1; }
  return 0;
}

bool Simulation::CanScan(const Body& body) const {
  const int tier = SensorTier();
  const std::string curSysId = CurrentSystemId();
  if (body.type != "region" && body.type != "system" && catalog_.BodySystem(body) == curSysId) {
    return true;
  }
  if (tier <= 0) { return false; }

  const Body* curSys = CurrentSystem();
  const std::string curRegion = curSys == nullptr ? std::string() : curSys->region;
  if (tier == 1) {
    if (body.type == "region") { return false; }
    if (body.type == "system") { return !curRegion.empty() && body.region == curRegion; }
    const Body* ownerSys = catalog_.FindSystem(catalog_.BodySystem(body));
    return ownerSys != nullptr && !curRegion.empty() && ownerSys->region == curRegion;
  }
  // Tier 2 deep-array sensors reach systems, regions, and charted bodies across outer sectors.
  return true;
}

double Simulation::EffectiveScanCost(const Body& body) const {
  const double base = std::max(1.0, scanCost_ - (state_.sim.sensor ? 2.0 : 0.0));
  const std::string curSysId = CurrentSystemId();
  if (body.type != "region" && body.type != "system" && catalog_.BodySystem(body) == curSysId) {
    return base;
  }
  const Body* curSys = CurrentSystem();
  const std::string curRegion = curSys == nullptr ? std::string() : curSys->region;
  std::string targetRegion = body.region;
  if (body.type == "region") {
    targetRegion = body.id;
  } else if (body.type != "system") {
    const Body* ownerSys = catalog_.FindSystem(catalog_.BodySystem(body));
    if (ownerSys != nullptr) { targetRegion = ownerSys->region; }
  }
  if (!curRegion.empty() && targetRegion == curRegion && body.type != "region") {
    return base + 1.0;
  }
  return base + 2.0;
}

namespace {

bool IsGasBody(const Body& body) {
  return body.type == "planet" &&
         (body.subtype == "gas" || body.klass.find("Gas") != std::string::npos ||
          body.klass.find("gas") != std::string::npos);
}

bool IsVolatileIceBody(const Body& body) {
  return body.subtype == "ice" || body.subtype == "ocean" ||
         body.klass.find("Ice") != std::string::npos ||
         body.klass.find("ice") != std::string::npos ||
         body.klass.find("Ocean") != std::string::npos;
}

}  // namespace

bool Simulation::CanExtractBody(const Body& body) const {
  return body.type == "planet" || body.type == "moon" ||
         body.type == "belt" || body.type == "site";
}

int Simulation::ExtractionMaxPasses(const Body& body) const {
  return CanExtractBody(body) ? 1 : 0;
}

int Simulation::ExtractionPassesUsed(const Body& body) const {
  for (int i = 1; i <= 3; ++i) {
    const std::string key = "extract-" + std::to_string(i) + ":" + body.id;
    auto it = state_.sim.observed.find(key);
    if (it != state_.sim.observed.end() && it->second) { return 1; }
  }
  return 0;
}

std::string Simulation::ExtractionScanSummary(const Body& body) const {
  if (!CanExtractBody(body)) { return std::string(); }
  if (ExtractionPassesUsed(body) >= ExtractionMaxPasses(body)) {
    return "Lootable: Depleted (already looted).";
  }
  const std::uint64_t h = MapEventHash(body.id + "#extract#1");
  const bool jackpot = (h % 5ULL) == 0ULL;
  if (IsGasBody(body)) {
    return jackpot
               ? "Lootable: Helium-3 pocket (+20% fuel, 2 fuel cells, 35 sh condensate)."
               : "Lootable: Upper-atmosphere volatiles (+14% fuel, 1 fuel cell).";
  }
  if (body.type == "belt") {
    return jackpot
               ? "Lootable: 3 m3 Belt Salvage, 1 repair kit, and 40 sh transponder cores."
               : "Lootable: 3 m3 Belt Salvage.";
  }
  if (body.type == "site") {
    const std::uint64_t pick = (h + 1ULL) % 4ULL;
    if (pick == 0ULL) { return "Lootable: 3 m3 Derelict Salvage and 1 field repair kit."; }
    if (pick == 1ULL) { return "Lootable: 2 m3 Recovered Components."; }
    if (pick == 2ULL) { return "Lootable: 2 m3 Sealed Relic Cask (+1 insight)."; }
    return "Lootable: 2 m3 Unregistered Strongbox and 55 sh chits.";
  }
  if (IsVolatileIceBody(body)) {
    return jackpot
               ? "Lootable: 2 m3 Subglacial Relic Core and up to 2 dry rations."
               : "Lootable: 4 m3 Cryo-Volatile Ice (Water).";
  }
  if (jackpot) {
    return body.type == "moon"
               ? "Lootable: 3 m3 Rare-Earth Seam (Components) and 1 He-3 fuel cell."
               : "Lootable: 3 m3 Rare-Earth Seam (Components).";
  }
  return body.type == "moon"
             ? "Lootable: 4 m3 Lunar Regolith Ore (Bulk metal)."
             : "Lootable: 4 m3 Planetary Ore (Bulk metal).";
}

double Simulation::ExtractionReserveVol(const Body& body) const {
  if (!CanExtractBody(body) || IsGasBody(body)) { return 0.0; }
  if (body.type == "belt" || body.type == "site") { return 3.0; }
  return 4.0;
}

double Simulation::ExtractionWorkCost(const Body& body) const {
  if (IsGasBody(body) || body.type == "belt") { return 2.0; }
  return 3.0;
}

std::string Simulation::ExtractionActionLabel(const Body& body) const {
  if (IsGasBody(body)) { return "Skim upper atmosphere"; }
  if (body.type == "planet") {
    return IsVolatileIceBody(body) ? "Harvest volatile ice" : "Extract planetary ore";
  }
  if (body.type == "moon") {
    return IsVolatileIceBody(body) ? "Mine lunar ice & ore" : "Mine lunar regolith";
  }
  if (body.type == "belt") { return "Loot debris field"; }
  return "Loot derelict site";
}

std::string Simulation::ExtractionDoneLabel(const Body& body) const {
  if (IsGasBody(body)) { return "Condense Skimmed Fuel"; }
  if (body.type == "belt") { return "Haul Debris Salvage"; }
  if (body.type == "site") { return "Haul Site Loot"; }
  if (body.type == "moon") { return "Haul Lunar Yield"; }
  return "Haul Planetary Ore";
}

bool Simulation::AtTarget(const Body& body) const {
  if (body.type == "system") { return body.id == CurrentSystemId(); }
  return body.id == map_.here;
}

const Body* Simulation::HomeBody() const {
  // Position at the current map scale, not the region being browsed. The
  // sector chart paints every system, so the ship's own system is always on it
  // whether or not it belongs to the region being read.
  const Body* system = CurrentSystem();
  if (map_.layer == "system") { return FindBodyById(map_.here); }
  if (map_.layer == "sector") { return system; }
  return system == nullptr ? nullptr : FindBodyById(system->region);
}

void Simulation::ApplyChartBoard() {
  const MapGeometry& authored = catalog_.map;
  auto it = catalog_.layerBoards.find(map_.layer);
  const MapGeometry& board = it == catalog_.layerBoards.end() ? authored : it->second;
  map_.w = board.w;
  map_.h = board.h;
  map_.cx = board.cx;
  map_.cy = board.cy;
  // The window is the board unless the scale states one. A sector board ten times
  // the reference has to be read through a window, or enlarging it would change
  // nothing but the numbers.
  map_.vw = board.vw > 0.0 ? board.vw : board.w;
  map_.vh = board.vh > 0.0 ? board.vh : board.h;
  map_.zmin = board.zmin;
  map_.zmax = board.zmax;
  map_.zstep = board.zstep;
  map_.grid = board.grid;
  map_.vpy = board.vpy;
  map_.rungs = board.rungs;
  map_.rungPow = board.rungPow;
  map_.pxPerAction = board.pxPerAction;
  map_.lyPerAction = board.lyPerAction;
}

std::vector<const Body*> Simulation::CurBodies() const {
  std::vector<const Body*> out;
  if (map_.layer == "galaxy") {
    out.reserve(catalog_.regions.size());
    for (const Body& body : catalog_.regions) { out.push_back(&body); }
    return out;
  }
  if (map_.layer == "system") {
    const auto& indices = catalog_.BodiesInSystem(CurrentSystemId());
    out.reserve(indices.size());
    for (std::size_t idx : indices) {
      if (idx < catalog_.bodies.size()) { out.push_back(&catalog_.bodies[idx]); }
    }
    return out;
  }
  out.reserve(catalog_.systems.size());
  for (const Body& body : catalog_.systems) { out.push_back(&body); }
  return out;
}

void Simulation::SetLayer(const std::string& key) {
  if (key == map_.layer) { return; }
  if (key != "system" && key != "sector" && key != "galaxy") { return; }

  // Each layer keeps its own zoom, pan and selection, so coming back finds the
  // map where it was left.
  LayerView& old = views_[map_.layer];
  old.zoom = map_.zoom;
  old.sel = state_.navTarget.value_or(std::string());

  map_.layer = key;
  ApplyChartBoard();
  LayerView& fresh = views_[key];
  map_.zoom = fresh.zoom;
  state_.navTarget = fresh.sel.empty() ? std::optional<std::string>() : std::optional<std::string>(fresh.sel);
  // The drifting rocks and the station beacon belong to the system view; the
  // client starts and stops that animation from here.
}

bool Simulation::ZoomMap(int dir) {
  // Free. One step on the ladder, rounded to a tenth exactly as the reference
  // rounds it, so the caption and the raster always agree.
  //
  // The wheel is the only scale control: reaching the bottom of a scale's ladder
  // while zooming out steps out to the wider chart, and reaching the top while
  // zooming in steps in. The ship does not move - zooming is looking, and travel
  // is travel.
  if (dir == 0) { return false; }

  const bool atWidest = map_.zoom <= map_.zmin + 1e-9;
  const bool atClosest = map_.zoom >= map_.zmax - 1e-9;

  if (dir < 0 && atWidest) { return StepScale(-1); }
  if (dir > 0 && atClosest) { return StepScale(1); }

  double zoom = map_.zoom + static_cast<double>(dir) * map_.zstep;
  if (zoom < map_.zmin) { zoom = map_.zmin; }
  if (zoom > map_.zmax) { zoom = map_.zmax; }
  map_.zoom = js::Round(zoom * 10) / 10;
  return true;
}

bool Simulation::StepScale(int dir) {
  // dir -1 widens the chart (system -> sector -> galaxy), +1 deepens it.
  const std::string here = map_.here;
  const std::string region = map_.sector;

  if (dir < 0) {
    if (map_.layer == "system") {
      SetLayer("sector");
      // Land on the ship's own star, the way the eye left it.
      if (const Body* system = catalog_.FindSystem(here); system != nullptr) {
        views_["sector"].sel = system->id;
        CentredOn(system->x, system->y);
      }
      map_.zoom = map_.zmin;
      return true;
    }
    if (map_.layer == "sector") {
      SetLayer("galaxy");
      if (const Body* body = catalog_.FindRegion(region); body != nullptr) {
        views_["galaxy"].sel = body->id;
        CentredOn(body->x, body->y);
      }
      map_.zoom = map_.zmin;
      return true;
    }
    return false;  // the galaxy is the widest chart there is
  }

  if (map_.layer == "galaxy") {
    SetLayer("sector");
    ViewSector(region);
    map_.zoom = map_.zmax;
    return true;
  }
  if (map_.layer == "sector") {
    // The system scale shows the star the ship is at; there is nowhere else to
    // stand, and zooming does not move the ship.
    SetLayer("system");
    map_.zoom = map_.zmax;
    return true;
  }
  return false;  // the ship's own system is the closest chart there is
}

void Simulation::CentredOn(double x, double y) {
  // The camera is the pan, and the pan is stored per scale. Centring means the
  // world point asked for sits under the middle of the viewport.
  LayerView& view = views_[map_.layer];
  const double spanX = std::max(map_.vw, map_.w);
  const double spanY = std::max(map_.vh, map_.h);
  const double limitX = js::Round(spanX * 0.5 * map_.zoom);
  const double limitY = js::Round(spanY * 0.5 * map_.zoom);
  view.px = std::clamp(js::Round((map_.cx - x) * map_.zoom), -limitX, limitX);
  view.py = std::clamp(js::Round((map_.cy - y) * map_.zoom), -limitY, limitY);
}

void Simulation::PanMap(double dx, double dy) {
  LayerView& view = views_[map_.layer];
  const double spanX = std::max(map_.vw, map_.w);
  const double spanY = std::max(map_.vh, map_.h);
  const double limitX = js::Round(spanX * 0.5 * map_.zoom);
  const double limitY = js::Round(spanY * 0.5 * map_.zoom);
  view.px = std::max(-limitX, std::min(limitX, view.px + dx));
  view.py = std::max(-limitY, std::min(limitY, view.py + dy));
}

bool Simulation::ViewSector(const std::string& regionId) {
  const Body* region = catalog_.FindRegion(regionId);
  if (region == nullptr) { return false; }

  const Body* system = CurrentSystem();
  map_.sector = regionId;
  LayerView& secView = views_["sector"];
  secView.sel = (system != nullptr && system->region == regionId) ? system->id : std::string();

  // Centre the sector camera over the chosen galactic region's star cluster so
  // clicking any galaxy on Layer 3 frames its systems immediately.
  const auto& sysIndices = catalog_.SystemsInRegion(regionId);
  auto boardIt = catalog_.layerBoards.find("sector");
  const MapGeometry& secBoard = boardIt == catalog_.layerBoards.end() ? catalog_.map : boardIt->second;
  if (!sysIndices.empty()) {
    double sumX = 0.0;
    double sumY = 0.0;
    for (std::size_t idx : sysIndices) {
      sumX += catalog_.systems[idx].x;
      sumY += catalog_.systems[idx].y;
    }
    const double meanX = sumX / static_cast<double>(sysIndices.size());
    const double meanY = sumY / static_cast<double>(sysIndices.size());
    const double z = secView.zoom > 0.0 ? secView.zoom : 1.0;
    const double limX = js::Round(secBoard.w * 0.5 * z);
    const double limY = js::Round(secBoard.h * 0.5 * z);
    secView.px = std::clamp(js::Round((secBoard.cx - meanX) * z), -limX, limX);
    secView.py = std::clamp(js::Round((secBoard.cy - meanY) * z), -limY, limY);
  }

  SetLayer("sector");
  return true;
}

bool Simulation::SelectTarget(const std::string& bodyId) {
  if (FindBodyById(bodyId) == nullptr) { return false; }
  state_.navTarget = bodyId;
  views_[map_.layer].sel = bodyId;
  return true;
}

// ---------------------------------------------------------------------------
// navMetrics(): what a crossing would cost
// ---------------------------------------------------------------------------

CrewMember* Simulation::AssignedNavigator() {
  for (CrewMember& crew : crew_) {
    if (crew.job == "navigation" && CrewBusy(crew) == nullptr) { return &crew; }
  }
  return nullptr;
}

Simulation::NavMetrics Simulation::MetricsFor(const Body& body) const {
  static const std::string kDefaultSystem = "sec-kessler";
  const Body* here = catalog_.FindBody(map_.here);
  const std::string& curSysId =
      (here == nullptr || here->system.empty()) ? kDefaultSystem : here->system;
  double distance = 0;

  if (body.type == "system") {
    const Body* origin = catalog_.FindSystem(curSysId);
    double px = 0;
    if (origin != nullptr && body.region == origin->region) {
      const double dx = body.x - origin->x;
      const double dy = body.y - origin->y;
      px = std::sqrt(dx * dx + dy * dy);
    } else if (origin != nullptr) {
      const Body* fromRegion = catalog_.FindRegion(origin->region);
      const Body* toRegion = catalog_.FindRegion(body.region);
      double dx = 0;
      double dy = 0;
      if (fromRegion != nullptr && toRegion != nullptr) {
        dx = (toRegion->x - fromRegion->x) * 1.3;
        dy = (toRegion->y - fromRegion->y) * 1.3;
      }
      px = std::sqrt(dx * dx + dy * dy) + 150;
      if (px > 1050.0) {
        const double excess = px - 1050.0;
        px = 1050.0 + 1150.0 * (excess / (excess + 1400.0));
      }
    }
    distance = std::max(6.0, js::Round(px / 24.0) + 5.0);
    if (body.id == curSysId) { distance = 0; }
  } else {
    const double dx = body.x - (here == nullptr ? 0.0 : here->x);
    const double dy = body.y - (here == nullptr ? 0.0 : here->y);
    const double px = std::sqrt(dx * dx + dy * dy);
    distance = std::max(3.0, js::Round(px / map_.pxPerAction));
    if (body.id == map_.here) { distance = 0; }
  }

  // The navigator shortens the crossing itself: a plotted course is fewer clicks
  // under way, and training buys a shorter one. A busy navigator plots nothing,
  // so only a free hand counts. Fuel follows the distance, so it is not
  // discounted a second time.
  const CrewMember* navigator = nullptr;
  for (const CrewMember& crew : crew_) {
    if (crew.job == "navigation" && CrewBusy(crew) == nullptr) { navigator = &crew; break; }
  }
  const double saving =
      navigator == nullptr ? 0.0 : 10.0 * std::max(0.0, std::min(5.0, navigator->jobAt));
  if (distance > 0.0 && saving > 0.0) {
    distance = std::max(1.0, js::Round(distance * (100.0 - saving) / 100.0));
  }
  const double drivePercent = state_.upgrades.count("drive") != 0 && state_.upgrades.at("drive") ? 80.0 : 100.0;

  NavMetrics metrics;
  metrics.dist = distance;
  metrics.fuel = js::Ceil(distance * drivePercent / 100.0);
  metrics.total = distance + 1;

  const double ly = distance * map_.lyPerAction;
  const double e13 = (ly * 9.46) / 10.0;
  const double days = distance * 19.0;
  const double months = js::Floor(days / 30.0);
  const double remainder = days - months * 30.0;

  metrics.ly = js::ToFixed(ly, 1) + " ly";
  metrics.km = js::ToFixed(e13, 1) + " x 10^13 km";
  metrics.transit = months > 0 ? "~" + Plur(months, "month") + ", " + Plur(remainder, "day")
                               : "~" + Plur(days, "day");
  return metrics;
}

// ---------------------------------------------------------------------------
// Commitments
// ---------------------------------------------------------------------------

bool Simulation::StartTravel() {
  if (!state_.navTarget.has_value()) { return false; }
  const Body* body = FindBodyById(*state_.navTarget);
  if (body == nullptr || !InRange(*body) || AtTarget(*body)) { return false; }
  if (FindOngoing("travel") != nullptr || FindOngoing("extract") != nullptr) { return false; }
  if (state_.engineCondition <= 0 || StatValue("energy") <= 0) { return false; }
  // Docking never blocks a crossing. A ship that plots a jump from a berth
  // leaves the berth as part of the same departure: no second action, no
  // refusal, the location readout follows.
  if (state_.docked) {
    const Port* port = HerePort();
    state_.docked = false;
    if (port != nullptr) {
      for (InfoRow& row : infos_) {
        if (row.id == "location") { row.value = port->name + " - undocked"; }
      }
    }
  }

  const NavMetrics metrics = MetricsFor(*body);
  if (StatValue("fuel") < metrics.fuel) { return false; }
  Stat* fuel = FindStat("fuel");
  if (fuel != nullptr) { fuel->value -= metrics.fuel; }

  // Order matters. The initiating action is spent FIRST, so the tick it causes
  // cannot eat a step of the journey it starts.
  PerformAction("Course locked - " + body->name);
  if (!state_.gameOver.empty()) { return true; }

  AddOngoing("travel", "Under way to " + body->name, metrics.dist, "travel", body->id);
  return true;
}

bool Simulation::StartScan() {
  if (!state_.navTarget.has_value()) { return false; }
  const Body* body = FindBodyById(*state_.navTarget);
  if (body == nullptr || !CanScan(*body)) { return false; }
  if (FindOngoing("scan") != nullptr || StatValue("energy") <= 0) { return false; }

  // Same ordering rule as a crossing: the initiating action first.
  const double cost = EffectiveScanCost(*body);
  state_.sim.sensor = 0;
  const bool remote = body->type == "system" || body->type == "region" ||
                      catalog_.BodySystem(*body) != CurrentSystemId();
  PerformAction((remote ? "Long-range scan started - " : "Deep scan started - ") + body->name);
  if (!state_.gameOver.empty()) { return true; }

  AddOngoing("scan", (remote ? "Long-range scan - " : "Deep scan - ") + body->name,
             cost, "scan", body->id);
  return true;
}

bool Simulation::StartExtraction() {
  if (!state_.gameOver.empty() || !state_.navTarget.has_value()) { return false; }
  const Body* body = FindBodyById(*state_.navTarget);
  if (body == nullptr || !CanExtractBody(*body) || !AtTarget(*body)) { return false; }
  if (state_.docked || StatValue("energy") <= 0) { return false; }
  if (FindOngoing("extract") != nullptr || FindOngoing("travel") != nullptr) { return false; }
  if (ExtractionPassesUsed(*body) >= ExtractionMaxPasses(*body)) { return false; }

  const double reserveVol = ExtractionReserveVol(*body);
  if (reserveVol > 0.0 && CargoUsed() + CargoReserved() + reserveVol > cargo_.capacity) {
    return false;
  }

  const std::string actionText = ExtractionActionLabel(*body);
  const double workCost = ExtractionWorkCost(*body);
  PerformAction(actionText + " - " + body->name);
  if (!state_.gameOver.empty()) { return true; }

  Task* task = AddOngoing("extract", actionText + " - " + body->name,
                          workCost, "extract", body->id);
  if (task != nullptr) { task->vol = reserveVol; }
  return true;
}

bool Simulation::StandWatch() {
  if (!state_.gameOver.empty()) { return false; }
  PerformAction("Stood watch");
  return true;
}

bool Simulation::Acknowledge(const std::string& taskId) {
  Task* task = FindOngoing(taskId);
  if (task == nullptr || task->state != "done") { return false; }

  // The hook runs BEFORE the task is cleared: the arrival happens while the
  // crossing is still on the books, exactly as the reference does it.
  const std::string type = task->type;
  const std::string target = task->target;
  const double reservedVol = task->vol;

  bool handled = false;
  if (type == "travel" && !target.empty()) { ArriveAt(target); handled = true; }
  else if (type == "scan") { RevealScan(target); handled = true; }
  else if (type == "extract" && !target.empty()) {
    FinishExtraction(target, reservedVol);
    const std::string cdTab = state_.activeTab.empty() ? "home" : state_.activeTab;
    state_.actionCooldowns["cd|" + cdTab + "|progress:extract"] = Cooldown{"routine", 5.0};
    handled = true;
  }

  ClearOngoing(taskId);
  LogLine("Process acknowledged and cleared.");
  return handled;
}

// ---------------------------------------------------------------------------
// Arrival and revelation
// ---------------------------------------------------------------------------

void Simulation::RevealScan(const std::string& bodyId) {
  const Body* body = FindBodyById(bodyId);
  if (body == nullptr) { return; }

  const bool first = state_.scanned.count(body->id) == 0 || !state_.scanned.at(body->id);
  state_.scanned[body->id] = true;

  // simOnScan(): the two authored signal beats.
  if (first) {
    SimRemember("Discovery", "The crew scanned " + body->name + ".");
  }
  if (body->id == "halvan" && state_.sim.signal == "dormant") {
    state_.sim.signal = "intercepted";
    SimRemember("Signal", "An old relay at Halvan's Reach repeats a name.");
    LogLine("An unusual signal was intercepted. Open the Comms Relay.");
  }
  if (body->id == "echo-site" && state_.sim.signal == "coordinates") {
    state_.sim.signal = "request";
    SimRemember("Signal", "The Choir Array carries a request from Silicon Marrow.");
    LogLine("The Choir Array answered. Open the Comms Relay.");
  }

  FileSurvey(*body);
  ResolveMapScanEvents(*body, first);
  ContractProgress("scan", body->id);
  CommissionProgress("scan", body->id);
  CheckCommendations();

  if (body->type == "system") {
    const auto& localIdx = catalog_.BodiesInSystem(body->id);
    int ports = 0;
    int deposits = 0;
    for (std::size_t idx : localIdx) {
      if (idx >= catalog_.bodies.size()) { continue; }
      const Body& local = catalog_.bodies[idx];
      if (catalog_.FindPort(local.id) != nullptr) { ++ports; }
      if (CanExtractBody(local)) { ++deposits; }
    }
    LogLine("Remote system survey filed: " + body->name + " (" +
            std::to_string(localIdx.size()) + " charted bodies, " +
            std::to_string(ports) + " port" + (ports == 1 ? "" : "s") + ", " +
            std::to_string(deposits) + " extractable deposit" + (deposits == 1 ? "" : "s") + ").");
    return;
  }
  if (body->type == "region") {
    const auto& sysIdx = catalog_.SystemsInRegion(body->id);
    if (first) {
      SimRemember("World", "Deep-array telemetry mapped cluster " + body->name + ".");
    }
    LogLine("Deep-region cluster survey filed: " + body->name + " (" +
            std::to_string(sysIdx.size()) + " star systems catalogued in Science).");
    return;
  }

  if (catalog_.BodySystem(*body) == CurrentSystemId()) {
    views_["system"].sel = body->id;
    if (map_.layer == "system") { state_.navTarget = body->id; }
  }
  LogLine("Deep scan readout filed: " + body->name + ". Survey record stored in the Science Room.");
}

void Simulation::FileSurvey(const Body& body) {
  for (const SurveyRecord& record : survey_) {
    if (record.id == "sv-" + body.id) { return; }  // already held
  }
  double worth = 60;
  if (body.type == "system") {
    worth = 95;
  } else if (body.type == "region") {
    worth = 145;
  } else {
    auto it = catalog_.surveyWorth.find(body.type);
    if (it != catalog_.surveyWorth.end()) { worth = it->second; }
  }
  if (SensorTier() >= 2) {
    worth = js::Ceil(worth * 1.25);
  }

  SurveyRecord record;
  record.id = "sv-" + body.id;
  record.name = body.name;
  record.src = (body.type == "system" || body.type == "region") ? "Long-range scan" : "Deep scan";
  record.value = worth;
  record.state = "filed";
  survey_.push_back(record);
}

void Simulation::FinishExtraction(const std::string& bodyId, double reservedVol) {
  const Body* body = catalog_.FindBody(bodyId);
  if (body == nullptr || !CanExtractBody(*body)) { return; }

  const int pass = 1;
  state_.sim.observed["extract-1:" + body->id] = true;

  const std::uint64_t h = MapEventHash(body->id + "#extract#1");
  const bool jackpot = (h % 5ULL) == 0ULL;
  const double freeHold = std::max(0.0, cargo_.capacity - (CargoUsed() + (CargoReserved() - reservedVol)));

  auto addLot = [&](const std::string& name, const std::string& kind,
                    double vol, double val, bool risk) -> bool {
    if (vol > freeHold + 1e-6) { return false; }
    state_.lotSerial += 1;
    GoodsLot lot;
    lot.id = "ext-" + std::to_string(static_cast<long long>(state_.lotSerial));
    lot.name = name;
    lot.kind = kind;
    lot.vol = vol;
    lot.value = val;
    lot.risk = risk;
    goods_.push_back(std::move(lot));
    return true;
  };

  if (IsGasBody(*body)) {
    ClampStat("fuel", jackpot ? 20.0 : 14.0);
    state_.fuelCells += jackpot ? 2 : 1;
    if (jackpot) {
      state_.shiplets += 35;
      LogLine("Atmospheric skim at " + body->name +
              " hit a helium-3 band: +20% fuel, +2 fuel cells, and +35 sh condensate.");
      SimRemember("Discovery", "Skimmed a rich helium-3 pocket at " + body->name + ".");
    } else {
      LogLine("Atmospheric skim at " + body->name +
              " condensed +14% drive fuel and 1 fuel cell.");
    }
  } else if (body->type == "belt") {
    const double val = 85.0 + static_cast<double>(h % 31ULL);
    if (addLot("Belt Salvage (" + body->name + ")", "Salvage", 3.0, val, false)) {
      CommissionProgress("salvage", body->id);
      LogLine("Debris field haul at " + body->name + ": 3 m3 salvage stowed in the hold.");
    }
    if (jackpot) {
      state_.repairKits += 1;
      state_.shiplets += 40;
      LogLine("Debris jackpot at " + body->name +
              ": stripped 1 intact repair kit and 40 sh in transponder cores.");
      SimRemember("Discovery", "Cracked an intact hull section inside " + body->name + ".");
    }
  } else if (body->type == "site") {
    const std::uint64_t pick = (h + static_cast<std::uint64_t>(pass)) % 4ULL;
    if (pick == 0ULL) {
      if (addLot("Derelict Salvage (" + body->name + ")", "Salvage", 3.0, 96.0, false)) {
        CommissionProgress("salvage", body->id);
      }
      state_.repairKits += 1;
      LogLine("Derelict cut at " + body->name +
              ": recovered 3 m3 hull salvage and 1 field repair kit.");
    } else if (pick == 1ULL) {
      addLot("Recovered Components (" + body->name + ")", "Components", 2.0, 150.0, false);
      LogLine("Derelict salvage at " + body->name +
              ": extracted 2 m3 intact shipboard components.");
    } else if (pick == 2ULL) {
      addLot("Sealed Relic Cask (" + body->name + ")", "Relics", 2.0, 195.0, false);
      state_.sim.insight += 1;
      LogLine("Vault breach at " + body->name +
              ": stowed 2 m3 pre-collapse relics and logged +1 insight.");
      SimRemember("Discovery", "Opened a sealed vault inside " + body->name + ".");
    } else {
      addLot("Unregistered Strongbox (" + body->name + ")", "Unregistered", 2.0, 210.0, true);
      state_.shiplets += 55;
      LogLine("Wreck locker at " + body->name +
              ": recovered +55 sh chits and 2 m3 unregistered cargo.");
    }
  } else if (IsVolatileIceBody(*body)) {
    if (jackpot) {
      addLot("Subglacial Relic Core (" + body->name + ")", "Relics", 2.0, 185.0, false);
      const double rationStock = mess_.rations;
      mess_.rations = std::min(mess_.rationCapacity, mess_.rations + 2.0);
      LogLine("Deep ice core at " + body->name + ": recovered 2 m3 subglacial relics and " +
              js::NumToString(mess_.rations - rationStock) + " dry rations.");
      SimRemember("Discovery", "Extracted a preserved subglacial core on " + body->name + ".");
    } else {
      const double val = 70.0 + static_cast<double>(h % 25ULL);
      addLot("Cryo-Volatile Ice (" + body->name + ")", "Water", 4.0, val, false);
      LogLine("Volatile harvest at " + body->name + ": 4 m3 cryo-ice stowed in the hold.");
    }
  } else {
    // Rocky, terrestrial, lava planet or moon regolith extraction.
    const std::string tag = body->type == "moon" ? "Lunar Regolith Ore" : "Planetary Ore";
    if (jackpot) {
      addLot("Rare-Earth Seam (" + body->name + ")", "Components", 3.0, 160.0, false);
      if (body->type == "moon") { state_.fuelCells += 1; }
      LogLine("Rich vein tapped on " + body->name +
              ": refined 3 m3 rare-earth components" +
              (body->type == "moon" ? " and 1 He-3 fuel cell." : "."));
      SimRemember("Discovery", "Tapped a rich rare-earth motherlode on " + body->name + ".");
    } else {
      const double val = (body->type == "moon" ? 84.0 : 80.0) + static_cast<double>(h % 26ULL);
      addLot(tag + " (" + body->name + ")", "Bulk metal", 4.0, val, false);
      LogLine("Surface extraction on " + body->name + ": 4 m3 " + tag + " winched into the hold.");
    }
  }

  // Dynamic extraction hazard roll (~24% base, elevated on deep passes or unstable hulks).
  const std::uint64_t hazardRoll = (h >> 8U) % 100ULL;
  const std::string feature = DetailValue(*body, "Map feature");
  const std::uint64_t threshold =
      (feature == "Unstable hulk") ? 55ULL : (pass >= 2 ? 70ULL : 80ULL);
  if (hazardRoll >= threshold) {
    const Body* sys = catalog_.FindSystem(catalog_.BodySystem(*body));
    const Body* reg = sys == nullptr ? nullptr : catalog_.FindRegion(sys->region);
    const std::string auth = reg == nullptr ? std::string() : DetailValue(*reg, "Authority");
    if ((auth == "None" || auth == "Free Captains") && hazardRoll >= 90ULL) {
      SpawnMapIncident("map-pirate-toll",
                       "Mining signature at " + body->name + " drew an armed claim-jumper.");
    } else if (body->type == "belt" || body->type == "site") {
      SpawnMapIncident("map-derelict-ping",
                       "Cutting charges at " + body->name + " kicked spall across the hull.");
    } else if (IsVolatileIceBody(*body)) {
      SpawnMapIncident("map-cryo-spore",
                       "Thermal drills on " + body->name + " vented a pressurized brine plume.");
    } else {
      SpawnMapIncident("map-ion-squall",
                       "Magnetic dust shear over " + body->name + " surged through the array.");
    }
  }
}

void Simulation::ChronicleVisit(const Body* system) {
  if (system == nullptr) { return; }
  state_.chronicle.systems[system->id] = true;
  state_.chronicle.regions[system->region] = true;
}

void Simulation::ArriveAt(const std::string& bodyId) {
  const Body* prevSystem = CurrentSystem();
  const std::string prevRegion = prevSystem == nullptr ? std::string() : prevSystem->region;
  const Body* system = catalog_.FindSystem(bodyId);

  const std::string localId = system != nullptr ? system->entry : bodyId;
  const Body* body = FindBodyById(localId);
  if (body == nullptr) { return; }

  map_.here = body->id;
  views_["system"].sel = body->id;

  if (system != nullptr) {
    map_.sector = system->region;
    views_["sector"].sel = system->id;
    views_["galaxy"].sel = system->region;
    if (map_.layer == "sector") { state_.navTarget = system->id; }
    else if (map_.layer == "galaxy") { state_.navTarget = system->region; }
    ContractProgress("visit", system->id);
  }
  if (map_.layer == "system") { state_.navTarget = body->id; }

  state_.docked = HerePort() != nullptr && HerePort()->id == body->id;
  RefreshLocationInfo();
  ContractProgress("visit", body->id);

  LogLine("Arrived at " + body->name +
          (state_.docked ? " - docked. Right-click the docked indicator to open the station" : "") + ".");
  SimRemember("Arrival", "The ship arrived at " + body->name + ".");
  ChronicleVisit(CurrentSystem());
  ResolveMapArrivalEvents(*body, system, prevRegion);
  CommissionProgress("visit", body->id);
  CheckCommendations();
}

void Simulation::ResolveMapArrivalEvents(const Body& localBody, const Body* jumpedSystem,
                                         const std::string& prevRegion) {
  // 1. Interstellar jumps into procedural systems/galaxies can trigger regional
  //    navigation hazards or relay telemetry bursts on first entry.
  if (jumpedSystem != nullptr && IsProceduralId(jumpedSystem->id)) {
    const std::string visitKey = "map-jump:" + jumpedSystem->id;
    if (!state_.sim.observed[visitKey]) {
      state_.sim.observed[visitKey] = true;
      const Body* region = catalog_.FindRegion(jumpedSystem->region);
      const std::string hazard = region == nullptr ? std::string() : DetailValue(*region, "Dominant hazard");
      const std::string auth = region == nullptr ? std::string() : DetailValue(*region, "Authority");
      const bool crossGalaxy = !prevRegion.empty() && prevRegion != jumpedSystem->region;
      const std::uint64_t roll = MapEventHash(jumpedSystem->id) % 100ULL;

      if (crossGalaxy && IsProceduralId(jumpedSystem->region)) {
        const std::string galKey = "map-galaxy:" + jumpedSystem->region;
        if (!state_.sim.observed[galKey]) {
          state_.sim.observed[galKey] = true;
          const std::string rname = region == nullptr ? jumpedSystem->region : region->name;
          SimRemember("World", "Crossed into outer cluster " + rname + ".");
          LogLine("Chart milestone: entered " + rname + ". Regional beacons logged.");
        }
      }

      if (roll < (crossGalaxy ? 52ULL : 34ULL)) {
        if (hazard == "Ion squalls") {
          SpawnMapIncident("map-ion-squall",
                           "Charged filaments in " + jumpedSystem->name + " wash across the hull.");
        } else if (hazard == "Subspace shear") {
          SpawnMapIncident("map-grav-shear",
                           "Jump wake turbulence in " + jumpedSystem->name + " shakes the drive spine.");
        } else if (hazard == "Cryo-dust plumes") {
          SpawnMapIncident("map-cryo-spore",
                           "Brine-ice plumes in " + jumpedSystem->name + " frost the outer seals.");
        } else if (hazard == "Ghost telemetry") {
          SpawnMapIncident("map-beacon-ghost",
                           "Silicate veil echoes in " + jumpedSystem->name + " flood the comms bus.");
        } else if (hazard == "Drifting hulks") {
          SpawnMapIncident("map-derelict-ping",
                           "Graveyard debris in " + jumpedSystem->name + " crosses the arrival cone.");
        } else if (hazard == "Lane blockades" || auth == "None" || auth == "Free Captains") {
          SpawnMapIncident("map-pirate-toll",
                           "An unflagged picket sweeps the approach into " + jumpedSystem->name + ".");
        }
      } else if (roll >= 82ULL) {
        // Calm survey beacon packet on arrival.
        state_.sim.insight += 1;
        LogLine("Arrival telemetry: an automated picket buoy in " + jumpedSystem->name +
                " transmitted +1 Simulacron insight.");
      }
    }
  }

  // 2. Local arrival at a procedural anomaly/wreck/depot resolves proximity
  //    encounters once per body.
  const std::string feature = DetailValue(localBody, "Map feature");
  if (feature.empty()) { return; }
  const std::string arriveKey = "map-arr:" + localBody.id;
  if (state_.sim.observed[arriveKey]) { return; }
  state_.sim.observed[arriveKey] = true;

  if (feature == "Fuel skimmer depot") {
    ClampStat("fuel", 10.0);
    state_.fuelCells += 1;
    LogLine("Orbital skimmer at " + localBody.name +
            " transferred +10% fuel and 1 condensed fuel cell.");
    SimRemember("Discovery", "Recovered condensed fuel from an automated depot at " + localBody.name + ".");
  } else if (feature == "Ion pocket") {
    SpawnMapIncident("map-ion-squall",
                     "Local magnetosphere pocket around " + localBody.name + " surged.");
  } else if (feature == "Unstable hulk") {
    state_.shiplets += 45;
    LogLine("Recovered a 45-shiplet registry transponder from " + localBody.name + ".");
    SpawnMapIncident("map-derelict-ping",
                     "Breakaway plating from " + localBody.name + " drifted across the bow.");
  }
}

void Simulation::ResolveMapScanEvents(const Body& body, bool firstScan) {
  if (!firstScan) { return; }
  const std::string feature = DetailValue(body, "Map feature");
  if (feature.empty()) { return; }
  const std::string scanKey = "map-scan:" + body.id;
  if (state_.sim.observed[scanKey]) { return; }
  state_.sim.observed[scanKey] = true;

  if (feature == "Salvage cache") {
    state_.lotSerial += 1;
    if (CargoUsed() + 3.0 <= cargo_.capacity) {
      GoodsLot lot;
      lot.id = "salvage-map-" + std::to_string(static_cast<long long>(state_.lotSerial));
      lot.name = "Recovered Salvage (" + body.name + ")";
      lot.kind = "Salvage";
      lot.vol = 3.0;
      lot.value = 75.0;
      lot.risk = false;
      goods_.push_back(std::move(lot));
      CommissionProgress("salvage", body.id);
      LogLine("Map discovery at " + body.name + ": 3 m3 salvage cache winched into the hold.");
    } else {
      state_.repairKits += 1;
      LogLine("Map discovery at " + body.name +
              ": hold full, stripped 1 field repair kit from the cache.");
    }
    SimRemember("Discovery", "Recovered a charted salvage cache at " + body.name + ".");
  } else if (feature == "Ancient nav buoy") {
    state_.sim.insight += 1;
    const auto& peers = catalog_.BodiesInSystem(catalog_.BodySystem(body));
    int revealed = 0;
    for (std::size_t idx : peers) {
      if (revealed >= 2 || idx >= catalog_.bodies.size()) { break; }
      const Body& peer = catalog_.bodies[idx];
      if (peer.id == body.id || state_.scanned[peer.id]) { continue; }
      state_.scanned[peer.id] = true;
      FileSurvey(peer);
      ++revealed;
    }
    LogLine("Ancient nav buoy at " + body.name + " unlocked " + std::to_string(revealed) +
            " local telemetry chart" + (revealed == 1 ? "" : "s") + " and +1 insight.");
    SimRemember("Discovery", "Decoded an ancient navigation buoy near " + body.name + ".");
  } else if (feature == "Rich mineral seam") {
    const std::string bonusId = "sv-seam-" + body.id;
    bool exists = false;
    for (const SurveyRecord& rec : survey_) {
      if (rec.id == bonusId) { exists = true; break; }
    }
    if (!exists) {
      SurveyRecord bonus;
      bonus.id = bonusId;
      bonus.name = body.name + " Core Assay";
      bonus.src = "Anomaly scan";
      bonus.value = 110.0;
      bonus.state = "filed";
      survey_.push_back(std::move(bonus));
      LogLine("Rich mineral assay filed at " + body.name + " (worth 110 sh in Science).");
    }
  } else if (feature == "Ion pocket") {
    state_.sim.insight += 1;
    state_.sim.sensor = 1;
    LogLine("Magnetospheric pocket at " + body.name +
            " calibrated the sensor array (+1 insight, next scan -2 work).");
  }
}

void Simulation::RefreshLocationInfo() {
  const Body* here = FindBodyById(map_.here);
  const std::string name = here == nullptr ? std::string("Unknown") : here->name;
  const bool docked = HerePort() != nullptr;
  for (InfoRow& row : infos_) {
    if (row.id == "location") {
      row.value = name + (docked ? " - docked" : "");
    } else if (row.id == "contacts") {
      const Body* system = CurrentSystem();
      row.value = docked ? name + " - station contact"
                         : "Charted objects in " + (system == nullptr ? std::string("unknown space") : system->name);
    }
  }
}

bool Simulation::Dock() {
  const Port* port = HerePort();
  if (port == nullptr || state_.docked || !state_.gameOver.empty()) { return false; }
  const std::string name = port->name;
  state_.docked = true;
  state_.visitedPorts[port->id] = true;
  ContractProgress("visit", map_.here);
  for (InfoRow& row : infos_) {
    if (row.id == "location") { row.value = name + " - docked"; }
  }
  PerformAction("Docked at " + name);
  return true;
}

bool Simulation::Undock() {
  const Port* port = HerePort();
  if (port == nullptr || !state_.docked || !state_.gameOver.empty()) { return false; }
  const std::string name = port->name;
  state_.docked = false;
  state_.activeTab = "home";
  for (InfoRow& row : infos_) {
    if (row.id == "location") { row.value = name + " - undocked"; }
  }
  PerformAction("Undocked from " + name);
  return true;
}

}  // namespace ship
