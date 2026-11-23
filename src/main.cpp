#include "overlay.h"
#include "settings.h"
#include "http.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <shellapi.h>

// bump this every release; VERSION file on github holds the latest
#define NYX_BUILD 12

static const char* kRepoBase = "https://raw.githubusercontent.com/nyxv4/airjugglersexternal/main";

// returns true if an update was downloaded and a swap is pending
static bool CheckForUpdate()
{
    std::string v = http::Get(std::string(kRepoBase) + "/VERSION");
    int remote = atoi(v.c_str());
    if (remote <= NYX_BUILD) return false;

    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) return false;
    std::string self(exePath);
    std::string dir = self.substr(0, self.find_last_of("\\/"));
    std::string newExe = dir + "\\NyxExternal.new.exe";

    if (!http::Download(std::string(kRepoBase) + "/releases/NyxExternal.exe", newExe))
        return false;

    std::string bat = dir + "\\nyx_update.bat";
    {
        std::ofstream b(bat, std::ios::trunc);
        b << "@echo off\r\n";
        b << "ping 127.0.0.1 -n 3 >nul\r\n";
        b << "move /y \"" << newExe << "\" \"" << self << "\"\r\n";
        b << "start \"\" \"" << self << "\"\r\n";
        b << "del \"%~f0\"\r\n";
    }
    ShellExecuteA(nullptr, "open", bat.c_str(), nullptr, dir.c_str(), SW_HIDE);
    return true;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    if (CheckForUpdate()) return 0; // updater bat swaps exe and relaunches

    CreateDirectoryA("config", nullptr);
    config::Load();

    if (!g_overlay.Create(inst))
    {
        MessageBoxW(nullptr, L"Failed to create overlay.", L"NYX", MB_ICONERROR);
        return 1;
    }

    g_overlay.Run();
    g_overlay.Shutdown();
    config::Save();
    return 0;
}
