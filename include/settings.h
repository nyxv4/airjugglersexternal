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
    float aimbotRange = 60.f;    // studs
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

    // reach (tool handle resize)
    bool reachEnabled = false;
    float reachStuds = 6.f;      // +6 studs over normal (~10.5 total)

    // hit fix: plants your character while swinging so hits register
    // consistently regardless of ping/fps
    bool hitfixEnabled = false;

    // killaura (auto-aim + click at CPS within range/angle)
    bool killauraEnabled = false;
    float kaCps = 20.f;          // attacks per second (slider to 40)
    float kaSwingRange = 18.f;   // target acquisition range (studs)
    float kaAttackRange = 18.f;  // click range (studs)
    float kaAngle = 90.f;        // max facing angle, 1-360
    float kaSmooth = 0.35f;      // aim pull strength 0..1
    bool kaRequireMouse = false; // only while LMB held
    bool kaTeamCheck = true;

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

inline const char* NYX_VERSION = "23";

namespace config
{
    void Load(const char* path = "config\\nyx.cfg");
    void Save(const char* path = "config\\nyx.cfg");
}
