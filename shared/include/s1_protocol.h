// s1_protocol.h - ACEMAGIC S1 front LCD wire protocol (host side).
//
// The panel is driven over the vendor-defined USB HID interface (VID 0x04D9,
// PID 0xFD01, interface MI_01). Every command is a fixed 4104-byte buffer
// written as one HID output report, prefixed with a single report ID byte, so
// the total transfer is 4105 bytes.
//
// Protocol reverse-engineered from the Linux implementation by Tomasz Jaworski
// (https://github.com/tjaworski/AceMagic-S1-LED-TFT-Linux, GPL-3.0) and
// corroborated by the official protocol notes in that repository's README.
// See docs/hardware-protocol.md for the full derivation and evidence.
//
// This header is ISOLATED from the transport: it only turns logical operations
// into byte buffers and back. It contains no USB or OS dependencies so it can
// be unit tested on any host.
//
// License: GPL-3.0 (see LICENSE) because protocol knowledge derives from a
// GPL-3.0 project.

#ifndef S1_PROTOCOL_H
#define S1_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace s1 {

// ---------------------------------------------------------------------------
// Fixed protocol constants (see docs/hardware-protocol.md, "Verified facts").
// ---------------------------------------------------------------------------

// The vendor HID device. "HID\VID_04D9&PID_FD01&REV_0110&MI_01".
constexpr uint16_t kVendorId = 0x04D9;   // Holtek Semiconductor
constexpr uint16_t kProductId = 0xFD01;
constexpr uint8_t kVendorInterfaceNumber = 0x01;  // MI_01

// Native panel geometry and colour format.
constexpr uint16_t kNativeWidth = 320;
constexpr uint16_t kNativeHeight = 170;

// One frame in RGB565 = 320 * 170 * 2 = 108800 bytes.
constexpr size_t kFramebufferBytes =
    static_cast<size_t>(kNativeWidth) * kNativeHeight * 2;

// HID report framing. The host writes REPORT_SIZE + BUFFER_SIZE bytes.
constexpr size_t kReportIdBytes = 1;    // leading report-ID byte (0x00)
constexpr size_t kHeaderBytes = 8;      // 8-byte command header
constexpr size_t kDataBytes = 4096;     // payload per report
constexpr size_t kBufferBytes = kHeaderBytes + kDataBytes;  // 4104
constexpr size_t kReportBytes = kReportIdBytes + kBufferBytes;  // 4105

// Redraw chunking: ceil(108800 / 4096) = 27 chunks. The first 26 carry a full
// 4096-byte payload, the final chunk carries 108800 - 26*4096 = 2304 bytes.
constexpr size_t kRedrawChunkCount = 27;
constexpr size_t kRedrawFinalChunkBytes =
    kFramebufferBytes - (kRedrawChunkCount - 1) * kDataBytes;  // 2304

// Header signature and command bytes.
constexpr uint8_t kSignature = 0x55;
constexpr uint8_t kCmdConfig = 0xA1;
constexpr uint8_t kCmdRefresh = 0xA2;  // partial-region bitblt
constexpr uint8_t kCmdRedraw = 0xA3;   // whole-frame bitblt

// kCmdConfig sub-commands.
constexpr uint8_t kSubOrientation = 0xF1;
constexpr uint8_t kSubSetTime = 0xF2;   // clock + heartbeat
constexpr uint8_t kSubSetTimeHeld = 0xF3;  // (unused here) hard clock set

// kCmdRedraw sub-commands (chunk sequencing).
constexpr uint8_t kRedrawStart = 0xF0;
constexpr uint8_t kRedrawContinue = 0xF1;
constexpr uint8_t kRedrawEnd = 0xF2;

// Orientation values.
constexpr uint8_t kOrientationLandscape = 0x01;
constexpr uint8_t kOrientationPortrait = 0x02;

// ---------------------------------------------------------------------------
// 16-byte RGB565 with byte swap.
//
// The panel expects RGB565 with the high and low bytes exchanged relative to
// the little-endian `uint16_t` the host naturally produces. The Linux
// implementation performs exactly this swap ("SWAPENDIAN"). Expressed as an
// explicit byte swap it is endian-independent and reproducible.
// ---------------------------------------------------------------------------
inline uint16_t SwapEndian16(uint16_t v) {
  return static_cast<uint16_t>((v >> 8) | (v << 8));
}

// Pack 8-bit channels into an RGB565 word (r5 g6 b5 in the high bits).
inline uint16_t PackRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) |
                               ((b & 0xF8u) >> 3));
}

// Pack RGB565 and apply the wire byte swap in one step.
inline uint16_t PackRgb565Wire(uint8_t r, uint8_t g, uint8_t b) {
  return SwapEndian16(PackRgb565(r, g, b));
}

// ---------------------------------------------------------------------------
// Logical command descriptions.
// ---------------------------------------------------------------------------

struct Orientation {
  bool portrait = false;  // false = landscape, true = portrait
};

struct Heartbeat {
  uint8_t hours = 0;
  uint8_t minutes = 0;
  uint8_t seconds = 0;
};

struct Region {
  uint16_t x = 0;
  uint16_t y = 0;
  uint8_t width = 0;
  uint8_t height = 0;
};

// ---------------------------------------------------------------------------
// Report builders. Each returns a full kReportBytes-byte buffer ready to be
// written to the HID output report.
// ---------------------------------------------------------------------------

// Wrap a 4104-byte header+data buffer into a 4105-byte report with a leading
// report-ID byte of 0x00.
std::vector<uint8_t> MakeReport(const std::vector<uint8_t>& buffer);

// Build a full report (report ID + header + zeroed data) for a command that
// needs no payload (orientation / heartbeat).
std::vector<uint8_t> BuildConfigReport(uint8_t subCommand, uint8_t p1);
std::vector<uint8_t> BuildOrientationReport(const Orientation& o);
std::vector<uint8_t> BuildHeartbeatReport(const Heartbeat& hb);

// Build the header-only report for a redraw chunk. `chunkIndex` is 0-based.
std::vector<uint8_t> BuildRedrawChunkReport(size_t chunkIndex);
// Build the header-only report for a partial-region refresh.
std::vector<uint8_t> BuildRefreshHeaderReport(const Region& r);

// ---------------------------------------------------------------------------
// Payload encoders. `pixels565` must be exactly width*height words in the
// host-natural order (see PackRgb565). Encoding is byte-swapped to wire order.
// ---------------------------------------------------------------------------

// Encode one redraw chunk's pixels into the trailing 4096-byte payload area of
// an already-built report, in place.
void EncodeRedrawChunkPayload(std::vector<uint8_t>& report, size_t chunkIndex,
                              const uint16_t* pixels565);
// Encode a partial-region refresh payload in place.
void EncodeRefreshPayload(std::vector<uint8_t>& report, const Region& r,
                          const uint16_t* pixels565);

// ---------------------------------------------------------------------------
// Dependency-free frame buffer -> RGB565 conversion.
// The renderer prefers a GPU path; this CPU implementation is the reference
// and is used by tests and as a fallback.
// ---------------------------------------------------------------------------

// Convert a tightly-packed top-row-first BGRA8 image (stride = width*4) into a
// native-size RGB565 buffer, scaling to fit while preserving aspect ratio and
// letter-boxing with black. Output must hold kNativeWidth*kNativeHeight words.
// Rows are emitted in panel order for landscape orientation.
void ConvertBgraToNativeRgb565(const uint8_t* bgra, int width, int height,
                               uint16_t* out565);

}  // namespace s1

#endif  // S1_PROTOCOL_H
