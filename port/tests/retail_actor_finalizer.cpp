#include "retail_actor_scene.h"
#include "filesystem.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
using Words = std::array<std::uint32_t,6>;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
RetailSkinBoundingBox box(Words words)
{
    RetailSkinBoundingBox result;
    static_assert(sizeof(result) == sizeof(words));
    std::memcpy(&result,words.data(),sizeof(result));
    return result;
}
Words words(const RetailSkinBoundingBox& box)
{
    Words result;
    std::memcpy(result.data(),&box,sizeof(result));
    return result;
}
Words values(std::array<float,6> value)
{
    Words result;
    std::memcpy(result.data(),value.data(),sizeof(result));
    return result;
}
const Words cube{0xbf800000,0xbf800000,0xbf800000,0x3f800000,0x3f800000,0x3f800000};
std::shared_ptr<RetailActorScene> fixture(std::size_t count, bool mixed = false, bool emptyTag = false)
{
    auto source = std::make_shared<RetailActorSceneSource>();
    auto& model = source->model;
    model.path = emptyTag ? "car_light.bdae" : "finalizer.bdae";
    model.materialCatalog.push_back({"material",{}});
    CityNode parent{};
    parent.name = "parent"; parent.parent = -1;
    parent.rotation = {0,0,0,1}; parent.scale = {1,1,1};
    for (std::size_t i = 0; i < count; ++i) {
        const auto name = std::string("mesh") + std::to_string(i);
        CityMesh mesh;
        mesh.name = name; mesh.positions = {{0,0,0},{1,0,0},{0,1,0}};
        mesh.buffers.push_back({"",{0,1,2}}); mesh.rawBoundsCm = box(cube);
        model.meshes.push_back(mesh);
        parent.geometry.push_back({"",name,{{"","material",0}}});
        parent.instances.push_back({3,i,"",name,false});
    }
    model.nodes.push_back(parent);
    if (mixed) {
        model.nodes[0].jointName = "Bone";
        auto nested = parent;
        nested.name = "nested"; nested.parent = 0;
        model.nodes.push_back(nested);
        auto skinNode = parent;
        skinNode.name = "skin-container"; skinNode.parent = 0;
        skinNode.geometry.resize(1);
        skinNode.instances = {{2,0,"","skin",false}};
        model.nodes.push_back(skinNode);
        CitySkin skin{};
        skin.meshName = "mesh0"; skin.controllerUid = "skin"; skin.bindShape = retailSceneIdentity;
        skin.joints.push_back({1,retailSceneIdentity,"Bone"});
        skin.jointBoundsCm = std::vector<RetailSkinBoundingBox>{box(cube)};
        source->skins.push_back(skin);
    }
    model.visualScenes.push_back({"visual","visual",0,model.nodes.size()});
    return std::make_shared<RetailActorScene>(source);
}
void emit(const char* label, bool result, const Words& output)
{
    std::cout << "finalizer {\"case\":" << std::quoted(label) << ",\"result\":" << result << ",\"words\":[";
    for (std::size_t i = 0; i < output.size(); ++i) { if (i) std::cout << ','; std::cout << output[i]; }
    std::cout << "]}\n";
}
void arithmeticControls(bool dump)
{
    struct Case { const char* name; Words initial; std::vector<Words> inputs; bool alias; Words expected; };
    // Captured by executing original0x2aef15 plus addInternalBox0x20eaad.
    const std::vector<Case> cases{
        {"empty",cube,{},false,cube},
        {"finite",cube,{values({-2,-3,-4,5,6,7})},false,{3221225472,3225419776,3229614080,1084227584,1086324736,1088421888}},
        {"two",values({101,102,103,104,105,106}),{values({-2,-3,-4,5,6,7}),values({-9,8,-7,6,10,12})},false,
            {3239051264,3225419776,3235905536,1120927744,1121058816,1121189888}},
        {"zeros",{0x80000000,0x80000000,0x80000000,0,0,0},{{0,0,0,0x80000000,0x80000000,0x80000000}},false,
            {0x80000000,0x80000000,0x80000000,0,0,0}},
        {"nan-output",{0x7fc01234,0x80000000,0,0x7fc05678,0,0x80000000},{values({-2,-3,-4,5,6,7})},false,
            {2143294004,3225419776,3229614080,2143311480,1086324736,1088421888}},
        {"nan-input",cube,{{0xc0000000,0xff800000,0,0x7fc01234,0x7f800000,0x80000000}},false,
            {3221225472,4286578688,3212836864,1065353216,2139095040,1065353216}},
        {"inverted",cube,{values({5,6,7,1,2,3})},false,{3212836864,3212836864,3212836864,1084227584,1086324736,1088421888}},
        {"alias-inverted",values({5,6,7,1,2,3}),{},true,{1065353216,1073741824,1077936128,1065353216,1073741824,1077936128}}};
    for (const auto& test : cases) {
        auto owner = fixture(test.alias ? 1 : test.inputs.size());
        auto* parent = owner->root().findUid("parent");
        for (std::size_t i = 0; i < test.inputs.size(); ++i)
            const_cast<RetailSkinBoundingBox&>(parent->children()[i]->transformedBoundingBox()) = box(test.inputs[i]);
        // Explicit mutable-cache fixture: actual nodes/getters, no immutable
        // geometry fallback. The original reference uses typed getter records.
        RetailSkinBoundingBox separate = box(test.initial);
        auto& output = test.alias ? const_cast<RetailSkinBoundingBox&>(parent->children()[0]->transformedBoundingBox()) : separate;
        output = box(test.initial);
        const bool result = parent->computeBoundingBox(output);
        require(!result && words(output) == test.expected, "Original helper strict/NaN/alias/return result");
        require(!parent->computeBoundingBox(output) && words(output) == test.expected,
                "Repeated call preserves literal return and exact aggregate bits");
        if (dump) emit(test.name,result,words(output));
    }
}
void nestedAndStale(bool dump)
{
    auto owner = fixture(1);
    auto* parent = owner->root().findUid("parent");
    auto* mesh = parent->children()[0];
    require(!(mesh->transformFlags() & 0x200) && words(owner->postLoadBoundingBox()) == cube &&
            words(owner->root().boundingBox()) == cube && &owner->postLoadBoundingBox() != &owner->root().boundingBox(),
            "Actual constructor finalizer consumes ordinary cache while retaining separate private/public cubes");
    mesh->setPosition({10,20,30});
    RetailSkinBoundingBox output = box(cube);
    require(!parent->computeBoundingBox(output) && words(output) == cube,
            "Helper uses current cached transform/box without eagerly refreshing a local write");
    mesh->updateAbsolutePosition(false);
    require(mesh->transformFlags() & 0x200, "Actual absolute update invalidates transformed cache");
    output = box(values({101,102,103,104,105,106}));
    require(!owner->root().computeBoundingBox(output) && words(output) == values({101,102,103,104,105,106}) &&
            !(mesh->transformFlags() & 0x200), "Nested false return discards aggregate while preserving getter side effects");
    if (dump) emit("nested-only",false,words(output));
    output = box(cube);
    require(!parent->computeBoundingBox(output) && words(output) == values({-1,-1,-1,11,21,31}),
            "Direct helper uses the actual newly computed transformBoxEx cache");
}
void typeControls(bool dump)
{
    auto owner = fixture(1,true);
    auto* parent = owner->root().findUid("parent");
    auto* ordinary = parent->children()[0];
    auto* nested = owner->root().findUid("nested")->children()[0];
    auto* skin = owner->root().findUid("skin-container")->children()[0];
    const_cast<RetailSkinBoundingBox&>(ordinary->transformedBoundingBox()) = box(values({-2,-3,-4,5,6,7}));
    nested->setPosition({10,20,30}); nested->updateAbsolutePosition(false);
    require(nested->transformFlags() & 0x200, "Nested ordinary cache is genuinely invalidated");
    const auto skinFlags = skin->transformFlags();
    RetailSkinBoundingBox output = box(cube);
    require(!parent->computeBoundingBox(output) && words(output) == values({-2,-3,-4,5,6,7}) &&
            !(nested->transformFlags() & 0x200) && skin->transformFlags() == skinFlags && skin->mesh()->skinCacheDirty(),
            "Mixed traversal visits nested ordinary, discards false-return aggregate and skips genuine skin getters");
    if (dump) emit("mixed-types",false,words(output));
    auto empty = fixture(1,false,true);
    auto* ignored = empty->root().findUid("parent")->children()[0];
    require(ignored->type() == 0x79746d65 && (ignored->transformFlags() & 0x200),
            "Original source path selector gives the actual ignored empty runtime tag");
    output = box(cube);
    require(!empty->root().computeBoundingBox(output) && words(output) == cube && (ignored->transformFlags() & 0x200),
            "Unknown/empty mesh category is not silently treated as ordinary geometry");
}
void actualAssets(FileSystemBase& files, bool dump)
{
    for (const char* name : {"char_pthug.bdae","char_pthug_jail.bdae"}) {
        RetailActorScene scene(loadRetailActorSceneSource(files,std::string("./") + name));
        require(words(scene.postLoadBoundingBox()) == cube && words(scene.root().boundingBox()) == cube,
                "Both original Player finalizer private/public root cubes remain distinct");
        for (const auto& node : scene.nodes()) {
            if (node->type() == retailSceneMeshType)
                require(!(node->transformFlags() & 0x200), "Completed finalizer queried each actual ordinary mesh");
            if (node->type() == retailSceneSkinnedMeshType)
                require((node->transformFlags() & 0x200) && node->mesh()->skinCacheDirty(),
                        "Box traversal skips skin before genuine joint attachment marks its cache dirty");
        }
        auto* shadow = scene.root().findUid("shadow-node");
        auto* mesh = shadow->children().front();
        Words first{};
        for (unsigned phase = 0; phase < 3; ++phase) {
            if (phase == 1) mesh->setPosition({10,20,30});
            if (phase == 2) mesh->updateAbsolutePosition(false);
            RetailSkinBoundingBox output = box(cube);
            const bool result = shadow->computeBoundingBox(output);
            if (phase == 0) first = words(output);
            if (phase == 1) require(words(output) == first, "Original live helper preserves stale ordinary cache after local write");
            if (dump) {
                std::cout << "finalizerasset {\"model\":" << std::quoted(name) << ",\"phase\":" << phase
                          << ",\"result\":" << result << ",\"flags\":" << mesh->transformFlags() << ",\"words\":[";
                const auto data = words(output);
                for (std::size_t i = 0; i < data.size(); ++i) { if (i) std::cout << ','; std::cout << data[i]; }
                std::cout << "]}\n";
            }
        }
    }
}
}
int main(int argc, char** argv)
{
    try {
        const bool assets = argc == 3 && std::string(argv[1]) == "--assets";
        const bool dump = assets || (argc == 2 && std::string(argv[1]) == "--oracle");
        arithmeticControls(dump);
        nestedAndStale(dump);
        typeControls(dump);
        if (assets) { ResourceFileSystem files(argv[2]); actualAssets(files,dump); }
        std::cout << "Original complete actor finalizer box phase, strict merge/return/cache behavior passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
