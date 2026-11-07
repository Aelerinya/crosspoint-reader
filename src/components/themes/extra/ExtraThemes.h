#pragma once

#include "components/themes/BaseTheme.h"

// Four extra themes. Each also swaps its own typeface into the UI font slots
// (see ThemeFonts.cpp), so list screens change face too.
//   Cyberdeck  terminal console: JetBrains Mono, black status bar, scanlines
//   Netrunner  angular HUD: Share Tech Mono, cut corners, hazard stripes
//   Folio      printed book: EB Garamond, rules, table-of-contents menu
//   Ex Libris  bookplate: Linux Libertine, double frames, catalog-card tabs

namespace ExtraMetrics {
constexpr ThemeMetrics base() {
  ThemeMetrics m = BaseMetrics::values;
  m.homeContinueReadingInMenu = true;
  m.homeRecentBooksCount = 1;
  m.homeTopPadding = 40;
  m.homeMenuTopOffset = 14;
  m.menuSpacing = 4;
  m.headerClockCentered = false;
  return m;
}

constexpr ThemeMetrics cyberdeck = [] {
  ThemeMetrics m = base();
  m.headerTitleAlign = 0;
  m.headerUnderlineSize = 0;
  m.listRowHeight = 34;
  m.listWithSubtitleRowHeight = 56;
  m.listSidePadding = 16;
  m.listSelectionStyle = 0;  // invert fill: a terminal cursor line
  m.listScrollWidth = 6;
  m.homeCoverHeight = 290;
  m.homeCoverTileHeight = 356;
  m.popupFrameThickness = 3;
  m.popupTextBold = true;
  m.popupProgressBarHeight = 6;
  m.popupProgressDrawOutline = true;
  m.textFieldNormalThickness = 2;
  m.textFieldCursorThickness = 4;
  m.buttonHintsHeight = 40;
  return m;
}();

constexpr ThemeMetrics netrunner = [] {
  ThemeMetrics m = base();
  m.headerTitleAlign = 0;
  m.headerUnderlineSize = 0;
  m.listRowHeight = 38;
  m.listWithSubtitleRowHeight = 60;
  m.listRowGap = 4;
  m.listInset = 12;
  m.listSidePadding = 16;
  m.listSelectionStyle = 0;
  m.listTitleBold = false;
  m.listScrollWidth = 3;
  m.homeCoverHeight = 280;
  m.homeCoverTileHeight = 356;
  m.popupFrameThickness = 2;
  m.popupTextBold = false;
  m.popupProgressDrawOutline = true;
  m.tabPillFullSlot = true;
  return m;
}();

constexpr ThemeMetrics folio = [] {
  ThemeMetrics m = base();
  m.headerTitleAlign = 1;
  m.headerUnderlineSize = 0;
  m.listRowHeight = 36;
  m.listWithSubtitleRowHeight = 58;
  m.listSidePadding = 28;
  m.listSelectionStyle = 2;  // underline
  m.listScrollWidth = 2;
  m.homeCoverHeight = 250;
  m.homeCoverTileHeight = 372;
  m.homeMenuTopOffset = 8;
  m.popupFrameThickness = 1;
  m.popupTextBold = false;
  m.popupMarginX = 28;
  m.popupMarginY = 18;
  m.popupProgressBarHeight = 2;
  m.textFieldNormalThickness = 1;
  m.textFieldCursorThickness = 2;
  return m;
}();

constexpr ThemeMetrics exlibris = [] {
  ThemeMetrics m = base();
  m.headerTitleAlign = 1;
  m.headerUnderlineSize = 0;
  m.listRowHeight = 38;
  m.listWithSubtitleRowHeight = 60;
  m.listRowGap = 2;
  m.listRowRadius = 12;
  m.listInset = 14;
  m.listSidePadding = 18;
  m.listSelectionStyle = 1;  // light pill
  m.listScrollWidth = 3;
  m.homeCoverHeight = 236;
  m.homeCoverTileHeight = 376;
  m.homeMenuTopOffset = 10;
  m.popupCornerRadius = 14;
  m.popupFrameThickness = 2;
  m.popupTextBold = false;
  m.popupTextInverted = false;
  m.popupProgressDrawOutline = true;
  m.popupProgressFillInverted = false;
  m.popupProgressOutlineInverted = false;
  m.controlRadius = 12;
  m.sheetRadius = 14;
  m.capsuleRadius = 255;
  return m;
}();
}  // namespace ExtraMetrics

// Shared home-screen plumbing: the cover tile is drawn once and cached by
// HomeActivity, so everything in it must not depend on the menu selection.
class ExtraThemeBase : public BaseTheme {
 public:
  int getMenuRowHeight(const GfxRenderer& renderer) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;

 protected:
  // Draws the tile (book present or not). Called only while the cover is not cached.
  virtual void drawHomeTile(GfxRenderer& renderer, Rect rect, const RecentBook* book) const = 0;
  virtual int menuRowPadding() const { return 16; }
  // Draws the book's cover thumbnail centred in `slot`; returns the drawn width, 0 if none.
  static int drawCoverInSlot(const GfxRenderer& renderer, const RecentBook& book, Rect slot, int thumbHeight);
};

class CyberdeckTheme : public ExtraThemeBase {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;

 protected:
  void drawHomeTile(GfxRenderer& renderer, Rect rect, const RecentBook* book) const override;
};

class NetrunnerTheme : public ExtraThemeBase {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;

 protected:
  void drawHomeTile(GfxRenderer& renderer, Rect rect, const RecentBook* book) const override;
  int menuRowPadding() const override { return 18; }
};

class FolioTheme : public ExtraThemeBase {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;

 protected:
  void drawHomeTile(GfxRenderer& renderer, Rect rect, const RecentBook* book) const override;
  int menuRowPadding() const override { return 12; }
};

class ExLibrisTheme : public ExtraThemeBase {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;

 protected:
  void drawHomeTile(GfxRenderer& renderer, Rect rect, const RecentBook* book) const override;
  int menuRowPadding() const override { return 14; }
};
