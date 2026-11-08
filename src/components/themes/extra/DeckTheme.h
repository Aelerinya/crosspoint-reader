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
  m.listRowGap = 4;
  m.listRowRadius = 14;  // drawn as 45° chamfers (GfxRenderer::setChamferCorners)
  m.listInset = 10;
  m.listSidePadding = 14;
  m.listSelectionStyle = 0;  // invert fill
  m.listScrollWidth = 4;
  m.listTitleBold = true;
  m.menuSpacing = 8;
  m.homeTopPadding = 62;
  m.homeContinueReadingInMenu = false;  // the hero card is the resume target
  m.homeRecentBooksCount = 1;
  m.homeCoverHeight = 733;      // thumb height: wide enough (0.6 x 733) to fill the 434 px hero card
  m.homeCoverTileHeight = 340;  // RESUME stripe + 314 px card
  m.homeMenuTopOffset = 16;
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
  m.popupCornerRadius = 14;
  m.statusBarVerticalMargin = 24;  // text lane
  m.progressBarMarginTop = 10;     // room for the 12 px diamond
  m.controlRadius = 10;
  m.sheetRadius = 14;
  return m;
}();
}  // namespace ExtraMetrics

class DeckTheme : public ExtraThemeBase {
  // Set by drawReaderTopLine for the status bar drawn right after it (which
  // then leaves clock, battery and title to the top line).
  mutable bool topLineDrawn = false;

 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  int getMenuRowHeight(const GfxRenderer& renderer) const override;
  bool showsFileIcons() const override { return true; }  // no "[folder]" brackets
  // Toast: black parallelogram, bottom centre above the key bar.
  Rect drawPopup(const GfxRenderer& renderer, const char* message) const override;
  void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, int progress) const override;
  // OPENING + title, fox.run, segmented bar.
  Rect drawBookOpening(const GfxRenderer& renderer, const char* title) const override;
  // "BOOK · CHAPTER" over a rule, clock, battery (top of the page).
  int readerTopReserve() const override;
  void drawReaderTopLine(GfxRenderer& renderer, const std::string& book, const std::string& chapter) const override;
  // Progress line with a diamond marker over "n/N · title · %".
  void drawStatusBar(GfxRenderer& renderer, float bookProgress, int currentPage, int pageCount, std::string title,
                     int paddingBottom = 0, int textYOffset = 0, bool fillMargin = true, bool isPageBookmarked = false,
                     bool pageCountEstimated = false) const override;

 protected:
  void drawHomeTile(GfxRenderer& renderer, Rect rect, const RecentBook* book) const override;
};

// Deck drawing primitives, shared with the Deck sleep and boot screens.
namespace deck {
// Display face (Saira Extra Condensed ExtraBold). Registered whatever the
// theme, so the Kitsune sleep screen works with any theme.
// 13, 22 (Latin), 17, 31 (capitals incl. Latin-1), 46, 76 (ASCII capitals).
int displayFont(int size);
// v2 compact header (design "generated" screens): small black tab with the
// label (+ kana) at top 12, a rule, clock and battery cells. Returns its bottom.
int drawSmallHeader(const GfxRenderer& r, const char* label, const char* right = nullptr);
// Companion art (Operator) on: SETTINGS.deckCompanion.
bool companionOn();
// Small Japanese tags (ホーム 書庫 目次 …): the font and the tag for a screen
// name (English UI labels only; nullptr when there is none).
int kanaFont();
const char* kanaFor(const std::string& upperLabel);
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
std::vector<std::string> wrapDisplay(const GfxRenderer& r, int fontId, const std::string& text, int width, int maxLines,
                                     bool* truncated = nullptr);
}  // namespace deck
