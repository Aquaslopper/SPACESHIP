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
  double cost = 1;    // what handing the trade over costs, in actions
  double learnOn = 0, learnSt = 0;    // the price of a lesson, in actions
  double feeOn = 0, feeSt = 0;        // ...and in shiplets
  double trainStep = 0, trainCap = 0;
  std::string unit;                   // "%" / " air" / " ration/action"
  std::string cloth, dark;            // what the sprite in the quarters wears
};

// Builds a live crew member from an authored definition. Shared by the new
// voyage and by a hiring at a port.
CrewMember MakeCrew(const CrewDefinition& def);

class Simulation {
 public:
  // A voyage in the state the reference reaches the moment its script has
  // finished loading: full stores, three canisters in the rack, three jobs.
  static Simulation NewVoyage(const Catalog& catalog);

  // ---- the clock ---------------------------------------------------------
  // The only function that advances the world. Mirrors performAction(detail).
  void PerformAction(const std::string& detail = std::string());
  // restartVoyage(): a fresh voyage in the state NewVoyage produces, with the
  // player's preferences carried across. Free - it is the new-voyage command,
  // not a turn.
  void RestartVoyage();

  // A burst of work that is NOT an action: the filter bank's reward.
  void ProAction(double n);

  // ---- commands that exist in Phase 1A -----------------------------------
  bool StartCanisterInstall(int slot);  // the player carries a canister over
  bool StartFilterBank();               // the player throws the switch
  bool StartMaintenanceSelf();          // startMaint(e, "maint", "the commander")
  bool StartCookingSelf();              // startCook(e, "cook", "the commander")
  bool ServiceSystem(const std::string& statId);  // buildSystemScreen() service row
  bool ApplyJob(const std::string& crewId, const std::string& jobId);  // scenario setup

  // ---- queries -----------------------------------------------------------
  double ActionCount() const { return actionCounter_; }
  double MessRack() const { return mess_.rack; }
  double MessCapacity() const { return mess_.capacity; }
  const GameState& State() const { return state_; }
  GameState& MutableState() { return state_; }
  const std::vector<CrewMember>& Crew() const { return crew_; }
  // The quarters needs to write down what a hand just said; nothing else does.
  std::vector<CrewMember>& MutableCrew() { return crew_; }
  const std::vector<Stat>& Stats() const { return stats_; }
  const LifeStore& Life() const { return life_; }
  LifeStore& MutableLife() { return life_; }
  Cargo& MutableCargo() { return cargo_; }
  Mess& MutableMess() { return mess_; }
  std::vector<GoodsLot>& MutableGoods() { return goods_; }
  const std::vector<Incident>& Events() const { return events_; }
  const std::vector<GoodsLot>& Goods() const { return goods_; }
  const Catalog& World() const { return catalog_; }
  const MapView& Map() const { return map_; }
  MapView& MutableMap() { return map_; }
  const std::map<std::string, LayerView>& Views() const { return views_; }
  const std::vector<Room>& Rooms() const { return rooms_; }
  const ShipDef& Ship() const { return ship_; }
  const std::vector<InfoRow>& Infos() const { return infos_; }
  const Cargo& CargoStore() const { return cargo_; }
  const std::vector<Passenger>& Passengers() const { return state_.passengers; }
  const std::vector<Good>& Market() const { return catalog_.goods; }
  const std::vector<SurveyRecord>& Survey() const { return survey_; }
  std::vector<SurveyRecord>& MutableSurvey() { return survey_; }
  double ScanCost() const { return scanCost_; }
  bool InRange(const Body& body) const;
  bool AtTarget(const Body& body) const;
  std::string CurrentSystemId() const;
  const Body* CurrentSystem() const;
  const Body* FindBodyById(const std::string& id) const;
  const Body* HomeBody() const;
  // navMetrics() — distance, fuel, and the strings the panel prints.
  struct NavMetrics {
    double dist = 0;
    double fuel = 0;
    double total = 0;
    std::string ly;
    std::string km;
    std::string transit;
  };
  NavMetrics MetricsFor(const Body& body) const;
  CrewMember* AssignedNavigator();
  const Rules& GameRules() const { return rules_; }
  std::vector<std::string> FeedText() const;

  // ---- Phase 5: the order sheet (Quarters) -------------------------------
  // Staging is free: selecting a hand, a trade and a set of orders moves no
  // clock. Handing the sheet over is one action, and the work it names is the
  // price - the crew spend it carrying orders out while the ship runs on.
  const std::vector<JobDef>& Jobs() const { return jobs_; }
  const JobDef* FindJobDef(const std::string& jobId) const;
  const OrderSheet* SheetOf(const CrewMember& crew) const;
  std::string StagedJob(const CrewMember& crew) const;
  bool StageJob(const std::string& crewId, const std::string& jobId);  // free
  bool ToggleOrder(const std::string& crewId, const std::string& orderId);  // free
  void ClearOrders();
  int OrderedCount() const;
  bool OrdersRunning() const;
  bool CrewAvailable(const CrewMember& crew) const;  // ordersAvailable()
  double JobCost(const std::string& jobId) const;
  double OrderCostFor(const std::string& jobId, const std::string& orderId) const;
  double OrderCost(const CrewMember& crew, const std::string& orderId) const;
  double CrewCostOf(const CrewMember& crew) const;   // crewCostOf()
  double OrderFeeFor(const std::string& jobId, const std::string& orderId) const;
  double CrewFeeOf(const CrewMember& crew) const;
  double OrdersCost() const;   // the work, in actions
  double OrdersFee() const;    // the schooling, in shiplets
  double SheetCost(const CrewMember& crew, const OrderSheet& sheet) const;
  bool CommitOrders();         // fireAction + queueOrders()
  void ApplyJobTo(CrewMember& crew, const std::string& jobId);
  void TrainCrew(CrewMember& crew, int steps, const std::string& where);
  void FireCrew(CrewMember& crew);

  // ---- Phase 5: the read-only views the crew windows share --------------
  // Everything a crew window prints is derived at paint time, never cached,
  // so a tab left open can never go stale.
  double CrewWage(const CrewMember& crew) const;
  double PayrollBill() const;
  double PayrollDue() const;
  std::string JobThreshold(const CrewMember& crew) const;
  std::string CrewWhere(const CrewMember& crew) const;
  std::string SimActivityOf(const CrewMember& crew) const;
  std::string NeedKeyFor(const CrewMember& crew) const;
  std::string SimDriveOf(const CrewMember& crew) const;
  std::string SimAnchorOf(const CrewMember& crew) const;
  double NavigatorFuelDiscount(const CrewMember& crew) const;
  // The scene the quarters draws for one hand: pose and props.
  std::string AirStageWord() const;
  // NEED_LINES, and the quote cycle behind "Talk to <name>". Asking is free and
  // spells out a different line each time.
  std::string NeedLine(const std::string& needKey, int n, const std::string& jobLabel) const;
  std::string AskCrew(CrewMember& crew);

  // ---- Phase 1B commands ------------------------------------------------
  // Navigation (Navigation.cpp). Selection is free; only travel and scans cost.
  bool SelectTarget(const std::string& bodyId);
  bool StartTravel();                       // startTravel()
  bool StartScan();                         // startScan()
  bool StandWatch();                        // standWatch()
  bool Acknowledge(const std::string& taskId);  // the finished button
  void SetLayer(const std::string& key);    // free: looking never costs
  bool ZoomMap(int dir);                    // free: one step on the zoom ladder
  void PanMap(double dx, double dy);        // free: clamped per layer
  bool ViewSector(const std::string& regionId);
  bool Dock();                              // Dock at <port>
  bool Undock();

  // Economy (Economy.cpp)
  double LotPrice(const GoodsLot& lot) const;
  double CargoUsed() const;
  double CargoReserved() const;
  bool BuySupply(const std::string& id);     // fuel cell, kit, medkit, canister, rations
  bool BuyGood(const std::string& goodId);
  bool SellGood(const std::string& lotId);
  bool SellSurvey(const std::string& recordId);
  bool Jettison(const std::string& lotId);
  bool StartSalvage(int bay);
  bool PackageSurvey(const std::string& recordId);
  bool AcceptContract(const std::string& contractId);
  bool CollectContract(const std::string& contractId);
  bool HireCrew(const std::string& candidateId);
  bool BuyUpgrade(const std::string& upgradeId);   // bay, bay2, drive, rack, scanner, hull
  void ContractProgress(const std::string& goal, const std::string& target);
  void CommissionProgress(const std::string& kind, const std::string& target);
  // commissionOffer(): the same deterministic draw the board shows before the
  // player accepts it, so the offer and the acceptance can never disagree.
  std::optional<Commission> CommissionOffer(const std::string& kind) const;
  bool AcceptCommission(const std::string& kind);
  bool CollectCommission();
  bool AbandonCommission();

  // Projects and the shipyard (Projects.cpp, Ships.cpp)
  bool StartProject(const std::string& id);
  bool StartMedical(const std::string& crewId);
  bool StartSocial(const std::string& a, const std::string& b);
  std::string ProjectBlock(const std::string& id) const;
  void ApplyShipLayout();
  bool BuyLargerShip();
  double ShipCargoCapacity(const std::string& kind, int level) const;
  int CargoRefitLevel() const;

  // Persistence (SaveSystem.cpp)
  std::string SaveJson() const;                       // saveSnapshot(), pretty
  bool LoadJson(const std::string& text, std::string* error);   // validSave + apply
  // Slot 1 is written as UTF-16LE with a BOM, because that is the file the HTA
  // writes beside itself; slots 2 and 3 are UTF-8. The names match the original.
  static std::string SlotFilename(int slot);
  static bool SlotIsUtf16(int slot);
  bool SaveSlot(int slot, const std::string& directory, std::string* error) const;
  bool LoadSlot(int slot, const std::string& directory, std::string* error);
  bool SaveToFile(const std::string& path, bool utf16le, std::string* error) const;
  bool LoadFromFile(const std::string& path, std::string* error);
  static bool ValidSave(const std::string& text, const Catalog& catalog, std::string* why);

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

  // ---- Phase 7: the logbook ----------------------------------------------
  const Commendation* FindCommendation(const std::string& id) const;
  // The public face of the chronicle metric, for the logbook's own lines.
  double ChronicleValueFor(const std::string& metric) const { return ChronicleValue(metric); }
  double ReadyCommendations() const;      // earned and not yet claimed
  double EarnedCommendations() const;
  std::string CaptainRank() const;        // cosmetic, fed by the log itself
  std::string RewardText(const std::string& commendationId) const;
  std::string RewardTextOf(const Commendation& commendation) const;
  bool ClaimCommendation(const std::string& id);   // free, once only

  // ---- Phase 7: the signal story ------------------------------------------
  const std::vector<std::string>& WorldHistory(const std::string& regionId) const;
  bool BroadcastLocal();                       // the comms relay's traffic report
  bool FinishSignal(const std::string& choice);  // "preserved" or "sold"

  // ---- Phase 7: incidents -------------------------------------------------
  bool RemedyIncident(const std::string& incidentId);       // quick fix, maybe a kit
  bool AssignIncidentCrew(const std::string& incidentId);   // six actions of work
  const CrewMember* IncidentHand(const Incident& incident) const;
  const IncidentType::Stage* IncidentStage(const Incident& incident) const;

  // ---- Phase 7: the dream engine with a chosen pair ------------------------
  bool StartDream(const std::string& firstId, const std::string& secondId);

  // ---- read-only job lookups for the interface --------------------------
  // The briefing and the sidebar ask "who is holding this job" and "what does
  // that job act at" without starting anything, so these are const and free.
  const CrewMember* Engineer() const;
  const CrewMember* FilterHand() const;
  const CrewMember* Cook() const;
  double MaintCostActions() const;
  bool MaintRunningAny() const;   // the commander's spanner or the engineer's
  double CleanerIntervalOf(const CrewMember& crew) const;

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
  void QueueOrders(const std::map<std::string, OrderSheet>& sheet, double total);

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

  // Navigation.cpp
  void ArriveAt(const std::string& bodyId);
  void RefreshLocationInfo();
  void RevealScan(const std::string& bodyId);
  void FileSurvey(const Body& body);
  void ChronicleVisit(const Body* system);
  std::vector<const Body*> CurBodies() const;

  // Economy.cpp
  const Port* HerePort() const;
  double SurveyWorth(const std::string& type) const;
  int ProjectLot(const std::string& kind) const;

  // Projects.cpp
  void ProjectDone(Task& task);
  std::vector<Memory> SimUnprocessed() const;

  // SaveSystem.cpp
  void ApplySnapshotDefaults();

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
  // The staged sheets, by crew id. STATE.pendingOrders mirrors which ids have
  // one, so a save written here still reads as the reference expects.
  std::map<std::string, OrderSheet> orderSheets_;

  ShipDef ship_;
  std::vector<Room> rooms_;
  std::vector<InfoRow> infos_;
  Cargo cargo_;
  Mess mess_;
  MapView map_;
  std::map<std::string, LayerView> views_;
  double scanCost_ = 5;

  double actionCounter_ = 0;

  // Re-entry guards. A command spends its action BEFORE it registers the work
  // it starts, and the tick inside that action must not let a crew member grab
  // the same job twice.
  bool maintClaim_ = false;
  bool o2Claim_ = false;
  bool cookClaim_ = false;
  std::string projectClaim_;
  std::map<std::string, bool> crewClaims_;
};

// The commission board's own words: the goal is a count, the description is a
// sentence naming the body. Both editions share one spelling of each.
double CommissionGoal(const std::string& kind);
std::string CommissionDescription(const Simulation& sim, const Commission& commission);

}  // namespace ship
