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

#if defined(__ANDROID__) && defined(NFL2K5_HAVE_FFMPEG)
/* Android has no ffmpeg program to run: the same conversion in process with
 * FFmpeg's libraries (a minimal static build, android/ffmpeg): decode,
 * resample to 44.1 kHz stereo, encode wmav2 at 128 kbit/s, write ASF with no
 * tags. */
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
}

namespace {

struct Transcode {
    AVFormatContext *in = nullptr, *out = nullptr;
    AVCodecContext *dec = nullptr, *enc = nullptr;
    SwrContext *swr = nullptr;
    AVAudioFifo *fifo = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr, *ef = nullptr;
    int stream = -1;
    int64_t pts = 0;
    ~Transcode()
    {
        av_frame_free(&frame);
        av_frame_free(&ef);
        av_packet_free(&pkt);
        if (fifo) av_audio_fifo_free(fifo);
        swr_free(&swr);
        avcodec_free_context(&dec);
        avcodec_free_context(&enc);
        if (out) {
            if (out->pb) avio_closep(&out->pb);
            avformat_free_context(out);
        }
        avformat_close_input(&in);
    }

    /* Encode whole frames from the FIFO (all that is left when flushing). */
    int drain(bool flush)
    {
        const int fs = enc->frame_size > 0 ? enc->frame_size : 2048;
        while (av_audio_fifo_size(fifo) >= fs || (flush && av_audio_fifo_size(fifo) > 0)) {
            int n = FFMIN(fs, av_audio_fifo_size(fifo));
            av_frame_unref(ef);
            ef->nb_samples = fs;
            ef->format = enc->sample_fmt;
            ef->sample_rate = enc->sample_rate;
            av_channel_layout_copy(&ef->ch_layout, &enc->ch_layout);
            if (av_frame_get_buffer(ef, 0) < 0) return -1;
            if (n < fs)   /* the last frame: silence after the song */
                av_samples_set_silence(ef->extended_data, n, fs - n, enc->ch_layout.nb_channels, enc->sample_fmt);
            if (av_audio_fifo_read(fifo, (void **)ef->extended_data, n) < n) return -1;
            ef->pts = pts;
            pts += fs;
            if (avcodec_send_frame(enc, ef) < 0 || write_packets() < 0) return -1;
        }
        return 0;
    }

    int write_packets()
    {
        for (;;) {
            int r = avcodec_receive_packet(enc, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
            if (r < 0) return -1;
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc->time_base, out->streams[0]->time_base);
            r = av_interleaved_write_frame(out, pkt);
            if (r < 0) return -1;
        }
    }

    int convert(AVFrame *f)
    {
        uint8_t **buf = nullptr;
        int max = swr_get_out_samples(swr, f ? f->nb_samples : 0);
        if (max <= 0) return 0;
        if (av_samples_alloc_array_and_samples(&buf, nullptr, enc->ch_layout.nb_channels, max, enc->sample_fmt, 0) < 0)
            return -1;
        int n = swr_convert(swr, buf, max, f ? (const uint8_t **)f->extended_data : nullptr, f ? f->nb_samples : 0);
        int r = n < 0 ? -1 : (n > 0 && av_audio_fifo_write(fifo, (void **)buf, n) < n ? -1 : 0);
        av_freep(&buf[0]);
        av_freep(&buf);
        return r;
    }

    int decoded()
    {
        for (;;) {
            int r = avcodec_receive_frame(dec, frame);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
            if (r < 0) return -1;
            if (convert(frame) < 0 || drain(false) < 0) return -1;
            av_frame_unref(frame);
        }
    }

    long run(const char *src, const char *dst)
    {
        if (avformat_open_input(&in, src, nullptr, nullptr) < 0 || avformat_find_stream_info(in, nullptr) < 0) return -2;
        const AVCodec *dc = nullptr;
        stream = av_find_best_stream(in, AVMEDIA_TYPE_AUDIO, -1, -1, &dc, 0);
        if (stream < 0 || !dc) return -3;
        dec = avcodec_alloc_context3(dc);
        if (!dec || avcodec_parameters_to_context(dec, in->streams[stream]->codecpar) < 0 || avcodec_open2(dec, dc, nullptr) < 0)
            return -4;
        const AVCodec *ec = avcodec_find_encoder(AV_CODEC_ID_WMAV2);
        if (!ec) return -5;
        enc = avcodec_alloc_context3(ec);
        enc->sample_rate = 44100;
        av_channel_layout_default(&enc->ch_layout, 2);
        enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
        enc->bit_rate = 128000;
        enc->time_base = AVRational{ 1, 44100 };
        if (avformat_alloc_output_context2(&out, nullptr, "asf", dst) < 0 || !out) return -6;
        if (out->oformat->flags & AVFMT_GLOBALHEADER) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(enc, ec, nullptr) < 0) return -7;
        AVStream *os = avformat_new_stream(out, nullptr);
        if (!os || avcodec_parameters_from_context(os->codecpar, enc) < 0) return -8;
        os->time_base = enc->time_base;
        if (avio_open(&out->pb, dst, AVIO_FLAG_WRITE) < 0 || avformat_write_header(out, nullptr) < 0) return -9;
        if (swr_alloc_set_opts2(&swr, &enc->ch_layout, enc->sample_fmt, enc->sample_rate,
                                &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0, nullptr) < 0 || swr_init(swr) < 0)
            return -10;
        fifo = av_audio_fifo_alloc(enc->sample_fmt, enc->ch_layout.nb_channels, 8192);
        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        ef = av_frame_alloc();
        if (!fifo || !pkt || !frame || !ef) return -11;
        while (av_read_frame(in, pkt) >= 0) {
            int r = 0;
            if (pkt->stream_index == stream) r = avcodec_send_packet(dec, pkt) < 0 ? 0 : decoded();   /* a bad packet is skipped */
            av_packet_unref(pkt);
            if (r < 0) return -12;
        }
        avcodec_send_packet(dec, nullptr);
        if (decoded() < 0 || convert(nullptr) < 0 || drain(true) < 0) return -13;
        avcodec_send_frame(enc, nullptr);
        if (write_packets() < 0 || av_write_trailer(out) < 0) return -14;
        return 0;
    }
};

} // namespace

extern "C" long nfl2k5_wma_transcode_utf8(const char *src, const char *dst)
{
    unlink(dst);
    long r;
    {
        Transcode t;
        r = t.run(src, dst);
    }
    if (r != 0) {
        unlink(dst);
        return r;
    }
    nfl2k5_asf_strip_tags_utf8(dst);
    return 0;
}

#else
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

#endif /* __ANDROID__ && NFL2K5_HAVE_FFMPEG */

extern "C" long nfl2k5_wma_transcode(const wchar_t *src, const wchar_t *dst)
{
    char u8_src[1024], u8_dst[1024];
    if (wcstombs(u8_src, src, sizeof u8_src) == (size_t)-1 ||
        wcstombs(u8_dst, dst, sizeof u8_dst) == (size_t)-1)
        return -1;
    return nfl2k5_wma_transcode_utf8(u8_src, u8_dst);
}
#endif