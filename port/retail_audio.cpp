#include "retail_audio.h"
#include "city_assets.h"
#include "vorbis_audio.h"

#include "FileSystemBase.h"
#include "IFileStream.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <stdexcept>
#include <string_view>
#include <vector>

std::string retailSoundPath(unsigned id)
{
    const auto digits = std::to_string(id);
    return "raw_" + std::string(digits.size() < 3 ? 3 - digits.size() : 0, '0') + digits + ".ogg";
}

std::vector<std::string> readRetailSoundIndex(FileSystemBase& assets)
{
    const std::unique_ptr<IFileStream> file(assets.openResource("Res.array"));
    if (!file || file->size() < 4 || file->size() > 16 * 1024 * 1024)
        throw std::runtime_error("Missing or invalid retail sound index Res.array.");
    std::vector<uint8_t> bytes(static_cast<size_t>(file->size()));
    if (file->read(bytes.data(), bytes.size()) != bytes.size())
        throw std::runtime_error("Could not read retail sound index Res.array.");
    size_t offset = 0;
    const auto word = [&] {
        if (bytes.size() - offset < 4) throw std::runtime_error("Truncated retail sound index.");
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value = (value << 8) | static_cast<uint8_t>(bytes[offset++]);
        return value;
    };
    const auto text = [&] {
        const auto length = word();
        if (!length || length > bytes.size() - offset)
            throw std::runtime_error("Invalid name in retail sound index.");
        const std::string_view name(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
        return name;
    };

    const auto count = word();
    if (count > (bytes.size() - offset) / 10) throw std::runtime_error("Invalid retail sound count.");
    std::vector<std::string> names;
    names.reserve(count);
    for (uint32_t id = 0; id < count; ++id) {
        text();  // DVD name. Android streams its numbered Ogg resource instead.
        names.emplace_back(text());
    }
    if (offset != bytes.size()) throw std::runtime_error("Unexpected bytes after retail sound index.");
    return names;
}

namespace {
void checkOutput(MMRESULT result, const char* action)
{
    if (result != MMSYSERR_NOERROR)
        throw std::runtime_error(std::string(action) + " failed with MMRESULT " + std::to_string(result));
}

float supportedPitch(float pitch)
{
    if (!std::isfinite(pitch) || pitch <= 0) throw std::invalid_argument("Invalid game audio pitch.");
    return std::clamp(pitch, 0.25f, 4.f);
}

bool restrictedEffect(unsigned id)
{
    // SoundManager::isRestrictedSfx selects these IDs for cutscene muting.
    constexpr std::array ids{
        103u, 106u, 111u, 119u, 123u, 157u, 158u, 159u, 160u, 161u, 162u, 163u,
        164u, 165u, 166u, 167u, 168u, 169u, 170u, 171u, 172u, 173u, 174u, 176u,
        177u, 178u, 179u, 180u, 181u, 182u, 183u, 184u, 185u, 186u, 187u, 188u,
        189u, 190u, 191u, 192u, 193u, 194u, 195u, 197u, 198u, 199u, 202u, 203u,
        205u, 206u, 208u, 209u, 241u};
    return std::binary_search(ids.begin(), ids.end(), id);
}

float supportedGain(float gain)
{
    if (!std::isfinite(gain)) throw std::invalid_argument("Invalid game audio gain.");
    return std::clamp(gain, 0.f, 1.f);
}
}

struct RetailAudio::Voice {
    struct Buffer {
        std::vector<int16_t> samples;
        WAVEHDR header{};
    };

    Voice(FileSystemBase& assets, unsigned id, bool loop, float gain, float pitch, bool priority, bool music)
        : file(assets.openResource(retailSoundPath(id).c_str())), loop(loop),
          gain(supportedGain(gain)), pitch(supportedPitch(pitch)), priority(priority), music(music)
    {
        if (!file) throw std::runtime_error("Missing audio resource " + retailSoundPath(id));
        decoder = std::make_unique<VorbisStream>(*file);
        if (!decoder->frameCount()) return;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(decoder->channels());
        format.nSamplesPerSec = decoder->sampleRate();
        format.wBitsPerSample = 16;
        format.nBlockAlign = format.nChannels * sizeof(int16_t);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        checkOutput(waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL), "Open game audio output");
    }

    ~Voice()
    {
        if (!device) return;
        waveOutReset(device);
        for (auto& buffer : pending)
            if (buffer.header.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(device, &buffer.header, sizeof buffer.header);
        waveOutClose(device);
    }

    bool pump(float mixGain)
    {
        if (!device) return false;
        while (!pending.empty() && (pending.front().header.dwFlags & WHDR_DONE)) {
            auto& header = pending.front().header;
            checkOutput(waveOutUnprepareHeader(device, &header, sizeof header), "Release game audio buffer");
            pending.pop_front();
        }
        const auto channels = decoder->channels();
        const auto capacity = std::max(1u, decoder->sampleRate() / 30);
        while (!ended && pending.size() < 8) {
            auto& buffer = pending.emplace_back();
            buffer.samples.resize(size_t(capacity) * channels);
            const auto frames = render(buffer.samples.data(), capacity, mixGain);
            if (!frames) {
                pending.pop_back();
                ended = true;
                break;
            }
            buffer.samples.resize(frames * channels);
            auto& header = buffer.header;
            header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
            header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(int16_t));
            checkOutput(waveOutPrepareHeader(device, &header, sizeof header), "Prepare game audio buffer");
            checkOutput(waveOutWrite(device, &header, sizeof header), "Queue game audio buffer");
        }
        return !ended || !pending.empty();
    }

    bool nextFrame(std::vector<int16_t>& frame)
    {
        const auto channels = decoder->channels();
        if (cacheRead == cacheFrames) {
            if (cache.empty()) cache.resize(size_t(1024) * channels);
            cacheFrames = decoder->readFrames(cache.data(), 1024);
            if (!cacheFrames && loop) {
                decoder = std::make_unique<VorbisStream>(*file);
                cacheFrames = decoder->readFrames(cache.data(), 1024);
            }
            cacheRead = 0;
            if (!cacheFrames) return false;
        }
        frame.resize(channels);
        std::copy_n(cache.data() + cacheRead * channels, channels, frame.begin());
        ++cacheRead;
        return true;
    }

    size_t render(int16_t* output, size_t capacity, float mixGain)
    {
        if (!started) {
            started = true;
            currentValid = nextFrame(current);
            followingValid = currentValid && nextFrame(following);
        }
        const auto channels = decoder->channels();
        size_t rendered = 0;
        for (; rendered < capacity && currentValid; ++rendered) {
            for (size_t channel = 0; channel < channels; ++channel) {
                const auto a = current[channel];
                const auto b = followingValid ? following[channel] : a;
                *output++ = static_cast<int16_t>((a + (b - a) * fraction) * mixGain);
            }
            fraction += pitch;
            while (fraction >= 1 && currentValid) {
                fraction -= 1;
                current.swap(following);
                currentValid = followingValid;
                followingValid = currentValid && nextFrame(following);
            }
        }
        return rendered;
    }

    std::unique_ptr<IFileStream> file;
    std::unique_ptr<VorbisStream> decoder;
    bool loop;
    float gain;
    float pitch = 1;
    bool priority;
    bool music;
    HWAVEOUT device = nullptr;
    std::deque<Buffer> pending;
    bool ended = false;
    std::vector<int16_t> cache, current, following;
    size_t cacheRead = 0, cacheFrames = 0;
    double fraction = 0;
    bool started = false, currentValid = false, followingValid = false;
};

RetailAudio::RetailAudio(FileSystemBase& assets) : assets(assets), soundNames(readRetailSoundIndex(assets)) {}
RetailAudio::~RetailAudio() = default;

void RetailAudio::play(unsigned id, bool loop, float gain, float pitch)
{
    startVoice(id, loop, gain, pitch, false);
}

void RetailAudio::playPriority(unsigned id, bool loop, float gain)
{
    startVoice(id, loop, gain, 1, true);
}

void RetailAudio::playScriptCue(const CityAudioCue& cue)
{
    switch (cue.action) {
    case CityAudioAction::playOnce:
        play(cue.soundId, false);
        break;
    case CityAudioAction::playPriorityLoop:
        // The retail priority opcode stops these ambient sounds before playing its loop.
        stop(25);
        stop(14);
        playPriority(cue.soundId, true);
        break;
    case CityAudioAction::stop:
        stop(cue.soundId);
        break;
    }
}

void RetailAudio::startVoice(unsigned id, bool loop, float gain, float pitch, bool priority)
{
    stop(id);
    const bool music = id < soundNames.size() && soundNames[id].compare(0, 2, "m_") == 0;
    auto voice = std::make_unique<Voice>(assets, id, loop, gain, pitch, priority, music);
    if (voice->pump(mixGain(id, *voice, priorityActive()))) voices.emplace(id, std::move(voice));
}

bool RetailAudio::priorityActive() const
{
    return std::any_of(voices.begin(), voices.end(), [](const auto& entry) { return entry.second->priority; });
}

float RetailAudio::mixGain(unsigned id, const Voice& voice, bool cutscene) const
{
    const bool music = voice.music;
    const float volume = music ? musicVolume : effectsVolume;
    return cutscene && !voice.priority && (music || restrictedEffect(id)) ? 0.f : voice.gain * volume;
}

float RetailAudio::mixGain(unsigned id) const
{
    const auto voice = voices.find(id);
    return voice == voices.end() ? 0.f : mixGain(id, *voice->second, priorityActive());
}

void RetailAudio::setBusVolumes(float effects, float music)
{
    const auto newEffects = supportedGain(effects);
    const auto newMusic = supportedGain(music);
    effectsVolume = newEffects;
    musicVolume = newMusic;
}

void RetailAudio::stop(unsigned id)
{
    voices.erase(id);
}

void RetailAudio::stopAll()
{
    voices.clear();
    v6Running = false;
    v6FilteredRpm = 0;
    v6LoadFadeStep = 0;
}

void RetailAudio::updateV6Engine(bool running, int rpm, float load)
{
    struct Sample { unsigned id; int recordedRpm, low, high; };
    // Sample IDs, reference RPM, and windows from Vehicle::InitV6naSounds.
    static constexpr Sample banks[][4]{
        {{180, 900, 0, 1633}, {170, 2400, 3000, 4500}, {181, 1144, 2385, 3407}, {169, 1633, 2954, 5000}},
        {{179, 1070, 0, 2226}, {178, 2000, 1566, 3352}, {172, 3000, 2777, 4813}, {171, 4500, 4080, 5000}}
    };
    constexpr unsigned startup = 175;
    if (!running) {
        if (!v6Running) return;
        stop(startup);
        for (const auto& bank : banks) for (const auto& sample : bank) stop(sample.id);
        v6Running = false;
        v6FilteredRpm = 0;
        v6LoadFadeStep = 0;
        return;
    }
    if (!v6Running) {
        v6Running = true;
        play(startup, false);
        return;
    }
    if (playing(startup)) return;
    static constexpr float pi = 3.14159265358979323846f;
    // InitV6naSounds sets the normalized cutoff to 0.1 for CarSounds::FilterRPMValues.
    static const float feedback = [] {
        const float x = static_cast<float>(2 - std::cos(double(pi * 0.1f)));
        return static_cast<float>(std::sqrt(double(x*x - 1)) - x);
    }();
    v6FilteredRpm = (1 + feedback) * std::clamp(rpm, 0, 5000) - feedback * v6FilteredRpm;
    rpm = std::clamp(static_cast<int>(v6FilteredRpm), 1000, 5000);
    // SoundFade uses four remaining updates between the two endpoint entries.
    constexpr int lastStep = 5;
    v6LoadFadeStep = std::clamp(v6LoadFadeStep + (load > 0 ? 1 : -1), 0, lastStep);
    const float onGain = (1 + std::sin(-pi / 2 + pi * v6LoadFadeStep / lastStep)) * 0.5f;
    for (size_t bank = 0; bank < 2; ++bank) {
        const float bankGain = bank == 0 ? 1 - onGain : onGain;
        const auto& samples = banks[bank];
        std::array<float, 4> weights{};
        size_t nearest = 0;
        int bestDistance = 5001;
        float total = 0;
        for (size_t i = 0; i < weights.size(); ++i) {
            const auto& sample = samples[i];
            const int distance = std::max({sample.low - rpm, rpm - sample.high, 0});
            if (distance < bestDistance) { bestDistance = distance; nearest = i; }
            if (distance != 0) continue;
            weights[i] = static_cast<float>(1 + std::min(rpm - sample.low, sample.high - rpm));
            total += weights[i];
        }
        if (total == 0) { weights[nearest] = 1; total = 1; }
        for (size_t i = 0; i < weights.size(); ++i) {
            const auto& sample = samples[i];
            const float gain = bankGain * weights[i] / total;
            if (gain <= 0) {
                stop(sample.id);
            } else {
                const float pitch = static_cast<float>(rpm) / sample.recordedRpm;
                if (playing(sample.id)) {
                    setGain(sample.id, gain);
                    setPitch(sample.id, pitch);
                } else {
                    play(sample.id, true, gain, pitch);
                }
            }
        }
    }
}

void RetailAudio::setPitch(unsigned id, float pitch)
{
    const auto voice = voices.find(id);
    if (voice != voices.end()) voice->second->pitch = supportedPitch(pitch);
}

void RetailAudio::setGain(unsigned id, float gain)
{
    const auto voice = voices.find(id);
    if (voice != voices.end()) voice->second->gain = supportedGain(gain);
}

void RetailAudio::update()
{
    const bool cutscene = priorityActive();
    for (auto voice = voices.begin(); voice != voices.end();) {
        try {
            if (voice->second->pump(mixGain(voice->first, *voice->second, cutscene))) ++voice;
            else voice = voices.erase(voice);
        } catch (...) {
            voices.erase(voice);
            throw;
        }
    }
}

bool RetailAudio::playing(unsigned id) const
{
    return voices.count(id) != 0;
}
