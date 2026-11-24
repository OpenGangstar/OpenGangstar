#include "retail_timeline.h"
#include "retail_actor_animation.h"
#include "filesystem.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
std::uint32_t bits(float value)
{
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
template<class F> void rejects(F action)
{
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, "Invalid timeline domain must fail explicitly");
}
CityAnimationClip clip(std::uint32_t index, std::string name, std::int32_t start, std::int32_t end)
{
    return {std::move(name), 0, {}, CityAnimationSource{index, start, end}};
}
std::shared_ptr<const RetailTimeline::Clips> fixture()
{
    auto clips = std::make_shared<RetailTimeline::Clips>();
    for (std::uint32_t i = 0; i < 68; ++i)
        clips->push_back(clip(i, "clip" + std::to_string(i), 0, 333));
    (*clips)[67] = clip(67, "reload", 77366, 78233);
    return clips;
}

// Original public operations only: no raw-state accessor, offset casts or test
// friendship. Callback counts and subsequent updates expose reset/anchor effects.
struct Snapshot {
    std::int32_t time, selected;
    bool loop;
    std::uint32_t scale;
};
Snapshot snapshot(const RetailTimeline& t)
{
    return {t.timeMs(), t.currentClipIndex(), t.getLoop(), bits(t.getScale())};
}
bool operator==(const Snapshot& a, const Snapshot& b)
{
    return a.time == b.time && a.selected == b.selected && a.loop == b.loop && a.scale == b.scale;
}
void emit(const Snapshot& state)
{
    std::cout << "{\"time\":" << state.time << ",\"clip\":" << state.selected
              << ",\"loop\":" << state.loop << ",\"scale\":" << state.scale << '}';
}
void step(RetailTimeline& t, std::int32_t time, bool dump, bool& first)
{
    const auto before = snapshot(t);
    t.update(time);
    if (dump) {
        if (!first) std::cout << ',';
        std::cout << "{\"input\":" << time << ",\"before\":";
        emit(before);
        std::cout << ",\"after\":";
        emit(snapshot(t));
        std::cout << '}';
    }
    first = false;
}
void begin(const char* name, bool dump)
{
    if (dump) std::cout << "timeline {\"case\":" << std::quoted(name) << ",\"steps\":[";
}
void finish(bool dump)
{
    if (dump) std::cout << "]}\n";
}

struct CallbackState {
    unsigned calls = 0;
    int action = 0;
    RetailTimeline* identity = nullptr;
    Snapshot during{};
    bool recursiveRejected = false;
};
void callback(RetailTimeline& t, void* userdata)
{
    auto& state = *static_cast<CallbackState*>(userdata);
    require(&t == state.identity, "Callback self and full-width userdata identity");
    ++state.calls;
    state.during = snapshot(t);
    if (state.calls != 1) return;
    switch (state.action) {
    case 1:
        t.jumpTo(123);
        t.setScale(-2.f);
        break;
    case 2:
        t.setRange(200, 400);
        t.setScale(.5f);
        t.setLoop(true);
        break;
    case 3:
        t.setClip(67);
        break;
    case 4:
        t.setCallback(nullptr);
        break;
    case 5:
        try { t.update(999); } catch (const std::logic_error&) { state.recursiveRejected = true; }
        break;
    case 6:
        throw std::runtime_error("Deliberate throwing callback");
    }
}

void referenceProfiles(std::shared_ptr<const RetailTimeline::Clips> clips, bool dump)
{
    for (unsigned allocation : {0u, 100u}) {
        // Constructor correctness under previously used allocation contents.
        alignas(RetailTimeline) std::array<unsigned char, sizeof(RetailTimeline)> storage;
        storage.fill(allocation ? 0xa7 : 0);
        auto* t = new (storage.data()) RetailTimeline(clips);
        begin(allocation ? "fresh-poison" : "fresh-zero", dump);
        bool first = true;
        for (auto time : {12345, 12445, 12545}) step(*t, time, dump, first);
        require(t->timeMs() == 199, "Float input-anchor subtraction must not become integer deltas");
        finish(dump);
        t->~RetailTimeline();
    }
    {
        RetailTimeline t(clips);
        CallbackState state{0, 1, &t};
        t.setCallback(callback, &state);
        begin("callback-live-mutation", dump);
        bool first = true;
        step(t, 0, dump, first);
        step(t, 1000, dump, first);
        require(state.calls == 1 && state.during.time == 0 && t.timeMs() == 123 && t.getScale() == -2,
                "Callback occurs before final integer store and live jump/scale changes survive");
        finish(dump);
    }
    // Captured by executing the original update body, including float rounding,
    // strict crossing, negative time and the lack of a lower clamp.
    const std::int32_t rangeTimes[8][7] = {
        {100, 200, 1000, 1000, 1000, -4001, 1000},
        {100, 100, 100, 100, 100, 100, 100},
        {100, 0, -900, -901, -4900, 100, -4900},
        {100, 300, 1000, 1000, 1000, -9002, 1000},
        {100, 200, 200, 201, 600, -4400, 600},
        {100, 100, 100, 100, 100, 100, 100},
        {100, 0, -900, -901, -4900, 100, -4900},
        {100, 300, 299, 302, 200, -9801, 200}};
    unsigned profile = 0;
    for (bool loop : {false, true}) {
        for (float scale : {1.f, 0.f, -1.f, 2.f}) {
            RetailTimeline t;
            t.setRange(100, 1000);
            t.setLoop(loop);
            t.setScale(scale);
            const auto name = std::string("range-") + (loop ? "loop-" : "once-") + std::to_string(int(scale));
            begin(name.c_str(), dump);
            bool first = true;
            unsigned sample = 0;
            for (auto time : {0, 100, 1000, 1001, 5000, -1, 5000}) {
                step(t, time, dump, first);
                require(t.timeMs() == rangeTimes[profile][sample++], "Original loop/scale/signed input result");
            }
            ++profile;
            finish(dump);
        }
    }
    {
        RetailTimeline t(clips);
        t.update(0);
        t.update(100);
        if (dump) std::cout << "timeline {\"case\":\"method-state-order\",\"states\":[";
        const auto emitState = [&](const char* operation, bool comma) {
            if (!dump) return;
            if (comma) std::cout << ',';
            std::cout << "{\"operation\":" << std::quoted(operation) << ",\"state\":";
            emit(snapshot(t));
            std::cout << '}';
        };
        emitState("updated", false);
        t.setClip(67);
        emitState("setClip67", true);
        t.setRange(-100, 999999);
        emitState("setRangeIgnoredLibrary", true);
        t.init(-55, 88);
        emitState("initOnlyInts", true);
        require(t.currentClipStart() == 77366 && t.currentClipEnd() == 78233,
                "Current clip getters use library metadata, not separately initialized bounds");
        t.jumpTo(-123);
        emitState("jumpNegative", true);
        if (dump) std::cout << "]}\n";
    }
    for (bool loop : {false, true}) {
        RetailTimeline t;
        t.setRange(100, 100);
        t.setLoop(loop);
        CallbackState state{0, 0, &t};
        t.setCallback(callback, &state);
        begin(loop ? "zero-loop" : "zero-once", dump);
        bool first = true;
        for (auto time : {0, 1, 1, 2}) step(t, time, dump, first);
        finish(dump);
        require(state.calls == (loop ? 2u : 1u) && t.timeMs() == 100, "Zero-span loop and one-shot callback rules");
    }
    {
        RetailTimeline t(clips);
        t.init(-55, 88);
        const auto before = snapshot(t);
        require(t.setClip("no-such-clip") == -1 && snapshot(t) == before, "Unknown name preserves live state");
        if (dump) {
            std::cout << "timeline {\"case\":\"metadata-after-init\",\"values\":["
                      << t.clipCount() << ',' << t.clipStart(0) << ',' << t.clipEnd(0) << ',' << t.clipLength(0)
                      << ',' << t.currentClipStart() << ',' << t.currentClipEnd() << ',' << t.currentClipLength()
                      << ',' << t.currentClipIndex() << "]}\n";
        }
    }
}

void legalCallbacks(std::shared_ptr<const RetailTimeline::Clips> clips, bool dump)
{
    const std::int32_t times[5][5] = {{0, 123, 123, -76, -3875}, {0, 200, 200, 249, 399},
        {0, 77366, 77367, 77466, 78233}, {0, 333, 333, 333, 333}, {0, 333, 333, 333, 333}};
    const unsigned counts[] = {1, 2, 2, 1, 1};
    for (int action : {1, 2, 3, 4, 5}) {
        auto t = action == 2 ? std::make_unique<RetailTimeline>() : std::make_unique<RetailTimeline>(clips);
        if (action == 2) t->setRange(0, 333);
        CallbackState state{0, action, t.get()};
        t->setCallback(callback, &state);
        if (dump) std::cout << "timelinecallback {\"action\":" << action << ",\"steps\":[";
        bool first = true;
        unsigned sample = 0;
        for (auto time : {0, 1000, 1001, 1101, 3000}) {
            step(*t, time, dump, first);
            require(t->timeMs() == times[action - 1][sample++], "Live callback changes and next-frame anchor preserve source order");
        }
        require(state.calls == counts[action - 1], "Live callback completion/replacement count");
        if (dump) std::cout << "],\"callbacks\":" << state.calls << "}\n";
        if (action == 5) require(state.recursiveRejected, "Recursive update explicitly rejected without mutating the nested frame");
    }
}

void domainsAndOwnership()
{
    RetailTimeline raw;
    require(raw.timeMs() == 0 && raw.currentClipIndex() == 0 && raw.getScale() == 1 && !raw.getLoop(), "Original constructor defaults");
    rejects([&] { raw.update(0); });
    rejects([&] { raw.setClip(0); });
    rejects([&] { raw.clipCount(); });
    raw.jumpTo(-123);
    require(raw.timeMs() == -123, "Jump is valid before configuring an integer range");
    rejects([&] { raw.update(0); });
    raw.init(0, 100);
    raw.update(0);
    require(raw.timeMs() == -123, "Init only changes integer range, not current floating time");

    auto clips = std::make_shared<RetailTimeline::Clips>();
    clips->push_back(clip(0, "same", 100, 100));
    clips->push_back(clip(1, "same", -10, 20));
    clips->push_back(clip(2, "Same", INT32_MIN, INT32_MAX));
    RetailTimeline t(clips);
    require(t.clipIndex("same") == 0 && t.clipIndex("Same") == 2 && t.clipLength(0) == 0 &&
                t.clipLength(2) == -1, "Source order/zero-span/signed32 subtract are original semantics");
    t.setLoop(true);
    for (auto time : {0, 1, 1, 2}) {
        t.update(time);
        require(t.timeMs() == 100, "Factory-backed zero-span clip uses the actual zero-span wrap branch");
    }
    require(t.setClip("Same") == 2 && t.timeMs() == INT32_MIN && t.setClip("same") == 0,
            "Case-sensitive names select first source-order duplicate and preserve signed endpoints");
    t.setClip(1);
    require(t.timeMs() == -10 && t.currentClipLength() == 30, "Signed clip endpoints");
    const auto before = snapshot(t);
    rejects([&] { t.setClip(-1); });
    rejects([&] { t.setClip(3); });
    rejects([&] { t.clipIndex(std::string("same\0tail", 9)); });
    require(snapshot(t) == before, "Checked invalid metadata lookup precedes state mutation");
    rejects([] { RetailTimeline missing(std::shared_ptr<const RetailTimeline::Clips>{}); });
    rejects([] { RetailTimeline empty(std::make_shared<const RetailTimeline::Clips>()); });
    auto malformed = std::make_shared<RetailTimeline::Clips>(*clips);
    (*malformed)[1].source.reset();
    rejects([&] { RetailTimeline missing(malformed); });

    RetailTimeline finite;
    finite.setRange(0, 1000);
    finite.setScale(std::numeric_limits<float>::infinity());
    finite.update(0); // Scale is unused for the first anchor.
    finite.update(1); // Original nonloop clamp repairs an infinite intermediate.
    require(finite.timeMs() == 1000, "Late conversion checks cannot reject an intermediate repaired by the original clamp");
    RetailTimeline invalid;
    invalid.setRange(0, 1000);
    invalid.setScale(std::numeric_limits<float>::quiet_NaN());
    invalid.update(0);
    rejects([&] { invalid.update(1); });
    RetailTimeline overflow;
    overflow.setRange(0, INT32_MAX);
    overflow.jumpTo(INT32_MAX);
    rejects([&] { overflow.update(0); });
    RetailTimeline throwing(fixture());
    CallbackState state{0, 6, &throwing};
    throwing.setCallback(callback, &state);
    throwing.update(0);
    rejects([&] { throwing.update(1000); }); // No rollback/resumption promise.

    auto catalog = std::make_shared<RetailActorAnimationCatalog>(
        std::vector<CityAnimationClip>{{"idle", 333, {}, CityAnimationSource{0, 0, 333}}});
    std::weak_ptr<RetailActorAnimationCatalog> lifetime = catalog;
    {
        auto alias = std::shared_ptr<const RetailTimeline::Clips>(catalog, &catalog->clips());
        RetailTimeline retained(alias);
        alias.reset(); catalog.reset();
        require(!lifetime.expired() && retained.clipName(0) == "idle", "Aliased real catalogue backing retained without copying");
    }
    require(lifetime.expired(), "Timeline owns backing without an implicit ownership cycle");
}

void rangeBoundaries()
{
    // Actual original outputs. Reversed ranges are not rejected or normalized;
    // their signed cached span goes unchanged into fmodf.
    const std::int32_t expected[3][4] = {
        {100, 100, 100, -1900}, {1000, 1001, 1100, -900}, {-500, -499, 500, -1500}};
    for (unsigned profile = 0; profile != 3; ++profile) {
        RetailTimeline t;
        t.setRange(profile == 2 ? -500 : 1000, profile == 2 ? 500 : 100);
        t.setLoop(profile != 0);
        unsigned sample = 0;
        for (auto time : {0, 1, 1000, -1000}) {
            t.update(time);
            require(t.timeMs() == expected[profile][sample++], "Original reversed/signed range boundary");
        }
    }
    RetailTimeline initialized;
    initialized.setRange(100, 1000);
    initialized.setLoop(true);
    initialized.init(200, 400);
    initialized.update(0);
    require(initialized.timeMs() == 100, "Init keeps existing floating current");
    initialized.update(1000);
    require(initialized.timeMs() == 800, "Init changes int crossing bound while preserving cached floating start/span");
}
}

int main(int argc, char** argv)
{
    try {
        domainsAndOwnership();
        rangeBoundaries();
        auto clips = fixture();
        bool dump = argc == 2 && std::string(argv[1]) == "--oracle";
        if (argc == 3 && std::string(argv[1]) == "--assets") {
            ResourceFileSystem files(argv[2]);
            clips = std::make_shared<const RetailTimeline::Clips>(loadCityAnimationCatalog(files, "anim_ingame.bdae"));
            dump = true;
            RetailTimeline t(clips);
            require(t.clipCount() == 213, "Actual indexed clip source count");
            for (std::int32_t i = 0; i < t.clipCount(); ++i)
                std::cout << "timelineclip {\"index\":" << i << ",\"name\":" << std::quoted(t.clipName(i))
                          << ",\"start\":" << t.clipStart(i) << ",\"end\":" << t.clipEnd(i)
                          << ",\"length\":" << t.clipLength(i) << ",\"lookup\":" << t.clipIndex(t.clipName(i)) << "}\n";
        }
        referenceProfiles(clips, dump);
        legalCallbacks(clips, dump);
        std::cout << "Original timeline defaults, metadata, state/clock/callback order, ownership and checked domains passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
