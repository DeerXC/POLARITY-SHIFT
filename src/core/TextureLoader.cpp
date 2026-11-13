#include "core/TextureLoader.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <vector>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace
{
ComPtr<IWICImagingFactory> g_Factory;

// the GPU limit of a 2D texture in feature level 11 (a larger file is refused rather than scaled)
constexpr UINT MAX_SIZE = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

HRESULT EnsureFactory()
{
    if (g_Factory != nullptr) return S_OK;
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(g_Factory.GetAddressOf()));
}

// Decodes the first frame of the file into tightly packed premultiplied RGBA8 rows.
HRESULT Decode(const wchar_t* fileName, std::vector<unsigned char>& pixels, UINT& width, UINT& height)
{
    HRESULT hr = EnsureFactory();
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = g_Factory->CreateDecoderFromFilename(fileName, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = frame->GetSize(&width, &height);
    if (FAILED(hr)) return hr;
    if (width == 0 || height == 0 || width > MAX_SIZE || height > MAX_SIZE) return E_INVALIDARG;

    // whatever the file holds (palette, RGB, 16 bit ...), WIC converts it to 8-bit RGBA with the alpha already multiplied in
    ComPtr<IWICFormatConverter> converter;
    hr = g_Factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    const UINT stride = width * 4;
    pixels.resize(static_cast<size_t>(stride) * height);
    return converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
}
}

HRESULT TextureLoader::LoadFromFile(ID3D11Device* device, ID3D11DeviceContext* context, const wchar_t* fileName, ID3D11ShaderResourceView** view,
                                    Info* info)
{
    if (view == nullptr) return E_POINTER;
    *view = nullptr;
    if (device == nullptr || context == nullptr || fileName == nullptr) return E_INVALIDARG;

    std::vector<unsigned char> pixels;
    UINT width = 0, height = 0;
    HRESULT hr = Decode(fileName, pixels, width, height);
    if (FAILED(hr)) return hr;

    // the texture: a full mip chain that the GPU fills from level 0 (GENERATE_MIPS needs the texture to be a render target too)
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0; // (0 = all levels down to 1 x 1)
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> texture;
    hr = device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf());
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels = static_cast<UINT>(-1);
    ComPtr<ID3D11ShaderResourceView> srv;
    hr = device->CreateShaderResourceView(texture.Get(), &viewDesc, srv.GetAddressOf());
    if (FAILED(hr)) return hr;

    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), width * 4, 0);
    context->GenerateMips(srv.Get());

    if (info != nullptr)
    {
        info->width = width;
        info->height = height;
    }
    *view = srv.Detach();
    return S_OK;
}

void TextureLoader::Shutdown()
{
    g_Factory.Reset();
}
