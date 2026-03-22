#include "net.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <netioapi.h>
#include <iphlpapi.h>
#include <spdlog/spdlog.h>

std::unique_ptr<Net> net = nullptr;

Net::Net()
{
    ULONG bufSize = 0;
    GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &bufSize);
    std::vector<BYTE> buf(bufSize);
    PIP_ADAPTER_ADDRESSES addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());

    if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addrs, &bufSize) != ERROR_SUCCESS)
        return;

    for (auto a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;

        networkInterface iface {};
        // Convert wide name to narrow
        char name[256];
        WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof(name), NULL, NULL);
        iface.name = name;
        iface.txBytes = 0;
        iface.rxBytes = 0;
        iface.txBps = 0;
        iface.rxBps = 0;
        iface.previousTime = std::chrono::steady_clock::now();
        interfaces.push_back(iface);
    }
}

void Net::update()
{
    ULONG bufSize = 0;
    GetIfTable2(NULL); // Just to ensure the API is available

    MIB_IF_TABLE2 *ifTable = nullptr;
    if (GetIfTable2(&ifTable) != NO_ERROR || !ifTable)
        return;

    auto currentTime = std::chrono::steady_clock::now();

    for (ULONG i = 0; i < ifTable->NumEntries; i++) {
        MIB_IF_ROW2& row = ifTable->Table[i];
        if (row.OperStatus != IfOperStatusUp)
            continue;
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;

        // Convert wide name
        char name[256];
        WideCharToMultiByte(CP_UTF8, 0, row.Alias, -1, name, sizeof(name), NULL, NULL);
        std::string ifName(name);

        for (auto& iface : interfaces) {
            if (iface.name != ifName)
                continue;

            uint64_t curTx = row.OutOctets;
            uint64_t curRx = row.InOctets;

            iface.txBps = calculateThroughput(curTx, iface.txBytes, iface.previousTime, currentTime);
            iface.rxBps = calculateThroughput(curRx, iface.rxBytes, iface.previousTime, currentTime);
            iface.txBytes = curTx;
            iface.rxBytes = curRx;
            iface.previousTime = currentTime;
            break;
        }
    }

    FreeMibTable(ifTable);
}

uint64_t Net::calculateThroughput(long long currentBytes, long long previousBytes,
                    std::chrono::steady_clock::time_point previousTime,
                    std::chrono::steady_clock::time_point currentTime)
{
    if (previousBytes == 0)
        return 0;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - previousTime).count();
    if (duration == 0)
        return 0;

    long long diff = currentBytes - previousBytes;
    if (diff < 0)
        diff = 0;

    return (uint64_t)(diff * 1000 / duration);
}
