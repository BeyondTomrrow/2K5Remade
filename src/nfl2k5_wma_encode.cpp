// Convert any audio file Windows can decode (MP3, AAC/M4A, FLAC, WAV, WMA)
// into the WMA the game's own soundtrack player decodes: Windows Media Audio
// 8/9 Standard (format tag 0x161), 44.1 kHz stereo, about 128 kbit/s, in an
// ASF file -- what an Xbox dashboard rip produced. Media Foundation does both
// halves, so the port needs no codec of its own.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")

template <class T> static void release(T *&p) { if (p) { p->Release(); p = nullptr; } }

// The encoder's own list of WMA 8/9 output types; take the 44.1 kHz stereo
// one closest to 128 kbit/s.
static IMFMediaType *pick_wma_type()
{
    IMFCollection *types = nullptr;
    IMFMediaType *best = nullptr;
    DWORD n = 0;
    UINT32 best_diff = 0xFFFFFFFFu;
    if (FAILED(MFTranscodeGetAudioOutputAvailableTypes(MFAudioFormat_WMAudioV8, MFT_ENUM_FLAG_ALL, nullptr, &types)))
        return nullptr;
    types->GetElementCount(&n);
    for (DWORD i = 0; i < n; i++) {
        IUnknown *u = nullptr;
        IMFMediaType *t = nullptr;
        UINT32 rate = 0, ch = 0, bps = 0;
        if (FAILED(types->GetElement(i, &u))) continue;
        u->QueryInterface(IID_PPV_ARGS(&t));
        u->Release();
        if (!t) continue;
        t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
        t->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bps);
        UINT32 diff = bps > 16000 ? bps - 16000 : 16000 - bps;
        if (rate == 44100 && ch == 2 && diff < best_diff) {
            release(best);
            best = t;
            best_diff = diff;
        } else {
            t->Release();
        }
    }
    types->Release();
    return best;
}

// Drop the Extended Content Description object (tags: WM/Encoder and the
// like) from an ASF file, in place. The XDK WMA decoder's parser for it
// rejects what Media Foundation writes, and with it the whole song
// (decoder create E_FAIL after reading the descriptor count, 2026-10-01).
// The dashboard's rips carried no such tags. Idempotent.
extern "C" int nfl2k5_asf_strip_tags(const wchar_t *path)
{
    static const uint8_t ext_cd[16] = { 0x40,0xA4,0xD0,0xD2,0x07,0xE3,0xD2,0x11,0x97,0xF0,0x00,0xA0,0xC9,0x5E,0xA8,0x50 };
    HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    GetFileSizeEx(f, &size);
    uint8_t *d = (uint8_t *)malloc((size_t)size.QuadPart);
    DWORD got = 0;
    int changed = 0;
    if (d && ReadFile(f, d, (DWORD)size.QuadPart, &got, nullptr) && got == size.QuadPart && got > 30) {
        uint64_t hsize;
        uint32_t count, keep = 0;
        memcpy(&hsize, d + 16, 8);
        memcpy(&count, d + 24, 4);
        uint64_t pos = 30, out = 30;
        for (uint32_t i = 0; i < count && pos + 24 <= hsize && hsize <= got; i++) {
            uint64_t osize;
            memcpy(&osize, d + pos + 16, 8);
            if (osize < 24 || pos + osize > hsize) break;
            if (memcmp(d + pos, ext_cd, 16)) {
                memmove(d + out, d + pos, (size_t)osize);
                out += osize;
                keep++;
            } else {
                changed = 1;
            }
            pos += osize;
        }
        if (changed) {
            memmove(d + out, d + hsize, (size_t)(got - hsize));
            memcpy(d + 16, &out, 8);
            memcpy(d + 24, &keep, 4);
            DWORD total = (DWORD)(out + (got - hsize)), w = 0;
            SetFilePointer(f, 0, nullptr, FILE_BEGIN);
            WriteFile(f, d, total, &w, nullptr);
            SetEndOfFile(f);
        }
    }
    free(d);
    CloseHandle(f);
    return changed;
}

// 0 on success; otherwise the failing HRESULT.
extern "C" long nfl2k5_wma_transcode(const wchar_t *src, const wchar_t *dst)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool co = SUCCEEDED(hr);
    IMFSourceReader *reader = nullptr;
    IMFSinkWriter *writer = nullptr;
    IMFMediaType *pcm = nullptr, *actual = nullptr, *wma = nullptr;
    DWORD stream = 0;

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) goto done;
    hr = MFCreateSourceReaderFromURL(src, nullptr, &reader);
    if (FAILED(hr)) goto done;
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    /* Decode to 16-bit PCM at the encoder's rate and layout. */
    MFCreateMediaType(&pcm);
    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 44100 * 4);
    pcm->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcm);
    if (FAILED(hr)) goto done;
    hr = reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
    if (FAILED(hr)) goto done;

    wma = pick_wma_type();
    if (!wma) { hr = MF_E_TOPO_CODEC_NOT_FOUND; goto done; }
    DeleteFileW(dst);
    {
        /* ASF regardless of the file name (the library writes to a temp name). */
        IMFAttributes *attr = nullptr;
        MFCreateAttributes(&attr, 1);
        attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_ASF);
        hr = MFCreateSinkWriterFromURL(dst, nullptr, attr, &writer);
        attr->Release();
    }
    if (FAILED(hr)) goto done;
    hr = writer->AddStream(wma, &stream);
    if (FAILED(hr)) goto done;
    hr = writer->SetInputMediaType(stream, actual, nullptr);
    if (FAILED(hr)) goto done;
    hr = writer->BeginWriting();
    if (FAILED(hr)) goto done;

    for (;;) {
        DWORD flags = 0;
        LONGLONG ts = 0;
        IMFSample *sample = nullptr;
        hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &ts, &sample);
        if (FAILED(hr)) break;
        if (sample) {
            hr = writer->WriteSample(stream, sample);
            sample->Release();
            if (FAILED(hr)) break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    if (SUCCEEDED(hr))
        hr = writer->Finalize();
    release(writer);
    if (SUCCEEDED(hr))
        nfl2k5_asf_strip_tags(dst);

done:
    release(writer);
    release(reader);
    release(pcm);
    release(actual);
    release(wma);
    MFShutdown();
    if (co) CoUninitialize();
    return SUCCEEDED(hr) ? 0 : (long)hr;
}

#ifdef NFL2K5_WMA_ENCODE_MAIN
int wmain(int argc, wchar_t **argv)
{
    if (argc < 3) { fwprintf(stderr, L"usage: wma-encode <in> <out.wma>\n"); return 2; }
    long r = nfl2k5_wma_transcode(argv[1], argv[2]);
    if (r) fwprintf(stderr, L"failed: 0x%08lX\n", (unsigned long)r);
    return r ? 1 : 0;
}
#endif
