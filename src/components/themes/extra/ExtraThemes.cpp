#include "ExtraThemes.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ================================================================ shared base

int ExtraThemeBase::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(UI_12_FONT_ID) + menuRowPadding();
}

int ExtraThemeBase::drawCoverInSlot(const GfxRenderer& renderer, const RecentBook& book, const Rect slot,
                                    const int thumbHeight) {
  if (book.coverBmpPath.empty()) return 0;
  const std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
  HalFile file;
  if (!Storage.openFileForRead("HOME", path, file)) return 0;
  int width = 0;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    width = std::min(bitmap.getWidth(), slot.width);
    drawCoverThumbFill(renderer, bitmap, Rect{slot.x + (slot.width - width) / 2, slot.y, width, slot.height}, 0, path);
  }
  file.close();
  return width;
}

void ExtraThemeBase::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                         const std::vector<RecentBook>& recentBooks, int, bool& coverRendered,
                                         bool& coverBufferStored, bool& bufferRestored,
                                         std::function<bool()> storeCoverBuffer) const {
  if (bufferRestored) return;
  if (coverRendered && coverBufferStored) return;
  drawHomeTile(renderer, rect, recentBooks.empty() ? nullptr : &recentBooks[0]);
  coverBufferStored = storeCoverBuffer();
  coverRendered = coverBufferStored;
}
