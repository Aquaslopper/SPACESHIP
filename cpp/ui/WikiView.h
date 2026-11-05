// ============================================================================
//  WikiView.h — the handbook's session state, on its own so the router can
//  take it without pulling in the screen that fills it (WIKI_VIEW in the HTA).
// ============================================================================
#pragma once

#include <string>

namespace ship {

struct WikiView {
  std::string query;
  std::string selected = "first-watch";
  bool revealed = false;   // the spoiler gate, off by default
};

}  // namespace ship
