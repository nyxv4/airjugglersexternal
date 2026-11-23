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
    bool aimbotTeamCheck = true;
    bool aimbotKnockCheck = true;
    bool aimbotHealthCheck = true;
    bool aimbotRangeCheck = true;

    // triggerbot (hold key)
    bool triggerbotEnabled = true;
    int triggerKey = 0x58;
    float triggerFov = 30.f;

    // hitbox expander (hold key)
    bool hitboxEnabled = true;
    int hitboxKey = 0x43;        // C
    float hitboxMult = 2.5f;
    float hitboxMaxStuds = 0.f;  // 0 = uncapped (14.4 for bedwars)

    // misc
    bool streamproof = true;
    bool menuVisible = false;
    int menuKey = 0xA1;         // VK_RSHIFT
};

extern Settings g_cfg;

namespace config
{
    void Load(const char* path = "config\\nyx.cfg");
    void Save(const char* path = "config\\nyx.cfg");
}
