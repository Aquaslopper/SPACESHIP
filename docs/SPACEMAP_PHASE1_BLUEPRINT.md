# SpaceMap Phase 1 — native C++ data and rules

**Status:** Phase 1 definitions are represented in the C++ catalogue. No procedural map generation, save-format change, HTA edit, or generated-asset edit is included. The world-size interpretation and seed-persistence policy remain unresolved.

## Scope and verified baseline

- Work is native C++ only. `Spaceship.hta` is read-only; `assets/data/*.json` remain exporter-produced and untouched.
- `Simulation::NewVoyage` copies `Catalog`; the native blueprint lives alongside that copy in `Catalog::spaceMap`.
- Current authored catalogue: 7 regions, 48 systems, 50 `star` bodies, 243 local bodies, and 30 ports.
- The existing chart vectors are dynamic. The 50 authored stars are not a discovered capacity limit.
- `Simulation::PerformAction` is the sole world-clock boundary. Selection, inspection, pan, zoom, and other reads are free.

## Defined data model

### Bodies, identity, names, and survey text

- `Body` gains `tag`; empty tags preserve legacy authored bodies. Recognized native profile types include `star`, `planet`, `moon`, `debris`, `derelict`, and `site`.
- `BodyProfileDef` is the authority for `type`, `tag`, `klass`, `subtype`, name rule, loot table, integer selection weight, details, scan text, and scan-locked text.
- Weighted body profile selection is grouped by `type`; the weights in each group sum to 100:

  | Type | Profiles and weights |
  |---|---|
  | star | primary star 100 |
  | planet | rocky 25, icy 25, ocean 25, gas 25 |
  | moon | natural satellite 100 |
  | debris | ore-bearing 45, wreckage 55 |
  | derelict | derelict structure 100 |
  | site | anomalous signal site 100 |

- Each `SpaceMapNameRule` contains explicit prefix and suffix lists. `SpaceMapGeneratedName` chooses both from independent seed/domain/ordinal streams and appends the one-based ordinal, so a category's names remain stable and distinct.
- `SpaceMapGeneratedId` uses the reserved `gen-v<version>-<category>-<16-digit-seed>-<8-digit-ordinal>` form. IDs are independent of display names and stable for the same version, seed, category, and index.
- `SpaceMapRandomWord` hashes the domain with FNV-1a and mixes seed/counter using SplitMix64. `SpaceMapWeightedIndex` uses integer weights and returns no selection for a zero-total list.
- `BodyProfileDef` holds explicit `details`, `scan`, and `scanLocked` text; generated bodies must copy those fields rather than silently omitting them.

### Planet and moon attachment

- Planet profiles carry a 0.5 moon chance; `SpaceMapRules::maxMoonsPerPlanet` is exactly one.
- A moon, when attached, is type `moon`, tag `satellite.natural`, and uses the natural-satellite profile. Its name and content use the same deterministic identity/name rules.
- This is a rule definition only; no coordinates or bodies are generated in Phase 1.

### Debris, tasks, incidents, and loot

- `LootTableDef` stores weighted `LootEntryDef` rows. Each table sums to 100; cargo rows identify an existing `Good` ID and specify a display name, quantity bounds, per-unit volume/value, and risk. Survey rows resolve to a `SurveyRecordDef`; shiplet rows specify their payout range, and survey rows specify the record's payout range (overriding the template's default `value`). Before the weighted draw, filter out cargo rows that do not fit and survey rows already collected for that body; renormalize the remaining integer weights. If no rows remain, grant nothing. This is represented by `LootTableDef::filterByEligibility`.
- Native table weights are:

  | Table | Outcomes (weight) |
  |---|---|
  | `debris-common` | shiplets 25; ore 20; parts 15; water 10; medicine 10; relic 10; survey fragment 5; empty 5 |
  | `provisions` | shiplets 35; greens 25; water 20; medicine 10; empty 10 |
  | `world-find` | survey notes 45; relic 15; shiplets 20; no reward 20 |

- Five original `SpaceTaskDef` entries draw on broad themes of engineering, life support, crew records, navigation, and provisions. Their prose is new, not copied from room text. Each declares start actions, work actions, energy cost, completion text, loot table, and acknowledgement reward behavior.
- `IncidentType` keeps the existing ship-report schedule/effects. New fields default to `scope="ship"`; native map incidents use explicit `scope="map"`, tag trigger, target tag, task/loot references, and integer spawn weight.
- Defined map events: entering `debris.wreck` may start `coil-relay` with `debris-common`; scanning `world.ocean` may start `dead-reckoning` with `world-find`.

### Ports, goods, and beta limits

- Legacy ports with empty `stationTier`, goods, or services preserve full-market/full-service behavior.
- The `mini` station profile is beta, offers 2–3 market goods (maximum 3) from `ore`, `parts`, and `water`, and provides only `trade` and `refuel`.
- The `normal` profile offers 4–7 goods (maximum 7) from the seven existing market goods, and provides `trade`, `refuel`, `resupply`, `repair`, `upgrades`, `hire`, and `contracts`; `upgrades` includes room installation and the larger-hull refit.
- `Good::miniStationEligible` is assigned in C++ for ore, parts, greens, water, and medicine; the mini profile intentionally exposes only three of those. Existing per-kind port pricing remains authoritative.
- `Port` carries tier, offered good IDs, service IDs, and beta state. `Catalog::Validate` checks bounds/references; `Economy.cpp` enforces the offered services and goods without changing legacy full-service ports.

### Other ships and action time

- `ShipContactProfileDef` defines contact type/tag/class/name rule, relative spawn weight, movement interval in committed actions, and available actions.
- Relative profile weights sum to 100: trader 50 (moves every 2 actions), distress vessel 20 (stationary), patrol 15 (moves every 3), salvager 15 (moves every 2).
- Every committed contact action has `clockTicks=1`. Its handler must call `PerformAction` exactly once; target selection and reading remain free. Runtime contact state and persistence are not part of this phase.

## Explicitly unresolved / excluded

- “50× bigger” is **not** converted to target star, system, or region counts, nor to an area/linear multiplier. Those optional `SpaceMapRules` fields remain unset pending owner clarification.
- The world-seed creation policy and native save/version fields needed to persist it remain undecided.
- No procedural generation, graph expansion, map resizing, contact runtime, or save migration is implemented here.
- No changes to `Spaceship.hta`, `assets/data/*.json`, or the `rescue/` directory.

## Verification

- `Catalog::Validate` checks native profile references, weight totals, loot/task/event links, station limits, and contact action clocks.
- C++ tests cover the defined tables, deterministic ID/name/weighted helpers, unresolved 50× fields, and mini-station service restrictions.
