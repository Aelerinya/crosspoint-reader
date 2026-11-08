#include "DeckScreens.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <memory>
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
  {
    const char* kana = "スタンバイ";
    const int kw = r.getTextWidth(kanaFont(), kana);
    r.drawText(kanaFont(), W - kPad - kw, ruleY - 4 - r.getLineHeight(kanaFont()), kana);
  }
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
  snprintf(bat, sizeof(bat), "BAT %u%%",
           static_cast<unsigned>(std::min<int>(100, powerManager.getBatteryPercentage())));
  const int bw = trackedWidth(r, small, bat, 2, true);
  drawTracked(r, small, W - kPad - bw, statusY, bat, 2, true, true);
}

void drawWakingScreen(const GfxRenderer& r, const char* message) {
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  constexpr int kPad = 24;
  r.clearScreen();
  const int big = displayFont(46);
  const int bigH = 46 * 150 * 70 / (72 * 100);
  const int total = bigH + 24 + kFoxWake.height + 30 + 10 + 10 + r.getLineHeight(SMALL_FONT_ID);
  int y = std::max(kPad, (H - total) / 2);
  r.drawText(kanaFont(), kPad, y - r.getLineHeight(kanaFont()) - 8, "おはよう");
  r.drawText(big, kPad, y - displayCapOffset(r, 46), "WAKING");
  y += bigH + 24;
  drawSprite(r, kFoxWake, (W - kFoxWake.width) / 2, y);
  y += kFoxWake.height + 30;
  segments(r, kPad, y, W - 2 * kPad, 10, 6, 4);
  y += 20;
  if (message && message[0]) drawTracked(r, SMALL_FONT_ID, kPad, y, upper(message).c_str(), 2, true, true);
}

void drawChapterClear(const GfxRenderer& r, const char* book, const char* finished, const char* next) {
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  constexpr int kPad = 18;
  r.clearScreen();
  // Positions follow the 480x800 design, scaled to the screen height.
  const auto sy = [H](const int y) { return y * H / 800; };

  // Hatch shard behind the fox: a quadrilateral leaning right.
  {
    const int top = sy(260);
    const int h = sy(300);
    const int left = W / 3;
    for (int row = 0; row < h; ++row) {
      const int x0 = left + (W - left) * 4 / 10 * (h - row) / h;
      hatch(r, x0, top + row, W - x0, 1);
    }
  }
  const int foxX = W - 24 - kFoxChapter.width;
  const int foxY = sy(226);
  // White cut-out behind the fox so the stripes don't run through it.
  drawSprite(r, kFoxChapter, foxX, foxY);

  const int small = SMALL_FONT_ID;
  int y = sy(74);
  const std::string kicker = "// " + upper(book ? book : "");
  drawTracked(r, small, kPad, y, r.truncatedText(small, kicker.c_str(), W - 2 * kPad, EpdFontFamily::BOLD).c_str(), 3,
              true, true);
  y += r.getLineHeight(small) + 8;
  const int cap = 46 * 150 * 70 / 7200;
  const int step = 46 * 150 * 80 / 7200;
  r.drawText(displayFont(46), kPad, y - displayCapOffset(r, 46), "CHAPTER");
  r.drawText(displayFont(46), kPad, y + step - displayCapOffset(r, 46), "CLEAR");
  y += step + cap + 12;
  const std::string done = upper(finished ? finished : "");
  drawTracked(r, UI_10_FONT_ID, kPad, y,
              r.truncatedText(UI_10_FONT_ID, done.c_str(), foxX - kPad - 10, EpdFontFamily::BOLD).c_str(), 1, true,
              true);

  // NEXT ▸ title, then a rule to the edge.
  y = std::max(sy(640), foxY + kFoxChapter.height + 16);
  const int lh = r.getLineHeight(small);
  drawTracked(r, small, kPad, y, "NEXT", 2, true, true);
  int x = kPad + trackedWidth(r, small, "NEXT", 2, true) + 8;
  const int cy = y + lh / 2;
  const int xs[] = {x, x + 7, x};
  const int ys[] = {cy - 5, cy, cy + 5};
  r.fillPolygon(xs, ys, 3, true);
  x += 16;
  const std::string n = r.truncatedText(small, upper(next ? next : "").c_str(), W - x - kPad - 40, EpdFontFamily::BOLD);
  drawTracked(r, small, x, y, n.c_str(), 1, true, true);
  x += trackedWidth(r, small, n.c_str(), 1, true) + 10;
  if (x < W - kPad) r.fillRect(x, cy - 1, W - kPad - x, 2, true);
}

void drawQuickResumeTab(const GfxRenderer& r) {
  const auto saved = r.getOrientation();
  const_cast<GfxRenderer&>(r).setOrientation(GfxRenderer::Orientation::Portrait);
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  const int font = SMALL_FONT_ID;
  const char* label = "QUICK RESUME";
  const int textLen = r.getTextWidth(font, label, EpdFontFamily::BOLD);
  const int lh = r.getLineHeight(font);
  const int w = lh + 22;
  const int h = 14 + 22 + 10 + textLen + 14;
  const int x = W - w;
  const int y = H * 300 / 800;
  // Tab with the left corners cut (10 px).
  const int xs[] = {x + 10, W, W, x + 10, x, x};
  const int ys[] = {y, y, y + h, y + h, y + h - 10, y + 10};
  r.fillPolygon(xs, ys, 6, true);
  // Moon: a 20 px disc with an offset bite.
  const int cx = x + w / 2 + 2;
  const int cy = y + 14 + 11;
  for (int dy = -10; dy <= 10; ++dy) {
    for (int dx = -10; dx <= 10; ++dx) {
      const bool in = dx * dx + dy * dy <= 100;
      const bool bite = (dx - 6) * (dx - 6) + (dy + 3) * (dy + 3) <= 64;
      if (in && !bite) r.drawPixel(cx + dx, cy + dy, false);
    }
  }
  // The rotated label runs upward from its anchor, so anchor it at the bottom.
  r.drawTextRotated90CW(font, x + (w - lh) / 2, y + h - 14, label, false, EpdFontFamily::BOLD);
  const_cast<GfxRenderer&>(r).setOrientation(saved);
}

namespace {

constexpr int kFrameMs = 170;

// Quadrilateral with horizontal top/bottom edges: x at the top and bottom of each side.
void fillQuad(const GfxRenderer& r, const int top, const int bottom, const int topL, const int topR, const int botL,
              const int botR, const bool striped) {
  const int h = std::max(1, bottom - top);
  for (int row = 0; row < h; ++row) {
    const int l = topL + (botL - topL) * row / h;
    const int rr = topR + (botR - topR) * row / h;
    if (rr <= l) continue;
    if (striped) {
      hatch(r, l, top + row, rr - l, 1);
    } else {
      r.fillRect(l, top + row, rr - l, 1, true);
    }
  }
}

struct PortraitScope {
  const GfxRenderer& r;
  GfxRenderer::Orientation saved;
  explicit PortraitScope(const GfxRenderer& renderer) : r(renderer), saved(renderer.getOrientation()) {
    const_cast<GfxRenderer&>(r).setOrientation(GfxRenderer::Orientation::Portrait);
  }
  ~PortraitScope() { const_cast<GfxRenderer&>(r).setOrientation(saved); }
};

}  // namespace

void transitionOut(const GfxRenderer& r, const char* destination, const bool flash) {
  PortraitScope portrait(r);
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  // 1. Shear out: black shard over the left, a hatch band, one slice offset 40 px.
  fillQuad(r, 0, H, 0, W * 58 / 100, 0, W * 30 / 100, false);
  fillQuad(r, 0, H, W * 62 / 100, W * 74 / 100, W * 34 / 100, W * 46 / 100, true);
  r.fillRect(40, H * 318 / 800, W - 40, 26, true);
  r.displayBuffer(HalDisplay::FAST_REFRESH);
  delay(kFrameMs);
  if (!flash) return;

  // 2. Flash: black frame, destination in white; this is the panel's full refresh.
  r.fillRect(0, 0, W, H, true);
  {
    const int top = H * 120 / 800;
    for (int row = 0; row < 60; ++row) hatch(r, 0, top + row, W - row, 1, false);
  }
  int y = H * 300 / 800;
  const int cap = 46 * 150 * 70 / 7200;
  const std::string dest = upper(destination ? destination : "");
  if (const char* kana = kanaFor(dest)) r.drawText(kanaFont(), 40, y - r.getLineHeight(kanaFont()) - 8, kana, false);
  r.drawText(displayFont(46), 40, y - displayCapOffset(r, 46), dest.c_str(), false);
  y += cap + 16;
  r.fillRect(40, y, W - 80, 3, false);
  y += 13;
  drawTracked(r, SMALL_FONT_ID, 40, y, "// FULL REFRESH \xc2\xb7 GHOST CLEAR", 3, false, true);
  r.displayBuffer(HalDisplay::FULL_REFRESH);
  delay(kFrameMs);
}

void transitionIn(const GfxRenderer& r) {
  uint8_t* fb = r.getFrameBuffer();
  const size_t size = r.getBufferSize();
  if (!fb || size == 0) return;
  // The incoming frame, restored after each overlay. No copy, no transition.
  auto saved = makeUniqueNoThrow<uint8_t[]>(size);
  if (!saved) return;
  memcpy(saved.get(), fb, size);
  {
    PortraitScope portrait(r);
    const int W = r.getScreenWidth();
    const int H = r.getScreenHeight();
    // 3. Shear in: the new screen underneath, the shard on the right.
    fillQuad(r, 0, H, W * 70 / 100, W, W * 42 / 100, W, false);
    fillQuad(r, 0, H, W * 56 / 100, W * 66 / 100, W * 28 / 100, W * 38 / 100, true);
    r.fillRect(0, H * 210 / 800, W * 60 / 100 - 24, 18, true);
    r.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(kFrameMs);
    memcpy(fb, saved.get(), size);
    // 4. Glitch: two scan lines across the clean screen.
    r.fillRect(0, H * 140 / 800, W, 4, true);
    r.fillRect(0, H * 420 / 800, W - 30, 2, true);
    r.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(kFrameMs);
  }
  // 5. Settle: the caller's refresh sends the clean frame.
  memcpy(fb, saved.get(), size);
}

}  // namespace deck
