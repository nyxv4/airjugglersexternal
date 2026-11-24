#include "sdk.h"
#include "offsets.h"
#include <cmath>
#include <set>

#define OFF(cls, field) offsets::Get(cls, field)

static bool printable(const std::string& s)
{
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) if (c < 32 || c > 126) return false;
    return true;
}

std::string Instance::GetName() const
{
    if (!valid()) return "";
    // verified layout: NameContainer holds a pointer to the name string object
    std::string s;
    uintptr_t nc = OFF("Instance", "NameContainer");
    if (nc)
    {
        uintptr_t nameObj = g_mem.ReadPtr(addr + nc);
        if (nameObj)
        {
            s = g_mem.ReadRobloxString(nameObj);
            if (printable(s)) return s;
        }
    }
    s = g_mem.ReadRobloxString(addr + OFF("Instance", "Name"));
    if (printable(s)) return s;
    uintptr_t namePtr = g_mem.ReadPtr(addr + OFF("Instance", "Name"));
    if (namePtr) { s = g_mem.ReadRobloxString(namePtr); if (printable(s)) return s; }
    return "";
}

std::string Instance::GetClassName() const
{
    if (!valid()) return "";
    uintptr_t classDesc = g_mem.ReadPtr(addr + OFF("Instance", "ClassDescriptor"));
    if (classDesc == 0) return "";
    std::string s = g_mem.ReadRobloxString(classDesc + OFF("Instance", "ClassName"));
    if (printable(s)) return s;
    uintptr_t namePtr = g_mem.ReadPtr(classDesc + OFF("Instance", "ClassName"));
    if (namePtr) { s = g_mem.ReadRobloxString(namePtr); if (printable(s)) return s; }
    return "";
}

std::vector<Instance> Instance::GetChildren() const
{
    std::vector<Instance> out;
    if (!valid()) return out;
    uintptr_t start = g_mem.ReadPtr(addr + OFF("Instance", "ChildrenStart"));
    if (start == 0) return out;
    uintptr_t end = g_mem.ReadPtr(start + OFF("Instance", "ChildrenEnd"));
    // cap the walk - a garbage 'end' from a bad read would otherwise spin
    // forever (roblox instances never have anywhere near 1024 children)
    for (uintptr_t entry = g_mem.ReadPtr(start), n = 0;
         entry && entry < end && n < 1024; entry += 0x10, n++)
    {
        uintptr_t inst = g_mem.ReadPtr(entry);
        if (inst > 0x10000) out.emplace_back(inst);
    }
    return out;
}

Instance Instance::FindFirstChild(const std::string& name) const
{
    for (auto& c : GetChildren())
        if (c.GetName() == name) return c;
    return Instance(0);
}

Instance Instance::FindFirstChildOfClass(const std::string& cls) const
{
    for (auto& c : GetChildren())
        if (c.GetClassName() == cls) return c;
    return Instance(0);
}

Instance Instance::GetCharacter() const
{
    if (!valid()) return Instance(0);
    return Instance(g_mem.ReadPtr(addr + OFF("Player", "ModelInstance")));
}

Instance Instance::GetLocalPlayer() const
{
    if (!valid()) return Instance(0);
    return Instance(g_mem.ReadPtr(addr + OFF("Player", "LocalPlayer")));
}

Instance Instance::GetHumanoid() const
{
    return GetCharacter().FindFirstChildOfClass("Humanoid");
}

float Instance::GetHealth() const { return g_mem.Read<float>(GetHumanoid().addr + OFF("Humanoid", "Health")); }
float Instance::GetMaxHealth() const { return g_mem.Read<float>(GetHumanoid().addr + OFF("Humanoid", "MaxHealth")); }
uintptr_t Instance::GetTeam() const { return g_mem.ReadPtr(addr + OFF("Player", "Team")); }

// ---- replicated attributes ----
// offsets from the 99offsetz dump for version-2c6fd5e09aaf4cfa (theo's
// dump doesn't include the attribute component map)
namespace AttrOff {
    constexpr uintptr_t ComponentMap       = 0x38;
    constexpr uint16_t  ComponentIdAttrs   = 9;
    constexpr size_t    EntrySize          = 0x10;
    constexpr uintptr_t IsMap              = 0x0;
    constexpr uintptr_t MapLength          = 0x8;
    constexpr uintptr_t MapAttributes      = 0x18;
    constexpr uintptr_t ArrAttributes      = 0x8;
    constexpr uintptr_t ArrLength          = 0x10;
    constexpr uintptr_t AttrSize           = 0x58;
    constexpr uintptr_t AttrKey            = 0x0;
    constexpr uintptr_t AttrValue          = 0x18;
    constexpr int       MaxAttrs           = 64;
}

static bool AttrPrintable(const std::string& s)
{
    if (s.empty() || s.size() > 200) return false;
    for (char c : s) if ((unsigned char)c < 32 || (unsigned char)c > 126) return false;
    return true;
}

// attribute string values are stored as a Roblox std::string; try both
// known layouts and keep whichever yields printable text
static std::string ReadAttrString(uintptr_t addr)
{
    std::string s = g_mem.ReadRobloxString(addr);
    if (AttrPrintable(s)) return s;

    // MSVC std::string: {buf[16]|ptr}@0, size@0x10, capacity@0x18
    uint64_t size = g_mem.Read<uint64_t>(addr + 0x10);
    uint64_t cap  = g_mem.Read<uint64_t>(addr + 0x18);
    if (size > 0 && size <= 200)
    {
        uintptr_t ptr = (cap > 15) ? g_mem.ReadPtr(addr) : addr;
        if (ptr > 0x10000)
        {
            std::string out; out.resize(size);
            SIZE_T got = 0;
            ReadProcessMemory(g_mem.hProcess, (LPCVOID)ptr, out.data(), size, &got);
            out.resize(got);
            auto nul = out.find('\0');
            if (nul != std::string::npos) out.resize(nul);
            if (AttrPrintable(out)) return out;
        }
    }
    return "";
}

void NyxAttrDebug(const char* fmt, ...);

std::vector<std::pair<std::string, std::string>> Instance::GetAttributes(bool debug) const
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!valid()) return out;

    uintptr_t compMap = g_mem.ReadPtr(addr + AttrOff::ComponentMap);
    if (debug) NyxAttrDebug("inst=%llx compmap=%llx", (unsigned long long)addr, (unsigned long long)compMap);
    if (!compMap) return out;
    uintptr_t start = g_mem.ReadPtr(compMap);
    uintptr_t end   = g_mem.ReadPtr(compMap + 8);
    if (debug) NyxAttrDebug("  vec start=%llx end=%llx", (unsigned long long)start, (unsigned long long)end);
    if (!start || end <= start || end - start > 0x10000) return out;

    uintptr_t attrs = 0;
    for (uintptr_t e = start; e < end; e += AttrOff::EntrySize)
    {
        uint16_t id = g_mem.Read<uint16_t>(e + 8);
        uintptr_t comp = g_mem.ReadPtr(e);
        if (debug) NyxAttrDebug("  entry=%llx comp=%llx id=%u", (unsigned long long)e, (unsigned long long)comp, (unsigned)id);
        if (id == AttrOff::ComponentIdAttrs)
        {
            attrs = comp;
            break;
        }
    }
    if (!attrs) { if (debug) NyxAttrDebug("  no attributes component"); return out; }

    bool isMap = g_mem.Read<uint8_t>(attrs + AttrOff::IsMap) != 0;
    uintptr_t entries; int len;
    if (isMap)
    {
        entries = g_mem.ReadPtr(attrs + AttrOff::MapAttributes);
        len     = (int)g_mem.Read<uint32_t>(attrs + AttrOff::MapLength);
    }
    else
    {
        entries = g_mem.ReadPtr(attrs + AttrOff::ArrAttributes);
        len     = (int)g_mem.Read<uint32_t>(attrs + AttrOff::ArrLength);
    }
    if (debug)
        NyxAttrDebug("attrcomp=%llx isMap=%d len=%d entries=%llx", (unsigned long long)attrs, (int)isMap, len, (unsigned long long)entries);
    if (!entries || len <= 0 || len > AttrOff::MaxAttrs) return out;

    for (int i = 0; i < len; i++)
    {
        uintptr_t e = entries + (uintptr_t)i * AttrOff::AttrSize;
        uintptr_t keyPtr = g_mem.ReadPtr(e + AttrOff::AttrKey);
        if (debug) NyxAttrDebug("  attr[%d] e=%llx keyptr=%llx", i, (unsigned long long)e, (unsigned long long)keyPtr);
        if (keyPtr < 0x10000) break;
        std::string key = g_mem.ReadString(keyPtr, 64);
        if (key.empty() || key.size() > 64) break;
        std::string val = ReadAttrString(e + AttrOff::AttrValue);
        if (debug) NyxAttrDebug("    key='%s' val='%s'", key.c_str(), val.c_str());
        out.emplace_back(key, val);
    }
    return out;
}

static uintptr_t PartPrimitive(uintptr_t part)
{
    return part ? g_mem.ReadPtr(part + OFF("BasePart", "Primitive")) : 0;
}

Vec3 Instance::PartPosition(uintptr_t part)
{
    uintptr_t prim = PartPrimitive(part);
    return prim ? g_mem.Read<Vec3>(prim + OFF("Primitive", "Position")) : Vec3{};
}

Vec3 Instance::PartSize(uintptr_t part)
{
    uintptr_t prim = PartPrimitive(part);
    return prim ? g_mem.Read<Vec3>(prim + OFF("Primitive", "Size")) : Vec3{};
}

float Instance::PartTransparency(uintptr_t part)
{
    // Transparency lives on the BasePart itself, not the Primitive
    return part ? g_mem.Read<float>(part + OFF("BasePart", "Transparency")) : 1.f;
}

// ---------------- sdk ----------------

namespace sdk
{
    static World w;

    bool Resolve()
    {
        if (!g_mem.IsAttached() || !offsets::Ready()) return false;

        uintptr_t ve = g_mem.ReadPtr(g_mem.base + OFF("VisualEngine", "Pointer"));
        if (ve == 0) return false;
        uintptr_t v1 = g_mem.ReadPtr(ve + OFF("VisualEngine", "FakeDataModel"));
        uintptr_t dm = v1 ? g_mem.ReadPtr(v1 + OFF("FakeDataModel", "RealDataModel")) : 0;
        if (dm == 0) return false;

        Instance dmI(dm);
        uintptr_t ws = dmI.FindFirstChildOfClass("Workspace").addr;
        uintptr_t pl = dmI.FindFirstChildOfClass("Players").addr;
        uintptr_t cam = ws ? Instance(ws).FindFirstChild("Camera").addr : 0;
        uintptr_t lp = pl ? Instance(pl).GetLocalPlayer().addr : 0;

        w = { ve, dm, ws, pl, cam, lp };
        return w.workspace && w.players;
    }

    const World& Get() { return w; }

    std::vector<Instance> GetPlayers()
    {
        std::vector<Instance> out;
        if (!w.players) return out;
        for (auto& c : Instance(w.players).GetChildren())
            if (c.valid()) out.push_back(c);
        return out;
    }

    static HWND FindRobloxWindow()
    {
        return FindWindowW(nullptr, L"Roblox");
    }

    bool WorldToScreen(const Vec3& world, Vec2& out)
    {
        HWND hwnd = FindRobloxWindow();
        if (!hwnd || !w.visualEngine) return false;

        RECT rc; POINT pt{};
        if (!GetClientRect(hwnd, &rc) || !ClientToScreen(hwnd, &pt)) return false;
        float rw = (float)(rc.right - rc.left), rh = (float)(rc.bottom - rc.top);

        // view matrix changes every frame; cache briefly to avoid an RPM
        // round-trip on every call (ESP does several per player)
        static Matrix4 view{};
        static DWORD viewTick = 0;
        DWORD now = GetTickCount();
        if (now - viewTick > 8)
        {
            view = g_mem.Read<Matrix4>(w.visualEngine + OFF("VisualEngine", "ViewMatrix"));
            viewTick = now;
        }
        Matrix4 m = view;

        float x = world.x * m.m[0] + world.y * m.m[1] + world.z * m.m[2] + m.m[3];
        float y = world.x * m.m[4] + world.y * m.m[5] + world.z * m.m[6] + m.m[7];
        float z = world.x * m.m[8] + world.y * m.m[9] + world.z * m.m[10] + m.m[11];
        float wv = world.x * m.m[12] + world.y * m.m[13] + world.z * m.m[14] + m.m[15];
        if (wv < 0.1f) { out = { -1, -1 }; return false; }

        float inv = 1.f / wv;
        out.x = ((rw / 2) * (x * inv) + (rw / 2)) + (float)pt.x;
        out.y = (-(rh / 2) * (y * inv) + (rh / 2)) + (float)pt.y;
        return true;
    }

    Vec3 LocalPlayerPos()
    {
        if (!w.localPlayer) return {};
        auto ch = Instance(w.localPlayer).GetCharacter();
        if (!ch) return {};
        auto hrp = ch.FindFirstChild("HumanoidRootPart");
        if (!hrp) hrp = ch.FindFirstChild("Torso");
        return hrp ? Instance::PartPosition(hrp.addr) : Vec3{};
    }
}
