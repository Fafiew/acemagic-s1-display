// s1_ipc_win.cpp - Windows shared-memory frame transport.
// License: GPL-3.0 (see LICENSE).

#include "s1_ipc_win.h"

#if defined(_WIN32)

#include <aclapi.h>

#include <cstring>

#include "s1_log.h"

namespace s1 {
namespace ipc {

PSECURITY_DESCRIPTOR BuildGlobalSecurityDescriptor() {
  // Grant: SYSTEM (full), Administrators (full), Users (read/write/sync).
  // SIDs are built with CreateWellKnownSid so no external header is needed.
  BYTE systemSid[SECURITY_MAX_SID_SIZE];
  DWORD systemSidSize = sizeof(systemSid);
  BYTE adminSid[SECURITY_MAX_SID_SIZE];
  DWORD adminSidSize = sizeof(adminSid);
  BYTE usersSid[SECURITY_MAX_SID_SIZE];
  DWORD usersSidSize = sizeof(usersSid);
  if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &systemSidSize) ||
      !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid,
                          &adminSidSize) ||
      !CreateWellKnownSid(WinBuiltinUsersSid, nullptr, usersSid, &usersSidSize)) {
    return nullptr;
  }

  EXPLICIT_ACCESS_W ea[3] = {};
  auto set = [](EXPLICIT_ACCESS_W& e, PSID sid, DWORD access) {
    e.grfAccessPermissions = access;
    e.grfAccessMode = SET_ACCESS;
    e.grfInheritance = NO_INHERITANCE;
    e.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    e.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    e.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
  };
  set(ea[0], systemSid, GENERIC_ALL);
  set(ea[1], adminSid, GENERIC_ALL);
  set(ea[2], usersSid, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE);

  PSECURITY_DESCRIPTOR sd = nullptr;
  if (SetEntriesInAclW(3, ea, nullptr, &sd) != ERROR_SUCCESS) {
    return nullptr;
  }
  return sd;
}

// ---------------------------------------------------------------------------
// FrameProducer
// ---------------------------------------------------------------------------

FrameProducer::~FrameProducer() {
  if (header_) {
    InterlockedExchange(&header_->producerAlive, 0);
  }
  if (base_) UnmapViewOfFile(base_);
  if (mapping_) CloseHandle(mapping_);
  if (frameReady_) CloseHandle(frameReady_);
}

bool FrameProducer::Create(uint32_t width, uint32_t height,
                           uint32_t slotDataBytes, std::string* error) {
  slotDataBytes_ = slotDataBytes;
  const uint64_t total = TotalBytes(kSlotCount, slotDataBytes);

  PSECURITY_DESCRIPTOR sd = BuildGlobalSecurityDescriptor();
  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = sd;  // may be null: then default DACL applies
  sa.bInheritHandle = FALSE;

  const DWORD hi = static_cast<DWORD>(total >> 32);
  const DWORD lo = static_cast<DWORD>(total & 0xFFFFFFFF);
  mapping_ = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, hi,
                                lo, kMappingName);
  if (sd) LocalFree(sd);
  if (!mapping_) {
    if (error) *error = "CreateFileMapping failed: " + std::to_string(GetLastError());
    return false;
  }

  base_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
  if (!base_) {
    if (error) *error = "MapViewOfFile failed: " + std::to_string(GetLastError());
    return false;
  }

  // Reset header (a previous process may have left state).
  std::memset(base_, 0, static_cast<size_t>(total));
  header_ = reinterpret_cast<Header*>(base_);
  header_->magic = kMagic;
  header_->version = kVersion;
  header_->headerBytes = sizeof(Header);
  header_->slotCount = kSlotCount;
  header_->slotDataBytes = slotDataBytes;
  header_->width = width;
  header_->height = height;
  header_->stride = width * 4;
  header_->pixelFormat = static_cast<uint32_t>(PixelFormat::Bgra8);
  header_->producerAlive = 1;
  header_->producerPid = static_cast<int32_t>(GetCurrentProcessId());

  frameReady_ = CreateEventA(&sa, FALSE, FALSE, kFrameReadyEventName);
  if (!frameReady_) {
    if (error) *error = "CreateEvent failed: " + std::to_string(GetLastError());
    return false;
  }
  return true;
}

void FrameProducer::SetAlive(bool alive) {
  if (header_) InterlockedExchange(&header_->producerAlive, alive ? 1 : 0);
}

uint8_t* FrameProducer::BeginWrite(uint32_t width, uint32_t height,
                                   uint32_t stride, int64_t timestampQpc) {
  if (!base_ || !header_) return nullptr;
  const uint32_t index = nextSlot_ % kSlotCount;
  Slot* slot = SlotAt(base_, index, slotDataBytes_);
  InterlockedExchange(&slot->state, static_cast<LONG>(SlotState::Writing));
  slot->width = width;
  slot->height = height;
  slot->stride = stride;
  slot->pixelFormat = static_cast<uint32_t>(PixelFormat::Bgra8);
  slot->timestampQpc = timestampQpc;
  writingSeq_ = InterlockedIncrement64(&header_->framesProduced);
  nextSlot_ = (index + 1) % kSlotCount;
  return SlotData(slot);
}

void FrameProducer::Publish() {
  if (!base_ || !header_) return;
  const uint32_t index = (nextSlot_ + kSlotCount - 1) % kSlotCount;
  Slot* slot = SlotAt(base_, index, slotDataBytes_);
  // Release: ensure pixels are visible before the sequence is published.
  InterlockedExchange64(&slot->seq, writingSeq_);
  MemoryBarrier();
  InterlockedExchange(&slot->state, static_cast<LONG>(SlotState::Ready));
  InterlockedExchange64(&header_->publishedSeq, writingSeq_);
  header_->lastProduceQpc = GetTickCount64();
  if (frameReady_) SetEvent(frameReady_);
}

// ---------------------------------------------------------------------------
// FrameConsumer
// ---------------------------------------------------------------------------

FrameConsumer::~FrameConsumer() {
  if (header_) InterlockedExchange(&header_->consumerAlive, 0);
  if (base_) UnmapViewOfFile(base_);
  if (mapping_) CloseHandle(mapping_);
  if (frameReady_) CloseHandle(frameReady_);
}

bool FrameConsumer::Attach(std::string* error) {
  mapping_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kMappingName);
  if (!mapping_) {
    if (error) *error = "OpenFileMapping failed: " + std::to_string(GetLastError());
    return false;
  }
  base_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
  if (!base_) {
    if (error) *error = "MapViewOfFile failed: " + std::to_string(GetLastError());
    return false;
  }
  header_ = reinterpret_cast<Header*>(base_);
  if (header_->magic != kMagic || header_->version != kVersion) {
    if (error) *error = "IPC version/magic mismatch";
    return false;
  }
  frameReady_ = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
                           kFrameReadyEventName);
  header_->consumerPid = static_cast<int32_t>(GetCurrentProcessId());
  header_->consumerAlive = 1;
  return true;
}

void FrameConsumer::SetAlive(bool alive) {
  if (header_) InterlockedExchange(&header_->consumerAlive, alive ? 1 : 0);
}
void FrameConsumer::SetPid(int32_t pid) {
  if (header_) header_->consumerPid = pid;
}

bool FrameConsumer::GetLatest(uint8_t** pixels, uint32_t* width,
                              uint32_t* height, uint32_t* stride, int64_t* seq,
                              int64_t* timestampQpc) {
  if (!base_ || !header_) return false;
  const int64_t newest = InterlockedCompareExchange64(&header_->publishedSeq, 0, 0);
  if (newest == lastSeq_ || newest <= 0) return false;

  const uint32_t index =
      static_cast<uint32_t>((newest - 1) % static_cast<int64_t>(kSlotCount));
  Slot* slot = SlotAt(base_, index, header_->slotDataBytes);

  // Seqlock read: read seq before and after copying to detect a concurrent write.
  const int64_t s0 = InterlockedCompareExchange64(&slot->seq, 0, 0);
  if (s0 != newest) return false;
  *width = slot->width;
  *height = slot->height;
  *stride = slot->stride;
  *timestampQpc = slot->timestampQpc;
  *pixels = SlotData(slot);
  MemoryBarrier();
  const int64_t s1 = InterlockedCompareExchange64(&slot->seq, 0, 0);
  if (s1 != s0) {
    // Raced with a newer write in this slot; skip this round.
    return false;
  }

  *seq = newest;
  lastSeq_ = newest;
  InterlockedExchange64(&header_->consumedSeq, newest);
  InterlockedIncrement64(&header_->framesConsumed);
  header_->lastConsumeQpc = GetTickCount64();
  return true;
}

}  // namespace ipc
}  // namespace s1

#endif  // _WIN32
