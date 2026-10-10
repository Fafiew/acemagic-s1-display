/*++

Module Name:

    Trace.h

Abstract:

    Minimal trace header. Production drivers would use WPP software tracing; to
    keep this project buildable without WPP preprocessing configuration, traces
    are routed to the Windows debug output via DbgPrintEx when a debugger is
    attached. The driver writes GPU/IPC diagnostics to the shared-memory header
    counters instead (read by the control app), which avoids any I/O on the
    frame-processing thread.

--*/

#pragma once

#include <windows.h>
#include <wdf.h>

#define WPP_CONTROL_GUIDS

#define TraceEvents(level, flags, message) \
  do {                                     \
    if (IsDebuggerPresent()) {             \
      DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, message); \
    }                                      \
  } while (0)

#define TRACE_LEVEL_INFORMATION 4
#define DPFLTR_IHVVIDEO_ID 111
#define DPFLTR_INFO_LEVEL 3
