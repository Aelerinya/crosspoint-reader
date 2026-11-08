#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Bookplate.h"
#include "MinizConfig.h"

namespace {

class MemoryInput : public BookplateInput {
 public:
  explicit MemoryInput(std::vector<uint8_t> bytes) : data(std::move(bytes)) {}
  uint32_t size() const override { return static_cast<uint32_t>(data.size()); }
  bool readAt(uint32_t offset, uint8_t* buf, size_t len) override {
    if (offset + len > data.size()) return false;
    memcpy(buf, data.data() + offset, len);
    return true;
  }
  std::vector<uint8_t> data;
};

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> drain(BookplateEpub& epub) {
  std::vector<uint8_t> out;
  uint8_t buf[1000];  // odd size: reads cross segment boundaries
  int n;
  while ((n = epub.read(buf, sizeof(buf))) > 0) out.insert(out.end(), buf, buf + n);
  EXPECT_EQ(n, 0);
  return out;
}

const char* OPF = R"(<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0">
  <manifest>
    <item id="cover" href="cover.xhtml" media-type="application/xhtml+xml"/>
    <item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="cover"/>
    <itemref idref="c1"/>
  </spine>
</package>
)";

BookplateContent sampleContent(bool image) {
  BookplateContent c;
  c.title = "Tom & Jerry <Vol. 1>";
  c.from = "Tail Light";
  c.date = "8 November 2026";
  if (image) {
    std::vector<uint8_t> rows(60 * 800, 0xFF);
    c.png = bookplate::encodePng1(rows.data(), 480, 800, 60);
  }
  return c;
}

}  // namespace

TEST(BookplateOpf, InsertsAfterCoverAndInManifest) {
  const std::string out = bookplate::patchOpf(OPF, true);
  ASSERT_FALSE(out.empty());
  EXPECT_NE(out.find("<item id=\"x4c-bookplate\" href=\"x4c-bookplate.xhtml\""), std::string::npos);
  EXPECT_NE(out.find("<item id=\"x4c-bookplate-img\" href=\"x4c-bookplate.png\" media-type=\"image/png\"/>"),
            std::string::npos);
  const size_t cover = out.find("<itemref idref=\"cover\"/>");
  const size_t plate = out.find("<itemref idref=\"x4c-bookplate\"/>");
  const size_t c1 = out.find("<itemref idref=\"c1\"/>");
  ASSERT_NE(plate, std::string::npos);
  EXPECT_LT(cover, plate);
  EXPECT_LT(plate, c1);
  EXPECT_LT(out.find("x4c-bookplate-img"), out.find("</manifest>"));
}

TEST(BookplateOpf, FirstWhenNoCoverPage) {
  std::string opf = OPF;
  opf.replace(opf.find("<itemref idref=\"cover\"/>"), 24, "");
  const std::string out = bookplate::patchOpf(opf, false);
  EXPECT_LT(out.find("idref=\"x4c-bookplate\""), out.find("idref=\"c1\""));
  EXPECT_EQ(out.find("x4c-bookplate-img"), std::string::npos);
}

TEST(BookplateOpf, ReplacesAnOldBookplate) {
  const std::string once = bookplate::patchOpf(OPF, true);
  const std::string twice = bookplate::patchOpf(once, false);
  EXPECT_EQ(twice, bookplate::patchOpf(OPF, false));
}

TEST(BookplateOpf, PrefixedOpf) {
  std::string opf =
      "<opf:package><opf:manifest><opf:item id=\"a\" href=\"a.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</opf:manifest><opf:spine><opf:itemref idref=\"a\"/></opf:spine></opf:package>";
  const std::string out = bookplate::patchOpf(opf, false);
  EXPECT_NE(out.find("<opf:item id=\"x4c-bookplate\""), std::string::npos);
  EXPECT_LT(out.find("<opf:itemref idref=\"x4c-bookplate\"/>"), out.find("<opf:itemref idref=\"a\"/>"));
}

TEST(BookplateOpf, RejectsOpfWithoutSpine) { EXPECT_TRUE(bookplate::patchOpf("<package/>", false).empty()); }

TEST(BookplateXhtml, EscapesAndHasFallbackText) {
  const std::string page = bookplate::xhtml(sampleContent(true));
  EXPECT_NE(page.find("Tom &amp; Jerry &lt;Vol. 1&gt;"), std::string::npos);
  EXPECT_NE(page.find("<img src=\"x4c-bookplate.png\""), std::string::npos);
  EXPECT_NE(page.find("Enjoy the book"), std::string::npos);
  EXPECT_NE(page.find("8 November 2026"), std::string::npos);
  EXPECT_EQ(bookplate::xhtml(sampleContent(false)).find("<img"), std::string::npos);
}

TEST(BookplatePng, ValidStreamAndCrc) {
  std::vector<uint8_t> rows(2 * 3, 0);
  rows[0] = 0xF0;  // first 4 pixels white
  const std::vector<uint8_t> png = bookplate::encodePng1(rows.data(), 20, 2, 3);
  ASSERT_GT(png.size(), 8u + 25 + 12);
  EXPECT_EQ(memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8), 0);
  // IDAT payload inflates to filter byte + 3 bytes per row; padding bits forced white.
  const size_t idat = 8 + 25;
  const uint32_t len = (png[idat] << 24) | (png[idat + 1] << 16) | (png[idat + 2] << 8) | png[idat + 3];
  ASSERT_EQ(memcmp(&png[idat + 4], "IDAT", 4), 0);
  size_t outLen = 0;
  void* raw = tinfl_decompress_mem_to_heap(&png[idat + 8], len, &outLen, TINFL_FLAG_PARSE_ZLIB_HEADER);
  ASSERT_NE(raw, nullptr);
  const std::vector<uint8_t> expect = {0, 0xF0, 0, 0x0F, 0, 0, 0, 0x0F};
  EXPECT_EQ(std::vector<uint8_t>(static_cast<uint8_t*>(raw), static_cast<uint8_t*>(raw) + outLen), expect);
  mz_free(raw);
  const uint32_t crc =
      (png[idat + 8 + len] << 24) | (png[idat + 9 + len] << 16) | (png[idat + 10 + len] << 8) | png[idat + 11 + len];
  EXPECT_EQ(crc, static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, &png[idat + 4], len + 4)));
}

class BookplateEpubFiles : public ::testing::TestWithParam<const char*> {};

TEST_P(BookplateEpubFiles, RewritesAndRewritesAgain) {
  MemoryInput original(readFile(std::string(TEST_EPUB_DIR) + "/" + GetParam()));
  ASSERT_GT(original.size(), 0u);

  BookplateEpub epub;
  std::string error;
  ASSERT_TRUE(epub.open(original, &error)) << error;
  EXPECT_GT(epub.memoryNeeded(0), epub.largestAllocation(0));
  ASSERT_TRUE(epub.prepare(sampleContent(true), &error)) << error;
  MemoryInput once(drain(epub));
  EXPECT_EQ(once.size(), epub.size());
  // Starts like the original (mimetype entry first, untouched).
  EXPECT_EQ(memcmp(once.data.data(), original.data.data(), 30), 0);

  // The output is itself a valid EPUB for the same code, and a second plate replaces the first.
  BookplateEpub again;
  ASSERT_TRUE(again.open(once, &error)) << error;
  ASSERT_TRUE(again.prepare(sampleContent(false), &error)) << error;
  MemoryInput twice(drain(again));
  EXPECT_EQ(twice.size(), again.size());
  EXPECT_LT(twice.size(), once.size());  // the PNG is gone

  // Dumped for the Python zip check (scripts in the xteink repo).
  if (const char* dir = getenv("BOOKPLATE_OUT_DIR")) {
    std::ofstream(std::string(dir) + "/" + GetParam(), std::ios::binary)
        .write(reinterpret_cast<const char*>(once.data.data()), once.data.size());
  }
}

INSTANTIATE_TEST_SUITE_P(Samples, BookplateEpubFiles,
                         ::testing::Values("test_jpeg_images.epub", "test_hidden.epub",
                                           "crosspoint-table-edge-cases.epub"));

TEST(BookplateEpub, RefusesNonZip) {
  MemoryInput junk(std::vector<uint8_t>(100, 'x'));
  BookplateEpub epub;
  std::string error;
  EXPECT_FALSE(epub.open(junk, &error));
  EXPECT_EQ(error, "not a zip");
  EXPECT_FALSE(epub.prepare(sampleContent(false), &error));
}

TEST(BookplateEpub, RefusesHugeCentralDirectory) {
  // End-of-central-directory record claiming 2 MB of central directory at offset 0.
  std::vector<uint8_t> zip(3u * 1024 * 1024, 0);
  uint8_t* e = zip.data() + zip.size() - 22;
  const uint8_t eocd[22] = {0x50, 0x4b, 0x05, 0x06, 0, 0, 0, 0, 0x10, 0, 0x10, 0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0};
  memcpy(e, eocd, sizeof(eocd));
  MemoryInput input(std::move(zip));
  BookplateEpub epub;
  std::string error;
  EXPECT_FALSE(epub.open(input, &error));
  EXPECT_EQ(error, "central directory too large");
}

TEST(BookplateTitle, DecodesEntitiesAndWhitespace) {
  EXPECT_EQ(bookplate::opfTitle("<metadata><dc:title id=\"t\">Tom &amp;\n  Jerry &#233;t&#xE9;</dc:title></metadata>"),
            "Tom & Jerry \xC3\xA9t\xC3\xA9");
  EXPECT_EQ(bookplate::opfTitle("<package/>"), "");
}

TEST(BookplateTitle, ReadsFromEpub) {
  MemoryInput input(readFile(std::string(TEST_EPUB_DIR) + "/test_jpeg_images.epub"));
  BookplateEpub epub;
  ASSERT_TRUE(epub.open(input));
  EXPECT_FALSE(epub.title().empty());
}

TEST(BookplateTitle, DropsInvalidCodePoints) {
  EXPECT_EQ(bookplate::opfTitle("<dc:title>A&#xD800;B&#x110000;</dc:title>"), "A&#xD800;B&#x110000;");
}

TEST(BookplateUtf8, DropsBrokenSequences) {
  EXPECT_EQ(bookplate::validUtf8("Ren\xC3\xA9e"), "Ren\xC3\xA9e");
  EXPECT_EQ(bookplate::validUtf8("Ren\xC3"), "Ren");  // cut mid-character
  EXPECT_EQ(bookplate::validUtf8("a\xED\xA0\x80"
                                 "b"),
            "ab");  // surrogate
  EXPECT_EQ(bookplate::validUtf8("a\xC0\xAF"
                                 "b"),
            "ab");  // overlong
  EXPECT_EQ(bookplate::validUtf8("a\x01"
                                 "b"),
            "ab");  // control
  EXPECT_NE(bookplate::xhtml({"T", "Ren\xC3", "", {}}).find("<b>Ren</b>"), std::string::npos);
}

TEST(BookplateOpf, CoverFromGuide) {
  // The guide names p0 the cover although its name does not say so.
  std::string opf = OPF;
  opf.replace(opf.find("href=\"cover.xhtml\""), 18, "href=\"p0.xhtml\"");
  opf.replace(opf.find("</package>"), 10,
              "<guide><reference type=\"cover\" href=\"p0.xhtml#top\"/></guide>\n</package>");
  const std::string out = bookplate::patchOpf(opf, false);
  EXPECT_LT(out.find("idref=\"cover\""), out.find("idref=\"x4c-bookplate\""));
}

TEST(BookplateOpf, GuidePointingAtImageKeepsNameRule) {
  std::string opf = OPF;
  opf.replace(opf.find("</package>"), 10,
              "<guide><reference type=\"cover\" href=\"Images/cover.jpg\"/></guide>\n</package>");
  const std::string out = bookplate::patchOpf(opf, false);
  EXPECT_LT(out.find("idref=\"cover\""), out.find("idref=\"x4c-bookplate\""));
}

TEST(BookplateOpf, CoverWordNotSubstring) {
  std::string opf = OPF;
  opf.replace(opf.find("id=\"cover\" href=\"cover.xhtml\""), 29, "id=\"disc\" href=\"discovery.xhtml\"");
  opf.replace(opf.find("idref=\"cover\""), 13, "idref=\"disc\"");
  const std::string out = bookplate::patchOpf(opf, false);
  EXPECT_LT(out.find("idref=\"x4c-bookplate\""), out.find("idref=\"disc\""));
}
