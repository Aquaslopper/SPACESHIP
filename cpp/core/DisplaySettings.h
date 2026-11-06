#pragma once

#include <algorithm>
#include <cmath>

namespace ship {

// The displayed 1.0 is calibrated to the original 1.6 gamma setting. Higher
// values darken midtones; brightness also changes the white point.
constexpr double kDisplayGammaBaseline = 1.6;
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

}  // namespace ship
