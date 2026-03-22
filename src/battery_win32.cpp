#include "battery.h"
#include <windows.h>

BatteryStats Battery_Stats;

void BatteryStats::numBattery()
{
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        // If AC line status indicates battery is present
        if (sps.BatteryFlag != 128 && sps.BatteryFlag != 255) {
            batt_count = 1;
            batt_check = true;
        }
    }
}

void BatteryStats::update()
{
    if (!batt_check)
        numBattery();

    if (batt_count < 1)
        return;

    SYSTEM_POWER_STATUS sps;
    if (!GetSystemPowerStatus(&sps))
        return;

    if (sps.BatteryLifePercent != 255)
        current_percent = (float)sps.BatteryLifePercent;
    else
        current_percent = 0;

    if (sps.ACLineStatus == 1)
        current_status = "Charging";
    else if (sps.ACLineStatus == 0)
        current_status = "Discharging";
    else
        current_status = "Unknown";

    if (sps.BatteryLifeTime != (DWORD)-1)
        remaining_time = (float)sps.BatteryLifeTime / 3600.f; // Convert seconds to hours
    else
        remaining_time = 0;

    // Windows doesn't easily expose wattage
    current_watt = 0;
}

float BatteryStats::getPower()
{
    return current_watt;
}

float BatteryStats::getPercent()
{
    return current_percent;
}

float BatteryStats::getTimeRemaining()
{
    return remaining_time;
}
