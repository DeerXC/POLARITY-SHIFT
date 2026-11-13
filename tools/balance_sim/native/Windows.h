#pragma once
// A stand-in for <Windows.h> so that the game logic compiles natively (tools/balance_sim): only the types and key codes the logic uses.
#include <cstdint>
#include <cstdlib>
typedef void* HWND;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned long long ULONGLONG;
#define VK_CANCEL 0x03
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_F3 0x72
inline ULONGLONG GetTickCount64() { return 0; }
