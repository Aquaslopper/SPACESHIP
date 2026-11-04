#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "voyage.hpp"

namespace {
using spaceship::Choice;
using spaceship::Control;
using spaceship::Json;
using spaceship::Voyage;
constexpr COLORREF background = RGB(12, 21, 31), panel = RGB(20, 34, 47), ink = RGB(220, 232, 235),
                   muted = RGB(144, 168, 180), accent = RGB(103, 216, 192);
constexpr int idWatch = 101, idNew = 102, idSave = 103, idLoad = 104, idSlot = 105, idList = 106,
              idCanvas = 107;
Voyage game;
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
  const int left = 188, right = 350, top = 94, bottom = 30;
  MoveWindow(GetDlgItem(mainWindow, idNew), w - 570, 20, 112, 34, TRUE);
  MoveWindow(slotBox, w - 450, 20, 80, 200, TRUE);
  MoveWindow(GetDlgItem(mainWindow, idSave), w - 360, 20, 85, 34, TRUE);
  MoveWindow(GetDlgItem(mainWindow, idLoad), w - 267, 20, 85, 34, TRUE);
  MoveWindow(watchButton, w - 174, 20, 154, 34, TRUE);
  int y = top;
  for (auto b : navButtons) {
    MoveWindow(b, 16, y, left - 32, 36, TRUE);
    y += 43;
  }
  MoveWindow(summaryBox, 16, y + 18, left - 32, std::max(80, h - y - bottom - 28), TRUE);
  const int contentW = w - left - right - 28, canvasH = page == "home" ? 270
                                                        : page == "navigation" || page == "bridge"
                                                            ? 210
                                                            : 0;
  ShowWindow(canvas, canvasH ? SW_SHOW : SW_HIDE);
  if (canvasH) MoveWindow(canvas, left + 8, top, contentW, canvasH, TRUE);
  int contentY = top + (canvasH ? canvasH + 14 : 0), listW = std::min(265, contentW / 2);
  MoveWindow(listBox, left + 8, contentY, listW, std::max(80, h - contentY - bottom - 164), TRUE);
  MoveWindow(detailBox, left + listW + 20, contentY, contentW - listW - 12,
             std::max(80, h - contentY - bottom - 164), TRUE);
  MoveWindow(feedBox, left + 8, h - bottom - 150, contentW, 140, TRUE);
  y = top;
  for (auto b : actionButtons) {
    MoveWindow(b, w - right + 4, y, right - 24, 32, TRUE);
    y += 37;
  }
  MoveWindow(statusLabel, 16, h - 26, w - 32, 24, TRUE);
  InvalidateRect(mainWindow, nullptr, TRUE);
  InvalidateRect(canvas, nullptr, TRUE);
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
    if (found < 0 && !choices.empty()) {
      found = 0;
      selected = choices[0].id;
    }
    if (choices.empty()) selected.clear();
    if (found >= 0) SendMessageW(listBox, LB_SETCURSEL, found, 0);
  }
  SetWindowTextW(summaryBox, lines(game.summary()).c_str());
  SetWindowTextW(detailBox, lines(game.describe(page, selected)).c_str());
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
  for (size_t i = 0; i < navButtons.size(); ++i)
    SendMessageW(navButtons[i], BM_SETSTATE, page == pages[i].first, 0);
  EnableWindow(watchButton, game.snapshot()["state"].value("gameOver", std::string{}).empty());
  layout();
}
std::filesystem::path savePath() {
  wchar_t folder[MAX_PATH];
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr,
                              SHGFP_TYPE_CURRENT, folder)))
    throw std::runtime_error("Cannot locate the per-user save directory.");
  auto dir = std::filesystem::path(folder) / L"SpaceshipCommand";
  std::filesystem::create_directories(dir);
  int slot = static_cast<int>(SendMessageW(slotBox, CB_GETCURSEL, 0, 0)) + 1;
  return dir / (L"native-slot-" + std::to_wstring(slot) + L".json");
}
bool confirm(const wchar_t* question) {
  return MessageBoxW(mainWindow, question, L"Spaceship Command",
                     MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}
void pickPage(const std::string& value) {
  page = value;
  selected.clear();
  refresh();
}
LRESULT CALLBACK canvasProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) {
    POINT p{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
    for (const auto& region : hitRegions)
      if (PtInRect(&region.first, p)) {
        if (page == "home")
          pickPage(region.second);
        else {
          selected = region.second;
          refresh();
        }
        return 0;
      }
    return 0;
  }
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT r;
    GetClientRect(h, &r);
    fill(dc, r, panel);
    hitRegions.clear();
    label(dc, {16, 12, r.right - 16, 44},
          page == "home"
              ? wide(game.snapshot()["ship"]["name"].get<std::string>()) + L" / COMPARTMENT PLAN"
              : L"CHART / SELECT A DESTINATION",
          accent, smallFont);
    if (page == "home") {
      bool compactShip = game.snapshot()["state"].value("shipClass", std::string{}) == "wren";
      double scale = std::min((r.right - 24.0) / (compactShip ? 600.0 : 1160.0),
                              (r.bottom - 48.0) / (compactShip ? 365.0 : 455.0));
      for (const auto& room : game.snapshot()["rooms"]) {
        int x = 12 + static_cast<int>(room["x"].get<int>() * scale),
            y = 40 + static_cast<int>(room["y"].get<int>() * scale);
        RECT box{x, y, x + std::max(20, static_cast<int>(room["w"].get<int>() * scale)),
                 y + std::max(16, static_cast<int>(room["h"].get<int>() * scale))};
        fill(dc, box, RGB(35, 59, 71));
        FrameRect(dc, &box, panelBrush);
        RECT textBox = box;
        InflateRect(&textBox, -3, -2);
        label(dc, textBox, wide(room["name"].get<std::string>()), ink, smallFont,
              DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        hitRegions.push_back({box, room["id"].get<std::string>()});
      }
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
    EndPaint(h, &ps);
    return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
LRESULT CALLBACK windowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      mainWindow = h;
      font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
      smallFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
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
      summaryBox = widget(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      detailBox = widget(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
      feedBox = widget(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL);
      listBox =
          widget(L"LISTBOX", L"",
                 LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, idList);
      SendMessageW(listBox, LB_SETHORIZONTALEXTENT, 700, 0);
      statusLabel = widget(L"STATIC", L"", SS_LEFT);
      canvas = widget(L"SpaceshipChart", L"", 0, idCanvas);
      refresh();
      return 0;
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
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT r;
      GetClientRect(h, &r);
      fill(dc, {0, 0, r.right, 80}, panel);
      label(dc, {22, 12, 500, 45}, L"SPACESHIP COMMAND", ink, titleFont);
      label(dc, {24, 48, 560, 75}, L"NATIVE C++  /  OFFLINE VOYAGE  /  MIGRATION BUILD", accent,
            smallFont);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
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
        if (id == idWatch)
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
          if (!game.load(savePath()))
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
          game.act(command.command, command.argument);
        } else
          return 0;
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
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR args, int show) {
  SetProcessDPIAware();
  backgroundBrush = CreateSolidBrush(background);
  panelBrush = CreateSolidBrush(panel);
  WNDCLASSW chart{};
  chart.lpfnWndProc = canvasProc;
  chart.hInstance = instance;
  chart.lpszClassName = L"SpaceshipChart";
  chart.hCursor = LoadCursorW(nullptr, IDC_HAND);
  RegisterClassW(&chart);
  WNDCLASSW wc{};
  wc.lpfnWndProc = windowProc;
  wc.hInstance = instance;
  wc.lpszClassName = L"SpaceshipNative";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = backgroundBrush;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  if (!RegisterClassW(&wc)) return 1;
  auto window = CreateWindowExW(0, wc.lpszClassName, L"Spaceship Command - native C++ migration",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 960,
                                nullptr, nullptr, instance, nullptr);
  if (!window) return 2;
  ShowWindow(window, show);
  UpdateWindow(window);
  if (std::wstring(args) == L"--smoke-test") {
    // Real native HWND creation + controller dispatch, no HTA or external host.
    bool ok = IsWindow(canvas) && IsWindow(listBox) && !choices.empty();
    SendMessageW(window, WM_COMMAND, idWatch, 0);
    ok = ok && game.actions() == 1;
    pickPage("navigation");
    ok = ok && choices.size() > 48;
    pickPage("crew");
    ok = ok && choices.size() == 4 && !actionButtons.empty();
    DestroyWindow(window);
    DeleteObject(font);
    DeleteObject(smallFont);
    DeleteObject(titleFont);
    DeleteObject(backgroundBrush);
    DeleteObject(panelBrush);
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
  return static_cast<int>(msg.wParam);
}
