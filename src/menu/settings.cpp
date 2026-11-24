#include "settings.h"
#include <fstream>
#include <sstream>
#include <unordered_map>

Settings g_cfg;

static std::unordered_map<std::string, std::string> read_kv(const char* path)
{
    std::unordered_map<std::string, std::string> kv;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

// safe parsers - stoi/stof throw on bad input and a single corrupt
// config line was crashing the exe on startup (the "won't reopen" bug)
static int toInt(const std::string& s, int def) { try { return std::stoi(s); } catch (...) { return def; } }
static float toFloat(const std::string& s, float def) { try { return std::stof(s); } catch (...) { return def; } }

void config::Load(const char* path)
{
    auto kv = read_kv(path);
    auto get = [&](const char* k, const char* def) {
        auto it = kv.find(k); return it == kv.end() ? std::string(def) : it->second;
    };
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    g_cfg.espBox = get("esp_box", "1") == "1";
    g_cfg.espName = get("esp_name", "1") == "1";
    g_cfg.espDistance = get("esp_distance", "1") == "1";
    g_cfg.espHealth = get("esp_health", "1") == "1";
    g_cfg.espSnaplines = get("esp_snaplines", "0") == "1";
    g_cfg.espTeamCheck = get("esp_teamcheck", "1") == "1";
    g_cfg.espShowTeammates = get("esp_teammates", "0") == "1";
    g_cfg.espKit = get("esp_kit", "1") == "1";
    g_cfg.espVisCheck = get("esp_vischeck", "0") == "1";
    g_cfg.espVisRay = get("esp_visray", "0") == "1";
    g_cfg.espChams = get("esp_chams", "0") == "1";
    g_cfg.espSkeleton = get("esp_skeleton", "0") == "1";
    g_cfg.objEsp = get("obj_esp", "1") == "1";
    g_cfg.objEspDist = toFloat(get("obj_esp_dist", "250"), 250.f);
    if (g_cfg.objEspDist < 25.f) g_cfg.objEspDist = 25.f;
    if (g_cfg.objEspDist > 1000.f) g_cfg.objEspDist = 1000.f;
    g_cfg.espHitbox = get("esp_hitbox", "0") == "1";
    g_cfg.espHitboxDist = toFloat(get("esp_hitbox_dist", "200"), 200.f);
    if (g_cfg.espHitboxDist < 25.f) g_cfg.espHitboxDist = 25.f;
    if (g_cfg.espHitboxDist > 500.f) g_cfg.espHitboxDist = 500.f;
    g_cfg.espHitboxColor = clampi(toInt(get("esp_hitbox_color", "0"), 0), 0, 2);
    g_cfg.aimbotEnabled = get("aimbot_enabled", "1") == "1";
    g_cfg.aimbotKey = toInt(get("aimbot_key", "88"), 88);
    g_cfg.aimbotMode = clampi(toInt(get("aimbot_mode", "1"), 1), 0, 2);
    g_cfg.aimbotFov = toFloat(get("aimbot_fov", "120"), 120.f);
    if (g_cfg.aimbotFov < 10.f) g_cfg.aimbotFov = 10.f;
    if (g_cfg.aimbotFov > 360.f) g_cfg.aimbotFov = 360.f;
    g_cfg.aimbotRange = toFloat(get("aimbot_range", "60"), 60.f);
    if (g_cfg.aimbotRange < 5.f) g_cfg.aimbotRange = 5.f;
    if (g_cfg.aimbotRange > 150.f) g_cfg.aimbotRange = 150.f;
    g_cfg.aimbotSmoothing = get("aimbot_smoothing", "1") == "1";
    g_cfg.aimbotUseFov = get("aimbot_usefov", "1") == "1";
    g_cfg.aimbotDrawFov = get("aimbot_drawfov", "0") == "1";
    g_cfg.aimbotRaycast = get("aimbot_raycast", "0") == "1";
    g_cfg.aimbotHuman = get("aimbot_human", "0") == "1";
    g_cfg.triggerAssist = get("trigger_assist", "0") == "1";
    g_cfg.triggerWall = get("trigger_wall", "0") == "1";
    g_cfg.aimbotFovColor = clampi(toInt(get("aimbot_fovcolor", "0"), 0), 0, 1);
    g_cfg.aimbotSmoothX = toFloat(get("aimbot_smooth_x", get("aimbot_smooth", "0.25").c_str()), 0.25f);
    g_cfg.aimbotSmoothY = toFloat(get("aimbot_smooth_y", get("aimbot_smooth", "0.25").c_str()), 0.25f);
    if (g_cfg.aimbotSmoothX < 0.f) g_cfg.aimbotSmoothX = 0.f;
    if (g_cfg.aimbotSmoothX > 1.f) g_cfg.aimbotSmoothX = 1.f;
    if (g_cfg.aimbotSmoothY < 0.f) g_cfg.aimbotSmoothY = 0.f;
    if (g_cfg.aimbotSmoothY > 1.f) g_cfg.aimbotSmoothY = 1.f;
    g_cfg.aimbotShake = get("aimbot_shake", "0") == "1";
    g_cfg.aimbotShakeX = toFloat(get("aimbot_shake_x", "3"), 3.f);
    g_cfg.aimbotShakeY = toFloat(get("aimbot_shake_y", "3"), 3.f);
    if (g_cfg.aimbotShakeX < 0.f) g_cfg.aimbotShakeX = 0.f;
    if (g_cfg.aimbotShakeX > 20.f) g_cfg.aimbotShakeX = 20.f;
    if (g_cfg.aimbotShakeY < 0.f) g_cfg.aimbotShakeY = 0.f;
    if (g_cfg.aimbotShakeY > 20.f) g_cfg.aimbotShakeY = 20.f;
    g_cfg.aimbotPrediction = get("aimbot_prediction", "0") == "1";
    g_cfg.aimbotPredMs = toFloat(get("aimbot_pred_ms", "150"), 150.f);
    if (g_cfg.aimbotPredMs < 0.f) g_cfg.aimbotPredMs = 0.f;
    if (g_cfg.aimbotPredMs > 500.f) g_cfg.aimbotPredMs = 500.f;
    g_cfg.aimbotWallCheck = get("aimbot_wallcheck", "0") == "1";
    g_cfg.aimbotTeamCheck = get("aimbot_teamcheck", "1") == "1";
    g_cfg.aimbotKnockCheck = get("aimbot_knockcheck", "1") == "1";
    g_cfg.aimbotHealthCheck = get("aimbot_healthcheck", "1") == "1";
    g_cfg.aimbotRangeCheck = get("aimbot_rangecheck", "1") == "1";
    g_cfg.hitboxEnabled = get("hitbox_enabled", "1") == "1";
    g_cfg.hitboxKey = toInt(get("hitbox_key", "67"), 67);
    g_cfg.hitboxMode = clampi(toInt(get("hitbox_mode", "2"), 2), 0, 2);
    g_cfg.hitboxMult = toFloat(get("hitbox_mult", "2.5"), 2.5f);
    if (g_cfg.hitboxMult < 1.f) g_cfg.hitboxMult = 1.f;
    if (g_cfg.hitboxMult > 6.f) g_cfg.hitboxMult = 6.f;
    g_cfg.hitboxMaxStuds = toFloat(get("hitbox_max", "14.4"), 14.4f);
    if (g_cfg.hitboxMaxStuds < 0.f) g_cfg.hitboxMaxStuds = 0.f;
    if (g_cfg.hitboxMaxStuds > 30.f) g_cfg.hitboxMaxStuds = 30.f;
    g_cfg.hitfixEnabled = get("hitfix_enabled", "0") == "1";
    g_cfg.aimbotSort = clampi(toInt(get("aimbot_sort", "0"), 0), 0, 3);
    g_cfg.bedwarsOnly = get("bedwars_only", "1") == "1";
    g_cfg.antiVoid = get("anti_void", "0") == "1";
    g_cfg.antiVoidDist = toFloat(get("anti_void_dist", "40"), 40.f);
    if (g_cfg.antiVoidDist < 10.f) g_cfg.antiVoidDist = 10.f;
    if (g_cfg.antiVoidDist > 120.f) g_cfg.antiVoidDist = 120.f;
    g_cfg.reachEnabled = get("reach_enabled", "0") == "1";
    g_cfg.reachStuds = toFloat(get("reach_studs", "4"), 4.f);
    if (g_cfg.reachStuds < 0.f) g_cfg.reachStuds = 0.f;
    if (g_cfg.reachStuds > 12.f) g_cfg.reachStuds = 12.f;
    g_cfg.streamproof = get("streamproof", "1") == "1";
    g_cfg.menuKey = toInt(get("menu_key", "161"), 161);
    g_cfg.uiTheme = clampi(toInt(get("ui_theme", "5"), 5), 0, 5);
    g_cfg.uiLayout = clampi(toInt(get("ui_layout", "1"), 1), 0, 1);
    g_cfg.uiScale = toFloat(get("ui_scale", "1"), 1.f);
    if (g_cfg.uiScale < 0.6f) g_cfg.uiScale = 0.6f;
    if (g_cfg.uiScale > 1.6f) g_cfg.uiScale = 1.6f;
    g_cfg.moveSpeedOn = get("move_speed_on", "0") == "1";
    g_cfg.moveSpeed = toFloat(get("move_speed", "24"), 24.f);
    if (g_cfg.moveSpeed < 16.f) g_cfg.moveSpeed = 16.f;
    if (g_cfg.moveSpeed > 100.f) g_cfg.moveSpeed = 100.f;
    g_cfg.jumpPowerOn = get("jump_on", "0") == "1";
    g_cfg.jumpPower = toFloat(get("jump_power", "50"), 50.f);
    if (g_cfg.jumpPower < 0.f) g_cfg.jumpPower = 0.f;
    if (g_cfg.jumpPower > 300.f) g_cfg.jumpPower = 300.f;
    g_cfg.flyOn = get("fly_on", "0") == "1";
    g_cfg.flyKey = toInt(get("fly_key", "86"), 86);
    g_cfg.flySpeed = toFloat(get("fly_speed", "40"), 40.f);
    if (g_cfg.flySpeed < 5.f) g_cfg.flySpeed = 5.f;
    if (g_cfg.flySpeed > 200.f) g_cfg.flySpeed = 200.f;
    g_cfg.spinOn = get("spin_on", "0") == "1";
    g_cfg.spinKey = toInt(get("spin_key", "66"), 66);
    g_cfg.spinSpeed = toFloat(get("spin_speed", "720"), 720.f);
    if (g_cfg.spinSpeed < 0.f) g_cfg.spinSpeed = 0.f;
    if (g_cfg.spinSpeed > 2000.f) g_cfg.spinSpeed = 2000.f;
    g_cfg.antiAfk = get("anti_afk", "0") == "1";
    g_cfg.aimbotSpeed = toFloat(get("aimbot_speed", "1"), 1.f);
    if (g_cfg.aimbotSpeed < 0.1f) g_cfg.aimbotSpeed = 0.1f;
    if (g_cfg.aimbotSpeed > 3.f) g_cfg.aimbotSpeed = 3.f;
    g_cfg.accent[0] = toFloat(get("accent_r", "0.55"), 0.55f);
    g_cfg.accent[1] = toFloat(get("accent_g", "0.30"), 0.30f);
    g_cfg.accent[2] = toFloat(get("accent_b", "0.95"), 0.95f);
}

void config::Save(const char* path)
{
    std::ofstream f(path, std::ios::trunc);
    f << "esp_box=" << g_cfg.espBox << "\n";
    f << "esp_name=" << g_cfg.espName << "\n";
    f << "esp_distance=" << g_cfg.espDistance << "\n";
    f << "esp_health=" << g_cfg.espHealth << "\n";
    f << "esp_snaplines=" << g_cfg.espSnaplines << "\n";
    f << "esp_teamcheck=" << g_cfg.espTeamCheck << "\n";
    f << "esp_teammates=" << g_cfg.espShowTeammates << "\n";
    f << "esp_kit=" << g_cfg.espKit << "\n";
    f << "esp_vischeck=" << g_cfg.espVisCheck << "\n";
    f << "esp_visray=" << g_cfg.espVisRay << "\n";
    f << "esp_chams=" << g_cfg.espChams << "\n";
    f << "esp_skeleton=" << g_cfg.espSkeleton << "\n";
    f << "obj_esp=" << g_cfg.objEsp << "\n";
    f << "obj_esp_dist=" << g_cfg.objEspDist << "\n";
    f << "esp_hitbox=" << g_cfg.espHitbox << "\n";
    f << "esp_hitbox_dist=" << g_cfg.espHitboxDist << "\n";
    f << "esp_hitbox_color=" << g_cfg.espHitboxColor << "\n";
    f << "aimbot_enabled=" << g_cfg.aimbotEnabled << "\n";
    f << "aimbot_key=" << g_cfg.aimbotKey << "\n";
    f << "aimbot_mode=" << g_cfg.aimbotMode << "\n";
    f << "aimbot_fov=" << g_cfg.aimbotFov << "\n";
    f << "aimbot_range=" << g_cfg.aimbotRange << "\n";
    f << "aimbot_smoothing=" << g_cfg.aimbotSmoothing << "\n";
    f << "aimbot_usefov=" << g_cfg.aimbotUseFov << "\n";
    f << "aimbot_drawfov=" << g_cfg.aimbotDrawFov << "\n";
    f << "aimbot_raycast=" << g_cfg.aimbotRaycast << "\n";
    f << "aimbot_human=" << g_cfg.aimbotHuman << "\n";
    f << "trigger_assist=" << g_cfg.triggerAssist << "\n";
    f << "trigger_wall=" << g_cfg.triggerWall << "\n";
    f << "aimbot_fovcolor=" << g_cfg.aimbotFovColor << "\n";
    f << "aimbot_smooth_x=" << g_cfg.aimbotSmoothX << "\n";
    f << "aimbot_smooth_y=" << g_cfg.aimbotSmoothY << "\n";
    f << "aimbot_shake=" << g_cfg.aimbotShake << "\n";
    f << "aimbot_shake_x=" << g_cfg.aimbotShakeX << "\n";
    f << "aimbot_shake_y=" << g_cfg.aimbotShakeY << "\n";
    f << "aimbot_wallcheck=" << g_cfg.aimbotWallCheck << "\n";
    f << "aimbot_prediction=" << g_cfg.aimbotPrediction << "\n";
    f << "aimbot_pred_ms=" << g_cfg.aimbotPredMs << "\n";
    f << "aimbot_teamcheck=" << g_cfg.aimbotTeamCheck << "\n";
    f << "aimbot_knockcheck=" << g_cfg.aimbotKnockCheck << "\n";
    f << "aimbot_healthcheck=" << g_cfg.aimbotHealthCheck << "\n";
    f << "aimbot_rangecheck=" << g_cfg.aimbotRangeCheck << "\n";
    f << "hitbox_enabled=" << g_cfg.hitboxEnabled << "\n";
    f << "hitbox_key=" << g_cfg.hitboxKey << "\n";
    f << "hitbox_mode=" << g_cfg.hitboxMode << "\n";
    f << "hitbox_mult=" << g_cfg.hitboxMult << "\n";
    f << "hitbox_max=" << g_cfg.hitboxMaxStuds << "\n";
    f << "hitfix_enabled=" << g_cfg.hitfixEnabled << "\n";
    f << "aimbot_sort=" << g_cfg.aimbotSort << "\n";
    f << "bedwars_only=" << g_cfg.bedwarsOnly << "\n";
    f << "anti_void=" << g_cfg.antiVoid << "\n";
    f << "anti_void_dist=" << g_cfg.antiVoidDist << "\n";
    f << "reach_enabled=" << g_cfg.reachEnabled << "\n";
    f << "reach_studs=" << g_cfg.reachStuds << "\n";
    f << "streamproof=" << g_cfg.streamproof << "\n";
    f << "menu_key=" << g_cfg.menuKey << "\n";
    f << "ui_theme=" << g_cfg.uiTheme << "\n";
    f << "ui_layout=" << g_cfg.uiLayout << "\n";
    f << "ui_scale=" << g_cfg.uiScale << "\n";
    f << "move_speed_on=" << g_cfg.moveSpeedOn << "\n";
    f << "move_speed=" << g_cfg.moveSpeed << "\n";
    f << "jump_on=" << g_cfg.jumpPowerOn << "\n";
    f << "jump_power=" << g_cfg.jumpPower << "\n";
    f << "fly_on=" << g_cfg.flyOn << "\n";
    f << "fly_key=" << g_cfg.flyKey << "\n";
    f << "fly_speed=" << g_cfg.flySpeed << "\n";
    f << "spin_on=" << g_cfg.spinOn << "\n";
    f << "spin_key=" << g_cfg.spinKey << "\n";
    f << "spin_speed=" << g_cfg.spinSpeed << "\n";
    f << "anti_afk=" << g_cfg.antiAfk << "\n";
    f << "aimbot_speed=" << g_cfg.aimbotSpeed << "\n";
    f << "accent_r=" << g_cfg.accent[0] << "\n";
    f << "accent_g=" << g_cfg.accent[1] << "\n";
    f << "accent_b=" << g_cfg.accent[2] << "\n";
}
