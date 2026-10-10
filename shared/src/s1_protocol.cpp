// s1_protocol.cpp - ACEMAGIC S1 front LCD wire protocol (host side).
// See s1_protocol.h and docs/hardware-protocol.md.

#include "s1_protocol.h"

#include <algorithm>
#include <cstring>

namespace s1 {
namespace {

// Write a little-endian uint16 at `off` (two bytes).
inline void PutLe16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
  v[off] = static_cast<uint8_t>(x & 0xFF);
  v[off + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
}

// Allocate a 4104-byte header+data buffer with a zeroed payload.
std::vector<uint8_t> EmptyBuffer() {
  return std::vector<uint8_t>(kBufferBytes, 0);
}

}  // namespace

std::vector<uint8_t> MakeReport(const std::vector<uint8_t>& buffer) {
  std::vector<uint8_t> report(kReportBytes, 0);
  report[0] = 0x00;  // report ID
  const size_t n = std::min(buffer.size(), kBufferBytes);
  std::memcpy(report.data() + kReportIdBytes, buffer.data(), n);
  return report;
}

std::vector<uint8_t> BuildConfigReport(uint8_t subCommand, uint8_t p1) {
  std::vector<uint8_t> buf = EmptyBuffer();
  buf[0] = kSignature;
  buf[1] = kCmdConfig;
  buf[2] = subCommand;
  buf[3] = p1;
  return MakeReport(buf);
}

std::vector<uint8_t> BuildOrientationReport(const Orientation& o) {
  return BuildConfigReport(kSubOrientation,
                           o.portrait ? kOrientationPortrait
                                      : kOrientationLandscape);
}

std::vector<uint8_t> BuildHeartbeatReport(const Heartbeat& hb) {
  std::vector<uint8_t> buf = EmptyBuffer();
  buf[0] = kSignature;
  buf[1] = kCmdConfig;
  buf[2] = kSubSetTime;
  buf[3] = hb.hours;
  buf[4] = hb.minutes;
  buf[5] = hb.seconds;
  return MakeReport(buf);
}

std::vector<uint8_t> BuildRedrawChunkReport(size_t chunkIndex) {
  std::vector<uint8_t> buf = EmptyBuffer();

  uint8_t sub;
  if (chunkIndex == 0) {
    sub = kRedrawStart;
  } else if (chunkIndex == kRedrawChunkCount - 1) {
    sub = kRedrawEnd;
  } else {
    sub = kRedrawContinue;
  }

  const size_t length = (chunkIndex == kRedrawChunkCount - 1)
                            ? kRedrawFinalChunkBytes
                            : kDataBytes;

  // Header layout confirmed against the captured vendor-tool traffic:
  //   [0]=0x55 signature, [1]=0xA3 redraw, [2]=start/continue/end,
  //   [3]=sequence (1-based), [4..5]=byte offset (uint16 LE, wraps mod 65536),
  //   [6..7]=payload length (uint16 LE).
  // Example chunk 0:        55 a3 f0 01 00 00 00 10  -> seq=1 offset=0 len=0x1000
  // Example final chunk 27: 55 a3 f2 1b 00 a0 00 09  -> seq=27 offset=0xa000 len=0x0900
  buf[0] = kSignature;
  buf[1] = kCmdRedraw;
  buf[2] = sub;
  buf[3] = static_cast<uint8_t>(1 + chunkIndex);   // sequence number, 1-based
  PutLe16(buf, 4, static_cast<uint16_t>(chunkIndex * kDataBytes));  // offset
  PutLe16(buf, 6, static_cast<uint16_t>(length));  // length

  return MakeReport(buf);
}

std::vector<uint8_t> BuildRefreshHeaderReport(const Region& r) {
  std::vector<uint8_t> buf = EmptyBuffer();
  buf[0] = kSignature;
  buf[1] = kCmdRefresh;
  PutLe16(buf, 2, r.x);
  PutLe16(buf, 4, r.y);
  buf[6] = r.width;
  buf[7] = r.height;
  return MakeReport(buf);
}

void EncodeRedrawChunkPayload(std::vector<uint8_t>& report, size_t chunkIndex,
                              const uint16_t* pixels565) {
  const size_t byteOffset = chunkIndex * kDataBytes;
  const size_t length = (chunkIndex == kRedrawChunkCount - 1)
                            ? kRedrawFinalChunkBytes
                            : kDataBytes;
  const size_t pixelOffset = byteOffset / 2;
  const size_t pixelCount = length / 2;

  uint8_t* dst = report.data() + kReportIdBytes + kHeaderBytes;
  for (size_t i = 0; i < pixelCount; ++i) {
    const uint16_t wire = SwapEndian16(pixels565[pixelOffset + i]);
    dst[i * 2] = static_cast<uint8_t>(wire & 0xFF);
    dst[i * 2 + 1] = static_cast<uint8_t>((wire >> 8) & 0xFF);
  }
}

void EncodeRefreshPayload(std::vector<uint8_t>& report, const Region& r,
                          const uint16_t* pixels565) {
  const size_t pixelCount = static_cast<size_t>(r.width) * r.height;
  uint8_t* dst = report.data() + kReportIdBytes + kHeaderBytes;
  for (size_t i = 0; i < pixelCount; ++i) {
    const uint16_t wire = SwapEndian16(pixels565[i]);
    dst[i * 2] = static_cast<uint8_t>(wire & 0xFF);
    dst[i * 2 + 1] = static_cast<uint8_t>((wire >> 8) & 0xFF);
  }
}

void ConvertBgraToNativeRgb565(const uint8_t* bgra, int width, int height,
                               uint16_t* out565) {
  const size_t total =
      static_cast<size_t>(kNativeWidth) * kNativeHeight;

  // Letter-box background.
  std::fill(out565, out565 + total, PackRgb565(0, 0, 0));

  if (!bgra || width <= 0 || height <= 0) {
    return;
  }

  const double scale =
      std::min(static_cast<double>(kNativeWidth) / width,
               static_cast<double>(kNativeHeight) / height);
  int dstW = static_cast<int>(width * scale + 0.5);
  int dstH = static_cast<int>(height * scale + 0.5);
  dstW = std::max(1, std::min<int>(dstW, kNativeWidth));
  dstH = std::max(1, std::min<int>(dstH, kNativeHeight));

  const int offX = (kNativeWidth - dstW) / 2;
  const int offY = (kNativeHeight - dstH) / 2;

  for (int dy = 0; dy < dstH; ++dy) {
    // Nearest-neighbour vertical sample.
    const int sy = static_cast<int>((static_cast<long long>(dy) * height) / dstH);
    const uint8_t* srcRow = bgra + (static_cast<size_t>(sy) * width) * 4;
    uint16_t* dstRow =
        out565 + static_cast<size_t>(offY + dy) * kNativeWidth + offX;
    for (int dx = 0; dx < dstW; ++dx) {
      const int sx = static_cast<int>((static_cast<long long>(dx) * width) / dstW);
      const uint8_t* px = srcRow + static_cast<size_t>(sx) * 4;  // B,G,R,A
      dstRow[dx] = PackRgb565(px[2], px[1], px[0]);
    }
  }
}

}  // namespace s1
