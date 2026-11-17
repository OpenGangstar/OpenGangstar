#include "retail_radio.h"
#include "retail_audio.h"

#include "FileSystemBase.h"
#include "IFileStream.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
class RadioReader {
public:
    RadioReader(FileSystemBase& assets, const char* name)
        : file(assets.openResource(name)), name(name)
    {
        if (!file || file->size() > 64 * 1024) fail();
        limit = file->size();
    }

    uint32_t word()
    {
        std::array<unsigned char, 4> bytes{};
        if (file->tell() > limit || limit - file->tell() < bytes.size() ||
            file->read(bytes.data(), bytes.size()) != bytes.size()) fail();
        uint32_t value = 0;
        for (auto byte : bytes) value = value << 8 | byte;
        return value;
    }

    std::vector<uint32_t> tracks()
    {
        const auto count = word();
        if (!count || count > (limit - file->tell()) / 4) fail();
        std::vector<uint32_t> ids;
        ids.reserve(count);
        for (uint32_t i = 0; i < count; ++i) ids.push_back(word());
        return ids;
    }

    unsigned long long startRecord()
    {
        const auto size = word();
        const auto outer = limit;
        if (size > outer - file->tell()) fail();
        limit = file->tell() + size;
        return outer;
    }

    void finishRecord(unsigned long long outer)
    {
        if (file->tell() != limit) fail();
        limit = outer;
    }

    void finish()
    {
        const auto remaining = limit - file->tell();
        if (!remaining) return;
        std::array<unsigned char, 2> padding{};
        if (remaining != padding.size() || file->read(padding.data(), padding.size()) != padding.size() ||
            padding[0] != 0 || padding[1] != 0) fail();
    }

    [[noreturn]] void fail() const { throw std::runtime_error("Invalid retail radio resource " + name); }

private:
    std::unique_ptr<IFileStream> file;
    std::string name;
    unsigned long long limit = 0;
};

uint32_t takeTrack(const std::vector<uint32_t>& original, std::vector<uint32_t>& queue,
                   const std::function<uint32_t()>& random)
{
    if (queue.empty()) queue = original;
    const auto index = random() % queue.size();
    const auto id = queue[index];
    queue.erase(queue.begin() + index);
    return id;
}
}

RadioCatalog loadRetailRadio(FileSystemBase& assets)
{
    RadioReader stations(assets, "RadioStation.gmap");
    const auto count = stations.word();
    if (!count || count > 256) stations.fail();
    RadioCatalog catalog;
    for (uint32_t i = 0; i < count; ++i) {
        const auto outer = stations.startRecord();
        catalog.stations.push_back({stations.tracks(), stations.tracks(), stations.word()});
        stations.finishRecord(outer);
    }
    stations.finish();
    RadioReader ads(assets, "RadioAds.array");
    catalog.ads = ads.tracks();
    ads.finish();
    return catalog;
}

RadioSelector::RadioSelector(RadioCatalog catalog) : catalog(std::move(catalog)),
    stations(this->catalog.stations.size())
{
    if (stations.empty() || this->catalog.ads.empty())
        throw std::runtime_error("Radio catalog has no stations or advertisements.");
    for (const auto& station : this->catalog.stations)
        if (station.songs.empty() || station.jingles.empty())
            throw std::runtime_error("Radio station has no songs or jingles.");
}

RadioTrack RadioSelector::next(size_t station, const std::function<uint32_t()>& random)
{
    using Kind = RadioTrackKind;
    const auto& source = catalog.stations.at(station);
    auto& queue = stations[station];
    const auto refill = [&] {
        frequency = {Kind::song, Kind::song, Kind::song, Kind::advertisement, Kind::jingle, Kind::jingle};
    };

    if (frequency.empty()) refill();
    auto index = random() % frequency.size();
    auto kind = frequency[index];
    if (kind == Kind::jingle) {
        // Match SoundManager::_getRadioTrack's handling of consecutive jingles.
        if (previousJingle) {
            if (frequency.size() == 1) {
                refill();
                index = random() % frequency.size();
                kind = frequency[index];
            }
            if (kind == Kind::jingle) {
                index = (index + 1) % frequency.size();
                kind = frequency[index];
            }
            previousJingle = false;
        } else {
            previousJingle = true;
        }
    }
    frequency.erase(frequency.begin() + index);
    switch (kind) {
    case Kind::song: return {takeTrack(source.songs, queue.songs, random), kind};
    case Kind::advertisement: return {takeTrack(catalog.ads, ads, random), kind};
    case Kind::jingle: return {takeTrack(source.jingles, queue.jingles, random), kind};
    }
    throw std::logic_error("Invalid radio track kind.");
}

RadioPlayback::RadioPlayback(RetailAudio& audio, RadioCatalog catalog, float gain)
    : audio(audio), stationCount(catalog.stations.size()), selector(std::move(catalog)), gain(gain)
{
}

RadioPlayback::~RadioPlayback()
{
    stop();
}

uint32_t RadioPlayback::random()
{
    // Irrlicht's retail Randomizer::rand and reset constants.
    seed = 40692 * (seed % 52774) - 3791 * (seed / 52774);
    if (seed < 0) seed += 2147483399;
    return static_cast<uint32_t>(seed);
}

void RadioPlayback::play(uint32_t id)
{
    if (track) audio.stop(*track);
    track.reset();
    audio.play(id, false, gain);
    track = id;
}

void RadioPlayback::start(size_t defaultStation)
{
    if (defaultStation >= stationCount) throw std::runtime_error("Invalid default vehicle radio station.");
    if (!station) station = defaultStation;
    active = true;
    update();
}

void RadioPlayback::stop()
{
    active = false;
    if (track) audio.stop(*track);
    track.reset();
}

void RadioPlayback::change(int direction)
{
    if (!active || !direction) return;
    station = (*station + (direction < 0 ? stationCount - 1 : 1)) % stationCount;
    play(124);  // SoundManager::changeRadioStation uses the retail tuning cue.
}

void RadioPlayback::update()
{
    if (!active || (track && audio.playing(*track))) return;
    play(selector.next(*station, [this] { return random(); }).soundId);
}
