// frame_pipeline.h - portable renderer core.
//
// Consumes the newest available desktop frame, converts it to the panel format
// and transmits it. Kept free of OS dependencies so the drop/pace/convert logic
// is unit-testable; the platform pieces (shared-memory source, HID transport,
// main loop) plug in behind small interfaces.
//
// License: GPL-3.0 (see LICENSE).
#ifndef S1_FRAME_PIPELINE_H
#define S1_FRAME_PIPELINE_H

#include <cstdint>
#include <memory>
#include <string>

#include "s1_device.h"
#include "s1_protocol.h"

namespace s1 {

// A source of desktop frames. Implemented by the shared-memory consumer on
// Windows and by a synthetic source in tests.
class IFrameSource {
 public:
  struct Frame {
    const uint8_t* data = nullptr;  // BGRA8, top row first
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    int64_t seq = 0;
    int64_t timestampQpc = 0;
  };

  virtual ~IFrameSource() = default;

  // Fetch the newest frame. Returns false if no new frame is available since
  // the last successful call. The returned buffer stays valid until the next
  // call.
  virtual bool GetLatest(Frame* out) = 0;

  // Called after a frame's pixels have been fully consumed so the producer can
  // reuse the slot.
  virtual void Release(const Frame& frame) = 0;
};

struct PipelineConfig {
  // Minimum interval between full-frame transmissions, in microseconds. Keeps
  // the firmware from being overrun. 0 = as fast as the link allows.
  int64_t minFrameIntervalUs = 0;
  // Optional region-update mode (not enabled by default; see hardware docs).
  bool useRegionUpdates = false;
  unsigned writeTimeoutMs = 1000;
};

struct PipelineStats {
  uint64_t framesReceived = 0;   // distinct source frames observed
  uint64_t framesSent = 0;
  uint64_t framesDropped = 0;    // superseded before being sent
  uint64_t framesFailed = 0;
  int64_t lastConvertUs = 0;
  int64_t lastSendUs = 0;
  int64_t lastEndToEndUs = 0;
};

// Monotonic microsecond clock; injectable for tests.
using ClockFn = int64_t (*)();

class FramePipeline {
 public:
  FramePipeline(IFrameSource* source, S1Device* device,
                const PipelineConfig& config, ClockFn clock);

  // Process at most one frame. Returns:
  //   1  a frame was sent
  //   0  nothing to do
  //  -1  a send failed (caller should reconnect)
  int PumpOnce(std::string* error);

  // Convert and send a specific frame, bypassing pacing (used by tests and the
  // "send test pattern" path).
  bool SendFrame(const IFrameSource::Frame& frame, std::string* error);

  const PipelineStats& stats() const { return stats_; }

  // The RGB565 conversion buffer (exposed for tests).
  const uint16_t* converted() const { return converted_.get(); }

 private:
  void Convert(const IFrameSource::Frame& f);

  IFrameSource* source_;
  S1Device* device_;
  PipelineConfig config_;
  ClockFn clock_;
  PipelineStats stats_;
  std::unique_ptr<uint16_t[]> converted_;
  int64_t lastSendUs_ = 0;
  bool hasSent_ = false;
  int64_t lastSeq_ = -1;
};

}  // namespace s1

#endif  // S1_FRAME_PIPELINE_H
