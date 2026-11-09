#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;
#include "components/themes/BaseTheme.h"

// Full-screen Deck frames drawn outside the theme hooks. Both only fill the
// framebuffer; the caller refreshes the panel.
namespace deck {
// Sleep screen option "Kitsune": STANDBY, the sleeping fox, the last book.
void drawKitsuneSleepScreen(const GfxRenderer& renderer);
// Boot splash when the Deck theme is active: WAKING, the stretching fox, a bar.
void drawWakingScreen(const GfxRenderer& renderer, const char* message);
// Interstitial between chapters: CHAPTER CLEAR, the finished chapter, fox on a
// hatch shard, NEXT ▸ the next chapter.
void drawChapterClear(const GfxRenderer& renderer, const char* book, const char* finished, const char* next);
// Book Cover sleep: notched black bar at the bottom ("◆ HOLD PWR TO WAKE" ·
// `right`). maskOnly fills just its shape black (a gray plane's "no gray"),
// for panels whose gray planes overlay the black/white frame.
void drawCoverSleepBar(const GfxRenderer& renderer, const char* right, bool maskOnly = false);
// The bar's text band (full width, below the notch), for copying between planes.
Rect coverSleepBarBand(const GfxRenderer& renderer);
// Quick Resume sleep: a black tab on the right edge (moon, vertical label) over the page.
void drawQuickResumeTab(const GfxRenderer& renderer);
// Screen transition (design/SPEC.md "Transitions"). transitionOut draws over
// the outgoing screen in the framebuffer and refreshes: the shear shard, then
// (flash) a full black frame with `destination` huge in white, on the full
// refresh. transitionIn runs right before the incoming screen's first refresh:
// shard on the right, then the glitch lines, then the clean frame goes out.
void transitionOut(const GfxRenderer& renderer, const char* destination, bool flash);
void transitionIn(const GfxRenderer& renderer);
// v2 (Operator companion) screens, see DeckV2Screens.cpp.
// receiving: a file is coming in; percent (-1 = size unknown) draws the segment bar.
// detail: small lines under it ('\n'-separated: bytes, files so far).
void drawOnlineScreen(const GfxRenderer& renderer, bool received, const char* mode, const char* host,
                      const char* message, bool receiving = false, int percent = -1, const char* detail = nullptr);
void drawUsbConnected(const GfxRenderer& renderer);
// Modal over the current frame: hatch shade, LOW BATTERY card with the percent.
void drawLowBatteryCard(const GfxRenderer& renderer, int percent);
void drawFlashingScreen(const GfxRenderer& renderer, int percent);
// Settings: framed illustration panel (y 51..351) above the rows.
void drawSettingsPanel(const GfxRenderer& renderer);

// v2 settings / Wi-Fi pieces (DeckV2Settings.cpp).
// Display-face title at x 16 with its capitals' top at capTop, kana superscript
// (looked up when null); returns the right edge.
int drawBigTitle(const GfxRenderer& renderer, int capTop, const char* title, const char* kana = nullptr, int size = 31);
int bigTitleCap(int size);
// 3 px rule across the content width.
void drawTitleRule(const GfxRenderer& renderer, int y);
const char* englishLanguageName(uint8_t language);
// Keyboard key: outlined square with the bottom-right corner cut, filled when focused;
// `alt` is the long-press character drawn small in the top-right corner.
void drawKeyCap(const GfxRenderer& renderer, int x, int y, int w, int h, const char* label, const char* alt,
                bool focused, int font);
// "// PREVIEW · caption" and the layout's first three letter rows; returns the bottom.
int drawKeyboardPreview(const GfxRenderer& renderer, int y, uint8_t layoutId, const char* caption);
// Black state panel (SCANNING / CONNECTING / FAILED / CALIBRE) at y top..top+109 with the
// title in display face and a kana superscript (looked up when null).
void drawStatePanel(const GfxRenderer& renderer, const char* title, const char* kana = nullptr, int top = 61);
// Black tags with tracked white text at (x, y): pointed right end / plain block. Return the height.
int drawArrowTag(const GfxRenderer& renderer, int x, int y, const char* text);
int drawBlockTag(const GfxRenderer& renderer, int x, int y, const char* text);
// Four slanted signal bars (14 px tall) at (x, y), `level` 0..4 filled.
void drawSignalBars(const GfxRenderer& renderer, int x, int y, int level, bool black = true);
// Small outlined tag ("WPA2") right-aligned at `right`, centred on cy.
void drawOutlineTag(const GfxRenderer& renderer, int right, int cy, const char* text, bool black = true);
// Transfer menu, Join a network chosen: 6 filling segments in its (inverted) card
// and "STARTING WI-FI..." under the cards; refreshes the panel.
void drawJoinLoading(const GfxRenderer& renderer);
// Diagonal hatch over the whole frame (modal backdrop).
void hatchShade(const GfxRenderer& renderer);

// Send to Reader and the book options sheet (DeckSend.cpp).
struct BookCardInfo {
  std::string title;
  std::string meta;   // "EPUB · 2.1 MB", "EPUB · 2.1 MB · /BOOKS", "AUTHOR · 3% READ"
  std::string cover;  // thumbnail BMP, empty = hatched placeholder
};
// Cover thumb height shared with the Library rows (its cache files are reused).
constexpr int kBookThumbHeight = 74;
std::string formatBookSize(uint32_t bytes);
// The book's cached kBookThumbHeight thumb, or empty when there is none yet.
std::string cachedBookThumb(const std::string& bookPath);
// Cover BMP centred and clipped in the slot, 1 px frame; hatch when missing.
void drawBookCover(const GfxRenderer& renderer, const std::string& path, int x, int y, int w, int h);
// Outlined e-reader icon.
void drawReaderGlyph(const GfxRenderer& renderer, int x, int y, int w, int h, bool black = true);
// "// SENDING", cover, title, meta and a rule (y 52..136); returns its bottom.
int drawSendBookCard(const GfxRenderer& renderer, const BookCardInfo& book);
// Display-face words wrapped in `width`, a ">" word drawn as a triangle; returns the bottom.
int drawRouteText(const GfxRenderer& renderer, int x, int capTop, int width, const std::string& text);
// Bottom sheet over a hatch shade: book header, then the option rows.
void drawOptionSheet(const GfxRenderer& renderer, const BookCardInfo& book, const std::vector<std::string>& options,
                     int selected);
}  // namespace deck
