// s1_transport_platform_none.cpp - non-Windows platform stub.
//
// Defines CreatePlatformTransport() on platforms that have no hardware
// transport, so that portable tools link. It always returns nullptr; callers
// fall back to the mock transport. License: GPL-3.0 (see LICENSE).

#include "s1_transport.h"

#if !defined(_WIN32)

#include <memory>

namespace s1 {
std::unique_ptr<ITransport> CreatePlatformTransport() { return nullptr; }
}  // namespace s1

#endif  // !_WIN32
