/*++

Module Name:

    Driver.h

Abstract:

    AcemagicS1DisplayDriver - an IddCx indirect display driver that exposes the
    ACEMAGIC S1 front LCD as a native, independently addressable extended Windows
    display.

    Adapted from Microsoft's IddSampleDriver
    (https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay),
    which is licensed under the MIT License. See LICENSE and docs/architecture.md.

    The driver never touches USB. It GPU-copies each acquired desktop frame into
    a staging surface, maps it, copies it into the shared-memory ring created by
    shared/include/s1_ipc.h, and publishes a sequence number. The user-session
    renderer consumes those frames and drives the hardware.

Environment:

    User Mode, UMDF. Requires IddCx 1.4 or later (Windows 10 1903+).

--*/

#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <memory>
#include <vector>

#include "s1_ipc_win.h"
#include "Trace.h"

namespace Microsoft {
namespace WRL {
namespace Wrappers {
typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
}
}  // namespace WRL
}  // namespace Microsoft

namespace AcemagicS1 {

// Reported mode table. All modes are exact integer multiples of the 320x170
// panel so scaling to the physical LCD stays clean.
struct S1MonitorMode {
  DWORD Width;
  DWORD Height;
  DWORD VSync;
};

constexpr DWORD kModeListSize = 3;
extern const S1MonitorMode kModeList[kModeListSize];
constexpr DWORD kPreferredModeIdx = 0;  // 320x170 @ 60

// Native panel geometry (must match shared/include/s1_protocol.h).
constexpr DWORD kNativeWidth = 320;
constexpr DWORD kNativeHeight = 170;

// Managed Direct3D render device.
struct Direct3DDevice {
  Direct3DDevice(LUID AdapterLuid);
  Direct3DDevice();
  HRESULT Init();

  LUID AdapterLuid;
  Microsoft::WRL::ComPtr<IDXGIFactory5> DxgiFactory;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
  Microsoft::WRL::ComPtr<ID3D11Device> Device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;
};

// Consumes buffers from the indirect display swap-chain, converts, and publishes
// them to shared memory.
class SwapChainProcessor {
 public:
  SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain,
                     std::shared_ptr<Direct3DDevice> Device,
                     HANDLE NewFrameEvent,
                     s1::ipc::FrameProducer* Producer,
                     DWORD Width,
                     DWORD Height);
  ~SwapChainProcessor();

 private:
  static DWORD CALLBACK RunThread(LPVOID Argument);
  void Run();
  void RunCore();

  // Copy the acquired D3D surface into `stage`, map it and publish to IPC.
  bool ProcessFrame(ID3D11Texture2D* Surface);

  IDDCX_SWAPCHAIN m_hSwapChain;
  std::shared_ptr<Direct3DDevice> m_Device;
  HANDLE m_hAvailableBufferEvent;
  Microsoft::WRL::Wrappers::Thread m_hThread;
  Microsoft::WRL::Wrappers::Event m_hTerminateEvent;

  s1::ipc::FrameProducer* m_Producer;
  DWORD m_Width;
  DWORD m_Height;

  // CPU-readable staging texture matching the swap-chain size.
  Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Stage;
  DWORD m_StageWidth = 0;
  DWORD m_StageHeight = 0;
};

// Adapter-level driver context.
class IndirectDeviceContext {
 public:
  IndirectDeviceContext(_In_ WDFDEVICE WdfDevice);
  virtual ~IndirectDeviceContext();

  void InitAdapter();
  void FinishInit(UINT ConnectorIndex);

 protected:
  WDFDEVICE m_WdfDevice;
  IDDCX_ADAPTER m_Adapter;
};

// Per-monitor context: owns the IPC producer and the processing thread.
class IndirectMonitorContext {
 public:
  IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor);
  virtual ~IndirectMonitorContext();

  void AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter,
                       HANDLE NewFrameEvent);
  void UnassignSwapChain();

  s1::ipc::FrameProducer& producer() { return m_producer; }

 private:
  IDDCX_MONITOR m_Monitor;
  std::unique_ptr<SwapChainProcessor> m_ProcessingThread;
  s1::ipc::FrameProducer m_producer;
  DWORD m_CurrentWidth = kNativeWidth;
  DWORD m_CurrentHeight = kNativeHeight;
};

}  // namespace AcemagicS1
