// ============================================================================
//  CrewRecord.h — the four-shelf personal record (Section I-1b).
//  ---------------------------------------------------------------------------
//  Opening a crew tab is read-only. Nothing here can spend an action or move a
//  clock, and everything on it is read off the live record at paint time rather
//  than cached on the tab, so a window left open never goes stale.
//
//  Four shelves, because they are four different questions: Status (how are
//  they), Profile (who are they), Personality (what are they like) and
//  Employment (what are they worth to the ship).
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Widgets.h"

namespace ship {

// vitalWord(): a high number is not always a good number. Health 91% is
// excellent; hunger 91% is somebody about to collapse.
std::string VitalWord(double value, bool bad, const std::vector<std::string>& words);

// OUTLOOK_WORD, spelled out for the record. Read-only, unlike crewNeed().
std::string OutlookWord(const std::string& needKey);

// GENDER_WORD: the sex field the sprite code already keeps.
std::string GenderWord(const std::string& sex);

struct CrewRecordModel {
  TaskHead head;
  std::vector<NavPanel> shelves;  // Status, Profile, Personality, Employment
};

CrewRecordModel BuildCrewRecord(const Simulation& sim, const std::string& crewId);

}  // namespace ship
