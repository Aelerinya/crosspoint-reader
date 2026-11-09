// Deck v2 full-screen layouts with the Operator companion (design/SPEC.md "v2").
// Positions follow the 480x800 design; each frame only fills the framebuffer.
#include <GfxRenderer.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <string>

#include "DeckScreens.h"
#include "DeckTheme.h"
#include "components/Sprite.h"
#include "fontIds.h"
#include "images/DeckOperator.h"

namespace deck {

namespace {

void poly(const GfxRenderer& r, std::initializer_list<std::pair<int, int>> pts, const bool black = true) {
  int xs[12], ys[12], n = 0;
  for (const auto& p : pts) {
    if (n == 12) break;
    xs[n] = p.first;
    ys[n] = p.second;
    ++n;
  }
  r.fillPolygon(xs, ys, n, black);
}

int capOf(const int size) { return size * 150 * 70 / 7200; }

// Display-face text with its capitals' top at `capTop`.
void display(const GfxRenderer& r, const int size, const int x, const int capTop, const char* text,
             const bool black = true) {
  r.drawText(displayFont(size), x, capTop - displayCapOffset(r, size), text, black);
}

int batteryPercent() { return std::min<int>(100, powerManager.getBatteryPercentage()); }

}  // namespace

void drawOnlineScreen(const GfxRenderer& r, const bool received, const char* mode, const char* host,
                      const char* message, const bool receiving, const int percent, const char* detail) {
  const int W = r.getScreenWidth();
  r.clearScreen();
  drawSmallHeader(r, "TRANSFER");
  const int small = SMALL_FONT_ID;
  if (!received) {
    // Black panel with ONLINE, a white window she sits in, the address under it.
    poly(r, {{16, 62}, {W - 16, 62}, {W - 16, 652}, {W - 56, 692}, {16, 692}});
    display(r, 46, 34, 82, "ONLINE", false);
    const char* kana = "接続中";
    r.drawText(kanaFont(), 34 + r.getTextWidth(displayFont(46), "ONLINE") + 10, 82, kana, false);
    poly(r, {{30, 176}, {W - 30, 176}, {W - 30, 466}, {60, 466}, {30, 436}}, false);
    drawTracked(r, UI_12_FONT_ID, 212, 478, host ? host : "", 0, false, true);
    std::string foot = std::string("// ") + upper(mode ? mode : "") + " \xc2\xb7 " + upper(message ? message : "");
    foot = r.truncatedText(small, foot.c_str(), W - 2 * 34, EpdFontFamily::BOLD);
    drawTracked(r, small, 34, 692 - 40 - r.getLineHeight(small), foot.c_str(), 2, false, true);
    if (companionOn()) drawSprite(r, kOpOnWait, 0, 0);
    return;
  }
  // Received: ONLINE, the address tag, the message; frame bars; she catches books.
  display(r, 46, 16, 64, "ONLINE");
  r.drawText(kanaFont(), 16 + r.getTextWidth(displayFont(46), "ONLINE") + 10, 64, "接続中");
  const std::string h = host ? host : "";
  const int hw = trackedWidth(r, UI_10_FONT_ID, h.c_str(), 0, true) + 24;
  r.fillRect(16, 150, hw, r.getLineHeight(UI_10_FONT_ID) + 14, true);
  drawTracked(r, UI_10_FONT_ID, 28, 157, h.c_str(), 0, false, true);
  const std::string msg = upper(message ? message : "");
  // Up to two lines in 170 px.
  std::string line1 = msg, line2;
  if (trackedWidth(r, small, msg.c_str(), 1, true) > 170) {
    const size_t cut = msg.rfind(' ', msg.size() / 2 + 6);
    if (cut != std::string::npos) {
      line1 = msg.substr(0, cut);
      line2 = msg.substr(cut + 1);
    }
  }
  drawTracked(r, small, 16, 196, r.truncatedText(small, line1.c_str(), 200, EpdFontFamily::BOLD).c_str(), 1, true,
              true);
  int y = 196 + r.getLineHeight(small) + 2;
  if (!line2.empty()) {
    drawTracked(r, small, 16, y, r.truncatedText(small, line2.c_str(), 200, EpdFontFamily::BOLD).c_str(), 1, true,
                true);
    y += r.getLineHeight(small) + 2;
  }
  // Under the message, left of her head (x < 150): bar + percent, then one detail line each.
  y += 10;
  if (receiving && percent >= 0) {
    const int p = std::clamp(percent, 0, 100);
    segments(r, 16, y, 96, 14, 8, p * 8 / 100);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", p);
    drawTracked(r, small, 122, y + (14 - r.getLineHeight(small)) / 2, pct, 1, true, true);
    y += 14 + 8;
  }
  const std::string info = upper(detail ? detail : "");
  size_t from = 0;
  while (from < info.size()) {
    size_t to = info.find('\n', from);
    if (to == std::string::npos) to = info.size();
    const std::string line = info.substr(from, to - from);
    drawTracked(r, small, 16, y, r.truncatedText(small, line.c_str(), 134, EpdFontFamily::BOLD).c_str(), 1, true, true);
    y += r.getLineHeight(small) + 2;
    from = to + 1;
  }
  poly(r, {{W - 24, 150}, {W - 8, 130}, {W - 8, 670}, {W - 24, 670}});
  hatch(r, W - 70, 440, 40, 190);
  r.fillRect(10, 330, 2, 300, true);
  if (companionOn()) drawSprite(r, kOpOnRecv, 34, 0);
}

void drawUsbConnected(const GfxRenderer& r) {
  const int W = r.getScreenWidth();
  r.clearScreen();
  drawSmallHeader(r, "USB");
  hatch(r, 16, 48, W - 32, 10);
  poly(r, {{16, 70}, {W - 70, 70}, {W - 40, 100}, {W - 40, 194}, {16, 194}});
  display(r, 46, 32, 70 + (124 - capOf(46)) / 2, "CONNECTED", false);
  const char* tag = "EJECT BEFORE UNPLUGGING";
  const int tw = trackedWidth(r, UI_10_FONT_ID, tag, 1, true);
  poly(r, {{16, 214}, {16 + 14 + tw + 14, 214}, {16 + tw + 50, 241}, {16 + 14 + tw + 14, 268}, {16, 268}});
  drawTracked(r, UI_10_FONT_ID, 30, 241 - r.getLineHeight(UI_10_FONT_ID) / 2, tag, 1, false, true);
  if (companionOn()) drawSprite(r, kOpUsb, 0, 0);
}

void drawLowBatteryCard(const GfxRenderer& r, const int percent) {
  const int W = r.getScreenWidth();
  // Hatch shade over the screen underneath: 2 px of ink every 7.
  for (int y = 0; y < r.getScreenHeight(); ++y) {
    for (int x = (7 - (y % 7)) % 7; x < W; x += 7) r.fillRect(x, y, 2, 1, true);
  }
  const int x0 = 22, y0 = 118, w = W - 44, h = 566;
  fillChamfer(r, x0, y0, w, h, 24, true);
  fillChamfer(r, x0 + 3, y0 + 3, w - 6, h - 6, 23, false);
  const int ix = x0 + 3, iy = y0 + 3, iw = w - 6;
  // LOW BATTERY tag, flush right, slanted left edge.
  const char* tag = "LOW BATTERY";
  const int tw = trackedWidth(r, UI_10_FONT_ID, tag, 2, true);
  const int th = r.getLineHeight(UI_10_FONT_ID) + 16;
  poly(r, {{ix + iw - tw - 42, iy + 16},
           {ix + iw, iy + 16},
           {ix + iw, iy + 16 + th},
           {ix + iw - tw - 42 - 14, iy + 16 + th}});
  drawTracked(r, UI_10_FONT_ID, ix + iw - tw - 14, iy + 24, tag, 2, false, true);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  const int big = displayFont(76);
  const int pw = r.getTextWidth(big, pct);
  display(r, 76, ix + iw - 18 - pw, iy + 60 + 8, pct);
  if (companionOn()) drawSprite(r, kOpLowCard, ix + 4, iy + 96);
  // "OK ◆ DISMISS" (the diamond drawn: the small face has no U+25C6).
  const int fy = iy + h - 6 - 14 - r.getLineHeight(SMALL_FONT_ID);
  const int ok = trackedWidth(r, SMALL_FONT_ID, "OK", 2, true);
  drawTracked(r, SMALL_FONT_ID, ix + 18, fy, "OK", 2, true, true);
  const int dcx = ix + 18 + ok + 10, dcy = fy + r.getLineHeight(SMALL_FONT_ID) / 2;
  poly(r, {{dcx, dcy - 5}, {dcx + 5, dcy}, {dcx, dcy + 5}, {dcx - 5, dcy}});
  drawTracked(r, SMALL_FONT_ID, dcx + 12, fy, "DISMISS", 2, true, true);
}

void drawFlashingScreen(const GfxRenderer& r, const int percent) {
  const int W = r.getScreenWidth();
  r.clearScreen();
  drawSmallHeader(r, "FIRMWARE UPDATE");
  if (companionOn()) drawSprite(r, kOpFlash, 0, 0);
  poly(r, {{16, 478}, {W - 46, 478}, {W - 16, 508}, {W - 16, 562}, {16, 562}});
  display(r, 46, 32, 478 + (84 - capOf(46)) / 2, "FLASHING", false);
  r.drawText(kanaFont(), 32 + r.getTextWidth(displayFont(46), "FLASHING") + 10, 478 + 16, "更新中", false);
  r.drawRect(16, 576, W - 32, 128, 3, true);
  drawTracked(r, UI_12_FONT_ID, 30, 588, "DO NOT POWER OFF", 2, true, true);
  const int p = std::clamp(percent, 0, 100);
  segments(r, 30, 630, W - 60, 18, 20, p / 5);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", p);
  drawTracked(r, SMALL_FONT_ID, 30, 664, pct, 2, true, true);
  const int bw = (W - 32) * 45 / 100;
  poly(r, {{16, 800 - 16 - 50}, {16 + bw, 800 - 16 - 50}, {16 + bw + 30, 800 - 16}, {16, 800 - 16}});
}

void drawSettingsPanel(const GfxRenderer& r) {
  const int W = r.getScreenWidth();
  // Black frame (top-right cut) around a white window holding the dial art.
  poly(r, {{16, 51}, {W - 46, 51}, {W - 16, 81}, {W - 16, 351}, {16, 351}});
  const int ix = 26, iy = 61, iw = W - 52, ih = 280;
  poly(r,
       {{ix, iy}, {ix + iw - 24, iy}, {ix + iw, iy + 24}, {ix + iw, iy + ih}, {ix + 24, iy + ih}, {ix, iy + ih - 24}},
       false);
  if (!companionOn()) {
    hatch(r, ix + 30, iy + 30, iw - 60, ih - 60);
    return;
  }
  const auto clip = r.getClipRect();
  const_cast<GfxRenderer&>(r).setClipRect(ix, iy, iw, ih);
  drawSprite(r, kOpSettings, ix + 14, iy - 60);
  const_cast<GfxRenderer&>(r).setClipRect(clip[0], clip[1], clip[2], clip[3]);
}

}  // namespace deck
