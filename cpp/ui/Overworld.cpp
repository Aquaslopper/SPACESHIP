// ============================================================================
//  Overworld.cpp — PC-98 overworld planet crawler (Dispatch surface & mine).
//  ---------------------------------------------------------------------------
//  Deterministic planet surface and underground mine generation, first-person
//  step/turn movement with portal traversal, and the 480x288 indexed RGB4
//  software rasterizer for ShellView::planetSurface_. The Bridge monitor
//  overlay, minimap, D-pad, and click handling remain in cpp/ui/Draw.cpp.
// ============================================================================
#include "ui/Draw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ship {

void ShellView::LoadPlanetLayer(const Simulation& sim, std::uint8_t targetInMine, bool placeOres) {
  const Body* here = sim.FindBodyById(sim.Map().here);
  if (here == nullptr) { return; }
  constexpr int N = PlanetSurfaceState::kSize;
  static constexpr int kCardDx[4] = {0, 1, 0, -1};
  static constexpr int kCardDy[4] = {-1, 0, 1, 0};

  std::uint32_t baseSeed = 2166136261u;
  for (unsigned char c : here->id) {
    baseSeed ^= c;
    baseSeed *= 16777619u;
  }
  baseSeed ^= static_cast<std::uint32_t>(sim.World().chartSettings.seed * 2654435761u);
  if (baseSeed == 0u) { baseSeed = 1u; }

  std::uint32_t oreHash = baseSeed ^ 0x9E3779B9u;
  oreHash ^= oreHash << 13;
  oreHash ^= oreHash >> 17;
  oreHash ^= oreHash << 5;
  const int density = static_cast<int>((oreHash >> 8) % 4u);
  const int totalOreGoal = std::min(5, 3 + static_cast<int>(oreHash % 3u) + (density >= 2 ? 1 : 0));
  const int mineOreGoal = (totalOreGoal >= 4) ? 2 : 1;
  const int surfOreGoal = totalOreGoal - mineOreGoal;

  // Compact 288 B stack BFS (32 B row bitset + 256 B packed (y << 4) | x queue)
  auto connectedFrom = [&](int startX, int startY, std::uint8_t layerBit, bool checkLayerOres) -> bool {
    auto isBlocked = [&](int x, int y) -> bool {
      const std::uint8_t t = planetSurface_.Tile(x, y);
      if (t == 1u || (layerBit == 0u && t == 2u)) { return true; }
      if (checkLayerOres) {
        for (int oi = 0; oi < static_cast<int>(planetSurface_.oreCount); ++oi) {
          if (((planetSurface_.oreLayerMask >> oi) & 1u) == layerBit &&
              planetSurface_.oreX[oi] == x && planetSurface_.oreY[oi] == y) {
            return true;
          }
        }
      }
      return false;
    };

    std::uint16_t visRows[N]{};
    std::uint8_t queue[N * N];
    int head = 0, tail = 0;
    if (!isBlocked(startX, startY)) {
      visRows[startY] = static_cast<std::uint16_t>(1u << startX);
      queue[tail++] = static_cast<std::uint8_t>((startY << PlanetSurfaceState::kSizeShift) | startX);
    }
    int openTotal = 0;
    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        if (!isBlocked(x, y)) { ++openTotal; }
      }
    }
    while (head < tail) {
      const std::uint8_t packed = queue[head++];
      const int cx = static_cast<int>(packed & (N - 1));
      const int cy = static_cast<int>(packed >> PlanetSurfaceState::kSizeShift);
      for (int d = 0; d < 4; ++d) {
        const int nx = cx + kCardDx[d];
        const int ny = cy + kCardDy[d];
        if (static_cast<unsigned>(nx) >= static_cast<unsigned>(N) ||
            static_cast<unsigned>(ny) >= static_cast<unsigned>(N)) {
          continue;
        }
        const std::uint16_t bit = static_cast<std::uint16_t>(1u << nx);
        if ((visRows[ny] & bit) != 0u || isBlocked(nx, ny)) { continue; }
        visRows[ny] |= bit;
        queue[tail++] = static_cast<std::uint8_t>((ny << PlanetSurfaceState::kSizeShift) | nx);
      }
    }
    if (tail != openTotal) { return false; }
    if (checkLayerOres) {
      for (int oi = 0; oi < static_cast<int>(planetSurface_.oreCount); ++oi) {
        if (((planetSurface_.oreLayerMask >> oi) & 1u) != layerBit) { continue; }
        bool adjVis = false;
        for (int d = 0; d < 4; ++d) {
          const int nx = static_cast<int>(planetSurface_.oreX[oi]) + kCardDx[d];
          const int ny = static_cast<int>(planetSurface_.oreY[oi]) + kCardDy[d];
          if (static_cast<unsigned>(nx) < static_cast<unsigned>(N) &&
              static_cast<unsigned>(ny) < static_cast<unsigned>(N) &&
              (visRows[ny] & static_cast<std::uint16_t>(1u << nx)) != 0u) {
            adjVis = true;
            break;
          }
        }
        if (!adjVis) { return false; }
      }
    }
    return true;
  };

  if (targetInMine == 0u) {
    std::uint32_t h = baseSeed;
    auto nextRand = [&h]() -> std::uint32_t {
      h ^= h << 13;
      h ^= h >> 17;
      h ^= h << 5;
      return h ? h : 1u;
    };

    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        planetSurface_.cells[(y << PlanetSurfaceState::kSizeShift) | x] =
            (x == 0 || x == N - 1 || y == 0 || y == N - 1) ? 1u : 0u;
      }
    }
    constexpr int kSpawnX = N / 2;
    constexpr int kShipY = N - 2;
    constexpr int kSpawnY = N - 3;
    planetSurface_.shipX = static_cast<std::uint8_t>(kSpawnX);
    planetSurface_.shipY = static_cast<std::uint8_t>(kShipY);
    planetSurface_.SetTile(kSpawnX, kShipY, 2u);

    const int obstacleGoal = 24 + static_cast<int>(nextRand() % 9u) + density * 12;
    int placed = 0;
    for (int attempt = 0; attempt < 256 && placed < obstacleGoal; ++attempt) {
      const int rx = 1 + static_cast<int>(nextRand() % (N - 2));
      const int ry = 1 + static_cast<int>(nextRand() % (N - 2));
      if ((rx == kSpawnX && (ry == kShipY || ry == kSpawnY || ry == kSpawnY - 1)) ||
          planetSurface_.Tile(rx, ry) != 0u) {
        continue;
      }
      planetSurface_.SetTile(rx, ry, 1u);
      if (connectedFrom(kSpawnX, kSpawnY, 0u, /*checkLayerOres=*/false)) {
        ++placed;
      } else {
        planetSurface_.SetTile(rx, ry, 0u);
      }
    }

    // Carve a winding trail network (3) with natural bends and branches across the 16x16 surface
    std::uint8_t trailPos[N * N];
    int trailLen = 0;
    auto markTrail = [&](int x, int y) {
      if (planetSurface_.Tile(x, y) == 0u) {
        planetSurface_.SetTile(x, y, 3u);
        if (trailLen < N * N) {
          trailPos[trailLen++] = static_cast<std::uint8_t>((y << PlanetSurfaceState::kSizeShift) | x);
        }
      }
    };
    auto countAdjTrail = [&](int x, int y) -> int {
      int c = 0;
      for (int d = 0; d < 4; ++d) {
        const int nx = x + kCardDx[d];
        const int ny = y + kCardDy[d];
        if (nx > 0 && nx < N - 1 && ny > 0 && ny < N - 1 &&
            planetSurface_.Tile(nx, ny) == 3u) {
          ++c;
        }
      }
      return c;
    };

    markTrail(kSpawnX, kSpawnY);
    markTrail(kSpawnX, kSpawnY - 1);
    int wx = kSpawnX, wy = kSpawnY - 1, wDir = 0;
    const int mainGoal = 22 + static_cast<int>(nextRand() % 9u) + density * 4;
    for (int step = 0; step < mainGoal; ++step) {
      int cands[8];
      int nCand = 0;
      for (int d = 0; d < 4; ++d) {
        if (d == (wDir + 2) % 4) { continue; }
        const int nx = wx + kCardDx[d];
        const int ny = wy + kCardDy[d];
        if (nx <= 0 || nx >= N - 1 || ny <= 0 || ny >= N - 1) { continue; }
        if (planetSurface_.Tile(nx, ny) != 0u) { continue; }
        if (countAdjTrail(nx, ny) > 1) { continue; }
        const int weight = (d == wDir) ? ((step % 3 == 0) ? 1 : 2) : 2;
        for (int w = 0; w < weight && nCand < 8; ++w) { cands[nCand++] = d; }
      }
      if (nCand == 0) { break; }
      wDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
      wx += kCardDx[wDir];
      wy += kCardDy[wDir];
      markTrail(wx, wy);
    }

    // Add curving side branches off the main trail across the 16x16 world
    const int branches = 3 + static_cast<int>(nextRand() % 3u) + density * 2;
    for (int b = 0; b < branches && trailLen > 2; ++b) {
      const int seedIdx = 1 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(trailLen - 1));
      int bx = static_cast<int>(trailPos[seedIdx] & (N - 1));
      int by = static_cast<int>(trailPos[seedIdx] >> PlanetSurfaceState::kSizeShift);
      int bDir = static_cast<int>(nextRand() % 4u);
      const int branchSteps = 5 + static_cast<int>(nextRand() % 5u);
      for (int step = 0; step < branchSteps; ++step) {
        int cands[4];
        int nCand = 0;
        for (int d = 0; d < 4; ++d) {
          if (d == (bDir + 2) % 4) { continue; }
          const int nx = bx + kCardDx[d];
          const int ny = by + kCardDy[d];
          if (nx <= 0 || nx >= N - 1 || ny <= 0 || ny >= N - 1) { continue; }
          if (planetSurface_.Tile(nx, ny) != 0u) { continue; }
          if (countAdjTrail(nx, ny) > 1) { continue; }
          cands[nCand++] = d;
        }
        if (nCand == 0) { break; }
        bDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
        bx += kCardDx[bDir];
        by += kCardDy[bDir];
        markTrail(bx, by);
      }
    }

    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        planetSurface_.SetVariant(x, y, static_cast<std::uint8_t>(nextRand() & 7u));
      }
    }

    // Pick 1 enterable mine portal cut into a cliff cell facing a reachable approach tile
    struct PortalCand {
      std::uint8_t x;
      std::uint8_t y;
      std::uint8_t outDir;
    };
    PortalCand tier1[128], tier2[128], tier3[64];
    int nT1 = 0, nT2 = 0, nT3 = 0;
    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        if (planetSurface_.Tile(x, y) != 1u) { continue; }
        const bool interior = (x > 0 && x < N - 1 && y > 0 && y < N - 1);
        for (int d = 0; d < 4; ++d) {
          const int ax = x + kCardDx[d];
          const int ay = y + kCardDy[d];
          if (ax <= 0 || ax >= N - 1 || ay <= 0 || ay >= N - 1) { continue; }
          const std::uint8_t at = planetSurface_.Tile(ax, ay);
          if (at != 0u && at != 3u) { continue; }
          if (ax == kSpawnX && (ay == kSpawnY || ay == kShipY)) { continue; }
          const int shipDist = std::abs(ax - kSpawnX) + std::abs(ay - kShipY);
          if (shipDist < 4) { continue; }
          if (interior) {
            const int bx = x - kCardDx[d];
            const int by = y - kCardDy[d];
            const bool backed = (bx <= 0 || bx >= N - 1 || by <= 0 || by >= N - 1 ||
                                 planetSurface_.Tile(bx, by) == 1u);
            const bool nearTrail = (at == 3u) || (countAdjTrail(ax, ay) > 0);
            if ((backed || nearTrail) && nT1 < 128) {
              tier1[nT1++] = PortalCand{static_cast<std::uint8_t>(x),
                                        static_cast<std::uint8_t>(y),
                                        static_cast<std::uint8_t>(d)};
            } else if (nT2 < 128) {
              tier2[nT2++] = PortalCand{static_cast<std::uint8_t>(x),
                                        static_cast<std::uint8_t>(y),
                                        static_cast<std::uint8_t>(d)};
            }
          } else if (nT3 < 64) {
            tier3[nT3++] = PortalCand{static_cast<std::uint8_t>(x),
                                      static_cast<std::uint8_t>(y),
                                      static_cast<std::uint8_t>(d)};
          }
        }
      }
    }
    PortalCand chosen{static_cast<std::uint8_t>(kSpawnX), 0u, 2u};
    if (nT1 > 0) {
      chosen = tier1[nextRand() % static_cast<std::uint32_t>(nT1)];
    } else if (nT2 > 0) {
      chosen = tier2[nextRand() % static_cast<std::uint32_t>(nT2)];
    } else if (nT3 > 0) {
      chosen = tier3[nextRand() % static_cast<std::uint32_t>(nT3)];
    }
    planetSurface_.portalX = chosen.x;
    planetSurface_.portalY = chosen.y;
    planetSurface_.portalOutDir = chosen.outDir;
    planetSurface_.mineInDir = static_cast<std::uint8_t>((chosen.outDir + 2u) & 3u);
    planetSurface_.SetVariant(chosen.x, chosen.y, 7u);
    const int portalApronX = static_cast<int>(chosen.x) + kCardDx[chosen.outDir & 3u];
    const int portalApronY = static_cast<int>(chosen.y) + kCardDy[chosen.outDir & 3u];
    if (planetSurface_.Tile(portalApronX, portalApronY) == 0u) {
      planetSurface_.SetTile(portalApronX, portalApronY, 3u);
    }

    if (placeOres) {
      std::uint8_t candPos[N * N];
      int candCount = 0;
      for (int y = 1; y < N - 1; ++y) {
        for (int x = 1; x < N - 1; ++x) {
          const std::uint8_t t = planetSurface_.Tile(x, y);
          if (t != 0u && t != 3u) { continue; }
          if (x == kSpawnX && y == kSpawnY) { continue; }
          if (x == portalApronX && y == portalApronY) { continue; }
          const int dist = std::abs(x - kSpawnX) + std::abs(y - kShipY);
          if (dist >= 3) {
            candPos[candCount++] = static_cast<std::uint8_t>((y << PlanetSurfaceState::kSizeShift) | x);
          }
        }
      }
      const int wantSurf = std::min({candCount, PlanetSurfaceState::kMaxOre, surfOreGoal});
      for (int i = 0; i < candCount && static_cast<int>(planetSurface_.oreCount) < wantSurf; ++i) {
        const int pick = i + static_cast<int>(nextRand() % static_cast<std::uint32_t>(candCount - i));
        std::swap(candPos[i], candPos[pick]);
        const int slot = static_cast<int>(planetSurface_.oreCount);
        planetSurface_.oreX[slot] = static_cast<std::uint8_t>(candPos[i] & (N - 1));
        planetSurface_.oreY[slot] = static_cast<std::uint8_t>(candPos[i] >> PlanetSurfaceState::kSizeShift);
        planetSurface_.oreLayerMask = static_cast<std::uint8_t>(planetSurface_.oreLayerMask & ~(1u << slot));
        ++planetSurface_.oreCount;
        if (!connectedFrom(kSpawnX, kSpawnY, 0u, /*checkLayerOres=*/true)) {
          --planetSurface_.oreCount;
        }
      }
    }
    return;
  }

  // targetInMine == 1: carve deterministic underground mine sub-map into cells[256] in place
  std::uint32_t h = baseSeed ^ 0x85EBCA6Bu;
  if (h == 0u) { h = 1u; }
  auto nextRand = [&h]() -> std::uint32_t {
    h ^= h << 13;
    h ^= h >> 17;
    h ^= h << 5;
    return h ? h : 1u;
  };

  std::uint8_t mTile[N * N];
  std::memset(mTile, 1, sizeof(mTile));
  auto mAt = [&](int u, int v) -> std::uint8_t& {
    return mTile[(v << PlanetSurfaceState::kSizeShift) | u];
  };
  auto countOpenAdj = [&](int u, int v) -> int {
    int c = 0;
    for (int d = 0; d < 4; ++d) {
      const int nu = u + kCardDx[d];
      const int nv = v + kCardDy[d];
      if (nu > 0 && nu < N - 1 && nv > 0 && nv < N - 1 && mAt(nu, nv) != 1u) {
        ++c;
      }
    }
    return c;
  };

  std::uint8_t spine[N * N];
  int spineLen = 0;
  auto carveCell = [&](int u, int v, std::uint8_t kind) {
    if (mAt(u, v) == 1u) {
      mAt(u, v) = kind;
      if (spineLen < N * N) {
        spine[spineLen++] = static_cast<std::uint8_t>((v << PlanetSurfaceState::kSizeShift) | u);
      }
    } else if (kind == 3u && mAt(u, v) == 0u) {
      mAt(u, v) = 3u;
    }
  };

  // Canonical entrance shaft at (8,14) -> (8,13..11) heading North; rotated to match mineInDir
  carveCell(8, 13, 3u);
  carveCell(8, 12, 3u);
  carveCell(8, 11, 3u);

  int cu = 8, cv = 11, cDir = 0;
  const int aditSteps = 20 + static_cast<int>(nextRand() % 7u) + density * 4;
  for (int step = 0; step < aditSteps; ++step) {
    int cands[8];
    int nCand = 0;
    for (int pass = 0; pass < 2 && nCand == 0; ++pass) {
      const int maxAdj = (pass == 0) ? 1 : 2;
      for (int d = 0; d < 4; ++d) {
        if (d == (cDir + 2) % 4) { continue; }
        if (step < 3 && d == 2) { continue; }
        const int nu = cu + kCardDx[d];
        const int nv = cv + kCardDy[d];
        if (nu <= 0 || nu >= N - 1 || nv <= 0 || nv >= N - 2) { continue; }
        if (mAt(nu, nv) != 1u || countOpenAdj(nu, nv) > maxAdj) { continue; }
        const int weight = (d == cDir) ? ((step % 3 == 0) ? 1 : 2) : 2;
        for (int w = 0; w < weight && nCand < 8; ++w) { cands[nCand++] = d; }
      }
    }
    if (nCand == 0) { break; }
    cDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
    cu += kCardDx[cDir];
    cv += kCardDy[cDir];
    carveCell(cu, cv, 3u);
  }

  // Carve branching side drifts off the main adit
  const int driftCount = 3 + static_cast<int>(nextRand() % 2u) + density * 2;
  for (int b = 0; b < driftCount && spineLen > 3; ++b) {
    const int seedIdx = 2 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(spineLen - 2));
    int bu = static_cast<int>(spine[seedIdx] & (N - 1));
    int bv = static_cast<int>(spine[seedIdx] >> PlanetSurfaceState::kSizeShift);
    int bDir = static_cast<int>(nextRand() % 4u);
    const int driftSteps = 5 + static_cast<int>(nextRand() % 5u);
    for (int step = 0; step < driftSteps; ++step) {
      int cands[4];
      int nCand = 0;
      const int maxAdj = (b == 0 && step + 1 == driftSteps) ? 2 : 1;
      for (int d = 0; d < 4; ++d) {
        if (d == (bDir + 2) % 4) { continue; }
        const int nu = bu + kCardDx[d];
        const int nv = bv + kCardDy[d];
        if (nu <= 0 || nu >= N - 1 || nv <= 0 || nv >= N - 2) { continue; }
        if (mAt(nu, nv) != 1u || countOpenAdj(nu, nv) > maxAdj) { continue; }
        cands[nCand++] = d;
      }
      if (nCand == 0) { break; }
      bDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
      bu += kCardDx[bDir];
      bv += kCardDy[bDir];
      carveCell(bu, bv, (step < 3) ? 3u : 0u);
    }
  }

  // Excavate small 2x2 subterranean pockets anchored on carved drifts for alcoves/chambers
  const int pocketCount = 2 + static_cast<int>(nextRand() % 2u) + density;
  for (int p = 0; p < pocketCount && spineLen > 4; ++p) {
    const int seedIdx = 3 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(spineLen - 3));
    const int pu = std::clamp(static_cast<int>(spine[seedIdx] & (N - 1)), 1, N - 3);
    const int pv = std::clamp(static_cast<int>(spine[seedIdx] >> PlanetSurfaceState::kSizeShift), 1, N - 4);
    for (int dv = 0; dv < 2; ++dv) {
      for (int du = 0; du < 2; ++du) {
        carveCell(pu + du, pv + dv, 0u);
      }
    }
  }

  // Ensure every planet's underground mine has a generous network (>= 32 walkable cells)
  for (int extra = 0; extra < 32 && spineLen < 34 && spineLen > 2; ++extra) {
    const int seedIdx = 1 + static_cast<int>(nextRand() % static_cast<std::uint32_t>(spineLen - 1));
    int eu = static_cast<int>(spine[seedIdx] & (N - 1));
    int ev = static_cast<int>(spine[seedIdx] >> PlanetSurfaceState::kSizeShift);
    int eDir = static_cast<int>(nextRand() % 4u);
    for (int step = 0; step < 5 && spineLen < 34; ++step) {
      int cands[4];
      int nCand = 0;
      for (int d = 0; d < 4; ++d) {
        const int nu = eu + kCardDx[d];
        const int nv = ev + kCardDy[d];
        if (nu <= 0 || nu >= N - 1 || nv <= 0 || nv >= N - 2) { continue; }
        if (mAt(nu, nv) != 1u || countOpenAdj(nu, nv) > 1) { continue; }
        cands[nCand++] = d;
      }
      if (nCand == 0) { break; }
      eDir = cands[nextRand() % static_cast<std::uint32_t>(nCand)];
      eu += kCardDx[eDir];
      ev += kCardDy[eDir];
      carveCell(eu, ev, (step < 2) ? 3u : 0u);
    }
  }

  const int rot = static_cast<int>(planetSurface_.mineInDir & 3u);
  auto mapCoord = [rot](int u, int v, int& xOut, int& yOut) {
    if (rot == 0) {
      xOut = u;
      yOut = v;
    } else if (rot == 1) {
      xOut = (N - 1) - v;
      yOut = u;
    } else if (rot == 2) {
      xOut = (N - 1) - u;
      yOut = (N - 1) - v;
    } else {
      xOut = v;
      yOut = (N - 1) - u;
    }
  };

  for (int v = 0; v < N; ++v) {
    for (int u = 0; u < N; ++u) {
      int x = 0, y = 0;
      mapCoord(u, v, x, y);
      const std::uint8_t t = mAt(u, v);
      planetSurface_.SetTile(x, y, t);
      if (t == 1u) {
        // 100% of underground walls are 3D rock/vein/shored-timber faces (variants 4..7)
        planetSurface_.SetVariant(x, y, static_cast<std::uint8_t>(4u + (nextRand() & 3u)));
      } else {
        planetSurface_.SetVariant(x, y, static_cast<std::uint8_t>(nextRand() & 7u));
      }
    }
  }

  int mPortalX = 0, mPortalY = 0, mSpawnX = 0, mSpawnY = 0, mShaftX = 0, mShaftY = 0;
  mapCoord(8, 14, mPortalX, mPortalY);
  mapCoord(8, 13, mSpawnX, mSpawnY);
  mapCoord(8, 12, mShaftX, mShaftY);
  planetSurface_.portalX = static_cast<std::uint8_t>(mPortalX);
  planetSurface_.portalY = static_cast<std::uint8_t>(mPortalY);
  planetSurface_.portalOutDir = static_cast<std::uint8_t>(rot);
  planetSurface_.SetVariant(mPortalX, mPortalY, 7u);

  if (placeOres) {
    std::uint8_t tierA[N * N], tierB[N * N];
    int nA = 0, nB = 0;
    for (int y = 1; y < N - 1; ++y) {
      for (int x = 1; x < N - 1; ++x) {
        const std::uint8_t t = planetSurface_.Tile(x, y);
        if (t != 0u && t != 3u) { continue; }
        if ((x == mSpawnX && y == mSpawnY) || (x == mShaftX && y == mShaftY)) { continue; }
        const int dist = std::abs(x - mSpawnX) + std::abs(y - mSpawnY);
        int openDeg = 0;
        for (int d = 0; d < 4; ++d) {
          const std::uint8_t nt = planetSurface_.Tile(x + kCardDx[d], y + kCardDy[d]);
          if (nt == 0u || nt == 3u) { ++openDeg; }
        }
        const std::uint8_t packed = static_cast<std::uint8_t>((y << PlanetSurfaceState::kSizeShift) | x);
        if (dist >= 5 && (openDeg == 1 || t == 0u)) {
          tierA[nA++] = packed;
        } else if (dist >= 2) {
          tierB[nB++] = packed;
        }
      }
    }
    for (int i = 0; i < nA; ++i) {
      const int pick = i + static_cast<int>(nextRand() % static_cast<std::uint32_t>(nA - i));
      std::swap(tierA[i], tierA[pick]);
    }
    for (int i = 0; i < nB; ++i) {
      const int pick = i + static_cast<int>(nextRand() % static_cast<std::uint32_t>(nB - i));
      std::swap(tierB[i], tierB[pick]);
    }
    std::uint8_t candPos[N * N];
    int candCount = 0;
    for (int i = 0; i < nA; ++i) { candPos[candCount++] = tierA[i]; }
    for (int i = 0; i < nB; ++i) { candPos[candCount++] = tierB[i]; }

    int placedMineOres = 0;
    for (int i = 0; i < candCount &&
                    placedMineOres < mineOreGoal &&
                    static_cast<int>(planetSurface_.oreCount) < PlanetSurfaceState::kMaxOre; ++i) {
      const int slot = static_cast<int>(planetSurface_.oreCount);
      planetSurface_.oreX[slot] = static_cast<std::uint8_t>(candPos[i] & (N - 1));
      planetSurface_.oreY[slot] = static_cast<std::uint8_t>(candPos[i] >> PlanetSurfaceState::kSizeShift);
      planetSurface_.oreLayerMask = static_cast<std::uint8_t>(planetSurface_.oreLayerMask | (1u << slot));
      ++planetSurface_.oreCount;
      if (connectedFrom(mSpawnX, mSpawnY, 1u, /*checkLayerOres=*/true)) {
        ++placedMineOres;
      } else {
        planetSurface_.oreLayerMask = static_cast<std::uint8_t>(planetSurface_.oreLayerMask & ~(1u << slot));
        --planetSurface_.oreCount;
      }
    }
  }
}

void ShellView::EnsurePlanetSurface(const Simulation& sim) {
  if (!sim.LandedOnPlanet()) { return; }
  const Body* here = sim.FindBodyById(sim.Map().here);
  if (here == nullptr) { return; }
  if (planetSurface_.planetId == here->id) { return; }

  planetSurface_ = PlanetSurfaceState{};
  planetSurface_.planetId = here->id;
  constexpr int N = PlanetSurfaceState::kSize;

  // Build surface & place surface ores, then build mine once to place underground ores,
  // then restore surface cells in-place (0 B extra persistent map buffer).
  LoadPlanetLayer(sim, 0u, /*placeOres=*/true);
  std::uint8_t surfCells[N * N];
  std::memcpy(surfCells, planetSurface_.cells, sizeof(surfCells));
  const std::uint8_t sPortalX = planetSurface_.portalX;
  const std::uint8_t sPortalY = planetSurface_.portalY;
  const std::uint8_t sPortalOut = planetSurface_.portalOutDir;

  LoadPlanetLayer(sim, 1u, /*placeOres=*/true);

  std::memcpy(planetSurface_.cells, surfCells, sizeof(surfCells));
  planetSurface_.portalX = sPortalX;
  planetSurface_.portalY = sPortalY;
  planetSurface_.portalOutDir = sPortalOut;
  planetSurface_.inMine = 0u;
  planetSurface_.px = static_cast<std::uint8_t>(N / 2);
  planetSurface_.py = static_cast<std::uint8_t>(N - 3);
  planetSurface_.dir = 0u;
}

void ShellView::StepPlanetSurface(const Simulation& sim, int actionKind) {
  constexpr int N = PlanetSurfaceState::kSize;
  static constexpr int kDx[4] = {0, 1, 0, -1};
  static constexpr int kDy[4] = {-1, 0, 1, 0};

  auto canTraversePortal = [&](int fromX, int fromY, int toX, int toY) -> bool {
    const int pOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
    return toX == static_cast<int>(planetSurface_.portalX) &&
           toY == static_cast<int>(planetSurface_.portalY) &&
           fromX == static_cast<int>(planetSurface_.portalX) + kDx[pOut] &&
           fromY == static_cast<int>(planetSurface_.portalY) + kDy[pOut];
  };
  auto traversePortal = [&]() {
    if (planetSurface_.inMine == 0u) {
      planetSurface_.surfPx = planetSurface_.px;
      planetSurface_.surfPy = planetSurface_.py;
      planetSurface_.surfDir = static_cast<std::uint8_t>(planetSurface_.portalOutDir & 3u);
      LoadPlanetLayer(sim, 1u, /*placeOres=*/false);
      planetSurface_.inMine = 1u;
      const int mOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
      planetSurface_.px = static_cast<std::uint8_t>(static_cast<int>(planetSurface_.portalX) + kDx[mOut]);
      planetSurface_.py = static_cast<std::uint8_t>(static_cast<int>(planetSurface_.portalY) + kDy[mOut]);
      planetSurface_.dir = static_cast<std::uint8_t>(mOut);
    } else {
      LoadPlanetLayer(sim, 0u, /*placeOres=*/false);
      planetSurface_.inMine = 0u;
      planetSurface_.px = planetSurface_.surfPx;
      planetSurface_.py = planetSurface_.surfPy;
      planetSurface_.dir = static_cast<std::uint8_t>(planetSurface_.surfDir & 3u);
    }
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  };

  if (actionKind == 8) {
    const int fx = static_cast<int>(planetSurface_.px) + kDx[planetSurface_.dir & 3u];
    const int fy = static_cast<int>(planetSurface_.py) + kDy[planetSurface_.dir & 3u];
    if (canTraversePortal(planetSurface_.px, planetSurface_.py, fx, fy)) {
      traversePortal();
    }
    return;
  }

  if (actionKind == 1 || actionKind == 2) {
    const int sign = actionKind == 1 ? 1 : -1;
    const int nx = static_cast<int>(planetSurface_.px) + kDx[planetSurface_.dir & 3u] * sign;
    const int ny = static_cast<int>(planetSurface_.py) + kDy[planetSurface_.dir & 3u] * sign;
    if (actionKind == 1 && canTraversePortal(planetSurface_.px, planetSurface_.py, nx, ny)) {
      traversePortal();
      return;
    }
    std::uint8_t blk = 0;
    if (static_cast<unsigned>(nx) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(ny) >= static_cast<unsigned>(N)) {
      blk = (planetSurface_.inMine != 0u) ? 4u : 1u;
    } else if (planetSurface_.Tile(nx, ny) == 1u) {
      blk = ((planetSurface_.inMine != 0u) || ((planetSurface_.Variant(nx, ny) & 4) != 0)) ? 4u : 1u;
    } else if (planetSurface_.inMine == 0u &&
               nx == planetSurface_.shipX && ny == planetSurface_.shipY) {
      blk = 2u;
    } else {
      const std::string& hereId = sim.Map().here;
      for (int i = 0; i < static_cast<int>(planetSurface_.oreCount); ++i) {
        if (((planetSurface_.oreLayerMask >> i) & 1u) == planetSurface_.inMine &&
            planetSurface_.oreX[i] == nx && planetSurface_.oreY[i] == ny) {
          const std::string key = "planet-ore:" + hereId + ":" + std::to_string(i);
          auto it = sim.State().mapMarks.find(key);
          if (it == sim.State().mapMarks.end() || !it->second) {
            blk = 3u;
            break;
          }
        }
      }
    }
    if (blk == 0) {
      planetSurface_.px = static_cast<std::uint8_t>(nx);
      planetSurface_.py = static_cast<std::uint8_t>(ny);
      planetSurface_.bumpKind = 0;
      planetSurface_.bumpBack = false;
    } else {
      planetSurface_.bumpKind = blk;
      planetSurface_.bumpBack = (actionKind == 2);
    }
  } else if (actionKind == 3) {
    planetSurface_.dir = static_cast<std::uint8_t>((planetSurface_.dir + 3u) & 3u);
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  } else if (actionKind == 4) {
    planetSurface_.dir = static_cast<std::uint8_t>((planetSurface_.dir + 1u) & 3u);
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  } else if (actionKind == 7) {
    planetSurface_.dir = static_cast<std::uint8_t>((planetSurface_.dir + 2u) & 3u);
    planetSurface_.bumpKind = 0;
    planetSurface_.bumpBack = false;
  }
}

void ShellView::RenderPlanetSurfacePixels(const Simulation& sim) {
  constexpr int W = PlanetSurfaceState::kTexW;
  constexpr int H = PlanetSurfaceState::kTexH;
  constexpr int N = PlanetSurfaceState::kSize;
  const Body* planet = sim.FindBodyById(sim.Map().here);
  if (planet == nullptr) { return; }

  std::uint64_t pHash = 14695981039346656037ULL;
  for (unsigned char ch : planet->id) {
    pHash ^= static_cast<std::uint64_t>(ch);
    pHash *= 1099511628211ULL;
  }
  if (pHash == 0ULL) { pHash = 1ULL; }

  const std::size_t marksCount = sim.State().mapMarks.size();
  const int lotSerial = static_cast<int>(sim.State().lotSerial);
  std::uint8_t oreMask = planetSurface_.renderOreMask;
  if (planetSurface_.renderPlanetHash != pHash ||
      planetSurface_.renderMarksCount != marksCount ||
      planetSurface_.renderLotSerial != lotSerial ||
      oreMask == 0xFFu) {
    oreMask = 0u;
    char keyBuf[96];
    for (int i = 0; i < static_cast<int>(planetSurface_.oreCount); ++i) {
      const int n = std::snprintf(keyBuf, sizeof(keyBuf), "planet-ore:%s:%d",
                                  planet->id.c_str(), i);
      bool collected = false;
      if (n > 0 && static_cast<std::size_t>(n) < sizeof(keyBuf)) {
        auto it = sim.State().mapMarks.find(std::string(keyBuf, static_cast<std::size_t>(n)));
        collected = (it != sim.State().mapMarks.end() && it->second);
      } else {
        const std::string key = "planet-ore:" + planet->id + ":" + std::to_string(i);
        auto it = sim.State().mapMarks.find(key);
        collected = (it != sim.State().mapMarks.end() && it->second);
      }
      if (!collected) {
        oreMask = static_cast<std::uint8_t>(oreMask | (1u << i));
      }
    }
    planetSurface_.renderMarksCount = marksCount;
    planetSurface_.renderLotSerial = lotSerial;
  }

  if (planetTex_.id != 0 &&
      planetSurface_.renderPlanetHash == pHash &&
      planetSurface_.renderPx == static_cast<std::int8_t>(planetSurface_.px) &&
      planetSurface_.renderPy == static_cast<std::int8_t>(planetSurface_.py) &&
      planetSurface_.renderDir == static_cast<std::int8_t>(planetSurface_.dir) &&
      planetSurface_.renderInMine == static_cast<std::int8_t>(planetSurface_.inMine) &&
      planetSurface_.renderOreMask == oreMask) {
    return;
  }

  planetSurface_.renderPlanetHash = pHash;
  planetSurface_.renderPx = static_cast<std::int8_t>(planetSurface_.px);
  planetSurface_.renderPy = static_cast<std::int8_t>(planetSurface_.py);
  planetSurface_.renderDir = static_cast<std::int8_t>(planetSurface_.dir);
  planetSurface_.renderInMine = static_cast<std::int8_t>(planetSurface_.inMine);
  planetSurface_.renderOreMask = oreMask;
  planetSurface_.frontKind = 0;
  planetSurface_.frontArg = 0;
  planetSurface_.frontUv = Area{};

  const std::string& subtype = planet->subtype;
  const bool lavaWorld =
      subtype == "lava" ||
      planet->klass.find("Lava") != std::string::npos ||
      planet->klass.find("Volcanic") != std::string::npos ||
      planet->klass.find("lava") != std::string::npos;
  const bool iceWorld = !lavaWorld && subtype == "ice";
  const bool oceanWorld = !lavaWorld && subtype == "ocean";
  const bool rockWorld = !lavaWorld && !iceWorld && !oceanWorld &&
      (subtype == "rock" ||
       planet->klass.find("Rugged") != std::string::npos ||
       planet->klass.find("Barren") != std::string::npos ||
       planet->klass.find("Crater") != std::string::npos);

  const int biomeIdx = iceWorld ? 1 : oceanWorld ? 2 : lavaWorld ? 3 : rockWorld ? 4 : 0;

  // Deterministic per-planet visual DNA (0 B struct increase):
  // 4 distinct sub-biome palettes & moods per biome (20 total) + sky, horizon, road & prop variations.
  const std::uint32_t pSeed =
      static_cast<std::uint32_t>(pHash ^ (pHash >> 32)) ^
      (static_cast<std::uint32_t>(sim.World().chartSettings.seed) * 2654435761u);
  const int subBiome      = static_cast<int>((pSeed >> 2) & 3u);
  const int celestialKind = static_cast<int>((pSeed >> 5) & 3u);
  const int cloudStyle    = static_cast<int>((pSeed >> 7) & 3u);
  const int horizonStyle  = static_cast<int>((pSeed >> 9) & 3u);
  const int roadStyle     = static_cast<int>((pSeed >> 11) & 1u);
  const int propBias      = static_cast<int>((pSeed >> 12) & 3u);
  const int palIdx        = biomeIdx * 4 + subBiome;
  const bool airlessWorld = (biomeIdx == 4 && subBiome == 1);
  const bool nightSky     = airlessWorld ||
                            (biomeIdx == 1 && subBiome == 1) ||
                            (biomeIdx == 2 && subBiome == 3) ||
                            (biomeIdx == 3 && subBiome == 3);
  const bool duskSky      = (biomeIdx == 0 && (subBiome == 1 || subBiome == 2)) ||
                            (biomeIdx == 1 && subBiome == 2) ||
                            (biomeIdx == 2 && (subBiome == 1 || subBiome == 2)) ||
                            (biomeIdx == 4 && subBiome == 2);

  // PC-98 .GTA-style 12-bit RGB4 (4-4-4, 0..15 * 17) nibble-packed biome palettes:
  // 16 colors * 12 bits = 24 B per sub-biome, 480 B total across 20 sub-biomes (5 biomes x 4 moods).
  struct PackedBiomePalettes {
    std::uint8_t bytes[20][24]{};
  };
  static constexpr PackedBiomePalettes kBiomePalettes = []() constexpr {
    PackedBiomePalettes p{};
    auto setPair = [&](int b, int pairIdx,
                       int r0, int g0, int b0,
                       int r1, int g1, int b1) constexpr {
      const int o = pairIdx * 3;
      p.bytes[b][o + 0] = static_cast<std::uint8_t>(((r0 & 0xF) << 4) | (g0 & 0xF));
      p.bytes[b][o + 1] = static_cast<std::uint8_t>(((b0 & 0xF) << 4) | (r1 & 0xF));
      p.bytes[b][o + 2] = static_cast<std::uint8_t>(((g1 & 0xF) << 4) | (b1 & 0xF));
    };
    // 0: Temperate 0 — Emerald Noon Meadow
    setPair(0, 0,  1, 1, 1,   1, 2, 4);   // 0:ink,      1:sky0
    setPair(0, 1,  2, 4, 7,   4, 7,10);   // 2:sky1,     3:sky2
    setPair(0, 2,  7,11,12,   2, 4, 2);   // 4:sky3,     5:gnd0
    setPair(0, 3,  3, 6, 3,   5, 8, 3);   // 6:gnd1,     7:gnd2
    setPair(0, 4,  4, 4, 5,   7, 7, 8);   // 8:stone shadow, 9:stone face
    setPair(0, 5, 11,11,11,   8, 8, 9);   // 10:stone lit,    11:stone mid
    setPair(0, 6,  9,10, 8,   1, 4, 2);   // 12:rockHi,  13:leaf0
    setPair(0, 7,  3, 8, 3,   7,11, 4);   // 14:leaf1,   15:leaf2
    // 1: Temperate 1 — Golden Autumn Highlands
    setPair(1, 0,  1, 1, 1,   2, 2, 4);
    setPair(1, 1,  5, 3, 5,   9, 5, 4);
    setPair(1, 2, 13, 9, 6,   3, 3, 1);
    setPair(1, 3,  5, 5, 2,   7, 6, 2);
    setPair(1, 4,  4, 3, 3,   7, 6, 5);
    setPair(1, 5, 10, 9, 7,   7, 5, 3);
    setPair(1, 6, 11, 8, 5,   6, 2, 1);
    setPair(1, 7, 11, 4, 2,  14, 9, 3);
    // 2: Temperate 2 — Misty Pine Twilight
    setPair(2, 0,  0, 1, 1,   1, 1, 3);
    setPair(2, 1,  2, 2, 6,   5, 4, 8);
    setPair(2, 2,  8, 7,10,   1, 3, 3);
    setPair(2, 3,  2, 5, 4,   4, 6, 5);
    setPair(2, 4,  3, 2, 2,   5, 4, 3);
    setPair(2, 5,  8, 7, 5,   4, 5, 6);
    setPair(2, 6,  8, 9,10,   1, 3, 3);
    setPair(2, 7,  2, 6, 5,   5, 9, 7);
    // 3: Temperate 3 — Alien Teal-Amber Biosphere
    setPair(3, 0,  1, 0, 1,   1, 3, 4);
    setPair(3, 1,  2, 6, 7,   4, 9, 9);
    setPair(3, 2,  8,12,11,   1, 4, 4);
    setPair(3, 3,  2, 6, 5,   4, 8, 6);
    setPair(3, 4,  5, 4, 5,   8, 7, 7);
    setPair(3, 5, 11,10, 9,   5, 4, 6);
    setPair(3, 6,  9, 7, 9,   7, 2, 5);
    setPair(3, 7, 11, 4, 6,  14, 9, 7);

    // 4: Ice 0 — Glacial Daylight
    setPair(4, 0,  1, 1, 2,   1, 2, 4);
    setPair(4, 1,  2, 4, 8,   4, 8,11);
    setPair(4, 2,  8,12,14,   4, 6, 8);
    setPair(4, 3,  6, 9,11,   9,12,13);
    setPair(4, 4,  2, 4, 6,   5, 7, 9);
    setPair(4, 5,  9,11,13,   5, 7, 9);
    setPair(4, 6, 10,13,15,   2, 5, 7);
    setPair(4, 7,  5,10,12,  12,15,15);
    // 5: Ice 1 — Aurora Borealis Night
    setPair(5, 0,  0, 0, 1,   0, 1, 2);
    setPair(5, 1,  1, 2, 4,   1, 4, 6);
    setPair(5, 2,  2, 7, 8,   2, 3, 5);
    setPair(5, 3,  3, 5, 8,   5, 7,10);
    setPair(5, 4,  1, 2, 4,   3, 5, 7);
    setPair(5, 5,  6, 9,11,   4, 5, 8);
    setPair(5, 6,  8,10,13,   1, 6, 5);
    setPair(5, 7,  3,12, 9,   9,15,13);
    // 6: Ice 2 — Polar Rose Dusk
    setPair(6, 0,  1, 0, 1,   2, 1, 3);
    setPair(6, 1,  5, 2, 5,   9, 4, 6);
    setPair(6, 2, 13, 8, 8,   4, 4, 6);
    setPair(6, 3,  6, 6, 8,   9, 8,10);
    setPair(6, 4,  3, 3, 4,   6, 5, 7);
    setPair(6, 5, 10, 9,10,   6, 5, 7);
    setPair(6, 6, 11,10,12,   4, 4, 7);
    setPair(6, 7,  8, 7,10,  14,12,13);
    // 7: Ice 3 — Glacier Jade Crevasse
    setPair(7, 0,  0, 1, 1,   2, 3, 4);
    setPair(7, 1,  4, 6, 7,   7, 9,10);
    setPair(7, 2, 10,12,12,   2, 5, 6);
    setPair(7, 3,  4, 8, 8,   6,10,10);
    setPair(7, 4,  2, 3, 4,   4, 5, 6);
    setPair(7, 5,  8, 9, 9,   4, 7, 8);
    setPair(7, 6,  9,12,13,   1, 5, 5);
    setPair(7, 7,  3, 9, 9,  10,14,13);

    // 8: Ocean 0 — Tropical Turquoise Reef
    setPair(8, 0,  1, 1, 1,   1, 3, 6);
    setPair(8, 1,  2, 6, 9,   5, 9,12);
    setPair(8, 2,  8,13,14,   1, 5, 6);
    setPair(8, 3,  2, 7, 8,   4,10,10);
    setPair(8, 4,  4, 4, 5,   7, 7, 8);
    setPair(8, 5, 11,11,11,   8, 8, 9);
    setPair(8, 6,  7, 9,10,   1, 5, 5);
    setPair(8, 7,  3, 9, 8,  14, 6, 5);
    // 9: Ocean 1 — Stormy Sea-Stack Dusk
    setPair(9, 0,  0, 1, 1,   1, 1, 2);
    setPair(9, 1,  2, 3, 4,   4, 5, 6);
    setPair(9, 2,  7, 8, 9,   1, 3, 4);
    setPair(9, 3,  2, 4, 5,   3, 6, 7);
    setPair(9, 4,  3, 3, 4,   5, 6, 6);
    setPair(9, 5,  8, 9, 9,   5, 6, 7);
    setPair(9, 6,  6, 7, 8,   2, 4, 3);
    setPair(9, 7,  4, 7, 5,  13, 9, 4);
    // 10: Ocean 2 — Golden Sunset Atoll
    setPair(10, 0, 1, 0, 1,   2, 1, 4);
    setPair(10, 1, 6, 3, 5,  10, 5, 5);
    setPair(10, 2,14, 9, 6,   2, 3, 5);
    setPair(10, 3, 3, 5, 7,   5, 7, 8);
    setPair(10, 4, 4, 3, 3,   7, 6, 5);
    setPair(10, 5,10, 9, 8,   8, 7, 6);
    setPair(10, 6, 9, 7, 7,   2, 5, 5);
    setPair(10, 7, 5, 8, 7,  15, 7, 6);
    // 11: Ocean 3 — Bioluminescent Lagoon
    setPair(11, 0, 0, 0, 1,   0, 1, 2);
    setPair(11, 1, 1, 2, 4,   1, 4, 6);
    setPair(11, 2, 2, 7, 9,   1, 2, 4);
    setPair(11, 3, 1, 4, 6,   2, 6, 8);
    setPair(11, 4, 4, 5, 6,   7, 8, 9);
    setPair(11, 5,10,11,12,   3, 4, 6);
    setPair(11, 6, 6, 8,10,   1, 5, 6);
    setPair(11, 7, 2,11,11,   6,15,14);

    // 12: Lava 0 — Crimson Magma Inferno
    setPair(12, 0, 1, 0, 0,   2, 0, 1);
    setPair(12, 1, 5, 1, 1,   8, 2, 1);
    setPair(12, 2,12, 5, 2,   2, 1, 1);
    setPair(12, 3, 3, 2, 1,   5, 3, 2);
    setPair(12, 4, 2, 1, 2,   4, 3, 3);
    setPair(12, 5, 6, 5, 4,   5, 3, 3);
    setPair(12, 6, 9, 5, 4,   5, 2, 1);
    setPair(12, 7,14, 5, 1,  15,12, 4);
    // 13: Lava 1 — Ash-Choked Violet Caldera
    setPair(13, 0, 1, 0, 1,   2, 1, 3);
    setPair(13, 1, 4, 2, 5,   7, 3, 5);
    setPair(13, 2,10, 5, 5,   2, 1, 2);
    setPair(13, 3, 3, 2, 3,   5, 3, 4);
    setPair(13, 4, 4, 3, 4,   6, 5, 6);
    setPair(13, 5, 9, 8, 8,   6, 3, 4);
    setPair(13, 6, 8, 6, 7,   7, 2, 3);
    setPair(13, 7,13, 4, 6,  15,10, 5);
    // 14: Lava 2 — Sulfur Yellow Badlands
    setPair(14, 0, 1, 1, 0,   2, 2, 1);
    setPair(14, 1, 4, 4, 1,   8, 7, 2);
    setPair(14, 2,12,10, 3,   2, 2, 1);
    setPair(14, 3, 4, 3, 1,   5, 4, 2);
    setPair(14, 4, 4, 3, 1,   6, 5, 2);
    setPair(14, 5, 9, 8, 4,   7, 5, 3);
    setPair(14, 6, 8, 7, 4,   6, 4, 2);
    setPair(14, 7,13,11, 1,  15,15, 6);
    // 15: Lava 3 — Blue-Core Obsidian Rift
    setPair(15, 0, 0, 0, 1,   1, 1, 2);
    setPair(15, 1, 1, 2, 4,   2, 3, 6);
    setPair(15, 2, 4, 6, 9,   1, 1, 2);
    setPair(15, 3, 2, 2, 3,   3, 3, 5);
    setPair(15, 4, 3, 3, 4,   5, 5, 6);
    setPair(15, 5, 8, 8, 9,   4, 4, 6);
    setPair(15, 6, 7, 8,10,   2, 5, 8);
    setPair(15, 7, 4,11,14,  12,15,15);

    // 16: Rock 0 — Red Sandstone Canyon
    setPair(16, 0, 1, 0, 0,   3, 1, 2);
    setPair(16, 1, 6, 3, 3,  10, 5, 4);
    setPair(16, 2,13, 9, 6,   5, 2, 1);
    setPair(16, 3, 7, 4, 2,  10, 5, 3);
    setPair(16, 4, 4, 2, 2,   7, 5, 3);
    setPair(16, 5,11, 8, 5,   9, 5, 3);
    setPair(16, 6,12, 7, 4,   5, 4, 2);
    setPair(16, 7, 6, 7, 3,  10,10, 5);
    // 17: Rock 1 — Airless Starlit Crater World
    setPair(17, 0, 0, 0, 1,   0, 0, 1);
    setPair(17, 1, 0, 0, 1,   1, 1, 2);
    setPair(17, 2, 2, 3, 4,   3, 3, 4);
    setPair(17, 3, 5, 5, 6,   7, 7, 8);
    setPair(17, 4, 4, 4, 5,   8, 7, 7);
    setPair(17, 5,12,11, 9,   7, 6, 5);
    setPair(17, 6,10, 9, 7,   3, 4, 5);
    setPair(17, 7, 6, 7, 8,  11,12,13);
    // 18: Rock 2 — Copper-Rust Badlands
    setPair(18, 0, 1, 0, 1,   3, 2, 3);
    setPair(18, 1, 6, 3, 4,   9, 5, 4);
    setPair(18, 2,12, 8, 6,   4, 2, 2);
    setPair(18, 3, 6, 3, 2,   8, 5, 3);
    setPair(18, 4, 5, 4, 3,   8, 7, 5);
    setPair(18, 5,11,10, 8,   8, 4, 3);
    setPair(18, 6,10, 7, 5,   3, 5, 4);
    setPair(18, 7, 4, 9, 7,   8,12,10);
    // 19: Rock 3 — Golden Steppe Monoliths
    setPair(19, 0, 1, 1, 1,   2, 4, 6);
    setPair(19, 1, 4, 6, 8,   7, 9,10);
    setPair(19, 2,11,12,11,   6, 5, 3);
    setPair(19, 3, 8, 7, 4,  11, 9, 5);
    setPair(19, 4, 5, 4, 3,   8, 7, 5);
    setPair(19, 5,12,11, 8,   9, 7, 1);
    setPair(19, 6,12,11, 8,   5, 4, 1);
    setPair(19, 7, 6, 7, 3,  10,10, 4);
    return p;
  }();
  static_assert(sizeof(kBiomePalettes) == 480, "20 * 24 B packed PC-98 RGB4 sub-biome palettes");

  // Shared 12-bit RGB4 sprite bank (indices 16..25: stars/clouds, ship hull/visor, 3-tone ore crate)
  struct PackedSpritePalette {
    std::uint8_t bytes[15]{};
  };
  static constexpr PackedSpritePalette kSharedSpritePalette = []() constexpr {
    PackedSpritePalette p{};
    auto setPair = [&](int pairIdx, int r0, int g0, int b0, int r1, int g1, int b1) constexpr {
      const int o = pairIdx * 3;
      p.bytes[o + 0] = static_cast<std::uint8_t>(((r0 & 0xF) << 4) | (g0 & 0xF));
      p.bytes[o + 1] = static_cast<std::uint8_t>(((b0 & 0xF) << 4) | (r1 & 0xF));
      p.bytes[o + 2] = static_cast<std::uint8_t>(((g1 & 0xF) << 4) | (b1 & 0xF));
    };
    setPair(0, 14,14,15,  10,12,13);  // 16:starCool,  17:cloudCool
    setPair(1, 15,13, 9,   6, 6, 7);  // 18:warmGleam, 19:shipDark
    setPair(2,  9, 9,10,  13,13,13);  // 20:shipMid,   21:shipLight
    setPair(3,  4,12,14,  10, 6, 2);  // 22:shipVisor, 23:oreBody
    setPair(4, 14,10, 3,  14, 8, 5);  // 24:oreGold,   25:lavaCloudRim
    return p;
  }();

  constexpr std::uint8_t inkOut   = 0u;
  constexpr std::uint8_t sky0     = 1u;
  constexpr std::uint8_t sky1     = 2u;
  constexpr std::uint8_t sky2     = 3u;
  constexpr std::uint8_t sky3     = 4u;
  constexpr std::uint8_t gnd0     = 5u;
  constexpr std::uint8_t gnd1     = 6u;
  constexpr std::uint8_t gnd2     = 7u;
  constexpr std::uint8_t pathDark = 8u;
  constexpr std::uint8_t pathMid  = 9u;
  constexpr std::uint8_t pathHi   = 10u;
  const std::uint8_t rockDark     = oceanWorld ? gnd0 : pathDark;
  constexpr std::uint8_t rockMid  = 11u;
  constexpr std::uint8_t rockHi   = 12u;
  const std::uint8_t trunkCol     = oceanWorld ? pathDark : pathMid;
  constexpr std::uint8_t leaf0    = 13u;
  constexpr std::uint8_t leaf1    = 14u;
  const std::uint8_t leaf2        = oceanWorld ? sky3 : static_cast<std::uint8_t>(15u);
  const std::uint8_t accent0      = iceWorld   ? leaf1
                                  : oceanWorld ? static_cast<std::uint8_t>(15u)
                                  : lavaWorld  ? leaf1
                                  : (rockWorld && subBiome == 2) ? leaf1
                                               : rockHi;
  const std::uint8_t accent1      = iceWorld   ? static_cast<std::uint8_t>(15u)
                                  : oceanWorld ? static_cast<std::uint8_t>(18u)
                                  : lavaWorld  ? static_cast<std::uint8_t>(15u)
                                  : (rockWorld && subBiome == 2) ? static_cast<std::uint8_t>(15u)
                                               : static_cast<std::uint8_t>(18u);
  constexpr std::uint8_t kIdxStarCool  = 16u;
  constexpr std::uint8_t kIdxCloudCool = 17u;
  constexpr std::uint8_t kIdxWarmGleam = 18u;
  constexpr std::uint8_t kIdxShipDark  = 19u;
  constexpr std::uint8_t kIdxShipMid   = 20u;
  constexpr std::uint8_t kIdxShipLight = 21u;
  constexpr std::uint8_t kIdxShipVisor = 22u;
  constexpr std::uint8_t kIdxOreBody   = 23u;
  constexpr std::uint8_t kIdxOreGold   = 24u;
  constexpr std::uint8_t kIdxLavaRim   = 25u;
  constexpr int kBaseColorCount = 26;
  constexpr std::uint8_t kMidFogBit = 32u;  // 32..57: RGB4 midpoint blend toward sky3
  constexpr std::uint8_t kInkFogBase = 64u; // 64..68: discrete RGB4 atmospheric silhouette inks

  // Unpack active 4-4-4 RGB4 palette + pre-quantized RGB4 mid-haze and silhouette entries
  auto unpackNibbleRgb = [](const std::uint8_t* packed, int slot, int& r4, int& g4, int& b4) {
    const int base = (slot >> 1) * 3;
    if ((slot & 1) == 0) {
      r4 = (packed[base + 0] >> 4) & 0x0F;
      g4 =  packed[base + 0]       & 0x0F;
      b4 = (packed[base + 1] >> 4) & 0x0F;
    } else {
      r4 =  packed[base + 1]       & 0x0F;
      g4 = (packed[base + 2] >> 4) & 0x0F;
      b4 =  packed[base + 2]       & 0x0F;
    }
  };
  auto rgb4ToColor = [](int r4, int g4, int b4) -> Color {
    return Color{static_cast<unsigned char>(std::clamp(r4, 0, 15) * 17),
                 static_cast<unsigned char>(std::clamp(g4, 0, 15) * 17),
                 static_cast<unsigned char>(std::clamp(b4, 0, 15) * 17),
                 255};
  };

  int baseR4[kBaseColorCount]{};
  int baseG4[kBaseColorCount]{};
  int baseB4[kBaseColorCount]{};
  for (int i = 0; i < 16; ++i) {
    unpackNibbleRgb(kBiomePalettes.bytes[palIdx], i, baseR4[i], baseG4[i], baseB4[i]);
  }
  for (int i = 0; i < 10; ++i) {
    unpackNibbleRgb(kSharedSpritePalette.bytes, i, baseR4[16 + i], baseG4[16 + i], baseB4[16 + i]);
  }

  const bool inMine = (planetSurface_.inMine != 0u);
  const std::uint8_t fogTargetIdx = inMine ? inkOut : sky3;

  Color resolvedLut[72]{};
  const int fogR4 = baseR4[fogTargetIdx];
  const int fogG4 = baseG4[fogTargetIdx];
  const int fogB4 = baseG4[fogTargetIdx];
  for (int i = 0; i < kBaseColorCount; ++i) {
    resolvedLut[i] = rgb4ToColor(baseR4[i], baseG4[i], baseB4[i]);
    resolvedLut[kMidFogBit + i] = rgb4ToColor((baseR4[i] + fogR4 + 1) >> 1,
                                              (baseG4[i] + fogG4 + 1) >> 1,
                                              (baseB4[i] + fogB4 + 1) >> 1);
  }
  // 4 stepped RGB4 silhouette inks toward sky2 (d=4..6) + 1 horizon ridge crest toward sky3
  static constexpr int kInkWeights[4] = {4, 7, 9, 12};  // out of 16
  for (int k = 0; k < 4; ++k) {
    const int w = kInkWeights[k];
    resolvedLut[kInkFogBase + k] = rgb4ToColor(
        (baseR4[inkOut] * (16 - w) + baseR4[sky2] * w + 8) >> 4,
        (baseG4[inkOut] * (16 - w) + baseG4[sky2] * w + 8) >> 4,
        (baseB4[inkOut] * (16 - w) + baseB4[sky2] * w + 8) >> 4);
  }
  resolvedLut[kInkFogBase + 4] = rgb4ToColor(
      (baseR4[inkOut] * 7 + fogR4 * 9 + 8) >> 4,
      (baseG4[inkOut] * 7 + fogG4 * 9 + 8) >> 4,
      (baseB4[inkOut] * 7 + fogB4 * 9 + 8) >> 4);

  // Static BSS 480x288 8-bit indexed framebuffer (138,240 B static, 0 B heap, 0 B stack)
  static std::uint8_t idxBuf[W * H];
  std::memset(idxBuf, 0, sizeof(idxBuf));

  constexpr int kResScale = W / 240;  // 2 at 480x288 (PC-98 Clouds of Xeen 432x280 class!)
  constexpr int hy = 69 * kResScale;
  constexpr int kGndH = H - hy;
  constexpr int cxMid = W / 2;
  static constexpr float kProjY = 33.0f * static_cast<float>(kResScale);
  static constexpr float kProjX = 126.0f * static_cast<float>(kResScale);
  static constexpr float kHalfRoad = 0.44f;

  static constexpr int kBayer4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  struct FogMaskLut {
    std::uint8_t row[17][4]{};
  };
  static constexpr FogMaskLut kFogMasks = []() constexpr {
    FogMaskLut m{};
    for (int thr = 0; thr <= 16; ++thr) {
      for (int ry = 0; ry < 4; ++ry) {
        std::uint8_t bits = 0u;
        for (int rx = 0; rx < 4; ++rx) {
          if (kBayer4[ry][rx] < thr) {
            bits = static_cast<std::uint8_t>(bits | (1u << rx));
          }
        }
        m.row[thr][ry] = bits;
      }
    }
    return m;
  }();
  struct ScanlineProjLut {
    float z[kGndH]{};
    float invScaleX[kGndH]{};
    std::uint8_t fogStep[kGndH]{};  // 0..32 two-band 4x4 Bayer mist (0..16 -> midHaze, 16..32 -> sky3)
  };
  static constexpr ScanlineProjLut kScanLut = []() constexpr {
    ScanlineProjLut lut{};
    constexpr float kMinDenom = 1.125f * static_cast<float>(kResScale);
    for (int gy = 0; gy < kGndH; ++gy) {
      const float denom = (static_cast<float>(gy) + 0.5f) < kMinDenom
                              ? kMinDenom
                              : (static_cast<float>(gy) + 0.5f);
      const float z = kProjY / denom;
      lut.z[gy] = z;
      lut.invScaleX[gy] = z / kProjX;
      if (z <= 2.40f) {
        lut.fogStep[gy] = 0u;
      } else {
        const float x = (z - 2.40f) * 0.34f;
        const float expPos = 1.0f + x + 0.5f * x * x + (x * x * x) * (1.0f / 6.0f);
        const float fog = 1.0f - (1.0f / expPos);
        const int st = static_cast<int>(fog * 32.0f + 0.5f);
        lut.fogStep[gy] = static_cast<std::uint8_t>(st > 31 ? 31 : (st < 0 ? 0 : st));
      }
    }
    return lut;
  }();
  // Discrete 0..32 mist/torchlight steps for painter depths d = 0..6
  static constexpr std::uint8_t kDepthFog[7] = {0u, 0u, 0u, 6u, 13u, 20u, 26u};
  static constexpr std::uint8_t kMineDepthFog[7] = {0u, 0u, 5u, 12u, 19u, 28u, 32u};

  // 32x32 PC-98 Might & Magic tileable pixel-art textures (3 KB ROM, 0 B heap):
  // '.'=0 (ink/mortar), ':'=1 (dark bevel/shadow), '+'=2 (mid stone), '#'=3 (sunlit highlight)
  struct TileTex32 {
    std::uint8_t px[32][32]{};
    static constexpr TileTex32 FromArt(const char (&art)[32][33]) {
      TileTex32 t{};
      for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
          const char c = art[y][x];
          t.px[y][x] = (c == '#') ? 3u : (c == '+') ? 2u : (c == ':') ? 1u : 0u;
        }
      }
      return t;
    }
  };
  // World of Xeen chunky rounded beveled dungeon / castle ashlar stone blocks
  static constexpr char kStoneWallArt[32][33] = {
      "................................",
      ".#########.#########.#########.#",
      ".#+:+++++:.#+++++++:.#+:+++++:.#",
      ".#+++++++:.#+++++++:.#+++++++:.#",
      ".#+++++++:.#+++:+++:.#+++++++:.#",
      ".#+++++++:.#+++++++:.#+++++++:.#",
      ".#+++++++:.#+++++++:.#+++++++:.#",
      ".:::::::::.:::::::::.:::::::::.:",
      "................................",
      "##.#############.###########.###",
      "+:.#+++++++++++:.#+++++++++:.#++",
      "+:.#++:++++++++:.#+++++++++:.#++",
      "+:.#+++++++++++:.#+++++++++:.#++",
      "+:.#+++++++++++:.#++++++:++:.#++",
      "+:.#+++++++++++:.#+++++++++:.#++",
      "::.:::::::::::::.:::::::::::.:::",
      "................................",
      "######.###########.##########.##",
      "+++++:.#+++:+++++:.#++++:+++:.#+",
      "+++++:.#+++++++++:.#++++++++:.#+",
      "+:+++:.#+++++++++:.#++++++++:.#+",
      "+++++:.#+++++++++:.#++++++++:.#+",
      "+++++:.#+++++++++:.#++++++++:.#+",
      "::::::.:::::::::::.::::::::::.::",
      "................................",
      "#######.#############.##########",
      "++++++:.#+:+++++++++:.#+++++++++",
      "++++++:.#+++++++++++:.#+++++++++",
      "++++++:.#+++++++++++:.#+:+++++++",
      "+++:++:.#+++++++++++:.#+++++++++",
      "++++++:.#+++++++++++:.#+++++++++",
      ":::::::.:::::::::::::.::::::::::",
  };
  static constexpr TileTex32 kStoneWallTex = TileTex32::FromArt(kStoneWallArt);
  // Clouds of Xeen irregular rounded riverstone / cobblestone road tiles
  // ('.'=crisp dark stone outline, ':'=warm earth mortar, '+'=stone face, '#'=sunlit stone crown)
  static constexpr char kCobbleRoadArt[32][33] = {
      "::.#####.:::::.######.::::.###.:",
      ":.#######.:::.########.::.#####.",
      ":.#++++++.:.:.#+++++++.::.#++++.",
      ":.#++#+++.:::.#+++#+++.::.#++++.",
      "::.++++++.::::.+++++++.:::.++++.",
      ":::......::::::.......:::::....:",
      "::::::::::::::::::::::::::::::::",
      "####.:::::.########.::::.#####.:",
      "++++.::::.##########.::.######.:",
      "+++..::::.#+++++++++.::.#+++++.:",
      "++..:::::.#+++#+++++.::.#++#++.:",
      "...:::::::.+++++++++.:::.+++++.:",
      ":::::::::::.........:::::.....::",
      "::::::::::::::::::::::::::::::::",
      "::.######.::::.#######.::::.###.",
      ":.########.::.#########.::.#####",
      ":.#+++++++.::.#++++++++.::.#++++",
      ":.#++#++++.::.#+++#++++.::.#++++",
      "::.+++++++.:::.++++++++.:::.++++",
      ":::.......:::::........:::::....",
      "::::::::::::::::::::::::::::::::",
      "#####.::::.######.::::.######.::",
      "+++++.:::.########.::.#######.::",
      "++++..:::.#+++++++.::.#++++++.::",
      "+++..::::.#++#++++.::.#+++#++.::",
      "....::::::.+++++++.:::.++++++.::",
      ":::::::::::.......:::::......:::",
      "::::::::::::::::::::::::::::::::",
      "::.#######.::::.######.::::.###.",
      ":.#########.::.########.::.#####",
      ":.#++++++++.::.#+++++++.::.#++++",
      "::.++++++++.:::.+++++++.:::.++++",
  };
  static constexpr TileTex32 kCobbleRoadTex = TileTex32::FromArt(kCobbleRoadArt);

  // ---- PC-98 road stone field ----------------------------------------------
  // Generated once, 128x128 texels, sampled 1:1 against the world so the road
  // never shows a repeating tile. Jittered feature points are welded into
  // stones by their F2-F1 distance, giving joints that meet at odd angles
  // instead of a brick grid; each stone gets its own tone, then a lit rim on
  // the upper left and a shadow rim on the lower right, with near-black mortar
  // in the gaps. 0 = mortar, 1 = shadow, 2 = face, 3 = lit.
  struct StoneField { std::uint8_t v[128][128]{}; };
  static const StoneField kStoneField = []() {
    StoneField f;
    auto mix = [](unsigned n) {
      n ^= n >> 16; n *= 0x7FEB352Du;
      n ^= n >> 15; n *= 0x846CA68Bu;
      n ^= n >> 16;
      return n;
    };
    auto feat = [&](int cx, int cy, int k) {
      return mix((static_cast<unsigned>(cx & 7) << 9) ^
                 (static_cast<unsigned>(cy & 7) << 4) ^
                 static_cast<unsigned>(k * 37 + 11));
    };
    // Low-frequency warp of the sample point. Without it the cell lattice
    // shows through as rows of stones of the same width; with it the rows
    // wander and the stones come out in irregular sizes.
    auto warp = [&](int x, int y, int salt) {
      constexpr int kW = 32;
      const int lx = x / kW, ly = y / kW;
      const int fx = ((x % kW) * 256) / kW, fy = ((y % kW) * 256) / kW;
      const int a00 = static_cast<int>(feat(lx, ly, salt) & 63u);
      const int a10 = static_cast<int>(feat(lx + 1, ly, salt) & 63u);
      const int a01 = static_cast<int>(feat(lx, ly + 1, salt) & 63u);
      const int a11 = static_cast<int>(feat(lx + 1, ly + 1, salt) & 63u);
      const int top = a00 + (((a10 - a00) * fx) >> 8);
      const int bot = a01 + (((a11 - a01) * fx) >> 8);
      return ((top + (((bot - top) * fy) >> 8)) * 9) / 64 - 4;   // -4..+5 texels
    };
    constexpr int kCell = 16;            // 8 stones across the field
    for (int y = 0; y < 128; ++y) {
      for (int x = 0; x < 128; ++x) {
        const int xw = x + warp(x, y, 5);
        const int yw = y + warp(x, y, 6);
        const int cx0 = (xw - kCell / 2) / kCell;
        const int cy0 = (yw - kCell / 2) / kCell;
        int bestD = 1 << 22, nextD = 1 << 22, bx = 0, by = 0;
        for (int oy = 0; oy <= 1; ++oy) {
          for (int ox = 0; ox <= 1; ++ox) {
            const int cx = cx0 + ox;
            const int cy = cy0 + oy;
            // Jitter stays near the cell centre: enough to make the joints
            // wander and meet at odd angles, not enough to collapse cells into
            // slivers the way full-cell jitter does.
            const int jx = cx * kCell + kCell / 2 + static_cast<int>(feat(cx, cy, 1) % 5u) - 2;
            const int jy = cy * kCell + kCell / 2 + static_cast<int>(feat(cx, cy, 2) % 5u) - 2;
            int dx = xw - jx, dy = yw - jy;
            if (dx > 64) { dx -= 128; } else if (dx < -64) { dx += 128; }
            if (dy > 64) { dy -= 128; } else if (dy < -64) { dy += 128; }
            // Stones are generated taller than they are wide, because the
            // perspective compresses rows on screen: this keeps them reading
            // as rounded cobbles instead of wide horizontal bars.
            const int d = dx * dx + (dy * dy * 39) / 100;
            if (d < bestD) { nextD = bestD; bestD = d; bx = cx; by = cy; }
            else if (d < nextD) { nextD = d; }
          }
        }
        const float edge = std::sqrt(static_cast<float>(nextD)) -
                           std::sqrt(static_cast<float>(bestD));
        if (edge < 1.25f) { f.v[y][x] = 0u; continue; }  // mortar seam
        const unsigned tone = feat(bx, by, 3) % 10u;
        f.v[y][x] = static_cast<std::uint8_t>(tone < 2u ? 1u : (tone < 9u ? 2u : 3u));
      }
    }
    // Relief, light from above: the top of each stone catches it and the
    // bottom falls away into shadow. The sides are left alone on purpose -
    // rimming them turns a round cobble back into a rectangular chip.
    for (int y = 0; y < 128; ++y) {
      for (int x = 0; x < 128; ++x) {
        if (f.v[y][x] == 0u) { continue; }
        if (f.v[(y + 127) & 127][x] == 0u) { f.v[y][x] = 3u; continue; }
        if (f.v[(y + 1) & 127][x] == 0u) { f.v[y][x] = 1u; continue; }
        if (f.v[y][(x + 1) & 127] == 0u) { f.v[y][x] = 1u; }
      }
    }
    return f;
  }();
  // Clouds of Xeen jagged faceted craggy mountain / boulder rock texture
  static constexpr char kCragRockArt[32][33] = {
      "##++::..###++:..##+++::.###+::..",
      "###++:..##+++::.###++:..##+++::.",
      "+##+++:.+##++::.++##++:.+##+++:.",
      ":+##++:::+##+::..+##++:::+#++::.",
      ".:++#++:.:++::..::++#++:.:++::..",
      "..:++:::..:::...:.::++::..:::...",
      "...::::...::.###:...:::...::.##.",
      ".##:::..:##+####++:..::..:##+###",
      "####+::.####++++++::.##+.####+++",
      "###++::.+###+++#++:..###++###+++",
      "+##+++:.+##+++++++::.+##+++##++:",
      ":+##++:::+##++:+++::.:+##+++#+::",
      ".:++++::.:++::.:++::..:++++++::.",
      "..:::....::.....:::....::::::...",
      "...:..####+::....:..####+::.....",
      ".##+:######++::.##+:######++::..",
      "####+###+++++::####+###++++++::.",
      "###++##++++++:.###++##+++#++++:.",
      "+##+++#+++#++:.+##+++#++++++++::",
      ":+##++#+++++::.:+##++#++++:+++:.",
      ".:+++++++:++::..:++++++::.:++::.",
      "..::+++:::++:...::+++::...:++:..",
      "...::::..:::.....::::.....:::...",
      "###+::...:..####+::...:..####+:.",
      "####++:.##+:#####++:.##+:#####+:",
      "+###++:####+##+++++:####+##++++:",
      ":+##++:+###+++#++++:+###+++++++:",
      ".:+##+::+##+++++++:::+##+++:++::",
      "..:+++:::+##++:+++::.:+++::.::..",
      "...::::..:++::::++:...::::......",
      "..::......::....::....::...###..",
      ".##++::..####+:..##++::..####+:.",
  };
  static constexpr TileTex32 kCragRockTex = TileTex32::FromArt(kCragRockArt);

  // ---- PC-98 ground field ---------------------------------------------------
  // Smooth value noise at three scales, sampled 1:1 with the world, so open
  // ground reads as patches of deeper and drier cover with dithered borders
  // instead of one flat dither everywhere. Stored raw 0..255: the sampling
  // side does the quantising, so the level count stays a rendering decision.
  struct GroundField { std::uint8_t v[128][128]{}; };
  static const GroundField kGroundField = []() {
    GroundField f;
    auto mix = [](unsigned n) {
      n ^= n >> 16; n *= 0x7FEB352Du;
      n ^= n >> 15; n *= 0x846CA68Bu;
      n ^= n >> 16;
      return n;
    };
    auto lattice = [&](int lx, int ly, int period, int salt) {
      return static_cast<int>(mix((static_cast<unsigned>(lx & 31) * 73856093u) ^
                                  (static_cast<unsigned>(ly & 31) * 19349663u) ^
                                  static_cast<unsigned>(period * 7919 + salt)) & 0xFFu);
    };
    auto smooth = [](int t) { return (t * t * (3 * 256 - 2 * t)) / (256 * 256); };
    static constexpr int kPeriods[3] = {8, 21, 47};
    static constexpr int kWeights[3] = {2, 3, 3};
    for (int y = 0; y < 128; ++y) {
      for (int x = 0; x < 128; ++x) {
        int acc = 0, wsum = 0;
        for (int o = 0; o < 3; ++o) {
          const int p = kPeriods[o];
          const int lx = x / p, ly = y / p;
          const int sx = smooth(((x % p) * 256) / p), sy = smooth(((y % p) * 256) / p);
          const int a00 = lattice(lx, ly, p, o), a10 = lattice(lx + 1, ly, p, o);
          const int a01 = lattice(lx, ly + 1, p, o), a11 = lattice(lx + 1, ly + 1, p, o);
          const int top = a00 + ((a10 - a00) * sx) / 256;
          const int bot = a01 + ((a11 - a01) * sx) / 256;
          acc += (top + ((bot - top) * sy) / 256) * kWeights[o];
          wsum += kWeights[o];
        }
        f.v[y][x] = static_cast<std::uint8_t>(acc / wsum);   // 0..255 raw
      }
    }
    return f;
  }();

  int activeFog = 0;  // 0..32 (two 16-step 4x4 Bayer bands in index space)
  std::uint8_t activeInk = inkOut;
  auto inkForFog = [&](int fog32) -> std::uint8_t {
    if (inMine || fog32 <= 6) { return inkOut; }
    if (fog32 <= 13) { return kInkFogBase + 0u; }
    if (fog32 <= 19) { return kInkFogBase + 1u; }
    if (fog32 <= 24) { return kInkFogBase + 2u; }
    return kInkFogBase + 3u;
  };
  auto fogIndexPair = [&](std::uint8_t idx, int fog32,
                          std::uint8_t& idxA, std::uint8_t& idxB, int& thr16) {
    const std::uint8_t mid = static_cast<std::uint8_t>(idx | kMidFogBit);
    if (fog32 <= 16) {
      idxA = idx;
      idxB = mid;
      thr16 = fog32;
    } else {
      idxA = mid;
      idxB = fogTargetIdx;
      thr16 = std::min(16, fog32 - 16);
    }
  };

  int propClipLeft = 0, propClipRight = W, propClipTop = 0, propClipBottom = H;
  auto putPx = [&](int x, int y, std::uint8_t idx) {
    if (x >= propClipLeft && x < propClipRight && y >= propClipTop && y < propClipBottom) {
      if (idx == inkOut) {
        idxBuf[y * W + x] = activeInk;
        return;
      }
      if (activeFog > 0) {
        std::uint8_t idxA = idx, idxB = idx;
        int thr16 = 0;
        fogIndexPair(idx, activeFog, idxA, idxB, thr16);
        idx = (((kFogMasks.row[thr16][y & 3] >> (x & 3)) & 1u) != 0u) ? idxB : idxA;
      }
      idxBuf[y * W + x] = idx;
    }
  };
  auto bayerPx = [&](int x, int y, float t, std::uint8_t a, std::uint8_t b) {
    const int thr = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 16.0f);
    putPx(x, y, thr > kBayer4[y & 3][x & 3] ? b : a);
  };
  auto fillRect = [&](int x, int y, int w, int h, std::uint8_t idx) {
    const int x0 = std::max(propClipLeft, x);
    const int y0 = std::max(propClipTop, y);
    const int x1 = std::min(propClipRight, x + w);
    const int y1 = std::min(propClipBottom, y + h);
    if (idx == inkOut) {
      for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
          idxBuf[py * W + px] = activeInk;
        }
      }
      return;
    }
    if (activeFog <= 0) {
      for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
          idxBuf[py * W + px] = idx;
        }
      }
    } else {
      std::uint8_t idxA = idx, idxB = idx;
      int thr16 = 0;
      fogIndexPair(idx, activeFog, idxA, idxB, thr16);
      for (int py = y0; py < y1; ++py) {
        const std::uint8_t rowMask = kFogMasks.row[thr16][py & 3];
        for (int px = x0; px < x1; ++px) {
          idxBuf[py * W + px] = (((rowMask >> (px & 3)) & 1u) != 0u) ? idxB : idxA;
        }
      }
    }
  };
  auto fillOutlineRect = [&](int x, int y, int w, int h, std::uint8_t fill, std::uint8_t rim) {
    fillRect(x, y, w, h, rim);
    if (w > 2 && h > 2) {
      fillRect(x + 1, y + 1, w - 2, h - 2, fill);
    }
  };
  // Fast integer organic ellipse/line helpers (zero sqrt/trig, tight bounds)
  auto fillDisc = [&](int cx, int cy, int rx, int ry, std::uint8_t col) {
    if (rx <= 0 || ry <= 0) { putPx(cx, cy, col); return; }
    const int y0 = std::max(0, cy - ry);
    const int y1 = std::min(H - 1, cy + ry);
    const int x0 = std::max(0, cx - rx);
    const int x1 = std::min(W - 1, cx + rx);
    if (x0 > x1 || y0 > y1) { return; }
    const int rx2 = rx * rx;
    const int ry2 = ry * ry;
    const int lim = rx2 * ry2;
    for (int py = y0; py <= y1; ++py) {
      const int dy = py - cy;
      const int dyTerm = dy * dy * rx2;
      for (int px = x0; px <= x1; ++px) {
        const int dx = px - cx;
        if (dx * dx * ry2 + dyTerm <= lim) {
          putPx(px, py, col);
        }
      }
    }
  };
  auto fillDitherDisc = [&](int cx, int cy, int rx, int ry,
                            std::uint8_t colA, std::uint8_t colB, int thr16) {
    if (rx <= 0 || ry <= 0) { putPx(cx, cy, colA); return; }
    const int y0 = std::max(0, cy - ry);
    const int y1 = std::min(H - 1, cy + ry);
    const int x0 = std::max(0, cx - rx);
    const int x1 = std::min(W - 1, cx + rx);
    if (x0 > x1 || y0 > y1) { return; }
    const int rx2 = rx * rx;
    const int ry2 = ry * ry;
    const int lim = rx2 * ry2;
    const int clampedThr = std::clamp(thr16, 0, 16);
    for (int py = y0; py <= y1; ++py) {
      const int dy = py - cy;
      const int dyTerm = dy * dy * rx2;
      const std::uint8_t rMask = kFogMasks.row[clampedThr][py & 3];
      for (int px = x0; px <= x1; ++px) {
        const int dx = px - cx;
        if (dx * dx * ry2 + dyTerm <= lim) {
          putPx(px, py, (((rMask >> (px & 3)) & 1u) != 0u) ? colB : colA);
        }
      }
    }
  };
  auto fillOutlinedDisc = [&](int cx, int cy, int rx, int ry,
                              std::uint8_t fill, std::uint8_t rim) {
    const int orx = std::max(1, rx + 1);
    const int ory = std::max(1, ry + 1);
    const int y0 = std::max(0, cy - ory);
    const int y1 = std::min(H - 1, cy + ory);
    const int x0 = std::max(0, cx - orx);
    const int x1 = std::min(W - 1, cx + orx);
    if (x0 > x1 || y0 > y1) { return; }
    const int orx2 = orx * orx, ory2 = ory * ory, olim = orx2 * ory2;
    const int irx = std::max(0, rx), iry = std::max(0, ry);
    const int irx2 = irx * irx, iry2 = iry * iry, ilim = irx2 * iry2;
    for (int py = y0; py <= y1; ++py) {
      const int dy = py - cy;
      const int ody = dy * dy * orx2;
      const int idy = dy * dy * irx2;
      for (int px = x0; px <= x1; ++px) {
        const int dx = px - cx;
        const int dx2 = dx * dx;
        if (dx2 * ory2 + ody <= olim) {
          const bool inside = (irx > 0 && iry > 0) && (dx2 * iry2 + idy <= ilim);
          putPx(px, py, inside ? fill : rim);
        }
      }
    }
  };
  auto drawHLine = [&](int x0, int x1, int y, std::uint8_t col) {
    if (y < 0 || y >= H) { return; }
    if (x0 > x1) { std::swap(x0, x1); }
    const int cx0 = std::max(0, x0);
    const int cx1 = std::min(W - 1, x1);
    if (cx0 <= cx1) { fillRect(cx0, y, cx1 - cx0 + 1, 1, col); }
  };

  static constexpr int kDx[4] = {0, 1, 0, -1};
  static constexpr int kDy[4] = {-1, 0, 1, 0};
  const int fdx = kDx[planetSurface_.dir];
  const int fdy = kDy[planetSurface_.dir];
  const int rdx = kDx[(planetSurface_.dir + 1) % 4];
  const int rdy = kDy[(planetSurface_.dir + 1) % 4];
  const int ldx = -rdx;
  const int ldy = -rdy;

  auto tileAt = [&](int tx, int ty) -> std::uint8_t {
    if (static_cast<unsigned>(tx) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(ty) >= static_cast<unsigned>(N)) {
      return 1u;
    }
    return planetSurface_.Tile(tx, ty);
  };
  auto variantAt = [&](int tx, int ty) -> int {
    if (static_cast<unsigned>(tx) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(ty) >= static_cast<unsigned>(N)) {
      return static_cast<int>((static_cast<unsigned>(tx + 32) * 7u +
                               static_cast<unsigned>(ty + 32) * 13u) & 7u);
    }
    return planetSurface_.Variant(tx, ty);
  };
  auto isBorderTile = [&](int tx, int ty) -> bool {
    return tx <= 0 || tx >= N - 1 || ty <= 0 || ty >= N - 1;
  };
  auto isPathTile = [&](int tx, int ty) -> bool {
    const std::uint8_t t = tileAt(tx, ty);
    return t == 2 || t == 3;
  };
  auto oreAt = [&](int tx, int ty) -> int {
    for (int i = 0; i < static_cast<int>(planetSurface_.oreCount); ++i) {
      if ((oreMask & (1u << i)) != 0u &&
          ((planetSurface_.oreLayerMask >> i) & 1u) == planetSurface_.inMine &&
          planetSurface_.oreX[i] == tx && planetSurface_.oreY[i] == ty) {
        return i;
      }
    }
    return -1;
  };
  auto isCliffCell = [&](int x, int y) -> bool {
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(N) ||
        static_cast<unsigned>(y) >= static_cast<unsigned>(N)) {
      return false;
    }
    if (tileAt(x, y) != 1) { return false; }
    const int v = variantAt(x, y);
    if (inMine) { return (v & 4) != 0; }
    // On surface, ONLY ancient fortress/ruin masonry (var == 7, non-portal) forms vertical walls;
    // var 4..6 & surface mine portal are sculpted sloped organic mountains & domed cave mounds!
    if (x == static_cast<int>(planetSurface_.portalX) &&
        y == static_cast<int>(planetSurface_.portalY)) {
      return false;
    }
    return v == 7;
  };

  // True 1/z first-person pinhole projection (camera eye inside d=0, ~88 deg FOV)
  auto yAtZ = [&](float z) -> int {
    return hy + static_cast<int>(std::lround(kProjY / std::max(0.35f, z) - 0.5f));
  };
  auto halfTileAtZ = [&](float z) -> float {
    return (0.5f * kProjX) / std::max(0.35f, z);
  };
  auto tileCenterAtZ = [&](int s, float z) -> float {
    return static_cast<float>(cxMid) + static_cast<float>(s) * (kProjX / std::max(0.35f, z));
  };
  // Per-cell wall height for Might & Magic-style silhouette variety (low ruins, mid bluffs, towering crags)
  auto cliffTopScaleFor = [&](int tx, int ty, int var) -> float {
    if (inMine) { return 1.00f; }
    if (tx == static_cast<int>(planetSurface_.portalX) &&
        ty == static_cast<int>(planetSurface_.portalY)) {
      return 0.48f;
    }
    const unsigned h = (static_cast<unsigned>(tx) * 73u) ^
                       (static_cast<unsigned>(ty) * 151u) ^
                       (pSeed >> 5) ^ (static_cast<unsigned>(var) * 29u);
    const bool border = isBorderTile(tx, ty);
    if (var == 5) {
      // Towering crags, glacial peaks, basalt organ-pipes, sandstone hoodoo mesas
      const float base = border ? 0.68f : 0.56f;
      return base + static_cast<float>(h % 4u) * 0.09f;  // 0.56 .. 0.95
    }
    if (var == 7) {
      // Uniform architectural height for connected ruined stone fortress walls (no erratic step jumps!)
      return 0.46f + static_cast<float>(pSeed & 1u) * 0.04f;
    }
    if (var == 6) {
      // Mineral-veined / overgrown cliff faces
      return 0.36f + static_cast<float>(h % 3u) * 0.11f + (border ? 0.10f : 0.0f);
    }
    // var == 4: low weathered outcrops & rolling bluffs
    static constexpr float kBluffH[4] = {0.22f, 0.34f, 0.46f, 0.58f};
    return kBluffH[h & 3u] + (border ? 0.12f : 0.0f);
  };
  auto cliffX = [&](float z, float lat) -> int {
    return cxMid + static_cast<int>(std::lround(lat * (kProjX / z)));
  };
  auto cliffYBot = [&](float z) -> int {
    return hy + static_cast<int>(std::lround(kProjY / z - 0.5f));
  };
  auto cliffYTopScaled = [&](float z, float topScale) -> int {
    return hy - static_cast<int>(std::lround(kProjY * topScale / z - (inMine ? 0.5f : 0.0f)));
  };
  auto cliffYTop = [&](float z) -> int {
    return cliffYTopScaled(z, inMine ? 1.00f : 0.42f);
  };

  // Front-to-back 1D horizontal occlusion ([xlower, xupper] + 32 B 240-bit column mask)
  // and 16 B stack-bitset LOS (uint64_t vis[2] covering all 77 frustum cells, 0 B heap).
  constexpr int kOccWords = (W + 31) / 32;  // 8 words for 240 columns
  std::uint64_t vis[2]{};         // 16 B stack bitset: frustum slots d=0..6, s=-(d+2)..+(d+2)
  std::uint32_t colOcc[kOccWords]{};
  std::uint32_t depthColOcc[7][kOccWords]{};
  std::int16_t rowX0[7]{};
  std::int16_t rowX1[7]{};
  int xlower = 0;
  int xupper = W - 1;
  int maxVisD = 0;
  bool viewSealed = false;
  float maxSealZ = 0.50f;

  auto frustumBit = [](int d, int s) -> int {
    return d * d + 4 * d + (s + d + 2);
  };
  auto setVis = [&](int d, int s) {
    const int b = frustumBit(d, s);
    vis[b >> 6] |= (std::uint64_t{1} << (b & 63));
  };
  auto isVis = [&](int d, int s) -> bool {
    const int b = frustumBit(d, s);
    return ((vis[b >> 6] >> (b & 63)) & 1ULL) != 0ULL;
  };
  auto hasUnoccludedCol = [&](const std::uint32_t occ[kOccWords], int clipL, int clipR,
                              int x0, int x1) -> bool {
    x0 = std::max(clipL, x0);
    x1 = std::min(clipR, x1);
    if (x0 > x1) { return false; }
    const int w0 = x0 >> 5;
    const int w1 = x1 >> 5;
    const std::uint32_t m0 = ~0u << (x0 & 31);
    const std::uint32_t m1 = ~0u >> (31 - (x1 & 31));
    if (w0 == w1) {
      return ((~occ[w0]) & (m0 & m1)) != 0u;
    }
    if (((~occ[w0]) & m0) != 0u) { return true; }
    for (int w = w0 + 1; w < w1; ++w) {
      if (~occ[w] != 0u) { return true; }
    }
    return ((~occ[w1]) & m1) != 0u;
  };
  auto markColSpan = [&](int x0, int x1, float zDepth) {
    x0 = std::max(0, x0);
    x1 = std::min(W - 1, x1);
    if (x0 > x1) { return; }
    const int w0 = x0 >> 5;
    const int w1 = x1 >> 5;
    const std::uint32_t m0 = ~0u << (x0 & 31);
    const std::uint32_t m1 = ~0u >> (31 - (x1 & 31));
    bool addedNew = false;
    if (w0 == w1) {
      const std::uint32_t mask = m0 & m1;
      addedNew = ((~colOcc[w0]) & mask) != 0u;
      colOcc[w0] |= mask;
    } else {
      if (((~colOcc[w0]) & m0) != 0u) { addedNew = true; }
      colOcc[w0] |= m0;
      for (int w = w0 + 1; w < w1; ++w) {
        if (~colOcc[w] != 0u) { addedNew = true; }
        colOcc[w] = ~0u;
      }
      if (((~colOcc[w1]) & m1) != 0u) { addedNew = true; }
      colOcc[w1] |= m1;
    }
    if (addedNew && zDepth > maxSealZ) {
      maxSealZ = zDepth;
    }
    while (xlower <= xupper && ((colOcc[xlower >> 5] >> (xlower & 31)) & 1u) != 0u) {
      ++xlower;
    }
    while (xlower <= xupper && ((colOcc[xupper >> 5] >> (xupper & 31)) & 1u) != 0u) {
      --xupper;
    }
  };

  for (int d = 0; d <= 6; ++d) {
    if (xlower > xupper) {
      viewSealed = true;
      break;
    }
    for (int w = 0; w < kOccWords; ++w) {
      depthColOcc[d][w] = colOcc[w];
    }
    rowX0[d] = static_cast<std::int16_t>(xlower);
    rowX1[d] = static_cast<std::int16_t>(xupper);

    const int maxSRoad = d + 2;
    if (d == 0) {
      for (int s = -maxSRoad; s <= maxSRoad; ++s) {
        setVis(0, s);
      }
      maxVisD = 0;
    } else {
      const float zNearCell = static_cast<float>(d) - 0.50f;
      const float zFarCell  = static_cast<float>(d) + 0.50f;
      for (int s = -maxSRoad; s <= maxSRoad; ++s) {
        const float latL = static_cast<float>(s) - 0.50f;
        const float latR = static_cast<float>(s) + 0.50f;
        const int xMin = std::min(cliffX(zNearCell, latL), cliffX(zFarCell, latL)) - 9 * kResScale;
        const int xMax = std::max(cliffX(zNearCell, latR), cliffX(zFarCell, latR)) + 9 * kResScale;
        if (hasUnoccludedCol(depthColOcc[d], xlower, xupper, xMin, xMax)) {
          setVis(d, s);
          maxVisD = d;
        }
      }
    }

    // Full-height wall occluders narrow [xlower, xupper] and set colOcc bits for deeper layers.
    // On the surface, walls vary in height so only full sky-blocking heights occlude columns
    // (letting towering crags & keeps peek cleanly above shorter foreground ruins).
    const bool canOccludeDeeper = inMine ? (d <= 5) : (d >= 1 && d <= 2);
    if (canOccludeDeeper) {
      const int maxSOcc = d + 1;
      for (int s = -maxSOcc; s <= maxSOcc; ++s) {
        if (d == 0 && s == 0) { continue; }
        if (!isVis(d, s)) { continue; }
        const int tx = planetSurface_.px + fdx * d + rdx * s;
        const int ty = planetSurface_.py + fdy * d + rdy * s;
        if (!isCliffCell(tx, ty)) { continue; }
        const int occVar = variantAt(tx, ty);
        const float occTopScale = cliffTopScaleFor(tx, ty, occVar);
        if (!inMine && occTopScale < 0.85f) { continue; }
        if (d == 0) {
          if (inMine && std::abs(s) == 1) {
            const int cheekW = 9 * kResScale;
            markColSpan((s < 0) ? 0 : (W - cheekW), (s < 0) ? (cheekW - 1) : (W - 1), 0.50f);
          }
          continue;
        }
        const float zNearW = static_cast<float>(d) - 0.50f;
        const float zFarW  = static_cast<float>(d) + 0.50f;
        if (!isCliffCell(tx - fdx, ty - fdy)) {
          const int xL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
          const int xR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
          const int yTBase = cliffYTopScaled(zNearW, occTopScale);
          const int yB = std::clamp(cliffYBot(zNearW), 0, H - 1);
          if (xR >= 0 && xL < W && xR >= xL && yTBase < yB) {
            markColSpan(xL, xR, zNearW);
          }
        }
        if (inMine) {
          if (s < 0 && !isCliffCell(tx + rdx, ty + rdy)) {
            const int xNear = cliffX(zNearW, static_cast<float>(s) + 0.50f);
            const int xFar  = cliffX(zFarW, static_cast<float>(s) + 0.50f);
            markColSpan(xNear, xFar - 1, zFarW);
          } else if (s > 0 && !isCliffCell(tx + ldx, ty + ldy)) {
            const int xNear = cliffX(zNearW, static_cast<float>(s) - 0.50f);
            const int xFar  = cliffX(zFarW, static_cast<float>(s) - 0.50f);
            markColSpan(xFar + 1, xNear, zFarW);
          }
        }
      }
      if (xlower > xupper) {
        viewSealed = true;
        break;
      }
    }
  }

  // When full-height mine walls seal all 240 columns at maxSealZ, skip 100%-overwritten ceiling/floor rows
  const int ceilEndY = (inMine && viewSealed)
                           ? std::clamp(cliffYTop(maxSealZ) + 1, 0, hy)
                           : hy;
  const int floorStartY = (inMine && viewSealed)
                              ? std::clamp(cliffYBot(maxSealZ), hy, H)
                              : hy;

  if (!inMine) {
    // Authentic PC-98 sky: 4x4 Bayer bands accented with classic horizontal interlace transitions
    for (int y = 0; y < hy; ++y) {
      const float u = static_cast<float>(y) / static_cast<float>(hy);
      std::uint8_t cA = sky0, cB = sky1;
      float bandT = 0.0f;
      if (u < 0.34f) {
        cA = sky0; cB = sky1; bandT = u / 0.34f;
      } else if (u < 0.68f) {
        cA = sky1; cB = sky2; bandT = (u - 0.34f) / 0.34f;
      } else {
        cA = sky2; cB = sky3; bandT = (u - 0.68f) / 0.32f;
      }
      // Subtle PC-98 horizontal line-interlace modulation at mid-band boundaries
      const int baseThr = static_cast<int>(std::clamp(bandT, 0.0f, 1.0f) * 16.0f);
      const int thr = (baseThr >= 5 && baseThr <= 11)
                          ? std::clamp(baseThr + ((y & 1) ? 2 : -2), 0, 16)
                          : baseThr;
      const std::uint8_t rowMask = kFogMasks.row[thr][y & 3];
      for (int x = 0; x < W; ++x) {
        idxBuf[y * W + x] = (((rowMask >> (x & 3)) & 1u) != 0u) ? cB : cA;
      }
    }

    // Starfield (dense 2-tone + twinkling crosses on airless/night worlds, subtle upper-sky stars by day)
    const int starCount = (airlessWorld ? 72 : nightSky ? 46 : duskSky ? 26 : 14) * 3 / 2;
    const int starMaxY  = airlessWorld ? (hy - 6 * kResScale)
                        : nightSky     ? (hy - 14 * kResScale)
                        : duskSky      ? (34 * kResScale)
                                       : (22 * kResScale);
    for (int st = 0; st < starCount; ++st) {
      const unsigned sh = static_cast<unsigned>(st) * 2654435761u ^ pSeed;
      const int sx = 3 + static_cast<int>((sh + static_cast<unsigned>(planetSurface_.dir) * 118u) %
                                          static_cast<unsigned>(W - 6));
      const int sy = 2 + static_cast<int>((sh >> 8) % static_cast<unsigned>(std::max(8, starMaxY)));
      const std::uint8_t sCol =
          ((sh & 3u) == 0u) ? kIdxWarmGleam
                            : ((sh & 1u) == 0u) ? kIdxStarCool : kIdxCloudCool;
      putPx(sx, sy, sCol);
      if ((nightSky || airlessWorld) && (sh % 11u) == 0u && sy > 3 && sy < starMaxY - 2) {
        putPx(sx - 1, sy, sky2);
        putPx(sx + 1, sy, sky2);
        putPx(sx, sy - 1, sky2);
        putPx(sx, sy + 1, sky2);
        putPx(sx, sy, kIdxStarCool);
      }
    }

    // Ice sub-biome 1: sweeping PC-98 Aurora Borealis vertical curtains across night sky
    if (iceWorld && subBiome == 1) {
      for (int x = 6 * kResScale; x < W - 6 * kResScale; ++x) {
        const float af = static_cast<float>(x) / static_cast<float>(kResScale)
                       + static_cast<float>(planetSurface_.dir * 52 + static_cast<int>(pSeed & 31u));
        const float w1 = std::sin(af * 0.042f);
        const float w2 = std::sin(af * 0.095f + 1.4f);
        const float env = 0.55f + 0.45f * std::sin(af * 0.021f + 0.6f);
        const int baseY = (9 + static_cast<int>(std::lround((w1 * 5.0f + w2 * 3.0f) + 7.0f))) * kResScale;
        const int ribbonH = (8 + static_cast<int>(std::lround(env * 14.0f))) * kResScale;
        for (int k = 0; k < ribbonH; ++k) {
          const int py = baseY + k;
          if (py < 2 || py >= hy - 14 * kResScale) { continue; }
          const int distEdge = std::min(k + 1, ribbonH - k);
          const int density = std::min(15, (distEdge * 22) / std::max(1, ribbonH));
          if (kBayer4[py & 3][x & 3] < density && ((x + (py >> 2)) & 1) == 0) {
            putPx(x, py, (k < 4 * kResScale) ? leaf2 : (k < ribbonH * 2 / 3) ? leaf1 : accent1);
          }
        }
      }
    }

    // Celestial landmarks with compass parallax (ringed gas giant, cratered moon, coronal sun, twin moons)
    {
      const int celX = (28 + ((static_cast<int>(pSeed >> 14) + (3 - planetSurface_.dir) * 56) % (240 - 56))) * kResScale;
      const int celY = (16 + static_cast<int>((pSeed >> 18) % 14u)) * kResScale;
      const std::uint8_t celHi  = (lavaWorld || duskSky) ? kIdxWarmGleam : kIdxStarCool;
      const std::uint8_t celMid = (lavaWorld || duskSky) ? sky3 : kIdxCloudCool;
      const std::uint8_t celShd = sky0;

      if (celestialKind == 0) {
        // Ringed gas giant: back ring -> full sphere (lit bands + shaded night hemisphere) -> front ring
        const int r = (11 + static_cast<int>((pSeed >> 20) & 3u)) * kResScale;
        const int ringRx = r * 2 + 5 * kResScale;
        for (int rx = -ringRx; rx <= ringRx; ++rx) {
          const int ryBack = celY - (rx * 3) / 10 - 2 * kResScale;
          if (rx * rx > (r - 1) * (r - 1) && ryBack >= 1 && ryBack < hy - 6 * kResScale) {
            putPx(celX + rx, ryBack, celMid);
            putPx(celX + rx, ryBack - 1, ((rx & 1) == 0) ? sky2 : celMid);
          }
        }
        const int r2 = r * r;
        for (int dy = -r; dy <= r; ++dy) {
          const int py = celY + dy;
          if (py < 1 || py >= hy - 8 * kResScale) { continue; }
          for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r2) { continue; }
            const int px = celX + dx;
            const int lit = (-dx * 10 - dy * 3) + r * 4;  // upper-left sunlight
            const int band = ((dy + r) / (3 * kResScale)) & 1;
            if (lit < 0) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < 4) ? sky1 : celShd);
            } else if (lit < r * 4) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < (lit * 16) / (r * 4)) ? sky2 : celShd);
            } else if (lit < r * 8) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < (band ? 11 : 7)) ? celMid : sky2);
            } else {
              putPx(px, py, (band && (py & 1) == 0) ? celMid : celHi);
            }
          }
        }
        for (int rx = -ringRx; rx <= ringRx; ++rx) {
          const int ryFront = celY - (rx * 3) / 10 + 1 * kResScale;
          if (ryFront >= 1 && ryFront < hy - 6 * kResScale) {
            putPx(celX + rx, ryFront, celHi);
            putPx(celX + rx, ryFront + 1, ((rx & 1) == 0) ? celMid : sky2);
          }
        }
      } else if (celestialKind == 1) {
        // Curved crescent / gibbous moon with offset-circle shadow cutout + crater detail
        const int r = (10 + static_cast<int>((pSeed >> 20) & 3u)) * kResScale;
        const int r2 = r * r;
        const int shOffX = r * 55 / 100;
        const int shOffY = -r / 6;
        const int shR2 = (r * 92 / 100) * (r * 92 / 100);
        for (int dy = -r; dy <= r; ++dy) {
          const int py = celY + dy;
          if (py < 1 || py >= hy - 8 * kResScale) { continue; }
          for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r2) { continue; }
            const int px = celX + dx;
            const int sdx = dx - shOffX;
            const int sdy = dy - shOffY;
            const int dSh = sdx * sdx + sdy * sdy;
            if (dSh < shR2 - r * 3 * kResScale) {
              if (airlessWorld && kBayer4[py & 3][px & 3] < 4) { putPx(px, py, sky1); }
              continue;
            }
            if (dSh < shR2 + r * 2 * kResScale) {
              putPx(px, py, (kBayer4[py & 3][px & 3] < 9) ? celMid : sky1);
              continue;
            }
            const int c1x = dx + 4 * kResScale;
            const int c1y = dy - 2 * kResScale;
            const int c2x = dx + 2 * kResScale;
            const int c2y = dy + 4 * kResScale;
            const bool crater = (c1x * c1x + c1y * c1y <= 4 * kResScale * kResScale) ||
                                (c2x * c2x + c2y * c2y <= 3 * kResScale * kResScale);
            putPx(px, py, crater ? celMid : celHi);
          }
        }
      } else if (celestialKind == 2) {
        // Low sun with horizontal PC-98 interlace corona (plus binary companion on odd seeds)
        const int sunY = std::min(hy - 18 * kResScale, celY + 10 * kResScale);
        const int r = 7 * kResScale;
        fillDitherDisc(celX, sunY, r + 7 * kResScale, r + 5 * kResScale, sky2, sky3, 9);
        fillDitherDisc(celX, sunY, r + 3 * kResScale, r + 2 * kResScale, sky3, celHi, 10);
        fillDisc(celX, sunY, r, r, celHi);
        if ((pSeed & 1u) != 0u) {
          fillDitherDisc(celX + 16 * kResScale, sunY - 7 * kResScale, 5 * kResScale, 5 * kResScale, sky3, kIdxWarmGleam, 8);
          fillDisc(celX + 16 * kResScale, sunY - 7 * kResScale, 3 * kResScale, 3 * kResScale, kIdxWarmGleam);
        }
      } else {
        // Twin moons (primary cratered disc + distant companion moonlet)
        const int r = 8 * kResScale;
        fillDitherDisc(celX, celY, r, r, celMid, celHi, 10);
        fillDisc(celX - 2 * kResScale, celY - 2 * kResScale, r - 3 * kResScale, r - 3 * kResScale, celHi);
        fillDisc(celX + 2 * kResScale, celY + 2 * kResScale, 2 * kResScale, 2 * kResScale, celMid);
        fillDisc(celX + 18 * kResScale, celY - 5 * kResScale, 3 * kResScale, 3 * kResScale, celMid);
        fillDisc(celX + 17 * kResScale, celY - 6 * kResScale, std::max(1, kResScale), std::max(1, kResScale), celHi);
      }
    }

    // Expansive, sweeping PC-98 Clouds of Xeen cumulus cloud banks with flat wispy undersides
    if (!airlessWorld) {
      const std::uint8_t cRim = lavaWorld ? kIdxLavaRim
                              : duskSky   ? kIdxWarmGleam
                              : nightSky  ? sky3
                                          : kIdxStarCool;
      const std::uint8_t cMid = lavaWorld ? sky3
                              : duskSky   ? sky3
                              : nightSky  ? sky2
                                          : kIdxCloudCool;
      const std::uint8_t cBot = nightSky ? sky1 : sky2;
      auto drawCloudBank = [&](int cx, int cy, int cw, int ch, unsigned seed) {
        // Flat horizontal wispy cloud-bank base + asymmetric multi-lobe billowing cumulus tops
        const int effCh = (cloudStyle == 1) ? std::max(3 * kResScale, ch * 65 / 100) : ch;
        const int effCw = (cloudStyle == 1) ? (cw + 14 * kResScale) : cw;
        const int baseY = cy + effCh;
        const int lobes = 7;
        for (int i = 0; i < lobes; ++i) {
          const int lx = cx - effCw * 40 / 100 + (i * effCw * 80 / 100) / (lobes - 1)
                       + (static_cast<int>(((seed + static_cast<unsigned>(i) * 13u) % 5u)) - 2) * kResScale;
          const int distEdge = std::min(i + 1, lobes - i);
          const int rx = std::max(5 * kResScale, effCw / 6 + distEdge * 2 * kResScale);
          const int ry = std::max(2 * kResScale, (effCh * (distEdge + 1)) / 4 +
                                  static_cast<int>((seed >> (i * 2)) & 3u) * kResScale);
          const int ly = baseY - ry * 7 / 10;
          fillDisc(lx, ly + kResScale, rx + kResScale, ry, cBot);
          fillDisc(lx, ly, rx, ry, cMid);
          fillDisc(lx - kResScale, ly - kResScale, std::max(3, rx - 2 * kResScale), std::max(2, ry - kResScale), cRim);
        }
        // Crisp flat cumulus underside trimmed with horizontal PC-98 interlace dither
        for (int py = baseY + 1; py <= baseY + 3 * kResScale; ++py) {
          if (py >= hy - 6 * kResScale) { break; }
          const int shrink = (py - baseY) * 4;
          for (int px = cx - effCw / 2 - 4 * kResScale + shrink; px <= cx + effCw / 2 + 4 * kResScale - shrink; ++px) {
            if (py <= baseY + kResScale) {
              putPx(px, py, (((px + py) & 1) == 0) ? cMid : cBot);
            } else if (((px + py) & 3) == 0) {
              putPx(px, py, cBot);
            }
          }
        }
        // Trailing wispy PC-98 cloud tails extending left/right along the underside
        drawHLine(cx - effCw / 2 - 16 * kResScale, cx - effCw / 3, baseY, cRim);
        drawHLine(cx - effCw / 2 - 11 * kResScale, cx - effCw / 4, baseY + 1, cMid);
        drawHLine(cx + effCw / 3, cx + effCw / 2 + 14 * kResScale, baseY, cRim);
        drawHLine(cx + effCw / 4, cx + effCw / 2 + 9 * kResScale, baseY + 1, cMid);
      };
      const int cOff = (3 - planetSurface_.dir) * 52;
      drawCloudBank((38 + (cOff + static_cast<int>(pSeed & 31u)) % 120) * kResScale,
                    12 * kResScale, 68 * kResScale, 7 * kResScale, pSeed);
      drawCloudBank((182 - ((planetSurface_.dir * 37 + static_cast<int>((pSeed >> 4) & 31u)) % 96)) * kResScale,
                    21 * kResScale, 56 * kResScale, 6 * kResScale, pSeed >> 3);
      drawCloudBank((96 + ((planetSurface_.dir * 43 + static_cast<int>((pSeed >> 7) & 31u)) % 88)) * kResScale,
                    33 * kResScale, 44 * kResScale, 5 * kResScale, pSeed >> 7);
    }

    // Two-layer parallax horizon silhouettes varied by biome & planet horizonStyle
    // Uses incommensurate integer triangle waves scaled smoothly to W (no stair-step aliasing)
    auto triWave = [](int v, int period) -> int {
      const int m = ((v % period) + period) % period;
      const int half = period / 2;
      return half - std::abs(m - half);
    };
    // Layer 1: Far atmospheric range blended softly toward sky3
    activeFog = 24;
    activeInk = kInkFogBase + 4u;
    int prevFarH = 6 * kResScale;
    for (int x = 0; x < W; ++x) {
      const int ph = x + (planetSurface_.dir * 64 + static_cast<int>((pSeed >> 6) & 127u)) * kResScale;
      int farH = 6 * kResScale;
      if (horizonStyle == 1 || (rockWorld && horizonStyle != 2)) {
        // Irregular flat-topped mesas & broad canyon gaps
        const int segW = 46 * kResScale;
        const int seg = ph / segW;
        const int r = ph % segW;
        const int mesaW = (18 + ((seg * 7 + static_cast<int>(pSeed & 7u)) % 14)) * kResScale;
        const int mesaH = (10 + ((seg * 5) % 7)) * kResScale;
        if (r >= 6 * kResScale && r <= 6 * kResScale + mesaW) {
          farH = (r < 9 * kResScale || r > 3 * kResScale + mesaW) ? (mesaH - 3 * kResScale) : mesaH;
        } else {
          farH = 4 * kResScale + (triWave(ph, 37 * kResScale) * 3) / 18;
        }
      } else if (horizonStyle == 2 || lavaWorld || iceWorld) {
        // Sweeping asymmetric alpine peaks & volcanic calderas
        const int w1 = triWave(ph, 86 * kResScale);
        const int w2 = triWave(ph + 29 * kResScale, 54 * kResScale);
        const int w3 = triWave(ph * 2 + 13 * kResScale, 38 * kResScale);
        farH = 4 * kResScale + (w1 * 12) / 43 + (w2 * 6) / 27 + (w3 * 3) / 19;
      } else {
        // Broad rolling highlands / coastal ridges
        const int w1 = triWave(ph, 98 * kResScale);
        const int w2 = triWave(ph + 41 * kResScale, 62 * kResScale);
        const int w3 = triWave(ph + 19 * kResScale, 34 * kResScale);
        farH = 4 * kResScale + (w1 * 9) / 49 + (w2 * 5) / 31 + (w3 * 2) / 17;
      }
      if (oceanWorld) { farH = std::max(2 * kResScale, farH - 5 * kResScale); }
      const int yTop = std::max(8 * kResScale, hy - farH);
      const bool litFar = (farH >= prevFarH);
      prevFarH = farH;
      putPx(x, yTop - 1, sky2);
      for (int y = yTop; y < hy; ++y) {
        putPx(x, y, (y == yTop && litFar) ? rockHi : rockMid);
      }
    }

    // Distant Might & Magic-style overworld horizon landmarks (compass-anchored per planet)
    {
      const int lmX = (36 + ((static_cast<int>(pSeed >> 9) + (3 - planetSurface_.dir) * 62) % (240 - 72))) * kResScale;
      const int sc = kResScale;
      activeFog = 22;
      activeInk = kInkFogBase + 4u;
      if (lavaWorld) {
        // Smoking caldera cone + distant obsidian spire
        for (int k = 0; k < 16 * sc; ++k) {
          const int py = hy - 14 * sc - k;
          const int drift = (k * k) / (14 * sc);
          const int rw = 2 * sc + k / 3;
          fillDitherDisc(lmX + drift, py, rw, 2 * sc, sky1, rockDark, 8);
        }
        const int sx = (lmX > W / 2) ? (lmX - 42 * sc) : (lmX + 42 * sc);
        fillRect(sx - 4 * sc, hy - 16 * sc, 9 * sc, 16 * sc, rockDark);
        fillRect(sx - 2 * sc, hy - 23 * sc, 5 * sc, 8 * sc, rockDark);
        fillRect(sx - 1 * sc, hy - 28 * sc, 3 * sc, 6 * sc, rockMid);
        fillRect(sx, hy - 29 * sc, sc, sc, accent0);
        fillRect(sx, hy - 19 * sc, sc, sc, kIdxWarmGleam);
      } else if (biomeIdx == 0) {
        // Distant ruined castle keep & watchtower perched on a hilltop
        fillRect(lmX - 10 * sc, hy - 14 * sc, 21 * sc, 14 * sc, rockDark);
        fillRect(lmX - 9 * sc, hy - 13 * sc, 8 * sc, 12 * sc, rockMid);
        // Left keep tower + crisp crenels
        fillRect(lmX - 10 * sc, hy - 22 * sc, 7 * sc, 9 * sc, rockDark);
        fillRect(lmX - 9 * sc, hy - 21 * sc, 3 * sc, 7 * sc, rockMid);
        fillRect(lmX - 10 * sc, hy - 23 * sc, sc, sc, rockHi);
        fillRect(lmX - 7 * sc, hy - 23 * sc, sc, sc, rockHi);
        fillRect(lmX - 4 * sc, hy - 23 * sc, sc, sc, rockHi);
        // Right broken watchtower + glowing windows
        fillRect(lmX + 4 * sc, hy - 19 * sc, 6 * sc, 6 * sc, rockDark);
        fillRect(lmX + 5 * sc, hy - 20 * sc, sc, sc, rockHi);
        fillRect(lmX + 8 * sc, hy - 20 * sc, sc, sc, rockHi);
        fillRect(lmX - 7 * sc, hy - 18 * sc, sc, sc, kIdxWarmGleam);
        fillRect(lmX + 6 * sc, hy - 16 * sc, sc, sc, kIdxWarmGleam);
      } else if (iceWorld) {
        // Towering glacial crystal citadel spire on the distant horizon
        for (int row = 0; row < 22 * sc; ++row) {
          const int hw = std::max(1, (22 * sc - row) / 3);
          drawHLine(lmX - hw, lmX + hw, hy - 4 * sc - row, (row > 14 * sc) ? rockHi : rockMid);
          drawHLine(lmX, lmX + hw, hy - 4 * sc - row, rockDark);
        }
        fillRect(lmX, hy - 27 * sc, sc, sc, accent1);
        fillRect(lmX - 11 * sc, hy - 15 * sc, 4 * sc, 15 * sc, rockMid);
        fillRect(lmX + 8 * sc, hy - 13 * sc, 4 * sc, 13 * sc, rockDark);
        fillRect(lmX - 10 * sc, hy - 16 * sc, sc, sc, accent1);
      } else if (oceanWorld) {
        // Distant sunken coastal aqueduct arches & sea-beacon lighthouse
        for (int a = 0; a < 3; ++a) {
          const int ax = lmX - 14 * sc + a * 9 * sc;
          fillRect(ax, hy - 11 * sc, 9 * sc, 3 * sc, rockMid);
          fillRect(ax, hy - 8 * sc, 2 * sc, 8 * sc, rockDark);
          fillRect(ax + 7 * sc, hy - 8 * sc, 2 * sc, 8 * sc, rockDark);
        }
        fillRect(lmX + 15 * sc, hy - 18 * sc, 4 * sc, 18 * sc, rockMid);
        fillRect(lmX + 17 * sc, hy - 18 * sc, 2 * sc, 18 * sc, rockDark);
        fillRect(lmX + 16 * sc, hy - 19 * sc, 2 * sc, sc, kIdxWarmGleam);
      } else {
        // Rock / Badlands: colossal stepped desert ziggurat / alien obelisk silhouette
        if ((pSeed & 1u) == 0u) {
          fillRect(lmX - 15 * sc, hy - 7 * sc, 31 * sc, 7 * sc, rockDark);
          fillRect(lmX - 11 * sc, hy - 12 * sc, 23 * sc, 5 * sc, rockMid);
          fillRect(lmX - 7 * sc, hy - 17 * sc, 15 * sc, 5 * sc, rockDark);
          fillRect(lmX - 3 * sc, hy - 21 * sc, 7 * sc, 4 * sc, rockHi);
          fillRect(lmX, hy - 22 * sc, sc, sc, accent1);
        } else {
          fillRect(lmX - 3 * sc, hy - 24 * sc, 7 * sc, 24 * sc, rockDark);
          fillRect(lmX - 3 * sc, hy - 24 * sc, 3 * sc, 24 * sc, rockMid);
          fillRect(lmX - 2 * sc, hy - 25 * sc, 5 * sc, sc, rockHi);
          fillRect(lmX - 1 * sc, hy - 26 * sc, 3 * sc, sc, accent1);
          fillRect(lmX, hy - 27 * sc, sc, sc, accent1);
        }
      }
    }

    // Layer 2: Nearer detailed horizon ridge with sunlit/shadowed facets & 1px atmospheric crest
    activeFog = 18;
    activeInk = kInkFogBase + 4u;
    int prevNearH = 4 * kResScale;
    for (int x = 0; x < W; ++x) {
      const int ph = x + (planetSurface_.dir * 84 + static_cast<int>((pSeed >> 11) & 63u)) * kResScale;
      int nearH = 4 * kResScale;
      if (rockWorld && horizonStyle == 1) {
        const int blkW = 38 * kResScale;
        const int blk = ph / blkW;
        const int rem = ph % blkW;
        nearH = ((blk % 3) == 0 && rem > 5 * kResScale && rem < 28 * kResScale)
                    ? (9 * kResScale)
                    : (3 * kResScale + (triWave(ph, 29 * kResScale) * 3) / 14);
      } else if (oceanWorld) {
        const int w1 = triWave(ph, 72 * kResScale);
        const int w2 = triWave(ph + 19 * kResScale, 36 * kResScale);
        nearH = (w1 > 24 * kResScale) ? (3 * kResScale + (w1 - 24 * kResScale) / 2 + w2 / 9) : (2 * kResScale);
      } else {
        const int p1 = triWave(ph, 68 * kResScale);
        const int p2 = triWave(ph + 23 * kResScale, 42 * kResScale);
        const int p3 = triWave(ph + 7 * kResScale, 22 * kResScale);
        nearH = 3 * kResScale + (p1 * 6) / 34 + (p2 * 4) / 21 + (p3 * 2) / 11;
      }
      const int yTop = std::max(14 * kResScale, hy - nearH);
      putPx(x, yTop - 1, inkOut);
      const bool litSlope = (nearH >= prevNearH);
      prevNearH = nearH;
      for (int k = 0; k < nearH; ++k) {
        std::uint8_t col = rockDark;
        if (k == 0 && litSlope) {
          col = (iceWorld || (biomeIdx == 0 && subBiome == 1)) ? leaf2 : rockHi;
        } else if (k <= 2 * kResScale && litSlope && ((x + k) & 1) == 0) {
          col = rockMid;
        }
        putPx(x, yTop + k, col);
      }
    }
    activeFog = 0;
    activeInk = inkOut;
  } else {
    // Underground foreshortened World of Xeen timber-raftered stone ceiling (0 B extra LUTs)
    const std::uint8_t ceilDarkMid = static_cast<std::uint8_t>(rockDark | kMidFogBit);
    const std::uint8_t ceilLitMid = static_cast<std::uint8_t>(rockMid | kMidFogBit);
    const std::uint8_t dripCol = iceWorld ? accent1
                               : lavaWorld ? accent0
                               : (biomeIdx == 0) ? leaf0
                                                 : rockMid;
    for (int y = 0; y < ceilEndY; ++y) {
      const int cy = (hy - 1) - y;
      const int baseFog = static_cast<int>(kScanLut.fogStep[cy]);
      const int f32 = (baseFog > 0) ? std::min(31, baseFog + 5) : 0;
      activeFog = f32;
      activeInk = inkOut;
      const std::uint8_t fogMaskRow =
          (f32 > 0) ? kFogMasks.row[(f32 <= 16) ? f32 : std::min(16, f32 - 16)][y & 3] : 0u;
      const float u = static_cast<float>(cy) / static_cast<float>(hy);
      const float zCeil = kScanLut.z[cy];
      const float zCellFrac = zCeil - std::floor(zCeil + 0.5f) + 0.5f;  // 0..1 within cell depth
      const bool isCrossBeam = (zCeil < 4.5f) && (zCellFrac < 0.16f);
      const bool isCrossEdge = (zCeil < 4.5f) && (zCellFrac < 0.04f || (zCellFrac >= 0.13f && zCellFrac < 0.17f));
      const bool isCrossHi   = (zCeil < 4.5f) && (zCellFrac >= 0.04f && zCellFrac < 0.08f);
      const float invScaleX = kScanLut.invScaleX[cy];
      const int gradThr = static_cast<int>(std::clamp((u - 0.12f) * 0.82f, 0.0f, 1.0f) * 16.0f);
      for (int x = 0; x < W; ++x) {
        std::uint8_t cIdx = rockDark;
        if (isCrossEdge) {
          cIdx = inkOut;
        } else if (isCrossBeam) {
          // Perspective overhead wooden dungeon ceiling rafter beam
          cIdx = isCrossHi ? pathHi : ((((x + y) & 3) == 0) ? pathDark : trunkCol);
        } else {
          // Check longitudinal side-beam timber runners along corridor edges (lat ~ +-0.46)
          const float absLat = std::abs((static_cast<float>(x) + 0.5f - static_cast<float>(cxMid)) * invScaleX);
          const float latMod = absLat - std::floor(absLat);
          if (zCeil < 4.2f && (latMod > 0.41f && latMod < 0.53f)) {
            cIdx = (latMod < 0.43f || latMod > 0.51f) ? inkOut
                 : (latMod < 0.46f)                   ? pathHi
                                                      : trunkCol;
          } else {
            const bool pickB = gradThr > kBayer4[(y + 2) & 3][(x + 1) & 3];
            cIdx = pickB ? rockMid : rockDark;
          }
        }
        if (f32 > 0) {
          const std::uint8_t cMid = (cIdx == rockDark) ? ceilDarkMid
                                  : (cIdx == rockMid)  ? ceilLitMid
                                                       : static_cast<std::uint8_t>(cIdx | kMidFogBit);
          const bool mBit = ((fogMaskRow >> (x & 3)) & 1u) != 0u;
          cIdx = (cIdx == inkOut) ? inkOut
               : (f32 <= 16)      ? (mBit ? cMid : cIdx)
                                  : (mBit ? inkOut : cMid);
        }
        idxBuf[y * W + x] = cIdx;
      }
    }
    // Subtle corner stalactites / damp mineral drips tucked between rafters
    activeFog = 0;
    activeInk = inkOut;
    if (ceilEndY > 14 * kResScale) {
      for (int st = 0; st < 10; ++st) {
        const unsigned sh = static_cast<unsigned>(st) * 374761393u ^
                            (static_cast<unsigned>(planetSurface_.px) * 668265263u) ^
                            (static_cast<unsigned>(planetSurface_.py) * 2147483647u) ^
                            (static_cast<unsigned>(planetSurface_.dir) * 1274126177u);
        const int sx = 14 * kResScale + static_cast<int>(sh % static_cast<unsigned>(W - 28 * kResScale));
        const int len = (4 + static_cast<int>((sh >> 8) % 6u)) * kResScale;
        const int halfW = (1 + static_cast<int>((sh >> 12) % 2u)) * kResScale;
        if (len >= ceilEndY - 4 * kResScale) { continue; }
        for (int dy = 0; dy < len; ++dy) {
          const int hw = std::max(0, halfW - (dy * (halfW + 1)) / len);
          putPx(sx - hw - 1, dy, inkOut);
          putPx(sx + hw + 1, dy, inkOut);
          for (int px = sx - hw; px <= sx + hw; ++px) {
            putPx(px, dy, (px == sx - hw) ? dripCol : rockDark);
          }
        }
        putPx(sx, len, inkOut);
      }
    }
  }

  // World-anchored PC-98 4x4 Bayer dithered ground + biome-specific perspective surface textures
  // (pure 16.16 fixed-point horizontal stepping — zero inner-loop float math or division)
  const int gFieldOffX = static_cast<int>(pSeed & 127u);
  const int gFieldOffY = static_cast<int>((pSeed >> 7) & 127u);
  const std::uint8_t floorA = inMine ? rockDark : gnd0;
  const std::uint8_t floorB = inMine ? rockMid  : gnd1;
  const std::uint8_t floorC = inMine ? rockMid  : gnd2;
  const std::uint8_t floorD = inMine ? rockMid  : leaf0;
  const float gndBaseWx = static_cast<float>(planetSurface_.px) + 0.5f;
  const float gndBaseWy = static_cast<float>(planetSurface_.py) + 0.5f;
  for (int y = floorStartY; y < H; ++y) {
    const int gy = y - hy;
    const int baseFog = static_cast<int>(kScanLut.fogStep[gy]);
    const int f32 = (inMine && baseFog > 0) ? std::min(31, baseFog + 4) : baseFog;
    activeFog = f32;
    activeInk = inkForFog(f32);
    const std::uint8_t fogMaskRow =
        (f32 > 0) ? kFogMasks.row[(f32 <= 16) ? f32 : std::min(16, f32 - 16)][y & 3] : 0u;
    const float u = static_cast<float>(gy) / static_cast<float>(kGndH);
    const int gradThr = static_cast<int>(
        std::clamp((u < 0.45f) ? (u / 0.45f) : ((u - 0.45f) / 0.55f), 0.0f, 1.0f) * 16.0f);
    const float z = kScanLut.z[gy];
    const bool detailRow = (z < 3.5f);
    std::int32_t wx_fp = 0, wy_fp = 0, dwx_fp = 0, dwy_fp = 0;
    if (detailRow) {
      constexpr float kGndTexScale = 16.0f * static_cast<float>(kResScale);
      const float invScaleX = kScanLut.invScaleX[gy];
      const float lat0 = (0.5f - static_cast<float>(cxMid)) * invScaleX;
      const float wx0 = (gndBaseWx + static_cast<float>(fdx) * z + static_cast<float>(rdx) * lat0) * kGndTexScale;
      const float wy0 = (gndBaseWy + static_cast<float>(fdy) * z + static_cast<float>(rdy) * lat0) * kGndTexScale;
      wx_fp  = static_cast<std::int32_t>(std::lround(wx0 * 65536.0f));
      wy_fp  = static_cast<std::int32_t>(std::lround(wy0 * 65536.0f));
      dwx_fp = static_cast<std::int32_t>(std::lround(static_cast<float>(rdx) * invScaleX * kGndTexScale * 65536.0f));
      dwy_fp = static_cast<std::int32_t>(std::lround(static_cast<float>(rdy) * invScaleX * kGndTexScale * 65536.0f));
    }

    for (int x = 0; x < W; ++x, wx_fp += dwx_fp, wy_fp += dwy_fp) {
      // Ground clumps come from the noise field; the 4x4 Bayer order only
      // feathers the switch between levels. The threshold has to be scaled to
      // the field's own range, or whole Bayer rows flip at once and the
      // terrain picks up horizontal banding instead of stipple.
      const int fgx = ((wx_fp >> 17) + gFieldOffX) & 127;
      const int fgy = ((wy_fp >> 17) + gFieldOffY) & 127;
      // Hard quantise, no dither: the clumps are the dither. A per-pixel
      // Bayer here lays a fine grid over the whole meadow.
      static constexpr std::uint8_t kBandEdges[4] = {88u, 130u, 172u, 255u};
      const int gn = static_cast<int>(kGroundField.v[fgy][fgx]);
      const int gBand = (gn < kBandEdges[0]) ? 0 : (gn < kBandEdges[1]) ? 1
                      : (gn < kBandEdges[2]) ? 2 : 3;
      std::uint8_t gIdx = (gBand == 0) ? floorA : (gBand == 1) ? floorB
                        : (gBand == 2) ? floorC : floorD;

      if (detailRow) {
        const int gx = wx_fp >> 16;
        const int gcy = wy_fp >> 16;
        const int fx = (wx_fp >> 12) & 15;
        const int fy = (wy_fp >> 12) & 15;
        const unsigned cellH = static_cast<unsigned>(gx) * 73856093u ^
                               static_cast<unsigned>(gcy) * 19349663u;
        if (inMine) {
          // Mine floor beside the track: broken dark rock and grit, caught by
          // the lamp only where the stone stands proud. Not the road surface.
          const std::uint8_t st = kStoneField.v[(gcy + 37) & 127][(gx + 91) & 127];
          const bool mineStone = st >= 2u && (cellH & 15u) < 3u;
          gIdx = (st >= 2u && st <= 2u) ? pathDark
                 : mineStone ? rockMid
                             : pathDark;
        } else if (lavaWorld) {
          // Cracked basalt crust with glowing magma seams near camera
          const int seam = (gx * 3 + gcy * 5) & 7;
          if (seam == 0 && (fx == 6 || fx == 7) && (cellH & 3u) != 0u) {
            gIdx = (z < 1.85f && (cellH & 1u) == 0u) ? accent1 : accent0;
          } else if ((cellH % 19u) == 0u && fx >= 5 && fx <= 9 && fy == 6) {
            gIdx = inkOut;
          }
        } else if (oceanWorld) {
          // Horizontal tidal shimmer ripples + dark sea-foam streaks
          if (((gcy + (gx >> 2)) & 3) == 0 && fy == 5 && (fx >= 2 && fx <= 12)) {
            gIdx = ((cellH & 1u) == 0u) ? sky3 : gnd2;
          } else if ((cellH % 29u) == 0u && fy == 9 && fx >= 5 && fx <= 10) {
            gIdx = rockDark;
          }
        } else if (iceWorld) {
          // Wind-carved snowdrift bands + sparkling ice glints
          if (((gx + gcy) & 3) == 0 && fy <= 2) {
            gIdx = rockHi;
          } else if ((cellH % 31u) == 0u && fx == 6 && fy == 6) {
            gIdx = accent1;
          } else if ((cellH % 23u) == 0u && fy == 11 && fx >= 4 && fx <= 10) {
            gIdx = gnd0;
          }
        } else if (rockWorld) {
          // Clouds of Xeen stippled desert sand & scattered ochre pebbles
          if (((cellH + static_cast<unsigned>(fx * 3 + fy * 5)) % 7u) == 0u) {
            gIdx = ((cellH & 1u) == 0u) ? gnd2 : gnd0;
          } else if ((cellH % 17u) == 0u && fx >= 5 && fx <= 9 && fy >= 6 && fy <= 8) {
            gIdx = (fy == 6) ? rockHi : rockDark;
          }
        } else {
          // Clouds of Xeen dappled meadow: a tuft is a small round clump with
          // a lit top, so the meadow dots instead of striping.
          const int tdx = fx - 7, tdy = fy - 7;
          if ((cellH % 13u) == 0u && (tdx * tdx + tdy * tdy) < 5) {
            gIdx = (tdy <= -1) ? leaf2 : (tdy == 0 ? leaf1 : leaf0);
          } else if ((cellH % 29u) == 0u && fx == 7 && fy == 7) {
            gIdx = (subBiome == 1 || subBiome == 3) ? leaf2 : kIdxWarmGleam;
          }
        }
      }

      if (f32 > 0) {
        const std::uint8_t gMid = static_cast<std::uint8_t>(gIdx | kMidFogBit);
        const bool mBit = ((fogMaskRow >> (x & 3)) & 1u) != 0u;
        gIdx = (f32 <= 16) ? (mBit ? gMid : gIdx) : (mBit ? fogTargetIdx : gMid);
      }
      idxBuf[y * W + x] = gIdx;
    }
  }
  activeFog = 0;
  activeInk = inkOut;

  // Integer 16.16 fixed-point DDA trapezoid road rasterizer (rec98 make_linework / draw_trapezoid style).
  // Replaces 8,000 B roadBuf + 8,000 per-pixel float isRoadAtSample evaluations with exact projected
  // world-space road spans for visible path cells only.
  struct RoadSpan {
    float z0;
    float z1;
    float lat0;
    float lat1;
    std::uint8_t bevelMask;  // bit0=BL, bit1=BR, bit2=FL, bit3=FR outer dead-end/convex corner bevel
  };
  RoadSpan roadSpans[64]{};
  int roadSpanCount = 0;
  auto pushRoadSpan = [&](float z0, float z1, float lat0, float lat1, std::uint8_t bevel) {
    if (z1 <= 0.42f || z0 >= 6.60f || roadSpanCount >= 64) { return; }
    roadSpans[roadSpanCount++] = RoadSpan{
        std::max(0.42f, z0), std::min(6.60f, z1), lat0, lat1, bevel};
  };

  for (int d = 0; d <= maxVisD; ++d) {
    const int maxSRoad = d + 2;
    for (int s = -maxSRoad; s <= maxSRoad; ++s) {
      if (!isVis(d, s) &&
          (s == -maxSRoad || !isVis(d, s - 1)) &&
          (s == maxSRoad || !isVis(d, s + 1)) &&
          (d == 0 || std::abs(s) > d + 1 || !isVis(d - 1, s)) &&
          (d == maxVisD || !isVis(d + 1, s))) {
        continue;
      }
      const int tx = planetSurface_.px + fdx * d + rdx * s;
      const int ty = planetSurface_.py + fdy * d + rdy * s;
      if (!isPathTile(tx, ty)) { continue; }
      const bool nF = isPathTile(tx + fdx, ty + fdy);
      const bool nB = isPathTile(tx - fdx, ty - fdy);
      const bool nL = isPathTile(tx + ldx, ty + ldy);
      const bool nR = isPathTile(tx + rdx, ty + rdy);
      const float dc = static_cast<float>(d);
      const float sc = static_cast<float>(s);

      // Lateral pad + optional left/right arms across [d - kHalfRoad, d + kHalfRoad]
      std::uint8_t bevH = 0u;
      if (!nB && !nL) { bevH |= 1u; }
      if (!nB && !nR) { bevH |= 2u; }
      if (!nF && !nL) { bevH |= 4u; }
      if (!nF && !nR) { bevH |= 8u; }
      pushRoadSpan(dc - kHalfRoad, dc + kHalfRoad,
                   nL ? (sc - 0.50f) : (sc - kHalfRoad),
                   nR ? (sc + 0.50f) : (sc + kHalfRoad),
                   bevH);

      // Forward/backward connector arms across [s - kHalfRoad, s + kHalfRoad]
      if (nB || nF) {
        std::uint8_t bevV = 0u;
        if (!nB && !nL) { bevV |= 1u; }
        if (!nB && !nR) { bevV |= 2u; }
        if (!nF && !nL) { bevV |= 4u; }
        if (!nF && !nR) { bevV |= 8u; }
        pushRoadSpan(nB ? (dc - 0.50f) : (dc - kHalfRoad),
                     nF ? (dc + 0.50f) : (dc + kHalfRoad),
                     sc - kHalfRoad, sc + kHalfRoad,
                     bevV);
      }
    }
  }

  constexpr std::uint8_t kRoadSentinel = 254u;
  constexpr float kSlopeScale = (kProjX / kProjY) * 65536.0f;
  std::int16_t roadRowMinX[kGndH];
  std::int16_t roadRowMaxX[kGndH];
  for (int gy = 0; gy < kGndH; ++gy) {
    roadRowMinX[gy] = static_cast<std::int16_t>(W);
    roadRowMaxX[gy] = -1;
  }

  // Pass 1 (curb) & Pass 2 (sentinel interior): pure 16.16 fixed-point scanline edge walking
  for (int pass = 0; pass < 2; ++pass) {
    const int pad = (pass == 0) ? 1 : 0;
    for (int i = 0; i < roadSpanCount; ++i) {
      const RoadSpan& sp = roadSpans[i];
      const int gyTopRaw = static_cast<int>(std::ceil(kProjY / sp.z1 - 0.5f));
      const int gyBotRaw = static_cast<int>(std::floor(kProjY / sp.z0 - 0.5f));
      if (gyTopRaw > gyBotRaw) { continue; }
      const int gy0 = std::max(0, gyTopRaw - pad);
      const int gy1 = std::min(kGndH - 1, gyBotRaw + pad);
      if (gy0 > gy1) { continue; }

      const std::int32_t dXL_fp = static_cast<std::int32_t>(std::lround(sp.lat0 * kSlopeScale));
      const std::int32_t dXR_fp = static_cast<std::int32_t>(std::lround(sp.lat1 * kSlopeScale));
      std::int32_t xL_fp = (cxMid << 16) +
          static_cast<std::int32_t>(std::lround(sp.lat0 * kSlopeScale * (static_cast<float>(gy0) + 0.5f)));
      std::int32_t xR_fp = (cxMid << 16) +
          static_cast<std::int32_t>(std::lround(sp.lat1 * kSlopeScale * (static_cast<float>(gy0) + 0.5f)));

      for (int gy = gy0; gy <= gy1; ++gy) {
        int x0 = ((xL_fp + 32768) >> 16) - pad;
        int x1 = ((xR_fp + 32768) >> 16) - 1 + pad;
        xL_fp += dXL_fp;
        xR_fp += dXR_fp;

        // Subtle 1px outer convex corner bevel on road dead-ends / L-turns
        if (x1 - x0 >= 4 && (gy1 - gy0) >= 2) {
          if (gy == gy0) {
            if ((sp.bevelMask & 4u) != 0u) { ++x0; }
            if ((sp.bevelMask & 8u) != 0u) { --x1; }
          } else if (gy == gy1) {
            if ((sp.bevelMask & 1u) != 0u) { ++x0; }
            if ((sp.bevelMask & 2u) != 0u) { --x1; }
          }
        }

        const int cx0 = std::max(0, x0);
        const int cx1 = std::min(W - 1, x1);
        if (cx0 > cx1) { continue; }
        const int y = hy + gy;
        if (pass == 0) {
          const std::uint8_t curbInk = inkForFog(static_cast<int>(kScanLut.fogStep[gy]));
          for (int x = cx0; x <= cx1; ++x) {
            idxBuf[y * W + x] = curbInk;
          }
        } else {
          if (cx0 < roadRowMinX[gy]) { roadRowMinX[gy] = static_cast<std::int16_t>(cx0); }
          if (cx1 > roadRowMaxX[gy]) { roadRowMaxX[gy] = static_cast<std::int16_t>(cx1); }
          for (int x = cx0; x <= cx1; ++x) {
            idxBuf[y * W + x] = kRoadSentinel;
          }
        }
      }
    }
  }

  // Pass 3: Shade road bevel rims + world-anchored pebbles/dither in 16.16 fixed-point along active spans
  auto isCurbInk = [&](std::uint8_t v) -> bool {
    return v == inkOut || (v >= kInkFogBase && v <= kInkFogBase + 4u);
  };
  const int stepParity = (planetSurface_.px + planetSurface_.py + planetSurface_.dir) & 1;
  const float baseWx = static_cast<float>(planetSurface_.px) + 0.5f;
  const float baseWy = static_cast<float>(planetSurface_.py) + 0.5f;
  for (int gy = 0; gy < kGndH; ++gy) {
    const int xStart = roadRowMinX[gy];
    const int xEnd = roadRowMaxX[gy];
    if (xStart > xEnd) { continue; }
    const int y = hy + gy;
    const float z = kScanLut.z[gy];
    const float invScaleX = kScanLut.invScaleX[gy];
    activeFog = static_cast<int>(kScanLut.fogStep[gy]);
    activeInk = inkForFog(activeFog);
    const bool allowPebble = (z < 3.4f);

    const float lat0 = (static_cast<float>(xStart) + 0.5f - static_cast<float>(cxMid)) * invScaleX;
    // 128 texels/tile at 480x288 (4x4 cobble repeats per cell -> crisp 1:1 PC-98 Clouds of Xeen stones!)
    constexpr float kRoadTexScale = 64.0f * static_cast<float>(kResScale);
    constexpr int kRailShift = (kResScale == 2) ? 2 : 1;
    constexpr int kCellShift = (kResScale == 2) ? 7 : 6;
    const float wx0 = (baseWx + static_cast<float>(fdx) * z + static_cast<float>(rdx) * lat0) * kRoadTexScale;
    const float wy0 = (baseWy + static_cast<float>(fdy) * z + static_cast<float>(rdy) * lat0) * kRoadTexScale;
    std::int32_t sx32_fp = static_cast<std::int32_t>(std::lround(wx0 * 65536.0f));
    std::int32_t sy32_fp = static_cast<std::int32_t>(std::lround(wy0 * 65536.0f));
    const std::int32_t dsx32_fp =
        static_cast<std::int32_t>(std::lround(static_cast<float>(rdx) * invScaleX * kRoadTexScale * 65536.0f));
    const std::int32_t dsy32_fp =
        static_cast<std::int32_t>(std::lround(static_cast<float>(rdy) * invScaleX * kRoadTexScale * 65536.0f));

    for (int x = xStart; x <= xEnd; ++x, sx32_fp += dsx32_fp, sy32_fp += dsy32_fp) {
      if (idxBuf[y * W + x] != kRoadSentinel) { continue; }
      const bool upRim = (gy > 0) && isCurbInk(idxBuf[(y - 1) * W + x]);
      const bool lfRim = (x > 0) && isCurbInk(idxBuf[y * W + (x - 1)]);
      const bool rtRim = (x + 1 < W) && isCurbInk(idxBuf[y * W + (x + 1)]);
      if (upRim || lfRim) {
        putPx(x, y, (lavaWorld && !inMine) ? accent0 : pathHi);
      } else if (rtRim) {
        putPx(x, y, pathDark);
      } else {
        const int gx = sx32_fp >> 16;
        const int gcy = sy32_fp >> 16;
        // Rail coords (0..31 per world tile) and the stone field, sampled 1:1
        // against the world so the road never shows a repeating tile.
        const int tx32 = (gx >> kRailShift) & 31;
        const int ty32 = (gcy >> kRailShift) & 31;
        const unsigned h = static_cast<unsigned>(gx) * 73856093u ^
                           static_cast<unsigned>(gcy) * 19349663u;
        const std::uint8_t st = kStoneField.v[(gcy + 64) & 127][(gx + 64) & 127];
        if (inMine) {
          // Minecart track: rails and split-timber sleepers on dark ballast.
          // The mine floor is its own surface, not the road in the dark.
          const int cellX = gx >> kCellShift;
          const int cellY = gcy >> kCellShift;
          const bool connNS = isPathTile(cellX, cellY - 1) || isPathTile(cellX, cellY + 1);
          const bool connEW = isPathTile(cellX - 1, cellY) || isPathTile(cellX + 1, cellY);
          const bool doNS = connNS || !connEW;
          const bool doEW = connEW;
          const bool railNS = doNS && (tx32 == 10 || tx32 == 11 || tx32 == 20 || tx32 == 21);
          const bool railEW = doEW && (ty32 == 10 || ty32 == 11 || ty32 == 20 || ty32 == 21);
          const bool tieNS  = doNS && (tx32 >= 8 && tx32 <= 23) && ((ty32 & 7) == 1 || (ty32 & 7) == 2);
          const bool tieEW  = doEW && (ty32 >= 8 && ty32 <= 23) && ((tx32 & 7) == 1 || (tx32 & 7) == 2);
          if (railNS || railEW) {
            const bool railLit = (railNS && (tx32 == 10 || tx32 == 20)) ||
                                 (railEW && (ty32 == 10 || ty32 == 20));
            putPx(x, y, railLit ? kIdxShipLight : kIdxShipDark);
          } else if (tieNS || tieEW) {
            // Split timber sleepers, warm against the cold rock.
            const bool tieLit = (tieNS && (ty32 & 7) == 1) || (tieEW && (tx32 & 7) == 1);
            putPx(x, y, tieLit ? kIdxOreBody : pathDark);
          } else {
            // Ballast and broken rock: flat dark bed with a scatter of loose
            // stones for texture. A full stone field here reads as paving,
            // which is exactly what this floor must not be.
            const bool looseStone = st >= 2u && (h & 15u) < 3u;
            putPx(x, y, (st == 2u) ? pathDark
                                   : looseStone ? rockMid
                                                : pathDark);
          }
        } else if (lavaWorld) {
          // Volcanic flagstones: black basalt with ember light in the seams.
          if (st == 0u) {
            putPx(x, y, (z < 2.2f && (h & 7u) == 0u) ? accent0 : inkOut);
          } else if (st == 1u) {
            putPx(x, y, rockDark);
          } else if (st == 2u) {
            putPx(x, y, pathMid);
          } else {
            putPx(x, y, pathHi);
          }
        } else if (biomeIdx == 0 || oceanWorld) {
          // Clouds of Xeen road: irregular rounded stones, a tone per stone,
          // lit crown up-left and shadow down-right, set in dark earth mortar.
          if (st == 0u) {
            putPx(x, y, (z < 2.6f) ? inkOut : pathDark);
          } else if (st == 1u) {
            putPx(x, y, (biomeIdx == 0) ? pathMid : rockDark);
          } else if (st == 2u) {
            putPx(x, y, (roadStyle == 0) ? rockMid : pathMid);
          } else {
            putPx(x, y, (roadStyle == 0) ? pathHi : pathHi);
          }
        } else if (iceWorld) {
          // Frost-rimmed glacier stone: pale faces, blue shadow in the joints.
          if (st == 0u) {
            putPx(x, y, (z < 2.5f) ? inkOut : pathDark);
          } else if (st == 1u) {
            putPx(x, y, rockMid);
          } else if (st == 2u) {
            putPx(x, y, pathMid);
          } else {
            putPx(x, y, pathHi);
          }
        } else {
          // Rock / Badlands: sun-baked flagstone in warm dust.
          if (st == 0u) {
            putPx(x, y, (z < 2.5f) ? inkOut : pathDark);
          } else if (st == 1u) {
            putPx(x, y, rockDark);
          } else if (st == 2u) {
            putPx(x, y, pathMid);
          } else {
            putPx(x, y, pathHi);
          }
        }
      }
    }
  }

  activeFog = 0;
  activeInk = inkOut;

  // Handcrafted PC-98 Clouds of Xeen towering multi-trunk leafy forest grove
  auto drawForestGrove = [&](int tx, int gy, float s) {
    const int th = std::max(16, static_cast<int>(std::lround(76.0f * s)));
    const int tw = std::max(16, static_cast<int>(std::lround(56.0f * s)));
    const int trunkH = std::max(7, th * 48 / 100);
    // Lush dark undergrowth shadow + mossy grass bank at grove base
    fillDisc(tx, gy, std::max(5, tw * 48 / 100), std::max(2, static_cast<int>(std::lround(3.0f * s))), inkOut);
    fillOutlinedDisc(tx - tw / 5, gy - std::max(2, th / 14), std::max(4, tw * 32 / 100), std::max(2, th / 11), leaf0, inkOut);
    fillOutlinedDisc(tx + tw / 5, gy - std::max(2, th / 14), std::max(4, tw * 32 / 100), std::max(2, th / 11), leaf0, inkOut);
    fillDitherDisc(tx, gy - std::max(2, th / 12), std::max(4, tw * 34 / 100), std::max(2, th / 12), leaf0, leaf1, 10);
    // 4 tall overlapping tree trunks (birch/oak mix like PC-98 Clouds of Xeen!)
    static constexpr int kTrunkOff[4] = {-32, -10, 12, 31};
    for (int t = 0; t < 4; ++t) {
      const int bx = tx + (tw * kTrunkOff[t]) / 100;
      const int bH = trunkH + (((t + tx) & 1) ? th / 12 : 0);
      const int halfW = std::max(1, static_cast<int>(std::lround(((t == 1 || t == 2) ? 2.4f : 1.8f) * s)));
      const bool birch = ((t + (tx & 3)) & 1) == 0 && subBiome != 3;
      const std::uint8_t barkLit = birch ? rockHi : pathHi;
      const std::uint8_t barkMid = birch ? rockMid : trunkCol;
      for (int row = 0; row < bH; ++row) {
        const int py = gy - row;
        const int hw = std::max(1, halfW - (row > bH * 2 / 3 ? 1 : 0));
        putPx(bx - hw - 1, py, inkOut);
        putPx(bx + hw + 1, py, inkOut);
        for (int px = bx - hw; px <= bx + hw; ++px) {
          std::uint8_t col = (px <= bx - hw / 2) ? barkLit : (px == bx + hw ? pathDark : barkMid);
          if (birch && ((row + t * 3) % 5) == 0 && px == bx) { col = inkOut; }
          putPx(px, py, col);
        }
      }
    }
    // Towering, billowing multi-lobe PC-98 leafy canopy with crisp ink outlines & crunchy leaf stipple
    const int canBaseY = gy - trunkH;
    const int canH = std::max(10, th - trunkH);
    struct Lobe { int dxP; int dyP; int rxP; int ryP; };
    static constexpr Lobe kLobes[7] = {
        {-34, -22, 24, 30}, { 34, -20, 24, 30},
        {-16, -42, 28, 34}, { 18, -40, 28, 34},
        {  0, -56, 30, 34}, {-26, -60, 22, 26}, { 26, -58, 22, 26},
    };
    // Back dark-outlined foliage mass
    for (int i = 0; i < 7; ++i) {
      const int lx = tx + (tw * kLobes[i].dxP) / 100;
      const int ly = canBaseY + (canH * kLobes[i].dyP) / 100;
      const int rx = std::max(4, (tw * kLobes[i].rxP) / 100);
      const int ry = std::max(3, (canH * kLobes[i].ryP) / 100);
      fillOutlinedDisc(lx, ly, rx, ry, leaf0, inkOut);
    }
    // Mid emerald leafy billow clusters (upper-left offset for crisp foliage volume)
    for (int i = 0; i < 7; ++i) {
      const int lx = tx + (tw * kLobes[i].dxP) / 100 - std::max(1, tw / 24);
      const int ly = canBaseY + (canH * kLobes[i].dyP) / 100 - std::max(1, canH / 16);
      const int rx = std::max(3, (tw * kLobes[i].rxP) / 122);
      const int ry = std::max(2, (canH * kLobes[i].ryP) / 122);
      fillDitherDisc(lx, ly, rx, ry, leaf0, leaf1, (i >= 2) ? 13 : 9);
    }
    // Sunlit crown highlights + crunchy PC-98 dark ink leaf-crevice clusters
    for (int i = 2; i < 7; ++i) {
      const int lx = tx + (tw * kLobes[i].dxP) / 100 - std::max(1, tw / 18);
      const int ly = canBaseY + (canH * kLobes[i].dyP) / 100 - std::max(2, canH / 9);
      const int rx = std::max(2, (tw * kLobes[i].rxP) / 170);
      const int ry = std::max(2, (canH * kLobes[i].ryP) / 170);
      fillDitherDisc(lx, ly, rx, ry, leaf1, leaf2, 11);
    }
    // Authentic PC-98 speckled leaf texture (leaf2 tips + inkOut leaf-shadow pockets)
    const int yTopCan = std::max(0, canBaseY - canH);
    const int yBotCan = std::min(H - 1, canBaseY + 2);
    const int x0Can = std::max(0, tx - tw / 2);
    const int x1Can = std::min(W - 1, tx + tw / 2);
    for (int py = yTopCan; py <= yBotCan; py += 2) {
      for (int px = x0Can; px <= x1Can; px += 2) {
        const std::uint8_t cur = idxBuf[py * W + px];
        if (cur == leaf1 || cur == leaf0) {
          const unsigned lh = static_cast<unsigned>(px) * 37u ^ static_cast<unsigned>(py) * 91u;
          if ((lh % 7u) == 0u && py < canBaseY - canH / 3 && px < tx + tw / 4) {
            putPx(px, py, leaf2);
          } else if ((lh % 9u) == 0u && py > canBaseY - canH * 2 / 3) {
            putPx(px, py, inkOut);
          }
        }
      }
    }
  };

  // Handcrafted PC-98 organic & architectural biome prop families (240x144 scale, zero boxy blobs)
  auto drawBroadleafTree = [&](int tx, int gy, float s) {
    drawForestGrove(tx, gy, s * 0.92f);
  };

  auto drawPineTree = [&](int tx, int gy, float s) {
    const int th = std::max(12, static_cast<int>(std::lround(60.0f * s)));
    const int tw = std::max(9, static_cast<int>(std::lround(32.0f * s)));
    const int trunkW = std::max(2, static_cast<int>(std::lround(4.5f * s)));
    const int trunkH = std::max(3, th * 30 / 100);
    fillDisc(tx, gy, std::max(3, tw * 38 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    fillOutlineRect(tx - trunkW / 2 - 1, gy - trunkH, trunkW + 2, trunkH + 1, trunkCol, inkOut);
    const int coneBase = gy - trunkH + 1;
    const int coneH = std::max(9, th - trunkH);
    const int tiers = (coneH >= 16) ? 4 : 3;
    for (int row = 0; row < coneH; ++row) {
      const int tier = (row * tiers) / coneH;
      const int tierStart = (tier * coneH) / tiers;
      const int tierSpan = std::max(2, ((tier + 1) * coneH) / tiers - tierStart);
      const int tierRow = row - tierStart;
      const float frac = static_cast<float>(tierRow + 1) / static_cast<float>(tierSpan);
      const int baseHalf = (tw / 2) * (tier + 2) / (tiers + 1);
      const int droop = (tierRow == tierSpan - 1 && baseHalf >= 4) ? 1 : 0;
      const int hw = std::max(1, static_cast<int>(std::lround(static_cast<float>(baseHalf) * (0.28f + 0.72f * frac)))) + droop;
      const int py = coneBase - coneH + row;
      putPx(tx - hw - 1, py, inkOut);
      putPx(tx + hw + 1, py, inkOut);
      if (row == 0) { putPx(tx, py - 1, inkOut); }
      const bool snowCap = iceWorld && (tierRow <= tierSpan / 2);
      for (int px = tx - hw; px <= tx + hw; ++px) {
        std::uint8_t col = leaf0;
        if (snowCap) {
          col = (px < tx + hw / 4) ? leaf2 : (tierRow == 0 ? rockHi : leaf1);
        } else if (tierRow == tierSpan - 1 && ((px + tier) & 1) == 0) {
          col = inkOut;  // shadowed bough underside notches
        } else if (px < tx - hw / 5) {
          col = (tierRow < tierSpan / 2) ? leaf2 : leaf1;
        } else if (px <= tx + hw / 4) {
          col = (kBayer4[py & 3][px & 3] < 9) ? leaf1 : leaf0;
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - tw / 2, tx + tw / 2, coneBase, inkOut);
  };

  auto drawExoticCanopy = [&](int tx, int gy, float s) {
    const int th = std::max(10, static_cast<int>(std::lround(34.0f * s)));
    const int tw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int trunkH = std::max(5, th * 54 / 100);
    const int lean = ((tx & 1) ? 1 : -1) * std::max(1, tw / 7);
    fillDisc(tx, gy, std::max(3, tw * 38 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Curved organic stem
    for (int row = 0; row < trunkH; ++row) {
      const int py = gy - row;
      const int cx = tx + (lean * row * row) / std::max(1, trunkH * trunkH);
      const int hw = std::max(1, static_cast<int>(std::lround((2.6f - 1.0f * static_cast<float>(row) / static_cast<float>(trunkH)) * s)));
      putPx(cx - hw - 1, py, inkOut);
      putPx(cx + hw + 1, py, inkOut);
      for (int px = cx - hw; px <= cx + hw; ++px) {
        putPx(px, py, (px < cx) ? pathHi : trunkCol);
      }
    }
    const int cx = tx + lean;
    const int cy = gy - trunkH;
    const int capRx = std::max(5, tw / 2);
    const int capRy = std::max(3, th * 20 / 100);
    if (biomeIdx == 0 && subBiome == 3) {
      // Alien Biosphere giant glowing mushroom canopy with gills
      fillOutlinedDisc(cx, cy, capRx, capRy, leaf1, inkOut);
      fillDisc(cx, cy + capRy / 2, std::max(3, capRx * 7 / 10), std::max(2, capRy / 2), pathDark);
      for (int px = cx - capRx * 6 / 10; px <= cx + capRx * 6 / 10; px += 2) {
        putPx(px, cy + capRy / 2, pathHi);
      }
      fillDisc(cx - capRx / 4, cy - capRy / 3, std::max(2, capRx / 2), std::max(2, capRy / 2), leaf2);
      putPx(cx + capRx / 3, cy - capRy / 4, kIdxWarmGleam);
    } else {
      // Wide sculpted umbrella / cypress / willow canopy with hanging tendrils
      fillOutlinedDisc(cx, cy - capRy / 3, capRx, capRy, leaf0, inkOut);
      fillDitherDisc(cx - capRx / 5, cy - capRy / 2, std::max(3, capRx * 7 / 10), std::max(2, capRy * 7 / 10), leaf1, leaf2, 8);
      fillDisc(cx - capRx / 4, cy - capRy * 2 / 3, std::max(2, capRx / 2), std::max(1, capRy / 2), leaf2);
      if (capRx >= 6) {
        for (int dx = -capRx + 2; dx <= capRx - 2; dx += 4) {
          const int drop = 2 + ((std::abs(dx) + tx) % 4);
          fillRect(cx + dx, cy + capRy / 2, 1, drop, (dx < 0) ? leaf1 : leaf0);
        }
      }
    }
  };

  auto drawMossyMonolith = [&](int tx, int gy, float s) {
    const int mh = std::max(11, static_cast<int>(std::lround(33.0f * s)));
    const int mw = std::max(7, static_cast<int>(std::lround(17.0f * s)));
    fillDisc(tx, gy, std::max(3, mw * 65 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Tapering weathered standing stone with broken top bevel
    for (int row = 0; row < mh; ++row) {
      const int py = gy - row;
      const int taper = (row * (mw / 4)) / std::max(1, mh);
      const int hwL = std::max(2, mw / 2 - taper);
      const int hwR = std::max(2, mw / 2 - taper - ((row > mh * 3 / 4) ? (row - mh * 3 / 4) / 2 : 0));
      putPx(tx - hwL - 1, py, inkOut);
      putPx(tx + hwR + 1, py, inkOut);
      for (int px = tx - hwL; px <= tx + hwR; ++px) {
        std::uint8_t col = rockMid;
        if (row >= mh - std::max(2, mh / 6)) {
          col = (px < tx) ? leaf2 : leaf1;  // moss cap
        } else if (px <= tx - hwL / 2) {
          col = rockHi;
        } else if (px >= tx + hwR / 2) {
          col = rockDark;
        } else if (mh >= 16 && (row == mh / 2 || row == mh / 3) && std::abs(px - tx) <= hwL / 2) {
          col = inkOut;  // carved ancient rune band
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - mw / 3, tx + mw / 4, gy - mh, inkOut);
    // Flanking mossy boulder at foot
    fillOutlinedDisc(tx + mw / 2, gy - std::max(2, mh / 8), std::max(2, mw / 3), std::max(2, mh / 8), rockDark, inkOut);
    fillDisc(tx + mw / 2 - 1, gy - std::max(2, mh / 8) - 1, std::max(1, mw / 5), std::max(1, mh / 12), rockMid);
  };

  auto drawCrystalCluster = [&](int tx, int gy, float s, bool obsidian) {
    const int cw = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    const int ch = std::max(12, static_cast<int>(std::lround(36.0f * s)));
    fillDisc(tx, gy, std::max(3, cw / 2), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    const std::uint8_t cDark = obsidian ? rockDark : (iceWorld ? leaf0 : rockDark);
    const std::uint8_t cMid  = obsidian ? rockMid  : accent0;
    const std::uint8_t cHi   = obsidian ? accent0  : accent1;
    auto drawShard = [&](int baseX, int h, int halfW, int leanPx) {
      if (h < 4) { return; }
      for (int row = 0; row < h; ++row) {
        const int py = gy - row;
        const int cx = baseX + (leanPx * row) / h;
        const int topSpan = std::max(2, h / 4);
        int hw = halfW;
        if (row > h - topSpan) {
          hw = std::max(0, (halfW * (h - row)) / topSpan);
        }
        putPx(cx - hw - 1, py, inkOut);
        putPx(cx + hw + 1, py, inkOut);
        for (int px = cx - hw; px <= cx + hw; ++px) {
          std::uint8_t col = cMid;
          if (px <= cx - hw / 3) {
            col = cHi;
          } else if (px >= cx + hw / 2) {
            col = cDark;
          } else if (((row + px) & 3) == 0 && row > h / 3) {
            col = cHi;
          }
          putPx(px, py, col);
        }
      }
      putPx(baseX + leanPx, gy - h, inkOut);
    };
    drawShard(tx - cw * 3 / 10, ch * 62 / 100, std::max(2, cw / 6), -std::max(2, cw / 4));
    drawShard(tx + cw * 3 / 10, ch * 55 / 100, std::max(2, cw / 6),  std::max(2, cw / 4));
    drawShard(tx, ch, std::max(2, cw * 24 / 100), ((tx & 1) ? 1 : -1) * std::max(1, cw / 10));
  };

  auto drawGlacialSerac = [&](int tx, int gy, float s) {
    const int sw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int sh = std::max(9, static_cast<int>(std::lround(26.0f * s)));
    fillDisc(tx, gy, sw / 2, std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Curved wind-carved ice arch with shadowed tunnel opening
    fillOutlinedDisc(tx, gy - sh / 2, sw / 2, sh / 2, rockMid, inkOut);
    fillDitherDisc(tx - sw / 6, gy - sh * 2 / 3, std::max(3, sw / 3), std::max(2, sh / 4), rockHi, accent1, 9);
    // Shadowed arch hollow
    if (sw >= 12 && sh >= 10) {
      fillOutlinedDisc(tx, gy - sh / 5, std::max(2, sw / 5), std::max(2, sh / 4), sky2, inkOut);
      fillDisc(tx, gy - sh / 6, std::max(1, sw / 6), std::max(1, sh / 5), rockDark);
    }
  };

  auto drawSeaStack = [&](int tx, int gy, float s) {
    const int sw = std::max(10, static_cast<int>(std::lround(28.0f * s)));
    const int sh = std::max(11, static_cast<int>(std::lround(34.0f * s)));
    // White surf-foam collar at base
    fillOutlinedDisc(tx, gy, std::max(4, sw * 52 / 100), std::max(1, static_cast<int>(std::lround(2.4f * s))), sky3, inkOut);
    // Undercut tide-eroded basalt stack (narrower at wave-cut notch, wider above)
    for (int row = 1; row < sh; ++row) {
      const int py = gy - row;
      const int u16 = (row * 16) / sh;
      int hw = (sw / 2);
      if (u16 < 5) {
        hw = sw * 34 / 100 + (u16 * sw) / 48;
      } else if (u16 > 12) {
        hw = std::max(2, (sw * (17 - u16)) / 10);
      } else {
        hw = sw * 46 / 100 + ((u16 == 8) ? 1 : 0);
      }
      putPx(tx - hw - 1, py, inkOut);
      putPx(tx + hw + 1, py, inkOut);
      for (int px = tx - hw; px <= tx + hw; ++px) {
        std::uint8_t col = rockMid;
        if (u16 >= 13) {
          col = (px < tx) ? leaf1 : leaf0;  // sea-moss crown
        } else if (px <= tx - hw / 2) {
          col = rockHi;
        } else if (px >= tx + hw / 3 || u16 < 3) {
          col = rockDark;
        } else if (u16 == 6 || u16 == 10) {
          col = rockDark;
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - sw / 5, tx + sw / 5, gy - sh, inkOut);
  };

  auto drawBranchingCoral = [&](int tx, int gy, float s) {
    const int cw = std::max(9, static_cast<int>(std::lround(26.0f * s)));
    const int ch = std::max(9, static_cast<int>(std::lround(26.0f * s)));
    fillDisc(tx, gy, std::max(3, cw * 42 / 100), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Basal rock mound + forked staghorn coral limbs with bright polyp tips
    fillOutlinedDisc(tx, gy - std::max(2, ch / 6), std::max(3, cw / 3), std::max(2, ch / 6), rockDark, inkOut);
    auto drawLimb = [&](int x0, int y0, int x1, int y1, int rad) {
      const int steps = std::max(4, std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
      for (int i = 0; i <= steps; ++i) {
        const int px = x0 + ((x1 - x0) * i) / steps;
        const int py = y0 + ((y1 - y0) * i) / steps;
        fillOutlinedDisc(px, py, rad, rad, (i > steps * 2 / 3) ? accent1 : accent0, inkOut);
      }
    };
    const int r = std::max(1, static_cast<int>(std::lround(1.6f * s)));
    drawLimb(tx, gy - ch / 6, tx - cw / 3, gy - ch * 7 / 10, r);
    drawLimb(tx - cw / 6, gy - ch * 4 / 10, tx - cw * 4 / 10, gy - ch * 9 / 10, std::max(1, r));
    drawLimb(tx, gy - ch / 6, tx, gy - ch, r);
    drawLimb(tx, gy - ch / 4, tx + cw / 3, gy - ch * 75 / 100, r);
    drawLimb(tx + cw / 6, gy - ch * 5 / 10, tx + cw * 4 / 10, gy - ch * 92 / 100, std::max(1, r));
  };

  auto drawHydroVent = [&](int tx, int gy, float s) {
    const int vw = std::max(10, static_cast<int>(std::lround(28.0f * s)));
    const int vh = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    fillDisc(tx, gy, vw / 2, std::max(1, static_cast<int>(std::lround(2.2f * s))), inkOut);
    // Sloped volcanic/hydrothermal cone with glowing crater lip & magma cracks
    for (int row = 0; row < vh; ++row) {
      const int py = gy - row;
      const int hw = std::max(2, (vw / 2) - (row * (vw / 3)) / std::max(1, vh));
      putPx(tx - hw - 1, py, inkOut);
      putPx(tx + hw + 1, py, inkOut);
      for (int px = tx - hw; px <= tx + hw; ++px) {
        std::uint8_t col = (px < tx - hw / 3) ? rockMid : rockDark;
        if (row >= vh - std::max(2, vh / 5) && std::abs(px - tx) <= hw * 6 / 10) {
          col = (row == vh - 1) ? accent1 : accent0;
        } else if (lavaWorld && (px == tx - row / 4 || px == tx + row / 3) && row > vh / 3) {
          col = accent0;  // lava rivulets running down cone
        } else if (px == tx - hw + 1) {
          col = rockHi;
        }
        putPx(px, py, col);
      }
    }
    // Glowing crater mouth + rising ember/bubble wisps
    fillDisc(tx, gy - vh, std::max(2, vw / 6), std::max(1, vh / 10), accent1);
    if (vh >= 12) {
      const std::uint8_t wispCol = lavaWorld ? accent0 : sky3;
      putPx(tx - 1, gy - vh - 3, wispCol);
      putPx(tx + 2, gy - vh - 5, lavaWorld ? kIdxWarmGleam : kIdxStarCool);
      putPx(tx, gy - vh - 7, wispCol);
    }
  };

  auto drawBasaltArch = [&](int tx, int gy, float s) {
    const int aw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int ah = std::max(10, static_cast<int>(std::lround(30.0f * s)));
    fillDisc(tx, gy, aw / 2, std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Dramatic skeletal volcanic arch / charred spire
    for (int row = 0; row < ah; ++row) {
      const int py = gy - row;
      const int u16 = (row * 16) / ah;
      const int outerHw = std::max(3, aw / 2 - (u16 * aw) / 48);
      if (u16 < 11) {
        const int legW = std::max(2, aw / 5 - u16 / 6);
        fillOutlineRect(tx - outerHw, py, legW + 2, 1, (u16 & 2) ? rockMid : rockDark, inkOut);
        fillOutlineRect(tx + outerHw - legW - 1, py, legW + 2, 1, rockDark, inkOut);
        if (lavaWorld && u16 == 4) { putPx(tx - outerHw + 1, py, accent0); }
      } else {
        putPx(tx - outerHw - 1, py, inkOut);
        putPx(tx + outerHw + 1, py, inkOut);
        for (int px = tx - outerHw; px <= tx + outerHw; ++px) {
          putPx(px, py, (u16 >= 14 && px < tx) ? rockHi : ((px < tx) ? rockMid : rockDark));
        }
      }
    }
    drawHLine(tx - aw / 3, tx + aw / 3, gy - ah, inkOut);
  };

  auto drawSandstoneHoodoo = [&](int tx, int gy, float s) {
    const int hw = std::max(10, static_cast<int>(std::lround(30.0f * s)));
    const int hh = std::max(11, static_cast<int>(std::lround(34.0f * s)));
    fillDisc(tx, gy, hw * 44 / 100, std::max(1, static_cast<int>(std::lround(2.2f * s))), inkOut);
    // Wind-eroded narrow pedestal supporting a wide stratified caprock!
    for (int row = 0; row < hh; ++row) {
      const int py = gy - row;
      const int u16 = (row * 16) / hh;
      int halfW = hw / 4;
      if (u16 < 3) {
        halfW = hw * 38 / 100 - (u16 * hw) / 24;
      } else if (u16 < 9) {
        halfW = std::max(2, hw * 19 / 100);  // slender neck
      } else if (u16 < 14) {
        halfW = hw * 48 / 100;               // overhanging stratified caprock
      } else {
        halfW = std::max(3, hw * (17 - u16) / 10);
      }
      putPx(tx - halfW - 1, py, inkOut);
      putPx(tx + halfW + 1, py, inkOut);
      for (int px = tx - halfW; px <= tx + halfW; ++px) {
        std::uint8_t col = rockMid;
        if (u16 == 9 || u16 == 8) {
          col = rockDark;  // deep shadow notch under overhang
        } else if (u16 == 12 || u16 == 5) {
          col = rockHi;    // bright sedimentary stratum
        } else if (px <= tx - halfW / 3) {
          col = rockHi;
        } else if (px >= tx + halfW / 3) {
          col = rockDark;
        }
        putPx(px, py, col);
      }
    }
    drawHLine(tx - hw / 4, tx + hw / 4, gy - hh, inkOut);
  };

  auto drawBandedButte = [&](int tx, int gy, float s) {
    const int rw = std::max(11, static_cast<int>(std::lround(32.0f * s)));
    const int rh = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    fillDisc(tx, gy, rw / 2, std::max(1, static_cast<int>(std::lround(2.2f * s))), inkOut);
    // Multi-tier sculpted boulder / stepped rock mesa with diagonal strata
    fillOutlinedDisc(tx - rw / 6, gy - rh * 4 / 10, rw * 38 / 100, rh * 42 / 100, rockDark, inkOut);
    fillOutlinedDisc(tx + rw / 4, gy - rh * 3 / 10, rw * 28 / 100, rh * 34 / 100, rockMid, inkOut);
    fillOutlinedDisc(tx - rw / 10, gy - rh * 65 / 100, rw * 32 / 100, rh * 36 / 100, rockMid, inkOut);
    fillDitherDisc(tx - rw / 6, gy - rh * 78 / 100, std::max(2, rw * 22 / 100), std::max(2, rh * 20 / 100), rockMid, rockHi, 11);
    fillDisc(tx + rw / 5, gy - rh * 45 / 100, std::max(2, rw * 14 / 100), std::max(1, rh * 12 / 100), rockHi);
  };

  auto drawDesertSucculent = [&](int tx, int gy, float s) {
    if (airlessWorld) {
      drawCrystalCluster(tx, gy, s * 0.88f, true);
      return;
    }
    const int ch = std::max(10, static_cast<int>(std::lround(30.0f * s)));
    const int cw = std::max(8, static_cast<int>(std::lround(22.0f * s)));
    const int stemW = std::max(3, static_cast<int>(std::lround(5.0f * s)));
    fillDisc(tx, gy, std::max(3, cw / 3), std::max(1, static_cast<int>(std::lround(2.0f * s))), inkOut);
    // Ribbed branching saguaro / desert succulent
    fillOutlineRect(tx - stemW / 2 - 1, gy - ch, stemW + 2, ch + 1, leaf1, inkOut);
    fillRect(tx - stemW / 2, gy - ch + 1, std::max(1, stemW / 3), ch - 1, leaf2);
    fillRect(tx + stemW / 2 - 1, gy - ch + 1, 1, ch - 1, leaf0);
    // Left & right arms
    const int armW = std::max(2, stemW * 3 / 4);
    const int lArmY = gy - ch * 55 / 100;
    const int rArmY = gy - ch * 42 / 100;
    fillOutlineRect(tx - cw * 42 / 100, lArmY, cw * 42 / 100, armW + 1, leaf0, inkOut);
    fillOutlineRect(tx - cw * 42 / 100, lArmY - ch * 28 / 100, armW + 2, ch * 28 / 100 + armW, leaf1, inkOut);
    fillRect(tx - cw * 42 / 100 + 1, lArmY - ch * 28 / 100 + 1, 1, ch * 24 / 100, leaf2);
    fillOutlineRect(tx + stemW / 2, rArmY, cw * 36 / 100, armW + 1, leaf0, inkOut);
    fillOutlineRect(tx + stemW / 2 + cw * 36 / 100 - armW - 1, rArmY - ch * 24 / 100, armW + 2, ch * 24 / 100 + armW, leaf1, inkOut);
  };

  auto drawScrubBush = [&](int tx, int gy, float s) {
    const int bw = std::max(6, static_cast<int>(std::lround(17.0f * s)));
    const int bh = std::max(4, static_cast<int>(std::lround(10.0f * s)));
    fillOutlinedDisc(tx - bw / 4, gy - bh / 3, std::max(2, bw * 28 / 100), std::max(2, bh * 36 / 100), leaf0, inkOut);
    fillOutlinedDisc(tx + bw / 4, gy - bh / 3, std::max(2, bw * 28 / 100), std::max(2, bh * 36 / 100), leaf0, inkOut);
    fillOutlinedDisc(tx, gy - bh * 55 / 100, std::max(2, bw * 34 / 100), std::max(2, bh * 42 / 100), leaf1, inkOut);
    fillDisc(tx - bw / 6, gy - bh * 65 / 100, std::max(1, bw / 5), std::max(1, bh / 4), leaf2);
    if (biomeIdx == 0 && bw >= 10) {
      putPx(tx + bw / 5, gy - bh / 2, kIdxWarmGleam);
    }
  };

  auto drawGroundScatter = [&](int tx, int gy, float s, int var) {
    const int w = std::max(3, static_cast<int>(std::lround(8.0f * s)));
    if (inMine) {
      fillOutlinedDisc(tx, gy - 1, std::max(1, w / 2), 1, (var & 2) ? accent0 : rockMid, inkOut);
      if ((var & 1) != 0) { putPx(tx - 1, gy - 2, rockHi); }
    } else if (iceWorld) {
      fillRect(tx - w / 2, gy - 1, w, 1, inkOut);
      fillRect(tx - w / 2 + 1, gy - 2, std::max(2, w - 2), 1, rockHi);
      putPx(tx, gy - 3, accent1);
    } else if (oceanWorld) {
      fillOutlinedDisc(tx, gy - 1, std::max(2, w / 2), 1, sky3, inkOut);
      if ((var & 1) == 0) { putPx(tx, gy - 2, accent0); }
    } else if (lavaWorld) {
      fillRect(tx - w / 2, gy - 1, w, 1, inkOut);
      putPx(tx - 1, gy - 2, accent0);
      putPx(tx, gy - 2, accent1);
      putPx(tx + 1, gy - 1, accent0);
    } else if (rockWorld) {
      fillOutlinedDisc(tx, gy - 1, std::max(2, w / 2), std::max(1, w / 4), rockMid, inkOut);
      putPx(tx - 1, gy - 2, rockHi);
    } else {
      // Temperate grass tuft + flower/autumn leaf
      putPx(tx - 2, gy - 1, leaf0);
      putPx(tx - 1, gy - 2, leaf1);
      putPx(tx, gy - 3, leaf2);
      putPx(tx + 1, gy - 2, leaf1);
      putPx(tx + 2, gy - 1, leaf0);
      if ((var & 1) != 0) { putPx(tx + 1, gy - 3, kIdxWarmGleam); }
    }
  };

  auto drawBiomeProp = [&](int model, int tx, int gy, float s) {
    const int m = (model + propBias) & 3;
    if (iceWorld) {
      if (m == 0) { drawPineTree(tx, gy, s); }
      else if (m == 1) { drawCrystalCluster(tx, gy, s, false); }
      else if (m == 2) { drawGlacialSerac(tx, gy, s); }
      else { drawBandedButte(tx, gy, s); }
    } else if (oceanWorld) {
      if (m == 0) { drawSeaStack(tx, gy, s); }
      else if (m == 1) { drawBranchingCoral(tx, gy, s); }
      else if (m == 2) { drawHydroVent(tx, gy, s); }
      else { drawExoticCanopy(tx, gy, s * 0.90f); }
    } else if (lavaWorld) {
      if (m == 0) { drawCrystalCluster(tx, gy, s, true); }
      else if (m == 1) { drawHydroVent(tx, gy, s); }
      else if (m == 2) { drawBasaltArch(tx, gy, s); }
      else { drawBandedButte(tx, gy, s); }
    } else if (rockWorld) {
      if (m == 0) { drawSandstoneHoodoo(tx, gy, s); }
      else if (m == 1) { drawBandedButte(tx, gy, s); }
      else if (m == 2) { drawBasaltArch(tx, gy, s); }
      else { drawDesertSucculent(tx, gy, s); }
    } else {
      if (m == 0) { drawBroadleafTree(tx, gy, s); }
      else if (m == 1) { drawPineTree(tx, gy, s); }
      else if (m == 2) { drawExoticCanopy(tx, gy, s); }
      else { drawMossyMonolith(tx, gy, s); }
    }
  };

  auto cliffCrownLift = [&](int uPermille, int spanW, int colBaseH, int var, unsigned cellH,
                            bool adjStart, bool adjEnd) -> int {
    if (inMine || spanW < 8 || colBaseH < 8) { return 0; }
    if (var == 7) {
      // Clean perspective-true castle battlements: corners (u < 120 or u > 880) ALWAYS stay flush at yTBase
      // so adjacent front & receding side walls meet at the exact same roofline height!
      if (uPermille < 120 || uPermille > 880) { return 0; }
      const int merlonH = std::max(2, (colBaseH * 11) / 100);
      // 3 crisp evenly-spaced merlons across each wall bay (perspective-consistent across all depths)
      const bool isMerlon = (uPermille >= 150 && uPermille <= 320) ||
                            (uPermille >= 415 && uPermille <= 585) ||
                            (uPermille >= 680 && uPermille <= 850);
      return isMerlon ? merlonH : 0;
    }
    // Natural cliff crown fallback (unused on surface since var 4..6 route to drawMountainCell)
    int edgeDrop = 0;
    if (!adjStart && uPermille < 170) {
      edgeDrop = ((170 - uPermille) * std::clamp(spanW / 8, 2 * kResScale, 7 * kResScale)) / 170;
    } else if (!adjEnd && uPermille > 830) {
      edgeDrop = ((uPermille - 830) * std::clamp(spanW / 8, 2 * kResScale, 7 * kResScale)) / 170;
    }
    const int maxLift = std::clamp(colBaseH / 8, 2 * kResScale, 10 * kResScale);
    const int dMid = std::abs(uPermille - (420 + static_cast<int>((cellH & 3u) * 50u)));
    const int lift = std::max(0, (maxLift * 2 / 3) - (dMid * maxLift) / 560);
    return lift - edgeDrop;
  };

  auto drawCliffCell = [&](int tx, int ty, int d, int s, int var) {
    const unsigned cellH = static_cast<unsigned>(tx * 37 + ty * 91 + var * 13);
    const bool adjNear  = isCliffCell(tx - fdx, ty - fdy);
    const bool adjFar   = isCliffCell(tx + fdx, ty + fdy);
    const bool adjLeft  = isCliffCell(tx + ldx, ty + ldy);
    const bool adjRight = isCliffCell(tx + rdx, ty + rdy);
    const float cellTopScale = cliffTopScaleFor(tx, ty, var);

    const bool fogActive = (activeFog > 0);
    const bool fogHighBand = (activeFog > 16);
    const int wallThr16 = fogHighBand ? std::min(16, activeFog - 16) : activeFog;
    auto colFogBitsForX = [&](int x) -> std::uint8_t {
      if (!fogActive) { return 0u; }
      const int rx = x & 3;
      return static_cast<std::uint8_t>(
          (((kFogMasks.row[wallThr16][0] >> rx) & 1u) << 0) |
          (((kFogMasks.row[wallThr16][1] >> rx) & 1u) << 1) |
          (((kFogMasks.row[wallThr16][2] >> rx) & 1u) << 2) |
          (((kFogMasks.row[wallThr16][3] >> rx) & 1u) << 3));
    };

    // Peripheral d == 0 cheek wall ONLY in underground mines (outdoor lat=+-0.50 at z=0.50 lies outside the frustum)
    if (d == 0) {
      if (!inMine || std::abs(s) != 1) { return; }
      const int cheekW = 9 * kResScale;
      const int x0 = (s < 0) ? 0 : (W - cheekW);
      const int x1 = (s < 0) ? (cheekW - 1) : (W - 1);
      const int yTopInner = cliffYTopScaled(0.50f, cellTopScale);
      const int yTopOuter = inMine ? 0 : std::max(0, yTopInner - 10 * kResScale);
      const int yBotOuter = H - 1;
      const int yBotInner = cliffYBot(0.50f);
      const int yT0 = (s < 0) ? yTopOuter : yTopInner;
      const int yT1 = (s < 0) ? yTopInner : yTopOuter;
      const int yB0 = (s < 0) ? yBotOuter : yBotInner;
      const int yB1 = (s < 0) ? yBotInner : yBotOuter;
      const int dx = std::max(1, x1 - x0);
      std::int32_t yT_fp = yT0 << 16;
      std::int32_t yB_fp = yB0 << 16;
      const std::int32_t dYT_fp = ((yT1 - yT0) << 16) / dx;
      const std::int32_t dYB_fp = ((yB1 - yB0) << 16) / dx;
      const int innerEdgeX = (s < 0) ? x1 : x0;
      for (int x = x0; x <= x1; ++x, yT_fp += dYT_fp, yB_fp += dYB_fp) {
        const int yT = std::max(0, yT_fp >> 16);
        const int yB = std::min(H - 1, yB_fp >> 16);
        if (yT > yB) { continue; }
        if (x == innerEdgeX && !adjFar) {
          fillRect(x, yT, 1, yB - yT + 1, inkOut);
          continue;
        }
        idxBuf[yT * W + x] = inkOut;
        idxBuf[yB * W + x] = inkOut;
        const int colH = std::max(1, yB - yT);
        const std::uint32_t invColH =
            ((1u << 20) + static_cast<std::uint32_t>(colH) - 1u) / static_cast<std::uint32_t>(colH);
        // Texel coords track screen pixels 1:1 horizontally: any horizontal
        // scale here shears the courses into diagonal streaks instead of
        // leaving them reading as stacked ashlar.
        // Magnify the courses (2 screen px per texel) so a block stays big
        // enough to read; sampling at 1:1 here makes the wall a mosaic.
        const int uTex = (x >> 1) & 31;
        for (int y = yT + 1; y < yB; ++y) {
          const int v32 = static_cast<int>((static_cast<std::uint32_t>((y - yT) << 5) * invColH) >> 20) & 31;
          const std::uint8_t st = kStoneWallTex.px[v32][uTex];
          std::uint8_t cIdx = (st == 0u) ? inkOut : (st == 3u) ? rockMid : rockDark;
          if (!inMine && biomeIdx == 0 && v32 <= 4 && st > 0u) { cIdx = leaf0; }
          else if (!inMine && lavaWorld && st == 0u && v32 >= 14 && v32 <= 18) { cIdx = accent0; }
          idxBuf[y * W + x] = cIdx;
        }
      }
      return;
    }

    const float zNearW = static_cast<float>(d) - 0.50f;
    const float zFarW  = static_cast<float>(d) + 0.50f;
    const int clipL = std::max(0, static_cast<int>(rowX0[d]));
    const int clipR = std::min(W - 1, static_cast<int>(rowX1[d]));

    // 1. Quadrant-culled receding side face (defold sign rule: s < 0 -> right face, s > 0 -> left face)
    bool drawSide = false;
    float sideLat = 0.0f;
    if (s < 0 && !adjRight) {
      drawSide = true;
      sideLat = static_cast<float>(s) + 0.50f;
    } else if (s > 0 && !adjLeft) {
      drawSide = true;
      sideLat = static_cast<float>(s) - 0.50f;
    }

    if (drawSide) {
      const int xNear = cliffX(zNearW, sideLat);
      const int xFar  = cliffX(zFarW, sideLat);
      const int yTopN = cliffYTopScaled(zNearW, cellTopScale);
      const int yBotN = cliffYBot(zNearW);
      const int yTopF = cliffYTopScaled(zFarW, cellTopScale);
      const int yBotF = cliffYBot(zFarW);

      const bool nearLeft = (s < 0);
      const int x0 = nearLeft ? xNear : xFar;
      const int x1 = nearLeft ? xFar  : xNear;
      const int yT0 = nearLeft ? yTopN : yTopF;
      const int yT1 = nearLeft ? yTopF : yTopN;
      const int yB0 = nearLeft ? yBotN : yBotF;
      const int yB1 = nearLeft ? yBotF : yBotN;
      const bool adjSideStart = nearLeft ? adjNear : adjFar;
      const bool adjSideEnd   = nearLeft ? adjFar  : adjNear;

      if (x1 >= clipL && x0 <= clipR && x1 >= x0) {
        const int spanW = std::max(1, x1 - x0);
        std::int32_t yT_fp = yT0 << 16;
        std::int32_t yB_fp = yB0 << 16;
        const std::int32_t dYT_fp = ((yT1 - yT0) << 16) / spanW;
        const std::int32_t dYB_fp = ((yB1 - yB0) << 16) / spanW;
        int startX = x0;
        if (startX < clipL) {
          const int skip = clipL - startX;
          yT_fp += dYT_fp * skip;
          yB_fp += dYB_fp * skip;
          startX = clipL;
        }
        const int endX = std::min(clipR, x1);
        for (int x = startX; x <= endX; ++x, yT_fp += dYT_fp, yB_fp += dYB_fp) {
          if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { continue; }
          const int uPerm = ((x - x0) * 1000) / spanW;
          const int yTBaseCol = std::clamp(yT_fp >> 16, 0, H - 1);
          const int yB = std::clamp(yB_fp >> 16, 0, H - 1);
          const int colBaseH = std::max(1, yB - yTBaseCol);
          const int lift = cliffCrownLift(uPerm, spanW, colBaseH, var, cellH, adjSideStart, adjSideEnd);
          const int yT = std::clamp(yTBaseCol - lift, 0, H - 1);
          if (yT > yB) { continue; }
          if ((x == xNear && !adjNear) || (x == xFar && !adjFar)) {
            fillRect(x, yT, 1, yB - yT + 1, inkOut);
            continue;
          }
          idxBuf[yT * W + x] = activeInk;
          idxBuf[yB * W + x] = activeInk;
          const int colH = std::max(1, yB - yT);
          const std::uint32_t invColH =
              ((1u << 20) + static_cast<std::uint32_t>(colH) - 1u) / static_cast<std::uint32_t>(colH);
          const std::uint8_t colFogMask = colFogBitsForX(x);
          const bool sidePilaster = (inMine && var == 7 && spanW >= 12 * kResScale && (uPerm < 90 || uPerm > 910));
          const int vineLen = (!inMine && biomeIdx == 0 && colH >= 12)
                                  ? static_cast<int>(((static_cast<unsigned>(x) * 13u + cellH) % 7u) == 0u
                                                         ? (colH * (25 + static_cast<int>((x * 7 + cellH) % 30u)) / 100)
                                                         : 2)
                                  : 0;
          // No per-cell U offset: offsetting each bay breaks the courses at
          // every depth step and the wall reads as a zigzag chevron.
          const int uTexSide = ((uPerm * 48) / 1000) & 31;
          for (int y = yT + 1; y < yB; ++y) {
            const int v32 = static_cast<int>((static_cast<std::uint32_t>((y - yT) << 5) * invColH) >> 20) & 31;
            const int v16 = v32 >> 1;
            const std::uint8_t st = kStoneWallTex.px[v32][uTexSide];
            // Ice worlds get snow-capped stone: lit courses white, faces the
            // cold rock blue, joints the darkest tone. Reusing the generic
            // rock/mortar pair here just gives a field of teal blotches.
            // Ice worlds: snow-capped stone. Rock worlds: a sandstone cliff,
            // so faces and shadows stay one tone apart and only the lit
            // course and the joints carry contrast.
            std::uint8_t cIdx = iceWorld
                                    ? ((st == 0u) ? inkOut : (st == 3u) ? pathHi
                                                   : (st == 2u) ? pathDark : rockMid)
                                : rockWorld
                                    ? ((st == 0u) ? inkOut : (st == 3u) ? rockMid : rockDark)
                                    : ((st == 0u) ? inkOut : (st == 3u) ? rockMid : rockDark);
            if (sidePilaster) {
              // Mine timber post framing receding bay edges
              cIdx = (uPerm < 50 || uPerm > 950) ? trunkCol : pathHi;
            } else if (!inMine && oceanWorld && y >= yB - 2 && ((x + y) & 1) == 0) {
              cIdx = sky3;  // surf foam collar along sea-cliff side
            } else if (!inMine && biomeIdx == 0 && y <= yT + vineLen) {
              cIdx = (y == yT + 1) ? leaf1 : leaf0;  // hanging forest moss/ivy tendrils
            } else if (!inMine && iceWorld && y <= yT + 2) {
              cIdx = (y == yT + 1) ? rockHi : rockMid;   // snow cap, not mint
            } else if (var == 6 && inMine && st == 3u && std::abs((v16 * 55) + (uPerm / 4) - 560) < 38) {
              cIdx = (((x + y) & 1) == 0) ? accent0 : accent1;  // diagonal glowing ore vein in mine stone
            } else if (st == 0u) {
              if ((inMine || biomeIdx == 0) && !lavaWorld && !iceWorld &&
                  ((uTexSide + v32 + static_cast<int>(cellH)) % 9) == 0) {
                cIdx = leaf0;  // World of Xeen damp mossy mortar pockets
              } else if (lavaWorld && !inMine && v16 == 8) {
                cIdx = accent0;
              } else {
                cIdx = inkOut;
              }
            } else if (st == 2u && kBayer4[y & 3][x & 3] < (inMine ? 2 : 3)) {
              cIdx = rockMid;
            } else if (st == 3u && !inMine && iceWorld) {
              cIdx = rockHi;
            }
            if (!fogActive) {
              idxBuf[y * W + x] = cIdx;
            } else if (cIdx == inkOut) {
              idxBuf[y * W + x] = activeInk;
            } else {
              const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
              const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
              idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
            }
          }
        }

        // Might & Magic corridor side-wall dressing (underground torches, barred alcoves, surface runes)
        const int visSpanW = endX - startX + 1;
        if (visSpanW >= 10 * kResScale && d <= 4) {
          const int smx = (x0 + x1) / 2;
          if (smx >= startX + 3 * kResScale && smx <= endX - 3 * kResScale &&
              ((depthColOcc[d][smx >> 5] >> (smx & 31)) & 1u) == 0u) {
            const int syT = (yT0 + yT1) / 2;
            const int syB = (yB0 + yB1) / 2;
            const int sH = syB - syT;
            if (inMine && sH >= 16 * kResScale) {
              if ((cellH % 3u) == 0u) {
                // Foreshortened side-wall glowing torch sconce lining the dark mine corridor
                const int tyC = syT + sH * 42 / 100;
                const int haloR = std::clamp(std::min(visSpanW / 3, sH / 6), 3 * kResScale, 9 * kResScale);
                const int savedFog = activeFog;
                activeFog = std::max(0, savedFog - 14);
                fillDitherDisc(smx, tyC - kResScale, haloR, haloR, rockDark, pathHi, 7);
                fillDitherDisc(smx, tyC - kResScale, std::max(2, haloR / 2), std::max(2, haloR / 2),
                               pathHi, kIdxOreGold, 6);
                fillRect(smx - kResScale, tyC + kResScale, 2 * kResScale, std::max(3 * kResScale, sH / 10), inkOut);
                fillRect(smx, tyC + 2 * kResScale, std::max(1, kResScale / 2), std::max(2 * kResScale, sH / 12), trunkCol);
                fillDisc(smx, tyC - kResScale, std::max(1, haloR / 5), std::max(2, haloR / 4), kIdxOreGold);
                fillDisc(smx, tyC - kResScale, std::max(1, haloR / 9), std::max(1, haloR / 7), kIdxWarmGleam);
                activeFog = savedFog;
              } else if (var == 7 && (cellH % 3u) == 1u && visSpanW >= 14 * kResScale && sH >= 22 * kResScale) {
                // Recessed dungeon iron-barred alcove on corridor side wall
                const int aw = std::clamp(visSpanW / 3, 4 * kResScale, 10 * kResScale);
                const int ah = std::clamp(sH * 36 / 100, 7 * kResScale, 20 * kResScale);
                const int ax = smx - aw / 2;
                const int ay = syT + sH * 30 / 100;
                fillOutlineRect(ax, ay, aw, ah, inkOut, rockMid);
                const int barStep = std::max(2, 2 * kResScale);
                for (int bx = ax + barStep; bx < ax + aw - 1; bx += barStep) {
                  fillRect(bx, ay + 1, 1, ah - 2, kIdxShipDark);
                }
                if (ah >= 10 * kResScale) {
                  drawHLine(ax + 1, ax + aw - 2, ay + ah / 2, kIdxShipDark);
                }
              }
            } else if (!inMine && var == 7 && visSpanW >= 14 * kResScale && sH >= 18 * kResScale && (cellH & 1u) == 0u) {
              // Recessed gothic arrow-slit / dark window slit on surface ruin side wall
              const int sw = std::clamp(visSpanW / 5, 3 * kResScale, 6 * kResScale);
              const int sh = std::clamp(sH / 3, 5 * kResScale, 14 * kResScale);
              fillOutlineRect(smx - sw / 2, syT + sH * 28 / 100, sw, sh, inkOut, rockMid);
              if (lavaWorld) {
                fillRect(smx, syT + sH * 28 / 100 + sh / 2, kResScale, kResScale, accent0);
              }
            }
          }
        }
      }
    }

    // 2. Front-facing cliff / canyon / ruin-mine facade at zNearW (culled if nearer cell is also cliff)
    if (!adjNear) {
      const int xL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
      const int xR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
      const int yTBase = cliffYTopScaled(zNearW, cellTopScale);
      const int yB = std::clamp(cliffYBot(zNearW), 0, H - 1);
      if (xR >= 0 && xL < W && xR >= xL && yTBase < yB) {
        const int faceW = std::max(1, xR - xL);
        const int baseH = std::max(1, yB - yTBase);
        const int cx0 = std::max(clipL, xL);
        const int cx1 = std::min(clipR, xR);

        if (cx0 <= cx1) {
          const int maxLiftBound = std::max(4 * kResScale, (baseH * 14) / 100) + 4;
          const int yRowStart = std::max(0, yTBase - maxLiftBound);
          std::int8_t rowV32[H]{};
          for (int y = yRowStart; y <= yB; ++y) {
            const int dy = y - yTBase;
            // Sample solid beveled stone courses for merlons above yTBase (avoiding row 30..31/0 mortar seam!)
            const int vRaw = (dy >= 0) ? ((dy * 32) / baseH) : (2 + (((-dy) * 28) / baseH));
            rowV32[y] = static_cast<std::int8_t>(vRaw & 31);
          }

          const int leftNeighYT = adjLeft
                                      ? cliffYTopScaled(zNearW, cliffTopScaleFor(tx + ldx, ty + ldy,
                                                                                 variantAt(tx + ldx, ty + ldy)))
                                      : yB + 1;
          const int rightNeighYT = adjRight
                                       ? cliffYTopScaled(zNearW, cliffTopScaleFor(tx + rdx, ty + rdy,
                                                                                  variantAt(tx + rdx, ty + rdy)))
                                       : yB + 1;
          for (int x = cx0; x <= cx1; ++x) {
            if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { continue; }
            const int uPerm = ((x - xL) * 1000) / faceW;
            const int lift = cliffCrownLift(uPerm, faceW, baseH, var, cellH, adjLeft, adjRight);
            const int yT = std::clamp(yTBase - lift, 0, H - 1);
            if (yT > yB) { continue; }
            if ((x == xL && !adjLeft) || (x == xR && !adjRight)) {
              fillRect(x, yT, 1, yB - yT + 1, inkOut);
              continue;
            }
            const int edgeOutlineBot =
                (x == xL && yT < leftNeighYT) ? std::min(yB, leftNeighYT)
              : (x == xR && yT < rightNeighYT) ? std::min(yB, rightNeighYT)
                                               : -1;
            if (edgeOutlineBot >= yT) {
              fillRect(x, yT, 1, edgeOutlineBot - yT + 1, inkOut);
            } else {
              idxBuf[yT * W + x] = activeInk;
            }
            idxBuf[yB * W + x] = activeInk;
            const std::uint8_t colFogMask = colFogBitsForX(x);
            const int uTex = ((uPerm * 64) / 1000 + static_cast<int>((cellH & 3u) * 8u)) & 31;
            const int capDrop = (!inMine && baseH >= 14 * kResScale)
                                    ? ((1 + static_cast<int>((static_cast<unsigned>(x) * 3u + cellH) % 3u)) * kResScale)
                                    : 0;
            const int vineDrop = (!inMine && biomeIdx == 0 && baseH >= 16 * kResScale)
                                     ? (((static_cast<unsigned>(x) * 11u + cellH) % 9u) == 0u
                                            ? (baseH * (24 + static_cast<int>((static_cast<unsigned>(x) * 7u + cellH) % 34u)) / 100)
                                            : 0)
                                     : 0;
            const int icicleDrop = (!inMine && iceWorld && baseH >= 16 * kResScale)
                                       ? (((static_cast<unsigned>(x) * 7u + cellH) % 11u) == 0u
                                              ? ((3 + static_cast<int>((static_cast<unsigned>(x) * 5u + cellH) % 7u)) * kResScale)
                                              : 0)
                                       : 0;

            for (int y = std::max(yT + 1, edgeOutlineBot + 1); y < yB; ++y) {
              const int v32 = rowV32[y];
              const int v16 = v32 >> 1;
              const std::uint8_t st = kStoneWallTex.px[v32][uTex];
              std::uint8_t cIdx = (st == 0u) ? inkOut
                                : iceWorld
                                    ? ((st == 1u) ? rockMid : (st == 2u) ? pathDark : pathHi)
                                : rockWorld
                                    ? ((st == 1u) ? rockDark : (st == 2u) ? rockMid : rockHi)
                                    : ((st == 1u) ? rockDark : (st == 2u) ? rockMid : rockHi);

              if (!inMine && y <= yT + capDrop) {
                if (iceWorld) {
                  cIdx = (y == yT + 1) ? rockHi : rockMid;   // snow cap
                } else if (biomeIdx == 0) {
                  cIdx = (y == yT + 1) ? leaf2 : leaf1;
                } else if (lavaWorld) {
                  cIdx = (y == yT + 1) ? rockHi : rockDark;
                } else {
                  cIdx = (y == yT + 1) ? rockHi : rockMid;
                }
              } else if (!inMine && biomeIdx == 0 && vineDrop > 0 && y <= yT + vineDrop) {
                cIdx = (y == yT + vineDrop) ? leaf0 : (((x + y) & 1) ? leaf2 : leaf1);
              } else if (!inMine && iceWorld && icicleDrop > 0 && y <= yT + capDrop + icicleDrop) {
                cIdx = (y == yT + capDrop + icicleDrop) ? rockHi : rockMid;
              } else if (!inMine && oceanWorld && y >= yB - 3 && ((x + y) & 1) == 0) {
                cIdx = sky3;
              } else if (st == 0u) {
                // World of Xeen chunky rounded mortar joints with biome moss / forge embers
                if ((biomeIdx == 0 || inMine) && !lavaWorld && !iceWorld &&
                    ((uTex * 3 + v32 * 5 + static_cast<int>(cellH)) % 7) == 0) {
                  cIdx = leaf0;
                } else if (lavaWorld && v16 >= 6 && v16 <= 10 && ((uTex + v32) & 3) == 0) {
                  cIdx = accent0;
                } else {
                  cIdx = inkOut;
                }
              } else if (st == 1u && (biomeIdx == 0 || inMine) && !lavaWorld && !iceWorld && !rockWorld &&
                         ((uTex + v32 * 3 + static_cast<int>(cellH)) % 11) == 0) {
                // Dappled green dungeon moss clinging to stone block edges (just like World of Xeen!)
                cIdx = (v32 & 1) ? leaf1 : leaf0;
              } else if (inMine) {
                // Underground the stone sits in shadow: mortar goes to ink,
                // faces to deep rock, and only some crowns catch the lamps.
                cIdx = (st == 0u) ? inkOut
                     : (st == 3u && ((uTex * 3 + v32 * 5 + static_cast<int>(cellH)) % 5) == 0u) ? rockMid
                     : rockDark;
              } else if (inMine && var == 6 && st == 3u && std::abs((v16 * 55) + (uPerm / 4) - 560) < 46) {
                cIdx = ((x + y) & 1) ? accent0 : accent1;  // sparkling mineral vein in dungeon stone
              } else if (!inMine && rockWorld && baseH >= 16 && v16 == 3) {
                cIdx = (((uPerm / 65) & 1) == 0) ? rockHi : rockDark;  // carved sandstone temple frieze
              } else if (!inMine && iceWorld && st == 3u) {
                cIdx = rockHi;
              }

              if (!fogActive) {
                idxBuf[y * W + x] = cIdx;
              } else if (cIdx == inkOut) {
                idxBuf[y * W + x] = activeInk;
              } else {
                const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
                const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
                idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
              }
            }
          }
        }

        const int pOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
        const bool isPortalFront =
            (tx == static_cast<int>(planetSurface_.portalX) &&
             ty == static_cast<int>(planetSurface_.portalY) &&
             fdx == -kDx[pOut] && fdy == -kDy[pOut]);

        // Might & Magic storytelling front-wall dressing (recessed arches, iron grates, glowing rune tablets, vents)
        if (!inMine && !isPortalFront && faceW >= 20 * kResScale && baseH >= 16 * kResScale && d <= 4) {
          const int fcx = (xL + xR) / 2;
          const int fcy = yTBase + baseH * 52 / 100;
          if (var == 7) {
            const unsigned dress = (cellH >> 1) & 3u;
            if (dress == 0u) {
              // Recessed arched dungeon/ruin alcove with iron grate bars & faint inner gleam
              const int aw = std::clamp(faceW * 32 / 100, 7 * kResScale, 22 * kResScale);
              const int ah = std::clamp(baseH * 46 / 100, 8 * kResScale, 26 * kResScale);
              const int ax = fcx - aw / 2;
              const int ay = std::clamp(yB - ah - 1, yTBase + 4 * kResScale, yB - 6 * kResScale);
              // Stone arch surround + dark interior
              fillOutlineRect(ax - 1, ay + 2 * kResScale, aw + 2, ah - 1, inkOut, rockHi);
              fillOutlineRect(ax + kResScale, ay, aw - 2 * kResScale, 4 * kResScale, inkOut, rockHi);
              fillRect(ax, ay + 2 * kResScale, aw, ah - 2 * kResScale, inkOut);
              // Vertical wrought-iron grate bars + cross-bar
              const int stepB = std::max(2 * kResScale, aw / 4);
              for (int bx = ax + 2 * kResScale; bx <= ax + aw - 3 * kResScale; bx += stepB) {
                fillRect(bx, ay + 2 * kResScale, 1, ah - 3 * kResScale, kIdxShipDark);
                if (ah >= 12 * kResScale) { putPx(bx, ay + 4 * kResScale, kIdxShipLight); }
              }
              drawHLine(ax + 1, ax + aw - 2, ay + ah / 2, kIdxShipDark);
              // Faint mysterious gleam inside the barred recess
              if (aw >= 10 * kResScale && ah >= 11 * kResScale) {
                const std::uint8_t glint = lavaWorld ? accent0 : (iceWorld ? accent1 : kIdxWarmGleam);
                fillRect(fcx - kResScale, ay + ah * 3 / 5, kResScale, kResScale, glint);
                fillRect(fcx + kResScale, ay + ah * 3 / 5, kResScale, kResScale, glint);
              }
            } else if (dress == 1u) {
              // Weathered carved stone embrasure / arrow-slit recessed into masonry (pure stone tones, no neon!)
              const int pw = std::clamp(faceW * 20 / 100, 6 * kResScale, 16 * kResScale);
              const int ph = std::clamp(baseH * 28 / 100, 7 * kResScale, 18 * kResScale);
              const int px = fcx - pw / 2;
              const int py = fcy - ph / 2;
              // Beveled stone lintel & sill framing a recessed dark stone niche
              fillOutlineRect(px, py, pw, ph, rockDark, inkOut);
              drawHLine(px - 1, px + pw, py - 1, rockHi);
              drawHLine(px - 1, px + pw, py + ph, rockMid);
              // Recessed arrow-slit opening
              const int slitW = std::max(2, kResScale);
              fillRect(fcx - slitW / 2, py + 2 * kResScale, slitW, std::max(3, ph - 4 * kResScale), inkOut);
              if (pw >= 10 * kResScale && ph >= 10 * kResScale) {
                drawHLine(fcx - 2 * kResScale, fcx + 2 * kResScale, fcy, inkOut);
              }
            } else if (dress == 2u) {
              // Biome-themed wall emblem / banner / brazier grate / drainage arch
              const int bw = std::clamp(faceW * 22 / 100, 5 * kResScale, 13 * kResScale);
              const int bh = std::clamp(baseH * 42 / 100, 7 * kResScale, 22 * kResScale);
              const int bx = fcx - bw / 2;
              const int by = yTBase + std::max(3 * kResScale, baseH * 22 / 100);
              if (lavaWorld) {
                fillOutlineRect(bx, by + bh / 3, bw, std::max(4, bh / 2), accent0, inkOut);
                fillRect(bx + 1, by + bh / 3 + 1, std::max(1, bw - 2), std::max(2, bh / 4), kIdxWarmGleam);
                for (int gx = bx + 2 * kResScale; gx < bx + bw - 1; gx += 2 * kResScale) {
                  fillRect(gx, by + bh / 3, 1, std::max(4, bh / 2), inkOut);
                }
              } else if (oceanWorld) {
                // Weathered coastal stone drainage arch with muted tidal water cascade
                fillOutlineRect(bx, by, bw, std::max(5, bh / 2), inkOut, rockMid);
                fillRect(fcx - kResScale, by + bh / 3, 2 * kResScale + 1, std::max(3, yB - (by + bh / 3) - 1), sky2);
                fillRect(fcx, by + bh / 3, kResScale, std::max(3, yB - (by + bh / 3) - 1), sky3);
              } else {
                const std::uint8_t banCol = (biomeIdx == 0) ? accent0
                                          : iceWorld        ? accent1
                                                            : kIdxOreBody;
                drawHLine(bx - 1, bx + bw, by, kIdxOreGold);
                fillOutlineRect(bx, by + 1, bw, bh, banCol, inkOut);
                fillRect(fcx, by + 3 * kResScale, 1, std::max(2, bh - 6 * kResScale), kIdxWarmGleam);
                if (bw >= 7 * kResScale) { drawHLine(bx + 2 * kResScale, bx + bw - 3 * kResScale, by + bh / 2, kIdxWarmGleam); }
              }
            }
          } else if (var == 4 || var == 5) {
            if (lavaWorld && (cellH & 1u) == 0u && faceW >= 24 * kResScale) {
              const int vx = fcx + (((cellH & 2u) != 0u) ? -faceW / 6 : faceW / 6);
              const int vy = yTBase + baseH * 38 / 100;
              fillDisc(vx, vy, 2 * kResScale, 2 * kResScale, accent0);
              putPx(vx, vy, kIdxWarmGleam);
              for (int py = vy + 1; py < yB - 1; ++py) {
                const int jx = vx + (((py / 3) & 1) ? 1 : 0);
                putPx(jx, py, ((py & 1) == 0) ? accent0 : accent1);
              }
            } else if (oceanWorld && (cellH & 1u) == 0u && faceW >= 24 * kResScale && baseH >= 18 * kResScale) {
              const int gw = std::clamp(faceW * 28 / 100, 6 * kResScale, 16 * kResScale);
              const int gh = std::clamp(baseH * 34 / 100, 5 * kResScale, 14 * kResScale);
              fillOutlineRect(fcx - gw / 2, yB - gh, gw, gh, inkOut, rockDark);
              drawHLine(fcx - gw / 2 + 1, fcx + gw / 2 - 1, yB - 1, sky3);
            } else if (rockWorld && (cellH % 3u) == 0u && faceW >= 24 * kResScale && baseH >= 18 * kResScale) {
              const int aw = std::clamp(faceW * 28 / 100, 7 * kResScale, 16 * kResScale);
              const int ah = std::clamp(baseH * 36 / 100, 6 * kResScale, 15 * kResScale);
              const int ax = fcx - aw / 2;
              const int ay = yB - ah;
              fillRect(ax + 2 * kResScale, ay, aw - 4 * kResScale, 2 * kResScale, inkOut);
              fillRect(ax + kResScale, ay + 2 * kResScale, aw - 2 * kResScale, 2 * kResScale, inkOut);
              fillRect(ax, ay + 4 * kResScale, aw, std::max(2, ah - 4 * kResScale), inkOut);
              drawHLine(ax + 2 * kResScale, ax + aw - 3 * kResScale, ay - 1, rockHi);
              drawHLine(ax + kResScale, ax + aw - 2 * kResScale, ay + 1, rockDark);
              if (aw >= 10 * kResScale && ah >= 9 * kResScale) {
                fillRect(fcx, ay + ah / 2, kResScale, kResScale, accent0);
              }
            }
          }
        }

        // Shored wooden mine-shaft timbers + arched vault header + glowing wall torches on underground var == 7 bays
        if (inMine && var == 7 && !isPortalFront && faceW >= 12 * kResScale && baseH >= 12 * kResScale) {
          const int postW = std::clamp(faceW / 11, 2 * kResScale, 4 * kResScale);
          const int beamH = std::clamp(baseH / 13, 2 * kResScale, 4 * kResScale);
          fillOutlineRect(xL + 1, yTBase + 1, postW + 2, baseH - 1, trunkCol, inkOut);
          fillOutlineRect(xR - postW - 2, yTBase + 1, postW + 2, baseH - 1, trunkCol, inkOut);
          fillOutlineRect(xL + 1, yTBase + 1, faceW - 2, beamH + 2, pathHi, inkOut);
          // Diagonal timber knee-braces + iron corner plates
          if (faceW >= 22 * kResScale) {
            const int braceLen = std::min(5 * kResScale, faceW / 8);
            for (int kb = 0; kb < braceLen; ++kb) {
              putPx(xL + postW + 2 + kb, yTBase + beamH + braceLen - kb, trunkCol);
              putPx(xR - postW - 3 - kb, yTBase + beamH + braceLen - kb, trunkCol);
            }
            fillRect(xL + postW + 1, yTBase + beamH + 1, kResScale, kResScale, kIdxShipDark);
            fillRect(xR - postW - 2, yTBase + beamH + 1, kResScale, kResScale, kIdxShipDark);
          }
          // Glowing wall torch sconce with warm dithered radial halo contained inside timber bay
          if ((cellH & 1u) == 0u && faceW >= 18 * kResScale && baseH >= 18 * kResScale && d <= 4) {
            const int tcx = (xL + xR) / 2;
            const int tcy = yTBase + baseH * 48 / 100;
            const int maxR = std::max(2 * kResScale, tcy - (yTBase + beamH + 4 * kResScale));
            const int haloR = std::min(std::clamp(faceW / 5, 3 * kResScale, 10 * kResScale), maxR);
            const int savedFog = activeFog;
            activeFog = std::max(0, savedFog - 12);
            fillDitherDisc(tcx, tcy - kResScale, haloR, haloR, rockMid, pathHi, 7);
            fillDitherDisc(tcx, tcy - kResScale, std::max(2, haloR / 2), std::max(2, haloR / 2), pathHi, kIdxOreGold, 6);
            fillRect(tcx - kResScale, tcy + kResScale, 2 * kResScale + 1, std::max(3 * kResScale, baseH / 8), inkOut);
            fillRect(tcx, tcy + 2 * kResScale, std::max(1, kResScale / 2), std::max(2 * kResScale, baseH / 10), trunkCol);
            fillDisc(tcx, tcy - kResScale, std::max(1, haloR / 5), std::max(2, haloR / 4), kIdxOreGold);
            fillDisc(tcx, tcy - kResScale, std::max(1, haloR / 9), std::max(1, haloR / 7), kIdxWarmGleam);
            activeFog = savedFog;
          } else if ((cellH & 1u) != 0u && faceW >= 24 * kResScale && baseH >= 20 * kResScale && d <= 4) {
            // Alternate mine bay: barred dungeon niche / recessed drainage grate
            const int aw = std::clamp(faceW * 30 / 100, 7 * kResScale, 18 * kResScale);
            const int ah = std::clamp(baseH * 36 / 100, 7 * kResScale, 18 * kResScale);
            const int ax = (xL + xR) / 2 - aw / 2;
            const int ay = yTBase + baseH * 34 / 100;
            fillOutlineRect(ax, ay, aw, ah, inkOut, rockHi);
            const int barStep = std::max(2, 3 * kResScale);
            for (int bx = ax + 2 * kResScale; bx < ax + aw - 1; bx += barStep) {
              fillRect(bx, ay + 1, 1, ah - 2, kIdxShipDark);
            }
            drawHLine(ax + 1, ax + aw - 2, ay + ah / 2, kIdxShipDark);
          }
        }

        // Enterable PC-98 timbered mine entrance (surface) or daylight exit shaft (mine)
        if (isPortalFront && faceW >= 9 * kResScale && baseH >= 8 * kResScale) {
          const int pW = std::max(8 * kResScale, faceW * 48 / 100);
          const int pTopTarget = hy - static_cast<int>(std::lround(kProjY * 0.28f / zNearW));
          const int pY = std::clamp(pTopTarget, yTBase + 3 * kResScale, yB - 6 * kResScale);
          const int pH = std::max(6 * kResScale, yB - pY);
          const int pX = xL + (faceW - pW) / 2;
          const int postW = std::max(2 * kResScale, std::min(5 * kResScale, pW / 6));
          const int lintelH = std::max(2 * kResScale, std::min(5 * kResScale, pH / 6));
          fillOutlineRect(pX - 2 * kResScale, pY - lintelH, pW + 4 * kResScale, lintelH + 2, pathHi, inkOut);
          if (!inMine) {
            fillRect(pX, pY, pW, pH, inkOut);
            // Deep recessed timber struts inside the mine tunnel
            if (pW >= 14 && pH >= 12) {
              const int inW = pW - postW * 2 - 4;
              const int inH = pH - 4;
              if (inW >= 6 && inH >= 6) {
                fillOutlineRect(pX + postW + 2, pY + 2, inW, inH, inkOut, rockDark);
              }
            }
            fillOutlineRect(pX, pY, postW + 1, pH, trunkCol, inkOut);
            fillOutlineRect(pX + pW - postW - 1, pY, postW + 1, pH, trunkCol, inkOut);
            // Diagonal timber knee-braces + warm hanging brass mine lantern
            if (pW >= 12 && pH >= 10) {
              putPx(pX + postW + 1, pY + 2, pathHi);
              putPx(pX + pW - postW - 2, pY + 2, pathHi);
              const int lx = pX + pW / 2;
              const int ly = pY + std::max(2, std::min(12, pH / 5));
              const int lHalo = std::clamp(pW / 11, 2, 7);
              fillRect(lx, pY, 1, ly - pY, kIdxShipDark);
              fillDitherDisc(lx, ly + 2, lHalo, lHalo, inkOut, kIdxOreBody, 6);
              fillOutlineRect(lx - 2, ly, 5, 5, kIdxOreGold, inkOut);
              fillRect(lx - 1, ly + 1, 3, 3, kIdxWarmGleam);
              drawHLine(lx - 2, lx + 2, ly - 1, kIdxShipDark);
            }
          } else {
            // Glowing surface daylight shining down the exit adit through cavern gloom
            const int savedFog = activeFog;
            activeFog = std::max(0, savedFog - 18);
            const int inX0 = pX + postW + 1;
            const int inX1 = pX + pW - postW - 1;
            for (int py = pY; py < yB; ++py) {
              const float vFrac = static_cast<float>(py - pY) / static_cast<float>(std::max(1, pH));
              for (int px = inX0; px < inX1; ++px) {
                if (py >= yB - std::max(2, pH / 6)) {
                  putPx(px, py, kIdxWarmGleam);
                } else {
                  bayerPx(px, py, vFrac, sky2, sky3);
                }
              }
            }
            activeFog = savedFog;
            fillOutlineRect(pX, pY, postW + 1, pH, trunkCol, inkOut);
            fillOutlineRect(pX + pW - postW - 1, pY, postW + 1, pH, trunkCol, inkOut);
            putPx(pX, yB, inkOut);
          }
          if (d == 1 && s == 0) {
            const int ux0 = std::clamp(pX - 1, 0, W);
            const int ux1 = std::clamp(pX + pW + 1, 0, W);
            const int uy0 = std::clamp(pY - lintelH, 0, H);
            const int uy1 = std::clamp(yB, 0, H);
            if (ux1 > ux0 && uy1 > uy0) {
              planetSurface_.frontKind = 8;
              planetSurface_.frontArg = inMine ? 0 : 1;
              planetSurface_.frontUv = Area{
                  static_cast<float>(ux0) / static_cast<float>(W),
                  static_cast<float>(uy0) / static_cast<float>(H),
                  static_cast<float>(ux1 - ux0) / static_cast<float>(W),
                  static_cast<float>(uy1 - uy0) / static_cast<float>(H)};
            }
          }
        }
      }
    }
  };

  // Authentic PC-98 Clouds of Xeen / Might & Magic III sloped craggy mountain & geological mass
  // (replaces rectangular outdoor cliff boxes for var 4, 5, 6 — wide rocky base, tapered jagged summit,
  // sunlit left ridge slope vs. deep craggy shadowed right slope using kCragRockTex!)
  auto drawMountainCell = [&](int tx, int ty, int d, int s, int var) {
    const unsigned cellH = static_cast<unsigned>(tx) * 73u ^
                           static_cast<unsigned>(ty) * 151u ^
                           (pSeed >> 5) ^ (static_cast<unsigned>(var) * 29u);
    const bool fogActive = (activeFog > 0);
    const bool fogHighBand = (activeFog > 16);
    const int mtnThr16 = fogHighBand ? std::min(16, activeFog - 16) : activeFog;
    auto colFogBitsForX = [&](int x) -> std::uint8_t {
      if (!fogActive) { return 0u; }
      const int rx = x & 3;
      return static_cast<std::uint8_t>(
          (((kFogMasks.row[mtnThr16][0] >> rx) & 1u) << 0) |
          (((kFogMasks.row[mtnThr16][1] >> rx) & 1u) << 1) |
          (((kFogMasks.row[mtnThr16][2] >> rx) & 1u) << 2) |
          (((kFogMasks.row[mtnThr16][3] >> rx) & 1u) << 3));
    };

    // Peripheral d == 0 looming sloped mountain flank at left/right edge (tucked outside road curb!)
    if (d == 0) {
      if (std::abs(s) != 1) { return; }
      const bool onLeft = (s < 0);
      const bool onRoad = isPathTile(planetSurface_.px, planetSurface_.py);
      const int flankW = (onRoad ? 14 : 26) * kResScale;
      const int xStart = onLeft ? 0 : (W - flankW);
      const int xEnd   = onLeft ? (flankW - 1) : (W - 1);
      int prevYT = -1;
      for (int x = xStart; x <= xEnd; ++x) {
        const int frac = onLeft ? ((x * 1000) / flankW) : (((xEnd - x) * 1000) / flankW);
        // Organic craggy stepped mountain shoulder (not a straight ruler diagonal!)
        const int cragLedge = (frac > 300 && frac < 520) ? (-7 * kResScale) : (frac > 680 ? (5 * kResScale) : 0);
        const int jag = (static_cast<int>(((static_cast<unsigned>(x / kResScale) * 7u + cellH) % 5u)) - 2) * kResScale;
        const int yT = std::clamp(8 * kResScale + (frac * (hy + 22 * kResScale)) / 1000 + cragLedge + jag, 0, H - 1);
        const int yB = std::clamp(H - 1 - (frac * 18 * kResScale) / 1000, yT, H - 1);
        const int outlineBot = (prevYT >= 0 && yT > prevYT + 1) ? yT : yT;
        const int outlineTop = (prevYT >= 0 && prevYT > yT + 1) ? yT : ((prevYT >= 0) ? std::min(yT, prevYT) : yT);
        for (int oy = outlineTop; oy <= outlineBot; ++oy) {
          putPx(x, oy, inkOut);
        }
        prevYT = yT;
        const int uTex = (x + static_cast<int>(cellH & 31u)) & 31;
        const bool shadowSide = !onLeft ? (frac < 580) : (frac > 480);
        for (int y = yT + 1; y <= yB; ++y) {
          const int vTex = (y + static_cast<int>((cellH >> 4) & 31u)) & 31;
          const std::uint8_t cr = kCragRockTex.px[vTex][uTex];
          std::uint8_t cIdx = shadowSide
                                  ? ((cr <= 1u) ? inkOut : (cr == 2u) ? rockDark : rockMid)
                                  : ((cr == 0u) ? inkOut : (cr == 1u) ? rockDark : (cr == 2u) ? rockMid : rockHi);
          if (y == yB) { cIdx = inkOut; }
          else if (iceWorld && !shadowSide && cr == 3u) { cIdx = accent1; }
          idxBuf[y * W + x] = cIdx;
        }
      }
      return;
    }

    const float zNearW = std::max(0.50f, static_cast<float>(d) - 0.36f);
    const int clipL = std::max(0, static_cast<int>(rowX0[d]));
    const int clipR = std::min(W - 1, static_cast<int>(rowX1[d]));
    const int baseL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
    const int baseR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
    const int tileW = std::max(6, baseR - baseL);
    // Only expand overlapping mountain skirts toward adjacent blocking tiles — NEVER spill over open roads!
    const bool adjBlockL = (tileAt(tx + ldx, ty + ldy) == 1);
    const bool adjBlockR = (tileAt(tx + rdx, ty + rdy) == 1);
    const int skirtL = adjBlockL ? ((tileW * 26) / 100 + 2) : 0;
    const int skirtR = adjBlockR ? ((tileW * 26) / 100 + 2) : 0;
    const int xL = baseL - skirtL;
    const int xR = baseR + skirtR;
    const int spanW = std::max(6, xR - xL);
    const int yB = std::clamp(cliffYBot(zNearW) + 1, hy + 2, H - 1);

    // Tall dramatic Clouds of Xeen mountain height reaching boldly into the bright sky
    const bool border = isBorderTile(tx, ty);
    float peakScale = (var == 5) ? (0.94f + static_cast<float>(cellH % 4u) * 0.08f)
                    : (var == 6) ? (0.76f + static_cast<float>(cellH % 3u) * 0.09f)
                                 : (0.64f + static_cast<float>(cellH % 4u) * 0.08f);
    if (border) { peakScale += 0.14f; }
    if (oceanWorld) { peakScale *= 0.86f; }
    const int yPeak = std::clamp(cliffYTopScaled(zNearW, peakScale), 3, yB - 6);
    const int peakH = std::max(6, yB - yPeak);

    // Asymmetric primary summit + secondary rocky shoulder
    const int peakU = 380 + static_cast<int>((cellH % 5u) * 60u);  // 380..620
    const int shldU = (peakU < 500) ? (peakU + 230) : (peakU - 230);
    const int shldH = 560 + static_cast<int>(((cellH >> 3) % 3u) * 75u);  // permille of peakH

    auto profilePerm = [&](int u) -> int {
      if (u <= 0 || u >= 1000) { return 0; }
      int h = 0;
      if (rockWorld) {
        // Monument Valley stepped flat-topped canyon mesa & overhanging caprock plateau
        if (u < 190) {
          h = (u * 520) / 190;
        } else if (u < 290) {
          h = 520 + ((u - 190) * 440) / 100;
        } else if (u <= 710) {
          const int jag = static_cast<int>(((static_cast<unsigned>(u / 48) * 7u + cellH) % 5u)) * 18 - 30;
          h = std::clamp(960 + jag, 850, 1000);
        } else if (u < 810) {
          h = 520 + ((810 - u) * 440) / 100;
        } else {
          h = ((1000 - u) * 520) / 190;
        }
      } else if (iceWorld) {
        // Razor-sharp jagged twin-crested glacial peak / iceberg
        const int d1 = std::abs(u - peakU);
        const int span1 = (u < peakU) ? peakU : (1000 - peakU);
        const int h1 = std::max(0, 1000 - (d1 * 1000) / std::max(1, span1));
        const int d2 = std::abs(u - shldU);
        const int h2 = std::max(0, shldH - (d2 * shldH) / 340);
        const int notch = ((u / 55 + static_cast<int>(cellH)) & 1) ? 35 : -20;
        h = std::clamp(std::max(h1, h2) + (h1 > 200 ? notch : 0), 0, 1000);
      } else if (lavaWorld) {
        // Steep volcanic caldera cone with craggy summit rim notch
        const int d1 = std::abs(u - peakU);
        const int span1 = (u < peakU) ? peakU : (1000 - peakU);
        int h1 = std::max(0, 1000 - (d1 * 1020) / std::max(1, span1));
        if (d1 < 85 && var == 5) { h1 -= (85 - d1) * 2; }  // volcanic crater rim dip
        const int d2 = std::abs(u - shldU);
        const int h2 = std::max(0, (shldH - 60) - (d2 * shldH) / 360);
        h = std::max(h1, h2);
      } else {
        // Classic PC-98 Clouds of Xeen craggy Alpine mountain / coastal crag with stepped shoulders
        const int d1 = std::abs(u - peakU);
        const int span1 = (u < peakU) ? peakU : (1000 - peakU);
        // Slightly convex craggy slope (broader upper shoulder like PC-98 Clouds of Xeen!)
        const int lin1 = std::max(0, 1000 - (d1 * 1000) / std::max(1, span1));
        const int h1 = std::min(1000, (lin1 * 1160) / 1000 - (lin1 * lin1) / 6200);
        const int d2 = std::abs(u - shldU);
        const int h2 = std::max(0, shldH - (d2 * shldH) / 390);
        const int cragStep = static_cast<int>(((static_cast<unsigned>(u / 62) * 11u + cellH) % 5u)) * 22 - 40;
        h = std::clamp(std::max(h1, h2) + ((std::max(h1, h2) > 180) ? cragStep : 0), 0, 1000);
      }
      return h;
    };

    const int cx0 = std::max(clipL, xL);
    const int cx1 = std::min(clipR, xR);
    if (cx0 > cx1) { return; }

    int prevYT = -1;
    const int texUOff = static_cast<int>((cellH * 7u) & 31u);
    const int texVOff = static_cast<int>((cellH * 13u) & 31u);
    for (int x = cx0; x <= cx1; ++x) {
      if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { prevYT = -1; continue; }
      const int uPerm = ((x - xL) * 1000) / spanW;
      const int hPerm = profilePerm(uPerm);
      if (hPerm < 35) { prevYT = -1; continue; }
      const int colH = std::max(2, (peakH * hPerm) / 1000);
      // Slight rocky foot arch so outer edges tuck softly into terrain
      const int footLift = (uPerm < 120 || uPerm > 880) ? 1 : 0;
      const int yColB = std::min(H - 1, yB - footLift);
      const int yT = std::clamp(yColB - colH, 0, yColB);
      if (yT >= yColB) { prevYT = -1; continue; }

      // Crisp unbroken PC-98 black ink contour along steep jagged mountain slopes
      const int stepBot = (prevYT >= 0 && yT > prevYT + 1) ? yT : (yT + 1);
      const int stepTop = (prevYT >= 0 && prevYT > yT + 1) ? yT : ((prevYT >= 0) ? std::min(yT, prevYT) : yT);
      for (int oy = stepTop; oy <= std::min(yColB, stepBot); ++oy) {
        idxBuf[oy * W + x] = activeInk;
      }
      prevYT = yT;
      idxBuf[yColB * W + x] = activeInk;

      const std::uint8_t colFogMask = colFogBitsForX(x);
      // Scale crag texture density proportionally + per-32px cell mirror/jitter to eliminate tiling rhythm!
      const int uScale = std::clamp(spanW, 32 * kResScale, 112 * kResScale);
      const int uRaw = (uPerm * uScale) / 1000 + texUOff;
      const int vScale = std::clamp(peakH, 28 * kResScale, 64 * kResScale);
      const std::uint32_t invPeakH =
          ((1u << 20) + static_cast<std::uint32_t>(peakH) - 1u) / static_cast<std::uint32_t>(peakH);
      const int tileU = uRaw >> 5;

      for (int y = yT + 1; y < yColB; ++y) {
        const int vFromPeak = static_cast<int>((static_cast<std::uint32_t>((y - yPeak) << 5) * invPeakH) >> 20);
        const int vRaw = ((y - yPeak) * vScale) / std::max(1, peakH) + texVOff;
        const int tileV = vRaw >> 5;
        const int uLocal = ((tileU ^ tileV) & 1) ? (31 - (uRaw & 31)) : (uRaw & 31);
        const int uTex = (uLocal + ((tileV * 13) & 31)) & 31;
        const int vTex = (vRaw + ((tileU * 7) & 31)) & 31;
        const int v16 = std::clamp(vFromPeak >> 1, 0, 15);
        // Winding 3D diagonal mountain spine ridge splitting sunlit left face from shadowed right face!
        const int ridgeWobble = (((vFromPeak / 4 + static_cast<int>(cellH & 3u)) & 1) ? 35 : -30);
        const int ridgeU = peakU + ((vFromPeak - 4) * ((cellH & 1u) ? 6 : -4)) + ridgeWobble;
        const bool isRidgeLine = (std::abs(uPerm - ridgeU) < 22) && (vFromPeak >= 2);
        const bool shadowSide = (uPerm > ridgeU);
        const std::uint8_t cr = kCragRockTex.px[vTex][uTex];

        std::uint8_t cIdx = rockMid;
        if (isRidgeLine) {
          cIdx = (cr == 0u || (vFromPeak & 1)) ? inkOut : rockDark;
        } else if (rockWorld) {
          // Terracotta canyon strata warped across sunlit/shadowed mesa faces
          const int sv = (v16 + ((uPerm / 140) & 1)) & 15;
          if (shadowSide) {
            cIdx = (cr == 0u) ? inkOut : (sv == 3 || sv == 9) ? rockMid : rockDark;
          } else {
            if (cr == 0u) { cIdx = rockDark; }
            else if (sv == 2 || sv == 3 || sv == 8 || cr == 3u) { cIdx = rockHi; }
            else if (sv == 6 || sv == 11) { cIdx = pathMid; }
            else { cIdx = (cr == 1u) ? rockDark : rockMid; }
          }
        } else if (iceWorld) {
          // Razor crystalline glacial peak: bright snow/cyan left slope, cobalt dark right slope
          if (shadowSide) {
            cIdx = (cr == 0u) ? inkOut : (cr == 3u && v16 < 10) ? rockMid : rockDark;
          } else {
            cIdx = (cr == 0u) ? rockDark : (cr == 3u || v16 < 4) ? accent1 : (cr == 2u ? rockHi : rockMid);
          }
        } else if (lavaWorld) {
          // Dark volcanic basalt crag with glowing molten magma river winding near the ridge
          const bool magmaVein = (std::abs(uPerm - (ridgeU + 30)) < 26) && (vFromPeak >= 5);
          if (magmaVein && ((y + x) & 1) == 0) {
            cIdx = (vFromPeak < 12) ? kIdxWarmGleam : ((y & 2) ? accent1 : accent0);
          } else if (shadowSide) {
            cIdx = (cr <= 1u) ? inkOut : (cr == 3u ? rockMid : rockDark);
          } else {
            cIdx = (cr == 0u) ? inkOut : (cr == 1u) ? rockDark : (cr == 2u) ? rockMid : rockHi;
          }
        } else {
          // Classic PC-98 Clouds of Xeen high-contrast craggy Alpine / Sea mountain!
          if (!shadowSide) {
            cIdx = (cr == 0u) ? inkOut
                 : (cr == 1u) ? rockDark
                 : (cr == 2u) ? rockMid
                              : rockHi;
            if (y <= yT + 2 && cr > 0u) { cIdx = rockHi; }
          } else {
            cIdx = (cr <= 1u) ? inkOut
                 : (cr == 2u) ? rockDark
                              : ((kBayer4[y & 3][x & 3] < 10) ? rockMid : rockDark);
          }
          // Biome organic footing accents (surf foam on Ocean, mossy clumps on Temperate lower slope)
          if (oceanWorld && y >= yColB - 3 && ((x + y) & 1) == 0) {
            cIdx = sky3;
          } else if (biomeIdx == 0 && y >= yColB - std::max(2, peakH / 6) && cr >= 2u && ((x + y) & 1) == 0) {
            cIdx = shadowSide ? leaf0 : leaf1;
          }
        }
        // Mineral vein sparkle on var == 6 mountains
        if (var == 6 && !shadowSide && cr == 3u && v16 >= 6 && v16 <= 11 && ((x + y) & 1) == 0) {
          cIdx = accent0;
        }

        if (!fogActive) {
          idxBuf[y * W + x] = cIdx;
        } else if (cIdx == inkOut) {
          idxBuf[y * W + x] = activeInk;
        } else {
          const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
          const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
          idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
        }
      }
    }

    // Grounded base scrub/boulders nestled at foot of Temperate / Rock mountains
    if (!oceanWorld && spanW >= 24 && d <= 4) {
      if (biomeIdx == 0) {
        drawScrubBush(xL + spanW * 24 / 100, yB, std::min(1.0f, static_cast<float>(tileW) / 85.0f));
        drawScrubBush(xL + spanW * 78 / 100, yB, std::min(0.9f, static_cast<float>(tileW) / 95.0f));
      }
    }
  };

  // Authentic PC-98 Clouds of Xeen domed craggy rock cave entrance (replaces rectangular portal box on surface!)
  auto drawCaveMoundPortal = [&](int tx, int ty, int d, int s) {
    if (d == 0) {
      drawMountainCell(tx, ty, d, s, 5);
      return;
    }
    const float zNearW = std::max(0.50f, static_cast<float>(d) - 0.42f);
    const int clipL = std::max(0, static_cast<int>(rowX0[d]));
    const int clipR = std::min(W - 1, static_cast<int>(rowX1[d]));
    const int baseL = cliffX(zNearW, static_cast<float>(s) - 0.50f);
    const int baseR = cliffX(zNearW, static_cast<float>(s) + 0.50f);
    const int tileW = std::max(8, baseR - baseL);
    const int skirtL = (tileAt(tx + ldx, ty + ldy) == 1) ? (tileW * 16 / 100) : 0;
    const int skirtR = (tileAt(tx + rdx, ty + rdy) == 1) ? (tileW * 16 / 100) : 0;
    const int xL = baseL - skirtL;
    const int xR = baseR + skirtR;
    const int spanW = std::max(8, xR - xL);
    const int yB = std::clamp(cliffYBot(zNearW) + 1, hy + 2, H - 1);
    const int yPeak = std::clamp(cliffYTopScaled(zNearW, 0.70f), 6, yB - 10);
    const int peakH = std::max(10, yB - yPeak);

    const bool fogActive = (activeFog > 0);
    const bool fogHighBand = (activeFog > 16);
    const int thr16 = fogHighBand ? std::min(16, activeFog - 16) : activeFog;
    auto colFogBitsForX = [&](int x) -> std::uint8_t {
      if (!fogActive) { return 0u; }
      const int rx = x & 3;
      return static_cast<std::uint8_t>(
          (((kFogMasks.row[thr16][0] >> rx) & 1u) << 0) |
          (((kFogMasks.row[thr16][1] >> rx) & 1u) << 1) |
          (((kFogMasks.row[thr16][2] >> rx) & 1u) << 2) |
          (((kFogMasks.row[thr16][3] >> rx) & 1u) << 3));
    };

    // Draw broad craggy domed boulder hill (Clouds of Xeen PC-98 cave mound silhouette!)
    const int cx0 = std::max(clipL, xL);
    const int cx1 = std::min(clipR, xR);
    int prevYT = -1;
    for (int x = cx0; x <= cx1; ++x) {
      if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { prevYT = -1; continue; }
      const int uPerm = ((x - xL) * 1000) / spanW;
      const int du = std::abs(uPerm - 500);
      // Rounded craggy dome profile (steep sides, broad boulder crown)
      const int domeSq = std::max(0, 250000 - du * du);
      int hPerm = (domeSq * 1000) / 250000;
      const int bump = static_cast<int>(((static_cast<unsigned>(uPerm / 70) * 9u) % 5u)) * 22 - 35;
      hPerm = std::clamp(hPerm + ((hPerm > 220) ? bump : 0), 0, 1000);
      if (hPerm < 45) { prevYT = -1; continue; }
      const int yT = std::clamp(yB - std::max(3, (peakH * hPerm) / 1000), 0, yB - 1);
      const int stepTop = (prevYT >= 0) ? std::min(yT, prevYT) : yT;
      const int stepBot = (prevYT >= 0 && yT > prevYT + 1) ? yT : (yT + 1);
      for (int oy = stepTop; oy <= std::min(yB, stepBot); ++oy) {
        idxBuf[oy * W + x] = activeInk;
      }
      prevYT = yT;
      idxBuf[yB * W + x] = activeInk;
      const std::uint8_t colFogMask = colFogBitsForX(x);
      const int uScale = std::clamp(spanW, 32 * kResScale, 104 * kResScale);
      const int uRaw = (uPerm * uScale) / 1000;
      const int tileU = uRaw >> 5;
      const int vScale = std::clamp(peakH, 28 * kResScale, 56 * kResScale);
      for (int y = yT + 1; y < yB; ++y) {
        const int vRaw = ((y - yPeak) * vScale) / std::max(1, peakH);
        const int tileV = vRaw >> 5;
        const int uLocal = ((tileU ^ tileV) & 1) ? (31 - (uRaw & 31)) : (uRaw & 31);
        const int uTex = (uLocal + ((tileV * 13) & 31)) & 31;
        const int vTex = (vRaw + ((tileU * 7) & 31)) & 31;
        const std::uint8_t cr = kCragRockTex.px[vTex][uTex];
        const bool shadeRight = (uPerm > 580);
        std::uint8_t cIdx = shadeRight
                                ? ((cr <= 1u) ? inkOut : (cr == 2u) ? rockDark : rockMid)
                                : ((cr == 0u) ? inkOut : (cr == 1u) ? rockDark : (cr == 2u) ? rockMid : rockHi);
        if (!fogActive) {
          idxBuf[y * W + x] = cIdx;
        } else if (cIdx == inkOut) {
          idxBuf[y * W + x] = activeInk;
        } else {
          const std::uint8_t mid = static_cast<std::uint8_t>(cIdx | kMidFogBit);
          const bool mBit = ((colFogMask >> (y & 3)) & 1u) != 0u;
          idxBuf[y * W + x] = fogHighBand ? (mBit ? fogTargetIdx : mid) : (mBit ? mid : cIdx);
        }
      }
    }

    // Timber-framed dark cave entrance carved into the front face of the craggy mound!
    const int pOut = static_cast<int>(planetSurface_.portalOutDir & 3u);
    const bool isPortalFront = (fdx == -kDx[pOut] && fdy == -kDy[pOut]);
    if (isPortalFront && tileW >= 10 && peakH >= 10) {
      const int pW = std::max(8 * kResScale, tileW * 44 / 100);
      const int pTopTarget = hy - static_cast<int>(std::lround(kProjY * 0.28f / zNearW));
      const int pY = std::clamp(pTopTarget, yPeak + std::max(4 * kResScale, peakH / 4), yB - 6 * kResScale);
      const int pH = std::max(6 * kResScale, yB - pY);
      const int pX = (xL + xR - pW) / 2;
      const int postW = std::max(2 * kResScale, std::min(5 * kResScale, pW / 6));
      const int lintelH = std::max(2 * kResScale, std::min(5 * kResScale, pH / 6));
      // Heavy rough-hewn timber crossbeam lintel + black cave maw + interior support frame
      fillOutlineRect(pX - 2 * kResScale, pY - lintelH, pW + 4 * kResScale, lintelH + 2, pathHi, inkOut);
      fillRect(pX, pY, pW, pH, inkOut);
      if (pW >= 14 * kResScale && pH >= 12 * kResScale) {
        const int inW = pW - postW * 2 - 4 * kResScale;
        const int inH = pH - 4 * kResScale;
        if (inW >= 6 && inH >= 6) {
          fillOutlineRect(pX + postW + 2 * kResScale, pY + 2 * kResScale, inW, inH, inkOut, rockDark);
        }
      }
      fillOutlineRect(pX, pY, postW + 1, pH, trunkCol, inkOut);
      fillOutlineRect(pX + pW - postW - 1, pY, postW + 1, pH, trunkCol, inkOut);
      if (pW >= 12 * kResScale && pH >= 10 * kResScale) {
        fillRect(pX + postW + 1, pY + 2 * kResScale, kResScale, kResScale, pathHi);
        fillRect(pX + pW - postW - 2, pY + 2 * kResScale, kResScale, kResScale, pathHi);
        const int lx = pX + pW / 2;
        const int ly = pY + std::max(2 * kResScale, std::min(12 * kResScale, pH / 5));
        const int lHalo = std::clamp(pW / 11, 2 * kResScale, 7 * kResScale);
        fillRect(lx, pY, 1, ly - pY, kIdxShipDark);
        fillDitherDisc(lx, ly + 2 * kResScale, lHalo, lHalo, inkOut, kIdxOreBody, 6);
        fillOutlineRect(lx - 2 * kResScale, ly, 4 * kResScale + 1, 4 * kResScale + 1, kIdxOreGold, inkOut);
        fillRect(lx - kResScale, ly + kResScale, 2 * kResScale + 1, 2 * kResScale + 1, kIdxWarmGleam);
        drawHLine(lx - 2 * kResScale, lx + 2 * kResScale, ly - 1, kIdxShipDark);
      }
      if (d == 1 && s == 0) {
        const int ux0 = std::clamp(pX - 1, 0, W);
        const int ux1 = std::clamp(pX + pW + 1, 0, W);
        const int uy0 = std::clamp(pY - lintelH, 0, H);
        const int uy1 = std::clamp(yB, 0, H);
        if (ux1 > ux0 && uy1 > uy0) {
          planetSurface_.frontKind = 8;
          planetSurface_.frontArg = 1;
          planetSurface_.frontUv = Area{
              static_cast<float>(ux0) / static_cast<float>(W),
              static_cast<float>(uy0) / static_cast<float>(H),
              static_cast<float>(ux1 - ux0) / static_cast<float>(W),
              static_cast<float>(uy1 - uy0) / static_cast<float>(H)};
        }
      }
    }
  };

  // Back-to-front painter's algorithm across visible cells (culled by vis[2] + depthColOcc)
  for (int d = maxVisD; d >= 0; --d) {
    activeFog = static_cast<int>(inMine ? kMineDepthFog[d] : kDepthFog[d]);
    activeInk = inkForFog(activeFog);
    const float zMid = (d == 0) ? 0.54f : static_cast<float>(d);
    const int gyMid = yAtZ(zMid);
    const float sTile = ((d == 0) ? 1.35f : (1.02f / zMid)) * static_cast<float>(kResScale);
    const float htMid = halfTileAtZ(zMid);
    const int maxS = d + 1;

    for (int k = maxS; k >= 0; --k) {
      for (int sidePass = 0; sidePass < (k == 0 ? 1 : 2); ++sidePass) {
        const int s = (k == 0) ? 0 : (sidePass == 0 ? -k : k);
        if (d == 0 && s == 0) { continue; }
        if (!isVis(d, s)) { continue; }
        const int tx = planetSurface_.px + fdx * d + rdx * s;
        const int ty = planetSurface_.py + fdy * d + rdy * s;
        if (tx < 0 || tx >= N || ty < 0 || ty >= N) { continue; }
        const std::uint8_t tile = tileAt(tx, ty);
        const int var = variantAt(tx, ty);
        const float tcxMid = tileCenterAtZ(s, zMid);

        if (tile == 1) {
          if (inMine) {
            drawCliffCell(tx, ty, d, s, var);
            continue;
          }

          // A blocked surface cell fills its square: draw its exposed faces
          // on tile edges instead of centering disconnected props within it.
          const float nearZ = d == 0 ? 0.35f : static_cast<float>(d) - 0.5f;
          const float farZ = static_cast<float>(d) + 0.5f;
          const bool side = (s < 0 && tileAt(tx + rdx, ty + rdy) != 1u) ||
                            (s > 0 && tileAt(tx + ldx, ty + ldy) != 1u);
          const bool front = d > 0 && tileAt(tx - fdx, ty - fdy) != 1u;
          const bool vegetation = biomeIdx == 0 && var < 4;
          for (int face = 0; face < 2; ++face) {
            if ((face == 0 && !side) || (face == 1 && !front)) { continue; }
            int x0, x1, top0, top1, bottom0, bottom1;
            if (face == 0) {
              const float lat = static_cast<float>(s) + (s < 0 ? 0.5f : -0.5f);
              const int nx = cliffX(nearZ, lat), fx = cliffX(farZ, lat);
              const int nt = cliffYTopScaled(nearZ, 0.85f), ft = cliffYTopScaled(farZ, 0.85f);
              const int nb = cliffYBot(nearZ), fb = cliffYBot(farZ);
              x0 = s < 0 ? nx : fx; x1 = s < 0 ? fx : nx;
              top0 = s < 0 ? nt : ft; top1 = s < 0 ? ft : nt;
              bottom0 = s < 0 ? nb : fb; bottom1 = s < 0 ? fb : nb;
            } else {
              x0 = cliffX(nearZ, static_cast<float>(s) - 0.5f);
              x1 = cliffX(nearZ, static_cast<float>(s) + 0.5f);
              top0 = top1 = cliffYTopScaled(nearZ, 0.85f);
              bottom0 = bottom1 = cliffYBot(nearZ);
            }
            const int span = std::max(1, x1 - x0);
            const int fromX = std::max({0, x0, static_cast<int>(rowX0[d])});
            const int toX = std::min({W - 1, x1, static_cast<int>(rowX1[d])});
            for (int x = fromX; x <= toX; ++x) {
              if (((depthColOcc[d][x >> 5] >> (x & 31)) & 1u) != 0u) { continue; }
              const int u = (x - x0) * 128 / span;
              const int top = top0 + (top1 - top0) * (x - x0) / span;
              const int bottom = bottom0 + (bottom1 - bottom0) * (x - x0) / span;
              const int height = std::max(1, bottom - top);
              for (int y = std::max(0, top); y <= std::min(H - 1, bottom); ++y) {
                const int v = (y - top) * 128 / height;
                unsigned hash = static_cast<unsigned>(u * 73 + v * 151 + tx * 37 + ty * 91);
                hash ^= hash >> 7;
                hash *= 2654435761u;
                hash ^= hash >> 13;
                std::uint8_t color;
                if (vegetation) {
                  // Overlapping crowns and trunks against dark undergrowth.
                  const int treeX = (u + (var & 1) * 8) % 32 - 16;
                  const int crownX = (u + 8 + (var & 1) * 8) % 32 - 16;
                  const int crownY = v - 40;
                  const bool crown = treeX * treeX * 3 + crownY * crownY < 1650 ||
                                     crownX * crownX * 3 + (v - 23) * (v - 23) < 850;
                  if (v > 58 && std::abs(treeX) < 3) {
                    color = treeX < 0 ? pathHi : trunkCol;
                  } else if (crown) {
                    const unsigned fleck = (hash ^ (hash >> 3)) % 16u;
                    color = fleck < 3u ? leaf2 : (fleck < 11u ? leaf1 : leaf0);
                    if (treeX * treeX * 3 + crownY * crownY > 1350 && v > 28) { color = leaf0; }
                  } else {
                    color = (hash % 11u < 2u) ? leaf1 : leaf0;
                  }
                } else {
                  const std::uint8_t stone = kCragRockTex.px[((v / 2) + ty * 3) & 31][((u / 2) + tx * 5) & 31];
                  color = stone == 0u ? rockDark : stone == 3u ? rockHi : rockMid;
                  if (iceWorld && stone == 3u) { color = accent1; }
                  if (lavaWorld && stone == 0u && (hash % 13u) == 0u) { color = accent0; }
                }
                if (face == 0 && ((x + y) & 1) == 0) { color = vegetation ? leaf0 : rockDark; }
                if (y == top || y == bottom) { color = inkOut; }
                putPx(x, y, color);
              }
            }
            if (face == 1 && var < 4 && d <= 4) {
              propClipLeft = std::clamp(fromX, 0, W);
              propClipRight = std::clamp(toX + 1, 0, W);
              propClipTop = std::clamp(top0, 0, H);
              propClipBottom = std::clamp(bottom0 + 1, 0, H);
              drawBiomeProp(var % 4, x0 + span / 4, bottom0, sTile * 1.7f);
              drawBiomeProp((var + 1) % 4, x0 + span * 3 / 4, bottom0, sTile * 1.7f);
              propClipLeft = 0; propClipRight = W; propClipTop = 0; propClipBottom = H;
            }
          }
          if (tx == static_cast<int>(planetSurface_.portalX) &&
              ty == static_cast<int>(planetSurface_.portalY)) {
            drawCaveMoundPortal(tx, ty, d, s);
          }
          continue;
        }

        // Non-blocking low ground accents on open tiles (0)
        if (tile == 0 && (var % 3) == 0 && d >= 1 && d <= 4) {
          const float off = ((var & 1) ? -0.44f : 0.44f) * htMid;
          drawGroundScatter(static_cast<int>(std::lround(tcxMid + off)), gyMid, sTile, var);
        }

        // Ship & Ore sprites across forward view cone (clickable only at d==1 && s==0)
        if (d >= 1 && d <= 6 && std::abs(s) <= d) {
          const int objCx = static_cast<int>(std::lround(tcxMid));
          if (!inMine && tx == planetSurface_.shipX && ty == planetSurface_.shipY) {
            const int sw = std::max(10, static_cast<int>(std::lround(44.0f * sTile)));
            const int sh = std::max(6, static_cast<int>(std::lround(26.0f * sTile)));
            const int sx = objCx - sw / 2;
            const int sy = gyMid - sh;
            if (sx + sw > 2 && sx < W - 2) {
              // Elliptical ground shadow + thruster scorch
              fillDisc(objCx, gyMid, std::max(4, sw * 46 / 100), std::max(1, sh / 8), inkOut);
              // Splayed landing struts & footpads
              const int strutH = std::max(2, sh * 28 / 100);
              const int footW = std::max(2, sw * 14 / 100);
              fillOutlineRect(objCx - sw * 46 / 100, gyMid - strutH, footW, strutH + 1, kIdxShipDark, inkOut);
              fillOutlineRect(objCx + sw * 46 / 100 - footW, gyMid - strutH, footW, strutH + 1, kIdxShipDark, inkOut);
              // Side stabilizer wings / pods
              const int wingH = std::max(3, sh * 32 / 100);
              fillOutlineRect(objCx - sw * 48 / 100, gyMid - sh * 58 / 100, sw * 96 / 100, wingH, kIdxShipDark, inkOut);
              fillRect(objCx - sw * 46 / 100, gyMid - sh * 56 / 100, sw * 92 / 100, std::max(1, wingH / 3), kIdxShipMid);
              // Main tapered hull fuselage with upper specular bevel & panel seams
              const int hullW = std::max(6, sw * 74 / 100);
              const int hullH = std::max(4, sh * 46 / 100);
              const int hullY = gyMid - sh * 72 / 100;
              fillOutlineRect(objCx - hullW / 2, hullY, hullW, hullH, kIdxShipMid, inkOut);
              fillRect(objCx - hullW / 2 + 1, hullY + 1, std::max(2, hullW - 2), std::max(1, hullH * 4 / 10), kIdxShipLight);
              fillRect(objCx - hullW / 2 + 1, hullY + hullH - std::max(2, hullH / 4), std::max(2, hullW - 2),
                       std::max(1, hullH / 5), kIdxShipDark);
              // Twin ventral thruster bells
              if (sw >= 18) {
                fillOutlineRect(objCx - hullW / 4 - 2, hullY + hullH - 2, 4, std::max(2, sh / 7), kIdxShipDark, inkOut);
                fillOutlineRect(objCx + hullW / 4 - 2, hullY + hullH - 2, 4, std::max(2, sh / 7), kIdxShipDark, inkOut);
              }
              // Upper bridge superstructure + glowing cyan multi-pane cockpit canopy + beacon mast
              const int topW = std::max(4, sw * 44 / 100);
              const int topH = std::max(3, sh * 32 / 100);
              const int topY = gyMid - sh * 96 / 100;
              fillOutlineRect(objCx - topW / 2, topY, topW, topH, kIdxShipMid, inkOut);
              fillRect(objCx - topW / 2 + 1, topY + 1, std::max(2, topW - 2), std::max(1, topH / 3), kIdxShipLight);
              const int visW = std::max(2, sw * 28 / 100);
              const int visH = std::max(1, sh * 16 / 100);
              fillOutlineRect(objCx - visW / 2 - 1, topY + std::max(1, topH / 3), visW + 2, visH + 2, kIdxShipVisor, inkOut);
              if (visW >= 5) {
                putPx(objCx - visW / 3, topY + std::max(1, topH / 3) + 1, kIdxStarCool);
              }
              if (sh >= 14) {
                fillRect(objCx + topW / 4, topY - std::max(2, sh / 7), 1, std::max(2, sh / 7), kIdxShipLight);
                putPx(objCx + topW / 4, topY - std::max(2, sh / 7) - 1, kIdxWarmGleam);
              }
              if (d == 1 && s == 0) {
                const int cx0 = std::clamp(sx, 0, W);
                const int cx1 = std::clamp(sx + sw, 0, W);
                if (cx1 > cx0) {
                  planetSurface_.frontKind = 6;
                  planetSurface_.frontArg = 0;
                  planetSurface_.frontUv = Area{
                      static_cast<float>(cx0) / static_cast<float>(W),
                      static_cast<float>(std::max(0, sy)) / static_cast<float>(H),
                      static_cast<float>(cx1 - cx0) / static_cast<float>(W),
                      static_cast<float>(sh) / static_cast<float>(H)};
                }
              }
            }
          }
          const int oreIdx = (d <= 5) ? oreAt(tx, ty) : -1;
          if (oreIdx >= 0) {
            const int ow = std::max(7, static_cast<int>(std::lround(21.0f * sTile)));
            const int oh = std::max(5, static_cast<int>(std::lround(15.0f * sTile)));
            const int ox = objCx - ow / 2;
            const int oy = gyMid - oh;
            if (ox + ow > 2 && ox < W - 2) {
              // Cast shadow + riveted PC-98 sci-fi ore container with beveled sunlit lid & glowing core
              fillDisc(objCx, gyMid, std::max(3, ow * 48 / 100), std::max(1, oh / 6), inkOut);
              fillOutlineRect(ox, oy, ow, oh, kIdxOreBody, inkOut);
              // Sunlit top beveled lid
              const int lidH = std::max(2, oh * 30 / 100);
              fillRect(ox + 1, oy + 1, std::max(2, ow - 2), lidH, kIdxOreGold);
              drawHLine(ox + 1, ox + ow - 2, oy + 1, kIdxWarmGleam);
              drawHLine(ox + 1, ox + ow - 2, oy + lidH + 1, inkOut);
              // Dark metal corner brackets & vertical ribs
              if (ow >= 11 && oh >= 9) {
                fillRect(ox + 1, oy + 1, 2, oh - 2, kIdxShipDark);
                fillRect(ox + ow - 3, oy + 1, 2, oh - 2, kIdxShipDark);
                putPx(ox + 1, oy + 2, kIdxShipLight);
                putPx(ox + ow - 2, oy + 2, kIdxShipLight);
              }
              // Recessed glowing ore core window + specular gleam
              const int coreW = std::max(3, ow * 40 / 100);
              const int coreH = std::max(2, oh * 34 / 100);
              const int coreX = objCx - coreW / 2;
              const int coreY = oy + lidH + std::max(1, (oh - lidH - coreH) / 2);
              fillOutlineRect(coreX - 1, coreY - 1, coreW + 2, coreH + 2, kIdxOreGold, inkOut);
              fillRect(coreX, coreY, std::max(1, coreW - 1), std::max(1, coreH - 1), kIdxWarmGleam);
              if (coreW >= 4) { putPx(coreX, coreY, kIdxStarCool); }
              if (d == 1 && s == 0) {
                const int cx0 = std::clamp(ox, 0, W);
                const int cx1 = std::clamp(ox + ow, 0, W);
                if (cx1 > cx0) {
                  planetSurface_.frontKind = 5;
                  planetSurface_.frontArg = oreIdx;
                  planetSurface_.frontUv = Area{
                      static_cast<float>(cx0) / static_cast<float>(W),
                      static_cast<float>(std::max(0, oy)) / static_cast<float>(H),
                      static_cast<float>(cx1 - cx0) / static_cast<float>(W),
                      static_cast<float>(oh) / static_cast<float>(H)};
                }
              }
            }
          }
        }
      }
    }
  }
  activeFog = 0;
  activeInk = inkOut;

  // 1px retro PC-98 bezel inner shadow
  fillRect(0, 0, W, 1, inkOut);
  fillRect(0, 0, 1, H, inkOut);

  // Create GPU texture once (540 KB VRAM at 480x288, 0 B persistent heap), then stream RGB4 palette
  // resolves through an L1-cache-resident 16-row stack strip (30 KB stack, 0 B heap allocations/step).
  if (planetTex_.id == 0) {
    Image blank = GenImageColor(W, H, BLACK);
    planetTex_ = LoadTextureFromImage(blank);
    UnloadImage(blank);
    if (planetTex_.id != 0) {
      SetTextureFilter(planetTex_, TEXTURE_FILTER_POINT);
    }
  }
  if (planetTex_.id != 0) {
    constexpr int kStripH = 16;
    Color stripRgba[W * kStripH];
    for (int y0 = 0; y0 < H; y0 += kStripH) {
      const int rows = std::min(kStripH, H - y0);
      const int count = rows * W;
      const std::uint8_t* srcRow = idxBuf + y0 * W;
      for (int i = 0; i < count; ++i) {
        stripRgba[i] = resolvedLut[srcRow[i]];
      }
      UpdateTextureRec(
          planetTex_,
          Rectangle{0.0f, static_cast<float>(y0), static_cast<float>(W), static_cast<float>(rows)},
          stripRgba);
    }
  }
  if (planetScanlineTex_.id == 0) {
    // True PC-98 24 kHz CRT scanline mask: 1 x (H * 4) bilinear beam-gap profile locked 1:1 to all H rows.
    // Built once (4.5 KB VRAM at H=288, 0 B persistent heap, 1 DrawTexturePro/frame, moire-free at arbitrary scales).
    constexpr int kScanSub = 4;
    constexpr int kScanH = H * kScanSub;
    static constexpr unsigned char kRowAlpha[kScanSub] = {18u, 0u, 14u, 88u};
    Color scanBuf[kScanH];
    for (int y = 0; y < H; ++y) {
      for (int s = 0; s < kScanSub; ++s) {
        scanBuf[y * kScanSub + s] = Color{0u, 0u, 0u, kRowAlpha[s]};
      }
    }
    Image scanImg{};
    scanImg.data = scanBuf;
    scanImg.width = 1;
    scanImg.height = kScanH;
    scanImg.mipmaps = 1;
    scanImg.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    planetScanlineTex_ = LoadTextureFromImage(scanImg);
    if (planetScanlineTex_.id != 0) {
      SetTextureFilter(planetScanlineTex_, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(planetScanlineTex_, TEXTURE_WRAP_CLAMP);
    }
  }
}
}  // namespace ship
