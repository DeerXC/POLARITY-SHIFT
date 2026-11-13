#include "core/Renderer2D.h"
#include "core/DirectX.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <vector>
#include <fstream>
#include <cmath>
#include <cstring>

#include "core/TextureLoader.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

struct Vertex
{
    float position[3];
    float color[4];
    float texcoord[2];
};

namespace
{
ComPtr<ID3D11VertexShader> g_VertexShader;

ComPtr<ID3D11PixelShader> g_PixelShader;
ComPtr<ID3D11PixelShader> g_PatternShader; // (optional: the backdrop patterns; null when shader_pattern_2d.cso is missing)
ComPtr<ID3D11Buffer> g_PatternBuffer;

ComPtr<ID3D11InputLayout> g_InputLayout;

ComPtr<ID3D11Buffer> g_VertexBuffer;

ComPtr<ID3D11ShaderResourceView> g_WhiteTextureSRV;

ComPtr<ID3D11SamplerState> g_SamplerState;

ComPtr<ID3D11BlendState> g_AlphaBlendState;

ComPtr<ID3D11BlendState> g_AdditiveBlendState;

Renderer2DBlendMode g_CurrentBlendMode = Renderer2DBlendMode::Alpha;

float g_CameraOffsetX = 0.0f;
float g_CameraOffsetY = 0.0f;

constexpr int TEXTURE_MAX = 64;

ComPtr<ID3D11ShaderResourceView> g_Textures[TEXTURE_MAX];
}

std::vector<char> LoadBinaryFile(const char* fileName)
{
    std::ifstream file(fileName, std::ios::binary | std::ios::ate);

    if (!file)
    {
        return {};
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);

    if (!file.read(buffer.data(), fileSize))
    {
        return {};
    }

    return buffer;
}

bool CreateShaders()
{
    std::vector<char> vertexShaderData = LoadBinaryFile("shader_vertex_2d.cso");
    std::vector<char> pixelShaderData = LoadBinaryFile("shader_pixel_2d.cso");

    if (vertexShaderData.empty())
    {
        MessageBoxA(nullptr, "shader_vertex_2d.cso not found.", "Error", MB_OK);
        return false;
    }

    if (pixelShaderData.empty())
    {
        MessageBoxA(nullptr, "shader_pixel_2d.cso not found.", "Error", MB_OK);
        return false;
    }

    ID3D11Device* device = DirectXSystem::GetDevice();

    HRESULT hr =
        device->CreateVertexShader(vertexShaderData.data(), vertexShaderData.size(), nullptr, g_VertexShader.GetAddressOf());

    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "CreateVertexShader failed.", "Error", MB_OK);
        return false;
    }

    hr = device->CreatePixelShader(pixelShaderData.data(), pixelShaderData.size(), nullptr, g_PixelShader.GetAddressOf());

    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "CreatePixelShader failed.", "Error", MB_OK);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0}};

    hr = device->CreateInputLayout(inputLayoutDesc, ARRAYSIZE(inputLayoutDesc), vertexShaderData.data(), vertexShaderData.size(),
                                   g_InputLayout.GetAddressOf());

    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "CreateInputLayout failed.", "Error", MB_OK);
        return false;
    }

    return true;
}

bool CreateVertexBuffer()
{
    Vertex vertices[] = {
        {{-0.1f, 0.1f, 0.0f}, {1, 1, 1, 1}, {0, 0}},
        {{0.1f, 0.1f, 0.0f}, {1, 1, 1, 1}, {1, 0}},
        {{-0.1f, -0.1f, 0.0f}, {1, 1, 1, 1}, {0, 1}},
        {{0.1f, -0.1f, 0.0f}, {1, 1, 1, 1}, {1, 1}},
    };

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA subresourceData{};
    subresourceData.pSysMem = vertices;

    HRESULT hr = DirectXSystem::GetDevice()->CreateBuffer(&bufferDesc, &subresourceData, g_VertexBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "CreateBuffer failed.", "Error", MB_OK);
        return false;
    }

    return true;
}

bool CreateWhiteTexture()
{
    unsigned int whitePixel = 0xffffffff;

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = 1;
    textureDesc.Height = 1;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA textureData{};
    textureData.pSysMem = &whitePixel;
    textureData.SysMemPitch = sizeof(unsigned int);

    ComPtr<ID3D11Texture2D> whiteTexture;

    ID3D11Device* device = DirectXSystem::GetDevice();

    HRESULT hr = device->CreateTexture2D(&textureDesc, &textureData, whiteTexture.GetAddressOf());

    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "CreateTexture2D failed.", "Error", MB_OK);
        return false;
    }

    hr = device->CreateShaderResourceView(whiteTexture.Get(), nullptr, g_WhiteTextureSRV.GetAddressOf());

    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "CreateShaderResourceView failed.", "Error", MB_OK);
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    samplerDesc.MipLODBias = -0.4f;

    hr = device->CreateSamplerState(&samplerDesc, g_SamplerState.GetAddressOf());

    if (FAILED(hr))
    {
        MessageBoxA(nullptr, "CreateSamplerState failed.", "Error", MB_OK);
        return false;
    }

    return true;
}

void ApplyBlendStateFor(Renderer2DBlendMode mode)
{
    ID3D11BlendState* blendState = g_AlphaBlendState.Get();

    if (mode == Renderer2DBlendMode::Additive)
    {
        blendState = g_AdditiveBlendState.Get();
    }

    const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    DirectXSystem::GetContext()->OMSetBlendState(blendState, blendFactor, 0xffffffff);
}

void ApplyCurrentBlendState()
{
    ApplyBlendStateFor(g_CurrentBlendMode);
}

// ---------------------------------------------------------------------------------------------------------------------
// Batch. Every draw call of the game (a sprite, a quad, a polygon) is turned into triangles and queued. A run of draws that use the same
// texture and the same blend mode is sent to the GPU with one draw call, in the order in which it was drawn.
//
// ADDITIVE draws are grouped by texture: with additive blending a sprite adds (colour x alpha) to what is on the screen, and a sum does not depend
// on the order of its terms, so the draws of one run of additive blending can be sent texture by texture. The effects of the game (glow, spark,
// ring, flame ...) switch texture on nearly every sprite: in the order of drawing they were 50 - 230 batches a frame, grouped they are a handful.
//
// The vertex buffer is filled as a ring: every batch is appended behind the last one (MAP_WRITE_NO_OVERWRITE) and only a full buffer is thrown
// away (MAP_WRITE_DISCARD). The pipeline state (shaders, input layout, sampler, buffer) is bound by the first batch of a frame; a batch only sets
// the texture and the blend state when they change.
namespace
{
constexpr int POLYGON_VERTEX_MAX = 32;
constexpr int BATCH_CAPACITY = 24576; // vertices of the dynamic buffer (a multiple of 3)
std::vector<Vertex> g_Batch;
ID3D11ShaderResourceView* g_BatchTexture = nullptr;
Renderer2DBlendMode g_BatchBlend = Renderer2DBlendMode::Alpha;
ComPtr<ID3D11Buffer> g_BatchBuffer;
UINT g_BufferUsed = BATCH_CAPACITY; // vertices written into the buffer since its last DISCARD (full at first: the first map discards)

// the state that is bound on the device context (invalid after Renderer2D_Flush: the text renderer draws with its own state)
bool g_PipelineBound = false;
ID3D11PixelShader* g_BatchPixelShader = nullptr; // the pixel shader of the batch that is sent next (null: the sprite shader)
ID3D11PixelShader* g_BoundPixelShader = nullptr;
ID3D11ShaderResourceView* g_BoundTexture = nullptr;
int g_BoundBlend = -1;

struct AdditiveBucket
{
    ID3D11ShaderResourceView* texture = nullptr;
    std::vector<Vertex> vertices;
};
constexpr int ADDITIVE_BUCKET_MAX = 16;
AdditiveBucket g_Additive[ADDITIVE_BUCKET_MAX];
int g_AdditiveCount = 0; // buckets in use in this run of additive draws

bool EnsureBatchBuffer()
{
    if (g_BatchBuffer != nullptr)
    {
        return true;
    }

    ID3D11Device* device = DirectXSystem::GetDevice();
    if (device == nullptr)
    {
        return false;
    }

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * BATCH_CAPACITY);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (g_Batch.capacity() < static_cast<size_t>(BATCH_CAPACITY))
    {
        g_Batch.reserve(BATCH_CAPACITY);
    }

    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, g_BatchBuffer.GetAddressOf()));
}

void FlushBatch()
{
    if (g_Batch.empty())
    {
        return;
    }

    ID3D11DeviceContext* context = DirectXSystem::GetContext();

    if (context == nullptr || g_BatchTexture == nullptr || !EnsureBatchBuffer())
    {
        g_Batch.clear();
        return;
    }

    if (!g_PipelineBound)
    {
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ID3D11Buffer* vertexBuffer = g_BatchBuffer.Get();
        ID3D11SamplerState* sampler = g_SamplerState.Get();
        context->IASetInputLayout(g_InputLayout.Get());
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(g_VertexShader.Get(), nullptr, 0);
        context->PSSetShader(g_PixelShader.Get(), nullptr, 0);
        context->PSSetSamplers(0, 1, &sampler);
        g_PipelineBound = true;
        g_BoundPixelShader = g_PixelShader.Get();
        g_BoundTexture = nullptr;
        g_BoundBlend = -1;
    }
    {
        ID3D11PixelShader* shader = g_BatchPixelShader != nullptr ? g_BatchPixelShader : g_PixelShader.Get();
        if (g_BoundPixelShader != shader)
        {
            context->PSSetShader(shader, nullptr, 0);
            g_BoundPixelShader = shader;
        }
    }
    if (g_BoundTexture != g_BatchTexture)
    {
        ID3D11ShaderResourceView* texture = g_BatchTexture;
        context->PSSetShaderResources(0, 1, &texture);
        g_BoundTexture = g_BatchTexture;
    }
    if (g_BoundBlend != static_cast<int>(g_BatchBlend))
    {
        ApplyBlendStateFor(g_BatchBlend);
        g_BoundBlend = static_cast<int>(g_BatchBlend);
    }

    size_t done = 0;
    const size_t total = g_Batch.size();

    while (done < total)
    {
        size_t chunk = total - done;
        if (chunk > static_cast<size_t>(BATCH_CAPACITY))
        {
            chunk = static_cast<size_t>(BATCH_CAPACITY);
        }
        chunk -= chunk % 3;
        if (chunk == 0)
        {
            break;
        }

        // append behind what the GPU may still be drawing from; start over (and let the driver hand out a fresh buffer) only when it is full
        D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
        if (g_BufferUsed + chunk > static_cast<size_t>(BATCH_CAPACITY))
        {
            mode = D3D11_MAP_WRITE_DISCARD;
            g_BufferUsed = 0;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(g_BatchBuffer.Get(), 0, mode, 0, &mapped)))
        {
            break;
        }

        memcpy(static_cast<Vertex*>(mapped.pData) + g_BufferUsed, g_Batch.data() + done, sizeof(Vertex) * chunk);
        context->Unmap(g_BatchBuffer.Get(), 0);
        context->Draw(static_cast<UINT>(chunk), g_BufferUsed);
        g_BufferUsed += static_cast<UINT>(chunk);
        done += chunk;
    }

    g_Batch.clear();
}

// Sends the additive draws that are waiting, one batch per texture (in the order in which each texture was first used).
void FlushAdditive()
{
    for (int i = 0; i < g_AdditiveCount; i++)
    {
        AdditiveBucket& bucket = g_Additive[i];
        if (bucket.vertices.empty())
        {
            continue;
        }
        g_Batch.swap(bucket.vertices); // (the swaps keep the memory of both vectors: nothing is allocated from frame to frame)
        g_BatchTexture = bucket.texture;
        g_BatchBlend = Renderer2DBlendMode::Additive;
        FlushBatch();
        g_Batch.swap(bucket.vertices);
    }
    g_AdditiveCount = 0;
}

// The next `extraVertices` vertices go into the batch: send the batch first when they do not belong to it.
void PrepareBatch(ID3D11ShaderResourceView* texture, size_t extraVertices)
{
    if (!g_Batch.empty()
        && (g_BatchTexture != texture || g_BatchBlend != g_CurrentBlendMode
            || g_Batch.size() + extraVertices > static_cast<size_t>(BATCH_CAPACITY)))
    {
        FlushBatch();
    }

    if (g_Batch.empty())
    {
        g_BatchTexture = texture;
        g_BatchBlend = g_CurrentBlendMode;
    }
}

// Where the next draw goes: an additive draw goes into the bucket of its texture (the draws in the order queue before it are sent first),
// any other draw goes into the batch (the additive draws that wait are sent first: they were drawn before it).
std::vector<Vertex>& BatchFor(ID3D11ShaderResourceView* texture, size_t extraVertices)
{
    if (g_CurrentBlendMode == Renderer2DBlendMode::Additive)
    {
        if (!g_Batch.empty())
        {
            FlushBatch();
        }
        for (int i = 0; i < g_AdditiveCount; i++)
        {
            if (g_Additive[i].texture == texture)
            {
                return g_Additive[i].vertices;
            }
        }
        if (g_AdditiveCount == ADDITIVE_BUCKET_MAX)
        {
            FlushAdditive();
        }
        AdditiveBucket& bucket = g_Additive[g_AdditiveCount++];
        bucket.texture = texture;
        bucket.vertices.clear();
        return bucket.vertices;
    }

    FlushAdditive();
    PrepareBatch(texture, extraVertices);
    return g_Batch;
}

// Triangle fan of a convex polygon. Front faces are clockwise (y up), so a counter-clockwise polygon is walked backwards.
void AppendFan(std::vector<Vertex>& batch, const Renderer2DVertex* source, int count)
{
    float area = 0.0f;
    for (int i = 0; i < count; i++)
    {
        const Renderer2DVertex& a = source[i];
        const Renderer2DVertex& b = source[(i + 1) % count];
        area += a.x * b.y - b.x * a.y;
    }

    int order[POLYGON_VERTEX_MAX];
    for (int i = 0; i < count; i++)
    {
        order[i] = area > 0.0f ? count - 1 - i : i;
    }

    auto make = [&](int index)
    {
        const Renderer2DVertex& s = source[index];
        Vertex v{};
        v.position[0] = s.x + g_CameraOffsetX;
        v.position[1] = s.y + g_CameraOffsetY;
        v.position[2] = 0.0f;
        v.color[0] = s.r;
        v.color[1] = s.g;
        v.color[2] = s.b;
        v.color[3] = s.a;
        v.texcoord[0] = s.u;
        v.texcoord[1] = s.v;
        return v;
    };

    for (int k = 1; k + 1 < count; k++)
    {
        batch.push_back(make(order[0]));
        batch.push_back(make(order[k]));
        batch.push_back(make(order[k + 1]));
    }
}
}

// A quad (vertices in triangle strip order: top left, top right, bottom left, bottom right) is queued as two triangles.
void DrawVertices(const Vertex sourceVertices[4], ID3D11ShaderResourceView* texture)
{
    if (texture == nullptr)
    {
        return;
    }

    std::vector<Vertex>& batch = BatchFor(texture, 6);

    Vertex vertices[4];

    for (int i = 0; i < 4; i++)
    {
        vertices[i] = sourceVertices[i];

        vertices[i].position[0] += g_CameraOffsetX;

        vertices[i].position[1] += g_CameraOffsetY;
    }

    batch.push_back(vertices[0]);
    batch.push_back(vertices[1]);
    batch.push_back(vertices[2]);
    batch.push_back(vertices[1]);
    batch.push_back(vertices[3]);
    batch.push_back(vertices[2]);
}

bool CreateBlendState()
{
    ID3D11Device* device = DirectXSystem::GetDevice();

    if (device == nullptr)
    {
        return false;
    }

    // ==========================================
    // Alpha blending
    // ==========================================

    D3D11_BLEND_DESC alphaDesc{};

    alphaDesc.RenderTarget[0].BlendEnable = TRUE;

    alphaDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; // (premultiplied colour: the shader has multiplied it by alpha)

    alphaDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

    alphaDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

    alphaDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;

    alphaDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;

    alphaDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

    alphaDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT result = device->CreateBlendState(&alphaDesc, g_AlphaBlendState.GetAddressOf());

    if (FAILED(result))
    {
        MessageBoxA(nullptr, "Create Alpha BlendState failed.", "Renderer2D Error", MB_OK | MB_ICONERROR);

        return false;
    }

    // ==========================================
    // Additive blending
    // ==========================================

    D3D11_BLEND_DESC additiveDesc{};

    additiveDesc.RenderTarget[0].BlendEnable = TRUE;

    additiveDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; // (premultiplied: light adds colour x alpha)

    additiveDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

    additiveDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

    additiveDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;

    additiveDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

    additiveDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

    additiveDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    result = device->CreateBlendState(&additiveDesc, g_AdditiveBlendState.GetAddressOf());

    if (FAILED(result))
    {
        MessageBoxA(nullptr, "Create Additive BlendState failed.", "Renderer2D Error", MB_OK | MB_ICONERROR);

        return false;
    }

    g_CurrentBlendMode = Renderer2DBlendMode::Alpha;

    ApplyCurrentBlendState();

    return true;
}

static bool CreatePatternShader(); // (below, with Renderer2D_DrawPolygonPattern)

bool Renderer2D_Initialize()
{
    if (!CreateShaders())
    {
        return false;
    }

    CreatePatternShader(); // optional: without it the menus draw their patterns with polygons

    if (!CreateVertexBuffer())
    {
        return false;
    }

    if (!CreateWhiteTexture())
    {
        return false;
    }

    if (!CreateBlendState())
    {
        return false;
    }

    return true;
}

int Renderer2D_LoadTexture(const wchar_t* fileName)
{
    if (fileName == nullptr)
    {
        return -1;
    }

    for (int i = 0; i < TEXTURE_MAX; i++)
    {
        if (g_Textures[i] != nullptr)
        {
            continue;
        }

        const HRESULT result = TextureLoader::LoadFromFile(DirectXSystem::GetDevice(), DirectXSystem::GetContext(), fileName,
                                                           g_Textures[i].GetAddressOf());

        if (FAILED(result))
        {
            g_Textures[i].Reset();
            return -1;
        }

        return i;
    }

    return -1;
}

void Renderer2D_Finalize()
{
    g_Batch.clear();
    g_Batch.shrink_to_fit();
    g_BatchTexture = nullptr;
    g_BatchBuffer.Reset();
    g_BufferUsed = BATCH_CAPACITY;
    g_PipelineBound = false;
    for (AdditiveBucket& bucket : g_Additive)
    {
        bucket = AdditiveBucket{};
    }
    g_AdditiveCount = 0;

    for (int i = 0; i < TEXTURE_MAX; i++)
    {
        g_Textures[i].Reset();
    }

    g_AdditiveBlendState.Reset();
    g_AlphaBlendState.Reset();

    g_SamplerState.Reset();
    g_WhiteTextureSRV.Reset();
    TextureLoader::Shutdown();

    g_VertexBuffer.Reset();
    g_InputLayout.Reset();

    g_PixelShader.Reset();
    g_PatternShader.Reset();
    g_PatternBuffer.Reset();
    g_VertexShader.Reset();

    g_CurrentBlendMode = Renderer2DBlendMode::Alpha;

    g_CameraOffsetX = 0.0f;
    g_CameraOffsetY = 0.0f;
}

void Renderer2D_SetBlendMode(Renderer2DBlendMode mode)
{
    g_CurrentBlendMode = mode; // part of the batch key (PrepareBatch): the batch is sent when it changes
}

Renderer2DBlendMode Renderer2D_GetBlendMode()
{
    return g_CurrentBlendMode;
}

void Renderer2D_SetCameraOffset(float offsetX, float offsetY)
{
    g_CameraOffsetX = offsetX;

    g_CameraOffsetY = offsetY;
}

void Renderer2D_ClearCameraOffset()
{
    g_CameraOffsetX = 0.0f;
    g_CameraOffsetY = 0.0f;
}

void Renderer2D_DrawQuad(float left, float top, float right, float bottom, float r, float g, float b, float a)
{
    const Vertex vertices[4] = {{{left, top, 0.0f}, {r, g, b, a}, {0.0f, 0.0f}},
                                {{right, top, 0.0f}, {r, g, b, a}, {1.0f, 0.0f}},
                                {{left, bottom, 0.0f}, {r, g, b, a}, {0.0f, 1.0f}},
                                {{right, bottom, 0.0f}, {r, g, b, a}, {1.0f, 1.0f}}};

    DrawVertices(vertices, g_WhiteTextureSRV.Get());
}

void Renderer2D_DrawTexture(int textureId, float left, float top, float right, float bottom, float r, float g, float b, float a,
                            float u0, float v0, float u1, float v1)
{
    if (textureId < 0 || textureId >= TEXTURE_MAX)
    {
        return;
    }

    if (g_Textures[textureId] == nullptr)
    {
        return;
    }

    const Vertex vertices[4] = {{{left, top, 0.0f}, {r, g, b, a}, {u0, v0}},
                                {{right, top, 0.0f}, {r, g, b, a}, {u1, v0}},
                                {{left, bottom, 0.0f}, {r, g, b, a}, {u0, v1}},
                                {{right, bottom, 0.0f}, {r, g, b, a}, {u1, v1}}};

    DrawVertices(vertices, g_Textures[textureId].Get());
}

void Renderer2D_DrawTextureEx(int textureId, float centerX, float centerY, float width, float height, float rotationRadians,
                              float r, float g, float b, float a, float u0, float v0, float u1, float v1)
{
    if (textureId < 0 || textureId >= TEXTURE_MAX)
    {
        return;
    }

    if (g_Textures[textureId] == nullptr)
    {
        return;
    }

    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    const float halfWidth = width * 0.5f;

    const float halfHeight = height * 0.5f;

    const float cosine = std::cos(rotationRadians);

    const float sine = std::sin(rotationRadians);

    // ==========================================
    // Aspect-correct rotation
    //
    // X and Y do not represent the same number
    // of screen pixels. Convert to an equal-scale
    // coordinate system before rotating.
    // ==========================================

    auto rotatePoint = [centerX, centerY, cosine, sine](float localX, float localY, float& outputX, float& outputY)
    {
        constexpr float ASPECT = 720.0f / 1280.0f;

        outputX = centerX + localX * cosine - localY * ASPECT * sine;

        outputY = centerY + localX / ASPECT * sine + localY * cosine;
    };

    float topLeftX = 0.0f;

    float topLeftY = 0.0f;

    float topRightX = 0.0f;

    float topRightY = 0.0f;

    float bottomLeftX = 0.0f;

    float bottomLeftY = 0.0f;

    float bottomRightX = 0.0f;

    float bottomRightY = 0.0f;

    rotatePoint(-halfWidth, halfHeight, topLeftX, topLeftY);

    rotatePoint(halfWidth, halfHeight, topRightX, topRightY);

    rotatePoint(-halfWidth, -halfHeight, bottomLeftX, bottomLeftY);

    rotatePoint(halfWidth, -halfHeight, bottomRightX, bottomRightY);

    const Vertex vertices[4] = {{{topLeftX, topLeftY, 0.0f}, {r, g, b, a}, {u0, v0}},
                                {{topRightX, topRightY, 0.0f}, {r, g, b, a}, {u1, v0}},
                                {{bottomLeftX, bottomLeftY, 0.0f}, {r, g, b, a}, {u0, v1}},
                                {{bottomRightX, bottomRightY, 0.0f}, {r, g, b, a}, {u1, v1}}};

    DrawVertices(vertices, g_Textures[textureId].Get());
}

void Renderer2D_BindExternalTexture(ID3D11ShaderResourceView* texture)
{
    if (g_Textures[RENDERER2D_EXTERNAL_TEXTURE_ID].Get() != texture)
    {
        FlushAdditive(); // the queued draws may still point at the old texture
        FlushBatch();
        g_Textures[RENDERER2D_EXTERNAL_TEXTURE_ID] = texture;
    }
}

void Renderer2D_DrawPolygon(const Renderer2DVertex* source, int count)
{
    if (source == nullptr || count < 3 || count > POLYGON_VERTEX_MAX || g_WhiteTextureSRV == nullptr)
    {
        return;
    }

    AppendFan(BatchFor(g_WhiteTextureSRV.Get(), static_cast<size_t>(count - 2) * 3), source, count);
}

void Renderer2D_DrawPolygonTextured(int textureId, const Renderer2DVertex* source, int count)
{
    if (source == nullptr || count < 3 || count > POLYGON_VERTEX_MAX || textureId < 0 || textureId >= TEXTURE_MAX)
    {
        return;
    }
    if (g_Textures[textureId] == nullptr)
    {
        return;
    }

    AppendFan(BatchFor(g_Textures[textureId].Get(), static_cast<size_t>(count - 2) * 3), source, count);
}

// the constants of shader_pattern_2d.hlsl (cbuffer Pattern)
struct PatternConstants
{
    float view[4];
    float axes[4];
    float normal[4];
    float color[4];
    float color2[4];
};

static bool CreatePatternShader()
{
    const std::vector<char> code = LoadBinaryFile("shader_pattern_2d.cso");
    if (code.empty()) return false; // (optional: the menus draw the patterns another way)
    ID3D11Device* device = DirectXSystem::GetDevice();
    if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, g_PatternShader.GetAddressOf()))) return false;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(PatternConstants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&desc, nullptr, g_PatternBuffer.GetAddressOf())))
    {
        g_PatternShader.Reset();
        return false;
    }
    return true;
}

bool Renderer2D_DrawPolygonPattern(const Renderer2DVertex* source, int count, const Renderer2DPattern& pattern)
{
    if (g_PatternShader == nullptr || g_PatternBuffer == nullptr || g_WhiteTextureSRV == nullptr) return false;
    if (source == nullptr || count < 3 || count > POLYGON_VERTEX_MAX) return true; // (nothing to draw)
    ID3D11DeviceContext* context = DirectXSystem::GetContext();
    if (context == nullptr) return false;

    // what was drawn before goes first (the pattern is a batch of its own)
    FlushAdditive();
    FlushBatch();
    const Renderer2DBlendMode saved = g_CurrentBlendMode;
    g_CurrentBlendMode = Renderer2DBlendMode::Alpha;
    AppendFan(BatchFor(g_WhiteTextureSRV.Get(), static_cast<size_t>(count - 2) * 3), source, count);
    g_CurrentBlendMode = saved;

    // the viewport maps the virtual UI space onto the render target (the HDR scene target or the 16:9 rectangle of the back buffer)
    D3D11_VIEWPORT viewport{};
    UINT viewports = 1;
    context->RSGetViewports(&viewports, &viewport);
    PatternConstants k{};
    k.view[0] = viewport.TopLeftX;
    k.view[1] = viewport.TopLeftY;
    k.view[2] = viewport.Width > 0.0f ? viewport.Width / 1280.0f : 1.0f;
    k.view[3] = pattern.kind == Renderer2DPatternKind::Glass ? 1.0f : 0.0f;
    k.axes[0] = pattern.originX;
    k.axes[1] = pattern.originY;
    k.axes[2] = pattern.alongX;
    k.axes[3] = pattern.alongY;
    k.normal[0] = pattern.normalX;
    k.normal[1] = pattern.normalY;
    k.normal[2] = pattern.step;
    k.normal[3] = pattern.reach;
    k.color[0] = pattern.r;
    k.color[1] = pattern.g;
    k.color[2] = pattern.b;
    k.color[3] = pattern.a;
    k.color2[0] = pattern.r2;
    k.color2[1] = pattern.g2;
    k.color2[2] = pattern.b2;
    k.color2[3] = pattern.radius;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(g_PatternBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        g_Batch.clear();
        return false;
    }
    memcpy(mapped.pData, &k, sizeof(k));
    context->Unmap(g_PatternBuffer.Get(), 0);
    ID3D11Buffer* buffer = g_PatternBuffer.Get();
    context->PSSetConstantBuffers(0, 1, &buffer);

    g_BatchPixelShader = g_PatternShader.Get();
    FlushBatch();
    g_BatchPixelShader = nullptr;
    return true;
}

void Renderer2D_Flush()
{
    FlushAdditive();
    FlushBatch();
    g_PipelineBound = false; // what is drawn next (the text) sets its own state
}

