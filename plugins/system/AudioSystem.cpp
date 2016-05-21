#include "GEK\Utility\Trace.h"
#include "GEK\Utility\FileSystem.h"
#include "GEK\System\AudioSystem.h"
#include "GEK\Context\COM.h"
#include "GEK\Context\ContextUserMixin.h"
#include "audiere.h"

#include <mmsystem.h>
#define DIRECTSOUND_VERSION 0x0800
#include <dsound.h>

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "audiere.lib")

namespace Gek
{
    template <typename CLASS>
    class SampleMixin
        : public CLASS
    {
    protected:
        CComQIPtr<IDirectSoundBuffer8, &IID_IDirectSoundBuffer8> directSoundBuffer;

    public:
        SampleMixin(IDirectSoundBuffer8 *directSoundBuffer)
            : directSoundBuffer(directSoundBuffer)
        {
            setVolume(1.0f);
        }

        virtual ~SampleMixin(void)
        {
        }

        // AudioSample
        void * const getBuffer(void)
        {
            GEK_REQUIRE(directSoundBuffer);

            return static_cast<void *>(directSoundBuffer.p);
        }

        void setFrequency(UINT32 frequency)
        {
            GEK_REQUIRE(directSoundBuffer);

            if (frequency == -1)
            {
                directSoundBuffer->SetFrequency(DSBFREQUENCY_ORIGINAL);
            }
            else
            {
                directSoundBuffer->SetFrequency(frequency);
            }
        }

        void setVolume(float volume)
        {
            GEK_REQUIRE(directSoundBuffer);

            directSoundBuffer->SetVolume(UINT32((DSBVOLUME_MAX - DSBVOLUME_MIN) * volume) + DSBVOLUME_MIN);
        }
    };

    class EffectImplementation
        : public SampleMixin<AudioEffect>
    {
    public:
        EffectImplementation(IDirectSoundBuffer8 *directSoundBuffer)
            : SampleMixin(directSoundBuffer)
        {
        }

        // AudioEffect
        void setPan(float pan)
        {
            GEK_REQUIRE(directSoundBuffer);
            directSoundBuffer->SetPan(UINT32((DSBPAN_RIGHT - DSBPAN_LEFT) * pan) + DSBPAN_LEFT);
        }

        void play(bool loop)
        {
            GEK_REQUIRE(directSoundBuffer);

            DWORD dwStatus = 0;
            if (SUCCEEDED(directSoundBuffer->GetStatus(&dwStatus)) && !(dwStatus & DSBSTATUS_PLAYING))
            {
                directSoundBuffer->Play(0, 0, (loop ? DSBPLAY_LOOPING : 0));
            }
        }
    };

    class SoundImplementation
        : public SampleMixin<AudioSound>
    {
    private:
        CComQIPtr<IDirectSound3DBuffer8, &IID_IDirectSound3DBuffer8> directSound8Buffer3D;

    public:
        SoundImplementation(IDirectSoundBuffer8 *directSoundBuffer, IDirectSound3DBuffer8 *directSound8Buffer3D)
            : SampleMixin(directSoundBuffer)
            , directSound8Buffer3D(directSound8Buffer3D)
        {
        }

        // AudioSound
        void setDistance(float minimum, float maximum)
        {
            GEK_REQUIRE(directSound8Buffer3D);

            directSound8Buffer3D->SetMinDistance(minimum, DS3D_DEFERRED);
            directSound8Buffer3D->SetMaxDistance(maximum, DS3D_DEFERRED);
        }

        void play(const Math::Float3 &origin, bool loop)
        {
            GEK_REQUIRE(directSound8Buffer3D);
            GEK_REQUIRE(directSoundBuffer);

            directSound8Buffer3D->SetPosition(origin.x, origin.y, origin.z, DS3D_DEFERRED);

            DWORD dwStatus = 0;
            if (SUCCEEDED(directSoundBuffer->GetStatus(&dwStatus)) && !(dwStatus & DSBSTATUS_PLAYING))
            {
                directSoundBuffer->Play(0, 0, (loop ? DSBPLAY_LOOPING : 0));
            }
        }
    };

    class AudioSystemImplementation
        : public ContextUserMixin
        , public AudioSystem
    {
    private:
        CComQIPtr<IDirectSound8, &IID_IDirectSound8> directSound;
        CComQIPtr<IDirectSound3DListener8, &IID_IDirectSound3DListener8> directSoundListener;
        CComQIPtr<IDirectSoundBuffer, &IID_IDirectSoundBuffer> primarySoundBuffer;

    public:
        // Interface
        void initialize(HWND window)
        {
            GEK_REQUIRE(window);

            HRESULT resultValue = DirectSoundCreate8(nullptr, &directSound, nullptr);
            GEK_CHECK_EXCEPTION(FAILED(resultValue), Audio::Exception, "Unable to initialize DirectSound8: %d", resultValue);

            resultValue = directSound->SetCooperativeLevel(window, DSSCL_PRIORITY);
            GEK_CHECK_EXCEPTION(FAILED(resultValue), Audio::Exception, "Unable to set cooperative level: %d", resultValue);

            DSBUFFERDESC primaryBufferDescription = { 0 };
            primaryBufferDescription.dwSize = sizeof(DSBUFFERDESC);
            primaryBufferDescription.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_PRIMARYBUFFER;
            resultValue = directSound->CreateSoundBuffer(&primaryBufferDescription, &primarySoundBuffer, nullptr);
            GEK_CHECK_EXCEPTION(FAILED(resultValue), Audio::Exception, "Unable to create primary sound buffer: %d", resultValue);

            WAVEFORMATEX primaryBufferFormat;
            ZeroMemory(&primaryBufferFormat, sizeof(WAVEFORMATEX));
            primaryBufferFormat.wFormatTag = WAVE_FORMAT_PCM;
            primaryBufferFormat.nChannels = 2;
            primaryBufferFormat.wBitsPerSample = 8;
            primaryBufferFormat.nSamplesPerSec = 48000;
            primaryBufferFormat.nBlockAlign = (primaryBufferFormat.wBitsPerSample / 8 * primaryBufferFormat.nChannels);
            primaryBufferFormat.nAvgBytesPerSec = (primaryBufferFormat.nSamplesPerSec * primaryBufferFormat.nBlockAlign);
            resultValue = primarySoundBuffer->SetFormat(&primaryBufferFormat);
            GEK_CHECK_EXCEPTION(FAILED(resultValue), Audio::Exception, "Unable to set primary sound buffer format: %d", resultValue);

            directSoundListener = primarySoundBuffer;
            GEK_CHECK_EXCEPTION(!directSoundListener, Audio::Exception, "Unable to query for primary sound listener: %d", resultValue);

            setMasterVolume(1.0f);
            setDistanceFactor(1.0f);
            setDopplerFactor(0.0f);
            setRollOffFactor(1.0f);
        }

        void setMasterVolume(float volume)
        {
            GEK_REQUIRE(primarySoundBuffer);

            primarySoundBuffer->SetVolume(UINT32((DSBVOLUME_MAX - DSBVOLUME_MIN) * volume) + DSBVOLUME_MIN);
        }

        float getMasterVolume(void)
        {
            GEK_REQUIRE(primarySoundBuffer);

            long volumeNumber = 0;
            if (FAILED(primarySoundBuffer->GetVolume(&volumeNumber)))
            {
                volumeNumber = 0;
            }

            return (float(volumeNumber - DSBVOLUME_MIN) / float(DSBVOLUME_MAX - DSBVOLUME_MIN));
        }

        void setListener(const Math::Float4x4 &matrix)
        {
            GEK_REQUIRE(directSoundListener);
            directSoundListener->SetPosition(matrix.translation.x, matrix.translation.y, matrix.translation.z, DS3D_DEFERRED);
            directSoundListener->SetOrientation(matrix.rz.x, matrix.rz.y, matrix.rz.z, matrix.ry.x, matrix.ry.y, matrix.ry.z, DS3D_DEFERRED);
            directSoundListener->CommitDeferredSettings();
        }

        void setDistanceFactor(float factor)
        {
            GEK_REQUIRE(directSoundListener);

            directSoundListener->SetDistanceFactor(factor, DS3D_DEFERRED);
        }

        void setDopplerFactor(float factor)
        {
            GEK_REQUIRE(directSoundListener);

            directSoundListener->SetDopplerFactor(factor, DS3D_DEFERRED);
        }

        void setRollOffFactor(float factor)
        {
            GEK_REQUIRE(directSoundListener);

            directSoundListener->SetRolloffFactor(factor, DS3D_DEFERRED);
        }

        std::shared_ptr<AudioEffect> copyEffect(AudioEffect *source)
        {
            GEK_REQUIRE(directSound);
            GEK_REQUIRE(source);

            CComPtr<IDirectSoundBuffer> directSoundBuffer;
            HRESULT resultValue = directSound->DuplicateSoundBuffer(static_cast<IDirectSoundBuffer *>(source->getBuffer()), &directSoundBuffer);
            GEK_CHECK_EXCEPTION(!directSoundBuffer, Audio::Exception, "Unable to duplicate sound buffer: %d", resultValue);

            CComQIPtr<IDirectSoundBuffer8, &IID_IDirectSoundBuffer8> directSound8Buffer(directSoundBuffer);
            GEK_CHECK_EXCEPTION(!directSound8Buffer, Audio::Exception, "Unable to query for advanced sound buffer");

            return std::dynamic_pointer_cast<AudioEffect>(std::make_shared<EffectImplementation>(directSound8Buffer.p));
        }

        std::shared_ptr<AudioSound> copySound(AudioSound *source)
        {
            GEK_REQUIRE(directSound);
            GEK_REQUIRE(source);

            CComPtr<IDirectSoundBuffer> directSoundBuffer;
            HRESULT resultValue = directSound->DuplicateSoundBuffer(static_cast<IDirectSoundBuffer *>(source->getBuffer()), &directSoundBuffer);
            GEK_CHECK_EXCEPTION(!directSoundBuffer, Audio::Exception, "Unable to duplicate sound buffer: %d", resultValue);

            CComQIPtr<IDirectSoundBuffer8, &IID_IDirectSoundBuffer8> directSound8Buffer(directSoundBuffer);
            GEK_CHECK_EXCEPTION(!directSound8Buffer, Audio::Exception, "Unable to query for advanced sound buffer");

            CComQIPtr<IDirectSound3DBuffer8, &IID_IDirectSound3DBuffer8> directSound8Buffer3D(directSound8Buffer);
            GEK_CHECK_EXCEPTION(!directSound8Buffer3D, Audio::Exception, "Unable to query for 3D sound buffer");

            return std::dynamic_pointer_cast<AudioSound>(std::make_shared<SoundImplementation>(directSound8Buffer.p, directSound8Buffer3D.p));
        }

        CComPtr<IDirectSoundBuffer> loadFromFile(LPCWSTR fileName, DWORD flags, GUID soundAlgorithm)
        {
            GEK_REQUIRE(directSound);
            GEK_REQUIRE(fileName);

            std::vector<UINT8> fileData;
            Gek::FileSystem::load(fileName, fileData);

            audiere::RefPtr<audiere::File> audiereFile(audiere::CreateMemoryFile(fileData.data(), fileData.size()));
            GEK_CHECK_EXCEPTION(!audiereFile, Audio::Exception, "Unable to create memory mapping of audio file");

            audiere::RefPtr<audiere::SampleSource> audiereSample(audiere::OpenSampleSource(audiereFile));
            GEK_CHECK_EXCEPTION(!audiereSample, Audio::Exception, "YUnable to open audio sample source data");
            
            int channelCount = 0;
            int samplesPerSecond = 0;
            audiere::SampleFormat sampleFormat;
            audiereSample->getFormat(channelCount, samplesPerSecond, sampleFormat);

            WAVEFORMATEX bufferFormat;
            bufferFormat.cbSize = 0;
            bufferFormat.nChannels = channelCount;
            bufferFormat.wBitsPerSample = (sampleFormat == audiere::SF_U8 ? 8 : 16);
            bufferFormat.nSamplesPerSec = samplesPerSecond;
            bufferFormat.nBlockAlign = (bufferFormat.wBitsPerSample / 8 * bufferFormat.nChannels);
            bufferFormat.nAvgBytesPerSec = (bufferFormat.nSamplesPerSec * bufferFormat.nBlockAlign);
            bufferFormat.wFormatTag = WAVE_FORMAT_PCM;

            DWORD sampleLength = audiereSample->getLength();
            sampleLength *= (bufferFormat.wBitsPerSample / 8);
            sampleLength *= channelCount;

            DSBUFFERDESC bufferDescription = { 0 };
            bufferDescription.dwSize = sizeof(DSBUFFERDESC);
            bufferDescription.dwBufferBytes = sampleLength;
            bufferDescription.lpwfxFormat = (WAVEFORMATEX *)&bufferFormat;
            bufferDescription.guid3DAlgorithm = soundAlgorithm;
            bufferDescription.dwFlags = flags;

            CComPtr<IDirectSoundBuffer> directSoundBuffer;
            HRESULT resultValue = directSound->CreateSoundBuffer(&bufferDescription, &directSoundBuffer, nullptr);
            GEK_CHECK_EXCEPTION(!directSoundBuffer, Audio::Exception, "Unable create sound buffer for audio file: %d", resultValue);

            void *sampleData = nullptr;
            resultValue = directSoundBuffer->Lock(0, sampleLength, &sampleData, &sampleLength, 0, 0, DSBLOCK_ENTIREBUFFER);
            GEK_CHECK_EXCEPTION(!sampleData, Audio::Exception, "Unable to lock sound buffer: %d", resultValue);

            audiereSample->read((sampleLength / bufferFormat.nBlockAlign), sampleData);
            directSoundBuffer->Unlock(sampleData, sampleLength, 0, 0);

            return directSoundBuffer;
        }

        std::shared_ptr<AudioEffect> loadEffect(LPCWSTR fileName)
        {
            GEK_REQUIRE(directSound);
            GEK_REQUIRE(fileName);

            CComPtr<IDirectSoundBuffer> directSoundBuffer(loadFromFile(fileName, DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY, GUID_NULL));

            CComQIPtr<IDirectSoundBuffer8, &IID_IDirectSoundBuffer8> directSound8Buffer(directSoundBuffer);
            GEK_CHECK_EXCEPTION(!directSound8Buffer, Audio::Exception, "Unable to query for advanced sound buffer");

            return std::dynamic_pointer_cast<AudioEffect>(std::make_shared<EffectImplementation>(directSound8Buffer.p));
        }

        std::shared_ptr<AudioSound> loadSound(LPCWSTR fileName)
        {
            GEK_REQUIRE(directSound);
            GEK_REQUIRE(fileName);

            CComPtr<IDirectSoundBuffer> directSoundBuffer(loadFromFile(fileName, DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY, GUID_NULL));

            CComQIPtr<IDirectSoundBuffer8, &IID_IDirectSoundBuffer8> directSound8Buffer(directSoundBuffer);
            GEK_CHECK_EXCEPTION(!directSound8Buffer, Audio::Exception, "Unable to query for advanced sound buffer");

            CComQIPtr<IDirectSound3DBuffer8, &IID_IDirectSound3DBuffer8> directSound8Buffer3D(directSound8Buffer);
            GEK_CHECK_EXCEPTION(!directSound8Buffer3D, Audio::Exception, "Unable to query for 3D sound buffer");

            return std::dynamic_pointer_cast<AudioSound>(std::make_shared<SoundImplementation>(directSound8Buffer.p, directSound8Buffer3D.p));
        }
    };

    REGISTER_CLASS(AudioSystemImplementation)
}; // namespace Gek
