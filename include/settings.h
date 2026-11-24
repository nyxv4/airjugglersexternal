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
    // kit-specific object esp (hidden metal, orbs, bees, traps...)
    bool objEsp = true;
    float objEspDist = 250.f;   // studs
    // hitbox esp: draws each body part's real bounds (pos+size+rotation
    // projected to screen) - visual only, separate from the expander
    bool espHitbox = false;
    float espHitboxDist = 200.f; // studs
    int espHitboxColor = 0;      // 0=red 1=accent 2=white

    // aimbot (hold key)
    bool aimbotEnabled = true;
    int aimbotKey = 0x58;        // X
    float aimbotFov = 120.f;     // px radius
    // vape-style: independent X/Y smoothing + shake, use/draw FOV toggles
    bool aimbotUseFov = true;
    bool aimbotDrawFov = false;
    int aimbotFovColor = 0;      // 0 = white, 1 = black
    float aimbotSmoothX = 0.25f; // 0..1 per-axis pull strength
    float aimbotSmoothY = 0.25f;
    float aimbotShakeX = 3.f;    // px jitter per axis
    float aimbotShakeY = 3.f;
    float aimbotRange = 60.f;    // studs
    bool aimbotSmoothing = true;
    bool aimbotShake = false;
    bool aimbotPrediction = false;
    float aimbotPredMs = 150.f;
    bool aimbotWallCheck = false; // occlusion test vs cached map geometry
    // activation modes: 0 = hold key, 1 = toggle key, 2 = always on
    int aimbotMode = 1;
    bool aimbotTeamCheck = true;
    bool aimbotKnockCheck = true;
    bool aimbotHealthCheck = true;
    bool aimbotRangeCheck = true;

    // hitbox expander
    bool hitboxEnabled = true;
    int hitboxKey = 0x43;        // C
    int hitboxMode = 2;          // scripts-style: on while enabled
    float hitboxMult = 2.5f;
    float hitboxMaxStuds = 14.4f; // per-axis cap (bedwars-safe default)

    // hit fix: plants your character while swinging so hits register
    // consistently regardless of ping/fps
    bool hitfixEnabled = false;

    // target priority: 0 = crosshair, 1 = nearest, 2 = lowest hp, 3 = none
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
    int uiTheme = 5;            // 0=midnight 1=incognito 2=neon 3=light 4=vape 5=nyx
    int uiLayout = 1;           // sidebar is the only layout now
    float accent[3] = { 0.55f, 0.30f, 0.95f }; // nyx purple
    float uiScale = 1.f;        // imgui font/global scale
    float aimbotSpeed = 1.f;    // mouse movement multiplier 0.1-3.0
};

extern Settings g_cfg;

inline const char* NYX_VERSION = "42";

namespace config
{
    void Load(const char* path = "config\\nyx.cfg");
    void Save(const char* path = "config\\nyx.cfg");
}
