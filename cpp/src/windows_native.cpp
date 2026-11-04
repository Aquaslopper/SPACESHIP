#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Common-dialog declarations require Windows types before this header.
#include <commdlg.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

// GDI+ depends on the COM types declared above; keep this include after objidl.h.
#include <gdiplus.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "runtime_files.hpp"
#include "voyage.hpp"
#include "windows_audio.hpp"

namespace {
using spaceship::Choice;
using spaceship::Control;
using spaceship::Json;
using spaceship::Voyage;
constexpr COLORREF background = RGB(255, 255, 255), panel = RGB(250, 247, 242),
                   ink = RGB(59, 54, 48), muted = RGB(138, 127, 114), accent = RGB(140, 111, 78);
constexpr int idWatch = 101, idNew = 102, idSave = 103, idLoad = 104, idSlot = 105, idList = 106,
              idCanvas = 107;
bool titleVisible = true, hasVoyage = false;
ULONG_PTR graphicsToken = 0;
IStream* titleStream = nullptr;
std::unique_ptr<Gdiplus::Bitmap> titleArtwork;
std::vector<HWND> titleButtons, tabButtons;
std::vector<std::string> openTabs = {"home"};
int tabStart = 0;
RECT titleRect{};
void refresh(bool rebuildList);
void showTitle(bool visible);
Voyage game;
spaceship::RuntimeFiles runtimeFiles;
spaceship::WavePlayer musicPlayer, effectPlayer;
bool audioStarted = false, smokeMode = false;
HWND cooldownBox;
HWND commandPane;
int commandScroll = 0;
HWND mainWindow, summaryBox, detailBox, feedBox, listBox, canvas, slotBox, statusLabel, watchButton;
HFONT font, smallFont, titleFont;
HBRUSH backgroundBrush, panelBrush;
std::string page = "home", selected;
std::vector<Choice> choices;
std::vector<Control> controls;
std::vector<HWND> navButtons, actionButtons;
std::vector<std::pair<RECT, std::string>> hitRegions;
const std::vector<std::pair<std::string, std::wstring>> pages = {{"home", L"Ship / deck plan"},
                                                                 {"navigation", L"Navigation"},
                                                                 {"crew", L"Crew quarters"},
                                                                 {"cargo", L"Cargo hold"},
                                                                 {"station", L"Station services"},
                                                                 {"simulacron", L"Simulacron Core"},
                                                                 {"events", L"Incident reports"},
                                                                 {"rewards", L"Captain's log"},
                                                                 {"guide", L"Handbook"}};
std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L' ');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}
std::wstring lines(const std::string& s) {
  std::wstring out;
  for (auto c : wide(s)) {
    if (c == L'\n') out += L'\r';
    out += c;
  }
  return out;
}
void syncAudio() {
  if (smokeMode) return;
  const auto& options = game.snapshot()["state"]["options"];
  const double level = options.value("audioLevel", 0.4);
  if (audioStarted && options.value("music", true) && level > 0) {
    if (!musicPlayer.playing()) musicPlayer.play(runtimeFiles.music / L"ambient.wav", true, level);
    musicPlayer.volume(level);
  } else
    musicPlayer.stop();
  if (!options.value("soundEffects", true) || level == 0) effectPlayer.stop();
}
void playCue(const std::string& command) {
  if (smokeMode) return;
  const auto& options = game.snapshot()["state"]["options"];
  if (!options.value("soundEffects", true) || options.value("audioLevel", 0.4) <= 0) return;
  auto name = command == "travel" || command == "jump" ? L"travel.wav"
              : command == "scan"                      ? L"scan.wav"
              : command == "acknowledge"               ? L"complete.wav"
                                                       : L"click.wav";
  effectPlayer.play(runtimeFiles.sounds / name, false, options.value("audioLevel", 0.4));
}
HWND widget(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
  HWND h = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
                           WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, mainWindow,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return h;
}
void label(HDC dc, RECT r, const std::wstring& value, COLORREF color, HFONT f = nullptr,
           UINT format = DT_LEFT | DT_TOP | DT_WORDBREAK) {
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  auto old = SelectObject(dc, f ? f : font);
  DrawTextW(dc, value.c_str(), -1, &r, format);
  SelectObject(dc, old);
}
void fill(HDC dc, RECT r, COLORREF color) {
  HBRUSH b = CreateSolidBrush(color);
  FillRect(dc, &r, b);
  DeleteObject(b);
}
void layout() {
  RECT r;
  GetClientRect(mainWindow, &r);
  int w = r.right, h = r.bottom;
  for (HWND child = GetWindow(mainWindow, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
    bool titleControl =
        std::find(titleButtons.begin(), titleButtons.end(), child) != titleButtons.end();
    ShowWindow(child, (titleVisible == titleControl) ? SW_SHOW : SW_HIDE);
  }
  if (titleVisible) {
    const double ratio = 1672.0 / 941.0;
    int cw = std::min(w - 24, 1672), ch = static_cast<int>(cw / ratio);
    if (ch > h - 24) {
      ch = h - 24;
      cw = static_cast<int>(ch * ratio);
    }
    titleRect = {(w - cw) / 2, (h - ch) / 2, (w + cw) / 2, (h + ch) / 2};
    for (size_t i = 0; i < titleButtons.size(); ++i)
      MoveWindow(titleButtons[i],
                 titleRect.left + static_cast<int>(cw * .02) + static_cast<int>(i * cw * .818 / 5),
                 titleRect.bottom - static_cast<int>(ch * .097), static_cast<int>(cw * .818 / 5),
                 std::max(26, static_cast<int>(ch * .0565)), TRUE);
    SetWindowTextW(titleButtons.back(), hasVoyage ? L"RESUME" : L"EXIT");
    InvalidateRect(mainWindow, nullptr, TRUE);
    return;
  }
  const int left = 164, top = 112, bottom = 24, right = 310;
  int y = 44;
  for (auto b : navButtons) {
    MoveWindow(b, 8, y, left - 16, 29, TRUE);
    y += 35;
  }
  const int sideHeight = std::max(120, h - y - 150);
  MoveWindow(summaryBox, 8, y + 8, left - 16, sideHeight / 2, TRUE);
  MoveWindow(cooldownBox, 8, y + 12 + sideHeight / 2, left - 16, sideHeight / 2 - 4, TRUE);
  MoveWindow(GetDlgItem(mainWindow, idNew), 8, h - 122, 70, 28, TRUE);
  MoveWindow(GetDlgItem(mainWindow, 993), 84, h - 122, 70, 28, TRUE);
  MoveWindow(slotBox, 8, h - 88, 65, 160, TRUE);
  MoveWindow(GetDlgItem(mainWindow, idSave), 77, h - 88, 36, 28, TRUE);
  MoveWindow(GetDlgItem(mainWindow, idLoad), 116, h - 88, 40, 28, TRUE);
  MoveWindow(watchButton, 8, h - 52, left - 16, 26, TRUE);
  for (size_t i = 0; i < tabButtons.size(); ++i) {
    bool visible = i >= static_cast<size_t>(tabStart) && i < static_cast<size_t>(tabStart + 6);
    ShowWindow(tabButtons[i], visible ? SW_SHOW : SW_HIDE);
    if (visible)
      MoveWindow(tabButtons[i], 8 + static_cast<int>(i - tabStart) * 154, 5, 150, 28, TRUE);
  }
  MoveWindow(GetDlgItem(mainWindow, 990), w - 116, 5, 30, 28, TRUE);
  MoveWindow(GetDlgItem(mainWindow, 991), w - 82, 5, 30, 28, TRUE);
  MoveWindow(GetDlgItem(mainWindow, 992), w - 48, 5, 42, 28, TRUE);
  const bool home = page == "home";
  const int contentW = w - left - 24 - (home ? 0 : right);
  int canvasH = home ? std::max(300, std::min(440, h - 380)) : 200;
  ShowWindow(canvas, SW_SHOW);
  MoveWindow(canvas, left + 12, top, contentW, canvasH, TRUE);
  ShowWindow(listBox, home ? SW_HIDE : SW_SHOW);
  ShowWindow(commandPane, home ? SW_HIDE : SW_SHOW);
  if (home) {
    MoveWindow(detailBox, left + 12, top + canvasH + 8, contentW, 94, TRUE);
    MoveWindow(feedBox, left + 12, top + canvasH + 114, contentW,
               std::max(80, h - top - canvasH - 144), TRUE);
  } else {
    const int cy = top + canvasH + 12, lw = std::min(230, contentW / 3);
    MoveWindow(listBox, left + 12, cy, lw, std::max(100, h - cy - 155), TRUE);
    MoveWindow(detailBox, left + lw + 24, cy, contentW - lw - 12, std::max(100, h - cy - 155),
               TRUE);
    MoveWindow(feedBox, left + 12, h - 140, contentW, 108, TRUE);
  }
  const int ph = h - top - bottom;
  MoveWindow(commandPane, w - right, top, right - 12, ph, TRUE);
  SCROLLINFO info{sizeof(SCROLLINFO),
                  SIF_RANGE | SIF_PAGE | SIF_POS,
                  0,
                  std::max(0, static_cast<int>(actionButtons.size()) * 42 - 1),
                  static_cast<UINT>(ph),
                  commandScroll,
                  0};
  SetScrollInfo(commandPane, SB_VERT, &info, TRUE);
  GetScrollInfo(commandPane, SB_VERT, &info);
  commandScroll = info.nPos;
  y = -commandScroll;
  for (auto b : actionButtons) {
    MoveWindow(b, 0, y, right - 36, 38, TRUE);
    y += 42;
  }
  MoveWindow(statusLabel, 8, h - 22, w - 16, 20, TRUE);
  InvalidateRect(mainWindow, nullptr, TRUE);
  InvalidateRect(canvas, nullptr, TRUE);
}
void showTitle(bool visible) {
  titleVisible = visible;
  layout();
}

void refresh(bool rebuildList = true) {
  if (rebuildList) {
    choices = game.choices(page);
    SendMessageW(listBox, LB_RESETCONTENT, 0, 0);
    int index = 0, found = -1;
    for (const auto& c : choices) {
      auto value = wide(c.label);
      SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
      if (c.id == selected) found = index;
      ++index;
    }
    if (found < 0 && !choices.empty() && page != "home") {
      found = 0;
      selected = choices[0].id;
    }
    if (choices.empty()) selected.clear();
    if (found >= 0) SendMessageW(listBox, LB_SETCURSEL, found, 0);
  }
  auto summary = game.summary();
  const auto cooldownStart = summary.find("CONTROL COOLDOWNS");
  if (cooldownStart != std::string::npos) summary.erase(cooldownStart);
  const auto loss = game.snapshot()["state"].value("gameOver", std::string{});
  if (!loss.empty()) summary += "\nVOYAGE LOST: " + loss;
  SetWindowTextW(summaryBox, lines(summary).c_str());
  SetWindowTextW(cooldownBox, lines(game.cooldownReport()).c_str());
  auto detail = page == "home" && !selected.empty() ? game.describe(selected, "")
                                                    : game.describe(page, selected);
  if (page == "home" && selected.empty())
    detail = "NAVIGATION\r\nHolding at " + game.location() +
             ". Left-click a room to read; right-click opens its task window.\r\nReading is free. "
             "Check life support before your next crossing.";
  SetWindowTextW(detailBox, lines(detail).c_str());
  std::string feed = "SHIP'S LOG\n";
  const auto& logs = game.snapshot()["state"]["feed"];
  size_t start = logs.size() > 8 ? logs.size() - 8 : 0;
  for (size_t i = start; i < logs.size(); ++i) feed += logs[i].get<std::string>() + "\n";
  SetWindowTextW(feedBox, lines(feed).c_str());
  SetWindowTextW(
      statusLabel,
      wide(game.message().empty()
               ? "Native C++ migration build | Reading is free. Stand watch advances one action."
               : game.message())
          .c_str());
  for (auto b : actionButtons) DestroyWindow(b);
  actionButtons.clear();
  controls = game.controls(page, selected);
  if (page == "home")
    actionButtons.push_back(
        widget(L"BUTTON", L"Open selected compartment", BS_PUSHBUTTON | WS_TABSTOP, 490));
  for (size_t i = 0; i < controls.size(); ++i) {
    auto name = controls[i].label;
    if (!controls[i].enabled) name = "[unavailable] " + name;
    auto b = widget(L"BUTTON", wide(name).c_str(), BS_PUSHBUTTON | BS_MULTILINE | WS_TABSTOP,
                    500 + static_cast<int>(i));
    EnableWindow(b, controls[i].enabled);
    actionButtons.push_back(b);
  }
  if (page == "home" || page == "guide") {
    actionButtons.push_back(
        widget(L"BUTTON", L"Import HTA save (.txt)", BS_PUSHBUTTON | WS_TABSTOP, 489));
    actionButtons.push_back(
        widget(L"BUTTON", L"Open saves folder", BS_PUSHBUTTON | WS_TABSTOP, 488));
  }
  for (auto button : actionButtons) SetParent(button, commandPane);
  for (size_t i = 0; i < navButtons.size(); ++i)
    SendMessageW(navButtons[i], BM_SETSTATE, page == pages[i].first, 0);
  Voyage probe = game;
  EnableWindow(watchButton, probe.act("watch"));
  const int wait = game.controlCooldown("watch");
  SetWindowTextW(watchButton, wait ? (L"Watch [CD: " + std::to_wstring(wait) + L" clicks]").c_str()
                                   : L"Stand watch (+1)");
  layout();
}
std::filesystem::path savePath(bool loading = false) {
  const int slot = static_cast<int>(SendMessageW(slotBox, CB_GETCURSEL, 0, 0)) + 1;
  return runtimeFiles.slot(slot, loading);
}

bool confirm(const wchar_t* question) {
  return MessageBoxW(mainWindow, question, L"Spaceship Command",
                     MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}
void pickPage(const std::string& value) {
  if (std::find(openTabs.begin(), openTabs.end(), value) == openTabs.end()) {
    openTabs.push_back(value);
    std::string caption = value;
    for (const auto& room : game.snapshot()["rooms"])
      if (room.value("id", std::string{}) == value) caption = room.value("name", value);
    tabButtons.push_back(widget(L"BUTTON", wide(caption).c_str(), BS_PUSHBUTTON | WS_TABSTOP,
                                1000 + static_cast<int>(openTabs.size() - 1)));
    tabStart = std::max(0, static_cast<int>(openTabs.size()) - 6);
  }

  page = value;
  commandScroll = 0;
  selected.clear();
  refresh();
}
LRESULT CALLBACK canvasProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) {
    POINT p{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
    for (const auto& region : hitRegions)
      if (PtInRect(&region.first, p)) {
        if (page == "home") {
          if (msg == WM_RBUTTONUP)
            pickPage(region.second);
          else {
            selected = region.second;
            refresh(false);
          }
        } else {
          selected = region.second;
          refresh();
        }
        return 0;
      }
    return 0;
  }
  if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
    PAINTSTRUCT ps;
    HDC dc = msg == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wp) : BeginPaint(h, &ps);
    RECT r;
    GetClientRect(h, &r);
    fill(dc, r, background);
    hitRegions.clear();
    if (page != "home")
      label(dc, {16, 12, r.right - 16, 44},
            page == "home"
                ? wide(game.snapshot()["ship"]["name"].get<std::string>()) + L" / COMPARTMENT PLAN"
                : L"CHART / SELECT A DESTINATION",
            accent, smallFont);
    if (page == "home") {
      const bool compactShip = game.snapshot()["state"].value("shipClass", std::string{}) == "wren";
      const double scale = std::min((r.right - 30.0) / (compactShip ? 600.0 : 1160.0),
                                    (r.bottom - 30.0) / (compactShip ? 365.0 : 455.0));
      const int ox = static_cast<int>((r.right - (compactShip ? 600 : 1160) * scale) / 2), oy = 10;
      auto rect = [&](double x, double y, double w, double hh) {
        return RECT{ox + static_cast<LONG>(x * scale), oy + static_cast<LONG>(y * scale),
                    ox + static_cast<LONG>((x + w) * scale),
                    oy + static_cast<LONG>((y + hh) * scale)};
      };
      auto color = [](const std::string& text) {
        unsigned n = 0;
        try {
          n = std::stoul(text.substr(1), nullptr, 16);
        } catch (...) {
        }
        return RGB((n >> 16) & 255, (n >> 8) & 255, n & 255);
      };
      auto pen = CreatePen(PS_SOLID, 1, accent);
      auto op = SelectObject(dc, pen);
      auto ob = SelectObject(dc, panelBrush);
      const auto& visual = Voyage::catalog()["visual"];
      for (const auto& hull : visual[compactShip ? "smallHull" : "largeHull"]) {
        RECT box = rect(hull["x"], hull["y"], hull["w"], hull["h"]);
        auto cls = hull.value("cls", std::string{});
        if (cls == "hp-lens")
          Ellipse(dc, box.left, box.top, box.right, box.bottom);
        else
          RoundRect(dc, box.left, box.top, box.right, box.bottom, cls == "hp-nacelle" ? 18 : 8,
                    cls == "hp-nacelle" ? 18 : 8);
      }
      const auto& cor = visual[compactShip ? "smallCorridor" : "largeCorridor"];
      auto corridor = rect(cor["x"], cor["y"], cor["w"], cor["h"]);
      fill(dc, corridor, RGB(243, 239, 230));
      label(dc, corridor, L"  CORRIDOR", muted, smallFont);
      for (const auto& room : game.snapshot()["rooms"]) {
        const auto id = room.value("id", std::string{});
        const auto& scene = Voyage::catalog()["scenes"][id];
        auto tone = color(room.value("tone", std::string("#faf7f2"))),
             tint = color(scene.value("accent", std::string("#8c6f4e")));
        RECT box = rect(room["x"], room["y"], room["w"], room["h"]);
        fill(dc, box, tone);
        FrameRect(dc, &box, panelBrush);
        fill(dc, {box.left, box.top, box.left + 3, box.bottom}, tint);
        label(dc, {box.left + 6, box.top + 4, box.right - 3, box.top + 30},
              wide(room.value("name", id)), ink, smallFont);
        if (!room.value("small", false) && box.bottom - box.top > 48) {
          RECT glyph{box.left + 10, box.bottom - 29, box.right - 10, box.bottom - 8};
          FrameRect(dc, &glyph, panelBrush);
          auto brush = CreateSolidBrush(tint);
          SelectObject(dc, brush);
          auto type = scene.value("glyph", std::string{});
          if (type == "filter" || type == "garden") {
            for (int j = 0; j < 3; ++j)
              Rectangle(dc, glyph.left + 5 + j * 9, glyph.top + 4, glyph.left + 11 + j * 9,
                        glyph.bottom - 3);
          } else if (type == "cargo" || type == "habitat")
            Rectangle(dc, glyph.left + 6, glyph.top + 5, glyph.left + 23, glyph.bottom - 3);
          else
            Ellipse(dc, glyph.left + 5, glyph.top + 3, glyph.left + 24, glyph.bottom - 2);
          SelectObject(dc, panelBrush);
          DeleteObject(brush);
          for (int j = 0; j < 3; ++j) {
            MoveToEx(dc, glyph.right - 22, glyph.top + 5 + j * 5, nullptr);
            LineTo(dc, glyph.right - 4, glyph.top + 5 + j * 5);
          }
        }
        hitRegions.push_back({box, id});
      }
      for (const auto& person : game.snapshot()["crew"])
        for (const auto& room : game.snapshot()["rooms"])
          if (person.value("room", std::string{}) == room.value("id", std::string{})) {
            auto dot = rect(room["x"].get<int>() + room["w"].get<int>() / 2,
                            room["y"].get<int>() + room["h"].get<int>() - 6, 8, 8);
            auto b = CreateSolidBrush(color(person.value("colour", std::string("#829377"))));
            SelectObject(dc, b);
            Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
            SelectObject(dc, panelBrush);
            DeleteObject(b);
            auto name = person.value("name", std::string{});
            name = name.substr(0, name.find(' '));
            label(dc, {dot.left - 12, dot.bottom, dot.left + 48, dot.bottom + 14}, wide(name),
                  muted, smallFont);
          }
      SelectObject(dc, ob);
      SelectObject(dc, op);
      DeleteObject(pen);
    } else if (Voyage::catalog()["scenes"].contains(page)) {
      const auto& scene = Voyage::catalog()["scenes"][page];
      const std::string hex = scene.value("accent", std::string("#8c6f4e"));
      const auto n = std::stoul(hex.substr(1), nullptr, 16);
      const COLORREF tint = RGB((n >> 16) & 255, (n >> 8) & 255, n & 255);
      RECT plate{12, 12, r.right - 12, r.bottom - 12};
      fill(dc, plate, panel);
      FrameRect(dc, &plate, panelBrush);
      auto pen = CreatePen(PS_SOLID, 2, tint);
      auto op = SelectObject(dc, pen);
      auto ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
      Ellipse(dc, 36, 38, 160, 162);
      Ellipse(dc, 50, 52, 146, 148);
      auto brush = CreateSolidBrush(tint);
      SelectObject(dc, brush);
      Ellipse(dc, 76, 78, 120, 122);
      SelectObject(dc, ob);
      DeleteObject(brush);
      SelectObject(dc, op);
      DeleteObject(pen);
      label(dc, {190, 32, r.right - 24, 54}, wide(scene.value("code", std::string{})), muted,
            smallFont);
      label(dc, {190, 59, r.right - 24, 96}, wide(scene.value("label", std::string{})), ink,
            titleFont);
      label(dc, {190, 112, r.right - 24, 160}, wide(scene.value("description", std::string{})), ink,
            font);
      for (int i = 0; i < 8; ++i)
        fill(dc, {190 + i * 20, r.bottom - 36, 202 + i * 20, r.bottom - 27},
             i < 6 ? tint : RGB(224, 219, 209));
    } else {
      const auto& data = Voyage::catalog();
      const Json* chosen = nullptr;
      for (const auto& b : data["bodies"])
        if (b.value("id", std::string{}) == selected) chosen = &b;
      const bool local = chosen != nullptr;
      const auto& list = data[local ? "bodies" : "systems"];
      std::string system = chosen ? chosen->value("system", std::string("sec-kessler")) : "";
      const int chartW = r.right - 40, chartH = r.bottom - 65;
      auto pen = CreatePen(PS_DOT, 1, RGB(43, 65, 78));
      auto oldPen = SelectObject(dc, pen);
      for (int x = 20; x < r.right; x += 45) {
        MoveToEx(dc, x, 45, nullptr);
        LineTo(dc, x, r.bottom - 12);
      }
      for (int y = 45; y < r.bottom; y += 35) {
        MoveToEx(dc, 20, y, nullptr);
        LineTo(dc, r.right - 10, y);
      }
      SelectObject(dc, oldPen);
      DeleteObject(pen);
      for (const auto& b : list) {
        if (local && b.value("system", std::string("sec-kessler")) != system) continue;
        if (!local && selected.rfind("sec-", 0) == 0) {
          const Json* target = nullptr;
          for (const auto& s : data["systems"])
            if (s.value("id", std::string{}) == selected) target = &s;
          if (target && b.value("region", std::string{}) != target->value("region", std::string{}))
            continue;
        }
        int x = 20 + b["x"].get<int>() * chartW / 638, y = 48 + b["y"].get<int>() * chartH / 278;
        RECT dot{x - 5, y - 5, x + 6, y + 6};
        fill(dc, dot, b.value("id", std::string{}) == selected ? accent : RGB(192, 165, 102));
        label(dc, {x + 8, y - 6, std::min<LONG>(x + 150, r.right - 5), y + 28},
              wide(b["name"].get<std::string>()), ink, smallFont);
        hitRegions.push_back(
            {{x - 8, y - 9, std::min<LONG>(x + 145, r.right), y + 26}, b["id"].get<std::string>()});
      }
    }
    if (msg == WM_PAINT) EndPaint(h, &ps);
    return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
INT_PTR CALLBACK menuDialog(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_INITDIALOG) {
    SetWindowLongPtrW(h, GWLP_USERDATA, lp);
    if (lp == 202) {
      for (auto text : {L"Slot 1", L"Slot 2", L"Slot 3"})
        SendDlgItemMessageW(h, 110, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
      SendDlgItemMessageW(h, 110, CB_SETCURSEL, SendMessageW(slotBox, CB_GETCURSEL, 0, 0), 0);
    } else {
      const auto& o = game.snapshot()["state"]["options"];
      CheckDlgButton(h, 111, o.value("music", true) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(h, 112, o.value("soundEffects", true) ? BST_CHECKED : BST_UNCHECKED);
      for (auto text : {L"0%", L"25%", L"40%", L"50%", L"75%", L"100%"})
        SendDlgItemMessageW(h, 113, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
      const double level = o.value("audioLevel", .4);
      int sel = level == 0     ? 0
                : level == .25 ? 1
                : level == .4  ? 2
                : level == .5  ? 3
                : level == .75 ? 4
                               : 5;
      SendDlgItemMessageW(h, 113, CB_SETCURSEL, sel, 0);
    }
    return TRUE;
  }
  if (msg == WM_COMMAND) {
    if (LOWORD(wp) == IDCANCEL) {
      EndDialog(h, IDCANCEL);
      return TRUE;
    }
    if (LOWORD(wp) == IDOK) {
      if (GetWindowLongPtrW(h, GWLP_USERDATA) == 202)
        SendMessageW(slotBox, CB_SETCURSEL, SendDlgItemMessageW(h, 110, CB_GETCURSEL, 0, 0), 0);
      else {
        const auto options = game.snapshot()["state"]["options"];
        if ((IsDlgButtonChecked(h, 111) == BST_CHECKED) != options.value("music", true))
          game.act("toggle-music");
        if ((IsDlgButtonChecked(h, 112) == BST_CHECKED) != options.value("soundEffects", true))
          game.act("toggle-sounds");
        const int levels[] = {0, 25, 40, 50, 75, 100};
        int sel = static_cast<int>(SendDlgItemMessageW(h, 113, CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < 6) game.act("audio-level", std::to_string(levels[sel]));
        syncAudio();
      }
      EndDialog(h, IDOK);
      return TRUE;
    }
  }
  return FALSE;
}
LRESULT CALLBACK commandsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_COMMAND || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN)
    return SendMessageW(mainWindow, msg, wp, lp);
  if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL) {
    SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
    GetScrollInfo(h, SB_VERT, &info);
    int value = info.nPos;
    if (msg == WM_MOUSEWHEEL)
      value -= static_cast<short>(HIWORD(wp)) / WHEEL_DELTA * 74;
    else
      switch (LOWORD(wp)) {
        case SB_LINEUP:
          value -= 37;
          break;
        case SB_LINEDOWN:
          value += 37;
          break;
        case SB_PAGEUP:
          value -= static_cast<int>(info.nPage);
          break;
        case SB_PAGEDOWN:
          value += static_cast<int>(info.nPage);
          break;
        case SB_THUMBTRACK:
          value = info.nTrackPos;
          break;
      }
    commandScroll = std::clamp(value, 0, std::max(0, info.nMax - static_cast<int>(info.nPage) + 1));
    layout();
    return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
LRESULT CALLBACK windowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      mainWindow = h;
      font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
      smallFont = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI");
      titleFont = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI");
      widget(L"BUTTON", L"New voyage", BS_PUSHBUTTON | WS_TABSTOP, idNew);
      slotBox = widget(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, idSlot);
      for (auto name : {L"Slot 1", L"Slot 2", L"Slot 3"})
        SendMessageW(slotBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
      SendMessageW(slotBox, CB_SETCURSEL, 0, 0);
      widget(L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, idSave);
      widget(L"BUTTON", L"Load", BS_PUSHBUTTON | WS_TABSTOP, idLoad);
      watchButton = widget(L"BUTTON", L"Stand watch (+1)", BS_PUSHBUTTON | WS_TABSTOP, idWatch);
      for (size_t i = 0; i < pages.size(); ++i)
        navButtons.push_back(widget(L"BUTTON", pages[i].second.c_str(), BS_PUSHBUTTON | WS_TABSTOP,
                                    200 + static_cast<int>(i)));
      summaryBox = widget(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
      cooldownBox = widget(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
      detailBox = widget(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
      feedBox = widget(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      listBox =
          widget(L"LISTBOX", L"",
                 LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, idList);
      SendMessageW(listBox, LB_SETHORIZONTALEXTENT, 700, 0);
      statusLabel = widget(L"STATIC", L"", SS_LEFT);
      commandPane = widget(L"SpaceshipCommands", L"", WS_VSCROLL);
      SetWindowLongPtrW(commandPane, GWL_EXSTYLE,
                        GetWindowLongPtrW(commandPane, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
      canvas = widget(L"SpaceshipChart", L"", 0, idCanvas);
      widget(L"BUTTON", L"Menu", BS_PUSHBUTTON | WS_TABSTOP, 993);
      widget(L"BUTTON", L"<", BS_PUSHBUTTON | WS_TABSTOP, 990);
      widget(L"BUTTON", L">", BS_PUSHBUTTON | WS_TABSTOP, 991);
      widget(L"BUTTON", L"Home", BS_PUSHBUTTON | WS_TABSTOP, 992);
      tabButtons.push_back(widget(L"BUTTON", L"SHIP  SC Wren", BS_PUSHBUTTON | WS_TABSTOP, 1000));
      for (auto text : {L"NEW VOYAGE", L"LOAD VOYAGE", L"OPTIONS", L"HOW TO PLAY", L"EXIT"})
        titleButtons.push_back(widget(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP,
                                      900 + static_cast<int>(titleButtons.size())));
      refresh();
      return 0;
    }
    case WM_DRAWITEM: {
      auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
      if (item->CtlID >= 900 && item->CtlID <= 904) {
        const bool focus = (item->itemState & (ODS_SELECTED | ODS_FOCUS)) != 0;
        fill(item->hDC, item->rcItem, focus ? RGB(107, 84, 63) : RGB(56, 47, 35));
        wchar_t text[64]{};
        GetWindowTextW(item->hwndItem, text, 64);
        label(item->hDC, item->rcItem, text, RGB(242, 229, 204), font,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (focus) DrawFocusRect(item->hDC, &item->rcItem);
        return TRUE;
      }
      break;
    }
    case WM_GETMINMAXINFO: {
      auto info = reinterpret_cast<MINMAXINFO*>(lp);
      info->ptMinTrackSize = {1220, 850};
      return 0;
    }
    case WM_SIZE:
      if (summaryBox) layout();
      return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      auto dc = reinterpret_cast<HDC>(wp);
      SetTextColor(dc, ink);
      SetBkColor(dc, panel);
      return reinterpret_cast<LRESULT>(panelBrush);
    }
    case WM_PRINTCLIENT:
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = msg == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wp) : BeginPaint(h, &ps);
      RECT r;
      GetClientRect(h, &r);
      fill(dc, r, background);
      if (titleVisible) {
        fill(dc, r, RGB(231, 222, 203));
        if (titleArtwork) {
          Gdiplus::Graphics graphics(dc);
          graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
          graphics.DrawImage(titleArtwork.get(), static_cast<INT>(titleRect.left),
                             static_cast<INT>(titleRect.top),
                             static_cast<INT>(titleRect.right - titleRect.left),
                             static_cast<INT>(titleRect.bottom - titleRect.top));
        } else
          label(dc, {40, 60, r.right - 40, 160}, L"SPACESHIP", accent, titleFont, DT_CENTER);
      } else {
        fill(dc, {0, 0, r.right, 38}, RGB(243, 239, 230));
        fill(dc, {0, 38, 164, r.bottom}, panel);
        const wchar_t* names[] = {L"HULL INTEGRITY", L"SHIELDS", L"ENERGY", L"FUEL"};
        const char* keys[] = {"hull", "shield", "energy", "fuel"};
        const int width = (r.right - 188) / 4;
        for (int i = 0; i < 4; ++i) {
          RECT card{176 + i * width, 48, 168 + (i + 1) * width, 102};
          fill(dc, card, panel);
          FrameRect(dc, &card, panelBrush);
          label(dc, {card.left + 10, 54, card.right - 8, 68}, names[i], muted, smallFont);
          label(dc, {card.left + 10, 69, card.right - 8, 88},
                std::to_wstring(game.stat(keys[i])) + L"%", ink, font);
          fill(dc, {card.left + 10, 93, card.right - 10, 97}, RGB(232, 226, 214));
          fill(dc,
               {card.left + 10, 93, card.left + 10 + (width - 38) * game.stat(keys[i]) / 100, 97},
               i == 1 ? RGB(183, 139, 70) : RGB(129, 142, 100));
        }
      }
      if (msg == WM_PAINT) EndPaint(h, &ps);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
      // Only activation notifications can commit actions, never focus changes.
      if (id != idList && HIWORD(wp) != BN_CLICKED) return 0;
      if (id == 993) {
        showTitle(true);
        return 0;
      }
      if (id == 990 || id == 991) {
        tabStart = std::clamp(tabStart + (id == 990 ? -1 : 1), 0,
                              std::max(0, static_cast<int>(openTabs.size()) - 6));
        layout();
        return 0;
      }
      if (id == 992) {
        pickPage("home");
        return 0;
      }
      if (id >= 1000 && id < 1000 + static_cast<int>(openTabs.size())) {
        pickPage(openTabs[id - 1000]);
        return 0;
      }
      if (id >= 900 && id <= 904) {
        if (id == 900) {
          if (hasVoyage && !confirm(L"Start a new voyage? Unsaved progress will be lost."))
            return 0;
          game = Voyage();
          hasVoyage = true;
          titleVisible = false;
          pickPage("home");
        } else if (id == 901) {
          if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(202), h, menuDialog,
                              202) != IDOK)
            return 0;
          if (hasVoyage && !confirm(L"Load this slot? Unsaved progress will be lost.")) return 0;
          if (game.load(savePath(true))) {
            hasVoyage = true;
            titleVisible = false;
            pickPage("home");
          } else
            MessageBoxW(h, wide(game.message()).c_str(), L"Load voyage", MB_ICONINFORMATION);
        } else if (id == 902) {
          DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(203), h, menuDialog, 203);
        } else if (id == 903) {
          MessageBoxW(
              h,
              L"Start aboard SC Wren. Reading, inspecting rooms and choosing targets are "
              L"free.\n\nLeft-click a compartment to read about it; right-click to open its task "
              L"tab. Seat three O2 canisters and start the filter bank. Start a crossing to Tolme "
              L"Anchorage and tend useful jobs while work progresses. Acknowledge completed "
              L"readouts for free.\n\nEvery committed gameplay action advances the whole ship. "
              L"Watch and other controls have action-count cooldowns, not timers.",
              L"Your first watch", MB_OK | MB_ICONINFORMATION);
        } else if (hasVoyage)
          showTitle(false);
        else
          DestroyWindow(h);
        audioStarted = true;
        syncAudio();
        return 0;
      }
      audioStarted = true;
      syncAudio();
      const int actionsBefore = game.actions();
      std::string cue = "click";
      if (id >= 200 && id < 200 + static_cast<int>(pages.size())) {
        pickPage(pages[id - 200].first);
        return 0;
      }
      if (id == 490 || (id == idList && HIWORD(wp) == LBN_DBLCLK && page == "home")) {
        if (!selected.empty()) pickPage(selected);
        return 0;
      }
      if (id == idList && HIWORD(wp) == LBN_SELCHANGE) {
        int i = static_cast<int>(SendMessageW(listBox, LB_GETCURSEL, 0, 0));
        if (i >= 0 && i < static_cast<int>(choices.size())) {
          selected = choices[i].id;
          refresh(false);
        }
        return 0;
      }
      try {
        if (id == 488) {
          if (reinterpret_cast<INT_PTR>(ShellExecuteW(h, L"open", runtimeFiles.saves.c_str(),
                                                      nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            MessageBoxW(h, runtimeFiles.saves.c_str(), L"Save folder", MB_OK);
          return 0;
        }
        if (id == 489) {
          if (!confirm(L"Import an HTA voyage? Unsaved native progress will be replaced. The "
                       L"source file will not be modified."))
            return 0;
          wchar_t filename[32768]{};
          OPENFILENAMEW dialog{};
          dialog.lStructSize = sizeof(dialog);
          dialog.hwndOwner = h;
          dialog.lpstrFile = filename;
          dialog.nMaxFile = 32768;
          dialog.lpstrFilter = L"Spaceship saves (*.txt;*.json)\0*.txt;*.json\0All files\0*.*\0";
          dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
          if (!GetOpenFileNameW(&dialog)) return 0;
          if (!game.load(std::filesystem::path(filename)))
            MessageBoxW(h, wide(game.message()).c_str(), L"Import refused; live voyage unchanged",
                        MB_ICONERROR);
          else {
            page = "home";
            selected.clear();
          }
        } else if (id == idWatch)
          game.act("watch");
        else if (id == idNew) {
          if (!confirm(
                  L"Start a new voyage? Unsaved progress will be lost. Save slots will be kept."))
            return 0;
          game = Voyage();
          page = "home";
          selected.clear();
        } else if (id == idSave) {
          auto path = savePath();
          if (std::filesystem::exists(path) &&
              !confirm(L"Replace this native save slot? A .bak backup will be retained."))
            return 0;
          if (!game.save(path))
            MessageBoxW(h, L"Could not save. Your existing slot may be available as a .bak backup.",
                        L"Save failed", MB_ICONERROR);
          else
            MessageBoxW(h, path.c_str(), L"Voyage saved", MB_OK);
        } else if (id == idLoad) {
          if (!confirm(L"Load this slot? Unsaved progress will be replaced.")) return 0;
          if (!game.load(savePath(true)))
            MessageBoxW(h, wide(game.message()).c_str(), L"Load failed", MB_ICONERROR);
        } else if (id >= 500 && id < 500 + static_cast<int>(controls.size())) {
          auto command = controls[id - 500];
          if (!command.enabled) return 0;
          if ((command.command == "jettison" || command.command == "preserve" ||
               command.command == "sell-signal" || command.command == "abandon" ||
               command.command == "ship") &&
              !confirm(
                  L"Commit this consequential action? It cannot be undone without loading a save."))
            return 0;
          cue = command.command;
          if (game.act(command.command, command.argument) && command.command == "acknowledge")
            playCue(command.command);
        } else
          return 0;
        syncAudio();
        if (game.actions() > actionsBefore) playCue(cue);
        refresh();
      } catch (const std::exception& e) {
        MessageBoxW(h, wide(e.what()).c_str(), L"Spaceship error", MB_ICONERROR);
      }
      return 0;
    }
    case WM_CLOSE:
      if (confirm(L"Exit? Unsaved progress will be lost.")) DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      musicPlayer.stop();
      effectPlayer.stop();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
bool capture(const wchar_t* name) {
  RECT r;
  GetClientRect(mainWindow, &r);
  auto dc = GetDC(mainWindow), memory = CreateCompatibleDC(dc);
  auto bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
  auto old = SelectObject(memory, bitmap);
  RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  const BOOL painted = PrintWindow(mainWindow, memory, PW_CLIENTONLY);
  SelectObject(memory, old);
  auto image = std::make_unique<Gdiplus::Bitmap>(bitmap, nullptr);
  UINT count = 0, size = 0;
  Gdiplus::GetImageEncodersSize(&count, &size);
  std::vector<BYTE> storage(size);
  auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
  Gdiplus::GetImageEncoders(count, size, encoders);
  bool saved = false;
  std::filesystem::create_directories(L"visual-captures");
  auto path = std::filesystem::path(L"visual-captures") / (std::wstring(name) + L".png");
  for (UINT i = 0; i < count; ++i)
    if (std::wstring(encoders[i].MimeType) == L"image/png")
      saved = painted && image->Save(path.c_str(), &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
  image.reset();
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(mainWindow, dc);
  return saved;
}
}  // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR args, int show) {
  const bool captureMode = std::wstring(args) == L"--capture-ui";
  smokeMode = std::wstring(args) == L"--smoke-test" || captureMode;
  try {
    wchar_t exe[32768]{}, user[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe, 32768);
    if (!length || length >= 32768 ||
        FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr,
                                SHGFP_TYPE_CURRENT, user)))
      throw std::runtime_error("Cannot locate game/save folders.");
    runtimeFiles =
        spaceship::RuntimeFiles::prepare(std::filesystem::path(exe).parent_path(),
                                         std::filesystem::path(user) / L"SpaceshipCommand");
  } catch (const std::exception& e) {
    MessageBoxW(nullptr, wide(e.what()).c_str(), L"Startup failed", MB_ICONERROR);
    return 4;
  }

  Gdiplus::GdiplusStartupInput input;
  if (Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr) != Gdiplus::Ok) return 5;
  auto resource = FindResourceW(instance, MAKEINTRESOURCEW(201), RT_RCDATA);
  if (resource) {
    auto data = LoadResource(instance, resource);
    titleStream = SHCreateMemStream(static_cast<const BYTE*>(LockResource(data)),
                                    SizeofResource(instance, resource));
    if (titleStream) {
      titleArtwork.reset(Gdiplus::Bitmap::FromStream(titleStream));
      if (titleArtwork->GetLastStatus() != Gdiplus::Ok) titleArtwork.reset();
    }
  }
  SetProcessDPIAware();
  backgroundBrush = CreateSolidBrush(background);
  panelBrush = CreateSolidBrush(panel);
  WNDCLASSW chart{};
  chart.lpfnWndProc = canvasProc;
  chart.hInstance = instance;
  chart.lpszClassName = L"SpaceshipChart";
  chart.hCursor = LoadCursorW(nullptr, IDC_HAND);
  RegisterClassW(&chart);
  WNDCLASSW commands{};
  commands.lpfnWndProc = commandsProc;
  commands.hInstance = instance;
  commands.lpszClassName = L"SpaceshipCommands";
  commands.hbrBackground = backgroundBrush;
  commands.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&commands);
  WNDCLASSW wc{};
  wc.lpfnWndProc = windowProc;
  wc.hInstance = instance;
  wc.lpszClassName = L"SpaceshipNative";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = backgroundBrush;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  if (!RegisterClassW(&wc)) return 1;
  auto window =
      CreateWindowExW(0, wc.lpszClassName, L"Spaceship Command", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                      CW_USEDEFAULT, 1440, 960, nullptr, nullptr, instance, nullptr);
  if (!window) return 2;
  ShowWindow(window, show);
  UpdateWindow(window);
  if (smokeMode) {
    // Real native HWND creation + controller dispatch, no HTA or external host.
    bool ok =
        titleVisible && titleArtwork && IsWindowVisible(titleButtons[0]) && game.actions() == 0;
    if (captureMode) ok = capture(L"01-title") && ok;
    SendMessageW(window, WM_COMMAND, 900, 0);
    if (captureMode) ok = capture(L"02-deck") && ok;
    ok = ok && !titleVisible && game.actions() == 0;
    SendMessageW(window, WM_COMMAND, 993, 0);
    ok = ok && titleVisible && game.actions() == 0;
    SendMessageW(window, WM_COMMAND, 904, 0);
    ok = ok && !titleVisible && game.actions() == 0;
    ok = ok && IsWindow(canvas) && IsWindow(listBox) && !choices.empty();
    UpdateWindow(canvas);
    if (hitRegions.empty())
      ok = false;
    else {
      const auto room = hitRegions.front();
      const int x = (room.first.left + room.first.right) / 2,
                y = (room.first.top + room.first.bottom) / 2;
      const size_t tabs = openTabs.size();
      SendMessageW(canvas, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
      ok = ok && page == "home" && selected == room.second && openTabs.size() == tabs &&
           game.actions() == 0;
      SendMessageW(canvas, WM_RBUTTONUP, 0, MAKELPARAM(x, y));
      ok = ok && page == room.second && openTabs.size() == tabs + 1 && game.actions() == 0;
      pickPage("home");
    }

    SendMessageW(window, WM_COMMAND, idWatch, 0);
    ok = ok && game.actions() == 1 && !IsWindowEnabled(watchButton) &&
         game.controlCooldown("watch") == 15;
    for (int i = 0; i < 20; ++i) SendMessageW(window, WM_COMMAND, idWatch, 0);
    ok = ok && game.actions() == 1 && GetWindowTextLengthW(cooldownBox) > 0;
    pickPage("navigation");
    ok = ok && choices.size() > 48;
    pickPage("crew");
    ok = ok && choices.size() == 4 && !actionButtons.empty() && IsWindow(commandPane);
    SendMessageW(commandPane, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    ok = ok && game.actions() == 1;
    for (const auto& room : game.snapshot()["rooms"]) {
      const auto id = room.value("id", std::string{});
      pickPage(id);
      ok = ok && game.actions() == 1 && page == id;
      if (captureMode) ok = capture(wide(id).c_str()) && ok;
    }
    DestroyWindow(window);
    DeleteObject(font);
    DeleteObject(smallFont);
    DeleteObject(titleFont);
    DeleteObject(backgroundBrush);
    DeleteObject(panelBrush);
    titleArtwork.reset();
    if (titleStream) titleStream->Release();
    Gdiplus::GdiplusShutdown(graphicsToken);
    return ok ? 0 : 3;
  }
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  DeleteObject(font);
  DeleteObject(smallFont);
  DeleteObject(titleFont);
  DeleteObject(backgroundBrush);
  DeleteObject(panelBrush);
  titleArtwork.reset();
  if (titleStream) titleStream->Release();
  Gdiplus::GdiplusShutdown(graphicsToken);
  return static_cast<int>(msg.wParam);
}
