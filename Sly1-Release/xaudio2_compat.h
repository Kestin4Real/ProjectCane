#pragma once

// Software XAudio2 subset used when the Windows SDK is not available.
#include <cstdint>
#include <cstring>

using BYTE = std::uint8_t;
using UINT32 = std::uint32_t;
using UINT64 = std::uint64_t;
using ULONG = unsigned long;
using BOOL = int;
using HRESULT = long;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

inline constexpr HRESULT S_OK = 0;
inline constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005L);
inline constexpr HRESULT E_POINTER = static_cast<HRESULT>(0x80004003L);
inline constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057L);

inline bool FAILED(HRESULT hr) { return hr < 0; }
inline bool SUCCEEDED(HRESULT hr) { return hr >= 0; }

inline constexpr UINT32 XAUDIO2_END_OF_STREAM = 0x0040;
inline constexpr UINT32 XAUDIO2_LOOP_INFINITE = 255;
inline constexpr UINT32 XAUDIO2_DEFAULT_PROCESSOR = 0;
inline constexpr float XAUDIO2_MIN_FREQ_RATIO = 1.0f / 1024.0f;
inline constexpr UINT32 WAVE_FORMAT_PCM = 1;

struct WAVEFORMATEX
{
    std::uint16_t wFormatTag;
    std::uint16_t nChannels;
    std::uint32_t nSamplesPerSec;
    std::uint32_t nAvgBytesPerSec;
    std::uint16_t nBlockAlign;
    std::uint16_t wBitsPerSample;
    std::uint16_t cbSize;
};

struct XAUDIO2_BUFFER
{
    UINT32 Flags = 0;
    UINT32 AudioBytes = 0;
    const BYTE* pAudioData = nullptr;
    UINT32 PlayBegin = 0;
    UINT32 PlayLength = 0;
    UINT32 LoopBegin = 0;
    UINT32 LoopLength = 0;
    UINT32 LoopCount = 0;
    void* pContext = nullptr;
};

struct XAUDIO2_VOICE_STATE
{
    void* pCurrentBufferContext = nullptr;
    UINT32 BuffersQueued = 0;
    UINT64 SamplesPlayed = 0;
};

struct XAUDIO2_VOICE_DETAILS
{
    UINT32 CreationFlags = 0;
    UINT32 ActiveFlags = 0;
    UINT32 InputChannels = 0;
    UINT32 InputSampleRate = 0;
};

struct IUnknown
{
    virtual ~IUnknown() = default;
    virtual ULONG Release()
    {
        delete this;
        return 0;
    }
};

struct IXAudio2Voice
{
    virtual ~IXAudio2Voice() = default;
    virtual void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* details) = 0;
    virtual HRESULT SetVolume(float volume) = 0;
    virtual HRESULT SetFrequencyRatio(float ratio) = 0;
    virtual HRESULT SetOutputMatrix(IXAudio2Voice* destination, UINT32 sourceChannels,
        UINT32 destinationChannels, const float* levelMatrix) = 0;
    virtual HRESULT SetEffectParameters(UINT32 effectIndex, const void* parameters, UINT32 size) = 0;
    virtual void DestroyVoice() = 0;
};

struct IXAudio2SourceVoice : IXAudio2Voice
{
    virtual HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER* buffer, const void* bufferWma = nullptr) = 0;
    virtual HRESULT FlushSourceBuffers() = 0;
    virtual HRESULT Start(UINT32 flags = 0, UINT32 operationSet = 0) = 0;
    virtual HRESULT Stop(UINT32 flags = 0, UINT32 operationSet = 0) = 0;
    virtual HRESULT GetState(XAUDIO2_VOICE_STATE* state, UINT32 flags = 0) = 0;
};

struct IXAudio2MasteringVoice : IXAudio2Voice {};
struct IXAudio2SubmixVoice : IXAudio2Voice {};

struct XAUDIO2_SEND_DESCRIPTOR
{
    UINT32 Flags = 0;
    IXAudio2Voice* pOutputVoice = nullptr;
};

struct XAUDIO2_VOICE_SENDS
{
    UINT32 SendCount = 0;
    XAUDIO2_SEND_DESCRIPTOR* pSends = nullptr;
};

struct XAUDIO2_EFFECT_DESCRIPTOR
{
    IUnknown* pEffect = nullptr;
    BOOL InitialState = TRUE;
    UINT32 OutputChannels = 2;
};

struct XAUDIO2_EFFECT_CHAIN
{
    UINT32 EffectCount = 0;
    XAUDIO2_EFFECT_DESCRIPTOR* pEffectDescriptors = nullptr;
};

struct IXAudio2
{
    virtual ~IXAudio2() = default;
    virtual ULONG Release() = 0;
    virtual HRESULT CreateMasteringVoice(IXAudio2MasteringVoice** voice,
        UINT32 inputChannels = 0, UINT32 sampleRate = 0, UINT32 flags = 0,
        UINT32 deviceIndex = 0, const void* effectChain = nullptr, int streamCategory = 0) = 0;
    virtual HRESULT CreateSubmixVoice(IXAudio2SubmixVoice** voice, UINT32 inputChannels,
        UINT32 inputSampleRate, UINT32 flags = 0, UINT32 processingStage = 0,
        const XAUDIO2_VOICE_SENDS* sendList = nullptr, const XAUDIO2_EFFECT_CHAIN* effectChain = nullptr) = 0;
    virtual HRESULT CreateSourceVoice(IXAudio2SourceVoice** voice, const WAVEFORMATEX* format,
        UINT32 flags = 0, float maxFrequencyRatio = 2.0f, void* callback = nullptr,
        const XAUDIO2_VOICE_SENDS* sendList = nullptr, const XAUDIO2_EFFECT_CHAIN* effectChain = nullptr) = 0;
};

struct XAUDIO2FX_REVERB_I3DL2_PARAMETERS
{
    float WetDryMix;
    int Room;
    int RoomHF;
    float RoomRolloffFactor;
    float DecayTime;
    float DecayHFRatio;
    int Reflections;
    float ReflectionsDelay;
    int Reverb;
    float ReverbDelay;
    float Diffusion;
    float Density;
    float HFReference;
};

struct XAUDIO2FX_REVERB_PARAMETERS
{
    float WetDryMix;
    float DecayTime;
};

inline constexpr XAUDIO2FX_REVERB_I3DL2_PARAMETERS XAUDIO2FX_I3DL2_PRESET_DEFAULT =
{ 100, -1000, -100, 0, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100, 100, 5000 };
inline constexpr XAUDIO2FX_REVERB_I3DL2_PARAMETERS XAUDIO2FX_I3DL2_PRESET_SMALLROOM =
{ 100, -1000, -600, 0, 1.10f, 0.83f, -400, 0.005f, 500, 0.010f, 100, 100, 5000 };
inline constexpr XAUDIO2FX_REVERB_I3DL2_PARAMETERS XAUDIO2FX_I3DL2_PRESET_MEDIUMROOM =
{ 100, -1000, -600, 0, 1.30f, 0.83f, -1000, 0.010f, -200, 0.020f, 100, 100, 5000 };
inline constexpr XAUDIO2FX_REVERB_I3DL2_PARAMETERS XAUDIO2FX_I3DL2_PRESET_LARGEROOM =
{ 100, -1000, -600, 0, 1.50f, 0.83f, -1200, 0.020f, -400, 0.040f, 100, 100, 5000 };
inline constexpr XAUDIO2FX_REVERB_I3DL2_PARAMETERS XAUDIO2FX_I3DL2_PRESET_SEWERPIPE =
{ 80, -1000, -1000, 0, 2.81f, 0.14f, 429, 0.014f, 1023, 0.021f, 80, 60, 5000 };
inline constexpr XAUDIO2FX_REVERB_I3DL2_PARAMETERS XAUDIO2FX_I3DL2_PRESET_ROOM =
{ 100, -1000, -454, 0, 0.40f, 0.83f, -1646, 0.002f, 53, 0.003f, 100, 100, 5000 };

HRESULT XAudio2Create(IXAudio2** xaudio2, UINT32 flags, UINT32 processor);
HRESULT XAudio2CreateReverb(IUnknown** reverb);
void ReverbConvertI3DL2ToNative(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS* i3dl2,
    XAUDIO2FX_REVERB_PARAMETERS* native, BOOL sevenDotOne);
