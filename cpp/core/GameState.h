// ============================================================================
//  GameState.h — everything the voyage remembers.
//  ---------------------------------------------------------------------------
//  The reference keeps one global STATE object plus a handful of parallel
//  stores (CREW, LIFE, MESS, STATS, EVENTS, GOODS, SURVEY, MAP) precisely so
//  that serialising a voyage is trivial. The port keeps the same split and the
//  same field names, because SaveSystem has to produce a file the HTA can read
//  and consume one it wrote.
//
//  Field names are therefore not up for tidying: `rackWarnedMess` stays.
// ============================================================================
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "data/Types.h"

namespace ship {

// ---------------------------------------------------------------------------
// Crew
// ---------------------------------------------------------------------------
struct CrewMember {
  std::string id;
  std::string name;
  std::string role;
  std::string room;
  std::string activity;
  std::string colour;

  std::string sex;
  std::string hair;
  std::string hair2;
  std::string skin;
  std::string hairStyle;

  double health = 0;
  double morale = 0;
  double fatigue = 0;
  double hunger = 0;
  double wallet = 0;
  double hungerTick = 0;

  std::string traits;

  // Biographical record: authored once, never computed at render time.
  std::string race;
  int age = 0;
  std::string homeworld;
  std::string hobby;
  std::string religion;

  // Standing assignment. `jobAt` is read BY THE JOB: a condition percentage
  // for the engineer, an air stage for the filter hand, a rate for the cook.
  std::string job;
  std::string skill;
  double jobAt = 0;

  // What the quarters last heard them say, and where in the cycle they are.
  std::string said;
  std::string saidKey;
  int saidN = 0;
};

// ---------------------------------------------------------------------------
// Hull readouts (STATS). Right-clickable in the reference; clamped 0..100.
// ---------------------------------------------------------------------------
struct Stat {
  std::string id;
  std::string label;
  double value = 0;
  std::string unit;
  std::string colour;
};

// ---------------------------------------------------------------------------
// Ongoing work. The payload fields are optional by type, exactly as the
// reference hangs them on the object.
// ---------------------------------------------------------------------------
// What the commander has staged for ONE hand. Staging is free; handing the
// sheet over is the single action. The job is staged separately from the
// orders because a job is a trade, not a chore.
struct OrderSheet {
  bool hasJob = false;
  std::string job;
  std::map<std::string, bool> flags;

  bool Empty() const { return !hasJob && flags.empty(); }
  bool Has(const std::string& id) const {
    auto it = flags.find(id);
    return it != flags.end() && it->second;
  }
  void Set(const std::string& id, bool on) {
    if (on) { flags[id] = true; } else { flags.erase(id); }
  }
};

struct Task {
  std::string id;
  std::string label;
  double total = 0;
  double left = 0;
  std::string state = "running";  // running | done
  std::string type;
  std::string target;

  std::string by;     // crew name credited with the work
  std::string who;    // crew id, where the task is about a person
  std::string other;  // second person (shared meals)

  double slot = -1;   // o2slot: which of the three slots
  double rate = 0;    // cook: rations per action
  double vol = 0;     // salvage: cubic metres held
  std::string payload;  // observe: the body recorded; dream: unused

  // A dream carries both halves of what the crew consolidated: their ids, kinds,
  // texts and the two people each half was about.
  std::vector<std::string> dreamIds;
  std::vector<std::string> dreamKinds;
  std::vector<std::string> dreamTexts;
  std::vector<std::string> dreamWho;
  std::vector<std::string> dreamOther;
  bool rest = false;  // crewJob payload: the rest that autoDesires queues
  bool hasOrders = false;

  // The order sheet a crewJob carries, applied when the work finishes.
  bool sheetHasJob = false;
  std::string sheetJob;
  std::map<std::string, bool> sheetFlags;
  // The whole sheet, on the crewOrder bar: one task per person, one bar for all.
  std::string sheetOwner;
};

// ---------------------------------------------------------------------------
// Incidents. Definitions live in progression.json; these are live instances.
// ---------------------------------------------------------------------------
struct Incident {
  std::string id;
  std::string type;
  std::string kind;
  std::string source;
  int stage = 0;
  double left = 0;
  std::string a;
  std::string b;
};

// ---------------------------------------------------------------------------
// Simulacron: bounded memory, bonds and the signal story.
// ---------------------------------------------------------------------------
struct Memory {
  double id = 0;
  std::string kind;
  std::string text;
  double at = 0;
  std::string who;
  std::string other;
  bool processed = false;
};

struct Dream {
  double at = 0;
  std::string text;
};

struct SimState {
  int version = 1;
  double serial = 1;
  double insight = 0;
  double sensor = 0;
  std::string signal = "dormant";
  std::map<std::string, double> bonds;   // "a~b" -> -100..100
  std::map<std::string, bool> observed;
  std::map<std::string, bool> broadcasts;
  std::vector<Dream> dreams;
  std::vector<Memory> memories;
};

struct Chronicle {
  int version = 1;
  std::map<std::string, bool> systems;
  std::map<std::string, bool> regions;
  double banks = 0;
  double projects = 0;
  double dreams = 0;
  double meals = 0;
  double incidents = 0;
  std::map<std::string, bool> earned;
  std::map<std::string, bool> claimed;
};

struct Commission {
  std::string kind;
  std::string origin;
  std::string target;
  double progress = 0;
  double reward = 0;
};

struct Commissions {
  int version = 1;
  double serial = 0;
  double completed = 0;
  std::optional<Commission> active;
};

struct Options {
  bool largeText = false;
  bool confirmRisk = false;
  bool hideCrewLabels = false;
  std::string theme = "classic";
  bool soundEffects = true;
  bool music = true;
  double audioLevel = 0.4;
  double displayGamma = 1.0;
  double displayBrightness = 1.0;
  // Phase 9: typography floor and the four steps above it, plus the borderless
  // window. Both are free preferences, saved with the voyage like the rest.
  int fontStep = 0;        // 0..3 -> 18, 19, 20, 21 px
  bool fullscreen = false;
};

// A cooldown belongs to one control and is counted in actions, never seconds.
struct Cooldown {
  std::string category;
  double remaining = 0;
};

struct FeedLine {
  std::string stamp;  // wall clock in the reference; excluded from parity
  std::string text;
};

// ---------------------------------------------------------------------------
// Stores. These are mutable game state, not constants: LIFE.canisters is the
// rack, MESS.rack is the counter.
// ---------------------------------------------------------------------------
struct CanisterSlot {
  std::string id;
  std::string name;
  std::string state = "empty";  // empty | installed | active
};

struct LifeStore {
  double canisters = 0;
  double capacity = 0;
  std::vector<CanisterSlot> slots;
};

struct GoodsLot {
  std::string id;
  std::string name;
  std::string kind;
  double vol = 0;
  double value = 0;
  bool risk = false;
  std::string contractId;
};

struct SurveyRecord {
  std::string id;
  std::string name;
  std::string src;
  double value = 0;
  std::string state;  // filed | packaged
};

// ---------------------------------------------------------------------------
// The chart's live view. The authored geometry lives in the catalogue; this is
// where the ship is, which layer is on screen, and how each layer was left.
// ---------------------------------------------------------------------------
struct MapView {
  std::string layer = "system";
  std::string here = "kessler";
  std::string sector = "gal-inner";
  double zoom = 1;
  double cx = 319;
  double cy = 139;
  double w = 638;
  double h = 278;
  double vw = 638;
  double vh = 278;
  double pxPerAction = 16;
  double lyPerAction = 0.42;
  double grid = 45;
  double zmin = 0.6;
  double zmax = 2.0;
  double zstep = 0.2;
  double vpy = 0.35;
  double rungs = 9;
  double rungPow = 2.1;
};

// One scale's remembered pan, zoom and selection.
struct LayerView {
  double zoom = 1;
  double px = 0;
  double py = 0;
  std::string sel;
};

// The hull the crew are standing in. Authored display fields are kept verbatim
// so a save round-trips what the reference wrote.
struct ShipDef {
  std::map<std::string, std::string> fields;

  std::string Get(const std::string& key) const {
    auto it = fields.find(key);
    return it == fields.end() ? std::string() : it->second;
  }
  double Berths() const {
    const std::string value = Get("berths");
    return value.empty() ? 0.0 : std::stod(value);
  }
};

// A right-clickable readout in the side panel.
struct InfoRow {
  std::string id;
  std::string label;
  std::string value;
};

struct Cargo {
  double capacity = 40;
};

struct Mess {
  double rack = 18;
  double capacity = 24;
};

// An open task tab. The reference keeps these as objects so a restored voyage
// reopens exactly what the player had on screen.
struct Tab {
  std::string key;
  std::string id;
  std::string kind;
  std::string title;
  std::string kindLabel;
  std::string sub;
};

struct Passenger {
  std::string id;
  std::string name;
};

// ---------------------------------------------------------------------------
// STATE itself.
// ---------------------------------------------------------------------------
struct GameState {
  double medkits = 2;

  bool reducedMotion = false;
  std::string shipClass = "wren";

  Options options = {};
  Chronicle chronicle = {};
  Commissions commissions = {};

  std::map<std::string, bool> spOpen;

  std::vector<Task> ongoing;
  std::optional<std::string> navTarget;
  std::map<std::string, bool> scanned;
  SimState sim = {};

  // The engine. Decays with attention, not with time.
  double engineCondition = 100;
  double wearTick = 0;

  // Life support: charge in the bank, and the breathable air in the cabin.
  double filterLife = 0;
  double airHold = 20;
  bool rackWarned = false;
  bool rackWarnedMess = false;

  std::string crewSel = "rell";
  std::map<std::string, bool> pendingOrders;
  double crewTick = 0;
  double shiplets = 340;
  double payTick = 0;
  bool docked = false;
  double fuelCells = 12;
  double repairKits = 3;
  double lotSerial = 0;
  std::map<std::string, std::string> contracts;  // contract id -> active/ready/complete/failed
  std::vector<Passenger> passengers;
  std::map<std::string, bool> upgrades;
  std::map<std::string, bool> hired;
  std::string gameOver;

  std::vector<FeedLine> feed;

  // UI-only in the reference, kept here because they are saved with the voyage.
  std::string activeTab = "home";
  std::vector<Tab> tabs;
  std::map<std::string, bool> freshTabs;
  // Transient presentation request: never serialized, never a world action.
  std::string tabRevealKey;
  double popups = 0;
  std::map<std::string, Cooldown> actionCooldowns;
};

}  // namespace ship
