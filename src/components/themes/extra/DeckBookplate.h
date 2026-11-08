#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;

namespace deck {
// Draws the Ex Libris bookplate page (design/uncovered/designed/plate-image.png)
// at 480x800 portrait into the framebuffer and returns it as a 1-bit PNG
// (empty on failure). The framebuffer is overwritten and left white: call it
// under the render lock and redraw the screen afterwards.
// `date`: short form such as "08 NOV 2026", or empty.
std::vector<uint8_t> renderBookplatePng(GfxRenderer& r, const std::string& title, const std::string& from,
                                        const std::string& date);
}  // namespace deck
