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
    float aimbotRange = 1000.f;  // studs

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
    bool menuVisible = true;
    int menuKey = VK_INSERT;
};

extern Settings g_cfg;

namespace config
{
    void Load(const char* path = "config\\nyx.cfg");
    void Save(const char* path = "config\\nyx.cfg");
}
