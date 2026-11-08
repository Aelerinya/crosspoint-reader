#include "Sprite.h"

#include <GfxRenderer.h>

namespace {

// Calls span(row, col, len) for every "on" run of one layer.
template <typename Span>
void forEachSpan(const Sprite& s, const uint8_t* runs, const size_t size, Span span) {
  size_t i = 0;
  long pos = 0;
  const long total = static_cast<long>(s.width) * s.height;
  bool ink = false;
  while (i < size && pos < total) {
    long run = 0;
    while (i < size) {
      const uint8_t b = runs[i++];
      run += b;
      if (b != 255) break;
    }
    if (ink) {
      // Split the run at row ends.
      long p = pos;
      long left = run;
      while (left > 0 && p < total) {
        const int row = static_cast<int>(p / s.width);
        const int col = static_cast<int>(p % s.width);
        const int len = static_cast<int>(left < s.width - col ? left : s.width - col);
        span(row, col, len);
        p += len;
        left -= len;
      }
    }
    pos += run;
    ink = !ink;
  }
}

}  // namespace

void drawSprite(const GfxRenderer& renderer, const Sprite& sprite, const int x, const int y, const bool black) {
  if (sprite.halo) {
    forEachSpan(sprite, sprite.halo, sprite.haloSize,
                [&](int row, int col, int len) { renderer.fillRect(x + col, y + row, len, 1, !black); });
  }
  forEachSpan(sprite, sprite.runs, sprite.size,
              [&](int row, int col, int len) { renderer.fillRect(x + col, y + row, len, 1, black); });
}

void drawSpriteMirrored(const GfxRenderer& renderer, const Sprite& sprite, const int x, const int y, const bool black) {
  const auto mirror = [&](const bool state) {
    return [&, state](int row, int col, int len) {
      renderer.fillRect(x + sprite.width - col - len, y + row, len, 1, state);
    };
  };
  if (sprite.halo) forEachSpan(sprite, sprite.halo, sprite.haloSize, mirror(!black));
  forEachSpan(sprite, sprite.runs, sprite.size, mirror(black));
}
