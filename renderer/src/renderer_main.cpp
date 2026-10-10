// renderer_main.cpp - user-session renderer process.
//
// Reads desktop frames published by the IddCx driver through shared memory,
// converts them to the panel format and transmits them to the ACEMAGIC S1 LCD
// over the vendor HID interface. Handles USB disconnect/reconnect, driver
// absence, and clean shutdown.
//
// Windows entry point. A portable "--selftest" mode exercises the pipeline with
// a synthetic frame source and the mock transport so the loop can be validated
// without hardware (see tests/).
//
// License: GPL-3.0 (see LICENSE).

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "frame_pipeline.h"
#include "s1_device.h"
#include "s1_log.h"
#include "s1_protocol.h"
#include "s1_transport.h"

#if defined(_WIN32)
#include <windows.h>
#include "shm_frame_source.h"
#endif

using namespace s1;

namespace {
std::atomic<bool> g_stop{false};

void OnSignal(int) { g_stop.store(true); }

int64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch())
      .count();
}

struct RendererConfig {
  std::string logPath;
  int64_t minFrameIntervalUs = 0;  // 0 = uncapped; set for firmware pacing
  unsigned heartbeatIntervalMs = 60000;
  std::string preferredDevicePath;  // optional explicit S1 instance
  bool selftest = false;
  int64_t stopAfterFrames = 0;      // >0: exit after this many frames (tests)
};

class ConsoleSink : public ILogSink {
 public:
  void Write(LogLevel level, const char* line) override {
    std::fprintf(level >= LogLevel::kWarn ? stderr : stdout, "%s\n", line);
  }
};

// Portable self-test source: emits a moving pattern on demand.
class SyntheticSource : public IFrameSource {
 public:
  bool GetLatest(Frame* out) override {
    // Emit a new frame roughly every call, with a changing pattern.
    buffer_.assign(static_cast<size_t>(kNativeWidth) * kNativeHeight * 4, 0);
    for (int y = 0; y < kNativeHeight; ++y) {
      for (int x = 0; x < kNativeWidth; ++x) {
        auto* px = &buffer_[(static_cast<size_t>(y) * kNativeWidth + x) * 4];
        px[0] = static_cast<uint8_t>(x + phase_);  // B
        px[1] = static_cast<uint8_t>(y);           // G
        px[2] = static_cast<uint8_t>(phase_);      // R
        px[3] = 255;                               // A
      }
    }
    out->data = buffer_.data();
    out->width = kNativeWidth;
    out->height = kNativeHeight;
    out->stride = kNativeWidth * 4;
    out->seq = ++seq_;
    out->timestampQpc = 0;
    ++phase_;
    return true;
  }
  void Release(const Frame&) override {}

 private:
  std::vector<uint8_t> buffer_;
  int64_t seq_ = 0;
  int phase_ = 0;
};

int Run(const RendererConfig& cfg) {
  auto transport = CreatePlatformTransport();
#if defined(_WIN32)
  if (!transport && !cfg.selftest) {
    S1_ERROR("no platform transport available");
    return 1;
  }
#endif
  // On non-Windows hosts (and in self-test mode) use the mock transport so the
  // loop can run without hardware.
  if (!transport) {
    S1_WARN("using mock transport (no hardware transport on this host)");
    transport.reset(new MockTransport());
  }

  DeviceConfig dcfg;
  dcfg.heartbeatIntervalMs = cfg.heartbeatIntervalMs;
  DeviceAddress addr;
  addr.devicePath = cfg.preferredDevicePath;

  S1Device device(std::move(transport), addr, dcfg);

  std::string err;
  if (!device.Open(&err)) {
    S1_ERROR("renderer: cannot open LCD: %s", err.c_str());
    // Keep trying: the LCD may not be plugged in yet.
  }

  PipelineConfig pcfg;
  pcfg.minFrameIntervalUs = cfg.minFrameIntervalUs;

  SyntheticSource synthetic;

#if defined(_WIN32)
  ShmFrameSource source;
  bool attached = false;
  IFrameSource* active = nullptr;
  if (cfg.selftest) {
    active = &synthetic;
  }
#else
  IFrameSource* active = &synthetic;  // portable build uses synthetic only
#endif

  FramePipeline pipeline(active, &device, pcfg, &NowUs);

  S1_INFO("renderer: running (selftest=%d)", cfg.selftest ? 1 : 0);

  while (!g_stop.load()) {
#if defined(_WIN32)
    if (!cfg.selftest) {
      if (!attached) {
        if (source.Attach(&err)) {
          attached = true;
          source.consumer().SetPid(static_cast<int32_t>(GetCurrentProcessId()));
          pipeline = FramePipeline(&source, &device, pcfg, &NowUs);
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          continue;
        }
      }
    }
#endif
    if (!device.IsOpen()) {
      device.Reconnect(&err);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    const int rc = pipeline.PumpOnce(&err);
    if (rc == -1) {
      S1_ERROR("renderer: send failed (%s); reconnecting USB", err.c_str());
      device.Reconnect(&err);
    }

    device.MaybeHeartbeat(&err);

    // Small yield so the loop does not spin when there is no new frame.
    if (rc == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    if (cfg.stopAfterFrames > 0 &&
        pipeline.stats().framesSent >= static_cast<uint64_t>(cfg.stopAfterFrames)) {
      break;
    }
  }

  S1_INFO("renderer: stopping. frames=%llu sent=%llu dropped=%llu failed=%llu",
          (unsigned long long)pipeline.stats().framesReceived,
          (unsigned long long)pipeline.stats().framesSent,
          (unsigned long long)pipeline.stats().framesDropped,
          (unsigned long long)pipeline.stats().framesFailed);
  device.Close();
  return 0;
}

RendererConfig ParseArgs(int argc, char** argv) {
  RendererConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--selftest") cfg.selftest = true;
    else if (a == "--log" && i + 1 < argc) cfg.logPath = argv[++i];
    else if (a == "--fps" && i + 1 < argc) {
      const int fps = std::atoi(argv[++i]);
      if (fps > 0) cfg.minFrameIntervalUs = 1000000 / fps;
    } else if (a == "--heartbeat-ms" && i + 1 < argc) {
      cfg.heartbeatIntervalMs = static_cast<unsigned>(std::atoi(argv[++i]));
    } else if (a == "--device-path" && i + 1 < argc) {
      cfg.preferredDevicePath = argv[++i];
    } else if (a == "--frames" && i + 1 < argc) {
      cfg.stopAfterFrames = std::atoll(argv[++i]);
    }
  }
  return cfg;
}
}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
#if defined(_WIN32)
  SetConsoleCtrlHandler([](DWORD) -> BOOL { g_stop.store(true); return TRUE; }, TRUE);
#endif

  const RendererConfig cfg = ParseArgs(argc, argv);

  ConsoleSink console;
  std::unique_ptr<FileLogSink> fileSink;
  if (!cfg.logPath.empty()) {
    fileSink.reset(new FileLogSink(cfg.logPath));
    Log::SetSink(fileSink.get());
  } else {
    Log::SetSink(&console);
  }
  Log::SetLevel(LogLevel::kInfo);

  return Run(cfg);
}
