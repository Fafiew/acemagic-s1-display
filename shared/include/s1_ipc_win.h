// s1_ipc_win.h - Windows implementation of the shared-memory frame transport.
//
// Producer side runs in the Session 0 IddCx driver; consumer side runs in the
// interactive-session renderer. Both use the Global\ object names and the
// versioned layout from s1_ipc.h.
//
// License: GPL-3.0 (see LICENSE).
#ifndef S1_IPC_WIN_H
#define S1_IPC_WIN_H

#if defined(_WIN32)

#include <windows.h>

#include <cstdint>
#include <string>

#include "s1_ipc.h"

namespace s1 {
namespace ipc {

// Builds a SECURITY_DESCRIPTOR granting read/write to the interactive user,
// SYSTEM and Administrators, for the Global\ named objects. Caller must
// LocalFree the returned pointer.
PSECURITY_DESCRIPTOR BuildGlobalSecurityDescriptor();

// ---------------------------------------------------------------------------
// Producer (driver side).
// ---------------------------------------------------------------------------
class FrameProducer {
 public:
  FrameProducer() = default;
  ~FrameProducer();

  // Create the mapping and the frame-ready event. `slotDataBytes` must be at
  // least width*height*4.
  bool Create(uint32_t width, uint32_t height, uint32_t slotDataBytes,
              std::string* error);

  // Mark the producer as alive/not-alive (used by the control app).
  void SetAlive(bool alive);

  // Begin writing the next slot. Returns the destination pixel buffer and the
  // slot stride. Fills slot metadata. Returns nullptr on failure.
  uint8_t* BeginWrite(uint32_t width, uint32_t height, uint32_t stride,
                      int64_t timestampQpc);

  // Publish the slot written by BeginWrite: bumps publishedSeq with release
  // ordering and signals the frame-ready event.
  void Publish();

  // Record a dropped frame (produced but not consumed in time).
  void NoteDropped() { if (header_) InterlockedIncrement64(&header_->framesDropped); }

  const Header* header() const { return header_; }

 private:
  HANDLE mapping_ = nullptr;
  HANDLE frameReady_ = nullptr;
  Header* header_ = nullptr;
  uint8_t* base_ = nullptr;
  uint32_t slotDataBytes_ = 0;
  uint32_t nextSlot_ = 0;
  int64_t writingSeq_ = 0;
};

// ---------------------------------------------------------------------------
// Consumer (renderer side).
// ---------------------------------------------------------------------------
class FrameConsumer {
 public:
  FrameConsumer() = default;
  ~FrameConsumer();

  // Attach to an existing mapping. Returns false if it does not exist or a
  // version/magic mismatch is found.
  bool Attach(std::string* error);

  void SetAlive(bool alive);
  void SetPid(int32_t pid);

  // Get the newest frame if its sequence differs from the last returned one.
  // The pixels remain valid until the next call to GetLatest.
  bool GetLatest(uint8_t** pixels, uint32_t* width, uint32_t* height,
                 uint32_t* stride, int64_t* seq, int64_t* timestampQpc);

  HANDLE frame_ready_event() const { return frameReady_; }
  const Header* header() const { return header_; }

 private:
  HANDLE mapping_ = nullptr;
  HANDLE frameReady_ = nullptr;
  Header* header_ = nullptr;
  uint8_t* base_ = nullptr;
  int64_t lastSeq_ = -1;
};

}  // namespace ipc
}  // namespace s1

#endif  // _WIN32
#endif  // S1_IPC_WIN_H
