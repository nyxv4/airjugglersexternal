#include "sdk.h"
#include "offsets.h"
#include <cmath>
#include <set>

#define OFF(cls, field) offsets::Get(cls, field)

std::string Instance::GetName() const
{
    if (!valid()) return "";
    uintptr_t namePtr = g_mem.ReadPtr(addr + OFF("Instance", "Name"));
    if (namePtr == 0) return "";
    return g_mem.ReadRobloxString(namePtr);
}

std::string Instance::GetClassName() const
{
    if (!valid()) return "";
    uintptr_t classDesc = g_mem.ReadPtr(addr + OFF("Instance", "ClassDescriptor"));
    if (classDesc == 0) return "";
    uintptr_t namePtr = g_mem.ReadPtr(classDesc + OFF("Instance", "ClassName"));
    if (namePtr == 0) return "";
    return g_mem.ReadRobloxString(namePtr);
}

std::vector<Instance> Instance::GetChildren() const
{
    std::vector<Instance> out;
    if (!valid()) return out;
    uintptr_t start = g_mem.ReadPtr(addr + OFF("Instance", "ChildrenStart"));
    if (start == 0) return out;
    uintptr_t end = g_mem.ReadPtr(start + OFF("Instance", "ChildrenEnd"));
    for (uintptr_t entry = g_mem.ReadPtr(start); entry && entry < end; entry += 0x10)
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
    uintptr_t prim = PartPrimitive(part);
    return prim ? g_mem.Read<float>(prim + OFF("Primitive", "Transparency")) : 1.f;
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

        Matrix4 m = g_mem.Read<Matrix4>(w.visualEngine + OFF("VisualEngine", "ViewMatrix"));

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
