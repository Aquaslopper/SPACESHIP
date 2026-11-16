// ============================================================================
//  main.cpp — the client.
//  ---------------------------------------------------------------------------
//  Phase 2 turns the Phase 0 skeleton into the cockpit: the window opens at the
//  reference's size, the catalogue loads, a voyage begins, and ShellView paints
//  the taskbar, the sidebar, the pinned foot, the status bar and the idle
//  screen's ship cross-section. Task windows arrive in Phase 3.
//
//  Everything the shell decides is in cpp/ui's headless view-models; this file
//  is only the loop that turns a mouse into a verb and a frame into a paint.
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "rlgl.h"

#include "audio/Audio.h"
#include "audio/AudioDevice.h"
#include "core/Simulation.h"
#include "core/AutoSave.h"
#include "core/DisplaySettings.h"
#include "data/Catalog.h"
#include "ui/Draw.h"
#include "ui/NavScreen.h"
#include "ui/PointerOwnership.h"
#include "ui/Palette.h"
#include "ui/Theme.h"

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall MessageBoxA(void*, const char*, const char*, unsigned int);
#endif

namespace {

void SaveWarning(const std::string& message) {
  std::fprintf(stderr, "spaceship: %s\n", message.c_str());
#ifdef _WIN32
  MessageBoxA(nullptr, message.c_str(), "Spaceship save warning", 0x30u);
#endif
}

using ship::FeedLine;
using ship::Options;
using ship::Simulation;
namespace audio = ship::audio;

// The reference opens itself at 1440x940 and centres the window.
constexpr int kDefaultWidth = 1440;
constexpr int kDefaultHeight = 940;

bool HasCatalog(const std::filesystem::path& assets) {
  return std::filesystem::exists(assets / "data" / "manifest.json");
}

std::filesystem::path FindAssetDir() {
  std::filesystem::path selected;
  const std::filesystem::path relativeAssets = "assets";
  if (HasCatalog(relativeAssets)) {
    std::error_code ec;
    selected = std::filesystem::absolute(relativeAssets, ec);
    if (ec) { selected = relativeAssets; }
  }

  const char* applicationDirectory = GetApplicationDirectory();
  if (applicationDirectory != nullptr && *applicationDirectory != '\0') {
    const std::filesystem::path appDir(applicationDirectory);
    const std::filesystem::path besideExecutable = appDir / "assets";
    if (HasCatalog(besideExecutable)) { selected = besideExecutable; }

    // Save slots are relative to the working directory. Explorer may launch an
    // application with an unrelated current directory, so anchor runtime data
    // beside the executable before the UI or save system is initialized.
    std::error_code ec;
    std::filesystem::current_path(appDir, ec);
  }

  return selected.empty() ? ship::Catalog::DefaultAssetDir() : selected;
}

struct FontResources {
  Font body{};
  Font bold{};
  Font mono{};
  std::array<ImFont*, 6> imguiBody{};
};

std::string FirstFont(const std::vector<std::filesystem::path>& candidates) {
  for (const std::filesystem::path& candidate : candidates) {
    if (std::filesystem::exists(candidate)) { return candidate.string(); }
  }
  return {};
}

Font LoadRayFont(const std::vector<std::filesystem::path>& candidates) {
  std::vector<int> codepoints;
  for (int cp = 32; cp <= 126; ++cp) { codepoints.push_back(cp); }
  for (int cp = 160; cp <= 255; ++cp) { codepoints.push_back(cp); }
  for (int cp : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
                 0x2190, 0x2191, 0x2192, 0x2193}) {
    codepoints.push_back(cp);
  }
  for (const std::filesystem::path& candidate : candidates) {
    if (!std::filesystem::exists(candidate)) { continue; }
    Font font = LoadFontEx(candidate.string().c_str(), 48, codepoints.data(),
                           static_cast<int>(codepoints.size()));
    if (font.texture.id != 0) {
      SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
      return font;
    }
  }
  return Font{};
}

FontResources LoadFonts(const std::filesystem::path& assets) {
  const std::filesystem::path fonts = assets / "fonts";
  const std::vector<std::filesystem::path> sans = {
      "C:/Windows/Fonts/segoeui.ttf", fonts / "DejaVuSans.ttf"};
  const std::vector<std::filesystem::path> bold = {
      "C:/Windows/Fonts/segoeuib.ttf", fonts / "DejaVuSans-Bold.ttf"};
  const std::vector<std::filesystem::path> mono = {
      "C:/Windows/Fonts/consola.ttf", fonts / "DejaVuSansMono.ttf"};
  const std::string sansPath = FirstFont(sans);
  const std::string boldPath = FirstFont(bold);
  const std::string monoPath = FirstFont(mono);

  FontResources loaded;
  loaded.body = LoadRayFont(sans);
  loaded.bold = LoadRayFont(bold);
  loaded.mono = LoadRayFont(mono);
  if (loaded.body.texture.id == 0) { loaded.body = loaded.bold; }
  if (loaded.bold.texture.id == 0) { loaded.bold = loaded.body; }
  if (loaded.mono.texture.id == 0) { loaded.mono = loaded.body; }

  ImGuiIO& io = ImGui::GetIO();
  // Include the +2px larger-reading preference, not only the four steps.
  // Even without an installed font file the ImGui fallback must stay >=18px.
  for (int i = 0; i < 6; ++i) {
    ImFontConfig config;
    config.SizePixels = static_cast<float>(18 + i);
    loaded.imguiBody[static_cast<std::size_t>(i)] = sansPath.empty()
        ? io.Fonts->AddFontDefault(&config)
        : io.Fonts->AddFontFromFileTTF(sansPath.c_str(), config.SizePixels);
  }
  if (!monoPath.empty()) { io.Fonts->AddFontFromFileTTF(monoPath.c_str(), 18.0f); }
  std::printf("fonts: %s | %s | %s\n", sansPath.c_str(), boldPath.c_str(), monoPath.c_str());
  return loaded;
}

void ReloadImGuiFonts() {
  // rlImGui 6.0 has no rlImGuiReloadFonts() entry point. Building the atlas here
  // is the supported equivalent; its raylib backend uploads the texture on the
  // first frame, including every TTF size added above.
  ImGui::GetIO().Fonts->Build();
}

void SelectImGuiFont(const FontResources& fonts, int fontStep, bool largeText) {
  const int clamped = fontStep < 0 ? 0 : (fontStep > 3 ? 3 : fontStep);
  ImFont* selected = fonts.imguiBody[static_cast<std::size_t>(clamped + (largeText ? 2 : 0))];
  if (selected != nullptr) { ImGui::GetIO().FontDefault = selected; }
}

Texture2D LoadTitleTexture(const std::filesystem::path& assets) {
  const char* appDirectory = GetApplicationDirectory();
  const std::filesystem::path appDir =
      appDirectory != nullptr && *appDirectory != '\0'
          ? std::filesystem::path(appDirectory)
          : std::filesystem::current_path();
  const std::vector<std::filesystem::path> candidates = {
      assets / "art" / "title.jpg", appDir / "assets" / "art" / "title.jpg",
      std::filesystem::current_path() / "assets" / "art" / "title.jpg",
      std::filesystem::current_path() / "spaceship.png",
      assets / "art" / "spaceship.png", appDir / "spaceship.png"};
  for (const std::filesystem::path& candidate : candidates) {
    if (!std::filesystem::exists(candidate)) { continue; }
    Texture2D texture = LoadTexture(candidate.string().c_str());
    if (texture.id != 0) {
      SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
      std::printf("title artwork: %s\n", candidate.string().c_str());
      return texture;
    }
    std::fprintf(stderr, "spaceship: title artwork exists but could not be decoded/uploaded: %s\n",
                 candidate.string().c_str());
  }
  std::fprintf(stderr, "spaceship: no usable title artwork\n");
  return Texture2D{};
}

Texture2D LoadSetupTexture(const std::filesystem::path& assets) {
  const std::filesystem::path path = assets / "art" / "setupscreen.png";
  if (!std::filesystem::exists(path)) { return Texture2D{}; }
  Texture2D texture = LoadTexture(path.string().c_str());
  if (texture.id != 0) { SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR); }
  return texture;
}

Texture2D LoadSplashTexture(const std::filesystem::path& assets) {
  const std::filesystem::path path = assets / "art" / "splash.png";
  if (!std::filesystem::exists(path)) { return Texture2D{}; }
  Texture2D texture = LoadTexture(path.string().c_str());
  if (texture.id != 0) { SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR); }
  return texture;
}

// The calibrated default needs the shader. Only an identity transform can
// bypass the render target. The shader and target cover the complete frame
// (including ImGui, title art and popups), rather than changing the theme.
class DisplayAdjustment {
 public:
  void Init(int width, int height) {
    static const char* fragment = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float displayGamma;
uniform float displayBrightness;
uniform float uColorShift;
uniform float uGlow;
uniform float uLensFlare;
uniform sampler2D uFlareCoreTex;
uniform sampler2D uFlareMidTex;
uniform sampler2D uFlareWideTex;
uniform vec2 uResolution;
uniform float uTime;
uniform vec2 uWaveDir;
uniform float uIntensity;
uniform float uOptionScale;
uniform vec3 uWarmTint;
out vec4 finalColor;
void main() {
  vec2 uv = fragTexCoord;
  vec4 center = texture(texture0, uv);
  vec3 rgb = center.rgb * colDiffuse.rgb * fragColor.rgb;
  if (uOptionScale > 0.001 && uIntensity > 0.001) {
    vec2 res = max(uResolution, vec2(1.0));
    vec2 texel = 1.0 / res;
    vec2 dir = normalize(uWaveDir);
    // RenderTexture Y runs upward; flip Y in spatial projection so screen-down
    // ship bearings match visual screen coordinates.
    vec2 screenPos = vec2(uv.x, 1.0 - uv.y) * res;
    vec2 centered = (screenPos - 0.5 * res) / min(res.x, res.y);
    float proj = dot(centered, dir);
    float phase = proj * 2.25 - uTime * 0.42 + 0.16 * sin(proj * 1.1 + uTime * 0.13);
    float crest = pow(0.5 + 0.5 * cos(phase), 3.5);

    // 0.5 to 1.0 px sub-pixel R/B shift relative to anchored Green along crest
    // (scalable by player preference, safely capped so small text remains sharp).
    float shiftPx = clamp(mix(0.45, 0.90, clamp(uIntensity, 0.0, 1.0)) * uOptionScale, 0.0, 1.35) * crest;
    vec2 shiftUV = vec2(dir.x, -dir.y) * (shiftPx * texel);
    float r = texture(texture0, clamp(uv + shiftUV, vec2(0.0), vec2(1.0))).r;
    float b = texture(texture0, clamp(uv - shiftUV, vec2(0.0), vec2(1.0))).b;
    rgb = vec3(r, center.g, b) * colDiffuse.rgb * fragColor.rgb;

    // Subtle milky grey/amber black-level lift & contrast dip on sensor saturation.
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 milkyFloor = mix(vec3(0.26, 0.24, 0.21), uWarmTint, 0.42);
    float dip = crest * clamp(uIntensity * uOptionScale, 0.0, 1.5) * 0.055;
    rgb = mix(rgb, milkyFloor, dip * (1.0 - 0.58 * luma));
    if (uGlow > 0.0) {
      vec3 bloom = vec3(0.0);
      float weights = 0.0;
      for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
          vec2 offset = vec2(x, y);
          float weight = exp(-dot(offset, offset) / 8.0);
          vec2 sampleUV = clamp(uv + offset * 2.0 * texel, vec2(0.0), vec2(1.0));
          vec2 samplePos = vec2(sampleUV.x, 1.0 - sampleUV.y) * res;
          float sampleProj = dot((samplePos - 0.5 * res) / min(res.x, res.y), dir);
          float samplePhase = sampleProj * 2.25 - uTime * 0.42
                              + 0.16 * sin(sampleProj * 1.1 + uTime * 0.13);
          float sampleCrest = pow(0.5 + 0.5 * cos(samplePhase), 3.5);
          float sampleShift = clamp(mix(0.45, 0.90, clamp(uIntensity, 0.0, 1.0))
                                    * uOptionScale, 0.0, 1.35) * sampleCrest;
          vec2 sampleShiftUV = vec2(dir.x, -dir.y) * sampleShift * texel;
          vec3 original = texture(texture0, sampleUV).rgb;
          vec3 shifted = vec3(
              texture(texture0, clamp(sampleUV + sampleShiftUV, vec2(0.0), vec2(1.0))).r,
              original.g,
              texture(texture0, clamp(sampleUV - sampleShiftUV, vec2(0.0), vec2(1.0))).b);
          bloom += max(shifted - original, vec3(0.0)) * weight;
          weights += weight;
        }
      }
      rgb += bloom * colDiffuse.rgb * fragColor.rgb * (uGlow / weights);
    }
    if (uLensFlare > 0.001) {
      vec3 fCore = texture(uFlareCoreTex, uv).rgb;
      vec3 fMid = texture(uFlareMidTex, uv).rgb;
      vec3 fWide = texture(uFlareWideTex, uv).rgb;
      vec3 fGhost = texture(uFlareWideTex, vec2(1.0 - uv.x, uv.y)).rgb;
      float corePeak = max(fCore.r, max(fCore.g, fCore.b));
      float widePeak = max(fWide.r, max(fWide.g, fWide.b));
      vec3 hotCore = mix(fCore, vec3(1.0, 0.97, 0.90) * corePeak, 0.62);
      vec3 wideAnam = mix(fWide, vec3(0.12, 0.58, 1.00) * widePeak, 0.28);
      vec3 streak = hotCore * 0.85 + fMid * 1.55 + wideAnam * 3.30
                    + fGhost * vec3(0.11, 0.26, 0.48);
      vec3 flare = max(streak * uLensFlare, vec3(0.0)) * colDiffuse.rgb * fragColor.rgb;
      vec3 base = clamp(rgb, 0.0, 1.0);
      vec3 exposed = vec3(1.0) - (vec3(1.0) - base) * exp(-flare * 0.85);
      float fPeak = max(flare.r, max(flare.g, flare.b));
      if (fPeak > 0.0004) {
        vec3 fChroma = flare / fPeak;
        float coreHot = clamp(corePeak * uLensFlare * 0.72, 0.0, 0.68);
        vec3 beamTint = mix(mix(fChroma, vec3(0.86, 0.96, 1.0), 0.14),
                            vec3(1.0, 0.99, 0.96), coreHot);
        float daylight = smoothstep(0.54, 0.94, luma);
        float veil = (1.0 - exp(-fPeak * 0.88)) * daylight * 0.52;
        rgb = mix(exposed, beamTint, veil);
      } else {
        rgb = exposed;
      }
    }
  }
  float angle = radians(uColorShift);
  vec3 axis = normalize(vec3(1.0));
  vec3 shiftedRgb = rgb * cos(angle) + cross(axis, rgb) * sin(angle)
                    + axis * dot(axis, rgb) * (1.0 - cos(angle));
  rgb = mix(rgb, shiftedRgb, center.a);
  vec3 calibrated = pow(clamp(rgb, 0.0, 1.0), vec3(displayGamma)) * displayBrightness;
  finalColor = vec4(clamp(calibrated, 0.0, 1.0), 1.0);
}
)GLSL";
    static const char* extractFragment = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uResolution;
uniform float uTime;
uniform vec2 uWaveDir;
uniform float uIntensity;
uniform float uOptionScale;
uniform float uGlow;
uniform vec3 uWarmTint;
uniform float uFlarePack;
out vec4 finalColor;
float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
  vec2 uv = fragTexCoord;
  vec2 res = max(uResolution, vec2(1.0));
  vec2 texel = 1.0 / res;
  float dirLen = length(uWaveDir);
  vec2 dir = dirLen > 0.0001 ? (uWaveDir / dirLen) : vec2(0.76, 0.65);
  vec2 screenPos = vec2(uv.x, 1.0 - uv.y) * res;
  vec2 centered = (screenPos - 0.5 * res) / min(res.x, res.y);
  float proj = dot(centered, dir);
  float phase = proj * 2.25 - uTime * 0.42 + 0.16 * sin(proj * 1.1 + uTime * 0.13);
  float wave01 = clamp(0.5 + 0.5 * cos(phase), 0.0, 1.0);
  float crest = pow(wave01, 3.5);
  float glint = pow(wave01, 8.0);
  float baseShift = clamp(mix(0.45, 0.90, clamp(uIntensity, 0.0, 1.0)) * uOptionScale, 0.0, 1.35);
  float shiftPx = baseShift * crest;
  vec2 shiftUV = vec2(dir.x, -dir.y) * (shiftPx * texel);

  vec2 uv0 = clamp(uv + vec2(-0.35, -0.35) * texel, vec2(0.0), vec2(1.0));
  vec3 c0 = texture(texture0, uv0).rgb;
  vec3 c0P = texture(texture0, clamp(uv0 + shiftUV, vec2(0.0), vec2(1.0))).rgb;
  vec3 c0M = texture(texture0, clamp(uv0 - shiftUV, vec2(0.0), vec2(1.0))).rgb;
  vec3 sh0 = max(vec3(c0P.r, c0.g, c0M.b) - c0, vec3(0.0));

  vec2 uv1 = clamp(uv + vec2(0.65, 0.65) * texel, vec2(0.0), vec2(1.0));
  vec3 c1 = texture(texture0, uv1).rgb;
  vec3 c1P = texture(texture0, clamp(uv1 + shiftUV, vec2(0.0), vec2(1.0))).rgb;
  vec3 c1M = texture(texture0, clamp(uv1 - shiftUV, vec2(0.0), vec2(1.0))).rgb;
  vec3 sh1 = max(vec3(c1P.r, c1.g, c1M.b) - c1, vec3(0.0));

  vec3 shimmer = mix(0.5 * (sh0 + sh1), max(sh0, sh1), 0.60);
  float shEnergy = max(shimmer.r, shimmer.b) + 0.35 * min(shimmer.r, shimmer.b);
  float shKnee = smoothstep(0.024, 0.25, shEnergy);
  shKnee *= sqrt(shKnee) * smoothstep(0.0, 0.18, crest);
  if (shKnee <= 0.0001) {
    finalColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  float lC = Luma(c0);
  float lL1 = Luma(texture(texture0, clamp(uv + vec2(-2.0 * texel.x, 0.0), vec2(0.0), vec2(1.0))).rgb);
  float lR1 = Luma(texture(texture0, clamp(uv + vec2( 2.0 * texel.x, 0.0), vec2(0.0), vec2(1.0))).rgb);
  float lU1 = Luma(texture(texture0, clamp(uv + vec2(0.0,  2.0 * texel.y), vec2(0.0), vec2(1.0))).rgb);
  float lD1 = Luma(texture(texture0, clamp(uv + vec2(0.0, -2.0 * texel.y), vec2(0.0), vec2(1.0))).rgb);
  float lL2 = Luma(texture(texture0, clamp(uv + vec2(-5.0 * texel.x, 0.0), vec2(0.0), vec2(1.0))).rgb);
  float lR2 = Luma(texture(texture0, clamp(uv + vec2( 5.0 * texel.x, 0.0), vec2(0.0), vec2(1.0))).rgb);
  float lL3 = Luma(texture(texture0, clamp(uv + vec2(-9.0 * texel.x, 0.0), vec2(0.0), vec2(1.0))).rgb);
  float lR3 = Luma(texture(texture0, clamp(uv + vec2( 9.0 * texel.x, 0.0), vec2(0.0), vec2(1.0))).rgb);
  float lU2 = Luma(texture(texture0, clamp(uv + vec2(0.0,  5.0 * texel.y), vec2(0.0), vec2(1.0))).rgb);
  float lD2 = Luma(texture(texture0, clamp(uv + vec2(0.0, -5.0 * texel.y), vec2(0.0), vec2(1.0))).rgb);

  float surround = (lL1 + lR1 + lU1 + lD1 + lL2 + lR2 + lU2 + lD2) * 0.125;
  float outerSpan = max(abs(lL2 - lR2), abs(lU2 - lD2));
  float darkSpeck = smoothstep(0.05, 0.20, surround - lC) * (1.0 - smoothstep(0.04, 0.16, outerSpan));
  float chatL = abs(lL1 - lC) + abs(lL2 - lL1) + abs(lL3 - lL2);
  float chatR = abs(lR1 - lC) + abs(lR2 - lR1) + abs(lR3 - lR2);
  float textRun = smoothstep(0.14, 0.44, min(chatL, chatR)) * smoothstep(0.28, 0.78, max(chatL, chatR));
  float cleanMask = (1.0 - 0.94 * darkSpeck) * (1.0 - 0.90 * textRun);

  vec3 hiCol = max(max(c0, c1), max(c0P, c0M));
  float hiLuma = Luma(hiCol);
  float peakL = max(lC, Luma(c1));
  float starHit = smoothstep(0.042, 0.25, peakL - surround) * (0.72 + 0.88 * hiLuma);
  float gx = abs(lR1 - lL1);
  float gy = abs(lD1 - lU1);
  float specHit = smoothstep(0.052, 0.26, sqrt(max(0.0, gx * gy))) * smoothstep(0.24, 0.86, hiLuma);
  float edgeHit = smoothstep(0.085, 0.30, length(vec2(gx, gy))) * smoothstep(0.28, 0.90, hiLuma);

  float waveGain = clamp(uIntensity * uOptionScale, 0.0, 2.0)
                   * (0.85 + 0.25 * clamp(uGlow, 0.0, 2.0));
  float energy = shKnee * cleanMask
                 * (starHit * (2.90 + 4.10 * glint)
                    + specHit * (1.35 + 2.65 * glint)
                    + edgeHit * (0.28 + 0.65 * glint))
                 * waveGain;

  vec3 coatCool = vec3(0.10, 0.56, 1.00);
  vec3 coatWarm = mix(vec3(1.00, 0.64, 0.22), uWarmTint, 0.30);
  float redShare = shimmer.r / max(shimmer.r + shimmer.b, 0.0001);
  float warmFactor = clamp(uWarmTint.r - uWarmTint.b + 0.35, 0.0, 1.0);
  float warmMix = clamp(0.06 + 0.50 * redShare * (0.35 + 0.65 * warmFactor), 0.0, 0.64);
  vec3 coat = mix(coatCool, coatWarm, warmMix);
  float srcMax = max(hiCol.r, max(hiCol.g, hiCol.b));
  float srcMin = min(hiCol.r, min(hiCol.g, hiCol.b));
  float srcSat = clamp((srcMax - srcMin) / max(srcMax, 0.1), 0.0, 1.0);
  vec3 col = mix(coat, hiCol / max(srcMax, 0.15), 0.26 * srcSat);
  finalColor = vec4(clamp(col * energy * uFlarePack, vec3(0.0), vec3(10.0)), 1.0);
}
)GLSL";
    static const char* blurFragment = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uTexel;
uniform float uStride;
uniform float uDispersion;
uniform float uVertTap;
uniform float uFalloff;
out vec4 finalColor;
void main() {
  vec2 uv = fragTexCoord;
  vec3 acc = vec3(0.0);
  float wSum = 0.0;
  for (int i = -8; i <= 8; ++i) {
    float fi = float(i);
    float w = exp(-0.042 * fi * fi - abs(fi) * uFalloff);
    float dx = fi * uStride * uTexel.x;
    float r = texture(texture0, clamp(uv + vec2(dx * (1.0 - uDispersion), 0.0), vec2(0.0), vec2(1.0))).r;
    float g = texture(texture0, clamp(uv + vec2(dx, 0.0), vec2(0.0), vec2(1.0))).g;
    float b = texture(texture0, clamp(uv + vec2(dx * (1.0 + uDispersion), 0.0), vec2(0.0), vec2(1.0))).b;
    vec3 s = vec3(r, g, b);
    if (uVertTap > 0.001 && abs(i) <= 2) {
      float dy = uVertTap * uTexel.y;
      vec3 su = texture(texture0, clamp(uv + vec2(dx,  dy), vec2(0.0), vec2(1.0))).rgb;
      vec3 sd = texture(texture0, clamp(uv + vec2(dx, -dy), vec2(0.0), vec2(1.0))).rgb;
      s = s * 0.68 + (su + sd) * 0.16;
    }
    acc += s * w;
    wSum += w;
  }
  finalColor = vec4(acc / max(wSum, 0.0001), 1.0);
}
)GLSL";
    shader_ = LoadShaderFromMemory(nullptr, fragment);
    colorShiftLoc_ = GetShaderLocation(shader_, "uColorShift");
    glowLoc_ = GetShaderLocation(shader_, "uGlow");
    lensFlareLoc_ = GetShaderLocation(shader_, "uLensFlare");
    flareCoreLoc_ = GetShaderLocation(shader_, "uFlareCoreTex");
    flareMidLoc_ = GetShaderLocation(shader_, "uFlareMidTex");
    flareWideLoc_ = GetShaderLocation(shader_, "uFlareWideTex");
    gammaLoc_ = GetShaderLocation(shader_, "displayGamma");
    brightnessLoc_ = GetShaderLocation(shader_, "displayBrightness");
    resLoc_ = GetShaderLocation(shader_, "uResolution");
    timeLoc_ = GetShaderLocation(shader_, "uTime");
    waveDirLoc_ = GetShaderLocation(shader_, "uWaveDir");
    intensityLoc_ = GetShaderLocation(shader_, "uIntensity");
    optionScaleLoc_ = GetShaderLocation(shader_, "uOptionScale");
    warmTintLoc_ = GetShaderLocation(shader_, "uWarmTint");
    shaderReady_ = IsShaderValid(shader_) && gammaLoc_ >= 0 && brightnessLoc_ >= 0;
    if (!shaderReady_) {
      std::fprintf(stderr, "spaceship: display adjustment shader unavailable; using unadjusted display\n");
      return;
    }
    extractShader_ = LoadShaderFromMemory(nullptr, extractFragment);
    extResLoc_ = GetShaderLocation(extractShader_, "uResolution");
    extTimeLoc_ = GetShaderLocation(extractShader_, "uTime");
    extWaveDirLoc_ = GetShaderLocation(extractShader_, "uWaveDir");
    extIntensityLoc_ = GetShaderLocation(extractShader_, "uIntensity");
    extOptionScaleLoc_ = GetShaderLocation(extractShader_, "uOptionScale");
    extGlowLoc_ = GetShaderLocation(extractShader_, "uGlow");
    extWarmTintLoc_ = GetShaderLocation(extractShader_, "uWarmTint");
    extPackLoc_ = GetShaderLocation(extractShader_, "uFlarePack");
    blurShader_ = LoadShaderFromMemory(nullptr, blurFragment);
    blurTexelLoc_ = GetShaderLocation(blurShader_, "uTexel");
    blurStrideLoc_ = GetShaderLocation(blurShader_, "uStride");
    blurDispLoc_ = GetShaderLocation(blurShader_, "uDispersion");
    blurVertLoc_ = GetShaderLocation(blurShader_, "uVertTap");
    blurFalloffLoc_ = GetShaderLocation(blurShader_, "uFalloff");
    flareShadersReady_ = IsShaderValid(extractShader_) && IsShaderValid(blurShader_) &&
                         lensFlareLoc_ >= 0 && flareCoreLoc_ >= 0 &&
                         flareMidLoc_ >= 0 && flareWideLoc_ >= 0;
    Resize(width, height);
  }
  void Resize(int width, int height) {
    if (!shaderReady_ || width < 1 || height < 1) return;
    if (width == width_ && height == height_) return;
    if (target_.id != 0) UnloadRenderTexture(target_);
    ReleaseFlareTargets();
    target_ = LoadRenderTexture(width, height);
    width_ = width; height_ = height;
    if (!IsRenderTextureValid(target_)) {
      std::fprintf(stderr, "spaceship: display adjustment render target unavailable\n");
    } else {
      SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(target_.texture, TEXTURE_WRAP_CLAMP);
      if (flareShadersReady_) {
        const int fw = std::max(1, width / 2);
        const int fh = std::max(1, height / 2);
        bool hdr0 = false, hdr1 = false, hdr2 = false;
        flareCore_ = CreateFlareTarget(fw, fh, &hdr0);
        flareMid_ = CreateFlareTarget(fw, fh, &hdr1);
        flareWide_ = CreateFlareTarget(fw, fh, &hdr2);
        if (IsFlareTargetValid(flareCore_) && IsFlareTargetValid(flareMid_) &&
            IsFlareTargetValid(flareWide_)) {
          const bool allHdr = hdr0 && hdr1 && hdr2;
          flarePack_ = allHdr ? 1.0f : 0.25f;
          flareUnpack_ = allHdr ? 1.0f : 4.0f;
          flareReady_ = true;
        } else {
          ReleaseFlareTargets();
        }
      }
    }
  }
  bool Ready() const { return shaderReady_ && IsRenderTextureValid(target_); }
  void Begin() const { BeginTextureMode(target_); }
  void End(double gamma, double brightness, float waveTime, float waveDirX, float waveDirY,
           float intensity, float optionScale, float warmBias, float colorShift, float glow,
           float lensFlare) const {
    EndTextureMode();
    const float g = static_cast<float>(ship::EffectiveDisplayGamma(gamma));
    const float b = static_cast<float>(ship::DisplayBrightness(brightness));
    const float res[2] = {static_cast<float>(width_), static_cast<float>(height_)};
    const float dir[2] = {waveDirX, waveDirY};
    const float tint[3] = {
        0.64f + (0.88f - 0.64f) * warmBias,
        0.58f + (0.68f - 0.58f) * warmBias,
        0.82f + (0.42f - 0.82f) * warmBias,
    };
    const bool runFlare = flareReady_ && lensFlare > 0.001f &&
                          optionScale > 0.001f && intensity > 0.001f;
    if (runFlare) {
      if (extResLoc_ >= 0) SetShaderValue(extractShader_, extResLoc_, res, SHADER_UNIFORM_VEC2);
      if (extTimeLoc_ >= 0) SetShaderValue(extractShader_, extTimeLoc_, &waveTime, SHADER_UNIFORM_FLOAT);
      if (extWaveDirLoc_ >= 0) SetShaderValue(extractShader_, extWaveDirLoc_, dir, SHADER_UNIFORM_VEC2);
      if (extIntensityLoc_ >= 0) SetShaderValue(extractShader_, extIntensityLoc_, &intensity, SHADER_UNIFORM_FLOAT);
      if (extOptionScaleLoc_ >= 0) SetShaderValue(extractShader_, extOptionScaleLoc_, &optionScale, SHADER_UNIFORM_FLOAT);
      if (extGlowLoc_ >= 0) SetShaderValue(extractShader_, extGlowLoc_, &glow, SHADER_UNIFORM_FLOAT);
      if (extWarmTintLoc_ >= 0) SetShaderValue(extractShader_, extWarmTintLoc_, tint, SHADER_UNIFORM_VEC3);
      if (extPackLoc_ >= 0) SetShaderValue(extractShader_, extPackLoc_, &flarePack_, SHADER_UNIFORM_FLOAT);
      const float fTexel[2] = {
          1.0f / static_cast<float>(flareCore_.texture.width),
          1.0f / static_cast<float>(flareCore_.texture.height),
      };
      if (blurTexelLoc_ >= 0) SetShaderValue(blurShader_, blurTexelLoc_, fTexel, SHADER_UNIFORM_VEC2);
      auto blitPass = [](const RenderTexture2D& dst, const Texture2D& src, const Shader& sh) {
        BeginTextureMode(dst);
        BeginShaderMode(sh);
        DrawTexturePro(
            src,
            Rectangle{0, 0, static_cast<float>(src.width), -static_cast<float>(src.height)},
            Rectangle{0, 0, static_cast<float>(dst.texture.width), static_cast<float>(dst.texture.height)},
            Vector2{0, 0}, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();
      };
      auto runBlur = [&](const RenderTexture2D& dst, const Texture2D& src,
                         float stride, float disp, float vert, float falloff) {
        if (blurStrideLoc_ >= 0) SetShaderValue(blurShader_, blurStrideLoc_, &stride, SHADER_UNIFORM_FLOAT);
        if (blurDispLoc_ >= 0) SetShaderValue(blurShader_, blurDispLoc_, &disp, SHADER_UNIFORM_FLOAT);
        if (blurVertLoc_ >= 0) SetShaderValue(blurShader_, blurVertLoc_, &vert, SHADER_UNIFORM_FLOAT);
        if (blurFalloffLoc_ >= 0) SetShaderValue(blurShader_, blurFalloffLoc_, &falloff, SHADER_UNIFORM_FLOAT);
        blitPass(dst, src, blurShader_);
      };
      rlDisableColorBlend();
      blitPass(flareWide_, target_.texture, extractShader_);
      runBlur(flareCore_, flareWide_.texture, 1.0f, 0.04f, 0.65f, 0.14f);
      runBlur(flareMid_, flareCore_.texture, 4.2f, 0.09f, 0.18f, 0.11f);
      runBlur(flareWide_, flareMid_.texture, 18.0f, 0.14f, 0.00f, 0.09f);
      rlEnableColorBlend();
    }
    const float flareAmount = runFlare ? (lensFlare * flareUnpack_) : 0.0f;
    SetShaderValue(shader_, colorShiftLoc_, &colorShift, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, glowLoc_, &glow, SHADER_UNIFORM_FLOAT);
    if (lensFlareLoc_ >= 0) SetShaderValue(shader_, lensFlareLoc_, &flareAmount, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, gammaLoc_, &g, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, brightnessLoc_, &b, SHADER_UNIFORM_FLOAT);
    if (resLoc_ >= 0) SetShaderValue(shader_, resLoc_, res, SHADER_UNIFORM_VEC2);
    if (timeLoc_ >= 0) SetShaderValue(shader_, timeLoc_, &waveTime, SHADER_UNIFORM_FLOAT);
    if (waveDirLoc_ >= 0) SetShaderValue(shader_, waveDirLoc_, dir, SHADER_UNIFORM_VEC2);
    if (intensityLoc_ >= 0) SetShaderValue(shader_, intensityLoc_, &intensity, SHADER_UNIFORM_FLOAT);
    if (optionScaleLoc_ >= 0) SetShaderValue(shader_, optionScaleLoc_, &optionScale, SHADER_UNIFORM_FLOAT);
    if (warmTintLoc_ >= 0) SetShaderValue(shader_, warmTintLoc_, tint, SHADER_UNIFORM_VEC3);
    BeginShaderMode(shader_);
    if (runFlare) {
      SetShaderValueTexture(shader_, flareCoreLoc_, flareCore_.texture);
      SetShaderValueTexture(shader_, flareMidLoc_, flareMid_.texture);
      SetShaderValueTexture(shader_, flareWideLoc_, flareWide_.texture);
    }
    DrawTexturePro(target_.texture,
                   Rectangle{0, 0, static_cast<float>(width_), -static_cast<float>(height_)},
                   Rectangle{0, 0, static_cast<float>(width_), static_cast<float>(height_)},
                   Vector2{0, 0}, 0, WHITE);
    EndShaderMode();
  }
  void Shutdown() {
    ReleaseFlareTargets();
    if (target_.id != 0) UnloadRenderTexture(target_);
    if (extractShader_.locs != nullptr) UnloadShader(extractShader_);
    if (blurShader_.locs != nullptr) UnloadShader(blurShader_);
    if (shader_.locs != nullptr) UnloadShader(shader_);
  }
 private:
  static bool IsFlareTargetValid(const RenderTexture2D& rt) {
    return rt.id > 0 && IsTextureValid(rt.texture);
  }
  static RenderTexture2D CreateFlareTarget(int width, int height, bool* isHdr) {
    auto makeWithFormat = [&](int pixelFormat) {
      RenderTexture2D rt{};
      rt.id = rlLoadFramebuffer();
      if (rt.id == 0) return rt;
      rlEnableFramebuffer(rt.id);
      rt.texture.id = rlLoadTexture(nullptr, width, height, pixelFormat, 1);
      rt.texture.width = width;
      rt.texture.height = height;
      rt.texture.format = pixelFormat;
      rt.texture.mipmaps = 1;
      if (rt.texture.id > 0) {
        rlFramebufferAttach(rt.id, rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                            RL_ATTACHMENT_TEXTURE2D, 0);
      }
      const bool ok = rt.texture.id > 0 && rlFramebufferComplete(rt.id);
      rlDisableFramebuffer();
      if (!ok) {
        UnloadRenderTexture(rt);
        return RenderTexture2D{};
      }
      SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
      return rt;
    };
    RenderTexture2D rt = makeWithFormat(PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    if (IsFlareTargetValid(rt)) {
      if (isHdr != nullptr) *isHdr = true;
      return rt;
    }
    if (isHdr != nullptr) *isHdr = false;
    return makeWithFormat(PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  }
  void ReleaseFlareTargets() {
    if (flareCore_.id != 0) { UnloadRenderTexture(flareCore_); flareCore_ = RenderTexture2D{}; }
    if (flareMid_.id != 0) { UnloadRenderTexture(flareMid_); flareMid_ = RenderTexture2D{}; }
    if (flareWide_.id != 0) { UnloadRenderTexture(flareWide_); flareWide_ = RenderTexture2D{}; }
    flareReady_ = false;
  }

  Shader shader_{};
  Shader extractShader_{};
  Shader blurShader_{};
  RenderTexture2D target_{};
  RenderTexture2D flareCore_{};
  RenderTexture2D flareMid_{};
  RenderTexture2D flareWide_{};
  int gammaLoc_ = -1, brightnessLoc_ = -1;
  int colorShiftLoc_ = -1, glowLoc_ = -1, lensFlareLoc_ = -1;
  int flareCoreLoc_ = -1, flareMidLoc_ = -1, flareWideLoc_ = -1;
  int resLoc_ = -1, timeLoc_ = -1, waveDirLoc_ = -1;
  int intensityLoc_ = -1, optionScaleLoc_ = -1, warmTintLoc_ = -1;
  int extResLoc_ = -1, extTimeLoc_ = -1, extWaveDirLoc_ = -1;
  int extIntensityLoc_ = -1, extOptionScaleLoc_ = -1, extGlowLoc_ = -1;
  int extWarmTintLoc_ = -1, extPackLoc_ = -1;
  int blurTexelLoc_ = -1, blurStrideLoc_ = -1, blurDispLoc_ = -1;
  int blurVertLoc_ = -1, blurFalloffLoc_ = -1;
  float flarePack_ = 1.0f, flareUnpack_ = 1.0f;
  int width_ = 0, height_ = 0;
  bool shaderReady_ = false;
  bool flareShadersReady_ = false;
  bool flareReady_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  bool voyageStarted = false;
  if (argc == 3 && std::string(argv[1]) == "--check-music-file") {
    // Decodes with the same raylib codecs as music streams; no sound device or
    // graphics context is required. Keep the argument relative to launch CWD.
    Wave wave = LoadWave(argv[2]);
    const bool decoded = wave.data != nullptr && wave.frameCount > 0;
    std::printf("music decode: %s (%u frames): %s\n", decoded ? "PASS" : "FAIL", wave.frameCount, argv[2]);
    if (wave.data != nullptr) UnloadWave(wave);
    return decoded ? 0 : 1;
  }

  // This CPU-only probe exercises the *linked raylib decoder*, not a separate
  // JPEG library or an existence check. Run it against the packaged executable
  // in CI before opening any window or initializing graphics/audio.
  std::error_code cwdError;
  const std::filesystem::path launchAssets = std::filesystem::current_path(cwdError) / "assets";
  const std::filesystem::path assets = FindAssetDir();
  if (argc == 2 && std::string(argv[1]) == "--check-title-art") {
    const auto path = assets / "art" / "title.jpg";
    Image image = LoadImage(path.string().c_str());
    const bool decoded = image.data != nullptr && image.width > 0 && image.height > 0;
    std::printf("title JPEG decode: %s (%d x %d): %s\n",
                decoded ? "PASS" : "FAIL", image.width, image.height, path.string().c_str());
    if (image.data != nullptr) { UnloadImage(image); }
    return decoded ? 0 : 1;
  }

  if (argc == 2 && std::string(argv[1]) == "--check-ship-art") {
    const auto path = assets / "art" / "ships" / "wren-exterior.png";
    Image image = LoadImage(path.string().c_str());
    const bool decoded = image.data != nullptr && image.width > 0 && image.height > 0;
    std::printf("Wren PNG decode: %s (%d x %d): %s\n",
                decoded ? "PASS" : "FAIL", image.width, image.height, path.string().c_str());
    if (image.data != nullptr) { UnloadImage(image); }
    return decoded ? 0 : 1;
  }

  // ---- catalogue ---------------------------------------------------------

  ship::Catalog catalog;
  try {
    catalog = ship::Catalog::LoadFromAssets(assets);
  } catch (const ship::CatalogError& error) {
    std::fprintf(stderr, "spaceship: %s\n", error.what());
    return 1;
  }

  const std::vector<std::string> problems = catalog.Validate();
  std::printf("catalogue: %d regions, %d systems, %d bodies, %d ports\n",
              catalog.counts.regions, catalog.counts.systems,
              catalog.counts.bodies, catalog.counts.ports);
  for (const std::string& problem : problems) {
    std::fprintf(stderr, "catalogue problem: %s\n", problem.c_str());
  }

  // ---- the voyage --------------------------------------------------------
  ship::Simulation sim = ship::Simulation::NewVoyage(catalog);
  const char* appDir = GetApplicationDirectory();
  const std::filesystem::path saveDir = appDir != nullptr && *appDir != '\0'
      ? std::filesystem::path(appDir) / "runtime" / "saves"
      : std::filesystem::path("runtime") / "saves";
  std::string loadedPath, loadWarning;

  // ---- window ------------------------------------------------------------
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(kDefaultWidth, kDefaultHeight, "Spaceship Command");
  SetWindowMinSize(1180, 720);
  SetTargetFPS(60);
  SetExitKey(KEY_NULL);

  ship::Theme theme = ship::Theme::ById(sim.State().options.theme);
  theme.fontStep = sim.State().options.fontStep;   // Phase 9's reading size
  theme.largeText = sim.State().options.largeText;
  theme.hideCrewLabels = sim.State().options.hideCrewLabels;
  theme.reducedMotion = sim.State().reducedMotion;
  theme.stageCompact = (sim.State().shipClass == "wren" || sim.State().shipClass == "explorer-needle" || sim.State().shipClass == "explorer-manta" || sim.State().shipClass == "yamato" || sim.State().shipClass == "arcadia");

  // rlImGui owns the ImGui context, so add the four reading sizes between its
  // two init halves. The renderer builds/uploads the complete atlas afterward.
  rlImGuiBeginInitImGui();
  const FontResources fonts = LoadFonts(assets);
  rlImGuiEndInitImGui();
  ReloadImGuiFonts();
  SelectImGuiFont(fonts, sim.State().options.fontStep, sim.State().options.largeText);
  ship::ui::ApplyPalette(theme);

  Texture2D bridgeTexture = LoadTexture((assets / "art" / "rooms" / "bridge.png").string().c_str());
  SetTextureFilter(bridgeTexture, TEXTURE_FILTER_BILINEAR);
  Texture2D titleTexture = LoadTitleTexture(assets);
  Texture2D setupTexture = LoadSetupTexture(assets);
  Texture2D splashTexture = LoadSplashTexture(assets);
  Texture2D wrenExterior{};
  Image wrenImage{};
  const auto exteriorPath = assets / "art" / "ships" / "wren-exterior.png";
  if (std::filesystem::exists(exteriorPath)) {
    wrenImage = LoadImage(exteriorPath.string().c_str());
    if (wrenImage.data != nullptr && wrenImage.width > 0 && wrenImage.height > 0) {
      wrenExterior = LoadTextureFromImage(wrenImage);
      if (wrenExterior.id != 0) {
        SetTextureFilter(wrenExterior, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(wrenExterior, TEXTURE_WRAP_CLAMP);
      }
    }
  }
  if (wrenExterior.id == 0) {
    std::fprintf(stderr, "spaceship: Wren exterior unavailable; using deck plan\n");
  }
  std::map<std::string, Texture2D> cargoExteriors;
  for (const std::string kind : {"cargo-spine", "cargo-bulk", "explorer-needle", "explorer-manta", "yamato", "arcadia"}) {
    const auto path = assets / "art" / "ships" / (kind + "-exterior.png");
    Texture2D texture = LoadTexture(path.string().c_str());
    if (texture.id != 0) {
      SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
    }
    cargoExteriors[kind] = texture;
  }
  std::array<Texture2D, 6> tutorialTextures{};
  for (std::size_t i = 0; i < tutorialTextures.size(); ++i) {
    const auto path = assets / "art" / "tutorial" / ("spaceship0" + std::to_string(i + 1) + ".png");
    if (std::filesystem::exists(path)) {
      tutorialTextures[i] = LoadTexture(path.string().c_str());
      if (tutorialTextures[i].id != 0) { SetTextureFilter(tutorialTextures[i], TEXTURE_FILTER_BILINEAR); }
    }
    if (tutorialTextures[i].id == 0) { std::fprintf(stderr, "spaceship: tutorial card unavailable: %s\n", path.string().c_str()); }
  }
  // Crew pictures: every PNG in assets/art/crew, kept under its file name
  // without the extension. No folder, or an empty one, means no pictures, and
  // the quarters paint their figures from the sprite tables.
  std::map<std::string, Texture2D> crewTextures;
  {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(assets / "art" / "crew", ec), end;
         !ec && it != end; it.increment(ec)) {
      const std::filesystem::path& path = it->path();
      if (path.extension() != ".png") { continue; }
      Texture2D texture = LoadTexture(path.string().c_str());
      if (texture.id == 0) {
        std::fprintf(stderr, "spaceship: crew picture unavailable: %s\n", path.string().c_str());
        continue;
      }
      SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
      crewTextures[path.stem().string()] = texture;
    }
  }
  ship::ShellView shell;
  shell.SetSaveDir(saveDir.u8string());
  shell.SetFonts(fonts.body, fonts.bold, fonts.mono);
  shell.bridgeArt = &bridgeTexture;
  shell.SetTitleTexture(&titleTexture);
  shell.SetSetupTexture(&setupTexture);
  shell.SetSplashTexture(&splashTexture);
  shell.SetWrenExteriorTexture(&wrenExterior);
  if (wrenImage.data != nullptr) {
    shell.SetWrenExteriorImage(&wrenImage);
    UnloadImage(wrenImage);
    wrenImage = {};
  }
  for (std::size_t i = 0; i < tutorialTextures.size(); ++i) { shell.SetTutorialTexture(i, &tutorialTextures[i]); }
  for (auto& picture : crewTextures) { shell.SetCrewTexture(picture.first, &picture.second); }
  shell.Configure(static_cast<float>(GetScreenWidth()),
                  static_cast<float>(GetScreenHeight()), theme);

  DisplayAdjustment display;
  display.Init(GetScreenWidth(), GetScreenHeight());
  shell.SetDisplayAdjustmentAvailable(display.Ready());

  // Sound is optional: a machine without a device keeps a silent, working ship.
  audio::AudioDevice audio;
  const bool audioReady = audio.Start(assets.string(), launchAssets.string());
  audio.Configure(sim.State().options);
  std::printf("audio: %zu music file(s), %zu sound file(s)\n",
              audio.External().music.size(), audio.External().sfx.size());

  // Restore the voyage in memory, but always present the title screen — behind
  // the dev's splash card, which opens the window. A restored voyage can be
  // entered with RESUME, or replaced explicitly.
  shell.ShowSplash(voyageStarted);

  // Phase 9: the borderless window follows Options, whichever way it changes.
  bool fullscreen = sim.State().options.fullscreen;
  if (fullscreen != IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
    ToggleBorderlessWindowed();
  }

  ship::PointerOwnership leftPointer, rightPointer, middlePointer;
  ship::RadiationTelemetry smoothedWave = ship::ComputeRadiationTelemetry(sim);
  float waveTime = 0.0f;
  while (!WindowShouldClose() && !shell.QuitRequested()) {
    if (IsKeyPressed(KEY_ESCAPE)) { shell.Escape(sim); }
    if (IsKeyPressed(KEY_LEFT)) { shell.PageTutorial(-1, GetTime() * 1000.0); }
    if (IsKeyPressed(KEY_RIGHT)) { shell.PageTutorial(1, GetTime() * 1000.0); }
    display.Resize(GetScreenWidth(), GetScreenHeight());
    shell.SetDisplayAdjustmentAvailable(display.Ready());
    BeginDrawing();
    // rlImGui's event processing and frame setup happen inside the active
    // raylib drawing frame, matching its documented render-loop contract.
    rlImGuiBegin();

    // Dear ImGui owns a complete press/release gesture. A close button may
    // remove its window before release, but that release must not hit the ship.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      leftPointer.Press(ImGui::GetIO().WantCaptureMouse);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      rightPointer.Press(ImGui::GetIO().WantCaptureMouse);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
      middlePointer.Press(ImGui::GetIO().WantCaptureMouse);
    }
    const Vector2 pointer = GetMousePosition();
    shell.Move(pointer.x, pointer.y);
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !leftPointer.UiOwned()) {
      const Vector2 delta = GetMouseDelta();
      shell.Drag(sim, pointer.x, pointer.y, delta.x, delta.y, true,
                 IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !middlePointer.UiOwned()) {
      const Vector2 delta = GetMouseDelta();
      shell.OrbitDrag(sim, pointer.x, pointer.y, delta.x, delta.y, true,
                      IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE));
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) { shell.Scroll(sim, pointer.x, pointer.y, wheel); }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      const Vector2 mouse = GetMousePosition();
      if (!leftPointer.Release()) {
        shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Left,
                    static_cast<double>(GetTime()) * 1000.0);
      }
    } else if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
      const Vector2 mouse = GetMousePosition();
      if (!rightPointer.Release()) {
        shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Right,
                    static_cast<double>(GetTime()) * 1000.0);
      }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
      const Vector2 mouse = GetMousePosition();
      if (!middlePointer.Release()) {
        shell.OrbitDrag(sim, mouse.x, mouse.y, 0.0f, 0.0f, false, false);
      }
    }

    // Apply settings from either the title sheet or the in-game Options panel
    // before rendering this frame, so palettes and type sizes change at once.
    const auto cargoExterior = cargoExteriors.find(sim.State().shipClass);
    if (cargoExterior != cargoExteriors.end()) {
      shell.SetWrenExteriorTexture(&cargoExterior->second,
          "art/ships/" + cargoExterior->first + "-exterior.png");
    } else {
      shell.SetWrenExteriorTexture(&wrenExterior);
    }
    ship::Theme nextTheme = ship::Theme::ById(sim.State().options.theme);
    nextTheme.fontStep = sim.State().options.fontStep;
    nextTheme.largeText = sim.State().options.largeText;
    nextTheme.hideCrewLabels = sim.State().options.hideCrewLabels;
    nextTheme.reducedMotion = sim.State().reducedMotion;
    nextTheme.stageCompact = (sim.State().shipClass == "wren" || sim.State().shipClass == "explorer-needle" || sim.State().shipClass == "explorer-manta" || sim.State().shipClass == "yamato" || sim.State().shipClass == "arcadia");
    const bool themeChanged = nextTheme.id != theme.id ||
                              nextTheme.fontStep != theme.fontStep ||
                              nextTheme.largeText != theme.largeText ||
                              nextTheme.hideCrewLabels != theme.hideCrewLabels ||
                              nextTheme.reducedMotion != theme.reducedMotion ||
                              nextTheme.stageCompact != theme.stageCompact;
    const bool resized = IsWindowResized();
    if (themeChanged || resized) {
      theme = nextTheme;
      shell.Configure(static_cast<float>(GetScreenWidth()),
                      static_cast<float>(GetScreenHeight()), theme);
      ship::ui::ApplyPalette(theme);
    }
    SelectImGuiFont(fonts, sim.State().options.fontStep, sim.State().options.largeText);

    if (sim.State().options.fullscreen != fullscreen) {
      fullscreen = sim.State().options.fullscreen;
      ToggleBorderlessWindowed();
    }

    audio.Configure(sim.State().options);
    audio.Tick(sim);
    shell.SetAudioStatus(audio.Status());

    const ship::RadiationTelemetry targetWave = ship::ComputeRadiationTelemetry(sim);
    const float dt = std::clamp(GetFrameTime(), 0.0f, 0.1f);
    const float blend = 1.0f - std::exp(-dt * 2.5f);
    smoothedWave.waveDirX += (targetWave.waveDirX - smoothedWave.waveDirX) * blend;
    smoothedWave.waveDirY += (targetWave.waveDirY - smoothedWave.waveDirY) * blend;
    const float dirLen = std::hypot(smoothedWave.waveDirX, smoothedWave.waveDirY);
    if (dirLen > 1e-4f) {
      smoothedWave.waveDirX /= dirLen;
      smoothedWave.waveDirY /= dirLen;
    }
    smoothedWave.baseIntensity += (targetWave.baseIntensity - smoothedWave.baseIntensity) * blend;
    smoothedWave.warmBias += (targetWave.warmBias - smoothedWave.warmBias) * blend;
    if (!sim.State().reducedMotion) { waveTime += dt; }

    const float waveScale = sim.State().reducedMotion
                                ? 0.0f
                                : static_cast<float>(ship::RadiationWaveStrength(
                                      sim.State().options.radiationWave));
    const bool adjusted = display.Ready() &&
        (ship::EffectiveDisplayGamma(sim.State().options.displayGamma) != 1.0 ||
         ship::DisplayBrightness(sim.State().options.displayBrightness) != 1.0 ||
         waveScale > 0.001f || sim.State().options.colorShift != 0.0 ||
         sim.State().options.glow > 0.0 || sim.State().options.lensFlare > 0.0);
    if (adjusted) {
      display.Begin();
      // The offscreen texture uses logical screen pixels even on Retina
      // displays; rlImGui otherwise scissors as if it were the HiDPI window.
      ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }
    if (adjusted) {
      // Scene alpha carries colorshift coverage; ordinary UI draws source-over.
      rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                                RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
      BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    }
    shell.Draw(sim, static_cast<double>(GetTime()) * 1000.0);
    if (adjusted) { EndBlendMode(); }
    if (!shell.TitleActive()) voyageStarted = true;
    rlImGuiEnd();
    if (adjusted) {
      display.End(sim.State().options.displayGamma, sim.State().options.displayBrightness,
                  waveTime, smoothedWave.waveDirX, smoothedWave.waveDirY,
                  smoothedWave.baseIntensity, waveScale, smoothedWave.warmBias,
                  static_cast<float>(sim.State().options.colorShift),
                  static_cast<float>(sim.State().options.glow),
                  static_cast<float>(std::clamp(sim.State().options.lensFlare, 0.0, 2.0)));
    }
    EndDrawing();
  }

  if (voyageStarted) {
    std::string saved, error;
    if (!ship::SaveDated(sim, saveDir.u8string(), &saved, &error)) {
      SaveWarning("Autosave failed. Your earlier saves have not been changed.\n" + error);
    } else {
      std::printf("autosaved: %s\n", saved.c_str());
    }
  }
  audio.Shutdown();
  rlImGuiShutdown();
  display.Shutdown();
  shell.ReleaseGraphics();
  if (bridgeTexture.id != 0) UnloadTexture(bridgeTexture);
  if (titleTexture.id != 0) { UnloadTexture(titleTexture); }
  if (setupTexture.id != 0) { UnloadTexture(setupTexture); }
  if (splashTexture.id != 0) { UnloadTexture(splashTexture); }
  if (wrenExterior.id != 0) { UnloadTexture(wrenExterior); }
  for (auto& entry : cargoExteriors) { if (entry.second.id != 0) UnloadTexture(entry.second); }
  for (auto texture : tutorialTextures) { if (texture.id != 0) { UnloadTexture(texture); } }
  for (auto& picture : crewTextures) { UnloadTexture(picture.second); }
  if (fonts.body.texture.id != 0) { UnloadFont(fonts.body); }
  if (fonts.bold.texture.id != 0 && fonts.bold.texture.id != fonts.body.texture.id) {
    UnloadFont(fonts.bold);
  }
  if (fonts.mono.texture.id != 0 && fonts.mono.texture.id != fonts.body.texture.id &&
      fonts.mono.texture.id != fonts.bold.texture.id) {
    UnloadFont(fonts.mono);
  }
  CloseWindow();
  (void)audioReady;
  return 0;
}
