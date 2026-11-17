#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class FileSystemBase;
class RetailAudio;

enum class RadioTrackKind { song, advertisement, jingle };

struct RadioStationTracks {
    std::vector<uint32_t> jingles;
    std::vector<uint32_t> songs;
    uint32_t identifier;
};

struct RadioCatalog {
    std::vector<RadioStationTracks> stations;
    std::vector<uint32_t> ads;
};

struct RadioTrack {
    uint32_t soundId;
    RadioTrackKind kind;
};

RadioCatalog loadRetailRadio(FileSystemBase& assets);

class RadioSelector {
public:
    explicit RadioSelector(RadioCatalog catalog);
    RadioTrack next(size_t station, const std::function<uint32_t()>& random);

private:
    struct StationQueue {
        std::vector<uint32_t> songs;
        std::vector<uint32_t> jingles;
    };
    RadioCatalog catalog;
    std::vector<StationQueue> stations;
    std::vector<uint32_t> ads;
    std::vector<RadioTrackKind> frequency;
    bool previousJingle = false;
};

class RadioPlayback {
public:
    RadioPlayback(RetailAudio& audio, RadioCatalog catalog, float gain = 1);
    ~RadioPlayback();
    RadioPlayback(const RadioPlayback&) = delete;
    RadioPlayback& operator=(const RadioPlayback&) = delete;
    void start(size_t defaultStation);
    void stop();
    void change(int direction);
    void update();
    std::optional<uint32_t> currentTrack() const { return track; }
    size_t currentStation() const { return station.value_or(0); }

private:
    uint32_t random();
    void play(uint32_t id);

    RetailAudio& audio;
    size_t stationCount;
    RadioSelector selector;
    float gain;
    int32_t seed = 0x0f0f0f0f;
    bool active = false;
    std::optional<size_t> station;
    std::optional<uint32_t> track;
};
