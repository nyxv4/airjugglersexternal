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
    std::string name;
    float health, maxHealth;
    float dist;
    bool teammate;
    uintptr_t headAddr;
};

static std::vector<PlayerBox> g_players;
static Vec2 g_localScreen;
static float g_localX, g_localY; // screen center of roblox client

static void CollectPlayers()
{
    g_players.clear();
    auto& w = sdk::Get();
    if (!w.localPlayer) return;

    uintptr_t localTeam = Instance(w.localPlayer).GetTeam();
    Vec3 localPos = sdk::LocalPlayerPos();

    HWND rbx = FindWindowW(nullptr, L"Roblox");
    if (!rbx) return;
    RECT rc{}; POINT pt{};
    GetClientRect(rbx, &rc); ClientToScreen(rbx, &pt);
    float rw = (float)(rc.right - rc.left), rh = (float)(rc.bottom - rc.top);
    g_localX = pt.x + rw / 2.f; g_localY = pt.y + rh / 2.f;

    // name cache: reading names costs an RPM round-trip per player
    static std::unordered_map<uintptr_t, std::string> nameCache;

    for (auto& p : sdk::GetPlayers())
    {
        if (p.addr == w.localPlayer) continue;
        auto ch = p.GetCharacter();
        if (!ch) continue;

        // enumerate character children ONCE, then look parts up by name
        // (each FindFirstChild would re-enumerate = dozens of extra RPM calls)
        static thread_local std::unordered_map<std::string, uintptr_t> partMap;
        partMap.clear();
        for (auto& c : ch.GetChildren())
        {
            std::string cn = c.GetName();
            if (!cn.empty()) partMap[cn] = c.addr;
        }
        auto findPart = [&](const char* n) -> uintptr_t {
            auto it = partMap.find(n); return it == partMap.end() ? 0 : it->second;
        };

        uintptr_t headA = findPart("Head");
        uintptr_t hrpA = findPart("HumanoidRootPart");
        if (!hrpA) hrpA = findPart("Torso");
        if (!headA || !hrpA) continue;
        if (Instance::PartTransparency(headA) > 0.9f) continue;

        bool teammate = false;
        if (g_cfg.espTeamCheck)
        {
            uintptr_t t = p.GetTeam();
            teammate = (t != 0 && t == localTeam);
            if (teammate && !g_cfg.espShowTeammates) continue;
        }

        Vec3 hp = Instance::PartPosition(headA); hp.y += 0.4f;
        Vec3 fp = Instance::PartPosition(hrpA); fp.y -= 3.f;
        Vec3 root = Instance::PartPosition(hrpA);

        Vec2 hs, fs, rs;
        bool hok = sdk::WorldToScreen(hp, hs);
        bool fok = sdk::WorldToScreen(fp, fs);
        bool rok = sdk::WorldToScreen(root, rs);
        if (!hok && !fok) continue;

        float dist = sqrtf((localPos.x - root.x) * (localPos.x - root.x) +
                           (localPos.z - root.z) * (localPos.z - root.z));
        if (dist > g_cfg.aimbotRange) continue;

        PlayerBox pb{};
        auto nit = nameCache.find(p.addr);
        if (nit != nameCache.end()) pb.name = nit->second;
        else { pb.name = p.GetName(); nameCache[p.addr] = pb.name; }
        pb.headAddr = headA;
        pb.dist = dist;
        pb.teammate = teammate;
        pb.health = p.GetHealth(); pb.maxHealth = p.GetMaxHealth();

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
    if (!key_down(g_cfg.aimbotKey)) return;
    if (g_players.empty()) return;

    float cx = g_localX, cy = g_localY;
    const PlayerBox* best = nullptr;
    float bestD = g_cfg.aimbotFov;

    for (auto& p : g_players)
    {
        if (p.teammate) continue;
        float dx = p.headX - cx, dy = p.headY - cy;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < bestD) { bestD = d; best = &p; }
    }
    if (!best) return;

    float dx = (best->headX - cx) * g_cfg.aimbotSmooth;
    float dy = (best->headY - cy) * g_cfg.aimbotSmooth;
    mouse_move((int)dx, (int)dy);
}

static void RunTriggerbot()
{
    if (!g_cfg.triggerbotEnabled || !g_mem.IsAttached()) return;
    if (!key_down(g_cfg.triggerKey)) return;

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

static void RunHitbox()
{
    static bool wasHeld = false;
    static std::vector<std::pair<uintptr_t, Vec3>> originals;
    static DWORD lastRun = 0;

    bool held = g_cfg.hitboxEnabled && g_mem.IsAttached() && GameFocused() && key_down(g_cfg.hitboxKey);
    if (!held)
    {
        if (wasHeld)
        {
            for (auto& [addr, sz] : originals)
            {
                uintptr_t prim = g_mem.ReadPtr(addr + OFF("BasePart", "Primitive"));
                if (prim) g_mem.Write<Vec3>(prim + OFF("Primitive", "Size"), sz);
            }
            originals.clear();
        }
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
            g_mem.Write<Vec3>(sizeAddr, ns);
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

static void DrawMenu()
{
    ImGui::SetNextWindowSize({ 520, 480 }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("NYX", nullptr, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }

    if (ImGui::BeginTabBar("tabs"))
    {
        if (ImGui::BeginTabItem("Aimbot"))
        {
            ImGui::Checkbox("Enabled", &g_cfg.aimbotEnabled);
            ImGui::SliderFloat("FOV", &g_cfg.aimbotFov, 10.f, 500.f, "%.0f px");
            ImGui::SliderFloat("Smoothness", &g_cfg.aimbotSmooth, 0.05f, 1.f, "%.2f");
            ImGui::SliderFloat("Range", &g_cfg.aimbotRange, 50.f, 3000.f, "%.0f studs");
            ImGui::TextDisabled("Hold X (key %d) to aim", g_cfg.aimbotKey);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Triggerbot"))
        {
            ImGui::Checkbox("Enabled", &g_cfg.triggerbotEnabled);
            ImGui::SliderFloat("FOV", &g_cfg.triggerFov, 5.f, 150.f, "%.0f px");
            ImGui::TextDisabled("Hold X to fire when crosshair is on a target");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Hitbox"))
        {
            ImGui::Checkbox("Enabled", &g_cfg.hitboxEnabled);
            ImGui::SliderFloat("Multiplier", &g_cfg.hitboxMult, 1.f, 6.f, "%.1fx");
            ImGui::SliderFloat("Max studs (0=off)", &g_cfg.hitboxMaxStuds, 0.f, 30.f, "%.1f");
            ImGui::TextDisabled("Hold C to expand enemy hitboxes");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Visuals"))
        {
            ImGui::Checkbox("Box ESP", &g_cfg.espBox);
            ImGui::Checkbox("Name ESP", &g_cfg.espName);
            ImGui::Checkbox("Distance ESP", &g_cfg.espDistance);
            ImGui::Checkbox("Health bar", &g_cfg.espHealth);
            ImGui::Checkbox("Snaplines", &g_cfg.espSnaplines);
            ImGui::Checkbox("Team check", &g_cfg.espTeamCheck);
            ImGui::Checkbox("Show teammates", &g_cfg.espShowTeammates);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Misc"))
        {
            ImGui::Checkbox("Streamproof", &g_cfg.streamproof);
            if (g_cfg.streamproof) g_overlay.ApplyStreamproof();
            ImGui::Separator();
            ImGui::Text("Keybinds");
            RebindButton("Menu", &g_cfg.menuKey);
            RebindButton("Aimbot", &g_cfg.aimbotKey);
            RebindButton("Triggerbot", &g_cfg.triggerKey);
            RebindButton("Hitbox", &g_cfg.hitboxKey);
            ImGui::TextDisabled("INSERT still toggles this menu");
            ImGui::Text("Roblox PID: %lu", g_mem.pid);
            ImGui::Text("Base: 0x%llx", (unsigned long long)g_mem.base);
            ImGui::Text("Offsets: %s", offsets::Ready() ? offsets::ClientVersion().c_str() : "fetching...");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
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
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, ctx);
    return true;
}

void Overlay::ApplyStreamproof()
{
    if (g_cfg.streamproof)
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    else
        SetWindowDisplayAffinity(hwnd, WDA_NONE);
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
            if (!offsets::Ready()) offsets::Fetch();
            if (g_mem.Attach(L"RobloxPlayerBeta.exe") || g_mem.Attach(L"RobloxPlayer.exe"))
                sdk::Resolve();
        }
        else if (g_mem.IsAttached())
        {
            static DWORD lastResolve = 0;
            if (GetTickCount() - lastResolve > 1500) { lastResolve = GetTickCount(); sdk::Resolve(); }
            static DWORD lastCollect = 0;
            // only burn RPM calls while Roblox is actually focused
            if (focused && GetTickCount() - lastCollect > 16) { lastCollect = GetTickCount(); CollectPlayers(); }
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

        // idle throttle: nothing to draw and menu closed -> don't burn a core
        if (!focused && !g_cfg.menuVisible && !g_mem.IsAttached()) Sleep(8);
    }
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
