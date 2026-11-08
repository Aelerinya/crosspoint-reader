#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <HttpHeader.h>
#include <I18n.h>

#include <memory>
#include <string>
#include <utility>

#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckList.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int IDX_USERNAME = 0;
constexpr int IDX_PASSWORD = 1;
constexpr int IDX_SERVER_URL = 2;
constexpr int IDX_SERVER_TYPE = 3;
constexpr int IDX_CUSTOM_HEADER_1 = 4;
constexpr int IDX_CUSTOM_HEADER_2 = 5;
constexpr int IDX_MATCH_METHOD = 6;
constexpr int IDX_SEND_METADATA = 7;
constexpr int IDX_SYNC_BEHAVIOR = 8;
constexpr int IDX_SYNC_CLIPPINGS = 9;
constexpr int IDX_SIGN_UP = 10;
constexpr int IDX_AUTHENTICATE = 11;

constexpr StrId menuNames[KOReaderSettingsActivity::MENU_ITEMS] = {
    StrId::STR_USERNAME,        StrId::STR_PASSWORD,        StrId::STR_SYNC_SERVER_URL,   StrId::STR_SERVER_TYPE,
    StrId::STR_CUSTOM_HEADER_1, StrId::STR_CUSTOM_HEADER_2, StrId::STR_DOCUMENT_MATCHING, StrId::STR_SEND_METADATA,
    StrId::STR_SYNC_BEHAVIOR,   StrId::STR_SYNC_CLIPPINGS,  StrId::STR_SIGN_UP,           StrId::STR_AUTHENTICATE};

constexpr int SERVER_TYPE_ITEMS = 3;
constexpr StrId serverTypeNames[SERVER_TYPE_ITEMS] = {StrId::STR_CROSSPOINT, StrId::STR_KOSYNC_SERVER,
                                                      StrId::STR_OTHER};
}  // namespace

KOReaderSettingsActivity::KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("KOReaderSettings", renderer, mappedInput) {
  // Labels never change (unlike the values, which track live KOREADER_STORE
  // state), so they're set once here rather than every buildScreen() call.
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int KOReaderSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* KOReaderSettingsActivity::headerTitle() const { return tr(STR_KOREADER_SYNC); }

bool KOReaderSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void KOReaderSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // Activation opens a keyboard/sub-activity or repaints a new value; a
  // lingering flash would gray an unrelated row.
  app.clearTapFlash();
  if (index == IDX_USERNAME) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   KOREADER_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               KOREADER_STORE.setCredentials(kb.text, KOREADER_STORE.getPassword());
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (index == IDX_PASSWORD) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                KOREADER_STORE.getPassword(), 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), kb.text);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (index == IDX_SERVER_URL) {
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               KOREADER_STORE.setServerUrl(urlToSave);
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (index == IDX_SERVER_TYPE) {
    const auto current = KOREADER_STORE.getServerType();
    optionPopup.show(StrId::STR_SERVER_TYPE, serverTypeNames, SERVER_TYPE_ITEMS, static_cast<int>(current),
                     [this](const int selected) {
                       if (selected == static_cast<int>(KOREADER_STORE.getServerType())) return;
                       KOREADER_STORE.setServerType(static_cast<KOReaderServerType>(selected));
                       KOREADER_STORE.saveToFile();
                     });
    requestUpdate();
  } else if (index == IDX_CUSTOM_HEADER_1 || index == IDX_CUSTOM_HEADER_2) {
    // Single "Name: Value" line, parsed on save.
    const size_t slot = static_cast<size_t>(index == IDX_CUSTOM_HEADER_1 ? 0 : 1);
    const StrId label = index == IDX_CUSTOM_HEADER_1 ? StrId::STR_CUSTOM_HEADER_1 : StrId::STR_CUSTOM_HEADER_2;
    const std::string prefill = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot]);
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(label), prefill, 160, InputType::Text),
        [this, slot](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            HttpHeader header;
            parseHttpHeaderLine(kb.text, header);
            KOREADER_STORE.setCustomHeader(slot, header.name, header.value);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (index == IDX_MATCH_METHOD) {
    const auto current = KOREADER_STORE.getMatchMethod();
    const auto newMethod =
        (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
    KOREADER_STORE.setMatchMethod(newMethod);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (index == IDX_SEND_METADATA) {
    KOREADER_STORE.setSendMetadata(!KOREADER_STORE.getSendMetadata());
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (index == IDX_SYNC_BEHAVIOR) {
    const auto current = KOREADER_STORE.getSyncBehavior();
    const auto newBehavior = (current == KOReaderSyncBehavior::ASK_EVERY_TIME) ? KOReaderSyncBehavior::SMART
                                                                               : KOReaderSyncBehavior::ASK_EVERY_TIME;
    KOREADER_STORE.setSyncBehavior(newBehavior);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (index == IDX_SYNC_CLIPPINGS) {
    KOREADER_STORE.setSyncClippings(!KOREADER_STORE.getSyncClippings());
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (index == IDX_SIGN_UP) {
    if (!KOREADER_STORE.hasCredentials()) return;
    startActivityForResult(
        std::make_unique<KOReaderAuthActivity>(renderer, mappedInput, KOReaderAuthActivity::Mode::SIGN_UP),
        [](const ActivityResult&) {});
  } else if (index == IDX_AUTHENTICATE) {
    if (!KOREADER_STORE.hasCredentials()) return;
    startActivityForResult(std::make_unique<KOReaderAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void KOReaderSettingsActivity::refreshRowValues() {
  // rowItems_'s labels/actionValue were set once in the constructor; only the
  // live value text needs refreshing here, by assigning into the existing
  // rowValues_ strings (no array growth) rather than building a new
  // items/values vector on every render.
  for (int i = 0; i < MENU_ITEMS; i++) {
    if (i == IDX_USERNAME) {
      const auto username = KOREADER_STORE.getUsername();
      rowValues_[i] = username.empty() ? tr(STR_NOT_SET) : username;
    } else if (i == IDX_PASSWORD) {
      rowValues_[i] = KOREADER_STORE.getPassword().empty() ? tr(STR_NOT_SET) : "******";
    } else if (i == IDX_SERVER_URL) {
      rowValues_[i] = KOREADER_STORE.getServerUrl();
      if (rowValues_[i].empty()) {
        std::string defaultUrl = KOREADER_STORE.getBaseUrl();
        const auto schemeEnd = defaultUrl.find("://");
        if (schemeEnd != std::string::npos) defaultUrl.erase(0, schemeEnd + 3);
        rowValues_[i] = std::string(tr(STR_DEFAULT_VALUE)) + ": " + defaultUrl;
      }
    } else if (i == IDX_SERVER_TYPE) {
      const auto type = KOREADER_STORE.getServerType();
      rowValues_[i] = type == KOReaderServerType::CROSSPOINT ? tr(STR_CROSSPOINT)
                      : type == KOReaderServerType::KOSYNC   ? tr(STR_KOSYNC_SERVER)
                                                             : tr(STR_OTHER);
    } else if (i == IDX_CUSTOM_HEADER_1 || i == IDX_CUSTOM_HEADER_2) {
      const size_t slot = static_cast<size_t>(i == IDX_CUSTOM_HEADER_1 ? 0 : 1);
      rowValues_[i] = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot], true);
      if (rowValues_[i].empty()) rowValues_[i] = tr(STR_NOT_SET);
    } else if (i == IDX_MATCH_METHOD) {
      rowValues_[i] =
          KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? tr(STR_FILENAME) : tr(STR_BINARY);
    } else if (i == IDX_SEND_METADATA || i == IDX_SYNC_CLIPPINGS) {
      rowValues_[i].clear();
    } else if (i == IDX_SYNC_BEHAVIOR) {
      rowValues_[i] =
          KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART ? tr(STR_SMART_SYNC) : tr(STR_ASK_EVERY_TIME);
    } else {
      rowValues_[i] = KOREADER_STORE.hasCredentials() ? "" : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
    }
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }
  GUI.setCheckboxRow(rowItems_[IDX_SEND_METADATA], KOREADER_STORE.getSendMetadata());
  GUI.setCheckboxRow(rowItems_[IDX_SYNC_CLIPPINGS], KOREADER_STORE.getSyncClippings());
}

void KOReaderSettingsActivity::drawChrome() {
  if (!deck::listsActive()) {
    UiListActivity::drawChrome();
    return;
  }
  refreshRowValues();
  const GfxRenderer& r = renderer;
  const int W = r.getScreenWidth();
  const int small = SMALL_FONT_ID;
  deck::drawSmallHeader(r, "SYNC");
  deck::drawBigTitle(r, 53, "SYNC", "同期");
  const int sel = nav.selected;
  struct Group {
    const char* title;
    int first, count;
  };
  static constexpr Group kGroups[] = {
      {"// ACCOUNT", IDX_USERNAME, 2}, {"// SERVER", IDX_SERVER_URL, 4}, {"// BEHAVIOUR", IDX_MATCH_METHOD, 4}};
  int y = 112;
  deck::ListSpec spec;
  spec.style = deck::ListStyle::Rows;
  for (const auto& g : kGroups) {
    deck::drawTracked(r, small, 16, y, g.title, 2, true, true);
    r.fillRect(16, y + r.getLineHeight(small) + 3, W - 32, 2, true);
    y += r.getLineHeight(small) + 6;
    int top = 0;
    deck::drawList(r, Rect{16, y, W - 32, g.count * 41}, spec, g.count, sel - g.first, top,
                   [&](const int i, deck::Row& row) {
                     const auto& item = rowItems_[g.first + i];
                     row.label = item.label ? item.label : "";
                     row.right = item.value ? item.value : "";
                     row.hasToggle = item.toggle;
                     row.toggleOn = item.toggleChecked;
                     row.kind = deck::RowKind::Enum;
                     return true;
                   });
    y += g.count * 41 + 8;
  }
  // SIGN UP / AUTHENTICATE: outlined buttons, dashed until credentials exist.
  const bool creds = KOREADER_STORE.hasCredentials();
  const int bw = (W - 32 - 10) / 2, bh = 38;
  y += 6;
  for (int i = 0; i < 2; ++i) {
    const int idx = i == 0 ? IDX_SIGN_UP : IDX_AUTHENTICATE;
    const int bx = 16 + i * (bw + 10);
    const bool on = sel == idx;
    if (on) {
      deck::fillChamfer(r, bx, y, bw, bh, 10, true);
    } else if (creds) {
      deck::drawChamfer(r, bx, y, bw, bh, 10, 2);
    } else {
      for (int x = bx; x < bx + bw; x += 6) {
        r.fillRect(x, y, 3, 1, true);
        r.fillRect(x, y + bh - 1, 3, 1, true);
      }
      for (int yy = y; yy < y + bh; yy += 6) {
        r.fillRect(bx, yy, 1, 3, true);
        r.fillRect(bx + bw - 1, yy, 1, 3, true);
      }
    }
    const std::string label = deck::upper(rowItems_[idx].label ? rowItems_[idx].label : "");
    const int lw = deck::trackedWidth(r, UI_10_FONT_ID, label.c_str(), 2, true);
    deck::drawTracked(r, UI_10_FONT_ID, bx + (bw - lw) / 2, y + (bh - r.getLineHeight(UI_10_FONT_ID)) / 2,
                      label.c_str(), 2, !on, true);
  }
  if (!creds) {
    deck::drawTracked(r, small, 16, y + bh + 14, deck::upper(tr(STR_SET_CREDENTIALS_FIRST)).c_str(), 2, true, true);
  }
}

void KOReaderSettingsActivity::drawFooter() {
  if (!deck::listsActive()) {
    UiListActivity::drawFooter();
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "EDIT", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void KOReaderSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  refreshRowValues();
  if (deck::listsActive()) return;  // drawChrome lays the rows out in groups

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
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

void KOReaderSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
