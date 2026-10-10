// test_protocol.cpp - host-runnable tests for the S1 wire protocol.
//
// These assertions pin the exact bytes documented in
// docs/hardware-protocol.md, which were derived from the Linux implementation
// and the captured traffic shown in the upstream README.
//
// License: GPL-3.0 (see LICENSE).

#include "s1_protocol.h"
#include "test_framework.h"

using namespace s1;

TEST(framing_constants) {
  CHECK_EQ(kHeaderBytes, 8u);
  CHECK_EQ(kDataBytes, 4096u);
  CHECK_EQ(kBufferBytes, 4104u);
  CHECK_EQ(kReportBytes, 4105u);
  CHECK_EQ(kRedrawChunkCount, 27u);
  CHECK_EQ(kRedrawFinalChunkBytes, 2304u);
  CHECK_EQ(kFramebufferBytes, 108800u);
}

TEST(endian_swap) {
  CHECK_EQ(SwapEndian16(0x1234), 0x3412);
  CHECK_EQ(SwapEndian16(0xF800), 0x00F8);  // pure red in RGB565
}

TEST(rgb565_packing) {
  CHECK_EQ(PackRgb565(255, 0, 0), 0xF800);
  CHECK_EQ(PackRgb565(0, 255, 0), 0x07E0);
  CHECK_EQ(PackRgb565(0, 0, 255), 0x001F);
  CHECK_EQ(PackRgb565(0, 0, 0), 0x0000);
  // Wire variants are byte-swapped.
  CHECK_EQ(PackRgb565Wire(255, 0, 0), 0x00F8);  // swapped; stores as bytes F8 00
}

TEST(orientation_landscape_matches_capture) {
  // Captured: 55 a1 f1 02 ... is portrait; landscape is 0x01.
  Orientation o;
  o.portrait = false;
  auto r = BuildOrientationReport(o);
  CHECK_EQ(r.size(), kReportBytes);
  CHECK_EQ(r[0], 0x00);  // report ID
  CHECK_EQ(r[1], 0x55);  // signature
  CHECK_EQ(r[2], 0xA1);  // config
  CHECK_EQ(r[3], 0xF1);  // orientation sub-command
  CHECK_EQ(r[4], 0x01);  // landscape
}

TEST(orientation_portrait_matches_capture) {
  // Captured header: 55 a1 f1 02 00 00 00
  Orientation o;
  o.portrait = true;
  auto r = BuildOrientationReport(o);
  CHECK_EQ(r[3], 0xF1);
  CHECK_EQ(r[4], 0x02);  // portrait
}

TEST(heartbeat_hms_in_header) {
  // Captured: 55 a1 f2 0e 1c 2d (14:28:45)
  Heartbeat hb{0x0E, 0x1C, 0x2D};
  auto r = BuildHeartbeatReport(hb);
  CHECK_EQ(r[1], 0x55);
  CHECK_EQ(r[2], 0xA1);
  CHECK_EQ(r[3], 0xF2);
  CHECK_EQ(r[4], 0x0E);
  CHECK_EQ(r[5], 0x1C);
  CHECK_EQ(r[6], 0x2D);
}

TEST(redraw_first_chunk_header) {
  // Captured chunk 0 header: 55 a3 f0 01 00 00 00 10
  // seq=1, offset=0x0000, length=0x1000 (4096)
  auto r = BuildRedrawChunkReport(0);
  CHECK_EQ(r[1], 0x55);
  CHECK_EQ(r[2], 0xA3);
  CHECK_EQ(r[3], 0xF0);  // start
  CHECK_EQ(r[4], 0x01);  // sequence 1
  CHECK_EQ(r[5], 0x00);  // offset lo
  CHECK_EQ(r[6], 0x00);  // offset hi
  CHECK_EQ(r[7], 0x00);  // length lo = 0x00
  CHECK_EQ(r[8], 0x10);  // length hi = 0x10 -> 0x1000
}

TEST(redraw_middle_chunk_offset_and_length) {
  // Captured chunk 12: 55 a3 f1 0d 00 c0 00 10
  // seq=13, offset=0xC000, length=0x1000
  auto r12 = BuildRedrawChunkReport(12);
  CHECK_EQ(r12[3], 0xF1);  // continue
  CHECK_EQ(r12[4], 0x0D);  // sequence 13
  CHECK_EQ(r12[5], 0x00);  // 0xC000 lo
  CHECK_EQ(r12[6], 0xC0);  // 0xC000 hi
  CHECK_EQ(r12[7], 0x00);  // len lo
  CHECK_EQ(r12[8], 0x10);  // len hi
}

TEST(redraw_wrapped_chunk_offset) {
  // Captured chunk 16: 55 a3 f1 11 00 00 00 10
  // seq=17, offset wraps to 0x0000 (firmware must track by sequence)
  auto r16 = BuildRedrawChunkReport(16);
  CHECK_EQ(r16[4], 0x11);  // sequence 17
  CHECK_EQ(r16[5], 0x00);  // offset low byte wrapped to 0
  CHECK_EQ(r16[6], 0x00);  // offset high byte wrapped to 0
}

TEST(redraw_final_chunk_is_end_2304) {
  // Captured final chunk: 55 a3 f2 1b 00 a0 00 09
  // seq=27, offset=0xA000 (26*4096=106496=0x1A000 -> 16-bit 0xA000),
  // length=0x0900 = 2304
  auto r = BuildRedrawChunkReport(kRedrawChunkCount - 1);
  CHECK_EQ(r[3], 0xF2);  // end
  CHECK_EQ(r[4], 0x1B);  // sequence 27
  CHECK_EQ(r[5], 0x00);  // 0xA000 lo
  CHECK_EQ(r[6], 0xA0);  // 0xA000 hi
  CHECK_EQ(r[7], 0x00);  // 2304 lo
  CHECK_EQ(r[8], 0x09);  // 2304 hi
}

TEST(redraw_payload_roundtrip_first_pixel) {
  // Logical RGB565 red = 0xF800. The wire order is big-endian (the Linux
  // implementation writes with DataView.setUint16 default = big-endian, and
  // per-pixel SWAPENDIAN), so the bytes are F8 00.
  std::vector<uint16_t> frame(kNativeWidth * kNativeHeight, 0);
  frame[0] = PackRgb565(255, 0, 0);  // 0xF800
  auto r = BuildRedrawChunkReport(0);
  EncodeRedrawChunkPayload(r, 0, frame.data());
  const size_t base = kReportIdBytes + kHeaderBytes;
  CHECK_EQ(r[base + 0], 0xF8);
  CHECK_EQ(r[base + 1], 0x00);
}

TEST(redraw_payload_final_chunk_length) {
  std::vector<uint16_t> frame(kNativeWidth * kNativeHeight, 0xFFFF);
  auto r = BuildRedrawChunkReport(kRedrawChunkCount - 1);
  EncodeRedrawChunkPayload(r, kRedrawChunkCount - 1, frame.data());
  // The final chunk writes exactly 2304/2 = 1152 pixels into the data area.
  const size_t base = kReportIdBytes + kHeaderBytes;
  CHECK_EQ(r[base + 2 * (1152 - 1)], 0xFF);  // last written pixel lo
  CHECK_EQ(r[base + 2 * (1152 - 1) + 1], 0xFF);
  CHECK_EQ(r[base + 2 * 1152], 0x00);  // one past the end stays zero
}

TEST(refresh_header_fields) {
  // Captured: 55 a2 12 00 32 00 18 0c  (x=18,y=50,w=24,h=12)
  Region r{18, 50, 24, 12};
  auto rep = BuildRefreshHeaderReport(r);
  CHECK_EQ(rep[1], 0x55);
  CHECK_EQ(rep[2], 0xA2);
  CHECK_EQ(rep[3], 0x12);  // x lo
  CHECK_EQ(rep[4], 0x00);  // x hi
  CHECK_EQ(rep[5], 0x32);  // y lo
  CHECK_EQ(rep[6], 0x00);  // y hi
  CHECK_EQ(rep[7], 0x18);  // width
  CHECK_EQ(rep[8], 0x0C);  // height
}

TEST(bgra_to_native_identity_size) {
  // A 320x170 source maps 1:1 and must not be letterboxed.
  const int w = kNativeWidth, h = kNativeHeight;
  std::vector<uint8_t> bgra(w * h * 4, 0);
  // Set pixel (0,0) to red (BGRA order).
  bgra[0] = 0; bgra[1] = 0; bgra[2] = 255; bgra[3] = 255;
  std::vector<uint16_t> out(kNativeWidth * kNativeHeight, 0);
  ConvertBgraToNativeRgb565(bgra.data(), w, h, out.data());
  CHECK_EQ(out[0], PackRgb565(255, 0, 0));
}

TEST(bgra_to_native_letterbox) {
  // A 640x170 source scales to 320x85 and is centred vertically (black bars).
  const int w = 640, h = 170;
  std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4, 0);
  std::vector<uint16_t> out(kNativeWidth * kNativeHeight, 0xFFFF);
  ConvertBgraToNativeRgb565(bgra.data(), w, h, out.data());
  // Row 0 must be background black.
  CHECK_EQ(out[0], 0x0000);
}

int main() { return s1test::RunAll(); }
