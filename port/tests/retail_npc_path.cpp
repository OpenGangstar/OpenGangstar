#include "retail_npc_path.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
struct Load final : RetailNpcPathLoadHost {
    unsigned calls = 0;
    std::size_t count = 0;
    std::uint32_t index = 0;
    bool fail = false;
    void loadLevelObject(const RetailNpcPathSource& source) override {
        ++calls;
        count = source.scalarCoordinates.size();
        index = source.sourceIndex;
        if (fail)
            throw std::runtime_error("load host failure");
    }
};
struct Host;
struct Random final : RetailNpcRandomPosition {
    Host& host;
    int identity;
    Random(Host& host, int identity) : host(host), identity(identity) {}
    RetailNpcPathPoint getNextRandomPos(std::int32_t, std::int32_t,
                                       const RetailNpcPathPoint&) override;
};
struct Host final : RetailNpcPathHost {
    RetailNpcPathState& state;
    std::uint32_t& flags;
    const std::array<const RetailNpcPath*, 4>& paths;
    std::vector<std::int64_t> events;
    RetailNpcPathPoint destination{-9, -8, -7};
    Random first{*this, 0}, second{*this, 1};
    Random* provider = &first;
    bool mutate = false;
    Host(RetailNpcPathState& state, std::uint32_t& flags,
         const std::array<const RetailNpcPath*, 4>& paths) : state(state), flags(flags), paths(paths) {}
    int id(const RetailNpcPath* path) const {
        for (int i = 0; i < 4; ++i)
            if (path == paths[static_cast<std::size_t>(i)])
                return i;
        require(!path, "Unknown native path identity");
        return -1;
    }
    void event(int code) {
        for (auto value : {std::int64_t(code), std::int64_t(flags), std::int64_t(state.activeWaypoint),
                           std::int64_t(state.currentWaypoint), std::int64_t(id(state.pathToFollow)),
                           std::int64_t(id(state.activePath))})
            events.push_back(value);
    }
    RetailNpcRandomPosition& physicAttributes() override {
        event(1);
        return *provider;
    }
    RetailNpcPathPoint getPosition() override {
        event(2);
        if (mutate) {
            provider = &second;
            flags ^= 0x00200000u;
            state.pathToFollow = paths[2];
            state.currentWaypoint = 71;
        }
        return {7, 11, 13};
    }
    void goToLocation(const RetailNpcPathPoint& point) override {
        event(4);
        destination = point;
        if (mutate) {
            flags ^= 0x00400000u;
            state.activeWaypoint = 91;
            state.currentWaypoint = 92;
        }
    }
    void goToIdle() override {
        event(5);
        if (mutate) {
            flags ^= 0x00400000u;
            state.activeWaypoint = 93;
            state.currentWaypoint = 94;
        }
    }
};
RetailNpcPathPoint Random::getNextRandomPos(std::int32_t first, std::int32_t second,
                                           const RetailNpcPathPoint& point) {
    require(first == 0 && second == 0 && point == RetailNpcPathPoint{7, 11, 13},
            "Original random-position parameters and live position");
    host.event(3 + identity * 10);
    if (host.mutate) {
        host.flags ^= 0x100u;
        host.state.currentWaypoint = 72;
    }
    return {point[0] + 100 + identity, point[1] + 200, point[2] + 300};
}

void loadChecks() {
    Load host;
    RetailNpcPathSource source{0xffffffffu,
                              {std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max(), 16777217, -16777217, 0, 123}};
    RetailNpcPath path(source, host);
    require(host.calls == 1 && host.count == 6 && host.index == 0xffffffffu &&
                path.sourceIndex() == 0xffffffffu && path.getSize() == 2,
            "LevelObject load precedes owned conversion and preserves fixed-width source identity");
    source.scalarCoordinates.assign(6, 999);
    RetailNpcPathPoint point{-1, -2, -3};
    require(path.getWaypoint(0, point) && bits(point[0]) == 0xcf000000u &&
                bits(point[1]) == 0x4f000000u && bits(point[2]) == 0x4b800000u,
            "Direct signed integer-to-float points, no placement transform or borrowed source storage");
    const auto before = point;
    require(!path.getWaypoint(2, point) && point == before &&
                !path.getWaypoint(std::numeric_limits<std::int32_t>::max(), point) && point == before,
            "At/past end preserves output");
    bool rejected = false;
    try { path.getWaypoint(-1, point); } catch (const std::out_of_range&) { rejected = true; }
    require(rejected && point == before, "Explicit native rejection of original negative OOB domain");
    for (std::size_t count = 0; count <= 8; ++count) {
        RetailNpcPath shortPath({19, std::vector<std::int32_t>(count, 10)}, host);
        require(shortPath.getSize() == std::int32_t(count / 3), "Original floor division and tail discard");
    }
    host.fail = true;
    rejected = false;
    try { RetailNpcPath fail({20, {1, 2, 3}}, host); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Required common load failure propagates without returning a partial path");
}

void loadOracle(const char* input) {
    std::ifstream file(input);
    require(bool(file), "Missing path scalar fixture input");
    std::uint32_t index;
    std::size_t count;
    std::size_t cases = 0;
    while (file >> index >> count) {
        require(count <= std::size_t(std::numeric_limits<std::int32_t>::max()), "Fixture scalar domain");
        RetailNpcPathSource source{index, std::vector<std::int32_t>(count)};
        for (auto& value : source.scalarCoordinates)
            require(bool(file >> value), "Truncated scalar fixture");
        Load host;
        RetailNpcPath path(std::move(source), host);
        require(host.calls == 1 && host.count == count && host.index == index,
                "Every supplied path requires its common LevelObject load exactly once");
        std::cout << "loaded {\"index\":" << index << ",\"size\":" << path.getSize() << ",\"points\":[";
        for (std::int32_t i = 0; i < path.getSize(); ++i) {
            RetailNpcPathPoint point;
            require(path.getWaypoint(i, point), "Loaded point access");
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (i || axis) std::cout << ',';
                std::cout << bits(point[axis]);
            }
        }
        std::cout << "]}\n";
        ++cases;
    }
    require(file.eof(), "Invalid scalar fixture input");
    std::cout << cases << " original path scalar fixtures loaded\n";
}

void sequenceChecks() {
    Load loader;
    RetailNpcPath path({9, {11, 12, 13, 21, 22, 23, 31, 32, 33}}, loader);
    RetailNpcPath other({9, {41, 42, 43}}, loader);
    const std::array<const RetailNpcPath*, 4> paths{&path, &other, &path, &other};
    RetailNpcPathState state;
    std::uint32_t flags = 0x80000000u;
    Host host(state, flags, paths);
    setRetailNpcPath(state, flags, &path);
    goToNextRetailNpcPoint(state, flags, host, false);
    require(host.destination == RetailNpcPathPoint{11, 12, 13} && state.currentWaypoint == 0 &&
                state.activeWaypoint == 1 && flags == 0x80400000u,
            "First dispatch publishes index, advances cursor and sets movement bit");
    setRetailNpcPath(state, flags, &path);
    RetailNpcPathPoint point{};
    require(nextRetailNpcPoint(state, flags, host, point) && point == RetailNpcPathPoint{21, 22, 23} &&
                state.currentWaypoint == 1 && state.activeWaypoint == 2,
            "Selecting the same owner preserves progress until next publication");
    goToNextRetailNpcPoint(state, flags, host, true);
    require(host.destination == RetailNpcPathPoint{21, 22, 23} && state.activeWaypoint == 2,
            "Repeat requests the previous point without advancing past it");
    require(nextRetailNpcPoint(state, flags, host, point) && point == RetailNpcPathPoint{31, 32, 33},
            "Nonloop tail is a valid point");
    point = {-9, -8, -7};
    loopRetailNpcPath(flags, true);
    require(!nextRetailNpcPoint(state, flags, host, point) && state.currentWaypoint == 3 &&
                point == RetailNpcPathPoint{-9, -8, -7},
            "Enabling looping after exhaustion does not rewind or overwrite output");
    setRetailNpcPath(state, flags, &other);
    loopRetailNpcPath(flags, true);
    goToNextRetailNpcPoint(state, flags, host, false);
    require(host.destination == RetailNpcPathPoint{41, 42, 43} && state.currentWaypoint == 0 &&
                state.activeWaypoint == 0 && state.activePath == &other,
            "Distinct equal-index owner resets cursor and one-point loop wraps");
    goToNextRetailNpcPoint(state, flags, host, true);
    require(host.destination == RetailNpcPathPoint{41, 42, 43} && state.activeWaypoint == 0,
            "Repeat on wrapped cursor zero requests the head");
    setRetailNpcPath(state, flags, nullptr);
    require(state.activePath == &other, "Clearing selected path retains active borrowed owner until request");
    host.mutate = true;
    host.events.clear();
    require(nextRetailNpcPoint(state, flags, host, point) && point == RetailNpcPathPoint{107, 211, 313} &&
                !state.activePath && state.activeWaypoint == 0 && state.currentWaypoint == 72 &&
                host.events.size() == 18 && host.events[0] == 1 && host.events[6] == 2 &&
                host.events[12] == 3,
            "Null path captures original provider before position mutation and preserves live effects");
}

void transitions(bool dump) {
    Load loader;
    RetailNpcPath empty({7, {}}, loader), one({8, {101, 102, 103}}, loader);
    RetailNpcPath three({9, {11, 12, 13, 21, 22, 23, 31, 32, 33}}, loader);
    RetailNpcPath sameIndex({9, {11, 12, 13, 21, 22, 23, 31, 32, 33}}, loader);
    const std::array<const RetailNpcPath*, 4> paths{&empty, &one, &three, &sameIndex};
    RetailNpcPathState initial;
    require(initial.currentWaypoint == 1 && initial.activeWaypoint == 0 && !initial.activePath &&
                !initial.pathToFollow, "Original constructor path field defaults");
    std::size_t cases = 0;
    for (int op = 0; op < 5; ++op)
        for (int selected = -1; selected < 4; ++selected)
            for (int active = -1; active < 4; ++active)
                for (int cursor = 0; cursor <= 4; ++cursor)
                    for (int loop = 0; loop < 2; ++loop)
                        for (int repeat = 0; repeat < 2; ++repeat)
                            for (int mutation = 0; mutation < 2; ++mutation) {
                                RetailNpcPathState state;
                                state.pathToFollow = selected < 0 ? nullptr : paths[std::size_t(selected)];
                                state.activePath = active < 0 ? nullptr : paths[std::size_t(active)];
                                state.activeWaypoint = cursor;
                                state.currentWaypoint = 123;
                                std::uint32_t flags = 0x81014567u | (loop ? 0x00200000u : 0);
                                Host host(state, flags, paths);
                                host.mutate = mutation != 0;
                                RetailNpcPathPoint point{-9, -8, -7};
                                bool result = false;
                                if (op == 0) point = nextRetailNpcWaypoint(state, flags);
                                if (op == 1) result = nextRetailNpcPoint(state, flags, host, point);
                                if (op == 2) goToNextRetailNpcPoint(state, flags, host, repeat != 0);
                                if (op == 3) setRetailNpcPath(state, flags, state.pathToFollow);
                                if (op == 4) loopRetailNpcPath(flags, repeat != 0);
                                if (op == 3)
                                    require(state.currentWaypoint == -1 && state.activeWaypoint == cursor &&
                                                host.id(state.activePath) == active && !isLoopingRetailNpcPath(flags),
                                            "setPath does not reset active cursor/owner, even repeated/null selection");
                                if (op == 0)
                                    require(state.currentWaypoint == 123 && host.events.empty(),
                                            "Direct nextWaypoint does not update current waypoint or host");
                                if (op == 1 && selected >= 0 && selected == active &&
                                    cursor >= paths[std::size_t(selected)]->getSize())
                                    require(!result && point == RetailNpcPathPoint{-9, -8, -7} &&
                                                state.currentWaypoint == cursor,
                                            "Exhaustion updates current index and preserves caller output");
                                if (dump) {
                                    std::cout << "case {\"input\":[" << op << ',' << selected << ',' << active
                                              << ',' << cursor << ',' << loop << ',' << repeat << ',' << mutation
                                              << "],\"output\":[" << flags << ',' << host.id(state.pathToFollow)
                                              << ',' << host.id(state.activePath) << ',' << state.activeWaypoint
                                              << ',' << state.currentWaypoint << ',' << result;
                                    for (float x : point) std::cout << ',' << bits(x);
                                    for (float x : host.destination) std::cout << ',' << bits(x);
                                    std::cout << "],\"events\":[";
                                    for (std::size_t i = 0; i < host.events.size(); ++i) {
                                        if (i) std::cout << ',';
                                        std::cout << host.events[i];
                                    }
                                    std::cout << "]}\n";
                                }
                                ++cases;
                            }
    std::cout << cases << " native NPC path transition cases passed\n";
}
}

int main(int argc, char** argv) {
    try {
        loadChecks();
        sequenceChecks();
        if (argc == 3 && std::string(argv[1]) == "--load-oracle")
            loadOracle(argv[2]);
        else
            transitions(argc == 2 && std::string(argv[1]) == "--oracle");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
