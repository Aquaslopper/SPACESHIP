#include "core/Simulation.h"
#include "ui/NavScreen.h"
#include "ui/Shell.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
  const auto catalog = ship::Catalog::LoadFromAssets("assets");
  auto sim = ship::Simulation::NewVoyage(catalog);
  sim.MutableMap().here = sim.World().bodies.front().id;
  sim.MutableState().engineCondition = 100;
  sim.MutableState().airHold = sim.GameRules().air.holdMax;
  assert(ship::ComputeRadiationTelemetry(sim).alertLevel == 0.0f);

  ship::Task task;
  task.id = "test";
  task.total = 100;
  task.left = 100;
  sim.MutableState().ongoing.push_back(task);
  assert(!ship::IndicatorLines(sim).back().grow);
  assert(ship::ComputeRadiationTelemetry(sim).alertLevel == 0.0f);

  sim.MutableState().engineCondition = sim.GameRules().engine.warnBelow * 0.5;
  assert(std::abs(ship::ComputeRadiationTelemetry(sim).alertLevel - 0.5f) < 0.001f);
  sim.MutableState().airHold = sim.GameRules().air.step;
  assert(std::abs(ship::ComputeRadiationTelemetry(sim).alertLevel - 0.75f) < 0.001f);
  sim.MutableState().airHold = sim.GameRules().air.holdMax;
  sim.MutableState().engineCondition = 100;
  assert(ship::ComputeRadiationTelemetry(sim).alertLevel == 0.0f);

  sim.MutableState().ongoing.clear();
  auto save = nlohmann::ordered_json::parse(sim.SaveJson());
  save["events"].push_back({{"id", "test-radiation"}, {"type", "radiation"},
                             {"stage", 1}, {"left", 6}, {"a", ""}, {"b", ""}});
  auto incident = ship::Simulation::NewVoyage(catalog);
  std::string error;
  if (!incident.LoadJson(save.dump(), &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  assert(std::abs(ship::ComputeRadiationTelemetry(incident).alertLevel - 0.5f) < 0.001f);
  save["events"][0]["stage"] = 2;
  save["events"][0]["left"] = 1;
  assert(incident.LoadJson(save.dump(), &error));
  assert(ship::IndicatorLines(incident).back().pct > 100);
  assert(ship::ComputeRadiationTelemetry(incident).alertLevel == 1.0f);
  std::cout << "PASS: red engine, air and incident indicators drive wave alert; green tasks do not\n";
}
