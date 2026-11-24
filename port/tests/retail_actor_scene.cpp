#include "retail_actor_scene.h"
#include "filesystem.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::uint32_t bits(float value) {
  std::uint32_t result;
  std::memcpy(&result, &value, 4);
  return result;
}
CityNode node(std::string uid, std::string scope, int parent) {
  CityNode result{};
  result.name = std::move(uid);
  result.jointName = std::move(scope);
  result.parent = parent;
  result.rotation = {0, 0, 0, 1};
  result.scale = {1, 1, 1};
  result.displayName = "display-" + result.name;
  return result;
}
CityMesh mesh(std::string uid) {
  CityMesh result{};
  result.name = std::move(uid);
  result.positions = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}};
  result.buffers.push_back({"", {0, 1, 2}});
  result.rawBoundsCm =
      std::array<std::array<float, 3>, 2>{{{-1, -2, -3}, {111, 222, 333}}};
  return result;
}
std::shared_ptr<RetailActorSceneSource> fixture() {
  auto source = std::make_shared<RetailActorSceneSource>();
  auto &scene = source->model;
  scene.path = "./actor.bdae";
  scene.meshes = {mesh("body"), mesh("shadow")};
  scene.nodes = {node("body-node", "", -1), node("Bip01-node", "Bone1", -1),
                 node("shadow-node", "", 1), node("Bip01-node", "Bone1", -1)};
  scene.nodes[1].translation = {2, 3, 4};
  scene.nodes[3].translation = {50, 60, 70};
  scene.nodes[0].geometry.push_back({"", "body", {}});
  scene.nodes[0].instances.push_back({2, 0, "", "body-skin", false});
  scene.nodes[2].geometry.push_back({"", "shadow", {}});
  scene.nodes[2].instances.push_back({3, 0, "", "shadow", false});
  scene.visualScenes.push_back({"visual-uid", "visual-display", 0, 4});
  CitySkin skin{};
  skin.meshName = "body";
  skin.controllerUid = "body-skin";
  skin.bindShape = retailSceneIdentity;
  skin.bindShape[12] = 4;
  skin.bindShape[13] = 5;
  skin.bindShape[14] = 6;
  CitySkinJoint joint{1, retailSceneIdentity, "Bone1"};
  joint.inverseBind[12] = 1;
  joint.inverseBind[13] = 2;
  joint.inverseBind[14] = 3;
  skin.joints.push_back(joint);
  source->skins.push_back(skin);
  return source;
}
struct Animator final : RetailActorSceneAnimator {
  std::vector<int> &calls;
  int id;
  std::array<float, 3> *target = nullptr;
  unsigned bind = 0, unbind = 0;
  std::function<void(RetailActorSceneNode &)> effect;
  Animator(std::vector<int> &calls, int id) : calls(calls), id(id) {}
  void onBind(RetailActorSceneNode &) override { ++bind; }
  void onUnbind(RetailActorSceneNode &) noexcept override {
    ++unbind;
    target = nullptr;
    calls.push_back(-id);
  }
  void animateNode(RetailActorSceneNode &node, std::uint32_t) override {
    calls.push_back(id);
    if (effect)
      effect(node);
  }
};
void snapshot(const RetailActorScene &scene, int phase, const char *prefix) {
  for (const auto &owned : scene.nodes()) {
    const auto &n = *owned;
    std::cout << prefix << " {\"phase\":" << phase << ",\"index\":" << n.index()
              << ",\"uid\":" << std::quoted(n.uid())
              << ",\"scope\":" << std::quoted(n.scope())
              << ",\"name\":" << std::quoted(n.name())
              << ",\"type\":" << n.type() << ",\"parent\":"
              << (n.parent() ? std::int64_t(n.parent()->index()) : -1)
              << ",\"flags\":" << n.transformFlags() << ",\"visible\":["
              << n.visible() << ',' << n.locallyVisible() << ','
              << n.parentVisible() << "],\"matrix\":[";
    for (std::size_t i = 0; i < 16; ++i) {
      if (i)
        std::cout << ',';
      std::cout << bits(n.absolute()[i]);
    }
    std::cout << "]}\n";
  }
}
void operations(RetailActorScene &scene, bool dump, const char *prefix) {
  auto &root = scene.root();
  auto *bone = root.findUid("Bip01-node");
  require(bone, "Genuine root-motion node UID");
  if (dump)
    snapshot(scene, 0, prefix);
  const auto oldRoot = root.absolute(), oldBone = bone->absolute();
  root.setPosition({10, 20, 30});
  require(root.absolute() == oldRoot && bone->absolute() == oldBone,
          "Setters leave absolute caches stale");
  if (dump)
    snapshot(scene, 1, prefix);
  root.updateAbsolutePosition(false);
  require(bone->absolute() == oldBone,
          "Nonrecursive update does not refresh descendants");
  if (dump)
    snapshot(scene, 2, prefix);
  bone->updateAbsolutePosition(
      false); // Uses its cached parent, never refreshes ancestors implicitly.
  if (dump)
    snapshot(scene, 3, prefix);
  root.onAnimate(123);
  if (dump)
    snapshot(scene, 4, prefix);
  root.setOption(1, true);
  root.setPosition({40, 50, 60});
  root.onAnimate(124);
  if (dump)
    snapshot(scene, 5, prefix);
  root.setOption(1, false);
  bone->setVisible(false);
  root.onAnimate(125);
  if (dump)
    snapshot(scene, 6, prefix);
  root.setVisible(false);
  bone->setVisible(true);
  root.onAnimate(126);
  require(!bone->visible() && bone->locallyVisible() && !bone->parentVisible(),
          "Local visible cannot bypass hidden inherited parent");
  if (dump)
    snapshot(scene, 7, prefix);
  root.setVisible(true);
  root.onAnimate(127);
  if (dump)
    snapshot(scene, 8, prefix);
  bone->setRotation({0, 0, 1, 1});
  bone->setScale({2, -3, 4});
  bone->setPosition({-0.f, 13, -0.f});
  root.onAnimate(128);
  if (dump)
    snapshot(scene, 9, prefix);
}
void synthetic(bool dump) {
  auto source = fixture();
  RetailActorScene first(source), second(source);
  require(first.nodes().size() == 8 &&
              first.root().findUid("./actor.bdae") == &first.root(),
          "Distinct root, visual, source and generated nodes");
  auto *bone = first.root().findUid("Bip01-node");
  require(bone == first.nodes()[4].get() &&
              first.root().findScope("Bone1") == bone &&
              !first.root().findUid("bip01-node") &&
              bone->name() == "display-Bip01-node",
          "UID/scope exact case and self-first ordered duplicate lookup; "
          "display is separate");
  require(first.root().findUid(std::string_view("Bip01-node\0suffix", 17)) ==
                  bone &&
              first.root().findScope(std::string_view("Bone1\0suffix", 12)) ==
                  bone,
          "Original strcmp lookup stops at the first NUL");
  auto *skinned = first.root().findMesh();
  require(skinned && skinned->uid() == "body-skin" &&
              skinned->type() == retailSceneSkinnedMeshType &&
              skinned->mesh()->geometry().name == "body",
          "Controller UID differs from underlying mesh UID");
  auto &skin = *skinned->mesh();
  require(
      skin.joints().size() == 1 && skin.joints()[0] == bone &&
          skin.jointMatrices()[0] == &bone->absolute(),
      "Skin owns actual live joint cached matrix pointers selected by scope");
  const auto initial = skin.skinMatrices()[0];
  require(initial[12] == 7 && initial[13] == 10 && initial[14] == 13 &&
              !skin.skinCacheDirty(),
          "Original joint absolute * inverse bind * bind shape");
  bone->setPosition({12, 23, 34});
  bone->updateAbsolutePosition(false);
  require(skin.skinMatrices()[0] == initial,
          "Skin cache stays stale until original mesh frame marks dirty");
  skinned->setOption(1, true);
  skinned->onAnimate(1);
  require(skin.skinCacheDirty() && skin.skinMatrices()[0][12] == 17,
          "Visible skin override marks dirty even when base option1 suppresses "
          "node frame");
  skinned->setVisible(false);
  skinned->onAnimate(2);
  require(!skin.skinCacheDirty(),
          "Invisible skin suppresses mesh dirty callback");
  require(skin.bufferByte9().size() == 1 && skin.bufferByte9()[0] == 0,
          "Mesh hide establishes owned buffer byte9 zero");
  skin.setBufferByte9(0, 7);
  skinned->setVisible(false);
  require(skin.bufferByte9()[0] == 0,
          "Repeated hide still clears buffer state");
  require(first.root().findMesh() == skinned,
          "FindAMesh does not invent a visibility filter");
  skin.setBufferByte9(0, 7);
  first.root().setVisible(false);
  require(skin.bufferByte9()[0] == 0,
          "Inherited false notification invokes the original mesh buffer clear "
          "override");
  first.root().setVisible(true);
  require(second.root().findUid("Bip01-node")->position() ==
                  source->model.nodes[1].translation &&
              &second.root() != &first.root() &&
              second.root().findMesh()->mesh() != &skin,
          "Two live instances share immutable data without pose/resource state "
          "leakage");
  RetailActorScene traced(source);
  operations(traced, dump, "synthetic");
  std::vector<int> calls;
  auto animator = std::make_shared<Animator>(calls, 1);
  std::vector<long> counts;
  {
    RetailActorScene owned(source);
    auto *target = owned.root().findUid("Bip01-node");
    animator->target = &target->animationPosition(animator);
    target->animationRotation(animator);
    target->animationScale(animator);
    require(target->boundAnimatorCount() == 1 && target->animatorCount() == 0 &&
                (target->transformFlags() & 7) == 7,
            "Bound retain deduplicates and never adds frame dispatch");
    counts.push_back(animator.use_count());
    *animator->target = {5, 6, 7};
    owned.root().onAnimate(1);
    require(calls.empty() && target->absolute()[12] == 5,
            "Raw bound target changes refresh by persistent masks");
    target->addAnimator(animator);
    target->addAnimator(animator);
    owned.root().onAnimate(2);
    counts.push_back(animator.use_count());
    require(calls == std::vector<int>{1, 1} && animator->bind == 2,
            "Ordinary duplicates retain/dispatch in order");
    target->removeAnimator(*animator);
    counts.push_back(animator.use_count());
    target->removeBoundAnimator(*animator);
    counts.push_back(animator.use_count());
    require(!animator->target && animator->unbind == 2 &&
                target->animatorCount() == 1,
            "Both removal APIs invalidate before releasing one retention");
  }
  counts.push_back(animator.use_count());
  require(animator->unbind == 3 && !animator->target,
          "Teardown invalidates remaining target before storage dies");
  require(counts == std::vector<long>{2, 4, 3, 2, 1},
          "Actual separate ordinary/bound list ownership counts");
  if (dump) {
    std::cout << "retention {\"counts\":[";
    for (std::size_t i = 0; i < counts.size(); ++i) {
      if (i)
        std::cout << ',';
      std::cout << counts[i];
    }
    std::cout << "],\"calls\":[";
    for (std::size_t i = 0; i < calls.size(); ++i) {
      if (i)
        std::cout << ',';
      std::cout << calls[i];
    }
    std::cout << "],\"bind\":" << animator->bind
              << ",\"unbind\":" << animator->unbind << "}\n";
  }
  std::vector<int> appended;
  {
    RetailActorScene owned(source);
    auto first = std::make_shared<Animator>(appended, 1);
    auto tail = std::make_shared<Animator>(appended, 2);
    bool added = false;
    first->effect = [&](RetailActorSceneNode &node) {
      if (!added) {
        added = true;
        node.addAnimator(tail);
      }
    };
    owned.root().addAnimator(first);
    owned.root().onAnimate(1);
    require(appended == std::vector<int>{1, 2},
            "Appended ordinary animator participates in the live frame");
  }
  std::vector<int> released;
  {
    RetailActorScene owned(source);
    auto animator = std::make_shared<Animator>(released, 1);
    auto *target = owned.root().findUid("Bip01-node");
    animator->target = &target->animationPosition(animator);
    animator->effect = [&](RetailActorSceneNode &n) {
      n.removeBoundAnimator(*animator);
    };
    target->addAnimator(animator);
    owned.root().onAnimate(1);
    require(target->boundAnimatorCount() == 0 && target->animatorCount() == 1 &&
                animator->target == nullptr &&
                released == std::vector<int>{1, -1},
            "Ordinary dispatch may remove independent bound retention and "
            "invalidate targets");
  }
  auto invalid = fixture();
  invalid->skins[0].joints[0].scope = "absent";
  bool rejected = false;
  try {
    RetailActorScene bad(invalid);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "Missing original joint rejects construction instead of "
                    "dereferencing NULL");
  invalid = fixture();
  invalid->model.lightUids.push_back("actual-light");
  invalid->model.nodes[0].instances.push_back(
      {4, std::nullopt, {}, "actual-light", false});
  rejected = false;
  try {
    RetailActorScene bad(invalid);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "Resolved original lights require a real runtime "
                    "implementation; missing lookup remains NULL");
}
void repeatedResources() {
  // The actual factory constructs a resource for each instance, including a
  // fresh ordinary base for every skin. A source UID identifies immutable
  // decoded data; it does not identify these mutable records.
  for (unsigned kind = 0; kind != 3; ++kind) {
    auto source = fixture();
    const bool skinned = kind != 0;
    auto repeated = node("repeated-node", "", -1);
    const std::string geometry = skinned ? "body" : "shadow";
    std::string controller = "body-skin";
    if (kind == 2) {
      auto other = source->skins.front();
      other.controllerUid = controller = "other-skin";
      source->skins.push_back(std::move(other));
    }
    repeated.geometry.push_back({"", geometry, {}});
    repeated.instances.push_back(
        {skinned ? 2u : 3u, 0, "", skinned ? controller : geometry, false});
    source->model.nodes.push_back(std::move(repeated));
    ++source->model.visualScenes.front().nodeCount;
    RetailActorScene scene(source);
    auto *first = scene.root().findUid(skinned ? "body-skin" : "shadow");
    auto *second = scene.root().findUid("repeated-node")->children().front();
    auto &a = *first->mesh();
    auto &b = *second->mesh();
    require(&a != &b && &a.baseMesh() != &b.baseMesh() &&
                &a.geometry() == &b.geometry(),
            "Repeated factory instances own distinct mutable resources/bases "
            "while sharing immutable source geometry");
    require(kind == 2 ? a.skin() != b.skin() : a.skin() == b.skin(),
            "Decoded controller identity remains separate from mutable "
            "resource identity");
    require(!a.bufferByte9()[0] && !b.bufferByte9()[0],
            "Each new buffer retains its independently unknown original byte");
    a.setBufferByte9(0, 7);
    b.setBufferByte9(0, 9);
    first->setVisible(false);
    require(a.bufferByte9()[0] == 0 && b.bufferByte9()[0] == 9 &&
                second->visible(),
            "Hiding one instance never clears a visible sibling's buffer");
    a.baseMesh().setBufferByte9(0, 11);
    b.baseMesh().setBufferByte9(0, 13);
    require(a.baseMesh().bufferByte9()[0] == 11 &&
                b.baseMesh().bufferByte9()[0] == 13,
            "Ordinary bases also retain independent mutable buffer state");
    if (skinned) {
      first->setVisible(true);
      const auto before = a.skinMatrices();
      require(b.skinMatrices() == before && !a.skinCacheDirty() &&
                  !b.skinCacheDirty() &&
                  a.joints()[0] == b.joints()[0] &&
                  a.jointMatrices()[0] == b.jointMatrices()[0] &&
                  a.skinMatrices().data() != b.skinMatrices().data(),
              "Separate skin caches may reference the same genuine live joint");
      auto *bone = a.joints()[0];
      bone->setPosition({12, 23, 34});
      bone->updateAbsolutePosition(false);
      first->onAnimate(1);
      require(a.skinCacheDirty() && !b.skinCacheDirty() &&
                  b.skinMatrices() == before && a.skinMatrices() != before,
              "One skin's frame dirties/refreshes only its own cache");
      second->onAnimate(2);
      require(b.skinCacheDirty() && !a.skinCacheDirty() &&
                  b.skinMatrices() == a.skinMatrices(),
              "The sibling refreshes independently after its own frame");
    }
  }
}
void matrixControls(bool dump) {
  constexpr float epsilon = .000001f;
  for (std::size_t component : {0u, 1u, 12u})
    for (float delta :
         {0.f, epsilon, std::nextafter(epsilon, 1.f), -epsilon,
          std::nextafter(-epsilon, -1.f), .5f * epsilon, -.5f * epsilon}) {
      auto source = fixture();
      source->skins[0].bindShape = retailSceneIdentity;
      const float value = component == 0 ? 1.f + delta : delta;
      source->skins[0].bindShape[component] = value;
      RetailActorScene scene(source);
      const auto &matrix = scene.root().findMesh()->mesh()->skinMatrices()[0];
      if (delta == 0)
        require(matrix[12] == 3 && matrix[13] == 5 && matrix[14] == 7,
                "Identity bind shape is skipped after original "
                "joint/inverse-bind product");
      if (dump) {
        std::cout << "identity {\"component\":" << component
                  << ",\"value\":" << bits(value) << ",\"matrix\":[";
        for (std::size_t i = 0; i < 16; ++i) {
          if (i)
            std::cout << ',';
          std::cout << bits(matrix[i]);
        }
        std::cout << "]}\n";
      }
    }
  for (unsigned mask : {1u, 2u, 4u, 7u}) {
    std::vector<int> events;
    RetailActorScene scene(fixture());
    auto *bone = scene.root().findUid("Bip01-node");
    auto animator = std::make_shared<Animator>(events, 1);
    if (mask & 1)
      bone->animationPosition(animator) = {7, 8, 9};
    if (mask & 2)
      bone->animationRotation(animator) = {0, 0, 1, 1};
    if (mask & 4)
      bone->animationScale(animator) = {2, 3, 4};
    bone->updateAbsolutePosition(false);
    if (mask == 4)
      require(bone->absolute()[0] == 1 && bone->absolute()[5] == 1,
              "Original raw animation scale mask4 alone retains cached basis");
    if (dump) {
      std::cout << "mask {\"value\":" << mask
                << ",\"flags\":" << bone->transformFlags() << ",\"matrix\":[";
      for (std::size_t i = 0; i < 16; ++i) {
        if (i)
          std::cout << ',';
        std::cout << bits(bone->absolute()[i]);
      }
      std::cout << "]}\n";
    }
  }
}
void retail(const char *directory) {
  ResourceFileSystem assets{std::filesystem::path(directory)};
  for (const char *model : {"./char_pthug.bdae", "./char_pthug_jail.bdae"}) {
    const auto source = loadRetailActorSceneSource(assets, model);
    require(source->model.nodes.size() == 32 && source->skins.size() == 1 &&
                source->skins[0].joints.size() == 22,
            "Supplied original Player source/joint inventory");
    RetailActorScene first(source), second(source);
    require(first.nodes().size() == 36 && source->model.lightUids.empty() &&
                first.root().findMesh()->uid() ==
                    source->skins[0].controllerUid,
            "Genuine generated Player graph/controller identity");
    auto &skin = *first.root().findMesh()->mesh();
    std::vector<RetailSceneMatrix> untouched;
    for (const auto &n : second.nodes())
      untouched.push_back(n->absolute());
    auto &secondSkin = *second.root().findMesh()->mesh();
    const auto untouchedSkin = secondSkin.skinMatrices();
    require(&first.source() == &second.source() &&
                &skin.geometry() == &secondSkin.geometry() &&
                &skin != &secondSkin,
            "Immutable geometry is shared while actual runtime mesh/cache "
            "identities differ");
    for (std::size_t i = 0; i < 22; ++i)
      require(
          skin.joints()[i] ==
                  first.root().findScope(source->skins[0].joints[i].scope) &&
              skin.jointMatrices()[i] == &skin.joints()[i]->absolute() &&
              secondSkin.joints()[i] ==
                  second.root().findScope(source->skins[0].joints[i].scope) &&
              secondSkin.jointMatrices()[i] ==
                  &secondSkin.joints()[i]->absolute() &&
              skin.jointMatrices()[i] != secondSkin.jointMatrices()[i],
          "All live original joint pointers");
    operations(first, true, model);
    for (std::size_t i = 0; i < untouched.size(); ++i)
      require(std::memcmp(untouched[i].data(),
                          second.nodes()[i]->absolute().data(), 64) == 0,
              "Every independent live instance cached matrix remains bitwise "
              "unchanged");
    require(secondSkin.skinMatrices() == untouchedSkin &&
                !secondSkin.skinCacheDirty(),
            "Independent runtime skin dirty flag/cache cannot leak across "
            "shared-source instances");
    const auto &matrices = skin.skinMatrices();
    for (std::size_t i = 0; i < matrices.size(); ++i) {
      std::cout << "skin {\"model\":" << std::quoted(model)
                << ",\"index\":" << i
                << ",\"node\":" << skin.joints()[i]->index() << ",\"matrix\":[";
      for (std::size_t j = 0; j < 16; ++j) {
        if (j)
          std::cout << ',';
        std::cout << bits(matrices[i][j]);
      }
      std::cout << "]}\n";
    }
    require(
        second.root().findUid("Bip01-node")->position() ==
            source->model.nodes[source->skins[0].joints[0].node].translation,
        "Independent actor instance retains original source pose");
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    synthetic(argc == 2 && std::string(argv[1]) == "--oracle");
    repeatedResources();
    matrixControls(argc == 2 && std::string(argv[1]) == "--oracle");
    if (argc == 3 && std::string(argv[1]) == "--assets")
      retail(argv[2]);
    std::cout << "Owned actor scene tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
