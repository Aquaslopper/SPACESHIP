// ============================================================================
//  Incidents.cpp — what the ship has noticed and nobody has looked at.
//  ---------------------------------------------------------------------------
//  Reports appear on a fixed schedule measured in actions and escalate if they
//  are ignored: every stage has its own countdown and its own effect on the
//  hull, the shields, the reserve or the crew's morale. Resolving one is worth
//  a commendation tick; leaving one alone costs the ship.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>

namespace ship {

const IncidentType* Simulation::IncidentSpec(const Incident& incident) const {
  for (const IncidentType& type : catalog_.incidentTypes) {
    if (type.id == incident.type) { return &type; }
  }
  return nullptr;
}

void Simulation::IncidentEffect(const IncidentType::Stage& step) {
  // Report damage reaches further on a weakened ship: a hit that leaves the
  // hull at or under reports.hullLine takes the same amount of fuel, and a hit
  // that leaves the energy at or under reports.energyLine takes the same
  // amount of shield.
  if (step.hull.has_value()) {
    ClampStat("hull", *step.hull);
    if (*step.hull < 0 && StatValue("hull") <= rules_.reports.hullLine) {
      ClampStat("fuel", *step.hull);
      LogLine("Hull at " + js::NumToString(StatValue("hull")) +
              "%: the same damage also hit the fuel.");
    }
  }
  if (step.shield.has_value()) { ClampStat("shield", *step.shield); }
  if (step.energy.has_value()) {
    ClampStat("energy", *step.energy);
    if (*step.energy < 0 && StatValue("energy") <= rules_.reports.energyLine) {
      ClampStat("shield", *step.energy);
      LogLine("Energy at " + js::NumToString(StatValue("energy")) +
              "%: the same loss also hit the shields.");
    }
  }
  if (step.morale.has_value()) {
    for (CrewMember& crew : crew_) {
      crew.morale = std::max(0.0, crew.morale + *step.morale);
    }
  }
}

void Simulation::RemoveIncident(const std::string& id) {
  const Incident* found = nullptr;
  for (const Incident& incident : events_) {
    if (incident.id == id) { found = &incident; }
  }
  const bool hadIncident = found != nullptr;
  for (std::size_t i = events_.size(); i-- > 0;) {
    if (events_[i].id == id) { events_.erase(events_.begin() + static_cast<std::ptrdiff_t>(i)); }
  }
  if (hadIncident) {
    state_.chronicle.incidents += 1;
    CheckCommendations();
  }
}

const IncidentType::Stage* Simulation::IncidentStage(const Incident& incident) const {
  const IncidentType* spec = IncidentSpec(incident);
  if (spec == nullptr || spec->stages.empty()) { return nullptr; }
  const std::size_t index = static_cast<std::size_t>(
      std::max(0, std::min(incident.stage, static_cast<int>(spec->stages.size()) - 1)));
  return &spec->stages[index];
}

bool Simulation::RemedyIncident(const std::string& incidentId) {
  const Incident* found = nullptr;
  for (const Incident& incident : events_) {
    if (incident.id == incidentId) { found = &incident; }
  }
  if (found == nullptr) { return false; }
  const IncidentType* spec = IncidentSpec(*found);
  if (spec == nullptr || !state_.gameOver.empty()) { return false; }
  const bool needsKit = spec->requiresKit.value_or(spec->kind != "Crew");
  if (needsKit && state_.repairKits < 1) { return false; }

  const std::string remedy = spec->remedy;
  const std::string id = incidentId;
  if (needsKit) { state_.repairKits -= 1; }
  // commitChange() removes the report before the action advances the world.
  RemoveIncident(id);
  PerformAction(remedy);
  return true;
}

// The two hands in the lowest spirits; on equal morale, the earlier on the roster.
std::pair<std::string, std::string> Simulation::DisputePair() const {
  const CrewMember* low = nullptr;
  const CrewMember* next = nullptr;
  for (const CrewMember& crew : crew_) {
    if (low == nullptr || crew.morale < low->morale) {
      next = low;
      low = &crew;
    } else if (next == nullptr || crew.morale < next->morale) {
      next = &crew;
    }
  }
  if (low == nullptr || next == nullptr) { return {}; }
  return {low->id, next->id};
}

void Simulation::TickIncidents() {
  // ---- escalate what is already live ------------------------------------
  for (std::size_t i = events_.size(); i-- > 0;) {
    Incident& incident = events_[i];
    const IncidentType* spec = IncidentSpec(incident);
    if (spec == nullptr) { continue; }

    incident.left -= 1;
    if (incident.left > 0) { continue; }

    incident.stage += 1;
    if (incident.stage >= static_cast<int>(spec->stages.size())) { continue; }

    const IncidentType::Stage& step = spec->stages[static_cast<std::size_t>(incident.stage)];
    IncidentEffect(step);
    LogLine(step.text);
    incident.left = step.ticks;
  }

  // ---- open new ones -----------------------------------------------------
  for (const IncidentType& spec : catalog_.incidentTypes) {
    bool exists = false;
    for (const Incident& incident : events_) {
      if (incident.type == spec.id) { exists = true; }
    }
    if (exists) { continue; }
    if (actionCounter_ < spec.first) { continue; }
    if (spec.every <= 0) { continue; }
    if (std::fmod(actionCounter_ - spec.first, spec.every) != 0) { continue; }
    if ((spec.id == "crew" || spec.kind == "Crew") && crew_.size() < 2) { continue; }

    Incident incident;
    incident.id = spec.id + "-" + std::to_string(static_cast<long long>(actionCounter_));
    incident.type = spec.id;
    incident.kind = spec.kind;
    incident.source = spec.source;
    incident.stage = 0;
    incident.left = spec.stages.empty() ? 0 : spec.stages[0].ticks;
    if (spec.id == "crew") {
      const std::pair<std::string, std::string> pair = DisputePair();
      incident.a = pair.first;
      incident.b = pair.second;
    }
    events_.push_back(incident);

    std::string kind = spec.kind;
    std::transform(kind.begin(), kind.end(), kind.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    LogLine("New " + kind + " report. Open Events to investigate.");
  }
}

bool Simulation::SpawnMapIncident(const std::string& typeId, const std::string& contextNote) {
  if (!state_.gameOver.empty() || events_.size() >= 4) { return false; }
  const IncidentType* spec = nullptr;
  for (const IncidentType& candidate : catalog_.incidentTypes) {
    if (candidate.id == typeId) {
      spec = &candidate;
      break;
    }
  }
  if ((spec->id == "crew" || spec->kind == "Crew") && crew_.size() < 2) { return false; }
  for (const Incident& live : events_) {
    if (live.type == typeId) { return false; }
  }

  Incident incident;
  incident.id = spec->id + "-" + std::to_string(static_cast<long long>(actionCounter_)) + "-" +
                std::to_string(events_.size() + 1);
  incident.type = spec->id;
  incident.kind = spec->kind;
  incident.source = spec->source;
  incident.stage = 0;
  incident.left = spec->stages[0].ticks;
  if (spec->kind == "Crew" && crew_.size() >= 2) {
    const std::pair<std::string, std::string> pair = DisputePair();
    incident.a = pair.first;
    incident.b = pair.second;
  }
  events_.push_back(incident);

  if (!contextNote.empty()) { LogLine(contextNote); }
  LogLine("Navigation alert: " + spec->title + ". Open Events to respond.");
  return true;
}

}  // namespace ship
