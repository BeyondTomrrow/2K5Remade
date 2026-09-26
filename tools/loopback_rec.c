/* loopback_rec <out.wav> <seconds>
 * Records what the default audio device plays (WASAPI loopback) to a
 * 16-bit WAV at the device's mix rate. Used to capture xemu's audio (the
 * reference for our APU) and our own real speaker output.
 * Build: cl /O2 loopback_rec.c ole32.lib */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdlib.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator_, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator_, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient_, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient_, 0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

int main(int argc, char **argv)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *ac = NULL;
    IAudioCaptureClient *cc = NULL;
    WAVEFORMATEX *wf = NULL;
    FILE *f;
    unsigned long long frames = 0, want;
    int is_float;
    DWORD t0;
    if (argc < 3) { fprintf(stderr, "usage: loopback_rec out.wav seconds\n"); return 2; }
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator_, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_, (void **)&en)) ||
        FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev)) ||
        FAILED(IMMDevice_Activate(dev, &IID_IAudioClient_, CLSCTX_ALL, NULL, (void **)&ac)) ||
        FAILED(IAudioClient_GetMixFormat(ac, &wf)) ||
        FAILED(IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, wf, NULL)) ||
        FAILED(IAudioClient_GetService(ac, &IID_IAudioCaptureClient_, (void **)&cc))) {
        fprintf(stderr, "WASAPI loopback setup failed\n");
        return 1;
    }
    is_float = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
               (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                IsEqualGUID(&((WAVEFORMATEXTENSIBLE *)wf)->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_));
    printf("device mix: %lu Hz, %u ch, %u bit%s\n", wf->nSamplesPerSec, wf->nChannels, wf->wBitsPerSample, is_float ? " float" : "");
    f = fopen(argv[1], "wb");
    if (!f) return 1;
    {
        unsigned char hdr[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                                  0,0,0,0,0,0,0,0,4,0,16,0,'d','a','t','a',0,0,0,0 };
        *(DWORD *)(hdr + 24) = wf->nSamplesPerSec;
        *(DWORD *)(hdr + 28) = wf->nSamplesPerSec * 4;
        fwrite(hdr, 1, 44, f);
    }
    want = (unsigned long long)atoi(argv[2]) * wf->nSamplesPerSec;
    IAudioClient_Start(ac);
    t0 = GetTickCount();
    while (GetTickCount() - t0 < (DWORD)atoi(argv[2]) * 1000u + 500u && frames < want) {
        UINT32 n;
        Sleep(10);
        while (SUCCEEDED(IAudioCaptureClient_GetNextPacketSize(cc, &n)) && n) {
            BYTE *data; UINT32 got; DWORD flags; UINT32 i;
            if (FAILED(IAudioCaptureClient_GetBuffer(cc, &data, &got, &flags, NULL, NULL))) break;
            for (i = 0; i < got; i++) {
                short s[2];
                int c;
                for (c = 0; c < 2; c++) {
                    int ch = c < wf->nChannels ? c : 0;
                    float v;
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) v = 0;
                    else if (is_float) v = ((float *)data)[i * wf->nChannels + ch];
                    else v = ((short *)data)[i * wf->nChannels + ch] / 32768.0f;
                    v = v > 1 ? 1 : v < -1 ? -1 : v;
                    s[c] = (short)(v * 32767);
                }
                fwrite(s, 2, 2, f);
            }
            frames += got;
            IAudioCaptureClient_ReleaseBuffer(cc, got);
        }
    }
    IAudioClient_Stop(ac);
    {
        DWORD data_bytes = (DWORD)(frames * 4), riff = data_bytes + 36;
        fseek(f, 4, SEEK_SET); fwrite(&riff, 4, 1, f);
        fseek(f, 40, SEEK_SET); fwrite(&data_bytes, 4, 1, f);
    }
    fclose(f);
    printf("%llu frames\n", frames);
    return 0;
}
