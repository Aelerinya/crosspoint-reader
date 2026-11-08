#include "DeckList.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "DeckTheme.h"
#include "components/Sprite.h"
#include "fontIds.h"

namespace deck {

bool listsActive() { return SETTINGS.uiTheme == CrossPointSettings::DECK && !gpio.hasTouch(); }

int nativeFont() { return 0x0DEC0A71; }

namespace {

void dashedLine(const GfxRenderer& r, const int x, const int y, const int w) {
  for (int xx = 0; xx < w; xx += 6) r.fillRect(x + xx, y, std::min(3, w - xx), 1, true);
}

void diamondMark(const GfxRenderer& r, const int cx, const int cy, const int s, const bool black) {
  const int xs[] = {cx, cx + s, cx, cx - s};
  const int ys[] = {cy - s, cy, cy + s, cy};
  r.fillPolygon(xs, ys, 4, black);
}

void triangleMark(const GfxRenderer& r, const int x, const int cy, const bool black) {
  const int xs[] = {x, x + 9, x};
  const int ys[] = {cy - 5, cy, cy + 5};
  r.fillPolygon(xs, ys, 3, black);
}

void toggleBox(const GfxRenderer& r, const int x, const int cy, const bool on, const bool black) {
  const int s = 16;
  r.drawRect(x, cy - s / 2, s, s, 2, black);
  if (on) r.fillRect(x + 4, cy - s / 2 + 4, s - 8, s - 8, black);
}

// Small right/left pointing triangles (the trimmed fonts have no ◂ ▸).
void arrowRight(const GfxRenderer& r, const int x, const int cy, const bool black, const int s = 4) {
  const int xs[] = {x, x + s + 1, x};
  const int ys[] = {cy - s, cy, cy + s};
  r.fillPolygon(xs, ys, 3, black);
}

void arrowLeft(const GfxRenderer& r, const int x, const int cy, const bool black, const int s = 4) {
  const int xs[] = {x + s + 1, x, x + s + 1};
  const int ys[] = {cy - s, cy, cy + s};
  r.fillPolygon(xs, ys, 3, black);
}

// Slanted checkbox (Pick lists): outlined, filled when on.
void slantCheck(const GfxRenderer& r, const int right, const int cy, const bool on, const bool black) {
  const int w = 22, h = 16;
  if (on) {
    fillSlant(r, right - w, cy - h / 2, w, h, black);
  } else {
    drawSlant(r, right - w, cy - h / 2, w, h, 2, black);
  }
}

// Truncates with an ellipsis so the tracked width fits `maxW`.
std::string fitTracked(const GfxRenderer& r, const int font, const std::string& text, const int maxW,
                       const int tracking) {
  if (trackedWidth(r, font, text.c_str(), tracking, true) <= maxW) return text;
  std::string t = text;
  while (!t.empty()) {
    do t.pop_back();
    while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    const std::string c = t + "...";
    if (trackedWidth(r, font, c.c_str(), tracking, true) <= maxW) return c;
  }
  return t;
}

// " (DECK)" marks Deck-only options in the shared lists; the Deck screens drop it.
void stripDeckSuffix(std::string& label) {
  for (const char* suffix : {" (DECK)", " (Deck)"}) {
    const size_t n = strlen(suffix);
    if (label.size() > n && label.compare(label.size() - n, n, suffix) == 0) label.erase(label.size() - n);
  }
}

int rowHeight(const GfxRenderer& r, const ListStyle style, const bool cardKicker) {
  switch (style) {
    case ListStyle::Rows:
      return kRowsRowHeight;
    case ListStyle::Categories:
      return 56;
    case ListStyle::Pick:
      return kPickRowHeight;
    case ListStyle::Menu:
      return 30;
    case ListStyle::Display:
      return displayLineStep(13) + 14;
    case ListStyle::Cards:
      return 24 + displayLineStep(13) + (cardKicker ? r.getLineHeight(SMALL_FONT_ID) : 0) + 4;
    case ListStyle::Books:
      return 16 + 13 * 150 * 70 / 7200 + 8 + r.getLineHeight(SMALL_FONT_ID) + 6;
    case ListStyle::Library:
      return 84;
    case ListStyle::Files:
      return 50;
  }
  return 40;
}

// Slanted tabs; the active one filled, a focused bar underlined.
int drawTabs(const GfxRenderer& r, const int x, const int y, const ListSpec& spec) {
  const int h = r.getLineHeight(SMALL_FONT_ID) + 10;
  int tx = x + 6;
  for (int i = 0; i < spec.tabCount; ++i) {
    const std::string label = upper(spec.tabs[i] ? spec.tabs[i] : "");
    const int w = trackedWidth(r, SMALL_FONT_ID, label.c_str(), 2, true) + 30;
    const bool active = i == spec.activeTab;
    if (active) {
      fillSlant(r, tx, y, w, h, true);
    } else {
      drawSlant(r, tx, y, w, h, 2, true);
    }
    drawTracked(r, SMALL_FONT_ID, tx + 15, y + 5, label.c_str(), 2, !active, true);
    if (active && spec.tabsFocused) r.fillRect(tx, y + h + 3, w - 6, 3, true);
    tx += w + 6;
  }
  return h + 8;
}

int fitRows(const int height, const int rowH, const int gap) { return std::max(1, (height + gap) / (rowH + gap)); }

// Keeps the selection in view.
void settleTop(int& top, const int sel, const int visible, const int count) {
  if (sel >= 0) {
    if (sel < top) top = sel;
    if (sel >= top + visible) top = sel - visible + 1;
  }
  top = std::clamp(top, 0, std::max(0, count - visible));
}

int rowGap(const ListStyle style) {
  if (style == ListStyle::Cards) return 10;
  if (style == ListStyle::Categories) return 7;
  return style == ListStyle::Menu ? 2 : 0;
}

int specRowHeight(const GfxRenderer& r, const ListSpec& spec, const bool cardKicker) {
  return spec.rowHeight > 0 ? spec.rowHeight : rowHeight(r, spec.style, cardKicker);
}

// Height of the tab bar, kicker line and top rule above the rows.
int headerHeight(const GfxRenderer& r, const ListSpec& spec) {
  int h = 0;
  if (spec.tabCount > 0) h += r.getLineHeight(SMALL_FONT_ID) + 10 + 16;
  if (!spec.kicker.empty()) h += r.getLineHeight(SMALL_FONT_ID) + 10;
  if (spec.style == ListStyle::Display || spec.style == ListStyle::Books) h += 2;
  return h;
}

}  // namespace

void drawTabBar(const GfxRenderer& r, const int x, const int y, const ListSpec& spec) { drawTabs(r, x, y, spec); }

int drawToggle(const GfxRenderer& r, const int right, const int cy, const bool on, const bool black) {
  constexpr int kW = 38, kH = 17, kGap = 1;
  const int font = SMALL_FONT_ID;
  const int x0 = right - 2 * kW - kGap;
  const int y = cy - kH / 2;
  const char* labels[] = {"OFF", "ON"};
  for (int i = 0; i < 2; ++i) {
    const int hx = x0 + i * (kW + kGap);
    const bool active = (i == 1) == on;
    if (active) {
      fillSlant(r, hx, y, kW, kH, black);
    } else {
      fillSlant(r, hx, y, kW, kH, black);
      // 1 px outline: inner parallelogram in the background colour.
      const int xs[] = {hx + 6, hx + kW - 2, hx + kW - 6, hx + 2};
      const int ys[] = {y + 1, y + 1, y + kH - 2, y + kH - 2};
      r.fillPolygon(xs, ys, 4, !black);
    }
    const int tw = trackedWidth(r, font, labels[i], 0, true);
    drawTracked(r, font, hx + (kW - tw) / 2 + 1, cy - r.getLineHeight(font) / 2, labels[i], 0, active ? !black : black,
                true);
  }
  return 2 * kW + kGap;
}

int drawCategoryStrip(const GfxRenderer& r, const int y, const char* const* labels, const int count, const int active) {
  constexpr int kH = 38, kTabH = 20;
  const int W = r.getScreenWidth();
  r.fillRect(16, y, W - 32, kH, true);
  const int font = SMALL_FONT_ID;
  const int ty = y + (kH - kTabH) / 2;
  int tx = 24;
  for (int i = 0; i < count; ++i) {
    const char* label = labels[i] ? labels[i] : "";
    const int w = trackedWidth(r, font, label, 2, true) + 30;
    if (i == active) {
      fillSlant(r, tx, ty, w, kTabH, false);
    } else {
      drawSlant(r, tx, ty, w, kTabH, 1, false);
    }
    drawTracked(r, font, tx + 15, ty + (kTabH - r.getLineHeight(font)) / 2, label, 2, i == active, true);
    tx += w + 4;
  }
  return kH;
}

int tabBarHeight(const GfxRenderer& r) { return r.getLineHeight(SMALL_FONT_ID) + 10 + 22; }

int listViewport(const GfxRenderer& r, const Rect area, const ListSpec& spec, const int count, const int selected,
                 int& top) {
  const bool cardKicker = spec.style == ListStyle::Cards && spec.subtitles;
  const int rowH = specRowHeight(r, spec, cardKicker);
  const int visible = fitRows(area.height - headerHeight(r, spec), rowH, rowGap(spec.style));
  settleTop(top, std::clamp(selected, -1, count - 1), visible, count);
  return visible;
}

int drawList(const GfxRenderer& r, const Rect area, const ListSpec& spec, const int count, const int selected, int& top,
             const std::function<bool(int index, Row& row)>& rowAt) {
  int y = area.y;
  const int x = area.x;
  const int w = area.width;

  if (spec.tabCount > 0) y += drawTabs(r, x, y, spec) + 8;

  if (!spec.kicker.empty()) {
    const int font = SMALL_FONT_ID;
    const int lh = r.getLineHeight(font);
    int rw = 0;
    if (!spec.kickerRight.empty()) {
      const std::string kr = upper(spec.kickerRight);
      rw = trackedWidth(r, font, kr.c_str(), 2, true);
      drawTracked(r, font, x + w - rw, y, kr.c_str(), 2, true, true);
    }
    // Leave at least 60 px of stripe between the label and the right text.
    const int maxK = w - rw - 60 - (rw ? 20 : 10);
    std::string k = upper(spec.kicker);
    while (k.size() > 3 && trackedWidth(r, font, k.c_str(), 3, true) > maxK) {
      do k.pop_back();
      while (!k.empty() && (static_cast<unsigned char>(k.back()) & 0xC0) == 0x80);
      while (!k.empty() && (static_cast<unsigned char>(k.back()) & 0x80)) k.pop_back();
    }
    drawTracked(r, font, x, y, k.c_str(), 3, true, true);
    const int kw = trackedWidth(r, font, k.c_str(), 3, true);
    hatch(r, x + kw + 10, y + (lh - 10) / 2, w - kw - rw - (rw ? 20 : 10), 10);
    y += lh + 10;
  }

  if (spec.style == ListStyle::Display || spec.style == ListStyle::Books) {
    r.fillRect(x, y, w, 2, true);
    y += 2;
  }

  if (count <= 0) {
    if (!spec.emptyText.empty()) drawTracked(r, UI_10_FONT_ID, x + 10, y + 20, upper(spec.emptyText).c_str(), 1);
    top = 0;
    return 1;
  }

  // Cards carry a kicker line when the screen shows subtitles.
  const bool cardKicker = spec.style == ListStyle::Cards && spec.subtitles;
  const int gap = rowGap(spec.style);
  const int rowH = specRowHeight(r, spec, cardKicker);
  int bottom = area.y + area.height;
  const int visible = fitRows(bottom - y, rowH, gap);
  const int sel = std::clamp(selected, -1, count - 1);
  settleTop(top, sel, visible, count);

  const int lastRowBottom = y + std::min(visible, count - top) * (rowH + gap);
  if (spec.fox) {
    // Fox only when it fits under the rows.
    const int foxY = bottom - spec.fox->height - 4;
    if (foxY > lastRowBottom) drawSprite(r, *spec.fox, x + w - spec.fox->width, foxY);
  }

  for (int i = 0; i < visible && top + i < count; ++i) {
    const int index = top + i;
    Row row;
    if (!rowAt(index, row)) continue;
    if (!spec.subtitles && spec.style != ListStyle::Books && spec.style != ListStyle::Library) row.kicker.clear();
    if (index == spec.markedIndex) row.marked = true;
    if (spec.style == ListStyle::Display && row.kicker.empty()) {
      // "Chapter 1: Supply" -> kicker "Chapter 1", title "Supply".
      const size_t colon = row.label.find(": ");
      if (colon != std::string::npos && colon < 24) {
        row.kicker = row.label.substr(0, colon);
        row.label = row.label.substr(colon + 2);
      }
    }
    const size_t lead = row.label.find_first_not_of(' ');
    if (lead != std::string::npos && lead > 0) row.label.erase(0, lead);
    if (spec.upperLabels && !spec.nativeLabels) row.label = upper(row.label);
    stripDeckSuffix(row.label);
    const int ry = y + i * (rowH + gap);
    const bool on = index == sel && !row.isHeader;
    const bool ink = !on;  // text colour: black normally, white on the inverted selection
    if (row.isHeader && spec.style == ListStyle::Rows) {
      // "// ACCOUNT" over a 2 px rule.
      const int font = SMALL_FONT_ID;
      const std::string h = r.truncatedText(font, upper(row.label).c_str(), w - 20, EpdFontFamily::BOLD);
      drawTracked(r, font, x, ry + rowH - r.getLineHeight(font) - 6, ("// " + h).c_str(), 2, true, true);
      r.fillRect(x, ry + rowH - 2, w, 2, true);
      continue;
    }
    if (row.isHeader) {
      const int font = SMALL_FONT_ID;
      const std::string h = r.truncatedText(font, upper(row.label).c_str(), w - 20, EpdFontFamily::BOLD);
      const int ty = ry + rowH - r.getLineHeight(font) - 6;
      drawTracked(r, font, x, ty, ("// " + h).c_str(), 3, true, true);
      continue;
    }
    switch (spec.style) {
      case ListStyle::Rows: {
        // v2: 41 px rows, tracked label; toggle pair, "◂ VALUE ▸" on the selected enum, ▸ for actions.
        if (on) r.fillRect(x, ry, w, rowH, true);
        const int cy = ry + rowH / 2;
        const int right = x + w - 12;
        const int vf = SMALL_FONT_ID;
        int rightW = 0;
        if (row.hasToggle) {
          rightW = drawToggle(r, right, cy, row.toggleOn, ink);
        } else if (row.kind == RowKind::Action && row.right.empty()) {
          arrowRight(r, right - 5, cy, ink);
          rightW = 10;
        } else if (!row.right.empty()) {
          const bool arrows = on && row.kind == RowKind::Enum;
          const std::string v = fitTracked(r, vf, upper(row.right), w / 2, 2);
          const int vw = trackedWidth(r, vf, v.c_str(), 2, true);
          const bool action = row.kind == RowKind::Action;
          const int vx = right - vw - (arrows || action ? 14 : 0);
          drawTracked(r, vf, vx, cy - r.getLineHeight(vf) / 2, v.c_str(), 2, ink, true);
          if (arrows) {
            arrowLeft(r, vx - 14, cy, ink);
            arrowRight(r, right - 5, cy, ink);
          }
          if (action) arrowRight(r, right - 5, cy, ink);
          rightW = vw + (arrows ? 28 : action ? 14 : 0);
        }
        const int font = UI_10_FONT_ID;
        const std::string l = fitTracked(r, font, row.label, w - 24 - rightW - 14, 2);
        drawTracked(r, font, x + 12, cy - r.getLineHeight(font) / 2, l.c_str(), 2, row.dimmed ? true : ink, true);
        if (!on) dashedLine(r, x, ry + rowH - 1, w);
        break;
      }
      case ListStyle::Categories: {
        // Chamfered rows, display-face label, kana tag on the right; the selected one filled with ▶.
        if (on) {
          fillChamfer(r, x, ry, w, rowH, 14, true);
          const int cy = ry + rowH / 2;
          const int xs[] = {x + 18, x + 30, x + 18};
          const int ys[] = {cy - 7, cy, cy + 7};
          r.fillPolygon(xs, ys, 3, false);
        } else {
          drawChamfer(r, x, ry, w, rowH, 14, 2);
        }
        const int cap = 13 * 150 * 70 / 7200;
        int kw = 0;
        if (row.kana) {
          kw = r.getTextWidth(kanaFont(), row.kana);
          r.drawText(kanaFont(), x + w - 16 - kw, ry + (rowH - r.getLineHeight(kanaFont())) / 2, row.kana, ink);
        }
        const int font = displayFont(13);
        const std::string l = r.truncatedText(font, row.label.c_str(), w - 42 - kw - 24);
        r.drawText(font, x + 42, ry + (rowH - cap) / 2 - displayCapOffset(r, 13), l.c_str(), ink);
        break;
      }
      case ListStyle::Pick: {
        // Single choice: ◆ current, ▶ focus in a marker column. Multi (Check rows): slanted boxes on the right.
        if (on) r.fillRect(x, ry, w, rowH, true);
        if (!on) dashedLine(r, x, ry + rowH - 1, w);
        const int cy = ry + rowH / 2;
        const bool multi = row.kind == RowKind::Check;
        int lx = x + 12;
        if (!multi) {
          if (on) {
            const int xs[] = {x + 14, x + 25, x + 14};
            const int ys[] = {cy - 6, cy, cy + 6};
            r.fillPolygon(xs, ys, 3, false);
          } else if (row.marked) {
            diamondMark(r, x + 19, cy, 6, true);
          }
          lx = x + 42;
        }
        const int right = x + w - 12;
        int rightW = 0;
        if (multi) {
          slantCheck(r, right, cy, row.toggleOn, ink);
          rightW = 30;
        }
        if (!row.right.empty()) {
          const std::string v = upper(row.right);
          const int vw = trackedWidth(r, SMALL_FONT_ID, v.c_str(), 2, true);
          drawTracked(r, SMALL_FONT_ID, right - rightW - vw, cy - r.getLineHeight(SMALL_FONT_ID) / 2, v.c_str(), 2, ink,
                      true);
          rightW += vw + 12;
        }
        const int font = spec.nativeLabels ? nativeFont() : UI_12_FONT_ID;
        const std::string l = r.truncatedText(font, row.label.c_str(), right - rightW - lx - 8, EpdFontFamily::BOLD);
        r.drawText(font, lx, cy - r.getLineHeight(font) / 2, l.c_str(), ink, EpdFontFamily::BOLD);
        break;
      }
      case ListStyle::Menu: {
        // v2: display-face labels; the selected row inverted with a slanted right end.
        const int cy = ry + rowH / 2;
        if (on) {
          const int xs[] = {x, x + w, x + w - 16, x};
          const int ys[] = {ry, ry, ry + rowH, ry + rowH};
          r.fillPolygon(xs, ys, 4, true);
          triangleMark(r, x + 14, cy, false);
        } else {
          r.fillRect(x + 17, cy - 1, 3, 3, true);  // the "·" of unselected rows
        }
        int rightW = 0;
        if (row.hasToggle) {
          rightW = 16;
          toggleBox(r, x + w - 24 - 16, cy, row.toggleOn, ink);
        } else if (!row.right.empty()) {
          const std::string v = r.truncatedText(SMALL_FONT_ID, upper(row.right).c_str(), w / 3, EpdFontFamily::BOLD);
          rightW = trackedWidth(r, SMALL_FONT_ID, v.c_str(), 1, true);
          drawTracked(r, SMALL_FONT_ID, x + w - 24 - rightW, cy - r.getLineHeight(SMALL_FONT_ID) / 2, v.c_str(), 1, ink,
                      true);
        }
        const int font = displayFont(13);
        const int cap = 13 * 150 * 70 / 7200;
        const std::string l = r.truncatedText(font, row.label.c_str(), w - 38 - rightW - 30);
        r.drawText(font, x + 38, cy - cap / 2 - displayCapOffset(r, 13), l.c_str(), ink);
        break;
      }
      case ListStyle::Display: {
        if (on) r.fillRect(x, ry, w, rowH, true);
        const int cy = ry + rowH / 2;
        if (row.marked) diamondMark(r, x + 14, cy, 5, ink);
        int tx = x + 28;
        if (!row.kicker.empty()) {
          const std::string k = r.truncatedText(SMALL_FONT_ID, upper(row.kicker).c_str(), 112, EpdFontFamily::BOLD);
          drawTracked(r, SMALL_FONT_ID, tx, cy - r.getLineHeight(SMALL_FONT_ID) / 2, k.c_str(), 1, ink, true);
          tx += 120;
        }
        int rightW = 0;
        if (!row.right.empty()) {
          rightW = trackedWidth(r, SMALL_FONT_ID, row.right.c_str(), 1, true);
          drawTracked(r, SMALL_FONT_ID, x + w - 10 - rightW, cy - r.getLineHeight(SMALL_FONT_ID) / 2, row.right.c_str(),
                      1, ink, true);
        }
        const int font = displayFont(13);
        const std::string l = r.truncatedText(font, row.label.c_str(), x + w - tx - rightW - 20);
        const int capH = 13 * 150 * 70 / (72 * 100);
        r.drawText(font, tx, cy - capH / 2 - displayCapOffset(r, 13), l.c_str(), ink);
        break;
      }
      case ListStyle::Files: {
        if (on) r.fillRect(x, ry, w, rowH, true);
        if (!on) dashedLine(r, x, ry + rowH - 1, w);
        const int cy = ry + rowH / 2;
        // Glyph: a solid folder, or an outlined page with a folded corner.
        const int gx = x + 12;
        if (row.folder) {
          r.fillRect(gx, cy - 6, 20, 13, ink);
          r.fillRect(gx, cy - 9, 9, 4, ink);
        } else {
          const int px[] = {gx + 2, gx + 12, gx + 16, gx + 16, gx + 2};
          const int py[] = {cy - 9, cy - 9, cy - 5, cy + 9, cy + 9};
          r.fillPolygon(px, py, 5, ink);
          r.fillRect(gx + 4, cy - 7, 7, 15, !ink);
          r.fillRect(gx + 4, cy - 3, 10, 11, !ink);
        }
        int rightW = 0;
        if (row.folder) {
          const int tx[] = {x + w - 20, x + w - 12, x + w - 20};
          const int ty[] = {cy - 5, cy, cy + 5};
          r.fillPolygon(tx, ty, 3, ink);
          rightW = 24;
        } else if (!row.right.empty()) {
          const std::string v = upper(row.right);
          const int vw = trackedWidth(r, SMALL_FONT_ID, v.c_str(), 1, true);
          const int bh = r.getLineHeight(SMALL_FONT_ID) + 2;
          r.drawRect(x + w - 12 - vw - 10, cy - bh / 2, vw + 10, bh, 1, ink);
          drawTracked(r, SMALL_FONT_ID, x + w - 12 - vw - 5, cy - bh / 2 + 1, v.c_str(), 1, ink, true);
          rightW = vw + 22;
        }
        const int tx = x + 12 + 24 + 12;
        const int font = displayFont(13);
        const int cap = 13 * 150 * 70 / 7200;
        const std::string name = r.truncatedText(font, row.label.c_str(), x + w - tx - rightW - 16);
        if (!row.kicker.empty()) {
          // Name over a small detail line (firmware picker: size).
          const int lh = r.getLineHeight(SMALL_FONT_ID);
          const int top = cy - (cap + 4 + lh) / 2;
          r.drawText(font, tx, top - displayCapOffset(r, 13), name.c_str(), ink);
          drawTracked(r, SMALL_FONT_ID, tx, top + cap + 4, upper(row.kicker).c_str(), 2, ink, true);
          break;
        }
        r.drawText(font, tx, cy - cap / 2 - displayCapOffset(r, 13), name.c_str(), ink);
        break;
      }
      case ListStyle::Library: {
        // Cover cell | title + author | slanted end block; selected row inverted.
        if (on) r.fillRect(x, ry, w, rowH, true);
        r.fillRect(x, ry + rowH - 2, w, 2, true);
        r.fillRect(x + 72, ry, 2, rowH - 2, true);
        const int cw = 50, ch = 74;
        const int cx = x + (72 - cw) / 2, cyy = ry + (rowH - 2 - ch) / 2;
        r.fillRect(cx, cyy, cw, ch, false);
        bool drawn = false;
        if (!row.coverPath.empty()) {
          HalFile file;
          if (Storage.openFileForRead("LIB", row.coverPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              drawn = BaseTheme::drawCoverThumbFill(r, bitmap, Rect{cx, cyy, cw, ch});
            }
            file.close();
          }
        }
        if (!drawn) hatch(r, cx + 4, cyy + 4, cw - 8, ch - 8);
        r.drawRect(cx - 1, cyy - 1, cw + 2, ch + 2, 1, true);
        if (on) r.drawRect(cx - 2, cyy - 2, cw + 4, ch + 4, 1, false);
        // End block: black (white when selected), slanted left edge.
        const int bw = 96;
        const int bx = x + w - bw;
        const int xs[] = {bx + 30, x + w, x + w, bx};
        const int ys[] = {ry, ry, ry + rowH - 2, ry + rowH - 2};
        r.fillPolygon(xs, ys, 4, !on);
        if (!row.right.empty()) {
          const std::string v = upper(row.right);
          const int vw = trackedWidth(r, UI_10_FONT_ID, v.c_str(), 1, true);
          drawTracked(r, UI_10_FONT_ID, x + w - 12 - vw, ry + (rowH - r.getLineHeight(UI_10_FONT_ID)) / 2, v.c_str(), 1,
                      on, true);
        }
        const int tx = x + 72 + 16;
        const int tmax = bx + 10 - tx;
        const int font = displayFont(13);
        const int cap = 13 * 150 * 70 / 7200;
        const int authorH = r.getLineHeight(SMALL_FONT_ID);
        const int blockH = cap + 6 + authorH;
        const int top = ry + (rowH - blockH) / 2;
        r.drawText(font, tx, top - displayCapOffset(r, 13), r.truncatedText(font, row.label.c_str(), tmax).c_str(),
                   ink);
        if (!row.kicker.empty()) {
          const std::string a = r.truncatedText(SMALL_FONT_ID, upper(row.kicker).c_str(), tmax, EpdFontFamily::BOLD);
          drawTracked(r, SMALL_FONT_ID, tx, top + cap + 6, a.c_str(), 2, ink, true);
        }
        break;
      }
      case ListStyle::Books: {
        if (on) r.fillRect(x, ry, w, rowH, true);
        if (!on) dashedLine(r, x, ry + rowH - 1, w);
        int rightW = 0;
        if (!row.right.empty()) {
          const std::string v = upper(row.right);
          rightW = trackedWidth(r, SMALL_FONT_ID, v.c_str(), 1, true);
          drawTracked(r, SMALL_FONT_ID, x + w - 10 - rightW, ry + (rowH - r.getLineHeight(SMALL_FONT_ID)) / 2,
                      v.c_str(), 1, ink, true);
        }
        const int tx = x + 28;
        if (row.marked) diamondMark(r, x + 14, ry + rowH / 2, 5, ink);
        const int font = displayFont(13);
        const std::string l = r.truncatedText(font, row.label.c_str(), x + w - tx - rightW - 20);
        r.drawText(font, tx, ry + 14 - displayCapOffset(r, 13), l.c_str(), ink);
        if (!row.kicker.empty()) {
          const std::string a =
              r.truncatedText(SMALL_FONT_ID, upper(row.kicker).c_str(), x + w - tx - rightW - 20, EpdFontFamily::BOLD);
          drawTracked(r, SMALL_FONT_ID, tx, ry + rowH - 8 - r.getLineHeight(SMALL_FONT_ID), a.c_str(), 2, ink, true);
        }
        break;
      }
      case ListStyle::Cards: {
        if (on) {
          fillChamfer(r, x, ry, w, rowH, 14, true);
        } else {
          drawChamfer(r, x, ry, w, rowH, 14, 2);
        }
        int ty = ry + 12;
        if (!row.kicker.empty()) {
          const std::string k = r.truncatedText(SMALL_FONT_ID, upper(row.kicker).c_str(), w - 28, EpdFontFamily::BOLD);
          drawTracked(r, SMALL_FONT_ID, x + 14, ty, k.c_str(), 2, ink, true);
          ty += r.getLineHeight(SMALL_FONT_ID);
        }
        int rightW = 0;
        if (!row.right.empty()) {
          rightW = trackedWidth(r, UI_10_FONT_ID, row.right.c_str(), 1, true);
          drawTracked(r, UI_10_FONT_ID, x + w - 14 - rightW, ry + (rowH - r.getLineHeight(UI_10_FONT_ID)) / 2,
                      row.right.c_str(), 1, ink, true);
        }
        const int font = displayFont(13);
        const int capH = 13 * 150 * 70 / (72 * 100);
        int lx = x + 14;
        if (on) {
          triangleMark(r, lx, ty + 2 + capH / 2, false);
          lx += 18;
        }
        const std::string l = r.truncatedText(font, row.label.c_str(), x + w - lx - rightW - 24);
        r.drawText(font, lx, ty + 2 - displayCapOffset(r, 13), l.c_str(), ink);
        break;
      }
    }
  }
  return visible;
}

}  // namespace deck
