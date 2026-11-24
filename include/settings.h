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

    // hit fix: plants your character while swinging so hits register
    // consistently regardless of ping/fps
    bool hitfixEnabled = false;

    // silent aim for bows: snaps instantly + auto-shoots while a
    // bow/crossbow/slingshot is equipped (closest external equivalent -
    // true silent aim needs code inside the game)
    bool silentBow = false;

    // killaura (auto-aim + click at CPS within range/angle)
    bool killauraEnabled = false;
    int kaKey = 0x56;             // V
    int kaMode = 1;               // 0=hold 1=toggle 2=always
    float kaCps = 20.f;          // attacks per second (slider to 40)
    float kaSwingRange = 18.f;   // target acquisition range (studs)
    float kaAttackRange = 18.f;  // click range (studs)
    float kaAngle = 90.f;        // max facing angle, 1-360
    float kaSmooth = 0.35f;      // aim pull strength 0..1
    float kaPredMs = 80.f;       // lead target by velocity * ms (hitreg vs movers)
    float kaClickPx = 25.f;      // only click when crosshair is this close to target
    bool kaRequireMouse = false; // only while LMB held
    bool kaTeamCheck = true;

    // target priority: 0 = crosshair, 1 = nearest, 2 = lowest hp
    int aimbotSort = 0;
    // gate all combat features to bedwars place ids only
    bool bedwarsOnly = true;

    // anti-void: teleport back up if you fall too far below your peak
    bool antiVoid = false;
    float antiVoidDist = 40.f;  // studs below peak before rescue

    // reach: brief position lunge toward the target on click frames so
    // swings connect past normal range without breaking hitreg
    bool reachEnabled = false;
    float reachStuds = 4.f;

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

inline const char* NYX_VERSION = "28";

namespace config
{
    void Load(const char* path = "config\\nyx.cfg");
    void Save(const char* path = "config\\nyx.cfg");
}
