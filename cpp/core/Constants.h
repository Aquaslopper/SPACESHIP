#pragma once

#include <map>
#include <string>
#include <vector>

namespace ship {

struct EngineRules {
  double wearEvery = 2;
  double wearStep = 1;
  double warnBelow = 50;
  double engineerAt = 60;
  double dryEnergy = 1;
};

struct AirStage {
  std::string word;
  std::string tone;
  double slow = 1;
  double harm = 0;
};

struct AirRules {
  double holdMax = 20;
  double step = 5;
  std::vector<AirStage> stages;
};

struct LifeRules {
  double lifeMax = 200;
  double decay = 4;
  double capacity = 10;
};

struct MessRules {
  double rackStart = 18;
  double capacity = 24;
  double every = 25;
  double starveAt = 80;
  double starveEvery = 4;
  double rationStart = 21;
  double rationCapacity = 24;
  double foodPerRation = 5;
  double cookBelow = 10;
  double hungerPerMeal = 15;
  double hungerPerFood = 5;
  double hungerFloor = 5;
};

struct PayRules {
  double every = 100;
  double hit = 12;
};

struct ReportRules {
  double hullLine = 60;
  double energyLine = 60;
};

struct ChargeRule {
  double volume = 0;
  std::string label;
};

struct Rules {
  EngineRules engine;
  AirRules air;
  LifeRules life;
  MessRules mess;
  PayRules pay;
  ReportRules reports;
  double docAway = 7;
  double scanCost = 5;
  std::map<std::string, ChargeRule> charges;
};

}  // namespace ship
