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
    g_cfg.objEsp = get("obj_esp", "1") == "1";
    g_cfg.objEspDist = toFloat(get("obj_esp_dist", "250"), 250.f);
    g_cfg.espHitbox = get("esp_hitbox", "0") == "1";
    g_cfg.espHitboxDist = toFloat(get("esp_hitbox_dist", "200"), 200.f);
    g_cfg.aimbotEnabled = get("aimbot_enabled", "1") == "1";
    g_cfg.aimbotKey = toInt(get("aimbot_key", "88"), 88);
    g_cfg.aimbotMode = clampi(toInt(get("aimbot_mode", "1"), 1), 0, 2);
    g_cfg.aimbotFov = toFloat(get("aimbot_fov", "120"), 120.f);
    g_cfg.aimbotSmooth = toFloat(get("aimbot_smooth", "0.25"), 0.25f);
    g_cfg.aimbotRange = toFloat(get("aimbot_range", "60"), 60.f);
    if (g_cfg.aimbotRange > 150.f) g_cfg.aimbotRange = 150.f;
    g_cfg.aimbotSmoothing = get("aimbot_smoothing", "1") == "1";
    g_cfg.aimbotSmoothStyle = clampi(toInt(get("aimbot_style", "1"), 1), 0, 1);
    g_cfg.aimbotShake = get("aimbot_shake", "0") == "1";
    g_cfg.aimbotShakePx = toFloat(get("aimbot_shake_px", "3"), 3.f);
    g_cfg.aimbotPrediction = get("aimbot_prediction", "0") == "1";
    g_cfg.aimbotPredMs = toFloat(get("aimbot_pred_ms", "150"), 150.f);
    g_cfg.aimbotTeamCheck = get("aimbot_teamcheck", "1") == "1";
    g_cfg.aimbotKnockCheck = get("aimbot_knockcheck", "1") == "1";
    g_cfg.aimbotHealthCheck = get("aimbot_healthcheck", "1") == "1";
    g_cfg.aimbotRangeCheck = get("aimbot_rangecheck", "1") == "1";
    g_cfg.triggerbotEnabled = get("trigger_enabled", "1") == "1";
    g_cfg.triggerKey = toInt(get("trigger_key", "88"), 88);
    g_cfg.triggerMode = clampi(toInt(get("trigger_mode", "1"), 1), 0, 2);
    g_cfg.triggerFov = toFloat(get("trigger_fov", "30"), 30.f);
    g_cfg.hitboxEnabled = get("hitbox_enabled", "1") == "1";
    g_cfg.hitboxKey = toInt(get("hitbox_key", "67"), 67);
    g_cfg.hitboxMode = clampi(toInt(get("hitbox_mode", "2"), 2), 0, 2);
    g_cfg.hitboxMult = toFloat(get("hitbox_mult", "2.5"), 2.5f);
    g_cfg.hitboxMaxStuds = toFloat(get("hitbox_max", "0"), 0.f);
    g_cfg.hitfixEnabled = get("hitfix_enabled", "0") == "1";
    g_cfg.silentBow = get("silent_bow", "0") == "1";
    g_cfg.killauraEnabled = get("killaura_enabled", "0") == "1";
    g_cfg.kaKey = toInt(get("ka_key", "86"), 86);
    g_cfg.kaMode = clampi(toInt(get("ka_mode", "1"), 1), 0, 2);
    g_cfg.kaCps = toFloat(get("ka_cps", "12"), 12.f);
    g_cfg.kaSwingRange = toFloat(get("ka_swing", "18"), 18.f);
    g_cfg.kaAttackRange = toFloat(get("ka_attack", "18"), 18.f);
    g_cfg.kaAngle = toFloat(get("ka_angle", "90"), 90.f);
    g_cfg.kaSmooth = toFloat(get("ka_smooth", "0.35"), 0.35f);
    g_cfg.kaPredMs = toFloat(get("ka_pred_ms", "80"), 80.f);
    g_cfg.kaClickPx = toFloat(get("ka_click_px", "25"), 25.f);
    g_cfg.kaRequireMouse = get("ka_requiremouse", "0") == "1";
    g_cfg.kaTeamCheck = get("ka_teamcheck", "1") == "1";
    g_cfg.aimbotSort = clampi(toInt(get("aimbot_sort", "0"), 0), 0, 2);
    g_cfg.bedwarsOnly = get("bedwars_only", "1") == "1";
    g_cfg.antiVoid = get("anti_void", "0") == "1";
    g_cfg.antiVoidDist = toFloat(get("anti_void_dist", "40"), 40.f);
    g_cfg.reachEnabled = get("reach_enabled", "0") == "1";
    g_cfg.reachStuds = toFloat(get("reach_studs", "4"), 4.f);
    g_cfg.streamproof = get("streamproof", "1") == "1";
    g_cfg.menuKey = toInt(get("menu_key", "161"), 161);
    g_cfg.uiTheme = clampi(toInt(get("ui_theme", "0"), 0), 0, 4);
    g_cfg.uiLayout = clampi(toInt(get("ui_layout", "0"), 0), 0, 1);
    g_cfg.accent[0] = toFloat(get("accent_r", "0.35"), 0.35f);
    g_cfg.accent[1] = toFloat(get("accent_g", "0.75"), 0.75f);
    g_cfg.accent[2] = toFloat(get("accent_b", "1.0"), 1.0f);
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
    f << "obj_esp=" << g_cfg.objEsp << "\n";
    f << "obj_esp_dist=" << g_cfg.objEspDist << "\n";
    f << "esp_hitbox=" << g_cfg.espHitbox << "\n";
    f << "esp_hitbox_dist=" << g_cfg.espHitboxDist << "\n";
    f << "aimbot_enabled=" << g_cfg.aimbotEnabled << "\n";
    f << "aimbot_key=" << g_cfg.aimbotKey << "\n";
    f << "aimbot_mode=" << g_cfg.aimbotMode << "\n";
    f << "aimbot_fov=" << g_cfg.aimbotFov << "\n";
    f << "aimbot_smooth=" << g_cfg.aimbotSmooth << "\n";
    f << "aimbot_range=" << g_cfg.aimbotRange << "\n";
    f << "aimbot_smoothing=" << g_cfg.aimbotSmoothing << "\n";
    f << "aimbot_style=" << g_cfg.aimbotSmoothStyle << "\n";
    f << "aimbot_shake=" << g_cfg.aimbotShake << "\n";
    f << "aimbot_shake_px=" << g_cfg.aimbotShakePx << "\n";
    f << "aimbot_prediction=" << g_cfg.aimbotPrediction << "\n";
    f << "aimbot_pred_ms=" << g_cfg.aimbotPredMs << "\n";
    f << "aimbot_teamcheck=" << g_cfg.aimbotTeamCheck << "\n";
    f << "aimbot_knockcheck=" << g_cfg.aimbotKnockCheck << "\n";
    f << "aimbot_healthcheck=" << g_cfg.aimbotHealthCheck << "\n";
    f << "aimbot_rangecheck=" << g_cfg.aimbotRangeCheck << "\n";
    f << "trigger_enabled=" << g_cfg.triggerbotEnabled << "\n";
    f << "trigger_key=" << g_cfg.triggerKey << "\n";
    f << "trigger_mode=" << g_cfg.triggerMode << "\n";
    f << "trigger_fov=" << g_cfg.triggerFov << "\n";
    f << "hitbox_enabled=" << g_cfg.hitboxEnabled << "\n";
    f << "hitbox_key=" << g_cfg.hitboxKey << "\n";
    f << "hitbox_mode=" << g_cfg.hitboxMode << "\n";
    f << "hitbox_mult=" << g_cfg.hitboxMult << "\n";
    f << "hitbox_max=" << g_cfg.hitboxMaxStuds << "\n";
    f << "hitfix_enabled=" << g_cfg.hitfixEnabled << "\n";
    f << "silent_bow=" << g_cfg.silentBow << "\n";
    f << "killaura_enabled=" << g_cfg.killauraEnabled << "\n";
    f << "ka_key=" << g_cfg.kaKey << "\n";
    f << "ka_mode=" << g_cfg.kaMode << "\n";
    f << "ka_cps=" << g_cfg.kaCps << "\n";
    f << "ka_swing=" << g_cfg.kaSwingRange << "\n";
    f << "ka_attack=" << g_cfg.kaAttackRange << "\n";
    f << "ka_angle=" << g_cfg.kaAngle << "\n";
    f << "ka_smooth=" << g_cfg.kaSmooth << "\n";
    f << "ka_pred_ms=" << g_cfg.kaPredMs << "\n";
    f << "ka_click_px=" << g_cfg.kaClickPx << "\n";
    f << "ka_requiremouse=" << g_cfg.kaRequireMouse << "\n";
    f << "ka_teamcheck=" << g_cfg.kaTeamCheck << "\n";
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
    f << "accent_r=" << g_cfg.accent[0] << "\n";
    f << "accent_g=" << g_cfg.accent[1] << "\n";
    f << "accent_b=" << g_cfg.accent[2] << "\n";
}
