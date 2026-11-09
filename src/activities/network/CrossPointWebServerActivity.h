#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "NetworkModeSelectionActivity.h"
#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"

// Web server activity states
enum class WebServerActivityState {
  MODE_SELECTION,  // Choosing between Join Network and Create Hotspot
  WIFI_SELECTION,  // WiFi selection subactivity is active (for Join Network mode)
  AP_STARTING,     // Starting Access Point mode
  SERVER_RUNNING,  // Web server is running and handling requests
  SHUTTING_DOWN    // Shutting down server and WiFi
};

/**
 * CrossPointWebServerActivity is the entry point for file transfer functionality.
 * It:
 * - First presents a choice between "Join a Network" (STA), "Connect to Calibre", and "Create Hotspot" (AP)
 * - For STA mode: Launches WifiSelectionActivity to connect to an existing network
 * - For AP mode: Creates an Access Point that clients can connect to
 * - Starts the CrossPointWebServer when connected
 * - Handles client requests in its loop() function
 * - Cleans up the server and shuts down WiFi on exit
 */
class CrossPointWebServerActivity final : public Activity {
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;

  // Network mode
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;
  // Set when re-entered after the heap-defrag reboot: skip mode selection, go
  // straight to Join Network, and don't reboot again.
  bool startInJoinNetwork = false;

  // Web server - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;  // For STA mode: network name, For AP mode: AP name
  // What the screen shows about uploads (HTTP POST or WebSocket). Written by the
  // main task, also from inside an HTTP upload; render() reads a copy taken under
  // transferMutex. Fixed-size names so nothing allocates under the lock.
  struct TransferView {
    bool active = false;
    size_t received = 0;
    size_t total = 0;  // 0 when unknown
    char name[96] = "";
    uint32_t count = 0;  // files received this session
    char lastName[96] = "";
  };
  TransferView transfer;
  mutable std::mutex transferMutex;
  TransferView transferSnapshot() const;
  // Progress bookkeeping, main task only.
  bool transferDrawPending = false;
  unsigned long lastTransferPoll = 0;
  unsigned long lastTransferDraw = 0;
  void pollTransfer();
  void renderDeck(const TransferView& tv) const;
  void renderTransferStatus(const TransferView& tv) const;

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  // Set when Back or Home is seen while an upload holds handleClient().
  bool leaveRequested = false;

  // Sustained WiFi-loss tracking; abandon only after WIFI_ABANDON_MS.
  int consecutiveDisconnects = 0;
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 5UL * 60UL * 1000UL;

  // Cached signal-strength bracket (0..4) for the WiFi indicator.
  int lastWifiBars = 0;

  void renderServerRunning(const TransferView& tv) const;
  void renderWifiIndicator(int subHeaderTop) const;

  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startWebServer();

 public:
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       bool startInJoinNetwork = false)
      : Activity("CrossPointWebServer", renderer, mappedInput), startInJoinNetwork(startInJoinNetwork) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
