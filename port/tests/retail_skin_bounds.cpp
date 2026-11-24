#include "retail_skin_bounds.h"
#include "filesystem.h"
#include "retail_actor_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Matrix = RetailSkinBoundsMatrix;
using Box = RetailSkinBoundingBox;
Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
uint32_t bits(float x) {
    uint32_t v;
    std::memcpy(&v, &x, 4);
    return v;
}
float value(uint32_t v) {
    float result;
    std::memcpy(&result, &v, 4);
    return result;
}
void require(bool x, const char *message) {
    if (!x)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Missing checked malformed-input rejection");
}
template <class T> void words(const T &values) {
    std::cout << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << bits(values[i]);
    }
    std::cout << ']';
}
void row(const std::string &name, const std::vector<RetailSkinBoundsJoint> &joints,
         const std::vector<Box> &boxes, const Box &output, bool dump) {
    if (!dump)
        return;
    std::cout << "bounds {\"name\":\"" << name << "\",\"matrices\":[";
    for (size_t i = 0; i < joints.size(); ++i) {
        if (i)
            std::cout << ',';
        words(*joints[i].absolute);
    }
    std::cout << "],\"hints\":[";
    for (size_t i = 0; i < joints.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << joints[i].identity;
    }
    std::cout << "],\"boxes\":[";
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << '[';
        words(boxes[i][0]);
        std::cout << ',';
        words(boxes[i][1]);
        std::cout << ']';
    }
    std::cout << "],\"result\":[";
    words(output[0]);
    std::cout << ',';
    words(output[1]);
    std::cout << "]}\n";
}
void cases(bool dump) {
    Matrix a = identity(), b = identity();
    const Box cube{{{-1, -2, -3}, {1, 2, 3}}};
    auto check = [&](const char *name, const std::vector<RetailSkinBoundsJoint> &js,
                     const std::vector<Box> &boxes, const Box &expected) {
        const auto actual = computeRetailSkinBounds(js, boxes);
        for (size_t p = 0; p < 2; ++p)
            for (size_t c = 0; c < 3; ++c)
                require(bits(actual[p][c]) == bits(expected[p][c]), name);
        row(name, js, boxes, actual, dump);
    };
    a = {1, 0, 0, 0, -1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    check("two opposite corners, not eight", {{&a, false}}, {cube}, {{{-1, -2, -3}, {1, 2, 3}}});
    a = {-2, 0, 0, 0, 0, 3, 0, 0, 0, 0, -4, 0, 10, 20, 30, 1};
    check("negative scales repair each axis", {{&a, false}}, {cube}, {{{8, 14, 18}, {12, 26, 42}}});
    a = identity();
    a[12] = 100;
    a[13] = 200;
    a[14] = 300;
    check("actual identity hint bypasses numeric transform", {{&a, true}}, {cube}, cube);
    const Box inverted{{{3, 2, 1}, {-3, -2, -1}}};
    check("hint preserves inverted authored box", {{&a, true}}, {inverted}, inverted);
    a = identity();
    check("false hint repairs inverted authored box", {{&a, false}}, {inverted}, {{{-3, -2, -1}, {3, 2, 1}}});
    a[12] = -0.f;
    a[13] = -0.f;
    a[14] = -0.f;
    b = identity();
    const Box negativeZero{{{-0.f, -0.f, -0.f}, {-0.f, -0.f, -0.f}}};
    const Box positiveZero{};
    check("strict union preserves first signed zero", {{&a, true}, {&b, true}}, {negativeZero, positiveZero},
          negativeZero);
    check("translation-only branch preserves first signed zero", {{&a, false}, {&b, false}}, {},
          negativeZero);
    a[12] = 4;
    a[13] = -7;
    a[14] = 10;
    b[12] = -9;
    b[13] = 11;
    b[14] = 2;
    check("no authored boxes uses live joint translations", {{&a, true}, {&b, false}}, {},
          {{{-9, -7, 2}, {4, 11, 10}}});
    check("union candidate max before min", {{&a, true}, {&b, true}}, {positiveZero, inverted},
          {{{-3, -2, -1}, {3, 2, 1}}});
    // Independently captured original routine output. The source boxes are the
    // first two authored Player boxes, with explicit fractional/sheared live
    // matrices to expose float rounding and cancellation beyond integer cases.
    a = {.1f, .2f, -.3f, 0, -.4f, .5f, .6f, 0, .7f, -.8f, .9f, 0, 10000, -20000, 30000, 1};
    b = {1000,  -1000, .000001f, 0, 1000,   1000,  -.000002f, 0,
         -1000, 1000,  .000003f, 0, -.125f, .375f, -.625f,    1};
    check("original fractional Player-box golden", {{&a, false}, {&b, false}},
          {{{{-10.261199951171875f, -12.427000045776367f, -26.513099670410156f},
             {18.778200149536133f, 19.56170082092285f, 26.84239959716797f}}},
           {{{-26.5132999420166f, -8.023320198059082f, -19.561800003051758f},
             {23.986400604248047f, 18.719999313354492f, -.000009999999747378752f}}}},
          {{{value(0xc669fbc8), value(0xc69c4fe0), value(0xbf200488)},
            {value(0x4726d249), value(0xc485ee40), value(0x46ea9c86)}}});
    std::vector<Matrix> matrices(258, identity());
    std::vector<RetailSkinBoundsJoint> joints;
    std::vector<Box> boxes;
    for (size_t i = 0; i < matrices.size(); ++i) {
        matrices[i][12] = static_cast<float>(i);
        joints.push_back({&matrices[i], false});
        boxes.push_back(positiveZero);
    }
    for (size_t count : {255u, 256u, 257u, 258u}) {
        auto js = joints;
        js.resize(count);
        auto bs = boxes;
        bs.resize(count);
        const auto used = static_cast<uint8_t>(count);
        const float high = used ? static_cast<float>(used - 1) : 0;
        const Box expected{{{0, 0, 0}, {high, 0, 0}}};
        const auto result = computeRetailSkinBounds(js, bs);
        require(result == expected, "Original uint8 joint-loop count");
        row("uint8 count " + std::to_string(count), js, bs, result, dump);
        require(computeRetailSkinBounds(js, std::vector<Box>{}) == expected, "Translation uint8 count");
        row("translation uint8 count " + std::to_string(count), js, {}, expected, dump);
    }
    rejects([&] { computeRetailSkinBounds({}, std::vector<Box>{}); });
    rejects([&] { computeRetailSkinBounds({{&a, false}}, std::nullopt); });
    rejects([&] { computeRetailSkinBounds({{nullptr, false}}, std::vector<Box>{}); });
    rejects([&] { computeRetailSkinBounds({{&a, false}, {&b, false}}, std::vector<Box>{cube}); });
    a = identity();
    a[0] = std::numeric_limits<float>::quiet_NaN();
    require(computeRetailSkinBounds({{&a, true}}, std::vector<Box>{cube}) == cube,
            "Hint does not touch matrix components");
    rejects([&] { computeRetailSkinBounds({{&a, false}}, std::vector<Box>{cube}); });
    require(computeRetailSkinBounds({{&a, false}}, std::vector<Box>{}) == positiveZero,
            "Translation branch ignores linear matrix");
    a = identity();
    a[3] = a[7] = a[11] = a[15] = std::numeric_limits<float>::quiet_NaN();
    require(computeRetailSkinBounds({{&a, false}}, std::vector<Box>{cube}) == cube,
            "Affine bounds ignores homogeneous row");
    a[12] = std::numeric_limits<float>::infinity();
    rejects([&] { computeRetailSkinBounds({{&a, false}}, std::vector<Box>{}); });
    auto ignored = joints;
    ignored.resize(257);
    ignored[1].absolute = nullptr;
    require(computeRetailSkinBounds(ignored, std::vector<Box>{positiveZero}) == positiveZero,
            "Do not read beyond truncated count");
}
std::shared_ptr<RetailActorSceneSource> actorFixture(bool repeated = false) {
    auto result = std::make_shared<RetailActorSceneSource>();
    auto &model = result->model;
    model.path = "./bounds.bdae";
    CityMesh geometry;
    geometry.name = "body";
    geometry.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    geometry.buffers.push_back({"", {0, 1, 2}});
    geometry.rawBoundsCm = Box{{{-100, -200, -300}, {100, 200, 300}}};
    model.meshes.push_back(geometry);
    CityNode container{};
    container.name = "mesh-container";
    container.parent = -1;
    container.rotation = {0, 0, 0, 1};
    container.scale = {1, 1, 1};
    container.geometry.push_back({"", "body", {}});
    container.instances.push_back({2, 0, "", "body-skin", false});
    model.nodes.push_back(container);
    CityNode joint{};
    joint.name = "bone";
    joint.jointName = "Bone";
    joint.parent = -1;
    joint.rotation = {0, 0, 0, 1};
    joint.scale = {1, 1, 1};
    joint.translation = {2, 3, 4};
    model.nodes.push_back(joint);
    if (repeated) {
        container.name = "repeated";
        model.nodes.push_back(container);
    }
    model.visualScenes.push_back({"visual", "visual", 0, model.nodes.size()});
    CitySkin skin{};
    skin.meshName = "body";
    skin.controllerUid = "body-skin";
    skin.bindShape = identity();
    skin.bindShape[12] = 4;
    skin.bindShape[13] = 5;
    skin.bindShape[14] = 6;
    CitySkinJoint binding{1, identity(), "Bone"};
    binding.inverseBind[12] = 1;
    binding.inverseBind[13] = 2;
    binding.inverseBind[14] = 3;
    skin.joints.push_back(binding);
    skin.jointBoundsCm = std::vector<Box>{{{{-1, -2, -3}, {1, 2, 3}}}};
    result->skins.push_back(skin);
    return result;
}
void ownerCases() {
    auto source = actorFixture(true);
    RetailActorScene scene(source);
    auto &node = *scene.root().findMesh();
    auto &skin = *node.mesh();
    auto &sibling = *scene.root().findUid("repeated")->children().front()->mesh();
    require(&skin != &sibling && &skin.baseMesh() != &sibling.baseMesh(),
            "Distinct mutable factory resource and base identities");
    const auto raw = *skin.geometry().rawBoundsCm;
    skin.skinMatrices();
    require(!skin.skinCacheDirty() && skin.boundingBox() == raw,
            "Palette-first exposes actual raw initial box");
    sibling.skinMatrices();
    require(sibling.boundingBox() == raw, "Sibling has its own initial bounds cache");
    node.onAnimate(1);
    const Box first{{{1, 1, 1}, {3, 5, 7}}};
    require(skin.boundingBox() == first,
            "Dynamic box uses live joint abs, ignoring inverse bind and bind shape");
    require(!skin.skinCacheDirty() && &node.boundingBox() == &skin.boundingBox() &&
                &node.transformedBoundingBox() == &skin.boundingBox(),
            "Actual mesh and skin transformed getter alias one cached box");
    require(sibling.boundingBox() == raw && !sibling.skinCacheDirty(),
            "Bounds dirty/cache never leaks across same-UID resources");
    auto *bone = skin.joints()[0];
    bone->setPosition({12, 23, 34});
    bone->updateAbsolutePosition(false);
    require(skin.boundingBox() == first, "TRS setter/update alone does not dirty skin cache");
    node.onAnimate(2);
    skin.skinMatrices();
    require(skin.boundingBox() == first,
            "Real palette preparation clears the single flag before bbox getter");
    skin.computeBoundingBox();
    const Box moved{{{11, 21, 31}, {13, 25, 37}}};
    require(skin.boundingBox() == moved, "Explicit compute forces preparation even while clean");
    bone->setPosition({22, 33, 44});
    bone->updateAbsolutePosition(false);
    skin.computeBoundingBox();
    require(skin.skinMatrices()[0][12] == 27 && skin.boundingBox() == Box{{{21, 31, 41}, {23, 35, 47}}},
            "Clean explicit bounds computation also refreshes genuine final palette");
    scene.root().setPosition({100, 200, 300});
    scene.root().updateAbsolutePosition(true);
    node.onAnimate(3);
    require(skin.boundingBox() == Box{{{121, 231, 341}, {123, 235, 347}}},
            "Joint world absolute already includes root transform");
    for (const auto &live : scene.nodes())
        require(!live->absoluteIdentityHint(), "Proved original constructor/update matrix-hint invariant");
    rejects([&] { scene.root().boundingBox(); });
    rejects([&] { skin.baseMesh().computeBoundingBox(); });
    source = actorFixture();
    source->skins[0].jointBoundsCm = std::vector<Box>{};
    RetailActorScene emptyTable(source);
    require(emptyTable.root().findMesh()->boundingBox() == Box{{{2, 3, 4}, {2, 3, 4}}},
            "Known authored empty table uses live joint translations");
    source = actorFixture();
    source->skins[0].jointBoundsCm.reset();
    RetailActorScene unknown(source);
    auto &unknownSkin = *unknown.root().findMesh()->mesh();
    rejects([&] { unknownSkin.boundingBox(); });
    require(unknownSkin.skinCacheDirty(), "Unknown getter metadata is rejected before mutating the cache");
    rejects([&] { unknownSkin.computeBoundingBox(); });
    require(!unknownSkin.skinCacheDirty(),
            "Explicit compute still prepares before failure and clears dirty; not a resumable transaction");
    rejects([&] { unknownSkin.boundingBox(); });
}
void decoderCases() {
    auto source = actorFixture();
    auto model = source->model;
    model.bytes.resize(512);
    auto word = [&](size_t offset, uint32_t value) {
        for (size_t i = 0; i < 4; ++i)
            model.bytes.at(offset + i) = static_cast<unsigned char>(value >> (i * 8));
    };
    auto append = [&](const std::string &value) {
        const auto offset = static_cast<uint32_t>(model.bytes.size());
        model.bytes.insert(model.bytes.end(), value.begin(), value.end());
        model.bytes.push_back(0);
        return offset;
    };
    auto allocate = [&](size_t count) {
        model.bytes.resize((model.bytes.size() + 3) & ~size_t(3));
        const auto offset = static_cast<uint32_t>(model.bytes.size());
        model.bytes.resize(model.bytes.size() + count);
        return offset;
    };
    word(84, 1);
    word(88, 128);
    word(128, 0);
    word(132, append("body-skin"));
    word(136, 160);
    word(160, append("#body"));
    for (size_t i = 0; i < 16; ++i)
        word(164 + i * 4, bits(identity()[i]));
    word(228, 1);
    word(232, allocate(4));
    word(236, 16);
    word(240, allocate(64));
    auto readWord = [&](size_t offset) {
        uint32_t value = 0;
        for (size_t i = 0; i < 4; ++i)
            value |= uint32_t(model.bytes.at(offset + i)) << (i * 8);
        return value;
    };
    word(readWord(232), append("Bone"));
    for (size_t i = 0; i < 16; ++i)
        word(readWord(240) + i * 4, bits(identity()[i]));
    word(244, 1);
    word(248, allocate(4));
    word(readWord(248), bits(1));
    word(252, 3);
    word(256, allocate(3));
    for (size_t i = 0; i < 3; ++i)
        model.bytes[readWord(256) + i] = 1;
    word(260, 6);
    word(264, allocate(12));
    word(268, 1);
    word(272, allocate(24));
    const std::array<float, 6> authored{3, 2, 1, -3, -2, -1};
    for (size_t i = 0; i < 6; ++i)
        word(readWord(272) + i * 4, bits(authored[i]));
    auto decoded = loadCityActorSkins(model);
    require(decoded.size() == 1 && decoded[0].jointBoundsCm &&
                decoded[0].jointBoundsCm->at(0) == Box{{{3, 2, 1}, {-3, -2, -1}}},
            "Authoritative decoder preserves ordered/inverted authored boxes");
    word(268, 0);
    word(272, 0);
    decoded = loadCityActorSkins(model);
    require(decoded[0].jointBoundsCm && decoded[0].jointBoundsCm->empty(),
            "Serialized empty table is known, not missing metadata");
    word(268, 1);
    word(272, static_cast<uint32_t>(model.bytes.size() - 4));
    rejects([&] { loadCityActorSkins(model); });
    word(272, allocate(24));
    word(readWord(272), bits(std::numeric_limits<float>::infinity()));
    rejects([&] { loadCityActorSkins(model); });
    model.bytes.resize(268);
    rejects([&] { loadCityActorSkins(model); });
}
void liveRow(const std::string &name, unsigned phase, RetailActorSceneNode &node, bool dump) {
    if (!dump)
        return;
    auto &skin = *node.mesh();
    const auto &box = skin.boundingBox();
    std::cout << "livebounds {\"model\":\"" << name << "\",\"phase\":" << phase
              << ",\"dirty\":" << skin.skinCacheDirty() << ",\"box\":[";
    words(box[0]);
    std::cout << ',';
    words(box[1]);
    std::cout << "],\"palette\":[";
    const auto &matrices = skin.skinMatrices();
    for (size_t i = 0; i < matrices.size(); ++i) {
        if (i)
            std::cout << ',';
        words(matrices[i]);
    }
    std::cout << "],\"jointAbs\":[";
    for (size_t i = 0; i < skin.jointMatrices().size(); ++i) {
        if (i)
            std::cout << ',';
        words(*skin.jointMatrices()[i]);
    }
    std::cout << "]}\n";
}
void retail(const std::filesystem::path &directory, bool dump) {
    ResourceFileSystem assets(directory);
    for (const char *name : {"char_pthug.bdae", "char_pthug_jail.bdae"}) {
        auto source = loadRetailActorSceneSource(assets, name);
        require(source->skins.size() == 1 && source->skins[0].jointBoundsCm &&
                    source->skins[0].jointBoundsCm->size() == 22,
                "Actual supplied Player joint-box metadata");
        RetailActorScene scene(source), other(source);
        auto &node = *scene.root().findMesh();
        auto &skin = *node.mesh();
        auto &otherSkin = *other.root().findMesh()->mesh();
        otherSkin.skinMatrices();
        const auto otherBox = otherSkin.boundingBox();
        skin.skinMatrices();
        require(skin.boundingBox() == *skin.geometry().rawBoundsCm, "Actual raw initial cache seed");
        liveRow(name, 0, node, dump);
        node.onAnimate(1);
        skin.boundingBox();
        liveRow(name, 1, node, dump);
        const auto old = skin.boundingBox();
        scene.root().setPosition({100, 200, 300});
        scene.root().updateAbsolutePosition(true);
        require(skin.boundingBox() == old, "Actual loaded owner retains clean cached bounds after transform");
        liveRow(name, 2, node, dump);
        skin.computeBoundingBox();
        liveRow(name, 3, node, dump);
        auto *bone = skin.joints()[0];
        bone->setPosition({17, 23, 31});
        scene.root().onAnimate(4);
        skin.skinMatrices();
        liveRow(name, 4, node, dump);
        skin.computeBoundingBox();
        liveRow(name, 5, node, dump);
        scene.root().setScale({-2, .75f, 1.5f});
        scene.root().setRotation({.2f, .3f, .4f, .5f});
        scene.root().onAnimate(6);
        skin.boundingBox();
        liveRow(name, 6, node, dump);
        node.setOption(1, true);
        node.onAnimate(7);
        skin.boundingBox();
        liveRow(name, 7, node, dump);
        scene.root().setPosition({-31, 11, 17});
        scene.root().updateAbsolutePosition(true);
        require(&node.transformedBoundingBox() == &skin.boundingBox(),
                "Actual loaded skin getter aliases box without extra node transform");
        liveRow(name, 8, node, dump);
        skin.computeBoundingBox();
        liveRow(name, 9, node, dump);
        require(otherSkin.boundingBox() == otherBox && !otherSkin.skinCacheDirty(),
                "Actual separate Player instance bounds/cache unchanged");
        for (const auto &n : scene.nodes())
            require(!n->absoluteIdentityHint(), "Actual full actor constructor/update hint domain");
    }
}
void metadata(const std::filesystem::path &directory, const std::string &path) {
    ResourceFileSystem assets(directory);
    const auto model = loadCityScene(assets, path);
    for (const auto &skin : loadCityActorSkins(model)) {
        require(skin.jointBoundsCm.has_value(), "Decoded metadata is authoritative known table");
        std::cout << "jointboxes {\"path\":" << std::quoted(path)
                  << ",\"controller\":" << std::quoted(skin.controllerUid) << ",\"boxes\":[";
        for (size_t i = 0; i < skin.jointBoundsCm->size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << '[';
            words(skin.jointBoundsCm->at(i)[0]);
            std::cout << ',';
            words(skin.jointBoundsCm->at(i)[1]);
            std::cout << ']';
        }
        std::cout << "]}\n";
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--metadata") {
            metadata(argv[2], argv[3]);
            return 0;
        }
        cases(argc > 1 && std::string(argv[1]) == "--dump");
        ownerCases();
        decoderCases();
        if (argc > 1 && std::string(argv[1]) != "--dump")
            retail(argv[1], argc > 2 && std::string(argv[2]) == "--dump");
        std::cout << "Original skin bounds core checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
