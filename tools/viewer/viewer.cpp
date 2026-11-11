// ============================================================================
//  viewer.cpp - headless viewer for the Spaceship client.
//  Not built by CMake or CI. Drives the game's real ShellView against
//  raylib's software renderer (PLATFORM_MEMORY) and writes PNG frames, so the
//  screens can be looked at on a machine with no display.
//
//  A frame shows what the game computes: layout, strings, sizes, hit boxes.
//  It does not show the Windows fonts, GPU anti-aliasing, the display
//  adjustment shader or sound. It is a look, not a test: nothing is asserted.
//
//  Build, usage and the script commands: README.md beside this file.
// ============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "imgui.h"
#include "raylib.h"
#include "rlgl.h"
#include "rlImGui.h"

#include "audio/Audio.h"
#include "core/AutoSave.h"
#include "core/DisplaySettings.h"
#include "core/Simulation.h"
#include "data/Catalog.h"
#include "data/ChartExpansion.h"
#include "ui/AwardsScreen.h"
#include "ui/CrewRecord.h"
#include "ui/Describe.h"
#include "ui/EventScreen.h"
#include "ui/First2000Styles.h"
#include "ui/FloatingPanels.h"
#include "ui/Fx.h"
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
// Viewer only: read the shell's hit lists so a script can press what is drawn.
#define private public
#include "ui/Draw.h"
#undef private
#include "ui/Palette.h"

namespace fs = std::filesystem;

// ---- text recorder: every string the shell paints in the current frame, with
// the pixel size and the measured width it was painted at. The link flag
// -Wl,--wrap=DrawTextEx routes the game's own calls through here. -------------
struct TextCall {
  std::string text;
  float x, y, size, width;
  Color tint;
  unsigned int font;
};
static bool g_writeTextLog = false;
static std::vector<TextCall> g_text;
extern "C" void __real_DrawTextEx(Font font, const char* text, Vector2 pos, float size,
                                  float spacing, Color tint);
extern "C" void __wrap_DrawTextEx(Font font, const char* text, Vector2 pos, float size,
                                  float spacing, Color tint) {
  const char* safe = text ? text : "";
  g_text.push_back({safe, pos.x, pos.y, size, MeasureTextEx(font, safe, size, spacing).x, tint,
                    font.texture.id});
  __real_DrawTextEx(font, text, pos, size, spacing, tint);
}

struct FontResources {
  Font body{};
  Font bold{};
  Font mono{};
  std::array<ImFont*, 6> imguiBody{};
};

static Font LoadRayFont(const std::string& path) {
  std::vector<int> codepoints;
  for (int cp = 32; cp <= 126; ++cp) { codepoints.push_back(cp); }
  for (int cp = 160; cp <= 255; ++cp) { codepoints.push_back(cp); }
  for (int cp : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
                 0x2190, 0x2191, 0x2192, 0x2193}) {
    codepoints.push_back(cp);
  }
  Font font = LoadFontEx(path.c_str(), 48, codepoints.data(), static_cast<int>(codepoints.size()));
  if (font.texture.id != 0) { SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR); }
  return font;
}

static std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> parts;
  std::istringstream stream(line);
  std::string word;
  while (stream >> word) { parts.push_back(word); }
  return parts;
}

static std::string Rest(const std::vector<std::string>& parts, std::size_t from) {
  std::string out;
  for (std::size_t i = from; i < parts.size(); ++i) {
    if (!out.empty()) { out += ' '; }
    out += parts[i];
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: viewer <script> [width height] [segoe|dejavu]\n"
                         "       run it through run.sh, which sets the working directory\n");
    return 2;
  }
  const std::string scriptPath = argv[1];
  const int width = argc >= 4 ? std::atoi(argv[2]) : 1440;
  const int height = argc >= 4 ? std::atoi(argv[3]) : 940;
  const std::string fontSet = argc >= 5 ? argv[4] : "segoe";
  // Where things go. run.sh sets all three; the defaults keep a bare call usable.
  //   VIEWER_OUT    frames and text logs            (default: ./out)
  //   VIEWER_SAVES  the save directory of the shell (default: <out>/saves)
  //   VIEWER_FONTS  directory holding Selawik-Regular.ttf and Selawik-Bold.ttf
  auto envPath = [](const char* name, const fs::path& fallback) {
    const char* value = std::getenv(name);
    return fs::absolute(value != nullptr && *value != '\0' ? fs::path(value) : fallback);
  };
  const fs::path outDir = envPath("VIEWER_OUT", "out");
  const fs::path saveDir = envPath("VIEWER_SAVES", outDir / "saves");
  const fs::path fontDir = envPath("VIEWER_FONTS", "fonts");
  fs::create_directories(outDir);
  fs::create_directories(saveDir);
  if (fontSet != "segoe" && fontSet != "dejavu") {
    std::fprintf(stderr, "viewer: font set must be segoe or dejavu, not '%s'\n", fontSet.c_str());
    return 2;
  }

  const fs::path assets = "assets";
  ship::Catalog catalog;
  try {
    catalog = ship::Catalog::LoadFromAssets(assets);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "viewer: %s\n", error.what());
    return 1;
  }
  ship::Simulation sim = ship::Simulation::NewVoyage(catalog);

  SetTraceLogLevel(LOG_WARNING);
  InitWindow(width, height, "Spaceship Command (viewer)");

  ship::Theme theme = ship::Theme::ById(sim.State().options.theme);
  theme.fontStep = sim.State().options.fontStep;
  theme.largeText = sim.State().options.largeText;
  theme.hideCrewLabels = sim.State().options.hideCrewLabels;
  theme.reducedMotion = sim.State().reducedMotion;
  theme.stageCompact = sim.State().shipClass == "wren";

  // Fonts. "segoe" stands in for the Windows faces with Selawik, which carries
  // Segoe UI's advance widths; "dejavu" is what the repository ships.
  std::string sansPath, boldPath, monoPath;
  if (fontSet == "dejavu") {
    sansPath = "assets/fonts/DejaVuSans.ttf";
    boldPath = "assets/fonts/DejaVuSans-Bold.ttf";
  } else {
    sansPath = (fontDir / "Selawik-Regular.ttf").string();
    boldPath = (fontDir / "Selawik-Bold.ttf").string();
  }
  monoPath = "assets/fonts/DejaVuSansMono.ttf";
  for (const std::string& path : {sansPath, boldPath, monoPath}) {
    if (!fs::exists(path)) {
      // Never fall back silently: a frame in the wrong face misleads.
      std::fprintf(stderr, "viewer: font file missing: %s\n", path.c_str());
      return 1;
    }
  }

  rlImGuiBeginInitImGui();
  FontResources fonts;
  fonts.body = LoadRayFont(sansPath);
  fonts.bold = LoadRayFont(boldPath);
  fonts.mono = LoadRayFont(monoPath);
  {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    for (int i = 0; i < 6; ++i) {
      fonts.imguiBody[static_cast<std::size_t>(i)] =
          io.Fonts->AddFontFromFileTTF(sansPath.c_str(), static_cast<float>(18 + i));
    }
    io.Fonts->AddFontFromFileTTF(monoPath.c_str(), 18.0f);
  }
  rlImGuiEndInitImGui();
  ImGui::GetIO().Fonts->Build();
  auto selectImGuiFont = [&](int fontStep, bool largeText) {
    const int clamped = fontStep < 0 ? 0 : (fontStep > 3 ? 3 : fontStep);
    ImFont* selected = fonts.imguiBody[static_cast<std::size_t>(clamped + (largeText ? 2 : 0))];
    if (selected != nullptr) { ImGui::GetIO().FontDefault = selected; }
  };
  selectImGuiFont(sim.State().options.fontStep, sim.State().options.largeText);
  ship::ui::ApplyPalette(theme);

  Texture2D titleTexture = LoadTexture("assets/art/title.jpg");
  if (titleTexture.id != 0) { SetTextureFilter(titleTexture, TEXTURE_FILTER_BILINEAR); }
  Texture2D setupTexture = LoadTexture("assets/art/setupscreen.png");
  if (setupTexture.id != 0) { SetTextureFilter(setupTexture, TEXTURE_FILTER_BILINEAR); }
  Texture2D wrenExterior = LoadTexture("assets/art/ships/wren-exterior.png");
  if (wrenExterior.id != 0) { SetTextureFilter(wrenExterior, TEXTURE_FILTER_BILINEAR); }
  std::array<Texture2D, 6> tutorialTextures{};
  for (std::size_t i = 0; i < tutorialTextures.size(); ++i) {
    const std::string path = "assets/art/tutorial/spaceship0" + std::to_string(i + 1) + ".png";
    tutorialTextures[i] = LoadTexture(path.c_str());
    if (tutorialTextures[i].id != 0) { SetTextureFilter(tutorialTextures[i], TEXTURE_FILTER_BILINEAR); }
  }
  // Crew pictures, as the client loads them: every PNG in assets/art/crew.
  std::map<std::string, Texture2D> crewTextures;
  {
    std::error_code ec;
    for (std::filesystem::directory_iterator it("assets/art/crew", ec), end;
         !ec && it != end; it.increment(ec)) {
      const std::filesystem::path& path = it->path();
      if (path.extension() != ".png") { continue; }
      Texture2D texture = LoadTexture(path.string().c_str());
      if (texture.id == 0) { continue; }
      SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
      crewTextures[path.stem().string()] = texture;
    }
  }

  ship::ShellView shell;
  shell.SetSaveDir(saveDir.u8string());
  shell.SetFonts(fonts.body, fonts.bold, fonts.mono);
  shell.SetTitleTexture(&titleTexture);
  shell.SetSetupTexture(&setupTexture);
  shell.SetWrenExteriorTexture(&wrenExterior);
  for (std::size_t i = 0; i < tutorialTextures.size(); ++i) { shell.SetTutorialTexture(i, &tutorialTextures[i]); }
  for (auto& picture : crewTextures) { shell.SetCrewTexture(picture.first, &picture.second); }
  shell.Configure(static_cast<float>(width), static_cast<float>(height), theme);
  shell.SetDisplayAdjustmentAvailable(false);
  shell.SetAudioStatus("viewer: no audio device");

  double nowMs = 1000.0;
  float mouseX = -1.0f, mouseY = -1.0f;
  int imButton = -1;  // -1 none, 1 press this frame, 0 release this frame

  auto frame = [&](double dtMs) {
    nowMs += dtMs;
    SetMousePosition(static_cast<int>(mouseX), static_cast<int>(mouseY));
    BeginDrawing();
    rlImGuiBegin();
    if (imButton == 1) { ImGui::GetIO().AddMouseButtonEvent(0, true); imButton = -1; }
    else if (imButton == 0) { ImGui::GetIO().AddMouseButtonEvent(0, false); imButton = -1; }
    shell.Move(mouseX, mouseY);

    ship::Theme next = ship::Theme::ById(sim.State().options.theme);
    next.fontStep = sim.State().options.fontStep;
    next.largeText = sim.State().options.largeText;
    next.hideCrewLabels = sim.State().options.hideCrewLabels;
    next.reducedMotion = sim.State().reducedMotion;
    next.stageCompact = sim.State().shipClass == "wren";
    const bool changed = next.id != theme.id || next.fontStep != theme.fontStep ||
                         next.largeText != theme.largeText ||
                         next.hideCrewLabels != theme.hideCrewLabels ||
                         next.reducedMotion != theme.reducedMotion ||
                         next.stageCompact != theme.stageCompact;
    if (changed) {
      theme = next;
      shell.Configure(static_cast<float>(width), static_cast<float>(height), theme);
      ship::ui::ApplyPalette(theme);
    }
    selectImGuiFont(sim.State().options.fontStep, sim.State().options.largeText);
    g_text.clear();
    shell.Draw(sim, nowMs);
    rlImGuiEnd();
    EndDrawing();
  };

  std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
  // The whole frame, or the rectangle x y w h of it.
  auto shot = [&](const std::string& name, int cx, int cy, int cw, int ch) {
    rlCopyFramebuffer(0, 0, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, pixels.data());
    cx = std::clamp(cx, 0, width - 1);
    cy = std::clamp(cy, 0, height - 1);
    cw = std::clamp(cw, 1, width - cx);
    ch = std::clamp(ch, 1, height - cy);
    std::vector<unsigned char> crop(static_cast<std::size_t>(cw) * ch * 4);
    for (int y = 0; y < ch; ++y) {
      for (int x = 0; x < cw; ++x) {
        const unsigned char* src = &pixels[(static_cast<std::size_t>(cy + y) * width + cx + x) * 4];
        unsigned char* dst = &crop[(static_cast<std::size_t>(y) * cw + x) * 4];
        dst[0] = src[2];  // the rasterizer hands back BGRA
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = 255;
      }
    }
    Image image{crop.data(), cw, ch, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    const fs::path file = outDir / (name + ".png");
    ExportImage(image, file.string().c_str());
    if (g_writeTextLog) {
      std::ofstream log(outDir / (name + ".text.tsv"));
      log << "size\tx\ty\twidth\tfont\trgba\ttext\n";
      for (const TextCall& call : g_text) {
        log << call.size << '\t' << call.x << '\t' << call.y << '\t' << call.width << '\t'
            << (call.font == fonts.bold.texture.id ? "bold" : call.font == fonts.mono.texture.id ? "mono" : "body")
            << '\t' << static_cast<int>(call.tint.r) << ',' << static_cast<int>(call.tint.g) << ','
            << static_cast<int>(call.tint.b) << ',' << static_cast<int>(call.tint.a) << '\t'
            << call.text << '\n';
      }
    }
    std::printf("shot %s %dx%d (%zu text calls)\n", file.string().c_str(), cw, ch, g_text.size());
  };

  // Strings of the current frame whose painted boxes intersect, optionally
  // only those that start inside one rectangle. A box is the measured width by
  // the font size, so two lines set tighter than the font size count.
  auto overlaps = [&](bool bounded, float rx, float ry, float rw, float rh) {
    int pairs = 0;
    std::size_t considered = 0;
    auto inside = [&](const TextCall& c) {
      return !bounded || (c.x >= rx && c.y >= ry && c.x < rx + rw && c.y < ry + rh);
    };
    for (std::size_t i = 0; i < g_text.size(); ++i) {
      const TextCall& a = g_text[i];
      if (a.text.empty() || !inside(a)) { continue; }
      ++considered;
      for (std::size_t j = i + 1; j < g_text.size(); ++j) {
        const TextCall& b = g_text[j];
        if (b.text.empty() || !inside(b)) { continue; }
        const float ix = std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
        const float iy = std::min(a.y + a.size, b.y + b.size) - std::max(a.y, b.y);
        if (ix < 1.0f || iy < 1.0f) { continue; }
        ++pairs;
        std::printf("OVERLAP %4.0f x %-3.0f px  \"%s\"  <->  \"%s\"\n", ix, iy, a.text.c_str(),
                    b.text.c_str());
      }
    }
    std::printf("OVERLAP %d pair(s) among %zu strings\n", pairs, considered);
  };

  // The illustrated stage of the active screen: every part that carries text,
  // the size its style row asks for, the size it was painted at, and how far
  // the painted string exceeds the part's own box.
  //
  // Where the stage sits and how large it is painted are read off its first
  // live control (painted box against model box), so this works on a checkout
  // that scales its stages and on one that does not. A stage without a control
  // is taken as painted 1:1 and its strings are matched by text alone.
  auto stageReport = [&]() {
    const ship::TaskScreenModel& screen = shell.taskScreen_;
    if (!screen.hasStage) { std::printf("STAGE none on the active screen\n"); return; }
    const ship::TaskStage& stage = screen.stage;
    bool placed = false;
    float scale = 1.0f, ox = 0.0f, oy = 0.0f;
    if (!stage.controls.empty() && stage.controls.front().box.w > 0.0f) {
      const ship::LiveControl& control = stage.controls.front();
      for (const auto& hit : shell.taskButtons_) {
        if (hit.second != control.button.taskId) { continue; }
        scale = hit.first.w / control.box.w;
        ox = hit.first.x - control.box.x * scale;
        oy = hit.first.y - control.box.y * scale;
        placed = true;
        break;
      }
    }
    std::printf("STAGE %s  model %.0f x %.0f  painted x%.3f = %.0f x %.0f  %zu parts  %zu controls\n",
                stage.id.c_str(), stage.width, stage.height, scale, stage.width * scale,
                stage.height * scale, stage.parts.size(), stage.controls.size());
    if (placed) {
      std::printf("STAGE origin %.1f,%.1f  right edge %.1f  bottom edge %.1f\n", ox, oy,
                  ox + stage.width * scale, oy + stage.height * scale);
    }
    std::printf("STAGE %-12s %3s %3s  %-9s %6s %6s %6s  %s\n", "class", "ref", "px", "box",
                "text.w", "over.w", "over.h", "text");
    std::vector<bool> used(g_text.size(), false);
    // The painted string equal to `text`; when the stage is placed, the one
    // nearest to where the part's box says it starts.
    auto find = [&](const std::string& text, float wantY) {
      int best = -1;
      float bestD = 1e9f;
      for (std::size_t i = 0; i < g_text.size(); ++i) {
        if (used[i] || g_text[i].text != text) { continue; }
        const float d = placed ? std::fabs(g_text[i].y - wantY) : 0.0f;
        if (d < bestD) { bestD = d; best = static_cast<int>(i); }
        if (!placed) { break; }
      }
      if (best >= 0 && placed && bestD > 3.0f) { best = -1; }
      if (best >= 0) { used[static_cast<std::size_t>(best)] = true; }
      return best;
    };
    int captions = 0, wide = 0, tall = 0, missing = 0, outside = 0;
    for (const ship::MachinePart& part : stage.parts) {
      if (part.text.empty()) { continue; }
      ++captions;
      const ship::SceneStyle style = ship::SceneStyleFor(part.cls);
      const float boxW = part.box.w * scale, boxH = part.box.h * scale;
      const float wantY = oy + part.box.y * scale;
      char box[32];
      std::snprintf(box, sizeof(box), "%.0fx%.0f", boxW, boxH);
      float textW = 0.0f, textH = 0.0f, left = 1e9f, right = -1e9f, bottom = -1e9f;
      bool found = true;
      // "label|value" may be painted as one string or as its two halves.
      std::vector<std::string> pieces{part.text};
      int first = find(part.text, wantY);
      if (first < 0 && part.text.find('|') != std::string::npos) {
        const std::size_t bar = part.text.find('|');
        pieces = {part.text.substr(0, bar), part.text.substr(bar + 1)};
        first = find(pieces[0], wantY);
      }
      for (std::size_t n = 0; n < pieces.size(); ++n) {
        const int index = n == 0 ? first : find(pieces[n], wantY);
        if (index < 0) { found = false; break; }
        const TextCall& call = g_text[static_cast<std::size_t>(index)];
        textW += call.width + (n > 0 ? 8.0f : 0.0f);  // halves keep 8 px between them
        textH = std::max(textH, call.size);
        left = std::min(left, call.x);
        right = std::max(right, call.x + call.width);
        bottom = std::max(bottom, call.y + call.size);
      }
      if (!found) {
        ++missing;
        std::printf("STAGE %-12s %3d %3s  %-9s %6s %6s %6s  %s  (NOT PAINTED)\n", part.cls.c_str(),
                    style.fontPx, "-", box, "-", "-", "-", part.text.c_str());
        continue;
      }
      const float overW = textW - boxW, overH = textH - boxH;
      if (overW > 0.5f) { ++wide; }
      if (overH > 0.5f) { ++tall; }
      const bool out = placed && (left < ox - 0.5f || right > ox + stage.width * scale + 0.5f ||
                                  bottom > oy + stage.height * scale + 0.5f);
      if (out) { ++outside; }
      std::printf("STAGE %-12s %3d %3.0f  %-9s %6.1f %6.1f %6.1f  %s%s\n", part.cls.c_str(),
                  style.fontPx, textH, box, textW, std::max(0.0f, overW), std::max(0.0f, overH),
                  part.text.c_str(), out ? "  (OUTSIDE THE STAGE)" : "");
    }
    for (const ship::LiveControl& control : stage.controls) {
      for (const auto& hit : shell.taskButtons_) {
        if (hit.second != control.button.taskId) { continue; }
        const int index = find(control.button.text, hit.first.y);
        // A label is searched within the painted button, not at its top edge.
        int label = index;
        if (label < 0) {
          for (std::size_t i = 0; i < g_text.size(); ++i) {
            if (!used[i] && g_text[i].text == control.button.text && g_text[i].y >= hit.first.y - 1.0f &&
                g_text[i].y <= hit.first.y + hit.first.h) { label = static_cast<int>(i); used[i] = true; break; }
          }
        }
        if (label < 0) { std::printf("STAGE control %-22s label not painted\n", control.button.taskId.c_str()); break; }
        const TextCall& call = g_text[static_cast<std::size_t>(label)];
        const float below = (call.y + call.size) - (hit.first.y + hit.first.h);
        const float beyond = (call.x + call.width) - (hit.first.x + hit.first.w);
        std::printf("STAGE control %-22s box %.0fx%.0f  label %.0f px, %.1f wide: %s\n",
                    control.button.taskId.c_str(), hit.first.w, hit.first.h, call.size, call.width,
                    below > 0.5f || beyond > 0.5f ? "RUNS OUT OF THE BUTTON" : "inside the button");
        break;
      }
    }
    std::printf("STAGE %d text parts: %d wider than their box, %d taller than their box, "
                "%d not painted, %d outside the stage\n", captions, wide, tall, missing, outside);
  };

  auto dumpArea = [](const char* list, const std::string& label, const ship::Area& box) {
    std::printf("HIT %-6s x=%7.1f y=%7.1f w=%7.1f h=%6.1f  %s\n", list, box.x, box.y, box.w, box.h,
                label.c_str());
  };
  auto dumpHits = [&](const std::string& which) {
    if (which == "tabs" || which == "all") {
      for (const auto& hit : shell.tabHits_) { dumpArea("tab", hit.key, hit.box); }
    }
    if (which == "side" || which == "all") {
      for (const auto& hit : shell.sideHits_) {
        dumpArea("side", hit.key + " | " + hit.text + " | action=" + std::to_string(static_cast<int>(hit.action)), hit.box);
      }
    }
    if (which == "nav" || which == "all") {
      for (const auto& hit : shell.navHits_) { dumpArea("nav", hit.action + " | " + hit.arg, hit.box); }
    }
    if (which == "band" || which == "all") {
      for (const auto& hit : shell.actionBandHits_) { dumpArea("band", hit.action + " | " + hit.arg, hit.box); }
    }
    if (which == "task" || which == "all") {
      for (const auto& hit : shell.taskButtons_) { dumpArea("task", hit.second, hit.first); }
    }
    if (which == "title" || which == "all") {
      for (const auto& hit : shell.titleHits_) { dumpArea("title", hit.action + " | " + hit.arg, hit.box); }
    }
    if (which == "loss" || which == "all") {
      for (const auto& hit : shell.lossHits_) { dumpArea("loss", hit.action + " | " + hit.arg, hit.box); }
    }
  };
  auto findHit = [&](const std::string& list, const std::string& needle, ship::Area* out) {
    auto match = [&](const std::string& label) { return label.find(needle) != std::string::npos; };
    if (list == "tabs") { for (const auto& h : shell.tabHits_) if (match(h.key)) { *out = h.box; return true; } }
    if (list == "side") { for (const auto& h : shell.sideHits_) if (match(h.key + " | " + h.text)) { *out = h.box; return true; } }
    if (list == "nav") { for (const auto& h : shell.navHits_) if (match(h.action + " | " + h.arg)) { *out = h.box; return true; } }
    if (list == "band") { for (const auto& h : shell.actionBandHits_) if (match(h.action + " | " + h.arg)) { *out = h.box; return true; } }
    if (list == "task") { for (const auto& h : shell.taskButtons_) if (match(h.second)) { *out = h.first; return true; } }
    if (list == "title") { for (const auto& h : shell.titleHits_) if (match(h.action + " | " + h.arg)) { *out = h.box; return true; } }
    if (list == "loss") { for (const auto& h : shell.lossHits_) if (match(h.action + " | " + h.arg)) { *out = h.box; return true; } }
    return false;
  };

  std::ifstream script(scriptPath);
  if (!script) { std::fprintf(stderr, "viewer: cannot open %s\n", scriptPath.c_str()); return 1; }
  std::string line;
  int lineNo = 0;
  int problems = 0;  // script lines that could not be carried out
  while (std::getline(script, line)) {
    ++lineNo;
    if (line.empty() || line[0] == '#') { continue; }
    const std::vector<std::string> p = Split(line);
    if (p.empty()) { continue; }
    const std::string& c = p[0];
    auto num = [&](std::size_t i, double fallback) { return p.size() > i ? std::atof(p[i].c_str()) : fallback; };

    if (c == "frames") {
      const int n = static_cast<int>(num(1, 1));
      const double dt = num(2, 1000.0 / 60.0);
      for (int i = 0; i < n; ++i) { frame(dt); }
    } else if (c == "shot" && p.size() >= 2) {
      if (p.size() >= 6) {
        shot(p[1], static_cast<int>(num(2, 0)), static_cast<int>(num(3, 0)),
             static_cast<int>(num(4, width)), static_cast<int>(num(5, height)));
      } else {
        shot(p[1], 0, 0, width, height);
      }
    } else if (c == "textlog" && p.size() >= 2) {
      g_writeTextLog = p[1] == "on";
    } else if (c == "stage") {
      stageReport();
    } else if (c == "overlaps") {
      if (p.size() >= 5) {
        overlaps(true, static_cast<float>(num(1, 0)), static_cast<float>(num(2, 0)),
                 static_cast<float>(num(3, 0)), static_cast<float>(num(4, 0)));
      } else {
        overlaps(false, 0, 0, 0, 0);
      }
    } else if (c == "move" && p.size() >= 3) {
      mouseX = static_cast<float>(num(1, 0)); mouseY = static_cast<float>(num(2, 0));
    } else if ((c == "click" || c == "rclick") && p.size() >= 3) {
      mouseX = static_cast<float>(num(1, 0)); mouseY = static_cast<float>(num(2, 0));
      shell.Move(mouseX, mouseY);
      shell.Click(sim, mouseX, mouseY, c == "click" ? ship::HitButton::Left : ship::HitButton::Right, nowMs);
    } else if ((c == "clickhit" || c == "rclickhit" || c == "hoverhit") && p.size() >= 3) {
      ship::Area box;
      if (!findHit(p[1], Rest(p, 2), &box)) {
        std::printf("line %d: no %s hit matching '%s'\n", lineNo, p[1].c_str(), Rest(p, 2).c_str());
        ++problems;
      } else if (box.x + box.w / 2.0f >= static_cast<float>(width) ||
                 box.y + box.h / 2.0f >= static_cast<float>(height)) {
        // The middle of the box is past the edge of the frame, as a row below
        // the fold is. A press there reaches nothing, so it is a failed line.
        std::printf("line %d: %s hit '%s' is outside the frame at %.0f,%.0f\n", lineNo, p[1].c_str(),
                    Rest(p, 2).c_str(), box.x + box.w / 2.0f, box.y + box.h / 2.0f);
        ++problems;
      } else {
        mouseX = box.x + box.w / 2.0f; mouseY = box.y + box.h / 2.0f;
        shell.Move(mouseX, mouseY);
        if (c != "hoverhit") {
          shell.Click(sim, mouseX, mouseY, c == "clickhit" ? ship::HitButton::Left : ship::HitButton::Right, nowMs);
        }
        std::printf("%s %s '%s' at %.0f,%.0f\n", c.c_str(), p[1].c_str(), Rest(p, 2).c_str(), mouseX, mouseY);
      }
    } else if (c == "scroll" && p.size() >= 4) {
      shell.Scroll(sim, static_cast<float>(num(1, 0)), static_cast<float>(num(2, 0)), static_cast<float>(num(3, 0)));
    } else if (c == "imclick" && p.size() >= 3) {
      mouseX = static_cast<float>(num(1, 0)); mouseY = static_cast<float>(num(2, 0));
      frame(16.0); imButton = 1; frame(16.0); imButton = 0; frame(16.0); frame(16.0);
    } else if (c == "hits") {
      dumpHits(p.size() >= 2 ? p[1] : "all");
    } else if (c == "open" && p.size() >= 3) {
      const bool ok = ship::OpenTask(sim, p[1], p[2], true);
      std::printf("open %s %s -> %d\n", p[1].c_str(), p[2].c_str(), ok ? 1 : 0);
      if (!ok) { ++problems; }
    } else if (c == "tab" && p.size() >= 2) {
      ship::SwitchTab(sim, p[1]);
    } else if (c == "title") {
      shell.ShowTitle(num(1, 0) != 0);
    } else if (c == "hidetitle") {
      shell.HideTitle();
    } else if (c == "popup" && p.size() >= 2) {
      shell.OpenPopup(p[1]);
    } else if (c == "closepopup") {
      shell.ClosePopup();
    } else if (c == "escape") {
      shell.Escape(sim);
    } else if (c == "tutorial" && p.size() >= 2) {
      shell.PageTutorial(static_cast<int>(num(1, 1)), nowMs);
    } else if (c == "act") {
      const int n = static_cast<int>(num(1, 1));
      for (int i = 0; i < n; ++i) { sim.PerformAction(p.size() >= 3 ? Rest(p, 2) : std::string("Viewer click")); }
    } else if (c == "stat" && p.size() >= 3) {
      if (ship::Stat* stat = sim.FindStat(p[1])) { stat->value = num(2, 0); }
      else { std::printf("line %d: no stat %s\n", lineNo, p[1].c_str()); ++problems; }
    } else if (c == "survival" && p.size() >= 3) {
      double* field = nullptr;
      if (p[1] == "airHold") field = &sim.MutableState().airHold;
      if (p[1] == "filterLife") field = &sim.MutableState().filterLife;
      if (p[1] == "engineCondition") field = &sim.MutableState().engineCondition;
      if (p[1] == "rack") field = &sim.MutableMess().rack;
      if (p[1] == "canisters") field = &sim.MutableLife().canisters;
      if (field != nullptr) { *field = num(2, 0); } else { std::printf("line %d: no survival field\n", lineNo); ++problems; }
    } else if (c == "crewset" && p.size() >= 4) {
      auto* crew = sim.FindCrew(p[1]);
      double* field = nullptr;
      if (crew != nullptr) {
        if (p[2] == "health") field = &crew->health;
        if (p[2] == "hunger") field = &crew->hunger;
        if (p[2] == "morale") field = &crew->morale;
        if (p[2] == "fatigue") field = &crew->fatigue;
      }
      if (field != nullptr) { *field = num(3, 0); } else { std::printf("line %d: no crew field\n", lineNo); ++problems; }
    } else if (c == "purse" && p.size() >= 2) {
      sim.MutableState().shiplets = num(1, 0);
    } else if (c == "theme" && p.size() >= 2) {
      sim.MutableState().options.theme = p[1];
    } else if (c == "fontstep" && p.size() >= 2) {
      sim.MutableState().options.fontStep = static_cast<int>(num(1, 0));
    } else if (c == "largetext" && p.size() >= 2) {
      sim.MutableState().options.largeText = num(1, 0) != 0;
    } else if (c == "target" && p.size() >= 2) {
      sim.SelectTarget(p[1]);
    } else if (c == "travel") { sim.StartTravel();
    } else if (c == "scan") { sim.StartScan();
    } else if (c == "dock") { sim.Dock();
    } else if (c == "undock") { sim.Undock();
    } else if (c == "cook") { sim.StartCookingSelf();
    } else if (c == "maint") { sim.StartMaintenanceSelf();
    } else if (c == "seat" && p.size() >= 2) { sim.StartCanisterInstall(std::atoi(p[1].c_str()));
    } else if (c == "bank") { sim.StartFilterBank();
    } else if (c == "job" && p.size() >= 3) { sim.ApplyJob(p[1], p[2]);
    } else if (c == "stagejob" && p.size() >= 3) {
      if (!sim.StageJob(p[1], p[2])) { std::printf("line %d: no %s order written for %s\n", lineNo, p[2].c_str(), p[1].c_str()); ++problems; }
    } else if (c == "hire" && p.size() >= 2) {
      if (!sim.HireCrew(p[1])) { std::printf("line %d: %s was not hired\n", lineNo, p[1].c_str()); ++problems; }
    } else if (c == "ack" && p.size() >= 2) { sim.Acknowledge(p[1]);
    } else if (c == "layer" && p.size() >= 2) { sim.SetLayer(p[1]);
    } else if (c == "state") {
      const ship::GameState& s = sim.State();
      std::printf("STATE actions=%.0f tab=%s here=%s docked=%d shiplets=%.0f gameOver='%s' title=%d popup='%s'\n",
                  sim.ActionCount(), s.activeTab.c_str(), sim.Map().here.c_str(), s.docked ? 1 : 0,
                  s.shiplets, s.gameOver.c_str(), shell.TitleActive() ? 1 : 0, shell.PopupKey().c_str());
      std::printf("STATE tabs:");
      for (const auto& tab : s.tabs) { std::printf(" %s:%s", tab.kind.c_str(), tab.id.c_str()); }
      std::printf("\n");
      std::printf("STATE air: hold=%.1f stage=%d word=%s filterLife=%.0f engine=%.0f rack=%.0f rations=%.0f canisters=%.0f\n",
                  s.airHold, sim.AirStageIndex(), sim.AirStageWord().c_str(), s.filterLife, s.engineCondition,
                  sim.MutableMess().rack, sim.MutableMess().rations, sim.Life().canisters);
      std::printf("STATE stats:");
      for (const auto& stat : sim.Stats()) { std::printf(" %s=%.0f", stat.id.c_str(), stat.value); }
      std::printf("\nSTATE crew:");
      for (const auto& crew : sim.Crew()) {
        std::printf(" %s[hp=%.0f hunger=%.0f morale=%.0f fat=%.0f job=%s]", crew.id.c_str(), crew.health,
                    crew.hunger, crew.morale, crew.fatigue, crew.job.c_str());
      }
      std::printf("\nSTATE events:");
      for (const auto& ev : sim.Events()) { std::printf(" %s(type=%s kind=%s stage=%d left=%.0f)", ev.id.c_str(), ev.type.c_str(), ev.kind.c_str(), ev.stage, ev.left); }
      std::printf("\nSTATE infos:");
      for (const auto& info : sim.Infos()) { std::printf(" [%s|%s|%s]", info.id.c_str(), info.label.c_str(), info.value.c_str()); }
      std::printf("\n");
    } else {
      std::printf("line %d: unknown command '%s'\n", lineNo, c.c_str());
      ++problems;
    }
    std::fflush(stdout);
  }

  rlImGuiShutdown();
  CloseWindow();
  if (problems > 0) { std::printf("viewer: %d script line(s) could not be carried out\n", problems); }
  return problems > 0 ? 3 : 0;
}
