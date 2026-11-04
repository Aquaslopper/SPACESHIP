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

  // Local bodies authored before systems existed belong to Kessler, exactly as
  // bodySystem() does in the reference.
  std::string BodySystem(const Body& body) const;

  // ---- data --------------------------------------------------------------
  std::string sourceSha256;
  long long sourceBytes = 0;
  Counts counts;

  MapGeometry map;
  DeckPlan wren;
  DeckPlan kestrel;

  std::vector<Body> regions;
  std::vector<Body> systems;
  std::vector<Body> bodies;
  std::vector<Lane> sectorLanes;
  std::vector<Lane> galaxyLanes;

  std::vector<Port> ports;
  std::vector<Good> goods;
  std::vector<Contract> contracts;

  std::vector<std::string> taskTypes;

  const Port* FindPort(const std::string& id) const;
};

}  // namespace ship
