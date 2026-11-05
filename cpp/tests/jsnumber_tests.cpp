// ============================================================================
//  jsnumber_tests.cpp — the ES5 semantics the port must not get wrong.
//  ---------------------------------------------------------------------------
//  Every expected value in this file was produced by the reference runtime
//  (node, which implements the same ECMAScript arithmetic Trident does). Where
//  C++ would differ, the test says so explicitly, because those are exactly the
//  places a future refactor is tempted to "simplify" back to the C++ default.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "core/JsNumber.h"

TEST_CASE("Math.round sends halves toward positive infinity", "[js][number]") {
  using ship::js::Round;

  CHECK(Round(0.5) == 1.0);
  CHECK(Round(1.5) == 2.0);
  CHECK(Round(2.5) == 3.0);

  // C++'s lround/round would give -1 and -2 here. JavaScript does not.
  CHECK(Round(-0.5) == 0.0);
  CHECK(Round(-1.5) == -1.0);
  CHECK(Round(-2.5) == -2.0);

  // The negative result of rounding to zero keeps its sign.
  CHECK(ship::js::IsNegativeZero(Round(-0.5)));
  CHECK(ship::js::IsNegativeZero(Round(-0.4)));
  CHECK_FALSE(ship::js::IsNegativeZero(Round(-0.6)));

  CHECK(Round(2.4999999) == 2.0);
  CHECK(Round(-0.0001) == 0.0);
  CHECK(Round(0.0) == 0.0);
  CHECK(std::isnan(Round(std::nan(""))));
  CHECK(Round(1e308) == 1e308);  // 1e308 + 0.5 is still 1e308
}

TEST_CASE("Math.ceil and Math.floor keep the sign of zero", "[js][number]") {
  using ship::js::Ceil;
  using ship::js::Floor;

  CHECK(Ceil(1.2) == 2.0);
  CHECK(Ceil(-1.2) == -1.0);
  CHECK(ship::js::IsNegativeZero(Ceil(-0.5)));
  CHECK(ship::js::IsNegativeZero(Ceil(-0.0)));

  CHECK(Floor(1.8) == 1.0);
  CHECK(Floor(-1.8) == -2.0);
  CHECK(Floor(-0.5) == -1.0);
}

TEST_CASE("String(number) matches JavaScript digit for digit", "[js][number]") {
  using ship::js::NumToString;

  CHECK(NumToString(0.0) == "0");
  CHECK(NumToString(-0.0) == "0");  // String(-0) is "0", not "-0"
  CHECK(NumToString(1.0) == "1");
  CHECK(NumToString(-1.0) == "-1");
  CHECK(NumToString(1.5) == "1.5");
  CHECK(NumToString(-1.5) == "-1.5");
  CHECK(NumToString(100.0) == "100");
  CHECK(NumToString(1000000.0) == "1000000");

  // The classic float artefact, which the feed must reproduce verbatim.
  CHECK(NumToString(0.1 + 0.2) == "0.30000000000000004");
  CHECK(NumToString(1.0 / 3.0) == "0.3333333333333333");

  // Exponent form only outside 1e-6 .. 1e21.
  CHECK(NumToString(1e21) == "1e+21");
  CHECK(NumToString(1e-7) == "1e-7");
  CHECK(NumToString(0.000001) == "0.000001");
  CHECK(NumToString(1.5e21) == "1.5e+21");
  CHECK(NumToString(1.23456e-9) == "1.23456e-9");
  CHECK(NumToString(123456789012345678901.0) == "123456789012345680000");

  CHECK(NumToString(std::nan("")) == "NaN");
  CHECK(NumToString(HUGE_VAL) == "Infinity");
  CHECK(NumToString(-HUGE_VAL) == "-Infinity");
}

TEST_CASE("toFixed rounds ties away from zero and pads like JavaScript",
          "[js][number]") {
  using ship::js::ToFixed;

  CHECK(ToFixed(0.0, 2) == "0.00");
  CHECK(ToFixed(1.0, 2) == "1.00");
  CHECK(ToFixed(1.005, 2) == "1.00");   // 1.005 is really 1.00499999...
  CHECK(ToFixed(1.45, 1) == "1.4");
  CHECK(ToFixed(1.25, 1) == "1.3");     // an exact tie, rounded up
  CHECK(ToFixed(2.5, 0) == "3");
  CHECK(ToFixed(-2.5, 0) == "-3");
  CHECK(ToFixed(0.5, 0) == "1");
  // Verified against node: a negative value that rounds to zero keeps its sign.
  CHECK(ToFixed(-0.4, 0) == "-0");
  CHECK(ToFixed(-0.04, 1) == "-0.0");
  CHECK(ToFixed(-0.0, 2) == "0.00");    // but -0 itself has no sign to keep
  CHECK(ToFixed(12.3456, 2) == "12.35");
  CHECK(ToFixed(1e21, 2) == "1e+21");   // at 1e21 JavaScript gives up on fixed
}
