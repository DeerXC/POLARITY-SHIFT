// A stand-in for the parts of <Windows.h> that the audio code uses, so that GameAudio.cpp can be built and run on a machine without Windows.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
typedef int HRESULT; typedef void* HWND; typedef unsigned int DWORD; typedef int BOOL; typedef unsigned long long ULONGLONG;
typedef unsigned int UINT; typedef unsigned int UINT32; typedef unsigned long long UINT64; typedef int LONG; typedef unsigned short WORD; typedef unsigned char BYTE;
typedef short SHORT; typedef int INT32; typedef unsigned int ULONG; typedef const wchar_t* LPCWSTR;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_FAIL ((HRESULT)0x80004005u)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define ZeroMemory(p, n) std::memset((p), 0, (n))
#define CopyMemory(d, s, n) std::memcpy((d), (s), (n))
#define STDMETHODCALLTYPE
#define __declspec(x)
struct GUID { unsigned long a; unsigned short b, c; unsigned char d[8]; };
typedef GUID CLSID; typedef const GUID& REFCLSID;
static const GUID GUID_NULL_ = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
#define __uuidof(x) GUID_NULL_
#define WAVE_FORMAT_PCM 1
#pragma pack(push, 1)
struct WAVEFORMATEX { WORD wFormatTag; WORD nChannels; DWORD nSamplesPerSec; DWORD nAvgBytesPerSec; WORD nBlockAlign; WORD wBitsPerSample; WORD cbSize; };
#pragma pack(pop)
struct IUnknown { virtual ULONG AddRef() { return 1; } virtual ULONG Release() { return 0; } virtual ~IUnknown() {} };
#include <cstdio>
inline void OutputDebugStringA(const char* s) { std::fprintf(stderr, "[debug] %s", s); }
inline HWND GetForegroundWindow() { return nullptr; }
inline DWORD GetWindowThreadProcessId(HWND, DWORD*) { return 0; }
inline DWORD GetCurrentProcessId() { return 0; }
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F3 0x72
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define MAPVK_VK_TO_VSC 0
inline int GetKeyNameTextA(LONG, char*, int) { return 0; }
inline UINT MapVirtualKeyA(UINT, UINT) { return 0; }
struct POINT { LONG x, y; }; struct RECT { LONG left, top, right, bottom; };
inline ULONGLONG GetTickCount64() { return 0; }
inline short GetAsyncKeyState(int) { return 0; }
inline BOOL GetCursorPos(POINT*) { return 0; }
inline BOOL ScreenToClient(HWND, POINT*) { return 0; }
inline BOOL GetClientRect(HWND, RECT*) { return 0; }

