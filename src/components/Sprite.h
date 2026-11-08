#pragma once

#include <cstddef>
#include <cstdint>

class GfxRenderer;

// A 1-bit image stored as alternating white/black run lengths (row-major,
// starting with white; a 255 byte continues the run). Built by
// scripts/convert_sprites.py. Lives in flash; drawing allocates nothing.
struct Sprite {
  int width;
  int height;
  const uint8_t* runs;
  size_t size;
};

// Paints the sprite's ink pixels with top-left at (x, y); white is transparent.
// `black` = false paints the ink white (for use on dark panels).
void drawSprite(const GfxRenderer& renderer, const Sprite& sprite, int x, int y, bool black = true);
// Same, mirrored left-right.
void drawSpriteMirrored(const GfxRenderer& renderer, const Sprite& sprite, int x, int y, bool black = true);
