#include "retail_actor_material.h"
#include "filesystem.h"
#include "retail_actor_graph.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Invalid/unsupported material binding was accepted");
}
CityMaterialSource material(std::string uid, uint32_t color) {
    CityMaterial value;
    value.state.setColor(color, 0);
    return {std::move(uid), value};
}
std::shared_ptr<RetailActorSceneSource> fixture() {
    auto result = std::make_shared<RetailActorSceneSource>();
    auto &m = result->model;
    m.path = "./material.bdae";
    m.materialCatalog = {material("same", 0x11223344), material("same", 0x55667788),
                         material("SAME", 0x99aabbcc), material("shadow", 0xddeeff00)};
    for (const char *name : {"body", "shadow-mesh"}) {
        CityMesh mesh;
        mesh.name = name;
        mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        mesh.buffers.push_back({"unused-symbol", {0, 1, 2}});
        if (mesh.name == "body")
            mesh.buffers.push_back(mesh.buffers.front());
        mesh.rawBoundsCm = RetailSkinBoundingBox{{{-1, -2, -3}, {1, 2, 3}}};
        m.meshes.push_back(mesh);
    }
    auto node = [](std::string name, std::string geometry,
                   std::vector<CityGeometryRef::MaterialRef> bindings) {
        CityNode n{};
        n.name = std::move(name);
        n.parent = -1;
        n.rotation = {0, 0, 0, 1};
        n.scale = {1, 1, 1};
        n.geometry.push_back({"", geometry, std::move(bindings)});
        n.instances.push_back({3, 0, "", geometry, false});
        return n;
    };
    m.nodes.push_back(node("body-node", "body", {{"", "wrong-URL-name", 1}, {"", "", 3}}));
    m.nodes.push_back(node("shadow-node", "shadow-mesh", {{"", "", 3}}));
    m.nodes.push_back(node("alias", "body", {{"", "", 0}, {"", "", 3}}));
    m.nodes.push_back(node("case", "shadow-mesh", {{"", "", 2}}));
    m.visualScenes.push_back({"visual", "visual", 0, m.nodes.size()});
    return result;
}
void controls() {
    auto source = fixture();
    auto actor = std::make_shared<RetailActorScene>(source);
    RetailActorScene other(source);
    auto *body = actor->root().findUid("body-node")->children().front();
    auto *shadow = actor->root().findUid("shadow-node");
    auto *shadowMesh = shadow->children().front();
    auto *alias = actor->root().findUid("alias")->children().front();
    auto *caseNode = actor->root().findUid("case")->children().front();
    require(actor->materialLibrary().size() == 3 && body->materialCount() == 2 &&
                actor->root().materialCount() == 0,
            "Actual root library/node material count");
    require(body->materialOwner(0) == alias->materialOwner(0) &&
                body->materialOwner(0)->sourceIndex() == 1 &&
                body->materialOwner(0)->state().diffuse == 0x55667788 &&
                body->materialOwner(0) != caseNode->materialOwner(0),
            "Indexed source selection precedes first exact UID lookup, preserving case");
    require(body->materialOwner(0) !=
                other.root().findUid("body-node")->children().front()->materialOwner(0),
            "Separate actors must own separate mutable materials");
    require(&body->materialOwner(0)->state() == &body->materialOwner(0)->current().state,
            "Renderer and mutation share one state");
    const auto shadowBefore = shadowMesh->materialOwner(0)->state().diffuse;
    setRetailActorColor(*body, 0x71563412, 0);
    setRetailActorAlpha(*body, 0x155, 0);
    require(body->materialOwner(0)->state().diffuse == 0x55563412 &&
                shadowMesh->materialOwner(0)->state().diffuse == shadowBefore,
            "Only recognized mesh material0 changes and alpha narrows to unsigned char");
    setRetailActorMaterialFlag(actor->root(), 16, true);
    require((body->materialOwner(0)->state().flags & 16) &&
                (shadowMesh->materialOwner(0)->state().flags & 16) &&
                body->mesh()->sourceMaterial(0).flags == 0x387,
            "Every own material/actual child changes, independently of source-buffer material");
    rejects([&] { actor->root().materialOwner(0); });
    auto bad = fixture();
    bad->model.nodes[0].geometry[0].materials[0].sourceIndex.reset();
    rejects([&] { RetailActorScene invalid(bad); });
    bad = fixture();
    bad->model.nodes[0].geometry[0].materials.pop_back();
    rejects([&] { RetailActorScene invalid(bad); });
    bad = fixture();
    bad->model.nodes[0].geometry[0].materials[0].sourceIndex = 99;
    rejects([&] { RetailActorScene invalid(bad); });
    bad = fixture();
    bad->model.nodes[0].geometry[0].materials[0].sourcePath = "external.bdae";
    rejects([&] { RetailActorScene invalid(bad); });
    bad = fixture();
    bad->model.nodes[0].geometry[0].materials.push_back({"", "", 0});
    rejects([&] { RetailActorScene invalid(bad); });
    bad = fixture();
    bad->model.materialCatalog.clear();
    rejects([&] { RetailActorScene invalid(bad); });
    rejects([&] { body->mesh()->sourceMaterial(2); });
    auto skinSource = fixture();
    CitySkin skin{};
    skin.meshName = "body";
    skin.controllerUid = "body-skin";
    skin.sourceUid = "body";
    skin.bindShape = retailSceneIdentity;
    skin.jointBoundsCm = std::vector<RetailSkinBoundingBox>{};
    skinSource->skins.push_back(skin);
    auto skinned = skinSource->model.nodes.front();
    skinned.name = "skin";
    skinned.instances[0].kind = 2;
    skinned.instances[0].target = "body-skin";
    skinned.geometry[0].materials[0].sourcePath = "ignored-controller-field.bdae";
    skinSource->model.nodes.push_back(skinned);
    ++skinSource->model.visualScenes.front().nodeCount;
    RetailActorScene skinActor(skinSource);
    auto &skinNode = *skinActor.root().findUid("skin")->children().front();
    require(
        &skinNode.mesh()->sourceMaterial(0) == &skinNode.mesh()->baseMesh().sourceMaterial(0) &&
            &skinNode.mesh()->sourceMaterial(0) != &skinActor.root()
                                                        .findUid("body-node")
                                                        ->children()
                                                        .front()
                                                        ->mesh()
                                                        ->sourceMaterial(0),
        "Skin proxies borrow actual fresh base-buffer material, independently of other resources");
    require(skinNode.materialOwner(0)->sourceIndex() == 1,
            "Controller binding uses local source index and ignores unused path");
    auto signSource = fixture();
    signSource->model.path = "./sign.bdae";
    RetailActorScene signActor(signSource);
    auto &signNode = *signActor.root().findUid("body-node")->children().front();
    const auto signColor = signNode.materialOwner(0)->state().diffuse;
    require(signNode.type() != retailSceneMeshType && signNode.materialCount() == 2,
            "Other ordinary runtime type still has actual materials");
    setRetailActorColor(signNode, 0x01020304, 0);
    setRetailActorAlpha(signNode, 0xaa, 0);
    require(signNode.materialOwner(0)->state().diffuse == signColor,
            "Color/alpha recognize exact tags, not materialCount");
    setRetailActorMaterialFlag(signNode, 16, true);
    require(signNode.materialOwner(0)->state().flags & 16,
            "Flags use actual materialCount regardless of runtime tag");
    // Only real nodes/material refs move; the detached owner never retains actor.
    RetailActorGraph graph;
    graph.attachActor(actor);
    graph.registerShadow(*actor, *shadow);
    std::weak_ptr<RetailActorScene> weakActor = actor;
    std::weak_ptr<RetailActorMaterial> weakMaterial = shadowMesh->materialOwner(0);
    auto *raw = actor.get();
    actor.reset();
    graph.releaseActor(*raw, shadow, 17);
    require(weakActor.expired() && !weakMaterial.expired(),
            "Shadow material independently survives dead actor");
    setRetailActorColor(*shadow, 0xabcdef01, 0);
    require(shadowMesh->materialOwner(0)->state().diffuse == 0xabcdef01,
            "Live moved material mutation");
    graph.removeChild(shadow);
    require(weakMaterial.expired(), "Final actual shadow removal releases material");
}
void writeMaterial(const char *model, unsigned phase, const char *role,
                   const RetailActorSceneNode &node, bool dump) {
    if (!dump)
        return;
    const auto &owner = node.materialOwner(0);
    const auto &v = owner->current();
    const auto &m = v.state;
    std::cout << "actor-material " << std::quoted(model) << ' ' << phase << ' ' << role << ' '
              << std::quoted(owner->uid()) << ' ' << owner->sourceIndex() << ' ' << uint32_t(v.mode)
              << ' ' << m.vertexColorControl << ' ' << m.flags << ' ' << m.ambient << ' '
              << m.emissive << ' ' << m.specular << ' ' << m.diffuse << ' ' << m.dirtyFlags << ' '
              << owner.use_count() << '\n';
}
void retail(const std::filesystem::path &directory, bool dump) {
    ResourceFileSystem files(directory);
    for (const char *name : {"char_pthug.bdae", "char_pthug_jail.bdae"}) {
        auto source =
            std::make_shared<RetailActorSceneSource>(*loadRetailActorSceneSource(files, name));
        const auto shadowSource =
            std::find_if(source->model.nodes.begin(), source->model.nodes.end(),
                         [](const auto &n) { return n.name == "shadow-node"; });
        require(shadowSource != source->model.nodes.end(), "Real authored shadow source");
        auto duplicate = *shadowSource;
        duplicate.name = "duplicate-shadow";
        duplicate.parent = -1;
        source->model.nodes.push_back(duplicate);
        ++source->model.visualScenes.back().nodeCount;
        auto actor = std::make_shared<RetailActorScene>(source);
        RetailActorScene clone(source);
        auto *body = actor->root().findMesh();
        auto *shadow = actor->root().findUid("shadow-node");
        auto *duplicateNode = actor->root().findUid("duplicate-shadow");
        auto *shadowMesh = shadow->children().front();
        auto *cloneBody = clone.root().findMesh();
        auto *cloneShadow = clone.root().findUid("shadow-node")->children().front();
        require(body->materialOwner(0)->sourceIndex() == 0 &&
                    shadowMesh->materialOwner(0)->sourceIndex() == 1 &&
                    shadowMesh->materialOwner(0) ==
                        duplicateNode->children().front()->materialOwner(0) &&
                    body->materialOwner(0) != cloneBody->materialOwner(0),
                "Real indexed bindings, source sharing and root-local aliases");
        const auto emit = [&](unsigned phase, bool alive = true) {
            if (alive)
                writeMaterial(name, phase, "body", *body, dump);
            writeMaterial(name, phase, "shadow", *shadowMesh, dump);
            writeMaterial(name, phase, "clone-body", *cloneBody, dump);
            writeMaterial(name, phase, "clone-shadow", *cloneShadow, dump);
        };
        const auto originalClone = cloneBody->materialOwner(0)->state().diffuse;
        const auto embeddedBefore = shadowMesh->mesh()->sourceMaterial(0);
        require(&body->mesh()->sourceMaterial(0) == &body->mesh()->baseMesh().sourceMaterial(0),
                "Real skin proxy/base source material identity");
        RetailActorGraph graph;
        graph.attachActor(actor);
        emit(0);
        setRetailActorColor(*shadow, 0x71563412, 0);
        graph.registerShadow(*actor, *shadow);
        emit(1);
        setRetailActorColor(actor->root(), 0x99887766, 0);
        emit(2);
        require(shadowMesh->materialOwner(0)->state().diffuse == 0x99887766,
                "Detached shadow still shares material with actual remaining alias");
        graph.registerShadow(*actor, *duplicateNode);
        graph.removeChild(duplicateNode);
        setRetailActorColor(actor->root(), 0x01020304, 0);
        setRetailActorAlpha(*shadow, 0x55, 0);
        setRetailActorMaterialFlag(*shadow, 16, true);
        emit(3);
        require(
            shadowMesh->materialOwner(0)->state().diffuse == 0x55887766 &&
                cloneBody->materialOwner(0)->state().diffuse == originalClone,
            "Real traversal no longer reaches shadow after alias removal; other actor unchanged");
        const auto &embedded = shadowMesh->mesh()->sourceMaterial(0);
        require(embedded.vertexColorControl == embeddedBefore.vertexColorControl &&
                    embedded.flags == embeddedBefore.flags &&
                    embedded.diffuse == embeddedBefore.diffuse &&
                    embedded.ambient == embeddedBefore.ambient &&
                    embedded.emissive == embeddedBefore.emissive &&
                    embedded.specular == embeddedBefore.specular &&
                    embedded.dirtyFlags == embeddedBefore.dirtyFlags,
                "Prepared node mutations never change real embedded source buffer");
        if (dump)
            std::cout << "embedded " << std::quoted(name) << ' ' << embedded.vertexColorControl
                      << ' ' << embedded.flags << ' ' << embedded.ambient << ' '
                      << embedded.emissive << ' ' << embedded.specular << ' ' << embedded.diffuse
                      << ' ' << embedded.dirtyFlags << '\n';
        std::weak_ptr<RetailActorScene> weakActor = actor;
        std::weak_ptr<RetailActorMaterial> weakMaterial = shadowMesh->materialOwner(0);
        auto *raw = actor.get();
        actor.reset();
        graph.releaseActor(*raw, shadow, 17);
        require(weakActor.expired() && !weakMaterial.expired(),
                "Real shadow keeps material but no dead actor");
        setRetailActorColor(*shadow, 0xabcdef01, 0);
        emit(4, false);
        graph.removeChild(shadow);
        require(weakMaterial.expired(), "Final real material release after shadow drop");
        if (dump)
            std::cout << "final " << std::quoted(name) << " 1 1\n";
    }
}
void callerControls(const std::filesystem::path &directory, bool dump) {
    ResourceFileSystem files(directory);
    for (const char *name : {"char_pthug.bdae", "char_pthug_jail.bdae"}) {
        RetailActorScene scene(loadRetailActorSceneSource(files, name));
        auto &root = scene.root();
        auto *body = root.findMesh();
        auto *shadow = root.findUid("shadow-node");
        auto *shadowMesh = shadow->children().front();
        for (uint32_t flag : {2, 4, 16})
            setRetailActorMaterialFlag(root, flag, true);
        setRetailActorColor(root, 0xff000000, 3);
        for (uint32_t selector : {0, 1, 2})
            setRetailActorColor(root, 0xffffffff, selector);
        writeMaterial(name, 5, "load-body", *body, dump);
        writeMaterial(name, 5, "load-shadow", *shadowMesh, dump);
        setRetailActorColor(*shadow, 0xff000000, 3);
        for (uint32_t selector : {0, 1, 2})
            setRetailActorColor(*shadow, 0xffffffff, selector);
        setRetailActorMaterialFlag(*shadow, 1024, false);
        setRetailActorMaterialFlag(*shadow, 512, false);
        setRetailActorMaterialFlag(*shadow, 4, true);
        setRetailActorMaterialFlag(*shadow, 16, true);
        writeMaterial(name, 6, "bind-body", *body, dump);
        writeMaterial(name, 6, "bind-shadow", *shadowMesh, dump);
        require(body->mesh()->sourceMaterial(0).emissive == 0 &&
                    shadowMesh->mesh()->sourceMaterial(0).flags == 0x387,
                "Actual caller scalar sequences keep source-buffer state separate");
    }
}
struct BinaryFixture {
    std::vector<unsigned char> bytes = std::vector<unsigned char>(156);
    uint32_t alloc(size_t count) {
        const auto at = static_cast<uint32_t>(bytes.size());
        bytes.resize(at + count);
        return at;
    }
    void word(uint32_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.at(at + i) = static_cast<unsigned char>(value >> (i * 8));
    }
    void scalar(uint32_t at, float value) {
        uint32_t raw;
        std::memcpy(&raw, &value, 4);
        word(at, raw);
    }
    uint32_t text(const std::string &value) {
        const auto at = alloc((value.size() + 4) & ~size_t(3));
        std::copy(value.begin(), value.end(), bytes.begin() + at);
        return at;
    }
};
uint32_t rawWord(const CityScene &scene, size_t at) {
    require(at + 4 <= scene.bytes.size(), "Independent raw expected-word bounds");
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= uint32_t(scene.bytes.at(at + i)) << (i * 8);
    return value;
}
std::string rawText(const CityScene &scene, uint32_t at) {
    require(at < scene.bytes.size(), "Independent raw expected-string bounds");
    std::string value;
    for (; at < scene.bytes.size() && scene.bytes[at]; ++at)
        value.push_back(static_cast<char>(scene.bytes[at]));
    require(at < scene.bytes.size(), "Independent raw expected-string terminator");
    return value;
}
void readerFixture() {
    struct Temporary {
        std::filesystem::path path;
        bool owned = false;
        ~Temporary() {
            if (owned) {
                std::error_code error;
                std::filesystem::remove(path / "materials.bdae", error);
                std::filesystem::remove(path, error);
            }
        }
    } temp{std::filesystem::temp_directory_path() /
           ("OpenGangstar-material-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    temp.owned = std::filesystem::create_directory(temp.path);
    require(temp.owned, "Create original-layout material fixture");
    BinaryFixture f;
    const auto effects = f.alloc(3 * 88), materials = f.alloc(3 * 64);
    f.word(32 + 60, 3);
    f.word(32 + 64, effects);
    f.word(32 + 68, 3);
    f.word(32 + 72, materials);
    const std::array<std::string, 3> names{"same", "SAME", "same"};
    const std::array<uint32_t, 3> colors{0xff112233, 0xff445566, 0xff778899};
    for (uint32_t i = 0; i < 3; ++i) {
        const auto e = effects + i * 88, m = materials + i * 64, c = f.alloc(4);
        f.word(c, colors[i]);
        f.word(e + 12, c);
        f.word(e + 20, c);
        f.word(e + 28, c);
        f.scalar(e + 56, 1.f);
        f.word(m, f.text(names[i]));
        f.word(m + 24, i);
        f.word(m + 36, UINT32_MAX);
        f.word(m + 40, UINT32_MAX);
    }
    std::copy_n("BRES", 4, f.bytes.begin());
    f.word(4, 0xfffe);
    f.word(8, 32);
    f.word(12, static_cast<uint32_t>(f.bytes.size()));
    f.word(20, 32);
    f.word(24, 32);
    f.word(28, 32);
    {
        std::ofstream file(temp.path / "materials.bdae", std::ios::binary);
        file.write(reinterpret_cast<const char *>(f.bytes.data()),
                   static_cast<std::streamsize>(f.bytes.size()));
        require(bool(file), "Write original-layout material fixture");
    }
    ResourceFileSystem files(temp.path);
    const auto decoded = loadCityScene(files, "materials.bdae");
    require(decoded.materialCatalog.size() == 3 && decoded.materials.size() == 2,
            "Ordered source records must not collapse into name map");
    for (size_t i = 0; i < 3; ++i)
        require(decoded.materialCatalog[i].uid == names[i] &&
                    decoded.materialCatalog[i].material.state.diffuse == colors[i],
                "Exact source-index identity and prepared value");
    require(decoded.materials.at("same").state.diffuse == colors[0],
            "Legacy renderer map retains first duplicate UID");
}
void catalog(const std::filesystem::path &directory, bool dump) {
    ResourceFileSystem files(directory);
    size_t libraries = 0, records = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".bdae")
            continue;
        const auto path = std::filesystem::relative(entry.path(), directory).generic_string();
        const auto scene = loadCityScene(files, path);
        const auto count = rawWord(scene, scene.rootOffset + 68),
                   table = rawWord(scene, scene.rootOffset + 72);
        require(scene.materialCatalog.size() == count,
                "Raw source material count differs from ordered decoder catalogue");
        for (uint32_t i = 0; i < count; ++i) {
            const auto uid = rawText(scene, rawWord(scene, table + i * 64));
            const auto &m = scene.materialCatalog[i];
            require(m.uid == uid, "Raw indexed UID/order differs from catalogue");
            if (dump)
                std::cout << "catalog " << std::quoted(path) << ' ' << i << ' ' << std::quoted(uid)
                          << ' ' << uint32_t(m.material.mode) << ' '
                          << m.material.state.vertexColorControl << ' ' << m.material.state.flags
                          << ' ' << m.material.state.ambient << ' ' << m.material.state.emissive
                          << ' ' << m.material.state.specular << ' ' << m.material.state.diffuse
                          << ' ' << m.material.state.dirtyFlags << '\n';
            ++records;
        }
        ++libraries;
    }
    require(libraries != 0, "Retail material enumeration found no original libraries");
    std::cout << "Raw indexed material catalogues: " << libraries << " libraries, " << records
              << " records.\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        controls();
        readerFixture();
        if (argc > 1) {
            const bool dump = argc > 2 && std::string(argv[2]) == "--dump";
            retail(argv[1], dump);
            callerControls(argv[1], dump);
            catalog(argv[1], dump);
        }
        std::cout << "Actual actor material ownership checks passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
