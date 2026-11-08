#include "ThemeFonts.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#ifndef NO_THEME_FONTS
#include <builtinFonts/deck_display_13.h>
#include <builtinFonts/deck_display_22.h>
#include <builtinFonts/deck_display_31.h>
#include <builtinFonts/deck_display_46.h>
#include <builtinFonts/deck_display_76.h>
#include <builtinFonts/deck_ui10_bold.h>
#include <builtinFonts/deck_ui10_regular.h>
#include <builtinFonts/deck_ui12_bold.h>
#include <builtinFonts/deck_ui12_regular.h>
#include <builtinFonts/deck_ui8_regular.h>
#endif

#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"

// Default UI families, defined in main.cpp.
extern EpdFontFamily ui10FontFamily;
extern EpdFontFamily ui12FontFamily;
extern EpdFontFamily smallFontFamily;

namespace {

struct FontSet {
  EpdFont ui10Regular, ui10Bold, ui12Regular, ui12Bold, small;
  EpdFontFamily ui10, ui12, smallFamily;

  FontSet(const EpdFontData* r10, const EpdFontData* b10, const EpdFontData* r12, const EpdFontData* b12,
          const EpdFontData* s8)
      : ui10Regular(r10),
        ui10Bold(b10),
        ui12Regular(r12),
        ui12Bold(b12),
        small(s8),
        ui10(10, &ui10Regular, &ui10Bold),
        ui12(12, &ui12Regular, &ui12Bold),
        smallFamily(8, &small) {}
};

#ifndef NO_THEME_FONTS
FontSet deckFonts(&deck_ui10_regular, &deck_ui10_bold, &deck_ui12_regular, &deck_ui12_bold, &deck_ui8_regular);

// Deck display face, one weight per size. Registered for every theme so the
// Kitsune sleep screen can draw it.
EpdFont deckDisplay13(&deck_display_13), deckDisplay22(&deck_display_22), deckDisplay31(&deck_display_31),
    deckDisplay46(&deck_display_46), deckDisplay76(&deck_display_76);
EpdFontFamily deckDisplay13Family(13, &deckDisplay13), deckDisplay22Family(22, &deckDisplay22),
    deckDisplay31Family(31, &deckDisplay31), deckDisplay46Family(46, &deckDisplay46),
    deckDisplay76Family(76, &deckDisplay76);
#endif

const FontSet* fontsFor(const CrossPointSettings::UI_THEME theme) {
#ifdef NO_THEME_FONTS
  (void)theme;
  return nullptr;
#else
  switch (theme) {
    case CrossPointSettings::DECK:
      return &deckFonts;
    default:
      return nullptr;
  }
#endif
}

}  // namespace

void applyThemeUiFonts(GfxRenderer& renderer, const CrossPointSettings::UI_THEME theme) {
  const FontSet* set = fontsFor(theme);
  renderer.removeFont(UI_10_FONT_ID);
  renderer.removeFont(UI_12_FONT_ID);
  renderer.removeFont(SMALL_FONT_ID);
  renderer.insertFont(UI_10_FONT_ID, set ? set->ui10 : ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, set ? set->ui12 : ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, set ? set->smallFamily : smallFontFamily);
#ifndef NO_THEME_FONTS
  for (const int size : {13, 22, 31, 46, 76}) renderer.removeFont(deck::displayFont(size));
  renderer.insertFont(deck::displayFont(13), deckDisplay13Family);
  renderer.insertFont(deck::displayFont(22), deckDisplay22Family);
  renderer.insertFont(deck::displayFont(31), deckDisplay31Family);
  renderer.insertFont(deck::displayFont(46), deckDisplay46Family);
  renderer.insertFont(deck::displayFont(76), deckDisplay76Family);
#endif
  if (FontCacheManager* cache = renderer.getFontCacheManager()) {
    cache->clearCache();
  }
}
