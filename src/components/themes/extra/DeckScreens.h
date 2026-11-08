#pragma once

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
}  // namespace deck
