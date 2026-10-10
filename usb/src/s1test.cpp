// s1test.cpp - standalone ACEMAGIC S1 LCD test utility.
//
// Verifies the USB transport and protocol independently of the display driver:
//   s1test pattern <name>      draw a built-in test pattern (solid/testsrc/gradient)
//   s1test fps <n>             send n full frames of a moving pattern, report FPS
//   s1test region              send a single small region update
//   s1test probe               enumerate and open the device, print identity
//
// On Windows it talks to real hardware. Elsewhere it uses the mock transport and
// can write captured reports to a file for offline inspection:
//   s1test --mock --dump reports.bin pattern testsrc
//
// License: GPL-3.0 (see LICENSE).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "s1_device.h"
#include "s1_log.h"
#include "s1_protocol.h"
#include "s1_transport.h"

using namespace s1;

namespace {

class StderrSink : public ILogSink {
 public:
  void Write(LogLevel level, const char* line) override {
    std::fprintf(level >= LogLevel::kWarn ? stderr : stdout, "%s\n", line);
  }
};

// Color bars across the top, a grey ramp across the bottom, plus corner marks.
void MakeTestPattern(uint16_t* f) {
  const int W = kNativeWidth, H = kNativeHeight;
  const uint16_t bars[8] = {
      PackRgb565(255, 255, 255), PackRgb565(255, 255, 0),
      PackRgb565(0, 255, 255),   PackRgb565(0, 255, 0),
      PackRgb565(255, 0, 255),   PackRgb565(255, 0, 0),
      PackRgb565(0, 0, 255),     PackRgb565(0, 0, 0)};
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      uint16_t c;
      if (y < H / 2) {
        c = bars[(x * 8) / W];
      } else {
        const uint8_t v = static_cast<uint8_t>((x * 255) / (W - 1));
        c = PackRgb565(v, v, v);
      }
      f[y * W + x] = c;
    }
  }
}

void MakeSolid(uint16_t* f, uint8_t r, uint8_t g, uint8_t b) {
  std::fill(f, f + kNativeWidth * kNativeHeight, PackRgb565(r, g, b));
}

// A pattern that changes with `phase` so successive frames differ.
void MakeMoving(uint16_t* f, int phase) {
  const int W = kNativeWidth, H = kNativeHeight;
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const int v = ((x + phase) / 8) % 2 ? 255 : 0;
      f[y * W + x] = PackRgb565(v, static_cast<uint8_t>(y & 0xFF), 128);
    }
  }
}

int64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch())
      .count();
}

void DumpReports(const std::vector<std::vector<uint8_t>>& reports,
                 const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return;
  }
  for (const auto& r : reports) {
    std::fwrite(r.data(), 1, r.size(), f);
  }
  std::fclose(f);
  std::printf("wrote %zu reports to %s\n", reports.size(), path.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  StderrSink sink;
  Log::SetSink(&sink);
  Log::SetLevel(LogLevel::kInfo);

  bool useMock = false;
  std::string dumpPath;
  std::string command;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--mock") {
      useMock = true;
    } else if (a == "--dump" && i + 1 < argc) {
      dumpPath = argv[++i];
    } else if (command.empty()) {
      command = a;
    } else {
      args.push_back(a);
    }
  }

  std::unique_ptr<ITransport> transport;
  MockTransport* mock = nullptr;
  if (useMock) {
    mock = new MockTransport();
    transport.reset(mock);
  } else {
    transport = CreatePlatformTransport();
    if (!transport) {
      std::fprintf(stderr,
                   "no platform transport for this OS; re-run with --mock\n");
      return 2;
    }
  }

  DeviceConfig cfg;
  DeviceAddress addr;
  S1Device dev(std::move(transport), addr, cfg);

  std::string err;
  if (!dev.Open(&err)) {
    std::fprintf(stderr, "open failed: %s\n", err.c_str());
    return 1;
  }
  if (mock) mock->ClearReports();

  std::vector<uint16_t> frame(kNativeWidth * kNativeHeight);

  if (command == "probe") {
    std::printf("device opened: %s\n", dev.IsOpen() ? "yes" : "no");
  } else if (command == "pattern") {
    const std::string which = args.empty() ? "testsrc" : args[0];
    if (which == "solid") {
      MakeSolid(frame.data(), 0, 0, 255);
    } else if (which == "gradient") {
      for (int y = 0; y < kNativeHeight; ++y)
        for (int x = 0; x < kNativeWidth; ++x)
          frame[y * kNativeWidth + x] = PackRgb565(
              static_cast<uint8_t>(x), static_cast<uint8_t>(y), 0);
    } else {
      MakeTestPattern(frame.data());
    }
    if (!dev.SendFrame(frame.data(), &err)) {
      std::fprintf(stderr, "send failed: %s\n", err.c_str());
      return 1;
    }
    std::printf("sent pattern '%s'\n", which.c_str());
  } else if (command == "fps") {
    const int n = args.empty() ? 20 : std::atoi(args[0].c_str());
    const int64_t t0 = NowUs();
    for (int i = 0; i < n; ++i) {
      MakeMoving(frame.data(), i * 4);
      if (!dev.SendFrame(frame.data(), &err)) {
        std::fprintf(stderr, "frame %d failed: %s\n", i, err.c_str());
        break;
      }
      dev.MaybeHeartbeat(&err);
    }
    const double secs = (NowUs() - t0) / 1e6;
    std::printf("sent %d frames in %.3f s -> %.2f FPS\n", n, secs, n / secs);
  } else if (command == "region") {
    Region r{10, 10, 51, 40};
    std::vector<uint16_t> pixels(51 * 40);
    for (size_t i = 0; i < pixels.size(); ++i)
      pixels[i] = PackRgb565(255, 255, 0);
    if (!dev.SendRegion(r, pixels.data(), &err)) {
      std::fprintf(stderr, "region failed: %s\n", err.c_str());
      return 1;
    }
    std::printf("sent region\n");
  } else {
    std::fprintf(stderr,
                 "usage: s1test [--mock] [--dump FILE] <probe|pattern NAME|fps N|region>\n");
    return 2;
  }

  if (mock && !dumpPath.empty()) {
    DumpReports(mock->reports(), dumpPath);
  }
  std::printf("stats: frames=%llu failed=%llu chunks=%llu heartbeats=%llu\n",
              (unsigned long long)dev.stats().framesSent,
              (unsigned long long)dev.stats().framesFailed,
              (unsigned long long)dev.stats().chunksSent,
              (unsigned long long)dev.stats().heartbeats);
  return 0;
}
