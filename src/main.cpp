#include "overlay.h"
#include "settings.h"
#include "http.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <shellapi.h>

// build number comes from NYX_VERSION in settings.h - never hardcode
// a second copy or the updater loops forever (build < remote VERSION)
static const int NYX_BUILD = atoi(NYX_VERSION);

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

// dumps the loaded aim-assist + combat settings to nyx_debug.log so config
// round-trips can be verified without guessing which key failed to persist
static void DumpSettings()
{
    std::ofstream f("nyx_debug.log", std::ios::app);
    f << "[nyx] settings dump:\n";
    f << "  aimbot: en=" << g_cfg.aimbotEnabled << " key=0x" << std::hex << g_cfg.aimbotKey << std::dec
      << " mode=" << g_cfg.aimbotMode << " fov=" << g_cfg.aimbotFov
      << " useFov=" << g_cfg.aimbotUseFov << " drawFov=" << g_cfg.aimbotDrawFov
      << " smooth=" << g_cfg.aimbotSmoothing << " sx=" << g_cfg.aimbotSmoothX
      << " sy=" << g_cfg.aimbotSmoothY << " shake=" << g_cfg.aimbotShake
      << " shX=" << g_cfg.aimbotShakeX << " shY=" << g_cfg.aimbotShakeY
      << " pred=" << g_cfg.aimbotPrediction << " predMs=" << g_cfg.aimbotPredMs
      << " wall=" << g_cfg.aimbotWallCheck << " speed=" << g_cfg.aimbotSpeed
      << " sort=" << g_cfg.aimbotSort << " range=" << g_cfg.aimbotRange << "\n";
    f << "  checks: team=" << g_cfg.aimbotTeamCheck << " knock=" << g_cfg.aimbotKnockCheck
      << " health=" << g_cfg.aimbotHealthCheck << " range=" << g_cfg.aimbotRangeCheck
      << " bedwars=" << g_cfg.bedwarsOnly << "\n";
    f << "  combat: hitboxEsp=" << g_cfg.espHitbox << " hitfix(diag)=" << g_cfg.hitfixEnabled
      << " reach=" << g_cfg.reachEnabled << " antiVoid=" << g_cfg.antiVoid << "\n";
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    if (CheckForUpdate()) return 0; // updater bat swaps exe and relaunches

    CreateDirectoryA("config", nullptr);
    config::Load();
    DumpSettings();

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
