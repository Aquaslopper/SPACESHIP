// Small shared helpers for the native HTA host.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hta {

// UTF-8 <-> UTF-16 helpers. The game script is pure ASCII, but user data
// (save files, titles) can carry anything, so conversions are total.
std::u16string widen(const std::string& utf8);
std::string narrow(const std::u16string& utf16);

// Codepoint-level UTF-8 handling used by the DOM text layer.
size_t utf8Length(const std::string& s);
std::string utf8Substr(const std::string& s, size_t start, size_t count);

std::string lowerAscii(std::string s);
std::string upperAscii(std::string s);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);

// Parses "12px", "1.5em" (emBase supplied), "%" against percentBase.
// Returns false when the token has no usable unit.
bool parseCssNumber(const std::string& token, float& value, bool& isPercent);

// Locale-independent number formatting that matches JavaScript's
// Number#toString closely enough for CSS and JSON output.
std::string jsNumber(double v);

}  // namespace hta
