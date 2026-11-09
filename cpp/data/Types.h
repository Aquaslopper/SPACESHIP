// ============================================================================
//  Types.h — the catalogues, as plain data.
//  ---------------------------------------------------------------------------
//  Every struct here corresponds to a table in the reference edition
//  (Spaceship.hta). They hold *content*: coordinates, names, prices, prose.
//  No behaviour lives in this header; the simulation reads these values and
//  the renderer draws them, and neither may invent a field that the reference
//  does not have.
//
//  Field names follow the JavaScript where the JavaScript name is already
//  clear (id, name, x, y, w, h, klass), because a reviewer comparing the two
//  editions should not have to translate twice.
// ============================================================================
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ship {

// ---------------------------------------------------------------------------
// Geometry. Stage coordinates are pixels on a fixed board: the ship diagrams
// are 1160x460 (large hull) or compact, the chart is a 638x278 world.
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

// A compartment. `tone` is the background hex; `small` marks the
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
  std::string accent;       // hex
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
  std::string type;    // origin, star, station, planet, moon, belt, site, system, region
  std::string name;
  std::string klass;
  double x = 0.0;
  double y = 0.0;

  // Local destinations carry the id of their system. The Kessler bodies were
  // there before systems existed and leave it empty; BodySystem() supplies
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

// ORBITAL_WORLDS: the four kinds of outer world the local-chart pass adds, in
// the reference's order, because the pass picks one by system index.
struct OrbitalWorld {
  std::string id;           // gas / ice / ocean / rock
  std::string label;        // "Outer gas giant"
  std::string atmosphere;   // "Hydrogen / helium"
  std::string scan;         // first scan line
  std::string locked;       // first locked scan line
};

struct Port {
  std::string id;
  std::string name;
  std::string tip;
  std::map<std::string, double> prices;  // commodity kind -> multiplier
};

struct Good {
  std::string id;
  std::string name;
  std::string kind;
  int vol = 0;
  double value = 0;
  bool risk = false;
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

// The chart's own drawing board. Distances are derived from these numbers,
// so they must not be adjusted for the port's window size.
struct MapGeometry {
  double w = 638, h = 278;
  double cx = 319, cy = 139;
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
// Starting values. These are content: the crew a new voyage begins
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

// One handbook entry, exactly as in content.json. `link` names the tab
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
