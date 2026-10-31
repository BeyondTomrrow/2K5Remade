/*
 * uwp_mf_shim.cpp -- MFTranscodeGetAudioOutputAvailableTypes for UWP.
 *
 * Custom music (nfl2k5_local_music.cpp -> nfl2k5_wma_encode.cpp) decodes
 * MP3/AAC/WMA/WAV with the Media Foundation source reader and encodes the
 * WMA the game's soundtrack player needs with the sink writer; all of that
 * is in the UWP API set. The one helper that is not, the list of the WMA
 * encoder's output types, is rebuilt here from MFTEnumEx + the encoder MFT
 * (both UWP APIs): the encoder is given 44.1 kHz stereo 16-bit PCM input,
 * the format nfl2k5_wma_encode.cpp feeds it, and lists its output types.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>

extern "C" HRESULT WINAPI MFTranscodeGetAudioOutputAvailableTypes(REFGUID subtype, DWORD flags, IMFAttributes *codec_config,
                                                                  IMFCollection **out)
{
    (void)codec_config;
    if (!out) return E_POINTER;
    *out = nullptr;
    IMFCollection *coll = nullptr;
    HRESULT hr = MFCreateCollection(&coll);
    if (FAILED(hr)) return hr;

    MFT_REGISTER_TYPE_INFO info = {MFMediaType_Audio, subtype};
    IMFActivate **acts = nullptr;
    UINT32 n = 0;
    hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, flags ? flags : MFT_ENUM_FLAG_ALL, nullptr, &info, &acts, &n);
    for (UINT32 a = 0; SUCCEEDED(hr) && a < n; a++) {
        IMFTransform *mft = nullptr;
        if (SUCCEEDED(acts[a]->ActivateObject(IID_PPV_ARGS(&mft)))) {
            IMFMediaType *pcm = nullptr;
            if (SUCCEEDED(MFCreateMediaType(&pcm))) {
                pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
                pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
                pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
                pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 44100 * 4);
                pcm->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
                mft->SetInputType(0, pcm, 0);   /* encoders list outputs for the current input */
                pcm->Release();
            }
            for (DWORD i = 0;; i++) {
                IMFMediaType *t = nullptr;
                if (FAILED(mft->GetOutputAvailableType(0, i, &t)) || !t) break;
                coll->AddElement(t);
                t->Release();
            }
            mft->Release();
            acts[a]->ShutdownObject();
        }
    }
    for (UINT32 a = 0; a < n; a++) acts[a]->Release();
    CoTaskMemFree(acts);
    DWORD count = 0;
    coll->GetElementCount(&count);
    if (!count) {
        coll->Release();
        return MF_E_TOPO_CODEC_NOT_FOUND;
    }
    *out = coll;
    return S_OK;
}
