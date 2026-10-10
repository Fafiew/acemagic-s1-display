// s1_transport.h - transport abstraction for the ACEMAGIC S1 vendor HID device.
//
// The protocol engine (S1Device) is written entirely against this interface so
// that it can be exercised with an in-memory mock on any host, and with the
// real Windows HID stack on the target machine. See docs/architecture.md,
// "Component C".
//
// License: GPL-3.0 (see LICENSE).
#ifndef S1_TRANSPORT_H
#define S1_TRANSPORT_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s1 {

// Describes which device to open. Values are the verified identifiers; see
// docs/hardware-protocol.md.
struct DeviceAddress {
  uint16_t vendorId = 0x04D9;
  uint16_t productId = 0xFD01;
  uint8_t interfaceNumber = 0x01;  // MI_01, the vendor-defined collection
  // Optional explicit device path; when non-empty it is used verbatim on
  // Windows (useful when several S1 units are attached and a specific instance
  // must be driven).
  std::string devicePath;
};

class ITransport {
 public:
  virtual ~ITransport() = default;

  // Open the device. Returns false and sets *error on failure.
  virtual bool Open(const DeviceAddress& address, std::string* error) = 0;

  // Send one complete HID output report (the full 4105-byte buffer). Must handle
  // partial writes internally. Returns false and sets *error on failure.
  virtual bool Write(const std::vector<uint8_t>& report, unsigned timeoutMs,
                     std::string* error) = 0;

  // Release the device handle. Safe to call when not open.
  virtual void Close() = 0;

  virtual bool IsOpen() const = 0;

  // Human-readable description of the open (or last-attempted) device, for logs.
  virtual std::string Describe() const = 0;
};

// ---------------------------------------------------------------------------
// Factory: returns the platform transport. On Windows this is the HID
// implementation; elsewhere it returns nullptr (callers then supply a mock).
// ---------------------------------------------------------------------------
std::unique_ptr<ITransport> CreatePlatformTransport();

// ---------------------------------------------------------------------------
// MockTransport: records every report written. Used by unit tests and by the
// capture/replay tooling. It can be configured to simulate failures.
// ---------------------------------------------------------------------------
class MockTransport : public ITransport {
 public:
  MockTransport() = default;

  bool Open(const DeviceAddress& address, std::string* error) override;
  bool Write(const std::vector<uint8_t>& report, unsigned timeoutMs,
             std::string* error) override;
  void Close() override;
  bool IsOpen() const override { return open_; }
  std::string Describe() const override { return "mock"; }

  // Configuration for fault injection.
  void set_fail_open(bool v) { failOpen_ = v; }
  void set_fail_after(int n) { failAfter_ = n; }  // fail the Nth write (0-based)
  void set_max_report_bytes(size_t n) { maxReportBytes_ = n; }

  const std::vector<std::vector<uint8_t>>& reports() const { return reports_; }
  void ClearReports() { reports_.clear(); }
  const DeviceAddress& opened_address() const { return address_; }
  int write_count() const { return static_cast<int>(reports_.size()); }

 private:
  bool open_ = false;
  bool failOpen_ = false;
  int failAfter_ = -1;
  size_t maxReportBytes_ = static_cast<size_t>(-1);
  DeviceAddress address_;
  std::vector<std::vector<uint8_t>> reports_;
};

}  // namespace s1

#endif  // S1_TRANSPORT_H
