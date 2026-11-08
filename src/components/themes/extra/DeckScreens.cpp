#include "DeckScreens.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <string>

#include "DeckTheme.h"
#include "RecentBooksStore.h"
#include "components/Sprite.h"
#include "fontIds.h"
#include "images/DeckFox.h"

namespace deck {

void drawKitsuneSleepScreen(const GfxRenderer& r) {
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  constexpr int kPad = 18;
  r.clearScreen();

  // STANDBY over a 2 px rule.
  const int big = displayFont(31);
  const int bigCap = 31 * 150 * 70 / (72 * 100);
  r.drawText(big, kPad, kPad - displayCapOffset(r, 31), "STANDBY");
  const int ruleY = kPad + bigCap + 8;
  r.fillRect(kPad, ruleY, W - 2 * kPad, 2);

  // Bottom block: last book card, then the status line.
  const int small = SMALL_FONT_ID;
  const int statusH = r.getLineHeight(small);
  const int statusY = H - kPad - statusH;
  const auto& books = RECENT_BOOKS.getBooks();
  const int titleFont = displayFont(22);
  const int cardPadX = 18;
  const int cardW = W - 2 * kPad;
  std::vector<std::string> lines;
  if (!books.empty()) lines = wrapDisplay(r, titleFont, books.front().title, cardW - 2 * cardPadX, 2);
  const int lh = displayLineStep(22);
  const int capH = 22 * 150 * 70 / (72 * 100);
  const bool hasAuthor = !books.empty() && !books.front().author.empty();
  constexpr int kCardPadY = 16;
  const int n = static_cast<int>(lines.size());
  const int cardH = n == 0 ? 0 : 2 * kCardPadY + (n - 1) * lh + capH + (hasAuthor ? 10 + statusH : 0);
  const int cardY = statusY - 10 - cardH;
  if (cardH > 0) {
    fillChamfer(r, kPad, cardY, cardW, cardH, 14);
    int y = cardY + kCardPadY - displayCapOffset(r, 22);
    for (const auto& l : lines) {
      r.drawText(titleFont, kPad + cardPadX, y, l.c_str(), false);
      y += lh;
    }
    if (hasAuthor) {
      const std::string a =
          r.truncatedText(small, upper(books.front().author).c_str(), cardW - 2 * cardPadX, EpdFontFamily::BOLD);
      drawTracked(r, small, kPad + cardPadX, cardY + kCardPadY + (n - 1) * lh + capH + 10, a.c_str(), 2, false, true);
    }
  }

  // Fox centred in the space between the rule and the card, with hatch shards.
  const int top = ruleY + 2;
  const int bottom = cardH > 0 ? cardY : statusY - 10;
  const int foxX = (W - kFoxSleep.width) / 2;
  const int foxY = top + std::max(0, (bottom - top - kFoxSleep.height) / 2);
  hatch(r, kPad, top + 14, 150, 18);
  hatch(r, W - kPad - 26, foxY + 40, 26, kFoxSleep.height - 80);
  hatch(r, kPad, bottom - 50, 60, 36);
  drawSprite(r, kFoxSleep, foxX, foxY);

  drawTracked(r, small, kPad, statusY, "HOLD PWR TO WAKE", 2, true, true);
  char bat[16];
  snprintf(bat, sizeof(bat), "BAT %u%%", static_cast<unsigned>(std::min<int>(100, powerManager.getBatteryPercentage())));
  const int bw = trackedWidth(r, small, bat, 2, true);
  drawTracked(r, small, W - kPad - bw, statusY, bat, 2, true, true);
}

void drawWakingScreen(const GfxRenderer& r, const char* message) {
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  constexpr int kPad = 24;
  r.clearScreen();
  const int big = displayFont(31);
  const int bigH = 31 * 150 * 70 / (72 * 100);
  const int total = bigH + 24 + kFoxWake.height + 30 + 10 + 10 + r.getLineHeight(SMALL_FONT_ID);
  int y = std::max(kPad, (H - total) / 2);
  r.drawText(big, kPad, y - displayCapOffset(r, 31), "WAKING");
  y += bigH + 24;
  drawSprite(r, kFoxWake, (W - kFoxWake.width) / 2, y);
  y += kFoxWake.height + 30;
  segments(r, kPad, y, W - 2 * kPad, 10, 6, 4);
  y += 20;
  if (message && message[0]) drawTracked(r, SMALL_FONT_ID, kPad, y, upper(message).c_str(), 2, true, true);
}

}  // namespace deck
