// Deck v2 pieces for the settings and Wi-Fi screens (design/uncovered/designed).
#include <GfxRenderer.h>
#include <I18nKeys.h>

#include <algorithm>
#include <string>

#include "DeckList.h"
#include "DeckScreens.h"
#include "DeckTheme.h"
#include "components/keyboard/keyboard.h"
#include "fontIds.h"

namespace deck {

namespace {
int capOf(const int size) { return size * 150 * 70 / 7200; }

// truncatedText measures plain text; shorten until the tracked width fits too.
std::string truncateTracked(const GfxRenderer& r, const int font, const std::string& text, const int maxW,
                            const int tracking) {
  int limit = maxW;
  std::string shown = r.truncatedText(font, text.c_str(), limit, EpdFontFamily::BOLD);
  while (limit > 20 && trackedWidth(r, font, shown.c_str(), tracking, true) > maxW) {
    limit -= 8;
    shown = r.truncatedText(font, text.c_str(), limit, EpdFontFamily::BOLD);
  }
  return shown;
}
}  // namespace

int drawBigTitle(const GfxRenderer& r, const int capTop, const char* title, const char* kana, const int size) {
  const std::string t = upper(title ? title : "");
  const int font = displayFont(size);
  r.drawText(font, 16, capTop - displayCapOffset(r, size), t.c_str());
  int right = 16 + r.getTextWidth(font, t.c_str());
  if (!kana) kana = kanaFor(t);
  if (kana) {
    r.drawText(kanaFont(), right + 10, capTop - 10, kana);
    right += 10 + r.getTextWidth(kanaFont(), kana);
  }
  return right;
}

int bigTitleCap(const int size) { return capOf(size); }

void drawTitleRule(const GfxRenderer& r, const int y) { r.fillRect(16, y, r.getScreenWidth() - 32, 3, true); }

const char* englishLanguageName(const uint8_t lang) {
  static constexpr const char* kNames[] = {
      "English",   "Arabic", "Azerbaijani", "Belarusian", "Bulgarian",       "Bosnian",         "Catalan",
      "Valencian", "Czech",  "Danish",      "German",     "Spanish",         "Persian",         "Finnish",
      "French",    "Hebrew", "Hungarian",   "Indonesian", "Italian",         "Kazakh",          "Lithuanian",
      "Norwegian", "Dutch",  "Orangutan",   "Polish",     "Portuguese (BR)", "Portuguese (PT)", "Romanian",
      "Russian",   "Slovak", "Slovenian",   "Swedish",    "Turkish",         "Ukrainian",       "Vietnamese",
  };
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(Language::_COUNT),
                "one English name per Language");
  return lang < sizeof(kNames) / sizeof(kNames[0]) ? kNames[lang] : "";
}

void drawKeyCap(const GfxRenderer& r, const int x, const int y, const int w, const int h, const char* label,
                const char* alt, const bool focused, const int font) {
  // Square key with its bottom-right corner cut; focused = filled.
  constexpr int kCut = 7;
  const int xs[] = {x, x + w, x + w, x + w - kCut, x};
  const int ys[] = {y, y, y + h - kCut, y + h, y + h};
  r.fillPolygon(xs, ys, 5, true);
  if (!focused) {
    const int ixs[] = {x + 2, x + w - 2, x + w - 2, x + w - kCut - 1, x + 2};
    const int iys[] = {y + 2, y + 2, y + h - kCut - 1, y + h - 2, y + h - 2};
    r.fillPolygon(ixs, iys, 5, false);
  }
  const bool ink = !focused;
  if (label && label[0]) {
    const int tw = r.getTextWidth(font, label, EpdFontFamily::BOLD);
    r.drawText(font, x + (w - tw) / 2, y + (h - r.getLineHeight(font)) / 2, label, ink, EpdFontFamily::BOLD);
  }
  if (alt && alt[0]) {
    const int aw = r.getTextWidth(SMALL_FONT_ID, alt);
    r.drawText(SMALL_FONT_ID, x + w - 4 - aw, y + 1, alt, ink);
  }
}

int drawKeyboardPreview(const GfxRenderer& r, const int y, const uint8_t layoutId, const char* caption) {
  const int W = r.getScreenWidth();
  const std::string cap =
      std::string("// PREVIEW") + (caption && caption[0] ? std::string(" \xc2\xb7 ") + caption : "");
  drawTracked(r, SMALL_FONT_ID, 16, y, upper(cap).c_str(), 2, true, true);
  const auto& layout = freeink::ui::builtinKeyboardLayout(static_cast<freeink::ui::KeyboardLayoutId>(layoutId));
  constexpr int kKey = 37, kGap = 6;
  int ky = y + r.getLineHeight(SMALL_FONT_ID) + 8;
  int drawnRows = 0;
  for (uint8_t ri = 0; ri < layout.rowCount && drawnRows < 3; ++ri) {
    const auto& row = layout.rows[ri];
    int n = 0;
    for (uint8_t k = 0; k < row.count; ++k) {
      if (row.keys[k].kind == freeink::ui::KeyKind::Normal && row.keys[k].label) ++n;
    }
    if (n == 0) continue;
    const int cols = std::min(n, (W - 32 + kGap) / (kKey + kGap));
    int kx = (W - (cols * kKey + (cols - 1) * kGap)) / 2;
    int drawn = 0;
    for (uint8_t k = 0; k < row.count && drawn < cols; ++k) {
      const auto& key = row.keys[k];
      if (key.kind != freeink::ui::KeyKind::Normal || !key.label) continue;
      drawKeyCap(r, kx, ky, kKey, kKey, key.label, nullptr, false, nativeFont());
      kx += kKey + kGap;
      ++drawn;
    }
    ky += kKey + kGap;
    ++drawnRows;
  }
  return ky;
}

void drawStatePanel(const GfxRenderer& r, const char* title, const char* kana, const int top) {
  // Black panel x 16..440, 109 px tall from `top` (61 by default), top-right corner cut.
  const int xs[] = {16, 410, 440, 440, 16};
  const int ys[] = {top, top, top + 30, top + 109, top + 109};
  r.fillPolygon(xs, ys, 5, true);
  const std::string t = upper(title ? title : "");
  // 46 when it fits next to the kana, else the 31 face.
  const int maxW = 440 - 34 - 70;
  const int size = r.getTextWidth(displayFont(46), t.c_str()) <= maxW ? 46 : 31;
  const int font = displayFont(size);
  const int capTop = top + (109 - capOf(size)) / 2;
  const std::string shown = r.truncatedText(font, t.c_str(), maxW);
  r.drawText(font, 34, capTop - displayCapOffset(r, size), shown.c_str(), false);
  if (!kana) kana = kanaFor(t);
  if (kana) r.drawText(kanaFont(), 34 + r.getTextWidth(font, shown.c_str()) + 12, capTop - 4, kana, false);
}

int drawArrowTag(const GfxRenderer& r, const int x, const int y, const char* text) {
  const std::string t = upper(text ? text : "");
  const int font = UI_10_FONT_ID;
  const int maxW = r.getScreenWidth() - x - 60;
  const std::string shown = truncateTracked(r, font, t, maxW, 2);
  const int tw = trackedWidth(r, font, shown.c_str(), 2, true);
  const int h = r.getLineHeight(font) + 14;
  const int xs[] = {x, x + tw + 24, x + tw + 24 + h / 2, x + tw + 24, x};
  const int ys[] = {y, y, y + h / 2, y + h, y + h};
  r.fillPolygon(xs, ys, 5, true);
  drawTracked(r, font, x + 12, y + 7, shown.c_str(), 2, false, true);
  return h;
}

int drawBlockTag(const GfxRenderer& r, const int x, const int y, const char* text) {
  const std::string t = upper(text ? text : "");
  const int font = UI_10_FONT_ID;
  const std::string shown = truncateTracked(r, font, t, r.getScreenWidth() - x - 40, 2);
  const int tw = trackedWidth(r, font, shown.c_str(), 2, true);
  const int h = r.getLineHeight(font) + 14;
  r.fillRect(x, y, tw + 24, h, true);
  drawTracked(r, font, x + 12, y + 7, shown.c_str(), 2, false, true);
  return h;
}

void drawSignalBars(const GfxRenderer& r, const int x, const int y, const int level, const bool black) {
  // Four slanted bars rising left to right, the first `level` filled.
  for (int i = 0; i < 4; ++i) {
    const int h = 4 + i * 3;
    const int bx = x + i * 7;
    const int by = y + 13 - h;
    if (i < level) {
      fillSlant(r, bx, by, 6, h, black);
    } else {
      drawSlant(r, bx, by, 6, h, 1, black);
    }
  }
}

void drawOutlineTag(const GfxRenderer& r, const int right, const int cy, const char* text, const bool black) {
  const std::string t = upper(text ? text : "");
  const int tw = trackedWidth(r, SMALL_FONT_ID, t.c_str(), 1, true);
  const int h = r.getLineHeight(SMALL_FONT_ID) + 4;
  r.drawRect(right - tw - 12, cy - h / 2, tw + 12, h, 1, black);
  drawTracked(r, SMALL_FONT_ID, right - tw - 6, cy - h / 2 + 2, t.c_str(), 1, black, true);
}

void drawJoinLoading(const GfxRenderer& r) {
  // Geometry of NetworkModeSelectionActivity's Cards list: first card y 72..122,
  // three cards 60 px apart, x 18..W-18.
  const int W = r.getScreenWidth();
  const int cy = 72 + 25;
  r.fillRect(W - 18 - 112, cy - 10, 96, 20, true);
  segments(r, W - 18 - 106, cy - 7, 84, 14, 6, 3, false);
  drawTracked(r, SMALL_FONT_ID, 22, 72 + 3 * 60 + 8, "STARTING WI-FI...", 2, true, true);
  r.displayBuffer();
}

void hatchShade(const GfxRenderer& r) {
  // 135° stripes over the whole screen (modal backdrop): 2 px of ink every 7.
  const int W = r.getScreenWidth(), H = r.getScreenHeight();
  for (int y = 0; y < H; ++y) {
    for (int x = (7 - (y % 7)) % 7; x < W; x += 7) r.fillRect(x, y, 2, 1, true);
  }
}

}  // namespace deck
