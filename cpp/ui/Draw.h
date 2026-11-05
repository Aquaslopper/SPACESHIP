// ============================================================================
//  Draw.h — native Raylib + ImGui presentation layer for Spaceship.
//  ---------------------------------------------------------------------------
//  All simulation rules and screen view-models live in cpp/core and cpp/ui's
//  headless modules. ShellView measures text, computes hit boxes, renders each
//  active screen or modal overlay, and translates pointer/scroll/key input
//  into view-model actions.
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "raylib.h"

#include "core/Simulation.h"
#include "ui/AwardsScreen.h"
#include "ui/CrewRecord.h"
#include "ui/Describe.h"
#include "ui/EventScreen.h"
#include "ui/Fx.h"
#include "ui/FloatingPanels.h"
#include "ui/IdleScreen.h"
#include "ui/Layout.h"
#include "ui/NavScreen.h"
#include "ui/Popups.h"
#include "ui/QuartersScreen.h"
#include "ui/RoomsScreen.h"
#include "ui/Shell.h"
#include "ui/SimCoreScreen.h"
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
#include "ui/Theme.h"
#include "ui/TitleScreen.h"
#include "ui/Widgets.h"
#include "ui/WikiView.h"

namespace ship {

enum class HitButton { None, Left, Right };

class ShellView {
 public:
  ShellView();

  void Configure(float width, float height, const Theme& theme);
  void SetFonts(Font body, Font bold, Font mono) {
    bodyFont_ = body;
    boldFont_ = bold;
    monoFont_ = mono;
  }
  void SetTitleTexture(void* texture) { titleArt_ = texture; }
  void SetOthersHeight(float others) { others_ = others; }

  void Draw(Simulation& sim, double nowMs);
  void Click(Simulation& sim, float x, float y, HitButton button, double nowMs);
  void Move(float x, float y);
  void Drag(Simulation& sim, float x, float y, float dx, float dy, bool down, bool pressed);
  void Scroll(float x, float y, float wheelDelta);
  void Escape(Simulation& sim);

  bool QuitRequested() const { return quitRequested_; }

  const Description& Selection() const { return selection_; }
  bool HasSelection() const { return floating_.HasBriefings(); }
  const ShellLayout& LayoutNow() const { return layout_; }

  void SetSaveDir(std::string dir) { saveDir_ = std::move(dir); }
  const std::string& SaveDir() const { return saveDir_; }

  const std::string& PopupKey() const { return popupKey_; }
  void OpenPopup(const std::string& key);
  int PopupCount() const { return popupCount_; }
  void ClosePopup();

  void ShowTitle(bool fromVoyage);
  void HideTitle();
  bool TitleActive() const { return titleActive_; }
  bool TitleFromVoyage() const { return fromVoyage_; }
  TitlePanel Panel() const { return titlePanel_; }
  const std::string& TitleMessage() const { return titleMessage_; }

  const WikiView& Wiki() const { return wiki_; }
  void SetWikiView(const WikiView& view) { wiki_ = view; }

  bool LossVisible(const Simulation& sim) const {
    return !sim.State().gameOver.empty() && !lossDismissed_;
  }

  const Theme& CurrentTheme() const { return theme_; }
  void SetTheme(const Theme& theme) { theme_ = theme; }

 private:
  struct NavHit {
    Area box;
    std::string action;
    std::string arg;
  };

  struct TabHit {
    std::string key;
    Area box;
    Area closeBox;
    bool closable = false;
  };

  int TypePx(int reference) const;
  // Font used to paint text at `size`: bold once the 18 px floor is reached.
  // TextWidth/WrapText measure with the same font Text paints with so panel
  // heights and truncation never drift from what is actually drawn.
  const Font& SelectFont(int size, bool mono) const;
  float TypeStep(float reference) const {
    return reference * (theme_.FontPx() / 13.0f);
  }
  void Text(const char* text, int x, int y, int size, ::Color tint,
            bool mono = false) const;
  float TextWidth(const char* text, int size, bool mono = false) const;
  std::string FitText(const std::string& text, float maxWidth, int size,
                      bool mono = false) const;

  float MeasurePanelHeight(const NavPanel& panel, float width) const;
  float PaintTaskHeader(const TaskHead& head, float left, float top) const;
  void PaintScrollbar(const Area& viewport, float contentHeight, float scroll) const;
  void ResetContentScroll();

  void SyncIdleDots(const Simulation& sim, double nowMs);
  void PaintChrome(Simulation& sim, double nowMs);
  void OpenBriefing(const Simulation& sim, const std::string& kind, const std::string& id);
  void PaintTaskScreen(Simulation& sim);
  void PaintPanelsScreen(Simulation& sim);
  void PaintPopup(Simulation& sim);
  void PaintHero(const Vignette& vignette, const Area& box);
  void PaintTitle(Simulation& sim);
  void PaintSettingsControls(Simulation& sim, const PopupModel& model);
  void PaintLoss(Simulation& sim);
  void PaintWikiSearch(const Area& box);
  void PaintNavScreen(Simulation& sim);
  void PaintQuartersScreen(Simulation& sim);
  void PaintCrewRecord(const Simulation& sim);
  void PaintPanel(const NavPanel& panel, const Area& box);
  bool PaintScenePart(const MachinePart& part, const Area& origin);
  void PaintStage(const TaskStage& stage, const Area& origin);
  void PaintIdle(Simulation& sim);
  float PaintActionBand(const std::string& title, const std::string& note,
                       const std::vector<ProgressButton>& buttons,
                       const std::string& warning = "", bool measureOnly = false);
  void PaintDiagram(const DiagramModel& diagram, const Area& stageBox, float scale);
  bool PressPanelRow(Simulation& sim, ScreenKind kind, const std::string& id);

  Theme theme_;
  Font bodyFont_{};
  Font boldFont_{};
  Font monoFont_{};
  ShellLayout layout_;
  FxRng rng_;
  FxRng driftRng_;
  double lastDriftMs_ = 0.0;
  std::vector<ActionMark> marks_;
  std::vector<DriftDot> dots_;
  IdleScreenModel idle_;
  Area idleStageBox_{};
  float idleDiagramScale_ = 1.0f;

  Description selection_;
  FloatingPanels floating_;
  int focusedPanel_ = 0;

  float width_ = 1440.0f;
  float height_ = 940.0f;
  float others_ = 300.0f;
  float mouseX_ = -1.0f;
  float mouseY_ = -1.0f;
  float quartersBandTop_ = 0.0f;
  bool draggingChart_ = false;
  bool chartDragMoved_ = false;
  std::string scrollTabKey_;
  mutable bool paintingContent_ = false;
  mutable float contentPaintBottom_ = 0.0f;
  double chartDragAccumX_ = 0.0;
  double chartDragAccumY_ = 0.0;
  bool quitRequested_ = false;

  float tabScroll_ = 0.0f;
  float maxTabScroll_ = 0.0f;
  std::string lastRevealedTab_;
  std::map<std::string, double> tabPulseStarts_;
  std::size_t lastTabCount_ = 0;
  std::vector<TabHit> tabHits_;

  float sideScroll_ = 0.0f;
  float sideContentH_ = 0.0f;
  std::vector<SideHit> sideHits_;

  float actionBandTop_ = 0.0f;
  std::vector<NavHit> actionBandHits_;
  std::uint64_t feedRevision_ = 0;
  std::vector<std::string> feedSeen_;
  float feedScroll_ = 0.0f;
  float feedMaxScroll_ = 0.0f;
  Area feedBox_{};
  Area feedThumb_{};
  bool feedActive_ = false;
  bool draggingFeed_ = false;
  float feedDragOffset_ = 0.0f;
  float contentScroll_ = 0.0f;
  float contentMaxScroll_ = 0.0f;
  float wikiIndexScroll_ = 0.0f;
  float wikiIndexMaxScroll_ = 0.0f;
  float wikiBodyScroll_ = 0.0f;
  float wikiBodyMaxScroll_ = 0.0f;
  Area wikiIndexBox_{};
  Area wikiBodyBox_{};
  Area navChartBox_{};
  float navChartScaleX_ = 1.0f;
  float navChartScaleY_ = 1.0f;

  TaskScreenModel taskScreen_;
  std::vector<std::pair<Area, std::string>> taskButtons_;
  std::string taskTabKey_;

  NavScreenModel navScreen_;
  bool screenNav_ = false;
  bool screenQuarters_ = false;
  bool screenRecord_ = false;
  bool screenPanels_ = false;
  ScreenKind panelsKind_ = ScreenKind::Described;
  QuartersScreenModel quarters_;
  CrewRecordModel record_;
  std::vector<NavHit> navHits_;

  std::string popupKey_;
  std::string saveDir_ = "runtime/saves";
  int popupCount_ = 0;
  std::string pendingConfirm_;
  std::string pendingAction_;

  bool titleActive_ = false;
  bool titleConfirm_ = false;
  bool fromVoyage_ = false;
  TitlePanel titlePanel_ = TitlePanel::None;
  int titleSlot_ = 1;
  int titleCard_ = 0;
  float titleScroll_ = 0.0f;
  float titleMaxScroll_ = 0.0f;
  Area titleDialog_{};
  Area titlePlate_{};
  std::string titleMessage_;
  TitleModel title_;
  std::vector<NavHit> titleHits_;
  void* titleArt_ = nullptr;

  WikiView wiki_;
  char wikiQuery_[96] = {};
  bool lossDismissed_ = false;
  int lossSlot_ = 1;
  float lossScroll_ = 0.0f;
  float lossMaxScroll_ = 0.0f;
  std::string lossMessage_;
  LossModel loss_;
  std::vector<NavHit> lossHits_;
};

}  // namespace ship
