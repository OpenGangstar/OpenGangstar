#include "retail_timeline.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
// ARM SUBS preserves the signed32 bit pattern even across overflow. Avoid native
// signed overflow while retaining the original value before float conversion.
std::int32_t subtract32(std::int32_t a, std::int32_t b)
{
    const auto bits = std::uint32_t(a) - std::uint32_t(b);
    const auto value = bits <= std::uint32_t(INT32_MAX) ? std::int64_t(bits) :
        std::int64_t(bits) - 0x100000000LL;
    return static_cast<std::int32_t>(value);
}

void checkedName(std::string_view name)
{
    if (name.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Embedded NUL in original timeline clip name.");
}

std::int32_t truncateTime(float seconds)
{
    const float milliseconds = seconds * 1000.f;
    if (!std::isfinite(milliseconds) || milliseconds < -2147483648.f || milliseconds >= 2147483648.f)
        throw std::domain_error("Timeline floating time is outside the native signed32 conversion domain.");
    return static_cast<std::int32_t>(milliseconds);
}
}

RetailTimeline::RetailTimeline(std::shared_ptr<const Clips> source) : clips(std::move(source))
{
    if (!clips || clips->empty() || clips->size() > std::size_t(INT32_MAX))
        throw std::invalid_argument("Timeline factory requires its nonempty original indexed clip library.");
    for (std::size_t i = 0; i < clips->size(); ++i) {
        const auto& entry = (*clips)[i];
        checkedName(entry.name);
        if (!entry.source || entry.source->index != i)
            throw std::invalid_argument("Timeline requires original source-ordered clip metadata.");
    }
    setClip(0);
}

void RetailTimeline::requireLibrary() const
{
    if (!clips) throw std::logic_error("Timeline clip operation requires actual library backing.");
}
void RetailTimeline::requireRange() const
{
    if (!rangeConfigured) throw std::logic_error("Original timeline range has not been configured.");
}
const CityAnimationClip& RetailTimeline::clip(std::int32_t index) const
{
    requireLibrary();
    if (index < 0 || std::size_t(index) >= clips->size())
        throw std::out_of_range("Original timeline clip index is unavailable.");
    return (*clips)[std::size_t(index)];
}
std::int32_t RetailTimeline::clipCount() const
{
    requireLibrary();
    return static_cast<std::int32_t>(clips->size());
}
std::int32_t RetailTimeline::clipIndex(std::string_view name) const
{
    requireLibrary();
    checkedName(name);
    for (std::size_t i = 0; i < clips->size(); ++i)
        if ((*clips)[i].name == name) return static_cast<std::int32_t>(i);
    return -1;
}
const std::string& RetailTimeline::clipName(std::int32_t index) const { return clip(index).name; }
std::int32_t RetailTimeline::clipStart(std::int32_t index) const { return clip(index).source->startMs; }
std::int32_t RetailTimeline::clipEnd(std::int32_t index) const { return clip(index).source->endMs; }
std::int32_t RetailTimeline::clipLength(std::int32_t index) const { return subtract32(clipEnd(index), clipStart(index)); }

void RetailTimeline::init(std::int32_t first, std::int32_t last)
{
    start = first;
    end = last;
    rangeConfigured = true;
}
void RetailTimeline::setRange(std::int32_t first, std::int32_t last)
{
    if (!clips) {
        init(first, last);
        startSeconds = static_cast<float>(first) / 1000.f;
        spanSeconds = static_cast<float>(subtract32(last, first)) / 1000.f;
    }
    requireRange();
    jumpTo(start);
}
void RetailTimeline::setClip(std::int32_t index)
{
    const auto& source = *clip(index).source; // Checked adaptation before an invalid original table read.
    completed = false;
    selectedClip = index;
    init(source.startMs, source.endMs);
    startSeconds = static_cast<float>(start) / 1000.f;
    spanSeconds = static_cast<float>(subtract32(end, start)) / 1000.f;
    currentTime = start;
    currentSeconds = startSeconds;
}
std::int32_t RetailTimeline::setClip(std::string_view name)
{
    const auto index = clipIndex(name);
    if (index >= 0) setClip(index);
    return index;
}
void RetailTimeline::jumpTo(std::int32_t time)
{
    currentTime = time;
    currentSeconds = static_cast<float>(time) / 1000.f;
    completed = false;
    hasPrevious = false;
}

void RetailTimeline::update(std::int32_t timestamp)
{
    if (updating) throw std::logic_error("Recursive timeline update is outside the native callback domain.");
    requireRange();
    updating = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{updating};

    const float inputSeconds = static_cast<float>(timestamp) / 1000.f;
    const float endSeconds = static_cast<float>(end) / 1000.f;
    float advance = 0.f;
    if (hasPrevious) {
        const float delta = inputSeconds - previousSeconds;
        advance = delta * scale;
    } else {
        hasPrevious = true;
    }
    currentSeconds = currentSeconds + advance;
    previousSeconds = inputSeconds;

    bool notify = false;
    if (endSeconds < currentSeconds) {
        if (loop) {
            float remainder = 0.f;
            if (spanSeconds != 0.f) {
                const float excess = currentSeconds - endSeconds;
                remainder = std::fmod(excess, spanSeconds);
            }
            currentSeconds = startSeconds + remainder;
            notify = true;
        } else {
            currentSeconds = endSeconds;
            if (!completed) {
                completed = true;
                notify = true;
            }
        }
    }
    if (notify && callback) {
        const auto invoke = callback;
        auto* data = userdata;
        invoke(*this, data);
    }
    // Callback jump/range/clip changes are live. Do not restore a preview or
    // validate an overflowing intermediate that the original clamp repaired.
    currentTime = truncateTime(currentSeconds);
}
