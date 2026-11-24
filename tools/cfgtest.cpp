#include "settings.h"
#include <cstdio>
#include <fstream>
int main() {
    // non-default values for every audited field
    g_cfg.aimbotShake = true; g_cfg.aimbotShakeX = 7.5f; g_cfg.aimbotShakeY = 2.5f;
    g_cfg.aimbotSmoothX = 0.6f; g_cfg.aimbotSmoothY = 0.4f;
    g_cfg.aimbotSpeed = 2.0f; g_cfg.aimbotFov = 200.f; g_cfg.aimbotPredMs = 300.f;
    g_cfg.aimbotSort = 3; g_cfg.espHitboxColor = 2; g_cfg.uiScale = 1.3f;
    config::Save("cfgtest_out.cfg");
    // mutate then reload
    g_cfg.aimbotShake = false; g_cfg.aimbotSmoothX = 0.1f; g_cfg.aimbotSpeed = 0.5f;
    g_cfg.aimbotSort = 0; g_cfg.uiScale = 1.0f;
    config::Load("cfgtest_out.cfg");
    int fails = 0;
    auto chk = [&](const char* n, bool ok){ printf("%s %s\n", ok?"ok  ":"FAIL", n); if(!ok) fails++; };
    chk("aimbot_shake restored", g_cfg.aimbotShake == true);
    chk("shake_x", g_cfg.aimbotShakeX == 7.5f);
    chk("shake_y", g_cfg.aimbotShakeY == 2.5f);
    chk("smooth_x", g_cfg.aimbotSmoothX == 0.6f);
    chk("smooth_y", g_cfg.aimbotSmoothY == 0.4f);
    chk("speed", g_cfg.aimbotSpeed == 2.0f);
    chk("fov", g_cfg.aimbotFov == 200.f);
    chk("pred_ms", g_cfg.aimbotPredMs == 300.f);
    chk("sort=3 (None) persists", g_cfg.aimbotSort == 3);
    chk("hitbox_color", g_cfg.espHitboxColor == 2);
    chk("ui_scale", g_cfg.uiScale == 1.3f);
    // clamp checks: write out-of-range, expect clamp
    { std::ofstream f("cfgtest_bad.cfg"); f << "aimbot_fov=9999\naimbot_smooth_x=5\naimbot_speed=99\nui_scale=9\n"; }
    config::Load("cfgtest_bad.cfg");
    chk("fov clamped <=360", g_cfg.aimbotFov <= 360.f);
    chk("smooth_x clamped <=1", g_cfg.aimbotSmoothX <= 1.f);
    chk("speed clamped <=3", g_cfg.aimbotSpeed <= 3.f);
    chk("ui_scale clamped <=1.6", g_cfg.uiScale <= 1.6f);
    printf(fails ? "CONFIG TEST FAILED (%d)\n" : "CONFIG TEST PASSED\n", fails);
    return fails;
}
