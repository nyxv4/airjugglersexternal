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
#include <cstring>

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

// r15 skeleton parts, cached per player for skeleton esp
enum SkelIdx
{
    SK_UPPERT, SK_LOWERT,
    SK_LUA, SK_LLA, SK_LH,   // left arm: upper/lower/hand
    SK_RUA, SK_RLA, SK_RH,   // right arm
    SK_LUL, SK_LLL, SK_LF,   // left leg: upper/lower/foot
    SK_RUL, SK_RLL, SK_RF,   // right leg
    SK_COUNT
};

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
    bool kitAttr = false; // kit came from a replicated attribute (reliable)
    bool isNpc = false;   // dummy/bot - kit ESP doesn't apply
    Vec3 skelPos[SK_COUNT]{}; // world positions, zero = missing
    bool skelOk = false;
    std::vector<uintptr_t> hitPrims; // primitive ptrs for hitbox esp
};

static std::vector<PlayerBox> g_players;
static bool g_localKnocked = false;
static bool g_aimbotHadTarget = false;
// live feature state for the on-screen status line - set inside each
// Run* function right after its activation check passes
static bool g_aimbotActive = false;
static Vec2 g_localScreen;
static float g_localX, g_localY; // screen center of roblox client
// (g_lastSwingAt removed - hitfix no longer tracks swing windows)

// ---------------- kit esp ----------------
// bedwars kits + generic kit names; matched case-insensitively against
// player/backpack/character child names (tools, values, folders)
struct KitEntry { const char* key; const char* label; ImU32 col; };
static const KitEntry kKits[] = {
    // real bedwars kit ids (PlayingAsKits attribute values) - specific
    // names first so substring matching doesn't get shadowed
    { "spirit_assassin", "SPIRIT ASSASSIN", IM_COL32(90, 70, 120, 255) },
    { "void_knight", "VOID KNIGHT", IM_COL32(120, 90, 220, 255) },
    { "void_regent", "VOID REGENT", IM_COL32(140, 100, 240, 255) },
    { "void_hunter", "VOID HUNTER", IM_COL32(110, 80, 200, 255) },
    { "block_kicker", "BLOCK KICKER", IM_COL32(200, 160, 60, 255) },
    { "elk_master", "ELK MASTER", IM_COL32(160, 110, 60, 255) },
    { "black_market_trader", "BLACK MARKET", IM_COL32(60, 50, 70, 255) },
    { "ghost_catcher", "GHOST CATCHER", IM_COL32(160, 200, 230, 255) },
    { "sheep_herder", "SHEEP HERDER", IM_COL32(220, 220, 240, 255) },
    { "star_collector", "STAR COLLECTOR", IM_COL32(240, 220, 90, 255) },
    { "metal_detector", "METAL DETECTOR", IM_COL32(170, 170, 180, 255) },
    { "farmer_cletus", "FARMER", IM_COL32(140, 190, 80, 255) },
    { "winter_lady", "WINTER LADY", IM_COL32(160, 210, 240, 255) },
    { "kitslinger", "KITSLINGER", IM_COL32(200, 60, 140, 255) },
    { "eldertree", "ELDERTREE", IM_COL32(90, 160, 70, 255) },
    { "gingerbread", "GINGERBREAD", IM_COL32(200, 120, 60, 255) },
    { "earthshaper", "EARTHSHAPER", IM_COL32(150, 110, 70, 255) },
    { "venomancer", "VENOMANCER", IM_COL32(90, 160, 60, 255) },
    { "battlemage", "BATTLEMAGE", IM_COL32(120, 100, 220, 255) },
    { "necromancer", "NECROMANCER", IM_COL32(70, 50, 90, 255) },
    { "ghost", "GHOST", IM_COL32(170, 190, 220, 255) },
    { "alchemist", "ALCHEMIST", IM_COL32(120, 200, 90, 255) },
    { "beekeeper", "BEEKEEPER", IM_COL32(240, 200, 60, 255) },
    { "bigman", "BIGMAN", IM_COL32(140, 100, 70, 255) },
    { "miner", "MINER", IM_COL32(160, 120, 80, 255) },
    { "trapper", "TRAPPER", IM_COL32(150, 110, 70, 255) },
    { "battery", "BATTERY", IM_COL32(240, 200, 60, 255) },
    { "summoner", "SUMMONER", IM_COL32(90, 200, 120, 255) },
    { "tinker", "TINKER", IM_COL32(200, 160, 80, 255) },
    { "hannah", "HANNAH", IM_COL32(230, 150, 200, 255) },
    { "dasher", "DASHER", IM_COL32(120, 200, 230, 255) },
    { "regent", "REGENT", IM_COL32(170, 130, 230, 255) },
    { "jade", "JADE", IM_COL32(60, 190, 110, 255) },
    { "aery", "AERY", IM_COL32(140, 200, 230, 255) },
    { "berserker", "BERSERKER", IM_COL32(200, 60, 50, 255) },
    { "disruptor", "DISRUPTOR", IM_COL32(200, 80, 160, 255) },
    { "mimic", "MIMIC", IM_COL32(160, 140, 110, 255) },
    { "yeti", "YETI", IM_COL32(190, 220, 240, 255) },
    { "kaliyah", "KALIYAH", IM_COL32(220, 90, 200, 255) },
    { "phoenix", "PHOENIX", IM_COL32(240, 120, 50, 255) },
    { "raven", "RAVEN", IM_COL32(60, 60, 80, 255) },
    { "ninja", "NINJA", IM_COL32(50, 50, 60, 255) },
    { "pyro", "PYRO", IM_COL32(230, 90, 40, 255) },
    { "tank", "TANK", IM_COL32(140, 140, 150, 255) },
    { "golem", "GOLEM", IM_COL32(130, 120, 110, 255) },
    { "pirate", "PIRATE", IM_COL32(80, 110, 160, 255) },
    { "crypt", "CRYPT", IM_COL32(90, 70, 60, 255) },
    { "sorcerer", "SORCERER", IM_COL32(140, 100, 230, 255) },
    { "engineer", "ENGINEER", IM_COL32(200, 150, 80, 255) },
    { "lancer", "LANCER", IM_COL32(170, 170, 190, 255) },
    { "tracking", "TRACKING", IM_COL32(100, 160, 220, 255) },
    { "mystery", "MYSTERY", IM_COL32(180, 120, 220, 255) },
    { "artificer", "ARTIFICER", IM_COL32(200, 140, 90, 255) },
    { "demon", "DEMON", IM_COL32(180, 40, 40, 255) },
    { "mana", "MANA", IM_COL32(90, 140, 230, 255) },
    { "farmer", "FARMER", IM_COL32(140, 190, 80, 255) },
    { "guardian", "GUARDIAN", IM_COL32(160, 170, 200, 255) },
    { "rogue", "ROGUE", IM_COL32(80, 80, 90, 255) },
    { "snowman", "SNOWMAN", IM_COL32(200, 220, 240, 255) },
    // bedwars kits
    { "voidknight", "VOID KNIGHT", IM_COL32(120, 90, 220, 255) },
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
    // pass 1: value contains the kit key ("void_knight" -> VOID KNIGHT)
    for (auto& k : kKits)
        if (lower.find(k.key) != std::string::npos) return &k;
    // pass 2: abbreviated value contained in a kit key - only for values
    // >=4 chars so "pyro" can't match everything, and "none" never does
    if (lower.size() >= 4 && lower != "none")
        for (auto& k : kKits)
            if (strlen(k.key) > lower.size() && std::string(k.key).find(lower) != std::string::npos)
                return &k;
    return nullptr;
}

static std::string ToLower(std::string s)
{
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static bool IsPartClass(const std::string& c);

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
    bool kitAttr = false; // kit came from a replicated attribute (reliable)
                        // vs a child-name guess (unreliable)
    bool isNpc = false;   // dummy/bot - kit ESP doesn't apply
    DWORD refreshAt = 0;
    // primitive pointers for every body part - hitbox esp reads pos/size/
    // rotation straight off these, no per-frame child walks
    std::vector<uintptr_t> prims;
    // r15 limb part addresses for skeleton esp (0 = not present)
    uintptr_t skel[SK_COUNT] = {};
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
    cp.prims.clear();
    memset(cp.skel, 0, sizeof(cp.skel));
    cp.team = g_mem.ReadPtr(p.addr + OFF("Player", "Team"));
    cp.refreshAt = GetTickCount() + 500;

    Instance ch(cp.character);
    if (!ch) return;

    cp.kit.clear();
    cp.kitAttr = false;
    auto tryKit = [&](const std::string& n) {
        // name guesses fill in first but never override an
        // attribute-sourced kit found later
        if (!cp.kit.empty() && cp.kitAttr) return;
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
        else if (cn == "UpperTorso") { cp.upperTorso = c.addr; cp.skel[SK_UPPERT] = c.addr; }
        else if (cn == "LowerTorso") { cp.lowerTorso = c.addr; cp.skel[SK_LOWERT] = c.addr; }
        // r15 limb parts for skeleton esp - name matched once at refresh
        else
        {
            static const struct { const char* n; int i; } sk[] = {
                {"LeftUpperArm", SK_LUA}, {"LeftLowerArm", SK_LLA}, {"LeftHand", SK_LH},
                {"RightUpperArm", SK_RUA}, {"RightLowerArm", SK_RLA}, {"RightHand", SK_RH},
                {"LeftUpperLeg", SK_LUL}, {"LeftLowerLeg", SK_LLL}, {"LeftFoot", SK_LF},
                {"RightUpperLeg", SK_RUL}, {"RightLowerLeg", SK_RLL}, {"RightFoot", SK_RF},
            };
            for (auto& s : sk) if (cn == s.n) { cp.skel[s.i] = c.addr; break; }
        }
        if (cp.humanoid == 0 && c.GetClassName() == "Humanoid") cp.humanoid = c.addr;
        // hitbox esp: cache primitive ptrs for every body part
        if (g_cfg.espHitbox && cp.prims.size() < 16 && IsPartClass(c.GetClassName()))
        {
            uintptr_t prim = g_mem.ReadPtr(c.addr + OFF("BasePart", "Primitive"));
            if (prim) cp.prims.push_back(prim);
        }
    }
    // NOTE: display-name / humanoid-name guessing was removed - usernames
    // are not kit identifiers and produced false kit labels. reliable
    // sources only: replicated attributes (below) + instance names.

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
                    {
                        // attribute values are the documented kit id source
                        // (PlayingAsKits=pyro etc.) - they override name guesses
                        if (auto* k = MatchKit(ToLower(v)))
                        { cp.kit = k->label; cp.kitCol = k->col; cp.kitAttr = true; }
                    }
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
static ImU32 HitboxEspColor();
static bool EspRayVisible(const Vec3& eye, const Vec3& tgt);

// ---------------- npc/dummy scan (bedwars bots etc.) ----------------
// dummies are Models in Workspace, not Player instances - scan for
// models containing Head + HumanoidRootPart + Humanoid, ~every 3s

struct NpcCache
{
    uintptr_t model = 0;
    std::string name;
    uintptr_t head = 0, hrp = 0, torso = 0, upper = 0, lower = 0, humanoid = 0;
    std::vector<uintptr_t> prims;
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
        if (g_cfg.espHitbox && n.prims.size() < 16 && IsPartClass(c.GetClassName()))
        {
            uintptr_t prim = g_mem.ReadPtr(c.addr + OFF("BasePart", "Primitive"));
            if (prim) n.prims.push_back(prim);
        }
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

// ---------------- object esp ----------------
// kit-specific world objects (hidden metal, elder orbs, bees, traps...)
// - names come from the bedwars script's ESPKits tag table. we scan
// workspace descendants every few seconds, resolve each match to a
// BasePart, then just read positions per frame.

struct ObjEspDef { const char* key; const char* label; ImU32 col; };
static const ObjEspDef kObjEsp[] = {
    // generators + ore blocks first (specific names win)
    { "diamond_oregenerator", "DIAMOND GEN", IM_COL32(85, 200, 255, 255) },
    { "emerald_oregenerator", "EMERALD GEN", IM_COL32(0, 255, 100, 255) },
    { "iron_oregenerator", "IRON GEN", IM_COL32(200, 200, 200, 255) },
    { "oregenerator", "ORE GEN", IM_COL32(200, 200, 200, 255) },
    { "teamgen", "TEAM GEN", IM_COL32(255, 255, 255, 255) },
    { "iron_ore_mesh_block", "IRON ORE", IM_COL32(190, 190, 200, 255) },
    { "hidden-metal", "METAL", IM_COL32(240, 200, 80, 255) },
    { "treeorb", "ELDER ORB", IM_COL32(90, 220, 120, 255) },
    { "natures_essence", "ESSENCE", IM_COL32(120, 230, 140, 255) },
    { "beehive", "BEEHIVE", IM_COL32(240, 210, 60, 255) },
    { "tamedbee", "BEE", IM_COL32(240, 210, 60, 255) },
    { "bee", "BEE", IM_COL32(240, 210, 60, 255) },
    { "ghost_orb", "GHOST ORB", IM_COL32(170, 200, 240, 255) },
    { "ghost", "GHOST", IM_COL32(170, 200, 240, 255) },
    { "thorns", "THORNS", IM_COL32(90, 160, 70, 255) },
    { "alchemy_crystal", "CRYSTAL", IM_COL32(150, 110, 230, 255) },
    { "wild_flower", "FLOWER", IM_COL32(230, 140, 200, 255) },
    { "crit_star", "STAR", IM_COL32(240, 220, 90, 255) },
    { "shadow_coin", "COIN", IM_COL32(200, 190, 90, 255) },
    { "petrified-player", "FOSSIL", IM_COL32(200, 200, 190, 255) },
    { "large_rock", "ROCK", IM_COL32(150, 140, 120, 255) },
    { "snap_trap", "TRAP", IM_COL32(230, 90, 60, 255) },
    { "elementtome", "TOME", IM_COL32(140, 100, 230, 255) },
    { "mage_spellbook", "SPELLBOOK", IM_COL32(140, 100, 230, 255) },
    { "sheepmodel", "SHEEP", IM_COL32(235, 235, 245, 255) },
    { "purple_hay_bale", "HAY", IM_COL32(190, 140, 220, 255) },
    { "telepearl", "PEARL", IM_COL32(200, 220, 240, 255) },
    { "landmine", "MINE", IM_COL32(160, 160, 60, 255) },
    { "tesla_trap", "TESLA", IM_COL32(120, 200, 240, 255) },
    { "venom_trap", "VENOM TRAP", IM_COL32(120, 200, 60, 255) },
    // names as the game actually spells them (tags != names)
    { "critstar", "CRIT STAR", IM_COL32(240, 220, 90, 255) },
    { "vitalitystar", "VITALITY", IM_COL32(240, 220, 90, 255) },
    { "mushroom", "SHROOM", IM_COL32(200, 120, 200, 255) },
    { "hidden_metal", "METAL", IM_COL32(240, 200, 80, 255) },
    { "hiddenmetal", "METAL", IM_COL32(240, 200, 80, 255) },
    { "gravestone", "GRAVE", IM_COL32(160, 160, 170, 255) },
    { "snow_pile", "SNOW PILE", IM_COL32(220, 235, 255, 255) },
    { "snowpile", "SNOW PILE", IM_COL32(220, 235, 255, 255) },
    { "desert_pot", "POT", IM_COL32(200, 140, 90, 255) },
    { "enchant-table", "ENCHANT", IM_COL32(120, 200, 240, 255) },
    { "enchanttable", "ENCHANT", IM_COL32(120, 200, 240, 255) },
    { "owl", "OWL", IM_COL32(200, 200, 220, 255) },
    { "evelynn", "SOUL", IM_COL32(170, 200, 240, 255) },
    // farmer crops (DescendantKits)
    { "carrot", "CARROT", IM_COL32(240, 140, 40, 255) },
    { "melon", "MELON", IM_COL32(120, 220, 80, 255) },
    { "pumpkin", "PUMPKIN", IM_COL32(240, 140, 30, 255) },
};

struct ObjEspEntry { uintptr_t part; char label[32]; ImU32 col; };
static std::vector<ObjEspEntry> g_objEsp;
static std::vector<ObjEspEntry> g_itemDrops; // workspace.ItemDrops resources

static bool IsPartClass(const std::string& c)
{
    return c == "Part" || c == "MeshPart" || c == "UnionOperation" ||
           c == "WedgePart" || c == "CornerWedgePart" || c == "TrussPart" ||
           c == "SpawnLocation" || c == "Seat" || c == "VehicleSeat";
}

// resolve a matched instance to a drawable part - the part itself, a
// model's PrimaryPart, or a nested descendant part (kit objects nest
// parts inside folders/models)
static uintptr_t ResolveObjPart(uintptr_t inst, int depth = 0)
{
    Instance in(inst);
    std::string cls = in.GetClassName();
    if (IsPartClass(cls)) return inst;
    if (cls != "Model" || depth > 3) return 0;

    // kit collectables (hidden-metal, orbs, bees...) are Models whose
    // PrimaryPart is the drawable piece - read it directly first
    uintptr_t ppOff = OFF("Model", "PrimaryPart");
    if (ppOff)
    {
        uintptr_t pp = g_mem.ReadPtr(inst + ppOff);
        if (pp) return pp;
    }
    for (auto& c : in.GetChildren())
    {
        uintptr_t r = ResolveObjPart(c.addr, depth + 1);
        if (r) return r;
    }
    return 0;
}

static void ScanObjectEsp()
{
    g_objEsp.clear();
    auto& w = sdk::Get();
    if (!w.workspace) return;

    struct Frame { uintptr_t inst; int depth; };
    std::vector<Frame> stack{ { w.workspace, 0 } };
    std::unordered_set<uintptr_t> seenParts;
    std::unordered_set<uintptr_t> seenModels;
    static std::unordered_set<std::string> loggedCollectables;
    int budget = 9000; // hard cap on instances visited per scan

    while (!stack.empty() && budget-- > 0)
    {
        Frame f = stack.back(); stack.pop_back();
        Instance in(f.inst);
        std::string cls = in.GetClassName();

        std::string name = in.GetName();
        bool matched = false;
        if (!name.empty())
        {
            for (auto& c : name) c = (char)tolower((unsigned char)c);
            for (auto& d : kObjEsp)
            {
                if (name.find(d.key) != std::string::npos)
                {
                    uintptr_t part = ResolveObjPart(f.inst);
                    if (part && !seenParts.count(part))
                    {
                        seenParts.insert(part);
                        ObjEspEntry e{ part, "", d.col };
                        snprintf(e.label, sizeof(e.label), "%s", d.label);
                        g_objEsp.push_back(e);
                    }
                    matched = true;
                    break; // first matching def wins for this instance
                }
            }
        }

        // kit collectables are CollectionService-tagged, not named - the
        // tag never reaches us, but the models carry an 'Id' (or 'BeeId')
        // replicated attribute + a PrimaryPart. check attrs only on
        // Models that have one so the cost stays tiny.
        if (!matched && cls == "Model" && !seenModels.count(f.inst))
        {
            seenModels.insert(f.inst);
            uintptr_t ppOff = OFF("Model", "PrimaryPart");
            if (ppOff && g_mem.ReadPtr(f.inst + ppOff))
            {
                auto attrs = in.GetAttributes(false);
                bool hasId = false, isBee = false;
                std::string attrDump;
                for (auto& [k, v] : attrs)
                {
                    if (k == "Id") hasId = true;
                    if (k == "BeeId") isBee = true;
                    if (attrDump.size() < 200) { attrDump += k; attrDump += "="; attrDump += v; attrDump += ";"; }
                }
                if (!attrDump.empty() && loggedCollectables.insert(name).second)
                    Log("[objesp] model='%s' attrs: %s", name.c_str(), attrDump.c_str());
                if (hasId || isBee)
                {
                    uintptr_t part = ResolveObjPart(f.inst);
                    if (part && !seenParts.count(part))
                    {
                        seenParts.insert(part);
                        ObjEspEntry e{ part, "", isBee ? IM_COL32(240, 210, 60, 255)
                                                       : IM_COL32(255, 214, 120, 255) };
                        if (isBee) snprintf(e.label, sizeof(e.label), "BEE");
                        else
                        {
                            std::string up = name;
                            for (auto& ch : up) ch = (char)toupper((unsigned char)ch);
                            // strip filler words, keep it short
                            snprintf(e.label, sizeof(e.label), "%s", up.c_str());
                        }
                        g_objEsp.push_back(e);
                    }
                }
            }
        }
        if (f.depth < 7)
            for (auto& c : in.GetChildren())
                stack.push_back({ c.addr, f.depth + 1 });
    }
}

// workspace.ItemDrops holds dropped resources - children are named
// exactly 'diamond'/'emerald'/'iron'/etc. scanned on a faster cadence
// than the big object sweep since loot moves
static const char* kResourceNames[] = { "diamond", "emerald", "iron", "gold", "wood", "stone", "fiber" };
static const char* kResourceExcl[] = {
    "_sword", "_axe", "_pickaxe", "_dao", "_bow", "_crossbow", "_rod",
    "_boots", "_chestplate", "_leggings", "_helmet", "_trap", "_kit",
    "_potion", "_snowball", "_pearl", "_grenade", "_shield", "_shear",
    "_launcher", "_hammer", "_scythe", "_fishing", "_slot", "_machine"
};

static ImU32 ResourceColor(const std::string& n)
{
    if (n.find("diamond") != std::string::npos) return IM_COL32(85, 200, 255, 255);
    if (n.find("emerald") != std::string::npos) return IM_COL32(0, 255, 100, 255);
    if (n.find("gold") != std::string::npos) return IM_COL32(240, 200, 80, 255);
    return IM_COL32(200, 200, 200, 255); // iron/wood/stone/fiber
}

static void ScanItemDrops()
{
    g_itemDrops.clear();
    auto& w = sdk::Get();
    if (!w.workspace) return;

    uintptr_t folder = 0;
    for (auto& c : Instance(w.workspace).GetChildren())
    {
        std::string cn = c.GetName();
        for (auto& ch : cn) ch = (char)tolower((unsigned char)ch);
        if (cn == "itemdrops") { folder = c.addr; break; }
    }
    if (!folder) return;

    for (auto& c : Instance(folder).GetChildren())
    {
        std::string cn = c.GetName();
        for (auto& ch : cn) ch = (char)tolower((unsigned char)ch);
        bool match = false;
        for (auto rn : kResourceNames)
            if (cn == rn) { match = true; break; }
        if (!match && cn.find("ore") != std::string::npos) match = true;
        if (!match) continue;
        for (auto ex : kResourceExcl)
            if (cn.find(ex) != std::string::npos) { match = false; break; }
        if (!match) continue;

        uintptr_t part = ResolveObjPart(c.addr);
        if (part)
        {
            ObjEspEntry e{ part, "", ResourceColor(cn) };
            for (auto& ch : cn) ch = (char)toupper((unsigned char)ch);
            snprintf(e.label, sizeof(e.label), "%s", cn.c_str());
            g_itemDrops.push_back(e);
        }
    }
}

static void DrawObjEntries(ImDrawList* dl, const std::vector<ObjEspEntry>& list)
{
    Vec3 lpos = CachedLocalPos();
    for (auto& e : list)
    {
        if (Instance::PartTransparency(e.part) > 0.9f) continue;
        Vec3 pos = Instance::PartPosition(e.part);
        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;
        float dx = pos.x - lpos.x, dy = pos.y - lpos.y, dz = pos.z - lpos.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > g_cfg.objEspDist || dist < 1.f) continue;
        Vec2 s;
        if (!sdk::WorldToScreen(pos, s) || s.x < 0.f) continue;

        ImU32 c = e.col;
        dl->AddQuad({ s.x, s.y - 6.f }, { s.x + 6.f, s.y }, { s.x, s.y + 6.f }, { s.x - 6.f, s.y }, c, 1.5f);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %.0fm", e.label, dist);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText({ s.x - ts.x / 2.f, s.y + 8.f }, c, buf);
    }
}

static void DrawObjEsp(ImDrawList* dl)
{
    if (!g_cfg.objEsp) return;
    DrawObjEntries(dl, g_objEsp);
    DrawObjEntries(dl, g_itemDrops);
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
        pb.kitAttr = cp.kitAttr;
        pb.hitPrims = cp.prims;
        // skeleton esp: world positions for each cached r15 part
        if (g_cfg.espSkeleton)
        {
            for (int i = 0; i < SK_COUNT; i++)
                if (cp.skel[i]) pb.skelPos[i] = Instance::PartPosition(cp.skel[i]);
            pb.skelOk = true;
        }
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
        pb.isNpc = true;
        pb.hitPrims = n.prims;
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

    // visible-only: raycast eye->chest per player, skip blocked ones.
    // the occluder cache is kept warm by UpdateOccluders() in the loop.
    Vec3 visEye{};
    bool visOn = g_cfg.espVisCheck;
    if (visOn)
    {
        Vec3 lp = CachedLocalPos();
        visEye = { lp.x, lp.y + 1.5f, lp.z };
    }

    for (auto& p : g_players)
    {
        if (visOn && !EspRayVisible(visEye,
            { p.rootWorld.x, p.rootWorld.y + 1.f, p.rootWorld.z }))
            continue;
        ImU32 c = p.teammate ? colTeam : colEnemy;
        float cx = (p.x1 + p.x2) / 2.f;

        if (g_cfg.espChams)
            dl->AddRectFilled({ p.x1, p.y1 }, { p.x2, p.y2 },
                p.teammate ? IM_COL32(60, 200, 100, 35) : IM_COL32(255, 70, 70, 35));

        if (g_cfg.espBox)
            dl->AddRect({ p.x1, p.y1 }, { p.x2, p.y2 }, c, 0.f, 0, 1.5f);

        // name + kit on one line, kit name beside the username - the
        // combined block stays centered over the box. kit labels only
        // render for attribute-sourced ids (PlayingAsKits etc.) - anything
        // else shows UNKNOWN rather than a guessed name.
        if ((g_cfg.espName && !p.name.empty()) || (g_cfg.espKit && !p.isNpc))
        {
            char kbuf[48];
            ImU32 kitDrawCol = p.kitCol;
            if (g_cfg.espKit && !p.isNpc)
            {
                if (p.kitAttr && !p.kit.empty())
                    snprintf(kbuf, sizeof(kbuf), " [%s]", p.kit.c_str());
                else
                {
                    snprintf(kbuf, sizeof(kbuf), " [UNKNOWN]");
                    kitDrawCol = IM_COL32(130, 130, 145, 255);
                }
            }
            else kbuf[0] = 0;

            bool hasName = g_cfg.espName && !p.name.empty();
            bool hasKit = kbuf[0] != 0;
            float nameW = hasName ? ImGui::CalcTextSize(p.name.c_str()).x : 0.f;
            float kitW = hasKit ? ImGui::CalcTextSize(kbuf).x : 0.f;
            float total = nameW + kitW;
            float tx = cx - total / 2.f;
            float ty = p.y1 - ImGui::GetTextLineHeight() - 2.f;

            if (hasName) { dl->AddText({ tx, ty }, c, p.name.c_str()); tx += nameW; }
            if (hasKit) dl->AddText({ tx, ty }, kitDrawCol, kbuf);
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

// ---------------- hitbox esp ----------------
// draws each body part's real bounds: Primitive rotation+position+size
// -> 8 corners -> view-projection -> screen rect. visual only - the
// hitbox expander is what actually changes game hitreg.

struct PrimSnapshot { Vec3 pos, size; float rot[9]; };

// rotation/position/size sit within ~0x120 bytes of each other on the
// primitive - one bulk read grabs the whole box
static bool ReadPrimBox(uintptr_t prim, PrimSnapshot& s)
{
    static const uintptr_t rO = OFF("Primitive", "Rotation");
    static const uintptr_t pO = OFF("Primitive", "Position");
    static const uintptr_t sO = OFF("Primitive", "Size");
    uintptr_t lo = rO, hi = sO + 16;
    if (pO < lo) lo = pO;
    if (hi - lo > 0x400) return false;

    unsigned char buf[0x400];
    if (!g_mem.Read(prim + lo, buf, hi - lo)) return false;
    memcpy(s.rot, buf + (rO - lo), sizeof(s.rot));
    memcpy(&s.pos,  buf + (pO - lo), sizeof(s.pos));
    memcpy(&s.size, buf + (sO - lo), sizeof(s.size));
    return true;
}

// projection context - same math as sdk::WorldToScreen but the view
// matrix + window rect are fetched once per frame instead of per point
struct ProjCtx { Matrix4 m; float rw, rh, ox, oy; };

static bool InitProj(ProjCtx& p)
{
    HWND hwnd = FindWindowW(nullptr, L"Roblox");
    auto& w = sdk::Get();
    if (!hwnd || !w.visualEngine) return false;
    RECT rc; POINT pt{};
    if (!GetClientRect(hwnd, &rc) || !ClientToScreen(hwnd, &pt)) return false;
    p.rw = (float)(rc.right - rc.left); p.rh = (float)(rc.bottom - rc.top);
    p.ox = (float)pt.x; p.oy = (float)pt.y;
    p.m = g_mem.Read<Matrix4>(w.visualEngine + OFF("VisualEngine", "ViewMatrix"));
    return true;
}

static bool ProjPt(const ProjCtx& p, const Vec3& w, Vec2& out)
{
    float x = w.x * p.m.m[0] + w.y * p.m.m[1] + w.z * p.m.m[2] + p.m.m[3];
    float y = w.x * p.m.m[4] + w.y * p.m.m[5] + w.z * p.m.m[6] + p.m.m[7];
    float z = w.x * p.m.m[8] + w.y * p.m.m[9] + w.z * p.m.m[10] + p.m.m[11];
    float wv = w.x * p.m.m[12] + w.y * p.m.m[13] + w.z * p.m.m[14] + p.m.m[15];
    if (wv < 0.1f) { out = { -1, -1 }; return false; }
    float inv = 1.f / wv;
    out.x = ((p.rw / 2) * (x * inv) + (p.rw / 2)) + p.ox;
    out.y = (-(p.rh / 2) * (y * inv) + (p.rh / 2)) + p.oy;
    return true;
}

static void DrawHitboxEsp(ImDrawList* dl)
{
    if (!g_cfg.espHitbox) return;
    ProjCtx pj;
    if (!InitProj(pj)) return;

    for (auto& p : g_players)
    {
        if (p.hitPrims.empty()) continue;
        if (p.dist > g_cfg.espHitboxDist) continue;
        if (!g_cfg.espShowTeammates && p.teammate) continue;
        if (p.maxHealth > 0.f && p.health <= 0.f) continue; // dead
        ImU32 c = p.teammate ? IM_COL32(80, 200, 120, 255) : HitboxEspColor();

        for (uintptr_t prim : p.hitPrims)
        {
            PrimSnapshot s;
            if (!ReadPrimBox(prim, s)) continue;
            if (s.size.x < 0.05f || s.size.y < 0.05f || s.size.z < 0.05f) continue;

            // rotation read as 9 floats; all-zero means identity
            bool ident = true;
            for (int i = 0; i < 9; i++)
                if (s.rot[i] != 0.f) { ident = false; break; }
            if (ident) { memset(s.rot, 0, sizeof(s.rot)); s.rot[0] = s.rot[4] = s.rot[8] = 1.f; }

            float hx = s.size.x / 2.f, hy = s.size.y / 2.f, hz = s.size.z / 2.f;
            float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
            int n = 0;
            for (int i = 0; i < 8; i++)
            {
                float lx = (i & 1) ? hx : -hx;
                float ly = (i & 2) ? hy : -hy;
                float lz = (i & 4) ? hz : -hz;
                Vec3 w{
                    s.pos.x + s.rot[0] * lx + s.rot[1] * ly + s.rot[2] * lz,
                    s.pos.y + s.rot[3] * lx + s.rot[4] * ly + s.rot[5] * lz,
                    s.pos.z + s.rot[6] * lx + s.rot[7] * ly + s.rot[8] * lz };
                Vec2 sc;
                if (!ProjPt(pj, w, sc)) continue;
                if (sc.x < mnx) mnx = sc.x; if (sc.x > mxx) mxx = sc.x;
                if (sc.y < mny) mny = sc.y; if (sc.y > mxy) mxy = sc.y;
                n++;
            }
            if (n >= 4 && mxx - mnx > 1.f && mxy - mny > 1.f)
                dl->AddRect({ mnx, mny }, { mxx, mxy }, c, 0.f, 0, 1.f);
        }
    }
}

// ---------------- skeleton esp ----------------
// connects cached r15 limb positions with lines - head/torso/hrp come
// from PlayerBox, limbs from skelPos (world space, projected once)

static void DrawSkeletonEsp(ImDrawList* dl)
{
    if (!g_cfg.espSkeleton || g_players.empty()) return;
    ProjCtx pj;
    if (!InitProj(pj)) return;

    // joint pairs: a -> b (SK_* indices into skelPos, -1 = head/rootWorld)
    static const int pairs[][2] = {
        { -1, SK_UPPERT }, { SK_UPPERT, SK_LOWERT }, { SK_LOWERT, -2 },
        { SK_UPPERT, SK_LUA }, { SK_LUA, SK_LLA }, { SK_LLA, SK_LH },
        { SK_UPPERT, SK_RUA }, { SK_RUA, SK_RLA }, { SK_RLA, SK_RH },
        { SK_LOWERT, SK_LUL }, { SK_LUL, SK_LLL }, { SK_LLL, SK_LF },
        { SK_LOWERT, SK_RUL }, { SK_RUL, SK_RLL }, { SK_RLL, SK_RF },
    };

    for (auto& p : g_players)
    {
        if (p.teammate && !g_cfg.espShowTeammates) continue;
        if (p.dist > 400.f) continue;
        ImU32 c = p.teammate ? IM_COL32(80, 200, 120, 200) : IM_COL32(255, 120, 120, 200);

        // project all joints once
        Vec2 sp[SK_COUNT]; bool ok[SK_COUNT] = {};
        for (int i = 0; i < SK_COUNT; i++)
            if (p.skelPos[i].x != 0.f || p.skelPos[i].y != 0.f || p.skelPos[i].z != 0.f)
                ok[i] = ProjPt(pj, p.skelPos[i], sp[i]);
        Vec2 sh, sr;
        bool hok = ProjPt(pj, p.headWorld, sh);
        bool rok = ProjPt(pj, p.rootWorld, sr);

        for (auto& pr : pairs)
        {
            int a = pr[0], b = pr[1];
            Vec2 pa, pb;
            bool aok, bok;
            if (a == -1)      { pa = sh; aok = hok; }
            else if (a == -2) { pa = sr; aok = rok; }
            else              { pa = sp[a]; aok = ok[a]; }
            if (b == -1)      { pb = sh; bok = hok; }
            else if (b == -2) { pb = sr; bok = rok; }
            else              { pb = sp[b]; bok = ok[b]; }
            if (aok && bok)
                dl->AddLine({ pa.x, pa.y }, { pb.x, pb.y }, c, 1.2f);
        }
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
    SendInput(2, in, sizeof(in[0]));
}

// subpixel accumulation shared by aimbot - fractional pixels
// carry over between frames instead of being truncated away every frame
// (the truncation was leaving a permanent aim deadzone)
static float g_aimAccX = 0.f, g_aimAccY = 0.f;
static void AimMove(float dxPx, float dyPx)
{
    g_aimAccX += dxPx; g_aimAccY += dyPx;
    int mx = (int)g_aimAccX, my = (int)g_aimAccY;
    g_aimAccX -= mx; g_aimAccY -= my;
    if (mx || my) mouse_move(mx, my);
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

// custom/private servers run on place ids we can't enumerate - detect
// bedwars by content instead: kit attributes on the local player plus
// bedwars map objects in workspace. throttled, only runs when the
// place id didn't match.
static DWORD g_contentCheckAt = 0;
static bool g_contentBedwars = false;

static bool ContentLooksBedWars()
{
    auto& w = sdk::Get();
    if (w.localPlayer)
    {
        Instance lp(w.localPlayer);
        for (auto& [k, v] : lp.GetAttributes(false))
        {
            std::string lk = k;
            for (auto& c : lk) c = (char)tolower((unsigned char)c);
            // bedwars stores kit selection in replicated attributes
            // (PlayingAsKits etc.) - a very strong signal
            if (lk.find("kit") != std::string::npos) return true;
        }
    }
    if (w.workspace)
    {
        for (auto& c : Instance(w.workspace).GetChildren())
        {
            std::string cn = c.GetName();
            for (auto& ch : cn) ch = (char)tolower((unsigned char)ch);
            if (cn.find("bed") != std::string::npos ||
                cn.find("generator") != std::string::npos ||
                cn.find("itempedestal") != std::string::npos ||
                cn.find("buyzone") != std::string::npos ||
                cn.find("teambase") != std::string::npos)
                return true;
        }
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
            // unknown place id (custom server?) - fall back to content
            // detection before declaring it not-bedwars
            if (g_isBedWars == 0)
            {
                if (now >= g_contentCheckAt)
                {
                    g_contentCheckAt = now + 10000;
                    g_contentBedwars = ContentLooksBedWars();
                    if (g_contentBedwars)
                    {
                        Log("[nyx] placeId=%llu not in list but bedwars content detected",
                            (unsigned long long)pid);
                    }
                }
                if (g_contentBedwars) g_isBedWars = 1;
            }
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

// ---------------- wall check (occluder cache) ----------------
// workspace parts that can block a shot. we can't raycast inside the
// engine, so we cache nearby opaque geometry and slab-test rays against
// their AABBs ourselves. character subtrees are excluded so players
// never occlude each other.

struct Occluder { uintptr_t prim = 0; Vec3 pos, size; };
static std::vector<uintptr_t> g_occluderParts;
static std::unordered_map<uintptr_t, Occluder> g_occluders;
static DWORD g_occlScanAt = 0;

static void ScanOccluders()
{
    g_occluderParts.clear();
    g_occluders.clear();
    auto& w = sdk::Get();
    if (!w.workspace) return;

    // never treat characters as walls
    std::unordered_set<uintptr_t> chars;
    for (auto& [a, cp] : g_cache) if (cp.character) chars.insert(cp.character);
    for (auto& n : g_npcs) chars.insert(n.model);
    if (w.localPlayer)
    {
        uintptr_t c = g_mem.ReadPtr(w.localPlayer + OFF("Player", "ModelInstance"));
        if (c) chars.insert(c);
    }

    struct Frame { uintptr_t inst; int depth; };
    std::vector<Frame> stack{ { w.workspace, 0 } };
    int budget = 5000;
    while (!stack.empty() && budget-- > 0)
    {
        Frame f = stack.back(); stack.pop_back();
        if (chars.count(f.inst)) continue; // skip character subtrees
        Instance in(f.inst);
        if (IsPartClass(in.GetClassName()) && g_occluderParts.size() < 1500)
            g_occluderParts.push_back(f.inst);
        if (f.depth < 6)
            for (auto& c : in.GetChildren())
                stack.push_back({ c.addr, f.depth + 1 });
    }
}

// lazily resolve pos+size - spread over frames so the warm-up doesn't
// hitch the render thread
static void WarmOccluders(int maxN)
{
    static const uintptr_t pO = OFF("Primitive", "Position");
    static const uintptr_t sO = OFF("Primitive", "Size");
    int n = 0;
    for (uintptr_t part : g_occluderParts)
    {
        if (g_occluders.count(part)) continue;
        uintptr_t prim = g_mem.ReadPtr(part + OFF("BasePart", "Primitive"));
        Occluder o{}; o.prim = prim;
        if (prim)
        {
            uintptr_t lo = pO < sO ? pO : sO, hi = (pO > sO ? pO : sO) + 16;
            if (hi - lo <= 0x400)
            {
                unsigned char buf[0x400];
                if (g_mem.Read(prim + lo, buf, hi - lo))
                {
                    memcpy(&o.pos,  buf + (pO - lo), 12);
                    memcpy(&o.size, buf + (sO - lo), 12);
                }
            }
        }
        g_occluders[part] = o;
        if (++n >= maxN) break;
    }
}

// slab test: does segment eye->tgt pass through any cached part's AABB?
// cheap reject first: the ray's own bounding box must overlap the part's
// bounds or the ray can't possibly hit it - skips ~90% of tests
static bool RayBlocked(const Vec3& eye, const Vec3& tgt)
{
    Vec3 d{ tgt.x - eye.x, tgt.y - eye.y, tgt.z - eye.z };
    float smn[3] = { eye.x < tgt.x ? eye.x : tgt.x,
                     eye.y < tgt.y ? eye.y : tgt.y,
                     eye.z < tgt.z ? eye.z : tgt.z };
    float smx[3] = { eye.x > tgt.x ? eye.x : tgt.x,
                     eye.y > tgt.y ? eye.y : tgt.y,
                     eye.z > tgt.z ? eye.z : tgt.z };
    for (auto& [addr, o] : g_occluders)
    {
        if (!o.prim) continue;
        // AABB overlap test between ray bounds and part bounds
        float op3[3] = { o.pos.x, o.pos.y, o.pos.z };
        float os3[3] = { o.size.x, o.size.y, o.size.z };
        bool overlap = true;
        for (int i = 0; i < 3; i++)
        {
            float h = os3[i] * 0.5f + 0.5f; // small margin
            if (op3[i] + h < smn[i] || op3[i] - h > smx[i]) { overlap = false; break; }
        }
        if (!overlap) continue;
        float mn[3] = { o.pos.x - o.size.x / 2, o.pos.y - o.size.y / 2, o.pos.z - o.size.z / 2 };
        float mx[3] = { o.pos.x + o.size.x / 2, o.pos.y + o.size.y / 2, o.pos.z + o.size.z / 2 };
        float o3[3] = { eye.x, eye.y, eye.z }, d3[3] = { d.x, d.y, d.z };
        float tmin = 0.f, tmax = 1.f;
        bool hit = true;
        for (int i = 0; i < 3 && hit; i++)
        {
            if (fabsf(d3[i]) < 1e-6f)
            {
                if (o3[i] < mn[i] || o3[i] > mx[i]) hit = false;
            }
            else
            {
                float inv = 1.f / d3[i];
                float t1 = (mn[i] - o3[i]) * inv, t2 = (mx[i] - o3[i]) * inv;
                if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) hit = false;
            }
        }
        if (hit && tmin <= tmax && tmin < 1.f && tmax > 0.f) return true;
    }
    return false;
}

// esp visible-only helper - empty cache means "can't tell", treat as visible
static bool EspRayVisible(const Vec3& eye, const Vec3& tgt)
{
    return g_occluders.empty() || !RayBlocked(eye, tgt);
}

// keeps the occluder cache fresh - rescan every ~8s, warm gradually
static void UpdateOccluders()
{
    DWORD now = GetTickCount();
    if (now >= g_occlScanAt) { g_occlScanAt = now + 8000; ScanOccluders(); }
    WarmOccluders(120);
}

// raycast debug state - last aim target + whether the ray to it is clear
static bool g_aimRayValid = false;
static Vec2 g_aimRayScreen;
static Vec3 g_aimRayWorld;
static bool g_aimRayBlocked = false;

// ---------------- aimbot / hitbox / hitfix / reach ----------------


static void RunAimbot()
{
    g_aimbotActive = false;
    if (!g_cfg.aimbotEnabled || !g_mem.IsAttached()) return;
    if (!IsBedWars()) return;
    static bool toggled = false, wasDown = false;
    if (!ActKey(g_cfg.aimbotKey, g_cfg.aimbotMode, toggled, wasDown)) return;
    g_aimbotActive = true;
    if (g_cfg.aimbotKnockCheck && g_localKnocked) return;
    if (g_players.empty()) return;

    float cx = g_localX, cy = g_localY;
    const PlayerBox* best = nullptr;
    float bestMetric = 1e9f;
    float bestD = 0.f;
    float tx = 0, ty = 0;

    // wall/raycast checks share the occluder cache - warm it gradually,
    // rescan every ~8s
    Vec3 eye{};
    if (g_cfg.aimbotWallCheck || g_cfg.aimbotRaycast)
    {
        UpdateOccluders();
        Vec3 lp = CachedLocalPos();
        eye = { lp.x, lp.y + 1.5f, lp.z };
    }

    for (auto& p : g_players)
    {
        if (p.teammate && g_cfg.aimbotTeamCheck) continue;
        if (g_cfg.aimbotHealthCheck && p.health <= 0.f) continue;
        if (g_cfg.aimbotRangeCheck && p.dist > g_cfg.aimbotRange) continue;
        // wall check: ray from eye to the target's chest must be clear
        if (g_cfg.aimbotWallCheck && !g_occluders.empty() &&
            RayBlocked(eye, { p.rootWorld.x, p.rootWorld.y + 1.f, p.rootWorld.z }))
            continue;

        // prediction: lead the target by velocity * predMs
        Vec3 aimWorld = p.headWorld;
        if (g_cfg.aimbotPrediction)
        {
            aimWorld.x += p.headVel.x * (g_cfg.aimbotPredMs / 1000.f);
            aimWorld.y += p.headVel.y * (g_cfg.aimbotPredMs / 1000.f);
            aimWorld.z += p.headVel.z * (g_cfg.aimbotPredMs / 1000.f);
        }
        Vec2 hs;
        if (!sdk::WorldToScreen(aimWorld, hs) || hs.x == -1) continue;
        float px = hs.x, py = hs.y;

        float dx = px - cx, dy = py - cy;
        float d = sqrtf(dx * dx + dy * dy);
        if (g_cfg.aimbotUseFov && d >= g_cfg.aimbotFov) continue;
        // target priority: none / crosshair px / nearest studs / lowest hp
        float metric = (g_cfg.aimbotSort == 1) ? p.dist :
                       (g_cfg.aimbotSort == 2) ? p.health :
                       (g_cfg.aimbotSort == 3) ? 0.f : d;
        if (metric < bestMetric) { bestMetric = metric; bestD = d; best = &p; tx = px; ty = py; }
    }
    if (!best) { g_aimbotHadTarget = false; g_aimRayValid = false; return; }

    // throttled diagnostic so we can verify it acquires + moves
    static DWORD lastAimLog = 0;
    if (GetTickCount() - lastAimLog > 2000)
    {
        lastAimLog = GetTickCount();
        Log("[nyx] aimbot target='%s' dist=%.0fpx players=%zu", best->name.c_str(), bestD, g_players.size());
    }
    g_aimbotHadTarget = true;

    // raycast debug: record the target + occlusion result for the overlay
    if (g_cfg.aimbotRaycast)
    {
        g_aimRayValid = true;
        g_aimRayScreen = { tx, ty };
        g_aimRayWorld = { best->rootWorld.x, best->rootWorld.y + 1.f, best->rootWorld.z };
        g_aimRayBlocked = !g_occluders.empty() && RayBlocked(eye, g_aimRayWorld);
    }

    float sx = g_cfg.aimbotSmoothing ? g_cfg.aimbotSmoothX : 1.f;
    float sy = g_cfg.aimbotSmoothing ? g_cfg.aimbotSmoothY : 1.f;
    float mx = (tx - cx) * sx;
    float my = (ty - cy) * sy;
    // shake jitters the actual mouse movement, not the aim point - the old
    // version jittered the target coords so smoothing ate most of it
    if (g_cfg.aimbotShake)
    {
        mx += ((rand() % 200) - 100) / 100.f * g_cfg.aimbotShakeX;
        my += ((rand() % 200) - 100) / 100.f * g_cfg.aimbotShakeY;
    }
    // humanized aim: irregular rhythm (~12% frame skip), subtle curved
    // path (perpendicular arc, direction fixed per target name), and
    // micro jitter on top of smoothing - looks less robotic than a
    // constant-velocity pull
    if (g_cfg.aimbotHuman)
    {
        if (rand() % 100 < 12) return;
        float ddx = tx - cx, ddy = ty - cy;
        float dd = sqrtf(ddx * ddx + ddy * ddy);
        if (dd > 1.f)
        {
            float nx = -ddy / dd, ny = ddx / dd;
            float arc = dd * 0.10f; if (arc > 14.f) arc = 14.f;
            unsigned h = 0;
            for (char ch : best->name) h = h * 31u + (unsigned char)ch;
            float sgn = (h & 1u) ? 1.f : -1.f;
            mx += nx * arc * sgn * 0.12f;
            my += ny * arc * sgn * 0.12f;
        }
        mx += ((rand() % 120) - 60) / 100.f;
        my += ((rand() % 120) - 60) / 100.f;
    }
    mx *= g_cfg.aimbotSpeed;
    my *= g_cfg.aimbotSpeed;
    AimMove(mx, my);
}

// ---------------- hitbox expander (rate-limited) ----------------
// writes Primitive.Size on enemy HRPs so hits register easier. v44 rework:
// writes are throttled to ~12hz per part instead of every frame - the
// per-frame rewrite storm is what caused the physics stutter/floating.
// HRP only (invisible), teammates skipped, originals restored on toggle-off.

struct HitboxEntry { uintptr_t prim; Vec3 orig; Vec3 expected; DWORD lastWrite; };
static std::unordered_map<uintptr_t, HitboxEntry> g_hitboxMap;
static std::vector<uintptr_t> g_hitboxParts;
static DWORD g_hitboxScanAt = 0;
static bool g_hitboxActive = false;

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
        if (e.prim) g_mem.Write<Vec3>(e.prim + OFF("Primitive", "Size"), e.orig);
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

    if (now >= g_hitboxScanAt)
    {
        g_hitboxScanAt = now + 500;
        g_hitboxParts.clear();
        std::unordered_set<uintptr_t> seen;
        for (auto& [paddr, cp] : g_cache)
        {
            if (cp.character == 0) continue;
            if (localTeam && cp.team == localTeam) continue;
            if (cp.hrp) { g_hitboxParts.push_back(cp.hrp); seen.insert(cp.hrp); }
        }
        for (auto& n : g_npcs)
            if (n.hrp) { g_hitboxParts.push_back(n.hrp); seen.insert(n.hrp); }
        for (auto it = g_hitboxMap.begin(); it != g_hitboxMap.end();)
            it = seen.count(it->first) ? std::next(it) : g_hitboxMap.erase(it);
    }

    // throttled writes: check drift at ~12hz per part, never every frame.
    // roblox resets primitive size on its own schedule; 80ms is fast
    // enough that the hitbox stays expanded without a write storm.
    for (uintptr_t part : g_hitboxParts)
    {
        auto it = g_hitboxMap.find(part);
        if (it == g_hitboxMap.end())
        {
            uintptr_t prim = g_mem.ReadPtr(part + OFF("BasePart", "Primitive"));
            if (!prim) continue;
            Vec3 orig = g_mem.Read<Vec3>(prim + OFF("Primitive", "Size"));
            HitboxEntry e{ prim, orig, HitboxExpected(orig), 0 };
            g_hitboxMap[part] = e;
            it = g_hitboxMap.find(part);
        }
        HitboxEntry& e = it->second;
        if (now - e.lastWrite < 80) continue;
        Vec3 cur = g_mem.Read<Vec3>(e.prim + OFF("Primitive", "Size"));
        if (cur.x != e.expected.x || cur.y != e.expected.y || cur.z != e.expected.z)
        {
            g_mem.Write<Vec3>(e.prim + OFF("Primitive", "Size"), e.expected);
            e.lastWrite = now;
        }
    }
}

// ---------------- movement diagnostic + hit fix ----------------
// hitfix v44: plants HRP velocity during swing windows so hits register
// consistently. v43 removed all writes because the old version held the
// plant for the entire LMB-hold - that killed sprint. now the window is
// a short ~100ms tail after each click and it never touches Y velocity
// (jumps/gravity stay intact). velocity + spin are still read out for
// the diagnostics panel.

static uintptr_t g_hitfixHrp = 0, g_hitfixPrim = 0;
static DWORD g_hitfixRefresh = 0;
static Vec3 g_moveVel{}, g_moveAngVel{};

static bool LocalToolIsBow()
{
    if (!g_mem.IsAttached()) return false;
    static std::string tool;
    static DWORD toolAt = 0;
    DWORD now = GetTickCount();
    if (now >= toolAt)
    {
        toolAt = now + 500;
        tool.clear();
        auto& w = sdk::Get();
        if (w.localPlayer)
        {
            uintptr_t charAddr = g_mem.ReadPtr(w.localPlayer + OFF("Player", "ModelInstance"));
            if (charAddr)
                for (auto& c : Instance(charAddr).GetChildren())
                    if (c.GetClassName() == "Tool") { tool = ToLower(c.GetName()); break; }
        }
    }
    return tool.find("bow") != std::string::npos ||
           tool.find("crossbow") != std::string::npos ||
           tool.find("slingshot") != std::string::npos ||
           tool.find("musket") != std::string::npos;
}

static void RunHitfix()
{
    bool want = g_cfg.hitfixEnabled && g_mem.IsAttached() && GameFocused();
    if (!want)
    {
        g_hitfixHrp = 0; g_hitfixPrim = 0; g_hitfixRefresh = 0;
        g_moveVel = {}; g_moveAngVel = {};
        return;
    }
    if (LocalToolIsBow()) return; // never plant while drawing a bow

    DWORD now = GetTickCount();

    auto& w = sdk::Get();
    if (!w.localPlayer) { g_moveVel = {}; g_moveAngVel = {}; return; }

    if (now >= g_hitfixRefresh || !g_hitfixPrim)
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
    if (!g_hitfixPrim) { g_moveVel = {}; g_moveAngVel = {}; return; }

    // diagnostics always fresh - read-only, harmless
    g_moveVel = g_mem.Read<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyLinearVelocity"));
    g_moveAngVel = g_mem.Read<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyAngularVelocity"));

    // short plant window: ~100ms after each fresh click, nothing more.
    // the old version held this for the whole LMB press - that's what
    // made sprint feel broken.
    static DWORD swingUntil = 0;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        swingUntil = now + 100;
    if (now > swingUntil) return;

    // plant only inside the swing window: zero XZ drift + spin, keep Y
    // so jumps and gravity are untouched
    if (g_moveVel.x != 0.f || g_moveVel.z != 0.f)
        g_mem.Write<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyLinearVelocity"), { 0.f, g_moveVel.y, 0.f });
    if (g_moveAngVel.x != 0.f || g_moveAngVel.y != 0.f || g_moveAngVel.z != 0.f)
        g_mem.Write<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyAngularVelocity"), { 0.f, 0.f, 0.f });
}

static uintptr_t LocalHrpPrimitive()
{
    if (!g_localHrpAddr) return 0;
    return g_mem.ReadPtr(g_localHrpAddr + OFF("BasePart", "Primitive"));
}

// ---------------- movement suite ----------------
// humanoid writes: walkspeed + jumppower (originals restored on disable),
// experimental fly (velocity along look dir while key held) and spin
// (angular velocity while key held). all gated on focus + attach.

static uintptr_t g_moveHum = 0;
static DWORD g_moveRefresh = 0;
static float g_origSpeed = -1.f, g_origJump = -1.f;
static bool g_moveWasOn = false;

static uintptr_t LocalHumanoid()
{
    auto& w = sdk::Get();
    if (!w.localPlayer) return 0;
    DWORD now = GetTickCount();
    if (now >= g_moveRefresh || !g_moveHum)
    {
        g_moveRefresh = now + 1000;
        g_moveHum = 0;
        uintptr_t charAddr = g_mem.ReadPtr(w.localPlayer + OFF("Player", "ModelInstance"));
        if (charAddr)
            for (auto& c : Instance(charAddr).GetChildren())
                if (c.GetClassName() == "Humanoid") { g_moveHum = c.addr; break; }
    }
    return g_moveHum;
}

static void RunMovement()
{
    bool want = (g_cfg.moveSpeedOn || g_cfg.jumpPowerOn || g_cfg.flyOn || g_cfg.spinOn)
        && g_mem.IsAttached() && GameFocused();
    if (!want)
    {
        // restore originals once when everything is off
        if (g_moveWasOn && g_moveHum)
        {
            uintptr_t wsO = offsets::Get("Humanoid", "Walkspeed");
            if (!wsO) wsO = offsets::Get("Humanoid", "WalkSpeed");
            uintptr_t jpO = offsets::Get("Humanoid", "JumpPower");
            if (g_origSpeed >= 0.f && wsO) g_mem.Write<float>(g_moveHum + wsO, g_origSpeed);
            if (g_origJump >= 0.f && jpO) g_mem.Write<float>(g_moveHum + jpO, g_origJump);
        }
        g_moveWasOn = false;
        g_origSpeed = g_origJump = -1.f;
        return;
    }
    g_moveWasOn = true;
    uintptr_t hum = LocalHumanoid();
    if (!hum) return;

    uintptr_t wsO = offsets::Get("Humanoid", "Walkspeed");
    if (!wsO) wsO = offsets::Get("Humanoid", "WalkSpeed");
    uintptr_t jpO = offsets::Get("Humanoid", "JumpPower");

    if (g_cfg.moveSpeedOn && wsO)
    {
        if (g_origSpeed < 0.f) g_origSpeed = g_mem.Read<float>(hum + wsO);
        g_mem.Write<float>(hum + wsO, g_cfg.moveSpeed);
    }
    else if (g_origSpeed >= 0.f && wsO)
    {
        g_mem.Write<float>(hum + wsO, g_origSpeed);
        g_origSpeed = -1.f;
    }

    if (g_cfg.jumpPowerOn && jpO)
    {
        if (g_origJump < 0.f) g_origJump = g_mem.Read<float>(hum + jpO);
        g_mem.Write<float>(hum + jpO, g_cfg.jumpPower);
    }
    else if (g_origJump >= 0.f && jpO)
    {
        g_mem.Write<float>(hum + jpO, g_origJump);
        g_origJump = -1.f;
    }

    // fly: hold key -> velocity along HRP look vector * flySpeed.
    // experimental - physics fights it, no noclip.
    static bool flyWas = false;
    uintptr_t fprim = 0;
    float rot[9]{};
    bool flyHeld = g_cfg.flyOn && (key_down(g_cfg.flyKey) != 0);
    if (flyHeld)
    {
        fprim = LocalHrpPrimitive();
        if (fprim)
        {
            uintptr_t rO = OFF("Primitive", "Rotation");
            uintptr_t pO = OFF("Primitive", "Position");
            uintptr_t lo = rO < pO ? rO : pO, hi = (rO > pO ? rO : pO) + 36;
            if (hi - lo <= 0x400)
            {
                unsigned char buf[0x400];
                if (g_mem.Read(fprim + lo, buf, hi - lo))
                    memcpy(rot, buf + (rO - lo), 36);
                else flyHeld = false;
            }
            else flyHeld = false;
        }
        else flyHeld = false;
    }
    if (flyHeld && fprim)
    {
        // look vector = -(3rd column) of the row-major rotation
        float lx = -rot[2], ly = -rot[5], lz = -rot[8];
        float ll = sqrtf(lx*lx + ly*ly + lz*lz);
        if (ll > 0.01f)
        {
            lx = lx / ll * g_cfg.flySpeed; ly = ly / ll * g_cfg.flySpeed; lz = lz / ll * g_cfg.flySpeed;
            g_mem.Write<Vec3>(fprim + OFF("Primitive", "AssemblyLinearVelocity"), { lx, ly, lz });
        }
    }
    else if (flyWas && fprim)
        g_mem.Write<Vec3>(fprim + OFF("Primitive", "AssemblyLinearVelocity"), { 0.f, 0.f, 0.f });
    flyWas = flyHeld;

    // spin: hold key -> angular velocity around Y (deg/s -> rad/s)
    static bool spinWas = false;
    bool spinHeld = g_cfg.spinOn && (key_down(g_cfg.spinKey) != 0);
    uintptr_t sprim = spinHeld ? LocalHrpPrimitive() : 0;
    if (sprim)
        g_mem.Write<Vec3>(sprim + OFF("Primitive", "AssemblyAngularVelocity"),
            { 0.f, g_cfg.spinSpeed / 57.2958f, 0.f });
    else if (spinWas && fprim)
        g_mem.Write<Vec3>(fprim + OFF("Primitive", "AssemblyAngularVelocity"), { 0.f, 0.f, 0.f });
    spinWas = spinHeld;
}

// ---------------- trigger assist ----------------
// clicks when the crosshair is inside an enemy ESP box - gated by the
// same occlusion raycast when available. humanized-ish: min 120ms gap.

static void RunTrigger()
{
    if (!g_cfg.triggerAssist || !g_mem.IsAttached() || !GameFocused()) return;
    static DWORD lastClick = 0;
    DWORD now = GetTickCount();
    if (now - lastClick < 120) return;
    float cx = g_localX, cy = g_localY;
    for (auto& p : g_players)
    {
        if (p.teammate) continue;
        if (cx < p.x1 || cx > p.x2 || cy < p.y1 || cy > p.y2) continue;
        lastClick = now;
        mouse_click();
        break;
    }
}

// ---------------- anti-afk ----------------
// tiny alternating mouse move every ~70s resets the idle timer

static void RunAntiAfk()
{
    if (!g_cfg.antiAfk || !g_mem.IsAttached()) return;
    static DWORD lastNudge = 0;
    static int dir = 1;
    DWORD now = GetTickCount();
    if (now - lastNudge < 70000) return;
    lastNudge = now;
    dir = -dir;
    mouse_move(dir, 0);
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

    case 5: // nyx bedwars: charcoal panels, neon purple glow
        c[ImGuiCol_WindowBg] = { 0.05f, 0.05f, 0.07f, 0.98f };
        c[ImGuiCol_ChildBg] = { 0.08f, 0.08f, 0.11f, 1.f };
        c[ImGuiCol_PopupBg] = { 0.07f, 0.07f, 0.10f, 0.98f };
        c[ImGuiCol_Border] = { acc.x * 0.45f, acc.y * 0.45f, acc.z * 0.45f, 0.9f };
        c[ImGuiCol_FrameBg] = { 0.11f, 0.11f, 0.15f, 1.f };
        c[ImGuiCol_FrameBgHovered] = { acc.x * 0.30f, acc.y * 0.30f, acc.z * 0.30f, 1.f };
        c[ImGuiCol_FrameBgActive] = { acc.x * 0.42f, acc.y * 0.42f, acc.z * 0.42f, 1.f };
        c[ImGuiCol_Button] = { 0.12f, 0.12f, 0.17f, 1.f };
        c[ImGuiCol_ButtonHovered] = { acc.x * 0.55f, acc.y * 0.55f, acc.z * 0.55f, 1.f };
        c[ImGuiCol_ButtonActive] = acc;
        c[ImGuiCol_CheckMark] = acc;
        c[ImGuiCol_SliderGrab] = acc;
        c[ImGuiCol_SliderGrabActive] = { acc.x, acc.y, acc.z, 1.f };
        c[ImGuiCol_Separator] = { acc.x * 0.4f, acc.y * 0.4f, acc.z * 0.4f, 0.8f };
        c[ImGuiCol_Text] = { 0.93f, 0.92f, 0.97f, 1.f };
        c[ImGuiCol_TextDisabled] = { 0.50f, 0.48f, 0.56f, 1.f };
        c[ImGuiCol_Header] = { acc.x * 0.35f, acc.y * 0.35f, acc.z * 0.35f, 1.f };
        c[ImGuiCol_HeaderHovered] = acc;
        c[ImGuiCol_ScrollbarBg] = { 0.06f, 0.06f, 0.09f, 1.f };
        c[ImGuiCol_ScrollbarGrab] = { acc.x * 0.4f, acc.y * 0.4f, acc.z * 0.4f, 1.f };
        s.WindowRounding = 10.f;
        s.ChildRounding = 8.f;
        s.FrameRounding = 6.f;
        s.PopupRounding = 8.f;
        s.WindowBorderSize = 1.5f;
        s.ChildBorderSize = 1.f;
        s.FrameBorderSize = 0.f;
        s.WindowPadding = { 0, 0 };
        s.ItemSpacing = { 8, 7 };
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

// ---------------- nyx ui kit ----------------
// cards auto-size to their contents; tabs lay cards out in two columns

static ImVec4 AccColor() { return { g_cfg.accent[0], g_cfg.accent[1], g_cfg.accent[2], 1.f }; }

static void CardBegin(const char* id, const char* title)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.085f, 0.085f, 0.115f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild(id, ImVec2(0, 0),
        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
        ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleColor(ImGuiCol_Text, AccColor());
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

static void CardEnd()
{
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// two-column card area: left gets ~half, right gets the rest; both
// scroll independently if cards overflow
static void ColsBegin()
{
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##colL", ImVec2(avail * 0.5f - 5.f, 0), false);
}
static void ColsMid()
{
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##colR", ImVec2(0, 0), false);
}
static void ColsEnd() { ImGui::EndChild(); }

// hitbox esp enemy color picker value -> imgui color
static ImU32 HitboxEspColor()
{
    if (g_cfg.espHitboxColor == 1) return IM_COL32((int)(g_cfg.accent[0]*255),(int)(g_cfg.accent[1]*255),(int)(g_cfg.accent[2]*255),255);
    if (g_cfg.espHitboxColor == 2) return IM_COL32(255, 255, 255, 255);
    return IM_COL32(255, 80, 80, 255);
}

static void MenuAim()
{
    ImGui::Checkbox("Enabled", &g_cfg.aimbotEnabled);
    ImGui::Checkbox("Use FOV", &g_cfg.aimbotUseFov);
    if (g_cfg.aimbotUseFov)
        ImGui::SliderFloat("FOV size", &g_cfg.aimbotFov, 10.f, 360.f, "%.0f px");
    ImGui::Checkbox("Draw FOV", &g_cfg.aimbotDrawFov);
    if (g_cfg.aimbotDrawFov)
    {
        const char* fcols[] = { "White", "Black" };
        ImGui::Combo("FOV color", &g_cfg.aimbotFovColor, fcols, 2);
    }
    ImGui::Checkbox("Raycast debug", &g_cfg.aimbotRaycast);
    if (g_cfg.aimbotRaycast)
        ImGui::TextDisabled("Draws ray to target - green clear, red blocked");
    ImGui::Checkbox("Humanized aim", &g_cfg.aimbotHuman);
    if (g_cfg.aimbotHuman)
        ImGui::TextDisabled("Arc + jitter + irregular rhythm");
    ImGui::Checkbox("Trigger assist", &g_cfg.triggerAssist);
    if (g_cfg.triggerAssist)
        ImGui::TextDisabled("Clicks when crosshair is over an enemy");
    ImGui::Checkbox("Enable smoothing", &g_cfg.aimbotSmoothing);
    if (g_cfg.aimbotSmoothing)
    {
        ImGui::SliderFloat("Smoothing X", &g_cfg.aimbotSmoothX, 0.f, 1.f, "%.2f");
        ImGui::SliderFloat("Smoothing Y", &g_cfg.aimbotSmoothY, 0.f, 1.f, "%.2f");
    }
    ImGui::Checkbox("Enable shake", &g_cfg.aimbotShake);
    if (g_cfg.aimbotShake)
    {
        ImGui::SliderFloat("Shake X", &g_cfg.aimbotShakeX, 0.f, 20.f, "%.0f px");
        ImGui::SliderFloat("Shake Y", &g_cfg.aimbotShakeY, 0.f, 20.f, "%.0f px");
    }
    ImGui::SliderFloat("Aim speed", &g_cfg.aimbotSpeed, 0.1f, 3.f, "%.1fx");
    ImGui::Checkbox("Enable prediction", &g_cfg.aimbotPrediction);
    if (g_cfg.aimbotPrediction)
        ImGui::SliderFloat("Prediction (ms)", &g_cfg.aimbotPredMs, 0.f, 500.f, "%.0f");
    const char* sorts[] = { "Crosshair", "Nearest", "Lowest HP", "None" };
    ImGui::Combo("Priority", &g_cfg.aimbotSort, sorts, 4);
    ImGui::Checkbox("BedWars check", &g_cfg.bedwarsOnly);
    if (g_cfg.bedwarsOnly)
        ImGui::TextDisabled("Gates aim assist");
    ImGui::SliderFloat("Range (studs)", &g_cfg.aimbotRange, 5.f, 150.f, "%.0f");
    ImGui::Combo("Activation", &g_cfg.aimbotMode, kActModes, 3);
    RebindButton("Key", &g_cfg.aimbotKey);
    if (g_cfg.aimbotMode == 1)
        ImGui::TextDisabled("Press %s to toggle aim assist", VkName(g_cfg.aimbotKey));
    ImGui::Separator();
    ImGui::Text("Checks");
    ImGui::Checkbox("Team check", &g_cfg.aimbotTeamCheck);
    ImGui::Checkbox("Knock check", &g_cfg.aimbotKnockCheck);
    ImGui::Checkbox("Wall check", &g_cfg.aimbotWallCheck);
    ImGui::Checkbox("Health check", &g_cfg.aimbotHealthCheck);
    ImGui::Checkbox("Range check", &g_cfg.aimbotRangeCheck);
}

static void CardHitboxExpander()
{
    ImGui::Checkbox("Enable Hitboxes", &g_cfg.hitboxEnabled);
    ImGui::SliderFloat("Hitbox size", &g_cfg.hitboxMult, 1.f, 6.f, "%.1fx");
    ImGui::SliderFloat("Max studs", &g_cfg.hitboxMaxStuds, 0.f, 30.f, "%.1f");
    ImGui::Combo("Activation", &g_cfg.hitboxMode, kActModes, 3);
    RebindButton("Key", &g_cfg.hitboxKey);
    ImGui::TextDisabled("Expands enemy HRPs - rate-limited writes");
}

static void CardHitboxEsp()
{
    ImGui::Checkbox("Hitbox ESP", &g_cfg.espHitbox);
    if (g_cfg.espHitbox)
    {
        ImGui::SliderFloat("Distance", &g_cfg.espHitboxDist, 25.f, 500.f, "%.0f studs");
        const char* hcols[] = { "Red", "Accent", "White" };
        ImGui::Combo("Colour", &g_cfg.espHitboxColor, hcols, 3);
    }
    ImGui::TextDisabled("Draws real body-part bounds - visual only");
}

static void CardHitFix()
{
    ImGui::Checkbox("Hit Fix", &g_cfg.hitfixEnabled);
    ImGui::TextDisabled("Plants you ~100ms per click - sprint safe");
    if (g_cfg.hitfixEnabled)
    {
        ImGui::Text("Vel:  %.1f, %.1f, %.1f", g_moveVel.x, g_moveVel.y, g_moveVel.z);
        ImGui::Text("Spin: %.1f, %.1f, %.1f", g_moveAngVel.x, g_moveAngVel.y, g_moveAngVel.z);
    }
}

static void CardReach()
{
    ImGui::TextDisabled("Reach removed - it wrote HRP positions");
    ImGui::TextDisabled("and caused random teleports. Distance");
    ImGui::TextDisabled("ESP still shows enemy ranges.");
}

static void CardPlayerEsp()
{
    ImGui::Checkbox("Box ESP", &g_cfg.espBox);
    ImGui::Checkbox("Name ESP", &g_cfg.espName);
    ImGui::Checkbox("Distance ESP", &g_cfg.espDistance);
    ImGui::Checkbox("Health ESP", &g_cfg.espHealth);
    ImGui::Checkbox("Snap Lines", &g_cfg.espSnaplines);
    ImGui::Checkbox("Team Check", &g_cfg.espTeamCheck);
    ImGui::Checkbox("Show Teammates", &g_cfg.espShowTeammates);
    ImGui::Checkbox("Kit ESP", &g_cfg.espKit);
    ImGui::Checkbox("Visible only", &g_cfg.espVisCheck);
    if (g_cfg.espVisCheck)
        ImGui::TextDisabled("Hides ESP on players behind walls (raycast)");
    ImGui::Checkbox("Chams", &g_cfg.espChams);
    ImGui::Checkbox("Skeleton", &g_cfg.espSkeleton);
}

static void CardWorldEsp()
{
    ImGui::Checkbox("Object ESP", &g_cfg.objEsp);
    if (g_cfg.objEsp)
        ImGui::SliderFloat("Object dist", &g_cfg.objEspDist, 25.f, 1000.f, "%.0f studs");
    ImGui::TextDisabled("Marks hidden metal, orbs, bees, traps, drops and kit objects");
}

static void CardMovement()
{
    ImGui::Checkbox("Speed", &g_cfg.moveSpeedOn);
    if (g_cfg.moveSpeedOn)
        ImGui::SliderFloat("Walkspeed", &g_cfg.moveSpeed, 16.f, 100.f, "%.0f");
    ImGui::Checkbox("Jump", &g_cfg.jumpPowerOn);
    if (g_cfg.jumpPowerOn)
        ImGui::SliderFloat("Jump power", &g_cfg.jumpPower, 0.f, 300.f, "%.0f");
    ImGui::Checkbox("Anti-AFK", &g_cfg.antiAfk);
    ImGui::Separator();
    ImGui::TextDisabled("Experimental");
    ImGui::Checkbox("Fly##mv", &g_cfg.flyOn);
    if (g_cfg.flyOn)
    {
        RebindButton("Fly key", &g_cfg.flyKey);
        ImGui::SliderFloat("Fly speed", &g_cfg.flySpeed, 5.f, 200.f, "%.0f");
    }
    ImGui::Checkbox("Spin##mv", &g_cfg.spinOn);
    if (g_cfg.spinOn)
    {
        RebindButton("Spin key", &g_cfg.spinKey);
        ImGui::SliderFloat("Spin deg/s", &g_cfg.spinSpeed, 0.f, 2000.f, "%.0f");
    }
}

static void CardWorld()
{
    ImGui::Checkbox("Streamproof", &g_cfg.streamproof);
    if (g_cfg.streamproof) g_overlay.ApplyStreamproof();
    ImGui::Separator();
    ImGui::Checkbox("Anti-void", &g_cfg.antiVoid);
    if (g_cfg.antiVoid)
        ImGui::SliderFloat("Fall distance", &g_cfg.antiVoidDist, 10.f, 120.f, "%.0f studs");
    ImGui::TextDisabled("Teleports you back up if you fall too far");
    ImGui::Separator();
    ImGui::TextDisabled("Coming soon");
    ImGui::BeginDisabled();
    static bool dummyToggles[4] = {};
    ImGui::Checkbox("Anti-fall", &dummyToggles[0]);
    ImGui::Checkbox("No-slow", &dummyToggles[1]);
    ImGui::Checkbox("Fast break", &dummyToggles[2]);
    ImGui::Checkbox("Auto bridge", &dummyToggles[3]);
    ImGui::EndDisabled();
}

static void CardStatus()
{
    ImGui::Text("Roblox PID: %lu", g_mem.pid);
    ImGui::Text("PlaceId: %llu", (unsigned long long)g_lastPlaceId);
    ImGui::Text("BedWars: %s", g_isBedWars == 1 ? "yes" : (g_isBedWars == 0 ? "NO (combat off)" : "unknown"));
    ImGui::Text("Offsets: %s", offsets::Ready() ? offsets::ClientVersion().c_str() : "fetching...");
    // kit attribute diagnostic - shows the raw PlayingAsKits value so we
    // can tell a broken attribute read from a kit-name mismatch
    {
        static DWORD attrDiagAt = 0;
        static std::string attrDiag;
        if (GetTickCount() >= attrDiagAt && sdk::Get().localPlayer)
        {
            attrDiagAt = GetTickCount() + 1000;
            Instance lp(sdk::Get().localPlayer);
            auto attrs = lp.GetAttributes(false);
            attrDiag.clear();
            for (auto& [k, v] : attrs)
            {
                std::string lk = ToLower(k);
                if (lk.find("kit") != std::string::npos)
                { attrDiag = k + " = " + v; break; }
            }
            char tmp[32]; snprintf(tmp, sizeof(tmp), " (%zu attrs)", attrs.size());
            attrDiag = attrDiag.empty()
                ? std::string("none") + tmp
                : attrDiag + tmp;
        }
        ImGui::Text("Kit attr: %s", attrDiag.c_str());
    }
}

static void CardDanger()
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.f));
    if (ImGui::Button("DESTROY / UNLOAD", { -1, 30 }))
        g_running = false;
    ImGui::PopStyleColor(2);
}

// ---------------- tabs ----------------

static void MenuCombat()
{
    ColsBegin();
    CardBegin("##c_aim", "Aim Assist"); MenuAim(); CardEnd();
    CardBegin("##c_hitbox", "Hitboxes"); CardHitboxExpander(); CardEnd();
    ColsMid();
    CardBegin("##c_hbesp", "Hitbox ESP"); CardHitboxEsp(); CardEnd();
    CardBegin("##c_hitfix", "Hit Fix"); CardHitFix(); CardEnd();
    CardBegin("##c_reach", "Reach"); CardReach(); CardEnd();
    ColsEnd();
}

static void MenuVisualsTab()
{
    ColsBegin();
    CardBegin("##v_esp", "Player ESP"); CardPlayerEsp(); CardEnd();
    ColsMid();
    CardBegin("##v_world", "World ESP"); CardWorldEsp(); CardEnd();
    ColsEnd();
}

static void MenuMiscTab()
{
    ColsBegin();
    CardBegin("##m_world", "World"); CardWorld(); CardEnd();
    CardBegin("##m_move", "Movement"); CardMovement(); CardEnd();
    CardBegin("##m_status", "Status"); CardStatus(); CardEnd();
    ColsMid();
    CardBegin("##m_keys", "Keybinds"); RebindButton("Menu", &g_cfg.menuKey); ImGui::TextDisabled("INSERT still toggles this menu"); CardEnd();
    CardBegin("##m_danger", "Danger Zone"); CardDanger(); CardEnd();
    ColsEnd();
}

static void MenuSettings()
{
    ColsBegin();
    CardBegin("##s_ui", "Interface");
    ImGui::SliderFloat("GUI scale", &g_cfg.uiScale, 0.6f, 1.6f, "%.2fx");
    const char* themes[] = { "Midnight", "Incognito", "Neon", "Light", "Vape", "NYX" };
    if (ImGui::Combo("Theme", &g_cfg.uiTheme, themes, 6)) config::Save();
    ImGui::ColorEdit3("Accent", g_cfg.accent, ImGuiColorEditFlags_NoInputs);
    CardEnd();
    CardBegin("##s_reset", "Reset");
    ImGui::TextDisabled("Restore every setting to defaults");
    if (ImGui::Button("Reset settings", { -1, 30 }))
    {
        g_cfg = Settings{};
        config::Save();
    }
    CardEnd();
    ColsMid();
    CardBegin("##s_cfg", "Configs");
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
    CardEnd();
    ColsEnd();
}

static void MenuAbout()
{
    ImGui::PushStyleColor(ImGuiCol_Text, AccColor());
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("NYX");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("BEDWARS EDITION");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Version: v%s", NYX_VERSION);
    ImGui::Text("Developer: nyx");
    ImGui::Text("Built with Dear ImGui + DX11");
    ImGui::Spacing();
    ImGui::TextDisabled("External overlay - reads memory, draws on top.");
    ImGui::TextDisabled("Not affiliated with Roblox Corporation.");
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
        // save once the values settle - dragging the color picker used to
        // write the config file every single frame
        static DWORD lastThemeSave = 0;
        DWORD now = GetTickCount();
        if (now - lastThemeSave > 800) { lastThemeSave = now; config::Save(); }
    }

    // gui scale applies to the whole imgui context
    ImGui::GetIO().FontGlobalScale = g_cfg.uiScale;

    ImGui::SetNextWindowSize({ 780, 560 }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({ 680, 460 }, { 1600, 1100 });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (!ImGui::Begin("##nyx", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End(); ImGui::PopStyleVar(); return;
    }

    // ---- custom title bar: drag handle + minimize/close ----
    static bool minimized = false;
    {
        ImVec2 wp = ImGui::GetWindowPos();
        float ww = ImGui::GetWindowWidth();
        ImGui::SetCursorScreenPos(wp);
        ImGui::InvisibleButton("##titledrag", { ww - 70.f, 46.f });
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos({ wp.x + d.x, wp.y + d.y });
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 ts = ImGui::CalcTextSize("NYX");
        dl->AddText({ wp.x + 16, wp.y + 8 },
            IM_COL32((int)(g_cfg.accent[0] * 255), (int)(g_cfg.accent[1] * 255), (int)(g_cfg.accent[2] * 255), 255), "NYX");
        ImVec2 bs = ImGui::CalcTextSize("BEDWARS");
        dl->AddText({ wp.x + 16 + ts.x + 8, wp.y + 14 },
            IM_COL32(140, 140, 155, 255), "BEDWARS");
        (void)bs;

        ImGui::SetCursorScreenPos({ wp.x + ww - 64, wp.y + 10 });
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
        if (ImGui::Button(minimized ? ">" : "_", { 26, 24 })) minimized = !minimized;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 0.8f));
        if (ImGui::Button("X", { 26, 24 })) g_cfg.menuVisible = false;
        ImGui::PopStyleColor(3);
        ImGui::SetCursorScreenPos({ wp.x, wp.y + 46 });
        dl->AddLine({ wp.x, wp.y + 46 }, { wp.x + ww, wp.y + 46 },
            IM_COL32((int)(g_cfg.accent[0] * 120), (int)(g_cfg.accent[1] * 120), (int)(g_cfg.accent[2] * 120), 255));
    }

    if (!minimized)
    {
        // ---- sidebar ----
        ImGui::BeginChild("##side", { 150, 0 }, false);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
        static int tab = 0;
        const char* tabs[] = { "Combat", "Visuals", "Misc", "Settings", "About" };
        for (int i = 0; i < 5; i++)
        {
            if (tab == i)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(
                    g_cfg.accent[0] * 0.30f, g_cfg.accent[1] * 0.30f, g_cfg.accent[2] * 0.30f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_Text, AccColor());
            }
            if (ImGui::Button(tabs[i], { -1, 36 })) tab = i;
            if (tab == i) { ImGui::PopStyleColor(2); }
            ImGui::Spacing();
        }
        // version at sidebar bottom
        float sy = ImGui::GetWindowHeight() - 34;
        ImGui::SetCursorPosY(sy);
        ImGui::TextDisabled("v%s", NYX_VERSION);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##content", { 0, 0 }, true);
        switch (tab)
        {
        case 0: MenuCombat(); break;
        case 1: MenuVisualsTab(); break;
        case 2: MenuMiscTab(); break;
        case 3: MenuSettings(); break;
        case 4: MenuAbout(); break;
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar();
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
    srand((unsigned)GetTickCount() ^ GetCurrentProcessId());
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
        bool justRebound = PollRebind();

        // menu toggle — only while Roblox (or this overlay) is focused.
        // skip the frame a rebind happened or pressing the new menu key
        // would instantly toggle the menu shut
        static bool prevMenuKey = false, prevIns = false;
        bool menuKey = focused && key_down(g_cfg.menuKey);
        bool insKey = focused && key_down(VK_INSERT);
        if (!justRebound && ((menuKey && !prevMenuKey) || (insKey && !prevIns)))
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
            // offsets fetch gets its own slower cadence - a dead host used
            // to block the render thread on every 1s attach retry
            static DWORD lastFetch = 0;
            if (!offsets::Ready() && GetTickCount() - lastFetch > 15000)
            {
                lastFetch = GetTickCount();
                bool ok = offsets::Fetch();
                Log("[nyx] offsets fetch: %s", ok ? "ok" : "FAILED");
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

            // object esp rescan - workspace walk is expensive so it runs
            // on a slow cadence, positions are read per-frame afterwards
            if (focused && g_cfg.objEsp)
            {
                static DWORD lastObjScan = 0, lastDropScan = 0;
                DWORD now = GetTickCount();
                if (now - lastObjScan > 4000)
                {
                    lastObjScan = now;
                    ScanObjectEsp();
                }
                // loot moves fast - rescan drops ~1.5x more often
                if (now - lastDropScan > 2500)
                {
                    lastDropScan = now;
                    ScanItemDrops();
                }
            }

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
            g_cache.clear();
            g_npcs.clear();
            g_objEsp.clear();
            g_itemDrops.clear();
            g_localHrpAddr = 0;
            g_hitboxActive = false;
            RestoreHitboxes();
        }

        if (focused && (g_cfg.aimbotWallCheck || g_cfg.aimbotRaycast || g_cfg.espVisCheck))
            UpdateOccluders();

        if (focused)
        {
            RunHitbox();
            RunAimbot();
            // input-injecting features pause while the menu has focus -
            // otherwise aim assist fights your cursor inside the GUI
            if (!g_cfg.menuVisible)
                RunHitfix(); // diagnostics + swing-window plant
            RunMovement();
            RunTrigger();
            RunAntiAfk();
            RunAntiVoid();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        if (g_mem.IsAttached() && focused) { DrawEsp(bg); DrawObjEsp(bg); DrawHitboxEsp(bg); DrawSkeletonEsp(bg); }
        // feature status line - shows what's actually armed right now so
        // there's no guessing whether a toggle/key actually engaged
        {
            char stat[160];
            snprintf(stat, sizeof(stat), "AIM:%s%s",
                g_aimbotActive ? "ON " : "off",
                g_hitboxActive ? " HIT:ON" : "");
            bool any = g_aimbotActive || g_hitboxActive;
            bg->AddText({ 12.f, 48.f },
                any ? IM_COL32(140, 230, 140, 220) : IM_COL32(140, 140, 140, 160), stat);
        }
        if (g_hitboxActive)
            bg->AddText({ 12.f, 12.f }, IM_COL32(120, 220, 255, 220), "HITBOX");
        if (g_cfg.aimbotEnabled && g_cfg.aimbotDrawFov && g_cfg.aimbotUseFov)
        {
            ImU32 fc = g_cfg.aimbotFovColor
                ? IM_COL32(0, 0, 0, 220) : IM_COL32(255, 255, 255, 220);
            bg->AddCircle({ g_localX, g_localY }, g_cfg.aimbotFov, fc, 64, 1.5f);
        }
        // raycast debug ray: screen center -> current aim target
        if (g_cfg.aimbotRaycast && g_aimRayValid)
        {
            ImU32 rc = g_aimRayBlocked ? IM_COL32(255, 70, 70, 230) : IM_COL32(80, 255, 130, 230);
            bg->AddLine({ g_localX, g_localY }, { g_aimRayScreen.x, g_aimRayScreen.y }, rc, 2.f);
            bg->AddCircle({ g_aimRayScreen.x, g_aimRayScreen.y }, 4.f, rc, 12, 1.5f);
        }
        if (g_cfg.menuVisible) DrawMenu();

        ImGui::Render();
        const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(0, 0);
        Sleep(1); // yield - keeps uncapped fps without pegging a core

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
