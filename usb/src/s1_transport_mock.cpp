// s1_transport_mock.cpp - in-memory transport used by tests and replay tools.
// License: GPL-3.0 (see LICENSE).

#include "s1_transport.h"

namespace s1 {

bool MockTransport::Open(const DeviceAddress& address, std::string* error) {
  address_ = address;
  if (failOpen_) {
    if (error) *error = "mock: open refused";
    open_ = false;
    return false;
  }
  open_ = true;
  return true;
}

bool MockTransport::Write(const std::vector<uint8_t>& report,
                          unsigned /*timeoutMs*/, std::string* error) {
  if (!open_) {
    if (error) *error = "mock: not open";
    return false;
  }
  if (failAfter_ >= 0 && write_count() == failAfter_) {
    if (error) *error = "mock: injected write failure";
    return false;
  }
  if (report.size() > maxReportBytes_) {
    if (error) *error = "mock: report too large";
    return false;
  }
  reports_.push_back(report);
  return true;
}

void MockTransport::Close() { open_ = false; }

}  // namespace s1
