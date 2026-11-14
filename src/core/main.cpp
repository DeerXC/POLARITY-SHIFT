#include <Windows.h>
#include <imm.h>
#include <combaseapi.h>

#include <thread>

#include "core/DirectX.h"
#include "core/Renderer2D.h"
#include "game/Game.h"
#include "core/AssetManager.h"
#include "resource.h"
#include "core/GameTimer.h"
#include "core/GameInput.h"
#include "core/GameAudio.h"
#include "core/DisplaySystem.h"
#include "game/GameSave.h"
#include "core/TextRenderer.h"
#include "core/PostFx.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "imm32.lib")

namespace
{
constexpr int SCREEN_WIDTH = 1280;
constexpr int SCREEN_HEIGHT = 720;

constexpr int MAX_UPDATES_PER_FRAME = 5;

constexpr char WINDOW_CLASS[] = "DirectXShootingWindow";

constexpr char WINDOW_TITLE[] = "POLARITY SHIFT";

// the arrow, or no cursor while the game hides it (the arrow is loaded once)
void ApplyGameCursor()
{
    static const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    SetCursor(Game_ShouldHideMouseCursor() ? nullptr : arrow);
}
}

// A laptop with two graphics chips (integrated + dedicated) runs a game on the slow one unless the program asks for the fast one.
// (DirectXSystem::Initialize also picks the high performance adapter by itself.)
extern "C"
{
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

LRESULT CALLBACK WndProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

int APIENTRY WinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE previousInstance, _In_ LPSTR commandLine, _In_ int showCommand)
{
    SetProcessDPIAware();

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ImmDisableIME(GetCurrentThreadId());

    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXA);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = nullptr;
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = WINDOW_CLASS;

    windowClass.hIcon = static_cast<HICON>(LoadImageA(instance, MAKEINTRESOURCEA(IDI_APP), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));

    windowClass.hIconSm =
        static_cast<HICON>(LoadImageA(instance, MAKEINTRESOURCEA(IDI_APP), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));

    if (RegisterClassExA(&windowClass) == 0)
    {
        MessageBoxA(nullptr, "RegisterClassEx failed.", "Error", MB_OK | MB_ICONERROR);

        if (SUCCEEDED(comResult))
        {
            CoUninitialize();
        }

        return 0;
    }

    RECT windowRect{};
    windowRect.left = 0;
    windowRect.top = 0;
    windowRect.right = SCREEN_WIDTH;
    windowRect.bottom = SCREEN_HEIGHT;

    const DWORD windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    const int windowWidth = windowRect.right - windowRect.left;

    const int windowHeight = windowRect.bottom - windowRect.top;

    HWND windowHandle = CreateWindowA(WINDOW_CLASS, WINDOW_TITLE, windowStyle, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth,
                                      windowHeight, nullptr, nullptr, instance, nullptr);

    if (windowHandle == nullptr)
    {
        MessageBoxA(nullptr, "CreateWindow failed.", "Error", MB_OK | MB_ICONERROR);

        if (SUCCEEDED(comResult))
        {
            CoUninitialize();
        }

        return 0;
    }

    if (!DirectXSystem::Initialize(windowHandle, SCREEN_WIDTH, SCREEN_HEIGHT))
    {
        if (SUCCEEDED(comResult))
        {
            CoUninitialize();
        }

        return 0;
    }

    DisplaySystem::Initialize(windowHandle);

    if (!Renderer2D_Initialize())
    {
        DirectXSystem::Finalize();

        if (SUCCEEDED(comResult))
        {
            CoUninitialize();
        }

        return 0;
    }

    if (!TextRenderer::Initialize())
    {
        Renderer2D_Finalize();
        DirectXSystem::Finalize();

        if (SUCCEEDED(comResult))
        {
            CoUninitialize();
        }

        return 0;
    }

    PostFx::Initialize(); // (not fatal: without it the picture goes straight to the screen)

    if (!AssetManager::Initialize())
    {
        TextRenderer::Finalize();
        Renderer2D_Finalize();
        DirectXSystem::Finalize();

        if (SUCCEEDED(comResult))
        {
            CoUninitialize();
        }

        return 0;
    }

    GameInput::Initialize(windowHandle);
    GameAudio::Initialize();

    Game_Initialize();

    const GameRecordData& recordData = GameSave::GetRecordData();

    DisplayMode savedDisplayMode = DisplayMode::Windowed;

    if (recordData.displayMode == 1)
    {
        savedDisplayMode = DisplayMode::Fullscreen;
    }

    DisplaySystem::Apply(savedDisplayMode, recordData.resolutionIndex);

    GameTimer::Initialize();

    ShowWindow(windowHandle, showCommand);

    UpdateWindow(windowHandle);

    MSG message{};
    bool isRunning = true;

    while (isRunning)
    {
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                isRunning = false;
                break;
            }

            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        if (!isRunning)
        {
            break;
        }

        GameTimer::StatsFrameBegin();
        GameTimer::BeginFrame();

        int updateCount = 0;

        while (GameTimer::ShouldUpdate() && updateCount < MAX_UPDATES_PER_FRAME)
        {
            GameInput::Update();

            Game_Update();

            ApplyGameCursor();

            GameTimer::ConsumeUpdate();

            updateCount++;
        }

        DirectXSystem::BeginFrame(0.02f, 0.03f, 0.08f, 1.0f);

        PostFx::BeginScene(0.02f, 0.03f, 0.08f); // the world goes into the HDR target (when the post effects are on)

        TextRenderer::BeginFrame();

        Game_Draw();

        Renderer2D_Flush(); // polygons that are still queued must be on the screen before the text is drawn over them

        PostFx::EndScene(); // bloom, grade, lens: onto the back buffer (the text is drawn after it and stays sharp)

        TextRenderer::Render();

        GameTimer::StatsBeforePresent();

        DirectXSystem::EndFrame();

        GameTimer::StatsFrameEnd();

        GameTimer::LimitFrameRate(); // the cap of the settings (none by default)

        std::this_thread::yield();
    }

    GameAudio::Finalize();

    TextRenderer::Finalize();

    PostFx::Finalize();
    Renderer2D_Finalize();
    DirectXSystem::Finalize();

    if (SUCCEEDED(comResult))
    {
        CoUninitialize();
    }

    return 0;
}

LRESULT CALLBACK WndProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            ApplyGameCursor();

            return TRUE;
        }

        break;

    case WM_ACTIVATE:
        DisplaySystem::OnActivate(LOWORD(wParam) != WA_INACTIVE);
        GameAudio::OnWindowActivate(LOWORD(wParam) != WA_INACTIVE); // the audio stops at once when the window is left
        break;
    case WM_DISPLAYCHANGE:
        DisplaySystem::OnDisplayChange(); // (the screen changed its size or was swapped: the back buffer follows it, the sizes on offer are worked out again)
        break;
    case WM_ERASEBKGND:
        return 1;

    case WM_IME_SETCONTEXT:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_NOTIFY:
    case WM_IME_REQUEST:
        return 0;

    case WM_SYSCHAR:
        return 0;

    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN && (lParam & (1 << 29)) != 0)
        {
            return 0;
        }

        break;

    case WM_DESTROY:
        Game_OnExit();
        PostQuitMessage(0);

        return 0;
    }

    return DefWindowProcA(windowHandle, message, wParam, lParam);
}



