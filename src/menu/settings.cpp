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

void config::Load(const char* path)
{
    auto kv = read_kv(path);
    auto get = [&](const char* k, const char* def) {
        auto it = kv.find(k); return it == kv.end() ? std::string(def) : it->second;
    };
    g_cfg.espBox = get("esp_box", "1") == "1";
    g_cfg.espName = get("esp_name", "1") == "1";
    g_cfg.espDistance = get("esp_distance", "1") == "1";
    g_cfg.espHealth = get("esp_health", "1") == "1";
    g_cfg.espSnaplines = get("esp_snaplines", "0") == "1";
    g_cfg.espTeamCheck = get("esp_teamcheck", "1") == "1";
    g_cfg.espShowTeammates = get("esp_teammates", "0") == "1";
    g_cfg.espKit = get("esp_kit", "1") == "1";
    g_cfg.aimbotEnabled = get("aimbot_enabled", "1") == "1";
    g_cfg.aimbotKey = std::stoi(get("aimbot_key", "88"));
    g_cfg.aimbotMode = std::stoi(get("aimbot_mode", "1"));
    g_cfg.aimbotFov = std::stof(get("aimbot_fov", "120"));
    g_cfg.aimbotSmooth = std::stof(get("aimbot_smooth", "0.25"));
    g_cfg.aimbotRange = std::stof(get("aimbot_range", "30"));
    if (g_cfg.aimbotRange > 30.f) g_cfg.aimbotRange = 30.f;
    g_cfg.aimbotSmoothing = get("aimbot_smoothing", "1") == "1";
    g_cfg.aimbotSmoothStyle = std::stoi(get("aimbot_style", "1"));
    g_cfg.aimbotShake = get("aimbot_shake", "0") == "1";
    g_cfg.aimbotShakePx = std::stof(get("aimbot_shake_px", "3"));
    g_cfg.aimbotPrediction = get("aimbot_prediction", "0") == "1";
    g_cfg.aimbotPredMs = std::stof(get("aimbot_pred_ms", "150"));
    g_cfg.aimbotTeamCheck = get("aimbot_teamcheck", "1") == "1";
    g_cfg.aimbotKnockCheck = get("aimbot_knockcheck", "1") == "1";
    g_cfg.aimbotHealthCheck = get("aimbot_healthcheck", "1") == "1";
    g_cfg.aimbotRangeCheck = get("aimbot_rangecheck", "1") == "1";
    g_cfg.triggerbotEnabled = get("trigger_enabled", "1") == "1";
    g_cfg.triggerKey = std::stoi(get("trigger_key", "88"));
    g_cfg.triggerMode = std::stoi(get("trigger_mode", "1"));
    g_cfg.triggerFov = std::stof(get("trigger_fov", "30"));
    g_cfg.hitboxEnabled = get("hitbox_enabled", "1") == "1";
    g_cfg.hitboxKey = std::stoi(get("hitbox_key", "67"));
    g_cfg.hitboxMode = std::stoi(get("hitbox_mode", "2"));
    g_cfg.hitboxMult = std::stof(get("hitbox_mult", "2.5"));
    g_cfg.hitboxMaxStuds = std::stof(get("hitbox_max", "0"));
    g_cfg.streamproof = get("streamproof", "1") == "1";
    g_cfg.menuKey = std::stoi(get("menu_key", "161"));
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
    f << "streamproof=" << g_cfg.streamproof << "\n";
    f << "menu_key=" << g_cfg.menuKey << "\n";
}
