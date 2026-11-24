#include "retail_npc_path.h"

#include <limits>
#include <stdexcept>

RetailNpcPath::RetailNpcPath(RetailNpcPathSource source, RetailNpcPathLoadHost& host)
    : index(source.sourceIndex) {
    host.loadLevelObject(source);
    if (source.scalarCoordinates.size() > std::size_t(std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("Path scalar count exceeds the original signed 32-bit domain.");
    // Original signed division by 3 ignores trailing one/two scalar values.
    points.resize(source.scalarCoordinates.size() / 3);
    for (std::size_t i = 0; i < points.size(); ++i)
        for (std::size_t axis = 0; axis < 3; ++axis)
            points[i][axis] = static_cast<float>(source.scalarCoordinates[i * 3 + axis]);
}

std::int32_t RetailNpcPath::getSize() const {
    return static_cast<std::int32_t>(points.size());
}

bool RetailNpcPath::getWaypoint(std::int32_t waypoint, RetailNpcPathPoint& point) const {
    if (waypoint >= getSize())
        return false;
    if (waypoint < 0)
        throw std::out_of_range("Negative original path waypoint would access before its allocation.");
    point = points[static_cast<std::size_t>(waypoint)];
    return true;
}

void loopRetailNpcPath(std::uint32_t& flags, bool loop) {
    if (loop)
        flags |= 0x00200000u;
    else
        flags &= ~0x00200000u;
}

bool isLoopingRetailNpcPath(std::uint32_t flags) {
    return (flags & 0x00200000u) != 0;
}

void setRetailNpcPath(RetailNpcPathState& state, std::uint32_t& flags, const RetailNpcPath* path) {
    state.currentWaypoint = -1;
    state.pathToFollow = path;
    loopRetailNpcPath(flags, false);
}

RetailNpcPathPoint nextRetailNpcWaypoint(RetailNpcPathState& state, std::uint32_t flags) {
    RetailNpcPathPoint point{};
    if (state.activePath && state.activeWaypoint < state.activePath->getSize()) {
        const auto previous = state.activeWaypoint++;
        state.activePath->getWaypoint(previous, point);
        if (isLoopingRetailNpcPath(flags))
            state.activeWaypoint %= state.activePath->getSize();
    }
    return point;
}

bool nextRetailNpcPoint(RetailNpcPathState& state, std::uint32_t& flags,
                       RetailNpcPathHost& host, RetailNpcPathPoint& point) {
    const auto* path = state.pathToFollow;
    if (!path || path != state.activePath) {
        state.activePath = path;
        state.activeWaypoint = 0;
    }
    if (state.activePath) {
        state.currentWaypoint = state.activeWaypoint;
        if (state.activeWaypoint >= state.activePath->getSize())
            return false;
        point = nextRetailNpcWaypoint(state, flags);
    } else {
        auto& physics = host.physicAttributes();
        const auto position = host.getPosition();
        point = physics.getNextRandomPos(0, 0, position);
    }
    return true;
}

void goToNextRetailNpcPoint(RetailNpcPathState& state, std::uint32_t& flags,
                            RetailNpcPathHost& host, bool repeat) {
    if (repeat && state.activeWaypoint > 0)
        --state.activeWaypoint;
    RetailNpcPathPoint point{};
    if (nextRetailNpcPoint(state, flags, host, point)) {
        flags |= 0x00400000u;
        host.goToLocation(point);
    } else {
        flags &= ~0x00400000u;
        host.goToIdle();
    }
}
