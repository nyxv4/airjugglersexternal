#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <string>

class Overlay
{
public:
    HWND hwnd = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

    bool Create(HINSTANCE inst);
    void Run();
    void Shutdown();
    void SetClickThrough(bool clickThrough);
    void ApplyStreamproof();
};

extern Overlay g_overlay;
