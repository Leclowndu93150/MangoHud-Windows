#pragma once
#include <windows.h>
#include <cstdint>
#include <atomic>

// ETW-based FPS measurement for a target process.
// Traces Microsoft-Windows-DXGI Present events to compute frametime.
// Requires admin privileges to start an ETW session.

namespace etw_fps {

bool start(DWORD target_pid);
void stop();

// Returns the most recent measured FPS (updated ~once per second)
double get_fps();

// Returns the most recent frametime in nanoseconds
uint64_t get_frametime_ns();

// Returns true if the trace is actively running
bool is_running();

}
