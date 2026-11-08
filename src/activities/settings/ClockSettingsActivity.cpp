#include "ClockSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <ctime>
#include <memory>

#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "TimezonePickerActivity.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"
#include "util/Timezones.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_TIMEZONE = 0,
  ITEM_DST,
  ITEM_FORMAT,
  ITEM_SHOW_ON_HOME,
  ITEM_SYNC,
};

const StrId menuNames[ClockSettingsActivity::ITEM_COUNT] = {
    StrId::STR_TIMEZONE,        StrId::STR_CLOCK_DST,      StrId::STR_CLOCK_FORMAT,
    StrId::STR_CLOCK_IN_HEADER, StrId::STR_CLOCK_SYNC_NOW,
};

const StrId dstNames[CrossPointSettings::CLOCK_DST_MODE_COUNT] = {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON,
                                                                  StrId::STR_STATE_OFF};
}  // namespace

ClockSettingsActivity::ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ClockSettings", renderer, mappedInput) {}

void ClockSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* ClockSettingsActivity::headerTitle() const { return tr(STR_CLOCK); }

void ClockSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  switch (index) {
    case ITEM_TIMEZONE:
      if (auto activity = makeUniqueNoThrow<TimezonePickerActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: TimezonePickerActivity");
      }
      return;
    case ITEM_DST:
      SETTINGS.clockDst = (SETTINGS.clockDst + 1) % CrossPointSettings::CLOCK_DST_MODE_COUNT;
      timezones::applyToClock();
      break;
    case ITEM_FORMAT:
      SETTINGS.clockFormat = (SETTINGS.clockFormat + 1) % 2;
      break;
    case ITEM_SHOW_ON_HOME:
      SETTINGS.clockShowInHeader = (SETTINGS.clockShowInHeader + 1) % 2;
      break;
    case ITEM_SYNC:
      if (auto activity = makeUniqueNoThrow<ClockSyncActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: ClockSyncActivity");
      }
      return;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

namespace {
constexpr int kDeckRowsTop = 271;
}  // namespace

void ClockSettingsActivity::drawChrome() {
  if (!deck::listsActive()) {
    UiListActivity::drawChrome();
    return;
  }
  deck::drawSmallHeader(renderer, "CLOCK", "");
  const int W = renderer.getScreenWidth();
  struct tm t{};
  const bool have = halClock.localTime(t);
  char time[8] = "--:--";
  if (have) {
    const int h = SETTINGS.clockFormat == 1 ? (t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12) : t.tm_hour;
    snprintf(time, sizeof(time), "%02d:%02d", h, t.tm_min);
  }
  const int big = deck::displayFont(76);
  const int tw = renderer.getTextWidth(big, time);
  renderer.drawText(big, (W - tw) / 2, 68 - deck::displayCapOffset(renderer, 76), time);
  if (have) {
    static constexpr const char* kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char date[48];
    snprintf(date, sizeof(date), "%s %02d %s %04d%s \xc2\xb7 %s", kDays[t.tm_wday % 7], t.tm_mday,
             kMonths[t.tm_mon % 12], t.tm_year + 1900,
             SETTINGS.clockFormat == 1 ? (t.tm_hour < 12 ? " AM" : " PM") : "",
             timezones::table()[timezones::activeIndex()].name);
    const std::string d = deck::upper(date);
    const int dw = deck::trackedWidth(renderer, SMALL_FONT_ID, d.c_str(), 3, true);
    deck::drawTracked(renderer, SMALL_FONT_ID, (W - dw) / 2, 68 + deck::bigTitleCap(76) + 22, d.c_str(), 3, true, true);
  }
  // Last sync under the rows.
  const char* last = SETTINGS.clockHasBeenSynced ? "LAST SYNC \xc2\xb7 DONE" : "LAST SYNC \xc2\xb7 NEVER";
  deck::drawTracked(renderer, SMALL_FONT_ID, 28, kDeckRowsTop + ITEM_COUNT * 41 + 12, last, 2, true, true);
}

void ClockSettingsActivity::drawFooter() {
  if (!deck::listsActive()) {
    UiListActivity::drawFooter();
    return;
  }
  const int sel = nav.selected;
  const char* ok = sel == ITEM_TIMEZONE || sel == ITEM_SYNC ? "OPEN" : "CHANGE";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), ok, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClockSettingsActivity::deckRowExtra(const int index, deck::Row& row) {
  if (index == ITEM_SYNC) {
    row.right.clear();
    row.kind = deck::RowKind::Action;
  } else if (!row.hasToggle) {
    row.kind = deck::RowKind::Enum;
  }
}

void ClockSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (deck::listsActive()) {
    screen.setContentMarginFromScreen(fui::Insets{
        kDeckRowsTop, 0, static_cast<int16_t>(renderer.getScreenHeight() - kDeckRowsTop - ITEM_COUNT * 41), 0});
  } else {
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  }

  // Every value is a flash/translation string or the member time buffer, so
  // the render pass allocates nothing.
  rowItems_[ITEM_TIMEZONE].value = timezones::table()[timezones::activeIndex()].name;
  const uint8_t dst = SETTINGS.clockDst < CrossPointSettings::CLOCK_DST_MODE_COUNT ? SETTINGS.clockDst : uint8_t{0};
  rowItems_[ITEM_DST].value = I18N.get(dstNames[dst]);
  rowItems_[ITEM_FORMAT].value = SETTINGS.clockFormat == 1 ? tr(STR_CLOCK_FORMAT_12H) : tr(STR_CLOCK_FORMAT_24H);
  GUI.setCheckboxRow(rowItems_[ITEM_SHOW_ON_HOME], SETTINGS.clockShowInHeader);
  // The sync row's value is the current time itself: it confirms the sync,
  // previews format/zone changes, and reads "Not Set" until the first sync.
  rowItems_[ITEM_SYNC].value =
      SETTINGS.clockHasBeenSynced && halClock.formatTime(syncTime_, sizeof(syncTime_), SETTINGS.clockFormat == 1)
          ? syncTime_
          : tr(STR_NOT_SET);

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
