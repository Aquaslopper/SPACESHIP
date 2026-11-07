// ============================================================================
//  Layout.h — the cockpit's measurements, as arithmetic.
//  ---------------------------------------------------------------------------
//  The reference measures itself with the browser: `offsetHeight` for what is
//  already drawn, then one leftover number handed to the feed. That is a sound
//  design with an unsound implementation (a browser is not a test), so the port
//  keeps the design and replaces the measuring with numbers: every anchor below
//  is the pixel the stylesheet pins, and `Give` is the same
//  "measure - subtract - give the rest away" step, written so CTest can check it
//  without a window.
// ============================================================================
#pragma once

#include <vector>

namespace ship {

struct Area {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  float Right() const { return x + w; }
  float Bottom() const { return y + h; }
  bool Contains(float px, float py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// §6.4 / stylesheet: the fixed furniture. Nothing below is negotiable at
// runtime; the window may grow, these do not.
struct Metrics {
  static constexpr float kTaskbarH = 35.0f;     // #taskbar height
  static constexpr float kControlsW = 48.0f;    // right-hand tab-scroll arrows
  static constexpr float kHomeW = 0.0f;         // Home is fixed at the left of the frame
  static constexpr float kSideTop = 36.0f;      // #sidepanel top
  static constexpr float kSideW = 165.0f;       // #sidepanel width
  static constexpr float kSideBottom = 97.0f;   // leave room for the pinned foot
  static constexpr float kFootH = 96.0f;        // #sidefoot height
  static constexpr float kFootBottom = 1.0f;    // no bottom status bar
  static constexpr float kStatusH = 0.0f;       // no bottom status bar
  static constexpr float kContentTop = 36.0f;
  static constexpr float kContentLeft = 166.0f;
  static constexpr float kFeedMin = 62.0f;      // FEED.min
  static constexpr float kFeedMax = 132.0f;     // FEED.max
  static constexpr float kHomeFeedMax = 152.0f; // Home feed ceiling from the compact layout
  static constexpr float kIndH = 104.0f;        // IND.h
  static constexpr float kIndGap = 5.0f;        // IND.gap
  static constexpr float kStageW = 1160.0f;     // STAGE.ship
  static constexpr float kStageH = 460.0f;
  static constexpr float kStageCompactW = 600.0f;
  static constexpr float kStageCompactH = 365.0f;
  static constexpr float kHomeStageMinH = 410.0f;
  static constexpr float kMapH = 500.0f;        // STAGE.map ceiling
  static constexpr float kMapParkH = 240.0f;    // layoutNav() parks here
  static constexpr float kTabH = 27.0f;         // .tab height
};

// One running process, as the indicator strip draws it: a line from the bottom
// up to `pct` of the strip, red if it grows as it gets worse.
struct IndicatorLine {
  int pct = 0;
  bool grow = false;
};

struct IndicatorRect {
  Area ghost;  // the unfilled remainder, drawn first
  Area line;   // the ink, drawn over it
  bool grow = false;
};

// What the shell needs to know about one window.
struct ShellLayout {
  Area taskbar;
  Area tabStrip;   // taskbar minus the controls
  Area controls;   // scroll left / scroll right / Home
  Area scrollLeft;
  Area scrollRight;
  Area home;
  Area sidepanel;
  Area sidefoot;
  Area content;
  Area statusbar;
  Area stage;
  Area feed;
  float feedHeight = Metrics::kFeedMin;
};

//  measure-and-give: everything that is not the feed has already been measured
//  (`others`), the content area is `contentHeight` tall, and the feed gets what
//  is left - four pixels of breathing room - never less than 62, never more
//  than 132.
float GiveFeedHeight(float contentHeight, float others);
float GiveMapHeight(float contentHeight, float others, float mapViewWidth);

ShellLayout ComputeLayout(float windowW, float windowH, float contentHeight,
                           float others, bool compactStage,
                           float homeWidth = Metrics::kHomeW);

// The stage is a fixed rectangle, centred in the content column. If the window
// is narrower than the ship the ship keeps its size and the column scrolls -
// the reference's `.screen` does exactly that.
Area StageRect(const Area& content, bool compactStage);

// Bottom-up lines: index 0 is the leftmost, one 5px pitch apart, 104px tall.
std::vector<IndicatorRect> PlaceIndicators(const std::vector<IndicatorLine>& lines);

}  // namespace ship
