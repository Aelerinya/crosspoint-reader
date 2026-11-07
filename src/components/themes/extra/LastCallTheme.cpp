#include "LastCallTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int kRadius = 8;

std::string upperAscii(const std::string& in) {
  std::string out = in;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

void centeredIn(const GfxRenderer& r, const int fontId, const int x, const int w, const int y, const char* text,
                const bool black = true) {
  const std::string t = r.truncatedText(fontId, text, w);
  r.drawText(fontId, x + (w - r.getTextWidth(fontId, t.c_str())) / 2, y, t.c_str(), black);
}

// Cocktail glass: bowl, stem, foot, and a cherry. `size` is the bowl width.
void glass(const GfxRenderer& r, const int cx, const int top, const int size, const bool black) {
  const int half = size / 2;
  const int bowlH = size * 3 / 5;
  const int xs[] = {cx - half, cx + half, cx};
  const int ys[] = {top, top, top + bowlH};
  r.fillPolygon(xs, ys, 3, black);
  r.fillRect(cx - 1, top + bowlH, 2, size / 2, black);
  r.fillRect(cx - half / 2 - 1, top + bowlH + size / 2, half + 2, 2, black);
  r.fillRoundedRect(cx + half - 3, top - 5, 6, 6, 3, black ? Color::Black : Color::White);
}

// Three little window buttons, drawn white on the black title bar.
void windowButtons(const GfxRenderer& r, const int rightX, const int y, const int s) {
  for (int i = 0; i < 3; ++i) {
    const int x = rightX - (i + 1) * (s + 5);
    r.drawRect(x, y, s, s, 2, false);
  }
  // A cross in the close box.
  const int x = rightX - (s + 5);
  r.drawLine(x + 3, y + 3, x + s - 4, y + s - 4, 2, false);
  r.drawLine(x + s - 4, y + 3, x + 3, y + s - 4, 2, false);
}

bool isHomeBand(const Rect& rect) { return rect.height < UITheme::getInstance().getMetrics().headerHeight; }

}  // namespace

void LastCallTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle,
                               const bool backButton) const {
  // Status strip (battery, clock) from the base; the title gets a window bar.
  BaseTheme::drawHeader(renderer, rect, nullptr, nullptr, backButton);
  if (title == nullptr || isHomeBand(rect)) return;

  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int barY = rect.y + 24;
  const int barH = rect.height - 30;
  renderer.fillRoundedRect(x, barY, w, barH, kRadius, Color::Black);
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int ty = barY + (barH - lineH) / 2;
  const int btnS = 14;
  const int btnSpace = 3 * (btnS + 5) + 10;
  int textRight = x + w - btnSpace;
  if (subtitle && subtitle[0]) {
    const std::string sub = renderer.truncatedText(UI_10_FONT_ID, subtitle, w / 3);
    const int sw = renderer.getTextWidth(UI_10_FONT_ID, sub.c_str());
    textRight -= sw + 12;
    renderer.drawText(UI_10_FONT_ID, textRight + 6, barY + (barH - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      sub.c_str(), false);
  }
  const std::string t = renderer.truncatedText(UI_12_FONT_ID, ("> " + std::string(title)).c_str(),
                                               textRight - (x + 14));
  renderer.drawText(UI_12_FONT_ID, x + 14, ty, t.c_str(), false);
  windowButtons(renderer, x + w - 8, barY + (barH - btnS) / 2, btnS);
}

void LastCallTheme::drawHomeTile(GfxRenderer& renderer, const Rect rect, const RecentBook* book) const {
  const auto& m = ExtraMetrics::lastcall;
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int y = rect.y;
  const int h = rect.height;
  const int barH = 30;

  // CRT window: title bar, halftone body.
  renderer.fillRoundedRect(x, y, w, h, kRadius, Color::LightGray);
  renderer.fillRoundedRect(x, y, w, barH, kRadius, true, true, false, false, Color::Black);
  renderer.drawRoundedRect(x, y, w, h, 3, kRadius, true);
  const char* label = book ? "NOW POURING" : "BAR CLOSED";
  renderer.drawText(UI_10_FONT_ID, x + 14, y + (barH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, label, false);
  windowButtons(renderer, x + w - 8, y + (barH - 12) / 2, 12);

  const int coverTop = y + barH + 12;
  const int coverH = m.homeCoverHeight - 22;
  int coverW = 0;
  if (book) coverW = drawCoverInSlot(renderer, *book, Rect{x, coverTop, w, coverH}, m.homeCoverHeight);
  const int cw = coverW ? coverW : coverH * 2 / 3;
  const int cx = x + (w - cw) / 2;
  if (coverW == 0) {
    renderer.fillRect(cx, coverTop, cw, coverH, false);
    glass(renderer, cx + cw / 2, coverTop + coverH / 2 - 40, 64, true);
    centeredIn(renderer, UI_12_FONT_ID, cx, cw, coverTop + coverH / 2 + 34, book ? "NO COVER" : "EMPTY");
  }
  // White matte and outline around the cover.
  renderer.drawRect(cx - 4, coverTop - 4, cw + 8, coverH + 8, 3, false);
  renderer.drawRect(cx - 6, coverTop - 6, cw + 12, coverH + 12, 2, true);

  // Title on a white strip.
  const int stripY = coverTop + coverH + 12;
  const int stripH = y + h - 8 - stripY;
  renderer.fillRect(x + 10, stripY, w - 20, stripH, false);
  renderer.drawRect(x + 10, stripY, w - 20, stripH, 1, true);
  const std::string line = book ? book->title : std::string(tr(STR_NO_OPEN_BOOK));
  centeredIn(renderer, UI_12_FONT_ID, x + 18, w - 36, stripY + (stripH - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
             line.c_str());
}

void LastCallTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                   const int selectedIndex, const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>&) const {
  // The drink menu.
  const int x = rect.x + 12;
  const int w = rect.width - 24;
  const int rowH = getMenuRowHeight(renderer);
  const int step = rowH + ExtraMetrics::lastcall.menuSpacing;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < buttonCount; ++i) {
    const int y = rect.y + i * step;
    if (y + rowH > rect.y + rect.height) break;
    const bool sel = i == selectedIndex;
    const int ty = y + (rowH - lineH) / 2;
    if (sel) {
      renderer.fillRoundedRect(x, y, w, rowH, kRadius, Color::Black);
      glass(renderer, x + 24, y + (rowH - 26) / 2 + 3, 22, false);
    } else {
      renderer.drawRoundedRect(x, y, w, rowH, 1, kRadius, true);
      renderer.fillRect(x + 18, y + rowH / 2 - 3, 7, 7, true);
    }
    char no[8];
    snprintf(no, sizeof(no), "No.%02d", i + 1);
    const int noW = renderer.getTextWidth(UI_10_FONT_ID, no);
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, upperAscii(buttonLabel(i)).c_str(),
                                                     w - 50 - noW - 24);
    renderer.drawText(UI_12_FONT_ID, x + 48, ty, label.c_str(), !sel);
    renderer.drawText(UI_10_FONT_ID, x + w - 14 - noW, y + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, no,
                      !sel);
  }
}

void LastCallTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                    const char* btn4) const {
  if (gpio.hasTouch()) return;
  const auto orig = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int h = 28;
  const int lip = 4;
  const int y = renderer.getScreenHeight() - h - lip - 4;
  const int wide = renderer.getScreenWidth() >= 528;
  const int xs[] = {wide ? 38 : 25, wide ? 154 : 130, wide ? 268 : 245, wide ? 384 : 350};
  const int cw = 106;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool gray = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();
  for (int i = 0; i < 4; ++i) {
    if (!labels[i] || !labels[i][0]) continue;
    const int kx = xs[i] + 4;
    const int kw = cw - 8;
    if (gray) {
      renderer.fillRect(kx, y, kw, h + lip, true);
      continue;
    }
    // Keycap: solid lower lip, white face.
    renderer.fillRoundedRect(kx, y + lip, kw, h, 6, Color::Black);
    renderer.fillRoundedRect(kx, y, kw, h, 6, Color::White);
    renderer.drawRoundedRect(kx, y, kw, h, 2, 6, true);
    centeredIn(renderer, UI_10_FONT_ID, kx + 4, kw - 8, y + (h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
               upperAscii(labels[i]).c_str());
  }
  renderer.setOrientation(orig);
}
