#!/bin/bash
# UI font sets for the extra themes (Cyberdeck, Netrunner, Folio, Ex Libris).
# Each theme swaps these into the UI_10 / UI_12 / SMALL font slots, so every
# screen (lists, headers, popups) uses the theme's face. Missing glyphs come
# from Ubuntu / Noto Sans (Latin ext, Greek, Cyrillic) and DejaVu (symbols).
#
# Usage: convert-theme-fonts.sh [theme...]   (default: all)
set -e
cd "$(dirname "$0")"
PY=${PYTHON:-python}
SRC=../builtinFonts/source
OUT=../builtinFonts

DEJAVU=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
DEJAVU_BOLD=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf

# Decorative glyphs the theme code draws.
EXTRA=(
  --additional-intervals 0x2042,0x2042   # asterism
  --additional-intervals 0x2766,0x2767   # fleurons
)

# name  regular-file  bold-file  ui10-size  ui12-size  small-size
THEMES=(
  "cyberdeck JetBrainsMono/JetBrainsMono-Regular.ttf JetBrainsMono/JetBrainsMono-Bold.ttf 10 12 8"
  "netrunner ShareTechMono/ShareTechMono-Regular.ttf ShareTechMono/ShareTechMono-Regular.ttf 11 13 9"
  "folio EBGaramond/EBGaramond12-Regular.ttf EBGaramond/EBGaramond12-Bold.ttf 12 14 10"
  "exlibris Libertine/LinLibertine_R.otf Libertine/LinLibertine_RB.otf 11 13 9"
  "lastcall VT323/VT323-Regular.ttf VT323/VT323-Regular.ttf 16 20 14"
)

want="$*"
for row in "${THEMES[@]}"; do
  read -r name reg bold s10 s12 s8 <<<"$row"
  [[ -n "$want" && " $want " != *" $name "* ]] && continue
  for slot in "10 regular $s10" "10 bold $s10" "12 regular $s12" "12 bold $s12" "8 regular $s8"; do
    read -r ui style size <<<"$slot"
    [[ $style == bold && $bold == "$reg" ]] && continue  # no bold face: ThemeFonts uses regular
    if [[ $style == bold ]]; then face=$SRC/$bold; ub=Bold; dv=$DEJAVU_BOLD; else face=$SRC/$reg; ub=Regular; dv=$DEJAVU; fi
    font_name="${name}_ui${ui}_${style}"
    FONTCONVERT_LEAN=1 $PY fontconvert.py "$font_name" "$size" "$face" \
      $SRC/Ubuntu/Ubuntu-$ub.ttf $SRC/NotoSans/NotoSans-$ub.ttf "$dv" \
      "${EXTRA[@]}" --2bit --compress > "$OUT/$font_name.h"
    echo "Generated $OUT/$font_name.h"
  done
done
