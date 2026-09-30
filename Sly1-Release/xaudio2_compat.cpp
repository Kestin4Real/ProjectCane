#include "miniaudio/miniaudio.h"

#include "xaudio2_compat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace
{
constexpr UINT32 kMixSampleRate = 48000;
constexpr UINT32 kMixChannels = 2;
constexpr int kReverbDelayFrames = 2400;

struct SourceVoice;
struct MasteringVoice;
struct SubmixVoice;
struct XAudio2Impl;

XAudio2Impl* g_audio = nullptr;

struct VoiceBase
{
    UINT32 channels = kMixChannels;
    UINT32 sampleRate = kMixSampleRate;
    float volume = 1.0f;

    virtual ~VoiceBase() = default;

    void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* details)
    {
        if (details == nullptr)
            return;
        *details = {};
        details->InputChannels = channels;
        details->InputSampleRate = sampleRate;
    }
};

struct MasteringVoice : IXAudio2MasteringVoice, VoiceBase
{
    HRESULT SetVolume(float value) override
    {
        volume = std::clamp(value, 0.0f, 8.0f);
        return S_OK;
    }
    HRESULT SetFrequencyRatio(float) override { return S_OK; }
    HRESULT SetOutputMatrix(IXAudio2Voice*, UINT32, UINT32, const float*) override { return S_OK; }
    HRESULT SetEffectParameters(UINT32, const void*, UINT32) override { return S_OK; }
    void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* details) override { VoiceBase::GetVoiceDetails(details); }
    void DestroyVoice() override;
};

struct SubmixVoice : IXAudio2SubmixVoice, VoiceBase
{
    float wetDryMix = 0.0f;
    float decayTime = 1.49f;

    HRESULT SetVolume(float value) override
    {
        volume = std::clamp(value, 0.0f, 8.0f);
        return S_OK;
    }
    HRESULT SetFrequencyRatio(float) override { return S_OK; }
    HRESULT SetOutputMatrix(IXAudio2Voice*, UINT32, UINT32, const float*) override { return S_OK; }
    HRESULT SetEffectParameters(UINT32, const void* parameters, UINT32 size) override
    {
        if (parameters == nullptr || size < sizeof(XAUDIO2FX_REVERB_PARAMETERS))
            return E_INVALIDARG;
        const auto* native = static_cast<const XAUDIO2FX_REVERB_PARAMETERS*>(parameters);
        wetDryMix = native->WetDryMix;
        decayTime = std::max(0.1f, native->DecayTime);
        return S_OK;
    }
    void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* details) override { VoiceBase::GetVoiceDetails(details); }
    void DestroyVoice() override;
};

struct SourceVoice : IXAudio2SourceVoice, VoiceBase
{
    float frequencyRatio = 1.0f;
    bool playing = false;
    bool hasBuffer = false;
    UINT32 buffersQueued = 0;
    double cursor = 0.0;
    UINT32 playBegin = 0;
    UINT32 playEnd = 0;
    UINT32 loopBegin = 0;
    UINT32 loopEnd = 0;
    UINT32 loopCount = 0;
    bool loopInfinite = false;
    UINT32 bitsPerSample = 16;
    std::vector<float> samples;
    float masterMatrix[8]{};
    float reverbMatrix[8]{};
    bool sendMaster = true;
    bool sendReverb = false;

    SourceVoice()
    {
        SetDefaultMatrix(masterMatrix, 1);
        SetDefaultMatrix(reverbMatrix, 1);
    }

    static void SetDefaultMatrix(float* matrix, UINT32 sourceChannels)
    {
        std::memset(matrix, 0, sizeof(float) * 8);
        if (sourceChannels <= 1)
        {
            matrix[0] = 1.0f;
            matrix[1] = 1.0f;
            return;
        }
        matrix[0] = 1.0f;
        matrix[kMixChannels + 1] = 1.0f;
    }

    HRESULT SetVolume(float value) override
    {
        volume = std::clamp(value, 0.0f, 16.0f);
        return S_OK;
    }

    HRESULT SetFrequencyRatio(float ratio) override
    {
        if (ratio < XAUDIO2_MIN_FREQ_RATIO)
            return E_INVALIDARG;
        frequencyRatio = ratio;
        return S_OK;
    }

    HRESULT SetOutputMatrix(IXAudio2Voice* destination, UINT32 sourceChannels,
        UINT32 destinationChannels, const float* levelMatrix) override;
    HRESULT SetEffectParameters(UINT32, const void*, UINT32) override { return S_OK; }
    void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* details) override { VoiceBase::GetVoiceDetails(details); }
    void DestroyVoice() override;

    HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER* buffer, const void*) override;
    HRESULT FlushSourceBuffers() override;
    HRESULT Start(UINT32, UINT32) override;
    HRESULT Stop(UINT32, UINT32) override;
    HRESULT GetState(XAUDIO2_VOICE_STATE* state, UINT32) override;
};

struct XAudio2Impl : IXAudio2
{
    std::mutex mutex;
    ma_device device{};
    bool deviceReady = false;
    MasteringVoice* master = nullptr;
    SubmixVoice* reverb = nullptr;
    std::vector<SourceVoice*> sources;
    float delay[kMixChannels][kReverbDelayFrames]{};
    int delayPos = 0;

    ~XAudio2Impl()
    {
        ShutdownDevice();
    }

    ULONG Release() override
    {
        if (g_audio == this)
            g_audio = nullptr;
        delete this;
        return 0;
    }

    bool EnsureDevice()
    {
        if (deviceReady)
            return true;

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = kMixChannels;
        config.sampleRate = kMixSampleRate;
        config.dataCallback = &XAudio2Impl::DataCallback;
        config.pUserData = this;
        if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS)
        {
            std::printf("[SOUND] miniaudio could not open a playback device.\n");
            return false;
        }
        if (ma_device_start(&device) != MA_SUCCESS)
        {
            ma_device_uninit(&device);
            std::printf("[SOUND] miniaudio playback device failed to start.\n");
            return false;
        }
        deviceReady = true;
        std::printf("[SOUND] miniaudio playback started (%u Hz stereo).\n", kMixSampleRate);
        return true;
    }

    void ShutdownDevice()
    {
        if (deviceReady)
        {
            ma_device_uninit(&device);
            deviceReady = false;
        }
    }

    static void DataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
    {
        auto* self = static_cast<XAudio2Impl*>(device->pUserData);
        if (self != nullptr)
            self->Mix(static_cast<float*>(output), frameCount);
    }

    void Mix(float* output, ma_uint32 frameCount)
    {
        std::memset(output, 0, static_cast<std::size_t>(frameCount) * kMixChannels * sizeof(float));
        std::lock_guard<std::mutex> lock(mutex);
        if (master == nullptr)
            return;

        const float masterVolume = master->volume;
        const float reverbVolume = reverb != nullptr ? reverb->volume : 0.0f;
        const float wet = reverb != nullptr ? std::clamp(reverb->wetDryMix / 100.0f, 0.0f, 1.0f) : 0.0f;
        const float decay = reverb != nullptr ? reverb->decayTime : 1.49f;
        const float feedback = std::exp(-3.0f * (static_cast<float>(kReverbDelayFrames) / kMixSampleRate) /
            std::max(0.1f, decay));

        for (ma_uint32 frame = 0; frame < frameCount; ++frame)
        {
            float dry[kMixChannels]{};
            float wetBus[kMixChannels]{};
            for (SourceVoice* voice : sources)
                MixVoiceFrame(*voice, dry, wetBus);

            float delayed[kMixChannels]{};
            for (UINT32 channel = 0; channel < kMixChannels; ++channel)
            {
                delayed[channel] = delay[channel][delayPos];
                delay[channel][delayPos] = wetBus[channel] + delayed[channel] * feedback;
            }
            delayPos = (delayPos + 1) % kReverbDelayFrames;

            for (UINT32 channel = 0; channel < kMixChannels; ++channel)
            {
                float sample = (dry[channel] + delayed[channel] * wet * reverbVolume) * masterVolume;
                sample = std::clamp(sample, -1.0f, 1.0f);
                output[frame * kMixChannels + channel] = sample;
            }
        }
    }

    static float SampleAt(const SourceVoice& voice, UINT32 frame, UINT32 channel)
    {
        const std::size_t index = static_cast<std::size_t>(frame) * voice.channels + channel;
        if (index >= voice.samples.size())
            return 0.0f;
        return voice.samples[index];
    }

    static bool AdvanceCursor(SourceVoice& voice, double increment)
    {
        voice.cursor += increment;
        const bool looping = voice.loopInfinite || voice.loopCount > 0;
        if (looping && voice.cursor >= static_cast<double>(voice.loopEnd) &&
            voice.loopEnd > voice.loopBegin)
        {
            const double overflow = voice.cursor - static_cast<double>(voice.loopEnd);
            voice.cursor = static_cast<double>(voice.loopBegin) + overflow;
            if (!voice.loopInfinite && voice.loopCount != XAUDIO2_LOOP_INFINITE && voice.loopCount > 0)
                --voice.loopCount;
            return true;
        }
        if (!looping && voice.cursor >= static_cast<double>(voice.playEnd))
        {
            voice.hasBuffer = false;
            voice.buffersQueued = 0;
            voice.playing = false;
            return false;
        }
        return true;
    }

    static void MixVoiceFrame(SourceVoice& voice, float* dry, float* wetBus)
    {
        if (!voice.playing || !voice.hasBuffer || voice.channels == 0)
            return;

        if (voice.cursor >= static_cast<double>(voice.playEnd) &&
            !voice.loopInfinite && voice.loopCount == 0)
        {
            voice.hasBuffer = false;
            voice.buffersQueued = 0;
            voice.playing = false;
            return;
        }

        const UINT32 i0 = static_cast<UINT32>(voice.cursor);
        const UINT32 i1 = i0 + 1 < voice.playEnd ? i0 + 1 : i0;
        const float frac = static_cast<float>(voice.cursor - static_cast<double>(i0));
        float src[2]{};
        for (UINT32 channel = 0; channel < voice.channels && channel < 2; ++channel)
        {
            const float a = SampleAt(voice, i0, channel);
            const float b = SampleAt(voice, i1, channel);
            src[channel] = a + (b - a) * frac;
        }

        const UINT32 srcChannels = std::max<UINT32>(1, std::min<UINT32>(voice.channels, 2));
        if (voice.sendMaster)
        {
            for (UINT32 dest = 0; dest < kMixChannels; ++dest)
            {
                float acc = 0.0f;
                for (UINT32 srcChannel = 0; srcChannel < srcChannels; ++srcChannel)
                    acc += src[srcChannel] * voice.masterMatrix[dest * srcChannels + srcChannel];
                dry[dest] += acc * voice.volume;
            }
        }
        if (voice.sendReverb)
        {
            for (UINT32 dest = 0; dest < kMixChannels; ++dest)
            {
                float acc = 0.0f;
                for (UINT32 srcChannel = 0; srcChannel < srcChannels; ++srcChannel)
                    acc += src[srcChannel] * voice.reverbMatrix[dest * srcChannels + srcChannel];
                wetBus[dest] += acc * voice.volume;
            }
        }

        AdvanceCursor(voice, static_cast<double>(voice.frequencyRatio) *
            static_cast<double>(voice.sampleRate) / static_cast<double>(kMixSampleRate));
    }

    HRESULT CreateMasteringVoice(IXAudio2MasteringVoice** voice, UINT32 inputChannels,
        UINT32 sampleRate, UINT32, UINT32, const void*, int) override
    {
        if (voice == nullptr)
            return E_POINTER;
        if (!EnsureDevice())
            return E_FAIL;
        std::lock_guard<std::mutex> lock(mutex);
        auto* created = new (std::nothrow) MasteringVoice();
        if (created == nullptr)
            return E_FAIL;
        created->channels = inputChannels == 0 ? kMixChannels : inputChannels;
        created->sampleRate = sampleRate == 0 ? kMixSampleRate : sampleRate;
        master = created;
        *voice = created;
        return S_OK;
    }

    HRESULT CreateSubmixVoice(IXAudio2SubmixVoice** voice, UINT32 inputChannels,
        UINT32 inputSampleRate, UINT32, UINT32, const XAUDIO2_VOICE_SENDS*,
        const XAUDIO2_EFFECT_CHAIN*) override
    {
        if (voice == nullptr)
            return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex);
        auto* created = new (std::nothrow) SubmixVoice();
        if (created == nullptr)
            return E_FAIL;
        created->channels = std::max<UINT32>(1, inputChannels);
        created->sampleRate = inputSampleRate == 0 ? kMixSampleRate : inputSampleRate;
        reverb = created;
        *voice = created;
        return S_OK;
    }

    HRESULT CreateSourceVoice(IXAudio2SourceVoice** voice, const WAVEFORMATEX* format,
        UINT32, float, void*, const XAUDIO2_VOICE_SENDS* sendList,
        const XAUDIO2_EFFECT_CHAIN*) override
    {
        if (voice == nullptr || format == nullptr)
            return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex);
        auto* created = new (std::nothrow) SourceVoice();
        if (created == nullptr)
            return E_FAIL;
        created->channels = std::max<UINT32>(1, format->nChannels);
        created->sampleRate = format->nSamplesPerSec == 0 ? kMixSampleRate : format->nSamplesPerSec;
        created->bitsPerSample = format->wBitsPerSample == 0 ? 16 : format->wBitsPerSample;
        SourceVoice::SetDefaultMatrix(created->masterMatrix, std::min<UINT32>(created->channels, 2));
        SourceVoice::SetDefaultMatrix(created->reverbMatrix, std::min<UINT32>(created->channels, 2));
        created->sendMaster = true;
        created->sendReverb = false;
        if (sendList != nullptr && sendList->pSends != nullptr)
        {
            created->sendMaster = false;
            created->sendReverb = false;
            for (UINT32 i = 0; i < sendList->SendCount; ++i)
            {
                if (sendList->pSends[i].pOutputVoice == master)
                    created->sendMaster = true;
                else if (sendList->pSends[i].pOutputVoice == reverb)
                    created->sendReverb = true;
            }
            if (!created->sendMaster && !created->sendReverb)
                created->sendMaster = true;
        }
        sources.push_back(created);
        *voice = created;
        return S_OK;
    }

    void RemoveSource(SourceVoice* voice)
    {
        sources.erase(std::remove(sources.begin(), sources.end(), voice), sources.end());
    }
};

HRESULT SourceVoice::SetOutputMatrix(IXAudio2Voice* destination, UINT32 sourceChannels,
    UINT32 destinationChannels, const float* levelMatrix)
{
    if (g_audio == nullptr || levelMatrix == nullptr || sourceChannels == 0 || destinationChannels == 0)
        return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(g_audio->mutex);
    const UINT32 src = std::min<UINT32>(sourceChannels, 2);
    const UINT32 dest = std::min<UINT32>(destinationChannels, kMixChannels);
    float* target = masterMatrix;
    if (g_audio != nullptr && destination == g_audio->reverb)
    {
        target = reverbMatrix;
        sendReverb = true;
    }
    else
    {
        sendMaster = true;
    }
    std::memset(target, 0, sizeof(float) * 8);
    for (UINT32 d = 0; d < dest; ++d)
        for (UINT32 s = 0; s < src; ++s)
            target[d * src + s] = levelMatrix[d * sourceChannels + s];
    return S_OK;
}

HRESULT SourceVoice::SubmitSourceBuffer(const XAUDIO2_BUFFER* buffer, const void*)
{
    if (g_audio == nullptr || buffer == nullptr || buffer->pAudioData == nullptr || buffer->AudioBytes == 0)
        return E_INVALIDARG;

    const UINT32 bytesPerSample = std::max<UINT32>(1, bitsPerSample / 8);
    const UINT32 frameBytes = std::max<UINT32>(1, channels * bytesPerSample);
    const UINT32 totalFrames = buffer->AudioBytes / frameBytes;
    if (totalFrames == 0)
        return E_INVALIDARG;

    std::vector<float> decoded(static_cast<std::size_t>(totalFrames) * channels);
    const BYTE* data = buffer->pAudioData;
    for (UINT32 frame = 0; frame < totalFrames; ++frame)
    {
        for (UINT32 channel = 0; channel < channels; ++channel)
        {
            const BYTE* sample = data + (frame * frameBytes) + channel * bytesPerSample;
            float value = 0.0f;
            if (bitsPerSample == 8)
                value = (static_cast<int>(sample[0]) - 128) / 128.0f;
            else
            {
                const int16_t pcm = static_cast<int16_t>(sample[0] | (sample[1] << 8));
                value = pcm / 32768.0f;
            }
            decoded[static_cast<std::size_t>(frame) * channels + channel] = value;
        }
    }

    std::lock_guard<std::mutex> lock(g_audio->mutex);
    samples = std::move(decoded);
    playBegin = std::min(buffer->PlayBegin, totalFrames);
    playEnd = buffer->PlayLength == 0 ? totalFrames : std::min(playBegin + buffer->PlayLength, totalFrames);
    loopBegin = buffer->LoopBegin;
    const UINT32 loopLength = buffer->LoopLength == 0 ? (playEnd - playBegin) : buffer->LoopLength;
    loopEnd = std::min(loopBegin + loopLength, playEnd);
    if (loopEnd <= loopBegin)
    {
        loopBegin = playBegin;
        loopEnd = playEnd;
    }
    loopCount = buffer->LoopCount;
    loopInfinite = buffer->LoopCount == XAUDIO2_LOOP_INFINITE;
    cursor = static_cast<double>(playBegin);
    hasBuffer = true;
    buffersQueued = 1;
    return S_OK;
}

HRESULT SourceVoice::FlushSourceBuffers()
{
    std::lock_guard<std::mutex> lock(g_audio->mutex);
    samples.clear();
    hasBuffer = false;
    buffersQueued = 0;
    playing = false;
    cursor = 0.0;
    return S_OK;
}

HRESULT SourceVoice::Start(UINT32, UINT32)
{
    std::lock_guard<std::mutex> lock(g_audio->mutex);
    playing = hasBuffer;
    return S_OK;
}

HRESULT SourceVoice::Stop(UINT32, UINT32)
{
    std::lock_guard<std::mutex> lock(g_audio->mutex);
    playing = false;
    return S_OK;
}

HRESULT SourceVoice::GetState(XAUDIO2_VOICE_STATE* state, UINT32)
{
    if (state == nullptr)
        return E_POINTER;
    std::lock_guard<std::mutex> lock(g_audio->mutex);
    *state = {};
    state->BuffersQueued = buffersQueued;
    state->SamplesPlayed = static_cast<UINT64>(cursor);
    return S_OK;
}

void SourceVoice::DestroyVoice()
{
    if (g_audio != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_audio->mutex);
        g_audio->RemoveSource(this);
    }
    delete this;
}

void MasteringVoice::DestroyVoice()
{
    if (g_audio != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_audio->mutex);
        if (g_audio->master == this)
            g_audio->master = nullptr;
    }
    delete this;
}

void SubmixVoice::DestroyVoice()
{
    if (g_audio != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_audio->mutex);
        if (g_audio->reverb == this)
            g_audio->reverb = nullptr;
    }
    delete this;
}

struct DummyReverb : IUnknown {};
}

HRESULT XAudio2Create(IXAudio2** xaudio2, UINT32, UINT32)
{
    if (xaudio2 == nullptr)
        return E_POINTER;
    auto* impl = new (std::nothrow) XAudio2Impl();
    if (impl == nullptr)
        return E_FAIL;
    g_audio = impl;
    *xaudio2 = impl;
    return S_OK;
}

HRESULT XAudio2CreateReverb(IUnknown** reverb)
{
    if (reverb == nullptr)
        return E_POINTER;
    *reverb = new (std::nothrow) DummyReverb();
    return *reverb != nullptr ? S_OK : E_FAIL;
}

void ReverbConvertI3DL2ToNative(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS* i3dl2,
    XAUDIO2FX_REVERB_PARAMETERS* native, BOOL)
{
    if (i3dl2 == nullptr || native == nullptr)
        return;
    native->WetDryMix = i3dl2->WetDryMix;
    native->DecayTime = i3dl2->DecayTime;
}
