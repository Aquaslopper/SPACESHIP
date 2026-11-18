#pragma once

#include <string>

namespace ship {
namespace js {

// Math.round: halves go up, and a negative result that rounds to zero keeps
// its sign.
double Round(double value);

// Math.ceil / Math.floor. Both preserve the sign of zero, as JavaScript does.
double Ceil(double value);
double Floor(double value);

// String(n) for a number: 1 -> "1", 1.5 -> "1.5", 1e21 -> "1e+21",
// 0.000001 -> "0.000001", 1e-7 -> "1e-7", -0 -> "0".
std::string NumToString(double value);

// n.toFixed(digits).
std::string ToFixed(double value, int digits);

// True when the value is the double JavaScript calls -0.
bool IsNegativeZero(double value);

}  // namespace js
}  // namespace ship
