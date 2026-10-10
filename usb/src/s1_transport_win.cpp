// s1_transport_win.cpp - Windows HID transport for the ACEMAGIC S1 LCD.
//
// Enumerates the HID class, selects the vendor-defined collection
// (VID 04D9 / PID FD01 / MI_01) by inspecting the device's hardware IDs, opens
// it and writes 4105-byte output reports.
//
// NOTE: This file targets Windows and is compiled with the Windows SDK. It is
// not compiled in the Linux build environment; see docs/build.md.
//
// License: GPL-3.0 (see LICENSE).
#include "s1_transport.h"

#if defined(_WIN32)

#include <setupapi.h>
#include <hidsdi.h>
#include <hidclass.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "s1_log.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace s1 {
namespace {

// Lower-case hex VID/PID substring as it appears in a hardware ID, e.g.
// "VID_04D9&PID_FD01".
std::string VidPidToken(uint16_t vid, uint16_t pid) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "VID_%04X&PID_%04X", vid, pid);
  return buf;
}

// "&MI_01" interface token.
std::string MiToken(uint8_t iface) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "&MI_%02X", iface);
  return buf;
}

bool ContainsNoCase(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  std::string h = hay, n = needle;
  for (char& c : h) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
  for (char& c : n) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
  return h.find(n) != std::string::npos;
}

}  // namespace

class WindowsHidTransport : public ITransport {
 public:
  ~WindowsHidTransport() override { Close(); }

  bool Open(const DeviceAddress& address, std::string* error) override {
    Close();
    address_ = address;

    std::string path;
    if (!address.devicePath.empty()) {
      path = address.devicePath;
    } else {
      path = FindDevicePath(address, error);
      if (path.empty()) return false;
    }

    // Open for write; share read so the OS/other tools can still query it.
    handle_ = CreateFileA(path.c_str(), GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, 0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      if (error) {
        *error = "CreateFile failed for " + path + ": " +
                 std::to_string(GetLastError()) +
                 " (is CYXTFTTool still holding the device?)";
      }
      handle_ = nullptr;
      return false;
    }

    // Prefer feeature-free output-report writes; query the output report length
    // so we can sanity-check our framing before the first write.
    HIDP_CAPS caps = {};
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(handle_, &preparsed)) {
      if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
        outputReportBytes_ = caps.OutputReportByteLength;
        S1_INFO("HID output report length = %u (we send %zu)",
                outputReportBytes_, kReportBytes);
      }
      HidD_FreePreparsedData(preparsed);
    }
    path_ = path;
    return true;
  }

  bool Write(const std::vector<uint8_t>& report, unsigned timeoutMs,
             std::string* error) override {
    if (!handle_) {
      if (error) *error = "device not open";
      return false;
    }
    // The HID class driver serializes writes; an overlapped write would allow a
    // timeout. We use overlapped I/O so a wedged device cannot hang the caller.
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
      if (error) *error = "CreateEvent failed";
      return false;
    }

    DWORD written = 0;
    const DWORD toWrite = static_cast<DWORD>(report.size());
    BOOL ok = WriteFile(handle_, report.data(), toWrite, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      const DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
      if (wait != WAIT_OBJECT_0) {
        CancelIo(handle_);
        CloseHandle(ov.hEvent);
        if (error) *error = "write timed out";
        return false;
      }
      ok = GetOverlappedResult(handle_, &ov, &written, FALSE);
    }
    CloseHandle(ov.hEvent);

    if (!ok) {
      if (error) *error = "WriteFile failed: " + std::to_string(GetLastError());
      return false;
    }
    if (written != toWrite) {
      // HID writes are all-or-nothing at the report boundary. A short write
      // means the framing does not match the device's report length.
      if (error) {
        *error = "short HID write: " + std::to_string(written) + "/" +
                 std::to_string(toWrite);
      }
      return false;
    }
    return true;
  }

  void Close() override {
    if (handle_) {
      CloseHandle(handle_);
      handle_ = nullptr;
    }
  }

  bool IsOpen() const override { return handle_ != nullptr; }

  std::string Describe() const override {
    return handle_ ? path_ : std::string("(closed)");
  }

 private:
  // Enumerate HID devices and return the path whose hardware IDs match
  // VID/PID and the requested interface.
  std::string FindDevicePath(const DeviceAddress& address, std::string* error) {
    const std::string vidpid = VidPidToken(address.vendorId, address.productId);
    const std::string mi = MiToken(address.interfaceNumber);

    HDEVINFO devs = SetupDiGetClassDevsA(
        &GUID_DEVINTERFACE_HID, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs == INVALID_HANDLE_VALUE) {
      if (error) *error = "SetupDiGetClassDevs failed";
      return {};
    }

    std::string found;
    for (DWORD i = 0; found.empty(); ++i) {
      SP_DEVINFO_DATA info = {};
      info.cbSize = sizeof(info);
      if (!SetupDiEnumDeviceInfo(devs, i, &info)) break;

      // Read the hardware IDs; the vendor collection contains both tokens.
      char hwids[1024] = {};
      if (!SetupDiGetDeviceRegistryPropertyA(
              devs, &info, SPDRP_HARDWAREID, nullptr,
              reinterpret_cast<PBYTE>(hwids), sizeof(hwids), nullptr)) {
        continue;
      }
      const std::string hwid(hwids);
      if (!ContainsNoCase(hwid, vidpid)) continue;
      if (!mi.empty() && !ContainsNoCase(hwid, mi)) continue;

      // Enumerate the device interfaces on this devnode and take the first HID
      // interface path.
      SP_DEVICE_INTERFACE_DATA ifData = {};
      ifData.cbSize = sizeof(ifData);
      for (DWORD j = 0;
           SetupDiEnumDeviceInterfaces(devs, &info, &GUID_DEVINTERFACE_HID, j,
                                       &ifData);
           ++j) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailA(devs, &ifData, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<uint8_t> buf(need);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (SetupDiGetDeviceInterfaceDetailA(devs, &ifData, detail, need, nullptr,
                                             nullptr)) {
          found = detail->DevicePath;
          S1_INFO("matched S1 vendor HID at %s (hwid=%s)", found.c_str(),
                  hwid.c_str());
          break;
        }
      }
    }
    SetupDiDestroyDeviceInfoList(devs);

    if (found.empty() && error) {
      *error = "no device matching " + vidpid + "/" + mi + " found";
    }
    return found;
  }

  HANDLE handle_ = nullptr;
  DeviceAddress address_;
  std::string path_;
  uint32_t outputReportBytes_ = 0;
};

std::unique_ptr<ITransport> CreatePlatformTransport() {
  return std::make_unique<WindowsHidTransport>();
}

}  // namespace s1

#endif  // _WIN32
