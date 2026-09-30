#pragma once

// Registers the angels95:// URI scheme with the host OS so browsers can hand
// deep links (angels95://join/<ip>:<port>) to the client. Windows uses
// HKCU\Software\Classes (no admin required); Linux writes a user .desktop
// handler and points xdg-mime at it. Header-only: included by Main.cpp only.

#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#ifdef _WIN32
    #include "../WindowsCompat.hpp"
#elif defined(__linux__)
    #include <unistd.h>
    #include <limits.h>
    #include <filesystem>
#endif

namespace ProtocolHandler
{
    inline constexpr const char* kScheme = "angels95";

    inline std::string ExecutablePath()
    {
#ifdef _WIN32
        char buf[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return "";
        return std::string(buf, n);
#elif defined(__linux__)
        char buf[PATH_MAX] = {0};
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return "";
        return std::string(buf, (size_t)n);
#else
        return "";
#endif
    }

#ifdef _WIN32
    inline constexpr const char* kRegistryRoot = "Software\\Classes\\angels95";
    inline constexpr const char* kCommandKey  = "Software\\Classes\\angels95\\shell\\open\\command";

    inline std::string ExpectedCommand()
    {
        return "\"" + ExecutablePath() + "\" \"%1\"";
    }

    inline bool IsRegistered()
    {
        std::string exe = ExecutablePath();
        if (exe.empty()) return false;

        HKEY key;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, kCommandKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;

        char buf[1024] = {0};
        DWORD size = sizeof(buf) - 1;
        DWORD type = 0;
        LONG rc = RegQueryValueExA(key, nullptr, nullptr, &type, (LPBYTE)buf, &size);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS || type != REG_SZ) return false;
        return ExpectedCommand() == buf;
    }

    inline bool Register()
    {
        std::string exe = ExecutablePath();
        if (exe.empty()) return false;

        HKEY key;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr, 0,
                            KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;
        const char* desc = "URL:ANGELS95 Protocol";
        RegSetValueExA(key, nullptr, 0, REG_SZ, (const BYTE*)desc, (DWORD)strlen(desc) + 1);
        const char* urlProto = "";
        RegSetValueExA(key, "URL Protocol", 0, REG_SZ, (const BYTE*)urlProto, 1);
        RegCloseKey(key);

        HKEY cmdKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, kCommandKey, 0, nullptr, 0,
                            KEY_WRITE, nullptr, &cmdKey, nullptr) != ERROR_SUCCESS)
            return false;
        std::string cmd = ExpectedCommand();
        LONG rc = RegSetValueExA(cmdKey, nullptr, 0, REG_SZ,
                                 (const BYTE*)cmd.c_str(), (DWORD)cmd.size() + 1);
        RegCloseKey(cmdKey);
        return rc == ERROR_SUCCESS;
    }
#elif defined(__linux__)
    inline std::string DesktopFilePath()
    {
        const char* home = getenv("HOME");
        if (!home || !*home) return "";
        return std::string(home) + "/.local/share/applications/angels95.desktop";
    }

    inline bool IsRegistered()
    {
        std::string path = DesktopFilePath();
        std::string exe = ExecutablePath();
        if (path.empty() || exe.empty()) return false;

        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        return content.find(exe) != std::string::npos &&
               content.find("x-scheme-handler/angels95") != std::string::npos;
    }

    inline bool Register()
    {
        std::string path = DesktopFilePath();
        std::string exe = ExecutablePath();
        if (path.empty() || exe.empty()) return false;

        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path(), ec);

        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << "[Desktop Entry]\n"
          << "Type=Application\n"
          << "Name=Angels95\n"
          << "Comment=Angels95 multiplayer deep-link handler\n"
          << "Exec=\"" << exe << "\" %u\n"
          << "Terminal=false\n"
          << "NoDisplay=true\n"
          << "StartupNotify=false\n"
          << "Categories=Game;\n"
          << "MimeType=x-scheme-handler/angels95;\n";
        f.close();

        std::system("xdg-mime default angels95.desktop x-scheme-handler/angels95 "
                    ">/dev/null 2>&1");
        std::system("update-desktop-database ~/.local/share/applications "
                    ">/dev/null 2>&1");
        return IsRegistered();
    }
#else
    inline bool IsRegistered() { return false; }
    inline bool Register() { return false; }
#endif

    // Called on every client launch: registers the scheme when missing and
    // re-registers when the executable moved (portable/System/ builds).
    inline bool EnsureRegistered()
    {
        if (ExecutablePath().empty()) return false;
        if (IsRegistered()) return true;
        return Register();
    }
}