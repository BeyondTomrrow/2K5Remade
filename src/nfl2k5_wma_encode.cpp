// Convert any audio file (MP3, AAC/M4A, FLAC, WAV, OGG, WMA) into the WMA
// format the game's own soundtrack player decodes: Windows Media Audio 8/9
// Standard (format tag 0x161 / wmav2), 44.1 kHz stereo, ~128 kbit/s, in an
// ASF container with Extended Content Description tags stripped.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Portable UTF-8 version of the ASF Extended Content Description tag stripper.
// The XDK WMA decoder rejects files containing WM/Encoder or Lavf tags.
extern "C" int nfl2k5_asf_strip_tags_utf8(const char *path)
{
    static const uint8_t ext_cd[16] = {
        0x40,0xA4,0xD0,0xD2,0x07,0xE3,0xD2,0x11,
        0x97,0xF0,0x00,0xA0,0xC9,0x5E,0xA8,0x50
    };
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 30) { fclose(f); return 0; }

    size_t got = (size_t)fsize;
    uint8_t *d = (uint8_t *)malloc(got);
    if (!d || fread(d, 1, got, f) != got) {
        free(d);
        fclose(f);
        return 0;
    }
    fclose(f);

    uint64_t hsize = 0;
    uint32_t count = 0, keep = 0;
    memcpy(&hsize, d + 16, 8);
    memcpy(&count, d + 24, 4);

    int changed = 0;
    uint64_t pos = 30, out = 30;
    for (uint32_t i = 0; i < count && pos + 24 <= hsize && hsize <= got; i++) {
        uint64_t osize = 0;
        memcpy(&osize, d + pos + 16, 8);
        if (osize < 24 || pos + osize > hsize) break;
        if (memcmp(d + pos, ext_cd, 16) != 0) {
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
        size_t total = (size_t)(out + (got - hsize));
        f = fopen(path, "wb");
        if (f) {
            fwrite(d, 1, total, f);
            fclose(f);
        }
    }

    free(d);
    return changed;
}

#if defined(_WIN32)
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")

template <class T> static void release(T *&p) { if (p) { p->Release(); p = nullptr; } }

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

extern "C" int nfl2k5_asf_strip_tags(const wchar_t *path)
{
    char utf8[1024];
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof utf8, nullptr, nullptr))
        return 0;
    return nfl2k5_asf_strip_tags_utf8(utf8);
}

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

extern "C" long nfl2k5_wma_transcode_utf8(const char *src, const char *dst)
{
    wchar_t wsrc[1024], wdst[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, src, -1, wsrc, 1024) ||
        !MultiByteToWideChar(CP_UTF8, 0, dst, -1, wdst, 1024))
        return -1;
    return nfl2k5_wma_transcode(wsrc, wdst);
}

#else /* POSIX: FFmpeg wmav2 transcode + ASF Extended Content Description strip */

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

extern "C" int nfl2k5_asf_strip_tags(const wchar_t *path)
{
    char utf8[1024];
    if (wcstombs(utf8, path, sizeof utf8) == (size_t)-1)
        return 0;
    return nfl2k5_asf_strip_tags_utf8(utf8);
}

extern "C" long nfl2k5_wma_transcode_utf8(const char *src, const char *dst)
{
    unlink(dst);

    const char *argv[] = {
        "ffmpeg",
        "-nostdin",
        "-y",
        "-v", "error",
        "-i", src,
        "-vn",
        "-map_metadata", "-1",
        "-acodec", "wmav2",
        "-ar", "44100",
        "-ac", "2",
        "-b:a", "128k",
        "-f", "asf",
        dst,
        nullptr
    };

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, "ffmpeg", nullptr, nullptr, const_cast<char *const *>(argv), environ);
    if (rc != 0)
        return -1;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;

    nfl2k5_asf_strip_tags_utf8(dst);
    return 0;
}

extern "C" long nfl2k5_wma_transcode(const wchar_t *src, const wchar_t *dst)
{
    char u8_src[1024], u8_dst[1024];
    if (wcstombs(u8_src, src, sizeof u8_src) == (size_t)-1 ||
        wcstombs(u8_dst, dst, sizeof u8_dst) == (size_t)-1)
        return -1;
    return nfl2k5_wma_transcode_utf8(u8_src, u8_dst);
}
#endif