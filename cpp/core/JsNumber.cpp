#include "core/JsNumber.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace ship {
namespace js {
namespace {

// Pull the significant digits and the decimal exponent out of the shortest
// scientific form to_chars produces: "1.234e+05" -> digits "1234", exp 5.
struct Decimal {
  std::string digits;  // no sign, no dot, no trailing zeros beyond significance
  int exponent = 0;    // value == 0.digits * 10^(exponent+1)
};

Decimal ShortestDigits(double magnitude) {
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    magnitude, std::chars_format::scientific);
  const std::string text(buffer.data(), result.ptr);

  Decimal out;
  std::size_t i = 0;
  for (; i < text.size() && text[i] != 'e'; ++i) {
    if (text[i] >= '0' && text[i] <= '9') { out.digits.push_back(text[i]); }
  }
  if (i < text.size()) { out.exponent = std::atoi(text.c_str() + i + 1); }

  // "0.0001234e+00" never happens: to_chars always normalises to one digit
  // before the point. Trim any trailing zeros to keep 100 -> "1e+02".
  while (out.digits.size() > 1 && out.digits.back() == '0') { out.digits.pop_back(); }
  return out;
}

std::string ExponentSuffix(int exponent) {
  std::string out = "e";
  if (exponent >= 0) {
    out += '+';
  } else {
    out += '-';
    exponent = -exponent;
  }
  out += std::to_string(exponent);
  return out;
}

}  // namespace

bool IsNegativeZero(double value) {
  return value == 0.0 && std::signbit(value);
}

double Round(double value) {
  if (!std::isfinite(value) || value == 0.0) { return value; }
  const double floored = std::floor(value + 0.5);
  if (floored == 0.0 && std::signbit(value)) { return -0.0; }
  return floored;
}

double Ceil(double value) {
  const double result = std::ceil(value);
  // std::ceil already keeps -0.0 for -0.5, but be explicit about the contract.
  return result;
}

double Floor(double value) { return std::floor(value); }

std::string NumToString(double value) {
  if (std::isnan(value)) { return "NaN"; }
  if (std::isinf(value)) { return value > 0 ? "Infinity" : "-Infinity"; }
  if (value == 0.0) { return "0"; }  // String(-0) is "0"

  std::string sign;
  if (value < 0) {
    sign = "-";
    value = -value;
  }

  const Decimal decimal = ShortestDigits(value);
  const std::string& digits = decimal.digits;
  const int k = static_cast<int>(digits.size());        // significant digits
  const int n = decimal.exponent + 1;                   // decimal point position

  std::string out;
  if (k <= n && n <= 21) {
    // Integer with trailing zeros: 100, 123456789012345680000.
    out = digits;
    out.append(static_cast<std::size_t>(n - k), '0');
  } else if (0 < n && n <= 21) {
    // Plain decimal inside the point: 1.5, 1234.5.
    out = digits.substr(0, static_cast<std::size_t>(n));
    out += '.';
    out += digits.substr(static_cast<std::size_t>(n));
  } else if (-6 < n && n <= 0) {
    // Leading zeros after the point: 0.000001, 0.1.
    out = "0.";
    out.append(static_cast<std::size_t>(-n), '0');
    out += digits;
  } else {
    // Exponent form, used outside 1e-6 .. 1e21.
    out = digits.substr(0, 1);
    if (k > 1) {
      out += '.';
      out += digits.substr(1);
    }
    out += ExponentSuffix(n - 1);
  }
  return sign + out;
}

std::string ToFixed(double value, int digits) {
  if (std::isnan(value)) { return "NaN"; }
  if (digits < 0) { digits = 0; }
  if (digits > 100) { digits = 100; }
  if (std::fabs(value) >= 1e21) { return NumToString(value); }

  // The specification works on the magnitude and prefixes the sign afterwards,
  // which is what makes (-2.5).toFixed(0) === "-3" rather than "-2".
  std::string sign;
  double magnitude = value;
  if (magnitude < 0) {
    sign = "-";
    magnitude = -magnitude;
  }

  // n / 10^d closest to x, ties going to the larger n: half away from zero.
  const long double scale = std::pow(10.0L, digits);
  const long double scaled = static_cast<long double>(magnitude) * scale;
  long double rounded = std::floor(scaled + 0.5L);
  if (rounded < 0) { rounded = 0; }

  std::array<char, 512> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    static_cast<unsigned long long>(rounded));
  std::string integer(buffer.data(), result.ptr);

  if (digits == 0) { return sign + integer; }

  // Left-pad so there is always at least one digit before the point.
  const std::size_t needed = static_cast<std::size_t>(digits) + 1;
  if (integer.size() < needed) { integer.insert(0, needed - integer.size(), '0'); }

  const std::size_t split = integer.size() - static_cast<std::size_t>(digits);
  return sign + integer.substr(0, split) + "." + integer.substr(split);
}

}  // namespace js
}  // namespace ship
