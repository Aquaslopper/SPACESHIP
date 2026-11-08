// ============================================================================
//  UniverseGen.h — one seed, one sky.
//  ----------------------------------------------------------------------------
//  The authored chart was a table of forty-eight rows shelling out the same five
//  bodies and eight price profiles, which is why every station felt the same.
//  This builds the galaxy instead: systems differ in what they have, and a
//  station's prices follow from what the place is, not from an index.
//
//  The same seed always produces the same universe, so a save can carry four
//  words and keep its sky. The first system is always `sec-kessler`, because the
//  voyage boots there.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "data/Catalog.h"

namespace ship {

// The owner's range for a chart: one thousand two hundred stars, and never fewer
// than eight hundred or more than fifteen hundred. GenerateUniverse clamps to it,
// so no caller, default or probe can produce a count outside it.
constexpr int kMinSystems = 800;
constexpr int kDefaultSystems = 1200;
constexpr int kMaxSystems = 1500;

// Replaces systems, local bodies, ports, lanes and their positions. The authored
// market, goods, regions, rooms and text are left alone.
void GenerateUniverse(Catalog& catalog, std::uint64_t seed, int systemCount = kDefaultSystems);

// A typed seed - "kessler" - is hashed, never stored as a word.
std::uint64_t SeedFromText(const std::string& text);

}  // namespace ship
