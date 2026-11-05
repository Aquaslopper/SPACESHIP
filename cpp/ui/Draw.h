// ============================================================================
//  Draw.h — the only file that knows a pixel has a colour.
//  ---------------------------------------------------------------------------
//  Everything the shell needs to decide is in Theme, Layout, Shell, Describe,
//  IdleScreen, ShipDiagram and Fx, and all of it is testable headless. This
//  class is the thin skin over that: it measures the window, asks Layout for
//  the rectangles, asks the view-models for the content, and paints.
//
//  It is compiled only when the GUI target is built (SPACESHIP_GUI); on a
//  machine without X11 or OpenGL the CMake gate skips it entirely and the
//  tests still run. In this sandbox it is compile-checked with -fsyntax-only.
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "raylib.h"

#include "core/Simulation.h"
#include "ui/Describe.h"
#include "ui/Fx.h"
#include "ui/IdleScreen.h"
#include "ui/Layout.h"
#include "ui/Shell.h"
#include "ui/Widgets.h"
#include "ui/CrewRecord.h"
#include "ui/NavScreen.h"
#include "ui/Popups.h"
#include "ui/QuartersScreen.h"
#include "ui/AwardsScreen.h"
#include "ui/EventScreen.h"
#include "ui/RoomsScreen.h"
#include "ui/SimCoreScreen.h"
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
#include "ui/Theme.h"
#include "ui/TitleScreen.h"
#include "ui/WikiView.h"

namespace ship {

// What the pointer did, independent of which library delivered it.
enum class HitButton { None, Left, Right };

class ShellView {
 public:
  ShellView();

  // Window size and options, before anything is drawn.
  void Configure(float width, float height, const Theme& theme);
  void SetFonts(Font body, Font bold, Font mono) {
    bodyFont_ = body;
    boldFont_ = bold;
    monoFont_ = mono;
  }
  // The app owns the texture and keeps it alive until the shell is destroyed.
  void SetTitleTexture(void* texture) { titleArt_ = texture; }

  // The idle screen's non-feed height, measured once per paint. Reported to
  // Layout::GiveFeedHeight, exactly as `other` is reported in the reference.
  void SetOthersHeight(float others) { others_ = others; }

  // One paint. `nowMs` drives the cosmetic effects only.
  void Draw(Simulation& sim, double nowMs);

  // The two verbs, from a hit test in window coordinates.
  void Click(Simulation& sim, float x, float y, HitButton button, double nowMs);

  // What the last left-click selected, if anything. Empty when the click
  // landed on chrome rather than on the ship.
  const Description& Selection() const { return selection_; }
  bool HasSelection() const { return hasSelection_; }

  const ShellLayout& LayoutNow() const { return layout_; }

  // Where save slots live. The app points this at runtime/saves/; the tests
  // point it at a scratch directory.
  void SetSaveDir(std::string dir) { saveDir_ = std::move(dir); }
  const std::string& SaveDir() const { return saveDir_; }

  // A popup opened by the pinned foot, if one is up. Free to open and close.
  const std::string& PopupKey() const { return popupKey_; }
  void OpenPopup(const std::string& key) {
    popupKey_ = key;
    popupHits_.clear();
    popupMessage_.clear();
    ++popupCount_;
  }
  int PopupCount() const { return popupCount_; }
  void ClosePopup() { popupKey_.clear(); popupHits_.clear(); confirmNewVoyage_ = false; }
  // ---- the main menu (Section N) -----------------------------------------
  // The menu covers the window; while. it is up nothing reaches the ship.
  void ShowTitle(bool fromVoyage);
  void HideTitle() {
    titleActive_ = false;
    titlePanel_ = TitlePanel::None;
    titleConfirm_ = false;
  }
  bool TitleActive() const { return titleActive_; }
  bool TitleFromVoyage() const { return fromVoyage_; }
  TitlePanel Panel() const { return titlePanel_; }
  const std::string& TitleMessage() const { return titleMessage_; }

  // ---- the handbook's session view ---------------------------------------
  const WikiView& Wiki() const { return wiki_; }
  void SetWikiView(const WikiView& view) { wiki_ = view; }

  // The loss overlay is shown while the voyage has ended and the reader has not
  // dealt with it; `LossDismissed` keeps it down after a load or a fresh start.
  bool LossVisible(const Simulation& sim) const {
    return !sim.State().gameOver.empty() && !lossDismissed_;
  }

  const Theme& CurrentTheme() const { return theme_; }
  void SetTheme(const Theme& theme) { theme_ = theme; }

 private:
  // Phase 9 typography: every face in the GUI is authored against the
  // reference's 13 px and steps with the reader's choice, so the whole
  // interface grows together instead of one label at a time.
  int TypePx(int reference) const;
  void Text(const char* text, int x, int y, int size, ::Color tint,
            bool mono = false) const;
  float TextWidth(const char* text, int size) const;
  // The same factor for text-driven row cadences: a line box grows with the
  // face so a list of toggles or slots never overlaps itself.
  float TypeStep(float reference) const {
    return reference * (theme_.FontPx() / 13.0f);
  }
  float TypeFace() const { return theme_.FontPx() / 13.0f; }

  Theme theme_;
  Font bodyFont_{};
  Font boldFont_{};
  Font monoFont_{};
  ShellLayout layout_;
  FxRng rng_;
  std::vector<ActionMark> marks_;
  std::vector<DriftDot> dots_;
  IdleScreenModel idle_;
  Description selection_;
  bool hasSelection_ = false;
  float width_ = 1280.0f;
  float height_ = 800.0f;
  float others_ = 300.0f;
  int tabScroll_ = 0;

  TaskScreenModel taskScreen_;
  std::vector<std::pair<Area, std::string>> taskButtons_;
  std::string taskTabKey_;

  void PaintChrome(Simulation& sim);
  struct NavHit {
    Area box;
    std::string action;
    std::string arg;
  };
  NavScreenModel navScreen_;
  bool screenNav_ = false;
  bool screenQuarters_ = false;
  bool screenRecord_ = false;
  QuartersScreenModel quarters_;
  CrewRecordModel record_;
  std::vector<NavHit> navHits_;

  bool screenPanels_ = false;
  ScreenKind panelsKind_ = ScreenKind::Described;
  std::string popupKey_;
  std::string saveDir_ = "runtime/saves";
  std::string popupMessage_;
  bool confirmNewVoyage_ = false;
  int popupSlot_ = 1;
  int popupCount_ = 0;
  std::string pendingConfirm_;   // the question on the band, if any
  std::string pendingAction_;
  PopupModel popup_;
  std::vector<NavHit> popupHits_;

  void PaintTaskScreen(Simulation& sim);
  void PaintPanelsScreen(Simulation& sim);
  void PaintPopup(Simulation& sim);
  void PaintHero(const Vignette& vignette, const Area& box);
  void PaintTitle(Simulation& sim);
  void PaintLoss(Simulation& sim);
  void PaintWikiSearch();
  bool PressPanelRow(Simulation& sim, ScreenKind kind, const std::string& id);

  bool titleActive_ = false;
  bool titleConfirm_ = false;
  bool fromVoyage_ = false;
  TitlePanel titlePanel_ = TitlePanel::None;
  int titleSlot_ = 1;
  int titleCard_ = 0;   // which tutorial card is up (Phase 9)
  std::string titleMessage_;
  TitleModel title_;
  std::vector<NavHit> titleHits_;
  void* titleArt_ = nullptr;      // Texture2D owned by the application

  WikiView wiki_;
  char wikiQuery_[96] = {};
  bool lossDismissed_ = false;
  int lossSlot_ = 1;
  std::string lossMessage_;
  LossModel loss_;
  std::vector<NavHit> lossHits_;
  void* audioBridge_ = nullptr;  // the app's AudioDevice, untouched by this file
  void PaintNavScreen(Simulation& sim);
  void PaintQuartersScreen(Simulation& sim);
  void PaintCrewRecord(const Simulation& sim);
  void PaintPanel(const NavPanel& panel, const Area& box);
  void PaintStage(const TaskStage& stage, const Area& origin);
  void PaintIdle(Simulation& sim);
  void PaintDiagram(const DiagramModel& diagram);
};

}  // namespace ship
