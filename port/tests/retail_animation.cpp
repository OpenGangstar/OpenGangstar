#include "retail_animation.h"
#include "filesystem.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
constexpr std::array<float, 16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message, float tolerance = 0.0001f)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw std::runtime_error(std::string(message) + " Expected " + std::to_string(expected) +
                                 ", observed " + std::to_string(actual));
}
template<size_t N> void near(const std::array<float, N>& actual, const std::array<float, N>& expected, const char* message)
{
    for (size_t i = 0; i < N; ++i) near(actual[i], expected[i], message);
}
CityAnimationChannel linearRoot(std::array<float, 3> first = {}, std::array<float, 3> last = {100,0,0})
{
    return {{}, CityAnimationTransform::translation,
        {{0, {first[0],first[1],first[2],0}}, {100, {last[0],last[1],last[2],0}}}, "Bip01-node"};
}
RetailAnimationClock running(bool loop = true)
{
    RetailAnimationClock clock;
    uint32_t flags = 0;
    require(selectRetailAnimation(clock, {202,1,loop,true,false}, 500,600,flags,false).selection ==
            RetailAnimationSelection::accepted, "Initial clock selection failed.");
    return clock;
}

void verifySelection()
{
    using Selection = RetailAnimationSelection;
    for (int id = 1; id <= 23; ++id)
        require(!retailAnimationCanBeInterrupted(id), "An original protected animation became interruptible.");
    for (const int id : {86,164,175})
        require(!retailAnimationCanBeInterrupted(id), "An original special protected animation became interruptible.");
    for (const int id : {0,24,85,87,163,165,174,176,202})
        require(retailAnimationCanBeInterrupted(id), "Interruptibility was inferred from a name or range.");
    auto clock = running();
    clock.cursor = 27.5f;
    clock.rate = 2;
    clock.cachedRoot = {4,5,6};
    clock.rootHeight = 6;
    clock.nextAnimation = 99;
    uint32_t flags = 0;
    require(selectRetailAnimation(clock, {202,9,false,false,true}, 0,0,flags,false).selection == Selection::repeated &&
            clock.cursor == 27.5f && clock.rate == 2 && clock.loop && clock.rootMotion &&
            !clock.directionalMotion && clock.nextAnimation == 99 && clock.cachedRoot == std::array<float,3>{4,5,6},
            "Repeated selection reset phase, request options, queue, or root cache.");
    flags = 0x800;
    require(selectRetailAnimation(clock, {-1,1,false,false,false}, 0,0,flags,false).selection == Selection::rejected &&
            clock.animation == 202, "Animation lock did not precede the clear request.");
    flags = 0;
    require(selectRetailAnimation(clock, {-1,1,false,false,false}, 0,0,flags,false).selection == Selection::cleared &&
            clock.animation == -1 && clock.cursor == 0 && clock.rate == 0 && !clock.loop &&
            clock.cachedRoot == std::array<float,3>{4,5,6} && clock.rootHeight == 6 && clock.nextAnimation == 99,
            "Clearing selection incorrectly cleared root cache, height, or deferred animation.");
    selectRetailAnimation(clock, {1,1,false,false,false}, 0,100,flags,false);
    clock.cursor = 10;
    require(selectRetailAnimation(clock, {202,2,true,true,false}, 0,100,flags,false).selection == Selection::rejected &&
            clock.animation == 1 && clock.cursor == 10 && clock.nextAnimation == -1,
            "Protected nonlooping animation did not reject the interruptible request.");
    require(selectRetailAnimation(clock, {86,1,true,true,false}, 0,100,flags,false).selection == Selection::deferred &&
            clock.nextAnimation == 86, "Protected-to-protected interruption did not defer the requested ID.");
    require(selectRetailAnimation(clock, {86,1,true,true,false}, 0,100,flags,true).selection == Selection::accepted &&
            clock.nextAnimation == -1 && clock.cursor == 0 && clock.cachedRoot == std::array<float,3>{4,5,6},
            "Cinematic interruption or accepted-animation cache retention differs from the original.");
    require(selectRetailAnimation(clock, {1,1,false,false,false}, 0,100,flags,false).selection == Selection::accepted,
            "A looping animation unexpectedly used the nonloop interruption guard.");
    clock.cursor = 100;
    require(selectRetailAnimation(clock, {86,1,true,true,false}, 0,100,flags,false).selection == Selection::accepted,
            "An ended nonlooping animation remained protected.");
    selectRetailAnimation(clock, {67,1,true,false,false}, 0,100,flags,false);
    const auto rocket = selectRetailAnimation(clock, {164,1,false,false,false}, 0,100,flags,false,7);
    require(rocket.selection == Selection::accepted && rocket.reloadRocket && flags == 0x80000000u,
            "Original reload intent or animation164's literal movement flag was lost.");
    clock = running();
    clock.speedMultiplier = 3;
    selectRetailAnimation(clock, {188,0.5f,true,true,false}, 700,800,flags,false);
    require(clock.rate == 1.5f, "Integer animation-speed multiplier was not applied on selection.");
}

void verifyIntegerClock()
{
    struct Case { float cursor, advance; bool loop, last; };
    for (const auto test : {Case{0,100,true,false}, {10,200,true,false}, {90,100,true,false},
                           {90,20,true,true}, {90,220,true,true}, {10,220,true,false},
                           {0,99,false,false}, {0,100,false,true}, {100,0,false,true}}) {
        auto clock = running(test.loop);
        clock.cursor = test.cursor;
        require(retailAnimationLastFrame(clock,test.advance) == test.last,
                "Integer remainder/end predicate counted an arbitrary number of animation cycles.");
    }
    auto root = linearRoot();
    for (const float elapsed : {20.f,220.f}) {
        auto clock = running();
        clock.cursor = 90;
        clock.cachedRoot = {90,0,0};
        const auto step = advanceRetailAnimation(clock,elapsed,&root,identity,true);
        require(step.lastFrame && step.relativeMs == 10 && step.absoluteMs == 510,
                "Loop did not finish at the truncated integer remainder.");
        near(step.scaledDisplacement[0],0.019f,"Loop must accumulate the end-1 tail and the new head once.");
    }
    auto clock = running();
    clock.cursor = 10;
    clock.cachedRoot = {10,0,0};
    auto step = advanceRetailAnimation(clock,220,&root,identity,true);
    require(!step.lastFrame && step.relativeMs == 30,"Large delta unexpectedly detected whole cycles.");
    near(step.scaledDisplacement[0],0.02f,"Large-delta direct sampling was replaced with cycle integration.");
    clock = running(false);
    step = advanceRetailAnimation(clock,100,&root,identity,true);
    require(step.lastFrame && step.relativeMs == 99 && clock.cursor == 100,
            "Nonloop end must sample length-1 without clamping the floating cursor.");
    near(step.scaledDisplacement[0],0.099f,"Nonloop end sampled the exclusive end key.");
    step = advanceRetailAnimation(clock,50,&root,identity,true);
    require(step.lastFrame && clock.cursor == 150 && step.bodyVelocity && !step.wakeBody,
            "Repeated nonloop end must preserve clock advance and write zero root velocity.");
    near(step.bodyVelocity->at(0),0,"Ended nonloop animation kept moving.");
    clock = running();
    clock.rate = 0.5f;
    step = advanceRetailAnimation(clock,1,&root,identity,true);
    require(clock.cursor == 0.5f && step.relativeMs == 0,"Sampling used a floating frame instead of integer truncation.");
    step = advanceRetailAnimation(clock,1,&root,identity,true);
    require(clock.cursor == 1 && step.relativeMs == 1,"Fractional animation phase was discarded.");
    clock.rate = 0;
    step = advanceRetailAnimation(clock,1000,&root,identity,true);
    require(clock.cursor == 1 && step.relativeMs == 1 && !step.bodyVelocity,
            "A zero animation rate used positive wall time to overwrite body velocity.");
}

void verifySignedZeroRoot()
{
    const CityAnimationChannel root{{}, CityAnimationTransform::translation,
        {{0,{0,0,0,0}}, {10,{-0.f,-0.f,-0.f,0}}, {99,{-0.f,-0.f,-0.f,0}},
         {100,{-0.f,-0.f,-0.f,0}}}, "Bip01-node"};
    struct Case { bool loop; float cursor, elapsed; bool negative; };
    for (const auto test : {Case{false,0,10,false}, {false,0,100,true}, {true,90,20,true}}) {
        auto clock = running(test.loop);
        clock.cursor = test.cursor;
        const auto step = advanceRetailAnimation(clock,test.elapsed,&root,identity,true);
        require(step.bodyVelocity && !step.wakeBody,"Signed zero must write velocity without waking the body.");
        for (const float component : step.scaledDisplacement)
            require(component == 0 && std::signbit(component) == test.negative,
                    "Ordinary +0 accumulation or direct tail assignment lost its original sign bit.");
        for (const float component : *step.bodyVelocity)
            require(component == 0 && std::signbit(component) == test.negative,
                    "Root displacement signed zero did not reach original body velocity.");
        for (const float component : step.visualRootTranslation)
            require(component == 0 && !std::signbit(component),"Visual root removal must assign literal positive zero.");
        for (const float component : clock.cachedRoot)
            require(component == 0 && std::signbit(component),"Cached sampled root lost its negative-zero key value.");
        require(std::signbit(clock.rootHeight),"Retained root height lost its sampled negative-zero sign.");
    }
}

void verifySamplesAndTransform()
{
    auto root = linearRoot({10,20,30},{110,220,330});
    near(sampleRetailAnimationRoot(root,25,RetailAnimationInterpolation::linear),{35,70,105},"Root interpolation differs from original lerp.");
    near(sampleRetailAnimationRoot(root,25,RetailAnimationInterpolation::step),{10,20,30},"Step root interpolation blended keys.");
    near(sampleRetailAnimationRoot(root,-20,RetailAnimationInterpolation::linear),{10,20,30},"Root sampler did not clamp before first key.");
    near(sampleRetailAnimationRoot(root,120,RetailAnimationInterpolation::linear),{110,220,330},"Root sampler did not hold the final key.");
    auto exact = linearRoot();
    exact.keys = {{0,{1e20f,1,1,0}}, {10,{3,-0.f,-4,0}}, {20,{-1e20f,2,7,0}}};
    const auto exactValue = sampleRetailAnimationRoot(exact,10,RetailAnimationInterpolation::linear);
    require(exactValue[0] == 3 && std::signbit(exactValue[1]) && exactValue[2] == -4,
            "Exact key sampling used lerp at weight zero/one instead of the original raw key copy.");
    exact.keys.insert(exact.keys.begin()+2,exact.keys[1]);
    bool duplicateRejected = false;
    try { sampleRetailAnimationRoot(exact,10,RetailAnimationInterpolation::linear); }
    catch (const std::runtime_error&) { duplicateRejected = true; }
    require(duplicateRejected,"Duplicate root keys escaped the native decoder's documented strict-key domain.");
    auto clock = running();
    auto step = advanceRetailAnimation(clock,0,&root,identity,true);
    near(step.scaledDisplacement,{0.01f,0.02f,0.03f},"First root sample did not subtract the constructor's zero cache.");
    require(!step.bodyVelocity && clock.cachedRoot == std::array<float,3>{10,20,30} && clock.rootHeight == 30 &&
            step.visualRootTranslation == std::array<float,3>{},"Zero advance lost cache/height or applied root translation twice.");
    root = linearRoot({}, {100,200,300});
    auto matrix = identity;
    matrix[0]=0; matrix[4]=-2; matrix[8]=1;
    matrix[1]=3; matrix[5]=0; matrix[9]=4;
    matrix[12]=123456; matrix[13]=-987654;
    clock = running();
    step = advanceRetailAnimation(clock,1,&root,matrix,true);
    require(step.bodyVelocity.has_value(),"Root motion did not produce body velocity.");
    near(*step.bodyVelocity,{-10,150},"Root displacement matrix axes, scale, or translation exclusion are incorrect.");
    require(clock.rootHeight == 3 && step.visualRootTranslation == std::array<float,3>{},
            "Root Z retention or visual translation removal differs from the original.");
    root = linearRoot({}, {0,0,300});
    clock = running();
    step = advanceRetailAnimation(clock,1,&root,matrix,true);
    require(step.bodyVelocity && !step.wakeBody,"Original body wake test must use localXY before the matrix transform.");
    near(*step.bodyVelocity,{30,120},"Root Z was incorrectly dropped before displacement transformation.");
    root = linearRoot({}, {300,400,0});
    clock = running(); clock.directionalMotion = true;
    step = advanceRetailAnimation(clock,1,&root,matrix,true,std::array<float,2>{0.6f,0.8f});
    near(*step.bodyVelocity,{30,40},"Directional root motion used the matrix or an average animation speed.");
    clock = running(); clock.rootMotion = false;
    step = advanceRetailAnimation(clock,20,&root,identity,true);
    require(step.evaluated && !step.bodyVelocity && clock.cursor == 20 && clock.cachedRoot[0] == 60 &&
            step.visualRootTranslation == std::array<float,3>{},"Disabling root motion must still sample/cache and strip visual root translation.");
    clock = running();
    step = advanceRetailAnimation(clock,10,&root,identity,false);
    require(step.evaluated && !step.bodyVelocity && clock.cursor == 10,"Missing body incorrectly stopped animation time.");
    clock = running();
    step = advanceRetailAnimation(clock,10,nullptr,identity,true);
    require(step.lastFrame && !step.evaluated && clock.cursor == 0,"Unbound root advanced the original guarded clock.");
    bool rejected = false;
    try { advanceRetailAnimation(clock,-1,&root,identity,true); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && clock.cursor == 0,"Unsupported negative clock was silently clamped or partially advanced.");
    clock.cursor = 2147483520.f;
    rejected = false;
    try { advanceRetailAnimation(clock,128,&root,identity,true); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && clock.cursor == 2147483520.f,"Int32 conversion overflow was allowed or silently capped.");
}

void verifyRetail(const std::filesystem::path& path)
{
    ResourceFileSystem assets(path);
    const auto library = loadCityScene(assets,"anim_ingame.bdae");
    const auto word = [&](size_t offset) {
        require(offset + 4 <= library.bytes.size(),"Truncated retail clip fixture metadata.");
        const auto* p = library.bytes.data() + offset;
        return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
    };
    const auto clips = loadCityDoorClips(assets,"anim_ingame.bdae");
    require(clips.size() == 213 && word(library.rootOffset+28) == clips.size(),"Retail animation catalog size changed.");
    size_t samples = 0;
    for (size_t id = 0; id < clips.size(); ++id) {
        const auto& clip = clips[id];
        const auto target = std::find_if(clip.channels.begin(),clip.channels.end(),[](const auto& channel) {
            return channel.target == "Bip01-node" && channel.transform == CityAnimationTransform::translation;
        });
        require(target != clip.channels.end() && target->keys.front().timeMs == 0 &&
                target->keys.back().timeMs >= clip.durationMs-1,"Decoded root track lost a necessary clip boundary neighbor.");
        const auto range = word(library.rootOffset+32) + id*12;
        const auto start = word(range+4), end = word(range+8);
        require(end-start == clip.durationMs,"Decoded clip duration differs from original end-start.");
        RetailAnimationClock clock;
        uint32_t flags = 0;
        selectRetailAnimation(clock,{static_cast<int32_t>(id),1,false,true,false},start,end,flags,false);
        auto step = advanceRetailAnimation(clock,0,&*target,identity,true);
        near(clock.cachedRoot,{target->keys.front().value[0],target->keys.front().value[1],target->keys.front().value[2]},
             "Initial retail root sample differs from the authored boundary.");
        step = advanceRetailAnimation(clock,float(clip.durationMs),&*target,identity,true);
        require(step.lastFrame && step.relativeMs == int32_t(clip.durationMs-1) && step.absoluteMs == int32_t(end-1) &&
                step.visualRootTranslation == std::array<float,3>{},"Retail nonloop boundary or visual root removal failed.");
        samples += 2;
    }
    struct Fixture { size_t id; const char* name; std::array<float,3> start, one, hundred, last; };
    // Independent constants read from the raw retail global root track, applying
    // the disassembled float lerp at original integer timeline positions.
    const Fixture fixtures[]{
        {188,"move_run",{1.5f,-9.3462095f,90.621803f},{1.4908556f,-9.8119526f,90.557014f},
            {0.60856736f,-53.490215f,84.321007f},{1.4936662f,-322.84933f,90.707138f}},
        {202,"move_walk",{0,0,89.253403f},{0.0082534244f,-0.13794515f,89.244591f},
            {0.91975188f,-13.522711f,88.388519f},{-0.011582524f,-127.31875f,89.280289f}},
        {212,"move_walk_to_idle",{0,0,89.253403f},{0.0089719594f,-0.104663f,89.2174f},
            {0.897196f,-10.4663f,85.653397f},{0.00056074187f,-36.06522f,94.216393f}}
    };
    for (const auto& fixture : fixtures) {
        const auto& clip = clips.at(fixture.id);
        require(clip.name == fixture.name,"Original animation ID was replaced with a guessed clip name.");
        const auto& root = *std::find_if(clip.channels.begin(),clip.channels.end(),[](const auto& c) {
            return c.target == "Bip01-node" && c.transform == CityAnimationTransform::translation;
        });
        near(sampleRetailAnimationRoot(root,0,RetailAnimationInterpolation::linear),fixture.start,"Retail root start differs from raw fixture.");
        near(sampleRetailAnimationRoot(root,1,RetailAnimationInterpolation::linear),fixture.one,"Retail integer-frame root lerp differs from raw fixture.");
        near(sampleRetailAnimationRoot(root,100,RetailAnimationInterpolation::linear),fixture.hundred,"Retail root profile became average-speed motion.");
        near(sampleRetailAnimationRoot(root,clip.durationMs-1,RetailAnimationInterpolation::linear),fixture.last,"Retail end-1 sample differs from raw fixture.");
        RetailAnimationClock clock;
        uint32_t flags = 0;
        selectRetailAnimation(clock,{int32_t(fixture.id),1,true,true,false},0,clip.durationMs,flags,false);
        advanceRetailAnimation(clock,0,&root,identity,true);
        auto step = advanceRetailAnimation(clock,float(clip.durationMs),&root,identity,true);
        require(!step.lastFrame && step.relativeMs == 0,"Exact retail full-cycle advance incorrectly counted a wrap.");
        near(step.scaledDisplacement,{},"Exact retail full-cycle advance gained invented cycle displacement.");
        step = advanceRetailAnimation(clock,float(2*clip.durationMs+100),&root,identity,true);
        require(!step.lastFrame && step.relativeMs == 100,"Retail large-delta remainder behavior differs from original.");
        for (size_t axis = 0; axis < 3; ++axis)
            near(step.scaledDisplacement[axis],(fixture.hundred[axis]-fixture.start[axis])*0.001f,
                 "Retail large-delta displacement counted unobserved full cycles.");
        samples += 6;
    }
    std::cout << clips.size() << " original clip roots validated; " << samples << " boundary/profile/clock samples checked.\n";
}
}

int wmain(int argc, wchar_t** argv)
{
    try {
        verifySelection();
        verifyIntegerClock();
        verifySignedZeroRoot();
        verifySamplesAndTransform();
        if (argc > 1) verifyRetail(argv[1]);
        std::cout << "Original animation clock, root cache, interpolation, and transform checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
