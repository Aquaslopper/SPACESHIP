#pragma once

#include <algorithm>
#include <cmath>

namespace ship {

// Neutral 1.0 preserves the authored palette. Higher gamma darkens midtones;
// brightness changes the white point as well, without changing colour themes.
inline double DisplayGamma(double value) {
  return std::isfinite(value) ? std::clamp(value, 0.7, 1.6) : 1.0;
}
inline double DisplayBrightness(double value) {
  return std::isfinite(value) ? std::clamp(value, 0.75, 1.15) : 1.0;
}
inline double AdjustDisplayChannel(double value, double gamma, double brightness) {
  return std::clamp(std::pow(std::clamp(value, 0.0, 1.0), DisplayGamma(gamma)) *
                    DisplayBrightness(brightness), 0.0, 1.0);
}

}  // namespace ship
