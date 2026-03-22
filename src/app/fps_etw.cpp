// ETW-based FPS measurement via Microsoft-Windows-DXGI Present events.
// Traces Present::Start (event ID 42) from the DXGI provider to measure
// how often a target process calls Present, giving us real game FPS.

#include "fps_etw.h"
#include <evntrace.h>
#include <evntcons.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <spdlog/spdlog.h>

// Microsoft-Windows-DXGI provider GUID
// {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
static const GUID DXGI_PROVIDER = {
    0xCA11C036, 0x0102, 0x4A2D,
    { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 }
};

// Microsoft-Windows-DxgKrnl provider GUID (more accurate, kernel-level)
// {802EC45A-1E99-4B83-9920-87C98277BA9D}
static const GUID DXGKRNL_PROVIDER = {
    0x802EC45A, 0x1E99, 0x4B83,
    { 0x99, 0x20, 0x87, 0xC9, 0x82, 0x77, 0xBA, 0x9D }
};

static const USHORT DXGI_PRESENT_START_ID = 42;
static const USHORT DXGKRNL_PRESENT_INFO_ID = 0x00b8; // 184

static const wchar_t* SESSION_NAME = L"MangoHudFPSTrace";

static TRACEHANDLE g_session_handle = 0;
static TRACEHANDLE g_trace_handle = INVALID_PROCESSTRACE_HANDLE;
static std::thread g_trace_thread;
static std::atomic<bool> g_running{false};
static DWORD g_target_pid = 0;

static std::atomic<double> g_fps{0.0};
static std::atomic<uint64_t> g_frametime_ns{0};

// Timing state
static LARGE_INTEGER g_last_present_time = {};
static LARGE_INTEGER g_qpc_freq = {};
static int g_frame_count = 0;
static LARGE_INTEGER g_fps_update_time = {};

static void WINAPI event_callback(PEVENT_RECORD pEvent)
{
    if (!g_running) return;

    if (pEvent->EventHeader.ProcessId != g_target_pid)
        return;

    // Only use DxgKrnl Present::Info events (kernel-level, most accurate)
    if (pEvent->EventHeader.EventDescriptor.Id != DXGKRNL_PRESENT_INFO_ID)
        return;

    LARGE_INTEGER now;
    now.QuadPart = pEvent->EventHeader.TimeStamp.QuadPart;

    if (g_last_present_time.QuadPart != 0) {
        // Compute frametime from QPC timestamps
        int64_t delta = now.QuadPart - g_last_present_time.QuadPart;
        if (delta > 0 && g_qpc_freq.QuadPart > 0) {
            uint64_t ft_ns = (uint64_t)((double)delta / (double)g_qpc_freq.QuadPart * 1000000000.0);
            g_frametime_ns.store(ft_ns, std::memory_order_relaxed);
        }

        g_frame_count++;

        // Update FPS approximately once per second
        int64_t elapsed = now.QuadPart - g_fps_update_time.QuadPart;
        double elapsed_sec = (double)elapsed / (double)g_qpc_freq.QuadPart;
        if (elapsed_sec >= 1.0) {
            double fps = (double)g_frame_count / elapsed_sec;
            g_fps.store(fps, std::memory_order_relaxed);
            g_frame_count = 0;
            g_fps_update_time = now;
        }
    } else {
        g_fps_update_time = now;
    }

    g_last_present_time = now;
}

static void trace_thread_func()
{
    EVENT_TRACE_LOGFILEW logfile = {};
    logfile.LoggerName = (LPWSTR)SESSION_NAME;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = event_callback;

    g_trace_handle = OpenTraceW(&logfile);
    if (g_trace_handle == INVALID_PROCESSTRACE_HANDLE) {
        SPDLOG_ERROR("ETW: OpenTrace failed (error {})", GetLastError());
        return;
    }

    // ProcessTrace blocks until the session is stopped
    ULONG status = ProcessTrace(&g_trace_handle, 1, nullptr, nullptr);
    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) {
        SPDLOG_ERROR("ETW: ProcessTrace failed (error {})", status);
    }

    CloseTrace(g_trace_handle);
    g_trace_handle = INVALID_PROCESSTRACE_HANDLE;
}

namespace etw_fps {

bool start(DWORD target_pid)
{
    if (g_running) return true;

    QueryPerformanceFrequency(&g_qpc_freq);
    g_target_pid = target_pid;
    g_last_present_time.QuadPart = 0;
    g_frame_count = 0;
    g_fps.store(0.0);
    g_frametime_ns.store(0);

    // Allocate EVENT_TRACE_PROPERTIES with space for session name
    size_t props_size = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(wchar_t) * 256;
    auto* props = (EVENT_TRACE_PROPERTIES*)calloc(1, props_size);
    props->Wnode.BufferSize = (ULONG)props_size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1; // QPC timestamps
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    // Stop any existing session with this name (from a previous crash)
    ControlTraceW(0, SESSION_NAME, props, EVENT_TRACE_CONTROL_STOP);

    // Start trace session
    memset(props, 0, props_size);
    props->Wnode.BufferSize = (ULONG)props_size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ULONG status = StartTraceW(&g_session_handle, SESSION_NAME, props);
    if (status != ERROR_SUCCESS) {
        SPDLOG_ERROR("ETW: StartTrace failed (error {}). Need admin privileges.", status);
        free(props);
        return false;
    }

    // Enable DxgKrnl provider (kernel-level present tracking)
    status = EnableTraceEx2(
        g_session_handle,
        &DXGKRNL_PROVIDER,
        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_INFORMATION,
        0x4000000008000001, // Present + Performance keywords
        0, 0, nullptr
    );
    if (status != ERROR_SUCCESS) {
        SPDLOG_ERROR("ETW: EnableTraceEx2 DxgKrnl failed (error {})", status);
        ControlTraceW(g_session_handle, nullptr, props, EVENT_TRACE_CONTROL_STOP);
        free(props);
        return false;
    }


    free(props);

    g_running = true;
    g_trace_thread = std::thread(trace_thread_func);

    SPDLOG_INFO("ETW: FPS tracing started for PID {}", target_pid);
    return true;
}

void stop()
{
    if (!g_running) return;
    g_running = false;

    // Stop the trace session, which causes ProcessTrace to return
    size_t props_size = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(wchar_t) * 256;
    auto* props = (EVENT_TRACE_PROPERTIES*)calloc(1, props_size);
    props->Wnode.BufferSize = (ULONG)props_size;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ControlTraceW(g_session_handle, nullptr, props, EVENT_TRACE_CONTROL_STOP);
    free(props);

    if (g_trace_thread.joinable())
        g_trace_thread.join();

    g_session_handle = 0;
    SPDLOG_INFO("ETW: FPS tracing stopped");
}

double get_fps()
{
    return g_fps.load(std::memory_order_relaxed);
}

uint64_t get_frametime_ns()
{
    return g_frametime_ns.load(std::memory_order_relaxed);
}

bool is_running()
{
    return g_running;
}

} // namespace etw_fps
