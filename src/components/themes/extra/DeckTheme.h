#pragma once

#include <string>
#include <vector>

#include "ExtraThemes.h"

class GfxRenderer;
struct Sprite;

// Deck: angular cyberpunk drawn in ink, with a fox companion (design/SPEC.md
// in the xteink handoff). Chakra Petch UI text, Saira Extra Condensed for
// titles, 45° chamfers, -18° slanted keys and segments, 135° hatching.
namespace ExtraMetrics {
constexpr ThemeMetrics deck = [] {
  ThemeMetrics m = base();
  m.topPadding = 14;
  m.headerHeight = 48;  // 34 px tab + gap
  m.verticalSpacing = 10;
  m.contentSidePadding = 18;
  m.headerSidePadding = 18;
  m.headerTitleAlign = 0;
  m.headerUnderlineSize = 2;
  m.listRowHeight = 40;
  m.listWithSubtitleRowHeight = 62;
  m.listRowGap = 0;
  m.listInset = 10;
  m.listSidePadding = 14;
  m.listSelectionStyle = 0;  // invert fill
  m.listScrollWidth = 4;
  m.listTitleBold = true;
  m.menuSpacing = 8;
  m.homeTopPadding = 62;
  m.homeContinueReadingInMenu = false;  // the hero card is the resume target
  m.homeRecentBooksCount = 1;
  m.homeCoverHeight = 182;  // thumb height in the hero card
  m.homeCoverTileHeight = 280;
  m.homeMenuTopOffset = 14;
  m.buttonHintsHeight = 54;
  m.tabPillFullSlot = true;
  m.popupFrameThickness = 3;
  m.popupTextBold = true;
  m.popupTextInverted = true;
  m.popupMarginX = 22;
  m.popupProgressBarHeight = 6;
  m.popupProgressDrawOutline = true;
  m.textFieldNormalThickness = 2;
  m.textFieldCursorThickness = 3;
  return m;
}();
}  // namespace ExtraMetrics

class DeckTheme : public ExtraThemeBase {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  int getMenuRowHeight(const GfxRenderer& renderer) const override;

 protected:
  void drawHomeTile(GfxRenderer& renderer, Rect rect, const RecentBook* book) const override;
};

// Deck drawing primitives, shared with the Deck sleep and boot screens.
namespace deck {
// Display face (Saira Extra Condensed ExtraBold). Registered whatever the
// theme, so the Kitsune sleep screen works with any theme.
int displayFont(int size);  // 13, 22 or 31 (31 has digits, A-Z and punctuation only)
// Line step for stacked display lines: 0.86 em, as in the design.
int displayLineStep(int size);
// Distance from a drawText y to the top of the capitals in display face `size`.
int displayCapOffset(const GfxRenderer& r, int size);
std::string upper(const std::string& text);
// Parallelogram skewed -18° (top edge shifted right).
void fillSlant(const GfxRenderer& r, int x, int y, int w, int h, bool black = true);
void drawSlant(const GfxRenderer& r, int x, int y, int w, int h, int thickness, bool black = true);
// Rectangle with 45° cuts on the top-left and bottom-right corners.
void fillChamfer(const GfxRenderer& r, int x, int y, int w, int h, int cut, bool black = true);
void drawChamfer(const GfxRenderer& r, int x, int y, int w, int h, int cut, int thickness);
// 135° stripes, 3 px ink / 9 px period, clipped to the rectangle.
void hatch(const GfxRenderer& r, int x, int y, int w, int h, bool black = true);
// Text with extra letter spacing (ASCII); falls back to plain text otherwise.
int trackedWidth(const GfxRenderer& r, int fontId, const char* text, int tracking, bool bold = false);
void drawTracked(const GfxRenderer& r, int fontId, int x, int y, const char* text, int tracking, bool black = true,
                 bool bold = false);
// Row of `count` slanted segments, the first `filled` solid.
void segments(const GfxRenderer& r, int x, int y, int w, int h, int count, int filled, bool black = true);
// Wraps upper-cased `text` into at most `maxLines` lines of `width`; the last
// line is truncated with an ellipsis if needed (`truncated` reports it).
std::vector<std::string> wrapDisplay(const GfxRenderer& r, int fontId, const std::string& text, int width,
                                     int maxLines, bool* truncated = nullptr);
}  // namespace deck
