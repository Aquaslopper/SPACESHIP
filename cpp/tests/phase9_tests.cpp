// ============================================================================
//  phase9_tests.cpp — anti-spam plumbing, the reading-size ladder, the
//  borderless window, the illustrated tutorial and the player's own audio.
//  ---------------------------------------------------------------------------
//  Phase 9's promise is that the port is *comfortable* to use: no paid button
//  can be double-clicked, the smallest face is 18 px, the palettes are calm,
//  the window can be borderless, a newcomer gets five cards of guidance, and
//  the soundtrack can be replaced with files. Each of those is held here.
// ============================================================================
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include "core/DisplaySettings.h"

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
    CHECK(theme.BodySize() >= 12.0f);
    CHECK(theme.LineHeight() > theme.BodySize());
    CHECK(theme.LineHeight() >= theme.BodySize() * 1.25f);
    CHECK(theme.LineHeight() <= theme.BodySize() * 1.4f);
  }

  // The old large-text flag still works, and still cannot take the face back
  // under the floor.
  ship::Theme theme = ship::Theme::ById("classic");
  theme.largeText = true;
  CHECK(theme.BodySize() >= 12.0f);
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
  CHECK(model.themeLabels[0] == "Flight Deck - parchment");

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

TEST_CASE("display adjustment uses the calibrated default and survives saves", "[phase9][settings][display]") {
  ship::Simulation sim = Fresh();
  CHECK(sim.State().options.displayGamma == 1.0);
  CHECK(sim.State().options.displayBrightness == 1.0);
  CHECK(ship::EffectiveDisplayGamma(1.0) == 1.6);
  CHECK(std::abs(ship::AdjustDisplayChannel(0.4, 1.0, 1.0) - std::pow(0.4, 1.6)) < 1e-12);
  CHECK(ship::AdjustDisplayChannel(0.4, 1.3, 1.0) < ship::AdjustDisplayChannel(0.4, 1.0, 1.0));
  CHECK(ship::AdjustDisplayChannel(0.4, 0.7, 1.0) > ship::AdjustDisplayChannel(0.4, 1.0, 1.0));
  CHECK(ship::AdjustDisplayChannel(1.0, 1.3, 0.8) == 0.8);
  const double before = sim.ActionCount();
  REQUIRE(ship::PressPopupAction(sim, "settings", "display-gamma", "1.35", "", nullptr));
  REQUIRE(ship::PressPopupAction(sim, "settings", "display-brightness", "0.82", "", nullptr));
  CHECK_FALSE(ship::PressPopupAction(sim, "settings", "display-gamma", "nan", "", nullptr));
  CHECK_FALSE(ship::PressPopupAction(sim, "settings", "display-gamma", "1oops", "", nullptr));
  CHECK(sim.ActionCount() == before);
  CHECK(std::abs(ship::BuildPopup(sim, "settings").displayGamma - 1.35f) < 0.001);
  ship::Simulation loaded = Fresh();
  REQUIRE(loaded.LoadJson(sim.SaveJson(), nullptr));
  CHECK(std::abs(loaded.State().options.displayGamma - 1.35) < 0.0001);
  CHECK(std::abs(loaded.State().options.displayBrightness - 0.82) < 0.0001);

  auto saved = nlohmann::json::parse(sim.SaveJson());
  saved["state"]["options"].erase("displayGamma");
  saved["state"]["options"].erase("displayBrightness");
  REQUIRE(loaded.LoadJson(saved.dump(), nullptr));
  CHECK(loaded.State().options.displayGamma == 1.0);
  CHECK(loaded.State().options.displayBrightness == 1.0);
  saved["state"]["options"]["displayGamma"] = 99;
  saved["state"]["options"]["displayBrightness"] = -10;
  REQUIRE(loaded.LoadJson(saved.dump(), nullptr));
  CHECK(loaded.State().options.displayGamma == 1.6);
  CHECK(loaded.State().options.displayBrightness == 0.75);
  saved["state"]["options"]["displayGamma"] = "not a number";
  saved["state"]["options"]["displayBrightness"] = nullptr;
  REQUIRE(loaded.LoadJson(saved.dump(), nullptr));
  CHECK(loaded.State().options.displayGamma == 1.0);
  CHECK(loaded.State().options.displayBrightness == 1.0);

  // Pre-calibration saves store the old absolute exponent without a scale tag.
  // A saved 1.60 retains its exact appearance and now reads 1.00 in the UI.
  saved["state"]["options"].erase("displayGammaScale");
  saved["state"]["options"]["displayGamma"] = 1.6;
  REQUIRE(loaded.LoadJson(saved.dump(), nullptr));
  CHECK(loaded.State().options.displayGamma == 1.0);
  CHECK(ship::EffectiveDisplayGamma(loaded.State().options.displayGamma) == 1.6);
  saved["state"]["options"]["displayGamma"] = 1.2;
  REQUIRE(loaded.LoadJson(saved.dump(), nullptr));
  CHECK(std::abs(ship::EffectiveDisplayGamma(loaded.State().options.displayGamma) - 1.2) < 1e-12);
  ship::Simulation resaved = Fresh();
  REQUIRE(resaved.LoadJson(loaded.SaveJson(), nullptr));
  CHECK(std::abs(ship::EffectiveDisplayGamma(resaved.State().options.displayGamma) - 1.2) < 1e-12);
  resaved.RestartVoyage();
  CHECK(std::abs(ship::EffectiveDisplayGamma(resaved.State().options.displayGamma) - 1.2) < 1e-12);
  saved["state"]["options"]["displayGamma"] = 0.7;
  REQUIRE(loaded.LoadJson(saved.dump(), nullptr));
  CHECK(std::abs(ship::EffectiveDisplayGamma(loaded.State().options.displayGamma) - 0.7) < 1e-12);
  saved["state"]["options"]["displayGamma"] = 1.0;
  REQUIRE(loaded.LoadJson(saved.dump(), nullptr));
  CHECK(loaded.State().options.displayGamma == 1.0);
}

// ---------------------------------------------------------------------------
//  The tutorial: six illustrated cards and accurate gameplay captions
// ---------------------------------------------------------------------------
TEST_CASE("how to play offers six illustrated cards without adding a menu command", "[phase9][tutorial]") {
  const std::vector<ship::TutorialCard>& cards = ship::TutorialCards();
  REQUIRE(cards.size() == 6);
  for (const ship::TutorialCard& card : cards) {
    CHECK_FALSE(card.title.empty());
    CHECK_FALSE(card.body.empty());
    CHECK(fs::exists(card.imagePath));
    CHECK_FALSE(card.habit.empty());
  }
  CHECK(cards[3].body.find("Not every click") != std::string::npos);
  CHECK(cards[5].body.find("illustrates possible roles") != std::string::npos);
  CHECK(cards[5].habit.find("costs one action") != std::string::npos);

  ship::Simulation sim = Fresh();
  bool fromVoyage = false;
  ship::TitlePanel panel = ship::TitlePanel::None;
  int slot = 1;
  std::string message;

  // Keep the five menu commands; HOW TO PLAY owns the illustrated guide.
  const ship::TitleModel menu = ship::BuildTitleScreen(sim, false, panel, "");
  bool hasTutorial = false;
  for (const ship::ActionRowView& row : menu.actions) {
    if (row.id == "tutorial") {
      hasTutorial = true;
      CHECK(row.label == "TUTORIAL");
      CHECK(row.cost == "free");
    }
  }
  CHECK_FALSE(hasTutorial);
  REQUIRE(ship::PressTitleAction(sim, "guide", "", &fromVoyage, &panel, &slot, "",
                                 &message));
  CHECK(panel == ship::TitlePanel::Guide);

  // The first card is the introduction, and it cannot go back any further.
  const ship::TitleModel first = ship::BuildTitleScreen(sim, false, panel, "", 1, "", 0);
  CHECK(first.tutorialCards == 6);
  CHECK(first.tutorialCard == 0);
  CHECK(first.cardTitle == cards[0].title);
  CHECK(first.cardHabit == cards[0].habit);
  CHECK(first.panelNote.find(cards[0].body) != std::string::npos);
  const ship::ActionRowView* prev = nullptr;
  const ship::ActionRowView* next = nullptr;
  for (const ship::ActionRowView& row : first.panelRows) {
    if (row.id == "card:prev") { prev = &row; }
    if (row.id == "card:next") { next = &row; }
  }
  REQUIRE(prev != nullptr);
  REQUIRE(next != nullptr);
  CHECK_FALSE(prev->enabled);
  CHECK(prev->reading);
  CHECK(next->enabled);
  CHECK(next->label == "NEXT CARD (1 of 6)");

  // The last card has nowhere further to go, and a nonsense index is clamped.
  const ship::TitleModel last = ship::BuildTitleScreen(sim, false, panel, "", 1, "", 5);
  CHECK(last.tutorialCard == 5);
  CHECK(last.cardTitle == cards[5].title);
  for (const ship::ActionRowView& row : last.panelRows) {
    if (row.id == "card:next") { CHECK_FALSE(row.enabled); }
  }
  const ship::TitleModel clamped = ship::BuildTitleScreen(sim, false, panel, "", 1, "", 99);
  CHECK(clamped.tutorialCard == 5);

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
  Write(dir / "audio" / "music" / "04-upper.MP3", "ID3");
  Write(dir / "audio" / "music" / "notes.txt", "not music");

  // Sound effects: the promised names, then the aliases.
  Write(dir / "audio" / "sfx" / "click.wav", "RIFF");
  Write(dir / "audio" / "sfx" / "thrust.wav", "RIFF");     // alias for action
  Write(dir / "audio" / "sfx" / "arrive.wav", "RIFF");     // alias for complete
  Write(dir / "audio" / "sfx" / "unrelated.wav", "RIFF");

  const ship::audio::ExternalAudio found =
      ship::audio::FindExternalAudio(dir.string());
  REQUIRE(found.music.size() == 4);
  CHECK(found.music[0].find("01-quiet-drift.mp3") != std::string::npos);
  CHECK(found.music[1].find("02-long-watch.ogg") != std::string::npos);
  CHECK(found.music[2].find("03-harbour.wav") != std::string::npos);
  CHECK(found.music[3].find("04-upper.MP3") != std::string::npos);
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
