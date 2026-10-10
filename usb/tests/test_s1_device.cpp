// test_s1_device.cpp - tests for the protocol engine using the mock transport.
// License: GPL-3.0 (see LICENSE).

#include <memory>

#include "s1_device.h"
#include "test_framework.h"

using namespace s1;

namespace {
int64_t g_nowUs = 0;
int64_t FakeNow() { return g_nowUs; }
void NoSleep(int64_t micros) { g_nowUs += micros; }
}  // namespace

TEST(open_sends_orientation_then_heartbeat) {
  auto mock = new MockTransport();
  DeviceConfig cfg;
  cfg.portrait = true;
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, cfg);
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&FakeNow);

  std::string err;
  CHECK(dev.Open(&err));
  CHECK(mock->IsOpen());
  CHECK_EQ(mock->write_count(), 2);

  const auto& first = mock->reports()[0];
  CHECK_EQ(first.size(), kReportBytes);
  CHECK_EQ(first[2], 0xA1);  // config
  CHECK_EQ(first[3], 0xF1);  // orientation
  CHECK_EQ(first[4], 0x02);  // portrait

  const auto& second = mock->reports()[1];
  CHECK_EQ(second[2], 0xA1);
  CHECK_EQ(second[3], 0xF2);  // heartbeat
  CHECK_EQ(dev.stats().heartbeats, 1u);
}

TEST(send_frame_is_27_chunks_with_correct_flags) {
  auto mock = new MockTransport();
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&FakeNow);
  std::string err;
  CHECK(dev.Open(&err));
  mock->ClearReports();

  std::vector<uint16_t> frame(kNativeWidth * kNativeHeight, 0x1234);
  CHECK(dev.SendFrame(frame.data(), &err));
  CHECK_EQ(mock->write_count(), static_cast<int>(kRedrawChunkCount));

  CHECK_EQ(mock->reports()[0][2], 0xA3);
  CHECK_EQ(mock->reports()[0][3], 0xF0);  // start
  CHECK_EQ(mock->reports()[1][3], 0xF1);  // continue
  CHECK_EQ(mock->reports()[26][3], 0xF2); // end
  CHECK_EQ(dev.stats().framesSent, 1u);
  CHECK_EQ(dev.stats().chunksSent, 27u);
}

TEST(frame_failure_abandons_frame) {
  auto mock = new MockTransport();
  mock->set_fail_after(10);  // fail on the 11th chunk
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&FakeNow);
  std::string err;
  CHECK(dev.Open(&err));

  std::vector<uint16_t> frame(kNativeWidth * kNativeHeight, 0);
  CHECK(!dev.SendFrame(frame.data(), &err));
  CHECK_EQ(dev.stats().framesSent, 0u);
  CHECK_EQ(dev.stats().framesFailed, 1u);
}

TEST(maybe_heartbeat_respects_interval) {
  g_nowUs = 0;
  auto mock = new MockTransport();
  DeviceConfig cfg;
  cfg.heartbeatIntervalMs = 1000;
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, cfg);
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&FakeNow);
  std::string err;
  CHECK(dev.Open(&err));  // sets lastHeartbeatUs_ = 0

  g_nowUs = 500 * 1000;  // 0.5 s
  CHECK(!dev.MaybeHeartbeat(&err));

  g_nowUs = 1500 * 1000;  // 1.5 s -> due
  CHECK(dev.MaybeHeartbeat(&err));
  CHECK_EQ(dev.stats().heartbeats, 2u);  // open's + this one
}

TEST(region_rejects_oversize) {
  auto mock = new MockTransport();
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  dev.set_sleep_fn(&NoSleep);
  std::string err;
  CHECK(dev.Open(&err));

  std::vector<uint16_t> big(300 * 170, 0);
  Region r{0, 0, 255, 170};  // 255*170*2 > 4096
  CHECK(!dev.SendRegion(r, big.data(), &err));

  std::vector<uint16_t> ok(51 * 40, 0);
  Region r2{0, 0, 51, 40};  // 51*40*2 = 4080 <= 4096
  CHECK(dev.SendRegion(r2, ok.data(), &err));
}

TEST(reconnect_increments_stat) {
  auto mock = new MockTransport();
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&FakeNow);
  std::string err;
  CHECK(dev.Open(&err));
  CHECK(dev.Reconnect(&err));
  CHECK_EQ(dev.stats().reconnects, 1u);
  CHECK(mock->IsOpen());
}

TEST(open_failure_reports_error) {
  auto mock = new MockTransport();
  mock->set_fail_open(true);
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  std::string err;
  CHECK(!dev.Open(&err));
  CHECK(!err.empty());
}

int main() { return s1test::RunAll(); }
