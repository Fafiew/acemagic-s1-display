// test_frame_pipeline.cpp - tests for the renderer core.
// License: GPL-3.0 (see LICENSE).

#include <memory>
#include <vector>

#include "frame_pipeline.h"
#include "s1_transport.h"
#include "test_framework.h"

using namespace s1;

namespace {
int64_t g_clockUs = 0;
int64_t Clock() { return g_clockUs; }
void NoSleep(int64_t micros) { g_clockUs += micros; }

// Synthetic frame source that hands out frames on demand.
class FakeSource : public IFrameSource {
 public:
  void SetFrame(uint32_t w, uint32_t h, int64_t seq) {
    width_ = w;
    height_ = h;
    seq_ = seq;
    buffer_.assign(static_cast<size_t>(w) * h * 4, 0);
    // Paint the top-left pixel red (BGRA).
    buffer_[2] = 255;
    buffer_[3] = 255;
    pending_ = true;
  }
  bool GetLatest(Frame* out) override {
    if (!pending_) return false;
    pending_ = false;
    out->data = buffer_.data();
    out->width = width_;
    out->height = height_;
    out->stride = width_ * 4;
    out->seq = seq_;
    out->timestampQpc = 0;
    return true;
  }
  void Release(const Frame&) override { released_++; }
  int released() const { return released_; }

 private:
  std::vector<uint8_t> buffer_;
  uint32_t width_ = 0, height_ = 0;
  int64_t seq_ = 0;
  bool pending_ = false;
  int released_ = 0;
};
}  // namespace

TEST(pump_once_sends_a_frame) {
  g_clockUs = 1000;
  auto mock = new MockTransport();
  DeviceConfig dcfg;
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, dcfg);
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&Clock);
  std::string err;
  CHECK(dev.Open(&err));
  mock->ClearReports();

  FakeSource src;
  src.SetFrame(kNativeWidth, kNativeHeight, 1);
  FramePipeline pipe(&src, &dev, PipelineConfig{}, &Clock);

  CHECK_EQ(pipe.PumpOnce(&err), 1);
  CHECK_EQ(mock->write_count(), static_cast<int>(kRedrawChunkCount));
  CHECK_EQ(pipe.stats().framesSent, 1u);
  CHECK_EQ(src.released(), 1);
}

TEST(pacing_drops_superseded_frames) {
  g_clockUs = 0;
  auto mock = new MockTransport();
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&Clock);
  std::string err;
  CHECK(dev.Open(&err));

  FakeSource src;
  PipelineConfig cfg;
  cfg.minFrameIntervalUs = 100000;  // 10 FPS cap
  FramePipeline pipe(&src, &dev, cfg, &Clock);

  src.SetFrame(kNativeWidth, kNativeHeight, 1);
  CHECK_EQ(pipe.PumpOnce(&err), 1);  // first frame always taken

  src.SetFrame(kNativeWidth, kNativeHeight, 2);
  g_clockUs += 10000;                 // only 10 ms later
  CHECK_EQ(pipe.PumpOnce(&err), 0);   // dropped
  CHECK_EQ(pipe.stats().framesDropped, 1u);

  src.SetFrame(kNativeWidth, kNativeHeight, 3);
  g_clockUs += 200000;                // 200 ms later -> allowed
  CHECK_EQ(pipe.PumpOnce(&err), 1);
  CHECK_EQ(pipe.stats().framesSent, 2u);
}

TEST(send_failure_returns_negative) {
  g_clockUs = 0;
  auto mock = new MockTransport();
  mock->set_fail_after(-1);  // no failure during open
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  dev.set_sleep_fn(&NoSleep);
  dev.set_now_fn(&Clock);
  std::string err;
  // Open consumes writes; clear the injected failure after opening.
  CHECK(dev.Open(&err));
  mock->set_fail_after(mock->write_count());

  FakeSource src;
  src.SetFrame(kNativeWidth, kNativeHeight, 1);
  FramePipeline pipe(&src, &dev, PipelineConfig{}, &Clock);
  CHECK_EQ(pipe.PumpOnce(&err), -1);
  CHECK_EQ(pipe.stats().framesFailed, 1u);
}

TEST(conversion_buffer_holds_red_pixel) {
  g_clockUs = 0;
  auto mock = new MockTransport();
  S1Device dev(std::unique_ptr<ITransport>(mock), DeviceAddress{}, DeviceConfig{});
  dev.set_sleep_fn(&NoSleep);
  std::string err;
  CHECK(dev.Open(&err));

  FakeSource src;
  src.SetFrame(kNativeWidth, kNativeHeight, 1);
  FramePipeline pipe(&src, &dev, PipelineConfig{}, &Clock);
  IFrameSource::Frame f;
  CHECK(src.GetLatest(&f));
  CHECK(pipe.SendFrame(f, &err));
  CHECK_EQ(pipe.converted()[0], PackRgb565(255, 0, 0));
}

int main() { return s1test::RunAll(); }
