// ============================================================================
//  Catalog.h — loads the extracted catalogues.
//  ---------------------------------------------------------------------------
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Constants.h"
#include "data/Types.h"

namespace ship {


struct ChartSettings {
  std::uint64_t seed = 0x5350414345534849ULL;
  int galaxies = 17;
  // 0 small (current), 1 normal (+20%), 2 medium (+20% again), 3 big (+20% again).
  int mapSize = 0;
  double mapScale = 1.0;
  double spread = 1.0;
};

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

  // Referential integrity, mirroring the assertions in tests/galaxy.test.cjs.
  // An empty result means the catalogue is sound.
  std::vector<std::string> Validate() const;

  // ---- lookups -----------------------------------------------------------
  const Body* FindBody(const std::string& id) const;
  const Body* FindSystem(const std::string& id) const;
  const Body* FindRegion(const std::string& id) const;
  const std::vector<std::size_t>& BodiesInSystem(const std::string& systemId) const;
  const std::vector<std::size_t>& SystemsInRegion(const std::string& regionId) const;
  void RebuildIndex() const;

  // Local bodies before systems existed belong to Kessler, exactly as
  // bodySystem() does in the reference.
  std::string BodySystem(const Body& body) const;

  // ---- data --------------------------------------------------------------
  std::string sourceSha256;
  long long sourceBytes = 0;
  Counts counts;

  MapGeometry map;


  std::map<std::string, MapGeometry> layerBoards;

  // Set by ExpandChart(). The chart is grown once per catalogue, so a voyage
  // started from an already-grown catalogue (or restarted from its own copy)
  // does not grow a second galaxy.
  bool chartExpanded = false;
  ChartSettings chartSettings;

  // Where each system sits on the galaxy board (ChartExpansion::PlaceGalaxyDots).
  // A system has one position, in its own region's cluster; the galaxy scale
  // reads this, the sector scale reads Body::x/y. Empty until the chart is
  // grown, and a system missing here is simply not drawn on the galaxy scale.
  std::map<std::string, std::pair<double, double>> galaxyPositions;

  DeckPlan wren;
  DeckPlan kestrel;
  std::map<std::string, RoomScene> roomScenes;
  // WORLD_HISTORY: two lines per region: its history, then its traffic.
  std::map<std::string, std::vector<std::string>> worldHistory;


  std::vector<WikiEntry> wiki;

  std::vector<Body> regions;
  std::vector<Body> systems;
  std::vector<Body> bodies;
  std::vector<Lane> sectorLanes;
  std::vector<Lane> galaxyLanes;

  std::vector<Port> ports;
  std::vector<Good> goods;
  std::vector<Contract> contracts;

  // The reference builds the chart from these tables rather than shipping the
  // result (see data/GalaxyGen.h). They are data, exported as-is:
  //   portPrices  PORT_PRICES: trade profile -> commodity kind -> multiplier
  //   portHints   PORT_HINTS: trade profile -> the line appended to a port's tip
  //   orbitalWorlds / orbitalPlanetNames / orbitalMoonNames: the local-chart
  //               pass's four profiles and its two name tables, in order.
  std::map<std::string, std::map<std::string, double>> portPrices;
  std::map<std::string, std::string> portHints;
  std::vector<OrbitalWorld> orbitalWorlds;
  std::vector<std::string> orbitalPlanetNames;
  std::vector<std::string> orbitalMoonNames;

  std::vector<std::string> taskTypes;
  std::vector<std::string> layerOrder;               // system, sector, galaxy
  std::map<std::string, double> surveyWorth;         // body type -> shiplets
  std::vector<CandidateDefinition> candidates;       // the hire pool
  std::map<std::string, std::map<std::string, std::string>> shipFields;  // by class
  std::map<std::string, std::vector<double>> cargoByClass;  // refit levels 0..2

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

  // The quarters art., never computed.
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

 private:
  void EnsureIndex() const;

  struct LookupCache {
    std::size_t bodyCount = 0;
    std::size_t systemCount = 0;
    std::size_t regionCount = 0;
    std::size_t portCount = 0;
    std::string firstBodyId;
    std::string lastBodyId;
    std::unordered_map<std::string, std::size_t> bodyById;
    std::unordered_map<std::string, std::size_t> systemById;
    std::unordered_map<std::string, std::size_t> regionById;
    std::unordered_map<std::string, std::size_t> portById;
    std::unordered_map<std::string, std::vector<std::size_t>> bodiesBySystem;
    std::unordered_map<std::string, std::vector<std::size_t>> systemsByRegion;
    bool ready = false;
  };
  mutable LookupCache index_;
};

}  // namespace ship
