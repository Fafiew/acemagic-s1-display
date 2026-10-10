// shm_frame_source.cpp - Windows shared-memory frame source.
// License: GPL-3.0 (see LICENSE).

#include "shm_frame_source.h"

#if defined(_WIN32)

#include "s1_log.h"

namespace s1 {

bool ShmFrameSource::Attach(std::string* error) {
  if (attached_) return true;
  if (!consumer_.Attach(error)) return false;
  attached_ = true;
  S1_INFO("renderer attached to driver frame buffer");
  return true;
}

void ShmFrameSource::Detach() {
  if (!attached_) return;
  consumer_.SetAlive(false);
  // FrameConsumer destructor unmaps; re-create by assigning a fresh one.
  consumer_ = s1::ipc::FrameConsumer();
  attached_ = false;
}

bool ShmFrameSource::GetLatest(Frame* out) {
  if (!attached_) return false;
  uint8_t* pixels = nullptr;
  uint32_t w = 0, h = 0, stride = 0;
  int64_t seq = 0, ts = 0;
  if (!consumer_.GetLatest(&pixels, &w, &h, &stride, &seq, &ts)) {
    return false;
  }
  out->data = pixels;
  out->width = w;
  out->height = h;
  out->stride = stride;
  out->seq = seq;
  out->timestampQpc = ts;
  return true;
}

void ShmFrameSource::Release(const Frame&) {
  // Nothing to do with a fixed ring: the producer overwrites the oldest slot.
}

}  // namespace s1

#endif  // _WIN32
