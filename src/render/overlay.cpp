#include "overlay.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "memory.h"
#include "offsets.h"
#include "sdk.h"
#include "settings.h"
#include <dwmapi.h>
#include <thread>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <chrono>
#include <mutex>
#include <cstdarg>
#include <cstdlib>
#include <cctype>

static void Log(const char* fmt, ...)
{
    static std::mutex logMtx;
    std::lock_guard<std::mutex> lk(logMtx);
    std::ofstream f("nyx_debug.log", std::ios::app);
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    tm lt{}; localtime_s(&lt, &t);
    char tb[32]; strftime(tb, sizeof(tb), "%H:%M:%S", &lt);
    f << "[" << tb << "] ";
    va_list ap; va_start(ap, fmt);
    char buf[512]; vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    f << buf << "\n";
}

#define OFF(cls, field) offsets::Get(cls, field)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

Overlay g_overlay;
static std::atomic<bool> g_running{ true };
static int g_width = 0, g_height = 0;

// ---------------- esp/aim data ----------------

struct PlayerBox
{
    float x1, y1, x2, y2;
    float headX, headY;
    Vec3 headWorld, headVel;
    std::string name;
    float health, maxHealth;
    float dist;
    bool teammate;
    uintptr_t headAddr;
    std::string kit;
    ImU32 kitCol = 0;
};

static std::vector<PlayerBox> g_players;
static bool g_localKnocked = false;
static Vec2 g_localScreen;
static float g_localX, g_localY; // screen center of roblox client

// ---------------- kit esp ----------------
// bedwars kits + generic kit names; matched case-insensitively against
// player/backpack/character child names (tools, values, folders)
struct KitEntry { const char* key; const char* label; ImU32 col; };
static const KitEntry kKits[] = {
    // bedwars kits
    { "void_knight", "VOID KNIGHT", IM_COL32(120, 90, 220, 255) },
    { "voidknight", "VOID KNIGHT", IM_COL32(120, 90, 220, 255) },
    { "block_kicker", "BLOCK KICKER", IM_COL32(200, 160, 60, 255) },
    { "blockkicker", "BLOCK KICKER", IM_COL32(200, 160, 60, 255) },
    { "elk_master", "ELK MASTER", IM_COL32(160, 110, 60, 255) },
    { "elkmaster", "ELK MASTER", IM_COL32(160, 110, 60, 255) },
    { "summoner", "SUMMONER", IM_COL32(90, 200, 120, 255) },
    { "paladin", "PALADIN", IM_COL32(230, 200, 80, 255) },
    { "davey", "DAVEY", IM_COL32(90, 140, 220, 255) },
    { "airbender", "AIRBENDER", IM_COL32(120, 200, 230, 255) },
    { "hatter", "HATTER", IM_COL32(140, 100, 160, 255) },
    { "mage", "MAGE", IM_COL32(90, 120, 230, 255) },
    { "archer", "ARCHER", IM_COL32(90, 170, 90, 255) },
    { "assassin", "ASSASSIN", IM_COL32(60, 60, 70, 255) },
    { "emerald", "EMERALD", IM_COL32(40, 200, 120, 255) },
    { "fireball", "FIREBALL", IM_COL32(230, 90, 40, 255) },
    { "pyromancer", "PYROMANCER", IM_COL32(230, 90, 40, 255) },
    { "frosty", "FROSTY", IM_COL32(140, 200, 240, 255) },
    { "gravity", "GRAVITY", IM_COL32(120, 120, 140, 255) },
    { "healer", "HEALER", IM_COL32(240, 240, 240, 255) },
    { "invis", "INVIS", IM_COL32(160, 160, 190, 255) },
    { "knight", "KNIGHT", IM_COL32(170, 170, 190, 255) },
    { "lucky", "LUCKY", IM_COL32(240, 210, 60, 255) },
    { "mason", "MASON", IM_COL32(160, 120, 80, 255) },
    { "shepard", "SHEPARD", IM_COL32(200, 140, 60, 255) },
    { "smelly", "SMELLY", IM_COL32(150, 190, 80, 255) },
    { "soapy", "SOAPY", IM_COL32(200, 220, 240, 255) },
    { "spider", "SPIDER", IM_COL32(80, 60, 50, 255) },
    { "tnt", "TNT", IM_COL32(220, 60, 40, 255) },
    { "warrior", "WARRIOR", IM_COL32(200, 80, 60, 255) },
    { "wizard", "WIZARD", IM_COL32(140, 100, 230, 255) },
    // single/metal kits
    { "metal", "METAL", IM_COL32(160, 160, 170, 255) },
    { "elder", "ELDER", IM_COL32(170, 130, 230, 255) },
    { "iron", "IRON", IM_COL32(190, 190, 200, 255) },
    { "steel", "STEEL", IM_COL32(170, 180, 190, 255) },
    { "bronze", "BRONZE", IM_COL32(200, 150, 90, 255) },
    { "copper", "COPPER", IM_COL32(220, 140, 80, 255) },
    { "silver", "SILVER", IM_COL32(220, 225, 235, 255) },
    { "gold", "GOLD", IM_COL32(240, 200, 60, 255) },
    { "diamond", "DIAMOND", IM_COL32(90, 200, 240, 255) },
    { "ruby", "RUBY", IM_COL32(220, 40, 70, 255) },
    { "sapphire", "SAPPHIRE", IM_COL32(50, 100, 230, 255) },
    { "topaz", "TOPAZ", IM_COL32(240, 200, 90, 255) },
    { "amethyst", "AMETHYST", IM_COL32(150, 80, 200, 255) },
    { "quartz", "QUARTZ", IM_COL32(235, 235, 240, 255) },
    { "jade", "JADE", IM_COL32(60, 190, 110, 255) },
    { "onyx", "ONYX", IM_COL32(40, 40, 50, 255) },
    { "pearl", "PEARL", IM_COL32(240, 235, 230, 255) },
    { "opal", "OPAL", IM_COL32(200, 180, 240, 255) },
    { "netherite", "NETHERITE", IM_COL32(70, 60, 60, 255) },
};

static const KitEntry* MatchKit(const std::string& lower)
{
    for (auto& k : kKits)
        if (lower.find(k.key) != std::string::npos) return &k;
    return nullptr;
}

static std::string ToLower(std::string s)
{
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// per-player cache: part/humanoid addresses only change on respawn,
// so re-resolve them at ~2hz instead of enumerating children every frame
struct CachedPlayer
{
    uintptr_t player = 0, character = 0;
    uintptr_t head = 0, hrp = 0, humanoid = 0;
    std::string name;
    std::string kit;
    ImU32 kitCol = 0;
    DWORD refreshAt = 0;
};
static std::unordered_map<uintptr_t, CachedPlayer> g_cache;

static void RefreshPlayer(CachedPlayer& cp, const Instance& p)
{
    cp.player = p.addr;
    cp.character = p.GetCharacter().addr;
    cp.head = cp.hrp = cp.humanoid = 0;
    cp.refreshAt = GetTickCount() + 500;

    Instance ch(cp.character);
    if (!ch) return;

    cp.kit.clear();
    auto tryKit = [&](const std::string& n) {
        if (cp.kit.empty())
            if (auto* k = MatchKit(ToLower(n))) { cp.kit = k->label; cp.kitCol = k->col; }
    };

    // player children (backpack, gui, values) then backpack contents then character
    for (auto& c : p.GetChildren())
    {
        tryKit(c.GetName());
        if (ToLower(c.GetName()).find("backpack") != std::string::npos)
            for (auto& b : c.GetChildren()) tryKit(b.GetName());
    }

    for (auto& c : ch.GetChildren())
    {
        std::string cn = c.GetName();
        tryKit(cn);
        if (cn == "Head") cp.head = c.addr;
        else if (cn == "HumanoidRootPart") cp.hrp = c.addr;
        else if (cn == "Torso" && !cp.hrp) cp.hrp = c.addr;
        if (cp.humanoid == 0 && c.GetClassName() == "Humanoid") cp.humanoid = c.addr;
    }
    cp.name = p.GetName();
}

static Vec3 CachedLocalPos();

static void CollectPlayers()
{
    g_players.clear();
    auto& w = sdk::Get();
    if (!w.localPlayer) { g_cache.clear(); return; }

    uintptr_t localTeam = g_mem.ReadPtr(w.localPlayer + OFF("Player", "Team"));
    Vec3 localPos = CachedLocalPos();

    // local knock state (FallingDown=0, Ragdoll=1, GettingUp=2)
    g_localKnocked = false;
    if (g_cfg.aimbotKnockCheck)
    {
        static uintptr_t cachedChar = 0, cachedHum = 0;
        static DWORD humRefresh = 0;
        DWORD t = GetTickCount();
        auto lch = Instance(w.localPlayer).GetCharacter();
        if (lch)
        {
            if (lch.addr != cachedChar || t >= humRefresh || !cachedHum)
            {
                cachedChar = lch.addr; humRefresh = t + 500; cachedHum = 0;
                auto hum = lch.FindFirstChildOfClass("Humanoid");
                cachedHum = hum.addr;
            }
            if (cachedHum)
            {
                int st = g_mem.Read<int>(cachedHum + OFF("Humanoid", "HumanoidStateID"));
                g_localKnocked = (st >= 0 && st <= 2);
            }
        }
    }

    HWND rbx = FindWindowW(nullptr, L"Roblox");
    if (!rbx) return;
    RECT rc{}; POINT pt{};
    GetClientRect(rbx, &rc); ClientToScreen(rbx, &pt);
    float rw = (float)(rc.right - rc.left), rh = (float)(rc.bottom - rc.top);
    g_localX = pt.x + rw / 2.f; g_localY = pt.y + rh / 2.f;

    DWORD now = GetTickCount();
    std::unordered_set<uintptr_t> alive;

    for (auto& p : sdk::GetPlayers())
    {
        if (p.addr == w.localPlayer) continue;
        alive.insert(p.addr);

        auto it = g_cache.find(p.addr);
        if (it == g_cache.end()) { g_cache[p.addr] = CachedPlayer{}; it = g_cache.find(p.addr); }
        CachedPlayer& cp = it->second;

        // cheap check: did the character pointer change (respawn)?
        uintptr_t curChar = g_mem.ReadPtr(p.addr + OFF("Player", "ModelInstance"));
        if (cp.character != curChar || now >= cp.refreshAt || !cp.head || !cp.hrp)
            RefreshPlayer(cp, p);
        if (!cp.head || !cp.hrp) continue;

        if (Instance::PartTransparency(cp.head) > 0.9f) continue;

        bool teammate = false;
        if (g_cfg.espTeamCheck || g_cfg.aimbotTeamCheck)
        {
            uintptr_t t = g_mem.ReadPtr(p.addr + OFF("Player", "Team"));
            teammate = (t != 0 && t == localTeam);
            if (teammate && !g_cfg.espShowTeammates && g_cfg.espTeamCheck) continue;
        }

        Vec3 hp = Instance::PartPosition(cp.head); hp.y += 0.4f;
        Vec3 fp = Instance::PartPosition(cp.hrp); fp.y -= 3.f;
        Vec3 root = Instance::PartPosition(cp.hrp);

        Vec2 hs, fs, rs;
        bool hok = sdk::WorldToScreen(hp, hs);
        bool fok = sdk::WorldToScreen(fp, fs);
        bool rok = sdk::WorldToScreen(root, rs);
        if (!hok && !fok) continue;

        float dist = sqrtf((localPos.x - root.x) * (localPos.x - root.x) +
                           (localPos.z - root.z) * (localPos.z - root.z));
        if (dist > 2000.f) continue; // esp distance cap; aimbot range handled separately

        PlayerBox pb{};
        pb.name = cp.name;
        pb.headAddr = cp.head;
        pb.headWorld = hp;
        {
            uintptr_t hprim = g_mem.ReadPtr(cp.head + OFF("BasePart", "Primitive"));
            pb.headVel = hprim ? g_mem.Read<Vec3>(hprim + OFF("Primitive", "AssemblyLinearVelocity")) : Vec3{};
        }
        pb.dist = dist;
        pb.teammate = teammate;
        pb.kit = cp.kit;
        pb.kitCol = cp.kitCol;
        if (cp.humanoid)
        {
            pb.health = g_mem.Read<float>(cp.humanoid + OFF("Humanoid", "Health"));
            pb.maxHealth = g_mem.Read<float>(cp.humanoid + OFF("Humanoid", "MaxHealth"));
        }

        if (hok && fok)
        {
            float h = fs.y - hs.y;
            float w = h * 0.55f;
            pb.x1 = hs.x - w / 2; pb.x2 = hs.x + w / 2;
            pb.y1 = hs.y - h * 0.08f; pb.y2 = fs.y;
            pb.headX = hs.x; pb.headY = hs.y;
        }
        else if (rok)
        {
            float s = 3000.f / (dist + 1.f);
            s = s < 10 ? 10 : (s > 80 ? 80 : s);
            pb.x1 = rs.x - s / 2; pb.x2 = rs.x + s / 2;
            pb.y1 = rs.y - s; pb.y2 = rs.y;
            pb.headX = rs.x; pb.headY = pb.y1;
        }
        g_players.push_back(pb);
    }

    // drop cache entries for players who left
    for (auto it = g_cache.begin(); it != g_cache.end();)
        it = alive.count(it->first) ? std::next(it) : g_cache.erase(it);
}

// local HRP cached the same way - LocalPlayerPos() re-enumerates
// character children every call which is the other big RPM cost
static Vec3 CachedLocalPos()
{
    static uintptr_t cachedChar = 0, cachedHrp = 0;
    static DWORD refreshAt = 0;
    DWORD now = GetTickCount();
    auto& w = sdk::Get();
    if (!w.localPlayer) return {};

    uintptr_t curChar = g_mem.ReadPtr(w.localPlayer + OFF("Player", "ModelInstance"));
    if (curChar != cachedChar || now >= refreshAt || !cachedHrp)
    {
        cachedChar = curChar; refreshAt = now + 500; cachedHrp = 0;
        if (curChar)
        {
            Instance ch(curChar);
            for (auto& c : ch.GetChildren())
            {
                std::string cn = c.GetName();
                if (cn == "HumanoidRootPart") { cachedHrp = c.addr; break; }
                if (cn == "Torso") cachedHrp = c.addr;
            }
        }
    }
    return cachedHrp ? Instance::PartPosition(cachedHrp) : Vec3{};
}

static void DrawEsp(ImDrawList* dl)
{
    ImU32 col = IM_COL32(255, 255, 255, 255);
    ImU32 colTeam = IM_COL32(80, 200, 120, 255);
    ImU32 colEnemy = IM_COL32(255, 80, 80, 255);

    for (auto& p : g_players)
    {
        ImU32 c = p.teammate ? colTeam : colEnemy;
        float cx = (p.x1 + p.x2) / 2.f;

        if (g_cfg.espBox)
            dl->AddRect({ p.x1, p.y1 }, { p.x2, p.y2 }, c, 0.f, 0, 1.5f);

        if (g_cfg.espName && !p.name.empty())
        {
            ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
            dl->AddText({ cx - ts.x / 2, p.y1 - ts.y - 2 }, c, p.name.c_str());
        }
        if (g_cfg.espKit && !p.kit.empty())
        {
            char kbuf[48]; snprintf(kbuf, sizeof(kbuf), "[%s]", p.kit.c_str());
            ImVec2 ts = ImGui::CalcTextSize(kbuf);
            float ky = p.y1 - ts.y - 2;
            if (g_cfg.espName && !p.name.empty()) ky -= ts.y + 1;
            dl->AddText({ cx - ts.x / 2, ky }, p.kitCol, kbuf);
        }
        if (g_cfg.espDistance)
        {
            char buf[32]; snprintf(buf, sizeof(buf), "%.0fm", p.dist);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddText({ cx - ts.x / 2, p.y2 + 2 }, IM_COL32(200, 200, 200, 255), buf);
        }
        if (g_cfg.espHealth && p.maxHealth > 0)
        {
            float frac = p.health / p.maxHealth;
            if (frac > 1.f) frac = 1.f;
            dl->AddRectFilled({ p.x1 - 5, p.y1 }, { p.x1 - 3, p.y2 }, IM_COL32(40, 40, 40, 255));
            dl->AddRectFilled({ p.x1 - 5, p.y2 - (p.y2 - p.y1) * frac }, { p.x1 - 3, p.y2 },
                IM_COL32((int)(255 * (1 - frac)), (int)(255 * frac), 0, 255));
        }
        if (g_cfg.espSnaplines)
            dl->AddLine({ g_localX, (float)g_height }, { cx, p.y2 }, c, 1.f);
    }
}

// ---------------- input helpers ----------------

static void mouse_move(int dx, int dy)
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx; in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

static void mouse_click()
{
    INPUT in[2]{};
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(in));
}

static bool key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// activation: 0 = hold key, 1 = press key to toggle, 2 = always on
static bool ActKey(int key, int mode, bool& toggled, bool& wasDown)
{
    bool down = key_down(key);
    if (mode == 1 && down && !wasDown) toggled = !toggled;
    wasDown = down;
    if (mode == 2) return true;
    return mode == 1 ? toggled : down;
}

static const char* const kActModes[] = { "Hold key", "Toggle key", "Always on" };

// true when Roblox (or our own overlay, so the menu stays usable) is foreground
static bool GameFocused()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (fg == g_overlay.hwnd) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid != 0 && pid == g_mem.pid;
}

static const char* VkName(int vk)
{
    switch (vk)
    {
    case VK_LSHIFT: return "LSHIFT"; case VK_RSHIFT: return "RSHIFT";
    case VK_LCONTROL: return "LCTRL"; case VK_RCONTROL: return "RCTRL";
    case VK_LMENU: return "LALT"; case VK_RMENU: return "RALT";
    case VK_INSERT: return "INSERT"; case VK_DELETE: return "DELETE";
    case VK_HOME: return "HOME"; case VK_END: return "END";
    case VK_PRIOR: return "PGUP"; case VK_NEXT: return "PGDN";
    case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
    case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
    case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
    case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
    case VK_SPACE: return "SPACE"; case VK_TAB: return "TAB";
    case VK_RETURN: return "ENTER"; case VK_BACK: return "BACKSPACE";
    case VK_ESCAPE: return "ESC"; case VK_CAPITAL: return "CAPS";
    }
    if (vk >= 'A' && vk <= 'Z') { static char b[2]; b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= '0' && vk <= '9') { static char b[2]; b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { static char b[8]; snprintf(b, 8, "NUM%d", vk - VK_NUMPAD0); return b; }
    if (vk >= VK_F13 && vk <= VK_F24) { static char b[8]; snprintf(b, 8, "F%d", vk - VK_F13 + 13); return b; }
    static char b[16]; snprintf(b, 16, "VK%02X", vk); return b;
}

// ---------------- aimbot / triggerbot / hitbox ----------------

static void RunAimbot()
{
    if (!g_cfg.aimbotEnabled || !g_mem.IsAttached()) return;
    static bool toggled = false, wasDown = false;
    if (!ActKey(g_cfg.aimbotKey, g_cfg.aimbotMode, toggled, wasDown)) return;
    if (g_cfg.aimbotKnockCheck && g_localKnocked) return;
    if (g_players.empty()) return;

    float cx = g_localX, cy = g_localY;
    const PlayerBox* best = nullptr;
    float bestD = g_cfg.aimbotFov;
    float tx = 0, ty = 0;

    for (auto& p : g_players)
    {
        if (p.teammate && g_cfg.aimbotTeamCheck) continue;
        if (g_cfg.aimbotHealthCheck && p.health <= 0.f) continue;
        if (g_cfg.aimbotRangeCheck && p.dist > g_cfg.aimbotRange) continue;

        // prediction: lead the target by velocity * predMs
        float px = p.headX, py = p.headY;
        if (g_cfg.aimbotPrediction)
        {
            Vec3 pred{ p.headWorld.x + p.headVel.x * (g_cfg.aimbotPredMs / 1000.f),
                       p.headWorld.y + p.headVel.y * (g_cfg.aimbotPredMs / 1000.f),
                       p.headWorld.z + p.headVel.z * (g_cfg.aimbotPredMs / 1000.f) };
            Vec2 s2;
            if (sdk::WorldToScreen(pred, s2) && s2.x != -1) { px = s2.x; py = s2.y; }
        }

        // shake: horizontal jitter on the target point
        if (g_cfg.aimbotShake)
            px += ((rand() % 200) - 100) / 100.f * g_cfg.aimbotShakePx;

        float dx = px - cx, dy = py - cy;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < bestD) { bestD = d; best = &p; tx = px; ty = py; }
    }
    if (!best) return;

    float smooth = (g_cfg.aimbotSmoothing && g_cfg.aimbotSmoothStyle == 1) ? g_cfg.aimbotSmooth : 1.f;
    float dx = (tx - cx) * smooth;
    float dy = (ty - cy) * smooth;
    mouse_move((int)dx, (int)dy);
}

static void RunTriggerbot()
{
    if (!g_cfg.triggerbotEnabled || !g_mem.IsAttached()) return;
    static bool toggled = false, wasDown = false;
    if (!ActKey(g_cfg.triggerKey, g_cfg.triggerMode, toggled, wasDown)) return;

    for (auto& p : g_players)
    {
        if (p.teammate) continue;
        float dx = p.headX - g_localX, dy = p.headY - g_localY;
        if (sqrtf(dx * dx + dy * dy) <= g_cfg.triggerFov)
        {
            mouse_click();
            Sleep(60);
            break;
        }
    }
}

static std::vector<std::pair<uintptr_t, Vec3>> g_hitboxOriginals;

static void RestoreHitboxes()
{
    for (auto& [addr, sz] : g_hitboxOriginals)
    {
        uintptr_t prim = g_mem.ReadPtr(addr + OFF("BasePart", "Primitive"));
        if (prim) g_mem.Write<Vec3>(prim + OFF("Primitive", "Size"), sz);
    }
    g_hitboxOriginals.clear();
}

static void RunHitbox()
{
    static bool wasHeld = false;
    auto& originals = g_hitboxOriginals;
    static DWORD lastRun = 0;

    static bool toggled = false, wasDown = false;
    bool held = g_cfg.hitboxEnabled && g_mem.IsAttached() && GameFocused() &&
        ActKey(g_cfg.hitboxKey, g_cfg.hitboxMode, toggled, wasDown);
    if (!held)
    {
        if (wasHeld) RestoreHitboxes();
        wasHeld = false;
        return;
    }
    wasHeld = true;

    // ~15hz is plenty; this does many RPM calls per player
    DWORD now = GetTickCount();
    if (now - lastRun < 66) return;
    lastRun = now;

    auto w = sdk::Get();
    if (!w.localPlayer) return;
    static const char* parts[] = { "Head", "HumanoidRootPart", "Torso", "UpperTorso", "LowerTorso" };

    for (auto& pl : sdk::GetPlayers())
    {
        if (pl.addr == w.localPlayer) continue;
        auto ch = pl.GetCharacter();
        if (!ch) continue;

        std::unordered_map<std::string, uintptr_t> partMap;
        for (auto& c : ch.GetChildren())
        {
            std::string cn = c.GetName();
            if (!cn.empty()) partMap[cn] = c.addr;
        }

        for (auto pn : parts)
        {
            auto it = partMap.find(pn);
            if (it == partMap.end()) continue;
            uintptr_t prim = g_mem.ReadPtr(it->second + OFF("BasePart", "Primitive"));
            if (!prim) continue;
            uintptr_t sizeAddr = prim + OFF("Primitive", "Size");
            Vec3 sz = g_mem.Read<Vec3>(sizeAddr);
            bool found = false;
            for (auto& o : originals) if (o.first == it->second) { found = true; break; }
            if (!found) originals.push_back({ it->second, sz });

            Vec3 ns{ sz.x * g_cfg.hitboxMult, sz.y * g_cfg.hitboxMult, sz.z * g_cfg.hitboxMult };
            if (g_cfg.hitboxMaxStuds > 0.f)
            {
                if (ns.x > g_cfg.hitboxMaxStuds) ns.x = g_cfg.hitboxMaxStuds;
                if (ns.y > g_cfg.hitboxMaxStuds) ns.y = g_cfg.hitboxMaxStuds;
                if (ns.z > g_cfg.hitboxMaxStuds) ns.z = g_cfg.hitboxMaxStuds;
            }
            static bool loggedWrite = false;
            if (!loggedWrite)
            {
                loggedWrite = true;
                Log("[nyx] hitbox write part=0x%llx prim=0x%llx size=(%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f) ok=%d",
                    (unsigned long long)it->second, (unsigned long long)prim,
                    sz.x, sz.y, sz.z, ns.x, ns.y, ns.z,
                    (int)g_mem.Write<Vec3>(sizeAddr, ns));
            }
            else g_mem.Write<Vec3>(sizeAddr, ns);
        }
    }
}

// ---------------- key rebind ----------------

static int* g_rebindTarget = nullptr;

static void RebindButton(const char* label, int* target)
{
    char buf[64];
    if (g_rebindTarget == target)
        snprintf(buf, sizeof(buf), "%s: press a key...", label);
    else
        snprintf(buf, sizeof(buf), "%s: %s", label, VkName(*target));

    if (ImGui::Button(buf, { 180, 0 }))
        g_rebindTarget = (g_rebindTarget == target) ? nullptr : target;
}

// returns true the first frame a key is pressed while rebinding
static bool PollRebind()
{
    if (!g_rebindTarget) return false;
    for (int vk = 1; vk < 255; vk++)
    {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON) continue;
        if (key_down(vk))
        {
            if (vk == VK_ESCAPE) { g_rebindTarget = nullptr; return true; }
            *g_rebindTarget = vk;
            g_rebindTarget = nullptr;
            config::Save();
            return true;
        }
    }
    return false;
}

// ---------------- menu ----------------

static void ApplyTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;
    ImVec4 acc(g_cfg.accent[0], g_cfg.accent[1], g_cfg.accent[2], 1.f);

    s.WindowRounding = 6.f; s.FrameRounding = 4.f;
    s.WindowPadding = { 10, 10 }; s.FramePadding = { 8, 4 };
    s.ItemSpacing = { 8, 6 }; s.ScrollbarSize = 12.f;

    switch (g_cfg.uiTheme)
    {
    case 1: // incognito: flat black, white text, accent highlights
        c[ImGuiCol_WindowBg] = { 0.03f, 0.03f, 0.03f, 0.97f };
        c[ImGuiCol_ChildBg] = { 0.05f, 0.05f, 0.05f, 1.f };
        c[ImGuiCol_Border] = { 0.15f, 0.15f, 0.15f, 1.f };
        c[ImGuiCol_FrameBg] = { 0.09f, 0.09f, 0.09f, 1.f };
        c[ImGuiCol_FrameBgHovered] = { 0.13f, 0.13f, 0.13f, 1.f };
        c[ImGuiCol_FrameBgActive] = { 0.16f, 0.16f, 0.16f, 1.f };
        c[ImGuiCol_Button] = { 0.10f, 0.10f, 0.10f, 1.f };
        c[ImGuiCol_ButtonHovered] = acc;
        c[ImGuiCol_ButtonActive] = { acc.x * 0.75f, acc.y * 0.75f, acc.z * 0.75f, 1.f };
        c[ImGuiCol_CheckMark] = acc;
        c[ImGuiCol_SliderGrab] = acc;
        c[ImGuiCol_SliderGrabActive] = { acc.x * 0.8f, acc.y * 0.8f, acc.z * 0.8f, 1.f };
        c[ImGuiCol_Tab] = { 0.05f, 0.05f, 0.05f, 1.f };
        c[ImGuiCol_TabHovered] = { 0.14f, 0.14f, 0.14f, 1.f };
        c[ImGuiCol_TabSelected] = { 0.09f, 0.09f, 0.09f, 1.f };
        c[ImGuiCol_Header] = acc;
        c[ImGuiCol_HeaderHovered] = { acc.x * 0.85f, acc.y * 0.85f, acc.z * 0.85f, 1.f };
        c[ImGuiCol_ScrollbarGrab] = { 0.2f, 0.2f, 0.2f, 1.f };
        break;

    case 2: // neon: dark purple, glowing accent
        c[ImGuiCol_WindowBg] = { 0.07f, 0.04f, 0.12f, 0.97f };
        c[ImGuiCol_ChildBg] = { 0.09f, 0.05f, 0.16f, 1.f };
        c[ImGuiCol_Border] = { acc.x * 0.5f, acc.y * 0.5f, acc.z * 0.5f, 1.f };
        c[ImGuiCol_FrameBg] = { 0.12f, 0.07f, 0.22f, 1.f };
        c[ImGuiCol_FrameBgHovered] = { acc.x * 0.4f, acc.y * 0.4f, acc.z * 0.4f, 1.f };
        c[ImGuiCol_FrameBgActive] = { acc.x * 0.55f, acc.y * 0.55f, acc.z * 0.55f, 1.f };
        c[ImGuiCol_Button] = { 0.14f, 0.08f, 0.25f, 1.f };
        c[ImGuiCol_ButtonHovered] = acc;
        c[ImGuiCol_ButtonActive] = { acc.x * 0.8f, acc.y * 0.8f, acc.z * 0.8f, 1.f };
        c[ImGuiCol_CheckMark] = acc;
        c[ImGuiCol_SliderGrab] = acc;
        c[ImGuiCol_SliderGrabActive] = { acc.x, acc.y, acc.z, 1.f };
        c[ImGuiCol_Tab] = { 0.10f, 0.06f, 0.18f, 1.f };
        c[ImGuiCol_TabHovered] = acc;
        c[ImGuiCol_TabSelected] = { acc.x * 0.35f, acc.y * 0.35f, acc.z * 0.35f, 1.f };
        c[ImGuiCol_Header] = acc;
        c[ImGuiCol_HeaderHovered] = { acc.x * 0.85f, acc.y * 0.85f, acc.z * 0.85f, 1.f };
        c[ImGuiCol_Text] = { 0.92f, 0.90f, 1.0f, 1.f };
        c[ImGuiCol_ScrollbarGrab] = { acc.x * 0.4f, acc.y * 0.4f, acc.z * 0.4f, 1.f };
        break;

    case 3: // light: clean bright theme
        c[ImGuiCol_WindowBg] = { 0.94f, 0.94f, 0.96f, 0.97f };
        c[ImGuiCol_ChildBg] = { 0.97f, 0.97f, 0.98f, 1.f };
        c[ImGuiCol_Border] = { 0.75f, 0.75f, 0.78f, 1.f };
        c[ImGuiCol_FrameBg] = { 1.f, 1.f, 1.f, 1.f };
        c[ImGuiCol_FrameBgHovered] = { 0.90f, 0.93f, 0.97f, 1.f };
        c[ImGuiCol_FrameBgActive] = { 0.85f, 0.90f, 0.96f, 1.f };
        c[ImGuiCol_Button] = { 0.88f, 0.89f, 0.91f, 1.f };
        c[ImGuiCol_ButtonHovered] = acc;
        c[ImGuiCol_ButtonActive] = { acc.x * 0.8f, acc.y * 0.8f, acc.z * 0.8f, 1.f };
        c[ImGuiCol_CheckMark] = acc;
        c[ImGuiCol_SliderGrab] = acc;
        c[ImGuiCol_SliderGrabActive] = { acc.x * 0.8f, acc.y * 0.8f, acc.z * 0.8f, 1.f };
        c[ImGuiCol_Tab] = { 0.87f, 0.88f, 0.90f, 1.f };
        c[ImGuiCol_TabHovered] = acc;
        c[ImGuiCol_TabSelected] = { 0.80f, 0.86f, 0.94f, 1.f };
        c[ImGuiCol_Header] = acc;
        c[ImGuiCol_HeaderHovered] = { acc.x * 0.85f, acc.y * 0.85f, acc.z * 0.85f, 1.f };
        c[ImGuiCol_Text] = { 0.10f, 0.10f, 0.12f, 1.f };
        c[ImGuiCol_TextDisabled] = { 0.45f, 0.45f, 0.48f, 1.f };
        c[ImGuiCol_ScrollbarGrab] = { 0.70f, 0.72f, 0.75f, 1.f };
        break;

    case 4: // vape v4: charcoal panels, thin borders, crisp white text
        c[ImGuiCol_WindowBg] = { 0.07f, 0.07f, 0.08f, 0.98f };
        c[ImGuiCol_ChildBg] = { 0.09f, 0.09f, 0.10f, 1.f };
        c[ImGuiCol_Border] = { 0.18f, 0.18f, 0.20f, 1.f };
        c[ImGuiCol_FrameBg] = { 0.11f, 0.11f, 0.13f, 1.f };
        c[ImGuiCol_FrameBgHovered] = { 0.15f, 0.15f, 0.18f, 1.f };
        c[ImGuiCol_FrameBgActive] = { 0.18f, 0.18f, 0.22f, 1.f };
        c[ImGuiCol_Button] = { 0.12f, 0.12f, 0.14f, 1.f };
        c[ImGuiCol_ButtonHovered] = { acc.x * 0.5f + 0.1f, acc.y * 0.5f + 0.1f, acc.z * 0.5f + 0.1f, 1.f };
        c[ImGuiCol_ButtonActive] = acc;
        c[ImGuiCol_CheckMark] = acc;
        c[ImGuiCol_SliderGrab] = acc;
        c[ImGuiCol_SliderGrabActive] = { acc.x, acc.y, acc.z, 1.f };
        c[ImGuiCol_Tab] = { 0.09f, 0.09f, 0.10f, 1.f };
        c[ImGuiCol_TabHovered] = { 0.16f, 0.16f, 0.19f, 1.f };
        c[ImGuiCol_TabSelected] = { 0.13f, 0.13f, 0.15f, 1.f };
        c[ImGuiCol_Header] = { acc.x * 0.4f, acc.y * 0.4f, acc.z * 0.4f, 1.f };
        c[ImGuiCol_HeaderHovered] = acc;
        c[ImGuiCol_Text] = { 0.92f, 0.92f, 0.94f, 1.f };
        c[ImGuiCol_TextDisabled] = { 0.45f, 0.45f, 0.48f, 1.f };
        c[ImGuiCol_ScrollbarGrab] = { 0.20f, 0.20f, 0.23f, 1.f };
        s.WindowBorderSize = 1.f;
        s.FrameBorderSize = 0.f;
        break;

    default: // 0 midnight: dark blue-grey, cyan accent
        c[ImGuiCol_WindowBg] = { 0.06f, 0.07f, 0.10f, 0.96f };
        c[ImGuiCol_ChildBg] = { 0.08f, 0.09f, 0.13f, 1.f };
        c[ImGuiCol_Border] = { 0.16f, 0.20f, 0.28f, 1.f };
        c[ImGuiCol_FrameBg] = { 0.10f, 0.12f, 0.17f, 1.f };
        c[ImGuiCol_FrameBgHovered] = { 0.14f, 0.18f, 0.25f, 1.f };
        c[ImGuiCol_FrameBgActive] = { 0.18f, 0.23f, 0.32f, 1.f };
        c[ImGuiCol_Button] = { 0.12f, 0.15f, 0.21f, 1.f };
        c[ImGuiCol_ButtonHovered] = acc;
        c[ImGuiCol_ButtonActive] = { acc.x * 0.8f, acc.y * 0.8f, acc.z * 0.8f, 1.f };
        c[ImGuiCol_CheckMark] = acc;
        c[ImGuiCol_SliderGrab] = acc;
        c[ImGuiCol_SliderGrabActive] = { acc.x, acc.y, acc.z, 1.f };
        c[ImGuiCol_Tab] = { 0.08f, 0.10f, 0.14f, 1.f };
        c[ImGuiCol_TabHovered] = acc;
        c[ImGuiCol_TabSelected] = { acc.x * 0.35f, acc.y * 0.35f, acc.z * 0.35f, 1.f };
        c[ImGuiCol_Header] = acc;
        c[ImGuiCol_HeaderHovered] = { acc.x * 0.85f, acc.y * 0.85f, acc.z * 0.85f, 1.f };
        c[ImGuiCol_ScrollbarGrab] = { 0.20f, 0.25f, 0.35f, 1.f };
        break;
    }
}

static void MenuAim()
{
    ImGui::Checkbox("Enabled", &g_cfg.aimbotEnabled);
    ImGui::SliderFloat("ANGLE", &g_cfg.aimbotFov, 10.f, 500.f, "%.0f px");
    float sm = g_cfg.aimbotSmooth * 10.f;
    if (ImGui::SliderFloat("Smoothness", &sm, 1.f, 10.f, "%.0f"))
        g_cfg.aimbotSmooth = sm / 10.f;
    const char* styles[] = { "None", "Linear" };
    ImGui::Combo("Smoothing style", &g_cfg.aimbotSmoothStyle, styles, 2);
    ImGui::Checkbox("Enable smoothing", &g_cfg.aimbotSmoothing);
    ImGui::Checkbox("Enable shake", &g_cfg.aimbotShake);
    if (g_cfg.aimbotShake)
        ImGui::SliderFloat("Shake px", &g_cfg.aimbotShakePx, 1.f, 20.f, "%.0f");
    ImGui::Checkbox("Enable prediction", &g_cfg.aimbotPrediction);
    if (g_cfg.aimbotPrediction)
        ImGui::SliderFloat("Prediction (ms)", &g_cfg.aimbotPredMs, 0.f, 500.f, "%.0f");
    ImGui::SliderFloat("Range (studs)", &g_cfg.aimbotRange, 5.f, 30.f, "%.0f");
    ImGui::Combo("Activation", &g_cfg.aimbotMode, kActModes, 3);
    ImGui::TextDisabled("%s: %s", kActModes[g_cfg.aimbotMode], VkName(g_cfg.aimbotKey));
    ImGui::Separator();
    ImGui::Text("Checks");
    ImGui::Checkbox("Team check", &g_cfg.aimbotTeamCheck);
    ImGui::Checkbox("Knock check", &g_cfg.aimbotKnockCheck);
    ImGui::Checkbox("Health check", &g_cfg.aimbotHealthCheck);
    ImGui::Checkbox("Range check", &g_cfg.aimbotRangeCheck);
}

static void MenuTrigger()
{
    ImGui::Checkbox("Enabled", &g_cfg.triggerbotEnabled);
    ImGui::SliderFloat("FOV", &g_cfg.triggerFov, 5.f, 150.f, "%.0f px");
    ImGui::Combo("Activation", &g_cfg.triggerMode, kActModes, 3);
    ImGui::TextDisabled("%s: %s", kActModes[g_cfg.triggerMode], VkName(g_cfg.triggerKey));
}

static void MenuHitbox()
{
    ImGui::Checkbox("Enabled", &g_cfg.hitboxEnabled);
    ImGui::SliderFloat("Multiplier", &g_cfg.hitboxMult, 1.f, 6.f, "%.1fx");
    ImGui::SliderFloat("Max studs (0=off)", &g_cfg.hitboxMaxStuds, 0.f, 30.f, "%.1f");
    ImGui::Combo("Activation", &g_cfg.hitboxMode, kActModes, 3);
    ImGui::TextDisabled("%s: %s", kActModes[g_cfg.hitboxMode], VkName(g_cfg.hitboxKey));
}

static void MenuVisuals()
{
    ImGui::Checkbox("Box ESP", &g_cfg.espBox);
    ImGui::Checkbox("Name ESP", &g_cfg.espName);
    ImGui::Checkbox("Distance ESP", &g_cfg.espDistance);
    ImGui::Checkbox("Health bar", &g_cfg.espHealth);
    ImGui::Checkbox("Snaplines", &g_cfg.espSnaplines);
    ImGui::Checkbox("Team check", &g_cfg.espTeamCheck);
    ImGui::Checkbox("Show teammates", &g_cfg.espShowTeammates);
    ImGui::Checkbox("Kit ESP", &g_cfg.espKit);
}

static void MenuMisc()
{
    ImGui::Checkbox("Streamproof", &g_cfg.streamproof);
    if (g_cfg.streamproof) g_overlay.ApplyStreamproof();
    ImGui::Separator();
    ImGui::Text("Interface");
    const char* themes[] = { "Midnight", "Incognito", "Neon", "Light", "Vape" };
    if (ImGui::Combo("Theme", &g_cfg.uiTheme, themes, 5)) config::Save();
    const char* layouts[] = { "Tabs", "Sidebar" };
    if (ImGui::Combo("Layout", &g_cfg.uiLayout, layouts, 2)) config::Save();
    ImGui::ColorEdit3("Accent", g_cfg.accent, ImGuiColorEditFlags_NoInputs);
    ImGui::Text("Keybinds");
    RebindButton("Menu", &g_cfg.menuKey);
    RebindButton("Aimbot", &g_cfg.aimbotKey);
    RebindButton("Triggerbot", &g_cfg.triggerKey);
    RebindButton("Hitbox", &g_cfg.hitboxKey);
    ImGui::TextDisabled("INSERT still toggles this menu");
    ImGui::Text("Roblox PID: %lu", g_mem.pid);
    ImGui::Text("Base: 0x%llx", (unsigned long long)g_mem.base);
    ImGui::Text("Offsets: %s", offsets::Ready() ? offsets::ClientVersion().c_str() : "fetching...");
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.f));
    if (ImGui::Button("DESTROY / UNLOAD", { -1, 30 }))
        g_running = false;
    ImGui::PopStyleColor(2);
}

static void DrawMenu()
{
    // live theme switching
    static int lastTheme = -1;
    static float lastAcc[3] = { -1, -1, -1 };
    if (lastTheme != g_cfg.uiTheme ||
        lastAcc[0] != g_cfg.accent[0] || lastAcc[1] != g_cfg.accent[1] || lastAcc[2] != g_cfg.accent[2])
    {
        lastTheme = g_cfg.uiTheme;
        lastAcc[0] = g_cfg.accent[0]; lastAcc[1] = g_cfg.accent[1]; lastAcc[2] = g_cfg.accent[2];
        ApplyTheme();
        config::Save();
    }

    ImGui::SetNextWindowSize({ 560, 480 }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("NYX", nullptr, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }

    if (g_cfg.uiLayout == 1)
    {
        // ---- sidebar layout ----
        static int section = 0;
        ImGui::BeginChild("##sidebar", { 110, 0 }, true);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        ImGui::SetWindowFontScale(1.3f);
        ImGui::Text("NYX");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("v%s", NYX_VERSION);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        const char* names[] = { "AIM ASSIST", "TRIGGERBOT", "HITBOX", "VISUALS", "MISC" };
        for (int i = 0; i < 5; i++)
        {
            if (section == i)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
            if (ImGui::Button(names[i], { -1, 34 })) section = i;
            if (section == i) ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##content", { 0, 0 }, true);
        switch (section)
        {
        case 0: MenuAim(); break;
        case 1: MenuTrigger(); break;
        case 2: MenuHitbox(); break;
        case 3: MenuVisuals(); break;
        case 4: MenuMisc(); break;
        }
        ImGui::EndChild();
    }
    else
    {
        // ---- classic tab layout ----
        if (ImGui::BeginTabBar("tabs"))
        {
            if (ImGui::BeginTabItem("Aim Assist")) { MenuAim(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Triggerbot")) { MenuTrigger(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Hitbox")) { MenuHitbox(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Visuals")) { MenuVisuals(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Misc")) { MenuMisc(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ---------------- wndproc / dx11 ----------------

static LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return true;
    switch (msg)
    {
    case WM_DESTROY: g_running = false; PostQuitMessage(0); return 0;
    case WM_SIZE:
        if (g_overlay.device && w != SIZE_MINIMIZED)
        {
            if (g_overlay.rtv) { g_overlay.rtv->Release(); g_overlay.rtv = nullptr; }
            g_overlay.swapChain->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* back = nullptr;
            g_overlay.swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
            if (back) { g_overlay.device->CreateRenderTargetView(back, nullptr, &g_overlay.rtv); back->Release(); }
        }
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

bool Overlay::Create(HINSTANCE inst)
{
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_width = sw; g_height = sh;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"NYXOverlay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"NYX", WS_POPUP,
        0, 0, sw, sh, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return false;

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);
    ShowWindow(hwnd, SW_SHOW);
    ApplyStreamproof();

    // dx11
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = sw; sd.BufferDesc.Height = sh;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &swapChain, &device, &fl, &ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* back = nullptr;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    device->CreateRenderTargetView(back, nullptr, &rtv);
    back->Release();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ApplyTheme();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, ctx);
    return true;
}

void Overlay::ApplyStreamproof()
{
    if (!hwnd) return;
    BOOL ok;
    if (g_cfg.streamproof)
        ok = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    else
        ok = SetWindowDisplayAffinity(hwnd, WDA_NONE);
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        Log("[nyx] streamproof=%d affinity_ok=%d err=%lu", (int)g_cfg.streamproof, (int)ok, GetLastError());
    }
}

void Overlay::SetClickThrough(bool clickThrough)
{
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (clickThrough) ex |= WS_EX_TRANSPARENT;
    else ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongW(hwnd, GWL_EXSTYLE, ex);
}

void Overlay::Run()
{
    MSG msg;
    while (g_running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) { g_running = false; break; }
        }
        if (!g_running) break;

        if (key_down(VK_END)) { g_running = false; break; }

        // re-assert streamproof periodically - some capture/driver
        // combos reset the affinity and it never retries otherwise
        static DWORD lastAffinity = 0;
        if (g_cfg.streamproof && GetTickCount() - lastAffinity > 2000)
        {
            lastAffinity = GetTickCount();
            SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
        }

        bool focused = GameFocused();

        // key rebind capture (menu must be open to click the button anyway)
        PollRebind();

        // menu toggle — only while Roblox (or this overlay) is focused
        static bool prevMenuKey = false, prevIns = false;
        bool menuKey = focused && key_down(g_cfg.menuKey);
        bool insKey = focused && key_down(VK_INSERT);
        if ((menuKey && !prevMenuKey) || (insKey && !prevIns))
        {
            g_cfg.menuVisible = !g_cfg.menuVisible;
            SetClickThrough(!g_cfg.menuVisible);
        }
        prevMenuKey = menuKey; prevIns = insKey;

        // auto-attach
        static DWORD lastTry = 0;
        if (!g_mem.IsAttached() && GetTickCount() - lastTry > 1000)
        {
            lastTry = GetTickCount();
            if (!offsets::Ready())
            {
                bool ok = offsets::Fetch();
                Log("[nyx] offsets fetch: %s (%zu classes)", ok ? "ok" : "FAILED", 0);
            }
            if (g_mem.Attach(L"RobloxPlayerBeta.exe") || g_mem.Attach(L"RobloxPlayer.exe"))
            {
                Log("[nyx] attached pid=%lu base=0x%llx", g_mem.pid, (unsigned long long)g_mem.base);
                bool r = sdk::Resolve();
                auto& w = sdk::Get();
                Log("[nyx] resolve=%d ve=0x%llx dm=0x%llx ws=0x%llx pl=0x%llx lp=0x%llx",
                    (int)r, (unsigned long long)w.visualEngine, (unsigned long long)w.dataModel,
                    (unsigned long long)w.workspace, (unsigned long long)w.players, (unsigned long long)w.localPlayer);
            }
        }
        else if (g_mem.IsAttached())
        {
            static DWORD lastResolve = 0;
            if (GetTickCount() - lastResolve > 1500) { lastResolve = GetTickCount(); sdk::Resolve(); }
            static DWORD lastCollect = 0;
            // only burn RPM calls while Roblox is actually focused
            if (focused && GetTickCount() - lastCollect > 16) { lastCollect = GetTickCount(); CollectPlayers(); }

            static DWORD lastStat = 0;
            static bool loggedNames = false;
            if (!loggedNames && sdk::Get().localPlayer)
            {
                loggedNames = true;
                Instance lp(sdk::Get().localPlayer);
                Log("[nyx] localplayer name='%s' class='%s'", lp.GetName().c_str(), lp.GetClassName().c_str());
                auto ch = lp.GetCharacter();
                if (ch)
                {
                    Log("[nyx] character class='%s' children=%zu", ch.GetClassName().c_str(), ch.GetChildren().size());
                    for (auto& c : ch.GetChildren())
                        Log("[nyx]   child '%s' (%s)", c.GetName().c_str(), c.GetClassName().c_str());
                }
                else Log("[nyx] no character yet");
            }
            if (GetTickCount() - lastStat > 5000)
            {
                lastStat = GetTickCount();
                Log("[nyx] players=%zu local=0x%llx esp=%zu focused=%d",
                    sdk::GetPlayers().size(), (unsigned long long)sdk::Get().localPlayer,
                    g_players.size(), (int)focused);
            }
        }
        else if (!g_mem.IsAttached())
        {
            g_players.clear();
        }

        if (focused)
        {
            RunAimbot();
            RunTriggerbot();
            RunHitbox();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        if (g_mem.IsAttached() && focused) DrawEsp(bg);
        if (g_cfg.menuVisible) DrawMenu();

        ImGui::Render();
        const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(0, 0);

        // cap overlay at ~120fps so the render thread doesn't pin a core
        {
            static auto lastFrame = std::chrono::steady_clock::now();
            auto nowT = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(nowT - lastFrame).count();
            if (elapsed < 8) Sleep((DWORD)(8 - elapsed));
            lastFrame = std::chrono::steady_clock::now();
        }

        // idle throttle: nothing to draw and menu closed -> don't burn a core
        if (!focused && !g_cfg.menuVisible && !g_mem.IsAttached()) Sleep(8);
    }

    RestoreHitboxes();
    g_mem.Detach();
}

void Overlay::Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (rtv) rtv->Release();
    if (swapChain) swapChain->Release();
    if (ctx) ctx->Release();
    if (device) device->Release();
    if (hwnd) DestroyWindow(hwnd);
}
