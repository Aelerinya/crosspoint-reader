#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckList.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"
#include "images/DeckOperator.h"

namespace fui = freeink::ui;

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right.
constexpr uint8_t kRoleCount = 4;
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
// Duration to show temporary error text when reassigning a button.
constexpr unsigned long kErrorDisplayMs = 1500;
}  // namespace

void ButtonRemapActivity::onEnter() {
  Activity::onEnter();

  // Start with all roles unassigned to avoid duplicate blocking.
  currentStep = 0;
  exitAction = ExitAction::None;
  tempMapping[0] = kUnassigned;
  tempMapping[1] = kUnassigned;
  tempMapping[2] = kUnassigned;
  tempMapping[3] = kUnassigned;
  errorMessage.clear();
  errorUntil = 0;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    rowItems[i].label = getRoleName(i);
  }
  resetUi();
  app.setScreen(&ButtonRemapActivity::screenTrampoline, this);
  requestUpdate();
}

void ButtonRemapActivity::loop() {
  // Remember the first side action even if another button is still held.
  if (exitAction == ExitAction::None || exitAction == ExitAction::Save) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      exitAction = ExitAction::Reset;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      exitAction = ExitAction::Cancel;
    }
  }

  // Clear any temporary warning after its timeout.
  if (errorUntil > 0 && millis() > errorUntil) {
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return;
  }

  // Finish the entire gesture before returning to Settings or changing the live mapping.
  if (exitAction != ExitAction::None) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.isPressed(MappedInputManager::Button::Left) ||
        mappedInput.isPressed(MappedInputManager::Button::Right) ||
        mappedInput.isPressed(MappedInputManager::Button::Up) ||
        mappedInput.isPressed(MappedInputManager::Button::Down)) {
      return;
    }
    switch (exitAction) {
      case ExitAction::None:
        return;
      case ExitAction::Save:
        applyTempMapping();
        SETTINGS.saveToFile();
        break;
      case ExitAction::Reset:
        SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
        SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
        SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
        SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
        SETTINGS.saveToFile();
        break;
      case ExitAction::Cancel:
        break;
    }
    finish();
    return;
  }

  {
    // Make sure UI done rendering before accepting another assignment.
    // This avoids rapid double-presses that can advance the step without a visible redraw.
    RenderLock lock(*this);

    // Wait for a front button press to assign to the current role.
    const int pressedButton = mappedInput.getPressedFrontButton();
    if (pressedButton < 0) {
      return;
    }

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= kRoleCount) {
      exitAction = ExitAction::Save;
    }

    requestUpdate();
  }
}

void ButtonRemapActivity::renderDeck() {
  const GfxRenderer& r = renderer;
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  const int small = SMALL_FONT_ID;
  r.clearScreen();
  deck::drawSmallHeader(r, "REMAP");
  deck::drawBigTitle(r, 53, "REMAP", "割当");
  deck::drawTracked(r, small, 16, 100, "PRESS THE BUTTON FOR", 2, true, true);

  // Device: dark slab, white screen, the four front buttons; the one for the
  // current role's default position framed.
  {
    const int xs[] = {30, 214, 230, 230, 214, 46, 30};
    const int ys[] = {121, 121, 137, 404, 420, 420, 404};
    r.fillPolygon(xs, ys, 7, true);
    r.fillRect(46, 137, 168, 215, false);
    for (uint8_t i = 0; i < kRoleCount; ++i) {
      const int bx = 46 + i * 44, by = 368;
      bool assigned = false;
      for (uint8_t role = 0; role < kRoleCount; ++role) assigned |= tempMapping[role] == i;
      if (i == currentStep && currentStep < kRoleCount) {
        r.fillRect(bx - 2, by - 4, 48, 36, false);
        r.drawRect(bx + 2, by, 40, 28, 3, true);
      } else if (assigned) {
        deck::hatch(r, bx, by, 36, 28, false);
      } else {
        r.drawRect(bx, by, 36, 28, 2, false);
      }
    }
  }

  // Current role in big type, step counter, Operator pressing a key.
  if (currentStep < kRoleCount) {
    const int big = deck::displayFont(31);
    const std::string role = r.truncatedText(big, deck::upper(getRoleName(currentStep)).c_str(), W - 250 - 16);
    r.drawText(big, 250, 155 - deck::displayCapOffset(r, 31), role.c_str());
    char step[8];
    snprintf(step, sizeof(step), "%u / %u", static_cast<unsigned>(currentStep + 1), static_cast<unsigned>(kRoleCount));
    r.drawText(small, 250, 155 + deck::bigTitleCap(31) + 8, step, true, EpdFontFamily::BOLD);
  }
  if (deck::companionOn()) drawSprite(r, kOpPressS, W - 30 - kOpPressS.width, 250);

  // Roles and their buttons.
  r.fillRect(16, 447, W - 32, 2, true);
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    const int y = 449 + i * 41, cy = y + 20;
    const bool on = i == currentStep;
    if (on) r.fillRect(16, y, W - 32, 41, true);
    int lx = 28;
    if (on) {
      const int xs[] = {30, 41, 30};
      const int ys[] = {cy - 6, cy, cy + 6};
      r.fillPolygon(xs, ys, 3, false);
      lx = 48;
    }
    const std::string label = deck::upper(getRoleName(i));
    deck::drawTracked(r, UI_10_FONT_ID, lx, cy - r.getLineHeight(UI_10_FONT_ID) / 2, label.c_str(), 2, !on, true);
    const std::string v =
        on ? std::string("WAITING...")
           : (tempMapping[i] == kUnassigned ? std::string("-") : deck::upper(getHardwareName(tempMapping[i])));
    const int vw = deck::trackedWidth(r, small, v.c_str(), 2, true);
    deck::drawTracked(r, small, W - 28 - vw, cy - r.getLineHeight(small) / 2, v.c_str(), 2, !on, true);
    if (!on) {
      for (int x = 16; x < W - 16; x += 6) r.fillRect(x, y + 40, 3, 1, true);
    }
  }

  if (!errorMessage.empty()) {
    const std::string e = deck::upper(errorMessage);
    const int ew = deck::trackedWidth(r, small, e.c_str(), 2, true);
    r.fillRect((W - ew) / 2 - 14, 700, ew + 28, r.getLineHeight(small) + 12, true);
    deck::drawTracked(r, small, (W - ew) / 2, 706, e.c_str(), 2, false, true);
  }

  // The only instructions: side buttons reset / cancel.
  const int hy = H - 28;
  const auto tri = [&](const int x, const bool up) {
    const int cy = hy + r.getLineHeight(small) / 2;
    const int xs[] = {x, x + 8, x + 4};
    const int ys[] = {up ? cy + 3 : cy - 3, up ? cy + 3 : cy - 3, up ? cy - 4 : cy + 4};
    r.fillPolygon(xs, ys, 3, true);
  };
  const int sw = deck::trackedWidth(r, small, "SIDE", 2, true);
  deck::drawTracked(r, small, 16, hy, "SIDE", 2, true, true);
  tri(16 + sw + 6, true);
  deck::drawTracked(r, small, 16 + sw + 20, hy, "\xc2\xb7 RESET TO DEFAULT", 2, true, true);
  const char* cancel = "\xc2\xb7 CANCEL";
  const int cw = deck::trackedWidth(r, small, cancel, 2, true);
  deck::drawTracked(r, small, W - 16 - cw, hy, cancel, 2, true, true);
  tri(W - 16 - cw - 14, false);
  deck::drawTracked(r, small, W - 16 - cw - 20 - sw, hy, "SIDE", 2, true, true);
  r.displayBuffer();
}

void ButtonRemapActivity::render(RenderLock&&) {
  if (deck::listsActive()) {
    renderDeck();
    return;
  }
  const auto labelForHardware = [&](uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < kRoleCount; i++) {
      if (tempMapping[i] == hardwareIndex) {
        return getRoleName(i);
      }
    }
    return "-";
  };

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_REMAP_FRONT_BUTTONS));
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    tr(STR_REMAP_PROMPT));

  int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  renderUi();

  // Temporary warning banner for duplicates.
  if (!errorMessage.empty()) {
    GUI.drawHelpText(renderer,
                     Rect{0, pageHeight - metrics.buttonHintsHeight - metrics.contentSidePadding - 15, pageWidth, 20},
                     errorMessage.c_str());
  }

  // Provide side button actions at the bottom of the screen (split across two lines).
  GUI.drawHelpText(renderer,
                   Rect{0, topOffset + 4 * metrics.listRowHeight + 4 * metrics.verticalSpacing, pageWidth, 20},
                   tr(STR_REMAP_RESET_HINT));
  GUI.drawHelpText(renderer,
                   Rect{0, topOffset + 4 * metrics.listRowHeight + 5 * metrics.verticalSpacing + 20, pageWidth, 20},
                   tr(STR_REMAP_CANCEL_HINT));

  // Live preview of logical labels under front buttons.
  // This mirrors the on-device front button order: Back, Confirm, Left, Right.
  GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FRONT_HW_BACK),
                      labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM),
                      labelForHardware(CrossPointSettings::FRONT_HW_LEFT),
                      labelForHardware(CrossPointSettings::FRONT_HW_RIGHT));
  renderer.displayBuffer();
}

void ButtonRemapActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ButtonRemapActivity*>(user)->buildScreen(screen);
}

void ButtonRemapActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + topOffset),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width) + metrics.verticalSpacing),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.verticalSpacing),
                  static_cast<int16_t>(safe.x + metrics.verticalSpacing)});

  for (uint8_t i = 0; i < kRoleCount; ++i) {
    const uint8_t assignedButton = tempMapping[i];
    rowItems[i].value = assignedButton == kUnassigned ? tr(STR_UNASSIGNED) : getHardwareName(assignedButton);
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = kRoleCount;
  props.selectedIndex = currentStep < kRoleCount ? currentStep : -1;
  props.inputMask = fui::InputNone;
  props.scrollIndicator = false;
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  screen.list(props);
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  SETTINGS.frontButtonBack = tempMapping[0];
  SETTINGS.frontButtonConfirm = tempMapping[1];
  SETTINGS.frontButtonLeft = tempMapping[2];
  SETTINGS.frontButtonRight = tempMapping[3];
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) {
  switch (roleIndex) {
    case 0:
      return tr(STR_BACK);
    case 1:
      return tr(STR_CONFIRM);
    case 2:
      return tr(STR_DIR_LEFT);
    case 3:
    default:
      return tr(STR_DIR_RIGHT);
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    default:
      return "Unknown";
  }
}
