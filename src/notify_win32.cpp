#include "notify.h"
#include <windows.h>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include "overlay_params.h"
#include "file_utils.h"

static void watchDirectory(notify_thread* nt)
{
    // Determine which directory to watch.
    // Prefer the directory containing the current config file if available,
    // otherwise fall back to the MangoHud config directory.
    std::string watch_dir;
    if (nt->params && !nt->params->config_file_path.empty()) {
        // Extract directory from config file path
        std::string cfg = nt->params->config_file_path;
        size_t sep = cfg.find_last_of("\\/");
        if (sep != std::string::npos)
            watch_dir = cfg.substr(0, sep);
    }
    if (watch_dir.empty()) {
        watch_dir = get_config_dir();
        if (!watch_dir.empty())
            watch_dir += "\\MangoHud";
    }
    if (watch_dir.empty()) {
        SPDLOG_ERROR("Config watcher: could not determine config directory");
        return;
    }

    HANDLE hChange = FindFirstChangeNotificationA(
        watch_dir.c_str(),
        FALSE,  // do not watch subtree
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME
    );

    if (hChange == INVALID_HANDLE_VALUE) {
        SPDLOG_ERROR("FindFirstChangeNotification failed for {}: error {}",
                     watch_dir, GetLastError());
        return;
    }

    SPDLOG_DEBUG("Config watcher: monitoring {}", watch_dir);

    while (!nt->quit) {
        DWORD waitStatus = WaitForSingleObject(hChange, 500); // 500 ms timeout for quit check
        if (nt->quit)
            break;

        if (waitStatus == WAIT_OBJECT_0) {
            // Brief delay so editors that do save-to-temp/delete/rename have time to finish
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            overlay_params local_params;
            {
                std::lock_guard<std::mutex> lk(nt->mutex);
                if (nt->params)
                    local_params = *nt->params;
            }

            parse_overlay_config(&local_params, getenv("MANGOHUD_CONFIG"), false);

            {
                std::lock_guard<std::mutex> lk(nt->mutex);
                if (nt->params)
                    *nt->params = local_params;
            }

            // Re-arm the notification
            if (!FindNextChangeNotification(hChange)) {
                SPDLOG_ERROR("FindNextChangeNotification failed: error {}", GetLastError());
                break;
            }
        }
    }

    FindCloseChangeNotification(hChange);
}

void start_notifier(notify_thread& nt)
{
    nt.quit = false;

    if (nt.thread.joinable())
        nt.thread.join();

    nt.thread = std::thread(watchDirectory, &nt);
}

void stop_notifier(notify_thread& nt)
{
    nt.quit = true;
    if (nt.thread.joinable())
        nt.thread.join();
}
