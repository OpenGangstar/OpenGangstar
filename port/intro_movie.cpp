#include "windows_audio.h"
#include "intro_movie.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Microsoft::WRL::ComPtr;
constexpr DWORD video = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
constexpr DWORD audio = MF_SOURCE_READER_FIRST_AUDIO_STREAM;

void check(HRESULT result, const char* action)
{
    if (FAILED(result))
        throw std::runtime_error(std::string(action) + " failed with HRESULT " + std::to_string(uint32_t(result)));
}

class MediaFoundation {
public:
    MediaFoundation()
    {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
        const auto result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(result)) CoUninitialize();
        check(result, "MFStartup");
    }

    ~MediaFoundation()
    {
        MFShutdown();
        CoUninitialize();
    }
};

class BufferLock {
public:
    explicit BufferLock(IMFMediaBuffer* buffer) : buffer(buffer)
    {
        check(buffer->Lock(&bytes, nullptr, &length), "IMFMediaBuffer::Lock");
    }

    ~BufferLock() { buffer->Unlock(); }
    BYTE* bytes = nullptr;
    DWORD length = 0;

private:
    IMFMediaBuffer* buffer;
};


class MovieAudio {
public:
    explicit MovieAudio(const std::filesystem::path& path)
    {
        check(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader), "Open intro audio");
        check(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE), "Reset intro audio streams");
        check(reader->SetStreamSelection(audio, TRUE), "Select intro audio stream");
        ComPtr<IMFMediaType> type;
        check(MFCreateMediaType(&type), "Create intro audio type");
        check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Select audio type");
        check(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Select PCM audio format");
        check(reader->SetCurrentMediaType(audio, nullptr, type.Get()), "Decode intro audio as PCM");
        check(reader->GetCurrentMediaType(audio, &type), "Get intro audio format");
        WAVEFORMATEX* wave = nullptr;
        check(MFCreateWaveFormatExFromMFMediaType(type.Get(), &wave, nullptr), "Read intro wave format");
        const std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(wave, CoTaskMemFree);
        sampleRate = wave->nSamplesPerSec;
        blockAlign = wave->nBlockAlign;
        if (!sampleRate || !blockAlign) throw std::runtime_error("The intro audio format is invalid.");
        checkWaveOutput(waveOutOpen(&device, WAVE_MAPPER, wave, 0, 0, CALLBACK_NULL), "Open intro audio output");
    }

    ~MovieAudio()
    {
        waveOutReset(device);
        for (auto& buffer : pending)
            waveOutUnprepareHeader(device, &buffer.header, sizeof buffer.header);
        waveOutClose(device);
    }

    void start()
    {
        checkWaveOutput(waveOutPause(device), "Prepare intro audio output");
        refill();
        if (!firstTimestamp) throw std::runtime_error("The intro audio has no PCM samples.");
        checkWaveOutput(waveOutRestart(device), "Start intro audio output");
    }

    std::optional<double> positionMs()
    {
        refill();
        MMTIME time{};
        time.wType = TIME_SAMPLES;
        checkWaveOutput(waveOutGetPosition(device, &time, sizeof time), "Read intro audio position");
        if (time.wType == TIME_SAMPLES)
            playedMs = double(time.u.sample) * 1000 / sampleRate;
        else if (time.wType == TIME_BYTES)
            playedMs = double(time.u.cb) * 1000 / (double(sampleRate) * blockAlign);
        else if (time.wType == TIME_MS)
            playedMs = time.u.ms;
        else
            throw std::runtime_error("The audio output returned an unsupported clock.");
        if (atEnd && pending.empty()) return std::nullopt;
        return firstTimestamp.value_or(0) + playedMs;
    }

    double playbackMs() const { return playedMs; }

private:
    struct Chunk {
        std::vector<BYTE> bytes;
        WAVEHDR header{};
    };

    void refill()
    {
        while (!pending.empty() && (pending.front().header.dwFlags & WHDR_DONE)) {
            auto& header = pending.front().header;
            checkWaveOutput(waveOutUnprepareHeader(device, &header, sizeof header), "Release played intro audio");
            pending.pop_front();
        }
        // Keep roughly a quarter-second of the retail AAC track queued.
        while (!atEnd && pending.size() < 12) {
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            check(reader->ReadSample(audio, 0, nullptr, &flags, &timestamp, &sample), "Decode intro audio sample");
            if (flags & MF_SOURCE_READERF_ERROR)
                throw std::runtime_error("The intro audio stream reported an error.");
            atEnd = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
            if (!sample) continue;
            ComPtr<IMFMediaBuffer> media;
            check(sample->ConvertToContiguousBuffer(&media), "Get intro PCM sample");
            const BufferLock locked(media.Get());
            if (!locked.length) continue;
            if (locked.length % blockAlign != 0) throw std::runtime_error("The intro PCM sample is incomplete.");
            if (!firstTimestamp) firstTimestamp = double(timestamp) / 10000;
            auto& chunk = pending.emplace_back();
            chunk.bytes.assign(locked.bytes, locked.bytes + locked.length);
            chunk.header.lpData = reinterpret_cast<LPSTR>(chunk.bytes.data());
            chunk.header.dwBufferLength = locked.length;
            checkWaveOutput(waveOutPrepareHeader(device, &chunk.header, sizeof chunk.header), "Prepare intro PCM sample");
            checkWaveOutput(waveOutWrite(device, &chunk.header, sizeof chunk.header), "Queue intro PCM sample");
        }
    }

    ComPtr<IMFSourceReader> reader;
    HWAVEOUT device = nullptr;
    std::deque<Chunk> pending;
    std::optional<double> firstTimestamp;
    DWORD sampleRate = 0;
    WORD blockAlign = 0;
    bool atEnd = false;
    double playedMs = 0;
};
}

struct IntroMovie::Decoder {
    MediaFoundation platform;
    ComPtr<IMFSourceReader> reader;
    std::filesystem::path path;
    std::unique_ptr<MovieAudio> sound;
    UINT32 width = 0;
    UINT32 height = 0;
    LONG stride = 0;
    double duration = 0;

    void readFormat()
    {
        ComPtr<IMFMediaType> current;
        check(reader->GetCurrentMediaType(video, &current), "GetCurrentMediaType");
        check(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height), "Read intro frame size");
        if (!width || !height || width > 4096 || height > 4096)
            throw std::runtime_error("The intro movie has unsupported dimensions.");
        UINT32 defaultStride = 0;
        if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &defaultStride)))
            stride = static_cast<LONG>(defaultStride);
        else
            check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride), "Read intro stride");
    }
};

IntroMovie::IntroMovie(const std::filesystem::path& path) : decoder(std::make_unique<Decoder>())
{
    decoder->path = path;
    ComPtr<IMFAttributes> attributes;
    check(MFCreateAttributes(&attributes, 1), "MFCreateAttributes");
    check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "Enable intro color conversion");
    auto& reader = decoder->reader;
    check(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader), "Open intro.mp4");
    check(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE), "Reset intro streams");
    check(reader->SetStreamSelection(video, TRUE), "Select intro video stream");
    ComPtr<IMFMediaType> target;
    check(MFCreateMediaType(&target), "MFCreateMediaType");
    check(target->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Select video type");
    check(target->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Select RGB32 video format");
    check(reader->SetCurrentMediaType(video, nullptr, target.Get()), "Decode intro.mp4 as RGB32");
    decoder->readFormat();
    PROPVARIANT duration{};
    check(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration),
          "Read intro duration");
    if (duration.vt == VT_UI8) decoder->duration = double(duration.uhVal.QuadPart) / 10000;
    PropVariantClear(&duration);
}

IntroMovie::~IntroMovie() = default;
uint32_t IntroMovie::width() const { return decoder->width; }
uint32_t IntroMovie::height() const { return decoder->height; }
double IntroMovie::durationMs() const { return decoder->duration; }

void IntroMovie::startAudio()
{
    try {
        decoder->sound = std::make_unique<MovieAudio>(decoder->path);
        decoder->sound->start();
    } catch (const std::runtime_error& error) {
        decoder->sound.reset();
        std::cerr << "Intro audio unavailable: " << error.what() << '\n';
    }
}

std::optional<double> IntroMovie::audioPositionMs()
{
    if (!decoder->sound) return std::nullopt;
    try {
        return decoder->sound->positionMs();
    } catch (const std::runtime_error& error) {
        stopAudio();
        std::cerr << "Intro audio interrupted: " << error.what() << '\n';
        return std::nullopt;
    }
}

void IntroMovie::stopAudio()
{
    if (!decoder->sound) return;
    std::cout << "Played " << static_cast<uint32_t>(decoder->sound->playbackMs())
              << " ms of intro audio.\n";
    decoder->sound.reset();
}

std::optional<MovieFrame> IntroMovie::next()
{
    const auto width = decoder->width, height = decoder->height;
    for (;;) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        check(decoder->reader->ReadSample(video, 0, nullptr, &flags, &timestamp, &sample), "Decode intro frame");
        if (flags & MF_SOURCE_READERF_ERROR) throw std::runtime_error("The intro video stream reported an error.");
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            decoder->readFormat();
            if (width != decoder->width || height != decoder->height)
                throw std::runtime_error("The intro video changed dimensions while playing.");
        }
        if (!sample) {
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return std::nullopt;
            continue;
        }
        ComPtr<IMFMediaBuffer> buffer;
        check(sample->ConvertToContiguousBuffer(&buffer), "Get intro frame pixels");
        const BufferLock locked(buffer.Get());
        const auto pitch = uint64_t(decoder->stride < 0 ? -int64_t(decoder->stride) : decoder->stride);
        const auto rowBytes = size_t(width) * sizeof(uint32_t);
        if (pitch < rowBytes || pitch * (height - 1) + rowBytes > locked.length)
            throw std::runtime_error("The intro decoder returned a truncated video frame.");
        Image image{width, height, std::vector<uint32_t>(size_t(width) * height)};
        for (uint32_t y = 0; y < height; ++y) {
            const auto row = decoder->stride < 0 ? height - 1 - y : y;
            std::memcpy(image.bgra.data() + size_t(y) * width, locked.bytes + size_t(pitch * row), rowBytes);
        }
        return MovieFrame{std::move(image), double(timestamp) / 10000};
    }
}
