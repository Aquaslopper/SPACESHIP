// ============================================================================
//  main.cpp — the client.
//  ---------------------------------------------------------------------------
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
      for (int y = -4; y <= 4; y += 2) {
        for (int x = -4; x <= 4; x += 2) {
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
    shader_ = LoadShaderFromMemory(nullptr, fragment);
    colorShiftLoc_ = GetShaderLocation(shader_, "uColorShift");
    glowLoc_ = GetShaderLocation(shader_, "uGlow");
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
    static const char* flareFrag = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform float uFlareGain;
out vec4 finalColor;
void main() {
  vec2 p = fragTexCoord * 2.0 - 1.0;
  float ax = abs(p.x);
  float ay = abs(p.y);
  float edge = 1.0 - smoothstep(0.40, 0.98, max(ax, ay));
  vec2 cp = vec2(p.x, p.y);
  float r2 = dot(cp, cp);
  float core = exp(-r2 * 120.0) * 1.8;
  float innerGlow = exp(-r2 * 14.0)  * 0.35;
  float outerBloom = exp(-sqrt(r2) * 3.0) * 0.35;
  float yLine = exp(-ay * ay * 420.0) * 1.0;
  float yBloom = exp(-ay * ay * 70.0) * 0.35;
  float sR = exp(-pow(ax * 1.60, 1.10) * 2.2) * (yLine + yBloom * 0.40);
  float sG = exp(-pow(ax * 0.75, 1.10) * 1.2) * (yLine + yBloom * 0.80);
  float sB = exp(-pow(ax * 0.45, 1.10) * 0.9) * (yLine + yBloom * 1.00);
  vec3 bloom = vec3(innerGlow + outerBloom);
  vec3 streak = vec3(sR * 0.85, sG * 1.05, sB * 1.25);
  vec3 col = bloom + streak + vec3(1.0, 0.98, 0.92) * core;
  float gain = max(fragColor.a * uFlareGain * edge, 0.0);
  vec3 emit = vec3(1.0) - exp(-col * gain);
  float a = max(emit.r, max(emit.g, emit.b)) * 0.9;
  finalColor = vec4(emit, a);
}
)GLSL";
    flareShader_ = LoadShaderFromMemory(nullptr, flareFrag);
    flareGainLoc_ = GetShaderLocation(flareShader_, "uFlareGain");
    flareReady_ = IsShaderValid(flareShader_);
    Resize(width, height);
  }
  void Resize(int width, int height) {
    if (!shaderReady_ || width < 1 || height < 1) return;
    if (width == width_ && height == height_) return;
    if (target_.id != 0) UnloadRenderTexture(target_);
    target_ = LoadRenderTexture(width, height);
    width_ = width; height_ = height;
    if (!IsRenderTextureValid(target_)) {
      std::fprintf(stderr, "spaceship: display adjustment render target unavailable\n");
    } else {
      SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
      SetTextureWrap(target_.texture, TEXTURE_WRAP_CLAMP);
    }
  }
  bool Ready() const { return shaderReady_ && IsRenderTextureValid(target_); }
  void Begin() const { BeginTextureMode(target_); }
  static constexpr float kFlareTailPx = 110.0f;  // horizontal flare half-length in px

  void End(double gamma, double brightness, float waveTime, float waveDirX, float waveDirY,
           float intensity, float optionScale, float warmBias, float colorShift, float glow,
           float lensFlare, const std::vector<ship::FlareTarget>& targets) const {
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
    SetShaderValue(shader_, colorShiftLoc_, &colorShift, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, glowLoc_, &glow, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, gammaLoc_, &g, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, brightnessLoc_, &b, SHADER_UNIFORM_FLOAT);
    if (resLoc_ >= 0) SetShaderValue(shader_, resLoc_, res, SHADER_UNIFORM_VEC2);
    if (timeLoc_ >= 0) SetShaderValue(shader_, timeLoc_, &waveTime, SHADER_UNIFORM_FLOAT);
    if (waveDirLoc_ >= 0) SetShaderValue(shader_, waveDirLoc_, dir, SHADER_UNIFORM_VEC2);
    if (intensityLoc_ >= 0) SetShaderValue(shader_, intensityLoc_, &intensity, SHADER_UNIFORM_FLOAT);
    if (optionScaleLoc_ >= 0) SetShaderValue(shader_, optionScaleLoc_, &optionScale, SHADER_UNIFORM_FLOAT);
    if (warmTintLoc_ >= 0) SetShaderValue(shader_, warmTintLoc_, tint, SHADER_UNIFORM_VEC3);
    BeginShaderMode(shader_);
    DrawTexturePro(target_.texture,
                   Rectangle{0, 0, static_cast<float>(width_), -static_cast<float>(height_)},
                   Rectangle{0, 0, static_cast<float>(width_), static_cast<float>(height_)},
                   Vector2{0, 0}, 0, WHITE);
    EndShaderMode();
    if (flareReady_ && lensFlare > 0.001f && optionScale > 0.001f &&
        intensity > 0.001f && !targets.empty()) {
      DrawFlares(waveTime, waveDirX, waveDirY, intensity, optionScale, warmBias,
                 colorShift, b, lensFlare, targets);
    }
  }
  void Shutdown() {
    if (target_.id != 0) UnloadRenderTexture(target_);
    if (flareShader_.locs != nullptr) UnloadShader(flareShader_);
    if (shader_.locs != nullptr) UnloadShader(shader_);
  }
 private:
  void DrawFlares(float waveTime, float waveDirX, float waveDirY, float intensity,
                  float optionScale, float warmBias, float colorShift, float brightness,
                  float lensFlare, const std::vector<ship::FlareTarget>& targets) const {
    const float dLen = std::hypot(waveDirX, waveDirY);
    const float dx = dLen > 1e-4f ? waveDirX / dLen : 1.0f;
    const float dy = dLen > 1e-4f ? waveDirY / dLen : 0.0f;
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    const float minRes = std::max(1.0f, std::min(w, h));
    auto phaseAt = [&](float sx, float sy) {
      const float proj = ((sx - 0.5f * w) * dx + (sy - 0.5f * h) * dy) / minRes;
      return proj * 2.25f - waveTime * 0.42f + 0.16f * std::sin(proj * 1.1f + waveTime * 0.13f);
    };
    auto fadeAt = [](float phase) {
      const float c = std::clamp(0.5f + 0.5f * std::cos(phase), 0.0f, 1.0f);
      return std::pow(c, 12.0f);
    };

    occs_.clear();
    for (std::size_t i = 0; i < targets.size(); ++i) {
      const auto& t = targets[i];
      if (t.kind == ship::FlareTarget::Occlude) occs_.push_back({i, t.x0, t.y0, t.x1, t.y1});
    }
    
    auto occlusionFade = [&](std::size_t idx, float sx, float sy) {
      float fade = 1.0f;
      const float margin = 56.0f;
      for (const Occ& o : occs_) {
        if (o.idx <= idx) continue;
        const float dx = std::max(o.x0 - sx, sx - o.x1);
        const float dy = std::max(o.y0 - sy, sy - o.y1);
        const float dist = std::max(dx, dy);
        const float t01 = std::clamp(dist / margin, 0.0f, 1.0f);
        fade = std::min(fade, t01 * t01 * (3.0f - 2.0f * t01));
      }
      return fade;
    };

    spots_.clear();
    for (std::size_t i = 0; i < targets.size(); ++i) {
      const auto& t = targets[i];
      if (t.kind == ship::FlareTarget::Occlude) continue;
     const float borderT = std::clamp(std::min(std::min(t.x0, w - t.x0),
                                                std::min(t.y0, h - t.y0)) / 64.0f,
                                       0.0f, 1.0f);
      const float s = fadeAt(phaseAt(t.x0, t.y0)) * t.weight
                      * occlusionFade(i, t.x0, t.y0)
                      * borderT * borderT * (3.0f - 2.0f * borderT);
      if (s < 0.012f) continue;
      if (t.kind == ship::FlareTarget::PanelCorner) {
        bool merged = false;
        for (Spot& e : spots_) {
          if (e.kind == t.kind &&
              (t.x0 - e.x) * (t.x0 - e.x) + (t.y0 - e.y) * (t.y0 - e.y) < 4.0f) {
            if (s > e.strength) { e.strength = s; }
            merged = true;
            break;
          }
        }
        if (merged) continue;
      }
      const float sc = (t.kind == ship::FlareTarget::ShipPoint)
                           ? 1.15f
                           : (t.kind == ship::FlareTarget::StarPoint ? 1.05f : 0.92f);
      spots_.push_back({t.x0, t.y0, std::min(s, 1.0f), sc, t.kind});
    }
    if (spots_.empty()) return;

    const float wb = std::clamp(warmBias, 0.0f, 1.0f);
    float tr = 0.34f + 0.66f * wb * 0.72f;
    float tg = 0.78f - 0.14f * wb * 0.72f;
    float tb = 1.00f - 0.58f * wb * 0.72f;
    if (std::fabs(colorShift) > 0.01f) {
      const float ang = colorShift * 0.01745329f;
      const float ca = std::cos(ang), sa = std::sin(ang);
      constexpr float k = 0.57735027f;
      const float dot = k * (tr + tg + tb);
      const float nr = tr * ca + k * (tb - tg) * sa + k * dot * (1.0f - ca);
      const float ng = tg * ca + k * (tr - tb) * sa + k * dot * (1.0f - ca);
      const float nb = tb * ca + k * (tg - tr) * sa + k * dot * (1.0f - ca);
      tr = std::clamp(nr, 0.0f, 1.0f);
      tg = std::clamp(ng, 0.0f, 1.0f);
      tb = std::clamp(nb, 0.0f, 1.0f);
    }
    const unsigned char cr = static_cast<unsigned char>(std::lround(std::clamp(tr, 0.0f, 1.0f) * 255.0f));
    const unsigned char cg = static_cast<unsigned char>(std::lround(std::clamp(tg, 0.0f, 1.0f) * 255.0f));
    const unsigned char cb = static_cast<unsigned char>(std::lround(std::clamp(tb, 0.0f, 1.0f) * 255.0f));
    const float gain = lensFlare * std::clamp(intensity * optionScale, 0.0f, 1.5f) * 0.36f *
                       std::clamp(brightness, 0.4f, 1.4f);
    if (flareGainLoc_ >= 0) SetShaderValue(flareShader_, flareGainLoc_, &gain, SHADER_UNIFORM_FLOAT);

    rlSetBlendFactors(RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    BeginShaderMode(flareShader_);
    rlDisableBackfaceCulling();
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (const Spot& s : spots_) {
      rlCheckRenderBatchLimit(4);
      const float rx = kFlareTailPx * s.scale;
      const float ry = std::max(10.0f, rx * 0.24f);
      const unsigned char ca = static_cast<unsigned char>(std::lround(s.strength * 255.0f));
      rlColor4ub(cr, cg, cb, ca);
      rlTexCoord2f(0.0f, 0.0f); rlVertex2f(s.x - rx, s.y - ry);
      rlTexCoord2f(0.0f, 1.0f); rlVertex2f(s.x - rx, s.y + ry);
      rlTexCoord2f(1.0f, 1.0f); rlVertex2f(s.x + rx, s.y + ry);
      rlTexCoord2f(1.0f, 0.0f); rlVertex2f(s.x + rx, s.y - ry);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    EndShaderMode();
    EndBlendMode();
  }

  Shader shader_{};
  Shader flareShader_{};
  RenderTexture2D target_{};
  int gammaLoc_ = -1, brightnessLoc_ = -1;
  int colorShiftLoc_ = -1, glowLoc_ = -1;
  int resLoc_ = -1, timeLoc_ = -1, waveDirLoc_ = -1;
  int intensityLoc_ = -1, optionScaleLoc_ = -1, warmTintLoc_ = -1;
  int flareGainLoc_ = -1;
  int width_ = 0, height_ = 0;
  bool shaderReady_ = false;
  bool flareReady_ = false;
  struct Occ { std::size_t idx; float x0, y0, x1, y1; };
  struct Spot { float x, y, strength, scale; ship::FlareTarget::Kind kind; };
  mutable std::vector<Occ> occs_;
  mutable std::vector<Spot> spots_;
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
  theme.fontStep = sim.State().options.fontStep;
  theme.largeText = sim.State().options.largeText;
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


  audio::AudioDevice audio;
  const bool audioReady = audio.Start(assets.string(), launchAssets.string());
  audio.Configure(sim.State().options);
  std::printf("audio: %zu music file(s), %zu sound file(s)\n",
              audio.External().music.size(), audio.External().sfx.size());


  shell.ShowSplash(voyageStarted);

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
    nextTheme.reducedMotion = sim.State().reducedMotion;
    nextTheme.stageCompact = (sim.State().shipClass == "wren" || sim.State().shipClass == "explorer-needle" || sim.State().shipClass == "explorer-manta" || sim.State().shipClass == "yamato" || sim.State().shipClass == "arcadia");
    const bool themeChanged = nextTheme.id != theme.id ||
                              nextTheme.fontStep != theme.fontStep ||
                              nextTheme.largeText != theme.largeText ||
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
                                : static_cast<float>(ship::EffectiveRadiationWave(
                                      sim.State().options.radiationWave));
    const float glowScale = static_cast<float>(ship::EffectiveGlow(sim.State().options.glow));
    const float flareScale =
        static_cast<float>(ship::EffectiveLensFlare(sim.State().options.lensFlare));
    const bool adjusted = display.Ready() &&
        (ship::EffectiveDisplayGamma(sim.State().options.displayGamma) != 1.0 ||
         ship::DisplayBrightness(sim.State().options.displayBrightness) != 1.0 ||
         waveScale > 0.001f || sim.State().options.colorShift != 0.0 ||
         glowScale > 0.0f);
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
                  glowScale, flareScale, shell.FlareTargets());
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
