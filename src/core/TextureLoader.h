#pragma once

#include <d3d11.h>

// TextureLoader: reads an image file (PNG, also JPG / BMP) into a D3D11 texture, written for this game instead of DirectXTK's
// WICTextureLoader (the game needs one case of it: an 8-bit RGBA sprite from disk).
//
//   * decoding: the Windows Imaging Component (WIC, part of Windows) - no extra library
//   * the pixels are converted to PREMULTIPLIED alpha while they are decoded (32bppPRGBA). Renderer2D blends premultiplied colour
//     (ONE, INV_SRC_ALPHA): the texels at the edge of a sprite are filtered without the dark / bright fringe that straight alpha has
//   * a full mip chain is generated on the GPU, so sprites drawn smaller than their pixels (enemies at 1280 x 720) stay smooth
//
// CoInitializeEx must have been called on the calling thread (main.cpp does it).
namespace TextureLoader
{
struct Info
{
    unsigned int width = 0;
    unsigned int height = 0;
};

// Returns S_OK and a shader resource view on success; on failure the HRESULT of the step that failed and *view stays null.
HRESULT LoadFromFile(ID3D11Device* device, ID3D11DeviceContext* context, const wchar_t* fileName, ID3D11ShaderResourceView** view,
                     Info* info = nullptr);

// The WIC factory is created on the first load and kept; call this before COM is shut down.
void Shutdown();
}
