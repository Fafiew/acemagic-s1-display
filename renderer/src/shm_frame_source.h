// shm_frame_source.h - IFrameSource backed by the driver's shared memory.
// Windows only. License: GPL-3.0 (see LICENSE).
#ifndef S1_SHM_FRAME_SOURCE_H
#define S1_SHM_FRAME_SOURCE_H

#include "frame_pipeline.h"

#if defined(_WIN32)

#include <string>

#include "s1_ipc_win.h"

namespace s1 {

class ShmFrameSource : public IFrameSource {
 public:
  // Attach to the mapping created by the driver. Returns false if the driver is
  // not running yet.
  bool Attach(std::string* error);
  void Detach();
  bool attached() const { return attached_; }

  HANDLE frame_ready_event() const { return consumer_.frame_ready_event(); }

  bool GetLatest(Frame* out) override;
  void Release(const Frame& frame) override;

  s1::ipc::FrameConsumer& consumer() { return consumer_; }

 private:
  s1::ipc::FrameConsumer consumer_;
  bool attached_ = false;
};

}  // namespace s1

#endif  // _WIN32
#endif  // S1_SHM_FRAME_SOURCE_H
