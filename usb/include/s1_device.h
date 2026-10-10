// s1_device.h - protocol engine: turns a transport into ACEMAGIC S1 display
// operations (initialise, send frame, partial update, heartbeat).
//
// The engine owns pacing and reconnection policy but delegates all hardware
// specifics to ITransport. It is fully testable with MockTransport.
//
// License: GPL-3.0 (see LICENSE).
#ifndef S1_DEVICE_H
#define S1_DEVICE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "s1_protocol.h"
#include "s1_transport.h"

namespace s1 {

struct DeviceConfig {
  // Pacing. Derived from the upstream reference defaults and hardware notes.
  unsigned heartbeatIntervalMs = 60000;  // README default
  unsigned interChunkDelayUs = 0;        // optional micro-delay between chunks
  unsigned writeTimeoutMs = 1000;
  unsigned reconnectBackoffMs = 1000;
  bool sendOrientationOnOpen = true;
  bool portrait = false;                  // default landscape

  // Statistics for the control app.
  struct Stats {
    uint64_t framesSent = 0;
    uint64_t framesFailed = 0;
    uint64_t chunksSent = 0;
    uint64_t heartbeats = 0;
    uint64_t reconnects = 0;
  };
};

// Callback for injecting a sleep so tests run instantly. Signature matches a
// monotonic clock in microseconds.
using SleepFn = void (*)(int64_t micros);

class S1Device {
 public:
  // `transport` must outlive the device. `clock` may be null for a default
  // monotonic clock; tests inject a fake.
  S1Device(std::unique_ptr<ITransport> transport, const DeviceAddress& address,
           const DeviceConfig& config);
  ~S1Device();

  void set_sleep_fn(SleepFn fn) { sleepFn_ = fn; }
  // Overridable "now" in microseconds for pacing tests; null = default clock.
  void set_now_fn(int64_t (*fn)()) { nowFn_ = fn; }

  // Open the device and perform initialisation (orientation + first heartbeat).
  bool Open(std::string* error);

  // Close (idempotent).
  void Close();

  bool IsOpen() const;

  // Send a full RGB565 frame (320x170, host-natural words). 27 chunks.
  // On any chunk failure the frame is abandoned and reported failed; the caller
  // decides whether to retry (the engine never resumes a partial frame).
  bool SendFrame(const uint16_t* rgb565, std::string* error);

  // Send a partial-region update. `pixels` has width*height words.
  bool SendRegion(const Region& r, const uint16_t* pixels, std::string* error);

  // Send a heartbeat if the interval has elapsed. Returns true if one was sent.
  bool MaybeHeartbeat(std::string* error);

  // Force a heartbeat now.
  bool SendHeartbeatNow(std::string* error);

  // Reconnect after a failure: close, back off, reopen, reinitialise.
  bool Reconnect(std::string* error);

  const DeviceConfig::Stats& stats() const { return stats_; }
  const DeviceAddress& address() const { return address_; }

 private:
  void SleepUs(int64_t micros);

  std::unique_ptr<ITransport> transport_;
  DeviceAddress address_;
  DeviceConfig config_;
  DeviceConfig::Stats stats_;
  SleepFn sleepFn_ = nullptr;
  int64_t (*nowFn_)() = nullptr;
  int64_t lastHeartbeatUs_ = 0;
  bool initialised_ = false;
};

}  // namespace s1

#endif  // S1_DEVICE_H
