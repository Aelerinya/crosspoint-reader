#include "AboutActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"
#include "images/DeckOperator.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_DEVICE = 0,
  ITEM_FIRMWARE,
  ITEM_CHIP,
  ITEM_FLASH,
  ITEM_SD_SIZE,
  ITEM_SD_USED,
  ITEM_SD_FREE,
  ITEM_DISPLAY,
  ITEM_RESOLUTION,
  ITEM_TOUCH,
  ITEM_FRONTLIGHT,
  ITEM_RTC,
  ITEM_IMU,
  ITEM_MAC,
};

// Deliberately hardcoded English, exempt from the tr() rule: support reads
// these screenshots across every device language, so the labels must be
// identical on every unit.
const char* const menuNames[AboutActivity::ITEM_COUNT] = {
    "Device",
    "Firmware",
    "Chip",
    "Flash",
    "SD Size",
    "SD Used",
    "SD Free",
    "Display Controller",
    "Resolution",
    "Touch",
    "Frontlight",
    "RTC",
    "Tilt Sensor (IMU)",
    "MAC Address",
};

// "14.5 GB" / "35 MB": GiB with one decimal, MiB below 1 GiB.
std::string formatBytes(const uint64_t bytes) {
  char buf[24];
  constexpr uint64_t MIB = 1024ull * 1024ull;
  constexpr uint64_t GIB = 1024ull * MIB;
  if (bytes >= GIB) {
    const uint64_t tenths = (bytes * 10 + GIB / 2) / GIB;
    snprintf(buf, sizeof(buf), "%u.%u GB", static_cast<unsigned>(tenths / 10), static_cast<unsigned>(tenths % 10));
  } else {
    snprintf(buf, sizeof(buf), "%u MB", static_cast<unsigned>((bytes + MIB / 2) / MIB));
  }
  return buf;
}

// Chip part numbers, not user prose — deliberately untranslated.
const char* displayControllerName(const BoardConfig::DisplayController c) {
  switch (c) {
    case BoardConfig::DisplayController::SSD1677:
      return "SSD1677";
    case BoardConfig::DisplayController::UC8253:
      return "UC8253";
    case BoardConfig::DisplayController::ED2208:
      return "ED2208";
    case BoardConfig::DisplayController::LgfxEpd:
      return "LovyanGFX EPD";
    case BoardConfig::DisplayController::IT8951:
      return "IT8951";
    case BoardConfig::DisplayController::UC8279:
      return "UC8279";
    case BoardConfig::DisplayController::UC8179:
      return "UC8179";
    case BoardConfig::DisplayController::UC8279C:
      return "UC8279C";
  }
  return "?";
}

const char* touchControllerName(const BoardConfig::TouchController c) {
  switch (c) {
    case BoardConfig::TouchController::None:
      return nullptr;
    case BoardConfig::TouchController::Chsc6x:
      return "CHSC6X";
    case BoardConfig::TouchController::Gt911:
      return "GT911";
    case BoardConfig::TouchController::Ft5x06:
      return "FT5x06";
    case BoardConfig::TouchController::Ft6336u:
      return "FT6336U";
    case BoardConfig::TouchController::Gslx680:
      return "GSLX680";
  }
  return nullptr;
}
}  // namespace

AboutActivity::AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("About", renderer, mappedInput) {}

const char* AboutActivity::headerTitle() const { return "About"; }

void AboutActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = menuNames[i];
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }

  char buf[32];
  // Hardware and firmware information is fixed after boot; fill it once here.
  // BoardConfig::ACTIVE reflects RUNTIME detection: selectDevice() picked the
  // profile and applyXteinkDisplayController() may have promoted the display
  // controller to the panel actually found on the bus.
  rowValues_[ITEM_DEVICE] = BoardConfig::ACTIVE.name;
  rowValues_[ITEM_FIRMWARE] = CROSSPOINT_VERSION;
  snprintf(buf, sizeof(buf), "%s rev %u", ESP.getChipModel(), static_cast<unsigned>(ESP.getChipRevision()));
  rowValues_[ITEM_CHIP] = buf;
  snprintf(buf, sizeof(buf), "%u MB", static_cast<unsigned>(ESP.getFlashChipSize() / (1024u * 1024u)));
  rowValues_[ITEM_FLASH] = buf;
  // The profile names a boot default; boards that probe their panel (Picco,
  // Sticky) can run another driver, and the driver knows which.
  const char* liveController = display.controllerName();
  rowValues_[ITEM_DISPLAY] =
      liveController ? liveController : displayControllerName(BoardConfig::ACTIVE.displayController);
  snprintf(buf, sizeof(buf), "%ux%u", static_cast<unsigned>(BoardConfig::ACTIVE.displayWidth),
           static_cast<unsigned>(BoardConfig::ACTIVE.displayHeight));
  rowValues_[ITEM_RESOLUTION] = buf;
  const char* touch = touchControllerName(BoardConfig::ACTIVE.touch.controller);
  rowValues_[ITEM_TOUCH] = touch ? touch : "No";
  rowValues_[ITEM_FRONTLIGHT] = Frontlight.present() ? "Yes" : "No";
  rowValues_[ITEM_RTC] = halClock.isAvailable() ? "Yes" : "No";
  rowValues_[ITEM_IMU] = halTiltSensor.isAvailable() ? "Yes" : "No";
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  rowValues_[ITEM_MAC] = buf;

  // The used-space scan walks the whole FAT (seconds on a large card): answer the press with "Loading" first.
  // onEnter runs without the render lock; take it for the popup.
  {
    RenderLock lock;
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  }
  // The firmware does not boot without a card, so About always has one.
  const uint64_t sdTotal = Storage.sdTotalBytes();
  rowValues_[ITEM_SD_SIZE] = formatBytes(sdTotal);
  uint64_t sdUsed = 0;
  sdTotal_ = sdTotal;
  if (Storage.sdUsedBytes(sdUsed)) {
    sdUsed_ = sdUsed;
    sdUsedKnown_ = true;
    rowValues_[ITEM_SD_USED] = formatBytes(sdUsed);
    rowValues_[ITEM_SD_FREE] = formatBytes(sdUsed <= sdTotal ? sdTotal - sdUsed : 0);
  } else {
    rowValues_[ITEM_SD_USED] = rowValues_[ITEM_SD_FREE] = "Error.";
  }
}

void AboutActivity::drawChrome() {
  if (!deck::listsActive()) {
    UiListActivity::drawChrome();
    return;
  }
  const GfxRenderer& r = renderer;
  const int W = r.getScreenWidth();
  deck::drawSmallHeader(r, "ABOUT");
  if (deck::companionOn()) drawSprite(r, kOpLens, W - 12 - kOpLens.width, 46);
  // Board id "xteink_x4_classic" -> "XTEINK X4 CLASSIC", model "X4C".
  std::string full = BoardConfig::ACTIVE.name ? BoardConfig::ACTIVE.name : "";
  for (char& c : full) {
    if (c == '_') c = ' ';
  }
  full = deck::upper(full);
  std::string model = full;
  if (model.rfind("XTEINK ", 0) == 0) model.erase(0, 7);
  for (const auto& rep : {std::pair<const char*, const char*>{" CLASSIC", "C"}, {" PRO", "P"}}) {
    const size_t at = model.find(rep.first);
    if (at != std::string::npos) model.replace(at, strlen(rep.first), rep.second);
  }
  const int big = deck::displayFont(76);
  r.drawText(big, 16, 60 - deck::displayCapOffset(r, 76),
             r.truncatedText(big, model.c_str(), W - 32 - (deck::companionOn() ? kOpLens.width : 0)).c_str());
  const std::string sub = full + " \xc2\xb7 CROSSPOINT";
  deck::drawTracked(r, UI_10_FONT_ID, 18, 194, r.truncatedText(UI_10_FONT_ID, sub.c_str(), 290).c_str(), 2, true, true);
  // SD card: used / total and 20 segments.
  const int small = SMALL_FONT_ID;
  deck::drawTracked(r, small, 16, 278, "// SD CARD", 2, true, true);
  const std::string usage =
      deck::upper((sdUsedKnown_ ? formatBytes(sdUsed_) : std::string("?")) + " / " + formatBytes(sdTotal_));
  const int uw = deck::trackedWidth(r, small, usage.c_str(), 2, true);
  deck::drawTracked(r, small, W - 16 - uw, 278, usage.c_str(), 2, true, true);
  const int filled = sdUsedKnown_ && sdTotal_ ? static_cast<int>((sdUsed_ * 20 + sdTotal_ - 1) / sdTotal_) : 0;
  deck::segments(r, 16, 296, W - 32, 15, 20, std::min(filled, 20));
  r.fillRect(16, 326, W - 32, 2, true);
  // Key/value rows.
  std::string display = rowValues_[ITEM_DISPLAY] + " \xc2\xb7 " + rowValues_[ITEM_RESOLUTION];
  const std::pair<const char*, const std::string*> rows[] = {
      {"FIRMWARE", &rowValues_[ITEM_FIRMWARE]}, {"CHIP", &rowValues_[ITEM_CHIP]},
      {"FLASH", &rowValues_[ITEM_FLASH]},       {"DISPLAY", &display},
      {"TOUCH", &rowValues_[ITEM_TOUCH]},       {"FRONTLIGHT", &rowValues_[ITEM_FRONTLIGHT]},
      {"RTC", &rowValues_[ITEM_RTC]},           {"TILT SENSOR", &rowValues_[ITEM_IMU]},
      {"MAC", &rowValues_[ITEM_MAC]},
  };
  int y = 328;
  constexpr int kRowH = 39;
  for (const auto& row : rows) {
    const int cy = y + kRowH / 2;
    deck::drawTracked(r, small, 28, cy - r.getLineHeight(small) / 2, row.first, 2, true, true);
    const std::string v =
        r.truncatedText(UI_10_FONT_ID, deck::upper(*row.second).c_str(), W - 16 - 178 - 8, EpdFontFamily::BOLD);
    deck::drawTracked(r, UI_10_FONT_ID, 178, cy - r.getLineHeight(UI_10_FONT_ID) / 2, v.c_str(), 1, true, true);
    for (int x = 16; x < W - 16; x += 6) r.fillRect(x, y + kRowH - 1, 3, 1, true);
    y += kRowH;
  }
}

void AboutActivity::drawFooter() {
  if (!deck::listsActive()) {
    UiListActivity::drawFooter();
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AboutActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (deck::listsActive()) return;  // drawChrome draws the whole sheet
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
