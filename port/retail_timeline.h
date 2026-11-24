#pragma once

#include "city_assets.h"

#include <memory>
#include <string_view>

// Original CTimelineController state, independent of Character's detached clock.
// Clip backing may alias an existing catalogue owner. It must remain immutable;
// this object retains its actual owner and creates no parser or copied catalogue.
class RetailTimeline {
public:
    using Clips = std::vector<CityAnimationClip>;
    using Callback = void (*)(RetailTimeline&, void*);

    // Raw original constructor: time/float state starts at zero, scale at one.
    // The original integer range is unspecified until configured; native update
    // rejects that domain rather than treating the unused zero storage as a range.
    RetailTimeline() = default;
    // Factory composition: attach an actual nonempty indexed library, select0.
    // Zero-span clips are valid. durationMs/channels are not timeline metadata.
    explicit RetailTimeline(std::shared_ptr<const Clips>);
    RetailTimeline(const RetailTimeline&) = delete;
    RetailTimeline& operator=(const RetailTimeline&) = delete;
    RetailTimeline(RetailTimeline&&) = delete;
    RetailTimeline& operator=(RetailTimeline&&) = delete;

    void init(std::int32_t startMs, std::int32_t endMs);
    void setRange(std::int32_t startMs, std::int32_t endMs);
    void setClip(std::int32_t index);
    std::int32_t setClip(std::string_view exactName);
    void jumpTo(std::int32_t timeMs);
    void setLoop(bool value) { loop = value; }
    bool getLoop() const { return loop; }
    void setScale(float value) { scale = value; }
    float getScale() const { return scale; }
    std::int32_t timeMs() const { return currentTime; }
    std::int32_t currentClipIndex() const { return selectedClip; }
    std::int32_t clipCount() const;
    std::int32_t clipIndex(std::string_view exactName) const;
    const std::string& clipName(std::int32_t index) const;
    std::int32_t clipStart(std::int32_t index) const;
    std::int32_t clipEnd(std::int32_t index) const;
    std::int32_t clipLength(std::int32_t index) const;
    std::int32_t currentClipStart() const { return clipStart(selectedClip); }
    std::int32_t currentClipEnd() const { return clipEnd(selectedClip); }
    std::int32_t currentClipLength() const { return clipLength(selectedClip); }

    // NULL callback is original valid state. Userdata is borrowed, full width,
    // and must remain valid through the call. Setters/jump/clip/range are legal
    // inside callbacks. update recursion and destruction during update are not.
    void setCallback(Callback value, void* data = nullptr) { callback = value; userdata = data; }
    void update(std::int32_t timestampMs);

private:
    const CityAnimationClip& clip(std::int32_t index) const;
    void requireLibrary() const;
    void requireRange() const;
    std::shared_ptr<const Clips> clips;
    std::int32_t currentTime = 0, start = 0, end = 0, selectedClip = 0;
    float startSeconds = 0, spanSeconds = 0, previousSeconds = 0, currentSeconds = 0, scale = 1;
    bool loop = false, completed = false, hasPrevious = false, rangeConfigured = false, updating = false;
    Callback callback = nullptr;
    void* userdata = nullptr;
};
