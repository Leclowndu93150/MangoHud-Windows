#include "file_utils.h"
#include <filesystem.h>
#include <string>
#include <spdlog/spdlog.h>
#include "hud_elements.h"
#include "overlay.h"

namespace fs = ghc::filesystem;
class WineSync {
    private:
        inline static const std::unordered_map<std::string, std::string> methods {
            {"NONE", "NONE"},
            {"winesync", "Wserver"},
            {"esync", "Esync"},
            {"fsync", "Fsync"},
            {"ntsync", "NTsync"},
        };

        int pid;
        std::string method = "NONE";
        bool inside_wine = true;
    public:
        void determine_sync_variant() {
            inside_wine = false;
            return;
        }

        bool valid() {
            return inside_wine;
        }

        // return sync method as display name
        const char* get_method() {
            return methods.at(method).c_str();
        }

        void set_pid(int _pid) {
            if (_pid != pid) {
                pid = _pid;
                determine_sync_variant();
            }
        }
};

extern std::unique_ptr<WineSync> winesync_ptr;
