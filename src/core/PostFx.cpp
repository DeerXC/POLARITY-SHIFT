#include "core/PostFx.h"
#include "core/DebugOptions.h"
#include "core/DirectX.h"
#include "core/GameSettings.h"

#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


using Microsoft::WRL::ComPtr;

namespace
{
// ---- the shaders (Shader Model 5). One source, one entry point per pass.
// The shaders are in shaders/postfx.hlsli, compiled at build time into postfx_*.cso (next to the program, like the 2D shaders).


struct Params
{
    float texel[2];
    float threshold;
    float knee;
    float tint[3];
    float bloomStrength;
    float exposure;
    float saturation;
    float contrast;
    float vignette;
    float aberration;
    float grain;
    float danger;
    float time;
    float rect[4];
};
static_assert(sizeof(Params) % 16 == 0, "a constant buffer is a multiple of 16 bytes");

struct Target
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    int width = 0, height = 0;
};

constexpr int BLOOM_LEVELS = 5;
constexpr DXGI_FORMAT HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

bool g_Ready = false;    // everything was created: the passes run
bool g_Failed = false;   // something failed once: never try again
bool g_InScene = false;
Target g_Scene;
Target g_Bloom[BLOOM_LEVELS];
ComPtr<ID3D11VertexShader> g_VS;
ComPtr<ID3D11PixelShader> g_PSPrefilter, g_PSDown, g_PSUp, g_PSComposite, g_PSFill;
ComPtr<ID3D11Buffer> g_ParamBuffer;
ComPtr<ID3D11SamplerState> g_Sampler;
ComPtr<ID3D11BlendState> g_Additive;
ComPtr<ID3D11BlendState> g_Opaque;
ComPtr<ID3D11RasterizerState> g_Raster;
PostFx::Look g_Look;
float g_Kick = 0.0f; // the fringe of a hit / a cut-in (PostFx::Kick), fades every frame
float g_Time = 0.0f;

void Fail(const char* what, HRESULT hr)
{
    g_Failed = true;
    g_Ready = false;
    FILE* f = nullptr;
    if (fopen_s(&f, "postfx_error.txt", "w") == 0 && f != nullptr)
    {
        std::fprintf(f, "PostFx is off: %s (HRESULT 0x%08lX)\n", what, static_cast<unsigned long>(hr));
        std::fclose(f);
    }
}

std::vector<char> LoadShaderFile(const char* fileName)
{
    std::vector<char> data;
    FILE* f = nullptr;
    if (fopen_s(&f, fileName, "rb") != 0 || f == nullptr) return data;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0)
    {
        data.resize(static_cast<size_t>(size));
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
    }
    std::fclose(f);
    return data;
}

bool MakePixelShader(const char* fileName, ComPtr<ID3D11PixelShader>& shader)
{
    const std::vector<char> code = LoadShaderFile(fileName);
    if (code.empty())
    {
        Fail(fileName, E_FAIL);
        return false;
    }
    const HRESULT hr = DirectXSystem::GetDevice()->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf());
    if (FAILED(hr))
    {
        Fail(fileName, hr);
        return false;
    }
    return true;
}

bool MakeTarget(Target& t, int width, int height)
{
    t = Target{};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width < 1 ? 1 : width);
    desc.Height = static_cast<UINT>(height < 1 ? 1 : height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = HDR_FORMAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Device* device = DirectXSystem::GetDevice();
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, t.texture.GetAddressOf());
    if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(t.texture.Get(), nullptr, t.rtv.GetAddressOf());
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(t.texture.Get(), nullptr, t.srv.GetAddressOf());
    if (FAILED(hr))
    {
        Fail("render target", hr);
        return false;
    }
    t.width = static_cast<int>(desc.Width);
    t.height = static_cast<int>(desc.Height);
    return true;
}

// the targets follow the size of the 16:9 game rectangle of the back buffer
bool EnsureTargets()
{
    int gx, gy, w, h;
    DirectXSystem::GetGameRect(gx, gy, w, h);
    if (g_Scene.texture != nullptr && g_Scene.width == w && g_Scene.height == h) return true;
    if (!MakeTarget(g_Scene, w, h)) return false;
    int bw = w, bh = h;
    for (int i = 0; i < BLOOM_LEVELS; ++i)
    {
        bw = bw / 2 > 1 ? bw / 2 : 1;
        bh = bh / 2 > 1 ? bh / 2 : 1;
        if (!MakeTarget(g_Bloom[i], bw, bh)) return false;
    }
    return true;
}

void SetViewport(ID3D11DeviceContext* context, int width, int height, int left = 0, int top = 0)
{
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = static_cast<float>(left);
    viewport.TopLeftY = static_cast<float>(top);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
}

void UploadParams(ID3D11DeviceContext* context, const Target& source)
{
    Params p{};
    p.texel[0] = 1.0f / static_cast<float>(source.width);
    p.texel[1] = 1.0f / static_cast<float>(source.height);
    p.threshold = g_Look.threshold;
    p.knee = 0.30f;
    p.tint[0] = g_Look.tint[0];
    p.tint[1] = g_Look.tint[1];
    p.tint[2] = g_Look.tint[2];
    p.bloomStrength = g_Look.bloom;
    p.exposure = g_Look.exposure;
    p.saturation = g_Look.saturation;
    p.contrast = g_Look.contrast;
    p.vignette = g_Look.vignette;
    p.aberration = g_Look.aberration + g_Kick;
    p.grain = g_Look.grain;
    p.danger = g_Look.danger;
    p.time = g_Time;
    {
        int gx, gy, gw, gh;
        DirectXSystem::GetGameRect(gx, gy, gw, gh);
        const float fw = static_cast<float>(DirectXSystem::GetBackBufferWidth() > 0 ? DirectXSystem::GetBackBufferWidth() : 1);
        const float fh = static_cast<float>(DirectXSystem::GetBackBufferHeight() > 0 ? DirectXSystem::GetBackBufferHeight() : 1);
        p.rect[0] = static_cast<float>(gx) / fw;
        p.rect[1] = static_cast<float>(gy) / fh;
        p.rect[2] = static_cast<float>(gw) / fw;
        p.rect[3] = static_cast<float>(gh) / fh;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(g_ParamBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &p, sizeof(p));
        context->Unmap(g_ParamBuffer.Get(), 0);
    }
}

// one full-screen pass: read `source` (and `second`), write `target` (nullptr: the back buffer)
void Pass(ID3D11DeviceContext* context, ID3D11PixelShader* shader, const Target& source, ID3D11ShaderResourceView* second, ID3D11RenderTargetView* target,
          int width, int height, bool additive, int left = 0, int top = 0)
{
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, none); // (a texture cannot be read while it is written)
    context->OMSetRenderTargets(1, &target, nullptr);
    SetViewport(context, width, height, left, top);
    UploadParams(context, source);
    const float factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(additive ? g_Additive.Get() : g_Opaque.Get(), factor, 0xffffffff);
    ID3D11ShaderResourceView* views[2] = {source.srv.Get(), second};
    context->PSSetShaderResources(0, 2, views);
    context->PSSetShader(shader, nullptr, 0);
    context->Draw(3, 0);
}
}

bool PostFx::Initialize()
{
    g_Look = DefaultLook();
    if (DebugOptions::PostFxOff())
    {
        g_Failed = true;
        return false;
    }
    ID3D11Device* device = DirectXSystem::GetDevice();
    if (device == nullptr)
    {
        g_Failed = true;
        return false;
    }
    const std::vector<char> vs = LoadShaderFile("postfx_vs.cso");
    if (vs.empty())
    {
        Fail("postfx_vs.cso", E_FAIL);
        return false;
    }
    HRESULT hr = device->CreateVertexShader(vs.data(), vs.size(), nullptr, g_VS.GetAddressOf());
    if (FAILED(hr))
    {
        Fail("postfx_vs.cso", hr);
        return false;
    }
    if (!MakePixelShader("postfx_prefilter.cso", g_PSPrefilter) || !MakePixelShader("postfx_down.cso", g_PSDown)
        || !MakePixelShader("postfx_up.cso", g_PSUp) || !MakePixelShader("postfx_composite.cso", g_PSComposite)
        || !MakePixelShader("postfx_fill.cso", g_PSFill))
    {
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Params);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb, nullptr, g_ParamBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Fail("constant buffer", hr);
        return false;
    }

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sampler, g_Sampler.GetAddressOf());
    if (FAILED(hr))
    {
        Fail("sampler", hr);
        return false;
    }

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&blend, g_Additive.GetAddressOf());
    blend.RenderTarget[0].BlendEnable = FALSE;
    if (SUCCEEDED(hr)) hr = device->CreateBlendState(&blend, g_Opaque.GetAddressOf());
    if (FAILED(hr))
    {
        Fail("blend state", hr);
        return false;
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&raster, g_Raster.GetAddressOf());
    if (FAILED(hr))
    {
        Fail("rasterizer state", hr);
        return false;
    }

    if (!EnsureTargets()) return false;
    g_Ready = true;
    return true;
}

void PostFx::Finalize()
{
    g_Ready = false;
    g_Scene = Target{};
    for (Target& t : g_Bloom) t = Target{};
    g_VS.Reset();
    g_PSPrefilter.Reset();
    g_PSDown.Reset();
    g_PSUp.Reset();
    g_PSComposite.Reset();
    g_PSFill.Reset();
    g_ParamBuffer.Reset();
    g_Sampler.Reset();
    g_Additive.Reset();
    g_Opaque.Reset();
    g_Raster.Reset();
}

bool PostFx::IsActive()
{
    return g_Ready && !g_Failed;
}

void PostFx::BeginScene(float r, float g, float b)
{
    g_InScene = false;
    if (!IsActive() || !EnsureTargets()) return;
    ID3D11DeviceContext* context = DirectXSystem::GetContext();
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, none);
    ID3D11RenderTargetView* target = g_Scene.rtv.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    SetViewport(context, g_Scene.width, g_Scene.height);
    const float clear[4] = {r, g, b, 1.0f};
    context->ClearRenderTargetView(target, clear);
    g_InScene = true;
}

void PostFx::EndScene()
{
    if (!g_InScene) return;
    g_InScene = false;
    g_Time += 1.0f / 60.0f;
    if (g_Time > 1000.0f) g_Time = 0.0f;
    g_Kick *= 0.86f;
    if (g_Kick < 0.0002f) g_Kick = 0.0f;
    ID3D11DeviceContext* context = DirectXSystem::GetContext();

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(g_VS.Get(), nullptr, 0);
    ID3D11Buffer* cb = g_ParamBuffer.Get();
    context->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* sampler = g_Sampler.Get();
    context->PSSetSamplers(0, 1, &sampler);
    context->RSSetState(g_Raster.Get());

    // bloom: the bright parts to half size, down the chain, back up while adding
    Pass(context, g_PSPrefilter.Get(), g_Scene, nullptr, g_Bloom[0].rtv.Get(), g_Bloom[0].width, g_Bloom[0].height, false);
    for (int i = 1; i < BLOOM_LEVELS; ++i)
    {
        Pass(context, g_PSDown.Get(), g_Bloom[i - 1], nullptr, g_Bloom[i].rtv.Get(), g_Bloom[i].width, g_Bloom[i].height, false);
    }
    for (int i = BLOOM_LEVELS - 1; i > 0; --i)
    {
        Pass(context, g_PSUp.Get(), g_Bloom[i], nullptr, g_Bloom[i - 1].rtv.Get(), g_Bloom[i - 1].width, g_Bloom[i - 1].height, true);
    }

    // the picture with its light onto the back buffer
    ID3D11RenderTargetView* backBuffer = DirectXSystem::GetBackBufferRTV();
    int gx, gy, gw, gh;
    DirectXSystem::GetGameRect(gx, gy, gw, gh);
    if (!DirectXSystem::IsGameRectFull()) // the bars round a 16:9 picture: filled first, the picture goes on top
    {
        Pass(context, g_PSFill.Get(), g_Scene, nullptr, backBuffer, DirectXSystem::GetBackBufferWidth(), DirectXSystem::GetBackBufferHeight(), false);
    }
    Pass(context, g_PSComposite.Get(), g_Scene, g_Bloom[0].srv.Get(), backBuffer, gw, gh, false, gx, gy);

    // leave the device as the rest of the frame expects it: the back buffer bound, no texture of ours bound, default states
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, none);
    context->OMSetRenderTargets(1, &backBuffer, nullptr);
    SetViewport(context, gw, gh, gx, gy);
    context->RSSetState(nullptr);
    g_Look = DefaultLook(); // (the next frame sets its own look, or keeps the default)
}

void PostFx::SetLook(const Look& look)
{
    g_Look = look;
}

PostFx::Look PostFx::DefaultLook()
{
    return Look{};
}

void PostFx::Kick(float aberration)
{
    aberration *= static_cast<float>(GameSettings::Get().flashPercent) / 100.0f; // (settings: FLASHES)
    if (aberration > g_Kick) g_Kick = aberration;
}





