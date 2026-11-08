#include "LowBatteryActivity.h"

#include <GfxRenderer.h>

#include <string>

#include "MappedInputManager.h"
#include "components/Sprite.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"

void LowBatteryActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void LowBatteryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void LowBatteryActivity::render(RenderLock&&) {
  // Drawn over the screen underneath (still in the framebuffer).
  const auto orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  deck::drawLowBatteryCard(renderer, percent);
  // Clear the key bar so only this modal's key shows.
  const int barH = UITheme::getInstance().getMetrics().buttonHintsHeight;
  renderer.fillRect(0, renderer.getScreenHeight() - barH, renderer.getScreenWidth(), barH, false);
  renderer.setOrientation(orientation);
  const auto labels = mappedInput.mapLabels("", "OK", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
