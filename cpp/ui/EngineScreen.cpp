// ============================================================================
//  EngineScreen.cpp — the machine, part for part.
//  ---------------------------------------------------------------------------
//  Every coordinate below is copied from buildMachine(). The loops are kept as
//  loops so the port can be diffed against the reference by eye: twenty rivets
//  on a 28px pitch, five valves on 114, twenty-four encoder ticks, twelve bolts,
//  eight blades, six turbine ticks, nine exchanger fins, eleven scale marks.
// ============================================================================
#include "ui/EngineScreen.h"

#include <cmath>

#include "core/JsNumber.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

MachinePart Part(const std::string& cls, float x, float y, float w, float h,
                 const std::string& text = std::string()) {
  return MachinePart{cls, Area{x, y, w, h}, text};
}

float Round(float value) { return std::round(value); }

}  // namespace

std::string EngineConditionTone(int condition) {
  if (condition >= 75) { return "#7f9161"; }
  if (condition >= 50) { return "#c08a3e"; }
  if (condition >= 25) { return "#c08a3e"; }
  return "#b05a42";
}

std::string EngineConditionWord(int condition) {
  if (condition >= 75) { return "NOMINAL"; }
  if (condition >= 50) { return "SERVICEABLE"; }
  if (condition >= 25) { return "DEGRADED"; }
  return "CRITICAL";
}

TaskStage BuildEngineMachine(const Simulation& sim) {
  TaskStage stage;
  stage.id = "machine";
  stage.width = 650.0f;   // STAGE.engine
  stage.height = 390.0f;
  std::vector<MachinePart>& parts = stage.parts;

  // --- chassis corner brackets -------------------------------------------
  const float BRK[4][4] = {{16.0f, 16.0f, 1.0f, 1.0f},
                           {634.0f, 16.0f, -1.0f, 1.0f},
                           {16.0f, 374.0f, 1.0f, -1.0f},
                           {634.0f, 374.0f, -1.0f, -1.0f}};
  for (const auto& brk : BRK) {
    const float x = brk[0];
    const float y = brk[1];
    parts.push_back(Part("mach-brk", brk[2] > 0 ? x : x - 20.0f, y, 20.0f, 2.0f));
    parts.push_back(Part("mach-brk", x, brk[3] > 0 ? y : y - 20.0f, 2.0f, 20.0f));
  }

  // --- the main pipe run, riveted, five valves ---------------------------
  parts.push_back(Part("mach-pipe", 30, 26, 576, 10));
  for (int k = 0; k < 20; ++k) {
    parts.push_back(Part("mach-rivet", 40.0f + k * 28.0f, 29, 4, 4));
  }
  for (int k = 0; k < 5; ++k) {
    const float x = 56.0f + k * 114.0f;
    parts.push_back(Part("mach-valve", x, 18, 16, 26));
    parts.push_back(Part("mach-cap", x - 8.0f, 4, 32, 10, "V" + std::to_string(k + 1)));
  }

  // --- the coolant circuit, a closed loop --------------------------------
  parts.push_back(Part("mach-pipe", 602, 26, 10, 268));   // right fall
  parts.push_back(Part("mach-pipe", 124, 284, 488, 10));  // cold return
  parts.push_back(Part("mach-pipe", 124, 250, 10, 34));   // core elbow
  parts.push_back(Part("mach-pipe", 420, 36, 10, 60));    // exchanger feed
  parts.push_back(Part("mach-pipe", 420, 192, 10, 92));   // exchanger drain
  for (int k = 0; k < 5; ++k) {
    parts.push_back(Part("mach-flow", 176.0f + k * 84.0f, 287, 5, 4));
  }

  // --- reactor core: rings, encoder ticks, bolts, mounts -----------------
  const float cx = 208.0f;
  const float cy = 182.0f;
  parts.push_back(Part("mach-mount", 112, 174, 12, 16));
  parts.push_back(Part("mach-mount", 292, 174, 12, 16));
  parts.push_back(Part("mach-mount", 200, 86, 16, 12));
  parts.push_back(Part("mach-ring", 124, 98, 164, 164));
  parts.push_back(Part("mach-ring2", 148, 122, 118, 118));
  for (int k = 0; k < 24; ++k) {  // inner encoder ring
    const double a = k * M_PI / 12.0;
    parts.push_back(Part("mach-tick",
                         Round(cx + 52.0f * static_cast<float>(std::cos(a))) - 2.0f,
                         Round(cy + 52.0f * static_cast<float>(std::sin(a))) - 2.0f, 3,
                         3));
  }
  for (int k = 0; k < 12; ++k) {  // bolts on the band
    const double a = k * M_PI / 6.0;
    parts.push_back(Part("mach-bolt",
                         Round(cx + 72.0f * static_cast<float>(std::cos(a))) - 4.0f,
                         Round(cy + 72.0f * static_cast<float>(std::sin(a))) - 4.0f, 8,
                         8));
  }
  parts.push_back(Part("mach-hub", 190, 164, 34, 34));
  parts.push_back(Part("mach-core", 201, 175, 14, 14));
  parts.push_back(Part("mach-cap", 124, 268, 164, 12, "PRIMARY CORE"));

  // --- linkage, flywheel and governor ------------------------------------
  parts.push_back(Part("mach-rod", 292, 150, 22, 6));
  parts.push_back(Part("mach-rod", 292, 230, 26, 6));

  parts.push_back(Part("mach-wheel", 306, 112, 76, 76));
  parts.push_back(Part("mach-ring2", 322, 128, 46, 46));
  parts.push_back(Part("mach-spoke", 306, 151, 80, 2));
  parts.push_back(Part("mach-spoke", 345, 112, 2, 80));
  for (int k = 0; k < 8; ++k) {  // blades
    const double a = k * M_PI / 4.0 + M_PI / 8.0;
    parts.push_back(Part("mach-tick",
                         Round(346.0f + 30.0f * static_cast<float>(std::cos(a))) - 3.0f,
                         Round(152.0f + 30.0f * static_cast<float>(std::sin(a))) - 3.0f,
                         6, 6));
  }
  parts.push_back(Part("mach-hub2", 338, 144, 16, 16));

  parts.push_back(Part("mach-rod", 344, 188, 4, 20));  // belt
  parts.push_back(Part("mach-wheel", 318, 206, 52, 52));
  parts.push_back(Part("mach-spoke", 318, 233, 56, 2));
  parts.push_back(Part("mach-spoke", 345, 206, 2, 56));
  for (int k = 0; k < 6; ++k) {
    const double a = k * M_PI / 3.0;
    parts.push_back(Part("mach-tick",
                         Round(346.0f + 20.0f * static_cast<float>(std::cos(a))) - 2.0f,
                         Round(234.0f + 20.0f * static_cast<float>(std::sin(a))) - 2.0f,
                         5, 5));
  }
  parts.push_back(Part("mach-hub2", 340, 228, 12, 12));
  parts.push_back(Part("mach-mini", 300, 266, 96, 10, "TURBINE STAGE"));

  // --- heat exchanger, finned --------------------------------------------
  parts.push_back(Part("mach-box", 396, 96, 62, 96));
  parts.push_back(Part("mach-mini", 401, 100, 56, 10, "EXCHANGER"));
  for (int k = 0; k < 9; ++k) {
    parts.push_back(Part("mach-fin", 402.0f + k * 6.0f, 114, 4, 66));
  }

  // --- two dial gauges down the left -------------------------------------
  const char* gaugeName[2] = {"PRS", "TMP"};
  const int gaugeValue[2] = {68, 41};
  const char* gaugeWord[2] = {"68 bar - nominal", "41 pct - nominal"};
  for (int k = 0; k < 2; ++k) {
    const float p = 52.0f + k * 98.0f;
    const float gx = 64.0f;
    const float gy = p + 48.0f;
    parts.push_back(Part("mach-box", 16, p, 96, 92));
    parts.push_back(Part("mach-mini", 22, p + 5.0f, 88, 10, gaugeName[k]));
    parts.push_back(Part("mach-dial", gx - 26.0f, gy - 26.0f, 50, 50));
    for (int i2 = 0; i2 < 11; ++i2) {  // 270 degree tick sweep
      const double a = (135 + i2 * 27) * M_PI / 180.0;
      parts.push_back(Part(
          "mach-tick", Round(gx + 21.0f * static_cast<float>(std::cos(a))) - 1.0f,
          Round(gy + 21.0f * static_cast<float>(std::sin(a))) - 1.0f, 2, 2));
    }
    const double a = (135 + gaugeValue[k] * 2.7) * M_PI / 180.0;
    for (int i2 = 0; i2 < 3; ++i2) {  // the needle, as three dots
      const float r = 7.0f + i2 * 5.0f;
      parts.push_back(Part(
          "mach-nd", Round(gx + r * static_cast<float>(std::cos(a))) - 2.0f,
          Round(gy + r * static_cast<float>(std::sin(a))) - 2.0f, 4, 4));
    }
    parts.push_back(Part("mach-hub2", gx - 4.0f, gy - 4.0f, 8, 8));
    parts.push_back(Part("mach-mini2", 22, p + 74.0f, 88, 10, gaugeWord[k]));
  }

  // --- the indicator bus: eight lamps ------------------------------------
  const char* lampState[8] = {"on", "on", "amb", "on", "on", "", "on", "on"};
  parts.push_back(Part("mach-box", 16, 248, 96, 48));
  parts.push_back(Part("mach-mini", 22, 252, 88, 10, "BUS A / B"));
  for (int k = 0; k < 8; ++k) {
    const std::string cls = std::string("mach-lamp") +
                            (lampState[k][0] != '\0'
                                 ? std::string(" mach-lamp-") + lampState[k]
                                 : std::string());
    parts.push_back(Part(cls, 24.0f + (k % 4) * 20.0f,
                         266.0f + (k > 3 ? 14.0f : 0.0f), 8, 8));
  }

  // --- statistics panel ---------------------------------------------------
  const char* stats[9][2] = {{"Output", "4.21 TJ"},   {"Flux", "0.98"},
                             {"Coolant", "STABLE"},   {"Flow", "83 pct"},
                             {"Bearing", "0.07 mm"},  {"Cycles", "18,402"},
                             {"Manifold", "2 of 2"},  {"Tolerance", "+/- 0.4%"},
                             {"Last svc", "412 hr"}};
  parts.push_back(Part("mach-box", 470, 56, 130, 152));
  parts.push_back(Part("mach-mini", 476, 60, 118, 10, "DRIVE STATISTICS"));
  for (int k = 0; k < 9; ++k) {
    parts.push_back(Part("mach-stat", 476, 74.0f + k * 14.0f, 118, 13,
                         std::string(stats[k][0]) + "|" + stats[k][1]));
  }

  // --- cylinder bank ------------------------------------------------------
  parts.push_back(Part("mach-mini", 470, 214, 130, 10, "CYLINDER BANK 1-16"));
  for (int k = 0; k < 16; ++k) {
    parts.push_back(Part("mach-cyl", 470.0f + k * 8.0f,
                         226.0f + (k % 2 ? 10.0f : 0.0f), 6,
                         (k % 2 ? 34.0f : 44.0f)));
  }

  // --- THE ONE LIVE READING ----------------------------------------------
  const int condition = static_cast<int>(sim.State().engineCondition);
  parts.push_back(Part("mach-box", 16, 310, 618, 64));
  parts.push_back(Part("mach-mini", 24, 316, 160, 10, "ENGINE CONDITION"));
  parts.push_back(Part("mach-track2", 24, 330, 420, 14));
  MachinePart fill = Part("mach-fill2", 24, 330, 420, 14);
  fill.text = "engine-meter";
  fill.box.w = Round(420.0f * (static_cast<float>(condition) / 100.0f));
  parts.push_back(fill);
  for (int k = 0; k < 11; ++k) {
    parts.push_back(Part("mach-scale", 24.0f + k * 42.0f, 348, 1, 5));
  }
  parts.push_back(Part("mach-plate", 24, 358, 340, 10,
                       "DRIVE UNIT K-4 . SER " + sim.Ship().Get("registry") +
                           "-E . BANK 1-16"));
  parts.push_back(
      Part("mach-pct", 470, 314, 158, 32, Num(condition) + "%"));
  parts.push_back(Part("mach-word", 470, 350, 158, 14,
                       EngineConditionWord(condition)));
  return stage;
}

EngineScreenModel BuildEngineScreen(const Simulation& sim) {
  EngineScreenModel model;
  model.condition = static_cast<int>(sim.State().engineCondition);
  model.tone = EngineConditionTone(model.condition);
  model.word = EngineConditionWord(model.condition);
  model.meterWidth =
      Round(420.0f * (static_cast<float>(model.condition) / 100.0f));

  TaskScreenModel& screen = model.screen;
  screen.head = TaskHead{"ROOM", "Engine Room",
                         "Propulsion and power - the drive wears with every "
                         "action spent aboard."};

  // --- left: what the drive is doing -------------------------------------
  NavPanel cond;
  cond.id = "eng-cond";
  cond.title = "Engine Condition";
  cond.Name(Num(model.condition) + "%", model.tone);
  cond.Sub(model.word);
  cond.Field("Threshold", Num(sim.GameRules().engine.warnBelow) + "% - warning line");
  screen.left.push_back(cond);

  const double wearEvery = sim.GameRules().engine.wearEvery;
  const double untilNext = wearEvery - sim.State().wearTick;
  NavPanel wear;
  wear.id = "eng-wear";
  wear.title = "Wear";
  wear.Field("Rate", "1% per " + Num(wearEvery) + " actions");
  wear.Field("Next step",
             Num(untilNext) + " action" + (untilNext == 1.0 ? "" : "s"));
  wear.Field("Full cycle", Num(100.0 * wearEvery) + " actions");
  screen.left.push_back(wear);

  NavPanel drive;
  drive.id = "eng-drive";
  drive.title = "Drive";
  drive.placement = "np-last";
  drive.Field("Class", "Kestrel light hauler");
  drive.Field("Core", "Primary - single");
  drive.Field("Cylinders", "12 in bank 1");
  screen.left.push_back(drive);

  // --- under the machine: maintenance and readings ------------------------
  const double cost = sim.MaintCostActions();
  NavPanel maint;
  maint.id = "eng-maint";
  maint.title = "Maintenance";
  maint.placement = "np-half";
  if (model.condition >= 100) {
    maint.Dim("Engine is at full condition. Nothing to maintain.");
  } else {
    maint.Field("Cost now", Num(cost) + " action" + (cost == 1.0 ? "" : "s"));
    maint.Field("Formula", "1 action per 10% missing");
    maint.Field("Restores", "to 100%");
  }
  screen.under.push_back(maint);

  NavPanel readings;
  readings.id = "eng-read";
  readings.title = "Readings";
  readings.placement = "np-half-last";
  readings.Field("Output", "4.21 TJ");
  readings.Field("Coolant", "Stable");
  readings.Field("Bearing wear",
                 Num(std::round((100.0 - model.condition) * 0.7) / 10.0) + " mm");
  screen.under.push_back(readings);

  // --- right: who is in here ---------------------------------------------
  const CrewMember* engineer = sim.Engineer();
  NavPanel crew;
  crew.id = "eng-crew";
  crew.title = "Engineering Crew";
  crew.placement = "np-last";
  if (engineer != nullptr) {
    const double actsAt = EffectiveJobAt(sim, *engineer);
    crew.Name(engineer->name);
    crew.Sub(engineer->role + " - maintenance job");
    crew.Field("Skill", engineer->skill);
    crew.Field("Acts at",
               Num(actsAt) + "% condition" +
                   (actsAt != engineer->jobAt
                        ? " (air: was " + Num(engineer->jobAt) + ")"
                        : ""));
    crew.Field("Activity", engineer->activity);
    crew.Note("He holds the maintenance job and will service the drive "
              "unasked - but not before it falls to " +
              Num(engineer->jobAt) +
              "%, which is long past the warning. Training at a station, or a "
              "better engineer, raises that.");
  } else {
    crew.Dim("Nobody holds the maintenance job. The commander runs it "
             "personally, or nobody does.");
  }
  screen.right.push_back(crew);

  // --- the commitments band ----------------------------------------------
  screen.bandNote =
      model.condition >= 100
          ? "The drive is at full condition. Maintenance cannot be run on a "
            "perfect engine."
          : "Maintenance will cost " + Num(cost) + " action" +
                (cost == 1.0 ? "" : "s") +
                " spent anywhere aboard, and restores the drive to 100%.";

  if (sim.FindOngoing("maint-eng") != nullptr) {
    screen.progress.push_back(
        BuildProgressButton(sim, "maint-eng", "Engineer working", false));
  } else {
    screen.progress.push_back(BuildProgressButton(
        sim, "maint", "Run Maintenance",
        model.condition < 100 && sim.FindOngoing("project-cooling") == nullptr));
  }

  NavPanel actions;
  actions.id = "eng-actions";
  actions.title = "Engine Actions";
  actions.placement = "np-full";
  screen.band.push_back(actions);

  screen.stage = BuildEngineMachine(sim);
  screen.hasStage = true;
  return model;
}

}  // namespace ship
