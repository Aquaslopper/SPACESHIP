// ============================================================================
//  SimCoreScreen.cpp — the memory mosaic, the dream engine and the signal desk.
// ============================================================================
#include "ui/SimCoreScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"
#include "ui/Shell.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

std::string SignalWord(const std::string& signal) {
  return signal == "dormant" ? std::string("None recorded") : signal;
}

}  // namespace

SimScreenModel BuildSimCoreScreen(const Simulation& sim) {
  SimScreenModel model;
  const SimState& simState = sim.State().sim;
  std::vector<Memory> pending;
  for (const Memory& memory : simState.memories) {
    if (!memory.processed) { pending.push_back(memory); }
  }

  model.head.kind = "ROOM";
  model.head.title = "Simulacron Core";
  model.head.sub = "Memory mosaic / dream engine";

  model.body.id = "sim-core";
  model.body.title = "Ship systems";
  model.body.Field("Insight", Num(simState.insight) + " / 5");
  model.body.Field("Unconsolidated",
                   Num(static_cast<double>(pending.size())) +
                       (pending.size() == 1 ? " memory" : " memories"));
  model.body.Field("Next scan", simState.sensor ? "Calibrated" : "Not calibrated");
  model.body.Field("Signal", SignalWord(simState.signal));
  model.body.Note(
      "The observer is the soil; the recursion is the harvest. Name anchors reflect "
      "biographies; Desire Fire follows present needs. The Memory Mosaic records ship "
      "events, not private thoughts. The Dream Engine combines two records into one "
      "insight. A healthy, exhausted crew member may take a short break on their own.");

  // dreamControls: the pair is chosen from the unconsolidated records. The
  // reference offers two selects; here the first three distinct pairs are rows,
  // which is the same choice with the arithmetic done for the reader.
  if (pending.size() >= 2) {
    model.body.Dim(
        "Choose two memories. Two discoveries, arrivals or world reports yield an extra "
        "insight; a shared person gains 2 morale. A matching conflict and reconciliation "
        "also repair that pair's bond by 4.");
    const std::size_t pairs[][2] = {{0, 1}, {1, 2}, {0, 2}};
    for (const auto& pair : pairs) {
      if (pair[1] >= pending.size()) { continue; }
      const Memory& first = pending[pair[0]];
      const Memory& second = pending[pair[1]];
      ActionRowView row;
      row.id = "dream:" + Num(first.id) + ":" + Num(second.id);
      row.label = "Consolidate #" + Num(first.at) + " " + first.kind + " with #" +
                  Num(second.at) + " " + second.kind;
      row.note = first.text + " / " + second.text;
      row.cost = "1 action";
      row.enabled = sim.ProjectBlock("dream").empty();
      model.rows.push_back(row);
    }
  }

  ActionRowView comms;
  comms.id = "open-comms";
  comms.label = "Open Comms Relay";
  comms.cost = "free";
  comms.enabled = true;
  model.rows.push_back(comms);

  // Name anchors and Desire Fire, one line per hand.
  for (const CrewMember& crew : sim.Crew()) {
    model.anchors.push_back(crew.name + " / " + sim.SimAnchorOf(crew) + ": " +
                            sim.SimDriveOf(crew));
  }
  if (!simState.dreams.empty()) {
    model.latestDream = "Latest dream: " + simState.dreams.back().text;
  }
  for (std::size_t i = simState.memories.size(); i-- > 0;) {
    const Memory& memory = simState.memories[i];
    model.memories.push_back("#" + Num(memory.at) + " / " + memory.kind + ": " +
                             memory.text + (memory.processed ? " (consolidated)" : ""));
  }

  // The engine's own button, so the running dream shows its progress.
  const ProjectDef* dream = nullptr;
  for (const ProjectDef& project : sim.World().projects) {
    if (project.id == "dream") { dream = &project; }
  }
  if (dream != nullptr) {
    model.body.Dim(Num(dream->energy) + "% energy; 1 action to begin, then " +
                   Num(dream->work) + " actions of work. Pauses without power.");
    const std::string blocked = sim.ProjectBlock("dream");
    model.buttons.push_back(
        BuildProgressButton(sim, "project-dream", dream->label, blocked.empty()));
  }
  return model;
}

namespace {
bool PressSimCoreActionRaw(Simulation& sim, const std::string& action, const std::string& arg) {
  (void)arg;
  if (action == "open-comms") {
    OpenTask(sim, "room", "r4", true);   // free
    return true;
  }
  if (action.compare(0, 6, "dream:") == 0) {
    const std::string rest = action.substr(6);
    const std::size_t cut = rest.find(':');
    if (cut == std::string::npos) { return false; }
    return sim.StartDream(rest.substr(0, cut), rest.substr(cut + 1));
  }
  return false;
}

}  // namespace

// ============================================================================
//  The press, with the anti-spam lock around it (Phase 9).
//  ---------------------------------------------------------------------------
//  One control is one control. The first press spends the action and starts the
//  tier's lock in ACTION_COOLDOWN_RULES; a second press of the same button
//  before another action resolves is refused, whether it comes from the window
//  or from this harness. Free reads spend nothing and therefore lock nothing.
// ============================================================================
bool PressSimCoreAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ActionCooldownKey(sim, "press:simcore:" + action + ":" + arg),
      ActionCooldownCategory(action + " " + arg),
      [&]() { return PressSimCoreActionRaw(sim, action, arg); });
}

}  // namespace ship
