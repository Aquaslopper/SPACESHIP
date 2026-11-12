#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "data/Types.h"

namespace ship {

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

  std::string race;
  int age = 0;
  std::string homeworld;
  std::string hobby;
  std::string religion;

  std::string job;
  std::string skill;
  double jobAt = 0;

  std::string said;
  std::string saidKey;
  int saidN = 0;
};

struct Stat {
  std::string id;
  std::string label;
  double value = 0;
  std::string unit;
  std::string colour;
};

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
  std::string state = "running";
  std::string type;
  std::string target;

  std::string by;
  std::string who;
  std::string other;

  double slot = -1;
  double vol = 0;
  bool rest = false;
  bool hasOrders = false;

  bool sheetHasJob = false;
  std::string sheetJob;
  std::map<std::string, bool> sheetFlags;
};

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

struct Chronicle {
  int version = 1;
  std::map<std::string, bool> systems;
  std::map<std::string, bool> regions;
  double banks = 0;
  double projects = 0;
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

inline std::string TradeKey(const std::string& mode, const std::string& to,
                            const std::string& good) {
  return mode + "|" + to + "|" + good;
}

struct TradeContract {
  std::string mode;
  std::string from;
  std::string to;
  std::string good;
  int crates = 0;
  double fee = 0.0;
  double freight = 0.0;
  std::string label;
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
  int fontStep = 0;
  bool fullscreen = false;
};

struct Charge {
  double volume = 0.0;
  double left = 0.0;
};

struct FeedLine {
  std::string stamp;
  std::string text;
};

struct CanisterSlot {
  std::string id;
  std::string name;
  std::string state = "empty";
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
  std::string triggerId;
};

struct SurveyRecord {
  std::string id;
  std::string name;
  std::string src;
  double value = 0;
  std::string state;
};

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

struct LayerView {
  double zoom = 1;
  double px = 0;
  double py = 0;
  std::string sel;
};

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
  double rations = 21;
  double rationCapacity = 24;
};

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
  std::map<std::string, bool> mapMarks;

  double engineCondition = 100;
  double wearTick = 0;
  bool engineerRun = false;

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
  std::map<std::string, std::string> contracts;
  std::vector<Passenger> passengers;
  std::map<std::string, bool> upgrades;
  std::map<std::string, bool> hired;
  std::map<std::string, bool> visitedPorts;
  std::map<std::string, int> systemSerials;
  int nextSystemSerial = 1;
  std::vector<TradeContract> trades;
  std::map<std::string, bool> spentTrades;
  std::map<std::string, int> freelanceRejects;
  std::map<std::string, bool> talked;
  std::string gameOver;

  std::vector<FeedLine> feed;

  std::string activeTab = "home";
  std::vector<Tab> tabs;
  std::map<std::string, bool> freshTabs;
  std::string tabRevealKey;
  double popups = 0;
  std::map<std::string, Charge> charges;
};

}  // namespace ship
