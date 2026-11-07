// ============================================================================
//  ChartArt.inc — the chart's celestial bodies, painted as themselves.
//  ---------------------------------------------------------------------------
//  A dot is not a planet. This file gives every scale of the star chart a
//  vocabulary: a star with a corona, a banded gas giant with its ring, an ice
//  world with a polar cap, an ocean world with its continents, a cratered moon,
//  a station with its mast, a belt of loose material, a system drawn as the
//  star it contains and a region drawn as the dust it is. Nothing here decides
//  anything; it is handed a body and a place to put it.
//
//  Included by Draw.cpp, not compiled on its own. It needs raylib, the palette
//  and the chart's view-model, and nothing else - which is what lets the native
//  draw recorder exercise it without a window.
// ============================================================================
//  This file declares no namespace of its own: Draw.cpp includes it inside its
//  own anonymous namespace, where ToRay(), Area and the palette already live.

// What the chart draws for a body, decided once and drawn once.
enum class ChartLook {
  Star,
  Gas,
  Ice,
  Ocean,
  Rock,
  Moon,
  Station,
  Site,
  Belt,
  System,
  Region,
  Origin
};

std::uint8_t ChartByte(double value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
}

::Color ChartMix(const ::Color& a, const ::Color& b, float t) {
  const float k = std::clamp(t, 0.0f, 1.0f);
  return ::Color{ChartByte(a.r + (b.r - a.r) * k), ChartByte(a.g + (b.g - a.g) * k),
                 ChartByte(a.b + (b.b - a.b) * k),
                 ChartByte(a.a + (b.a - a.a) * k)};
}

// Toward white, so a body keeps the palette's warmth while reading as lit.
::Color ChartLift(const ::Color& colour, float t) {
  return ChartMix(colour, ::Color{255, 255, 255, colour.a}, t);
}

// Toward black, for terminator shading and crater floors.
::Color ChartDeep(const ::Color& colour, float t) {
  return ChartMix(colour, ::Color{0, 0, 0, colour.a}, t);
}

::Color ChartFade(const ::Color& colour, float alpha) {
  const double a = static_cast<double>(colour.a) * std::clamp(alpha, 0.0f, 1.0f);
  return ::Color{colour.r, colour.g, colour.b, ChartByte(a)};
}

// A stable hash of the body's id: the same world is cratered the same way on
// every frame, and a rebuild does not shuffle its surface.
unsigned ChartSeed(const std::string& id) {
  unsigned hash = 2166136261u;
  for (const char c : id) {
    hash ^= static_cast<unsigned>(static_cast<unsigned char>(c));
    hash *= 16777619u;
  }
  return hash;
}

ChartLook ChartLookOf(const NavBodyView& body) {
  if (body.type == "star") { return ChartLook::Star; }
  if (body.type == "moon") { return ChartLook::Moon; }
  if (body.type == "station") { return ChartLook::Station; }
  if (body.type == "site") { return ChartLook::Site; }
  if (body.type == "belt") { return ChartLook::Belt; }
  if (body.type == "system") { return ChartLook::System; }
  if (body.type == "region") { return ChartLook::Region; }
  if (body.type == "origin") { return ChartLook::Origin; }
  if (body.subtype == "gas") { return ChartLook::Gas; }
  if (body.subtype == "ice") { return ChartLook::Ice; }
  if (body.subtype == "ocean") { return ChartLook::Ocean; }
  return ChartLook::Rock;
}

// No body may shrink out of sight just because the scale got wider.
float ChartMinimumRadius(ChartLook look, float bodyScale) {
  const float base = std::max(0.0f, bodyScale);
  switch (look) {
    case ChartLook::Region: return 5.0f + 2.0f * base;
    case ChartLook::Star: return 3.2f + 1.4f * base;
    case ChartLook::System: return 3.4f + 1.4f * base;
    case ChartLook::Gas: return 3.4f + 1.2f * base;
    case ChartLook::Ice:
    case ChartLook::Ocean:
    case ChartLook::Rock: return 2.8f + 1.1f * base;
    case ChartLook::Moon: return 1.8f + 0.7f * base;
    case ChartLook::Station: return 2.0f + 0.8f * base;
    case ChartLook::Site:
    case ChartLook::Belt: return 1.8f + 0.6f * base;
    case ChartLook::Origin: return 2.2f + 0.8f * base;
  }
  return 2.0f + base;
}

void PaintChartStar(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color amber = ToRay(p.amber);
  const ::Color white = ToRay(p.white);
  // The corona: three soft shells, so the star bleeds light instead of sitting
  // on the chart like a coin.
  DrawCircleGradient(Vector2{cx, cy}, radius * 4.0f,
                     ChartFade(amber, 0.10f * alpha),
                     ChartFade(amber, 0.0f));
  DrawCircleGradient(Vector2{cx, cy}, radius * 2.4f,
                     ChartFade(ChartLift(amber, 0.25f), 0.24f * alpha),
                     ChartFade(amber, 0.02f * alpha));
  DrawCircleV(Vector2{cx, cy}, radius, ChartFade(ChartLift(amber, 0.55f), alpha));
  DrawCircleV(Vector2{cx, cy}, radius * 0.58f, ChartFade(white, alpha));
  DrawCircleLinesV(Vector2{cx, cy}, radius, ChartFade(ChartDeep(amber, 0.25f), 0.9f * alpha));

  // Four rays: the chart's own star glyph, kept short so a crowded sector field
  // does not turn into a lattice.
  const float reach = radius * 3.1f;
  for (int i = 0; i < 4; ++i) {
    const float angle = 0.7853981634f + static_cast<float>(i) * 1.5707963268f;
    DrawLineEx(Vector2{cx + std::cos(angle) * radius * 1.25f,
                       cy + std::sin(angle) * radius * 1.25f},
               Vector2{cx + std::cos(angle) * reach, cy + std::sin(angle) * reach}, 1.0f,
               ChartFade(ChartLift(amber, 0.35f), 0.55f * alpha));
  }
}

// A disc with a lit limb and a shadowed hemisphere: the cheapest honest way to
// say "this is a sphere" without a texture.
void PaintChartSphere(float cx, float cy, float radius, const ::Color& base,
                      const ::Color& shadow, float alpha) {
  DrawCircleV(Vector2{cx, cy}, radius, ChartFade(base, alpha));
  DrawCircleSector(Vector2{cx, cy}, radius, 24.0f, 200.0f, 18,
                   ChartFade(shadow, 0.30f * alpha));
  DrawCircleLinesV(Vector2{cx, cy}, radius, ChartFade(ChartDeep(base, 0.35f), 0.85f * alpha));
}

void PaintChartGas(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color base = ChartLift(ToRay(p.amber), 0.46f);
  PaintChartSphere(cx, cy, radius, base, ToRay(p.coffeeDark), alpha);

  // Bands, each clipped to the disc by its own chord, so nothing spills.
  for (int i = -2; i <= 2; ++i) {
    const float offset = radius * 0.34f * static_cast<float>(i);
    const float half = std::sqrt(std::max(0.0f, radius * radius - offset * offset));
    const int width = static_cast<int>(half * 2.0f);
    if (width < 1) { continue; }
    DrawRectangle(static_cast<int>(cx - half), static_cast<int>(cy + offset),
                  width, std::max(1, static_cast<int>(radius * 0.22f)),
                  ChartFade(ChartDeep(base, 0.22f), 0.55f * alpha));
  }

  // The ring: two ellipses seen almost edge on.
  DrawEllipseLines(static_cast<int>(cx) + 1, static_cast<int>(cy) + 1,
                   radius * 2.0f, radius * 0.52f,
                   ChartFade(ChartLift(ToRay(p.coffeeLight), 0.35f), 0.7f * alpha));
  DrawEllipseLines(static_cast<int>(cx), static_cast<int>(cy),
                   radius * 1.65f, radius * 0.42f,
                   ChartFade(ChartLift(ToRay(p.coffeeLight), 0.15f), 0.4f * alpha));
}

void PaintChartIce(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color base = ChartMix(ToRay(p.white), ToRay(p.sage), 0.24f);
  PaintChartSphere(cx, cy, radius, base, ToRay(p.coffeeDark), alpha);
  // A polar cap on the lit side, then two frozen seas.
  DrawCircleSector(Vector2{cx, cy}, radius * 0.94f, -62.0f, 62.0f, 12,
                   ChartFade(ToRay(p.white), 0.55f * alpha));
  DrawCircleV(Vector2{cx - radius * 0.3f, cy + radius * 0.28f}, radius * 0.22f,
              ChartFade(ToRay(p.white), 0.4f * alpha));
  DrawCircleV(Vector2{cx + radius * 0.36f, cy - radius * 0.3f}, radius * 0.16f,
              ChartFade(ToRay(p.white), 0.3f * alpha));
}

void PaintChartOcean(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color base = ChartMix(ToRay(p.sage), ToRay(p.coffeeDark), 0.22f);
  PaintChartSphere(cx, cy, radius, base, ToRay(p.coffeeDark), alpha);
  const unsigned seed = ChartSeed("ocean");
  for (int i = 0; i < 3; ++i) {
    const double angle = static_cast<double>((seed >> (i * 5)) % 360u) * 0.0174532925;
    const double reach = radius * (0.18 + 0.12 * static_cast<double>((seed >> (i * 3)) % 4u));
    DrawCircleV(Vector2{cx + static_cast<float>(std::cos(angle) * reach),
                        cy + static_cast<float>(std::sin(angle) * reach * 0.8)},
                radius * 0.26f, ChartFade(ChartLift(ToRay(p.coffeeLight), 0.15f), 0.75f * alpha));
  }
}

void PaintChartRock(float cx, float cy, float radius, const Palette& p, float alpha,
                    unsigned seed) {
  const ::Color base = ChartMix(ToRay(p.coffee), ToRay(p.terracotta), 0.32f);
  PaintChartSphere(cx, cy, radius, base, ToRay(p.coffeeDark), alpha);
  for (int i = 0; i < 3; ++i) {
    const double angle = static_cast<double>((seed >> (i * 7)) % 360u) * 0.0174532925;
    const double reach = radius * (0.22 + 0.16 * static_cast<double>((seed >> (i * 4)) % 3u));
    DrawCircleV(Vector2{cx + static_cast<float>(std::cos(angle) * reach),
                        cy + static_cast<float>(std::sin(angle) * reach * 0.85)},
                radius * 0.2f, ChartFade(ChartDeep(base, 0.4f), 0.7f * alpha));
  }
}

void PaintChartMoon(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color base = ChartLift(ToRay(p.muted), 0.5f);
  PaintChartSphere(cx, cy, radius, base, ToRay(p.coffeeDark), alpha);
  DrawCircleV(Vector2{cx - radius * 0.28f, cy - radius * 0.18f}, radius * 0.26f,
              ChartFade(ChartDeep(base, 0.28f), 0.8f * alpha));
  DrawCircleV(Vector2{cx + radius * 0.32f, cy + radius * 0.3f}, radius * 0.18f,
              ChartFade(ChartDeep(base, 0.22f), 0.7f * alpha));
}

void PaintChartStation(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color ink = ToRay(p.coffee);
  const ::Color light = ToRay(p.coffeeLight);
  DrawCircleV(Vector2{cx, cy}, std::max(1.4f, radius * 0.42f), ChartFade(ink, alpha));
  DrawCircleLinesV(Vector2{cx, cy}, radius, ChartFade(light, alpha));
  // A mast and two docking arms: a station you can recognise at four pixels.
  DrawLineEx(Vector2{cx, cy - radius}, Vector2{cx, cy - radius * 1.9f}, 1.0f,
             ChartFade(light, alpha));
  DrawLineEx(Vector2{cx - radius * 1.6f, cy + radius * 0.5f},
             Vector2{cx + radius * 1.6f, cy + radius * 0.5f}, 1.0f, ChartFade(ink, 0.85f * alpha));
  DrawCircleV(Vector2{cx, cy - radius * 1.9f}, 1.2f, ChartFade(ToRay(p.terracotta), alpha));
}

void PaintChartSite(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color ink = ToRay(p.terracotta);
  const float r = radius * 1.15f;
  const Vector2 top{cx, cy - r};
  const Vector2 right{cx + r, cy};
  const Vector2 bottom{cx, cy + r};
  const Vector2 left{cx - r, cy};
  DrawLineEx(top, right, 1.0f, ChartFade(ink, alpha));
  DrawLineEx(right, bottom, 1.0f, ChartFade(ink, alpha));
  DrawLineEx(bottom, left, 1.0f, ChartFade(ink, alpha));
  DrawLineEx(left, top, 1.0f, ChartFade(ink, alpha));
  DrawCircleV(Vector2{cx, cy}, std::max(1.0f, radius * 0.28f), ChartFade(ink, alpha));
}

void PaintChartBelt(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color ink = ToRay(p.coffeeLight);
  for (int i = 0; i < 5; ++i) {
    const float t = static_cast<float>(i) / 4.0f;
    const float x = cx + (t - 0.5f) * radius * 4.2f;
    const float y = cy + (i % 2 == 0 ? -1.0f : 1.0f) * radius * 0.32f;
    DrawCircleV(Vector2{x, y}, std::max(0.9f, radius * 0.22f), ChartFade(ink, 0.75f * alpha));
  }
  DrawCircleLinesV(Vector2{cx + radius * 0.2f, cy}, radius * 0.9f, ChartFade(ink, 0.35f * alpha));
}

void PaintChartSystem(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color amber = ToRay(p.amber);
  DrawCircleGradient(Vector2{cx, cy}, radius * 2.5f, ChartFade(amber, 0.13f * alpha),
                     ChartFade(amber, 0.0f));
  DrawCircleV(Vector2{cx, cy}, radius * 0.78f, ChartFade(ChartLift(amber, 0.32f), alpha));
  DrawCircleV(Vector2{cx, cy}, radius * 0.34f, ChartFade(ToRay(p.ink), alpha));
  DrawCircleLinesV(Vector2{cx, cy}, radius * 1.25f, ChartFade(ToRay(p.coffeeLight), 0.45f * alpha));
  for (int i = 0; i < 4; ++i) {
    const float angle = 0.7853981634f + static_cast<float>(i) * 1.5707963268f;
    DrawLineEx(Vector2{cx + std::cos(angle) * radius * 1.5f, cy + std::sin(angle) * radius * 1.5f},
               Vector2{cx + std::cos(angle) * radius * 2.3f, cy + std::sin(angle) * radius * 2.3f},
               1.0f, ChartFade(ChartLift(amber, 0.3f), 0.55f * alpha));
  }
}

void PaintChartRegion(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color ink = ToRay(p.coffeeLight);
  DrawCircleGradient(Vector2{cx, cy}, radius * 3.4f, ChartFade(ToRay(p.panel3), 0.5f * alpha),
                     ChartFade(ToRay(p.panel3), 0.0f));
  DrawCircleLinesV(Vector2{cx, cy}, radius, ChartFade(ink, 0.8f * alpha));
  DrawCircleV(Vector2{cx, cy}, std::max(1.6f, radius * 0.3f), ChartFade(ToRay(p.ink), alpha));
  // A handful of its stars, so a region reads as a crowd and not a pin.
  for (int i = 0; i < 7; ++i) {
    const double angle = 0.8975979010 * static_cast<double>(i + 1);
    const float reach = radius * (1.5f + 0.18f * static_cast<float>(i % 3));
    DrawCircleV(Vector2{cx + static_cast<float>(std::cos(angle)) * reach,
                        cy + static_cast<float>(std::sin(angle)) * reach * 0.62f},
                std::max(0.8f, radius * 0.14f), ChartFade(ink, 0.85f * alpha));
  }
}

void PaintChartOrigin(float cx, float cy, float radius, const Palette& p, float alpha) {
  const ::Color sage = ToRay(p.sage);
  DrawCircleGradient(Vector2{cx, cy}, radius * 2.6f, ChartFade(sage, 0.2f * alpha),
                     ChartFade(sage, 0.0f));
  DrawCircleV(Vector2{cx, cy}, radius * 0.62f, ChartFade(sage, alpha));
  DrawCircleLinesV(Vector2{cx, cy}, radius * 1.5f, ChartFade(sage, 0.55f * alpha));
}

// One body, at one place, at one size.
void PaintChartBody(const NavBodyView& body, float cx, float cy, float radius, float bodyScale,
                    const Palette& p, float alpha) {
  const ChartLook look = ChartLookOf(body);
  const float drawn = std::max(radius, ChartMinimumRadius(look, bodyScale));
  const unsigned seed = ChartSeed(body.id);
  switch (look) {
    case ChartLook::Star: PaintChartStar(cx, cy, drawn, p, alpha); break;
    case ChartLook::Gas: PaintChartGas(cx, cy, drawn, p, alpha); break;
    case ChartLook::Ice: PaintChartIce(cx, cy, drawn, p, alpha); break;
    case ChartLook::Ocean: PaintChartOcean(cx, cy, drawn, p, alpha); break;
    case ChartLook::Rock: PaintChartRock(cx, cy, drawn, p, alpha, seed); break;
    case ChartLook::Moon: PaintChartMoon(cx, cy, drawn, p, alpha); break;
    case ChartLook::Station: PaintChartStation(cx, cy, drawn, p, alpha); break;
    case ChartLook::Site: PaintChartSite(cx, cy, drawn, p, alpha); break;
    case ChartLook::Belt: PaintChartBelt(cx, cy, drawn, p, alpha); break;
    case ChartLook::System: PaintChartSystem(cx, cy, drawn, p, alpha); break;
    case ChartLook::Region: PaintChartRegion(cx, cy, drawn, p, alpha); break;
    case ChartLook::Origin: PaintChartOrigin(cx, cy, drawn, p, alpha); break;
  }
}

// Back to front: dust and regions, then the stars that light the scene, then
// the worlds, then the marks people built.
int ChartPaintRank(ChartLook look) {
  switch (look) {
    case ChartLook::Region: return 0;
    case ChartLook::Star: return 1;
    case ChartLook::System: return 2;
    case ChartLook::Gas:
    case ChartLook::Ice:
    case ChartLook::Ocean:
    case ChartLook::Rock: return 3;
    case ChartLook::Belt: return 4;
    case ChartLook::Moon: return 5;
    case ChartLook::Site: return 6;
    case ChartLook::Station: return 7;
    case ChartLook::Origin: return 8;
  }
  return 9;
}

bool ChartRectsOverlap(const Area& a, const Area& b) {
  return a.x < b.Right() && b.x < a.Right() && a.y < b.Bottom() && b.y < a.Bottom();
}

// A label is drawn if it can be read: the busy scales carry two hundred names,
// and stacking them on top of each other would hide the chart, not show it.
int ChartLabelPriority(const NavBodyView& body) {
  if (body.selected) { return 0; }
  if (body.here) { return 1; }
  // A region outranks the stars inside it: on the galaxy scale the seven names
  // are the map, and the two hundred stars are the detail behind them. Without
  // this the cluster labels were whatever star happened to be nearest the ship.
  if (body.type == "region") { return 2; }
  if (body.inRange) { return 3; }
  if (body.type == "star" || body.type == "system") { return 4; }
  if (body.type == "planet") { return 5; }
  return 6;
}

