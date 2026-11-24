#pragma once
#include <Windows.h>
#include <string>

struct Settings
{
    // visuals
    bool espBox = true;
    bool espName = true;
    bool espDistance = true;
    bool espHealth = true;
    bool espSnaplines = false;
    bool espTeamCheck = true;
    bool espShowTeammates = false;
    bool espKit = true;

    // aimbot (hold key)
    bool aimbotEnabled = true;
    int aimbotKey = 0x58;        // X
    float aimbotFov = 120.f;     // px radius
    float aimbotSmooth = 0.25f;  // 0..1
    float aimbotRange = 30.f;    // studs
    bool aimbotSmoothing = true;
    int aimbotSmoothStyle = 1;   // 0 = None, 1 = Linear
    bool aimbotShake = false;
    float aimbotShakePx = 3.f;
    bool aimbotPrediction = false;
    float aimbotPredMs = 150.f;
    // activation modes: 0 = hold key, 1 = toggle key, 2 = always on
    int aimbotMode = 1;
    bool aimbotTeamCheck = true;
    bool aimbotKnockCheck = true;
    bool aimbotHealthCheck = true;
    bool aimbotRangeCheck = true;

    // triggerbot (hold key)
    bool triggerbotEnabled = true;
    int triggerKey = 0x58;
    int triggerMode = 1;
    float triggerFov = 30.f;

    // hitbox expander
    bool hitboxEnabled = true;
    int hitboxKey = 0x43;        // C
    int hitboxMode = 2;          // scripts-style: on while enabled
    float hitboxMult = 2.5f;
    float hitboxMaxStuds = 0.f;  // 0 = uncapped (14.4 for bedwars)

    // misc
    bool streamproof = true;
    bool menuVisible = false;
    int menuKey = 0xA1;         // VK_RSHIFT

    // ui
    int uiTheme = 0;            // 0=midnight 1=incognito 2=neon 3=light
    int uiLayout = 0;           // 0=tabs 1=sidebar
    float accent[3] = { 0.35f, 0.75f, 1.0f };
};

extern Settings g_cfg;

inline const char* NYX_VERSION = "13";

namespace config
{
    void Load(const char* path = "config\\nyx.cfg");
    void Save(const char* path = "config\\nyx.cfg");
}
