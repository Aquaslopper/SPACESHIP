// ============================================================================
//  phase8_tests.cpp — the handbook, the menu, the loss screen and the sound.
//  ---------------------------------------------------------------------------
//  Phase 8 closes the port. Its contract: the handbook is the reference's
//  twenty-one entries with a working search; the menu and the loss screen are
//  free and never advance the clock; the soundscape's numbers are the
//  reference's, and its options gate it exactly.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audio/Audio.h"
#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/Popups.h"
#include "ui/Shell.h"
#include "ui/RoomsScreen.h"
#include "ui/TitleScreen.h"
#include "ui/Widgets.h"
#include "ui/WikiScreen.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::NewVoyage(World()); }

std::string ScratchDir(const std::string& name) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("spaceship-phase8-" + name);
  std::filesystem::create_directories(dir);
  return dir.string();
}

const ship::ActionRowView* Row(const std::vector<ship::ActionRowView>& rows,
                               const std::string& id) {
  for (const ship::ActionRowView& row : rows) {
    if (row.id == id) { return &row; }
  }
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
//  The handbook
// ---------------------------------------------------------------------------
TEST_CASE("the handbook has the reference's twenty-one entries, grouped",
          "[phase8][wiki]") {
  ship::Simulation sim = Fresh();
  REQUIRE(World().wiki.size() == 21);
  CHECK(sim.World().counts.wiki == 21);

  const ship::WikiView view;
  const ship::WikiModel model = ship::BuildWikiScreen(sim, view);
  CHECK(model.head.kind == "WIKI");
  CHECK(model.head.title == "Ship's Handbook");
  CHECK(model.index.size() == 21);
  CHECK(model.emptyNote.empty());
  REQUIRE(model.selected != nullptr);
  CHECK(model.selected->id == "first-watch");
  // Group headings appear once, ahead of their run, in catalogue order.
  std::vector<std::string> groups;
  for (const ship::WikiIndexEntry& entry : model.index) {
    if (!entry.group.empty()) { groups.push_back(entry.group); }
  }
  REQUIRE_FALSE(groups.empty());
  CHECK(groups.front() == "Getting started");
  CHECK(groups.back() == "Rewards");
  for (std::size_t i = 1; i < groups.size(); ++i) { CHECK(groups[i] != groups[i - 1]); }
}

TEST_CASE("search is a case-insensitive match over title, group and body",
          "[phase8][wiki]") {
  ship::Simulation sim = Fresh();

  ship::WikiView title;
  title.query = "SILICON";
  const ship::WikiModel byTitle = ship::BuildWikiScreen(sim, title);
  REQUIRE(byTitle.index.size() == 1);
  CHECK(byTitle.selected->id == "signal");

  ship::WikiView group;
  group.query = "survival";
  const ship::WikiModel byGroup = ship::BuildWikiScreen(sim, group);
  CHECK(byGroup.index.size() >= 4);   // the four Survival entries, at least
  // Every Survival entry is in the index; the word also appears in other
  // bodies, which is the reference's own behaviour, not a mistake here.
  for (const ship::WikiEntry& entry : World().wiki) {
    if (entry.group != "Survival") { continue; }
    bool found = false;
    for (const ship::WikiIndexEntry& row : byGroup.index) {
      if (row.entry->id == entry.id) { found = true; }
    }
    CHECK(found);
  }

  ship::WikiView body;
  body.query = "palimpsest";
  const ship::WikiModel byBody = ship::BuildWikiScreen(sim, body);
  REQUIRE(byBody.index.size() == 1);
  CHECK(byBody.selected->id == "signal");

  // The selection falls back to the first match when it is filtered away.
  ship::WikiView narrowed;
  narrowed.query = "recyclers";
  const ship::WikiModel fallback = ship::BuildWikiScreen(sim, narrowed);
  REQUIRE_FALSE(fallback.index.empty());
  CHECK(fallback.selected == fallback.index.front().entry);
  CHECK(fallback.index.front().selected);

  ship::WikiView nothing;
  nothing.query = "no such phrase in the handbook";
  const ship::WikiModel empty = ship::BuildWikiScreen(sim, nothing);
  CHECK(empty.index.empty());
  CHECK(empty.selected == nullptr);
  CHECK(empty.emptyNote == "No matching entries.");
}

TEST_CASE("the story entry sits behind a spoiler gate that is free to open",
          "[phase8][wiki]") {
  ship::Simulation sim = Fresh();
  ship::WikiView view;
  view.selected = "signal";
  const ship::WikiModel hidden = ship::BuildWikiScreen(sim, view);
  REQUIRE(hidden.selected != nullptr);
  CHECK(hidden.selected->spoiler);
  CHECK(hidden.gated);
  CHECK(hidden.body.find("Palimpsest") == std::string::npos);
  REQUIRE(Row(hidden.rows, "reveal") != nullptr);
  CHECK(Row(hidden.rows, "reveal")->cost == "free");

  // Revealing is free: the clock does not move.
  const double before = sim.ActionCount();
  CHECK(ship::PressWikiAction(sim, "reveal", "", &view));
  CHECK(sim.ActionCount() == before);
  CHECK(view.revealed);

  const ship::WikiModel shown = ship::BuildWikiScreen(sim, view);
  CHECK_FALSE(shown.gated);
  CHECK(shown.body == shown.selected->text);
  CHECK(Row(shown.rows, "reveal") == nullptr);
  // The gate can be closed again without touching the voyage.
  CHECK(ship::PressWikiAction(sim, "hide-spoilers", "", &view));
  CHECK_FALSE(view.revealed);

  // A non-spoiler entry is never gated.
  view.selected = "controls";
  const ship::WikiModel plain = ship::BuildWikiScreen(sim, view);
  CHECK_FALSE(plain.gated);
  CHECK(plain.body == plain.selected->text);
}

TEST_CASE("a handbook entry opens its own room, and nothing costs an action",
          "[phase8][wiki]") {
  ship::Simulation sim = Fresh();
  ship::WikiView view;
  view.selected = "air";
  const ship::WikiModel model = ship::BuildWikiScreen(sim, view);
  const ship::ActionRowView* link = Row(model.rows, "link");
  REQUIRE(link != nullptr);
  CHECK(link->label == "Open O2 Filter Room");
  CHECK(link->cost == "free");

  const double before = sim.ActionCount();
  CHECK(ship::PressWikiAction(sim, "link", "", &view));
  CHECK(sim.ActionCount() == before);
  CHECK(sim.State().activeTab == "room:o2");

  // Selecting another entry is free, and an unknown entry is refused.
  CHECK(ship::PressWikiAction(sim, "entry", "markets", &view));
  CHECK(view.selected == "markets");
  CHECK_FALSE(ship::PressWikiAction(sim, "entry", "no-such-entry", &view));
  CHECK(sim.ActionCount() == before);

  // The nav entry is the chart, not a room.
  view.selected = "charts";
  CHECK(ship::PressWikiAction(sim, "link", "", &view));
  CHECK(sim.State().activeTab == "nav:main");
}

TEST_CASE("the handbook is a routed window with an index and an article",
          "[phase8][wiki][router]") {
  ship::Simulation sim = Fresh();
  ship::Tab tab;
  tab.kind = "wiki";
  tab.id = "main";
  tab.title = "Ship's Handbook";
  tab.kindLabel = "WIKI";
  CHECK(ship::ScreenKindFor(tab) == ship::ScreenKind::Wiki);

  ship::WikiView view;
  view.query = "power";
  const ship::TaskScreenModel screen = ship::BuildTabScreen(sim, tab, view);
  CHECK(screen.head.kind == "WIKI");
  REQUIRE(screen.panels.size() == 2);
  CHECK(screen.panels[0].title == "Index");
  CHECK_FALSE(screen.panels[1].title.empty());
  REQUIRE(screen.panelRows.size() == 2);
  // The index carries exactly the filtered entries, each pressable.
  CHECK(screen.panelRows[0].size() == screen.panelRowIds[0].size());
  CHECK_FALSE(screen.panelRowIds[0].empty());
  for (const std::string& id : screen.panelRowIds[0]) {
    CHECK(id.compare(0, 6, "entry:") == 0);
  }
  // A search with no matches says so and offers nothing to press.
  view.query = "zzz-nothing";
  const ship::TaskScreenModel empty = ship::BuildTabScreen(sim, tab, view);
  CHECK(empty.panelRows[0].empty());
  CHECK_FALSE(empty.panels[0].lines.empty());
}

TEST_CASE("no window the ship can open falls back to a placeholder",
          "[phase8][router]") {
  ship::Simulation sim = Fresh();
  // Every compartment, both store rooms and the quarters, the chart, a station,
  // a crew record, the logbook, the handbook and an incident report.
  std::vector<ship::Tab> tabs;
  const auto add = [&tabs](const std::string& kind, const std::string& id) {
    ship::Tab tab;
    tab.kind = kind;
    tab.id = id;
    tab.title = id;
    tabs.push_back(tab);
  };
  add("nav", "main");
  add("wiki", "main");
  add("awards", "main");
  for (const ship::Room& room : sim.Rooms()) { add("room", room.id); }
  for (const ship::CrewMember& crew : sim.Crew()) { add("crew", crew.id); }

  for (const ship::Tab& tab : tabs) {
    const ship::ScreenKind kind = ship::ScreenKindFor(tab);
    // `Described` is the reference's own briefing frame: only a hull room that
    // owns no work may use it, and nothing may be "deferred" any more.
    if (kind == ship::ScreenKind::Described) {
      CHECK(tab.kind == "room");
      CHECK_FALSE(ship::RoomHasScreen(sim, tab.id));
    }
    const ship::TaskScreenModel screen = ship::BuildTabScreen(sim, tab);
    CHECK_FALSE(screen.head.title.empty());
  }
}

// ---------------------------------------------------------------------------
//  The main menu
// ---------------------------------------------------------------------------
TEST_CASE("the menu offers six free commands and says resume or exit",
          "[phase8][wikititle]") {
  ship::Simulation sim = Fresh();
  const std::string dir = ScratchDir("title-menu");
  const ship::TitleModel fresh = ship::BuildTitleScreen(sim, false,
                                                        ship::TitlePanel::None, dir);
  CHECK(fresh.heading == "SPACESHIP");
  CHECK(fresh.artPath == "assets/art/title.jpg");
  REQUIRE(fresh.actions.size() == 5);
  CHECK(fresh.actions[0].id == "new-voyage");
  CHECK(fresh.actions[0].label == "NEW VOYAGE");
  CHECK(fresh.actions[1].id == "load");
  CHECK(fresh.actions[2].id == "options");
  CHECK(fresh.actions[3].id == "guide");
  CHECK(fresh.actions[4].id == "resume");
  CHECK(fresh.actions[4].label == "EXIT");
  CHECK(fresh.actions[1].enabled);
  for (const ship::ActionRowView& row : fresh.actions) {
    CHECK(row.cost == "free");
  }

  const ship::TitleModel live = ship::BuildTitleScreen(sim, true, ship::TitlePanel::None,
                                                       dir);
  CHECK(live.actions[4].label == "RESUME");
  CHECK(ship::TitleButtonLabel(true) == "RESUME");
  CHECK(ship::TitleButtonLabel(false) == "EXIT");
  CHECK_FALSE(ship::AnySlotWritten(sim, dir));
}

TEST_CASE("the menu's three slots read the same summaries the footer does",
          "[phase8][title]") {
  ship::Simulation sim = Fresh();
  const std::string dir = ScratchDir("title-slots");
  std::string error;
  REQUIRE(sim.SaveSlot(2, dir, &error));
  CHECK(ship::AnySlotWritten(sim, dir));

  const ship::TitleModel load = ship::BuildTitleScreen(sim, true, ship::TitlePanel::Load,
                                                       dir, 2);
  CHECK(load.panelTitle == "Load a voyage");
  REQUIRE(load.slots.size() == 3);
  CHECK(load.slots[0].label == "Slot 1 (original)");
  CHECK_FALSE(load.slots[0].written);
  CHECK(load.slots[1].written);
  CHECK(load.slots[1].summary == ship::SaveSlotSummary(sim, 2, dir, nullptr));

  CHECK(Row(load.panelRows, "slot:1") != nullptr);
  CHECK_FALSE(Row(load.panelRows, "slot:1")->enabled);
  CHECK(Row(load.panelRows, "slot:2")->enabled);
  CHECK(Row(load.panelRows, "slot:3")->enabled == false);
  CHECK(Row(load.panelRows, "load-slot") != nullptr);
  CHECK(Row(load.panelRows, "back") != nullptr);
}

TEST_CASE("settings and the illustrated guide are available from the menu",
          "[phase8][title]") {
  ship::Simulation sim = Fresh();
  const std::string dir = ScratchDir("title-settings");
  const ship::TitleModel options = ship::BuildTitleScreen(
      sim, true, ship::TitlePanel::Settings, dir);
  CHECK(options.panelTitle == "Options");
  REQUIRE(options.settings.toggles.size() == 6);
  CHECK(options.panelRows.size() == 6 + 5 + 4 + 4 + 1);  // toggles, palettes, audio, font steps, back
  CHECK(Row(options.panelRows, "audio:0.18") != nullptr);
  CHECK(Row(options.panelRows, "audio:0.9") != nullptr);
  CHECK(Row(options.panelRows, "theme:classic") != nullptr);
  CHECK(Row(options.panelRows, "font:0") != nullptr);
  CHECK(Row(options.panelRows, "font:3") != nullptr);

  // Toggling from the menu is the footer's own action, so the labels agree.
  bool fromVoyage = true;
  ship::TitlePanel panel = ship::TitlePanel::Settings;
  int slot = 1;
  std::string message;
  const bool music = sim.State().options.music;
  CHECK(ship::PressTitleAction(sim, "toggle:music", "", &fromVoyage, &panel, &slot, dir,
                               &message));
  CHECK(sim.State().options.music == !music);
  CHECK(ship::PressTitleAction(sim, "theme:void", "", &fromVoyage, &panel, &slot, dir,
                               &message));
  CHECK(sim.State().options.theme == "void");
  CHECK(ship::PressTitleAction(sim, "font:3", "", &fromVoyage, &panel, &slot, dir,
                               &message));
  CHECK(sim.State().options.fontStep == 3);

  const ship::TitleModel guide = ship::BuildTitleScreen(sim, true, ship::TitlePanel::Guide,
                                                        dir);
  CHECK(guide.tutorialCards == 6);
  CHECK(guide.tutorialCard == 0);
  CHECK(guide.cardImagePath == "assets/art/tutorial/spaceship01.png");

  CHECK(ship::PressTitleAction(sim, "back", "", &fromVoyage, &panel, &slot, dir, &message));
  CHECK(panel == ship::TitlePanel::None);
}

TEST_CASE("the first NEW VOYAGE click starts the boot voyage and dismisses the title",
          "[phase8][title]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.ActionCount() == 0.0);
  bool fromVoyage = false;
  bool titleActive = true;
  ship::TitlePanel panel = ship::TitlePanel::None;
  int slot = 1;
  std::string message;

  CHECK_FALSE(ship::TitleNeedsConfirmation(sim, fromVoyage));
  REQUIRE(ship::PressTitleAction(sim, "new-voyage", "", &fromVoyage, &panel, &slot,
                                 ScratchDir("title-first-click"), &message,
                                 &titleActive));
  CHECK_FALSE(titleActive);
  CHECK_FALSE(fromVoyage);
  CHECK(sim.ActionCount() == 0.0);
  CHECK(message == "A new voyage has begun.");
}

TEST_CASE("a new voyage from the menu starts the same voyage a loss recovers from",
          "[phase8][title]") {
  ship::Simulation sim = Fresh();
  sim.PerformAction("wait");
  REQUIRE(sim.ActionCount() > 0);

  bool fromVoyage = true;
  CHECK(ship::TitleNeedsConfirmation(sim, fromVoyage));
  ship::TitlePanel panel = ship::TitlePanel::Load;
  int slot = 1;
  std::string message;
  CHECK(ship::PressTitleAction(sim, "new-voyage", "", &fromVoyage, &panel, &slot,
                               ScratchDir("title-new"), &message));
  CHECK(sim.ActionCount() == 0);
  CHECK_FALSE(fromVoyage);
  CHECK(panel == ship::TitlePanel::None);
  CHECK(message == "A new voyage has begun.");
}

TEST_CASE("loading from the menu restores the voyage and closes the menu",
          "[phase8][title]") {
  ship::Simulation sim = Fresh();
  const std::string dir = ScratchDir("title-load");
  sim.PerformAction("wait");
  sim.PerformAction("wait");
  const double actions = sim.ActionCount();
  std::string error;
  REQUIRE(sim.SaveSlot(3, dir, &error));

  sim.PerformAction("wait");
  sim.PerformAction("wait");
  REQUIRE(sim.ActionCount() > actions);

  bool fromVoyage = true;
  ship::TitlePanel panel = ship::TitlePanel::Load;
  int slot = 1;
  std::string message;
  CHECK(ship::PressTitleAction(sim, "slot:3", "", &fromVoyage, &panel, &slot, dir,
                               &message));
  CHECK(slot == 3);
  CHECK(ship::PressTitleAction(sim, "load-slot", "", &fromVoyage, &panel, &slot, dir,
                               &message));
  CHECK(sim.ActionCount() == actions);
  CHECK(message == "Saved voyage restored.");
  CHECK(panel == ship::TitlePanel::None);

  // An empty slot refuses, and says why, without ending the menu.
  panel = ship::TitlePanel::Load;
  slot = 1;
  CHECK_FALSE(ship::PressTitleAction(sim, "load-slot", "", &fromVoyage, &panel, &slot,
                                     ScratchDir("title-empty"), &message));
  CHECK(message.find("Could not load") == 0);
}

// ---------------------------------------------------------------------------
//  The loss screen
// ---------------------------------------------------------------------------
TEST_CASE("a lost voyage prints its cause, its record and three ways out",
          "[phase8][loss]") {
  ship::Simulation sim = Fresh();
  CHECK_FALSE(ship::BuildLossScreen(sim).lost);

  sim.MutableState().gameOver = "The hull has failed.";
  const ship::LossModel loss = ship::BuildLossScreen(sim, 2);
  CHECK(loss.lost);
  CHECK(loss.heading == "Ship lost");
  CHECK(loss.cause == "The hull has failed.");
  CHECK(loss.note == "Load a saved game or start a new voyage.");
  CHECK(loss.slot == 2);

  bool hasRank = false;
  for (const ship::PopupRow& row : loss.summary) {
    if (row.key == "Captain's rank") { hasRank = true; }
  }
  CHECK(hasRank);
  REQUIRE(loss.summary.size() >= 6);

  REQUIRE(Row(loss.actions, "load") != nullptr);
  REQUIRE(Row(loss.actions, "new-voyage") != nullptr);
  REQUIRE(Row(loss.actions, "main-menu") != nullptr);
  CHECK(Row(loss.actions, "main-menu")->label == "Return to main menu");
  for (const ship::ActionRowView& row : loss.actions) { CHECK(row.cost == "free"); }
}

TEST_CASE("the loss screen recovers, restarts or returns to the menu",
          "[phase8][loss]") {
  ship::Simulation sim = Fresh();
  const std::string dir = ScratchDir("loss");
  sim.PerformAction("wait");
  const double actions = sim.ActionCount();
  std::string error;
  REQUIRE(sim.SaveSlot(1, dir, &error));

  sim.MutableState().gameOver = "The crew have been lost.";
  int slot = 1;
  bool toTitle = false;
  std::string message;

  CHECK(ship::PressLossAction(sim, "load", "", &slot, &toTitle, dir, &message));
  CHECK(sim.State().gameOver.empty());
  CHECK(sim.ActionCount() == actions);
  CHECK(message == "Saved voyage restored.");
  CHECK_FALSE(toTitle);

  // The menu is a request, not a state change here: the shell hides the ship.
  CHECK(ship::PressLossAction(sim, "main-menu", "", &slot, &toTitle, dir, &message));
  CHECK(toTitle);

  sim.MutableState().gameOver = "The hull has failed.";
  toTitle = false;
  CHECK(ship::PressLossAction(sim, "new-voyage", "", &slot, &toTitle, dir, &message));
  CHECK(sim.State().gameOver.empty());
  CHECK(sim.ActionCount() == 0);

  CHECK(ship::PressLossAction(sim, "slot:3", "", &slot, &toTitle, dir, &message));
  CHECK(slot == 3);
  CHECK_FALSE(ship::PressLossAction(sim, "slot:9", "", &slot, &toTitle, dir, &message));
}

TEST_CASE("the save sheet's return-to-menu command is free and says so",
          "[phase8][popups]") {
  ship::Simulation sim = Fresh();
  const std::string dir = ScratchDir("popup-menu");
  const double before = sim.ActionCount();
  std::string message;
  CHECK(ship::PressPopupAction(sim, "files", "main-menu", "", dir, &message));
  CHECK(message == "Returning to the main menu.");
  CHECK(sim.ActionCount() == before);
}

// ---------------------------------------------------------------------------
//  Sound
// ---------------------------------------------------------------------------
TEST_CASE("the audio level defaults, clamps and multiplies into three gains",
          "[phase8][audio]") {
  ship::Options options;
  CHECK(options.audioLevel == 0.4);
  CHECK(ship::audio::Level(options) == 0.4);

  options.audioLevel = -2.5;
  CHECK(ship::audio::Level(options) == 0.0);
  options.audioLevel = 4.0;
  CHECK(ship::audio::Level(options) == 1.0);
  options.audioLevel = std::nan("");
  CHECK(ship::audio::Level(options) == 0.4);

  options.audioLevel = 0.5;
  const ship::audio::Gains on = ship::audio::GainsFor(options);
  CHECK(on.master == 0.31);           // 0.5 * 0.62
  CHECK(on.effects == 0.8);
  CHECK(on.music == 0.18);
  CHECK(on.effectsOn);
  CHECK(on.musicOn);

  options.soundEffects = false;
  options.music = false;
  const ship::audio::Gains off = ship::audio::GainsFor(options);
  CHECK(off.effects == 0.0);
  CHECK(off.music == 0.0);
  CHECK(off.master == 0.31);          // the master track still exists
  CHECK_FALSE(off.effectsOn);
  CHECK_FALSE(off.musicOn);
}

TEST_CASE("the drone and the phrase are the reference's own notes", "[phase8][audio]") {
  const std::vector<double>& freqs = ship::audio::DroneFreqs();
  const std::vector<double>& levels = ship::audio::DroneLevels();
  REQUIRE(freqs.size() == 3);
  REQUIRE(levels.size() == 3);
  CHECK(freqs[0] == 55.0);
  CHECK(freqs[1] == 82.41);
  CHECK(freqs[2] == 110.0);
  CHECK(levels[0] == 0.10);
  CHECK(levels[1] == 0.055);
  CHECK(levels[2] == 0.022);

  const std::vector<double>& notes = ship::audio::MusicNotes();
  REQUIRE(notes.size() == 6);
  CHECK(notes[0] == 220.0);
  CHECK(notes[1] == 261.63);
  CHECK(notes[2] == 196.0);
  CHECK(notes[3] == 164.81);
  CHECK(notes[4] == 246.94);
  CHECK(notes[5] == 196.0);
  CHECK(ship::audio::MusicNoteIntervalMs() == 2450.0);
  CHECK(ship::audio::MusicNoteLengthSeconds() == 2.3);
  CHECK(ship::audio::MusicNotePeak() == 0.09);
}

TEST_CASE("the sequencer walks the phrase and respects the music switch",
          "[phase8][audio]") {
  ship::Options options;
  ship::audio::Sequencer sequencer;
  sequencer.Configure(options);

  // The first note is due as soon as the drone starts, then every 2450 ms.
  const std::vector<ship::audio::Note> first = sequencer.Advance(0.0);
  REQUIRE(first.size() == 1);
  CHECK(first[0].freq == 220.0);
  CHECK(first[0].durationMs == 2300.0);
  CHECK(sequencer.NoteIndex() == 1);
  CHECK(sequencer.NextDueMs() == 2450.0);

  // Jumping forward emits every note that fell due, at its own time.
  const std::vector<ship::audio::Note> burst = sequencer.Advance(5000.0);
  REQUIRE(burst.size() == 2);
  CHECK(burst[0].atMs == 2450.0);
  CHECK(burst[0].freq == 261.63);
  CHECK(burst[1].atMs == 4900.0);
  CHECK(burst[1].freq == 196.0);

  // Muted: no notes, but the phrase keeps its place.
  options.music = false;
  sequencer.Configure(options);
  const int index = sequencer.NoteIndex();
  const std::vector<ship::audio::Note> muted = sequencer.Advance(9000.0);
  CHECK(muted.empty());
  CHECK(sequencer.NoteIndex() == index);
  CHECK_FALSE(sequencer.MusicOn());
  CHECK(sequencer.NextDueMs() > 9000.0);

  // Unmuted, it resumes rather than replaying the silence.
  options.music = true;
  sequencer.Configure(options);
  const std::vector<ship::audio::Note> resumed = sequencer.Advance(12000.0);
  REQUIRE_FALSE(resumed.empty());
  CHECK(resumed.front().freq ==
        ship::audio::MusicNotes()[static_cast<std::size_t>(index) % 6]);
}

TEST_CASE("a schedule is note for note what the sequencer would play",
          "[phase8][audio]") {
  const std::vector<ship::audio::Note> schedule = ship::audio::ScheduleFrom(0, 7);
  REQUIRE(schedule.size() == 7);
  for (std::size_t i = 0; i < schedule.size(); ++i) {
    CHECK(schedule[i].atMs == static_cast<double>(i) * 2450.0);
    CHECK(schedule[i].freq == ship::audio::MusicNotes()[i % 6]);
    CHECK(schedule[i].durationMs == 2300.0);
    CHECK(schedule[i].peak == 0.09);
  }
}

TEST_CASE("the four cues are chosen by the action's own sentence", "[phase8][audio]") {
  using ship::audio::CueKindFor;
  CHECK(CueKindFor("Deep scan filed") == "ping");
  CHECK(CueKindFor("Sensors calibrated") == "ping");
  CHECK(CueKindFor("Signal intercepted") == "ping");
  CHECK(CueKindFor("Comms relay opened") == "ping");
  CHECK(CueKindFor("Course plotted") == "thrust");
  CHECK(CueKindFor("Travel started") == "thrust");
  // Travel words are tested before the completion words, so a completed jump
  // is still the thrust cue — the reference's own order.
  CHECK(CueKindFor("Jump complete") == "thrust");
  CHECK(CueKindFor("Crossing acknowledged") == "thrust");
  CHECK(CueKindFor("Complete") == "chime");
  CHECK(CueKindFor("Cargo arrangement complete") == "chime");
  CHECK(CueKindFor("Contract paid") == "chime");
  CHECK(CueKindFor("Arrived at Tolme Anchorage") == "chime");
  CHECK(CueKindFor("Repair kit made") == "click");
  CHECK(CueKindFor("") == "click");

  const ship::audio::CueSpec ping = ship::audio::CueSpecFor("ping");
  CHECK(ping.duration == 0.62);
  CHECK(ping.startHz == 570.0);
  CHECK(ping.wave == "sine");
  const ship::audio::CueSpec chime = ship::audio::CueSpecFor("chime");
  CHECK(chime.duration == 0.8);
  CHECK(chime.startHz == 760.0);
  const ship::audio::CueSpec thrust = ship::audio::CueSpecFor("thrust");
  CHECK(thrust.wave == "sawtooth");
  CHECK(thrust.startHz == 142.0);
  const ship::audio::CueSpec click = ship::audio::CueSpecFor("click");
  CHECK(click.wave == "triangle");
  CHECK(click.peak == 0.12);
  // An unknown kind falls back to the click rather than to silence.
  CHECK(ship::audio::CueSpecFor("no-such-cue").kind == "click");
}

TEST_CASE("a cue sweeps and fades the way the oscillator would", "[phase8][audio]") {
  const ship::audio::CueSpec ping = ship::audio::CueSpecFor("ping");
  CHECK(ship::audio::CueFrequencyAt(ping, 0.0) == 570.0);
  CHECK(std::abs(ship::audio::CueFrequencyAt(ping, 0.62) - 1120.0) < 1e-9);
  CHECK(ship::audio::CueFrequencyAt(ping, 0.31) == 570.0 + 550.0 * 0.5);

  const ship::audio::CueSpec thrust = ship::audio::CueSpecFor("thrust");
  CHECK(ship::audio::CueFrequencyAt(thrust, 0.0) == 142.0);
  CHECK(std::abs(ship::audio::CueFrequencyAt(thrust, 0.24) - 58.0) < 1e-6);

  // Envelope: a 25 ms linear attack to the peak, then an exponential fall.
  CHECK(ship::audio::CueEnvelopeAt(ping, 0.0) == 0.0001);
  CHECK(std::abs(ship::audio::CueEnvelopeAt(ping, 0.025) - 0.16) < 1e-12);
  CHECK(ship::audio::CueEnvelopeAt(ping, 0.62) == 0.0001);
  const double mid = ship::audio::CueEnvelopeAt(ping, 0.3);
  CHECK(mid > 0.0001);
  CHECK(mid < 0.16);
  const ship::audio::CueSpec click = ship::audio::CueSpecFor("click");
  CHECK(std::abs(ship::audio::CueEnvelopeAt(click, 0.025) - 0.12) < 1e-12);
}

TEST_CASE("the fallback PCM is 16-bit mono at 8 kHz, sample for sample",
          "[phase8][audio]") {
  const ship::audio::Buffer music = ship::audio::RenderMusic();
  CHECK(music.sampleRate == 8000);
  CHECK(music.Frames() == 64000);   // 8 seconds
  CHECK(music.Seconds() == 8.0);

  const ship::audio::Buffer ping = ship::audio::RenderCue("ping");
  CHECK(ping.Frames() == 3360);     // 0.42 seconds
  // The first sample is the reference's: sin(0) * exp(0) * 0.12 = 0.
  CHECK(ping.samples[0] == 0);
  // Quantisation clamps to ±0.85 and scales by 32767.
  for (const std::int16_t sample : ping.samples) {
    CHECK(sample <= static_cast<std::int16_t>(32767.0 * 0.85 + 0.5));
    CHECK(sample >= static_cast<std::int16_t>(-32767.0 * 0.85 - 0.5));
  }
  // A sample in the middle of the buffer matches the arithmetic, rounded.
  const std::size_t index = 1000;
  const double t = static_cast<double>(index) / 8000.0;
  const double expected = ship::audio::CueSampleAt("ping", t);
  CHECK(ping.samples[index] ==
        static_cast<std::int16_t>(std::llround(std::max(-0.85, std::min(0.85, expected)) * 32767.0)));
  // The music bed starts at zero too, and is bounded the same way.
  CHECK(music.samples[0] == 0);
  for (std::size_t i = 0; i < music.samples.size(); i += 977) {
    const double value = ship::audio::MusicSampleAt(static_cast<double>(i) / 8000.0);
    CHECK(std::abs(value) < 0.85);
  }
}

TEST_CASE("the wave header is the reference's own 44 bytes", "[phase8][audio]") {
  const ship::audio::Buffer cue = ship::audio::RenderCue("click");
  const std::vector<unsigned char> wav = ship::audio::ToWav(cue);
  REQUIRE(wav.size() == 44 + cue.samples.size() * 2);
  CHECK(std::string(wav.begin(), wav.begin() + 4) == "RIFF");
  CHECK(std::string(wav.begin() + 8, wav.begin() + 12) == "WAVE");
  CHECK(std::string(wav.begin() + 12, wav.begin() + 16) == "fmt ");
  CHECK(std::string(wav.begin() + 36, wav.begin() + 40) == "data");
  const std::uint32_t riff = static_cast<std::uint32_t>(wav[4]) |
                             (static_cast<std::uint32_t>(wav[5]) << 8) |
                             (static_cast<std::uint32_t>(wav[6]) << 16) |
                             (static_cast<std::uint32_t>(wav[7]) << 24);
  CHECK(riff == wav.size() - 8);
  const std::uint32_t rate = static_cast<std::uint32_t>(wav[24]) |
                             (static_cast<std::uint32_t>(wav[25]) << 8) |
                             (static_cast<std::uint32_t>(wav[26]) << 16) |
                             (static_cast<std::uint32_t>(wav[27]) << 24);
  CHECK(rate == 8000);
  CHECK(wav[22] == 1);   // mono
  CHECK(wav[34] == 16);  // bits per sample
}
