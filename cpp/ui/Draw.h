// ============================================================================
//  Draw.h — native Raylib + ImGui presentation layer for Spaceship.
//  ---------------------------------------------------------------------------
//  All simulation rules and screen view-models live in cpp/core and cpp/ui's
//  headless modules. ShellView measures text, computes hit boxes, renders each
//  active screen or modal overlay, and translates pointer/scroll/key input
//  into view-model actions.
// ============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "raylib.h"

#include "core/Simulation.h"
#include "data/ChartExpansion.h"
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
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
#include "ui/Theme.h"
#include "ui/TitleScreen.h"
#include "ui/Widgets.h"
#include "ui/WikiView.h"

namespace ship {

enum class HitButton { None, Left, Right };

struct FlareTarget {
  enum Kind : unsigned char { PanelCorner, ShipPoint, StarPoint, Occlude } kind = PanelCorner;
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f, weight = 3.0f;
};

class ShellView {
 public:
  ShellView();
  ~ShellView();
  // Frees the hull mesh, material and shader. Call while the window, and with
  // it the GL context, still exists; the destructor runs after CloseWindow.
  void ReleaseGraphics() {
    ReleaseWrenMesh();
    if (planetTex_.id != 0) {
      if (IsWindowReady()) { UnloadTexture(planetTex_); }
      planetTex_ = Texture2D{};
    }
  }
  ShellView(const ShellView&) = delete;
  ShellView& operator=(const ShellView&) = delete;

  void Configure(float width, float height, const Theme& theme);
  void SetFonts(Font body, Font bold, Font mono) {
    bodyFont_ = body;
    boldFont_ = bold;
    monoFont_ = mono;
  }
  void SetTitleTexture(void* texture) { titleArt_ = texture; }
  void SetSetupTexture(void* texture) { setupArt_ = texture; }
  void SetSplashTexture(void* texture) { splashArt_ = texture; }
  void SetAudioStatus(std::string status) { audioStatus_ = std::move(status); }
  void SetWrenExteriorTexture(void* texture,
      const std::string& path = "art/ships/wren-exterior.png");
  void SetWrenExteriorImage(const Image* image);
  void SetDisplayAdjustmentAvailable(bool available) { displayAdjustmentAvailable_ = available; }
  void SetTutorialTexture(std::size_t index, void* texture) {
    if (index < tutorialArt_.size()) { tutorialArt_[index] = texture; }
  }
  // A picture from assets/art/crew, under its file name without ".png".
  void SetCrewTexture(const std::string& name, void* texture) { crewArt_[name] = texture; }
  void PageTutorial(int direction, double nowMs);
  void SetOthersHeight(float others) { others_ = others; }

  void Draw(Simulation& sim, double nowMs);
  void Click(Simulation& sim, float x, float y, HitButton button, double nowMs);
  void Move(float x, float y);
  void Drag(Simulation& sim, float x, float y, float dx, float dy, bool down, bool pressed);
  void OrbitDrag(Simulation& sim, float x, float y, float dx, float dy, bool down, bool pressed);
  void Scroll(Simulation& sim, float x, float y, float wheelDelta);
  void Escape(Simulation& sim);

  bool QuitRequested() const { return quitRequested_; }
  const std::vector<FlareTarget>& FlareTargets() const { return flareTargets_; }

  const Description& Selection() const { return selection_; }
  bool HasSelection() const { return floating_.HasBriefings(); }
  const ShellLayout& LayoutNow() const { return layout_; }

  void SetSaveDir(std::string dir) { saveDir_ = std::move(dir); }
  const std::string& SaveDir() const { return saveDir_; }

  const std::string& PopupKey() const { return popupKey_; }
  void OpenPopup(const std::string& key);
  int PopupCount() const { return popupCount_; }
  void ClosePopup();

  void ShowSplash(bool fromVoyage);
  bool SplashActive() const { return splashActive_; }
  void SuppressSplashTitleFade() { titleInStartMs_ = -1.0; }
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

  // The station's and cargo hold's own session selections: choosing costs
  // nothing, so it is kept here and not in the voyage.
  StationSelection stationSelection_;
  CargoSelection cargoSelection_;

  struct ExteriorCallout {
    Area box;
    Vector2 anchor{};
    Vector2 normUV{};
    float localZ = 0.0f;
    std::string id;
    std::vector<std::string> lines;
    bool left = false;
  };
  struct ExteriorLayout {
    Area image;
    float height = 0;
    float lineHeight = 0;
    std::vector<ExteriorCallout> callouts;
  };
  struct WrenSdfGrid {
    int w = 0;
    int h = 0;
    float maxDist = 1.0f;
    std::vector<float> sdf;
    std::vector<float> luma;
  };
  bool HasWrenExterior(const Simulation& sim) const;
  void BuildWrenSdfFromImage(const Image& image);
  float SampleWrenHullZ(float u, float v) const;
  void EnsureWrenMesh();
  void ReleaseWrenMesh();
  ExteriorLayout BuildExteriorLayout(const Simulation& sim, const Area& stage) const;
  void PaintWrenExterior(const Simulation& sim);

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

  void PaintChrome(Simulation& sim, double nowMs);
  void OpenBriefing(const Simulation& sim, const std::string& kind, const std::string& id);
  void PaintTaskScreen(Simulation& sim);
  void PaintPanelsScreen(Simulation& sim);
  void PaintPopup(Simulation& sim);
  void PaintHero(const Vignette& vignette, const Area& box);
  void PaintTitle(Simulation& sim, double nowMs);
  void PaintTutorial(const Simulation& sim, double nowMs);
  void PaintSettingsControls(Simulation& sim, const PopupModel& model);
  void PaintLoss(Simulation& sim);
  void PaintWikiSearch(const Area& box);
  void PaintNavScreen(Simulation& sim);
  void PaintQuartersScreen(Simulation& sim);
  void PaintCargoScreen(Simulation& sim);
  void PaintCrewRecord(const Simulation& sim);
  void PaintPanel(const NavPanel& panel, const Area& box);
  bool PaintScenePart(const MachinePart& part, const Area& origin);
  void PaintStage(const Simulation& sim, const TaskStage& stage, const Area& origin);
  void EnsurePlanetSurface(const Simulation& sim);
  void RenderPlanetSurfacePixels(const Simulation& sim);
  void StepPlanetSurface(int actionKind);
  void PaintIdle(Simulation& sim);
  float PaintActionBand(const std::string& title, const std::string& note,
                       const std::vector<ProgressButton>& buttons,
                       const std::string& warning = "", bool measureOnly = false);
  bool PressPanelRow(Simulation& sim, ScreenKind kind, const std::string& id);

  Theme theme_;
  Font bodyFont_{};
  Font boldFont_{};
  Font monoFont_{};
  ShellLayout layout_;
  std::vector<ActionMark> marks_;
  IdleScreenModel idle_;
  void* wrenExteriorArt_ = nullptr;
  std::string exteriorArtPath_ = "art/ships/wren-exterior.png";
  ExteriorLayout exterior_;
  WrenSdfGrid wrenSdf_;
  Mesh wrenMesh_{};
  Material wrenMat_{};
  Shader wrenShader_{};
  int wrenShaderWarmLoc_ = -1;
  int wrenShaderLightLoc_ = -1;
  unsigned int wrenMeshTextureId_ = 0;
  bool wrenMeshBuilt_ = false;
  bool wrenMeshAttempted_ = false;
  double frameTimeMs_ = 0.0;

  Description selection_;
  FloatingPanels floating_;
  int focusedPanel_ = 0;

  float width_ = 1440.0f;
  float height_ = 940.0f;
  float others_ = 300.0f;
  float mouseX_ = -1.0f;
  float mouseY_ = -1.0f;
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
  struct DebrisHit { Area box; int click = -1; int reward = 0; int shiplets = 0; };
  std::array<DebrisHit, 2> debrisHits_{};
  Area bridgeBearingBar_{};
  float bridgeLook_ = 0.0f;  // turns from forward, -0.5 to +0.5
  bool draggingBearing_ = false;
  bool bridgeDispatch_ = false;
  float bridgeDispatchOpen_ = 0.0f;
  struct PlanetSurfaceState {
    static constexpr int kSize = 9;
    static constexpr int kMaxOre = 5;
    static constexpr int kTexW = 160;
    static constexpr int kTexH = 96;
    std::string planetId;
    std::uint8_t tiles[kSize * kSize]{};    // 0=open surface, 1=obstacle/border, 2=ship pad, 3=trail
    std::uint8_t variant[kSize * kSize]{};  // deterministic per-tile visual variant (0..7)
    int oreCount = 0;
    int oreX[kMaxOre]{};
    int oreY[kMaxOre]{};
    int shipX = 4;
    int shipY = 7;
    int px = 4;
    int py = 6;
    int dir = 0;  // 0=N, 1=E, 2=S, 3=W

    // Cached render state so the 160x96 buffer only rasterizes on change
    std::string renderPlanetId;
    int renderPx = -1;
    int renderPy = -1;
    int renderDir = -1;
    unsigned renderOreMask = 0xFFFFu;
    bool hasShipSprite = false;
    Area shipUv{};
    int visOreCount = 0;
    int visOreIdx[3]{};
    Area visOreUv[3]{};
  };
  PlanetSurfaceState planetSurface_{};
  Texture2D planetTex_{};
  std::vector<Color> planetPixels_;
  struct PlanetClickHit {
    Area box;
    int kind = 0;  // 1=up, 2=down, 3=left, 4=right, 5=ore, 6=ship, 7=turn180
    int arg = 0;
  };
  std::array<PlanetClickHit, 16> planetHits_{};
  int planetHitCount_ = 0;
  Area navChartBox_{};
  float navChartScaleX_ = 1.0f;
  float navChartScaleY_ = 1.0f;
  static constexpr float kDefaultNavOrbitYaw = 0.0f;
  static constexpr float kDefaultNavOrbitPitch = 50.0f;
  float navOrbitYaw_ = kDefaultNavOrbitYaw;
  float navOrbitPitch_ = kDefaultNavOrbitPitch;
  bool orbitingChart_ = false;
  bool orbitDragMoved_ = false;
  double orbitDragAccumX_ = 0.0;
  double orbitDragAccumY_ = 0.0;

  TaskScreenModel taskScreen_;
  std::vector<std::pair<Area, std::string>> taskButtons_;
  std::string taskTabKey_;
  bool stationWelcome_ = false;

  struct NavCacheKey {
    bool valid = false;
    double actions = -1.0;
    std::string layer;
    std::string sector;
    std::string here;
    double zoom = -1.0;
    double panX = 0.0;
    double panY = 0.0;
    std::string target;
    bool docked = false;
    bool reducedMotion = false;
    std::size_t scannedCount = 0;
    std::size_t ongoingSig = 0;
    double fuel = -1.0;
    double energy = -1.0;
    double drive = -1.0;
    int sensorTier = -1;
    double scanCost = -1.0;
    std::size_t mapMarksCount = 0;
    double cargoUsed = -1.0;
    double cargoCap = -1.0;
    std::size_t crewSig = 0;

    bool operator==(const NavCacheKey& o) const {
      return valid && o.valid && actions == o.actions && layer == o.layer &&
             sector == o.sector && here == o.here && zoom == o.zoom &&
             panX == o.panX && panY == o.panY && target == o.target &&
             docked == o.docked && reducedMotion == o.reducedMotion &&
             scannedCount == o.scannedCount && ongoingSig == o.ongoingSig &&
             fuel == o.fuel && energy == o.energy && drive == o.drive &&
             sensorTier == o.sensorTier &&
             scanCost == o.scanCost && mapMarksCount == o.mapMarksCount &&
             cargoUsed == o.cargoUsed && cargoCap == o.cargoCap &&
             crewSig == o.crewSig;
    }
  };

  void EnsureNavScreen(const Simulation& sim);

  NavScreenModel navScreen_;
  NavRaster navRaster_;
  NavCacheKey navCacheKey_;
  double navFxTick_ = 0.0;
  bool screenNav_ = false;
  bool screenQuarters_ = false;
  bool screenCargo_ = false;
  bool screenRecord_ = false;
  bool screenPanels_ = false;
  ScreenKind panelsKind_ = ScreenKind::Described;
  QuartersScreenModel quarters_;
  CargoRoomModel cargoModel_;
  struct CargoCrateAnim {
    float x = 0.0f;
    float y = 0.0f;
    float hop = 0.0f;
    double settleFxUntilMs = 0.0;
    bool initialized = false;
    bool wasStowed = false;
  };
  std::unordered_map<std::string, CargoCrateAnim> cargoAnims_;
  double lastCargoAnimMs_ = 0.0;
  CrewRecordModel record_;
  std::vector<NavHit> navHits_;

  std::string popupKey_;
  std::string saveDir_ = "runtime/saves";
  std::string audioStatus_;
  int popupCount_ = 0;
  std::string pendingConfirm_;
  std::string pendingAction_;

  bool splashActive_ = false;
  bool splashFromVoyage_ = false;
  double splashStartMs_ = -1.0;
  double titleInStartMs_ = -1.0;

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
  void* splashArt_ = nullptr;
  void* setupArt_ = nullptr;
  ChartSettings setupSettings_{};
  ChartPreview setupPreview_{};
  std::string setupLayer_ = "galaxy";
  std::string setupGalaxyId_;
  std::string setupSystemId_;
  char setupSeed_[21] = "SPACESHI";
  bool setupPreviewDirty_ = true;

  void PaintSetup(Simulation& sim);
  void PaintTitleBody(Simulation& sim, double nowMs);

  // Screen changes that would otherwise cut in one frame. The state has
  // already changed when one starts; only the picture is held back.
  //   1  menu <-> voyage settings: dip to dark and back.
  //   2  LAUNCH: iris out over the settings, a loading card, iris in on the ship.
  int fadeKind_ = 0;
  double fadeStartMs_ = 0.0;
  bool fadeFromSetup_ = false;
  bool titleWasSetup_ = false;
  double titlePaintMs_ = -1.0e9;
  std::string fadeLabel_;
  bool LaunchCoverUp(double nowMs) const;
  void PaintIris(float open, bool reduced);
  void PaintLaunchCard(double elapsedMs);
  void PaintSplash(double nowMs);
  void EndSplash(double nowMs);
  void RebuildSetupPreview(const Simulation& sim);
  bool displayAdjustmentAvailable_ = true;
  std::array<void*, 6> tutorialArt_{};
  std::map<std::string, void*> crewArt_;
  double tutorialTransitionMs_ = 0.0;
  int tutorialDirection_ = 1;

  WikiView wiki_;
  char wikiQuery_[96] = {};
  bool lossDismissed_ = false;
  int lossSlot_ = 1;
  float lossScroll_ = 0.0f;
  float lossMaxScroll_ = 0.0f;
  std::string lossMessage_;
  LossModel loss_;
  std::vector<NavHit> lossHits_;
  std::vector<FlareTarget> flareTargets_;
};

}  // namespace ship
