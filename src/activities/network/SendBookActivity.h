#pragma once

#include <BookSender.h>

#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Sends one book to a nearby reader that is showing its Wi-Fi transfer
// hotspot (stock Xteink or CrossPoint). See lib/BookSender/README.md.
class SendBookActivity final : public Activity {
  enum class State { Scanning, NoneFound, Picking, AskReplace, Connecting, Sending, Done, Failed };

  std::string bookPath;
  std::string bookName;
  State state = State::Scanning;
  const PeerHotspot* found[2] = {nullptr, nullptr};  // best stock / CrossPoint hotspot in range
  const PeerHotspot* peer = nullptr;
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
  void connectTo(const PeerHotspot* hotspot);
  void transfer(bool overwrite);
  void fail(const std::string& detail);
  static const char* peerLabel(const PeerHotspot* hotspot);

 public:
  SendBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return holdAwakeOnce || state == State::Connecting || state == State::Sending; }
};
