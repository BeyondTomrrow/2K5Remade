/*
 * uwp_selftest.cpp -- self-tests of the UWP game exe's platform pieces, run
 * without package identity (and so inside an AppContainer by
 * tools/uwp/appcontainer-run.exe, or directly):
 *
 *   NFL2K5.exe --audio-test <out dir>
 *       SDL audio through the UWP-minimal SDL: which driver opened (must be
 *       "xaudio2"), and whether 1.5 s of audio (silence; a quiet 440 Hz tone with NFL2K5_SELFTEST_TONE=1) was consumed by the
 *       device at the expected rate.
 *   NFL2K5.exe --music-test <out dir> <file> [<file> ...]
 *       The custom-music converter (nfl2k5_wma_transcode_utf8, Media
 *       Foundation decode + WMA encode) on each file; reports the result and
 *       checks that the output is an ASF/WMA file.
 *
 * Results go to <out dir>\uwp-selftest.txt; the exit code is the number of
 * failures.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" long nfl2k5_wma_transcode_utf8(const char *src, const char *dst);

namespace {

FILE *s_out;
int s_fail;

void report(bool ok, const char *name, const char *fmt, ...)
{
    char detail[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    if (!ok) s_fail++;
    if (s_out) { fprintf(s_out, "[%s] %-22s %s\n", ok ? "PASS" : "FAIL", name, detail); fflush(s_out); }
}

std::string utf8(const std::wstring &w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

/* Splits a command line into arguments (quotes honoured). */
std::vector<std::wstring> split(const wchar_t *cmd)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    bool q = false, any = false;
    for (const wchar_t *p = cmd; *p; p++) {
        if (*p == L'"') { q = !q; any = true; continue; }
        if (*p == L' ' && !q) { if (any) out.push_back(cur); cur.clear(); any = false; continue; }
        cur += *p;
        any = true;
    }
    if (any) out.push_back(cur);
    return out;
}

void audio_test()
{
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        report(false, "SDL audio init", "%s", SDL_GetError());
        return;
    }
    const char *drv = SDL_GetCurrentAudioDriver();
    report(drv && strcmp(drv, "xaudio2") == 0, "audio driver", "%s", drv ? drv : "(none)");
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 48000};
    SDL_AudioStream *st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!st) {
        report(false, "open playback", "%s", SDL_GetError());
        SDL_Quit();
        return;
    }
    SDL_AudioSpec dev = {};
    int frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(st), &dev, &frames);
    report(true, "device format", "%d Hz, %d ch, %d-frame buffer", dev.freq, dev.channels, frames);
    const int n = 48000 * 3 / 2;
    std::vector<float> pcm((size_t)n * 2);
    for (int i = 0; i < n; i++) {
        /* Silent by default (other tests on this PC may be recording the
         * output); NFL2K5_SELFTEST_TONE=1 makes it a quiet 440 Hz tone. */
        float v = getenv("NFL2K5_SELFTEST_TONE") ? 0.05f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f) : 0.0f;
        pcm[(size_t)i * 2] = pcm[(size_t)i * 2 + 1] = v;
    }
    SDL_PutAudioStreamData(st, pcm.data(), (int)(pcm.size() * sizeof(float)));
    SDL_ResumeAudioStreamDevice(st);
    Uint64 t0 = SDL_GetTicks();
    int start = SDL_GetAudioStreamQueued(st);
    while (SDL_GetAudioStreamQueued(st) > 0 && SDL_GetTicks() - t0 < 5000) SDL_Delay(20);
    double secs = (SDL_GetTicks() - t0) / 1000.0;
    int left = SDL_GetAudioStreamQueued(st);
    /* 1.5 s of audio should drain in roughly 1.5 s (a dummy driver also
     * drains, but at a timer's pace; the driver check above tells them apart). */
    report(left == 0 && secs > 1.0 && secs < 3.5, "tone consumed", "%d bytes in %.2f s, %d left", start, secs, left);
    SDL_DestroyAudioStream(st);
    SDL_Quit();
}

bool is_asf(const std::string &path, long *size)
{
    static const unsigned char asf[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                          0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char h[16] = {};
    size_t got = fread(h, 1, 16, f);
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fclose(f);
    return got == 16 && memcmp(h, asf, 16) == 0;
}

void music_test(const std::wstring &out_dir, const std::vector<std::wstring> &files)
{
    for (const std::wstring &w : files) {
        std::string src = utf8(w);
        std::wstring base = w.substr(w.find_last_of(L"\\/") + 1);
        std::string dst = utf8(out_dir + L"\\" + base + L".converted.wma");
        DWORD t0 = GetTickCount();
        long r = nfl2k5_wma_transcode_utf8(src.c_str(), dst.c_str());
        long size = 0;
        bool asf = r == 0 && is_asf(dst, &size);
        report(asf, "convert to WMA", "%s -> 0x%08lX, %ld bytes, %lu ms", utf8(base).c_str(), (unsigned long)r, size,
               GetTickCount() - t0);
    }
}

} // namespace

/* Returns -1 when the command line asks for no self-test. */
extern "C" int uwp_selftest_main(const wchar_t *cmd)
{
    std::vector<std::wstring> a = split(cmd);
    if (a.size() < 2 || (a[0] != L"--audio-test" && a[0] != L"--music-test")) return -1;
    std::wstring out = a[1];
    s_out = _wfopen((out + L"\\uwp-selftest.txt").c_str(), L"a");
    if (s_out) fprintf(s_out, "NFL2K5 UWP self-test %ls\n", a[0].c_str());
    if (a[0] == L"--audio-test") audio_test();
    else music_test(out, std::vector<std::wstring>(a.begin() + 2, a.end()));
    if (s_out) fclose(s_out);
    return s_fail;
}
