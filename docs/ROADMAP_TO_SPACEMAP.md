# Roadmap to a SpaceMap (C++ Port Expansion) 

Map size exceed HTA Ceiling 1100x500 / World 638x278 (Authorized by Owner).  
**Goal**: Expand the star map (system/sector/galaxy) "really big" and make it alive (debris, events, mini stations, other ships).  

---

## Phase 0: Plan & Environment
- **Objective**: Inspect and learn the code.
- **Actions**:
  - Trace HTA Truth
  - Dimensions in C++ port have to exceed these limits ("really big").
  - Inspect C++ paths: these and beyond `cpp/core/Navigation.cpp`, `cpp/core/Constants.h`, `cpp/core/GameState.h`, `cpp/ui/NavScreen.cpp`, `cpp/data/Catalog.h`, `cpp/data/Types.h`.
  - Identify scale constants and layer logic.
  - Set environment for "really big" expansion (dimensions).

## Phase 1: Build the Blueprints
- **Objective**: Define **ALL** data structures and rules **BEFORE** generation.
- **Actions**:
  - **Blueprints for Debris/Loot**:
    - Extend `Body::type` / `IncidentType` / `SurveyRecordDef` in `cpp/data/Types.h` / `cpp/data/Catalog.h`.
    - Define **Task Content**: Based on but not copied: Machine Room, O2 Filter, Crew Room, Navigation, Mess Hall (in `cpp/core/Incidents.cpp` / `cpp/ui/StoresScreen.cpp`).
    - Random loot (ore, shiplets, and more) and text.
  - **Blueprints for Tags/IDs/Names**:
    - Generation rules for `id`, `name`, `klass`, `type`, `tag` in `cpp/data/Catalog.h` / `cpp/core/Navigation.cpp`.
  - **Blueprints for Planet Info**:
    - `Body::details`, `scan`, `scanLocked` rules in `cpp/data/Types.h` / `cpp/data/Catalog.h`.
    - Random loot/moon attachment rules.
  - **Blueprints for Mini/Normal Stations**:
    - `Port` / `Good` limits (handful of goods) in `cpp/data/Types.h` / `cpp/data/Catalog.h` / `cpp/core/Economy.cpp`.
    - Minimalistic beta state rules.
  - **Blueprints for Other Ships**:
    - Click-logic time rules (1 click = 1 clock advance) in `cpp/core/Simulation.h` / `cpp/core/Navigation.cpp`.

## Phase 3: Environment & Dimensions (The "Really Big" Map)
- **Objective**: Define the new dimensions for System, Sector, and Galaxy layers in C++ (exceeding HTA 1100x500 / 638x278).
- **Test**: Verify connection and reachability (basic map rendering/navigation).
   
## Phase 4: The Generative Generator (Procedural Map)
- **Objective**: The generator uses **ALL** blueprints and procedurally auto generates the map ingame.
- **Actions**:
  - Implement generator in `cpp/core/Navigation.cpp` or `cpp/core/Simulation.h` (`NewVoyage` / `CurBodies` / `Catalog::LoadFromAssets` replacement).
  - Generate `regions`, `systems`, `bodies`, `sectorLanes`, `galaxyLanes`, `ports`, `goods`, `contracts`, `incidentTypes`, `surveyWorth`.
  - Ensure connectivity and reachability.
  - Apply "Really Big" dimensions.
  - Run a test ingame and report how the generative generator worked, if failed find the source and fix it then repeat the test until it works.
    
---

