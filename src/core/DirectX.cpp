#include "core/DirectX.h"

#include <dxgi.h>
#include <dxgi1_6.h>
#include "core/DebugOptions.h"
#include "core/DisplayMath.h"
#include "core/GameSettings.h"
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
ComPtr<ID3D11Device> g_Device;
ComPtr<ID3D11DeviceContext> g_Context;
ComPtr<IDXGISwapChain> g_SwapChain;
ComPtr<ID3D11RenderTargetView> g_RenderTargetView;

int g_BackBufferWidth = 1280;
int g_BackBufferHeight = 720;
int g_RenderWidth = 0;  // the size of the picture of the world (0 = the game rectangle): see SetRenderSize
int g_RenderHeight = 0;

bool CreateRenderTarget(int width, int height)
{
    if (g_Device == nullptr || g_Context == nullptr || g_SwapChain == nullptr)
    {
        return false;
    }

    ComPtr<ID3D11Texture2D> backBuffer;

    const HRESULT getBufferResult =
        g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));

    if (FAILED(getBufferResult))
    {
        MessageBoxA(nullptr, "Swap chain GetBuffer failed.", "DirectX Error", MB_OK | MB_ICONERROR);

        return false;
    }

    const HRESULT renderTargetResult =
        g_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, g_RenderTargetView.GetAddressOf());

    if (FAILED(renderTargetResult))
    {
        MessageBoxA(nullptr, "CreateRenderTargetView failed.", "DirectX Error", MB_OK | MB_ICONERROR);

        return false;
    }

    ID3D11RenderTargetView* renderTarget = g_RenderTargetView.Get();

    g_Context->OMSetRenderTargets(1, &renderTarget, nullptr);

    g_BackBufferWidth = width;
    g_BackBufferHeight = height;

    // the viewport is the 16:9 game rectangle (without PostFx the 2D renderer draws straight into it)
    int gx, gy, gw, gh;
    DirectXSystem::GetGameRect(gx, gy, gw, gh);
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = static_cast<float>(gx);
    viewport.TopLeftY = static_cast<float>(gy);
    viewport.Width = static_cast<float>(gw);
    viewport.Height = static_cast<float>(gh);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    g_Context->RSSetViewports(1, &viewport);

    return true;
}
}

namespace
{
char g_AdapterName[128] = "?";
bool g_FlipModel = false;

// The fast graphics chip of a laptop that has two (nullptr = let Windows choose).
ComPtr<IDXGIAdapter1> PickHighPerformanceAdapter()
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
    {
        return nullptr;
    }

    ComPtr<IDXGIFactory6> factory6;
    if (FAILED(factory.As(&factory6)))
    {
        return nullptr; // Windows older than 10 version 1803: the default adapter is used
    }

    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                         IID_PPV_ARGS(adapter.GetAddressOf()))))
        {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            return adapter;
        }
    }

    return nullptr;
}

void ReadAdapterName()
{
    g_AdapterName[0] = '?';
    g_AdapterName[1] = '\0';

    ComPtr<IDXGIDevice> dxgiDevice;
    if (g_Device == nullptr || FAILED(g_Device.As(&dxgiDevice)))
    {
        return;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
    {
        return;
    }

    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc)))
    {
        return;
    }

    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, g_AdapterName, static_cast<int>(sizeof(g_AdapterName)), nullptr, nullptr);
    g_AdapterName[sizeof(g_AdapterName) - 1] = '\0';
}
}

namespace DirectXSystem
{
bool Initialize(HWND hWnd, int width, int height)
{
    if (hWnd == nullptr || width <= 0 || height <= 0)
    {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc{};

    swapChainDesc.BufferCount = 1;

    swapChainDesc.BufferDesc.Width = static_cast<UINT>(width);

    swapChainDesc.BufferDesc.Height = static_cast<UINT>(height);

    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapChainDesc.OutputWindow = hWnd;

    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;

    swapChainDesc.Windowed = TRUE;

    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};

    D3D_FEATURE_LEVEL createdFeatureLevel{};

    // the fast graphics chip first (a laptop with two chips would otherwise run the game on the slow one), the default one when that fails
    const ComPtr<IDXGIAdapter1> adapter = PickHighPerformanceAdapter();
    auto createWith = [&](const DXGI_SWAP_CHAIN_DESC& desc) -> HRESULT
    {
        HRESULT result = E_FAIL;
        if (adapter.Get() != nullptr)
        {
            result = D3D11CreateDeviceAndSwapChain(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                   featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &desc,
                                                   g_SwapChain.ReleaseAndGetAddressOf(), g_Device.ReleaseAndGetAddressOf(),
                                                   &createdFeatureLevel, g_Context.ReleaseAndGetAddressOf());
        }
        if (FAILED(result))
        {
            g_SwapChain.Reset();
            g_Device.Reset();
            g_Context.Reset();
            result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                   featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &desc,
                                                   g_SwapChain.ReleaseAndGetAddressOf(), g_Device.ReleaseAndGetAddressOf(),
                                                   &createdFeatureLevel, g_Context.ReleaseAndGetAddressOf());
        }
        return result;
    };

    HRESULT createResult = E_FAIL;
    g_FlipModel = false;
    if (DebugOptions::FlipPresent())
    {
        // flip model: the window is presented without the extra copy of the old model; falls back to the old model when it is not available
        DXGI_SWAP_CHAIN_DESC flipDesc = swapChainDesc;
        flipDesc.BufferCount = 2;
        flipDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        createResult = createWith(flipDesc);
        g_FlipModel = SUCCEEDED(createResult);
    }
    if (FAILED(createResult))
    {
        createResult = createWith(swapChainDesc);
    }

    if (FAILED(createResult))
    {
        MessageBoxA(nullptr, "D3D11CreateDeviceAndSwapChain failed.", "DirectX Error", MB_OK | MB_ICONERROR);

        return false;
    }

    ReadAdapterName();

    return CreateRenderTarget(width, height);
}

bool Resize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    if (g_SwapChain == nullptr || g_Context == nullptr)
    {
        return false;
    }

    if (width == g_BackBufferWidth && height == g_BackBufferHeight)
    {
        return true;
    }

    g_Context->OMSetRenderTargets(0, nullptr, nullptr);

    g_Context->ClearState();
    g_Context->Flush();

    g_RenderTargetView.Reset();

    const HRESULT resizeResult =
        g_SwapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);

    if (FAILED(resizeResult))
    {
        MessageBoxA(nullptr, "Swap chain ResizeBuffers failed.", "DirectX Error", MB_OK | MB_ICONERROR);

        return false;
    }

    return CreateRenderTarget(width, height);
}

void Finalize()
{
    if (g_Context != nullptr)
    {
        g_Context->OMSetRenderTargets(0, nullptr, nullptr);

        g_Context->ClearState();
        g_Context->Flush();
    }

    g_RenderTargetView.Reset();
    g_SwapChain.Reset();
    g_Context.Reset();
    g_Device.Reset();
}

void BeginFrame(float r, float g, float b, float a)
{
    if (g_Context == nullptr || g_RenderTargetView == nullptr)
    {
        return;
    }

    ID3D11RenderTargetView* renderTarget = g_RenderTargetView.Get();

    g_Context->OMSetRenderTargets(1, &renderTarget, nullptr);
    {
        int gx, gy, gw, gh;
        GetGameRect(gx, gy, gw, gh);
        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = static_cast<float>(gx);
        viewport.TopLeftY = static_cast<float>(gy);
        viewport.Width = static_cast<float>(gw);
        viewport.Height = static_cast<float>(gh);
        viewport.MaxDepth = 1.0f;
        g_Context->RSSetViewports(1, &viewport);
    }

    const float clearColor[4] = {r, g, b, a};

    g_Context->ClearRenderTargetView(g_RenderTargetView.Get(), clearColor);
}

static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_SnapshotTexture;
static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_SnapshotSRV;
static UINT g_SnapshotWidth = 0;
static UINT g_SnapshotHeight = 0;
static DXGI_FORMAT g_SnapshotFormat = DXGI_FORMAT_UNKNOWN;
static bool g_SnapshotFrozen = false;
static bool g_SnapshotValid = false;
static void CaptureSnapshot()
{
    if (g_SnapshotFrozen || g_SwapChain == nullptr || g_Context == nullptr || g_Device == nullptr)
    {
        return;
    }
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()))))
    {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    if (g_SnapshotTexture == nullptr || g_SnapshotWidth != desc.Width || g_SnapshotHeight != desc.Height
        || g_SnapshotFormat != desc.Format)
    {
        g_SnapshotSRV.Reset();
        g_SnapshotTexture.Reset();
        g_SnapshotValid = false;
        D3D11_TEXTURE2D_DESC copyDesc = desc;
        copyDesc.Usage = D3D11_USAGE_DEFAULT;
        copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copyDesc.CPUAccessFlags = 0;
        copyDesc.MiscFlags = 0;
        copyDesc.MipLevels = 1;
        copyDesc.ArraySize = 1;
        copyDesc.SampleDesc.Count = 1;
        copyDesc.SampleDesc.Quality = 0;
        if (FAILED(g_Device->CreateTexture2D(&copyDesc, nullptr, g_SnapshotTexture.GetAddressOf())))
        {
            return;
        }
        if (FAILED(g_Device->CreateShaderResourceView(g_SnapshotTexture.Get(), nullptr, g_SnapshotSRV.GetAddressOf())))
        {
            g_SnapshotTexture.Reset();
            return;
        }
        g_SnapshotWidth = desc.Width;
        g_SnapshotHeight = desc.Height;
        g_SnapshotFormat = desc.Format;
    }
    g_Context->CopyResource(g_SnapshotTexture.Get(), backBuffer.Get());
    g_SnapshotValid = true;
}
ID3D11ShaderResourceView* GetSnapshotSRV()
{
    return g_SnapshotValid ? g_SnapshotSRV.Get() : nullptr;
}
bool HasSnapshot()
{
    return g_SnapshotValid;
}
void SetSnapshotFrozen(bool frozen)
{
    g_SnapshotFrozen = frozen;
}

void EndFrame()
{
    if (g_SwapChain == nullptr)
    {
        return;
    }
    CaptureSnapshot();
    g_SwapChain->Present(GameSettings::Get().vsync ? 1 : 0, 0); // (VSYNC off: no wait for the display, the cap of the settings paces the frames)
}

ID3D11RenderTargetView* GetBackBufferRTV()
{
    return g_RenderTargetView.Get();
}

void GetGameRect(int& x, int& y, int& w, int& h)
{
    const DisplayMath::Rect r = DisplayMath::GameRect(g_BackBufferWidth, g_BackBufferHeight); // (the same arithmetic as the mouse and the text: DisplayMath.h)
    x = r.x;
    y = r.y;
    w = r.w;
    h = r.h;
}

void SetRenderSize(int width, int height)
{
    g_RenderWidth = width > 0 ? width : 0;
    g_RenderHeight = height > 0 ? height : 0;
}

void GetRenderSize(int& width, int& height)
{
    const DisplayMath::Rect game = DisplayMath::GameRect(g_BackBufferWidth, g_BackBufferHeight);
    DisplayMath::EffectiveRender(g_RenderWidth, g_RenderHeight, game, width, height);
}

bool IsGameRectFull()
{
    int x, y, w, h;
    GetGameRect(x, y, w, h);
    return x == 0 && y == 0 && w == g_BackBufferWidth && h == g_BackBufferHeight;
}

int GetBackBufferWidth()
{
    return g_BackBufferWidth;
}

int GetBackBufferHeight()
{
    return g_BackBufferHeight;
}

ID3D11Device* GetDevice()
{
    return g_Device.Get();
}

ID3D11DeviceContext* GetContext()
{
    return g_Context.Get();
}

IDXGISwapChain* GetSwapChain()
{
    return g_SwapChain.Get();
}

const char* GetAdapterName()
{
    return g_AdapterName;
}

bool IsFlipModel()
{
    return g_FlipModel;
}
}



