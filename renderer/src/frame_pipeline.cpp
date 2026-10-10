// frame_pipeline.cpp - renderer core implementation.
// License: GPL-3.0 (see LICENSE).

#include "frame_pipeline.h"

#include "s1_log.h"

namespace s1 {

FramePipeline::FramePipeline(IFrameSource* source, S1Device* device,
                             const PipelineConfig& config, ClockFn clock)
    : source_(source),
      device_(device),
      config_(config),
      clock_(clock),
      converted_(new uint16_t[kNativeWidth * kNativeHeight]) {}

void FramePipeline::Convert(const IFrameSource::Frame& f) {
  const int64_t t0 = clock_();
  ConvertBgraToNativeRgb565(f.data, static_cast<int>(f.width),
                            static_cast<int>(f.height), converted_.get());
  stats_.lastConvertUs = clock_() - t0;
}

bool FramePipeline::SendFrame(const IFrameSource::Frame& frame,
                              std::string* error) {
  if (frame.width == 0 || frame.height == 0 || !frame.data) {
    if (error) *error = "invalid frame";
    return false;
  }
  Convert(frame);

  const int64_t t0 = clock_();
  if (!device_->SendFrame(converted_.get(), error)) {
    ++stats_.framesFailed;
    return false;
  }
  stats_.lastSendUs = clock_() - t0;
  ++stats_.framesSent;
  if (frame.timestampQpc != 0) {
    stats_.lastEndToEndUs = clock_() - frame.timestampQpc;
  }
  return true;
}

int FramePipeline::PumpOnce(std::string* error) {
  IFrameSource::Frame frame;
  if (!source_->GetLatest(&frame)) {
    return 0;
  }
  ++stats_.framesReceived;
  lastSeq_ = frame.seq;

  // Pacing: if we sent too recently, drop this frame (latest-wins) rather than
  // queueing it. This is the backpressure mechanism.
  const int64_t now = clock_();
  if (hasSent_ && config_.minFrameIntervalUs > 0 &&
      (now - lastSendUs_) < config_.minFrameIntervalUs) {
    ++stats_.framesDropped;
    source_->Release(frame);
    return 0;
  }

  const bool ok = SendFrame(frame, error);
  lastSendUs_ = clock_();
  hasSent_ = true;
  source_->Release(frame);
  return ok ? 1 : -1;
}

}  // namespace s1
