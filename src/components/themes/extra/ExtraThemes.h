#pragma once

#include "components/themes/BaseTheme.h"

// Shared base for custom themes (Deck). A custom theme may also swap its own
// typeface into the UI font slots (see ThemeFonts.cpp).

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
