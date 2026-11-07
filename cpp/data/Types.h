// ============================================================================
//  Types.h — the authored catalogues, as plain data.
//  ---------------------------------------------------------------------------
//  Reference-mirroring structs hold authored content: coordinates, names,
//  prices, prose, and state definitions. Native SpaceMap blueprint structs are
//  explicitly C++-only and are not exported to the HTA-derived JSON assets.
//  No behaviour lives in this header; the simulation reads these values and
//  the renderer draws them.
//
//  Reference-compatible field names follow the JavaScript where already clear
//  (id, name, x, y, w, h, klass), so reviewers can compare the two editions.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ship {

// ---------------------------------------------------------------------------
// Geometry. Stage coordinates are pixels on fixed boards: the ship diagrams
// are 1160x460 (large hull) or compact; chart-authored coordinates use 638x278.
// ---------------------------------------------------------------------------
struct Rect {
  double x = 0.0;
  double y = 0.0;
  double w = 0.0;
  double h = 0.0;
};

// One piece of hull silhouette: hp-body, hp-spine, hp-pod, hp-lens, ...
struct HullPart {
  std::string id;
  std::string cls;   // CSS class in the reference; the drawing recipe here
  Rect box;
};

// A compartment. `tone` is the authored background hex; `small` marks the
// compartments that get the compact treatment inside the dorsal spine,
// the ventral pod and the command lens.
struct Room {
  std::string id;
  std::string name;
  std::string systems;
  std::string tag;
  std::string tone;
  Rect box;
  bool small = false;
};

// A room's instrument-window vignette (ROOM_SCENES): which of the nine
// archetypes it is drawn as, and the three lines of text beneath the scope.
struct RoomScene {
  std::string glyph;        // reactor, filter, habitat, medical, cargo, orbit, core, network, garden
  std::string code;         // "PROPULSION / 01"
  std::string label;        // "Primary drive manifold"
  std::string description;  // the caption
  std::string accent;       // authored hex
};

struct DeckPlan {
  std::vector<HullPart> hull;
  Rect corridor;
  std::vector<Room> rooms;

  const Room* FindRoom(const std::string& id) const {
    for (const Room& r : rooms) { if (r.id == id) { return &r; } }
    return nullptr;
  }
};

// ---------------------------------------------------------------------------
// The chart. One struct serves all three layers because the reference uses one
// body shape for all three (type tells them apart).
// ---------------------------------------------------------------------------
struct Body {
  std::string id;
  std::string type;    // origin, star, station, planet, moon, belt, site, system, region, debris, derelict
  std::string tag;     // generated semantic tag; empty for legacy authored bodies
  std::string taskId;  // native encounter/task blueprint selected for a generated body
  std::string lootTable;  // native recovery table selected by its body profile
  std::string name;
  std::string klass;
  double x = 0.0;
  double y = 0.0;

  // Local destinations carry the id of their system. The Kessler bodies were
  // authored before systems existed and leave it empty; BodySystem() supplies
  // the reference's default ("sec-kessler").
  std::string system;

  // Systems and regions only.
  std::string region;
  std::string entry;   // systems: id of the local body that represents them

  // Regions only: cosmetic star count for the galaxy layer.
  int cluster = 0;

  std::vector<std::pair<std::string, std::string>> details;
  std::vector<std::string> scan;
  std::vector<std::string> scanLocked;

  // Planetary sub-type added by the local-chart expansion (gas/ice/ocean/rock).
  std::string subtype;
};

// One animation of the quarters figure: rows of characters, five-pixel cells.
struct SpriteFrame {
  std::vector<std::string> rows;
};

// A haircut is a sparse set of row overrides on SPRITE.
struct HairStyle {
  std::string id;
  std::map<int, std::string> rows;
};

struct Lane {
  std::string from;
  std::string to;
};

struct Port {
  std::string id;
  std::string name;
  std::string tip;
  std::map<std::string, double> prices;  // commodity kind -> multiplier
  std::string stationTier;               // empty = legacy full station; mini | normal
  std::vector<std::string> goodIds;       // empty = legacy full market
  std::vector<std::string> services;      // empty = legacy full services
  bool beta = false;                      // mini stations begin with limited beta services
};

struct Good {
  std::string id;
  std::string name;
  std::string kind;
  int vol = 0;
  double value = 0;
  bool risk = false;
  bool miniStationEligible = false;
};

struct Contract {
  std::string id;
  std::string title;
  std::string goal;      // visit, scan, freight, passenger
  std::string target;
  std::string origin;
  std::string note;
  double reward = 0;
  int vol = 0;
  std::string cargo;   // freight: what the consignment is called
  bool recognizePrior = false;
};

// Native SpaceMap blueprints. These are definitions only; Phase 4 consumes
// them to make a voyage-specific catalogue. They are not HTA-exported data.
struct IncidentType;
struct SurveyRecordDef;

struct SpaceMapNameRule {
  std::string category;
  std::vector<std::string> prefixes;
  std::vector<std::string> suffixes;
};

struct WeightedId {
  std::string id;
  std::uint32_t weight = 0;
};

struct BodyProfileDef {
  std::string id;
  std::string type;       // star, planet, moon, debris, derelict, site
  std::string tag;        // semantic tag, independent of type and display text
  std::string klass;
  std::string subtype;
  std::string nameRule;
  std::string lootTable;
  std::uint32_t weight = 0;
  double moonChance = 0;  // planet profiles only, 0..1; at most one moon
  std::vector<std::pair<std::string, std::string>> details;
  std::vector<std::string> scan;
  std::vector<std::string> scanLocked;
};

enum class LootKind { None, Shiplets, Cargo, Survey, FuelCell, RepairKit };

struct LootEntryDef {
  std::string id;
  std::string text;
  LootKind kind = LootKind::None;
  std::uint32_t weight = 0;  // integer weight; table weights sum to 100
  std::string goodId;         // Cargo only; maps to a Good kind for port pricing
  std::string cargoName;
  std::string surveyId;       // Survey only
  int minAmount = 0;          // cargo count, shiplets, or survey payout range by kind
  int maxAmount = 0;
  double volume = 0;          // per cargo unit
  double value = 0;           // per cargo unit; zero for non-cargo outcomes
  bool risk = false;
};

struct LootTableDef {
  std::string id;
  std::vector<LootEntryDef> entries;
  bool filterByEligibility = true;  // capacity/duplicate-survey filter before weighted draw
};

struct SpaceTaskDef {
  std::string id;
  std::string theme;  // engineering, life-support, crew, navigation, provisions
  std::string title;
  std::string discoveryText;
  std::string workText;
  std::string completionText;
  std::string lootTable;
  int startActions = 1;
  int workActions = 1;
  double energyCost = 0;
  bool rewardOnAcknowledge = true;
};

struct StationProfileDef {
  std::string id;
  std::string tier;  // mini | normal
  int minGoods = 0;
  int maxGoods = 0;
  std::vector<std::string> goodIds;
  std::vector<std::string> services;
  bool beta = false;
};

struct ShipContactActionDef {
  std::string id;
  std::string label;
  std::string text;
  int clockTicks = 1;
};

struct ShipContactProfileDef {
  std::string id;
  std::string type;
  std::string tag;
  std::string klass;
  std::string nameRule;
  std::uint32_t weight = 0;
  int movementEveryActions = 0;  // 0 = stationary; otherwise committed actions
  std::vector<ShipContactActionDef> actions;
};

struct SpaceMapRules {
  std::uint32_t version = 1;
  // Optional hard totals remain unset. Phase 4's default generator instead
  // applies its documented one-mirror growth policy, never a 50x star-count guess.
  std::optional<std::uint32_t> targetRegions;
  std::optional<std::uint32_t> targetSystems;
  std::optional<std::uint32_t> targetStarBodies;
  std::optional<double> areaMultiplier;
  double moonChance = 0.5;                 // per generated planet
  int maxMoonsPerPlanet = 1;
  int miniStationGoodsMax = 3;
  int normalStationGoodsMax = 7;
  int shipContactClickTicks = 1;
};

struct SpaceMapBlueprint {
  SpaceMapRules rules;
  std::vector<SpaceMapNameRule> names;
  std::vector<BodyProfileDef> bodies;
  std::vector<LootTableDef> lootTables;
  std::vector<SpaceTaskDef> tasks;
  std::vector<IncidentType> incidents;
  std::vector<SurveyRecordDef> surveys;
  std::vector<StationProfileDef> stations;
  std::vector<ShipContactProfileDef> shipContacts;
};

// Authored chart coordinates stay on the reference board. Native C++ layers
// receive a 50x linear extent while the logical viewport remains unchanged.
constexpr double kChartCoordinateWidth = 638.0;
constexpr double kChartCoordinateHeight = 278.0;
constexpr double kExpandedMapScale = 50.0;
constexpr double kExpandedMapWidth = kChartCoordinateWidth * kExpandedMapScale;
constexpr double kExpandedMapHeight = kChartCoordinateHeight * kExpandedMapScale;

struct MapLayerDimensions {
  double w = 0;
  double h = 0;
};

struct MapGeometry {
  // Authored bodies and save files keep the reference coordinate frame.
  double w = kChartCoordinateWidth, h = kChartCoordinateHeight;
  double cx = kChartCoordinateWidth * 0.5, cy = kChartCoordinateHeight * 0.5;

  // Each native layer has a 50x linear world extent relative to the C++ chart.
  // Keep the values per layer so each scale can later be tuned independently;
  // Phase 3 intentionally gives all three the same approved 50x bounds.
  MapLayerDimensions system{kExpandedMapWidth, kExpandedMapHeight};
  MapLayerDimensions sector{kExpandedMapWidth, kExpandedMapHeight};
  MapLayerDimensions galaxy{kExpandedMapWidth, kExpandedMapHeight};

  const MapLayerDimensions& DimensionsFor(const std::string& layer) const {
    if (layer == "sector") { return sector; }
    if (layer == "galaxy") { return galaxy; }
    return system;
  }

  double zoom = 1, zmin = 0.6, zmax = 2.0, zstep = 0.2;
  double grid = 45;
  double vpy = 0.35;
  double rungs = 9;
  double rungPow = 2.1;
  double pxPerAction = 16;
  double lyPerAction = 0.42;
};

struct Counts {
  int ships = 0;
  int roomsKestrel = 0;
  int roomsWren = 0;
  int fittedRooms = 0;
  int crew = 0;
  int hires = 0;
  int jobs = 0;
  int orders = 0;
  int commodities = 0;
  int regions = 0;
  int systems = 0;
  int bodies = 0;
  int ports = 0;
  int lanes = 0;
  int contracts = 0;
  int incidents = 0;
  int projects = 0;
  int commendations = 0;
  int wiki = 0;
  int themes = 0;
  int taskTypes = 0;
};


// ---------------------------------------------------------------------------
// Authored starting values. These are content: the crew a new voyage begins
// with, the hull readouts it begins at, and the definitions every subsystem
// reads. Live state lives in core/GameState.h and is built from these.
// ---------------------------------------------------------------------------
struct CrewDefinition {
  std::string id, name, role, room, activity, colour;
  std::string sex, hair, hair2, skin, hairStyle;
  double health = 0, morale = 0, fatigue = 0, hunger = 0, wallet = 0, hungerTick = 0;
  std::string traits;
  std::string race;
  int age = 0;
  std::string homeworld, hobby, religion;
  std::string job, skill;
  double jobAt = 0;
};

struct CandidateDefinition {
  CrewDefinition crew;
  std::string port;
  double fee = 0;
};

struct InfoDefinition {
  std::string id, label, value;
};

struct StatDefinition {
  std::string id, label;
  double value = 0;
  std::string unit, colour;
};

struct SurveyRecordDef {
  std::string id, name, src;
  double value = 0;
  std::string state;
  std::string bodyType;  // source body type; empty for legacy records
  std::string tag;       // survey-record category, not the source body's tag
  std::string text;
};

struct IncidentType {
  struct Stage {
    std::string text;
    double ticks = 0;
    std::optional<double> hull, shield, energy, morale;
  };
  std::string id, kind, source, title, remedy;
  double first = 0;   // action of the first report
  double every = 0;   // actions between reports
  // Map-only fields. Existing ship reports keep scope="ship" and their
  // authored first/every schedule; map events are keyed to tags and weights.
  std::string scope = "ship";  // ship | map
  std::string trigger;          // schedule | enter-tag | scan-tag | contact-action
  std::string targetTag;
  std::string taskId;
  std::string lootTable;
  std::uint32_t spawnWeight = 0;
  // Whether the quick remedy costs a repair kit. Absent means "every report
  // except a crew dispute".
  std::optional<bool> requiresKit;
  std::vector<Stage> stages;
};

// What claiming a commendation pays. Free to claim, and once only.
struct Reward {
  double cash = 0;
  double fuel = 0;   // fuel cells
  double kits = 0;   // repair kits
  double med = 0;    // medkits
};

struct Commendation {
  std::string id, title, metric;
  double goal = 0;
  std::string detail;
  Reward reward;
};

// One handbook entry, exactly as authored in content.json. `link` names the tab
// the entry can open; it is free, like every other read in the reference.
struct WikiEntry {
  std::string id, group, title, text, link, linkLabel;
  bool spoiler = false;   // the title carries the reference's "(spoilers)" tag
};

struct ProjectDef {
  std::string id, label, room, lot;
  double energy = 0, work = 0;
};

}  // namespace ship
