// NYX internal DLL - runs INSIDE RobloxPlayerBeta.exe
// phase 1: prove injection + direct memory reads (no RPM overhead)
// phase 2+: port features, then Lua bridge for raycast/executor stuff
#include <Windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include "offsets.h"

#define OFF(cls, field) offsets::Get(cls, field)

static void Log(const char* fmt, ...)
{
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    strcat_s(tmp, "nyx_internal.log");
    FILE* f = nullptr;
    fopen_s(&f, tmp, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---------- direct memory readers (no ReadProcessMemory) ----------

template <typename T> static T Rd(uintptr_t a) { return *reinterpret_cast<T*>(a); }
template <typename T> static T RdSafe(uintptr_t a) { __try { return *reinterpret_cast<T*>(a); } __except (EXCEPTION_EXECUTE_HANDLER) { return T{}; } }

static bool SafeMemCpy(void* dst, const void* src, size_t n)
{
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void ReadCStrBounded(uintptr_t addr, size_t maxLen, char* out, size_t outSize)
{
    size_t n = 0;
    __try {
        for (; n < maxLen && n + 1 < outSize; n++) { char c = *(char*)(addr + n); if (!c) break; out[n] = c; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    out[n < outSize ? n : outSize - 1] = 0;
}

static bool printable(const std::string& s)
{
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) if (c < 32 || c > 126) return false;
    return true;
}

static std::string ReadRobloxString(uintptr_t addr)
{
    if (!addr) return "";
    int len = RdSafe<int>(addr + 0x18);
    uintptr_t ptr = addr;
    if (len >= 16) ptr = RdSafe<uintptr_t>(addr);
    if (!ptr || len <= 0 || len > 200) return "";
    std::string out; out.resize(len);
    memcpy(out.data(), (void*)ptr, len);
    auto nul = out.find('\0');
    if (nul != std::string::npos) out.resize(nul);
    return out;
}

static uintptr_t Base() { return (uintptr_t)GetModuleHandleW(nullptr); }

static std::string InstName(uintptr_t inst)
{
    std::string s;
    uintptr_t nc = OFF("Instance", "NameContainer");
    if (nc)
    {
        uintptr_t nameObj = RdSafe<uintptr_t>(inst + nc);
        if (nameObj) { s = ReadRobloxString(nameObj); if (printable(s)) return s; }
    }
    s = ReadRobloxString(inst + OFF("Instance", "Name"));
    if (printable(s)) return s;
    uintptr_t np = RdSafe<uintptr_t>(inst + OFF("Instance", "Name"));
    if (np) { s = ReadRobloxString(np); if (printable(s)) return s; }
    return "";
}

static std::string InstClass(uintptr_t inst)
{
    uintptr_t cd = RdSafe<uintptr_t>(inst + OFF("Instance", "ClassDescriptor"));
    if (!cd) return "";
    std::string s = ReadRobloxString(cd + OFF("Instance", "ClassName"));
    if (printable(s)) return s;
    uintptr_t np = RdSafe<uintptr_t>(cd + OFF("Instance", "ClassName"));
    if (np) { s = ReadRobloxString(np); if (printable(s)) return s; }
    return "";
}

static std::vector<uintptr_t> Children(uintptr_t inst)
{
    std::vector<uintptr_t> out;
    uintptr_t start = RdSafe<uintptr_t>(inst + OFF("Instance", "ChildrenStart"));
    if (!start) return out;
    uintptr_t end = RdSafe<uintptr_t>(start + OFF("Instance", "ChildrenEnd"));
    for (uintptr_t e = RdSafe<uintptr_t>(start); e && e < end; e += 0x10)
    {
        uintptr_t c = RdSafe<uintptr_t>(e);
        if (c > 0x10000) out.push_back(c);
    }
    return out;
}

static uintptr_t FindChildClass(uintptr_t parent, const char* cls)
{
    for (uintptr_t c : Children(parent))
        if (InstClass(c) == cls) return c;
    return 0;
}

// replicated attributes - same layout as the external reader
namespace AttrOff {
    constexpr uintptr_t ComponentMap = 0x38;
    constexpr uint16_t  ComponentIdAttrs = 9;
    constexpr size_t    EntrySize = 0x10;
    constexpr uintptr_t IsMap = 0x0;
    constexpr uintptr_t MapLength = 0x8;
    constexpr uintptr_t MapAttributes = 0x18;
    constexpr uintptr_t ArrAttributes = 0x8;
    constexpr uintptr_t ArrLength = 0x10;
    constexpr uintptr_t AttrSize = 0x58;
    constexpr uintptr_t AttrKey = 0x0;
    constexpr uintptr_t AttrValue = 0x18;
    constexpr int MaxAttrs = 64;
}

static bool AttrPrintable(const std::string& s)
{
    if (s.empty() || s.size() > 200) return false;
    for (char c : s) if ((unsigned char)c < 32 || (unsigned char)c > 126) return false;
    return true;
}

static std::string ReadAttrString(uintptr_t addr)
{
    std::string s = ReadRobloxString(addr);
    if (AttrPrintable(s)) return s;
    uint64_t size = RdSafe<uint64_t>(addr + 0x10);
    uint64_t cap  = RdSafe<uint64_t>(addr + 0x18);
    if (size > 0 && size <= 200)
    {
        uintptr_t ptr = (cap > 15) ? RdSafe<uintptr_t>(addr) : addr;
        if (ptr > 0x10000)
        {
            std::string out; out.resize(size);
            if (!SafeMemCpy(out.data(), (void*)ptr, size)) return "";
            auto nul = out.find('\0');
            if (nul != std::string::npos) out.resize(nul);
            if (AttrPrintable(out)) return out;
        }
    }
    return "";
}

static std::vector<std::pair<std::string, std::string>> GetAttributes(uintptr_t inst)
{
    std::vector<std::pair<std::string, std::string>> out;
    uintptr_t compMap = RdSafe<uintptr_t>(inst + AttrOff::ComponentMap);
    if (!compMap) return out;
    uintptr_t start = RdSafe<uintptr_t>(compMap);
    uintptr_t end   = RdSafe<uintptr_t>(compMap + 8);
    if (!start || end <= start || end - start > 0x10000) return out;

    uintptr_t attrs = 0;
    for (uintptr_t e = start; e < end; e += AttrOff::EntrySize)
    {
        if (RdSafe<uint16_t>(e + 8) == AttrOff::ComponentIdAttrs) { attrs = RdSafe<uintptr_t>(e); break; }
    }
    if (!attrs) return out;

    bool isMap = RdSafe<uint8_t>(attrs + AttrOff::IsMap) != 0;
    uintptr_t entries; int len;
    if (isMap) { entries = RdSafe<uintptr_t>(attrs + AttrOff::MapAttributes); len = (int)RdSafe<uint32_t>(attrs + AttrOff::MapLength); }
    else       { entries = RdSafe<uintptr_t>(attrs + AttrOff::ArrAttributes);  len = (int)RdSafe<uint32_t>(attrs + AttrOff::ArrLength); }
    if (!entries || len <= 0 || len > AttrOff::MaxAttrs) return out;

    for (int i = 0; i < len; i++)
    {
        uintptr_t e = entries + (uintptr_t)i * AttrOff::AttrSize;
        // attribute key is a string_view {ptr,len} - prefer the length,
        // fall back to nul-scan if it looks bogus
        uintptr_t keyPtr = RdSafe<uintptr_t>(e + AttrOff::AttrKey);
        size_t keyLen = RdSafe<size_t>(e + AttrOff::AttrKey + 8);
        if (keyPtr < 0x10000 || keyLen == 0 || keyLen > 64) keyLen = 64;
        char kbuf[65]{};
        ReadCStrBounded(keyPtr, keyLen, kbuf, sizeof(kbuf));
        std::string key(kbuf);
        if (key.empty() || key.size() > 64) break;
        std::string val = ReadAttrString(e + AttrOff::AttrValue);
        out.emplace_back(key, val);
    }
    return out;
}

// ---------- game state ----------

struct World { uintptr_t dm = 0, ws = 0, players = 0, localPlayer = 0; };
static World g_w;

static bool Resolve()
{
    uintptr_t ve = RdSafe<uintptr_t>(Base() + OFF("VisualEngine", "Pointer"));
    if (!ve) return false;
    uintptr_t v1 = RdSafe<uintptr_t>(ve + OFF("VisualEngine", "FakeDataModel"));
    uintptr_t dm = v1 ? RdSafe<uintptr_t>(v1 + OFF("FakeDataModel", "RealDataModel")) : 0;
    if (!dm) return false;

    uintptr_t ws = FindChildClass(dm, "Workspace");
    uintptr_t pl = FindChildClass(dm, "Players");
    uintptr_t lp = pl ? RdSafe<uintptr_t>(pl + OFF("Player", "LocalPlayer")) : 0;

    g_w = { dm, ws, pl, lp };
    return ws && pl;
}

static DWORD WINAPI Main(LPVOID)
{
    Log("[nyx-int] dll loaded, base=0x%llx", (unsigned long long)Base());

    // wait for offsets
    for (int i = 0; i < 120 && !offsets::Fetch(); i++) Sleep(1000);
    if (!offsets::Ready()) { Log("[nyx-int] offsets fetch FAILED"); return 0; }
    Log("[nyx-int] offsets ok ver=%s", offsets::ClientVersion().c_str());

    // wait for datamodel
    for (int i = 0; i < 300 && !Resolve(); i++) Sleep(1000);
    if (!g_w.dm) { Log("[nyx-int] resolve FAILED"); return 0; }
    Log("[nyx-int] resolved dm=0x%llx ws=0x%llx players=0x%llx lp=0x%llx",
        (unsigned long long)g_w.dm, (unsigned long long)g_w.ws,
        (unsigned long long)g_w.players, (unsigned long long)g_w.localPlayer);

    // proof loop: dump players + kit attributes every 3s
    bool attrDumped = false;
    while (true)
    {
        Sleep(3000);
        if (!g_w.players) { Resolve(); continue; }

        auto kids = Children(g_w.players);
        Log("[nyx-int] players=%zu", kids.size());

        for (uintptr_t p : kids)
        {
            std::string name = InstName(p);
            if (name.empty()) continue;
            if (!attrDumped)
            {
                // dump attributes once per player to verify kit read path
                auto attrs = GetAttributes(p);
                std::string dump;
                for (auto& [k, v] : attrs)
                {
                    if (k.find("Kit") != std::string::npos || k.find("kit") != std::string::npos ||
                        k == "Team" || k == "TitleType")
                    {
                        dump += k; dump += "="; dump += v; dump += " ";
                    }
                }
                if (!dump.empty()) Log("[nyx-int]   '%s' attrs: %s", name.c_str(), dump.c_str());
                if (!attrs.empty()) attrDumped = true;
            }
        }
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        Log("[nyx-int] DllMain entered");
        DisableThreadLibraryCalls(mod);
        CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
    }
    return TRUE;
}
