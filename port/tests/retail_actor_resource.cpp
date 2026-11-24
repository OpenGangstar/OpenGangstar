#include "retail_actor_resource.h"
#include "filesystem.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  bool rejected = false;
  try {
    action();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Unavailable/foreign source geometry was accepted.");
}
bool sameBits(const RetailVertexStream &a, const RetailVertexStream &b) {
  return a.size() == b.size() &&
         (a.empty() ||
          std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
}
std::shared_ptr<const RetailActorSceneSource> fixture() {
  auto result = std::make_shared<RetailActorSceneSource>();
  CityMesh first;
  first.name = "same";
  first.positions = {{-0.f, 1, 2}, {3, 4, 5}};
  const std::uint32_t payload = 0x7fc01234;
  std::memcpy(&first.positions[1][2], &payload, sizeof payload);
  first.normals = {{1, -0.f, 0}, {0, 1, 0}};
  auto second = first;
  second.floatNormals = false;
  result->model.meshes = {
      first, second}; // Equal names/values are distinct source records.
  return result;
}
void controls() {
  rejects([] { RetailActorLoadedResource::create({}); });
  auto immutable = fixture();
  auto load = RetailActorLoadedResource::create(immutable);
  auto sameLoad = load;
  auto separateLoad = RetailActorLoadedResource::create(immutable);
  const auto &first = immutable->model.meshes[0];
  const auto &second = immutable->model.meshes[1];
  auto a = load->currentStreams(first), b = sameLoad->currentStreams(first);
  auto otherRecord = load->currentStreams(second);
  auto distinctLoad = separateLoad->currentStreams(first);
  require(
      a.get() == b.get() && a.get() != distinctLoad.get() &&
          a.get() != otherRecord.get() && load->source() == immutable &&
          separateLoad->source() == immutable,
      "Same explicit load shares; separate loads and source records do not.");
  require(
      sameBits(a->positions, first.positions) &&
          sameBits(a->normals, first.normals) && !second.floatNormals,
      "Initial streams and authoritative source normal format are preserved.");
  a->positions[0][0] = 123;
  a->normals[1][0] = -7;
  require(b->positions[0][0] == 123 && b->normals[1][0] == -7 &&
              sameBits(distinctLoad->positions, first.positions) &&
              sameBits(distinctLoad->normals, first.normals) &&
              sameBits(otherRecord->positions, second.positions),
          "Source mutations share only through actual loaded identity.");
  auto foreign = fixture();
  auto copy = first;
  rejects([&] { load->currentStreams(foreign->model.meshes[0]); });
  rejects([&] { load->currentStreams(copy); });
  auto empty = RetailActorLoadedResource::create(
      std::make_shared<RetailActorSceneSource>());
  rejects([&] { empty->currentStreams(first); });

  // Keep only one output alias, proving it retains the fixed bank and source,
  // but not the separately allocated loaded-resource wrapper.
  std::weak_ptr<const RetailActorSceneSource> sourceLife = immutable;
  std::weak_ptr<RetailActorLoadedResource> wrapperLife = load;
  immutable.reset();
  separateLoad.reset();
  distinctLoad.reset();
  otherRecord.reset();
  sameLoad.reset();
  load.reset();
  b.reset();
  require(wrapperLife.expired() && !sourceLife.expired() &&
              a->positions[0][0] == 123,
          "An output alias retains source/bank without retaining the wrapper.");
  a.reset();
  require(sourceLife.expired(),
          "The final stream alias releases source and bank.");
}
void catalogue(const std::filesystem::path &directory) {
  ResourceFileSystem files(directory);
  std::size_t libraries = 0, geometries = 0, vertices = 0, players = 0,
              vehicles = 0;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".bdae")
      continue;
    const auto path =
        std::filesystem::relative(entry.path(), directory).generic_string();
    auto loaded = RetailActorLoadedResource::load(files, path);
    const auto &source = *loaded->source();
    const auto expected = loadCityScene(files, path);
    require(source.model.meshes.size() == expected.meshes.size(),
            "Loaded-resource geometry catalogue differs from decoder.");
    for (std::size_t index = 0; index < source.model.meshes.size(); ++index) {
      const auto &mesh = source.model.meshes[index];
      const auto &decoded = expected.meshes[index];
      auto current = loaded->currentStreams(mesh);
      require(mesh.name == decoded.name &&
                  mesh.floatNormals == decoded.floatNormals &&
                  sameBits(current->positions, decoded.positions) &&
                  sameBits(current->normals, decoded.normals),
              "Initial loaded stream bits differ from authoritative decoder.");
      require(loaded->currentStreams(mesh).get() == current.get(),
              "Repeated target/actor access changed a loaded source stream "
              "identity.");
      vertices += mesh.positions.size();
      ++geometries;
    }
    if (entry.path().filename() == "char_pthug.bdae" ||
        entry.path().filename() == "char_pthug_jail.bdae") {
      require(!source.skins.empty(),
              "Player skin metadata was lost by loaded resource.");
      ++players;
    }
    if (!source.model.morphControllers.empty()) {
      require(!source.skins.empty(), "Vehicle nested skin metadata was lost.");
      ++vehicles;
    }
    ++libraries;
  }
  require(
      libraries == 359 && geometries == 1877 && players == 2 && vehicles == 21,
      "Supplied decoded library/geometry/Player/vehicle inventory differs.");
  std::cout << libraries << " libraries, " << geometries
            << " source geometries, " << vertices
            << " vertices, both Players and " << vehicles
            << " nested vehicle catalogues passed.\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    controls();
    if (argc > 1)
      catalogue(argv[1]);
    std::cout << "Canonical actor loaded-resource checks passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
