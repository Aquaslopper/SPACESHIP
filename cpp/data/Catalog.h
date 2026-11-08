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

#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

  // Local bodies authored before systems existed belong to Kessler, exactly as
  // bodySystem() does in the reference.
  std::string BodySystem(const Body& body) const;

  // ---- data --------------------------------------------------------------
  std::string sourceSha256;
  long long sourceBytes = 0;
  Counts counts;

  MapGeometry map;

  // The board each scale is drawn on. The system chart keeps the reference's
  // 638x278 board exactly; ChartExpansion() widens the sector board so every
  // system can own a position instead of six of them sharing one dot. A layer
  // with no entry here is drawn on `map`.
  std::map<std::string, MapGeometry> layerBoards;

  // Set by ExpandChart(). The chart is grown once per catalogue, so a voyage
  // started from an already-grown catalogue (or restarted from its own copy)
  // does not grow a second galaxy.
  bool chartExpanded = false;

  // The seed the generated universe was built from.
  std::uint64_t universeSeed = 0;
  int universeSystems = 180;

  // Where each system sits on the galaxy board (ChartExpansion::PlaceGalaxyDots).
  // A system has one position, in its own region's cluster; the galaxy scale
  // reads this, the sector scale reads Body::x/y. Empty until the chart is
  // grown, and a system missing here is simply not drawn on the galaxy scale.
  std::map<std::string, std::pair<double, double>> galaxyPositions;

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

  // The reference builds the chart from these tables rather than shipping the
  // result (see data/GalaxyGen.h). They are authored data, exported as-is:
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
