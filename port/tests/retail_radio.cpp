#include "retail_radio.h"

#include <array>
#include <iostream>
#include <stdexcept>

int main()
try {
    using Kind = RadioTrackKind;
    RadioSelector selector(RadioCatalog{
        {{{10, 11}, {20, 21, 22}, 0}, {{12, 13}, {30, 31, 32}, 1}},
        {40, 41}
    });
    const auto first = [] { return 0u; };
    const std::array<RadioTrack, 9> expected{{
        {20, Kind::song}, {21, Kind::song}, {22, Kind::song},
        {40, Kind::advertisement}, {10, Kind::jingle},
        {20, Kind::song}, {21, Kind::song}, {22, Kind::song}, {41, Kind::advertisement}
    }};
    for (const auto& track : expected) {
        const auto actual = selector.next(0, first);
        if (track.soundId != actual.soundId || track.kind != actual.kind)
            throw std::runtime_error("Radio category selection or track refill differs from retail.");
    }
    const auto newStation = selector.next(1, first);
    if (newStation.kind != Kind::jingle || newStation.soundId != 12)
        throw std::runtime_error("Radio station changed the global category queue or lost its own jingle queue.");
    const auto oldStation = selector.next(0, [] { return 5u; });
    if (oldStation.kind != Kind::song || oldStation.soundId != 22)
        throw std::runtime_error("Radio consecutive-jingle handling differs from retail.");
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
