// The Ex Libris page added to a book sent to another reader
// (design/uncovered/designed/plate-image.png, sample bookplate-sample-480x800.png).
#include "DeckBookplate.h"

#include <Bookplate.h>
#include <GfxRenderer.h>
#include <Memory.h>

#include <cmath>
#include <cstring>

#include "DeckTheme.h"
#include "components/Sprite.h"
#include "fontIds.h"
#include "images/BookplateArt.h"

namespace deck {

namespace {

constexpr int kW = 480;
constexpr int kH = 800;
constexpr int kEnjoySize = 22;  // display face size for ENJOY THE BOOK
constexpr int kNameSize = 22;   // and for the sender's name

// Letter-spaced text (the kickers): each character placed with `spacing` px
// between, `baseline` = bottom of the capitals. Returns the width.
int spacedWidth(const GfxRenderer& r, const int font, const char* text, const int spacing, const bool bold) {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  int w = 0;
  char ch[2] = {0, 0};
  for (const char* p = text; *p; ++p) {
    ch[0] = *p;
    w += (*p == ' ' ? r.getSpaceWidth(font, style) : r.getTextWidth(font, ch, style)) + (p[1] ? spacing : 0);
  }
  return w;
}

void drawSpaced(const GfxRenderer& r, const int font, int x, const int baseline, const char* text, const int spacing,
                const bool bold, const bool black = true) {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int top = baseline - r.getFontAscenderSize(font);
  char ch[2] = {0, 0};
  for (const char* p = text; *p; ++p) {
    ch[0] = *p;
    if (*p != ' ') r.drawText(font, x, top, ch, black, style);
    x += (*p == ' ' ? r.getSpaceWidth(font, style) : r.getTextWidth(font, ch, style)) + spacing;
  }
}

// Display-face line with the top of its capitals at capTop.
void drawDisplay(const GfxRenderer& r, const int size, const int x, const int capTop, const std::string& text) {
  r.drawText(displayFont(size), x, capTop - displayCapOffset(r, size), text.c_str(), true);
}

// Greedy word wrap into at most maxLines; the last line gets an ellipsis if cut.
std::vector<std::string> wrapText(const GfxRenderer& r, const int font, const std::string& text, const int width,
                                  const int maxLines, const EpdFontFamily::Style style) {
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find(' ', i);
    if (j == std::string::npos) j = text.size();
    const std::string word = text.substr(i, j - i);
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (line.empty() || r.getTextWidth(font, candidate.c_str(), style) <= width) {
      line = candidate;
    } else {
      lines.push_back(line);
      line = word;
      if (static_cast<int>(lines.size()) == maxLines) break;
    }
    i = j + 1;
  }
  if (static_cast<int>(lines.size()) < maxLines && !line.empty()) {
    lines.push_back(line);
    line.clear();
  }
  if (!line.empty() || i < text.size()) {
    // Text left over: shorten the last line.
    std::string& last = lines.back();
    last = r.truncatedText(font, (last + " " + line + "...").c_str(), width, style);
  } else if (!lines.empty() && r.getTextWidth(font, lines.back().c_str(), style) > width) {
    lines.back() = r.truncatedText(font, lines.back().c_str(), width, style);
  }
  return lines;
}

// "SENT FROM AN X4C" in a box, drawn straight in a scratch area and copied
// rotated by `degrees` (counter-clockwise) around (cx, cy).
void drawStamp(const GfxRenderer& r, const int cx, const int cy, const float degrees) {
  const char* text = "SENT FROM AN X4C";
  const int font = SMALL_FONT_ID;
  const int tw = spacedWidth(r, font, text, 1, false);
  const int bw = tw + 14;
  const int bh = 22;
  const int cap = r.getFontAscenderSize(font) * 72 / 100;
  // Scratch: inside the frame where the Operator goes later (still blank).
  constexpr int sx = 60, sy = 300;
  r.fillRect(sx, sy, bw, bh, false);
  r.drawRect(sx, sy, bw, bh, 2, true);
  drawSpaced(r, font, sx + 7, sy + (bh + cap) / 2, text, 1, false);
  std::vector<uint8_t> src(static_cast<size_t>(bw) * bh);
  for (int y = 0; y < bh; ++y) {
    for (int x = 0; x < bw; ++x) src[y * bw + x] = r.readPixel(sx + x, sy + y) ? 1 : 0;
  }
  r.fillRect(sx, sy, bw, bh, false);

  const float a = degrees * 3.14159265f / 180.0f;
  const float ca = std::cos(a), sa = std::sin(a);
  const int reach = (bw + bh) / 2 + 2;
  for (int y = cy - reach; y <= cy + reach; ++y) {
    for (int x = cx - reach; x <= cx + reach; ++x) {
      // Inverse rotation (screen y points down, so CCW uses +sin on y).
      const float dx = static_cast<float>(x - cx), dy = static_cast<float>(y - cy);
      const int u = static_cast<int>(std::lround(dx * ca - dy * sa + bw / 2.0f));
      const int v = static_cast<int>(std::lround(dx * sa + dy * ca + bh / 2.0f));
      if (u < 0 || v < 0 || u >= bw || v >= bh) continue;
      r.drawPixel(x, y, src[v * bw + u] != 0);
    }
  }
}

void drawPage(const GfxRenderer& r, const std::string& title, const std::string& from, const std::string& date) {
  r.clearScreen();
  drawChamfer(r, 20, 21, 440, 759, 20, 3);
  drawStamp(r, 360, 742, 7.0f);  // before the art: it borrows a blank area

  // Header: // EX LIBRIS, hatch bar, 蔵書票.
  drawSpaced(r, SMALL_FONT_ID, 47, 61, "// EX LIBRIS", 3, true);
  hatch(r, 185, 51, 190, 11);
  drawSprite(r, kPlateKana, 430 - kPlateKana.width, 48);

  drawSprite(r, kPlateGift, kW / 2 - kPlateGift.width / 2, 83);

  const std::string enjoy = "ENJOY THE BOOK";
  const int enjoyW = r.getTextWidth(displayFont(kEnjoySize), enjoy.c_str());
  drawDisplay(r, kEnjoySize, kW / 2 - enjoyW / 2, 460, enjoy);

  // Title: italic, centered, up to 2 lines.
  const int titleFont = NOTOSANS_12_FONT_ID;
  const int lineH = r.getLineHeight(titleFont);
  int y = 518;
  for (const std::string& line : wrapText(r, titleFont, title, 380, 2, EpdFontFamily::ITALIC)) {
    const int w = r.getTextWidth(titleFont, line.c_str(), EpdFontFamily::ITALIC);
    r.drawText(titleFont, kW / 2 - w / 2, y, line.c_str(), true, EpdFontFamily::ITALIC);
    y += lineH;
  }

  // Footer: rule, FROM + name, date, stamp.
  r.fillRect(55, 673, 370, 2, true);
  drawSpaced(r, SMALL_FONT_ID, 56, 697, "FROM", 3, true);
  const std::vector<std::string> name = wrapDisplay(r, displayFont(kNameSize), from, 225, 1);
  if (!name.empty()) drawDisplay(r, kNameSize, 55, 707, name[0]);
  if (!date.empty()) {
    const int dw = spacedWidth(r, UI_10_FONT_ID, date.c_str(), 3, true);
    drawSpaced(r, UI_10_FONT_ID, 424 - dw, 718, date.c_str(), 3, true);
  }
}

}  // namespace

std::vector<uint8_t> renderBookplatePng(GfxRenderer& r, const std::string& title, const std::string& from,
                                        const std::string& date) {
  constexpr int stride = kW / 8;
  // One 1-bit copy of the page (48 KB), freed before returning.
  auto rows = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(stride) * kH);
  if (!rows || !r.hasFrameBuffer()) return {};

  const auto orientation = r.getOrientation();
  r.setOrientation(GfxRenderer::Portrait);
  drawPage(r, title, from, date);
  memset(rows.get(), 0xFF, static_cast<size_t>(stride) * kH);
  for (int y = 0; y < kH; ++y) {
    uint8_t* row = rows.get() + static_cast<size_t>(y) * stride;
    for (int x = 0; x < kW; ++x) {
      if (r.readPixel(x, y)) row[x / 8] &= static_cast<uint8_t>(~(0x80 >> (x % 8)));
    }
  }
  r.clearScreen();
  r.setOrientation(orientation);
  return bookplate::encodePng1(rows.get(), kW, kH, stride);
}

}  // namespace deck
