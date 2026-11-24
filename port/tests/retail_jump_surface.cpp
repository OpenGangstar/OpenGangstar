#include "retail_jump_surface.h"
#include "filesystem.h"
#include "retail_jump.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
using Point = std::array<float, 3>;
void dump(const RetailJumpSurfaceSource&, uint8_t, const RetailJumpSurfaceDefinition&, uint8_t);
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void close(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < .002f, message);
}
uint32_t bits(float value) {
    uint32_t b;
    std::memcpy(&b, &value, 4);
    return b;
}
RetailJumpMatrix identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
b2AABB bounds() {
    b2AABB b;
    b.lowerBound.Set(-2000, -2000);
    b.upperBound.Set(2000, 2000);
    return b;
}
RetailJumpSurfaceSource source() {
    return {"source",    "visible-owner",
            "Jump-node", {{100, 0, 10}, {100, 100, 110}, {-100, 100, 110}, {-100, 0, 10}},
            identity(),  identity()};
}
void descriptorAndNativeSnapshot() {
    RetailJumpSurfaceSequence sequence(255);
    RetailJumpSurfaceCatalog catalog(sequence);
    auto input = source();
    input.currentAbsolute[12] = 300;
    input.currentAbsolute[13] = 400;
    input.currentAbsolute[14] = 500;
    input.placementAbsolute[12] = 1000;
    input.placementAbsolute[13] = 2000;
    input.placementAbsolute[14] = 700;
    require(catalog.append(input) && catalog.size() == 1 && sequence.nextID() == 0,
            "Original wrapping byte ID consumed at definition creation");
    const auto& def = catalog.definition(0);
    require(def.metadata.owner == nullptr && !def.metadata.nodeBounds && def.metadata.jumpSurface &&
                def.metadata.opaqueJumpFlag == 255,
            "Original jump metadata owner/node/flag/ID");
    require(def.metadata.heightsCm == std::array<float, 4>{710, 710, 810, 810},
            "Original cyclic raw vertex order and placement Z heights");
    require(def.body.position.x == 3 && def.body.position.y == 4 && def.body.angle == 0 &&
                def.body.massData.mass == 0 && def.body.massData.I == 0 && def.body.allowSleep &&
                !def.body.isSleeping && !def.body.fixedRotation && !def.body.isBullet,
            "Original current-node body pose and default static definition");
    const std::array<b2Vec2, 4> expected{{{-1, 0}, {1, 0}, {1, 1}, {-1, 1}}};
    for (size_t i = 0; i < 4; ++i)
        require(def.polygon.vertices[i].x == expected[i].x && def.polygon.vertices[i].y == expected[i].y,
                "Polygon follows vertex table rather than triangle index order");
    require(def.polygon.friction == .2f && def.polygon.density == 1 && def.polygon.restitution == 0 &&
                !def.polygon.isSensor && def.polygon.filter.categoryBits == 1 &&
                def.polygon.filter.maskBits == 0xffff && def.polygon.filter.groupIndex == 0,
            "Original jump material/filter");
    b2World world(bounds(), b2Vec2(0, 0), true);
    catalog.activate(world);
    require(world.GetBodyCount() == 2 && world.GetProxyCount() == 1 && catalog.bodies().size() == 1,
            "Actual jump body and proxy activation");
    auto* body = catalog.bodies()[0];
    require(body->IsStatic() && body->GetMass() == 0,
            "Density1 polygon remains static without SetMassFromShapes");
    b2MassData mass;
    body->GetShapeList()->ComputeMass(&mass);
    close(mass.mass, 2, "Actual polygon density computes positive mass while body remains static");
    RetailPhysicsOwnerState owner{5, 750, 0, 0};
    RetailJumpController jump(owner);
    jump.setCurrentShape(*body->GetShapeList());
    require(jump.state().quadCm[0] == Point{200, 400, 710} && jump.state().opaqueJumpFlag == 255,
            "Real snapshot binds body XY to ordered metadata heights");
    close(jump.height({300, 450, 750}), 760,
          "Independent original plane z=710+(y-400) through actual producer and snapshot");
    catalog.activate(world);
    require(world.GetBodyCount() == 2, "Visibility activation is idempotent");
    bool activeAppendRejected = false;
    try {
        catalog.append(input);
    } catch (const std::logic_error&) {
        activeAppendRejected = true;
    }
    require(activeAppendRejected && catalog.size() == 1 && sequence.nextID() == 0,
            "Active definitions cannot be mutated or consume another ID");
    catalog.deactivate();
    require(world.GetBodyCount() == 1 && world.GetProxyCount() == 0 && sequence.nextID() == 0 &&
                catalog.definition(0).metadata.opaqueJumpFlag == 255,
            "Deactivation retains definitions/metadata/ID");
    catalog.activate(world);
    require(world.GetProxyCount() == 1 && sequence.nextID() == 0,
            "Reactivation reuses original definition without consuming ID");
    catalog.deactivate();
    auto second = source();
    second.ownerIdentity = "different-scene-owner";
    bool ownerRejected = false;
    try {
        catalog.append(second);
    } catch (const std::invalid_argument&) {
        ownerRejected = true;
    }
    require(ownerRejected && catalog.size() == 1 && sequence.nextID() == 0,
            "Independent scene owners cannot be silently merged into one visibility catalog");
    second.ownerIdentity = input.ownerIdentity;
    require(catalog.append(second) && catalog.definition(1).metadata.opaqueJumpFlag == 0 &&
                sequence.nextID() == 1,
            "Subsequent ID0 remains a real enabled jump, not a boolean clear");
}
void skipsAndFirstVertex() {
    RetailJumpSurfaceSequence nonmeshSequence(42);
    RetailJumpSurfaceCatalog nonmeshCatalog(nonmeshSequence);
    auto nonmesh = source();
    nonmesh.runtimeType = retailSceneEmptyType;
    require(!nonmeshCatalog.append(nonmesh) && nonmeshSequence.nextID() == 42,
            "Unrecognized original runtime node type skips the direct leaf producer without consuming ID");

    RetailJumpSurfaceSequence sequence;
    RetailJumpSurfaceCatalog catalog(sequence);
    auto input = source();
    input.uid = "jump-node";
    require(!catalog.append(input) && sequence.nextID() == 0, "Original UID matching is case sensitive");
    input.uid = "Jump-node";
    input.verticesCm.pop_back();
    require(!catalog.append(input) && sequence.nextID() == 0,
            "Non-four-vertex original buffer skips without ID");
    input = source();
    for (auto& v : input.verticesCm)
        v[1] += 50;
    require(catalog.append(input), "No qualifying lower-left vertex uses original wrap-to-zero ordering");
    require(catalog.definition(0).metadata.heightsCm == std::array<float, 4>{10, 110, 110, 10},
            "y==50 does not pass strict y<50 seed condition");
    auto next = source();
    next.verticesCm[3][0] = 0;
    require(catalog.append(next) &&
                catalog.definition(1).metadata.heightsCm == std::array<float, 4>{10, 110, 110, 10},
            "x==0 does not pass strict x<0 seed condition");
}
struct Observer : b2DestructionListener {
    RetailJumpSurfaceCatalog* catalog = nullptr;
    size_t seen = 0;
    bool metadataLive = true, clearedEarlier = true;
    void SayGoodbye(b2Joint*) override {}
    void SayGoodbye(b2Shape* shape) override {
        const auto* data = static_cast<RetailPhysicsBodyData*>(shape->GetBody()->GetUserData());
        metadataLive = metadataLive && data && data->jumpSurface && std::isfinite(data->heightsCm[0]);
        for (size_t i = 0; i < seen; ++i)
            clearedEarlier = clearedEarlier && catalog->bodies()[i] == nullptr;
        ++seen;
    }
};
void lifetimeAndRange() {
    RetailJumpSurfaceSequence sequence;
    RetailJumpSurfaceCatalog catalog(sequence);
    auto a = source(), b = source();
    b.currentAbsolute[12] = 1000;
    catalog.append(a);
    catalog.append(b);
    b2World world(bounds(), b2Vec2(0, 0), true);
    catalog.activate(world);
    Observer listener;
    listener.catalog = &catalog;
    world.SetDestructionListener(&listener);
    catalog.deactivate();
    world.SetDestructionListener(nullptr);
    require(listener.seen == 2 && listener.metadataLive && listener.clearedEarlier,
            "Real destruction callback sees live metadata and NULL earlier body slots");
    RetailJumpSurfaceCatalog outside(sequence);
    a.currentAbsolute[12] = 300000;
    outside.append(a);
    bool failed = false;
    try {
        outside.activate(world);
    } catch (const std::runtime_error& e) {
        failed = std::string(e.what()).find("outside world proxy range") != std::string::npos;
    }
    require(failed && outside.bodies().empty() && world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Initial world-range preflight never silently drops jump proxy");
    failed = false;
    try {
        outside.activate(world);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "Failed activation remains inactive for explicit retry, not false idempotent success");
}
void actualFilter() {
    RetailJumpSurfaceSequence sequence;
    RetailJumpSurfaceCatalog catalog(sequence);
    auto input = source();
    catalog.append(input);
    b2World world(bounds(), b2Vec2(0, 0), true);
    world.SetContinuousPhysics(false);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    unsigned calls = 0;
    RetailContactFilter filter(&owner, [&](RetailPhysicsOwnerState& supplied, b2Shape& shape) {
        require(&supplied == &owner, "Actual jump filter owner identity");
        jump.setCurrentShape(shape);
        ++calls;
    });
    world.SetContactFilter(&filter);
    catalog.activate(world);
    RetailPhysicsBodyData actorData{&owner, RetailPhysicsZBounds{-25, 150}, {}, false};
    b2BodyDef bd;
    bd.position.Set(0, .25f);
    bd.userData = &actorData;
    auto* body = world.CreateBody(&bd);
    body->SetLinearVelocity(b2Vec2(0, 1));
    b2CircleDef circle;
    circle.radius = .2f;
    circle.density = 1;
    body->CreateShape(&circle);
    body->SetMassFromShapes();
    world.Step(1.f / 60, 10);
    require(calls && jump.state().enabled && jump.state().opaqueJumpFlag == 0,
            "Genuine produced polygon drives verified original filter and ID0 snapshot");
    close(jump.height({0, 50, 0}), 60, "Real produced surface/filter plane z=10+y");
    world.DestroyBody(body);
    catalog.deactivate();
    world.SetContactFilter(nullptr);
}
CityNode node(std::string name, Point translation = {}) {
    CityNode n{};
    n.name = std::move(name);
    n.parent = -1;
    n.translation = translation;
    n.rotation = {0, 0, 0, 1};
    n.scale = {1, 1, 1};
    return n;
}
void decodedBinding(bool emit = false) {
    CityAssets city;
    CityScene model;
    model.path = "model";
    CityMesh visible;
    visible.name = "visible";
    visible.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    visible.buffers.push_back({"", {0, 1, 2}});
    CityMesh mesh;
    mesh.name = "Jump-mesh";
    mesh.positions = source().verticesCm;
    mesh.buffers.push_back({"", {3, 2, 1, 1, 0, 3}});
    model.meshes = {visible, mesh};
    // Enclosing SNode UID deliberately disagrees with generated mesh UID.
    auto v = node("Jump-container-with-visible-child", {2, 3, 4});
    v.geometry.push_back({"", "visible", {}});
    auto j = node("ordinary-container", {9, 8, 7});
    j.geometry.push_back({"", "Jump-mesh", {}});
    model.nodes = {v, j};
    city.geometryLibraries.emplace("model", model);
    CityScene scene;
    scene.path = "district";
    auto inst = node("instance", {100, 200, 300});
    inst.scale = {5, 6, 7};
    inst.geometry.push_back({"model", "visible", {}});
    scene.nodes.push_back(inst);
    city.geometryLibraries.at("model").path = "road_sign.bdae";
    for (uint32_t kind : {1u, 2u, 11u}) {
        city.geometryLibraries.at("model").nodes[1].instances.push_back(
            {kind, std::nullopt, {}, "Jump-controller", false});
        bool rejected = false;
        try {
            collectRetailJumpSurfaceSources(city, scene);
        } catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("node/controller/modular-skin") != std::string::npos;
        }
        require(rejected, "Ordinary filename predicate cannot silently discard unsupported recognized "
                          "controller/node classes");
        city.geometryLibraries.at("model").nodes[1].instances.clear();
    }
    require(collectRetailJumpSurfaceSources(city, scene).empty(),
            "Original database-path sign marker makes generated mesh type empty, so no Jump collider");
    city.geometryLibraries.at("model").path = "model";
    const auto result = collectRetailJumpSurfaceSources(city, scene);
    require(result.size() == 1, "External scene binds authored Jump mesh exactly once");
    require(result[0].uid == "Jump-mesh",
            "Generated geometry UID comes from mesh, never enclosing SNode UID");
    require(result[0].currentAbsolute[12] == 109 && result[0].currentAbsolute[13] == 208 &&
                result[0].placementAbsolute[12] == 102 && result[0].placementAbsolute[14] == 304,
            "Distinct authored Jump and visible placement offsets, instance scale not copied by AddXRef");
    RetailJumpSurfaceSequence sequence;
    RetailJumpSurfaceCatalog catalog(sequence);
    catalog.append(result[0]);
    require(catalog.definition(0).body.position.x == 1.09f &&
                catalog.definition(0).metadata.heightsCm[0] == 314,
            "Decoded binding reaches genuine original descriptor");
    city.geometryLibraries.at("model").nodes[0].scale[0] = 1.f + 0.5e-6f;
    require(collectRetailJumpSurfaceSources(city, scene)[0].placementAbsolute[0] == 1.f,
            "Original inclusive near-unit scale comparison retains raw quaternion matrix");
    city.geometryLibraries.at("model").nodes[0].scale[0] = 1.f + 2.e-6f;
    require(collectRetailJumpSurfaceSources(city, scene)[0].placementAbsolute[0] == 1.f + 2.e-6f,
            "Outside original scale tolerance applies authored scale");
    // Generated mesh children have identity local TRS; their absolute matrices
    // inherit these distinct nonidentity SNode containers and external root.
    auto& authored = city.geometryLibraries.at("model");
    authored.nodes[0].rotation = {0, 0, .6f, .8f};
    authored.nodes[0].scale = {2, 3, 4};
    authored.nodes[1].rotation = {.125f, .25f, .375f, .875f};
    authored.nodes[1].scale = {1.5f, 2.5f, .75f};
    scene.nodes[0].rotation = {0, 0, -.6f, .8f};
    const auto transformed = collectRetailJumpSurfaceSources(city, scene);
    require(transformed.size() == 1 && transformed[0].uid == "Jump-mesh" &&
                transformed[0].currentAbsolute != transformed[0].placementAbsolute &&
                transformed[0].currentAbsolute[12] != 0 && transformed[0].currentAbsolute[1] != 0,
            "Identity-local generated children inherit distinct nonidentity container absolute TRS");
    RetailJumpSurfaceSequence transformedSequence(231);
    RetailJumpSurfaceCatalog transformedCatalog(transformedSequence);
    transformedCatalog.append(transformed[0]);
    require(transformedCatalog.definition(0).body.angle != 0,
            "Producer body yaw uses generated child's absolute matrix, not identity local rotation");
    if (emit)
        dump(transformed[0], 231, transformedCatalog.definition(0), transformedSequence.nextID());
    scene.nodes.push_back(inst);
    scene.nodes.back().parent = 0;
    bool failed = false;
    try {
        collectRetailJumpSurfaceSources(city, scene);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "Unproven nested instance hierarchy reports unsupported binding");
}
void rawSceneChildIdentity(bool emit) {
    struct SerializedScene {
        std::vector<unsigned char> bytes = std::vector<unsigned char>(156);
        uint32_t allocate(uint32_t count) {
            const auto offset = static_cast<uint32_t>(bytes.size());
            bytes.resize(offset + count);
            return offset;
        }
        void word(uint32_t at, uint32_t value) {
            for (unsigned i = 0; i < 4; ++i)
                bytes.at(at + i) = static_cast<unsigned char>(value >> (8 * i));
        }
        void scalar(uint32_t at, float value) { word(at, bits(value)); }
        uint32_t text(const std::string& value) {
            const auto offset = allocate(static_cast<uint32_t>((value.size() + 4) & ~size_t(3)));
            std::copy(value.begin(), value.end(), bytes.begin() + offset);
            return offset;
        }
        void visual(uint32_t count, uint32_t nodes) {
            const auto table = allocate(16), instance = allocate(8), reference = allocate(8);
            word(32 + 108, 1);
            word(32 + 112, table);
            word(table, text("Scene"));
            word(table + 8, count);
            word(table + 12, nodes);
            word(32 + 116, 1);
            word(32 + 120, instance);
            word(instance, 6);
            word(instance + 4, reference);
            word(reference + 4, text("#Scene"));
        }
        void node(uint32_t at, const std::string& uid, const std::string& mesh, bool external) {
            word(at, text(uid));
            word(at + 4, text("distinct display name"));
            scalar(at + 36, 1); // Raw identity quaternion.
            for (unsigned i = 0; i < 3; ++i)
                scalar(at + 40 + i * 4, i == 0 && !external ? -1.f : 1.f);
            const auto instance = allocate(8), reference = allocate(20);
            word(at + 64, 1);
            word(at + 68, instance);
            word(instance, 3);
            word(instance + 4, reference);
            if (external)
                word(reference, text("zero-model.bdae"));
            word(reference + 4, text("#" + mesh));
        }
        void mesh(uint32_t geometry, const std::string& uid) {
            word(geometry, text(uid));
            const auto record = allocate(44), stream = allocate(32), buffer = allocate(60);
            const auto offsets = allocate(4), types = allocate(4), data = allocate(48),
                       indices = allocate(12);
            word(geometry + 12, record);
            word(record, 1);
            word(record + 4, 4);
            word(record + 8, stream);
            word(record + 12, 1);
            word(record + 16, buffer);
            word(stream, 12);
            word(stream + 4, 1);
            word(stream + 8, offsets);
            word(stream + 16, types);
            word(types, 6);
            word(stream + 28, data);
            for (unsigned field : {13u, 16u, 19u, 22u, 23u})
                bytes[buffer + field] = 255;
            word(buffer + 8, 2);
            word(buffer + 24, 6);
            word(buffer + 28, indices);
            word(buffer + 32, 3u << 16);
            constexpr std::array<uint16_t, 6> order{0, 1, 2, 2, 3, 0};
            for (size_t i = 0; i < order.size(); ++i) {
                bytes[indices + i * 2] = static_cast<unsigned char>(order[i]);
                bytes[indices + i * 2 + 1] = 0;
            }
            const auto points = source().verticesCm;
            for (size_t i = 0; i < 4; ++i)
                for (size_t axis = 0; axis < 3; ++axis)
                    scalar(data + uint32_t(i * 12 + axis * 4), points[i][axis]);
        }
        void write(const std::filesystem::path& path) {
            std::copy_n("BRES", 4, bytes.begin());
            word(4, 0xfffe);
            word(8, 32);
            word(12, static_cast<uint32_t>(bytes.size()));
            word(20, 32);
            word(24, 32);
            word(28, 32);
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            require(bool(out), "Write raw generated-child scene fixture");
        }
    } library, outer;
    const auto geometries = library.allocate(32), nodes = library.allocate(160);
    library.word(32 + 76, 2);
    library.word(32 + 80, geometries);
    library.mesh(geometries, "visible-mesh");
    library.mesh(geometries + 16, "Jump-mesh");
    library.node(nodes, "Jump-container-only", "visible-mesh", false);
    library.node(nodes + 80, "ordinary-container", "Jump-mesh", false);
    for (size_t axis = 0; axis < 3; ++axis) {
        library.scalar(nodes + 12 + uint32_t(axis * 4), float(axis + 2));
        library.scalar(nodes + 80 + 12 + uint32_t(axis * 4), float(9 - axis));
    }
    library.visual(2, nodes);
    const auto instance = outer.allocate(80);
    outer.node(instance, "external-instance", "visible-mesh", true);
    for (unsigned axis = 0; axis < 3; ++axis)
        outer.scalar(instance + 12 + axis * 4, float(100 * (axis + 1)));
    outer.scalar(instance + 28, 1);
    outer.scalar(instance + 36, 0); // Exact quaternion (0,1,0,0).
    outer.visual(1, instance);
    struct Temporary {
        std::filesystem::path directory;
        bool owned = false;
        ~Temporary() {
            if (!owned)
                return;
            std::error_code ignored;
            std::filesystem::remove(directory / "zero-model.bdae", ignored);
            std::filesystem::remove(directory / "zero-root.bdae", ignored);
            std::filesystem::remove(directory, ignored);
        }
    } temporary{std::filesystem::temp_directory_path() /
                ("OpenGangstar-jump-child-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    require(std::filesystem::create_directory(temporary.directory),
            "Create raw generated-child fixture folder");
    library.write(temporary.directory / "zero-model.bdae");
    outer.write(temporary.directory / "zero-root.bdae");
    ResourceFileSystem files(temporary.directory);
    CityAssets assets;
    assets.geometryLibraries.emplace("zero-model.bdae", loadCityScene(files, "zero-model.bdae"));
    const auto scene = loadCityScene(files, "zero-root.bdae");
    const auto sources = collectRetailJumpSurfaceSources(assets, scene);
    require(sources.size() == 1 && sources[0].uid == "Jump-mesh", "Raw mesh UID qualification");
    // Independent actual original generated-child update+producer capture: all
    // off-diagonal zeros become +0 after mult34(container,identity), including M1.
    const RetailJumpMatrix expectedCurrent{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 91, 208, 293, 1};
    const RetailJumpMatrix expectedPlacement{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 98, 203, 296, 1};
    RetailJumpSurfaceSequence sequence(231);
    RetailJumpSurfaceCatalog catalog(sequence);
    catalog.append(sources[0]);
    if (emit)
        dump(sources[0], 231, catalog.definition(0), sequence.nextID());
    for (size_t i = 0; i < expectedCurrent.size(); ++i) {
        require(bits(sources[0].currentAbsolute[i]) == bits(expectedCurrent[i]),
                "Original identity-child current matrix signed-zero bits");
        require(bits(sources[0].placementAbsolute[i]) == bits(expectedPlacement[i]),
                "Original identity-child placement matrix signed-zero bits");
    }
    require(bits(catalog.definition(0).body.angle) == 0, "Original generated-child body yaw is +0, not -0");
    require(sequence.nextID() == 232, "Raw generated-child original wrapping ID transition");
    b2World world(bounds(), b2Vec2(0, 0), true);
    catalog.activate(world);
    require(world.GetProxyCount() == 1 && catalog.bodies()[0]->IsStatic() &&
                bits(catalog.bodies()[0]->GetAngle()) == 0,
            "Raw generated-child creates genuine static proxy with +0 body angle");
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    jump.setCurrentShape(*catalog.bodies()[0]->GetShapeList());
    close(jump.height(jump.state().centreCm), 236, "Raw generated-child original jump centre height");
    catalog.deactivate();
    require(world.GetProxyCount() == 0, "Raw generated-child proxy released");
}
void dump(const RetailJumpSurfaceSource& input, uint8_t before, const RetailJumpSurfaceDefinition& def,
          uint8_t after) {
    std::cout << "surface {\"identity\":" << std::quoted(input.identity) << ",\"before\":" << unsigned(before)
              << ",\"after\":" << unsigned(after) << ",\"vertices\":[";
    for (size_t i = 0; i < 4; ++i)
        for (size_t k = 0; k < 3; ++k) {
            if (i || k)
                std::cout << ',';
            std::cout << bits(input.verticesCm[i][k]);
        }
    for (auto item : {std::pair<const char*, const RetailJumpMatrix&>{"current", input.currentAbsolute},
                      {"placement", input.placementAbsolute}}) {
        std::cout << "],\"" << item.first << "\":[";
        for (size_t i = 0; i < 16; ++i) {
            if (i)
                std::cout << ',';
            std::cout << bits(item.second[i]);
        }
    }
    std::cout << "],\"body\":[" << bits(def.body.position.x) << ',' << bits(def.body.position.y) << ','
              << bits(def.body.angle) << "],\"polygon\":[";
    for (size_t i = 0; i < 4; ++i) {
        if (i)
            std::cout << ',';
        std::cout << bits(def.polygon.vertices[i].x) << ',' << bits(def.polygon.vertices[i].y);
    }
    std::cout << "],\"heights\":[";
    for (size_t i = 0; i < 4; ++i) {
        if (i)
            std::cout << ',';
        std::cout << bits(def.metadata.heightsCm[i]);
    }
    std::cout << "],\"flag\":" << unsigned(def.metadata.opaqueJumpFlag)
              << ",\"density\":" << bits(def.polygon.density)
              << ",\"friction\":" << bits(def.polygon.friction)
              << ",\"restitution\":" << bits(def.polygon.restitution) << "}\n";
}
void syntheticOracleDump() {
    for (float angle : {0.f, .125f, -.625f, 1.5707963267948966f, 3.14159265358979323846f})
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            RetailJumpSurfaceSequence sequence(uint8_t(rotation == 3 ? 255 : rotation));
            RetailJumpSurfaceCatalog catalog(sequence);
            auto input = source();
            std::rotate(input.verticesCm.begin(), input.verticesCm.begin() + rotation,
                        input.verticesCm.end());
            const float c = std::cos(angle), s = std::sin(angle);
            input.currentAbsolute = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 300, 400, 500, 1};
            input.placementAbsolute = {c, s, .1f, 0, -s, c, -.05f, 0, 0, 0, 1, 0, 1000, 2000, 700, 1};
            const auto before = sequence.nextID();
            catalog.append(input);
            dump(input, before, catalog.definition(0), sequence.nextID());
        }
    for (float pitch : {-1.f, 1.f, 1.1f}) {
        RetailJumpSurfaceSequence sequence;
        RetailJumpSurfaceCatalog catalog(sequence);
        auto input = source();
        input.currentAbsolute = {0,         0,   pitch, 0, -.5f, .8660254f, 0,   0,
                                 .8660254f, .5f, 0,     0, 300,  400,       500, 1};
        const auto before = sequence.nextID();
        catalog.append(input);
        dump(input, before, catalog.definition(0), sequence.nextID());
    }
}
void retail(const std::filesystem::path& root, bool emit) {
    ResourceFileSystem resources(root);
    auto city = loadCityAssets(resources);
    RetailJumpSurfaceSequence sequence;
    std::map<std::string, std::unique_ptr<RetailJumpSurfaceCatalog>> catalogs;
    size_t high = 0, low = 0;
    for (const auto& district : city.districts)
        for (const auto* scene : {&district.scene, &district.lowDetailScene}) {
            const auto sources = collectRetailJumpSurfaceSources(city, *scene);
            (scene == &district.scene ? high : low) += sources.size();
            std::cout << "Manifest scene " << scene->path << ": " << sources.size() << " jumps.\n";
            for (const auto& input : sources) {
                auto& catalog = catalogs[input.ownerIdentity];
                if (!catalog)
                    catalog = std::make_unique<RetailJumpSurfaceCatalog>(sequence);
                const auto before = sequence.nextID();
                require(catalog->append(input), "Supplied authored jump has original four-vertex buffer");
                if (emit)
                    dump(input, before, catalog->definition(catalog->size() - 1), sequence.nextID());
            }
        }
    // This additional supplied scene is not a low-detail path selected by the
    // current WorldDescriptor manifests. Validate it explicitly as a separate
    // resource, without claiming simultaneous production activation.
    const auto alternative = loadCityScene(resources, "santa_monica_lite.bdae");
    for (const auto& node : alternative.nodes)
        for (const auto& reference : node.geometry)
            if (!reference.sourcePath.empty() && !city.geometryLibraries.count(reference.sourcePath))
                city.geometryLibraries.emplace(reference.sourcePath,
                                               loadCityScene(resources, reference.sourcePath));
    const auto extra = collectRetailJumpSurfaceSources(city, alternative);
    for (const auto& input : extra) {
        auto& catalog = catalogs[input.ownerIdentity];
        if (!catalog)
            catalog = std::make_unique<RetailJumpSurfaceCatalog>(sequence);
        const auto before = sequence.nextID();
        require(catalog->append(input), "Additional supplied scene has original jump format");
        if (emit)
            dump(input, before, catalog->definition(catalog->size() - 1), sequence.nextID());
    }
    std::cout << "Supplied authored jumps: " << high << " manifest high-detail, " << low
              << " manifest low-detail, " << extra.size() << " additional-scene descriptors.\n";
    require(high == 11 && low == 0 && extra.size() == 4 && catalogs.size() == 15,
            "All supplied raw external jump instances accounted for");
    b2World world(bounds(), b2Vec2(0, 0), true);
    for (auto& entry : catalogs)
        entry.second->activate(world);
    require(world.GetProxyCount() == 15 && world.GetBodyCount() == 16,
            "Every supplied authored jump creates actual original-world static body/proxy");
    for (const auto& entry : catalogs)
        for (auto* body : entry.second->bodies()) {
            require(body->IsStatic() && body->GetMass() == 0,
                    "Supplied producer retains original static body");
            RetailPhysicsOwnerState owner{5, 0, 0, 0};
            RetailJumpController jump(owner);
            jump.setCurrentShape(*body->GetShapeList());
            require(std::isfinite(jump.height(jump.state().centreCm)),
                    "Every supplied authored jump reaches finite verified snapshot/plane query");
        }
    for (auto& entry : catalogs)
        entry.second->deactivate();
    require(world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Supplied producer completely releases actual world ownership");
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        const auto baseline = b2_byteCount;
        descriptorAndNativeSnapshot();
        skipsAndFirstVertex();
        lifetimeAndRange();
        actualFilter();
        const bool bindingDump = argc == 2 && std::wstring(argv[1]) == L"--binding-oracle-dump";
        decodedBinding(bindingDump);
        const bool signedDump = argc == 2 && std::wstring(argv[1]) == L"--signed-child-oracle-dump";
        rawSceneChildIdentity(signedDump);
        if (argc == 2 && std::wstring(argv[1]) == L"--oracle-dump")
            syntheticOracleDump();
        else if (argc >= 2 && !bindingDump && !signedDump)
            retail(argv[1], argc == 3 && std::wstring(argv[2]) == L"--oracle-dump");
        require(b2_byteCount == baseline,
                "Authored jump producer/probes/bodies release all native physics allocations");
        std::cout << "Original authored jump definition, scene binding, native body/filter/snapshot and "
                     "lifetime fixtures passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
