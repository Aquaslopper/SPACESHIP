// ============================================================================
//  Palette.h — the reference edition's colours, as a first-class value.
//  ---------------------------------------------------------------------------
//  The HTA hard-codes its hexes in CSS because Trident has no custom
//  properties, and then reskins five themes by overriding selectors under
//  body.theme-*. The port cannot do that, so every colour goes through this
//  struct instead: one table, five entries, and nothing in the UI may name a
//  colour literal of its own.
//
//  Phase 0 ships the classic flight-deck palette and applies it to Dear ImGui,
//  replacing the default dark-blue style. The other four themes already exist
//  in assets/data/content.json; wiring them up belongs to Phase 2 with the
//  theme switch itself.
// ============================================================================
#pragma once

#include "imgui.h"
#include "raylib.h"

namespace ship {
namespace ui {

struct Palette {
  const char* key;
  const char* label;

  Color paper;       // #ffffff  window and stage background
  Color panel;       // #faf7f2  cards, side panel
  Color panel2;      // #f4efe6  raised bars, taskbar
  Color panel3;      // #efe8dc  pressed / hovered fill
  Color border;      // #e4dbcb  hairlines
  Color borderDark;  // #cbb89c  stronger rules, tab edges
  Color coffee;      // #8c6f4e  primary ink accent, hull outline
  Color coffeeDark;  // #6b5338  pressed accent
  Color coffeeLite;  // #b89b76  muted accent
  Color ink;         // #3b3630  body text
  Color muted;       // #8a7f72  captions
  Color terracotta;  // #b05a42  warnings, foul air
  Color sage;        // #7f9161  nominal
  Color amber;       // #c08a3e  attention
};

// The five themes exist in the data; only the parchment flight deck is styled
// in Phase 0. The ink colour of each is what gives the themes their identity.
inline Palette ClassicPalette() {
  Palette p{};
  p.key = "classic";
  p.label = "Flight Deck - parchment";
  p.paper = Color{255, 255, 255, 255};
  p.panel = Color{250, 247, 242, 255};
  p.panel2 = Color{244, 239, 230, 255};
  p.panel3 = Color{239, 232, 220, 255};
  p.border = Color{228, 219, 203, 255};
  p.borderDark = Color{203, 184, 156, 255};
  p.coffee = Color{140, 111, 78, 255};
  p.coffeeDark = Color{107, 83, 56, 255};
  p.coffeeLite = Color{184, 155, 118, 255};
  p.ink = Color{59, 54, 48, 255};
  p.muted = Color{138, 127, 114, 255};
  p.terracotta = Color{176, 90, 66, 255};
  p.sage = Color{127, 145, 97, 255};
  p.amber = Color{192, 138, 62, 255};
  return p;
}

inline ImU32 ToImU32(Color c) {
  return IM_COL32(c.r, c.g, c.b, c.a);
}

// Flat surfaces, hairline borders, 3 px rounding: the house look, applied to
// every widget ImGui owns so nothing falls back to the stock theme.
inline void ApplyPalette(const Palette& p) {
  ImGuiStyle& style = ImGui::GetStyle();

  style.WindowRounding = 3.0f;
  style.ChildRounding = 3.0f;
  style.FrameRounding = 3.0f;
  style.PopupRounding = 3.0f;
  style.GrabRounding = 3.0f;
  style.ScrollbarRounding = 8.0f;
  style.TabRounding = 3.0f;

  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;

  style.WindowPadding = ImVec2(13.0f, 11.0f);  // .panel in the reference CSS
  style.FramePadding = ImVec2(7.0f, 3.0f);
  style.ItemSpacing = ImVec2(6.0f, 5.0f);
  style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
  style.CellPadding = ImVec2(5.0f, 3.0f);
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 9.0f;

  ImVec4* c = style.Colors;
  const auto col = [](Color v) { return ImVec4(v.r / 255.0f, v.g / 255.0f, v.b / 255.0f, v.a / 255.0f); };

  c[ImGuiCol_Text] = col(p.ink);
  c[ImGuiCol_TextDisabled] = col(p.muted);

  c[ImGuiCol_WindowBg] = col(p.panel);
  c[ImGuiCol_ChildBg] = col(p.paper);
  c[ImGuiCol_PopupBg] = col(p.panel);
  c[ImGuiCol_Border] = col(p.border);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

  c[ImGuiCol_FrameBg] = col(p.panel2);
  c[ImGuiCol_FrameBgHovered] = col(p.panel3);
  c[ImGuiCol_FrameBgActive] = col(p.panel3);

  c[ImGuiCol_TitleBg] = col(p.panel2);
  c[ImGuiCol_TitleBgActive] = col(p.panel2);
  c[ImGuiCol_TitleBgCollapsed] = col(p.panel2);
  c[ImGuiCol_MenuBarBg] = col(p.panel2);

  c[ImGuiCol_ScrollbarBg] = col(p.panel2);
  c[ImGuiCol_ScrollbarGrab] = col(p.borderDark);
  c[ImGuiCol_ScrollbarGrabHovered] = col(p.coffeeLite);
  c[ImGuiCol_ScrollbarGrabActive] = col(p.coffee);

  c[ImGuiCol_CheckMark] = col(p.coffee);
  c[ImGuiCol_SliderGrab] = col(p.coffee);
  c[ImGuiCol_SliderGrabActive] = col(p.coffeeDark);

  c[ImGuiCol_Button] = col(p.panel2);
  c[ImGuiCol_ButtonHovered] = col(p.panel3);
  c[ImGuiCol_ButtonActive] = col(p.panel3);

  c[ImGuiCol_Header] = col(p.panel3);
  c[ImGuiCol_HeaderHovered] = col(p.panel2);
  c[ImGuiCol_HeaderActive] = col(p.panel3);

  c[ImGuiCol_Separator] = col(p.border);
  c[ImGuiCol_SeparatorHovered] = col(p.coffeeLite);
  c[ImGuiCol_SeparatorActive] = col(p.coffee);

  c[ImGuiCol_ResizeGrip] = col(p.borderDark);
  c[ImGuiCol_ResizeGripHovered] = col(p.coffeeLite);
  c[ImGuiCol_ResizeGripActive] = col(p.coffee);

  c[ImGuiCol_Tab] = col(p.panel2);
  c[ImGuiCol_TabHovered] = col(p.panel3);
  c[ImGuiCol_TabSelected] = col(p.paper);
  c[ImGuiCol_TabSelectedOverline] = col(p.coffee);
  c[ImGuiCol_TabDimmed] = col(p.panel2);
  c[ImGuiCol_TabDimmedSelected] = col(p.panel);

  c[ImGuiCol_PlotLines] = col(p.sage);
  c[ImGuiCol_PlotLinesHovered] = col(p.amber);
  c[ImGuiCol_PlotHistogram] = col(p.sage);
  c[ImGuiCol_PlotHistogramHovered] = col(p.amber);

  c[ImGuiCol_TableHeaderBg] = col(p.panel2);
  c[ImGuiCol_TableBorderStrong] = col(p.borderDark);
  c[ImGuiCol_TableBorderLight] = col(p.border);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(p.ink.r / 255.0f, p.ink.g / 255.0f, p.ink.b / 255.0f, 0.02f);

  c[ImGuiCol_TextSelectedBg] = ImVec4(p.coffee.r / 255.0f, p.coffee.g / 255.0f, p.coffee.b / 255.0f, 0.20f);
  c[ImGuiCol_NavCursor] = col(p.coffee);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(p.ink.r / 255.0f, p.ink.g / 255.0f, p.ink.b / 255.0f, 0.35f);
}

}  // namespace ui
}  // namespace ship
