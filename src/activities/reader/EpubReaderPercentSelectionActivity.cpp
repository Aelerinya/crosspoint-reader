#include "EpubReaderPercentSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/Sprite.h"
#include "components/UITheme.h"
#include "components/UiSliderDialog.h"
#include "components/themes/extra/DeckList.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"
#include "images/DeckOperator.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_SLIDER = 1;
constexpr fui::ActionId ACTION_STEP = 2;
constexpr fui::ActionId ACTION_OK = 4;
constexpr fui::ActionId ACTION_CHROME = 5;  // absorbs taps on the dialog body; no handler
// Fine/coarse step sizes for percent adjustments (buttons and -/+ tap zones).
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
}  // namespace

EpubReaderPercentSelectionActivity::EpubReaderPercentSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const int initialPercent)
    : Activity("EpubReaderPercentSelection", renderer, mappedInput),
      UiAppHost(renderer),
      percent(initialPercent),
      nowPercent(initialPercent) {}

void EpubReaderPercentSelectionActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_SLIDER, &EpubReaderPercentSelectionActivity::onSliderEvent, this);
  app.on(ACTION_STEP, &EpubReaderPercentSelectionActivity::onStepEvent, this);
  app.on(ACTION_OK, &EpubReaderPercentSelectionActivity::onOkEvent, this);
  app.setScreen(&EpubReaderPercentSelectionActivity::percentScreen, this);
  // Set up rendering task and mark first frame dirty.
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderPercentSelectionActivity::adjustPercent(const int delta) {
  // Wrap using a 100-value ring (0% and 100% are the same wrap point), but keep 100 as the
  // natural landing value when reached without crossing the boundary (e.g. 90 + 10 = 100).
  const int raw = percent + delta;
  if (raw > 0 && raw % 100 == 0) {
    percent = 100;
  } else {
    percent = ((raw % 100) + 100) % 100;
  }
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::setPercent(const int value) {
  const int clamped = std::clamp(value, 0, 100);
  if (clamped == percent) return;
  percent = clamped;
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<EpubReaderPercentSelectionActivity*>(user);
  if (event.dragPermille < 0) return;
  self->setPercent((static_cast<int>(event.dragPermille) * 100 + 500) / 1000);
}

void EpubReaderPercentSelectionActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<EpubReaderPercentSelectionActivity*>(user)->adjustPercent(event.value * kSmallStep);
}

void EpubReaderPercentSelectionActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<EpubReaderPercentSelectionActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->confirm();
}

void EpubReaderPercentSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderPercentSelectionActivity::confirm() {
  setResult(PercentResult{percent});
  finish();
}

void EpubReaderPercentSelectionActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the slider and -/+ hit
  // rects; the slider follows the finger via InputDrag (dragPermille per held frame).
  // Runs before the Back handler because the release of a drag can also register as a
  // swipe (e.g. the left-edge rightward back gesture) — the drag must consume it so it
  // can't cancel the dialog or step the percent.
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.dragPermille >= 0) draggingSlider = true;
    return;
  }
  if (routingReady() && draggingSlider) {
    // Drag ended (possibly off the slider): swallow the tap/swipe events it produced.
    if (!route.snap.touchHeld) draggingSlider = false;
    return;
  }
  // Tap released outside the dialog (inside-taps are absorbed by the chrome
  // guard): cancel. Swipe-end releases arrive with -1,-1 coords and fall
  // through — same rule as OptionPopup.
  if (route.routed && route.snap.touchReleased && route.snap.touchX >= 0) {
    cancel();
    return;
  }

  // Back cancels, confirm selects, arrows adjust the percent.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Right) {
    adjustPercent(kLargeStep);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Left) {
    adjustPercent(-kLargeStep);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustPercent(-kSmallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustPercent(kSmallStep); });

  // On edge-button boards (X3, X4 Pro) the side buttons sit on the left/right edges of the screen rather
  // than as a vertical up/down rocker (X4), so BTN_UP is physically the left button and BTN_DOWN the right
  // one. Flip the large-step direction there so the left button decreases and the right button increases.
  const int upDelta = gpio.hasEdgeSideButtons() ? -kLargeStep : kLargeStep;
  const int downDelta = gpio.hasEdgeSideButtons() ? kLargeStep : -kLargeStep;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] { adjustPercent(upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, downDelta] { adjustPercent(downDelta); });
}

void EpubReaderPercentSelectionActivity::percentScreen(UiScreen& screen, void* user) {
  static_cast<EpubReaderPercentSelectionActivity*>(user)->buildPercentScreen(screen);
}

void EpubReaderPercentSelectionActivity::buildPercentScreen(UiScreen& screen) {
  char readout[16];
  snprintf(readout, sizeof(readout), "%d%%", percent);
  char hint1[64];
  snprintf(hint1, sizeof(hint1), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_FRONT), kSmallStep);
  char hint2[64];
  snprintf(hint2, sizeof(hint2), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_SIDE), kLargeStep);

  UiSliderDialogSpec spec;
  spec.title = tr(STR_GO_TO_PERCENT);
  spec.readout = readout;
  spec.value = percent;
  spec.max = 100;
  spec.minLabel = "0%";
  spec.maxLabel = "100%";
  spec.sliderAction = ACTION_SLIDER;
  spec.stepAction = ACTION_STEP;
  spec.okAction = ACTION_OK;
  spec.chromeAction = ACTION_CHROME;
  spec.hintLine1 = hint1;
  spec.hintLine2 = hint2;
  buildSliderDialogScreen(screen, uiTarget, mappedInput, spec);
}

void EpubReaderPercentSelectionActivity::renderDeck() {
  // Deck v2: caption, the percent huge, 20 segments, the Operator on a tightrope,
  // and the chapters around the landing point.
  const int W = renderer.getScreenWidth();
  renderer.clearScreen();
  deck::drawSmallHeader(renderer, "GO TO");
  char caption[64];
  snprintf(caption, sizeof(caption), "%% THROUGH THE WHOLE EPUB \xc2\xb7 NOW %d%%", nowPercent);
  deck::drawTracked(renderer, UI_10_FONT_ID, 20, 50, caption, 1, true, true);
  char num[8];
  snprintf(num, sizeof(num), "%d%%", percent);
  const int big = deck::displayFont(76);
  const int nw = renderer.getTextWidth(big, num);
  renderer.drawText(big, (W - nw) / 2, 104 - deck::displayCapOffset(renderer, 76), num);
  deck::segments(renderer, 30, 290, W - 60, 16, 20, (percent + 2) / 5);
  renderer.fillRect(16, 380, 8, 230, true);
  if (deck::companionOn()) drawSprite(renderer, kOpGoto, 0, 0);
  if (landsIn) {
    int y = 630;
    renderer.fillRect(16, y, W - 32, 2, true);
    y += 2;
    const int font = deck::displayFont(13);
    const int cap = 13 * 150 * 70 / 7200;
    const int rowH = 34;
    for (int off = -1; off <= 1; off++) {
      const std::string t = landsIn(percent, off);
      const bool cur = off == 0;
      if (cur) renderer.fillRect(16, y, W - 32, rowH, true);
      std::string kick, name = t;
      const size_t colon = t.find(": ");
      if (colon != std::string::npos && colon < 24) {
        kick = t.substr(0, colon);
        name = t.substr(colon + 2);
      }
      const int cy = y + rowH / 2;
      if (cur) {
        const int xs[] = {26, 33, 26};
        const int ys[] = {cy - 5, cy, cy + 5};
        renderer.fillPolygon(xs, ys, 3, false);
      }
      if (!kick.empty()) {
        deck::drawTracked(
            renderer, SMALL_FONT_ID, 48, cy - renderer.getLineHeight(SMALL_FONT_ID) / 2,
            renderer.truncatedText(SMALL_FONT_ID, deck::upper(kick).c_str(), 100, EpdFontFamily::BOLD).c_str(), 1, !cur,
            true);
      }
      renderer.drawText(font, 48 + 110, cy - cap / 2 - deck::displayCapOffset(renderer, 13),
                        renderer.truncatedText(font, deck::upper(name).c_str(), W - 16 - 10 - 158).c_str(), !cur);
      y += rowH;
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "GO", "-1", "+1");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void EpubReaderPercentSelectionActivity::render(RenderLock&&) {
  if (deck::listsActive()) {
    renderDeck();
    return;
  }
  // No clearScreen: the dialog is a popup — it dims the frame it opened over
  // and draws the card on top. Everything renders through the app so the
  // slider, -/+ zones, and Cancel/OK register touch hit rects.
  renderUi();

  // Button hints follow the current front button layout.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
