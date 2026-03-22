#include "file_utils.h"
#include "string_utils.h"
#include <fstream>
#include <string>
#include <windows.h>

std::vector<std::string> ls(const char* root, const char* prefix, LS_FLAGS flags)
{
    std::vector<std::string> list;
    std::string search_path = std::string(root) + "\\*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &ffd);

    if (hFind == INVALID_HANDLE_VALUE)
        return list;

    do {
        std::string name = ffd.cFileName;
        if (name == "." || name == "..")
            continue;

        if (prefix && name.compare(0, strlen(prefix), prefix) != 0)
            continue;

        bool is_dir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (is_dir && (flags & LS_DIRS))
            list.push_back(name);
        else if (!is_dir && (flags & LS_FILES))
            list.push_back(name);
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);
    return list;
}

bool file_exists(const std::string& path)
{
    DWORD attribs = GetFileAttributesA(path.c_str());
    return (attribs != INVALID_FILE_ATTRIBUTES);
}

bool dir_exists(const std::string& path)
{
    DWORD attribs = GetFileAttributesA(path.c_str());
    return (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY));
}

std::string get_exe_path()
{
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return std::string();
    return std::string(buf, len);
}

std::string get_wine_exe_name(bool keep_ext)
{
    return std::string();
}

std::string get_home_dir()
{
    const char* userprofile = getenv("USERPROFILE");
    if (userprofile)
        return std::string(userprofile);
    return std::string();
}

std::string get_data_dir()
{
    const char* localappdata = getenv("LOCALAPPDATA");
    if (localappdata)
        return std::string(localappdata) + "\\MangoHud";
    return std::string();
}

std::string get_config_dir()
{
    const char* appdata = getenv("APPDATA");
    if (appdata)
        return std::string(appdata) + "\\MangoHud";
    return std::string();
}

std::string read_symlink(const char * link)
{
    return std::string();
}

std::string read_symlink(const std::string&& link)
{
    return read_symlink(link.c_str());
}

