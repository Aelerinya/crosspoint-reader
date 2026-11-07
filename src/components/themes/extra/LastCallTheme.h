#pragma once

#include "ExtraThemes.h"

// Last Call: a late-night bar terminal (inspired by retro PC-98 cyberpunk bar
// games). VT323 pixel font, CRT window frames with title bars, halftone
// panels, the home menu as a drink menu, keycap button hints.
namespace ExtraMetrics {
constexpr ThemeMetrics lastcall = [] {
  ThemeMetrics m = base();
  m.headerTitleAlign = 0;
  m.headerUnderlineSize = 0;
  m.listRowHeight = 36;
  m.listWithSubtitleRowHeight = 58;
  m.listRowGap = 4;
  m.listRowRadius = 8;
  m.listInset = 12;
  m.listSidePadding = 14;
  m.listSelectionStyle = 0;  // invert fill
  m.listScrollWidth = 4;
  m.homeCoverHeight = 270;
  m.homeCoverTileHeight = 360;
  m.homeMenuTopOffset = 12;
  m.menuSpacing = 6;
  m.popupCornerRadius = 10;
  m.popupFrameThickness = 3;
  m.popupTextBold = false;
  m.popupTextInverted = true;
  m.popupProgressBarHeight = 6;
  m.popupProgressDrawOutline = true;
  m.controlRadius = 8;
  m.sheetRadius = 10;
  return m;
}();
}  // namespace ExtraMetrics

class LastCallTheme : public ExtraThemeBase {
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
  int menuRowPadding() const override { return 10; }
};
