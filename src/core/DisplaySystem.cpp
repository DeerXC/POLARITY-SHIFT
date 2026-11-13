#include "core/DisplaySystem.h"

#include "core/DirectX.h"
#include "core/TextRenderer.h"

namespace
{

constexpr DisplayResolution RESOLUTIONS[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

constexpr int RESOLUTION_COUNT = static_cast<int>(sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0]));

constexpr DWORD WINDOWED_STYLE = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

constexpr DWORD FULLSCREEN_STYLE = WS_POPUP;

HWND g_WindowHandle = nullptr;

DisplayMode g_DisplayMode = DisplayMode::Windowed;

int g_ResolutionIndex = 0;

int ClampResolutionIndex(int resolutionIndex)
{
    if (resolutionIndex < 0)
    {
        return 0;
    }

    if (resolutionIndex >= RESOLUTION_COUNT)
    {
        return RESOLUTION_COUNT - 1;
    }

    return resolutionIndex;
}

bool GetCurrentMonitorInfo(MONITORINFO& monitorInfo)
{
    if (g_WindowHandle == nullptr)
    {
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(g_WindowHandle, MONITOR_DEFAULTTONEAREST);

    if (monitor == nullptr)
    {
        return false;
    }

    monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFO);

    return GetMonitorInfoA(monitor, &monitorInfo) != FALSE;
}

// How many of the window sizes (they are in ascending order) fit on the screen the window is on, frame and task bar included.
int FittingResolutionCount()
{
    MONITORINFO monitorInfo{};
    if (!GetCurrentMonitorInfo(monitorInfo))
    {
        return RESOLUTION_COUNT;
    }

    const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

    int count = 0;
    for (int i = 0; i < RESOLUTION_COUNT; ++i)
    {
        RECT rect{0, 0, RESOLUTIONS[i].width, RESOLUTIONS[i].height};
        AdjustWindowRect(&rect, WINDOWED_STYLE, FALSE);
        // (a few pixels of frame may reach over the width of the screen: 2560 wide windows are fine on a 2560 wide screen)
        if (rect.right - rect.left > workWidth + 16 || rect.bottom - rect.top > workHeight)
        {
            break;
        }
        ++count;
    }

    return count > 0 ? count : 1;
}

// The largest 16:9 rectangle of the monitor: the size of the picture in full screen.
bool FullscreenFitSize(int& width, int& height)
{
    MONITORINFO monitorInfo{};
    if (!GetCurrentMonitorInfo(monitorInfo))
    {
        return false;
    }
    const int monitorWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    const int monitorHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    width = monitorWidth;
    height = monitorWidth * 9 / 16;
    if (height > monitorHeight)
    {
        height = monitorHeight;
        width = monitorHeight * 16 / 9;
    }
    return width > 0 && height > 0;
}

// First size that is at least as large as the screen picture ("native"). Smaller ones would only be a blurry enlargement.
int NativeResolutionIndex()
{
    int width = 0, height = 0;
    if (!FullscreenFitSize(width, height))
    {
        return 0;
    }
    for (int i = 0; i < RESOLUTION_COUNT; ++i)
    {
        if (RESOLUTIONS[i].width >= width)
        {
            return i;
        }
    }
    return RESOLUTION_COUNT - 1;
}

bool CenterWindow(int windowWidth, int windowHeight)
{
    if (g_WindowHandle == nullptr || windowWidth <= 0 || windowHeight <= 0)
    {
        return false;
    }

    MONITORINFO monitorInfo{};

    if (!GetCurrentMonitorInfo(monitorInfo))
    {
        return false;
    }

    const RECT& workArea = monitorInfo.rcWork;

    const int workWidth = workArea.right - workArea.left;

    const int workHeight = workArea.bottom - workArea.top;

    const int windowX = workArea.left + (workWidth - windowWidth) / 2;

    const int windowY = workArea.top + (workHeight - windowHeight) / 2;

    return SetWindowPos(g_WindowHandle, nullptr, windowX, windowY, windowWidth, windowHeight,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW)
           != FALSE;
}

HWND g_LetterboxWindow = nullptr;
RECT g_LetterboxRect{};          // the whole monitor
bool g_LetterboxNeeded = false;  // the 16:9 picture does not fill the monitor (no longer used in full screen: the game fills the bars itself)
int g_FullscreenBufferW = 0, g_FullscreenBufferH = 0;
bool g_FullscreenActive = false; // the game is in full screen mode

// The black window behind the game: a click on it brings the game back to the front.
LRESULT CALLBACK LetterboxProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (g_WindowHandle != nullptr)
        {
            SetForegroundWindow(g_WindowHandle);
        }
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    default:
        break;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

// black window behind the game: the game keeps its 16:9 shape on any screen (a 16:10 laptop panel stretched it by 11 percent)
void ShowLetterbox(const RECT& monitorRect)
{
    if (g_LetterboxWindow == nullptr)
    {
        WNDCLASSA windowClass{};
        windowClass.lpfnWndProc = LetterboxProc;
        windowClass.hInstance = GetModuleHandleA(nullptr);
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = "PolarityLetterbox";
        RegisterClassA(&windowClass);
        g_LetterboxWindow = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "PolarityLetterbox", "", WS_POPUP, 0, 0, 1, 1, nullptr,
                                            nullptr, GetModuleHandleA(nullptr), nullptr);
    }
    if (g_LetterboxWindow == nullptr)
    {
        return;
    }
    SetWindowPos(g_LetterboxWindow, HWND_TOPMOST, monitorRect.left, monitorRect.top, monitorRect.right - monitorRect.left,
                 monitorRect.bottom - monitorRect.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void HideLetterbox()
{
    if (g_LetterboxWindow != nullptr)
    {
        ShowWindow(g_LetterboxWindow, SW_HIDE);
    }
}

// Full screen: while the game is the active program, the black window and the game window are TOPMOST (the black one below), so the task
// bar and the bars of other windows cannot show through the black borders. When the player switches to another program, both give way.
void RaiseFullscreen()
{
    if (g_WindowHandle == nullptr || !g_FullscreenActive)
    {
        return;
    }
    if (g_LetterboxNeeded)
    {
        ShowLetterbox(g_LetterboxRect);
    }
    SetWindowPos(g_WindowHandle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// The game gives way: another program took the focus (Alt+Tab, a screenshot tool, a click on the task bar). The black window around a 16:9 picture
// does NOT go away: it stays right below the game window, so the windows of the desktop do not show through the black borders (otherwise
// the toolbars of the program behind appear above and below the picture). Only a minimized game takes it away.
void LowerFullscreen()
{
    if (g_WindowHandle == nullptr)
    {
        HideLetterbox();
        return;
    }
    SetWindowPos(g_WindowHandle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (IsIconic(g_WindowHandle) || !g_LetterboxNeeded || g_LetterboxWindow == nullptr)
    {
        HideLetterbox();
        return;
    }
    SetWindowPos(g_LetterboxWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_LetterboxWindow, g_WindowHandle, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // directly below the game window
}

bool ApplyWindowed(const DisplayResolution& resolution)
{
    g_FullscreenActive = false;
    g_LetterboxNeeded = false;
    LowerFullscreen();
    if (g_WindowHandle == nullptr || resolution.width <= 0 || resolution.height <= 0)
    {
        return false;
    }
    SetWindowLongPtrA(g_WindowHandle, GWL_STYLE, static_cast<LONG_PTR>(WINDOWED_STYLE));
    RECT windowRect{};
    windowRect.left = 0;
    windowRect.top = 0;
    windowRect.right = resolution.width;
    windowRect.bottom = resolution.height;
    if (!AdjustWindowRect(&windowRect, WINDOWED_STYLE, FALSE))
    {
        return false;
    }
    const int outerWidth = windowRect.right - windowRect.left;
    const int outerHeight = windowRect.bottom - windowRect.top;
    if (!CenterWindow(outerWidth, outerHeight))
    {
        return false;
    }
    TextRenderer::ReleaseRenderTarget();
    return DirectXSystem::Resize(resolution.width, resolution.height);
}

// render = the size the game is drawn at. A size larger than the screen is drawn at that size and scaled down (smoother edges, 4K on a 1440p screen).
bool ApplyFullscreen(const DisplayResolution& render)
{
    if (g_WindowHandle == nullptr)
    {
        return false;
    }
    MONITORINFO monitorInfo{};
    if (!GetCurrentMonitorInfo(monitorInfo))
    {
        return false;
    }
    const int monitorWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    const int monitorHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    if (monitorWidth <= 0 || monitorHeight <= 0)
    {
        return false;
    }
    // The window covers the whole screen. The game is 16:9: on a screen of another shape (16:10, 21:9) the back buffer takes the shape of the
    // screen at the chosen render size, the game is drawn in its 16:9 middle and PostFx fills the rest (DirectXSystem::GetGameRect): no black bars.
    int bufferW = render.width, bufferH = render.height;
    if (monitorWidth * 9 < monitorHeight * 16) // taller than 16:9 (16:10, 4:3)
        bufferH = render.width * monitorHeight / monitorWidth;
    else if (monitorWidth * 9 > monitorHeight * 16) // wider (21:9)
        bufferW = render.height * monitorWidth / monitorHeight;
    g_FullscreenBufferW = bufferW;
    g_FullscreenBufferH = bufferH;
    g_LetterboxRect = monitorInfo.rcMonitor;
    g_LetterboxNeeded = false;
    g_FullscreenActive = true;
    SetWindowLongPtrA(g_WindowHandle, GWL_STYLE, static_cast<LONG_PTR>(FULLSCREEN_STYLE));
    if (!SetWindowPos(g_WindowHandle, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top, monitorWidth, monitorHeight,
                      SWP_FRAMECHANGED | SWP_SHOWWINDOW))
    {
        return false;
    }
    HideLetterbox();
    RaiseFullscreen();
    TextRenderer::ReleaseRenderTarget();
    return DirectXSystem::Resize(g_FullscreenBufferW, g_FullscreenBufferH);
}
}

void DisplaySystem::Initialize(HWND windowHandle)
{
    g_WindowHandle = windowHandle;

    g_DisplayMode = DisplayMode::Windowed;

    g_ResolutionIndex = 0;
}

// The game window was activated (the player clicked on it after switching to another program) or deactivated (WM_ACTIVATE).
void DisplaySystem::OnActivate(bool active)
{
    if (!g_FullscreenActive)
    {
        return;
    }
    if (active)
    {
        RaiseFullscreen();
    }
    else
    {
        LowerFullscreen();
    }
}

int DisplaySystem::FirstResolutionIndex(DisplayMode mode)
{
    return mode == DisplayMode::Fullscreen ? NativeResolutionIndex() : 0;
}

int DisplaySystem::LastResolutionIndex(DisplayMode mode)
{
    return mode == DisplayMode::Fullscreen ? RESOLUTION_COUNT - 1 : FittingResolutionCount() - 1;
}

int DisplaySystem::NormalizeResolution(DisplayMode mode, int resolutionIndex)
{
    const int first = FirstResolutionIndex(mode);
    const int last = LastResolutionIndex(mode);
    resolutionIndex = ClampResolutionIndex(resolutionIndex);
    if (resolutionIndex < first) return first;
    if (resolutionIndex > last) return last;
    return resolutionIndex;
}

int DisplaySystem::StepResolution(DisplayMode mode, int resolutionIndex, int direction)
{
    const int first = FirstResolutionIndex(mode);
    const int last = LastResolutionIndex(mode);
    resolutionIndex = NormalizeResolution(mode, resolutionIndex) + (direction < 0 ? -1 : 1);
    if (resolutionIndex > last) resolutionIndex = first;
    if (resolutionIndex < first) resolutionIndex = last;
    return resolutionIndex;
}

bool DisplaySystem::Apply(DisplayMode displayMode, int resolutionIndex)
{
    if (g_WindowHandle == nullptr)
    {
        return false;
    }

    // windowed: only windows that fit on the screen; full screen: from the native size up (the picture is drawn at that size)
    resolutionIndex = NormalizeResolution(displayMode, resolutionIndex);
    const DisplayResolution resolution = RESOLUTIONS[resolutionIndex];

    bool succeeded = false;
    if (displayMode == DisplayMode::Fullscreen)
    {
        succeeded = ApplyFullscreen(resolution);
    }
    else
    {
        succeeded = ApplyWindowed(resolution);
    }

    if (!succeeded)
    {
        return false;
    }

    g_DisplayMode = displayMode;
    g_ResolutionIndex = resolutionIndex;
    return true;
}

DisplayResolution DisplaySystem::GetResolution(int resolutionIndex)
{
    resolutionIndex = ClampResolutionIndex(resolutionIndex);

    return RESOLUTIONS[resolutionIndex];
}


