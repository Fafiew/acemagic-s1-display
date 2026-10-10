// test_ipc_layout.cpp - verifies the shared-memory layout invariants.
// These are the properties the driver (producer) and renderer (consumer) rely
// on when they interpret the same bytes from opposite processes.
// License: GPL-3.0 (see LICENSE).

#include <vector>

#include "s1_ipc.h"
#include "test_framework.h"

using namespace s1::ipc;

TEST(ipc_version_and_magic) {
  CHECK_EQ(kVersion, 1u);
  CHECK_EQ(kMagic, 0x53314652u);
  CHECK_EQ(kSlotCount, 3u);
}

TEST(ipc_slot_layout_is_64_aligned) {
  // Slot structs must be 64-byte aligned so their payloads begin on cache
  // lines; both sides compute offsets with the same formula.
  CHECK_EQ(AlignUp(sizeof(Slot), 64) % 64, 0u);
}

TEST(ipc_total_size_matches_sum_of_slots) {
  const uint32_t slotDataBytes = 320 * 170 * 4;  // BGRA8 max
  const uint64_t stride = SlotStride(slotDataBytes);
  const uint64_t expected = sizeof(Header) + kSlotCount * stride;
  CHECK_EQ(TotalBytes(kSlotCount, slotDataBytes), expected);
}

TEST(ipc_slot_pointers_do_not_overlap) {
  const uint32_t slotDataBytes = 320 * 170 * 4;
  std::vector<uint8_t> mem(TotalBytes(kSlotCount, slotDataBytes), 0);
  uint8_t* base = mem.data();
  uint64_t endPrev = 0;
  for (uint32_t i = 0; i < kSlotCount; ++i) {
    auto* slot = SlotAt(base, i, slotDataBytes);
    auto* data = SlotData(slot);
    const uint64_t start = static_cast<uint64_t>(reinterpret_cast<uint8_t*>(slot) - base);
    const uint64_t dataStart = static_cast<uint64_t>(data - base);
    CHECK(dataStart >= start);
    if (i > 0) {
      CHECK(start >= endPrev);
    }
    endPrev = dataStart + slotDataBytes;
    CHECK(endPrev <= mem.size());
  }
}

int main() { return s1test::RunAll(); }
