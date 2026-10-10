// s1_device.cpp - ACEMAGIC S1 protocol engine.
// License: GPL-3.0 (see LICENSE).

#include "s1_device.h"

#include <chrono>
#include <thread>

#include "s1_log.h"

namespace s1 {
namespace {
int64_t DefaultNowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch())
      .count();
}
void DefaultSleep(int64_t micros) {
  if (micros <= 0) return;
  std::this_thread::sleep_for(std::chrono::microseconds(micros));
}
int64_t SecondsOfDay() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto secs = duration_cast<seconds>(now.time_since_epoch()).count();
  return secs % 86400;
}
}  // namespace

S1Device::S1Device(std::unique_ptr<ITransport> transport,
                   const DeviceAddress& address, const DeviceConfig& config)
    : transport_(std::move(transport)), address_(address), config_(config) {}

S1Device::~S1Device() { Close(); }

void S1Device::SleepUs(int64_t micros) {
  if (sleepFn_) {
    sleepFn_(micros);
  } else {
    DefaultSleep(micros);
  }
}

bool S1Device::IsOpen() const { return transport_ && transport_->IsOpen(); }

bool S1Device::Open(std::string* error) {
  if (!transport_->Open(address_, error)) {
    S1_ERROR("S1Device::Open failed: %s", error ? error->c_str() : "?");
    return false;
  }
  S1_INFO("S1Device opened %s", transport_->Describe().c_str());

  if (config_.sendOrientationOnOpen) {
    Orientation o;
    o.portrait = config_.portrait;
    auto report = BuildOrientationReport(o);
    if (!transport_->Write(report, config_.writeTimeoutMs, error)) {
      S1_ERROR("orientation write failed: %s", error ? error->c_str() : "?");
      return false;
    }
    // A brief settle before the first heartbeat, matching the reference flow.
    SleepUs(50 * 1000);
  }

  if (!SendHeartbeatNow(error)) {
    return false;
  }
  initialised_ = true;
  return true;
}

void S1Device::Close() {
  if (transport_) transport_->Close();
  initialised_ = false;
}

bool S1Device::SendHeartbeatNow(std::string* error) {
  const int64_t sod = SecondsOfDay();
  Heartbeat hb;
  hb.hours = static_cast<uint8_t>((sod / 3600) % 24);
  hb.minutes = static_cast<uint8_t>((sod / 60) % 60);
  hb.seconds = static_cast<uint8_t>(sod % 60);

  auto report = BuildHeartbeatReport(hb);
  if (!transport_->Write(report, config_.writeTimeoutMs, error)) {
    return false;
  }
  ++stats_.heartbeats;
  lastHeartbeatUs_ = nowFn_ ? nowFn_() : DefaultNowUs();
  return true;
}

bool S1Device::MaybeHeartbeat(std::string* error) {
  const int64_t now = nowFn_ ? nowFn_() : DefaultNowUs();
  const int64_t elapsedMs = (now - lastHeartbeatUs_) / 1000;
  if (elapsedMs >= static_cast<int64_t>(config_.heartbeatIntervalMs)) {
    return SendHeartbeatNow(error);
  }
  return false;
}

bool S1Device::SendFrame(const uint16_t* rgb565, std::string* error) {
  if (!rgb565) {
    if (error) *error = "null frame";
    return false;
  }
  if (!IsOpen()) {
    if (error) *error = "device not open";
    return false;
  }

  for (size_t chunk = 0; chunk < kRedrawChunkCount; ++chunk) {
    auto report = BuildRedrawChunkReport(chunk);
    EncodeRedrawChunkPayload(report, chunk, rgb565);
    if (!transport_->Write(report, config_.writeTimeoutMs, error)) {
      ++stats_.framesFailed;
      S1_ERROR("frame chunk %zu/%zu failed: %s", chunk + 1, kRedrawChunkCount,
               error ? error->c_str() : "?");
      return false;  // abandon the frame; do not resume mid-frame
    }
    ++stats_.chunksSent;
    if (config_.interChunkDelayUs) SleepUs(config_.interChunkDelayUs);
  }
  ++stats_.framesSent;
  return true;
}

bool S1Device::SendRegion(const Region& r, const uint16_t* pixels,
                          std::string* error) {
  if (!pixels) {
    if (error) *error = "null region";
    return false;
  }
  if (!IsOpen()) {
    if (error) *error = "device not open";
    return false;
  }
  // Enforce the hardware limits documented in hardware-protocol.md.
  const size_t bytes = static_cast<size_t>(r.width) * r.height * 2;
  if (r.width > 254 || r.height > 170 || bytes > kDataBytes) {
    if (error) *error = "region exceeds panel limits";
    return false;
  }
  auto report = BuildRefreshHeaderReport(r);
  EncodeRefreshPayload(report, r, pixels);
  if (!transport_->Write(report, config_.writeTimeoutMs, error)) {
    return false;
  }
  return true;
}

bool S1Device::Reconnect(std::string* error) {
  S1_WARN("reconnecting to %s", transport_->Describe().c_str());
  Close();
  SleepUs(static_cast<int64_t>(config_.reconnectBackoffMs) * 1000);
  ++stats_.reconnects;
  return Open(error);
}

}  // namespace s1
