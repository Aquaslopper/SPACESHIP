// ============================================================================
//  ShipDiagram.cpp — the deck plan, measured.
//  ---------------------------------------------------------------------------
//  Everything below is a straight reading of buildStage(): the same hull table,
//  the same three intake bars per nacelle, the same nozzle offsets, the same
//  two cosmetic dividers in the hold. The one thing a rectangle cannot carry is
//  the CSS border radius, so that travels separately (HullRadius) and the
//  renderer applies it.
// ============================================================================
#include "ui/ShipDiagram.h"

#include <cmath>

#include "ui/Fx.h"

namespace ship {

float HullRadius(const std::string& cls) {
  if (cls == "hp-body") { return 60.0f; }     // 0 60px 60px 0
  if (cls == "hp-spine") { return 14.0f; }    // 14px 14px 0 0
  if (cls == "hp-pod") { return 14.0f; }      // 0 0 14px 14px
  if (cls == "hp-nacelle") { return 15.0f; }
  // hp-lens is a circle (50%), and hp-pylon is a plain rectangle.
  return 0.0f;
}

int NozzleCount(bool compact) { return compact ? 2 : 3; }

const DeckPlan& ActivePlan(const Simulation& sim) {
  return sim.State().shipClass == "wren" ? sim.World().wren : sim.World().kestrel;
}

const Room* CrewRoom(const Simulation& sim, const CrewMember& crew) {
  const Task* work = sim.CrewBusy(crew);
  if (work != nullptr && work->target == "social") {
    return ActivePlan(sim).FindRoom("r13");
  }
  if (sim.FindOngoing("doc-" + crew.id) != nullptr) {
    return ActivePlan(sim).FindRoom("r1");
  }
  if (crew.job.empty()) { return nullptr; }  // the corridor: the dole queue
  return ActivePlan(sim).FindRoom(crew.room);
}

namespace {
const HullPart* FindPart(const DeckPlan& plan, const std::string& id) {
  for (const HullPart& part : plan.hull) {
    if (part.id == id) { return &part; }
  }
  return nullptr;
}

std::string FirstName(const std::string& full) {
  const std::size_t space = full.find(' ');
  return space == std::string::npos ? full : full.substr(0, space);
}
}  // namespace

DiagramModel BuildDiagram(const Simulation& sim, const Area& stage,
                          bool hideCrewLabels, FxRng& rng) {
  DiagramModel out;
  out.stage = stage;
  out.compact = sim.State().shipClass == "wren";

  const DeckPlan& plan = ActivePlan(sim);
  out.hull = plan.hull;
  out.rooms = plan.rooms;
  for (const auto& room : plan.rooms) {
    const auto it = sim.World().roomScenes.find(room.id);
    out.roomAccents[room.id] = it == sim.World().roomScenes.end() ? "#8c6f4e" : it->second.accent;
  }
  out.corridor = Area{static_cast<float>(plan.corridor.x),
                      static_cast<float>(plan.corridor.y),
                      static_cast<float>(plan.corridor.w),
                      static_cast<float>(plan.corridor.h)};
  out.corridorLabel = "CORRIDOR";
  out.corridorLabelBox =
      Area{static_cast<float>(plan.corridor.x + 9.0),
           static_cast<float>(plan.corridor.y + 7.0), 90.0f, 12.0f};

  // Intake bars at the forward end of each nacelle: three, 12px apart, 3px wide.
  for (const HullPart& part : plan.hull) {
    if (part.cls != "hp-nacelle") { continue; }
    for (int k = 0; k < 3; ++k) {
      out.nacBars.push_back(
          Area{static_cast<float>(part.box.x + part.box.w - 30.0 - k * 12.0),
               static_cast<float>(part.box.y + 8.0), 3.0f,
               static_cast<float>(part.box.h - 16.0)});
    }
  }

  const HullPart* body = FindPart(plan, "body");
  if (body != nullptr) {
    // Three nozzles aft of the main body (two on the courier).
    for (int i = 0; i < NozzleCount(out.compact); ++i) {
      out.nozzles.push_back(Area{
          static_cast<float>(body->box.x - 15.0),
          static_cast<float>(body->box.y +
                             (out.compact ? 35.0 + i * 85.0 : 40.0 + i * 73.0)),
          15.0f, 38.0f});
    }
    // Power conduit along the bottom of the main body.
    out.conduit = Area{static_cast<float>(plan.corridor.x),
                       static_cast<float>(body->box.y + body->box.h - 18.0),
                       static_cast<float>(plan.corridor.w), 12.0f};
  }

  // Cosmetic bay dividers inside the cargo hold.
  for (const Room& room : plan.rooms) {
    if (room.id != "cargo") { continue; }
    for (int k = 1; k <= 2; ++k) {
      out.dividers.push_back(
          Area{static_cast<float>(room.box.x + (room.box.w / 3.0) * k),
               static_cast<float>(room.box.y + 26.0), 1.0f,
               static_cast<float>(room.box.h - 40.0)});
    }
  }

  // Crew dots. A hand with no job is drawn in the corridor, which is the
  // reference's way of showing the player their whole payroll at a glance.
  for (const CrewMember& crew : sim.Crew()) {
    const Room* room = CrewRoom(sim, crew);
    Area box = room != nullptr
                   ? Area{static_cast<float>(room->box.x),
                          static_cast<float>(room->box.y),
                          static_cast<float>(room->box.w),
                          static_cast<float>(room->box.h)}
                   : Area{static_cast<float>(plan.corridor.x),
                          static_cast<float>(plan.corridor.y),
                          static_cast<float>(plan.corridor.w),
                          static_cast<float>(plan.corridor.h)};
    DiagramCrew dot;
    dot.id = crew.id;
    dot.firstName = FirstName(crew.name);
    dot.colour = crew.colour;
    dot.roomId = room != nullptr ? room->id : std::string("corridor");
    dot.dot.x = std::round(DotSpot(box, false, out.compact, rng));
    dot.dot.y = std::round(DotSpot(box, true, out.compact, rng));
    dot.dot.w = 15.0f;
    dot.dot.h = 15.0f;
    dot.label = Area{dot.dot.x - 8.0f, dot.dot.y + 14.0f, 0.0f, 0.0f};
    dot.labelled = !hideCrewLabels;
    out.crew.push_back(dot);
  }

  return out;
}

}  // namespace ship
