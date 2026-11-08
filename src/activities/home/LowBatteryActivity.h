#pragma once
#include "activities/Activity.h"

// Deck theme: low battery modal over the current screen (hatching, BATTERY n%,
// the tired fox). Confirm or Back dismisses it.
class LowBatteryActivity final : public Activity {
 public:
  LowBatteryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int percent)
      : Activity("LowBattery", renderer, mappedInput), percent(percent) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int percent;
};
