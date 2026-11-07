#pragma once

#include "CrossPointSettings.h"

class GfxRenderer;

// Registers the theme's typeface under UI_10 / UI_12 / SMALL font IDs, or the
// default Ubuntu UI fonts for themes without their own.
void applyThemeUiFonts(GfxRenderer& renderer, CrossPointSettings::UI_THEME theme);
