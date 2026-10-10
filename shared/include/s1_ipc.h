// s1_ipc.h - shared-memory frame transport between the indirect display driver
// (producer) and the user-mode renderer (consumer).
//
// The IddCx driver runs in Session 0; the renderer and control app run in the
// interactive session. Named objects therefore live in the "Global\" namespace
// and the mapping is created with a DACL that grants the interactive user
// read/write access. See docs/architecture.md, "Interprocess communication".
//
// Design goals: bounded memory, no unbounded queue growth, no tearing, and
// "latest frame wins" dropping so a slow USB transport never backs up into
// Windows' display pipeline. A seqlock-style publish counter is used: the
// producer writes a slot, then publishes a monotonically increasing sequence;
// the consumer re-checks the sequence after copying and retries if it changed.
//
// License: GPL-3.0 (see LICENSE).

#ifndef S1_IPC_H
#define S1_IPC_H

#include <cstdint>

namespace s1 {
namespace ipc {

// Bump when the layout below changes in an incompatible way. Producers and
// consumers refuse to attach to a mapping whose version they do not know.
constexpr uint32_t kVersion = 1;
constexpr uint32_t kMagic = 0x53314652u;  // 'S1FR'

// Names in the Global namespace so Session 0 (driver) and the interactive
// session (renderer / control app) can share them.
constexpr const char* kMappingName = "Global\\AcemagicS1FrameBuffer_v1";
constexpr const char* kFrameReadyEventName = "Global\\AcemagicS1FrameReady_v1";
constexpr const char* kRendererExitEventName = "Global\\AcemagicS1RendererExit_v1";

// Number of frame slots in the ring. Three is enough for the producer to never
// block while the consumer converts/sends: the consumer always reads the most
// recently published slot, and older slots are simply overwritten.
constexpr uint32_t kSlotCount = 3;

// Pixel formats the producer may publish.
enum class PixelFormat : uint32_t {
  Unknown = 0,
  Bgra8 = 1,  // 32-bit BGRA, top row first
};

// Slot states.
enum class SlotState : uint32_t {
  Empty = 0,
  Writing = 1,
  Ready = 2,
};

#pragma pack(push, 8)

// Fixed header at the start of the mapping. All counters are accessed with
// aligned 32/64-bit operations which are atomic on x86/x64; no locks are used
// so a stalled consumer can never stall the driver's callback thread.
struct Header {
  uint32_t magic;         // kMagic
  uint32_t version;       // kVersion
  uint32_t headerBytes;   // sizeof(Header)
  uint32_t slotCount;     // kSlotCount
  uint32_t slotDataBytes; // per-slot capacity (>= width*height*4)
  uint32_t width;         // current frame width
  uint32_t height;        // current frame height
  uint32_t stride;        // bytes per row
  uint32_t pixelFormat;   // PixelFormat
  uint32_t reserved0;

  // Producer side.
  volatile int64_t publishedSeq;  // sequence of the newest fully-written frame
  volatile int64_t framesProduced;
  volatile int64_t framesDropped;  // produced while consumer was behind
  volatile int32_t producerPid;
  volatile int32_t producerAlive;

  // Consumer side.
  volatile int64_t consumedSeq;  // sequence of the last consumed frame
  volatile int64_t framesConsumed;
  volatile int32_t consumerPid;
  volatile int32_t consumerAlive;

  volatile int64_t lastProduceQpc;  // QPC ticks of last publish
  volatile int64_t lastConsumeQpc;  // QPC ticks of last consume
};

// One frame slot. `seq` is written last (with a release barrier) so a consumer
// that observes a changed seq knows the payload is complete.
struct Slot {
  volatile int64_t seq;
  uint32_t state;       // SlotState
  uint32_t reserved0;
  int64_t timestampQpc;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t pixelFormat;
  uint64_t reserved1;
  // Payload follows immediately after this struct.
};

#pragma pack(pop)

constexpr uint32_t AlignUp(uint32_t x, uint32_t a) {
  return (x + a - 1) & ~(a - 1);
}

// Total mapping size for given parameters.
inline uint64_t TotalBytes(uint32_t slotCount, uint32_t slotDataBytes) {
  const uint32_t slotHeaderBytes = AlignUp(sizeof(Slot), 64);
  const uint32_t slotBytes = slotHeaderBytes + AlignUp(slotDataBytes, 64);
  return sizeof(Header) + static_cast<uint64_t>(slotCount) * slotBytes;
}

inline uint64_t SlotStride(uint32_t slotDataBytes) {
  const uint32_t slotHeaderBytes = AlignUp(sizeof(Slot), 64);
  return slotHeaderBytes + AlignUp(slotDataBytes, 64);
}

inline Slot* SlotAt(uint8_t* base, uint32_t index, uint32_t slotDataBytes) {
  return reinterpret_cast<Slot*>(base + sizeof(Header) +
                                 static_cast<uint64_t>(index) * SlotStride(slotDataBytes));
}

inline uint8_t* SlotData(Slot* slot) {
  return reinterpret_cast<uint8_t*>(slot) + AlignUp(sizeof(Slot), 64);
}

}  // namespace ipc
}  // namespace s1

#endif  // S1_IPC_H
