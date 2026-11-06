// ============================================================================
//  simulation_tests.cpp — the parts of the action loop worth pinning down.
//  ---------------------------------------------------------------------------
//  Selected differential scenarios are exercised by tools/trace-voyage.cjs
//  against the reference and diffed with spaceship_console. That coverage is a
//  useful model check, not proof of complete gameplay or UI parity. These unit
//  tests pin focused invariants that should fail loudly when edited incorrectly.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/Simulation.h"
#include "data/Catalog.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog = ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::ReferenceVoyage(World()); }

bool FeedContains(const ship::Simulation& sim, const std::string& text) {
  for (const std::string& line : sim.FeedText()) {
    if (line == text) { return true; }
  }
  return false;
}

}  // namespace

TEST_CASE("a new voyage opens where the reference opens", "[simulation]") {
  const ship::Simulation sim = Fresh();

  REQUIRE(sim.ActionCount() == 0);
  REQUIRE(sim.Crew().size() == 4);
  REQUIRE(sim.Crew()[0].id == "rell");
  REQUIRE(sim.Crew()[1].id == "jax");

  REQUIRE(sim.StatValue("hull") == 100);
  REQUIRE(sim.StatValue("energy") == 88);
  REQUIRE(sim.StatValue("shield") == 62);
  REQUIRE(sim.StatValue("fuel") == 74);

  REQUIRE(sim.State().engineCondition == 100);
  REQUIRE(sim.MessRack() == 18);
  REQUIRE(sim.MessCapacity() == 24);

  REQUIRE(sim.Life().canisters == 6);
  REQUIRE(sim.Life().slots.size() == 3);
  for (const ship::CanisterSlot& slot : sim.Life().slots) { REQUIRE(slot.state == "empty"); }

  REQUIRE(sim.State().medkits == 2);
  REQUIRE(sim.State().repairKits == 3);
  REQUIRE(sim.State().fuelCells == 12);
  REQUIRE(sim.State().shiplets == 340);
  REQUIRE(sim.State().feed.empty());

  // The archive starts with one authored memory, not an earned one.
  REQUIRE(sim.State().sim.memories.size() == 1);
  REQUIRE(sim.State().sim.memories[0].kind == "Watch");
  REQUIRE(sim.State().sim.memories[0].at == 0);
}

TEST_CASE("the clock spends reserve and wears the drive", "[simulation]") {
  ship::Simulation sim = Fresh();

  for (int i = 0; i < 19; ++i) { sim.PerformAction(); }
  REQUIRE(sim.StatValue("energy") == 88);  // every twentieth action
  REQUIRE(sim.State().engineCondition == 96);  // four steps of wear by action 16
  REQUIRE(sim.State().wearTick == 3);

  sim.PerformAction();
  REQUIRE(sim.StatValue("energy") == 87);
  REQUIRE(sim.State().engineCondition == 95);  // the fifth step lands on the twentieth action
  REQUIRE(sim.State().wearTick == 0);
}

TEST_CASE("seating a canister costs an action and two of work", "[simulation]") {
  ship::Simulation sim = Fresh();

  REQUIRE(sim.StartCanisterInstall(0));
  REQUIRE(sim.ActionCount() == 1);
  REQUIRE(sim.Life().canisters == 5);
  REQUIRE(sim.Life().slots[0].state == "empty");  // seated is not installed

  const ship::Task* task = sim.FindOngoing("o2s0");
  REQUIRE(task != nullptr);
  REQUIRE(task->type == "o2slot");
  REQUIRE(task->total == 2);
  REQUIRE(task->left == 2);
  REQUIRE(task->target == "s0");

  sim.PerformAction();
  REQUIRE(sim.Life().slots[0].state == "empty");  // one action of work left
  sim.PerformAction();
  REQUIRE(sim.Life().slots[0].state == "installed");
  REQUIRE(FeedContains(sim, "Canister seated in Slot 1."));
}

TEST_CASE("a full rack starts the bank, and the bank is worth two hundred actions",
          "[simulation]") {
  ship::Simulation sim = Fresh();
  for (int slot = 0; slot < 3; ++slot) { sim.StartCanisterInstall(slot); }
  for (int i = 0; i < 6; ++i) { sim.PerformAction(); }

  REQUIRE(sim.BankReady());
  REQUIRE(sim.StartFilterBank());
  REQUIRE(sim.State().filterLife == 200);
  REQUIRE(sim.State().chronicle.banks == 1);
  for (const ship::CanisterSlot& slot : sim.Life().slots) { REQUIRE(slot.state == "active"); }

  // The bank holds the top air stage while it runs.
  REQUIRE(sim.AirStageIndex() == 4);
  REQUIRE(sim.CrewSlow() == 1.0);

  // Fifty actions at four a piece, then the room's own air is all there is.
  for (int i = 0; i < 50; ++i) { sim.PerformAction(); }
  REQUIRE(sim.State().filterLife == 0);
  REQUIRE(sim.State().airHold == 20);
  for (const ship::CanisterSlot& slot : sim.Life().slots) { REQUIRE(slot.state == "empty"); }

  sim.PerformAction();
  REQUIRE(sim.State().airHold == 19);
  REQUIRE(sim.AirStageIndex() == 4);
}

TEST_CASE("the air has five visible steps", "[simulation]") {
  ship::Simulation sim = Fresh();
  ship::GameState& state = sim.MutableState();

  state.airHold = 20.0;  REQUIRE(sim.AirStageIndex() == 4);
  state.airHold = 16.0;  REQUIRE(sim.AirStageIndex() == 4);
  state.airHold = 15.0;  REQUIRE(sim.AirStageIndex() == 3);
  state.airHold = 10.0;  REQUIRE(sim.AirStageIndex() == 2);
  state.airHold = 5.0;   REQUIRE(sim.AirStageIndex() == 1);
  state.airHold = 1.0;   REQUIRE(sim.AirStageIndex() == 1);
  state.airHold = 0.0;   REQUIRE(sim.AirStageIndex() == 0);
}

TEST_CASE("taking a job writes what the reference writes", "[simulation]") {
  ship::Simulation sim = Fresh();

  // A hand with no skill is marked untrained by taking work, not before it.
  REQUIRE(sim.FindCrew("rell")->skill.empty());
  REQUIRE(sim.ApplyJob("rell", "cleaner"));
  REQUIRE(sim.FindCrew("rell")->job == "cleaner");
  REQUIRE(sim.FindCrew("rell")->skill == "Untrained");
  REQUIRE(sim.FindCrew("rell")->room == "quarters");
  REQUIRE(sim.FindCrew("rell")->jobAt == 0);
  REQUIRE(FeedContains(sim, "Rell Aventine now holds the cleaner job."));

  // A unique job relieves whoever held it, and says so by name.
  REQUIRE(sim.ApplyJob("rell", "filters"));
  REQUIRE(FeedContains(sim, "Bo Ferreira was relieved of the filters job."));
  REQUIRE(sim.FindCrew("bo")->job.empty());
  REQUIRE(sim.FindCrew("rell")->jobAt == 1);  // jobBase("filters")

  // Giving it up.
  REQUIRE(sim.ApplyJob("rell", ""));
  REQUIRE(FeedContains(sim, "Rell Aventine has no standing job."));
}

TEST_CASE("payday pays the dearest job first and lays off the rest", "[simulation]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().shiplets = 20;

  for (int i = 0; i < 100; ++i) { sim.PerformAction(); }

  // Bo (filters, 15) is the first of the two equal dearest wages, so Bo is the
  // one hand 20 sh can pay; Suri (cook, 15) and Jax (engineer, 10) come off the
  // payroll. The reference's insertion sort is stable, and so is this one.
  REQUIRE(FeedContains(sim, "Payday. 15 sh to 1 hand. 5 sh left."));
  REQUIRE(FeedContains(sim, "The purse came up short. 2 hands are off the payroll."));
  REQUIRE(sim.FindCrew("bo")->job == "filters");
  REQUIRE(sim.FindCrew("bo")->wallet == 45);  // 30 authored + 15 paid
  REQUIRE(sim.FindCrew("jax")->job.empty());
  REQUIRE(sim.FindCrew("suri")->job.empty());

  // Off the payroll is not off the ship: the hand stays aboard, poorer in morale.
  REQUIRE(sim.Crew().size() == 4);
  // 64 authored, minus a point per ten actions for working untrained, minus
  // the twelve-point hit for coming off the payroll.
  REQUIRE(sim.FindCrew("jax")->morale == 42);
}
