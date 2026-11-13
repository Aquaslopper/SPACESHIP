#include "ui/EngineScreen.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
  stage.width = 650.0f;
  stage.height = 575.0f;
  stage.typePx = 18.0f;  // every caption box below holds its caption at 18 px
  std::vector<MachinePart>& parts = stage.parts;

  // --- chassis corner brackets -------------------------------------------
  const float BRK[4][4] = {{16.0f, 16.0f, 1.0f, 1.0f},
                           {634.0f, 16.0f, -1.0f, 1.0f},
                           {16.0f, 559.0f, 1.0f, -1.0f},
                           {634.0f, 559.0f, -1.0f, -1.0f}};
  for (const auto& brk : BRK) {
    const float x = brk[0];
    const float y = brk[1];
    parts.push_back(Part("mach-brk", brk[2] > 0 ? x : x - 20.0f, y, 20.0f, 2.0f));
    parts.push_back(Part("mach-brk", x, brk[3] > 0 ? y : y - 20.0f, 2.0f, 20.0f));
  }

  // --- the main pipe run, riveted, five valves ---------------------------
  // The valve names stand clear above the valves.
  parts.push_back(Part("mach-pipe", 30, 34, 576, 10));
  for (int k = 0; k < 20; ++k) {
    parts.push_back(Part("mach-rivet", 40.0f + k * 28.0f, 37, 4, 4));
  }
  for (int k = 0; k < 5; ++k) {
    const float x = 56.0f + k * 114.0f;
    parts.push_back(Part("mach-valve", x, 26, 16, 26));
    parts.push_back(Part("mach-cap", x - 8.0f, 6, 32, 20, "V" + std::to_string(k + 1)));
  }

  // --- the coolant circuit, a closed loop --------------------------------
  parts.push_back(Part("mach-pipe", 602, 34, 10, 276));   // right fall
  parts.push_back(Part("mach-pipe", 188, 300, 424, 10));  // cold return
  parts.push_back(Part("mach-pipe", 188, 258, 10, 42));   // core elbow
  parts.push_back(Part("mach-pipe", 533, 44, 10, 78));    // exchanger feed
  parts.push_back(Part("mach-pipe", 533, 222, 10, 78));   // exchanger drain
  for (int k = 0; k < 5; ++k) {
    parts.push_back(Part("mach-flow", 240.0f + k * 84.0f, 303, 5, 4));
  }

  // --- reactor core: rings, encoder ticks, bolts, mounts -----------------
  const float cx = 270.0f;
  const float cy = 188.0f;
  parts.push_back(Part("mach-mount", 176, 180, 12, 16));
  parts.push_back(Part("mach-mount", 356, 180, 12, 16));
  parts.push_back(Part("mach-mount", 262, 94, 16, 12));
  parts.push_back(Part("mach-ring", 188, 106, 164, 164));
  parts.push_back(Part("mach-ring2", 212, 130, 118, 118));
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
  parts.push_back(Part("mach-hub", 252, 170, 34, 34));
  parts.push_back(Part("mach-core", 263, 181, 14, 14));
  parts.push_back(Part("mach-cap", 188, 276, 164, 20, "PRIMARY CORE"));

  // --- linkage, flywheel and governor ------------------------------------
  parts.push_back(Part("mach-rod", 356, 158, 38, 6));
  parts.push_back(Part("mach-rod", 356, 238, 42, 6));

  parts.push_back(Part("mach-wheel", 386, 120, 76, 76));
  parts.push_back(Part("mach-ring2", 402, 136, 46, 46));
  parts.push_back(Part("mach-spoke", 386, 159, 80, 2));
  parts.push_back(Part("mach-spoke", 425, 120, 2, 80));
  for (int k = 0; k < 8; ++k) {  // blades
    const double a = k * M_PI / 4.0 + M_PI / 8.0;
    parts.push_back(Part("mach-tick",
                         Round(426.0f + 30.0f * static_cast<float>(std::cos(a))) - 3.0f,
                         Round(160.0f + 30.0f * static_cast<float>(std::sin(a))) - 3.0f,
                         6, 6));
  }
  parts.push_back(Part("mach-hub2", 418, 152, 16, 16));

  parts.push_back(Part("mach-rod", 424, 196, 4, 20));  // belt
  parts.push_back(Part("mach-wheel", 398, 214, 52, 52));
  parts.push_back(Part("mach-spoke", 398, 241, 56, 2));
  parts.push_back(Part("mach-spoke", 425, 214, 2, 56));
  for (int k = 0; k < 6; ++k) {
    const double a = k * M_PI / 3.0;
    parts.push_back(Part("mach-tick",
                         Round(426.0f + 20.0f * static_cast<float>(std::cos(a))) - 2.0f,
                         Round(242.0f + 20.0f * static_cast<float>(std::sin(a))) - 2.0f,
                         5, 5));
  }
  parts.push_back(Part("mach-hub2", 420, 236, 12, 12));
  // Centred under the two wheels, on the row of the core's caption.
  parts.push_back(Part("mach-cap", 356, 276, 140, 20, "TURBINE STAGE"));

  // --- heat exchanger, finned --------------------------------------------
  // The box is as wide as its caption, with the nine fins across it.
  parts.push_back(Part("mach-box", 476, 122, 124, 100));
  parts.push_back(Part("mach-mini", 482, 126, 112, 20, "EXCHANGER"));
  for (int k = 0; k < 9; ++k) {
    parts.push_back(Part("mach-fin", 486.0f + k * 12.0f, 150, 8, 66));
  }

  // --- two dial gauges down the left -------------------------------------
  // Each box is as wide as its reading line: name, dial, reading.
  const char* gaugeName[2] = {"PRS", "TMP"};
  const int gaugeValue[2] = {68, 41};
  const char* gaugeWord[2] = {"68 bar - nominal", "41 pct - nominal"};
  for (int k = 0; k < 2; ++k) {
    const float p = 60.0f + k * 104.0f;
    const float gx = 96.0f;
    const float gy = p + 49.0f;
    parts.push_back(Part("mach-box", 16, p, 160, 98));
    parts.push_back(Part("mach-mini", 22, p + 4.0f, 148, 20, gaugeName[k]));
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
    parts.push_back(Part("mach-mini2", 22, p + 76.0f, 148, 20, gaugeWord[k]));
  }

  // --- the indicator bus: eight lamps ------------------------------------
  const char* lampState[8] = {"on", "on", "amb", "on", "on", "", "on", "on"};
  parts.push_back(Part("mach-box", 16, 268, 160, 54));
  parts.push_back(Part("mach-mini", 22, 272, 148, 20, "BUS A / B"));
  for (int k = 0; k < 8; ++k) {
    const std::string cls = std::string("mach-lamp") +
                            (lampState[k][0] != '\0'
                                 ? std::string(" mach-lamp-") + lampState[k]
                                 : std::string());
    parts.push_back(Part(cls, 24.0f + (k % 4) * 40.0f,
                         294.0f + (k > 3 ? 14.0f : 0.0f), 8, 8));
  }

  // --- statistics panel ---------------------------------------------------
  // Nine rows in two columns, read down the first and then down the second.
  // A row is "label|value": the painter sets the two at opposite edges.
  const char* stats[9][2] = {{"Output", "4.21 TJ"},   {"Flux", "0.98"},
                             {"Coolant", "STABLE"},   {"Flow", "83 pct"},
                             {"Bearing", "0.07 mm"},  {"Cycles", "18,402"},
                             {"Manifold", "2 of 2"},  {"Tolerance", "+/- 0.4%"},
                             {"Last svc", "412 hr"}};
  parts.push_back(Part("mach-box", 16, 330, 416, 137));
  parts.push_back(Part("mach-mini", 24, 334, 400, 20, "DRIVE STATISTICS"));
  for (int k = 0; k < 9; ++k) {
    parts.push_back(Part("mach-stat", 24.0f + (k / 5) * 214.0f,
                         356.0f + (k % 5) * 21.0f, 186, 21,
                         std::string(stats[k][0]) + "|" + stats[k][1]));
  }

  // --- cylinder bank ------------------------------------------------------
  // Sixteen cylinders across the width of their caption.
  parts.push_back(Part("mach-mini", 444, 334, 190, 20, "CYLINDER BANK 1-16"));
  for (int k = 0; k < 16; ++k) {
    parts.push_back(Part("mach-cyl", 444.0f + k * 12.0f,
                         358.0f + (k % 2 ? 10.0f : 0.0f), 9,
                         (k % 2 ? 34.0f : 44.0f)));
  }

  const int condition = static_cast<int>(sim.State().engineCondition);
  parts.push_back(Part("mach-box", 16, 475, 618, 84));
  parts.push_back(Part("mach-mini", 24, 479, 300, 20, "ENGINE CONDITION"));
  parts.push_back(Part("mach-track2", 24, 501, 420, 14));
  MachinePart fill = Part("mach-fill2", 24, 501, 420, 14);
  fill.box.w = Round(420.0f * (static_cast<float>(condition) / 100.0f));
  parts.push_back(fill);
  for (int k = 0; k < 11; ++k) {
    parts.push_back(Part("mach-scale", 24.0f + k * 42.0f, 519, 1, 5));
  }
  parts.push_back(Part("mach-plate", 24, 535, 602, 20,
                       "DRIVE UNIT K-4 . SER " + sim.Ship().Get("registry") +
                           "-E . BANK 1-16"));
  parts.push_back(
      Part("mach-pct", 470, 477, 156, 38, Num(condition) + "%"));
  parts.push_back(Part("mach-word", 470, 515, 156, 20,
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
                         "click of the clock."};

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
  wear.Field("Rate", "1% per " + Num(wearEvery) + " clicks");
  wear.Field("Next step", Num(untilNext) + " click" + (untilNext == 1.0 ? "" : "s"));
  wear.Field("Full cycle", Num(100.0 * wearEvery) + " clicks");
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
  const double missing = std::max(0.0, 100.0 - model.condition);
  const double presses = std::ceil(missing / 10.0);
  NavPanel maint;
  maint.id = "eng-maint";
  maint.title = "Maintenance";
  maint.placement = "np-half";
  if (model.condition >= 100) {
    maint.Dim("Engine is at full condition. Nothing to maintain.");
  } else {
    maint.Field("Missing", Num(missing) + "%");
    maint.Field("Per click", "+10%");
    maint.Field("Presses to full",
                Num(presses) + " click" + (presses == 1.0 ? "" : "s"));
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

  const CrewMember* engineer = sim.Engineer();
  NavPanel crew;
  crew.id = "eng-crew";
  crew.title = "Engineering Crew";
  crew.placement = "np-last";
  if (engineer != nullptr) {
    const double startsAt = sim.GameRules().engine.engineerAt;
    crew.Name(engineer->name);
    crew.Sub(engineer->role + " - maintenance job");
    crew.Field("Skill", engineer->skill);
    crew.Field("Acts at", JobThreshold(sim, *engineer));
    crew.Field("Activity", engineer->activity);
    crew.Note("Holds the maintenance job. At " + Num(startsAt) +
              "% he presses Run the drive (+10% per press) at his trained pace "
              "until the drive is full or the 4-click green bar is empty.");
  } else {
    crew.Dim("Nobody holds the maintenance job. The commander runs it "
             "personally, or nobody does.");
  }
  screen.right.push_back(crew);

  if (!sim.Crew().empty()) {
    screen.bandNote = "At " + Num(sim.GameRules().engine.engineerAt) +
                      "% the engineer presses this button (+10% per press) "
                      "until the drive is full.";
  }

  screen.stage = BuildEngineMachine(sim);

  const bool maintained = sim.State().engineCondition >= 100.0;
  // No charge bar: the drive is workable whenever there is work to do.
  const bool live = !maintained;
  LiveControl run;
  run.id = "eng-run";
  run.box = Area{444.0f, 412.0f, 190.0f, 34.0f};
  run.button.taskId = "engine-click";
  run.button.idleLabel = "Run the drive";
  run.button.text = "Run the drive";
  run.button.title = maintained
      ? "The drive is at full condition; there is nothing to maintain."
      : "One click of the clock restores up to 10% of the drive.";
  run.button.chargeKey.clear();
  run.button.fillPercent = 0.0;
  run.button.enabled = live;
  run.button.inert = !live;
  run.button.state = live ? ButtonState::Idle : ButtonState::Off;
  screen.stage.controls.push_back(run);
  screen.hasStage = true;
  return model;
}

}  // namespace ship
