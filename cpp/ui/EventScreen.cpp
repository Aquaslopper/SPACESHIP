// ============================================================================
//  EventScreen.cpp — the report, its stage, and its remedy.
// ============================================================================
#include "ui/EventScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

const IncidentType* SpecOf(const Simulation& sim, const Incident& incident) {
  for (const IncidentType& type : sim.World().incidentTypes) {
    if (type.id == incident.type) { return &type; }
  }
  return nullptr;
}

}  // namespace

EventScreenModel BuildEventScreen(const Simulation& sim, const std::string& eventId) {
  EventScreenModel model;
  model.head.kind = "EVENT";
  const Incident* incident = nullptr;
  for (const Incident& candidate : sim.Events()) {
    if (candidate.id == eventId) { incident = &candidate; }
  }
  model.report.id = "report";
  model.report.title = "Report";
  if (incident == nullptr) {
    model.head.title = "Report closed";
    model.head.sub = "Nothing is outstanding under this heading.";
    model.report.Dim("This report has already been resolved.");
    return model;
  }
  model.known = true;
  const IncidentType* spec = SpecOf(sim, *incident);
  model.head.title = spec == nullptr ? std::string("Report") : spec->title;
  model.head.sub = "Reported by " + incident->source;

  const IncidentType::Stage* stage = sim.IncidentStage(*incident);
  if (stage != nullptr) {
    model.report.Field("Condition", stage->text);
    model.report.Field("Next change", Num(incident->left) + " clicks");
  }
  if (incident->type == "crew" && !incident->a.empty() && !incident->b.empty()) {
    model.report.Field("Involved", sim.CrewNameOf(incident->a) + " / " +
                                       sim.CrewNameOf(incident->b));
  }
  // What leaving it open costs, read from the report's own stages.
  bool hitsHull = false;
  bool hitsEnergy = false;
  if (spec != nullptr) {
    for (const IncidentType::Stage& step : spec->stages) {
      hitsHull = hitsHull || (step.hull.has_value() && *step.hull < 0);
      hitsEnergy = hitsEnergy || (step.energy.has_value() && *step.energy < 0);
    }
  }
  std::string leftOpen = "Left open it gets worse each time the count runs out.";
  if (hitsHull) {
    leftOpen += " It damages the hull; with the hull at " +
                Num(sim.GameRules().reports.hullLine) +
                "% or less the same damage also hits the fuel.";
  }
  if (hitsEnergy) {
    leftOpen += " It drains the energy; with the energy at " +
                Num(sim.GameRules().reports.energyLine) +
                "% or less the same loss also hits the shields.";
  }
  model.report.Note(leftOpen);

  const bool needsKit = spec != nullptr && spec->requiresKit.value_or(spec->kind != "Crew");
  ActionRowView remedy;
  remedy.id = "remedy:" + eventId;
  remedy.label = spec == nullptr ? std::string("Resolve the report") : spec->remedy;
  if (needsKit) {
    remedy.label += sim.State().repairKits > 0 ? " - use one repair kit"
                                               : " - needs a repair kit, none aboard";
  }
  remedy.cost = "1 click";
  remedy.enabled = (spec != nullptr) && (!needsKit || sim.State().repairKits > 0);
  model.rows.push_back(remedy);

  return model;
}

namespace {
bool PressEventActionRaw(Simulation& sim, const std::string& action, const std::string& arg) {
  (void)arg;
  if (action.compare(0, 7, "remedy:") == 0) { return sim.RemedyIncident(action.substr(7)); }
  return false;
}

}  // namespace

// ============================================================================
//  The press, and the clicks it costs.
//  ---------------------------------------------------------------------------
//  One control is one control. A press spends one of the clicks that control
//  holds and returns one to every other control, so a button is only refused
//  once its own green has run out. Free reads spend nothing and return nothing.
// ============================================================================
bool PressEventAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ButtonChargeKey(sim, "press:event:" + action + ":" + arg),
      "systems",  // HTA explicitly marks remedy as systems.
      [&]() { return PressEventActionRaw(sim, action, arg); });
}

}  // namespace ship
