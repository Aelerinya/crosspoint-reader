#pragma once

#include <HalStorage.h>

#include <cstdint>

class Print;
class ZipFile;

// Thumbnail tone: `histogram` (256 bins) collects the output grays of a dry
// run; `toneLut` remaps grays before dithering. Both null for normal output.
struct JpegThumbTone {
  uint32_t* histogram = nullptr;
  const uint8_t* toneLut = nullptr;
};

class JpegToBmpConverter {
  using Tone = JpegThumbTone;
  static bool jpegFileToBmpStreamInternal(HalFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool crop = true, bool originalThresholds = false,
                                          Tone tone = {});

 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true, bool originalThresholds = false);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Convert to 1-bit BMP (black and white only, no grays)
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                              int targetMaxHeight);
};
