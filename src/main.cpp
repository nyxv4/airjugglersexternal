#include "overlay.h"
#include "settings.h"
#include <cstdio>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
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
