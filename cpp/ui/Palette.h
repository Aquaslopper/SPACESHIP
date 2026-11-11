// ============================================================================
//  Palette.h — the theme, handed to Dear ImGui.
//  ---------------------------------------------------------------------------
//  The colours themselves live in Theme.h, which is headless so the tests can
//  read them without a graphics library. This header is the only place that
//  converts them into the types ImGui and Raylib want, and the only place that
//  styles ImGui at all.
//
//  The rule from the plan stands: no widget may fall back to the stock
//  dark-blue ImGui theme. Every colour ImGui can be told about is set here,
//  from the same role table the rest of the cockpit uses - so a tab, a button
//  and the hull outline are always the same coffee.
// ============================================================================
#pragma once

#include "imgui.h"
#include "raylib.h"

#include "ui/Theme.h"

namespace ship {
namespace ui {

inline ::Color ToRay(const Rgb& rgb, int alpha = 255) {
  return ::Color{static_cast<unsigned char>(rgb.r), static_cast<unsigned char>(rgb.g),
                 static_cast<unsigned char>(rgb.b),
                 static_cast<unsigned char>(alpha)};
}

inline ImU32 ToImU32(const Rgb& rgb, int alpha = 255) {
  return IM_COL32(rgb.r, rgb.g, rgb.b, alpha);
}

// Flat surfaces, crisp 0px corners: cassette-futurism styling applied to every
// widget ImGui owns.
inline void ApplyPalette(const Theme& theme) {
  const Palette& p = theme.palette;
  ImGuiStyle& style = ImGui::GetStyle();

  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 0.0f;
  style.PopupRounding = 0.0f;
  style.GrabRounding = 0.0f;
  style.ScrollbarRounding = 0.0f;
  style.TabRounding = 0.0f;

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

  const auto col = [](const Rgb& v) {
    return ImVec4(v.r / 255.0f, v.g / 255.0f, v.b / 255.0f, 1.0f);
  };
  ImVec4* c = style.Colors;

  c[ImGuiCol_Text] = col(p.ink);
  c[ImGuiCol_TextDisabled] = col(p.muted);

  c[ImGuiCol_WindowBg] = col(p.panel);
  c[ImGuiCol_ChildBg] = col(p.content);
  c[ImGuiCol_PopupBg] = col(p.panel);
  c[ImGuiCol_Border] = col(p.border);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

  c[ImGuiCol_FrameBg] = col(p.input);
  c[ImGuiCol_FrameBgHovered] = col(p.panel3);
  c[ImGuiCol_FrameBgActive] = col(p.panel3);

  c[ImGuiCol_TitleBg] = col(p.panel2);
  c[ImGuiCol_TitleBgActive] = col(p.panel2);
  c[ImGuiCol_TitleBgCollapsed] = col(p.panel2);
  c[ImGuiCol_MenuBarBg] = col(p.panel2);

  c[ImGuiCol_ScrollbarBg] = col(p.panel2);
  c[ImGuiCol_ScrollbarGrab] = col(p.borderDark);
  c[ImGuiCol_ScrollbarGrabHovered] = col(p.coffeeLight);
  c[ImGuiCol_ScrollbarGrabActive] = col(p.coffee);

  c[ImGuiCol_CheckMark] = col(p.coffee);
  c[ImGuiCol_SliderGrab] = col(p.coffee);
  c[ImGuiCol_SliderGrabActive] = col(p.coffeeDark);

  c[ImGuiCol_Button] = col(p.control);
  c[ImGuiCol_ButtonHovered] = col(p.hover);
  c[ImGuiCol_ButtonActive] = col(p.panel3);

  c[ImGuiCol_Header] = col(p.panel3);
  c[ImGuiCol_HeaderHovered] = col(p.panel2);
  c[ImGuiCol_HeaderActive] = col(p.panel3);

  c[ImGuiCol_Separator] = col(p.border);
  c[ImGuiCol_SeparatorHovered] = col(p.coffeeLight);
  c[ImGuiCol_SeparatorActive] = col(p.coffee);

  c[ImGuiCol_ResizeGrip] = col(p.borderDark);
  c[ImGuiCol_ResizeGripHovered] = col(p.coffeeLight);
  c[ImGuiCol_ResizeGripActive] = col(p.coffee);

  c[ImGuiCol_Tab] = col(p.panel2);
  c[ImGuiCol_TabHovered] = col(p.panel3);
  c[ImGuiCol_TabSelected] = col(p.content);
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
  c[ImGuiCol_TableRowBgAlt] =
      ImVec4(p.ink.r / 255.0f, p.ink.g / 255.0f, p.ink.b / 255.0f, 0.02f);

  c[ImGuiCol_TextSelectedBg] =
      ImVec4(p.coffee.r / 255.0f, p.coffee.g / 255.0f, p.coffee.b / 255.0f, 0.20f);
  c[ImGuiCol_NavCursor] = col(p.coffee);
  c[ImGuiCol_ModalWindowDimBg] =
      ImVec4(p.ink.r / 255.0f, p.ink.g / 255.0f, p.ink.b / 255.0f, 0.35f);
}

}  // namespace ui
}  // namespace ship
