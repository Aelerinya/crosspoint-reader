#include "ThemeFonts.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#ifndef NO_THEME_FONTS
#include <builtinFonts/cyberdeck_ui10_bold.h>
#include <builtinFonts/cyberdeck_ui10_regular.h>
#include <builtinFonts/cyberdeck_ui12_bold.h>
#include <builtinFonts/cyberdeck_ui12_regular.h>
#include <builtinFonts/cyberdeck_ui8_regular.h>
#include <builtinFonts/exlibris_ui10_bold.h>
#include <builtinFonts/exlibris_ui10_regular.h>
#include <builtinFonts/exlibris_ui12_bold.h>
#include <builtinFonts/exlibris_ui12_regular.h>
#include <builtinFonts/exlibris_ui8_regular.h>
#include <builtinFonts/folio_ui10_bold.h>
#include <builtinFonts/lastcall_ui10_regular.h>
#include <builtinFonts/lastcall_ui12_regular.h>
#include <builtinFonts/lastcall_ui8_regular.h>
#include <builtinFonts/folio_ui10_regular.h>
#include <builtinFonts/folio_ui12_bold.h>
#include <builtinFonts/folio_ui12_regular.h>
#include <builtinFonts/folio_ui8_regular.h>
#include <builtinFonts/netrunner_ui10_regular.h>
#include <builtinFonts/netrunner_ui12_regular.h>
#include <builtinFonts/netrunner_ui8_regular.h>
#endif

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
FontSet cyberdeckFonts(&cyberdeck_ui10_regular, &cyberdeck_ui10_bold, &cyberdeck_ui12_regular, &cyberdeck_ui12_bold,
                       &cyberdeck_ui8_regular);
// Single-weight faces: bold slots reuse the regular data.
FontSet netrunnerFonts(&netrunner_ui10_regular, &netrunner_ui10_regular, &netrunner_ui12_regular, &netrunner_ui12_regular,
                       &netrunner_ui8_regular);
FontSet folioFonts(&folio_ui10_regular, &folio_ui10_bold, &folio_ui12_regular, &folio_ui12_bold, &folio_ui8_regular);
FontSet exlibrisFonts(&exlibris_ui10_regular, &exlibris_ui10_bold, &exlibris_ui12_regular, &exlibris_ui12_bold,
                      &exlibris_ui8_regular);

FontSet lastcallFonts(&lastcall_ui10_regular, &lastcall_ui10_regular, &lastcall_ui12_regular, &lastcall_ui12_regular,
                      &lastcall_ui8_regular);

#endif

const FontSet* fontsFor(const CrossPointSettings::UI_THEME theme) {
#ifdef NO_THEME_FONTS
  (void)theme;
  return nullptr;
#else
  switch (theme) {
    case CrossPointSettings::CYBERDECK:
      return &cyberdeckFonts;
    case CrossPointSettings::NETRUNNER:
      return &netrunnerFonts;
    case CrossPointSettings::FOLIO:
      return &folioFonts;
    case CrossPointSettings::EXLIBRIS:
      return &exlibrisFonts;
    case CrossPointSettings::LASTCALL:
      return &lastcallFonts;
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
  if (FontCacheManager* cache = renderer.getFontCacheManager()) {
    cache->clearCache();
  }
}
