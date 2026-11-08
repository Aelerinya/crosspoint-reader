# Bookplate

Adds an "Ex libris" page to a book while Send to Reader uploads it to another
reader. The receiver may run stock Xteink firmware or upstream CrossPoint, so
everything travels inside the EPUB: no sidecar file and no receiver support.
Tested against the simulator and `fake-stock-reader.py`, not yet on real readers.

## What changes in the EPUB

- `x4c-bookplate.xhtml` (and `x4c-bookplate.png` for the image style) are added
  next to the OPF.
- The OPF lists them in the manifest and puts the page in the spine after a
  leading cover page, else first. The first `itemref` counts as the
  cover when the OPF's `<guide>` cover reference points to it, or when its id
  or href has the word `cover` or `titlepage` in it. The page is not added to
  the table of contents.
- An old bookplate (same ids and file names) is removed first, so re-sending a
  book keeps one bookplate: the latest sender's.

Everything else is copied byte for byte: each zip entry's local header, data
and data descriptor are streamed from the original file without inflating.
Only the OPF is rewritten (stored, not deflated: deflate is compiled out of
miniz, see `MinizConfig.h`) and the central directory is rebuilt with the new offsets. The output
size is known before the upload starts, as the upload needs it for
`Content-Length`.

Not handled (the book is then sent unchanged): ZIP64, an OPF or central
directory over 1 MB, a missing `container.xml`, an OPF without `<manifest>` or
`<spine>`. The caller also checks `memoryNeeded()` / `largestAllocation()`
against the free heap after `open()`, as the firmware has no exceptions.

## The page

- Image + text: the image (480x800 1-bit PNG, stored deflate, about 49 KB),
  then the same words as text for readers that hide images.
- Text only: the words alone.

The image is drawn on the sending reader by
`src/components/themes/extra/DeckBookplate.cpp` with the Deck fonts and the
Operator gift pose (`src/images/bookplate/`).

## Code

- `Bookplate.h/.cpp`: plain C++, no Arduino. `BookplateEpub::open()` reads the
  zip index and OPF, `prepare()` plans the rewrite, then `read()` streams it (it
  is a `BookSendSource`).
- Host tests: `test/bookplate` (gtest). `BOOKPLATE_OUT_DIR=dir` keeps the
  rewritten sample EPUBs for checking with other tools.
