// ============================================================================
//  phase9_tests.cpp — anti-spam plumbing, the reading-size ladder, the
//  borderless window, the five-card tutorial and the player's own audio.
//  ---------------------------------------------------------------------------
//  Phase 9's promise is that the port is *comfortable* to use: no paid button
//  can be double-clicked, the smallest face is 18 px, the palettes are calm,
//  the window can be borderless, a newcomer gets five cards of guidance, and
//  the soundtrack can be replaced with files. Each of those is held here.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audio/Audio.h"
#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/Popups.h"
#include "ui/Theme.h"
#include "ui/TitleScreen.h"
#include "ui/Widgets.h"

namespace {

namespace fs = std::filesystem;

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::NewVoyage(World()); }

fs::path AssetsDir() {
  const fs::path assets = ship::Catalog::DefaultAssetDir();
  return assets.empty() ? fs::path("assets") : assets;
}

fs::path Scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / ("spaceship-phase9-" + name);
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << text;
}

const ship::PopupToggle* Toggle(const ship::PopupModel& model, const std::string& id) {
  for (const ship::PopupToggle& toggle : model.toggles) {
    if (toggle.id == id) { return &toggle; }
  }
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
//  The reading size: 18 px as a floor, four steps, nothing clipping
// ---------------------------------------------------------------------------
TEST_CASE("the reading size starts at 18px and climbs in four even steps",
          "[phase9][typography]") {
  CHECK(ship::Theme::kFontFloorPx == 18.0f);
  const std::vector<float> expected = {18.0f, 19.0f, 20.0f, 21.0f};
  for (int step = 0; step < 4; ++step) {
    CHECK(ship::Theme::FontPxForStep(step) == expected[static_cast<std::size_t>(step)]);
  }

  // The line box is always taller than the face: that is the whole point of
  // raising the floor, and it is what stops rows of text from clipping.
  for (int step = 0; step < 4; ++step) {
    ship::Theme theme = ship::Theme::ById("classic");
    theme.fontStep = step;
    CHECK(theme.BodySize() >= 18.0f);
    CHECK(theme.LineHeight() > theme.BodySize());
    CHECK(theme.LineHeight() >= theme.BodySize() * 1.3f);
    CHECK(theme.LineHeight() <= theme.BodySize() * 1.4f);
  }

  // The old large-text flag still works, and still cannot take the face back
  // under the floor.
  ship::Theme theme = ship::Theme::ById("classic");
  theme.largeText = true;
  CHECK(theme.BodySize() >= 18.0f);
}

TEST_CASE("the settings sheet offers the four steps and the borderless window",
          "[phase9][typography][settings]") {
  ship::Simulation sim = Fresh();
  const ship::PopupModel model = ship::BuildPopup(sim, "settings", "");
  REQUIRE(model.fontStepLabels.size() == 4);
  CHECK(model.fontStepLabels[0] == "Base (18px)");
  CHECK(model.fontStepLabels[1] == "+5% (19px)");
  CHECK(model.fontStepLabels[2] == "+10% (20px)");
  CHECK(model.fontStepLabels[3] == "+15% (21px)");
  CHECK(model.fontStep == 0);
  CHECK(model.fontStepLine == "Reading size: Base (18 px)");
  CHECK_FALSE(model.fullscreen);
  CHECK(model.fullscreenLabel == "Window: Windowed");

  // The palette chips are still there and still five.
  REQUIRE(model.themes.size() == 5);
  CHECK(model.themes[0] == "classic");
  CHECK(model.themeLabels[0] == "Flight Deck - warm parchment");

  // Choosing a step is free and exact; anything outside the ladder is refused.
  CHECK(ship::PressPopupAction(sim, "settings", "font", "2", "", nullptr));
  CHECK(sim.State().options.fontStep == 2);
  CHECK(ship::BuildPopup(sim, "settings", "").fontStepLine == "Reading size: +10% (20 px)");
  CHECK_FALSE(ship::PressPopupAction(sim, "settings", "font", "4", "", nullptr));
  CHECK_FALSE(ship::PressPopupAction(sim, "settings", "font", "-1", "", nullptr));
  CHECK(sim.State().options.fontStep == 2);

  // One step up and one down, stopping at the ends rather than wrapping.
  CHECK(ship::PressPopupAction(sim, "settings", "font-step", "-1", "", nullptr));
  CHECK(sim.State().options.fontStep == 1);
  CHECK(ship::PressPopupAction(sim, "settings", "font-step", "+1", "", nullptr));
  CHECK(sim.State().options.fontStep == 2);
  CHECK(ship::PressPopupAction(sim, "settings", "font", "3", "", nullptr));
  CHECK_FALSE(ship::PressPopupAction(sim, "settings", "font-step", "+1", "", nullptr));
  CHECK(sim.State().options.fontStep == 3);

  // The window switch is a plain toggle, saved with the voyage.
  CHECK(ship::PressPopupAction(sim, "settings", "fullscreen", "", "", nullptr));
  CHECK(sim.State().options.fullscreen);
  CHECK(ship::BuildPopup(sim, "settings", "").fullscreenLabel ==
        "Window: Borderless fullscreen");
  CHECK(ship::PressPopupAction(sim, "settings", "fullscreen", "", "", nullptr));
  CHECK_FALSE(sim.State().options.fullscreen);
}

TEST_CASE("the reading size and the window survive a save and a load",
          "[phase9][typography][settings]") {
  const fs::path dir = Scratch("options-roundtrip");
  ship::Simulation sim = Fresh();
  sim.MutableState().options.fontStep = 3;
  sim.MutableState().options.fullscreen = true;
  REQUIRE(sim.SaveSlot(1, dir.string(), nullptr));

  ship::Simulation loaded = Fresh();
  REQUIRE(loaded.LoadSlot(1, dir.string(), nullptr));
  CHECK(loaded.State().options.fontStep == 3);
  CHECK(loaded.State().options.fullscreen);

  // A stale or hand-edited file cannot put the interface below the floor or
  // above its last step: loading clamps instead of trusting.
  ship::Simulation wild = Fresh();
  wild.MutableState().options.fontStep = 9;
  REQUIRE(wild.SaveSlot(1, dir.string(), nullptr));
  ship::Simulation clamped = Fresh();
  REQUIRE(clamped.LoadSlot(1, dir.string(), nullptr));
  CHECK(clamped.State().options.fontStep == 3);

  wild.MutableState().options.fontStep = -4;
  REQUIRE(wild.SaveSlot(1, dir.string(), nullptr));
  ship::Simulation floor = Fresh();
  REQUIRE(floor.LoadSlot(1, dir.string(), nullptr));
  CHECK(floor.State().options.fontStep == 0);
}

// ---------------------------------------------------------------------------
//  The tutorial: five cards, each one idea and one habit
// ---------------------------------------------------------------------------
TEST_CASE("the main menu opens a five-card tutorial", "[phase9][tutorial]") {
  const std::vector<ship::TutorialCard>& cards = ship::TutorialCards();
  REQUIRE(cards.size() == 5);
  for (const ship::TutorialCard& card : cards) {
    CHECK_FALSE(card.title.empty());
    CHECK(card.body.size() > 60);
    CHECK_FALSE(card.habit.empty());
  }
  CHECK(cards[0].title.find("Morning coffee") != std::string::npos);
  CHECK(cards[1].title.find("Free Harbor") != std::string::npos);
  CHECK(cards[2].title.find("1-click") != std::string::npos);
  CHECK(cards[3].title.find("O2") != std::string::npos);
  CHECK(cards[4].title.find("sidebar") != std::string::npos);

  ship::Simulation sim = Fresh();
  bool fromVoyage = false;
  ship::TitlePanel panel = ship::TitlePanel::None;
  int slot = 1;
  std::string message;

  // TUTORIAL is on the menu, and it is free like every other menu verb.
  const ship::TitleModel menu = ship::BuildTitleScreen(sim, false, panel, "");
  bool hasTutorial = false;
  for (const ship::ActionRowView& row : menu.actions) {
    if (row.id == "tutorial") {
      hasTutorial = true;
      CHECK(row.label == "TUTORIAL");
      CHECK(row.cost == "free");
    }
  }
  CHECK(hasTutorial);
  REQUIRE(ship::PressTitleAction(sim, "tutorial", "", &fromVoyage, &panel, &slot, "",
                                 &message));
  CHECK(panel == ship::TitlePanel::Tutorial);

  // The first card is the coffee card, and it cannot go back any further.
  const ship::TitleModel first = ship::BuildTitleScreen(sim, false, panel, "", 1, "", 0);
  CHECK(first.tutorialCards == 5);
  CHECK(first.tutorialCard == 0);
  CHECK(first.cardTitle == cards[0].title);
  CHECK(first.cardHabit == cards[0].habit);
  CHECK(first.panelNote.find(cards[0].body) != std::string::npos);
  const ship::ActionRowView* prev = nullptr;
  const ship::ActionRowView* next = nullptr;
  const ship::ActionRowView* start = nullptr;
  for (const ship::ActionRowView& row : first.panelRows) {
    if (row.id == "card:prev") { prev = &row; }
    if (row.id == "card:next") { next = &row; }
    if (row.id == "start-voyage") { start = &row; }
  }
  REQUIRE(prev != nullptr);
  REQUIRE(next != nullptr);
  REQUIRE(start != nullptr);
  CHECK(start->enabled);
  CHECK(start->label == "START A NEW VOYAGE");
  CHECK_FALSE(prev->enabled);
  CHECK(prev->reading);
  CHECK(next->enabled);
  CHECK(next->label == "NEXT CARD (1 of 5)");

  // The last card has nowhere further to go, and a nonsense index is clamped.
  const ship::TitleModel last = ship::BuildTitleScreen(sim, false, panel, "", 1, "", 4);
  CHECK(last.tutorialCard == 4);
  CHECK(last.cardTitle == cards[4].title);
  for (const ship::ActionRowView& row : last.panelRows) {
    if (row.id == "card:next") { CHECK_FALSE(row.enabled); }
  }
  const ship::TitleModel clamped = ship::BuildTitleScreen(sim, false, panel, "", 1, "", 99);
  CHECK(clamped.tutorialCard == 4);

  // Paging is free and the cards never touch the voyage.
  const double actions = sim.ActionCount();
  CHECK(ship::PressTitleAction(sim, "card:next", "", &fromVoyage, &panel, &slot, "",
                               &message));
  CHECK_FALSE(ship::PressTitleAction(sim, "card:jump", "", &fromVoyage, &panel, &slot,
                                     "", &message));
  CHECK(sim.ActionCount() == actions);

  // BACK returns to the menu.
  REQUIRE(ship::PressTitleAction(sim, "back", "", &fromVoyage, &panel, &slot, "",
                                 &message));
  CHECK(panel == ship::TitlePanel::None);
}

// ---------------------------------------------------------------------------
//  The player's own audio
// ---------------------------------------------------------------------------
TEST_CASE("assets/audio documents the exact filenames the game looks for",
          "[phase9][audio]") {
  const fs::path readme = AssetsDir() / "audio" / "README.txt";
  REQUIRE(fs::exists(readme));
  std::ifstream in(readme);
  const std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

  for (const char* name : {"click.wav", "action.wav", "complete.wav", "alert.wav",
                           "arrive.wav"}) {
    INFO(name);
    CHECK(text.find(name) != std::string::npos);
  }
  for (const char* format : {".ogg", ".mp3", ".wav"}) {
    INFO(format);
    CHECK(text.find(format) != std::string::npos);
  }
  CHECK(text.find("music/") != std::string::npos);
  CHECK(text.find("sfx/") != std::string::npos);

  // The folders ship with the game, empty.
  CHECK(fs::is_directory(AssetsDir() / "audio" / "music"));
  CHECK(fs::is_directory(AssetsDir() / "audio" / "sfx"));

  // Every cue the simulator knows has a file it will accept first.
  CHECK(ship::audio::SfxFileNames("click").front() == "click.wav");
  CHECK(ship::audio::SfxFileNames("thrust").front() == "action.wav");
  CHECK(ship::audio::SfxFileNames("chime").front() == "complete.wav");
  CHECK(ship::audio::SfxFileNames("ping").front() == "alert.wav");
  const std::vector<std::string>& chimes = ship::audio::SfxFileNames("chime");
  CHECK(std::find(chimes.begin(), chimes.end(), "arrive.wav") != chimes.end());
  CHECK(ship::audio::SfxFileNames("no-such-cue").empty());
}

TEST_CASE("the game finds the player's files and falls back to the synth",
          "[phase9][audio]") {
  const fs::path dir = Scratch("audio-scan");

  // An empty asset tree: nothing found, everything synthesised.
  const ship::audio::ExternalAudio empty =
      ship::audio::FindExternalAudio(dir.string());
  CHECK(empty.music.empty());
  CHECK(empty.sfx.empty());
  CHECK_FALSE(empty.any());

  // Music is picked up in filename order, and only the three formats.
  Write(dir / "audio" / "music" / "02-long-watch.ogg", "OggS");
  Write(dir / "audio" / "music" / "01-quiet-drift.mp3", "ID3");
  Write(dir / "audio" / "music" / "03-harbour.wav", "RIFF");
  Write(dir / "audio" / "music" / "notes.txt", "not music");

  // Sound effects: the promised names, then the aliases.
  Write(dir / "audio" / "sfx" / "click.wav", "RIFF");
  Write(dir / "audio" / "sfx" / "thrust.wav", "RIFF");     // alias for action
  Write(dir / "audio" / "sfx" / "arrive.wav", "RIFF");     // alias for complete
  Write(dir / "audio" / "sfx" / "unrelated.wav", "RIFF");

  const ship::audio::ExternalAudio found =
      ship::audio::FindExternalAudio(dir.string());
  REQUIRE(found.music.size() == 3);
  CHECK(found.music[0].find("01-quiet-drift.mp3") != std::string::npos);
  CHECK(found.music[1].find("02-long-watch.ogg") != std::string::npos);
  CHECK(found.music[2].find("03-harbour.wav") != std::string::npos);
  for (const std::string& path : found.music) {
    CHECK(path.find("notes.txt") == std::string::npos);
  }

  REQUIRE(found.sfx.count("click") == 1);
  CHECK(found.sfx.at("click").find("click.wav") != std::string::npos);
  CHECK(found.sfx.count("thrust") == 1);
  CHECK(found.sfx.at("thrust").find("thrust.wav") != std::string::npos);
  CHECK(found.sfx.count("chime") == 1);
  CHECK(found.sfx.at("chime").find("arrive.wav") != std::string::npos);
  CHECK(found.sfx.count("ping") == 0);   // no alert file: the synth plays it
  CHECK(found.any());

  // A promised name wins over its alias when both are present.
  Write(dir / "audio" / "sfx" / "action.wav", "RIFF");
  const ship::audio::ExternalAudio both =
      ship::audio::FindExternalAudio(dir.string());
  CHECK(both.sfx.at("thrust").find("action.wav") != std::string::npos);

  // And an absent tree is silent rather than fatal.
  CHECK_FALSE(ship::audio::FindExternalAudio("").any());
  CHECK_FALSE(ship::audio::FindExternalAudio((dir / "nowhere").string()).any());
}
