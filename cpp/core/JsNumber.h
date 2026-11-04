// ============================================================================
//  JsNumber.h — the reference edition's arithmetic, exactly.
//  ---------------------------------------------------------------------------
//  Spaceship.hta is JavaScript, and a port that uses C++'s defaults instead of
//  JavaScript's would drift in three places that players can see: rounding at
//  halves, how a number becomes a string in the feed, and fixed-decimal
//  readouts. Every game number therefore goes through these helpers.
//
//    Math.round          rounds halves toward +Infinity   (C++ lround: away
//                        from zero, and -0.5 -> -1 instead of -0)
//    Math.ceil/floor     return -0 for negative fractions  (sign is visible
//                        through 1/x and Object.is)
//    String(n)           shortest round-trip decimal, giving up on exponent
//                        form only outside 1e-6 .. 1e21
//    n.toFixed(d)        ties away from zero, and falls back to String(n)
//                        at or above 1e21
//
//  Header-only would be tempting, but these run once per rendered number and
//  benefit from a single out-of-line copy.
// ============================================================================
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
