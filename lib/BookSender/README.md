# BookSender

Sends a book from this reader's SD card to another nearby e-reader over Wi-Fi.
The receiver needs no special software: it only has to be on its own Wi-Fi
transfer screen. The sender joins the receiver's hotspot and uploads the file
the same way a phone browser would.

Supported receivers:

- **Stock Xteink firmware** (X4). The protocol is reverse-engineered from the
  V7.2.3 app partition and Xteink's web app; it has not been tried on a real
  stock reader yet.
- **CrossPoint** (and forks that keep its `/api/status` and `/upload` endpoints)

The library is plain C++ with no Arduino dependency. It talks through two small
interfaces (`BookSendTransport` for the TCP connection, `BookSendSource` for the
file), so the firmware wires it to `WiFiClient` and `HalFile`, and the host
tests wire it to POSIX sockets and memory buffers.

## What the receiver must do

| | Stock Xteink | CrossPoint |
|---|---|---|
| Screen | Upgrade/Transfer → Phone Transfer (hotspot) | File Transfer → **Create Hotspot** |
| Wi-Fi network | `E-Paper`, password `12345678` | `CrossPoint-Reader`, open |
| Reader's address | `192.168.3.3` | `192.168.4.1` (the AP's gateway) |

The sender joins that network and uses `WiFi.gatewayIP()`, falling back to the
address above.

## Protocols

### Telling them apart

The hotspot name decides which protocol is used. `BookSender::detect()` also
probes the server (`GET /api/status` answering `200` → CrossPoint, else
`GET /status` answering `200` → stock); the firmware only logs a mismatch, which
helps when a receiver behaves unexpectedly. It would be the way to identify a
reader found on a shared Wi-Fi network.

### CrossPoint (documented in `docs/webserver-endpoints.md`)

- Make the folder: `POST /mkdir`, urlencoded `name=Books&path=/`. Fails with
  `400` if it already exists; that is fine to ignore.
- Upload: `POST /upload?path=/Books[&overwrite=1]`, multipart, one file part
  (field name `file`; the server only looks at the part's `filename`).
- `200` "File uploaded successfully: …" on success. `400` "File already
  exists: …" when the name is taken and `overwrite` is not set. The upload
  fails if the destination folder doesn't exist, hence the mkdir.
- The book shows up in the receiver's Library on its next open.

### Stock Xteink (not documented anywhere; reverse-engineered)

Sources: the stock app partition's strings (route names, hotspot SSID/password
and IP from the on-screen instructions) and Xteink's own transfer web app at
`http://bofi.xteink.com/` (`assets/pages-index-index.*.js`, object `W`), which
is what the reader's QR code opens. The server is the Arduino `WebServer`
"FSBrowser" pattern:

- Upload: `POST /edit`, multipart, **field `data`**, and the part's
  **`filename` is the full destination path** (`/Books/Title.epub`), not a bare
  name.
- Make a folder: `PUT /edit`, multipart field `path` = folder path with a
  trailing slash (`/Books/`).
- Delete: `DELETE /edit?path=…`. Rename/move: `PUT /edit` with `path` (new)
  and `src` (old).
- Other routes seen: `/status`, `/statusFast`, `/Read_staNameIp`
  (`"<name> <ip>"`), `/Read_sdInit`, `/setWallpaper`, `/fileOpen`,
  `/folderMove`, `/systemSet`.
- On a shared Wi-Fi network (not the hotspot) the stock reader answers at
  `http://e-paper.local/` (mDNS).
- The web app converts `.txt` files to UTF-8 in the browser before uploading;
  EPUBs go up unchanged.
- Unknown: whether it overwrites an existing file (FSBrowser code usually
  does), how it handles non-ASCII bytes in the filename, and whether it
  answers `200` even when the SD write failed.
- It keeps a partially uploaded file, so after a cancelled or broken upload
  the sender sends `DELETE /edit?path=/Books/<name>`. CrossPoint deletes
  partial uploads itself.

### Same Wi-Fi instead of a hotspot (not implemented)

CrossPoint answers a UDP broadcast `hello` on port 8134 with
`crosspoint (on <hostname>);81`; stock answers `e-paper.local` over mDNS. The
same upload code would work once the address is known.

## Request details

- One HTTP/1.1 request per connection, `Connection: close`, exact
  `Content-Length` computed up front, so the file streams from the SD card in
  fixed-size chunks with no buffering of the body.
- `"` and control characters in filenames are replaced with `_` in the
  `Content-Disposition` header. UTF-8 is sent as-is.
- The response is read up to a small cap; the status code decides success and
  the body text is kept for the error message.

## Testing

`test/book_sender/` runs the library against in-process fake stock and
CrossPoint servers on loopback and checks the exact bytes each one receives.
Its `EndToEndAgainstRealServer` test sends a real file to a running receiver
when `BOOK_SENDER_E2E_PORT` and `BOOK_SENDER_E2E_FILE` are set, for example
the simulator in Create Hotspot mode (`CROSSPOINT_SIM_HTTP_PORT`).
