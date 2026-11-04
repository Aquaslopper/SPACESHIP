// ============================================================================
//  Simulation.h — the voyage: one clock, one state, no window.
//  ---------------------------------------------------------------------------
//  Everything the reference does between performAction() and setStatus() lives
//  here, split across translation units by subsystem:
//
//    Action.cpp       performAction, tickWorld and its exact 13-step order
//    Survival.cpp     engine wear, the filter bank, the air, the stores
//    CrewLogic.cpp    crew ticks, hunger, jobs, payroll, the loss check
//    Tasks.cpp        the ongoing-task engine and its dispatch table
//    Cooldowns.cpp    per-control action-count cooldowns
//    Incidents.cpp    reports, escalation, dispute pairs
//    Simulacron.cpp   memories, bonds, desires
//    Awards.cpp       commendations
//    Trace.cpp        the canonical snapshot used for parity testing
//
//  Nothing in this class touches a renderer, a file or a clock. That is what
//  makes the differential harness possible: the same command script produces
//  the same numbers here and in the Node VM.
// ============================================================================
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Constants.h"
#include "core/GameState.h"
#include "data/Catalog.h"
#include "data/Types.h"

namespace ship {

struct JobDef {
  std::string id;
  std::string label;
  bool unique = false;
  double wage = 0;
  double baseAt = 0;  // jobBase(): the rate a hand starts at
  std::string room;   // where the job is worked
};

class Simulation {
 public:
  // A voyage in the state the reference reaches the moment its script has
  // finished loading: full stores, three canisters in the rack, three jobs.
  static Simulation NewVoyage(const Catalog& catalog);

  // ---- the clock ---------------------------------------------------------
  // The only function that advances the world. Mirrors performAction(detail).
  void PerformAction(const std::string& detail = std::string());
  // A burst of work that is NOT an action: the filter bank's reward.
  void ProAction(double n);

  // ---- commands that exist in Phase 1A -----------------------------------
  bool StartCanisterInstall(int slot);  // the player carries a canister over
  bool StartFilterBank();               // the player throws the switch
  bool ApplyJob(const std::string& crewId, const std::string& jobId);  // scenario setup

  // ---- queries -----------------------------------------------------------
  double ActionCount() const { return actionCounter_; }
  const GameState& State() const { return state_; }
  GameState& MutableState() { return state_; }
  const std::vector<CrewMember>& Crew() const { return crew_; }
  const std::vector<Stat>& Stats() const { return stats_; }
  const LifeStore& Life() const { return life_; }
  double MessRack() const { return messRack_; }
  double MessCapacity() const { return messCapacity_; }
  const std::vector<Incident>& Events() const { return events_; }
  const std::vector<GoodsLot>& Goods() const { return goods_; }
  const Catalog& World() const { return catalog_; }
  const Rules& GameRules() const { return rules_; }
  std::vector<std::string> FeedText() const;

  // Canonical snapshot, sorted keys, JS number formatting. Used by the
  // console harness and by the parity tests.
  std::string TraceJson() const;

  // ---- subsystems, public so the tick files and the tests can share them --
  double StatValue(const std::string& id) const;
  Stat* FindStat(const std::string& id);
  void ClampStat(const std::string& id, double delta);

  int AirStageIndex() const;
  const struct AirStage& AirSpec() const;
  bool BankRunning() const;
  bool BankReady() const;
  double CrewSlow() const;
  double CrewCost(double n) const;
  double AirPercent() const;

  void LogLine(const std::string& text);
  void SimRemember(const std::string& kind, const std::string& text,
                   const std::string& who = std::string(),
                   const std::string& other = std::string());
  double SimBond(const std::string& a, const std::string& b) const;
  void SimChangeBond(const std::string& a, const std::string& b, double amount);
  std::string SimCrewName(const std::string& id) const;

  Task* FindOngoing(const std::string& id);
  const Task* FindOngoing(const std::string& id) const;
  Task* AddOngoing(const std::string& id, const std::string& label, double total,
                   const std::string& type, const std::string& target = std::string());
  void ClearOngoing(const std::string& id);

  CrewMember* FindCrew(const std::string& id);
  const CrewMember* FindCrew(const std::string& id) const;
  const Task* CrewBusy(const CrewMember& crew) const;

  void RunBank(const std::string& line);
  void LayOff(CrewMember& crew, const std::string& why);
  double WageOf(const CrewMember& crew) const;
  void RemoveIncident(const std::string& id);
  void CheckCommendations(bool silent = false);

 private:
  void TickWorld();

  // Survival.cpp
  void WearEngine();
  void TickFilters();

  // CrewLogic.cpp
  void TickCrew();
  void TickCleaner();
  void TickPayroll();
  void CheckLoss();
  CrewMember* AssignedEngineer();
  CrewMember* AssignedFilterHand();
  CrewMember* AssignedCook();
  double CleanerInterval(const CrewMember& crew) const;
  double EffectiveJobAt(const CrewMember& crew) const;
  double MaintCost() const;
  bool MaintRunning() const;
  void ApplyOrderPayload(CrewMember& crew, const Task& task);

  // Tasks.cpp
  double AdvanceTasks(double n);
  void TickOngoing();
  void SweepAutoTasks();
  void OnTaskDone(Task& task);
  void TickTask(const Task& task, double used);

  // Cooldowns.cpp
  void TickActionCooldowns();

  // Incidents.cpp
  const IncidentType* IncidentSpec(const Incident& incident) const;
  void IncidentEffect(const IncidentType::Stage& step, const Incident& incident);
  void TickIncidents();
  std::pair<std::string, std::string> SimDisputePair() const;

  // Simulacron.cpp
  void TickSim();
  void AutoDesires();
  std::string SimDrive(const CrewMember& crew) const;
  std::string SimAnchor(const CrewMember& crew) const;

  // Awards.cpp
  double ChronicleValue(const std::string& metric) const;

  // Automation that follows the clock
  void AutoMaint();
  void AutoFilters();
  void AutoMess();

  Catalog catalog_;
  Rules rules_;
  GameState state_;
  std::vector<CrewMember> crew_;
  std::vector<Stat> stats_;
  LifeStore life_;
  std::vector<Incident> events_;
  std::vector<GoodsLot> goods_;
  std::vector<SurveyRecord> survey_;
  std::vector<JobDef> jobs_;

  double actionCounter_ = 0;
  double messRack_ = 0;
  double messCapacity_ = 0;

  // Re-entry guards. A command spends its action BEFORE it registers the work
  // it starts, and the tick inside that action must not let a crew member grab
  // the same job twice.
  bool maintClaim_ = false;
  bool o2Claim_ = false;
  bool cookClaim_ = false;
  std::string projectClaim_;
  std::map<std::string, bool> crewClaims_;
};

}  // namespace ship
