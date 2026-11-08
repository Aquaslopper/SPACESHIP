// ============================================================================
//  CrewRecord.cpp — Status, Profile, Personality, Employment.
// ============================================================================
#include "ui/CrewRecord.h"

#include "core/JsNumber.h"

namespace ship {

std::string VitalWord(double value, bool bad, const std::vector<std::string>& words) {
  const double good = bad ? 100.0 - value : value;
  if (good >= 70) { return words.empty() ? std::string() : words[0]; }
  if (good >= 40) { return words.size() > 1 ? words[1] : std::string(); }
  return words.size() > 2 ? words[2] : std::string();
}

std::string OutlookWord(const std::string& needKey) {
  if (needKey == "ill") { return "Not feeling well"; }
  if (needKey == "air") { return "Struggling with the thin air"; }
  if (needKey == "untrained") { return "Still finding their feet in the job"; }
  if (needKey == "morale") { return "Morale is wearing thin"; }
  if (needKey == "tired") { return "Running on empty"; }
  if (needKey == "hungry") { return "Could use a meal"; }
  if (needKey == "idle") { return "Idle, no job to focus on"; }
  return "Content";
}

std::string GenderWord(const std::string& sex) {
  if (sex == "m") { return "Male"; }
  if (sex == "f") { return "Female"; }
  return "Unspecified";
}

namespace {

// One vital: the number, the bar, and the word that agrees with the tone.
void AddVital(NavPanel& panel, const Simulation& /*sim*/, const std::string& label,
              double value, bool bad, const std::vector<std::string>& words) {
  panel.Field(label, js::NumToString(value) + "%  " + VitalWord(value, bad, words));
  const double good = bad ? 100.0 - value : value;
  const std::string tone =
      good >= 70 ? "#7f9161" : (good >= 40 ? "#c08a3e" : "#b05a42");
  panel.Bar(value, tone);
}

}  // namespace

CrewRecordModel BuildCrewRecord(const Simulation& sim, const std::string& crewId) {
  CrewRecordModel model;
  const CrewMember* crew = sim.FindCrew(crewId);

  model.head.kind = "CREW";
  if (crew == nullptr) {
    model.head.title = "Crew record";
    model.head.sub = "This hand is no longer aboard.";
    NavPanel gone;
    gone.id = "crew-gone";
    gone.title = "Status";
    gone.Dim("No record.");
    model.shelves.push_back(gone);
    return model;
  }

  model.head.title = crew->name;
  model.head.sub = crew->role;
  model.crewId = crewId;
  model.conversation = BuildConversationRow(sim, crewId);
  model.said = crew->said;

  NavPanel status;
  status.id = "crew-status";
  status.title = "Status";
  status.placement = "np-full";
  AddVital(status, sim, "Health", crew->health, false,
           {"Excellent", "Strained", "Critical"});
  AddVital(status, sim, "Morale", crew->morale, false,
           {"High spirits", "Wavering", "Low morale"});
  AddVital(status, sim, "Fatigue", crew->fatigue, true,
           {"Rested", "Tiring", "Exhausted"});
  AddVital(status, sim, "Hunger", crew->hunger, true,
           {"Well fed", "Peckish", "Starving"});

  NavPanel profile;
  profile.id = "crew-profile";
  profile.title = "Profile";
  profile.Field("Role", crew->role);
  profile.Field("Race", crew->race.empty() ? "Human" : crew->race);
  profile.Field("Gender", GenderWord(crew->sex));
  profile.Field("Age", crew->age > 0 ? js::NumToString(crew->age) + " years" : "Unknown");
  profile.Field("Homeworld", crew->homeworld.empty() ? "Unrecorded" : crew->homeworld);
  profile.Field("Station", sim.CrewWhere(*crew));
  profile.Field("Doing", sim.SimActivityOf(*crew));

  NavPanel personality;
  personality.id = "crew-personality";
  personality.title = "Personality";
  personality.Field("Traits", crew->traits);
  personality.Field("Hobby", crew->hobby.empty() ? "None recorded" : crew->hobby);
  personality.Field("Religion", crew->religion.empty() ? "Unrecorded" : crew->religion);
  personality.Field("Outlook", OutlookWord(sim.NeedKeyFor(*crew)));
  personality.Field("Name anchor", sim.SimAnchorOf(*crew));
  personality.Field("Driving need", sim.SimDriveOf(*crew));
  // HTA simPersonal: newest three involving this hand, including other-party memories.
  int personal = 0;
  for (auto it = sim.State().sim.memories.rbegin();
       it != sim.State().sim.memories.rend() && personal < 3; ++it) {
    if (it->who != crewId && it->other != crewId) { continue; }
    personality.Note("Action " + js::NumToString(it->at) + ": " + it->text);
    ++personal;
  }
  if (personal == 0) { personality.Note("No personal memories recorded yet."); }

  NavPanel employment;
  employment.id = "crew-employment";
  employment.title = "Employment";
  const JobDef* job = sim.FindJobDef(crew->job);
  const double wage = sim.CrewWage(*crew);
  employment.Field("Job", job == nullptr ? "None" : job->label);
  employment.Field("Skill", crew->skill.empty() ? "Untrained" : crew->skill);
  employment.Field("Acts at", sim.JobThreshold(*crew));
  employment.Field("Pay", wage > 0 ? js::NumToString(wage) + " sh each payday"
                                   : "Unpaid - holds no job");
  employment.Field("Next payday", wage > 0 ? js::NumToString(sim.PayrollDue()) + " clicks"
                                           : "Not on payroll");
  employment.Field("Wallet", js::NumToString(crew->wallet) + " sh");

  model.shelves = {status, profile, personality, employment};
  return model;
}

}  // namespace ship
