#include "memory.h"
#include <windows.h>
#include <psapi.h>

float memused, memmax, swapused;
int mem_temp;
uint64_t proc_mem_resident, proc_mem_shared, proc_mem_virt;

void update_meminfo()
{
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        memmax = float(memInfo.ullTotalPhys) / (1024.f * 1024.f); // MiB
        float memfree = float(memInfo.ullAvailPhys) / (1024.f * 1024.f);
        memused = memmax - memfree;

        float swaptotal = float(memInfo.ullTotalPageFile - memInfo.ullTotalPhys) / (1024.f * 1024.f);
        float swapfree = float(memInfo.ullAvailPageFile - memInfo.ullAvailPhys) / (1024.f * 1024.f);
        if (swaptotal > 0)
            swapused = swaptotal - swapfree;
        else
            swapused = 0;
    }
}

void update_mem_temp()
{
    // No standard Windows API for DIMM temperature
    mem_temp = 0;
}

void update_procmem()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        proc_mem_resident = pmc.WorkingSetSize;
        proc_mem_virt = pmc.PrivateUsage;
        proc_mem_shared = pmc.WorkingSetSize - pmc.PrivateUsage;
        if (proc_mem_shared > proc_mem_resident)
            proc_mem_shared = 0;
    }
}
