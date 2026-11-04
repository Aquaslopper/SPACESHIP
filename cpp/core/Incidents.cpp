// ============================================================================
//  Incidents.cpp — what the ship has noticed and nobody has looked at.
//  ---------------------------------------------------------------------------
//  Reports appear on a fixed schedule measured in actions and escalate if they
//  are ignored: every stage has its own countdown and its own effect on the
//  hull, the shields, the reserve or the crew's morale. Resolving one is worth
//  a commendation tick; leaving one alone costs the ship.
// ============================================================================
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

void Simulation::IncidentEffect(const IncidentType::Stage& step, const Incident& incident) {
  if (incident.type == "crew" && !incident.a.empty() && !incident.b.empty()) {
    SimChangeBond(incident.a, incident.b, -8);
    SimRemember("Conflict",
                SimCrewName(incident.a) + " and " + SimCrewName(incident.b) +
                    " remain in conflict.",
                incident.a, incident.b);
  }
  if (step.hull.has_value()) { ClampStat("hull", *step.hull); }
  if (step.shield.has_value()) { ClampStat("shield", *step.shield); }
  if (step.energy.has_value()) { ClampStat("energy", *step.energy); }
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
  if (found != nullptr && found->type == "crew" && !found->a.empty() && !found->b.empty()) {
    const std::string a = found->a;
    const std::string b = found->b;
    SimChangeBond(a, b, 12);
    SimRemember("Connection",
                SimCrewName(a) + " and " + SimCrewName(b) + " settled their dispute.", a, b);
  }
  for (std::size_t i = events_.size(); i-- > 0;) {
    if (events_[i].id == id) { events_.erase(events_.begin() + static_cast<std::ptrdiff_t>(i)); }
  }
  ClearOngoing("incident-" + id);
  if (found != nullptr) {
    state_.chronicle.incidents += 1;
    CheckCommendations();
  }
}

std::pair<std::string, std::string> Simulation::SimDisputePair() const {
  std::pair<std::string, std::string> pair;
  double worst = 101;
  for (std::size_t i = 0; i < crew_.size(); ++i) {
    for (std::size_t j = i + 1; j < crew_.size(); ++j) {
      const double bond = SimBond(crew_[i].id, crew_[j].id);
      if (bond < worst) {
        worst = bond;
        pair = {crew_[i].id, crew_[j].id};
      }
    }
  }
  return pair;
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
    if (incident.stage >= static_cast<int>(spec->stages.size())) {
      events_.erase(events_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    const IncidentType::Stage& step = spec->stages[static_cast<std::size_t>(incident.stage)];
    IncidentEffect(step, incident);
    LogLine(step.text);

    if (step.ticks <= 0) {
      events_.erase(events_.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      incident.left = step.ticks;
    }
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
    if (spec.id == "crew" && crew_.size() < 2) { continue; }

    Incident incident;
    incident.id = spec.id + "-" + std::to_string(static_cast<long long>(actionCounter_));
    incident.type = spec.id;
    incident.kind = spec.kind;
    incident.source = spec.source;
    incident.stage = 0;
    incident.left = spec.stages.empty() ? 0 : spec.stages[0].ticks;
    if (spec.id == "crew") {
      const std::pair<std::string, std::string> pair = SimDisputePair();
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

}  // namespace ship
