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

// called from sdk.cpp attribute diagnostics
void NyxAttrDebug(const char* fmt, ...)
{
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    std::ofstream f("nyx_debug.log", std::ios::app);
    va_list ap; va_start(ap, fmt);
    char buf[512]; vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    f << "[attr] " << buf << "\n";
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
    Vec3 headWorld, headVel, rootWorld;
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
static bool g_aimbotHadTarget = false;
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
    // newer bedwars kits
    { "barbarian", "BARBARIAN", IM_COL32(180, 90, 50, 255) },
    { "eldertree", "ELDERTREE", IM_COL32(90, 160, 70, 255) },
    { "gingerbread", "GINGERBREAD", IM_COL32(200, 120, 60, 255) },
    { "yuzi", "YUZI", IM_COL32(240, 180, 60, 255) },
    { "vulcan", "VULCAN", IM_COL32(220, 80, 40, 255) },
    { "kitslinger", "KITSLINGER", IM_COL32(200, 60, 140, 255) },
    { "shadow", "SHADOW", IM_COL32(70, 70, 90, 255) },
    { "phantom", "PHANTOM", IM_COL32(160, 200, 230, 255) },
    { "reaper", "REAPER", IM_COL32(60, 50, 70, 255) },
    { "beast", "BEAST", IM_COL32(140, 110, 70, 255) },
    { "skeleton", "SKELETON", IM_COL32(220, 220, 210, 255) },
    { "zombie", "ZOMBIE", IM_COL32(110, 160, 60, 255) },
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
    uintptr_t torso = 0, upperTorso = 0, lowerTorso = 0;
    uintptr_t team = 0;
    std::string name;
    std::string kit;
    ImU32 kitCol = 0;
    DWORD refreshAt = 0;
    // attribute scans are expensive (dozens of RPM reads per player) -
    // throttle to ~4s, rescan immediately on respawn (character change)
    uintptr_t attrChar = 0;
    DWORD attrScanAt = 0;
};
static std::unordered_map<uintptr_t, CachedPlayer> g_cache;

static void RefreshPlayer(CachedPlayer& cp, const Instance& p)
{
    cp.player = p.addr;
    cp.character = p.GetCharacter().addr;
    cp.head = cp.hrp = cp.humanoid = 0;
    cp.torso = cp.upperTorso = cp.lowerTorso = 0;
    cp.team = g_mem.ReadPtr(p.addr + OFF("Player", "Team"));
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
        else if (cn == "Torso") { cp.hrp = cp.hrp ? cp.hrp : c.addr; cp.torso = c.addr; }
        else if (cn == "UpperTorso") cp.upperTorso = c.addr;
        else if (cn == "LowerTorso") cp.lowerTorso = c.addr;
        if (cp.humanoid == 0 && c.GetClassName() == "Humanoid") cp.humanoid = c.addr;
    }
    // display names sometimes carry the kit ("xX_Kitslayer_Xx" etc.)
    if (cp.kit.empty())
    {
        std::string dn = g_mem.ReadRobloxString(p.addr + OFF("Player", "DisplayName"));
        tryKit(dn);
    }
    if (cp.kit.empty() && cp.humanoid)
    {
        std::string hd = g_mem.ReadRobloxString(cp.humanoid + OFF("Humanoid", "DisplayName"));
        tryKit(hd);
    }

    // replicated attributes - bedwars stores kit in PlayingAsKits etc.
    // (readable externally since attributes replicate to the client).
    // throttled: only rescan every ~4s, or right after a respawn
    {
        DWORD now = GetTickCount();
        bool charChanged = (cp.attrChar != cp.character);
        if ((cp.kit.empty() && now >= cp.attrScanAt) || charChanged)
        {
            cp.attrChar = cp.character;
            cp.attrScanAt = now + 4000;
            static std::unordered_set<uintptr_t> attrLogged, attrDbgDone;
            bool logThis = !attrLogged.count(p.addr);
            bool dbg = logThis && !attrDbgDone.count(p.addr);
            if (dbg) attrDbgDone.insert(p.addr);
            std::string attrDump;
            auto scanAttrs = [&](const Instance& inst, const char* tag) {
                for (auto& [k, v] : inst.GetAttributes(dbg))
                {
                    std::string lk = ToLower(k);
                    if (lk.find("kit") != std::string::npos || lk.find("class") != std::string::npos)
                        tryKit(v);
                    if (logThis && attrDump.size() < 400)
                    {
                        attrDump += tag; attrDump += ":"; attrDump += k;
                        attrDump += "="; attrDump += v; attrDump += " ";
                    }
                }
            };
            scanAttrs(p, "p");
            if (cp.kit.empty() && ch.valid()) scanAttrs(ch, "c");
            if (logThis && !attrDump.empty())
            {
                attrLogged.insert(p.addr);
                Log("attrs p='%s' %s", cp.name.c_str(), attrDump.c_str());
            }
        }
    }
    cp.name = p.GetName();
}

static Vec3 CachedLocalPos();

// ---------------- npc/dummy scan (bedwars bots etc.) ----------------
// dummies are Models in Workspace, not Player instances - scan for
// models containing Head + HumanoidRootPart + Humanoid, ~every 3s

struct NpcCache
{
    uintptr_t model = 0;
    std::string name;
    uintptr_t head = 0, hrp = 0, torso = 0, upper = 0, lower = 0, humanoid = 0;
};
static std::vector<NpcCache> g_npcs;
static DWORD g_npcScanAt = 0;

static void ScanNpcModel(uintptr_t maddr, const std::string& mname,
                         const std::unordered_set<uintptr_t>& playerChars)
{
    if (playerChars.count(maddr)) return; // skip real player characters
    Instance m(maddr);
    NpcCache n; n.model = maddr; n.name = mname;
    for (auto& c : m.GetChildren())
    {
        std::string cn = c.GetName();
        if (cn == "Head") n.head = c.addr;
        else if (cn == "HumanoidRootPart") n.hrp = c.addr;
        else if (cn == "Torso") { n.torso = c.addr; if (!n.hrp) n.hrp = c.addr; }
        else if (cn == "UpperTorso") n.upper = c.addr;
        else if (cn == "LowerTorso") n.lower = c.addr;
        if (!n.humanoid && c.GetClassName() == "Humanoid") n.humanoid = c.addr;
    }
    if (n.head && n.hrp && n.humanoid) g_npcs.push_back(n);
}

static void ScanNpcs()
{
    g_npcs.clear();
    auto& w = sdk::Get();
    if (!w.workspace) return;

    std::unordered_set<uintptr_t> playerChars;
    for (auto& [a, cp] : g_cache) if (cp.character) playerChars.insert(cp.character);
    // MUST exclude the local player's own character - it's a workspace
    // model too, and without this hitbox/ESP target yourself (the
    // "hitboxes ruin my camera/character" bug)
    if (w.localPlayer)
    {
        uintptr_t lc = g_mem.ReadPtr(w.localPlayer + OFF("Player", "ModelInstance"));
        if (lc) playerChars.insert(lc);
    }

    Instance ws(w.workspace);
    for (auto& c : ws.GetChildren())
    {
        std::string cls = c.GetClassName();
        if (cls == "Model")
        {
            ScanNpcModel(c.addr, c.GetName(), playerChars);
        }
        else if (cls == "Folder")
        {
            // dummies are often grouped in folders - one level deeper
            for (auto& g : c.GetChildren())
                if (g.GetClassName() == "Model")
                    ScanNpcModel(g.addr, g.GetName(), playerChars);
        }
    }
}

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
        pb.rootWorld = root;
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

    // npc/dummy pass (bedwars bots etc.) - rescan models ~every 3s
    if (now >= g_npcScanAt) { g_npcScanAt = now + 3000; ScanNpcs(); }
    for (auto& n : g_npcs)
    {
        if (Instance::PartTransparency(n.head) > 0.9f) continue;

        Vec3 hp = Instance::PartPosition(n.head); hp.y += 0.4f;
        Vec3 fp = Instance::PartPosition(n.hrp); fp.y -= 3.f;
        Vec3 root = Instance::PartPosition(n.hrp);

        Vec2 hs, fs, rs;
        bool hok = sdk::WorldToScreen(hp, hs);
        bool fok = sdk::WorldToScreen(fp, fs);
        bool rok = sdk::WorldToScreen(root, rs);
        if (!hok && !fok) continue;

        float dist = sqrtf((localPos.x - root.x) * (localPos.x - root.x) +
                           (localPos.z - root.z) * (localPos.z - root.z));
        if (dist > 2000.f) continue;

        PlayerBox pb{};
        pb.name = n.name;
        pb.headAddr = n.head;
        pb.headWorld = hp;
        pb.rootWorld = root;
        {
            uintptr_t hprim = g_mem.ReadPtr(n.head + OFF("BasePart", "Primitive"));
            pb.headVel = hprim ? g_mem.Read<Vec3>(hprim + OFF("Primitive", "AssemblyLinearVelocity")) : Vec3{};
        }
        pb.dist = dist;
        pb.teammate = false;
        if (n.humanoid)
        {
            pb.health = g_mem.Read<float>(n.humanoid + OFF("Humanoid", "Health"));
            pb.maxHealth = g_mem.Read<float>(n.humanoid + OFF("Humanoid", "MaxHealth"));
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
static uintptr_t g_localHrpAddr = 0;

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
    g_localHrpAddr = cachedHrp;
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

// ---------------- bedwars gate ----------------
// place ids: 6872265039 lobby, 6872274481 game
static int g_isBedWars = -1; // -1 unknown
static DWORD g_placeCheckAt = 0;
static uint64_t g_lastPlaceId = 0;

static bool IsBedWarsId(uint64_t pid)
{
    // bedwars lobby + game + a few related experiences
    switch (pid)
    {
    case 6872265039ULL: // bedwars lobby
    case 6872274481ULL: // bedwars game
    case 6872282384ULL:
    case 6872283856ULL:
    case 11627040162ULL: // bedwars (newer place id)
        return true;
    }
    return false;
}

static bool IsBedWars()
{
    if (!g_cfg.bedwarsOnly) return true;
    DWORD now = GetTickCount();
    if (now >= g_placeCheckAt)
    {
        g_placeCheckAt = now + 3000;
        auto& w = sdk::Get();
        if (w.dataModel)
        {
            uint64_t pid = g_mem.Read<uint64_t>(w.dataModel + OFF("DataModel", "PlaceId"));
            g_lastPlaceId = pid;
            // pid==0 means the offset/read failed - stay "unknown" and
            // DON'T gate, otherwise a broken offset kills all combat
            // features silently (this is what made everything feel dead)
            g_isBedWars = (pid == 0) ? -1 : (IsBedWarsId(pid) ? 1 : 0);
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                Log("[nyx] v%s placeId=%llu bedwars=%d", NYX_VERSION,
                    (unsigned long long)pid, g_isBedWars);
            }
            static DWORD lastBlockLog = 0;
            if (g_isBedWars == 0 && now - lastBlockLog > 10000)
            {
                lastBlockLog = now;
                Log("[nyx] combat features blocked: placeId=%llu is not bedwars", (unsigned long long)pid);
            }
        }
    }
    return g_isBedWars != 0; // unknown (-1) -> allow, don't break on read fail
}

// ---------------- aimbot / triggerbot / hitbox ----------------

static bool LocalToolIsBow();
static void TryReachLunge(const Vec3& target);

static void RunAimbot()
{
    if (!g_cfg.aimbotEnabled || !g_mem.IsAttached()) return;
    if (!IsBedWars()) return;
    static bool toggled = false, wasDown = false;
    if (!ActKey(g_cfg.aimbotKey, g_cfg.aimbotMode, toggled, wasDown)) return;
    if (g_cfg.aimbotKnockCheck && g_localKnocked) return;
    if (g_players.empty()) return;

    float cx = g_localX, cy = g_localY;
    const PlayerBox* best = nullptr;
    float bestMetric = 1e9f;
    float bestD = 0.f;
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
        if (d >= g_cfg.aimbotFov) continue;
        // target priority: crosshair px / nearest studs / lowest hp
        float metric = (g_cfg.aimbotSort == 1) ? p.dist :
                       (g_cfg.aimbotSort == 2) ? p.health : d;
        if (metric < bestMetric) { bestMetric = metric; bestD = d; best = &p; tx = px; ty = py; }
    }
    if (!best) { g_aimbotHadTarget = false; return; }

    // throttled diagnostic so we can verify it acquires + moves
    static DWORD lastAimLog = 0;
    if (GetTickCount() - lastAimLog > 2000)
    {
        lastAimLog = GetTickCount();
        Log("[nyx] aimbot target='%s' dist=%.0fpx players=%zu", best->name.c_str(), bestD, g_players.size());
    }
    g_aimbotHadTarget = true;

    // silent bow aim: instant snap while a bow-type tool is equipped
    bool bowMode = LocalToolIsBow();
    float smooth = bowMode ? 1.f :
        ((g_cfg.aimbotSmoothing && g_cfg.aimbotSmoothStyle == 1) ? g_cfg.aimbotSmooth : 1.f);
    float dx = (tx - cx) * smooth;
    float dy = (ty - cy) * smooth;
    mouse_move((int)dx, (int)dy);
}

static void RunTriggerbot()
{
    if (!g_cfg.triggerbotEnabled || !g_mem.IsAttached()) return;
    if (!IsBedWars()) return;
    static bool toggled = false, wasDown = false;
    // silent bow: auto-fires while a bow is equipped, no key needed
    if (!(LocalToolIsBow() && g_cfg.silentBow) &&
        !ActKey(g_cfg.triggerKey, g_cfg.triggerMode, toggled, wasDown)) return;

    for (auto& p : g_players)
    {
        if (p.teammate) continue;
        float dx = p.headX - g_localX, dy = p.headY - g_localY;
        if (sqrtf(dx * dx + dy * dy) <= g_cfg.triggerFov)
        {
            TryReachLunge(p.rootWorld);
            mouse_click();
            Sleep(60);
            break;
        }
    }
}

// hitbox: cached primitive addrs + original sizes; we always write
// orig*mult (never re-multiply the live size - that was the stutter bug)
struct HitboxEntry { uintptr_t prim; Vec3 orig; Vec3 expected; };
static std::unordered_map<uintptr_t, HitboxEntry> g_hitboxMap;
static std::vector<uintptr_t> g_hitboxParts;
static DWORD g_hitboxScanAt = 0;
static bool g_hitboxActive = false; // for the on-screen indicator

static Vec3 HitboxExpected(const Vec3& orig)
{
    Vec3 ns{ orig.x * g_cfg.hitboxMult, orig.y * g_cfg.hitboxMult, orig.z * g_cfg.hitboxMult };
    if (g_cfg.hitboxMaxStuds > 0.f)
    {
        if (ns.x > g_cfg.hitboxMaxStuds) ns.x = g_cfg.hitboxMaxStuds;
        if (ns.y > g_cfg.hitboxMaxStuds) ns.y = g_cfg.hitboxMaxStuds;
        if (ns.z > g_cfg.hitboxMaxStuds) ns.z = g_cfg.hitboxMaxStuds;
    }
    return ns;
}

static void RestoreHitboxes()
{
    for (auto& [part, e] : g_hitboxMap)
    {
        if (e.prim) g_mem.Write<Vec3>(e.prim + OFF("Primitive", "Size"), e.orig);
    }
    g_hitboxMap.clear();
    g_hitboxParts.clear();
}

static void RunHitbox()
{
    static bool wasHeld = false;
    static bool toggled = false, wasDown = false;
    bool held = g_cfg.hitboxEnabled && g_mem.IsAttached() && GameFocused() &&
        ActKey(g_cfg.hitboxKey, g_cfg.hitboxMode, toggled, wasDown);
    if (!held)
    {
        if (wasHeld) RestoreHitboxes();
        wasHeld = false;
        g_hitboxActive = false;
        return;
    }
    wasHeld = true;
    g_hitboxActive = true;

    DWORD now = GetTickCount();
    auto& w = sdk::Get();
    if (!w.localPlayer) return;
    uintptr_t localTeam = g_mem.ReadPtr(w.localPlayer + OFF("Player", "Team"));

    // rescan the part list ~2hz using the player cache (cheap)
    if (now >= g_hitboxScanAt)
    {
        g_hitboxScanAt = now + 500;
        g_hitboxParts.clear();
        std::unordered_set<uintptr_t> seen;
        for (auto& [paddr, cp] : g_cache)
        {
            if (cp.character == 0) continue;
            if (localTeam && cp.team == localTeam) continue; // don't expand teammates
            // HumanoidRootPart only - it's invisible so the expanded
            // hitbox doesn't render as a big box on the character
            if (cp.hrp) { g_hitboxParts.push_back(cp.hrp); seen.insert(cp.hrp); }
        }
        // dummies/npcs too - no team check, they're always enemies
        for (auto& n : g_npcs)
            if (n.hrp) { g_hitboxParts.push_back(n.hrp); seen.insert(n.hrp); }
        for (auto it = g_hitboxMap.begin(); it != g_hitboxMap.end();)
            it = seen.count(it->first) ? std::next(it) : g_hitboxMap.erase(it);
    }

    // every frame: read current size, rewrite only if it drifted
    // (roblox resets primitive size often - rewriting every frame is
    // what stops the damage stutter)
    static bool logged = false;
    for (uintptr_t part : g_hitboxParts)
    {
        auto it = g_hitboxMap.find(part);
        if (it == g_hitboxMap.end())
        {
            uintptr_t prim = g_mem.ReadPtr(part + OFF("BasePart", "Primitive"));
            if (!prim) continue;
            Vec3 orig = g_mem.Read<Vec3>(prim + OFF("Primitive", "Size"));
            HitboxEntry e{ prim, orig, HitboxExpected(orig) };
            g_hitboxMap[part] = e;
            it = g_hitboxMap.find(part);
            if (!logged)
            {
                logged = true;
                Log("[nyx] hitbox expand part=0x%llx orig=(%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f)",
                    (unsigned long long)part, orig.x, orig.y, orig.z,
                    e.expected.x, e.expected.y, e.expected.z);
            }
        }
        HitboxEntry& e = it->second;
        Vec3 cur = g_mem.Read<Vec3>(e.prim + OFF("Primitive", "Size"));
        if (cur.x != e.expected.x || cur.y != e.expected.y || cur.z != e.expected.z)
            g_mem.Write<Vec3>(e.prim + OFF("Primitive", "Size"), e.expected);
    }
}

// ---------------- equipped tool detection (silent bow aim) ----------------

static std::string g_localTool;
static DWORD g_toolRefresh = 0;

static bool LocalToolIsBow()
{
    if (!g_cfg.silentBow || !g_mem.IsAttached()) { g_localTool.clear(); return false; }
    DWORD now = GetTickCount();
    if (now >= g_toolRefresh)
    {
        g_toolRefresh = now + 500;
        g_localTool.clear();
        auto& w = sdk::Get();
        if (w.localPlayer)
        {
            uintptr_t charAddr = g_mem.ReadPtr(w.localPlayer + OFF("Player", "ModelInstance"));
            if (charAddr)
            {
                for (auto& c : Instance(charAddr).GetChildren())
                    if (c.GetClassName() == "Tool") { g_localTool = ToLower(c.GetName()); break; }
            }
        }
    }
    return g_localTool.find("bow") != std::string::npos ||
           g_localTool.find("crossbow") != std::string::npos ||
           g_localTool.find("slingshot") != std::string::npos ||
           g_localTool.find("musket") != std::string::npos;
}

// ---------------- hit fix ----------------
// while LMB is held, zero the local HRP's horizontal velocity so your
// character stays planted during swings - hits register the same every
// time regardless of ping/fps. closest external equivalent of the
// executor hitfix (which hooks the sword function inside the game).

static uintptr_t g_hitfixHrp = 0, g_hitfixPrim = 0;
static DWORD g_hitfixRefresh = 0;

static void RunHitfix()
{
    bool want = g_cfg.hitfixEnabled && g_mem.IsAttached() && GameFocused();
    if (!want) { g_hitfixHrp = 0; g_hitfixRefresh = 0; return; }
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return; // only while swinging

    DWORD now = GetTickCount();
    auto& w = sdk::Get();
    if (!w.localPlayer) return;

    if (now >= g_hitfixRefresh || !g_hitfixHrp)
    {
        g_hitfixRefresh = now + 500;
        uintptr_t charAddr = g_mem.ReadPtr(w.localPlayer + OFF("Player", "ModelInstance"));
        g_hitfixHrp = 0;
        if (charAddr)
        {
            Instance ch(charAddr);
            for (auto& c : ch.GetChildren())
            {
                std::string cn = c.GetName();
                if (cn == "HumanoidRootPart") { g_hitfixHrp = c.addr; break; }
                if (cn == "Torso") g_hitfixHrp = c.addr;
            }
        }
        g_hitfixPrim = g_hitfixHrp ? g_mem.ReadPtr(g_hitfixHrp + OFF("BasePart", "Primitive")) : 0;
    }
    if (!g_hitfixPrim) return;

    Vec3 vel = g_mem.Read<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyLinearVelocity"));
    if (vel.x != 0.f || vel.z != 0.f)
        g_mem.Write<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyLinearVelocity"), { 0.f, vel.y, 0.f });
}

// ---------------- reach lunge ----------------
// on click frames, briefly step the local HRP toward the target so the
// game's own distance check sees you in range. the position is restored
// ~90ms later so it can't become a permanent teleport - that's what keeps
// hitreg intact while extending effective reach.

struct ReachLunge { uintptr_t prim = 0; Vec3 orig{}; DWORD restoreAt = 0; };
static ReachLunge g_lunge;

static uintptr_t LocalHrpPrimitive()
{
    if (!g_localHrpAddr) return 0;
    return g_mem.ReadPtr(g_localHrpAddr + OFF("BasePart", "Primitive"));
}

static void TryReachLunge(const Vec3& target)
{
    if (!g_cfg.reachEnabled || g_cfg.reachStuds <= 0.f || !g_mem.IsAttached()) return;
    if (g_lunge.prim) return; // already lunging - don't overwrite the restore point
    uintptr_t prim = LocalHrpPrimitive();
    if (!prim) return;
    Vec3 pos = g_mem.Read<Vec3>(prim + OFF("Primitive", "Position"));
    float dx = target.x - pos.x, dz = target.z - pos.z;
    float flat = sqrtf(dx * dx + dz * dz);
    if (flat < 8.f || flat > 45.f) return; // on top of them / too far to matter
    float step = g_cfg.reachStuds < flat ? g_cfg.reachStuds : flat;
    Vec3 np{ pos.x + dx / flat * step, pos.y, pos.z + dz / flat * step };
    g_lunge.prim = prim;
    g_lunge.orig = pos;
    g_lunge.restoreAt = GetTickCount() + 90;
    g_mem.Write<Vec3>(prim + OFF("Primitive", "Position"), np);
}

static void UpdateReachLunge()
{
    if (!g_lunge.prim || !g_lunge.restoreAt) return;
    if (GetTickCount() < g_lunge.restoreAt) return;
    if (g_mem.IsAttached())
        g_mem.Write<Vec3>(g_lunge.prim + OFF("Primitive", "Position"), g_lunge.orig);
    g_lunge = {};
}

// ---------------- anti-void ----------------
// remembers the highest Y you've stood at recently; if you drop far below
// it (void/fall behind the map) you get teleported back up. the peak
// re-baselines after 8s at low altitude so legit descents don't trigger it.

static float g_voidPeakY = -1e9f;
static DWORD g_voidPeakAt = 0;

static void RunAntiVoid()
{
    if (!g_cfg.antiVoid || !g_mem.IsAttached() || !GameFocused())
    {
        g_voidPeakY = -1e9f;
        return;
    }
    if (!g_localHrpAddr) return;
    uintptr_t prim = LocalHrpPrimitive();
    if (!prim) return;

    DWORD now = GetTickCount();
    Vec3 pos = g_mem.Read<Vec3>(prim + OFF("Primitive", "Position"));
    if (pos.y > g_voidPeakY || now - g_voidPeakAt > 8000)
    {
        g_voidPeakY = pos.y;
        g_voidPeakAt = now;
    }
    if (pos.y < g_voidPeakY - g_cfg.antiVoidDist)
    {
        Vec3 np{ pos.x, g_voidPeakY - 2.f, pos.z };
        g_mem.Write<Vec3>(prim + OFF("Primitive", "Position"), np);
        Vec3 zero{ 0.f, 0.f, 0.f };
        g_mem.Write<Vec3>(prim + OFF("Primitive", "AssemblyLinearVelocity"), zero);
        static DWORD lastLog = 0;
        if (now - lastLog > 2000)
        {
            lastLog = now;
            Log("[nyx] anti-void rescue y=%.1f -> %.1f", pos.y, np.y);
        }
    }
}

// ---------------- killaura ----------------
// nyx1-style: acquire players in swing range, filter by facing angle,
// aim at the target and click at CPS while inside attack range.
// (scripts use firetouchinterest for hits - externals must click)

static void RunKillaura()
{
    if (!g_cfg.killauraEnabled || !g_mem.IsAttached()) return;
    if (!IsBedWars()) return;
    static bool toggled = false, wasDown = false;
    if (!ActKey(g_cfg.kaKey, g_cfg.kaMode, toggled, wasDown)) return;
    if (g_cfg.kaRequireMouse && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
    if (g_localKnocked) return;
    if (g_players.empty() || !g_localHrpAddr) return;

    Vec3 lpos = CachedLocalPos();
    if (lpos.x == 0.f && lpos.y == 0.f && lpos.z == 0.f) return;

    // local facing from HRP rotation matrix (look = -Z column)
    uintptr_t lprim = g_mem.ReadPtr(g_localHrpAddr + OFF("BasePart", "Primitive"));
    if (!lprim) return;
    float r[9];
    for (int i = 0; i < 9; i++) r[i] = g_mem.Read<float>(lprim + OFF("Primitive", "Rotation") + i * 4);
    float lx = -r[2], lz = -r[8];
    float llen = sqrtf(lx * lx + lz * lz);
    if (llen < 0.001f) return;
    lx /= llen; lz /= llen;

    const PlayerBox* best = nullptr;
    float bestDist = 1e9f;
    for (auto& p : g_players)
    {
        if (p.teammate && g_cfg.kaTeamCheck) continue;
        if (p.health <= 0.f) continue;

        float dx = p.rootWorld.x - lpos.x, dy = p.rootWorld.y - lpos.y, dz = p.rootWorld.z - lpos.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > g_cfg.kaSwingRange) continue;

        // flat facing angle vs target direction
        float flat = sqrtf(dx * dx + dz * dz);
        if (flat > 0.01f && g_cfg.kaAngle < 360.f)
        {
            float dot = (lx * dx + lz * dz) / flat;
            if (dot > 1.f) dot = 1.f; if (dot < -1.f) dot = -1.f;
            float angDeg = acosf(dot) * 57.29578f;
            if (angDeg > g_cfg.kaAngle / 2.f) continue;
        }
        if (dist < bestDist) { bestDist = dist; best = &p; }
    }
    if (!best) return;

    // aim at the predicted head position - leads moving targets so the
    // swing connects instead of whiffing on where they were
    Vec3 aimWorld = best->headWorld;
    if (g_cfg.kaPredMs > 0.f)
    {
        float lead = g_cfg.kaPredMs / 1000.f;
        aimWorld.x += best->headVel.x * lead;
        aimWorld.y += best->headVel.y * lead;
        aimWorld.z += best->headVel.z * lead;
    }
    float tx = best->headX, ty = best->headY;
    Vec2 ts;
    if (sdk::WorldToScreen(aimWorld, ts) && ts.x != -1.f) { tx = ts.x; ty = ts.y; }

    // subpixel accumulation: fractional pixels carry over between frames
    // instead of being truncated away (was leaving a permanent deadzone)
    float errX = (tx - g_localX) * g_cfg.kaSmooth;
    float errY = (ty - g_localY) * g_cfg.kaSmooth;
    static float accX = 0.f, accY = 0.f;
    accX += errX; accY += errY;
    int mx = (int)accX, my = (int)accY;
    accX -= mx; accY -= my;
    if (mx || my) mouse_move(mx, my);

    // only swing when the crosshair is actually on the target - clicking
    // at CPS while the aim is still travelling was the main whiff source
    float aimErr = sqrtf((tx - g_localX) * (tx - g_localX) +
                         (ty - g_localY) * (ty - g_localY));

    // click at CPS while inside attack range (+ reachStuds when reach is on
    // - the lunge steps you into range for that click, then restores)
    float clickRange = g_cfg.kaAttackRange + (g_cfg.reachEnabled ? g_cfg.reachStuds : 0.f);
    if (bestDist <= clickRange && aimErr <= g_cfg.kaClickPx)
    {
        static ULONGLONG nextClick = 0;
        ULONGLONG now = GetTickCount64();
        double interval = 1000.0 / (g_cfg.kaCps > 0.f ? g_cfg.kaCps : 1.f);
        if (now >= nextClick)
        {
            nextClick += (ULONGLONG)interval;
            if (now > nextClick + (ULONGLONG)interval) nextClick = now; // dropped >1 slot, resync
            if (bestDist > g_cfg.kaAttackRange) TryReachLunge(best->rootWorld);
            mouse_click();
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
    ImGui::SliderFloat("ANGLE", &g_cfg.aimbotFov, 10.f, 360.f, "%.0f px");
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
    const char* sorts[] = { "Crosshair", "Nearest", "Lowest HP" };
    ImGui::Combo("Target", &g_cfg.aimbotSort, sorts, 3);
    ImGui::Checkbox("BedWars only", &g_cfg.bedwarsOnly);
    ImGui::SliderFloat("Range (studs)", &g_cfg.aimbotRange, 5.f, 150.f, "%.0f");
    ImGui::Combo("Activation", &g_cfg.aimbotMode, kActModes, 3);
    RebindButton("Key", &g_cfg.aimbotKey);
    if (g_cfg.aimbotMode == 1)
        ImGui::TextDisabled("Press %s to toggle aim assist", VkName(g_cfg.aimbotKey));
    ImGui::Checkbox("Silent aim (bow)", &g_cfg.silentBow);
    if (g_cfg.silentBow)
        ImGui::TextDisabled("Instant snap + auto-shoot while bow/crossbow/slingshot equipped");
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
    RebindButton("Key", &g_cfg.triggerKey);
}

static void MenuHitbox()
{
    ImGui::Checkbox("Enabled", &g_cfg.hitboxEnabled);
    ImGui::SliderFloat("Multiplier", &g_cfg.hitboxMult, 1.f, 6.f, "%.1fx");
    ImGui::SliderFloat("Max studs (0=off)", &g_cfg.hitboxMaxStuds, 0.f, 30.f, "%.1f");
    ImGui::Combo("Activation", &g_cfg.hitboxMode, kActModes, 3);
    RebindButton("Key", &g_cfg.hitboxKey);
    ImGui::Separator();
    ImGui::Text("Hit Fix");
    ImGui::Checkbox("Enabled##hitfix", &g_cfg.hitfixEnabled);
    ImGui::TextDisabled("Plants you while swinging for consistent hitreg");
    ImGui::Separator();
    ImGui::Text("Killaura");
    ImGui::Checkbox("Enabled##ka", &g_cfg.killauraEnabled);
    ImGui::SliderFloat("CPS", &g_cfg.kaCps, 1.f, 40.f, "%.0f");
    ImGui::SliderFloat("Swing range (studs)", &g_cfg.kaSwingRange, 1.f, 30.f, "%.0f");
    ImGui::SliderFloat("Attack range (studs)", &g_cfg.kaAttackRange, 1.f, 30.f, "%.0f");
    ImGui::SliderFloat("Max angle", &g_cfg.kaAngle, 1.f, 360.f, "%.0f");
    ImGui::SliderFloat("Aim smooth", &g_cfg.kaSmooth, 0.05f, 1.f, "%.2f");
    ImGui::SliderFloat("Prediction (ms)", &g_cfg.kaPredMs, 0.f, 300.f, "%.0f");
    ImGui::SliderFloat("Click within px", &g_cfg.kaClickPx, 2.f, 80.f, "%.0f");
    ImGui::Combo("Activation##ka", &g_cfg.kaMode, kActModes, 3);
    RebindButton("Key##ka", &g_cfg.kaKey);
    if (g_cfg.kaMode == 1)
        ImGui::TextDisabled("Press %s to toggle killaura", VkName(g_cfg.kaKey));
    ImGui::Checkbox("Require mouse down##ka", &g_cfg.kaRequireMouse);
    ImGui::Checkbox("Team check##ka", &g_cfg.kaTeamCheck);
    ImGui::TextDisabled("Auto-aims and clicks at targets in range/angle");
    ImGui::Separator();
    ImGui::Text("Reach");
    ImGui::Checkbox("Enabled##reach", &g_cfg.reachEnabled);
    ImGui::SliderFloat("Studs##reach", &g_cfg.reachStuds, 0.f, 12.f, "%.1f");
    ImGui::TextDisabled("Extends melee reach on click - auto-restores, hitreg safe");
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
    ImGui::Text("World");
    ImGui::Checkbox("Anti-void", &g_cfg.antiVoid);
    if (g_cfg.antiVoid)
        ImGui::SliderFloat("Fall distance", &g_cfg.antiVoidDist, 10.f, 120.f, "%.0f studs");
    ImGui::TextDisabled("Teleports you back up if you fall too far");
    ImGui::Separator();
    ImGui::Text("Interface");
    const char* themes[] = { "Midnight", "Incognito", "Neon", "Light", "Vape" };
    if (ImGui::Combo("Theme", &g_cfg.uiTheme, themes, 5)) config::Save();
    const char* layouts[] = { "Tabs", "Sidebar" };
    if (ImGui::Combo("Layout", &g_cfg.uiLayout, layouts, 2)) config::Save();
    ImGui::ColorEdit3("Accent", g_cfg.accent, ImGuiColorEditFlags_NoInputs);
    ImGui::Text("Keybinds");
    RebindButton("Menu", &g_cfg.menuKey);
    ImGui::TextDisabled("Feature keys are rebound inside each tab");
    ImGui::TextDisabled("INSERT still toggles this menu");
    ImGui::Text("Roblox PID: %lu", g_mem.pid);
    ImGui::Text("Base: 0x%llx", (unsigned long long)g_mem.base);
    ImGui::Text("Version: v%s", NYX_VERSION);
    ImGui::Text("PlaceId: %llu", (unsigned long long)g_lastPlaceId);
    ImGui::Text("BedWars: %s", g_isBedWars == 1 ? "yes" : (g_isBedWars == 0 ? "NO (combat off)" : "unknown"));
    ImGui::Text("Offsets: %s", offsets::Ready() ? offsets::ClientVersion().c_str() : "fetching...");
    ImGui::Separator();
    ImGui::Text("Configs");
    static char cfgName[64] = "default";
    ImGui::InputText("Name", cfgName, sizeof(cfgName));
    if (ImGui::Button("Save", { 80, 0 }))
    {
        char p[MAX_PATH]; snprintf(p, sizeof(p), "config\\%s.cfg", cfgName);
        config::Save(p); config::Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", { 80, 0 }))
    {
        char p[MAX_PATH]; snprintf(p, sizeof(p), "config\\%s.cfg", cfgName);
        config::Load(p); config::Save();
    }
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
    Log("[nyx] === v%s starting ===", NYX_VERSION);
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

        UpdateReachLunge();
        if (focused)
        {
            RunAimbot();
            RunTriggerbot();
            RunHitbox();
            RunHitfix();
            RunKillaura();
            RunAntiVoid();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        if (g_mem.IsAttached() && focused) DrawEsp(bg);
        // feature status indicators - hitbox/reach are invisible by design
        // so show a small marker so you can tell they're actually running
        if (g_hitboxActive)
            bg->AddText({ 12.f, 12.f }, IM_COL32(120, 220, 255, 220), "HITBOX");
        if (g_cfg.reachEnabled && g_lunge.prim)
            bg->AddText({ 12.f, 30.f }, IM_COL32(120, 220, 255, 220), "REACH");
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
