#include "iostats.h"
#include <windows.h>

iostats g_io_stats {};

void getIoStats(iostats& io)
{
    IO_COUNTERS counters;
    if (!GetProcessIoCounters(GetCurrentProcess(), &counters))
        return;

    io.prev.read_bytes = io.curr.read_bytes;
    io.prev.write_bytes = io.curr.write_bytes;
    io.curr.read_bytes = counters.ReadTransferCount;
    io.curr.write_bytes = counters.WriteTransferCount;

    io.diff.read = (float)(io.curr.read_bytes - io.prev.read_bytes);
    io.diff.write = (float)(io.curr.write_bytes - io.prev.write_bytes);

    auto now = Clock::now();
    auto duration = std::chrono::duration<float>(now - io.last_update).count();
    if (duration > 0) {
        io.per_second.read = io.diff.read / duration;
        io.per_second.write = io.diff.write / duration;
    }
    io.last_update = now;
}
