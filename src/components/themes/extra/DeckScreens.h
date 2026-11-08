#pragma once

class GfxRenderer;

// Full-screen Deck frames drawn outside the theme hooks. Both only fill the
// framebuffer; the caller refreshes the panel.
namespace deck {
// Sleep screen option "Kitsune": STANDBY, the sleeping fox, the last book.
void drawKitsuneSleepScreen(const GfxRenderer& renderer);
// Boot splash when the Deck theme is active: WAKING, the stretching fox, a bar.
void drawWakingScreen(const GfxRenderer& renderer, const char* message);
}  // namespace deck
