#include "KeyboardLayoutsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"

namespace fui = freeink::ui;

void KeyboardLayoutsActivity::onEnter() {
  UiListActivity::onEnter();
  workingMask = keyboard_layouts::enabled();
  edited = false;

  for (int i = 0; i < keyboard_layouts::COUNT; ++i) {
    rowItems[i].label = I18N.getLanguageName(keyboard_layouts::ALL[i].language);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void KeyboardLayoutsActivity::onExit() {
  if (edited && workingMask != SETTINGS.keyboardLayouts) {
    SETTINGS.keyboardLayouts = workingMask;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* KeyboardLayoutsActivity::headerTitle() const { return tr(STR_KEYBOARD_LAYOUTS); }

bool KeyboardLayoutsActivity::isLocked(const uint8_t i) const {
  const uint16_t bit = keyboard_layouts::bitAt(i);
  if (!(workingMask & bit)) return false;
  const uint16_t without = static_cast<uint16_t>(workingMask & ~bit);
  return (without & keyboard_layouts::LATIN_BITS) == 0;
}

void KeyboardLayoutsActivity::activateIndex(const int index) {
  nav.selected = index;
  // The row stays on screen with a new checkbox state; a lingering flash would
  // gray an unrelated row on the repaint below.
  app.clearTapFlash();

  if (isLocked(static_cast<uint8_t>(index))) {
    requestUpdate();
    return;
  }

  const uint16_t bit = keyboard_layouts::bitAt(static_cast<uint8_t>(index));
  workingMask = static_cast<uint16_t>(workingMask ^ bit);
  edited = true;
  requestUpdate();
}

namespace {
// Layout names shown after the language on the Deck screen (KeyboardLayoutId order).
const char* layoutName(const freeink::ui::KeyboardLayoutId id) {
  using freeink::ui::KeyboardLayoutId;
  switch (id) {
    case KeyboardLayoutId::QwertyEn:
      return "QWERTY";
    case KeyboardLayoutId::AzertyFr:
      return "AZERTY";
    case KeyboardLayoutId::QwertzDe:
      return "QWERTZ";
    case KeyboardLayoutId::CyrillicRu:
    case KeyboardLayoutId::CyrillicUk:
    case KeyboardLayoutId::CyrillicBe:
    case KeyboardLayoutId::CyrillicKk:
      return "ЙЦУКЕН";
    default:
      return nullptr;
  }
}

constexpr int kDeckRowsTop = 116;
constexpr int kDeckRowH = deck::kPickRowHeight;
}  // namespace

void KeyboardLayoutsActivity::drawChrome() {
  if (!deck::listsActive()) {
    UiListActivity::drawChrome();
    return;
  }
  deck::drawSmallHeader(renderer, "KEYBOARD");
  deck::drawBigTitle(renderer, 53, "KEYBOARDS", "鍵盤");
  deck::drawTitleRule(renderer, 113);
  // Preview of the focused layout under the rows.
  const int sel = std::clamp<int>(nav.selected.load(), 0, keyboard_layouts::COUNT - 1);
  const auto id = keyboard_layouts::ALL[sel].id;
  const char* name = layoutName(id);
  const int rowsBottom = kDeckRowsTop + keyboard_layouts::COUNT * kDeckRowH;
  const int previewH = 18 + 3 * 43;
  const int y = std::min(rowsBottom + 22, renderer.getScreenHeight() - 70 - previewH);
  if (y > kDeckRowsTop + 3 * kDeckRowH) {
    // The caption is in the Latin-only UI face: Cyrillic layouts go by their transliteration.
    const bool latin = name && static_cast<unsigned char>(name[0]) < 0x80;
    deck::drawKeyboardPreview(
        renderer, y, static_cast<uint8_t>(id),
        latin  ? name
        : name ? "JCUKEN"
               : deck::englishLanguageName(static_cast<uint8_t>(keyboard_layouts::ALL[sel].language)));
  }
}

void KeyboardLayoutsActivity::drawFooter() {
  if (!deck::listsActive()) {
    UiListActivity::drawFooter();
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void KeyboardLayoutsActivity::deckRowExtra(const int index, deck::Row& row) {
  if (index < 0 || index >= keyboard_layouts::COUNT) return;
  row.kind = deck::RowKind::Check;
  const uint8_t i = static_cast<uint8_t>(index);
  row.toggleOn = (workingMask & keyboard_layouts::bitAt(i)) != 0;
  row.right = isLocked(i) ? "DEFAULT" : "";
  if (const char* name = layoutName(keyboard_layouts::ALL[i].id)) {
    row.label = std::string(I18N.getLanguageName(keyboard_layouts::ALL[i].language)) + " (" + name + ")";
  }
}

void KeyboardLayoutsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (deck::listsActive()) {
    // Rows end where the preview starts (drawChrome); the list scrolls inside.
    const int previewTop = renderer.getScreenHeight() - 70 - (18 + 3 * 43);
    const int bottom = std::max<int>(
        metrics.buttonHintsHeight,
        renderer.getScreenHeight() - std::min(previewTop - 22, kDeckRowsTop + keyboard_layouts::COUNT * kDeckRowH));
    screen.setContentMarginFromScreen(fui::Insets{kDeckRowsTop, 0, static_cast<int16_t>(bottom), 0});
  } else {
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  }

  for (int i = 0; i < keyboard_layouts::COUNT; ++i) {
    const uint8_t row = static_cast<uint8_t>(i);
    GUI.setCheckboxRow(rowItems[i], (workingMask & keyboard_layouts::bitAt(row)) != 0);
    if (isLocked(row)) {
      rowItems[i].toggle = false;
      rowItems[i].value = tr(STR_DEFAULT_VALUE);
    }
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = keyboard_layouts::COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
