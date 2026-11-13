#pragma once
#include <xaudio2.h>
struct XAUDIO2FX_REVERB_I3DL2_PARAMETERS { float WetDryMix; INT32 Room; INT32 RoomHF; float RoomRolloffFactor; float DecayTime; float DecayHFRatio; INT32 Reflections; float ReflectionsDelay; INT32 Reverb; float ReverbDelay; float Diffusion; float Density; float HFReference; };
struct XAUDIO2FX_REVERB_PARAMETERS { float WetDryMix; float Pad[24]; };
inline HRESULT XAudio2CreateReverb(IUnknown** pp, UINT32 = 0) { *pp = new IUnknown(); return S_OK; }
inline HRESULT ReverbConvertI3DL2ToNative(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS* a, XAUDIO2FX_REVERB_PARAMETERS* b, BOOL = TRUE) { b->WetDryMix = a->WetDryMix; return S_OK; }

