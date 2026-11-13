#pragma once
#include <xaudio2.h>
#define STDMETHOD(name) virtual HRESULT name
#define STDMETHOD_(type, name) virtual type name
enum XAPO_BUFFER_FLAGS { XAPO_BUFFER_SILENT = 0, XAPO_BUFFER_VALID = 1 };
#define XAPO_FLAG_CHANNELS_MUST_MATCH 0x1
#define XAPO_FLAG_FRAMERATE_MUST_MATCH 0x2
#define XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH 0x4
#define XAPO_FLAG_BUFFERCOUNT_MUST_MATCH 0x8
#define XAPO_FLAG_INPLACE_REQUIRED 0x20
struct XAPO_REGISTRATION_PROPERTIES { GUID clsid; wchar_t FriendlyName[256]; wchar_t CopyrightInfo[256]; UINT32 MajorVersion; UINT32 MinorVersion; UINT32 Flags; UINT32 MinInputBufferCount; UINT32 MaxInputBufferCount; UINT32 MinOutputBufferCount; UINT32 MaxOutputBufferCount; };
struct XAPO_LOCKFORPROCESS_PARAMETERS { const WAVEFORMATEX* pFormat; UINT32 MaxFrameCount; };
struct XAPO_PROCESS_BUFFER_PARAMETERS { void* pBuffer; XAPO_BUFFER_FLAGS BufferFlags; UINT32 ValidFrameCount; };
struct IXAPO : public IUnknown {
    virtual HRESULT LockForProcess(UINT32, const XAPO_LOCKFORPROCESS_PARAMETERS*, UINT32, const XAPO_LOCKFORPROCESS_PARAMETERS*) = 0;
    virtual void Process(UINT32, const XAPO_PROCESS_BUFFER_PARAMETERS*, UINT32, XAPO_PROCESS_BUFFER_PARAMETERS*, BOOL) = 0;
};
class CXAPOBase : public IXAPO {
public:
    CXAPOBase(const XAPO_REGISTRATION_PROPERTIES*) {}
    ULONG m_ref = 1;
    virtual ULONG AddRef() override { return ++m_ref; }
    virtual ~CXAPOBase() {}
    virtual HRESULT LockForProcess(UINT32, const XAPO_LOCKFORPROCESS_PARAMETERS*, UINT32, const XAPO_LOCKFORPROCESS_PARAMETERS*) override { return S_OK; }
    virtual void Process(UINT32, const XAPO_PROCESS_BUFFER_PARAMETERS*, UINT32, XAPO_PROCESS_BUFFER_PARAMETERS*, BOOL) override {}
    virtual ULONG Release() override { if (--m_ref == 0) { delete this; return 0; } return m_ref; }
};

