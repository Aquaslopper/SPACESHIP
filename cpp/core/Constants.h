// ============================================================================
//  Constants.h — the tunables, loaded from the catalogue.
//  ---------------------------------------------------------------------------
//  In the reference these are a handful of literals near the top of the file
//  (ENGINE, AIR, AIR_STAGES, MESS, PAY, LIFE...). They are exported to
//  assets/data/survival.json so the C++ port and the HTA cannot drift apart,
//  but they are fixed for the life of a process: nothing here changes during
//  play. Mutable stores (the canister rack, the ration counter) live in
//  GameState.h instead.
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ship {

struct EngineRules {
  double wearEvery = 4;   // actions per step of wear
  double wearStep = 1;    // percent lost per step
  double warnBelow = 50;  // sidebar bar appears here
  double perAction = 10;  // 1 action of work per 10% missing
};

struct AirStage {
  std::string word;
  std::string tone;
  double slow = 1;     // multiplier on work done by the crew
  double jobDrop = 0;  // how far a job threshold slips
  double harm = 0;     // one health point every N actions, 0 = none
};

struct AirRules {
  double holdMax = 20;
  double step = 5;
  std::vector<AirStage> stages;  // index 0 = foul .. 4 = optimal
};

struct LifeRules {
  double lifeMax = 200;  // a full charge, in actions
  double decay = 4;      // lost per action spent anywhere aboard
  double capacity = 10;  // canisters the rack holds
};

// The galley, as the owner specified it. Two stores, not one:
//   mess.rack     the cooked food sitting on the galley counter
//   mess.rations  dry rations in the larder - one is spent per batch cooked,
//                 and only buying or producing rations adds to it
//
// The whole room is one cycle of the crew's own clock, and it adds up exactly:
//   hunger climbs 15 over a hand's 25 clicks; one food drains 5 of it, so a
//   meal is 3 food and four hands eat 12 a round. The counter holds 24 - two
//   rounds - and the cook restarts at 10, so there is always food on the shelf
//   while the next batch cooks.
struct MessRules {
  double rackStart = 18;    // food on the galley counter at boot
  double capacity = 24;     // the counter's hard ceiling - two crew rounds
  double every = 25;        // each crew member's own hunger clock
  double starveAt = 90;     // hunger at which health starts to go
  double starveEvery = 8;
  double rationStart = 21;      // dry rations aboard at boot
  double rationCapacity = 24;   // what the dry larder holds
  double foodPerRation = 5;     // one cooked ration becomes this much food
  double cookBelow = 10;        // the cook starts once the counter is under this
  double hungerPerMeal = 15;    // hunger gained over one full 25-click clock
  double hungerPerFood = 5;     // one food drains this much hunger
  double hungerFloor = 5;       // a hand stops eating here - hunger never vanishes
};

struct PayRules {
  double every = 100;  // actions between paydays
  double hit = 12;     // morale lost by a hand nobody could pay
};

struct CooldownRule {
  double actions = 0;
  std::string label;
};

struct Rules {
  EngineRules engine;
  AirRules air;
  LifeRules life;
  MessRules mess;
  PayRules pay;
  double docAway = 7;
  double scanCost = 5;
  std::map<std::string, CooldownRule> cooldowns;  // routine/systems/space/critical
};

}  // namespace ship
