#include "ExtraThemes.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// ---------------------------------------------------------------- helpers

std::string upperAscii(const char* text) {
  std::string out = text ? text : "";
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

const char* romanNumeral(const int n) {
  static const char* const kRoman[] = {"I",   "II",   "III", "IV", "V",   "VI",   "VII",
                                       "VIII", "IX",  "X",   "XI", "XII", "XIII", "XIV"};
  return (n >= 1 && n <= 14) ? kRoman[n - 1] : "";
}

// Front-button hint columns (portrait). Same geometry as BaseTheme::drawButtonHints.
struct HintColumns {
  int x[4];
  int width;
};

HintColumns hintColumns(const GfxRenderer& renderer) {
  if (renderer.getScreenWidth() >= 528) return {{38, 154, 268, 384}, 106};
  return {{25, 130, 245, 350}, 106};
}

// Grayscale passes must not repaint the hints; protect their area and stop.
bool hintGrayPass(const GfxRenderer& renderer) {
  return renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();
}

// Octagon with cut corners of size c.
void chamferPoints(const int x, const int y, const int w, const int h, const int c, int* xs, int* ys) {
  const int px[] = {x + c, x + w - 1 - c, x + w - 1, x + w - 1, x + w - 1 - c, x + c, x, x};
  const int py[] = {y, y, y + c, y + h - 1 - c, y + h - 1, y + h - 1, y + h - 1 - c, y + c};
  for (int i = 0; i < 8; ++i) {
    xs[i] = px[i];
    ys[i] = py[i];
  }
}

void fillChamfer(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int c,
                 const bool black = true) {
  int xs[8], ys[8];
  chamferPoints(x, y, w, h, c, xs, ys);
  r.fillPolygon(xs, ys, 8, black);
}

void drawChamfer(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int c,
                 const int thickness = 1) {
  for (int t = 0; t < thickness; ++t) {
    int xs[8], ys[8];
    chamferPoints(x + t, y + t, w - 2 * t, h - 2 * t, std::max(0, c - t / 2), xs, ys);
    for (int i = 0; i < 8; ++i) {
      r.drawLine(xs[i], ys[i], xs[(i + 1) % 8], ys[(i + 1) % 8], true);
    }
  }
}

// Diagonal hazard stripes inside a rectangle.
void hatch(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int period,
           const int ink) {
  for (int yy = 0; yy < h; ++yy) {
    for (int xx = 0; xx < w; ++xx) {
      if ((xx + yy) % period < ink) r.drawPixel(x + xx, y + yy, true);
    }
  }
}

// Horizontal scanlines every `period` rows.
void scanlines(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int period) {
  if (w <= 0) return;
  for (int yy = y; yy < y + h; yy += period) r.drawLine(x, yy, x + w - 1, yy, true);
}

// L-shaped corner marks around a rectangle.
void cornerBrackets(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int len,
                    const int t) {
  r.fillRect(x, y, len, t);
  r.fillRect(x, y, t, len);
  r.fillRect(x + w - len, y, len, t);
  r.fillRect(x + w - t, y, t, len);
  r.fillRect(x, y + h - t, len, t);
  r.fillRect(x, y + h - len, t, len);
  r.fillRect(x + w - len, y + h - t, len, t);
  r.fillRect(x + w - t, y + h - len, t, len);
}

void diamond(const GfxRenderer& r, const int cx, const int cy, const int size, const bool black = true) {
  const int xs[] = {cx, cx + size, cx, cx - size};
  const int ys[] = {cy - size, cy, cy + size, cy};
  r.fillPolygon(xs, ys, 4, black);
}

// Dotted leader from x1 to x2 on baseline y.
void leader(const GfxRenderer& r, const int x1, const int x2, const int y, const int step = 6) {
  for (int x = x1; x <= x2; x += step) r.fillRect(x, y, 2, 2);
}

void drawCenteredIn(const GfxRenderer& r, const int fontId, const int x, const int w, const int y, const char* text,
                    const bool black = true, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string t = r.truncatedText(fontId, text, w, style);
  r.drawText(fontId, x + (w - r.getTextWidth(fontId, t.c_str(), style)) / 2, y, t.c_str(), black, style);
}

// Home passes a short band (and the book title); these themes show the book in the tile instead.
bool isHomeBand(const Rect& rect) { return rect.height < UITheme::getInstance().getMetrics().headerHeight; }

// Bottom of the header band the base theme draws.
int headerRuleY(const Rect& rect) { return rect.y + rect.height - 4; }

}  // namespace

// ================================================================ shared base

int ExtraThemeBase::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(UI_12_FONT_ID) + menuRowPadding();
}

int ExtraThemeBase::drawCoverInSlot(const GfxRenderer& renderer, const RecentBook& book, const Rect slot,
                                    const int thumbHeight) {
  if (book.coverBmpPath.empty()) return 0;
  const std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
  HalFile file;
  if (!Storage.openFileForRead("HOME", path, file)) return 0;
  int width = 0;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    width = std::min(bitmap.getWidth(), slot.width);
    drawCoverThumbFill(renderer, bitmap, Rect{slot.x + (slot.width - width) / 2, slot.y, width, slot.height});
  }
  file.close();
  return width;
}

void ExtraThemeBase::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                         const std::vector<RecentBook>& recentBooks, int, bool& coverRendered,
                                         bool& coverBufferStored, bool& bufferRestored,
                                         std::function<bool()> storeCoverBuffer) const {
  if (bufferRestored) return;
  if (coverRendered && coverBufferStored) return;
  drawHomeTile(renderer, rect, recentBooks.empty() ? nullptr : &recentBooks[0]);
  coverBufferStored = storeCoverBuffer();
  coverRendered = coverBufferStored;
}

// ================================================================ Cyberdeck

void CyberdeckTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle,
                                const bool backButton) const {
  if (title == nullptr || isHomeBand(rect)) {
    BaseTheme::drawHeader(renderer, rect, nullptr, subtitle, backButton);
    return;
  }
  const std::string prompt = "> " + upperAscii(title) + "_";
  BaseTheme::drawHeader(renderer, rect, prompt.c_str(), subtitle, backButton);
  const int y = headerRuleY(rect);
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  renderer.fillRect(x, y, w, 2);
  // Status pips at the right end of the rule.
  for (int i = 0; i < 3; ++i) renderer.fillRect(x + w - 10 - i * 14, y - 6, 10, 4);
}

void CyberdeckTheme::drawHomeTile(GfxRenderer& renderer, const Rect rect, const RecentBook* book) const {
  const auto& m = ExtraMetrics::cyberdeck;
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int y = rect.y;
  const int h = rect.height;
  cornerBrackets(renderer, x, y, w, h, 22, 3);

  const int labelY = y + 8;
  renderer.drawText(SMALL_FONT_ID, x + 14, labelY, book ? "// NOW_READING" : "// IDLE");
  renderer.drawText(SMALL_FONT_ID, x + w - 14 - renderer.getTextWidth(SMALL_FONT_ID, "SYS.OK"), labelY, "SYS.OK");

  const int coverTop = labelY + renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int coverH = m.homeCoverHeight - 30;
  int coverW = 0;
  if (book) {
    coverW = drawCoverInSlot(renderer, *book, Rect{x, coverTop, w, coverH}, m.homeCoverHeight);
  }
  if (coverW == 0) {
    // No image: a dead monitor.
    coverW = coverH * 2 / 3;
    const int cx = x + (w - coverW) / 2;
    renderer.drawRect(cx, coverTop, coverW, coverH, 2, true);
    scanlines(renderer, cx + 6, coverTop + 6, coverW - 12, coverH - 12, 3);
    const char* msg = book ? "NO IMAGE" : "NO SIGNAL";
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, msg, EpdFontFamily::BOLD);
    const int th = renderer.getLineHeight(UI_12_FONT_ID);
    const int my = coverTop + (coverH - th) / 2;
    renderer.fillRect(x + (w - tw) / 2 - 10, my - 4, tw + 20, th + 8, false);
    renderer.drawRect(x + (w - tw) / 2 - 10, my - 4, tw + 20, th + 8, 2, true);
    renderer.drawText(UI_12_FONT_ID, x + (w - tw) / 2, my, msg, true, EpdFontFamily::BOLD);
  } else {
    renderer.drawRect(x + (w - coverW) / 2 - 3, coverTop - 3, coverW + 6, coverH + 6, 1, true);
  }
  // Scanlines either side of the screen.
  const int sideW = (w - coverW) / 2 - 22;
  scanlines(renderer, x + 12, coverTop, sideW, coverH, 4);
  scanlines(renderer, x + w - 12 - sideW, coverTop, sideW, coverH, 4);

  const int textY = coverTop + coverH + 10;
  if (book) {
    const std::string t = "$ cat \"" + book->title + "\"";
    drawCenteredIn(renderer, UI_10_FONT_ID, x + 10, w - 20, textY, t.c_str(), true, EpdFontFamily::BOLD);
  } else {
    drawCenteredIn(renderer, UI_10_FONT_ID, x + 10, w - 20, textY, "$ awaiting input_");
  }
}

void CyberdeckTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                    const int selectedIndex, const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>&) const {
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int rowH = getMenuRowHeight(renderer);
  const int step = rowH + ExtraMetrics::cyberdeck.menuSpacing;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  // Left rail.
  renderer.fillRect(x, rect.y, 2, std::min(rect.height, buttonCount * step - ExtraMetrics::cyberdeck.menuSpacing));
  for (int i = 0; i < buttonCount; ++i) {
    const int y = rect.y + i * step;
    if (y + rowH > rect.y + rect.height) break;
    const bool sel = i == selectedIndex;
    char idx[8];
    snprintf(idx, sizeof(idx), "%02d", i + 1);
    const std::string label = upperAscii(buttonLabel(i).c_str());
    const int ty = y + (rowH - lineH) / 2;
    if (sel) {
      renderer.fillRect(x + 6, y, w - 6, rowH, true);
      renderer.drawText(UI_12_FONT_ID, x + 16, ty, ">", false, EpdFontFamily::BOLD);
    }
    const int idxX = x + 40;
    renderer.drawText(UI_10_FONT_ID, idxX, ty + 2, idx, !sel);
    const int labelX = idxX + renderer.getTextWidth(UI_10_FONT_ID, "00") + 16;
    const std::string shown = renderer.truncatedText(UI_12_FONT_ID, label.c_str(), x + w - 30 - labelX,
                                                     sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_12_FONT_ID, labelX, ty, shown.c_str(), !sel, sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (sel) {
      // Block cursor.
      renderer.fillRect(x + w - 22, y + (rowH - lineH) / 2 + 2, 10, lineH - 4, false);
    }
  }
}

void CyberdeckTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4) const {
  if (gpio.hasTouch()) return;
  const auto orig = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int pageW = renderer.getScreenWidth();
  const int barH = 32;
  const int barY = renderer.getScreenHeight() - barH - 4;
  if (hintGrayPass(renderer)) {
    renderer.fillRect(0, barY, pageW, barH, true);
    renderer.setOrientation(orig);
    return;
  }
  renderer.fillRect(0, barY, pageW, barH, true);
  const auto cols = hintColumns(renderer);
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const int ty = barY + (barH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  for (int i = 0; i < 4; ++i) {
    if (i > 0) renderer.fillRect(cols.x[i] - 1, barY + 8, 2, barH - 16, false);
    if (!labels[i] || !labels[i][0]) continue;
    const std::string t = upperAscii(labels[i]);
    drawCenteredIn(renderer, UI_10_FONT_ID, cols.x[i] + 4, cols.width - 8, ty, t.c_str(), false);
  }
  renderer.setOrientation(orig);
}

// ================================================================ Netrunner

void NetrunnerTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle,
                                const bool backButton) const {
  if (title == nullptr || isHomeBand(rect)) {
    BaseTheme::drawHeader(renderer, rect, nullptr, subtitle, backButton);
    return;
  }
  const std::string t = "// " + upperAscii(title);
  BaseTheme::drawHeader(renderer, rect, t.c_str(), subtitle, backButton);
  const int y = headerRuleY(rect);
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int stripeW = w * 2 / 5;
  // Rule that steps up into a hazard-striped block on the right.
  renderer.fillRect(x, y, w - stripeW - 10, 2);
  const int sx = x + w - stripeW;
  renderer.drawLine(x + w - stripeW - 10, y, sx, y - 6, 2, true);
  hatch(renderer, sx, y - 6, stripeW, 8, 8, 4);
  renderer.drawRect(sx, y - 6, stripeW, 8, 1, true);
}

void NetrunnerTheme::drawHomeTile(GfxRenderer& renderer, const Rect rect, const RecentBook* book) const {
  const auto& m = ExtraMetrics::netrunner;
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int y = rect.y;
  const int h = rect.height;
  drawChamfer(renderer, x, y, w, h, 22, 2);

  // Tab label notched into the top edge.
  const char* tag = book ? "JACK_IN::READ" : "JACK_IN::IDLE";
  const int tagW = renderer.getTextWidth(SMALL_FONT_ID, tag) + 28;
  fillChamfer(renderer, x + 34, y - 1, tagW, renderer.getLineHeight(SMALL_FONT_ID) + 8, 8, true);
  renderer.drawText(SMALL_FONT_ID, x + 48, y + 3, tag, false);

  const int coverTop = y + 34;
  const int coverH = m.homeCoverHeight - 20;
  int coverW = 0;
  if (book) coverW = drawCoverInSlot(renderer, *book, Rect{x, coverTop, w, coverH}, m.homeCoverHeight);
  const int cw = coverW ? coverW : coverH * 2 / 3;
  const int cx = x + (w - cw) / 2;
  if (coverW == 0) {
    hatch(renderer, cx, coverTop, cw, coverH, 12, 2);
    const char* msg = book ? "NO DATA" : "OFFLINE";
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, msg) + 24;
    const int th = renderer.getLineHeight(UI_12_FONT_ID) + 10;
    fillChamfer(renderer, x + (w - tw) / 2, coverTop + (coverH - th) / 2, tw, th, 8, true);
    renderer.drawText(UI_12_FONT_ID, x + (w - tw) / 2 + 12, coverTop + (coverH - th) / 2 + 5, msg, false);
  }
  // HUD crosshair corners around the cover.
  cornerBrackets(renderer, cx - 8, coverTop - 8, cw + 16, coverH + 16, 14, 2);
  // Side gauges.
  for (int i = 0; i < 8; ++i) {
    const int gy = coverTop + 10 + i * (coverH - 20) / 8;
    const int len = 8 + (i * 5) % 18;
    renderer.fillRect(x + 16, gy, len, 3);
    renderer.fillRect(x + w - 16 - len, gy, len, 3);
  }

  const int textY = coverTop + coverH + 14;
  const std::string line = book ? upperAscii(book->title.c_str()) : std::string("NO ACTIVE LINK");
  drawCenteredIn(renderer, UI_10_FONT_ID, x + 24, w - 48, textY, line.c_str());
}

void NetrunnerTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                    const int selectedIndex, const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>&) const {
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int rowH = getMenuRowHeight(renderer);
  const int step = rowH + ExtraMetrics::netrunner.menuSpacing + 2;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < buttonCount; ++i) {
    const int y = rect.y + i * step;
    if (y + rowH > rect.y + rect.height) break;
    const bool sel = i == selectedIndex;
    if (sel) {
      fillChamfer(renderer, x, y, w, rowH, 12, true);
    } else {
      drawChamfer(renderer, x, y, w, rowH, 12, 1);
    }
    const int ty = y + (rowH - lineH) / 2;
    const std::string label = upperAscii(buttonLabel(i).c_str());
    renderer.drawText(UI_12_FONT_ID, x + 20, ty, sel ? ">>" : "-", !sel);
    const int lx = x + 56;
    char hex[8];
    snprintf(hex, sizeof(hex), "0x%02X", i + 1);
    const int hexW = renderer.getTextWidth(SMALL_FONT_ID, hex);
    const std::string shown = renderer.truncatedText(UI_12_FONT_ID, label.c_str(), w - 56 - hexW - 30);
    renderer.drawText(UI_12_FONT_ID, lx, ty, shown.c_str(), !sel);
    renderer.drawText(SMALL_FONT_ID, x + w - 18 - hexW, y + (rowH - renderer.getLineHeight(SMALL_FONT_ID)) / 2, hex,
                      !sel);
  }
}

void NetrunnerTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4) const {
  if (gpio.hasTouch()) return;
  const auto orig = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int h = 32;
  const int y = renderer.getScreenHeight() - h - 6;
  const auto cols = hintColumns(renderer);
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool gray = hintGrayPass(renderer);
  for (int i = 0; i < 4; ++i) {
    if (!labels[i] || !labels[i][0]) continue;
    if (gray) {
      renderer.fillRect(cols.x[i], y, cols.width, h, true);
      continue;
    }
    fillChamfer(renderer, cols.x[i] + 3, y, cols.width - 6, h, 8, false);
    drawChamfer(renderer, cols.x[i] + 3, y, cols.width - 6, h, 8, 2);
    const std::string t = upperAscii(labels[i]);
    drawCenteredIn(renderer, UI_10_FONT_ID, cols.x[i] + 6, cols.width - 12,
                   y + (h - renderer.getLineHeight(UI_10_FONT_ID)) / 2, t.c_str());
  }
  renderer.setOrientation(orig);
}

// ================================================================ Folio

void FolioTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle,
                            const bool backButton) const {
  if (isHomeBand(rect)) title = nullptr;
  BaseTheme::drawHeader(renderer, rect, title, subtitle, backButton);
  if (title == nullptr) return;
  // Printer's double rule: thick over thin, with a centred fleuron.
  const int y = headerRuleY(rect) - 2;
  const int x = rect.x + 24;
  const int w = rect.width - 48;
  renderer.fillRect(x, y, w, 2);
  renderer.fillRect(x, y + 5, w, 1);
  const char* fleuron = "\xE2\x9D\xA6";  // U+2766
  const int fw = renderer.getTextWidth(UI_10_FONT_ID, fleuron);
  const int fx = rect.x + (rect.width - fw) / 2;
  const int fy = y + 3 - renderer.getLineHeight(UI_10_FONT_ID) / 2;
  renderer.fillRect(fx - 8, y - 2, fw + 16, 10, false);
  renderer.drawText(UI_10_FONT_ID, fx, fy, fleuron);
}

void FolioTheme::drawHomeTile(GfxRenderer& renderer, const Rect rect, const RecentBook* book) const {
  const auto& m = ExtraMetrics::folio;
  const int x = rect.x + 20;
  const int w = rect.width - 40;
  int y = rect.y + 4;

  // "Now reading" caption between short rules.
  const char* caption = book ? "now reading" : "an empty shelf";
  const int capW = renderer.getTextWidth(UI_10_FONT_ID, caption);
  const int capY = y;
  const int midY = capY + renderer.getLineHeight(UI_10_FONT_ID) / 2 + 2;
  renderer.drawText(UI_10_FONT_ID, x + (w - capW) / 2, capY, caption);
  renderer.fillRect(x + w / 2 - capW / 2 - 70, midY, 54, 1);
  renderer.fillRect(x + w / 2 + capW / 2 + 16, midY, 54, 1);
  y = capY + renderer.getLineHeight(UI_10_FONT_ID) + 12;

  const int coverH = m.homeCoverHeight;
  int coverW = 0;
  if (book) coverW = drawCoverInSlot(renderer, *book, Rect{x, y, w, coverH}, m.homeCoverHeight);
  const int cw = coverW ? coverW : coverH * 2 / 3;
  const int cx = x + (w - cw) / 2;
  if (coverW == 0) {
    // A plain title page.
    renderer.drawRect(cx, y, cw, coverH, 1, true);
    renderer.drawRect(cx + 6, y + 6, cw - 12, coverH - 12, 1, true);
    const char* asterism = "\xE2\x81\x82";  // U+2042
    drawCenteredIn(renderer, UI_12_FONT_ID, cx, cw, y + coverH / 2 - renderer.getLineHeight(UI_12_FONT_ID) / 2,
                   asterism);
  } else {
    // Thin plate frame around the cover.
    renderer.drawRect(cx - 6, y - 6, cw + 12, coverH + 12, 1, true);
  }
  y += coverH + 16;

  if (book) {
    drawCenteredIn(renderer, UI_12_FONT_ID, x, w, y, book->title.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
    if (!book->author.empty()) drawCenteredIn(renderer, UI_10_FONT_ID, x, w, y, book->author.c_str());
  } else {
    drawCenteredIn(renderer, UI_12_FONT_ID, x, w, y, tr(STR_NO_OPEN_BOOK));
  }
}

void FolioTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                const int selectedIndex, const std::function<std::string(int index)>& buttonLabel,
                                const std::function<UIIcon(int index)>&) const {
  // A table of contents: roman numeral, title, dotted leader.
  const int x = rect.x + 28;
  const int w = rect.width - 56;
  const int rowH = getMenuRowHeight(renderer);
  const int step = rowH + ExtraMetrics::folio.menuSpacing;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int numW = renderer.getTextWidth(UI_12_FONT_ID, "VIII.");
  for (int i = 0; i < buttonCount; ++i) {
    const int y = rect.y + i * step;
    if (y + rowH > rect.y + rect.height) break;
    const bool sel = i == selectedIndex;
    const auto style = sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int ty = y + (rowH - lineH) / 2;
    const std::string num = std::string(romanNumeral(i + 1)) + ".";
    renderer.drawText(UI_12_FONT_ID, x + numW - renderer.getTextWidth(UI_12_FONT_ID, num.c_str()), ty, num.c_str());
    const int lx = x + numW + 14;
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, buttonLabel(i).c_str(), w - numW - 60, style);
    renderer.drawText(UI_12_FONT_ID, lx, ty, label.c_str(), true, style);
    const int lw = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), style);
    const int baseY = ty + renderer.getTextHeight(UI_12_FONT_ID);
    leader(renderer, lx + lw + 10, x + w - 4, baseY - 2, sel ? 4 : 7);
    if (sel) {
      const char* hand = "\xE2\x9D\xA7";  // U+2767 rotated floral heart
      const int numX = x + numW - renderer.getTextWidth(UI_12_FONT_ID, num.c_str());
      renderer.drawText(UI_12_FONT_ID, numX - 8 - renderer.getTextWidth(UI_12_FONT_ID, hand), ty, hand);
    }
  }
}

void FolioTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                 const char* btn4) const {
  if (gpio.hasTouch()) return;
  const auto orig = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int h = 30;
  const int y = renderer.getScreenHeight() - h - 4;
  const int pageW = renderer.getScreenWidth();
  if (hintGrayPass(renderer)) {
    renderer.fillRect(16, y - 6, pageW - 32, h + 6, true);
    renderer.setOrientation(orig);
    return;
  }
  renderer.fillRect(16, y - 6, pageW - 32, h + 6, false);
  renderer.fillRect(24, y - 4, pageW - 48, 1);
  const auto cols = hintColumns(renderer);
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const int ty = y + (h - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  for (int i = 0; i < 4; ++i) {
    if (i > 0) diamond(renderer, cols.x[i] - 5, ty + renderer.getLineHeight(UI_10_FONT_ID) / 2, 2);
    if (!labels[i] || !labels[i][0]) continue;
    drawCenteredIn(renderer, UI_10_FONT_ID, cols.x[i], cols.width, ty, labels[i]);
  }
  renderer.setOrientation(orig);
}

// ================================================================ Ex Libris

void ExLibrisTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle,
                               const bool backButton) const {
  if (isHomeBand(rect)) title = nullptr;
  BaseTheme::drawHeader(renderer, rect, title, subtitle, backButton);
  if (title == nullptr) return;
  // Rule broken by a lozenge.
  const int y = headerRuleY(rect);
  const int x = rect.x + 30;
  const int w = rect.width - 60;
  const int cx = rect.x + rect.width / 2;
  renderer.fillRect(x, y, w / 2 - 16, 1);
  renderer.fillRect(cx + 16, y, w / 2 - 16, 1);
  diamond(renderer, cx, y, 6);
  diamond(renderer, cx - 13, y, 2);
  diamond(renderer, cx + 13, y, 2);
}

void ExLibrisTheme::drawHomeTile(GfxRenderer& renderer, const Rect rect, const RecentBook* book) const {
  const auto& m = ExtraMetrics::exlibris;
  const int x = rect.x + 14;
  const int w = rect.width - 28;
  const int y = rect.y;
  const int h = rect.height;
  // Bookplate: double frame with rounded corners.
  renderer.drawRoundedRect(x, y, w, h, 2, 16, true);
  renderer.drawRoundedRect(x + 6, y + 6, w - 12, h - 12, 1, 11, true);

  int cy = y + 16;
  const char* heading = "E X   L I B R I S";
  drawCenteredIn(renderer, UI_12_FONT_ID, x, w, cy, heading, true, EpdFontFamily::BOLD);
  cy += renderer.getLineHeight(UI_12_FONT_ID) + 8;
  const int mid = x + w / 2;
  renderer.fillRect(mid - 60, cy, 46, 1);
  renderer.fillRect(mid + 14, cy, 46, 1);
  diamond(renderer, mid, cy, 4);
  cy += 10;

  const int coverH = m.homeCoverHeight;
  int coverW = 0;
  if (book) coverW = drawCoverInSlot(renderer, *book, Rect{x + 20, cy, w - 40, coverH}, m.homeCoverHeight);
  if (coverW == 0) {
    const int cw = coverH * 2 / 3;
    const int cx = mid - cw / 2;
    renderer.fillRoundedRect(cx, cy, cw, coverH, 6, Color::LightGray);
    renderer.drawRoundedRect(cx, cy, cw, coverH, 1, 6, true);
    renderer.fillRect(cx + 14, cy, 2, coverH);  // spine line
  }
  cy += coverH + 12;
  const std::string line = book ? book->title : std::string(tr(STR_NO_OPEN_BOOK));
  drawCenteredIn(renderer, UI_10_FONT_ID, x + 24, w - 48, cy, line.c_str(), true,
                 book ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  if (book && !book->author.empty()) {
    cy += renderer.getLineHeight(UI_10_FONT_ID);
    drawCenteredIn(renderer, SMALL_FONT_ID, x + 24, w - 48, cy, book->author.c_str());
  }
}

void ExLibrisTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                   const int selectedIndex, const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>&) const {
  const int x = rect.x + 40;
  const int w = rect.width - 80;
  const int rowH = getMenuRowHeight(renderer);
  const int step = rowH + ExtraMetrics::exlibris.menuSpacing;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < buttonCount; ++i) {
    const int y = rect.y + i * step;
    if (y + rowH > rect.y + rect.height) break;
    const bool sel = i == selectedIndex;
    const auto style = sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    if (sel) {
      renderer.fillRoundedRect(x, y, w, rowH, rowH / 2, Color::LightGray);
      renderer.drawRoundedRect(x, y, w, rowH, 1, rowH / 2, true);
    }
    drawCenteredIn(renderer, UI_12_FONT_ID, x + 20, w - 40, y + (rowH - lineH) / 2, buttonLabel(i).c_str(), true,
                   style);
  }
}

void ExLibrisTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                    const char* btn4) const {
  if (gpio.hasTouch()) return;
  const auto orig = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int h = 30;
  const int y = renderer.getScreenHeight() - h;
  const auto cols = hintColumns(renderer);
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool gray = hintGrayPass(renderer);
  for (int i = 0; i < 4; ++i) {
    if (!labels[i] || !labels[i][0]) continue;
    if (gray) {
      renderer.fillRect(cols.x[i], y, cols.width, h, true);
      continue;
    }
    // Card-catalogue tab: rounded top, open bottom.
    renderer.fillRoundedRect(cols.x[i] + 3, y, cols.width - 6, h + 8, 10, true, true, false, false, Color::White);
    renderer.drawRoundedRect(cols.x[i] + 3, y, cols.width - 6, h + 8, 1, 10, true, true, false, false, true);
    drawCenteredIn(renderer, UI_10_FONT_ID, cols.x[i] + 6, cols.width - 12,
                   y + (h - renderer.getLineHeight(UI_10_FONT_ID)) / 2 + 2, labels[i]);
  }
  renderer.setOrientation(orig);
}
