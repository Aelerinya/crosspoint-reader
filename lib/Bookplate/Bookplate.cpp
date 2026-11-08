#include "Bookplate.h"

#include <InflateStream.h>
#include <MinizConfig.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t SIG_LOCAL = 0x04034b50;
constexpr uint32_t SIG_CENTRAL = 0x02014b50;
constexpr uint32_t SIG_EOCD = 0x06054b50;
constexpr size_t LOCAL_FIXED = 30;
constexpr size_t CENTRAL_FIXED = 46;
constexpr size_t EOCD_FIXED = 22;
// Limits on sizes read from the zip, so a damaged book falls back to an
// unchanged send instead of a huge allocation (the firmware has no exceptions).
// Real OPFs and central directories are tens of KB.
constexpr uint32_t MAX_XML_SIZE = 1024u * 1024;
constexpr uint32_t MAX_CD_SIZE = 1024u * 1024;
// First tail read for the end-of-central-directory record; widened to the
// largest zip comment only when the record is not in it.
constexpr uint32_t SHORT_TAIL = 1024;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
void put16(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  put16(v, x & 0xFFFF);
  put16(v, x >> 16);
}
void set32(uint8_t* p, uint32_t x) {
  p[0] = x & 0xFF;
  p[1] = (x >> 8) & 0xFF;
  p[2] = (x >> 16) & 0xFF;
  p[3] = (x >> 24) & 0xFF;
}
void putBE32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back(x & 0xFF);
}

struct Entry {
  std::string name;
  uint16_t flags, method, time, date;
  uint32_t crc, csize, usize, localOffset;
  uint32_t centralOffset;  // offset of the raw record inside the central dir copy
  uint32_t centralLength;
  uint32_t spanEnd = 0;  // end of this entry's bytes in the input (next entry or central dir)
  enum Role : uint8_t { Keep, Opf, Drop } role = Keep;
  uint32_t newOffset = 0;
};

bool fail(std::string* error, const char* why) {
  if (error) *error = why;
  return false;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Reads one entry's uncompressed bytes (stored or deflated).
bool readEntry(BookplateInput& in, const Entry& e, std::string& out) {
  if (e.usize > MAX_XML_SIZE || e.csize > MAX_XML_SIZE) return false;
  uint8_t lh[LOCAL_FIXED];
  if (!in.readAt(e.localOffset, lh, sizeof(lh)) || le32(lh) != SIG_LOCAL) return false;
  const uint32_t dataOffset = e.localOffset + LOCAL_FIXED + le16(lh + 26) + le16(lh + 28);
  std::vector<uint8_t> raw(e.csize);
  if (e.csize && !in.readAt(dataOffset, raw.data(), raw.size())) return false;
  if (e.method == 0) {
    out.assign(raw.begin(), raw.end());
    return true;
  }
  if (e.method != 8) return false;
  // One-shot inflate into a buffer of the declared size: state on the heap, output bounded.
  InflateStream inflater;
  if (!inflater.init(false)) return false;
  inflater.setSource(raw.data(), raw.size());
  out.assign(e.usize, '\0');
  return e.usize == 0 || inflater.read(reinterpret_cast<uint8_t*>(&out[0]), e.usize);
}

// Finds "<tag" or "<prefix:tag" (or "</..." when closing) followed by a
// delimiter, starting at `from`. Returns the index of '<' or npos.
size_t findTag(const std::string& s, const char* tag, size_t from, bool closing = false) {
  const size_t tagLen = strlen(tag);
  for (size_t i = s.find('<', from); i != std::string::npos; i = s.find('<', i + 1)) {
    size_t j = i + 1;
    if (closing) {
      if (j >= s.size() || s[j] != '/') continue;
      j++;
    }
    size_t nameEnd = j;
    while (nameEnd < s.size() && (std::isalnum(static_cast<unsigned char>(s[nameEnd])) || s[nameEnd] == ':' ||
                                  s[nameEnd] == '-' || s[nameEnd] == '_')) {
      nameEnd++;
    }
    const std::string name = s.substr(j, nameEnd - j);
    const size_t colon = name.rfind(':');
    const std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
    if (local.size() == tagLen && local == tag) return i;
  }
  return std::string::npos;
}

// Value of attribute `attr` in the tag starting at `tagStart` ("" if absent).
std::string attrValue(const std::string& s, size_t tagStart, const char* attr) {
  const size_t end = s.find('>', tagStart);
  if (end == std::string::npos) return {};
  const std::string tag = s.substr(tagStart, end - tagStart);
  const std::string key = attr;
  for (size_t p = tag.find(key); p != std::string::npos; p = tag.find(key, p + 1)) {
    if (p == 0 || !std::isspace(static_cast<unsigned char>(tag[p - 1]))) continue;
    size_t q = p + key.size();
    while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) q++;
    if (q >= tag.size() || tag[q] != '=') continue;
    q++;
    while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) q++;
    if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) continue;
    const char quote = tag[q];
    const size_t close = tag.find(quote, q + 1);
    if (close == std::string::npos) return {};
    return tag.substr(q + 1, close - q - 1);
  }
  return {};
}

// End of the element starting at `tagStart`: after "/>" or after its closing tag.
size_t elementEnd(const std::string& s, size_t tagStart, const char* tag) {
  const size_t gt = s.find('>', tagStart);
  if (gt == std::string::npos) return std::string::npos;
  if (s[gt - 1] == '/') return gt + 1;
  const size_t close = findTag(s, tag, gt, true);
  if (close == std::string::npos) return gt + 1;
  const size_t closeGt = s.find('>', close);
  return closeGt == std::string::npos ? std::string::npos : closeGt + 1;
}

void removeElements(std::string& s, const char* tag, const char* attr, const char* value) {
  size_t pos = 0;
  while ((pos = findTag(s, tag, pos)) != std::string::npos) {
    if (attrValue(s, pos, attr) == value) {
      size_t end = elementEnd(s, pos, tag);
      if (end == std::string::npos) return;
      // Take the line's indentation and newline with it.
      size_t start = pos;
      while (start > 0 && (s[start - 1] == ' ' || s[start - 1] == '\t')) start--;
      if (end < s.size() && s[end] == '\n') end++;
      s.erase(start, end - start);
      pos = start;
    } else {
      pos++;
    }
  }
}

// "opf:" when the tag at `pos` is prefixed, else "".
std::string prefixAt(const std::string& s, size_t pos, bool closing) {
  const size_t nameStart = pos + (closing ? 2 : 1);
  size_t nameEnd = nameStart;
  while (nameEnd < s.size() && s[nameEnd] != '>' && s[nameEnd] != '/' &&
         !std::isspace(static_cast<unsigned char>(s[nameEnd]))) {
    nameEnd++;
  }
  const std::string name = s.substr(nameStart, nameEnd - nameStart);
  const size_t colon = name.rfind(':');
  return colon == std::string::npos ? std::string() : name.substr(0, colon + 1);
}

// `word` in `text` at the start or after a non-letter ("Text/Cover.xhtml", not "discovery").
bool hasWord(const std::string& text, const char* word) {
  for (size_t p = text.find(word); p != std::string::npos; p = text.find(word, p + 1)) {
    if (p == 0 || !std::isalpha(static_cast<unsigned char>(text[p - 1]))) return true;
  }
  return false;
}

// Is the spine item `idref` a cover page? Yes when the guide's cover reference
// points at it, or when its id or href names it a cover or title page.
bool looksLikeCover(const std::string& s, const std::string& idref) {
  std::string href;
  for (size_t pos = 0; (pos = findTag(s, "item", pos)) != std::string::npos; pos++) {
    if (attrValue(s, pos, "id") == idref) {
      href = attrValue(s, pos, "href");
      break;
    }
  }
  for (size_t pos = 0; (pos = findTag(s, "reference", pos)) != std::string::npos; pos++) {
    if (lower(attrValue(s, pos, "type")) != "cover") continue;
    std::string guideHref = attrValue(s, pos, "href");
    guideHref = guideHref.substr(0, guideHref.find('#'));
    if (!href.empty() && guideHref == href) return true;
    break;  // it may name the cover image instead: fall back to the name
  }
  const std::string text = lower(idref + " " + href);
  return hasWord(text, "cover") || hasWord(text, "titlepage");
}

struct ZipIndex {              // BookplateEpub::Index derives from it
  std::vector<uint8_t> cd;     // raw central directory
  std::vector<Entry> entries;  // in central directory order
  std::vector<size_t> order;   // entry indexes in file order
  uint32_t cdOffset = 0;
  std::string opfPath;
  std::string opf;
  size_t opfIndex = 0;
};

// Reads the central directory and the OPF.
bool loadZip(BookplateInput& in, ZipIndex& z, std::string* error) {
  // End of central directory: last 22 bytes, or earlier when there is a comment.
  const uint32_t fileSize = in.size();
  if (fileSize < EOCD_FIXED) return fail(error, "not a zip");
  std::vector<uint8_t> tail;
  size_t eocd = std::string::npos;
  for (const uint32_t want : {SHORT_TAIL, static_cast<uint32_t>(EOCD_FIXED + 0xFFFF)}) {
    const uint32_t tailLen = std::min<uint32_t>(fileSize, want);
    tail.resize(tailLen);
    if (!in.readAt(fileSize - tailLen, tail.data(), tailLen)) return fail(error, "read failed");
    for (size_t i = tailLen - EOCD_FIXED + 1; i-- > 0;) {
      if (le32(&tail[i]) == SIG_EOCD) {
        eocd = i;
        break;
      }
    }
    if (eocd != std::string::npos || tailLen == fileSize) break;
  }
  if (eocd == std::string::npos) return fail(error, "not a zip");
  const uint16_t count = le16(&tail[eocd + 10]);
  const uint32_t cdSize = le32(&tail[eocd + 12]);
  const uint32_t cdOffset = le32(&tail[eocd + 16]);
  if (count == 0xFFFF || cdOffset == 0xFFFFFFFF || cdSize == 0xFFFFFFFF) return fail(error, "zip64");
  if (static_cast<uint64_t>(cdOffset) + cdSize > fileSize) return fail(error, "bad central directory");
  if (cdSize > MAX_CD_SIZE || count > cdSize / CENTRAL_FIXED) return fail(error, "central directory too large");

  z.cdOffset = cdOffset;
  std::vector<uint8_t>& cd = z.cd;
  cd.resize(cdSize);
  if (cdSize && !in.readAt(cdOffset, cd.data(), cdSize)) return fail(error, "read failed");
  std::vector<Entry>& entries = z.entries;
  entries.reserve(count);
  for (size_t p = 0; p < cd.size();) {
    if (p + CENTRAL_FIXED > cd.size() || le32(&cd[p]) != SIG_CENTRAL) return fail(error, "bad central directory");
    Entry e;
    e.flags = le16(&cd[p + 8]);
    e.method = le16(&cd[p + 10]);
    e.time = le16(&cd[p + 12]);
    e.date = le16(&cd[p + 14]);
    e.crc = le32(&cd[p + 16]);
    e.csize = le32(&cd[p + 20]);
    e.usize = le32(&cd[p + 24]);
    const uint16_t nameLen = le16(&cd[p + 28]);
    const size_t len = CENTRAL_FIXED + nameLen + le16(&cd[p + 30]) + le16(&cd[p + 32]);
    if (p + len > cd.size()) return fail(error, "bad central directory");
    e.localOffset = le32(&cd[p + 42]);
    if (e.localOffset >= cdOffset || e.csize == 0xFFFFFFFF) return fail(error, "zip64");
    e.name.assign(reinterpret_cast<const char*>(&cd[p + CENTRAL_FIXED]), nameLen);
    e.centralOffset = static_cast<uint32_t>(p);
    e.centralLength = static_cast<uint32_t>(len);
    entries.push_back(std::move(e));
    p += len;
  }
  if (entries.empty()) return fail(error, "empty zip");

  // Each entry's bytes run to the next entry's local header (data descriptors included).
  std::vector<size_t>& order = z.order;
  order.resize(entries.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return entries[a].localOffset < entries[b].localOffset; });
  for (size_t k = 0; k < order.size(); k++) {
    entries[order[k]].spanEnd = k + 1 < order.size() ? entries[order[k + 1]].localOffset : cdOffset;
  }

  // container.xml -> OPF path.
  const auto findEntry = [&](const std::string& name) -> Entry* {
    for (auto& e : entries) {
      if (e.name == name) return &e;
    }
    return nullptr;
  };
  Entry* container = findEntry("META-INF/container.xml");
  std::string& xml = z.opf;
  if (!container || !readEntry(in, *container, xml)) return fail(error, "no container.xml");
  const size_t rootfile = findTag(xml, "rootfile", 0);
  z.opfPath = rootfile == std::string::npos ? std::string() : attrValue(xml, rootfile, "full-path");
  Entry* opfEntry = z.opfPath.empty() ? nullptr : findEntry(z.opfPath);
  if (!opfEntry || !readEntry(in, *opfEntry, xml)) return fail(error, "no OPF");
  z.opfIndex = static_cast<size_t>(opfEntry - entries.data());
  return true;
}
}  // namespace

namespace bookplate {

std::string validUtf8(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    bool ok = n > 0 && i + n <= text.size();
    uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (size_t k = 1; ok && k < n; k++) {
      const auto cc = static_cast<unsigned char>(text[i + k]);
      ok = (cc & 0xC0) == 0x80;
      cp = (cp << 6) | (cc & 0x3F);
    }
    // Overlong forms, surrogates, beyond U+10FFFF, and XML-illegal controls.
    static constexpr uint32_t MIN_CP[] = {0, 0, 0x80, 0x800, 0x10000};
    ok = ok && cp >= MIN_CP[n] && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF) &&
         (cp >= 0x20 || cp == '\t' || cp == '\n' || cp == '\r');
    if (ok) out.append(text, i, n);
    i += ok ? n : 1;
  }
  return out;
}

std::string xmlEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

std::string xhtml(const BookplateContent& c) {
  const std::string title = xmlEscape(validUtf8(c.title));
  const std::string from = xmlEscape(validUtf8(c.from));
  const std::string date = xmlEscape(validUtf8(c.date));
  std::string alt = "Ex libris. Enjoy the book: " + title + ", from " + from;
  if (!date.empty()) alt += ", " + date;
  std::string h =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n<head>\n<title>Ex libris</title>\n"
      "<style type=\"text/css\">\n"
      "body { margin: 0; }\n"
      "div.plate { margin: 0; text-align: center; page-break-after: always; }\n"
      "div.plate img { width: 100%; }\n"
      "p { text-align: center; text-indent: 0; margin: 0; }\n"
      "p.kicker { margin-top: 3em; letter-spacing: 0.3em; font-size: 0.8em; }\n"
      "p.enjoy { margin-top: 1.5em; font-size: 1.6em; font-weight: bold; }\n"
      "p.title { margin: 0.5em 0 1.5em 0; font-style: italic; }\n"
      "p.from { border-top: 1px solid black; padding-top: 0.8em; }\n"
      "</style>\n</head>\n<body>\n";
  if (!c.png.empty()) {
    h += std::string("<div class=\"plate\" style=\"text-align: center\"><img src=\"") + PNG_NAME + "\" alt=\"" + alt +
         "\"/></div>\n";
  }
  // Plain paragraphs (no heading): some readers start a new page at every heading.
  // Alignment is repeated inline for readers that skip <style>.
  h += "<p class=\"kicker\" style=\"text-align: center\">EX LIBRIS</p>\n<p class=\"enjoy\" style=\"text-align: "
       "center\">Enjoy the book</p>\n<p class=\"title\" style=\"text-align: center\"><i>" +
       title + "</i></p>\n";
  h += "<p class=\"from\" style=\"text-align: center\">from <b>" + from + "</b></p>\n";
  if (!date.empty()) h += "<p class=\"date\" style=\"text-align: center\">" + date + "</p>\n";
  h += "</body>\n</html>\n";
  return h;
}

std::string patchOpf(const std::string& original, const bool withImage) {
  std::string s = original;
  removeElements(s, "item", "id", XHTML_ID);
  removeElements(s, "item", "id", PNG_ID);
  removeElements(s, "itemref", "idref", XHTML_ID);

  const size_t manifestClose = findTag(s, "manifest", 0, true);
  if (manifestClose == std::string::npos) return {};
  const std::string p = prefixAt(s, manifestClose, true);
  std::string items = std::string("<") + p + "item id=\"" + XHTML_ID + "\" href=\"" + XHTML_NAME +
                      "\" media-type=\"application/xhtml+xml\"/>\n";
  if (withImage) {
    items += std::string("<") + p + "item id=\"" + PNG_ID + "\" href=\"" + PNG_NAME + "\" media-type=\"image/png\"/>\n";
  }
  // At the start of the closing tag's line, so a second patch gives the same text.
  size_t itemsAt = manifestClose;
  while (itemsAt > 0 && (s[itemsAt - 1] == ' ' || s[itemsAt - 1] == '\t')) itemsAt--;
  if (itemsAt == 0 || s[itemsAt - 1] != '\n') itemsAt = manifestClose;
  s.insert(itemsAt, items);

  const size_t spine = findTag(s, "spine", 0);
  if (spine == std::string::npos) return {};
  const size_t spineGt = s.find('>', spine);
  if (spineGt == std::string::npos || s[spineGt - 1] == '/') return {};
  const std::string itemref = std::string("<") + p + "itemref idref=\"" + XHTML_ID + "\"/>\n";
  size_t insertAt = spineGt + 1;
  const size_t first = findTag(s, "itemref", insertAt);
  const size_t spineClose = findTag(s, "spine", insertAt, true);
  if (first != std::string::npos && (spineClose == std::string::npos || first < spineClose)) {
    insertAt = looksLikeCover(s, attrValue(s, first, "idref")) ? elementEnd(s, first, "itemref") : first;
    if (insertAt == std::string::npos) return {};
  }
  if (insertAt < s.size() && s[insertAt] == '\n') insertAt++;
  s.insert(insertAt, itemref);
  return s;
}

std::string opfTitle(const std::string& opf) {
  const size_t tag = findTag(opf, "title", 0);
  if (tag == std::string::npos) return {};
  const size_t gt = opf.find('>', tag);
  const size_t close = gt == std::string::npos ? gt : findTag(opf, "title", gt, true);
  if (close == std::string::npos) return {};
  const std::string raw = opf.substr(gt + 1, close - gt - 1);
  std::string out;
  for (size_t i = 0; i < raw.size(); i++) {
    if (raw[i] != '&') {
      // Collapse runs of whitespace (titles may be wrapped in the OPF).
      if (std::isspace(static_cast<unsigned char>(raw[i]))) {
        if (!out.empty() && out.back() != ' ') out += ' ';
      } else {
        out += raw[i];
      }
      continue;
    }
    const size_t semi = raw.find(';', i);
    if (semi == std::string::npos) {
      out += raw[i];
      continue;
    }
    const std::string ent = raw.substr(i + 1, semi - i - 1);
    uint32_t cp = 0;
    if (ent == "amp")
      cp = '&';
    else if (ent == "lt")
      cp = '<';
    else if (ent == "gt")
      cp = '>';
    else if (ent == "quot")
      cp = '"';
    else if (ent == "apos")
      cp = '\'';
    else if (ent.size() > 1 && ent[0] == '#')
      cp = static_cast<uint32_t>(strtoul(ent.c_str() + 1 + (ent[1] == 'x'), nullptr, ent[1] == 'x' ? 16 : 10));
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      out += raw[i];
      continue;
    }
    // UTF-8 encode.
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    i = semi;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return validUtf8(out);
}

std::vector<uint8_t> encodePng1(const uint8_t* rows, const int w, const int h, const int stride) {
  const size_t rowBytes = static_cast<size_t>(w + 7) / 8;
  const size_t rawSize = static_cast<size_t>(h) * (rowBytes + 1);
  // zlib stream of stored deflate blocks (deflate is compiled out of miniz here), built
  // straight from the rows: 5 bytes of block header per 64 KB.
  std::vector<uint8_t> z;
  z.reserve(2 + rawSize + 5 * (rawSize / 65535 + 1) + 4);
  z.push_back(0x78);
  z.push_back(0x01);
  mz_ulong adler = MZ_ADLER32_INIT;
  size_t blockLeft = 0;
  size_t emitted = 0;
  const auto emit = [&](const uint8_t* data, size_t n) {
    while (n > 0) {
      if (blockLeft == 0) {
        blockLeft = std::min<size_t>(rawSize - emitted, 65535);
        z.push_back(emitted + blockLeft == rawSize ? 1 : 0);
        put16(z, blockLeft);
        put16(z, ~blockLeft & 0xFFFF);
      }
      const size_t take = std::min(n, blockLeft);
      z.insert(z.end(), data, data + take);
      adler = mz_adler32(adler, data, take);
      data += take;
      n -= take;
      blockLeft -= take;
      emitted += take;
    }
  };
  std::vector<uint8_t> line(rowBytes + 1);
  for (int y = 0; y < h; y++) {
    line[0] = 0;  // filter: none
    memcpy(line.data() + 1, rows + static_cast<size_t>(y) * stride, rowBytes);
    // Bits past the width are ignored by decoders; keep them white anyway.
    if (w % 8) line[rowBytes] |= static_cast<uint8_t>(0xFF >> (w % 8));
    emit(line.data(), line.size());
  }
  putBE32(z, static_cast<uint32_t>(adler));

  std::vector<uint8_t> png;
  png.reserve(8 + (12 + 13) + (12 + z.size()) + 12);  // signature + IHDR, IDAT, IEND chunks
  png.insert(png.end(), {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'});
  const auto chunk = [&png](const char* type, const std::vector<uint8_t>& data) {
    putBE32(png, data.size());
    const size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    putBE32(png, static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, png.data() + start, png.size() - start)));
  };
  std::vector<uint8_t> ihdr;
  putBE32(ihdr, w);
  putBE32(ihdr, h);
  ihdr.insert(ihdr.end(), {1, 0, 0, 0, 0});  // depth 1, grayscale, deflate, filter 0, no interlace
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  return png;
}

}  // namespace bookplate

void BookplateEpub::addInput(const uint32_t offset, const uint32_t length) {
  if (length == 0) return;
  segments.push_back({true, offset, length});
  totalSize += length;
}

void BookplateEpub::addMemory(const uint8_t* data, const size_t length) {
  if (length == 0) return;
  segments.push_back({false, static_cast<uint32_t>(memory.size()), static_cast<uint32_t>(length)});
  memory.insert(memory.end(), data, data + length);
  totalSize += length;
}

struct BookplateEpub::Index : ZipIndex {};

BookplateEpub::BookplateEpub() = default;
BookplateEpub::~BookplateEpub() = default;

bool BookplateEpub::open(BookplateInput& in, std::string* error) {
  input = &in;
  index.reset(new (std::nothrow) Index());
  if (!index) return fail(error, "out of memory");
  if (loadZip(in, *index, error)) return true;
  index.reset();
  return false;
}

std::string BookplateEpub::title() const { return index ? bookplate::opfTitle(index->opf) : std::string(); }

size_t BookplateEpub::largestAllocation(const size_t pngSize) const {
  // `memory`: the stored OPF (plus the new items), the page, the PNG, the new central directory.
  return index ? index->opf.size() + pngSize + index->cd.size() + 16 * 1024 : 0;
}

size_t BookplateEpub::memoryNeeded(const size_t pngSize) const {
  // `memory`, plus the patched OPF and the new central directory alive while it is built.
  return index ? largestAllocation(pngSize) + index->opf.size() + index->cd.size() + 16 * 1024 : 0;
}

bool BookplateEpub::prepare(const BookplateContent& content, std::string* error) {
  if (!index) return fail(error, "not open");
  memory.clear();
  segments.clear();
  totalSize = 0;
  segIndex = 0;
  segPos = 0;

  ZipIndex& z = *index;
  std::vector<Entry>& entries = z.entries;
  const std::vector<size_t>& order = z.order;
  const std::vector<uint8_t>& cd = z.cd;
  const std::string& opfPath = z.opfPath;
  Entry* opfEntry = &entries[z.opfIndex];
  const std::string& xml = z.opf;
  const bool withImage = !content.png.empty();
  const std::string opf = bookplate::patchOpf(xml, withImage);
  if (opf.empty()) return fail(error, "OPF has no manifest or spine");
  opfEntry->role = Entry::Opf;
  memory.reserve(largestAllocation(content.png.size()));

  const size_t slash = opfPath.rfind('/');
  const std::string opfDir = slash == std::string::npos ? std::string() : opfPath.substr(0, slash + 1);
  const std::string xhtmlPath = opfDir + bookplate::XHTML_NAME;
  const std::string pngPath = opfDir + bookplate::PNG_NAME;
  for (auto& e : entries) {
    if (e.name == xhtmlPath || e.name == pngPath) e.role = Entry::Drop;
  }

  // Stored entry: local header + name + data; returns its central record.
  const auto storedEntry = [&](const std::string& name, const uint8_t* data, size_t len, uint16_t time, uint16_t date,
                               uint16_t utf8Flag) {
    const uint32_t crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, data, len));
    const uint32_t offset = totalSize;
    std::vector<uint8_t> lh;
    put32(lh, SIG_LOCAL);
    put16(lh, 20);
    put16(lh, utf8Flag);
    put16(lh, 0);
    put16(lh, time);
    put16(lh, date);
    put32(lh, crc);
    put32(lh, len);
    put32(lh, len);
    put16(lh, name.size());
    put16(lh, 0);
    lh.insert(lh.end(), name.begin(), name.end());
    addMemory(lh.data(), lh.size());
    // Generated bytes live in `memory`; copy the payload there too.
    addMemory(data, len);
    std::vector<uint8_t> c;
    put32(c, SIG_CENTRAL);
    put16(c, 20);
    put16(c, 20);
    put16(c, utf8Flag);
    put16(c, 0);
    put16(c, time);
    put16(c, date);
    put32(c, crc);
    put32(c, len);
    put32(c, len);
    put16(c, name.size());
    put16(c, 0);
    put16(c, 0);
    put16(c, 0);
    put16(c, 0);
    put32(c, 0);
    put32(c, offset);
    c.insert(c.end(), name.begin(), name.end());
    return c;
  };

  // Body: entries in file order; the OPF replaced in place, old plate files dropped.
  std::vector<uint8_t> opfCentral;
  for (const size_t i : order) {
    Entry& e = entries[i];
    if (e.role == Entry::Drop) continue;
    if (e.role == Entry::Opf) {
      opfCentral = storedEntry(e.name, reinterpret_cast<const uint8_t*>(opf.data()), opf.size(), e.time, e.date,
                               e.flags & 0x0800);
      continue;
    }
    e.newOffset = totalSize;
    addInput(e.localOffset, e.spanEnd - e.localOffset);
  }
  const uint16_t time = opfEntry->time, date = opfEntry->date;
  const std::string page = bookplate::xhtml(content);
  const std::vector<uint8_t> xhtmlCentral =
      storedEntry(xhtmlPath, reinterpret_cast<const uint8_t*>(page.data()), page.size(), time, date, 0);
  std::vector<uint8_t> pngCentral;
  if (withImage) pngCentral = storedEntry(pngPath, content.png.data(), content.png.size(), time, date, 0);

  // Central directory in the original order, offsets patched.
  const uint32_t newCdOffset = totalSize;
  uint16_t newCount = 0;
  std::vector<uint8_t> newCd;
  newCd.reserve(cd.size() + 3 * (CENTRAL_FIXED + opfPath.size() + 32) + EOCD_FIXED);
  for (const Entry& e : entries) {
    if (e.role == Entry::Drop) continue;
    newCount++;
    if (e.role == Entry::Opf) {
      newCd.insert(newCd.end(), opfCentral.begin(), opfCentral.end());
      continue;
    }
    const size_t at = newCd.size();
    newCd.insert(newCd.end(), cd.begin() + e.centralOffset, cd.begin() + e.centralOffset + e.centralLength);
    set32(&newCd[at + 42], e.newOffset);
  }
  newCd.insert(newCd.end(), xhtmlCentral.begin(), xhtmlCentral.end());
  newCount++;
  if (withImage) {
    newCd.insert(newCd.end(), pngCentral.begin(), pngCentral.end());
    newCount++;
  }
  const uint32_t cdLen = static_cast<uint32_t>(newCd.size());
  put32(newCd, SIG_EOCD);
  put16(newCd, 0);
  put16(newCd, 0);
  put16(newCd, newCount);
  put16(newCd, newCount);
  put32(newCd, cdLen);
  put32(newCd, newCdOffset);
  put16(newCd, 0);
  addMemory(newCd.data(), newCd.size());
  index.reset();  // the original central directory and OPF are no longer needed
  return true;
}

int BookplateEpub::read(uint8_t* buf, const size_t len) {
  size_t done = 0;
  while (done < len && segIndex < segments.size()) {
    const Segment& seg = segments[segIndex];
    const size_t n = std::min<size_t>(len - done, seg.length - segPos);
    if (seg.fromInput) {
      if (!input->readAt(seg.offset + segPos, buf + done, n)) return -1;
    } else {
      memcpy(buf + done, memory.data() + seg.offset + segPos, n);
    }
    done += n;
    segPos += static_cast<uint32_t>(n);
    if (segPos == seg.length) {
      segIndex++;
      segPos = 0;
    }
  }
  return static_cast<int>(done);
}
