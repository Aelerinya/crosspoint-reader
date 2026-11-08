#include "DeckTheme.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
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
#include "components/themes/extra/DeckList.h"
#include "fontIds.h"
#include "images/DeckFox.h"
#include "util/BookProgress.h"

// ================================================================ primitives

namespace deck {

// Arbitrary non-zero IDs; registered in ThemeFonts.cpp.
constexpr int kDisplay13 = 0x0DEC0013;
constexpr int kDisplay22 = 0x0DEC0022;
constexpr int kDisplay31 = 0x0DEC0031;
constexpr int kDisplay46 = 0x0DEC0046;
constexpr int kDisplay76 = 0x0DEC0076;

int displayFont(const int size) {
  if (size >= 76) return kDisplay76;
  if (size >= 46) return kDisplay46;
  if (size >= 31) return kDisplay31;
  if (size >= 22) return kDisplay22;
  return kDisplay13;
}

int kanaFont() { return 0x0DEC4A4A; }

const char* kanaFor(const std::string& label) {
  static constexpr struct {
    const char* label;
    const char* kana;
  } kTags[] = {
      {"HOME", "ホーム"},        {"LIBRARY", "書庫"},  {"SELECT CHAPTER", "目次"},
      {"INDEX", "目次"},         {"BOOKMARKS", "栞"},  {"GO TO", "移動"},
      {"FILE TRANSFER", "転送"}, {"TRANSFER", "転送"}, {"HOTSPOT MODE", "転送"},
      {"SETTINGS", "設定"},      {"DISPLAY", "設定"},  {"READER", "設定"},
      {"CONTROLS", "設定"},      {"SYSTEM", "設定"},   {"CONNECTIVITY", "設定"},
      {"SD CARD", "書庫"},       {"READ", "読書"},     {"FILES", "書庫"},
      {"ONLINE", "接続"},        {"CLEAR", "完了"},    {"OPEN", "開く"},
      {"SLEEP", "眠"},           {"WAKE", "起"},
  };
  for (const auto& t : kTags) {
    if (label == t.label) return t.kana;
  }
  return nullptr;
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
  const int xs[] = {topLeft, x + w - t - 1 - (s - is) / 2 - slantShift(t) / 2,
                    x + w - t - 1 - (s - is) / 2 - is - slantShift(t) / 2, topLeft - is};
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

// Words are measured and drawn one by one: with tracking, the small UI face's
// space advance can collapse to nothing next to some capitals.
int trackedWidth(const GfxRenderer& r, const int fontId, const char* text, const int tracking, const bool bold) {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int space = r.getSpaceWidth(fontId, style) + tracking;
  int width = 0;
  std::string word;
  const std::string all = text ? text : "";
  size_t i = 0;
  bool first = true;
  while (i <= all.size()) {
    const size_t j = std::min(all.find(' ', i), all.size());
    word.assign(all, i, j - i);
    if (!first) width += space;
    if (!word.empty()) width += r.getTextAdvanceX(fontId, word.c_str(), style, static_cast<int8_t>(tracking));
    first = false;
    i = j + 1;
  }
  return width;
}

void drawTracked(const GfxRenderer& r, const int fontId, const int x, const int y, const char* text, const int tracking,
                 const bool black, const bool bold) {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int space = r.getSpaceWidth(fontId, style) + tracking;
  const std::string all = text ? text : "";
  std::string word;
  int cx = x;
  size_t i = 0;
  bool first = true;
  while (i <= all.size()) {
    const size_t j = std::min(all.find(' ', i), all.size());
    word.assign(all, i, j - i);
    if (!first) cx += space;
    if (!word.empty()) {
      r.drawText(fontId, cx, y, word.c_str(), black, style, BidiUtils::BidiBaseDir::AUTO,
                 static_cast<int8_t>(tracking));
      cx += r.getTextAdvanceX(fontId, word.c_str(), style, static_cast<int8_t>(tracking));
    }
    first = false;
    i = j + 1;
  }
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

std::vector<std::string> wrapDisplay(const GfxRenderer& r, const int fontId, const std::string& text, const int width,
                                     const int maxLines, bool* truncated) {
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
    deck::drawTracked(renderer, UI_10_FONT_ID, statusX, lineY - 6 - renderer.getLineHeight(UI_10_FONT_ID) + 4, clock, 1,
                      true, true);
  }
  if (SETTINGS.deckCompanion) {
    statusX -= kFoxHead.width + 8;
    drawSprite(renderer, kFoxHead, statusX, lineY - 4 - kFoxHead.height);
  }
  statusX -= 6;

  // Tab: black, slanted right edge.
  const int tabFont = UI_10_FONT_ID;
  const int maxText = statusX - left - 60;
  const std::string shown = renderer.truncatedText(tabFont, label.c_str(), maxText, EpdFontFamily::BOLD);
  const int textW = deck::trackedWidth(renderer, tabFont, shown.c_str(), 2, true);
  const char* kana = deck::kanaFor(label);
  const int kanaW = kana ? renderer.getTextWidth(deck::kanaFont(), kana) + 8 : 0;
  const int tabW = textW + kanaW + 12 + 26;
  const int xs[] = {left, left + tabW, left + tabW - 16, left};
  const int ys[] = {y, y, y + kTabH, y + kTabH};
  renderer.fillPolygon(xs, ys, 4, true);
  deck::drawTracked(renderer, tabFont, left + 12, y + (kTabH - renderer.getLineHeight(tabFont)) / 2, shown.c_str(), 2,
                    false, true);
  if (kana) {
    renderer.drawText(deck::kanaFont(), left + 12 + textW + 8,
                      y + (kTabH - renderer.getLineHeight(deck::kanaFont())) / 2, kana, false);
  }

  // Rule from the tab to the status block, then under it.
  renderer.fillRect(left + tabW - 8, lineY, right - (left + tabW - 8), 2, true);

  if (subtitle && subtitle[0] && !home) {
    const std::string sub =
        renderer.truncatedText(SMALL_FONT_ID, deck::upper(subtitle).c_str(), statusX - (left + tabW + 8));
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

void DeckTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount, const int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
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
    const std::string label = renderer.truncatedText(font, deck::upper(buttonLabel(i)).c_str(), w - 40 - 14);
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
    // Arrows as shapes (the trimmed fonts have no ▲▼◀▶); ◂ before Back/Home.
    const bool ink = !confirm;
    const std::string up = deck::upper(labels[i]);
    const int cx = kx + keyW / 2;
    const int cy = y + kH / 2;
    const auto tri = [&](const int ax, const int ay, const int bx, const int by, const int qx, const int qy) {
      const int xs[] = {ax, bx, qx};
      const int ys[] = {ay, by, qy};
      renderer.fillPolygon(xs, ys, 3, ink);
    };
    if (up == "UP") {
      tri(cx - 7, cy + 4, cx + 7, cy + 4, cx, cy - 5);
      continue;
    }
    if (up == "DOWN") {
      tri(cx - 7, cy - 4, cx + 7, cy - 4, cx, cy + 5);
      continue;
    }
    if (up == "LEFT") {
      tri(cx + 4, cy - 7, cx + 4, cy + 7, cx - 5, cy);
      continue;
    }
    if (up == "RIGHT") {
      tri(cx - 4, cy - 7, cx - 4, cy + 7, cx + 5, cy);
      continue;
    }
    const bool back = i == 0 && (up == "BACK" || up == "HOME" || up == "EXIT");
    const int font = SMALL_FONT_ID;
    const int markW = back ? 10 : 0;
    const std::string t = renderer.truncatedText(font, up.c_str(), keyW - 14 - markW, EpdFontFamily::BOLD);
    const int tw = deck::trackedWidth(renderer, font, t.c_str(), 1, true) + markW;
    const int tx = kx + (keyW - tw) / 2;
    if (back) tri(tx + 5, cy - 4, tx + 5, cy + 4, tx, cy);
    deck::drawTracked(renderer, font, tx + markW, y + (kH - renderer.getLineHeight(font)) / 2, t.c_str(), 1, ink, true);
  }
  renderer.setOrientation(orig);
}

// ================================================================ popups

namespace {
constexpr int kToastSlant = 10;
constexpr int kToastPadX = 22;
constexpr int kToastPadY = 8;

void fillToastShape(const GfxRenderer& r, const int x, const int y, const int w, const int h, const bool black) {
  const int xs[] = {x + kToastSlant, x + w, x + w - kToastSlant, x};
  const int ys[] = {y, y, y + h, y + h};
  r.fillPolygon(xs, ys, 4, black);
}

// Opening screen geometry, kept so fillPopupProgress can update its bar.
Rect gOpeningBar{};
}  // namespace

Rect DeckTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  gOpeningBar = Rect{};
  const std::string text = deck::upper(message ? message : "");
  const int font = SMALL_FONT_ID;
  const int maxW = renderer.getScreenWidth() - 2 * kSide;
  const std::string shown = renderer.truncatedText(font, text.c_str(), maxW - 2 * kToastPadX, EpdFontFamily::BOLD);
  const int tw = deck::trackedWidth(renderer, font, shown.c_str(), 2, true);
  const int lineH = renderer.getLineHeight(font);
  const int w = tw + 2 * kToastPadX;
  const int h = lineH + 2 * kToastPadY + 10;  // room for an optional progress strip
  const int x = (renderer.getScreenWidth() - w) / 2;
  const int y = renderer.getScreenHeight() - 66 - h;
  // White halo so the toast reads over page text.
  fillToastShape(renderer, x - 4, y - 3, w + 8, h + 6, false);
  fillToastShape(renderer, x, y, w, h, true);
  deck::drawTracked(renderer, font, x + kToastPadX, y + kToastPadY, shown.c_str(), 2, false, true);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}

void DeckTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const int p = std::clamp(progress, 0, 100);
  if (gOpeningBar.width > 0) {
    const Rect& b = gOpeningBar;
    renderer.fillRect(b.x - 4, b.y, b.width + 8, b.height, false);
    deck::segments(renderer, b.x, b.y, b.width, b.height, 6, std::max(1, (p * 6 + 50) / 100));
  } else {
    const int sx = layout.x + kToastPadX;
    const int sw = layout.width - 2 * kToastPadX;
    const int sy = layout.y + layout.height - kToastPadY - 6;
    deck::segments(renderer, sx, sy, sw, 6, 10, (p + 5) / 10, false);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

Rect DeckTheme::drawBookOpening(const GfxRenderer& renderer, const char* title) const {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  constexpr int kPad = 24;
  renderer.clearScreen();
  int y = std::min(120, H / 7);
  deck::drawTracked(renderer, SMALL_FONT_ID, kPad, y, "// OPENING", 3, true, true);
  y += renderer.getLineHeight(SMALL_FONT_ID) + 10;

  const int size = 31;
  const int font = deck::displayFont(size);
  const auto lines = deck::wrapDisplay(renderer, font, title ? title : "", W - 2 * kPad, 3);
  const int lh = deck::displayLineStep(size);
  int ty = y - deck::displayCapOffset(renderer, size);
  for (const auto& l : lines) {
    renderer.drawText(font, kPad, ty, l.c_str());
    ty += lh;
  }
  y += static_cast<int>(lines.size()) * lh + 30;
  if (y + kFoxRun.height < H - 110) drawSprite(renderer, kFoxRun, (W - kFoxRun.width) / 2, y);

  const int msgY = H - 40 - renderer.getLineHeight(SMALL_FONT_ID);
  const int barY = msgY - 20;
  gOpeningBar = Rect{kPad, barY, W - 2 * kPad, 10};
  deck::segments(renderer, kPad, barY, W - 2 * kPad, 10, 6, 1);
  deck::drawTracked(renderer, SMALL_FONT_ID, kPad, msgY, deck::upper(tr(STR_INDEXING)).c_str(), 2, true, true);
  renderer.displayBuffer();
  return Rect{0, 0, W, H};
}

// ================================================================ reader status bar

namespace {
constexpr int kTopLinePad = 14;  // viewable top to the line's text
}

int DeckTheme::readerTopReserve() const {
  if (!deck::listsActive()) return 0;
  return kTopLinePad + 16 + 10;  // text line + gap above the page
}

void DeckTheme::drawReaderTopLine(GfxRenderer& renderer, const std::string& book, const std::string& chapter) const {
  int mTop, mRight, mBottom, mLeft;
  renderer.getOrientedViewableTRBL(&mTop, &mRight, &mBottom, &mLeft);
  const int font = SMALL_FONT_ID;
  const int left = mLeft + 28;
  const int right = renderer.getScreenWidth() - mRight - 28;
  const int y = mTop + kTopLinePad;
  const int lh = renderer.getLineHeight(font);
  int rx = right;
  const auto drawRight = [&](const char* t) {
    const int w = deck::trackedWidth(renderer, font, t, 1, true);
    rx -= w;
    deck::drawTracked(renderer, font, rx, y, t, 1, true, true);
    rx -= 10;
  };
  char bat[8];
  snprintf(bat, sizeof(bat), "%d%%", std::min<int>(100, powerManager.getBatteryPercentage()));
  drawRight(bat);
  char clock[10];
  if (halClock.isAvailable() && halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1)) drawRight(clock);
  // "BOOK · CHAPTER" when it fits, else the chapter alone.
  const int maxW = (rx - left) * 4 / 5;
  std::string label = deck::upper(book);
  if (!chapter.empty()) {
    const std::string both = label + " \xc2\xb7 " + deck::upper(chapter);
    label = deck::trackedWidth(renderer, font, both.c_str(), 2, true) <= maxW ? both : deck::upper(chapter);
  }
  const std::string shown = renderer.truncatedText(font, label.c_str(), maxW, EpdFontFamily::BOLD);
  deck::drawTracked(renderer, font, left, y, shown.c_str(), 2, true, true);
  const int lx = left + deck::trackedWidth(renderer, font, shown.c_str(), 2, true) + 8;
  if (rx - lx > 10) renderer.fillRect(lx, y + lh - 6, rx - lx, 2, true);
  topLineDrawn = true;
}

void DeckTheme::drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage,
                              const int pageCount, std::string title, const int paddingBottom, const int textYOffset,
                              const bool fillMargin, const bool isPageBookmarked, const bool pageCountEstimated) const {
  const auto& m = ExtraMetrics::deck;
  int mTop, mRight, mBottom, mLeft;
  renderer.getOrientedViewableTRBL(&mTop, &mRight, &mBottom, &mLeft);
  const auto sb = SETTINGS.statusBarSpec();
  const bool textLane = sb.textLaneVisible(halClock.isAvailable());
  const int corner = BoardConfig::ACTIVE.bottomCornerRadius != 0 ? BoardConfig::ACTIVE.bottomCornerRadius
                                                                 : BoardConfig::ACTIVE.cornerRadius;
  const int left = std::max(mLeft, corner) + 28;
  const int right = renderer.getScreenWidth() - std::max(mRight, corner) - 28;
  const int bottom = renderer.getScreenHeight() - mBottom - paddingBottom + BoardConfig::ACTIVE.statusBarOffsetY;
  const int font = SMALL_FONT_ID;
  const int lineH = renderer.getLineHeight(font);
  const int textY = bottom - 4 - lineH;
  (void)fillMargin;

  // Progress line: 2 px rule, 6 px filled run, 12 px diamond at the position.
  if (sb.showsProgressBar()) {
    float pct;
    if (sb.progressBarMode == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS) {
      pct = bookProgress;
    } else {
      pct = pageCount > 0 ? 100.0f * currentPage / pageCount : 0;
    }
    pct = std::clamp(pct, 0.0f, 100.0f);
    const int laneTop = textLane ? textY - m.progressBarMarginTop - sb.progressBarHeightPx : bottom - 14;
    const int cy = laneTop + 6;
    const int w = right - left;
    const int px = left + static_cast<int>(w * pct / 100.0f);
    renderer.fillRect(left, cy - 1, w, 2, true);
    if (px > left) renderer.fillRect(left, cy - 3, px - left, 6, true);
    const int xs[] = {px, px + 6, px, px - 6};
    const int ys[] = {cy - 6, cy, cy + 6, cy};
    renderer.fillPolygon(xs, ys, 4, true);
  }
  if (!textLane) return;

  // Left: page n/N (+ "~" while the count is an estimate).
  int lx = left;
  if (sb.showChapterPageCount) {
    char pages[24];
    snprintf(pages, sizeof(pages), "%s%d/%d", pageCountEstimated ? "~" : "", currentPage, pageCount);
    deck::drawTracked(renderer, font, lx, textY, pages, 1, true, true);
    lx += deck::trackedWidth(renderer, font, pages, 1, true) + 14;
  }

  const bool topLine = topLineDrawn;
  topLineDrawn = false;

  // Right: clock, battery, book % (clock and battery sit in the top line when it is drawn).
  int rx = right;
  const auto drawRight = [&](const char* t) {
    const int w = deck::trackedWidth(renderer, font, t, 1, true);
    rx -= w;
    deck::drawTracked(renderer, font, rx, textY, t, 1, true, true);
    rx -= 14;
  };
  if (sb.showBookProgressPercent) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%.0f%%", bookProgress);
    drawRight(pct);
  }
  if (sb.showBattery && !topLine) {
    const int percent = std::min<int>(100, powerManager.getBatteryPercentage());
    if (sb.showBatteryPercent) {
      char bat[8];
      snprintf(bat, sizeof(bat), "%d%%", percent);
      drawRight(bat);
    }
    rx -= 4 * 8 + 2;
    batteryCells(renderer, rx, textY + (lineH - 12) / 2, percent);
    rx -= 12;
  }
  if (sb.showsClock() && halClock.isAvailable() && !topLine) {
    char clock[10];
    if (halClock.formatTime(clock, sizeof(clock), sb.clock12h)) drawRight(clock);
  }

  // Middle: bookmark mark, else the title.
  std::string mid;
  if (isPageBookmarked) {
    mid = "MARKED";
  } else if (!title.empty() && !topLine) {
    mid = deck::upper(title);
  }
  if (!mid.empty() && rx - lx > 40) {
    const std::string t = renderer.truncatedText(font, mid.c_str(), rx - lx - 20, EpdFontFamily::BOLD);
    const int tw = deck::trackedWidth(renderer, font, t.c_str(), 1, true);
    int tx = (left + right - tw) / 2;
    tx = std::clamp(tx, lx + 10, std::max(lx + 10, rx - tw - 10));
    int markW = 0;
    if (isPageBookmarked) {
      // ◆ before the label.
      const int cy = textY + lineH / 2;
      const int xs[] = {tx, tx + 5, tx, tx - 5};
      const int ys[] = {cy - 5, cy, cy + 5, cy};
      renderer.fillPolygon(xs, ys, 4, true);
      markW = 12;
    }
    deck::drawTracked(renderer, font, tx + markW, textY - textYOffset, t.c_str(), 1, true, true);
  }
}
