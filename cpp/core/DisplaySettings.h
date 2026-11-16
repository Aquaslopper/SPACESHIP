#pragma once

#include <algorithm>
#include <cmath>

namespace ship {

// The displayed 1.0 is calibrated to the default 1.92 gamma exponent. Higher
// values darken midtones; brightness also changes the white point.
constexpr double kDisplayGammaBaseline = 1.92;
constexpr double kDisplayGammaMin = 0.4;
constexpr double kDisplayGammaMax = 1.6;
inline double DisplayGamma(double value) {
  return std::isfinite(value) ? std::clamp(value, kDisplayGammaMin, kDisplayGammaMax) : 1.0;
}
inline double EffectiveDisplayGamma(double value) {
  return kDisplayGammaBaseline * DisplayGamma(value);
}
inline double DisplayBrightness(double value) {
  return std::isfinite(value) ? std::clamp(value, 0.75, 1.15) : 1.0;
}
inline double AdjustDisplayChannel(double value, double gamma, double brightness) {
  return std::clamp(std::pow(std::clamp(value, 0.0, 1.0), EffectiveDisplayGamma(gamma)) *
                    DisplayBrightness(brightness), 0.0, 1.0);
}

constexpr double kRadiationWaveBaseline = 1.5;
constexpr double kRadiationWaveMin = 0.0;
constexpr double kRadiationWaveDefault = 1.0;
constexpr double kRadiationWaveMax = 2.0;
inline double RadiationWaveStrength(double value) {
  return std::isfinite(value) ? std::clamp(value, kRadiationWaveMin, kRadiationWaveMax)
                              : kRadiationWaveDefault;
}
inline double EffectiveRadiationWave(double value) {
  return kRadiationWaveBaseline * RadiationWaveStrength(value);
}

constexpr double kGlowBaseline = 1.5;
inline double EffectiveGlow(double value) {
  return kGlowBaseline * (std::isfinite(value) ? std::clamp(value, 0.0, 2.0) : 1.0);
}

constexpr double kLensFlareBaseline = 0.7;
inline double EffectiveLensFlare(double value) {
  return kLensFlareBaseline * (std::isfinite(value) ? std::clamp(value, 0.0, 2.0) : 1.0);
}

}  // namespace ship
