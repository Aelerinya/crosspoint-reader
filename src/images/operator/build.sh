#!/bin/bash
# Regenerates src/images/DeckOperator.h from the Operator art here (design/sim/assets/operator).
# Scenes are full-frame overlays drawn at a fixed spot; sizes follow design/SPEC.md (v2).
cd "$(dirname "$0")"
python3 ../../../scripts/convert_sprites.py ../DeckOperator.h \
  kOpHome=poses/home.png:x270 \
  kOpLibrary=scenes/library.png:480x800 \
  kOpMenu=scenes/menu.png:480x800 \
  kOpGoto=scenes/goto.png:480x800 \
  kOpClear=scenes/clear.png:452x753 \
  kOpOnWait=scenes/onwait.png:480x800 \
  kOpOnRecv=scenes/onrecv.png:446x744 \
  kOpUsb=scenes/usb.png:480x800 \
  kOpLowCard=scenes/low_card.png:400x447 \
  kOpSettings=scenes/settings.png:400x667 \
  kOpFlash=scenes/flash.png:480x800 \
  kOpSleep=scenes/sleep.png:432x720 \
  kOpWakeA=scenes/wakeA.png:480x800 \
  kOpWakeB=scenes/wakeB.png:480x800 \
  kOpWakeC=scenes/wakeC.png:480x800 \
  kOpOpen=poses/wakeA.png:x400 \
  kOpNet=poses/usb.png:x260
