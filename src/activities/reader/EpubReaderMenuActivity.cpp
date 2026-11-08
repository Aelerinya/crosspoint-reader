#include "EpubReaderMenuActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/Sprite.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckTheme.h"
#include "images/DeckOperator.h"
#include "util/PluginEvents.h"
#include "util/SlideSwitch.h"

namespace fui = freeink::ui;

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks,
                                               const bool hasClippings, MenuAction& selectedAction)
    : UiListActivity("EpubReaderMenu", renderer, mappedInput),
      selectedAction(selectedAction),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  buildMenuItems(menuItems, hasFootnotes, hasBookmarks, hasClippings);
  buildMenuRowItems();
}

void EpubReaderMenuActivity::onEnter() {
  RenderLock lock;
  UiListActivity::onEnter();
  for (size_t i = 0; i < menuItems.size(); ++i) {
    if (menuItems[i].action == selectedAction) {
      nav.reset(static_cast<int>(i));
      break;
    }
  }
}

void EpubReaderMenuActivity::onExit() {
  if (nav.selected >= 0 && nav.selected < static_cast<int>(menuItems.size())) {
    selectedAction = menuItems[nav.selected].action;
  }
  UiListActivity::onExit();
}

// Populates menuRowItems's labels/actionValue from menuItems. Called once
// here since menuItems (and thus which rows exist) never changes after
// construction; buildScreen() only touches the two rows with a live value.
void EpubReaderMenuActivity::buildMenuRowItems() {
  for (size_t i = 0; i < menuItems.size() && i < MAX_MENU_ITEMS; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i].labelId);
    item.actionValue = static_cast<int16_t>(i);
    menuRowItems[i] = item;
  }
}

void EpubReaderMenuActivity::buildMenuItems(std::vector<MenuItem>& items, const bool hasFootnotes,
                                            const bool hasBookmarks, const bool hasClippings) {
  items.clear();
  items.reserve(MAX_MENU_ITEMS);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::SAVE_CLIPPING, StrId::STR_SAVE_CLIPPING});
  if (hasClippings) {
    items.push_back({MenuAction::VIEW_CLIPPINGS, StrId::STR_VIEW_CLIPPINGS});
  }
  if (!slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_NIGHT_MODE)) {
    items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  }
  if (Frontlight.present() && !slideSwitchOwnsLight()) {
    items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  }
  if (bleturner::status().compiledIn && SETTINGS.ble.peerAddr[0] != '\0') {
    items.push_back({MenuAction::BLUETOOTH, StrId::STR_BT_PAGE_TURNER});
  }
  if (SETTINGS.dictionaryName[0] != '\0') {
    items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  }
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  bool canSync = KOREADER_STORE.hasCredentials();
  for (size_t event = 0; !canSync && event < static_cast<size_t>(pluginevents::Event::COUNT); ++event) {
    canSync = pluginevents::anySubscriber(static_cast<pluginevents::Event>(event));
  }
  if (canSync) {
    items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  }
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
}

void EpubReaderMenuActivity::toggleBluetooth() {
  SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
  if (!SETTINGS.ble.enabled) bleturner::switchOff();
  SETTINGS.saveToFile();
  // Enabling waits for the main-loop lifecycle's reader heap gates, then says in the status bar
  // whether a remote linked.
  if (SETTINGS.ble.enabled) bleturner::requestConnect();
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  const auto selectedAction = menuItems[index].action;
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       pendingOrientation = idx;
                       // Rotate the menu immediately. Only the renderer turns;
                       // SETTINGS.orientation stays unchanged so the reader's
                       // result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::BLUETOOTH) {
    toggleBluetooth();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  if (deck::listsActive()) {
    // Rows inside the panel drawChrome paints (30 px under its top, clear of the
    // scroll box on the right); no progress summary (labels only).
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(kDeckPanelTop + 33), 69, 65, 1});
  } else {
    screen.setContentMarginFromScreen(fui::Insets{
        static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
        static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
        static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

    // Progress summary where the old sub-header band sat.
    std::string progressLine;
    if (totalPages > 0) {
      progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                     std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
    }
    progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
    const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
    const int16_t pad = screen.theme().headerSidePadding;
    screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  }

  // menuRowItems's labels/actionValue were set once in the constructor (see
  // buildMenuRowItems()); only rows with live values need refreshing here.
  for (size_t i = 0; i < menuItems.size(); i++) {
    const auto action = menuItems[i].action;
    if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[i].value = pageTurnLabels[selectedPageTurnOption];
    } else if (action == MenuAction::NIGHT_MODE) {
      GUI.setCheckboxRow(menuRowItems[i], SETTINGS.screenInverted);
    } else if (action == MenuAction::BLUETOOTH) {
      GUI.setCheckboxRow(menuRowItems[i], SETTINGS.ble.enabled);
    } else if (action == MenuAction::FRONTLIGHT) {
      GUI.setCheckboxRow(menuRowItems[i], Frontlight.isOn());
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(menuItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderMenuActivity::drawChrome() {
  if (deck::listsActive()) {
    // v2: the Operator above, pressing a chamfered panel (top-right and
    // bottom-left cuts) that holds the rows; the page stays visible above.
    const int W = renderer.getScreenWidth();
    const int bottom = renderer.getScreenHeight() - 58;
    if (deck::companionOn()) drawSprite(renderer, kOpMenu, 0, 0);
    const int top = kDeckPanelTop;
    const int ox[] = {16, W - 16 - 24, W - 16, W - 16, 16 + 24, 16};
    const int oy[] = {top, top, top + 24, bottom, bottom, bottom - 24};
    renderer.fillPolygon(ox, oy, 6, true);
    const int ix[] = {19, W - 19 - 23, W - 19, W - 19, 19 + 23, 19};
    const int iy[] = {top + 3, top + 3, top + 26, bottom - 3, bottom - 3, bottom - 26};
    renderer.fillPolygon(ix, iy, 6, false);
    deck::hatch(renderer, 19 + 14, top + 13, W - 19 - 60 - (19 + 14), 8);
    const int sx = W - 19 - 14 - 40;
    renderer.drawRect(sx, top + 47, 40, bottom - 3 - 44 - (top + 47), 2, true);
    deck::hatch(renderer, sx + 4, top + 51, 32, bottom - 3 - 44 - (top + 47) - 8);
    return;
  }
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), title.c_str());
}

void EpubReaderMenuActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  UiListActivity::render(std::move(lock));
}
