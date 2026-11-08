#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "components/themes/BaseTheme.h"

class GfxRenderer;
struct Sprite;

// Deck list screens (design/SPEC.md screens 02, 05-07, 11, 13), drawn
// directly instead of through the FreeInkUI list on the Deck theme.
namespace deck {

enum class ListStyle : uint8_t {
  Rows,        // settings: bold label + right value, dashed separators, inverted selection
  Menu,        // reader menu: ▶ mark, label, right value, slanted inverted selection
  Display,     // TOC / library: small kicker + display-face title, inverted selection
  Cards,       // transfer / bookmarks: chamfered outlined cards, display-face label
  Books,       // library: display-face title over a small author line, right value
  Library,     // v2 library: 84 px rows, cover cell, title + author, slanted end block
  Files,       // v2 files: 50 px rows, folder/file glyph, name, extension tag, dashed rule
  Categories,  // v2 settings top: tall chamfered rows, display-face label, kana on the right
  Pick,        // v2 pick list: ◆ current, ▶ focus, native-script label, small right tag / checkbox
};

// What the right end of a settings row shows (v2).
enum class RowKind : uint8_t {
  Value,   // plain value text
  Enum,    // value; reads "◂ VALUE ▸" on the selected row
  Action,  // ▸
  Check,   // Pick lists: slanted checkbox (toggleOn)
};

struct Row {
  std::string label;
  std::string kicker;   // small text before (Display) or above (Cards) the label
  std::string right;    // right-aligned value
  bool marked = false;  // ◆ (current chapter, etc.)
  bool hasToggle = false;
  bool toggleOn = false;
  bool isHeader = false;  // group heading row (not selectable)
  std::string coverPath;  // Library rows: cover thumbnail BMP (74 px tall)
  bool folder = false;    // Files rows
  std::string heading;    // section heading drawn above the row
  RowKind kind = RowKind::Value;
  bool dimmed = false;         // drawn outlined/light (not available yet)
  const char* kana = nullptr;  // Categories rows
};

struct ListSpec {
  ListStyle style = ListStyle::Rows;
  std::string kicker;                 // "// BOOKMARKS · 3" stripe line above the rows
  std::string kickerRight;            // right end of the stripe line
  const char* const* tabs = nullptr;  // optional slanted tab bar
  int tabCount = 0;
  int activeTab = 0;
  bool tabsFocused = false;
  const Sprite* fox = nullptr;  // bottom-right companion under the rows
  std::string emptyText;        // shown when count == 0
  bool subtitles = false;       // show ListItem subtitles (design: labels only)
  bool upperLabels = true;
  int markedIndex = -1;       // row drawn with the ◆ mark
  bool nativeLabels = false;  // Pick: labels in the full-coverage UI face (any script), not upper-cased
  int rowHeight = 0;          // overrides the style's row height (Categories fill the area by default)
};

// v2 settings category strip: black band with slanted tabs, the active one white.
// Returns its height (38).
int drawCategoryStrip(const GfxRenderer& renderer, int y, const char* const* labels, int count, int active);
// v2 OFF | ON pair right-aligned at `right`, vertically centred on cy; returns its width.
int drawToggle(const GfxRenderer& renderer, int right, int cy, bool on, bool black = true);
// Full-coverage UI face (stock Ubuntu/Noto with Cyrillic, Hebrew, Arabic, Vietnamese)
// for names in their own script; registered while the Deck fonts are.
int nativeFont();

// Slanted tab bar (spec.tabs...) with its top-left at (x, y); tabBarHeight
// includes the space above and below.
void drawTabBar(const GfxRenderer& renderer, int x, int y, const ListSpec& spec);
int tabBarHeight(const GfxRenderer& renderer);

// True when the Deck theme draws list screens itself (button boards only:
// touch boards keep the FreeInkUI list and its hit testing).
bool listsActive();

// Draws the list in `area`; keeps `top` so `selected` stays visible and
// returns how many rows fit. `rowAt` fills a row (false = skip/blank).
int drawList(const GfxRenderer& renderer, Rect area, const ListSpec& spec, int count, int selected, int& top,
             const std::function<bool(int index, Row& row)>& rowAt);
// The same layout without drawing: settles `top`, returns the rows that fit.
// Cards size their rows by whether the selected row has a kicker; without rows
// the measurement assumes none.
int listViewport(const GfxRenderer& renderer, Rect area, const ListSpec& spec, int count, int selected, int& top);

}  // namespace deck
