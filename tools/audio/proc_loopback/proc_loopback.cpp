// proc_loopback.exe <pid> <out.wav> <seconds> [gain]
// Windows 10 2004+ per-process WASAPI loopback: records what one process (tree) plays, as the
// audio engine mixes it (float, after that session's volume), into a 48 kHz stereo s16 WAV.
// [gain] (default 1) undoes a tiny session volume (e.g. 1000 for 0.001), so an oracle run can be
// inaudible for the user yet recorded at full precision. pid 0 = ordinary loopback of the default
// render endpoint (everything that plays). docs/AUDIO-USER-REPORTS-20261022.md.
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mfapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")
struct Handler : IActivateAudioInterfaceCompletionHandler {
    HANDLE ev; IAudioClient *client = nullptr; HRESULT hr = E_FAIL; LONG ref = 1;
    Handler() { ev = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    STDMETHODIMP QueryInterface(REFIID iid, void **pp) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler) || iid == __uuidof(IAgileObject)) { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE; }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&ref); }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override {
        IUnknown *u = nullptr; HRESULT ahr = E_FAIL;
        op->GetActivateResult(&ahr, &u);
        hr = ahr;
        if (SUCCEEDED(ahr) && u) { u->QueryInterface(__uuidof(IAudioClient), (void **)&client); u->Release(); }
        SetEvent(ev); return S_OK; }
};
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: proc_loopback <pid|0> <out.wav> <seconds> [gain]\n"); return 2; }
    DWORD pid = (DWORD)strtoul(argv[1], nullptr, 10); const char *out = argv[2]; double secs = atof(argv[3]);
    float gain = argc > 4 ? (float)atof(argv[4]) : 1.0f;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Handler h; HRESULT hr;
    if (pid == 0) {
        IMMDeviceEnumerator *en = nullptr; IMMDevice *dev = nullptr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en);
        if (FAILED(hr) || FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) { fprintf(stderr, "no default endpoint\n"); return 1; }
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&h.client);
        if (FAILED(hr)) { fprintf(stderr, "Activate failed %08lx\n", hr); return 1; }
    } else {
        AUDIOCLIENT_ACTIVATION_PARAMS ap = {};
        ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        ap.ProcessLoopbackParams.TargetProcessId = pid;
        ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT pv = {}; pv.vt = VT_BLOB; pv.blob.cbSize = sizeof ap; pv.blob.pBlobData = (BYTE *)&ap;
        IActivateAudioInterfaceAsyncOperation *op = nullptr;
        hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, &h, &op);
        if (FAILED(hr)) { fprintf(stderr, "ActivateAudioInterfaceAsync failed %08lx\n", hr); return 1; }
        WaitForSingleObject(h.ev, 5000); if (op) op->Release();
        if (!h.client) { fprintf(stderr, "activation failed %08lx\n", h.hr); return 1; }
    }
    WAVEFORMATEX wf = {}; wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; wf.nChannels = 2; wf.nSamplesPerSec = 48000; wf.wBitsPerSample = 32;
    wf.nBlockAlign = 8; wf.nAvgBytesPerSec = 48000 * 8;
    DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK;
    if (pid) flags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    else flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = h.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, pid ? 0 : 2000000, 0, &wf, nullptr);
    if (FAILED(hr)) { fprintf(stderr, "Initialize failed %08lx\n", hr); return 1; }
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr); if (pid) h.client->SetEventHandle(ev);
    IAudioCaptureClient *cap = nullptr; h.client->GetService(__uuidof(IAudioCaptureClient), (void **)&cap);
    FILE *f = fopen(out, "wb"); if (!f) { fprintf(stderr, "cannot open %s\n", out); return 1; }
    unsigned char hdr[44] = {0}; fwrite(hdr, 1, 44, f);
    h.client->Start();
    ULONGLONG t0 = GetTickCount64(); unsigned long long frames = 0, silent_pad = 0; long long nonzero = 0; float rawpeak = 0.0f;
    while (GetTickCount64() - t0 < (ULONGLONG)(secs * 1000)) {
        if (pid) WaitForSingleObject(ev, 200); else Sleep(20);
        UINT32 n = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&n)) && n) {
            BYTE *d; UINT32 got; DWORD fl; UINT64 devpos, qpc;
            if (FAILED(cap->GetBuffer(&d, &got, &fl, &devpos, &qpc))) break;
            std::vector<short> o(got * 2, 0);
            if (fl & AUDCLNT_BUFFERFLAGS_SILENT) silent_pad += got;
            else {
                const float *s = (const float *)d;
                for (UINT32 i = 0; i < got * 2; i++) {
                    float x = s[i] * gain; if (x > 1.0f) x = 1.0f; if (x < -1.0f) x = -1.0f;
                    o[i] = (short)(x * 32767.0f);
                    if (s[i] != 0.0f) nonzero++;
                    float a = s[i] < 0 ? -s[i] : s[i]; if (a > rawpeak) rawpeak = a;
                }
            }
            fwrite(o.data(), 2, got * 2, f);
            frames += got; cap->ReleaseBuffer(got);
        }
    }
    h.client->Stop();
    unsigned long long bytes = frames * 4; unsigned int v; unsigned short s16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); v = (unsigned)(36 + bytes); fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); s16 = 1; fwrite(&s16, 2, 1, f); s16 = 2; fwrite(&s16, 2, 1, f);
    v = 48000; fwrite(&v, 4, 1, f); v = 48000 * 4; fwrite(&v, 4, 1, f); s16 = 4; fwrite(&s16, 2, 1, f); s16 = 16; fwrite(&s16, 2, 1, f);
    fwrite("data", 1, 4, f); v = (unsigned)bytes; fwrite(&v, 4, 1, f); fclose(f);
    printf("%s: %.1f s, %llu frames, silent-flag frames %llu, nonzero samples %lld, raw peak %.3e (%.1f dBFS)\n",
           out, frames / 48000.0, frames, silent_pad, nonzero, rawpeak, 20.0 * log10(rawpeak + 1e-30));
    return 0;
}
