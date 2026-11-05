// Literal scene CSS from Spaceship.hta lines 918–1199 only.
// Checked against the bounded source by tools/check-first-2000.cjs.
// Reference type sizes are retained, subject to the approved 18 px native floor.
#pragma once
#include <string>
namespace ship {
struct SceneStyle {
  const char* cls; const char* background; const char* border; const char* ink;
  int width, sides, radius; bool dashed; int align; int fontPx; bool mono;
};
inline const SceneStyle kFirst2000SceneStyles[] = {
    {"mach-pipe", "#efe8dc", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"mach-valve", "#f4efe6", "#8c6f4e", "", 1, 15, 0, false, 0, 0, false},
    {"mach-rod", "#cbb89c", "", "", 0, 0, 0, false, 0, 0, false},
    {"mach-spoke", "#cbb89c", "", "", 0, 0, 0, false, 0, 0, false},
    {"mach-brk", "#cbb89c", "", "", 0, 0, 0, false, 0, 0, false},
    {"mach-mount", "#cbb89c", "#a89476", "", 1, 15, 0, false, 0, 0, false},
    {"mach-rivet", "#cbb89c", "", "", 0, 0, 2, false, 0, 0, false},
    {"mach-flow", "#a89476", "", "", 0, 0, 2, false, 0, 0, false},
    {"mach-fin", "#ece5d8", "#cbb89c", "", 1, 8, 0, false, 0, 0, false},
    {"mach-tick", "#cbb89c", "", "", 0, 0, 3, false, 0, 0, false},
    {"mach-nd", "#8c6f4e", "", "", 0, 0, 2, false, 0, 0, false},
    {"mach-scale", "#ddd2bf", "", "", 0, 0, 0, false, 0, 0, false},
    {"mach-dial", "#fdfbf7", "#8c6f4e", "", 1, 15, -1, false, 0, 0, false},
    {"mach-hub2", "#cbb89c", "#8c6f4e", "", 1, 15, -1, false, 0, 0, false},
    {"mach-lamp", "#efe8dc", "#cbb89c", "", 1, 15, -1, false, 0, 0, false},
    {"mach-lamp-on", "#7f9161", "#6b7d50", "", 0, 0, 0, false, 0, 0, false},
    {"mach-lamp-amb", "#c08a3e", "#a8762f", "", 0, 0, 0, false, 0, 0, false},
    {"mach-ring", "#faf7f2", "#8c6f4e", "", 2, 15, -1, false, 0, 0, false},
    {"mach-ring2", "", "#cbb89c", "", 1, 15, -1, true, 0, 0, false},
    {"mach-hub", "#8c6f4e", "#6b5338", "", 1, 15, -1, false, 0, 0, false},
    {"mach-core", "#fdfbf7", "", "", 0, 0, -1, false, 0, 0, false},
    {"mach-bolt", "#f4efe6", "#8c6f4e", "", 1, 15, -1, false, 0, 0, false},
    {"mach-wheel", "#f7f2e9", "#8c6f4e", "", 2, 15, -1, false, 0, 0, false},
    {"mach-box", "#faf7f2", "#e4dbcb", "", 1, 15, 0, false, 0, 0, false},
    {"mach-cyl", "#efe8dc", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"mach-mini", "", "", "#a2947f", 0, 0, 0, false, 0, 8, false},
    {"mach-mini2", "", "", "#b3a794", 0, 0, 0, false, 0, 8, false},
    {"mach-cap", "", "", "#a2947f", 0, 0, 0, false, 1, 8, false},
    {"mach-stat", "", "#f0e9dd", "#6b6157", 1, 4, 0, false, 0, 9, false},
    {"mach-sv", "", "", "#3b3630", 0, 0, 0, false, 0, 0, true},
    {"mach-track", "#ece5d8", "", "", 0, 0, 0, false, 0, 0, false},
    {"mach-fillx", "#cbb89c", "", "", 0, 0, 0, false, 0, 0, false},
    {"mach-track2", "#ece5d8", "#ddd2bf", "", 1, 15, 0, false, 0, 0, false},
    {"mach-fill2", "#7f9161", "", "", 0, 0, 0, false, 0, 0, false},
    {"mach-pct", "", "", "#3b3630", 0, 0, 0, false, 2, 26, true},
    {"mach-word", "", "", "", 0, 0, 0, false, 2, 10, false},
    {"mach-plate", "", "", "#b3a794", 0, 0, 0, false, 0, 8, true},
    {"o2-duct", "#efe8dc", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"o2-louvre", "#cbb89c", "", "", 0, 0, 0, false, 0, 0, false},
    {"o2-flow", "#e4dbcb", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"o2-mini", "", "", "#a2947f", 0, 0, 0, false, 0, 8, false},
    {"o2-air", "#efe7d8", "#ddd2bf", "", 1, 15, 0, false, 0, 0, false},
    {"o2-air-clean", "#f1f6ea", "#cfdcc0", "", 0, 0, 0, false, 0, 0, false},
    {"o2-dust", "#b9a893", "", "", 0, 0, 0, false, 0, 0, false},
    {"o2-streak", "#cfdcc0", "", "", 0, 0, 0, false, 0, 0, false},
    {"o2-airword", "", "", "", 0, 0, 0, false, 0, 11, false},
    {"o2-airsub", "", "", "#a2947f", 0, 0, 0, false, 0, 8, false},
    {"o2-airtrack", "#e6ddcd", "#d3c6b1", "", 1, 15, 0, false, 0, 0, false},
    {"o2-airnotch", "#fdfbf7", "", "", 0, 0, 0, false, 0, 0, false},
    {"o2-airlab", "", "", "#a2947f", 0, 0, 0, false, 0, 8, false},
    {"o2-slot", "#faf7f2", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"o2-well", "#f4efe6", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"o2-well-bare", "#fdfbf7", "#cbb89c", "", 1, 15, 0, true, 0, 0, false},
    {"o2-can", "#e4dbcb", "#8c6f4e", "", 1, 15, 8, false, 0, 0, false},
    {"o2-can-live", "#dfeccd", "#7f9161", "", 0, 0, 0, false, 0, 0, false},
    {"o2-neck", "#cbb89c", "#8c6f4e", "", 1, 15, 0, false, 0, 0, false},
    {"o2-state", "", "", "", 0, 0, 0, false, 1, 10, false},
    {"o2-track", "#ece5d8", "#ddd2bf", "", 1, 15, 0, false, 0, 0, false},
    {"o2-fill", "#7f9161", "", "", 0, 0, 0, false, 0, 0, false},
    {"mess-counter", "#f3e6cf", "#d2a059", "", 1, 15, 0, false, 0, 0, false},
    {"mess-rackbox", "#faf7f2", "#d2a059", "", 1, 15, 0, false, 0, 0, false},
    {"mess-mini", "", "", "#a2947f", 0, 0, 0, false, 0, 8, false},
    {"mess-track", "#ece5d8", "#ddd2bf", "", 1, 15, 0, false, 0, 0, false},
    {"mess-fill", "#d2a059", "", "", 0, 0, 0, false, 0, 0, false},
    {"mess-fill-small", "#7f9161", "", "", 0, 0, 0, false, 0, 0, false},
    {"mess-readout", "", "", "#6b6157", 0, 0, 0, false, 1, 10, false},
    {"mess-plate", "#faf7f2", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"mess-plate-name", "", "", "#3b3630", 0, 0, 0, false, 0, 11, false},
    {"mess-plate-sub", "", "", "#a2947f", 0, 0, 0, false, 0, 9, false},
    {"cq-wall", "#faf7f2", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-floor", "#f0e9dd", "#cbb89c", "", 1, 1, 0, false, 0, 0, false},
    {"cq-board", "#e4dbcb", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-skirt", "#e9e0d0", "#d8cab4", "", 1, 1, 0, false, 0, 0, false},
    {"cq-light", "#f7f2e9", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"cq-glow", "#f4efe6", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-door", "#f4efe6", "#8c6f4e", "", 2, 15, 0, false, 0, 0, false},
    {"cq-dpane", "#faf7f2", "#cbb89c", "", 1, 15, 0, false, 0, 0, false},
    {"cq-dhand", "#cbb89c", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-lock", "#f4efe6", "#8c6f4e", "", 1, 15, 0, false, 0, 0, false},
    {"cq-lvent", "#ddd2bf", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-lhand", "#8c6f4e", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-lnum", "", "", "#b3a794", 0, 0, 0, false, 0, 8, false},
    {"cq-mini", "", "", "#a2947f", 0, 0, 0, false, 1, 8, false},
    {"cq-shadow", "#e4dbcb", "", "", 0, 0, -1, false, 0, 0, false},
    {"cq-pay", "#faf7f2", "#efe8dc", "", 1, 15, 0, false, 0, 0, false},
    {"cq-payh", "", "", "#8c6f4e", 0, 0, 0, false, 0, 9, false},
    {"cq-payk", "", "", "#8a7f72", 0, 0, 0, false, 0, 10, false},
    {"cq-payv", "", "", "#3b3630", 0, 0, 0, false, 0, 10, true},
    {"cq-row", "", "#f0e9dd", "", 1, 4, 0, false, 0, 0, false},
    {"cq-row-sel", "#f4efe6", "#8c6f4e", "", 3, 8, 0, false, 0, 0, false},
    {"cq-dot", "", "", "", 0, 0, 5, false, 0, 0, false},
    {"cq-nm", "", "", "#3b3630", 0, 0, 0, false, 0, 12, false},
    {"cq-flag", "#f4efe6", "#cbb89c", "#8c6f4e", 1, 15, 0, false, 0, 9, false},
    {"cq-rl", "", "", "#a2947f", 0, 0, 0, false, 0, 10, false},
    {"cq-sub", "", "", "#3b3630", 0, 0, 0, false, 0, 13, false},
    {"cq-lab", "", "", "#8c6f4e", 0, 0, 0, false, 0, 9, false},
    {"cq-sel", "#ffffff", "#cbb89c", "#3b3630", 1, 15, 0, false, 0, 12, false},
    {"cq-cost", "", "", "#a2947f", 0, 0, 0, false, 0, 10, false},
    {"cq-ord", "#ffffff", "#e4dbcb", "#6b6157", 1, 15, 0, false, 0, 11, false},
    {"cq-ord-on", "#f4efe6", "#8c6f4e", "#3b3630", 0, 0, 0, false, 0, 0, false},
    {"cq-box", "#ffffff", "#cbb89c", "#8c6f4e", 1, 15, 0, false, 1, 10, false},
    {"cq-ordc", "", "", "#a2947f", 0, 0, 0, false, 0, 10, false},
    {"cq-num", "", "", "#3b3630", 0, 0, 0, false, 0, 0, true},
    {"cq-row-busy", "#faf7f2", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-flag-busy", "#efe8dc", "#ddd2bf", "#6b6157", 0, 0, 0, false, 0, 0, true},
    {"cq-closed", "", "", "#8a7f72", 0, 0, 0, false, 0, 12, false},
    {"cq-ord-off", "#faf7f2", "", "", 0, 0, 0, false, 0, 0, false},
    {"cq-ordn", "", "", "#a2947f", 0, 0, 0, false, 0, 9, false},
    {"cq-sub2", "", "", "#8c6f4e", 0, 0, 0, false, 0, 0, true},
    {"cq-talkbtn", "#ffffff", "#8c6f4e", "#3b3630", 1, 15, 0, false, 1, 12, false},
    {"cq-talkbtn-off", "#faf7f2", "#e4dbcb", "#b3a794", 0, 0, 0, false, 0, 0, false},
    {"cq-says", "", "", "#5c5145", 0, 0, 0, false, 0, 12, false},
};
inline SceneStyle SceneStyleFor(const std::string& classes) {
  SceneStyle result{"", "", "", "", 0, 0, 0, false, 0, 12, false};
  const std::string words = " " + classes + " ";
  // Source order, not class attribute order: state overrides inherit base rules.
  for (const auto& rule : kFirst2000SceneStyles) {
    if (words.find(" " + std::string(rule.cls) + " ") == std::string::npos) continue;
    result.cls = rule.cls;
    if (*rule.background) result.background = rule.background;
    if (*rule.border) result.border = rule.border;
    if (*rule.ink) result.ink = rule.ink;
    if (rule.width) { result.width = rule.width; result.sides = rule.sides; result.dashed = rule.dashed; }
    if (rule.radius) result.radius = rule.radius;
    if (rule.align) result.align = rule.align;
    if (rule.fontPx) result.fontPx = rule.fontPx;
    if (rule.mono) result.mono = true;
  }
  return result;
}
} // namespace ship
