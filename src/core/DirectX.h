#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace DirectXSystem
{
bool Initialize(HWND hWnd, int width, int height);
void Finalize();
bool Resize(int width, int height);
void BeginFrame(float r, float g, float b, float a);
void EndFrame();
int GetBackBufferWidth();
int GetBackBufferHeight();
// The game is drawn 16:9. On a screen of another shape (16:10 laptops, 21:9) the back buffer is the whole screen and the game is the
// largest 16:9 rectangle in its middle; PostFx fills the rest with a soft, dark extension of the picture (no black bars).
// x, y, w, h in back buffer pixels. On a 16:9 back buffer it is the whole buffer.
void GetGameRect(int& x, int& y, int& w, int& h);
bool IsGameRectFull();
ID3D11RenderTargetView* GetBackBufferRTV(); // (PostFx draws its last pass onto it)
ID3D11Device* GetDevice();
ID3D11DeviceContext* GetContext();
IDXGISwapChain* GetSwapChain();
bool IsFlipModel(); // the swap chain uses the flip model (config.ini: flip_present=1)
const char* GetAdapterName(); // graphics chip that draws the game (UTF-8), shown by the FPS overlay

ID3D11ShaderResourceView* GetSnapshotSRV();
bool HasSnapshot();
void SetSnapshotFrozen(bool frozen);
}

