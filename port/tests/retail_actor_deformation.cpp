#include "retail_actor_deformation.h"
#include "retail_actor_graph.h"
#include "filesystem.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using Process = RetailDeformationMesh;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
  bool failed = false;
  try { action(); } catch (const std::exception&) { failed = true; }
  require(failed,"Expected explicit loaded deformation binding rejection");
}
std::uint32_t bits(float value) { std::uint32_t result; std::memcpy(&result,&value,4); return result; }
CityNode node(const char* uid, const char* scope = "") {
  CityNode result{}; result.name = uid; result.jointName = scope; result.parent = -1;
  result.rotation = {0,0,0,1}; result.scale = {1,1,1}; return result;
}
std::shared_ptr<RetailActorSceneSource> fixture(bool duplicateSkin = false) {
  auto source = std::make_shared<RetailActorSceneSource>(); auto& model = source->model;
  model.path = "pipeline.bdae"; model.materialCatalog.push_back({"material",{}});
  for (const char* uid : {"base","target0","target1","shadow"}) {
    CityMesh mesh; mesh.name = uid; mesh.floatNormals = true;
    for (unsigned i = 0; i < 16; ++i) {
      mesh.positions.push_back(uid == std::string("base") ? std::array<float,3>{float(i+1),float(i+2),float(i+3)} :
                                                            std::array<float,3>{40,50,60});
      mesh.normals.push_back({1,0,0});
    }
    mesh.buffers.push_back({"",{2,3,4,4,5,6}});
    mesh.rawBoundsCm = RetailSkinBoundingBox{{{-1,-2,-3},{4,5,6}}}; model.meshes.push_back(std::move(mesh));
  }
  model.morphControllers.push_back({"morph","base",0,{{1,.25f},{2,0}}});
  model.nodes = {node("body"),node("Bip01-node","Bone"),node("shadow-node")};
  model.nodes[0].geometry.push_back({{},"base",{{{},"material",0}}});
  model.nodes[0].instances.push_back({2,0,{},"skin",false});
  if (duplicateSkin) {
    model.nodes[0].geometry.push_back(model.nodes[0].geometry[0]);
    model.nodes[0].instances.push_back({2,1,{},"skin",false});
  }
  model.nodes[2].geometry.push_back({{},"shadow",{{{},"material",0}}});
  model.nodes[2].instances.push_back({3,0,{},"shadow",false});
  model.visualScenes.push_back({"visual","visual",0,model.nodes.size()});
  CitySkin skin{}; skin.meshName = "base"; skin.controllerUid = "skin"; skin.sourceUid = "morph";
  skin.bindShape = retailSceneIdentity; skin.joints.push_back({1,retailSceneIdentity,"Bone"});
  skin.vertices.resize(16,{{0,1}}); skin.jointBoundsCm = std::vector<RetailSkinBoundingBox>{};
  source->skins.push_back(std::move(skin)); return source;
}
std::vector<RetailActorSceneMesh*> skins(RetailActorScene& scene) {
  std::vector<RetailActorSceneMesh*> result;
  for (const auto& resource : scene.meshes()) if (resource->skin()) result.push_back(resource.get());
  return result;
}
void ownersAndGuards() {
  auto source = fixture(true);
  source->model.nodes[2].geometry[0].meshName = "base";
  source->model.nodes[2].instances[0].target = "base";
  auto loaded = RetailActorLoadedResource::create(source);
  auto a = std::make_shared<RetailActorScene>(loaded), b = std::make_shared<RetailActorScene>(loaded);
  auto aa = skins(*a), bb = skins(*b); require(aa.size() == 2 && bb.size() == 2,"All genuine repeated constructions remain fresh");
  for (std::size_t i = 0; i < aa.size(); ++i) {
    auto& am = aa[i]->baseMesh(); auto& bm = bb[i]->baseMesh();
    require(&am != &bm && am.morphRuntime() != bm.morphRuntime() && am.morphTargets().size() == 2 &&
            am.uid() == "morph" && am.baseMesh().uid() == "base","Actual immediate source/controller ownership");
    require(am.baseMesh().loadedSourceStreams() == bm.baseMesh().loadedSourceStreams() &&
            &am.sourceMaterial(0) == &am.baseMesh().sourceMaterial(0) &&
            &am.sourceMaterial(0) != &bm.sourceMaterial(0) &&
            &am.boundingBox() == &am.baseMesh().cachedBoundingBox() &&
            &am.cachedBoundingBox() == &am.baseMesh().cachedBoundingBox(),"Same-load streams, fresh materials and true morph aliases");
    for (std::size_t j = 0; j < 2; ++j)
      require(am.morphTargets()[j]->loadedSourceStreams() == bm.morphTargets()[j]->loadedSourceStreams() &&
              &am.morphTargets()[j]->geometry() == &source->model.meshes[j+1],"Ordered target identity/canonical loaded arrays");
  }
  auto foreignLoad = RetailActorLoadedResource::create(source);
  auto& base = aa[0]->baseMesh().baseMesh();
  rejects([&]{Process::source(a,base,foreignLoad->currentStreams(base.geometry()));});
  rejects([&]{Process::source(a,bb[0]->baseMesh().baseMesh(),base.loadedSourceStreams());});
  auto* shadow = a->root().findUid("shadow-node")->children()[0]->mesh();
  require(&shadow->geometry() == &base.geometry() && shadow->loadedSourceStreams() == base.loadedSourceStreams(),
          "Movable shadow can share decoded and loaded arrays without being the actual dependency resource");
  rejects([&]{Process::source(a,*shadow,shadow->loadedSourceStreams());});
  rejects([&]{RetailActorScene legacy(source);});
  {
    auto actual = [&](RetailActorSceneMesh& morph) {
      auto current = Process::source(a,morph.baseMesh(),morph.baseMesh().loadedSourceStreams());
      std::vector<Process::Ptr> targets;
      for (auto* target : morph.morphTargets()) targets.push_back(Process::source(a,*target,target->loadedSourceStreams()));
      auto reversed = targets; std::reverse(reversed.begin(),reversed.end());
      rejects([&]{Process::morph(current,a,morph,reversed,true);});
      return Process::morph(std::move(current),a,morph,std::move(targets),true);
    };
    auto first = actual(aa[0]->baseMesh()), second = actual(aa[1]->baseMesh());
    require(first->morphRuntime() == aa[0]->baseMesh().morphRuntime(),"Process uses sole Scene intrinsic state");
    rejects([&]{Process::skin(second,a,*aa[0],true);});
    auto valid = Process::skin(first,a,*aa[0],true);
    require(valid->kind() == Process::Kind::skin,"Actual same-scene immediate morph accepted");
  }
  auto pipeline = RetailActorDeformationPipeline::create(a);
  require(RetailActorDeformationPipeline::create(a) == pipeline,"One active pipeline association without a strong Scene cycle");
  rejects([&]{pipeline->process(*shadow);}); rejects([&]{pipeline->process(*bb[0]);});
  auto& morph = aa[0]->baseMesh(); auto runtime = morph.morphRuntime();
  auto& process = pipeline->process(morph); process.setMorphWeight(0,.5f);
  require(process.prepare(1,0) == 4 && runtime->baseWeight() == .5f &&
          aa[1]->baseMesh().morphRuntime()->controller().targets[0].weight == .25f,"Runtime weight/base isolated by actual resource, not UID");
  process.release(0); pipeline.reset();
  require(runtime == morph.morphRuntime() && runtime->baseWeight() == .5f,"Scene state survives process/output ownership destruction");
  pipeline = RetailActorDeformationPipeline::create(a);
  require(pipeline->process(morph).morphRuntime() == runtime && runtime->baseWeight() == .5f,"Recomposition preserves canonical state without reset");
  auto metadata = fixture(); metadata->model.morphControllers.clear(); metadata->skins[0].sourceUid = "base";
  auto legacy = std::make_shared<RetailActorScene>(metadata); auto& palette = *skins(*legacy)[0];
  auto copied = std::make_shared<RetailSkinningOutput>(); copied->positions = palette.geometry().positions; copied->normals = palette.geometry().normals;
  auto low = Process::source(legacy,palette.baseMesh(),copied);
  require(Process::skin(low,legacy,palette,true)->kind() == Process::Kind::skin,"Metadata-only explicit legacy current controls preserved");
  rejects([&]{RetailActorDeformationPipeline::create(legacy);});
  auto definitions = fixture(); definitions->model.hasSceneAnimations = true;
  require(!std::make_shared<RetailActorScene>(RetailActorLoadedResource::create(definitions))->nodes().empty(),
          "Unattached global animation definitions do not invent a scene animator");
  rejects([&]{RetailActorScene metadataOnly(definitions);});
  for (std::uint32_t kind : {0u,1u,5u,6u,7u,8u,9u,10u,11u,12u}) {
    auto unsupported = fixture(); unsupported->model.nodes[0].instances.push_back({kind,{},{},{},false});
    rejects([&]{RetailActorScene rejected(RetailActorLoadedResource::create(unsupported));});
  }
  auto external = fixture(); external->model.nodes[0].instances.push_back({4,{},"another.bdae","light",false});
  rejects([&]{RetailActorScene rejected(RetailActorLoadedResource::create(external));});
}
void lifetimes() {
  RetailActorGraph graph;
  auto source = fixture(); auto loaded = RetailActorLoadedResource::create(source);
  auto scene = std::make_shared<RetailActorScene>(loaded);
  auto* shadow = scene->root().findUid("shadow-node"); auto* mesh = shadow->children()[0]->mesh();
  std::weak_ptr<RetailActorScene> sceneWeak = scene; std::weak_ptr<RetailActorLoadedResource> loadWeak = loaded;
  std::weak_ptr<RetailSkinningOutput> streamWeak = mesh->loadedSourceStreams();
  graph.registerShadow(scene,*shadow);
  scene.reset(); loaded.reset(); source.reset();
  require(sceneWeak.expired() && loadWeak.expired() && !streamWeak.expired() && mesh->loadedSourceStreams()->positions[0][0] == 40,
          "Extracted ordinary resource carries its real physical-load alias without a hidden actor/wrapper owner");
  graph.removeChild(shadow); require(streamWeak.expired(),"Last ordinary resource releases loaded storage bank");
  loaded = RetailActorLoadedResource::create(fixture()); scene = std::make_shared<RetailActorScene>(loaded); sceneWeak = scene;
  auto pipeline = RetailActorDeformationPipeline::create(scene); auto& skin = *skins(*scene)[0];
  auto& process = pipeline->process(skin); require(process.prepare(1,0) == 4,"Actual nested transient kernel preparation");
  auto view = process.view(0); process.release(0);
  std::weak_ptr<RetailActorDeformationPipeline> pipelineWeak = pipeline;
  pipeline.reset(); scene.reset(); loaded.reset();
  require(pipelineWeak.expired() && !sceneWeak.expired() && view.positionsPresent() && view.streams().currentPositions.size() == 16,
          "Retained view owns actual allocations/scene but not process owner");
  view = {}; require(sceneWeak.expired(),"Final output view drops Scene with no reverse process cycle");
}
void streamWords(const RetailDeformationView& view, bool normal) {
  const auto data = view.streams(); const auto& stream = normal ? data.currentNormals : data.currentPositions;
  std::cout << '[';
  for (std::size_t i = view.firstVertex(); i < view.endVertex(); ++i) {
    if (i != view.firstVertex()) std::cout << ',';
    std::cout << '[' << bits(stream.at(i)[0]) << ',' << bits(stream.at(i)[1]) << ',' << bits(stream.at(i)[2]) << ']';
  }
  std::cout << ']';
}
void descriptorPhases(std::shared_ptr<const RetailActorSceneSource> source, const std::string& name, bool dump) {
  for (const char* scenario : {"disabled-first","present-then-disable","step2","mutated-targets"}) {
    // Each scenario is an explicitly separate physical load, like its original
    // counterpart. Repeated actors of one load are tested separately below.
    auto loaded = RetailActorLoadedResource::create(source);
    auto scene = std::make_shared<RetailActorScene>(loaded);
    auto list = skins(*scene);
    const auto found = std::find_if(list.begin(),list.end(),[](auto* skin){return bool(skin->baseMesh().morphRuntime());});
    require(found != list.end(),"Actual nested morph source, not first skin or filename guess");
    auto& skin = **found; auto& morph = skin.baseMesh();
    auto pipeline = RetailActorDeformationPipeline::create(scene);
    auto& sk = pipeline->process(skin); auto& mo = pipeline->process(morph);
    bool comma = false;
    if (dump) std::cout << "nested {\"model\":" << std::quoted(name) << ",\"scenario\":" << std::quoted(scenario) << ",\"phases\":[";
    const auto snapshot = [&](const char* phase, std::optional<std::uint32_t> flags = {}) {
      if (!dump) return;
      auto s = sk.view(0), m = mo.view(0);
      if (comma) std::cout << ',';
      comma = true;
      std::cout << "{\"phase\":" << std::quoted(phase) << ",\"flags\":";
      if (flags) std::cout << *flags; else std::cout << "null";
      std::cout << ",\"skinPresent\":" << sk.processBuffersPresent() << ",\"morphPresent\":" << mo.processBuffersPresent()
                << ",\"enabled\":" << sk.skinningEnabled() << ",\"dirty\":" << skin.skinCacheDirty()
                << ",\"release42\":" << sk.sourceReleasePending() << ",\"skinPositionPresent\":" << s.positionsPresent()
                << ",\"skinNormalPresent\":" << s.normalsPresent() << ",\"morphPositionPresent\":" << m.positionsPresent()
                << ",\"morphNormalPresent\":" << m.normalsPresent() << ",\"positionAlias\":"
                << (s.positionsPresent() && m.positionsPresent() && &s.streams().currentPositions == &m.streams().currentPositions)
                << ",\"normalAlias\":" << (s.normalsPresent() && m.normalsPresent() && &s.streams().currentNormals == &m.streams().currentNormals)
                << ",\"first\":" << s.firstVertex() << ",\"end\":" << s.endVertex() << ",\"baseBits\":" << bits(mo.morphBaseWeight());
      if (s.positionsPresent()) {std::cout << ",\"skinPositions\":"; streamWords(s,false);}
      if (s.normalsPresent()) {std::cout << ",\"skinNormals\":"; streamWords(s,true);}
      if (m.positionsPresent()) {std::cout << ",\"morphPositions\":"; streamWords(m,false);}
      if (m.normalsPresent()) {std::cout << ",\"morphNormals\":"; streamWords(m,true);}
      std::cout << '}';
    };
    snapshot("constructed");
    if (std::string_view(scenario) == "mutated-targets") {
      require(morph.morphTargets().size() == 2,"Explicit test mutation uses the two actual supplied targets");
      morph.morphRuntime()->setBaseWeight(99);
      mo.setMorphWeight(0,.25f); mo.setMorphWeight(1,.5f);
      const auto first = sk.view(0).firstVertex();
      auto target = morph.morphTargets()[0]->loadedSourceStreams();
      target->positions.at(first)[0] += 128.f; target->normals.at(first)[1] += .25f;
      snapshot("mutated-input");
      const auto flags = sk.prepare(1,0); snapshot("prepare-mutated1",flags);
      require(flags == 4 && mo.morphBaseWeight() == .25f,"Actual canonical base entry is reset/subtracted only by the kernel");
      sk.release(0); snapshot("release-mutated");
    } else if (std::string_view(scenario) == "disabled-first") {
      sk.setSkinningEnabled(false); snapshot("disable-before-source");
      auto flags = sk.prepare(0,0); snapshot("registration0",flags); require(flags == 16,"Deferred registration does not invent output");
      flags = sk.prepare(1,0); snapshot("draw1-disabled",flags);
      require(flags == 4 && mo.view(0).positionsPresent() && !sk.view(0).positionsPresent() &&
              !sk.processBuffersPresent() && sk.sourceReleasePending(),"Disabled saved descriptor does not auto-refresh after source allocation");
      sk.setSkinningEnabled(false); snapshot("disable-again-restores-now-current");
      require(&sk.view(0).streams().currentPositions == &mo.view(0).streams().currentPositions,
              "Repeated disable copies the now-current underlying descriptor");
      sk.release(0); snapshot("release-disabled");
      require(!sk.view(0).positionsPresent() && !mo.view(0).positionsPresent() && !sk.sourceReleasePending() && !sk.skinningEnabled(),
              "Source-first42 release clears borrowed descriptors and restores disabled state");
    } else {
      const auto step = std::string_view(scenario) == "step2" ? 2u : 1u;
      const auto flags = sk.prepare(step,0); snapshot(step == 1 ? "prepare1" : "prepare2",flags);
      require(flags == 4 && sk.processBuffersPresent() && mo.processBuffersPresent() &&
              &sk.view(0).streams().currentPositions != &mo.view(0).streams().currentPositions,"Nested preparation owns distinct actual kernel outputs");
      if (step == 1) {
        auto retained = sk.view(0); sk.setSkinningEnabled(false); snapshot("disable-present-retains-output");
        require(&retained.streams().currentPositions == &sk.view(0).streams().currentPositions,"Present output survives disable");
      }
      sk.release(0); snapshot("release-skin");
      if (step == 2) {
        require(mo.processBuffersPresent() && !sk.sourceReleasePending(),"Step2 does not fabricate the source-release obligation");
        mo.release(0); snapshot("explicit-release-morph2");
      } else require(!mo.processBuffersPresent(),"Step1 releases actual nested output");
    }
    if (dump) std::cout << "]}\n";
  }
}
void actualAssets(const std::filesystem::path& directory) {
  ResourceFileSystem files(directory); std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
    if (entry.is_regular_file() && entry.path().extension() == ".bdae") paths.push_back(entry.path());
  std::sort(paths.begin(),paths.end()); unsigned cars = 0, players = 0;
  for (const auto& path : paths) {
    const auto name = path.filename().string();
    auto loaded = RetailActorLoadedResource::load(files,"./" + std::filesystem::relative(path,directory).generic_string());
    const auto& source = loaded->source();
    const bool player = name == "char_pthug.bdae" || name == "char_pthug_jail.bdae";
    if (!player && source->model.morphControllers.empty()) continue;
    auto a = std::make_shared<RetailActorScene>(loaded), b = std::make_shared<RetailActorScene>(loaded);
    auto aa = skins(*a), bb = skins(*b); require(!aa.empty() && aa.size() == bb.size(),"All actual direct and nested skin instances constructed");
    auto pipeline = RetailActorDeformationPipeline::create(a);
    for (std::size_t i = 0; i < aa.size(); ++i) {
      auto& x = *aa[i]; auto& y = *bb[i];
      require(&x != &y && &x.baseMesh() != &y.baseMesh() && x.skinCacheDirty() && y.skinCacheDirty(),"Fresh palettes/dependencies after real finalizer");
      auto& base = x.baseMesh(); auto& other = y.baseMesh();
      if (base.morphRuntime()) {
        require(base.morphRuntime() != other.morphRuntime() && base.morphTargets().size() == other.morphTargets().size(),"Fresh intrinsic runtime, actual authored target count");
        require(&base.sourceMaterial(0) == &base.baseMesh().sourceMaterial(0) &&
                &base.cachedBoundingBox() == &base.baseMesh().cachedBoundingBox(),"Actual morph source material and box alias");
        for (std::size_t target = 0; target < base.morphTargets().size(); ++target)
          require(base.morphTargets()[target]->loadedSourceStreams() == other.morphTargets()[target]->loadedSourceStreams(),"Same-load target arrays shared across actual fresh instances");
      }
      auto& leaf = base.morphRuntime() ? base.baseMesh() : base;
      auto& otherLeaf = other.morphRuntime() ? other.baseMesh() : other;
      require(leaf.loadedSourceStreams() == otherLeaf.loadedSourceStreams() && &leaf.sourceMaterial(0) != &otherLeaf.sourceMaterial(0),
              "Shared physical source arrays never merge mutable embedded materials");
      if (!leaf.loadedSourceStreams()->normals.empty()) {
        const auto before = leaf.loadedSourceStreams()->normals.front()[0];
        leaf.loadedSourceStreams()->normals.front()[0] = -7;
        require(otherLeaf.loadedSourceStreams()->normals.front()[0] == -7,"Actual load sharing is live");
        leaf.loadedSourceStreams()->normals.front()[0] = before;
      }
      auto& process = pipeline->process(x);
      for (std::size_t slot = 0; slot < process.bufferCount(); ++slot) {
        require(process.prepare(1,slot) == 4 && process.view(slot).positionsPresent(),"Actual complete skin/morph kernel preparation");
        process.release(slot);
      }
    }
    if (player) ++players;
    if (!source->model.morphControllers.empty()) {descriptorPhases(source,name,true); ++cars;}
    std::cout << name << ": actual constructed resources/source aliases/palettes and all used buffers passed\n";
  }
  require(cars == 21 && players == 2,"Both Players and every supplied nested default vehicle accounted for");
}
}
int main(int argc, char** argv) {
  try {
    ownersAndGuards(); lifetimes(); descriptorPhases(fixture(),"synthetic",false);
    if (argc == 3 && std::string(argv[1]) == "--assets") actualAssets(argv[2]);
    std::cout << "Actual loaded actor deformation ownership and identity checks passed.\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
