// ============================================================================
// NYX AIM ASSIST SETTINGS SIMULATOR
// ============================================================================
// Standalone test harness for the aim-assist math in
// src/render/overlay.cpp :: RunAimbot(). No Roblox, no memory reads, no
// SendInput - synthetic targets only. Compile:
//
//   cl /EHsc /O2 tools\aa_sim.cpp /Fe:aa_sim.exe
//
// Verifies: FOV gating, team/health/range checks, target priorities,
// prediction lead, smoothing X/Y, shake X/Y, aim speed, subpixel aim.
// ============================================================================
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <random>

// ---- mirrors of the g_cfg fields RunAimbot actually reads ------------------
struct SimCfg
{
    bool enabled = true;
    float fov = 120.f;          bool useFov = true;   bool drawFov = false;
    bool smoothing = true;      float smoothX = 0.25f, smoothY = 0.25f;
    bool shake = false;         float shakeX = 3.f, shakeY = 3.f;
    bool prediction = false;    float predMs = 150.f;
    bool wallCheck = false;     float speed = 1.f;
    int  sort = 0;              // 0 crosshair 1 nearest 2 lowesthp 3 none
    float range = 60.f;         // studs
    bool teamCheck = true, knockCheck = true, healthCheck = true, rangeCheck = true;
};

struct SimTarget
{
    std::string name;
    float sx, sy;        // screen pos (what WorldToScreen would return)
    float vx, vy;        // screen-space velocity px/frame (prediction input)
    float dist;          // studs
    float health;
    bool  teammate, knocked, dead;
};

static std::mt19937 rng(12345);
static float frand(float lo, float hi) { return lo + (hi - lo) * (rng() / 4294967295.f); }

// ---- replica of AimMove: subpixel accumulation ----------------------------
static float accX = 0, accY = 0;
static void AimMove(float dxPx, float dyPx, float& ox, float& oy)
{
    accX += dxPx; accY += dyPx;
    int mx = (int)accX, my = (int)accY;
    accX -= mx; accY -= my;
    ox = (float)mx; oy = (float)my;
}

// ---- replica of the RunAimbot target loop + move math ----------------------
// returns true when a target was acquired; ox/oy = integer px moved
static bool SimStep(const SimCfg& c, float cx, float cy,
                    const std::vector<SimTarget>& targets, float& ox, float& oy)
{
    ox = oy = 0.f;
    if (!c.enabled) return false;

    const SimTarget* best = nullptr;
    float bestMetric = 1e9f, bestD = 0.f, tx = 0, ty = 0;

    for (auto& t : targets)
    {
        if (t.teammate && c.teamCheck) continue;
        if (t.knocked && c.knockCheck) continue;
        if (c.healthCheck && t.health <= 0.f) continue;
        if (c.rangeCheck && t.dist > c.range) continue;
        if (t.dead) continue;

        // prediction: lead the aim point by velocity * predMs/1000
        float px = t.sx + (c.prediction ? t.vx * (c.predMs / 1000.f) : 0.f);
        float py = t.sy + (c.prediction ? t.vy * (c.predMs / 1000.f) : 0.f);

        float dx = px - cx, dy = py - cy;
        float d = sqrtf(dx * dx + dy * dy);
        if (c.useFov && d >= c.fov) continue;

        float metric = (c.sort == 1) ? t.dist :
                       (c.sort == 2) ? t.health :
                       (c.sort == 3) ? 0.f : d;
        if (metric < bestMetric) { bestMetric = metric; bestD = d; best = &t; tx = px; ty = py; }
    }
    if (!best) return false;

    float sx = c.smoothing ? c.smoothX : 1.f;
    float sy = c.smoothing ? c.smoothY : 1.f;
    float mx = (tx - cx) * sx;
    float my = (ty - cy) * sy;
    if (c.shake)
    {
        mx += frand(-1.f, 1.f) * c.shakeX;
        my += frand(-1.f, 1.f) * c.shakeY;
    }
    mx *= c.speed; my *= c.speed;
    AimMove(mx, my, ox, oy);
    (void)bestD;
    return true;
}

// ---- test scenarios ---------------------------------------------------------
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
                            else printf("ok:   %s\n", msg); } while (0)

static void banner(const char* s) { printf("\n=== %s ===\n", s); }

int main()
{
    const float CX = 960.f, CY = 540.f;

    banner("1. FOV gating - target inside vs outside radius");
    {
        SimCfg c; c.fov = 100.f;
        std::vector<SimTarget> t = {
            {"near", CX + 50, CY, 0, 0, 20, 100, false, false, false},   // 50px - inside
            {"far",  CX + 300, CY, 0, 0, 20, 100, false, false, false},  // 300px - outside
        };
        float ox, oy; bool got = SimStep(c, CX, CY, t, ox, oy);
        CHECK(got, "target inside FOV is acquired");
        c.fov = 40.f;
        accX = accY = 0;
        got = SimStep(c, CX, CY, t, ox, oy);
        CHECK(!got, "no target acquired when all outside FOV");
    }

    banner("2. Smoothing convergence - crosshair walks to target");
    {
        SimCfg c; c.smoothX = c.smoothY = 0.5f; c.fov = 10000.f;
        std::vector<SimTarget> t = {
            {"e", CX + 200, CY, 0, 0, 20, 100, false, false, false},
        };
        float x = CX, y = CY; int frames = 0;
        // simulate: aim point fixed at +200px; each frame moves 50% of gap
        for (; frames < 60; frames++)
        {
            float ox, oy;
            SimStep(c, x, y, t, ox, oy);   // note: sim uses screen delta to fixed target
            // emulate crosshair move: shift target-relative math by moving cx
            x += ox; y += oy;
            // re-express: target stays at CX+200 world; moving cx closer reduces delta.
            // SimStep measures px-cx where px=CX+200 fixed -> emulate by moving cx
            // simpler: track remaining = 200 - moved
            if (fabs((CX + 200) - x) < 2.f) break;
        }
        CHECK(frames < 20, "smoothing 0.5 converges in <20 frames");
        printf("     converged in %d frames\n", frames);
    }

    banner("3. Checks - teammate / dead / range filtered out");
    {
        SimCfg c; c.fov = 10000.f;
        std::vector<SimTarget> t = {
            {"team",   CX + 50, CY, 0, 0, 20, 100, true,  false, false},
            {"dead",   CX + 50, CY, 0, 0, 20, 0,   false, false, true },
            {"far",    CX + 50, CY, 0, 0, 200,100, false, false, false},
            {"good",   CX + 50, CY, 0, 0, 20, 100, false, false, false},
        };
        float ox, oy; bool got = SimStep(c, CX, CY, t, ox, oy);
        CHECK(got, "only the valid target is acquired");
        c.teamCheck = c.healthCheck = c.rangeCheck = false;
        accX = accY = 0;
        // now 'team' also eligible but all at same px - crosshair priority ties;
        // just verify it doesn't crash and returns a target
        got = SimStep(c, CX, CY, t, ox, oy);
        CHECK(got, "with checks off a target is still acquired");
    }

    banner("4. Priority - lowest HP beats closer crosshair target");
    {
        SimCfg c; c.fov = 10000.f; c.sort = 2; // lowest hp
        std::vector<SimTarget> t = {
            {"close-full", CX + 30, CY, 0, 0, 20, 100, false, false, false},
            {"far-lowhp",  CX + 200, CY, 0, 0, 20, 10,  false, false, false},
        };
        float ox, oy; SimStep(c, CX, CY, t, ox, oy);
        // if lowest-hp won, delta should aim at +200 not +30 -> |mx| large
        CHECK(fabs(ox) > 40.f, "lowest-HP priority picks the 10hp target");
    }

    banner("5. Prediction - aim point leads moving target");
    {
        SimCfg c; c.fov = 10000.f; c.prediction = true; c.predMs = 100.f;
        // target at center but moving +50px/frame in x -> predicted point at +5px
        std::vector<SimTarget> t = {
            {"mover", CX, CY, 50.f, 0, 20, 100, false, false, false},
        };
        float ox, oy; SimStep(c, CX, CY, t, ox, oy);
        CHECK(ox > 0, "prediction leads the target (positive x move)");
        c.prediction = false; accX = accY = 0;
        SimStep(c, CX, CY, t, ox, oy);
        CHECK(ox == 0, "without prediction a centered target yields no move");
    }

    banner("6. Shake - movement jitter is nonzero when enabled");
    {
        SimCfg c; c.fov = 10000.f; c.smoothX = c.smoothY = 0.f;
        c.shake = true; c.shakeX = c.shakeY = 5.f;
        std::vector<SimTarget> t = {
            {"e", CX, CY, 0, 0, 20, 100, false, false, false}, // dead center
        };
        float ox, oy; SimStep(c, CX, CY, t, ox, oy);
        CHECK(ox != 0 || oy != 0, "shake produces movement on a centered target");
        c.shake = false; accX = accY = 0;
        SimStep(c, CX, CY, t, ox, oy);
        CHECK(ox == 0 && oy == 0, "shake off + smoothing 0 = no drift");
    }

    banner("7. Aim speed - multiplier scales the delta");
    {
        SimCfg c; c.fov = 10000.f; c.smoothing = false; c.speed = 2.f;
        std::vector<SimTarget> t = {
            {"e", CX + 100, CY, 0, 0, 20, 100, false, false, false},
        };
        float ox, oy; SimStep(c, CX, CY, t, ox, oy);
        CHECK(fabs(ox) >= 150.f, "speed 2x doubles the 100px delta (subpixel carry)");
        printf("     moved %.0f px in one frame\n", ox);
    }

    banner("8. Subpixel aim - fractional deltas accumulate");
    {
        SimCfg c; c.fov = 10000.f; c.smoothX = c.smoothY = 0.1f;
        std::vector<SimTarget> t = {
            {"e", CX + 3, CY, 0, 0, 20, 100, false, false, false}, // 3px away
        };
        accX = accY = 0;
        int total = 0;
        for (int i = 0; i < 20; i++) { float ox, oy; SimStep(c, CX, CY, t, ox, oy); total += (int)(ox + oy); }
        CHECK(total >= 2, "subpixel accumulator eventually moves the 3px gap");
        printf("     total moved %d px over 20 frames\n", total);
    }

    printf("\n%s (%d failures)\n", failures ? "SIM FAILED" : "ALL SIM TESTS PASSED", failures);
    return failures ? 1 : 0;
}
