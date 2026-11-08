#include "DeckTheme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/Sprite.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/DeckFox.h"
#include "util/BookProgress.h"

// ================================================================ primitives

namespace deck {

// Arbitrary non-zero IDs; registered in ThemeFonts.cpp.
constexpr int kDisplay13 = 0x0DEC0013;
constexpr int kDisplay22 = 0x0DEC0022;
constexpr int kDisplay31 = 0x0DEC0031;

int displayFont(const int size) {
  if (size >= 31) return kDisplay31;
  if (size >= 22) return kDisplay22;
  return kDisplay13;
}

// The fonts are built at size pt / 150 dpi, so ppem = size * 150 / 72.
int displayLineStep(const int size) { return size * 150 * 86 / (72 * 100); }

// Saira's capitals are ~0.70 em tall.
int displayCapOffset(const GfxRenderer& r, const int size) {
  return std::max(0, r.getFontAscenderSize(displayFont(size)) - size * 150 * 70 / (72 * 100));
}

std::string upper(const std::string& text) {
  // Drop a leading "« " (upstream's back-arrow prefix on "« Home", "« Back").
  std::string out = text.rfind("\xc2\xab", 0) == 0 ? text.substr(text[2] == ' ' ? 3 : 2) : text;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

namespace {
// tan(18°) ≈ 0.325: horizontal shift of the top edge over the height.
int slantShift(const int h) { return (h * 325 + 500) / 1000; }
}  // namespace

void fillSlant(const GfxRenderer& r, const int x, const int y, const int w, const int h, const bool black) {
  const int s = slantShift(h);
  const int xs[] = {x + s, x + w, x + w - s, x};
  const int ys[] = {y, y, y + h, y + h};
  r.fillPolygon(xs, ys, 4, black);
}

void drawSlant(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int thickness,
               const bool black) {
  fillSlant(r, x, y, w, h, black);
  // Inner parallelogram: same slant, inset by the thickness on every side.
  const int t = thickness;
  const int s = slantShift(h);
  const int ih = h - 2 * t;
  if (ih <= 0 || w - 2 * t - s <= 0) return;
  const int is = slantShift(ih);
  const int topLeft = x + s - slantShift(t) + t + 1;
  const int xs[] = {topLeft, x + w - t - 1 - (s - is) / 2 - slantShift(t) / 2, x + w - t - 1 - (s - is) / 2 - is - slantShift(t) / 2,
                    topLeft - is};
  const int ys[] = {y + t, y + t, y + h - t - 1, y + h - t - 1};
  r.fillPolygon(xs, ys, 4, !black);
}

void fillChamfer(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int cut,
                 const bool black) {
  const int xs[] = {x + cut, x + w, x + w, x + w - cut, x, x};
  const int ys[] = {y, y, y + h - cut, y + h, y + h, y + cut};
  r.fillPolygon(xs, ys, 6, black);
}

void drawChamfer(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int cut,
                 const int thickness) {
  fillChamfer(r, x, y, w, h, cut, true);
  // A 45° edge inset by t moves its corner points by t * (sqrt 2 - 1) ≈ 0.41 t.
  const int t = thickness;
  const int c = std::max(0, cut - (t * 41 + 50) / 100);
  fillChamfer(r, x + t, y + t, w - 2 * t, h - 2 * t, c, false);
}

void hatch(const GfxRenderer& r, const int x, const int y, const int w, const int h, const bool black) {
  constexpr int kPeriod = 9;
  constexpr int kInk = 3;
  for (int yy = 0; yy < h; ++yy) {
    // Ink where (col + row) mod period < ink: "/" stripes.
    int start = (kPeriod - ((yy + y + x) % kPeriod)) % kPeriod;
    for (int xx = start - kPeriod; xx < w; xx += kPeriod) {
      const int a = std::max(0, xx);
      const int b = std::min(w, xx + kInk);
      if (b > a) r.fillRect(x + a, y + yy, b - a, 1, black);
    }
  }
}

int trackedWidth(const GfxRenderer& r, const int fontId, const char* text, const int tracking, const bool bold) {
  return r.getTextAdvanceX(fontId, text, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR,
                           static_cast<int8_t>(tracking));
}

void drawTracked(const GfxRenderer& r, const int fontId, const int x, const int y, const char* text,
                 const int tracking, const bool black, const bool bold) {
  r.drawText(fontId, x, y, text, black, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR,
             BidiUtils::BidiBaseDir::AUTO, static_cast<int8_t>(tracking));
}

void segments(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int count,
              const int filled, const bool black) {
  constexpr int kGap = 4;
  const int s = slantShift(h);
  const int segW = (w - s - kGap * (count - 1)) / count + s;
  for (int i = 0; i < count; ++i) {
    const int sx = x + i * (segW - s + kGap);
    if (i < filled) {
      fillSlant(r, sx, y, segW, h, black);
    } else {
      drawSlant(r, sx, y, segW, h, 2, black);
    }
  }
}

std::vector<std::string> wrapDisplay(const GfxRenderer& r, const int fontId, const std::string& text,
                                     const int width, const int maxLines, bool* truncated) {
  std::vector<std::string> lines;
  const std::string up = upper(text);
  std::string line;
  size_t i = 0;
  while (i < up.size()) {
    size_t j = up.find(' ', i);
    if (j == std::string::npos) j = up.size();
    const std::string word = up.substr(i, j - i);
    i = j + 1;
    if (word.empty()) continue;
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (r.getTextWidth(fontId, candidate.c_str()) <= width || line.empty()) {
      line = candidate;
      continue;
    }
    lines.push_back(line);
    line = word;
    if (static_cast<int>(lines.size()) == maxLines - 1) {
      // Last line takes the rest, truncated.
      if (i < up.size()) line += " " + up.substr(i);
      i = up.size();
    }
  }
  if (!line.empty()) lines.push_back(line);
  bool cut = false;
  for (auto& l : lines) {
    std::string t = r.truncatedText(fontId, l.c_str(), width);
    cut = cut || t != l;
    l = std::move(t);
  }
  if (truncated) *truncated = cut;
  return lines;
}

}  // namespace deck

// ================================================================ theme

namespace {

constexpr int kSide = 18;
constexpr int kTabH = 34;

// Four slanted battery cells, filled from the left.
void batteryCells(const GfxRenderer& r, const int x, const int y, const int percent) {
  const int lit = percent <= 5 ? 0 : (percent + 24) / 25;
  for (int i = 0; i < 4; ++i) {
    if (i < lit) {
      deck::fillSlant(r, x + i * 8, y, 9, 12);
    } else {
      deck::drawSlant(r, x + i * 8, y, 9, 12, 1);
    }
  }
}

bool hintGrayPass(const GfxRenderer& renderer) {
  return renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();
}

// Hero card geometry inside the home tile, shared by the tile and the
// selection outline drawn later by drawButtonMenu.
Rect heroCard(const Rect& tile) { return Rect{tile.x + kSide, tile.y + 22, tile.width - 2 * kSide, tile.height - 22}; }

Rect homeTileRect(const GfxRenderer& renderer) {
  const auto& m = ExtraMetrics::deck;
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  return Rect{content.x, m.homeTopPadding, content.width, m.homeCoverTileHeight};
}

}  // namespace

void DeckTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle,
                           const bool backButton) const {
  const auto& m = ExtraMetrics::deck;
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int left = content.x + kSide;
  const int right = content.x + content.width - kSide;
  const int y = rect.y;
  // Home is the stack root and passes the book title; the tab names the screen.
  const bool home = !backButton && rect.y + rect.height == m.homeTopPadding;
  const std::string label = deck::upper(home || !title ? std::string("HOME") : std::string(title));

  // Status block (right): fox head, clock, battery cells, over a rule.
  const int lineY = y + kTabH - 2;
  int statusX = right;
  const int percent = std::min<int>(100, powerManager.getBatteryPercentage());
  statusX -= 4 * 8 + 4;
  batteryCells(renderer, statusX, lineY - 18, percent);
  char clock[10];
  if (SETTINGS.clockShowInHeader && halClock.isAvailable() &&
      halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1)) {
    const int cw = deck::trackedWidth(renderer, UI_10_FONT_ID, clock, 1, true);
    statusX -= cw + 8;
    deck::drawTracked(renderer, UI_10_FONT_ID, statusX, lineY - 6 - renderer.getLineHeight(UI_10_FONT_ID) + 4, clock,
                      1, true, true);
  }
  statusX -= kFoxHead.width + 8;
  drawSprite(renderer, kFoxHead, statusX, lineY - 4 - kFoxHead.height);
  statusX -= 6;

  // Tab: black, slanted right edge.
  const int tabFont = UI_10_FONT_ID;
  const int maxText = statusX - left - 60;
  const std::string shown = renderer.truncatedText(tabFont, label.c_str(), maxText, EpdFontFamily::BOLD);
  const int textW = deck::trackedWidth(renderer, tabFont, shown.c_str(), 2, true);
  const int tabW = textW + 12 + 26;
  const int xs[] = {left, left + tabW, left + tabW - 16, left};
  const int ys[] = {y, y, y + kTabH, y + kTabH};
  renderer.fillPolygon(xs, ys, 4, true);
  deck::drawTracked(renderer, tabFont, left + 12, y + (kTabH - renderer.getLineHeight(tabFont)) / 2, shown.c_str(), 2,
                    false, true);

  // Rule from the tab to the status block, then under it.
  renderer.fillRect(left + tabW - 8, lineY, right - (left + tabW - 8), 2, true);

  if (subtitle && subtitle[0] && !home) {
    const std::string sub = renderer.truncatedText(SMALL_FONT_ID, deck::upper(subtitle).c_str(),
                                                   statusX - (left + tabW + 8));
    const int sw = renderer.getTextWidth(SMALL_FONT_ID, sub.c_str());
    renderer.drawText(SMALL_FONT_ID, statusX - sw, lineY - 4 - renderer.getLineHeight(SMALL_FONT_ID), sub.c_str());
  }
}

void DeckTheme::drawHomeTile(GfxRenderer& renderer, const Rect rect, const RecentBook* book) const {
  const auto& m = ExtraMetrics::deck;
  const int x = rect.x + kSide;
  const int w = rect.width - 2 * kSide;
  const int progress = book ? loadBookProgress(book->path) : -1;

  // "// RESUME" stripe.
  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);
  const char* kicker = book ? "// RESUME" : "// STANDBY";
  deck::drawTracked(renderer, SMALL_FONT_ID, x, rect.y, kicker, 3, true, true);
  const int kickW = deck::trackedWidth(renderer, SMALL_FONT_ID, kicker, 3, true);
  char pct[8] = "";
  if (progress >= 0) snprintf(pct, sizeof(pct), "%d%%", progress);
  const int pctW = pct[0] ? deck::trackedWidth(renderer, SMALL_FONT_ID, pct, 2, true) : 0;
  if (pct[0]) deck::drawTracked(renderer, SMALL_FONT_ID, x + w - pctW, rect.y, pct, 2, true, true);
  deck::hatch(renderer, x + kickW + 10, rect.y + (labelH - 10) / 2, w - kickW - pctW - 20, 10);

  const Rect card = heroCard(rect);
  deck::fillChamfer(renderer, card.x, card.y, card.width, card.height, 24);
  const int padX = 18;
  const int padY = 16;
  const int coverW = 118;
  const int coverH = m.homeCoverHeight;
  const int coverX = card.x + padX;
  const int coverY = card.y + padY;

  int drawn = 0;
  if (book) drawn = drawCoverInSlot(renderer, *book, Rect{coverX, coverY, coverW, coverH}, m.homeCoverHeight);
  if (drawn == 0) {
    renderer.fillRect(coverX, coverY, coverW, coverH, false);
    deck::hatch(renderer, coverX + 6, coverY + 6, coverW - 12, coverH - 12);
  }
  // 2 px white frame around the cover slot (covers narrower than the slot are centred in it).
  const int frameX = drawn ? coverX + (coverW - drawn) / 2 : coverX;
  const int frameW = drawn ? drawn : coverW;
  renderer.drawRect(frameX - 2, coverY - 2, frameW + 4, coverH + 4, 2, false);

  const int colX = coverX + coverW + 16;
  const int colW = card.x + card.width - padX - colX;
  int ty = coverY;
  const std::string author = book ? deck::upper(book->author) : std::string("CROSSPOINT");
  // Leaves room for the selection mark drawn by drawButtonMenu.
  const std::string a = renderer.truncatedText(SMALL_FONT_ID, author.c_str(), colW - 26, EpdFontFamily::BOLD);
  deck::drawTracked(renderer, SMALL_FONT_ID, colX, ty, a.c_str(), 2, false, true);
  ty += labelH + 6;

  // Title: big display face if it fits beside the cover, else the small one.
  const std::string title = book ? book->title : std::string(tr(STR_NO_OPEN_BOOK));
  const int titleTop = ty + 4;
  for (const int size : {22, 13}) {
    const int fontId = deck::displayFont(size);
    const int lh = deck::displayLineStep(size);
    const int capH = size * 150 * 70 / (72 * 100);
    const int maxLines = std::max(1, (coverY + coverH - titleTop - capH) / lh + 1);
    bool cut = false;
    const auto lines = deck::wrapDisplay(renderer, fontId, title, colW, maxLines, &cut);
    if (cut && size != 13) continue;  // try the small face before losing words
    int y = titleTop - deck::displayCapOffset(renderer, size);
    for (const auto& l : lines) {
      renderer.drawText(fontId, colX, y, l.c_str(), false);
      y += lh;
    }
    break;
  }

  // Ten slanted progress segments under the cover.
  const int segY = coverY + coverH + 14;
  const int filled = progress < 0 ? 0 : std::max(1, (progress + 5) / 10);
  deck::segments(renderer, card.x + padX, segY, card.width - 2 * padX, 12, 10, filled, false);
}

int DeckTheme::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(deck::displayFont(13)) + 12;
}

void DeckTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                               const int selectedIndex, const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>&) const {
  const auto& m = ExtraMetrics::deck;
  const int x = rect.x + kSide;
  const int fullW = rect.width - 2 * kSide;
  const int rowH = getMenuRowHeight(renderer);
  const int step = rowH + m.menuSpacing;
  const int font = deck::displayFont(13);
  const int lineH = renderer.getLineHeight(font);

  // Hero card selected (the recent book is index -1 here): white inner outline.
  if (selectedIndex < 0) {
    const Rect card = heroCard(homeTileRect(renderer));
    // A 2 px ring 7 px inside the card edge; the cover sits 18 px inside, clear of it.
    const int x0 = card.x + 7, y0 = card.y + 7, x1 = card.x + card.width - 8, y1 = card.y + card.height - 8;
    const int c = 21;
    const int xs[] = {x0 + c, x1, x1, x1 - c, x0, x0};
    const int ys[] = {y0, y0, y1 - c, y1, y1, y0 + c};
    for (int k = 0; k < 6; ++k) renderer.drawLine(xs[k], ys[k], xs[(k + 1) % 6], ys[(k + 1) % 6], 2, false);
    // ▶ in the top-right corner (the tile keeps that spot clear).
    const int mx = card.x + card.width - 34;
    const int my = card.y + 18;
    const int tx[] = {mx, mx + 12, mx};
    const int ty[] = {my, my + 7, my + 14};
    renderer.fillPolygon(tx, ty, 3, false);
  }

  // Fox in the lower right, above the key bar; rows that reach it get shorter.
  const int foxX = rect.x + rect.width - kSide - kFoxIdle.width + 6;
  const int foxY = renderer.getScreenHeight() - m.buttonHintsHeight - 4 - kFoxIdle.height;
  drawSprite(renderer, kFoxIdle, foxX, foxY);

  for (int i = 0; i < buttonCount; ++i) {
    const int y = rect.y + i * step;
    if (y + rowH > foxY + kFoxIdle.height) break;
    const int w = y + rowH > foxY + 20 ? foxX - x - 4 : fullW;
    const bool sel = i == selectedIndex;
    if (sel) {
      deck::fillChamfer(renderer, x, y, w, rowH, 14, true);
    } else {
      deck::drawChamfer(renderer, x, y, w, rowH, 14, 2);
    }
    const int ty = y + (rowH - lineH) / 2;
    if (sel) {
      // ▶ marker.
      const int cy = y + rowH / 2;
      const int xs[] = {x + 16, x + 26, x + 16};
      const int ys[] = {cy - 6, cy, cy + 6};
      renderer.fillPolygon(xs, ys, 3, false);
    }
    const std::string label =
        renderer.truncatedText(font, deck::upper(buttonLabel(i)).c_str(), w - 40 - 14);
    renderer.drawText(font, x + 40, ty, label.c_str(), !sel);
  }
}

void DeckTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) return;
  const auto orig = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int pageW = renderer.getScreenWidth();
  constexpr int kH = 38 - 14;  // the spec's 38 px band holds 24 px keys above a 14 px margin
  constexpr int kGap = 10;
  constexpr int kPad = 22;
  const int y = renderer.getScreenHeight() - 14 - kH;
  const int keyW = (pageW - 2 * kPad - 3 * kGap) / 4;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool gray = hintGrayPass(renderer);
  for (int i = 0; i < 4; ++i) {
    if (!labels[i] || !labels[i][0]) continue;
    const int kx = kPad + i * (keyW + kGap);
    if (gray) {
      renderer.fillRect(kx - 4, y, keyW + 8, kH, true);
      continue;
    }
    const bool confirm = i == 1;
    if (confirm) {
      deck::fillSlant(renderer, kx, y, keyW, kH, true);
    } else {
      deck::drawSlant(renderer, kx, y, keyW, kH, 2, true);
    }
    // Small face with light tracking: "RESUME" and "SELECT" must fit a ~100 px key.
    const int font = SMALL_FONT_ID;
    const std::string t = renderer.truncatedText(font, deck::upper(labels[i]).c_str(), keyW - 14, EpdFontFamily::BOLD);
    const int tw = deck::trackedWidth(renderer, font, t.c_str(), 1, true);
    deck::drawTracked(renderer, font, kx + (keyW - tw) / 2, y + (kH - renderer.getLineHeight(font)) / 2, t.c_str(), 1,
                      !confirm, true);
  }
  renderer.setOrientation(orig);
}
