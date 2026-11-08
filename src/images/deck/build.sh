#!/bin/bash
# Regenerates src/images/DeckFox.h from the fox PNGs here (design/fox in the handoff).
cd "$(dirname "$0")"
python3 ../../../scripts/convert_sprites.py ../DeckFox.h \
  kFoxHead=fox.head.png:x22 \
  kFoxIdle=fox.idle.png:x150 \
  kFoxRun=fox.run.png:432x \
  kFoxChapter=fox.chapter.png:x360 \
  kFoxRead=fox.read.png:x300 \
  kFoxReadSmall=fox.read.png:x230 \
  kFoxNew=fox.new.png:x270 \
  kFoxWake=fox.wake.png:432x \
  kFoxSleep=fox.sleep.png:430x \
  kFoxTired=fox.tired.png:396x \
  kFoxProne=fox.prone.png:300x
