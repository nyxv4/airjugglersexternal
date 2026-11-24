// ============================================================================
// NYX FEATURE SOURCE - consolidated reference (v42)
// ============================================================================
// Everything for: HITBOXES (expander + ESP), AIM ASSIST, HIT FIX, KIT ESP.
//
// THIS FILE DOES NOT COMPILE STANDALONE. It is a single-file extraction of
// the feature code from src/render/overlay.cpp, in dependency order.
// To build it, the project needs:
//   - include/memory.h    (g_mem RPM/WPM, Memory class)
//   - include/sdk.h       (Instance, sdk::Get(), WorldToScreen, Vec2/Vec3)
//   - include/offsets.h   (OFF() macro, offsets::Ready())
//   - include/settings.h  (Settings g_cfg - included below for reference)
//   - ext/imgui/imgui.h   (menu card functions only)
//   - windows.h           (SendInput, GetAsyncKeyState, GetForegroundWindow)
//   - CollectPlayers()/g_players feed - see src/render/overlay.cpp
//   - main loop calls: RunAimbot/RunHitbox/RunHitfix/DrawHitboxEsp/DrawEsp
//     wired around overlay.cpp ~line 2600
// ============================================================================

// ============================ SETTINGS =====================================
// (verbatim from include/settings.h - the fields these features use)
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

// ============================ PLAYER DATA ==================================
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
static DWORD g_lastSwingAt = 0; // last manual or injected click - hitfix window

// ============================ KIT ESP ======================================
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
    DWORD refreshAt = 0;
    // primitive pointers for every body part - hitbox esp reads pos/size/
    // rotation straight off these, no per-frame child walks
    std::vector<uintptr_t> prims;
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
        // hitbox esp: cache primitive ptrs for every body part
        if (g_cfg.espHitbox && cp.prims.size() < 16 && IsPartClass(c.GetClassName()))
        {
            uintptr_t prim = g_mem.ReadPtr(c.addr + OFF("BasePart", "Primitive"));
            if (prim) cp.prims.push_back(prim);
        }
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
static ImU32 HitboxEspColor();

// ============================ ESP DRAW (kit label) =========================
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

        // name + kit on one line, kit name beside the username - the
        // combined block stays centered over the box
        if ((g_cfg.espName && !p.name.empty()) || (g_cfg.espKit && !p.kit.empty()))
        {
            char kbuf[48];
            if (g_cfg.espKit && !p.kit.empty())
                snprintf(kbuf, sizeof(kbuf), " [%s]", p.kit.c_str());
            else kbuf[0] = 0;

            bool hasName = g_cfg.espName && !p.name.empty();
            bool hasKit = kbuf[0] != 0;
            float nameW = hasName ? ImGui::CalcTextSize(p.name.c_str()).x : 0.f;
            float kitW = hasKit ? ImGui::CalcTextSize(kbuf).x : 0.f;
            float total = nameW + kitW;
            float tx = cx - total / 2.f;
            float ty = p.y1 - ImGui::GetTextLineHeight() - 2.f;

            if (hasName) { dl->AddText({ tx, ty }, c, p.name.c_str()); tx += nameW; }
            if (hasKit) dl->AddText({ tx, ty }, p.kitCol, kbuf);
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


// ============================ HITBOX ESP ===================================
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

// ============================ INPUT HELPERS ================================
// ---------------- input helpers ----------------

static void mouse_move(int dx, int dy)
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx; in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
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


// ============================ BEDWARS GATE =================================
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

// ============================ WALL CHECK (occluders) =======================
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
static bool RayBlocked(const Vec3& eye, const Vec3& tgt)
{
    Vec3 d{ tgt.x - eye.x, tgt.y - eye.y, tgt.z - eye.z };
    for (auto& [addr, o] : g_occluders)
    {
        if (!o.prim) continue;
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

// ============================ AIM ASSIST ===================================
// ---------------- aimbot / hitbox / hitfix / reach ----------------

static bool LocalToolIsBow();
static void TryReachLunge(const Vec3& target);

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

    // wall check: warm the occluder cache gradually, rescan every ~8s
    Vec3 eye{};
    if (g_cfg.aimbotWallCheck)
    {
        DWORD now = GetTickCount();
        if (now >= g_occlScanAt) { g_occlScanAt = now + 8000; ScanOccluders(); }
        WarmOccluders(120);
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
    if (!best) { g_aimbotHadTarget = false; return; }

    // throttled diagnostic so we can verify it acquires + moves
    static DWORD lastAimLog = 0;
    if (GetTickCount() - lastAimLog > 2000)
    {
        lastAimLog = GetTickCount();
        Log("[nyx] aimbot target='%s' dist=%.0fpx players=%zu", best->name.c_str(), bestD, g_players.size());
    }
    g_aimbotHadTarget = true;

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
    mx *= g_cfg.aimbotSpeed;
    my *= g_cfg.aimbotSpeed;
    AimMove(mx, my);
}

// ============================ HITBOX EXPANDER ==============================
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

// ---------------- equipped tool detection (silent bow aim) ----------------

static std::string g_localTool;
static DWORD g_toolRefresh = 0;

static bool LocalToolIsBow()
{
    // detects the equipped tool - callers decide whether to use it
    // what to do with it (silent aim, reach guard, hitfix guard)
    if (!g_mem.IsAttached()) { g_localTool.clear(); return false; }
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

// ============================ HIT FIX ======================================
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
    if (LocalToolIsBow()) return; // don't plant your feet while drawing a bow

    DWORD now = GetTickCount();
    // edge-triggered swing window: only plant for ~180ms after each
    // fresh click instead of the whole time LMB is held - zeroing
    // velocity continuously is what was killing sprint speed
    static DWORD swingUntil = 0;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
    {
        g_lastSwingAt = now;
        swingUntil = now + 180;
    }
    else if (now - g_lastSwingAt < 200)
        swingUntil = now + 60; // short tail after release
    if (now > swingUntil) return;

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

    // kill both linear drift and spin - a rotating HRP during the swing
    // window is one of the things that makes hits whiff at odd angles
    Vec3 vel = g_mem.Read<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyLinearVelocity"));
    if (vel.x != 0.f || vel.z != 0.f)
        g_mem.Write<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyLinearVelocity"), { 0.f, vel.y, 0.f });
    Vec3 ang = g_mem.Read<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyAngularVelocity"));
    if (ang.x != 0.f || ang.y != 0.f || ang.z != 0.f)
        g_mem.Write<Vec3>(g_hitfixPrim + OFF("Primitive", "AssemblyAngularVelocity"), { 0.f, 0.f, 0.f });
}

// ============================ KEY REBIND ===================================
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

// ============================ MENU (imgui cards) ===========================
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
    ImGui::TextDisabled("Invisible - expands enemy hitboxes, doesn't change visuals");
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
    ImGui::Checkbox("Enabled##hitfix", &g_cfg.hitfixEnabled);
    ImGui::TextDisabled("Zeroes velocity through the swing window - sprint safe");
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
}

// ============================================================================
// END OF CONSOLIDATED FEATURES
// Runtime wiring lives in src/render/overlay.cpp main loop (~line 2600):
//   RunAimbot(); RunHitbox(); RunHitfix(); DrawHitboxEsp(dl); DrawEsp(dl);
// ============================================================================
