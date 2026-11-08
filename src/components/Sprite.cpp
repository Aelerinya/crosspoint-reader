#include "Sprite.h"

#include <GfxRenderer.h>

namespace {

template <typename Span>
void forEachInkSpan(const Sprite& s, Span span) {
  size_t i = 0;
  long pos = 0;
  const long total = static_cast<long>(s.width) * s.height;
  bool ink = false;
  while (i < s.size && pos < total) {
    long run = 0;
    while (i < s.size) {
      const uint8_t b = s.runs[i++];
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
  forEachInkSpan(sprite, [&](int row, int col, int len) { renderer.fillRect(x + col, y + row, len, 1, black); });
}

void drawSpriteMirrored(const GfxRenderer& renderer, const Sprite& sprite, const int x, const int y,
                        const bool black) {
  forEachInkSpan(sprite, [&](int row, int col, int len) {
    renderer.fillRect(x + sprite.width - col - len, y + row, len, 1, black);
  });
}
