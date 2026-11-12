#pragma once

#include <cstdint>
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
  double baseAt = 0;
  std::string room;
  double cost = 1;
  double learnOn = 0, learnSt = 0;
  double feeOn = 0, feeSt = 0;
  double trainStep = 0, trainCap = 0;
  std::string unit;
  std::string cloth, dark;
};

struct TradeRun {
  std::string mode;
  std::string from;
  std::string to;
  std::string good;
  int crates = 0;
  double fee = 0.0;
  double freight = 0.0;
  bool accepted = false;
};

CrewMember MakeCrew(const CrewDefinition& def);

class Simulation {
 public:
  static constexpr const char* kEngineButtonChargeKey =
      "ch|room:engine|progress:engine-click";

  static Simulation NewVoyage(const Catalog& catalog);
  static Simulation NewVoyage(const Catalog& catalog, const ChartSettings& settings,
                              const std::string& startSystemId);
  static Simulation ReferenceVoyage(const Catalog& catalog);

  void PerformAction(const std::string& detail = std::string());
  void RestartVoyage();
  void BeginVoyage(const ChartSettings& settings, const std::string& startSystemId);
  const ChartSettings& VoyageChart() const { return chartSettings_; }
  const Catalog& SourceWorld() const { return sourceCatalog_.regions.empty() ? catalog_ : sourceCatalog_; }

  void ProAction(double n);
  void FreeCommit(const std::string& detail = std::string());

  bool StartCanisterInstall(int slot, const std::string& by = std::string(),
                            bool spendsAction = true);
  bool StartFilterBank();
  bool StartMaintenanceSelf();
  bool StartCookingSelf(const std::string& by = std::string(),
                        bool spendsAction = true);
  bool ServiceSystem(const std::string& statId);
  bool SplitFuelCell();
  bool ApplyJob(const std::string& crewId, const std::string& jobId);

  // ---- queries -----------------------------------------------------------
  double ActionCount() const { return actionCounter_; }
  std::uint64_t FeedRevision() const { return feedRevision_; }
  double MessRack() const { return mess_.rack; }
  double MessCapacity() const { return mess_.capacity; }
  double MessRations() const { return mess_.rations; }
  double MessRationCapacity() const { return mess_.rationCapacity; }
  const GameState& State() const { return state_; }
  GameState& MutableState() { return state_; }
  const std::vector<CrewMember>& Crew() const { return crew_; }
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
  bool HasRoom(const std::string& id) const;
  static bool StandardRoom(const std::string& id);
  static constexpr double RoomPrice() { return 500; }
  bool BuyRoom(const std::string& id);
  const ShipDef& Ship() const { return ship_; }
  const std::vector<InfoRow>& Infos() const { return infos_; }
  const Cargo& CargoStore() const { return cargo_; }
  const std::vector<Passenger>& Passengers() const { return state_.passengers; }
  const std::vector<Good>& Market() const { return catalog_.goods; }
  const std::vector<SurveyRecord>& Survey() const { return survey_; }
  std::vector<SurveyRecord>& MutableSurvey() { return survey_; }
  double ScanCost() const { return scanCost_; }
  int SensorTier() const;
  bool CanScan(const Body& body) const;
  double EffectiveScanCost(const Body& body) const;
  bool CanExtractBody(const Body& body) const;
  int ExtractionMaxPasses(const Body& body) const;
  int ExtractionPassesUsed(const Body& body) const;
  double ExtractionReserveVol(const Body& body) const;
  double ExtractionWorkCost(const Body& body) const;
  std::string ExtractionActionLabel(const Body& body) const;
  std::string ExtractionDoneLabel(const Body& body) const;
  std::string ExtractionScanSummary(const Body& body) const;
  bool InRange(const Body& body) const;
  bool AtTarget(const Body& body) const;
  std::string CurrentSystemId() const;
  const Body* CurrentSystem() const;
  const Body* FindBodyById(const std::string& id) const;
  const Body* HomeBody() const;
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
  const CrewMember* AssignedNavigator() const;
  const Rules& GameRules() const { return rules_; }
  std::vector<std::string> FeedText() const;

  const std::vector<JobDef>& Jobs() const { return jobs_; }
  const JobDef* FindJobDef(const std::string& jobId) const;
  double JobLevelFor(const CrewMember& crew, const std::string& jobId) const;
  const OrderSheet* SheetOf(const CrewMember& crew) const;
  std::string StagedJob(const CrewMember& crew) const;
  bool StageJob(const std::string& crewId, const std::string& jobId);
  bool ToggleOrder(const std::string& crewId, const std::string& orderId);
  void ClearOrders();
  int OrderedCount() const;
  bool HasPendingLayoff() const;
  bool OrdersRunning() const;
  bool CrewAvailable(const CrewMember& crew) const;
  double JobCost(const std::string& jobId) const;
  double OrderCostFor(const std::string& jobId, const std::string& orderId) const;
  double OrderCost(const CrewMember& crew, const std::string& orderId) const;
  double CrewCostOf(const CrewMember& crew) const;
  double OrderFeeFor(const std::string& jobId, const std::string& orderId) const;
  double CrewFeeOf(const CrewMember& crew) const;
  double OrdersCost() const;
  double OrdersFee() const;
  double SheetCost(const CrewMember& crew, const OrderSheet& sheet) const;
  bool CommitOrders();
  void ApplyJobTo(CrewMember& crew, const std::string& jobId);
  void TrainCrew(CrewMember& crew, int steps, const std::string& where);
  void FireCrew(CrewMember& crew);

  double CrewWage(const CrewMember& crew) const;
  double PayrollBill() const;
  double PayrollDue() const;
  std::string JobThreshold(const CrewMember& crew) const;
  std::string CrewWhere(const CrewMember& crew) const;
  std::string ActivityOf(const CrewMember& crew) const;
  std::string NeedKeyFor(const CrewMember& crew) const;
  double NavigatorFuelDiscount(const CrewMember& crew) const;
  std::string AirStageWord() const;
  std::string NeedLine(const std::string& needKey, int n, const std::string& jobLabel) const;
  std::string AskCrew(CrewMember& crew);
  static constexpr double kConversationMorale = 5.0;
  bool TalkToCrew(const std::string& crewId);

  bool SelectTarget(const std::string& bodyId);
  bool StartTravel();
  bool StartScan();
  bool StartExtraction();
  bool StandWatch();
  bool Acknowledge(const std::string& taskId);
  void SetLayer(const std::string& key);
  bool ZoomMap(int dir);
  void PanMap(double dx, double dy);
  bool ViewSector(const std::string& regionId);
  bool Dock();
  bool Undock();

  double LotPrice(const GoodsLot& lot) const;
  double CargoUsed() const;
  double GoodCounterPrice(const std::string& goodId) const;
  double CargoCapacity() const;
  double CargoReserved() const;
  bool BuySupply(const std::string& id);
  bool BuyGood(const std::string& goodId);
  bool SellGood(const std::string& lotId);
  bool SellSurvey(const std::string& recordId);
  bool TalkToPatron(const std::string& patronId);
  std::string PatronName(const std::string& patronId) const;
  bool Jettison(const std::string& lotId);
  bool StartSalvage(int bay);
  bool PackageSurvey(const std::string& recordId);
  bool AcceptContract(const std::string& contractId);
  bool CollectContract(const std::string& contractId);
  bool HireCrew(const std::string& candidateId);
  bool BuyUpgrade(const std::string& upgradeId);
  void ContractProgress(const std::string& goal, const std::string& target);

  std::vector<TradeRun> TradeBoard(const std::string& portId) const;
  bool EngineClick(const std::string& by = std::string());
  double EngineButtonChargeLeft() const;
  bool TrySpendEngineButton();

  void SetPressedButton(const std::string& key);
  void ClearPressedButton(const std::string& key);

  std::vector<TradeContract> TradesHere() const;
  double TradePayout(const TradeContract& trade) const;
  double TradeOfferPayout(const TradeContract& trade, int priceTier) const;
  int FreelanceRejectMask(const TradeContract& trade) const;
  bool HasFreelanceGoods(const TradeContract& trade) const;
  bool SettleTrade(const std::string& mode, const std::string& to,
                   const std::string& good, int priceTier = 0);
  bool StowCargo(const std::string& lotId);
  bool CargoStowed(const std::string& lotId) const;
  int CargoSortOrder(const std::string& lotId) const;
  int CargoMixEpoch(const std::string& lotId) const;
  void StepCargoMix();
  bool AcceptTrade(const std::string& mode, const std::string& to,
                   const std::string& good = std::string());
  bool TradeAccepted(const std::string& mode, const std::string& to,
                     const std::string& good = std::string()) const;
  void CommissionProgress(const std::string& kind, const std::string& target);
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

  std::string SaveJson() const;
  bool LoadJson(const std::string& text, std::string* error);
  static std::string SlotFilename(int slot);
  static bool SlotIsUtf16(int slot);
  bool SaveSlot(int slot, const std::string& directory, std::string* error) const;
  bool LoadSlot(int slot, const std::string& directory, std::string* error);
  bool SaveToFile(const std::string& path, bool utf16le, std::string* error) const;
  bool LoadFromFile(const std::string& path, std::string* error);
  static bool ValidSave(const std::string& text, const Catalog& catalog, std::string* why);

  std::string TraceJson() const;

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
  std::string CrewNameOf(const std::string& id) const;

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

  const Commendation* FindCommendation(const std::string& id) const;
  double ChronicleValueFor(const std::string& metric) const { return ChronicleValue(metric); }
  double ReadyCommendations() const;
  double EarnedCommendations() const;
  std::string CaptainRank() const;
  std::string RewardText(const std::string& commendationId) const;
  std::string RewardTextOf(const Commendation& commendation) const;
  bool ClaimCommendation(const std::string& id);

  bool RemedyIncident(const std::string& incidentId);
  bool SpawnMapIncident(const std::string& typeId, const std::string& contextNote = std::string());
  const IncidentType::Stage* IncidentStage(const Incident& incident) const;

  const CrewMember* Engineer() const;
  const CrewMember* FilterHand() const;
  const CrewMember* Cook() const;
  double MaintCostActions() const;
  bool MaintRunningAny() const;
  double CleanerIntervalOf(const CrewMember& crew) const;
  double CookThreshold(const CrewMember& crew) const;

 private:
  void TickWorld();

  void WearEngine();
  void TickFilters();

  void TickCrew();
  void TickCleaner();
  void TickPayroll();
  void CheckLoss();
  CrewMember* AssignedEngineer();
  CrewMember* AssignedFilterHand();
  CrewMember* AssignedCook();
  double CleanerInterval(const CrewMember& crew) const;
  double MaintCost() const;
  bool MaintRunning() const;
  void ApplyOrderPayload(CrewMember& crew, const Task& task);
  void QueueOrders(const std::map<std::string, OrderSheet>& sheet);

  void AdvanceTasks(double n);
  void TickOngoing();
  void SweepAutoTasks();
  void OnTaskDone(Task& task);

  void TickButtonCharges();

  const IncidentType* IncidentSpec(const Incident& incident) const;
  void IncidentEffect(const IncidentType::Stage& step);
  void TickIncidents();
  std::pair<std::string, std::string> DisputePair() const;

  double ChronicleValue(const std::string& metric) const;

  void ArriveAt(const std::string& bodyId);
  void ResolveMapArrivalEvents(const Body& localBody, const Body* jumpedSystem,
                               const std::string& prevRegion);
  void ResolveMapScanEvents(const Body& body, bool firstScan);
  void RefreshLocationInfo();
  void RevealScan(const std::string& bodyId);
  void FinishExtraction(const std::string& bodyId, double reservedVol);
  void FileSurvey(const Body& body);
  void ChronicleVisit(const Body* system);
  std::vector<const Body*> CurBodies() const;
  void ApplyChartBoard();

  const Port* HerePort() const;

  std::string pressedButton_;
  bool engineClicking_ = false;
  void ServiceDrive(const std::string& by);
  std::map<std::string, bool> stowed_;
  std::vector<std::string> stowOrder_;
  std::map<std::string, int> cargoMixGen_;
  void ClearStowLot(const std::string& lotId);
  int mixClicks_ = 0;
  int mixThreshold_ = 0;
  std::string justStowedLot_;
  std::uint64_t stowRng_ = 0x9E3779B97F4A7C15ULL;
  std::uint64_t NextCargoRng();
  int MixDraw();
  double SurveyWorth(const std::string& type) const;
  int ProjectLot(const std::string& kind) const;

  void ProjectDone(Task& task);

  void ApplySnapshotDefaults();

  void AutoMaint();
  void AutoFilters();
  void AutoMess();

  Catalog catalog_;
  Catalog sourceCatalog_;
  ChartSettings chartSettings_;
  Rules rules_;
  GameState state_;
  std::vector<CrewMember> crew_;
  std::vector<Stat> stats_;
  LifeStore life_;
  std::vector<Incident> events_;
  std::vector<GoodsLot> goods_;
  std::vector<SurveyRecord> survey_;
  std::vector<JobDef> jobs_;
  std::map<std::string, OrderSheet> orderSheets_;

  ShipDef ship_;
  std::vector<Room> rooms_;
  bool referenceFit_ = false;
  std::vector<InfoRow> infos_;
  Cargo cargo_;
  Mess mess_;
  MapView map_;
  std::map<std::string, LayerView> views_;
  double scanCost_ = 5;

  double actionCounter_ = 0;
  std::uint64_t feedRevision_ = 0;

  bool maintClaim_ = false;
  bool o2Claim_ = false;
  bool cookClaim_ = false;
  std::string projectClaim_;
  bool cookRestock_ = false;
  std::map<std::string, bool> crewClaims_;
};

double CommissionGoal(const std::string& kind);
std::string CommissionDescription(const Simulation& sim, const Commission& commission);

}  // namespace ship
