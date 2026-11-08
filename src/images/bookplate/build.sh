#!/bin/bash
# Regenerates src/images/BookplateArt.h: the Operator offering a book (design/sim/assets/operator/poses/gift.png)
# and the 蔵書票 tag (kana.png, Noto Sans CJK JP Bold 12 px; the Deck kana font has no 蔵 or 票).
cd "$(dirname "$0")"
python3 ../../../scripts/convert_sprites.py ../BookplateArt.h \
  kPlateGift=gift.png:x357 \
  kPlateKana=kana.png:38x16
