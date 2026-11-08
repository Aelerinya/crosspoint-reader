#pragma once

#include <BookSender.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class BookplateInput;
class BookplateEpub;

// Sends one book to a nearby reader that is showing its Wi-Fi transfer
// hotspot (stock Xteink or CrossPoint). The user picks the network; the
// receiver is then identified by probing it. See lib/BookSender/README.md.
class SendBookActivity final : public Activity {
  enum class State { Scanning, NoneFound, Picking, EnteringPassword, AskReplace, Connecting, Sending, Done, Failed };

  struct Network {
    std::string ssid;
    int32_t rssi;
    bool encrypted;
    const PeerHotspot* known;  // set when the SSID is a default reader hotspot name
  };

  std::string bookPath;
  std::string bookName;
  State state = State::Scanning;
  std::vector<Network> networks;  // picker entries, at most MAX_NETWORKS
  Network peer{};
  PeerFirmware peerFirmware = PeerFirmware::Unknown;
  unsigned long connectStartedAt = 0;
  volatile uint32_t sentBytes = 0;
  volatile uint32_t totalBytes = 0;
  volatile bool cancelRequested = false;
  bool holdAwakeOnce = false;  // keeps the device awake for the frame after a blocking transfer
  std::string failureDetail;
  OptionPopup picker;

  void setState(State next);
  void startScan();
  void collectScanResults(int count);
  void showPicker();
  void choose(int index);
  void connectTo(const char* password);
  // Builds the rewritten EPUB with a bookplate; false = send the file as is.
  bool preparePlate(BookplateInput& input, BookplateEpub& plated);
  void transfer(bool overwrite);
  void fail(const std::string& detail);
  const char* peerLabel() const;

 public:
  SendBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return holdAwakeOnce || state == State::Connecting || state == State::Sending; }
};
