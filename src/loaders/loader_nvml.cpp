// NVML loader - Windows version using LoadLibrary/GetProcAddress

#include "loader_nvml.h"
#include <iostream>
#include <spdlog/spdlog.h>

static std::shared_ptr<libnvml_loader> libnvml_;

std::shared_ptr<libnvml_loader> get_libnvml_loader()
{
    if (!libnvml_)
        libnvml_ = std::make_shared<libnvml_loader>("nvml.dll");
    return libnvml_;
}

libnvml_loader::libnvml_loader() : loaded_(false) {
}

libnvml_loader::~libnvml_loader() {
  CleanUp(loaded_);
}

bool libnvml_loader::Load(const std::string& library_name) {
  if (loaded_) {
    return false;
  }

  library_ = LoadLibraryA(library_name.c_str());
  if (!library_) {
    SPDLOG_ERROR("Failed to open " MANGOHUD_ARCH " {}", library_name);
    return false;
  }

#define LOAD_SYM(name) \
  name = reinterpret_cast<decltype(this->name)>(GetProcAddress(library_, #name)); \
  if (!name) { CleanUp(true); return false; }

  LOAD_SYM(nvmlInit_v2);
  LOAD_SYM(nvmlShutdown);
  LOAD_SYM(nvmlDeviceGetUtilizationRates);
  LOAD_SYM(nvmlDeviceGetTemperature);
  LOAD_SYM(nvmlDeviceGetPciInfo_v3);
  LOAD_SYM(nvmlDeviceGetCount_v2);
  LOAD_SYM(nvmlDeviceGetHandleByIndex_v2);
  LOAD_SYM(nvmlDeviceGetHandleByPciBusId_v2);
  LOAD_SYM(nvmlDeviceGetMemoryInfo);
  LOAD_SYM(nvmlDeviceGetClockInfo);
  LOAD_SYM(nvmlErrorString);
  LOAD_SYM(nvmlDeviceGetPowerUsage);
  LOAD_SYM(nvmlDeviceGetPowerManagementLimit);
  LOAD_SYM(nvmlDeviceGetCurrentClocksThrottleReasons);
  LOAD_SYM(nvmlUnitGetFanSpeedInfo);
  LOAD_SYM(nvmlUnitGetHandleByIndex);
  LOAD_SYM(nvmlDeviceGetFanSpeed);
  LOAD_SYM(nvmlDeviceGetGraphicsRunningProcesses);

#undef LOAD_SYM

  loaded_ = true;
  return true;
}


void libnvml_loader::CleanUp(bool unload) {
  if (unload && library_) {
    FreeLibrary(library_);
    library_ = NULL;
  }
  loaded_ = false;
  nvmlInit_v2 = NULL;
  nvmlShutdown = NULL;
  nvmlDeviceGetUtilizationRates = NULL;
  nvmlDeviceGetTemperature = NULL;
  nvmlDeviceGetPciInfo_v3 = NULL;
  nvmlDeviceGetCount_v2 = NULL;
  nvmlDeviceGetHandleByIndex_v2 = NULL;
  nvmlDeviceGetHandleByPciBusId_v2 = NULL;
  nvmlDeviceGetCurrentClocksThrottleReasons = NULL;
  nvmlUnitGetFanSpeedInfo = NULL;
  nvmlUnitGetHandleByIndex = NULL;
  nvmlDeviceGetFanSpeed = NULL;
  nvmlDeviceGetGraphicsRunningProcesses = NULL;
}
