// ============================================================================
//  Catalog.h — loads the extracted catalogues and proves they are complete.
//  ---------------------------------------------------------------------------
//  The JSON under assets/data is generated from Spaceship.hta by
//  tools/export-data.cjs. This class is the only place that knows the file
//  layout; the rest of the port asks it questions.
//
//  LoadFromAssets() is strict: a missing or malformed file throws, because a
//  half-loaded catalogue would produce a voyage that quietly differs from the
//  reference. Validate() is separate and returns every integrity problem it can
//  find, so the test suite can report them all at once instead of the first.
// ============================================================================
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Constants.h"
#include "data/Types.h"

namespace ship {

class CatalogError : public std::runtime_error {
 public:
  explicit CatalogError(const std::string& what) : std::runtime_error(what) {}
};

class Catalog {
 public:
  // Loads every file under <assetDir>/data. Throws CatalogError.
  static Catalog LoadFromAssets(const std::filesystem::path& assetDir);

  // The build's default asset directory (SPACESHIP_ASSET_DIR), which is the
  // source tree during development. A packaged build can pass its own path.
  static const std::filesystem::path& DefaultAssetDir();

  // Referential integrity for both reference data and native Phase 1 blueprints.
  // An empty result means the catalogue is internally sound.
  std::vector<std::string> Validate() const;

  // ---- lookups -----------------------------------------------------------
  const Body* FindBody(const std::string& id) const;
  const Body* FindSystem(const std::string& id) const;
  const Body* FindRegion(const std::string& id) const;
  const BodyProfileDef* FindSpaceBodyProfile(const std::string& id) const;
  const LootTableDef* FindSpaceLootTable(const std::string& id) const;
  const SpaceTaskDef* FindSpaceTask(const std::string& id) const;
  const StationProfileDef* FindStationProfile(const std::string& id) const;
  const ShipContactProfileDef* FindShipContactProfile(const std::string& id) const;
  const Good* FindGood(const std::string& id) const;

  // Local bodies authored before systems existed belong to Kessler, exactly as
  // bodySystem() does in the reference.
  std::string BodySystem(const Body& body) const;

  // ---- data --------------------------------------------------------------
  std::string sourceSha256;
  long long sourceBytes = 0;
  Counts counts;
  // NewVoyage adds a deterministic procedural expansion to its private copy.
  // The source catalogue remains authored and ungenerated for reference tests.
  bool spaceMapGenerated = false;
  std::uint64_t spaceMapSeed = 0;

  MapGeometry map;
  SpaceMapBlueprint spaceMap;  // native C++ generation blueprint; not exported from HTA
  DeckPlan wren;
  DeckPlan kestrel;
  // ROOM_SCENES, by room id. Authored, never computed.
  std::map<std::string, RoomScene> roomScenes;
  // WORLD_HISTORY: two authored lines per region. The first is what the
  // observation deck and the comms relay read out; the second is the traffic
  // report a relay will hand over for the price of two percent energy.
  std::map<std::string, std::vector<std::string>> worldHistory;

  // WIKI: twenty-one handbook entries, in the reference's order.
  std::vector<WikiEntry> wiki;

  std::vector<Body> regions;
  std::vector<Body> systems;
  std::vector<Body> bodies;
  std::vector<Lane> sectorLanes;
  std::vector<Lane> galaxyLanes;

  std::vector<Port> ports;
  std::vector<Good> goods;
  std::vector<Contract> contracts;

  std::vector<std::string> taskTypes;
  std::vector<std::string> layerOrder;               // system, sector, galaxy
  std::map<std::string, double> surveyWorth;         // body type -> shiplets; generated:<type> overrides native bodies
  std::vector<CandidateDefinition> candidates;       // the hire pool
  std::map<std::string, std::map<std::string, std::string>> shipFields;  // by class
  std::map<std::string, std::vector<double>> cargoByClass;  // refit levels 0..2

  // Authored starting values and definitions (Phase 1).
  std::vector<CrewDefinition> startingCrew;
  std::vector<StatDefinition> startingStats;
  std::vector<IncidentType> incidentTypes;
  std::vector<Commendation> commendations;
  std::vector<ProjectDef> projects;
  struct JobDefinition {
    std::string id, label;
    bool unique = false;
    double wage = 0;
    double baseAt = 0;      // jobBase()
    std::string room;       // jobRoom map in applyJob()
    // The price table. An order's price is also how long the sheet takes.
    double cost = 1;
    double learnOn = 0, learnSt = 0;
    double feeOn = 0, feeSt = 0;
    double trainStep = 0, trainCap = 0;
    std::string unit;
    std::string cloth, dark;  // what the sprite in the quarters wears
  };
  std::vector<JobDefinition> jobDefs;

  struct OrderDefinition {
    std::string id, label, note;
  };
  std::vector<OrderDefinition> orderDefs;

  // The quarters art. Authored, never computed.
  int spritePx = 4;
  double floorY = 230;
  SpriteFrame sprite, lie, sit;
  std::string spriteBlank;
  std::map<std::string, SpriteFrame> props;
  std::map<std::string, SpriteFrame> hats;
  std::map<std::string, std::pair<std::string, std::string>> jobHat;
  std::map<std::string, std::string> jobTool;
  std::map<std::string, HairStyle> hairStyles;
  std::vector<Good> startingCargo;          // the hold a new voyage opens with
  std::vector<SurveyRecordDef> startingSurvey;

  std::vector<InfoDefinition> infoDefs;     // INFOS: location/contacts/life
  std::vector<std::string> resourceLabels;  // RESOURCES, in sidebar order

  // Tunables, straight out of survival.json and content.json.
  Rules rules;

  const Port* FindPort(const std::string& id) const;
};

// Native-only task content is defined beside the incident/task logic.
std::vector<SpaceTaskDef> NativeSpaceMapTaskBlueprints();

// Stable generation primitives used by Phase 4. Kept pure so blueprints and
// IDs can be tested without making or mutating a world.
std::uint64_t SpaceMapRandomWord(std::uint64_t seed, const std::string& domain,
                                std::uint64_t ordinal);
std::optional<std::size_t> SpaceMapWeightedIndex(
    std::uint64_t seed, const std::string& domain, std::uint64_t ordinal,
    const std::vector<WeightedId>& options);
std::string SpaceMapGeneratedId(std::uint32_t version, std::uint64_t seed,
                                const std::string& category, std::uint64_t ordinal);
std::string SpaceMapGeneratedName(std::uint64_t seed, std::uint64_t ordinal,
                                  const SpaceMapNameRule& rule);

// Empty lists are reserved for legacy authored ports and mean unrestricted.
bool PortHasService(const Port& port, const std::string& service);
bool PortOffersGood(const Port& port, const std::string& goodId);

}  // namespace ship
