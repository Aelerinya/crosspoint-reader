// Deck pieces for Send to Reader and the book options sheet
// (design/uncovered/designed: send-*.png, DESIGNS.md "Send to reader").
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "DeckScreens.h"
#include "DeckTheme.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

namespace deck {

namespace {

constexpr int kSide = 16;

// Display-face text with its capitals' top at capTop.
void drawDisplay(const GfxRenderer& r, const int size, const int x, const int capTop, const char* text,
                 const bool black = true) {
  r.drawText(displayFont(size), x, capTop - displayCapOffset(r, size), text, black);
}

// Right-pointing triangle, `h` tall, its left edge at x, centred on cy.
void triangle(const GfxRenderer& r, const int x, const int cy, const int h, const bool black = true) {
  const int xs[] = {x, x + h * 3 / 4, x};
  const int ys[] = {cy - h / 2, cy, cy + h / 2};
  r.fillPolygon(xs, ys, 3, black);
}

// 2 px of ink every 7, 135°, over y0..y1 (modal backdrop for the sheet).
void shade(const GfxRenderer& r, const int y0, const int y1) {
  const int W = r.getScreenWidth();
  for (int y = std::max(0, y0); y < y1; ++y) {
    for (int x = (7 - (y % 7)) % 7; x < W; x += 7) r.fillRect(x, y, 2, 1, true);
  }
}

}  // namespace

std::string formatBookSize(const uint32_t bytes) {
  char buf[24];
  if (bytes >= 1024u * 1024u) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
  }
  return buf;
}

std::string cachedBookThumb(const std::string& bookPath) {
  if (!FsHelpers::hasEpubExtension(bookPath)) return {};
  std::string thumb = Epub(bookPath, "/.crosspoint").getThumbBmpPath(kBookThumbHeight);
  return Storage.exists(thumb.c_str()) ? thumb : std::string();
}

void drawBookCover(const GfxRenderer& r, const std::string& path, const int x, const int y, const int w, const int h) {
  r.fillRect(x, y, w, h, false);
  bool drawn = false;
  if (!path.empty()) {
    HalFile file;
    if (Storage.openFileForRead("DECK", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok)
        drawn = BaseTheme::drawCoverThumbFill(r, bitmap, Rect{x, y, w, h});
      file.close();
    }
  }
  if (!drawn) hatch(r, x + 4, y + 4, w - 8, h - 8);
  r.drawRect(x - 1, y - 1, w + 2, h + 2, 1, true);
}

void drawReaderGlyph(const GfxRenderer& r, const int x, const int y, const int w, const int h, const bool black) {
  // Outlined e-reader: body, screen, and the chin bar.
  const int t = std::max(2, w / 12);
  r.drawRect(x, y, w, h, t, black);
  const int m = t + std::max(2, w / 8);
  const int chin = std::max(4, h / 6);
  r.drawRect(x + m, y + m, w - 2 * m, h - 2 * m - chin, std::max(1, t / 2), black);
  r.fillRect(x + m, y + h - m - chin / 2 - std::max(2, chin / 3) / 2, w - 2 * m, std::max(2, chin / 3), black);
}

int drawSendBookCard(const GfxRenderer& r, const BookCardInfo& book) {
  const int W = r.getScreenWidth();
  drawBookCover(r, book.cover, kSide, 53, 48, 72);
  drawTracked(r, SMALL_FONT_ID, 76, 60, "// SENDING", 2, true, true);
  const int font = displayFont(13);
  const std::string title = r.truncatedText(font, upper(book.title).c_str(), W - kSide - 76);
  drawDisplay(r, 13, 76, 80, title.c_str());
  const std::string meta =
      r.truncatedText(SMALL_FONT_ID, upper(book.meta).c_str(), W - kSide - 76 - 20, EpdFontFamily::BOLD);
  drawTracked(r, SMALL_FONT_ID, 76, 103, meta.c_str(), 2, true, true);
  r.fillRect(kSide, 134, W - 2 * kSide, 2, true);
  return 136;
}

int drawRouteText(const GfxRenderer& r, const int x, const int y, const int width, const std::string& text) {
  // Words in the 13 display face, wrapped; ">" becomes a filled triangle.
  const int font = displayFont(13);
  const int cap = bigTitleCap(13);
  const int step = displayLineStep(13) + 4;
  // A lone space measures 0 (trimmed); take it from the gap in "I I".
  const int space = std::max(cap / 4, r.getTextWidth(font, "I I") - 2 * r.getTextWidth(font, "I"));
  const int tri = cap * 3 / 4;
  const std::string up = upper(text);
  int cx = x;
  int capTop = y;
  size_t i = 0;
  while (i < up.size()) {
    size_t j = up.find(' ', i);
    if (j == std::string::npos) j = up.size();
    const std::string word = up.substr(i, j - i);
    i = j + 1;
    if (word.empty()) continue;
    const bool arrow = word == ">";
    const int ww = arrow ? tri : r.getTextWidth(font, word.c_str());
    if (cx > x && cx + ww > x + width) {
      cx = x;
      capTop += step;
    }
    if (arrow) {
      triangle(r, cx, capTop + cap / 2, cap - 2);
    } else {
      drawDisplay(r, 13, cx, capTop, word.c_str());
    }
    cx += ww + space;
  }
  return capTop + cap;
}

void drawOptionSheet(const GfxRenderer& r, const BookCardInfo& book, const std::vector<std::string>& options,
                     const int selected) {
  const int W = r.getScreenWidth();
  constexpr int kRowH = 33, kHeadH = 101, kBottom = 740, kCut = 22;
  const int n = static_cast<int>(options.size());
  const int h = kHeadH + 10 + n * kRowH + 27;
  const int top = kBottom - h;
  const int x0 = kSide, x1 = W - kSide;
  shade(r, top - 40, r.getScreenHeight());
  // Panel: top-right and bottom-left corners cut, 3 px outline.
  {
    const int xs[] = {x0, x1 - kCut, x1, x1, x0 + kCut, x0};
    const int ys[] = {top, top, top + kCut, kBottom, kBottom, kBottom - kCut};
    r.fillPolygon(xs, ys, 6, true);
    const int ixs[] = {x0 + 3, x1 - kCut - 1, x1 - 3, x1 - 3, x0 + kCut + 1, x0 + 3};
    const int iys[] = {top + 3, top + 3, top + kCut + 1, kBottom - 3, kBottom - 3, kBottom - kCut - 1};
    r.fillPolygon(ixs, iys, 6, false);
  }
  // Book header.
  drawBookCover(r, book.cover, x0 + 18, top + 16, 48, 72);
  const int tx = x0 + 79;
  const std::string title = r.truncatedText(displayFont(13), upper(book.title).c_str(), x1 - 20 - tx);
  drawDisplay(r, 13, tx, top + 37, title.c_str());
  const std::string meta = r.truncatedText(SMALL_FONT_ID, upper(book.meta).c_str(), x1 - 30 - tx, EpdFontFamily::BOLD);
  drawTracked(r, SMALL_FONT_ID, tx, top + 61, meta.c_str(), 2, true, true);
  r.fillRect(x0, top + kHeadH, x1 - x0, 2, true);
  // Rows.
  const int rowsTop = top + kHeadH + 10;
  const int cap = bigTitleCap(13);
  const int barX = x1 - 36;
  for (int i = 0; i < n; ++i) {
    const int ry = rowsTop + i * kRowH;
    const int cy = ry + kRowH / 2;
    const bool on = i == selected;
    if (on) {
      const int xs[] = {x0, barX - 12, barX - 24, x0};
      const int ys[] = {ry, ry, ry + kRowH, ry + kRowH};
      r.fillPolygon(xs, ys, 4, true);
      triangle(r, x0 + 18, cy, 12, false);
      triangle(r, barX - 50, cy, 7, false);
    } else {
      r.fillRect(x0 + 17, cy, 2, 2, true);
    }
    const std::string label = r.truncatedText(displayFont(13), upper(options[i]).c_str(), barX - 70 - (x0 + 41));
    drawDisplay(r, 13, x0 + 41, cy - cap / 2, label.c_str(), !on);
  }
  // Scroll rail: every option is visible, so the thumb fills it.
  const int railTop = rowsTop - 3;
  const int railH = std::max(n * kRowH + 6, 40);
  r.drawRect(barX, railTop, 20, railH, 2, true);
  hatch(r, barX + 2, railTop + 2, 16, railH - 4);
}

}  // namespace deck
