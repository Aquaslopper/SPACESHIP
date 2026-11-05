#pragma once
namespace ship {
// Ownership belongs to the entire press/release gesture, not merely the
// window still hovered on release (a close button can remove it on press).
class PointerOwnership {
 public:
  void Press(bool uiWantsPointer) { uiOwned_ = uiWantsPointer; }
  bool UiOwned() const { return uiOwned_; }
  bool Release() { const bool owned = uiOwned_; uiOwned_ = false; return owned; }
 private:
  bool uiOwned_ = false;
};
}
