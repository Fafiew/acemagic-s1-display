/*++

Module Name:

    Driver.cpp

Abstract:

    AcemagicS1DisplayDriver - IddCx indirect display driver exposing the
    ACEMAGIC S1 front LCD as an extended Windows display.

    Derived from Microsoft's IddSampleDriver (MIT). See Driver.h for the design
    overview and docs/architecture.md for the full data flow.

Environment:

    User Mode, UMDF.

--*/

#include "Driver.h"

#include <d3d11.h>
#include <string>

using namespace std;
using namespace Microsoft::WRL;
using namespace AcemagicS1;

// ---------------------------------------------------------------------------
// Reported modes.
// ---------------------------------------------------------------------------
namespace AcemagicS1 {
const S1MonitorMode kModeList[kModeListSize] = {
    {320, 170, 60},  // native, preferred
    {640, 340, 60},
    {960, 510, 60},
};
}  // namespace AcemagicS1

namespace {

// Fill a DISPLAYCONFIG_VIDEO_SIGNAL_INFO for an IDD mode.
void FillSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& Mode, DWORD Width,
                    DWORD Height, DWORD VSync, bool bMonitorMode) {
  Mode.totalSize.cx = Mode.activeSize.cx = Width;
  Mode.totalSize.cy = Mode.activeSize.cy = Height;
  Mode.AdditionalSignalInfo.vSyncFreqDivider = bMonitorMode ? 0 : 1;
  Mode.AdditionalSignalInfo.videoStandard = 255;
  Mode.vSyncFreq.Numerator = VSync;
  Mode.vSyncFreq.Denominator = 1;
  Mode.hSyncFreq.Numerator = VSync * Height;
  Mode.hSyncFreq.Denominator = 1;
  Mode.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
  Mode.pixelRate = ((UINT64)VSync) * ((UINT64)Width) * ((UINT64)Height);
}

IDDCX_MONITOR_MODE CreateMonitorMode(DWORD Width, DWORD Height, DWORD VSync,
                                     IDDCX_MONITOR_MODE_ORIGIN Origin) {
  IDDCX_MONITOR_MODE Mode = {};
  Mode.Size = sizeof(Mode);
  Mode.Origin = Origin;
  FillSignalInfo(Mode.MonitorVideoSignalInfo, Width, Height, VSync, true);
  return Mode;
}

IDDCX_TARGET_MODE CreateTargetMode(DWORD Width, DWORD Height, DWORD VSync) {
  IDDCX_TARGET_MODE Mode = {};
  Mode.Size = sizeof(Mode);
  FillSignalInfo(Mode.TargetVideoSignalInfo.targetVideoSignalInfo, Width,
                 Height, VSync, false);
  return Mode;
}

}  // namespace

extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD S1DeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY S1DeviceD0Entry;

EVT_IDD_CX_ADAPTER_INIT_FINISHED S1AdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES S1AdapterCommitModes;
EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION S1ParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES S1MonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES S1MonitorQueryModes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN S1MonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN S1MonitorUnassignSwapChain;
EVT_IDD_CX_MONITOR_GET_PHYSICAL_SIZE S1MonitorGetPhysicalSize;

struct IndirectDeviceContextWrapper {
  IndirectDeviceContext* pContext;
  void Cleanup() {
    delete pContext;
    pContext = nullptr;
  }
};

struct IndirectMonitorContextWrapper {
  IndirectMonitorContext* pContext;
  void Cleanup() {
    delete pContext;
    pContext = nullptr;
  }
};

WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);
WDF_DECLARE_CONTEXT_TYPE(IndirectMonitorContextWrapper);

extern "C" BOOL WINAPI DllMain(_In_ HINSTANCE, _In_ UINT, _In_opt_ LPVOID) {
  return TRUE;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject,
                                PUNICODE_STRING pRegistryPath) {
  WDF_DRIVER_CONFIG Config;
  WDF_OBJECT_ATTRIBUTES Attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
  WDF_DRIVER_CONFIG_INIT(&Config, S1DeviceAdd);
  return WdfDriverCreate(pDriverObject, pRegistryPath, &Attributes, &Config,
                         WDF_NO_HANDLE);
}

NTSTATUS S1DeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT pDeviceInit) {
  NTSTATUS Status = STATUS_SUCCESS;
  UNREFERENCED_PARAMETER(Driver);

  WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
  PnpPowerCallbacks.EvtDeviceD0Entry = S1DeviceD0Entry;
  WdfDeviceInitSetPnpPowerEventCallbacks(pDeviceInit, &PnpPowerCallbacks);

  IDD_CX_CLIENT_CONFIG IddConfig;
  IDD_CX_CLIENT_CONFIG_INIT(&IddConfig);
  IddConfig.EvtIddCxAdapterInitFinished = S1AdapterInitFinished;
  IddConfig.EvtIddCxParseMonitorDescription = S1ParseMonitorDescription;
  IddConfig.EvtIddCxMonitorGetDefaultDescriptionModes = S1MonitorGetDefaultModes;
  IddConfig.EvtIddCxMonitorQueryTargetModes = S1MonitorQueryModes;
  IddConfig.EvtIddCxAdapterCommitModes = S1AdapterCommitModes;
  IddConfig.EvtIddCxMonitorAssignSwapChain = S1MonitorAssignSwapChain;
  IddConfig.EvtIddCxMonitorUnassignSwapChain = S1MonitorUnassignSwapChain;

  Status = IddCxDeviceInitConfig(pDeviceInit, &IddConfig);
  if (!NT_SUCCESS(Status)) return Status;

  WDF_OBJECT_ATTRIBUTES Attr;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);
  Attr.EvtCleanupCallback = [](WDFOBJECT Object) {
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Object);
    if (pContext) pContext->Cleanup();
  };

  WDFDEVICE Device = nullptr;
  Status = WdfDeviceCreate(&pDeviceInit, &Attr, &Device);
  if (!NT_SUCCESS(Status)) return Status;

  Status = IddCxDeviceInitialize(Device);
  if (!NT_SUCCESS(Status)) return Status;

  auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
  pContext->pContext = new IndirectDeviceContext(Device);
  return STATUS_SUCCESS;
}

NTSTATUS S1DeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE) {
  auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
  pContext->pContext->InitAdapter();
  return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Direct3DDevice
// ---------------------------------------------------------------------------
Direct3DDevice::Direct3DDevice(LUID AdapterLuid) : AdapterLuid(AdapterLuid) {}
Direct3DDevice::Direct3DDevice() { AdapterLuid = LUID{}; }

HRESULT Direct3DDevice::Init() {
  HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&DxgiFactory));
  if (FAILED(hr)) return hr;
  hr = DxgiFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
  if (FAILED(hr)) return hr;
  hr = D3D11CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                         D3D11_SDK_VERSION, &Device, nullptr, &DeviceContext);
  return hr;
}

// ---------------------------------------------------------------------------
// SwapChainProcessor
// ---------------------------------------------------------------------------
SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain,
                                       shared_ptr<Direct3DDevice> Device,
                                       HANDLE NewFrameEvent,
                                       s1::ipc::FrameProducer* Producer,
                                       DWORD Width, DWORD Height)
    : m_hSwapChain(hSwapChain),
      m_Device(Device),
      m_hAvailableBufferEvent(NewFrameEvent),
      m_Producer(Producer),
      m_Width(Width),
      m_Height(Height) {
  m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
  m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));
}

SwapChainProcessor::~SwapChainProcessor() {
  SetEvent(m_hTerminateEvent.Get());
  if (m_hThread.Get()) WaitForSingleObject(m_hThread.Get(), INFINITE);
}

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID Argument) {
  reinterpret_cast<SwapChainProcessor*>(Argument)->Run();
  return 0;
}

void SwapChainProcessor::Run() {
  DWORD AvTask = 0;
  HANDLE AvTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &AvTask);
  RunCore();
  // Deleting the swap-chain on exit lets the OS create a new one if needed.
  WdfObjectDelete((WDFOBJECT)m_hSwapChain);
  m_hSwapChain = nullptr;
  AvRevertMmThreadCharacteristics(AvTaskHandle);
}

bool SwapChainProcessor::ProcessFrame(ID3D11Texture2D* Surface) {
  if (!m_Producer) return false;

  D3D11_TEXTURE2D_DESC desc = {};
  Surface->GetDesc(&desc);

  // (Re)create the CPU-readable staging texture when the size changes.
  if (!m_Stage || m_StageWidth != desc.Width || m_StageHeight != desc.Height) {
    D3D11_TEXTURE2D_DESC sdesc = desc;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;
    m_Stage.Reset();
    if (FAILED(m_Device->Device->CreateTexture2D(&sdesc, nullptr, &m_Stage))) {
      return false;
    }
    m_StageWidth = desc.Width;
    m_StageHeight = desc.Height;
  }

  // GPU copy from the swap-chain surface to the staging texture, then map.
  m_Device->DeviceContext->CopyResource(m_Stage.Get(), Surface);

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(m_Device->DeviceContext->Map(m_Stage.Get(), 0, D3D11_MAP_READ, 0,
                                          &mapped))) {
    return false;
  }

  const uint32_t w = desc.Width;
  const uint32_t h = desc.Height;
  const uint32_t stride = w * 4;
  uint8_t* dst = m_Producer->BeginWrite(w, h, stride, (int64_t)GetTickCount64());
  if (dst) {
    // Copy row by row honouring the mapped pitch.
    for (uint32_t y = 0; y < h; ++y) {
      memcpy(dst + (size_t)y * stride,
             static_cast<const uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch,
             stride);
    }
    m_Producer->Publish();
  } else {
    m_Producer->NoteDropped();
  }
  m_Device->DeviceContext->Unmap(m_Stage.Get(), 0);
  return dst != nullptr;
}

void SwapChainProcessor::RunCore() {
  ComPtr<IDXGIDevice> DxgiDevice;
  if (FAILED(m_Device->Device.As(&DxgiDevice))) return;

  IDARG_IN_SWAPCHAINSETDEVICE SetDevice = {};
  SetDevice.pDevice = DxgiDevice.Get();
  if (FAILED(IddCxSwapChainSetDevice(m_hSwapChain, &SetDevice))) return;

  for (;;) {
    ComPtr<IDXGIResource> AcquiredBuffer;
    IDARG_OUT_RELEASEANDACQUIREBUFFER Buffer = {};
    HRESULT hr = IddCxSwapChainReleaseAndAcquireBuffer(m_hSwapChain, &Buffer);

    if (hr == E_PENDING) {
      HANDLE WaitHandles[] = {m_hAvailableBufferEvent, m_hTerminateEvent.Get()};
      DWORD WaitResult =
          WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE, 16);
      if (WaitResult == WAIT_OBJECT_0 || WaitResult == WAIT_TIMEOUT) continue;
      break;  // terminate or unexpected
    } else if (SUCCEEDED(hr)) {
      AcquiredBuffer.Attach(Buffer.MetaData.pSurface);

      // Frame processing: GPU copy -> staging -> shared memory. No USB here.
      ComPtr<ID3D11Texture2D> tex;
      if (SUCCEEDED(AcquiredBuffer.As(&tex))) {
        ProcessFrame(tex.Get());
      }
      AcquiredBuffer.Reset();

      hr = IddCxSwapChainFinishedProcessingFrame(m_hSwapChain);
      if (FAILED(hr)) break;
    } else {
      break;  // swap-chain abandoned
    }
  }
}

// ---------------------------------------------------------------------------
// IndirectDeviceContext
// ---------------------------------------------------------------------------
IndirectDeviceContext::IndirectDeviceContext(_In_ WDFDEVICE WdfDevice)
    : m_WdfDevice(WdfDevice) {
  m_Adapter = {};
}
IndirectDeviceContext::~IndirectDeviceContext() {}

void IndirectDeviceContext::InitAdapter() {
  IDDCX_ADAPTER_CAPS AdapterCaps = {};
  AdapterCaps.Size = sizeof(AdapterCaps);
  AdapterCaps.MaxMonitorsSupported = 1;
  AdapterCaps.EndPointDiagnostics.Size = sizeof(AdapterCaps.EndPointDiagnostics);
  AdapterCaps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
  AdapterCaps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
  AdapterCaps.EndPointDiagnostics.pEndPointFriendlyName = L"ACEMAGIC S1 LCD";
  AdapterCaps.EndPointDiagnostics.pEndPointManufacturerName = L"AcemagicS1Display";
  AdapterCaps.EndPointDiagnostics.pEndPointModelName = L"ACEMAGIC S1 Front Panel";

  IDDCX_ENDPOINT_VERSION Version = {};
  Version.Size = sizeof(Version);
  Version.MajorVer = 1;
  AdapterCaps.EndPointDiagnostics.pFirmwareVersion = &Version;
  AdapterCaps.EndPointDiagnostics.pHardwareVersion = &Version;

  WDF_OBJECT_ATTRIBUTES Attr;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);

  IDARG_IN_ADAPTER_INIT AdapterInit = {};
  AdapterInit.WdfDevice = m_WdfDevice;
  AdapterInit.pCaps = &AdapterCaps;
  AdapterInit.ObjectAttributes = &Attr;

  IDARG_OUT_ADAPTER_INIT AdapterInitOut;
  NTSTATUS Status = IddCxAdapterInitAsync(&AdapterInit, &AdapterInitOut);
  if (NT_SUCCESS(Status)) {
    m_Adapter = AdapterInitOut.AdapterObject;
    auto* pContext =
        WdfObjectGet_IndirectDeviceContextWrapper(AdapterInitOut.AdapterObject);
    pContext->pContext = this;
  }
}

void IndirectDeviceContext::FinishInit(UINT ConnectorIndex) {
  WDF_OBJECT_ATTRIBUTES Attr;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectMonitorContextWrapper);

  IDDCX_MONITOR_INFO MonitorInfo = {};
  MonitorInfo.Size = sizeof(MonitorInfo);
  MonitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
  MonitorInfo.ConnectorIndex = ConnectorIndex;

  // Report the panel as EDID-less: modes come from the driver's own mode lists.
  MonitorInfo.MonitorDescription.Size = sizeof(MonitorInfo.MonitorDescription);
  MonitorInfo.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
  MonitorInfo.MonitorDescription.DataSize = 0;
  MonitorInfo.MonitorDescription.pData = nullptr;

  // Stable container ID so the OS can tell this monitor apart across reboots.
  const GUID kContainer = {
      0xace5a91c, 0x1d5e, 0x4f2a, {0x9b, 0x3e, 0x00, 0x01, 0x53, 0x31, 0x4c, 0x43}};
  MonitorInfo.MonitorContainerId = kContainer;

  IDARG_IN_MONITORCREATE MonitorCreate = {};
  MonitorCreate.ObjectAttributes = &Attr;
  MonitorCreate.pMonitorInfo = &MonitorInfo;

  IDARG_OUT_MONITORCREATE MonitorCreateOut;
  NTSTATUS Status =
      IddCxMonitorCreate(m_Adapter, &MonitorCreate, &MonitorCreateOut);
  if (NT_SUCCESS(Status)) {
    auto* wrap =
        WdfObjectGet_IndirectMonitorContextWrapper(MonitorCreateOut.MonitorObject);
    wrap->pContext = new IndirectMonitorContext(MonitorCreateOut.MonitorObject);
    IDARG_OUT_MONITORARRIVAL ArrivalOut;
    IddCxMonitorArrival(MonitorCreateOut.MonitorObject, &ArrivalOut);
  }
}

// ---------------------------------------------------------------------------
// IndirectMonitorContext
// ---------------------------------------------------------------------------
IndirectMonitorContext::IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor)
    : m_Monitor(Monitor) {}

IndirectMonitorContext::~IndirectMonitorContext() { m_ProcessingThread.reset(); }

void IndirectMonitorContext::AssignSwapChain(IDDCX_SWAPCHAIN SwapChain,
                                             LUID RenderAdapter,
                                             HANDLE NewFrameEvent) {
  m_ProcessingThread.reset();

  // (Re)create the IPC producer sized for the current mode. The renderer may be
  // absent; the mapping is still created so a late renderer can attach.
  std::string err;
  const uint32_t slotBytes = m_CurrentWidth * m_CurrentHeight * 4;
  if (!m_producer.Create(m_CurrentWidth, m_CurrentHeight, slotBytes, &err)) {
    // If IPC cannot be created we still must not leak the swap-chain.
    WdfObjectDelete(SwapChain);
    return;
  }

  auto Device = make_shared<Direct3DDevice>(RenderAdapter);
  if (FAILED(Device->Init())) {
    WdfObjectDelete(SwapChain);
  } else {
    m_ProcessingThread.reset(new SwapChainProcessor(
        SwapChain, Device, NewFrameEvent, &m_producer, m_CurrentWidth,
        m_CurrentHeight));
  }
}

void IndirectMonitorContext::UnassignSwapChain() { m_ProcessingThread.reset(); }

// ---------------------------------------------------------------------------
// DDI callbacks
// ---------------------------------------------------------------------------
NTSTATUS S1AdapterInitFinished(IDDCX_ADAPTER AdapterObject,
                               const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs) {
  auto* wrap = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);
  if (NT_SUCCESS(pInArgs->AdapterInitStatus)) {
    wrap->pContext->FinishInit(0);
  }
  return STATUS_SUCCESS;
}

NTSTATUS S1AdapterCommitModes(IDDCX_ADAPTER, const IDARG_IN_COMMITMODES* pInArgs) {
  // The swap-chain already matches the committed mode and the renderer learns
  // the size from the IPC header, so no output reconfiguration is required.
  UNREFERENCED_PARAMETER(pInArgs);
  return STATUS_SUCCESS;
}

NTSTATUS S1ParseMonitorDescription(
    const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs,
    IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs) {
  // We report an EDID-less monitor, so this should not be called. If it is,
  // return no modes.
  UNREFERENCED_PARAMETER(pInArgs);
  pOutArgs->MonitorModeBufferOutputCount = 0;
  return STATUS_SUCCESS;
}

NTSTATUS S1MonitorGetDefaultModes(
    IDDCX_MONITOR, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs,
    IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs) {
  if (pInArgs->DefaultMonitorModeBufferInputCount == 0) {
    pOutArgs->DefaultMonitorModeBufferOutputCount = kModeListSize;
  } else {
    for (DWORD i = 0; i < kModeListSize; ++i) {
      pInArgs->pDefaultMonitorModes[i] =
          CreateMonitorMode(kModeList[i].Width, kModeList[i].Height,
                            kModeList[i].VSync, IDDCX_MONITOR_MODE_ORIGIN_DRIVER);
    }
    pOutArgs->DefaultMonitorModeBufferOutputCount = kModeListSize;
    pOutArgs->PreferredMonitorModeIdx = kPreferredModeIdx;
  }
  return STATUS_SUCCESS;
}

NTSTATUS S1MonitorQueryModes(IDDCX_MONITOR,
                             const IDARG_IN_QUERYTARGETMODES* pInArgs,
                             IDARG_OUT_QUERYTARGETMODES* pOutArgs) {
  vector<IDDCX_TARGET_MODE> TargetModes;
  for (DWORD i = 0; i < kModeListSize; ++i) {
    TargetModes.push_back(CreateTargetMode(kModeList[i].Width,
                                           kModeList[i].Height,
                                           kModeList[i].VSync));
  }
  pOutArgs->TargetModeBufferOutputCount = (UINT)TargetModes.size();
  if (pInArgs->TargetModeBufferInputCount >= TargetModes.size()) {
    copy(TargetModes.begin(), TargetModes.end(), pInArgs->pTargetModes);
  }
  return STATUS_SUCCESS;
}

NTSTATUS S1MonitorGetPhysicalSize(IDDCX_MONITOR, const IDARG_IN_GETPHYSICALSIZE*,
                                  IDARG_OUT_MONITORGETPHYSICALSIZE* pOutArgs) {
  // Report a plausible physical size so Windows picks a sane scaling factor.
  pOutArgs->PhysicalSize.cx = 94;  // mm, ~2.5" panel
  pOutArgs->PhysicalSize.cy = 50;
  return STATUS_SUCCESS;
}

NTSTATUS S1MonitorAssignSwapChain(IDDCX_MONITOR MonitorObject,
                                  const IDARG_IN_SETSWAPCHAIN* pInArgs) {
  auto* wrap = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
  wrap->pContext->AssignSwapChain(pInArgs->hSwapChain,
                                  pInArgs->RenderAdapterLuid,
                                  pInArgs->hNextSurfaceAvailable);
  return STATUS_SUCCESS;
}

NTSTATUS S1MonitorUnassignSwapChain(IDDCX_MONITOR MonitorObject) {
  auto* wrap = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
  wrap->pContext->UnassignSwapChain();
  return STATUS_SUCCESS;
}
