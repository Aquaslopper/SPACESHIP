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
  bool rest = false;  // crewJob payload: the only one Phase 1A can create
  bool hasOrders = false;
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

struct SimState {
  int version = 1;
  double serial = 1;
  double insight = 0;
  double sensor = 0;
  std::string signal = "dormant";
  std::map<std::string, double> bonds;   // "a~b" -> -100..100
  std::map<std::string, bool> observed;
  std::map<std::string, bool> broadcasts;
  std::vector<std::string> dreams;
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
  std::string target;
  double progress = 0;
  double serial = 0;
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
  std::vector<std::string> passengers;
  std::map<std::string, bool> upgrades;
  std::map<std::string, bool> hired;
  std::string gameOver;

  std::vector<FeedLine> feed;

  // UI-only in the reference, kept here because they are saved with the voyage.
  std::string activeTab = "home";
  std::vector<std::string> tabs;
  double popups = 0;
  std::map<std::string, Cooldown> actionCooldowns;
};

}  // namespace ship
