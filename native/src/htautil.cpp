#include "htautil.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hta {

std::u16string widen(const std::string& utf8) {
  std::u16string out;
  out.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    unsigned char c = static_cast<unsigned char>(utf8[i]);
    unsigned cp = 0;
    size_t extra = 0;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      out.push_back(0xFFFD);
      ++i;
      continue;
    }
    if (i + extra >= utf8.size() + 0 && extra && i + extra > utf8.size() - 1) {
      // truncated sequence
      out.push_back(0xFFFD);
      break;
    }
    bool ok = true;
    for (size_t k = 1; k <= extra; ++k) {
      unsigned char cc = static_cast<unsigned char>(utf8[i + k]);
      if ((cc & 0xC0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) {
      out.push_back(0xFFFD);
      ++i;
      continue;
    }
    i += extra + 1;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
  }
  return out;
}

std::string narrow(const std::u16string& utf16) {
  std::string out;
  out.reserve(utf16.size());
  for (size_t i = 0; i < utf16.size(); ++i) {
    unsigned cp = utf16[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < utf16.size() && utf16[i + 1] >= 0xDC00 &&
        utf16[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (utf16[i + 1] - 0xDC00);
      ++i;
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

size_t utf8Length(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) ++n;
  }
  return n;
}

std::string utf8Substr(const std::string& s, size_t start, size_t count) {
  size_t index = 0, pos = 0, begin = std::string::npos;
  while (pos < s.size()) {
    if (index == start) begin = pos;
    if (index == start + count) return s.substr(begin, pos - begin);
    ++index;
    pos += 1;
    while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) ++pos;
  }
  if (index == start) begin = pos;
  if (begin == std::string::npos) return std::string();
  return s.substr(begin);
}

std::string lowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string upperAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == sep) {
      out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parseCssNumber(const std::string& token, float& value, bool& isPercent) {
  std::string t = trim(token);
  isPercent = false;
  if (t.empty()) return false;
  char* end = nullptr;
  double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str()) return false;
  std::string unit = lowerAscii(std::string(end));
  if (unit == "%") {
    isPercent = true;
    value = static_cast<float>(v);
    return true;
  }
  if (unit.empty() || unit == "px") {
    value = static_cast<float>(v);
    return true;
  }
  // em/rem and friends are resolved against the element font size by the
  // style layer; the plain numeric value is returned for it to scale.
  if (unit == "em" || unit == "rem") {
    value = static_cast<float>(v);
    return true;
  }
  if (unit == "pt") {
    value = static_cast<float>(v * 96.0 / 72.0);
    return true;
  }
  return false;
}

std::string jsNumber(double v) {
  if (!std::isfinite(v)) return "0";
  if (v == static_cast<double>(static_cast<long long>(v)) && std::fabs(v) < 1e15) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    return buf;
  }
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.10g", v);
  return buf;
}

}  // namespace hta
