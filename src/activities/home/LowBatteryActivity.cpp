#include "LowBatteryActivity.h"

#include <GfxRenderer.h>

#include <string>

#include "MappedInputManager.h"
#include "components/Sprite.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckTheme.h"
#include "images/DeckFox.h"

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
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  // Light hatch over everything: 2 px ink every 7 px.
  for (int y = 0; y < H; ++y) {
    for (int x = (7 - (y % 7)) % 7; x < W; x += 7) renderer.fillRect(x, y, 2, 1, true);
  }
  constexpr int kSide = 24;
  const int w = W - 2 * kSide;
  const int titleCap = 31 * 150 * 70 / 7200;
  const int h = 18 + titleCap + 16 + kFoxTired.height + 18;
  const int y = H / 4;
  deck::fillChamfer(renderer, kSide, y, w, h, 14, true);
  deck::fillChamfer(renderer, kSide + 3, y + 3, w - 6, h - 6, 13, false);
  const std::string title = "BATTERY " + std::to_string(percent) + "%";
  renderer.drawText(deck::displayFont(31), kSide + 18, y + 18 - deck::displayCapOffset(renderer, 31), title.c_str());
  const char* kana = "充電";
  renderer.drawText(deck::kanaFont(), kSide + w - 18 - renderer.getTextWidth(deck::kanaFont(), kana),
                    y + 18 + titleCap - renderer.getLineHeight(deck::kanaFont()), kana);
  const int foxY = y + 18 + titleCap + 16;
  drawSprite(renderer, kFoxTired, kSide + (w - kFoxTired.width) / 2, foxY);
  // Clear the key bar so only this modal's key shows.
  const int barH = UITheme::getInstance().getMetrics().buttonHintsHeight;
  renderer.fillRect(0, H - barH, W, barH, false);
  renderer.setOrientation(orientation);
  const auto labels = mappedInput.mapLabels("", "OK", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
