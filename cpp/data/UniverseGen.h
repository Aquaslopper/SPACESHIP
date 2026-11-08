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

// Replaces systems, local bodies, ports, lanes and their positions. The authored
// market, goods, regions, rooms and text are left alone.
void GenerateUniverse(Catalog& catalog, std::uint64_t seed, int systemCount = 180);

// A typed seed - "kessler" - is hashed, never stored as a word.
std::uint64_t SeedFromText(const std::string& text);

}  // namespace ship
