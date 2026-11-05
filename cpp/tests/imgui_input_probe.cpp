// CPU-only ImGui event-path regression. Exercises the actual Dear ImGui close
// widget and WantCaptureMouse, but does not render pixels or test raylib/OS UI.
#include <cstdio>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/PointerOwnership.h"

int main() {
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(1000,700);
  io.DeltaTime = 1.0f/60;
  io.ConfigInputTrickleEventQueue = false;
  ImFontConfig font; font.SizePixels = 18;
  io.Fonts->AddFontDefault(&font);
  io.Fonts->Build();
  bool open = true;
  ship::PointerOwnership gesture;
  int shipClicks = 0;
  auto frame = [&] {
    ImGui::NewFrame();
    if (ImGui::IsMouseClicked(0)) { gesture.Press(io.WantCaptureMouse); }
    if (ImGui::IsMouseReleased(0) && !gesture.Release()) { ++shipClicks; }
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1000,700});
    ImGui::Begin("ship", nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::End();
    if (open) {
      ImGui::SetNextWindowPos({300,90}); ImGui::SetNextWindowSize({420,300});
      ImGui::Begin("ROOM - O2 Filter Room", &open, ImGuiWindowFlags_NoSavedSettings);
      ImGui::TextUnformatted("Right-click to open the O2 Filter Room window.");
      ImGui::End();
    }
    ImGui::Render();
  };
  io.AddMousePosEvent(40,40); frame(); frame();
  if (io.WantCaptureMouse) { std::puts("FAIL: NoInputs ship host captures exposed ship"); return 1; }
  auto* window = ImGui::FindWindowByName("ROOM - O2 Filter Room");
  const auto style = ImGui::GetStyle();
  const float closeX = window->Pos.x + window->Size.x - style.FramePadding.x - ImGui::GetFontSize()/2;
  const float closeY = window->Pos.y + style.FramePadding.y + ImGui::GetFontSize()/2;
  io.AddMousePosEvent(closeX,closeY); frame();
  io.AddMouseButtonEvent(0,true); frame();
  if (!gesture.UiOwned()) { std::puts("FAIL: panel close press not owned by ImGui"); return 1; }
  io.AddMouseButtonEvent(0,false); frame();
  if (open || shipClicks != 0) { std::puts("FAIL: close failed or leaked a click to ship"); return 1; }
  frame(); frame();
  io.AddMouseButtonEvent(0,true); frame();
  io.AddMouseButtonEvent(0,false); frame();
  if (shipClicks != 1) { std::puts("FAIL: independent exposed-ship click was swallowed"); return 1; }
  ImGui::DestroyContext();
  std::puts("PASS: actual ImGui close widget consumes its gesture; next ship click remains usable (CPU event test, not visual parity)");
}
