#include "city_assets.h"
#include "city_particles.h"
#include "filesystem.h"
#include "texture.h"
#include "vorbis_audio.h"
#include "retail_radio.h"
#include "retail_audio.h"
#include "retail_text.h"
#include "retail_constants.h"
#include "retail_sprite.h"
#include "retail_minimap.h"
#include "retail_mission.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {
void auditPrimaryColorLanes(const CityScene& scene)
{
    // Independent raw source comparison: the original color0 GL array consumes
    // literal byte0/1/2/3; the unused secondary decode keeps its previous lanes.
    const auto& bytes = scene.bytes;
    const auto word = [&](size_t offset) {
        if (offset > bytes.size() || bytes.size() - offset < 4)
            throw std::runtime_error("Short raw primary-color audit record.");
        return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 |
               uint32_t(bytes[offset + 2]) << 16 | uint32_t(bytes[offset + 3]) << 24;
    };
    const auto name = [&](uint32_t offset) {
        std::string value;
        while (bytes.at(offset)) value.push_back(char(bytes.at(offset++)));
        return value;
    };
    const auto table = word(scene.rootOffset + 80), count = word(scene.rootOffset + 76);
    for (uint32_t index = 0; index < count; ++index) {
        const auto geometry = table + index * 16;
        if (word(geometry + 8) != 0) continue;
        const auto found = std::find_if(scene.meshes.begin(), scene.meshes.end(), [&](const CityMesh& mesh) {
            return mesh.name == name(word(geometry));
        });
        if (found == scene.meshes.end()) throw std::runtime_error("Missing raw color-audit geometry.");
        const auto mesh = word(geometry + 12), stream = word(mesh + 8), buffer = word(mesh + 16);
        const auto data = word(stream + 28), stride = word(stream), offsets = word(stream + 8);
        for (const bool primary : {true, false}) {
            const auto component = bytes.at(buffer + (primary ? 22 : 23));
            const auto& colors = primary ? found->colors : found->secondaryColors;
            if (component == 255) {
                if (!colors.empty()) throw std::runtime_error("Unauthored primary/secondary colors were synthesized.");
                continue;
            }
            if (colors.size() != word(mesh + 4)) throw std::runtime_error("Raw color vertex count changed.");
            for (size_t vertex = 0; vertex < colors.size(); ++vertex) {
                const size_t source = data + vertex * stride + word(offsets + component * 4);
                const std::array<float, 4> expected{bytes.at(source + (primary ? 0 : 2)) / 255.f,
                    bytes.at(source + 1) / 255.f, bytes.at(source + (primary ? 2 : 0)) / 255.f,
                    bytes.at(source + 3) / 255.f};
                if (colors[vertex] != expected) throw std::runtime_error("Raw primary RGBA or retained secondary lanes differ.");
            }
        }
    }
}

struct SceneIndex {
    std::map<std::string, std::vector<std::string>> meshes;
    std::map<std::string, std::vector<std::string>> materials;
    std::set<std::string> materialTextures;
    std::vector<CityGeometryRef> instances;
    std::vector<std::string> images;
};

std::string resourceKey(const std::string& path)
{
    auto key = std::filesystem::path(path).lexically_normal().generic_string();
    // ResourceFileSystem opens files on case-insensitive Windows volumes.
    for (auto& c : key)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return key;
}

SceneIndex indexScene(const CityScene& scene)
{
    SceneIndex index;
    for (const auto& mesh : scene.meshes) {
        auto& buffers = index.meshes[mesh.name];
        for (const auto& buffer : mesh.buffers) buffers.push_back(buffer.material);
    }
    for (const auto& [name, material] : scene.materials) {
        auto& slots = index.materials[name];
        slots.push_back(resourceKey(material.texture));
        for (const auto& texture : material.additionalTextures)
            slots.push_back(resourceKey(texture));
        if (!material.texture.empty()) index.materialTextures.insert(resourceKey(material.texture));
        for (const auto& texture : material.additionalTextures)
            if (!texture.empty()) index.materialTextures.insert(resourceKey(texture));
    }
    for (const auto& node : scene.nodes)
        index.instances.insert(index.instances.end(), node.geometry.begin(), node.geometry.end());
    index.images = scene.images;
    return index;
}

bool auditReferences(const std::map<std::string, SceneIndex>& catalog,
                     const std::set<std::string>& decodedTextures)
{
    size_t checked = 0, missing = 0;
    size_t images = 0, missingImages = 0;
    size_t materialTextures = 0, missingMaterialTextures = 0;
    size_t boundTextures = 0, missingBindings = 0;
    size_t secondaryBindings = 0, missingSecondaryBindings = 0;
    std::set<std::string> missingBoundTextures;
    std::set<std::string> missingTableTextures;
    for (const auto& [path, scene] : catalog) {
        const auto check = [&](const std::string& source, const std::string& name, auto member) {
            if (source.empty()) return;
            ++checked;
            const auto library = catalog.find(resourceKey(source));
            if (library != catalog.end() && (library->second.*member).count(name)) return;
            ++missing;
            std::cerr << "Reference: " << path << " -> " << source << '#' << name << '\n';
        };
        std::map<std::string, size_t> sceneMissingBindings;
        for (const auto& instance : scene.instances) {
            check(instance.sourcePath, instance.meshName, &SceneIndex::meshes);
            for (const auto& material : instance.materials)
                check(material.sourcePath, material.name, &SceneIndex::materials);

            const auto meshScene = catalog.find(instance.sourcePath.empty() ? path : resourceKey(instance.sourcePath));
            if (meshScene == catalog.end()) continue;
            const auto mesh = meshScene->second.meshes.find(instance.meshName);
            if (mesh == meshScene->second.meshes.end()) continue; // Can be a spline instance.
            for (size_t i = 0; i < mesh->second.size(); ++i) {
                const auto* binding = i < instance.materials.size() ? &instance.materials[i] : nullptr;
                const auto materialScene = !binding ? meshScene :
                    catalog.find(binding->sourcePath.empty() ? path : resourceKey(binding->sourcePath));
                if (materialScene == catalog.end()) continue;
                const auto& materials = materialScene->second.materials;
                const auto material = materials.find(binding ? binding->name : mesh->second[i]);
                if (material == materials.end()) continue;
                const auto& slots = material->second;
                for (size_t slot = 0; slot < slots.size(); ++slot) {
                    const auto& texture = slots[slot];
                    if (texture.empty()) continue;
                    ++(slot == 0 ? boundTextures : secondaryBindings);
                    if (decodedTextures.count(texture)) continue;
                    ++(slot == 0 ? missingBindings : missingSecondaryBindings);
                    ++sceneMissingBindings[texture];
                    missingBoundTextures.insert(texture);
                }
            }
        }
        for (const auto& [texture, count] : sceneMissingBindings)
            std::cerr << "Unresolved mesh buffer texture: " << path << " -> " << texture
                      << " (" << count << " bindings)\n";
        for (const auto& image : scene.images) {
            if (image.empty()) continue;
            ++images;
            if (decodedTextures.count(resourceKey(image))) continue;
            ++missingImages;
            missingTableTextures.insert(resourceKey(image));
            std::cerr << "Unresolved image table entry: " << path << " -> " << image << '\n';
        }
        for (const auto& texture : scene.materialTextures) {
            ++materialTextures;
            if (decodedTextures.count(texture)) continue;
            ++missingMaterialTextures;
            missingTableTextures.insert(texture);
            std::cerr << "Unresolved material texture: " << path << " -> " << texture << '\n';
        }
    }
    size_t missingWithoutBindings = 0;
    for (const auto& texture : missingTableTextures) {
        if (missingBoundTextures.count(texture)) continue;
        ++missingWithoutBindings;
        std::cerr << "Unresolved catalog texture without decoded mesh-buffer binding: " << texture << '\n';
    }
    std::cout << checked - missing << '/' << checked << " external mesh and material references resolved\n";
    std::cout << images - missingImages << '/' << images << " BRES image references resolved to decoded textures\n";
    std::cout << materialTextures - missingMaterialTextures << '/' << materialTextures
              << " decoded material texture dependencies resolved\n";
    std::cout << boundTextures - missingBindings << '/' << boundTextures
              << " primary textured mesh buffer bindings resolved across the BRES catalog ("
              << missingBoundTextures.size() << " distinct missing textures across all slots)\n";
    std::cout << secondaryBindings - missingSecondaryBindings << '/' << secondaryBindings
              << " prepared indexed secondary material slots resolved across the BRES catalog (draw usage depends on mode)\n";
    // Catalog membership does not prove runtime use. In particular, an instance
    // can select different materials from its mesh library's defaults. Keep the
    // full dependency failure below; this classification only directs investigation.
    std::cout << missingBoundTextures.size() << " distinct unresolved textures have decoded catalog mesh-buffer bindings; "
              << missingWithoutBindings << " have only image/material table references (runtime reachability unverified)\n";
    return missing == 0 && missingMaterialTextures == 0 && missingBindings == 0 && missingSecondaryBindings == 0;
}

bool auditHighlights(FileSystemBase& assets, const CityAssets& city, const std::set<std::string>& textures)
{
    const auto highlights = loadCityHighlights(assets);
    if (highlights.empty()) throw std::runtime_error("Empty retail highlight table.");
    size_t surfaces = 0;
    bool ok = true;
    for (const auto& highlight : highlights) {
        const auto& scene = highlight.model;
        size_t modelSurfaces = 0;
        for (const auto& node : scene.nodes) {
            for (const auto& instance : node.geometry) {
                const auto& library = cityGeometryLibrary(city, scene, instance);
                const auto mesh = std::find_if(library.meshes.begin(), library.meshes.end(),
                    [&](const CityMesh& entry) { return entry.name == instance.meshName; });
                if (mesh == library.meshes.end())
                    throw std::runtime_error("Missing highlight mesh " + instance.meshName);
                modelSurfaces += mesh->buffers.size();
                for (size_t i = 0; i < mesh->buffers.size(); ++i) {
                    const auto material = resolveCityMaterial(city, scene, library, instance, *mesh, i);
                    if (!material.texture.empty() && !textures.count(resourceKey(material.texture))) {
                        std::cerr << "Highlight texture: " << scene.path << " -> " << material.texture << '\n';
                        ok = false;
                    }
                }
            }
        }
        if (!modelSurfaces) throw std::runtime_error("Missing highlight surfaces in " + scene.path);
        surfaces += modelSurfaces;
    }
    std::cout << highlights.size() << " animated retail highlight models, " << surfaces << " surfaces checked\n";
    return ok;
}

bool auditDistrictMaterials(FileSystemBase& assets, const std::set<std::string>& decodedTextures,
                            const std::map<std::string, SceneIndex>& scenesByPath,
                            bool radioPresent, const CityScriptInventory* scripts)
{
    const auto city = loadCityAssets(assets);
    const auto highlightsOk = auditHighlights(assets, city, decodedTextures);
    size_t spawnPoints = 0, spawnValues = 0;
    std::set<int32_t> spawnTypes;
    for (const auto& district : city.districts) {
        spawnPoints += district.spawnPoints.size();
        for (const auto& point : district.spawnPoints) {
            spawnValues += point.values.size();
            spawnTypes.insert(point.objectType);
        }
    }
    std::cout << spawnPoints << " retail spawn points decoded, " << spawnValues
              << " values across " << spawnTypes.size() << " object types\n";
    std::cout << city.spawnGroups.size() << " retail spawn groups bound to metatype IDs\n";
    size_t missingSpawnModels = 0;
    for (const auto& [id, definition] : city.spawnMetatypes) {
        if (scenesByPath.count(resourceKey(definition.model))) continue;
        std::cerr << "Spawn model: " << unsigned(id) << " -> " << definition.model << '\n';
        ++missingSpawnModels;
    }
    std::cout << city.spawnMetatypes.size() - missingSpawnModels << '/' << city.spawnMetatypes.size()
              << " retail spawn metatypes with decoded model scenes\n";
    size_t unresolvedConditions = 0;
    if (scripts) {
        using Type = CityMissionConditionType;
        size_t resolved = 0;
        for (const auto& command : scripts->conditions) {
            auto position = std::optional{std::array<float, 3>{}};
            if (!command.target.empty()) {
                position = findCityMissionTargetPosition(city, command.target);
                if (!position) {
                    std::cerr << "Unresolved mission condition target " << command.target << '\n';
                    ++unresolvedConditions;
                    continue;
                }
                ++resolved;
            }
            RetailMissionConditions conditions;
            if (!conditions.add(command)) throw std::runtime_error("Could not register retail mission condition.");
            RetailMissionTargetState target{*position};
            const auto lookup = [&](std::string_view name) -> std::optional<RetailMissionTargetState> {
                if (name != command.target) return std::nullopt;
                return target;
            };
            auto playerPosition = *position;
            if (command.type == Type::stayBetween)
                playerPosition[2] += (float(command.minimumDistanceCm) + command.argument) * 0.5f;
            const auto initial = conditions.update(0, false, playerPosition, lookup);
            bool valid = !initial.failureCause;
            switch (command.type) {
            case Type::stayBetween: {
                auto far = *position;
                far[2] += float(command.argument) + 1;
                valid = valid && initial.spyGauge && std::abs(*initial.spyGauge - 0.5f) < 0.001f &&
                        conditions.update(0, false, far, lookup).failureCause == -1;
                break;
            }
            case Type::stayWithin: {
                auto outside = *position;
                outside[2] += float(command.argument) + 200;
                const auto warning = conditions.update(1000, false, outside, lookup);
                valid = valid && !initial.warningText && !warning.failureCause &&
                        warning.warningText == command.warningText && warning.timerMs == 9000;
                break;
            }
            case Type::protectCar:
                target.exploded = true;
                [[fallthrough]];
            case Type::protectNpc:
                target.dead = true;
                valid = valid && conditions.update(0, false, *position, lookup).failureCause == command.argument;
                break;
            case Type::beQuick:
                valid = valid && initial.timerMs == command.argument && !conditions.update(0, true, *position, lookup).timerMs;
                break;
            case Type::photographQuickly:
                target.photographed = true;
                valid = valid && initial.timerMs == command.argument &&
                        conditions.update(0, false, *position, lookup).clearPhotoTargets;
                break;
            }
            if (!valid) throw std::runtime_error("Mission condition did not evaluate in retail script " +
                                                  std::to_string(command.script));
        }
        std::cout << scripts->conditions.size() - unresolvedConditions << '/' << scripts->conditions.size()
                  << " mission conditions evaluated offline (" << resolved << " placed targets resolved)\n";
    }
    const auto stations = radioPresent ? loadRetailRadio(assets).stations.size() : 0;
    size_t scenes = 0, bound = 0, missing = 0;
    size_t vehicles = 0, vehicleStations = 0, invalidVehicles = 0, missingVehicleModels = 0;
    size_t doorClips = 0, invalidDoorLibraries = 0;
    size_t matchingDoorTracks = 0, totalDoorTracks = 0, pairsWithoutDoorTracks = 0;
    std::set<std::string> textures, overrides;
    std::map<std::string, std::vector<CityAnimationClip>> doorLibraries;
    std::map<std::string, CityScene> vehicleModels;
    std::set<std::pair<std::string, std::string>> modelLibraryPairs;
    const auto auditVehicles = [&](const std::string& source, const std::vector<CityVehicleDefinition>& records) {
        for (const auto& vehicle : records) {
            ++vehicles;
            vehicleStations += vehicle.radioStation.has_value();
            for (const auto offset : {132, 172}) {
                const auto& path = vehicle.textFields.at(offset);
                if (path.empty()) continue;
                const auto key = resourceKey(path);
                auto [library, inserted] = doorLibraries.try_emplace(key);
                if (inserted) {
                    try {
                        library->second = loadCityDoorClips(assets, path);
                        doorClips += library->second.size();
                    } catch (const std::exception& error) {
                        ++invalidDoorLibraries;
                        std::cerr << "Door animation: " << path << " (" << error.what() << ")\n";
                    }
                }
                const auto modelKey = resourceKey(vehicle.model);
                if (modelKey.empty() || library->second.empty() || !modelLibraryPairs.emplace(modelKey, key).second)
                    continue;
                auto [model, modelInserted] = vehicleModels.try_emplace(modelKey);
                if (modelInserted) model->second = loadCityScene(assets, vehicle.model);
                auto clips = library->second;
                bindCityAnimationTracks(clips, model->second);
                size_t matches = 0;
                for (const auto& clip : clips) {
                    totalDoorTracks += clip.channels.size();
                    matches += std::count_if(clip.channels.begin(), clip.channels.end(),
                        [](const CityAnimationChannel& channel) { return channel.node.has_value(); });
                }
                matchingDoorTracks += matches;
                if (!matches) {
                    ++pairsWithoutDoorTracks;
                    std::cerr << "Door targets: " << vehicle.model << " -> " << path << '\n';
                }
            }
            for (const auto* path : {&vehicle.model, &vehicle.textFields.at(132), &vehicle.textFields.at(172)}) {
                if (path->empty()) continue;
                const std::unique_ptr<IFileStream> asset(assets.openResource(path->c_str()));
                if (asset) continue;
                std::cerr << "Vehicle asset: " << source << '#' << vehicle.name << " -> " << *path << '\n';
                ++missingVehicleModels;
            }
            if (vehicle.radioStation && radioPresent && *vehicle.radioStation >= stations) {
                std::cerr << "Vehicle radio: " << source << '#' << vehicle.name
                          << " -> " << *vehicle.radioStation << '\n';
                ++invalidVehicles;
            }
        }
    };
    for (const auto& district : city.districts) {
        auditVehicles(district.objectsPath, district.vehicles);
        for (const auto* scene : {&district.scene, &district.lowDetailScene}) {
            if (scene->path.empty()) continue;
            ++scenes;
            for (const auto& node : scene->nodes) {
                for (const auto& instance : node.geometry) {
                    const auto& library = cityGeometryLibrary(city, *scene, instance);
                    const auto mesh = std::find_if(library.meshes.begin(), library.meshes.end(),
                        [&](const CityMesh& entry) { return entry.name == instance.meshName; });
                    if (mesh == library.meshes.end()) throw std::runtime_error("Missing district mesh " + instance.meshName);
                    for (size_t i = 0; i < mesh->buffers.size(); ++i) {
                        const auto material = resolveCityMaterial(city, *scene, library, instance, *mesh, i);
                        if (material.texture.empty()) continue;
                        ++bound;
                        const auto texture = resourceKey(material.texture);
                        textures.insert(texture);
                        const auto original = library.materials.find(mesh->buffers[i].material);
                        if (i < instance.materials.size() && original != library.materials.end() &&
                            !original->second.texture.empty()) {
                            const auto path = resourceKey(original->second.texture);
                            if (!decodedTextures.count(path) && decodedTextures.count(texture))
                                overrides.insert(path);
                        }
                        if (decodedTextures.count(texture)) continue;
                        ++missing;
                        std::cerr << "District texture: " << scene->path << '#' << node.name << " -> "
                                  << material.texture << '\n';
                    }
                }
            }
        }
    }
    std::cout << bound - missing << '/' << bound << " textured district mesh buffers resolved across " << scenes
              << " scenes (" << textures.size() << " distinct textures)\n";
    std::cout << overrides.size() << " missing mesh-library textures overridden by available district materials\n";
    size_t metatypeCount = 0;
    if (const std::unique_ptr<IFileStream> meta(assets.openResource("metatypes.gmap")); meta) {
        const auto metatypes = loadCityVehicleMetatypes(assets);
        metatypeCount = metatypes.size();
        auditVehicles("metatypes.gmap", metatypes);
    }
    std::cout << vehicles - metatypeCount << " placed vehicle records and " << metatypeCount
              << " vehicle metatypes decoded (" << vehicleStations
              << " with default radio stations, " << invalidVehicles << " invalid station indices, "
              << missingVehicleModels << " unresolved model or door animation references)\n";
    if (const std::unique_ptr<IFileStream> scripts(assets.openResource("ScriptChapterStartup.array")); scripts) {
        const auto opening = loadCityOpening(assets, city);
        if (opening.vehicleActor) {
            const auto& vehicle = opening.actors.at(*opening.vehicleActor);
            std::cout << "Opening vehicle " << vehicle.name;
            if (vehicle.radioStation) std::cout << " uses radio station " << *vehicle.radioStation;
            std::cout << '\n';
            for (size_t side = 0; side < vehicle.doorClips.size(); ++side) {
                const auto& clips = vehicle.doorClips[side];
                const auto open = std::find_if(clips.begin(), clips.end(),
                    [](const CityAnimationClip& clip) { return clip.name == "car_open_door"; });
                if (open == clips.end() || std::none_of(open->channels.begin(), open->channels.end(),
                    [](const CityAnimationChannel& channel) { return channel.node.has_value(); }))
                    throw std::runtime_error("Opening vehicle has no bound door opening clip.");
            }
        }
    }
    std::cout << doorClips << " clips decoded across " << doorLibraries.size() << " vehicle door libraries ("
              << invalidDoorLibraries << " failed)\n";
    std::cout << matchingDoorTracks << '/' << totalDoorTracks << " vehicle door tracks bound across "
              << modelLibraryPairs.size() << " model-library pairs (" << pairsWithoutDoorTracks
              << " pairs with no matching targets)\n";
    return highlightsOk && missingSpawnModels == 0 && missing == 0 && invalidVehicles == 0 && missingVehicleModels == 0 && invalidDoorLibraries == 0 &&
           unresolvedConditions == 0;
}

bool auditAudio(FileSystemBase& assets, const std::vector<std::string>& paths)
{
    size_t decoded = 0, empty = 0;
    uint64_t totalFrames = 0;
    for (const auto& path : paths) {
        try {
            const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
            if (!file) throw std::runtime_error("Could not open audio resource");
            VorbisStream stream(*file);
            if (!file->size()) {
                ++empty;
                continue;
            }
            std::array<int16_t, 8192> samples{};
            uint64_t frames = 0;
            while (const auto count = stream.readFrames(samples.data(), samples.size() / stream.channels()))
                frames += count;
            if (frames != stream.frameCount())
                throw std::runtime_error("Decoded frame count does not match the Ogg stream.");
            totalFrames += frames;
            ++decoded;
        } catch (const std::exception& error) {
            std::cerr << "Audio: " << path << " (" << error.what() << ")\n";
        }
    }
    std::cout << decoded << " Vorbis streams decoded (" << totalFrames << " frames), "
              << empty << " empty Ogg slots, " << paths.size() - decoded - empty << " failures\n";
    return decoded + empty == paths.size();
}

void auditParticleSimulation(const CityScene& scene, const CityParticleEmitter& emitter)
{
    CityParticleSimulation simulation;
    constexpr float step = 1.f / 60;
    bool emitted = false;
    for (unsigned frame = 0; frame < 600; ++frame) {
        const auto elapsedMs = frame * step * 1000;
        simulation.advance(emitter, step, elapsedMs, {});
        const auto& particles = simulation.particles();
        emitted |= !particles.empty();
        if (emitter.maxParticles && particles.size() > emitter.maxParticles)
            throw std::runtime_error("Particle emitter exceeded its authored limit.");
        const auto finite = [](float component) { return std::isfinite(component); };
        for (const auto& particle : particles) {
            const auto color = cityParticleColor(scene, particle, elapsedMs);
            if (particle.age >= particle.lifetime || particle.size < 0 || !finite(particle.size) ||
                !std::all_of(particle.position.begin(), particle.position.end(), finite) ||
                !std::all_of(particle.velocity.begin(), particle.velocity.end(), finite) ||
                !std::all_of(color.begin(), color.end(), finite))
                throw std::runtime_error("Particle emitter generated an invalid sample.");
        }
    }
    const auto rate = emitter.tracks.find(CityParticleParameter::birthRate);
    const bool hasBirths = rate != emitter.tracks.end() ?
        std::any_of(rate->second.begin(), rate->second.end(), [](const CityParticleKey& key) {
            return key.value > 0 && key.timeMs < 10000;
        }) : emitter.birthRate > 0;
    if (hasBirths && !emitted)
        throw std::runtime_error("No live particles sampled from an enabled emitter.");
    const auto maxLife = std::max(0.f, emitter.life + std::abs(emitter.lifeVariation) / 2);
    simulation.advance(emitter, maxLife + step, 10000, {}, false);
    if (!simulation.particles().empty())
        throw std::runtime_error("Particles survived past the authored lifetime.");
}

bool auditSoundIndex(FileSystemBase& assets, const std::vector<std::string>& audio)
{
    const auto names = readRetailSoundIndex(assets);
    std::vector<std::string> indexed;
    indexed.reserve(names.size());
    size_t missing = 0;
    for (size_t id = 0; id < names.size(); ++id) {
        const auto path = retailSoundPath(static_cast<unsigned>(id));
        if (!std::binary_search(audio.begin(), audio.end(), path)) {
            std::cerr << "Missing indexed audio: " << path << " (" << names[id] << ")\n";
            ++missing;
        }
        indexed.push_back(path);
    }
    std::sort(indexed.begin(), indexed.end());
    size_t unindexed = 0;
    for (const auto& path : audio) {
        if (!std::binary_search(indexed.begin(), indexed.end(), path)) {
            std::cerr << "Unindexed Ogg resource: " << path << '\n';
            ++unindexed;
        }
    }
    std::cout << names.size() << " retail sound index entries, " << missing
              << " missing resources, " << unindexed << " unindexed Ogg files\n";
    return missing == 0 && unindexed == 0;
}

bool auditRadio(FileSystemBase& assets)
{
    const auto catalog = loadRetailRadio(assets);
    size_t tracks = 0, missing = 0;
    const auto check = [&](const std::vector<uint32_t>& ids) {
        for (const auto id : ids) {
            ++tracks;
            const auto path = retailSoundPath(id);
            const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
            if (file && file->size()) continue;
            ++missing;
            std::cerr << "Radio reference: " << id << " -> " << path << '\n';
        }
    };
    check(catalog.ads);
    for (const auto& station : catalog.stations) {
        check(station.jingles);
        check(station.songs);
    }
    std::cout << catalog.stations.size() << " radio stations, " << tracks - missing << '/'
              << tracks << " nonempty song, advertisement, and jingle references\n";
    return missing == 0;
}

bool auditText(FileSystemBase& assets, const std::vector<std::string>& paths)
{
    std::map<std::string, size_t> groups;
    size_t accepted = 0;
    for (const auto& path : paths) {
        try {
            const auto strings = readRetailStrings(assets, path);
            const auto group = std::filesystem::path(path).replace_extension().generic_string();
            const auto [known, inserted] = groups.emplace(group, strings.size());
            if (strings.empty() || (!inserted && known->second != strings.size()))
                throw std::runtime_error("String count differs within the table group.");
            ++accepted;
        } catch (const std::exception& error) {
            std::cerr << "Text: " << path << " (" << error.what() << ")\n";
        }
    }
    std::cout << accepted << '/' << paths.size() << " UTF-8 text and metadata tables accepted across "
              << groups.size() << " groups\n";
    return accepted == paths.size();
}
}

int wmain(int argc, wchar_t** argv)
try {
    if (argc != 2) {
        std::cerr << "Usage: retail_asset_audit <extracted game data directory>\n";
        return 2;
    }

    const std::filesystem::path root(argv[1]);
    ResourceFileSystem assets(root);
    std::vector<std::string> paths, textures, audio, text, constants, spritePaths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension();
        const auto path = entry.path().lexically_relative(root).generic_string();
        if (extension == L".bdae") paths.push_back(path);
        if (extension == L".bmp" || extension == L".tga" || extension == L".psd" || extension == L".png" ||
            extension == L".dds" || extension == L".jpg" || extension == L".jpeg" || extension == L".pcx" ||
            extension == L".ppm" || extension == L".pgm" || extension == L".pbm")
            textures.push_back(path);
        if (extension == L".ogg") audio.push_back(path);
        if (extension == L".bsprite") spritePaths.push_back(path);
        const std::string constantSuffix = "_pycst.bin";
        if (path.size() >= constantSuffix.size() &&
            path.compare(path.size() - constantSuffix.size(), constantSuffix.size(), constantSuffix) == 0)
            constants.push_back(path);
        const auto suffix = extension.string();
        if (extension == L".character" || extension == L".type" ||
            (suffix.size() > 1 && retailLanguage(std::string_view(suffix).substr(1))))
            text.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    std::sort(textures.begin(), textures.end());
    std::sort(audio.begin(), audio.end());
    std::sort(text.begin(), text.end());
    std::sort(constants.begin(), constants.end());
    std::sort(spritePaths.begin(), spritePaths.end());
    const bool soundIndexPresent = std::filesystem::exists(root / "Res.array");
    const bool radioPresent = std::filesystem::exists(root / "RadioStation.gmap") ||
                              std::filesystem::exists(root / "RadioAds.array");
    const bool scriptsPresent = std::filesystem::exists(root / "allscripts.gmap") ||
                                std::filesystem::exists(root / "ScriptChapterStartup.array");
    const bool missionsPresent = std::filesystem::exists(root / "Missions.gmap");
    if (paths.empty() && textures.empty() && audio.empty() && text.empty() && constants.empty() &&
        spritePaths.empty() && !radioPresent && !scriptsPresent && !missionsPresent && !soundIndexPresent) {
        std::cerr << "No supported resource files found.\n";
        return 2;
    }
    const auto scripts = scriptsPresent ? std::optional{auditCityScripts(assets)} : std::nullopt;
    if (scripts) {
        const auto program = loadCityScriptProgram(assets);
        size_t commands = 0;
        for (const auto& script : program) commands += script.size();
        if (program.size() != scripts->scripts || commands != scripts->commands)
            throw std::runtime_error("Executable script catalog disagrees with retail command counts.");
        std::cout << commands << " executable command records retained across " << program.size() << " scripts\n";
    }
    size_t spriteSheets = 0, spriteFrames = 0, spriteAnimations = 0, spriteDraws = 0;
    for (const auto& path : spritePaths) {
        try {
            const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
            if (!file) throw std::runtime_error("Could not open sprite.");
            const auto sprite = readRetailSprite(*file);
            const auto atlas = loadRetailSpriteAtlas(assets, path, sprite);
            Image sample{640, 480, std::vector<uint32_t>(640 * 480)};
            bool painted = false;
            size_t draws = 0;
            for (size_t frame = 0; frame < sprite.frames.size(); ++frame) {
                const auto composed = composeRetailSpriteFrame(sprite, frame, 320, 240);
                draws += composed.size();
                if (!painted && !composed.empty()) {
                    paintRetailSpriteDraws(sprite, atlas, composed, sample);
                    painted = std::any_of(sample.bgra.begin(), sample.bgra.end(),
                        [](uint32_t pixel) { return pixel != 0; });
                }
            }
            if (!painted) throw std::runtime_error("Sprite frames did not produce any visible pixels.");
            for (size_t animation = 0; animation < sprite.animations.size(); ++animation)
                for (size_t frame = 0; frame < sprite.animations[animation].count; ++frame)
                    draws += composeRetailSpriteAnimationFrame(sprite, animation, frame).size();
            ++spriteSheets;
            spriteDraws += draws;
            spriteFrames += sprite.frames.size();
            spriteAnimations += sprite.animations.size();
        } catch (const std::exception& error) {
            std::cerr << "Sprite: " << path << " (" << error.what() << ")\n";
        }
    }
    std::cout << spriteSheets << '/' << spritePaths.size() << " sprites decoded and sampled (" << spriteFrames
              << " frames, " << spriteAnimations << " animations and " << spriteDraws << " composed modules)\n";
    if (std::filesystem::exists(root / "cinematics_hud.bsprite")) {
        const auto bars = rasterRetailSpriteFrame(assets, "cinematics_hud.bsprite", "BARS", 480, 320);
        const auto alpha = [&](size_t x, size_t y) { return bars.bgra[y * bars.width + x] >> 24; };
        if (!alpha(240, 20) || !alpha(240, 280) || alpha(240, 160))
            throw std::runtime_error("Retail cinematic bars did not leave the viewport center transparent.");
        std::cout << "Retail BARS frame painted above and below a transparent viewport center.\n";
    }
    const auto auditMinimap = [&](RetailMap map, const char* path) {
        if (!std::filesystem::exists(root / path)) return;
        const auto minimap = loadRetailMinimap(assets, map);
        for (const auto* name : {"MINIMAP_BG", "MINIMAP_PLAYER", "MINIMAP_NORTH", "MINIMAP_CHECKPOINT"}) {
            const auto& image = minimap.frames.at(name).image;
            if (std::none_of(image.bgra.begin(), image.bgra.end(),
                             [](uint32_t pixel) { return pixel >> 24; }))
                throw std::runtime_error(std::string("Empty retail minimap frame ") + name);
        }
        if (retailMinimapScale(0) != 1.5f || retailMinimapScale(30) != 1.5f ||
            retailMinimapScale(65) != 2.f || retailMinimapScale(100) != 2.5f || retailMinimapScale(140) != 2.5f)
            throw std::runtime_error("Retail minimap zoom thresholds were not preserved.");
        const auto& player = minimap.frames.at("MINIMAP_PLAYER").image;
        if (std::none_of(player.bgra.begin(), player.bgra.end(), [](uint32_t pixel) { return !(pixel >> 24); }))
            throw std::runtime_error("Retail minimap player marker lost transparency.");
        const auto& animation = minimap.checkpointAnimation;
        constexpr std::array<const char*, 4> expected{
            "MINIMAP_CHECKPOINT", "MINIMAP_CHECKPOINT02", "MINIMAP_CHECKPOINT03", "MINIMAP_CHECKPOINT02"
        };
        if (animation.size() != expected.size() || retailMinimapAnimationStep(animation, 8) != 0)
            throw std::runtime_error("Retail checkpoint animation did not loop at eight ticks.");
        for (size_t i = 0; i < expected.size(); ++i)
            if (animation[i].frame != expected[i] || animation[i].duration != 2 ||
                retailMinimapAnimationStep(animation, 2.0 * i + 1.9) != i)
                throw std::runtime_error("Retail checkpoint animation sequence or duration changed.");
        const auto sample = retailMinimapSample({0.5f, 0.5f}, {0, 2}, {32, 16}, 2, 1024, 1024);
        if (sample != std::array<float, 2>{0.53125f, 0.4375f})
            throw std::runtime_error("Retail minimap viewport did not rotate and zoom about its center.");
        const auto marker = retailMinimapMarkerOffset({0.5f, 0.5f}, sample, {0, 2}, {40, 30}, 2, 1024, 1024);
        const auto farMarker = retailMinimapMarkerOffset({0.5f, 0.5f}, {0.375f, 0.5625f},
                                                        {-2, 0}, {24, 20}, 2, 1024, 1024);
        if (marker != std::array<float, 2>{32, 16} || farMarker != std::array<float, 2>{24, -12})
            throw std::runtime_error("Retail minimap marker rotation or edge clipping failed.");
        const auto project = [&](float x, float y, float u, float v) {
            if (retailMinimapUv(map, x, y) != std::array<float, 2>{u, v})
                throw std::runtime_error("Retail minimap projection changed.");
        };
        if (map == RetailMap::city) {
            project(-80000, 80000, 0, 0);
            project(0, 0, 0.5f, 0.5f);
            project(80000, -80000, 1, 1);
        } else {
            project(184700, -6000, 0, 0);
            project(187700, -9000, 0.5f, 0.5f);
            project(190700, -12000, 1, 1);
        }
        std::cout << "Decoded " << path << " (" << minimap.map.width << 'x' << minimap.map.height
                  << ") and " << minimap.frames.size() << " HUD frames. Projection and zoom validated.\n";
    };
    auditMinimap(RetailMap::city, "minimap_map.bmp");
    auditMinimap(RetailMap::prison, "prison_map.bmp");
    if (scripts) {
        std::cout << scripts->scripts << " scripts, " << scripts->commands << " commands, " << scripts->opcodes
                  << " opcodes, " << scripts->links << " script links and " << scripts->chapterStarts.size()
                  << " chapter startup references validated\n";
        using Type = CityMissionConditionType;
        for (const auto& command : scripts->conditions) {
            if ((command.type != Type::beQuick && command.target.empty()) ||
                ((command.type == Type::beQuick || command.type == Type::photographQuickly ||
                  command.type == Type::stayWithin) && command.argument < 0))
                throw std::runtime_error("Invalid mission condition command in retail script " +
                                         std::to_string(command.script));
        }
        std::cout << scripts->conditions.size() << " mission condition commands decoded\n";
    }
    if (missionsPresent) {
        const auto missions = loadCityMissions(assets);
        std::set<std::string> sprites;
        for (const auto& mission : missions) {
            if (mission.sprite.empty() || !sprites.insert(resourceKey(mission.sprite)).second) continue;
            const std::unique_ptr<IFileStream> asset(assets.openResource(mission.sprite.c_str()));
            if (!asset || !asset->size())
                throw std::runtime_error("Missing retail mission sprite " + mission.sprite);
        }
        std::cout << missions.size() << " mission definitions and " << sprites.size()
                  << " sprite references validated\n";
        if (scripts) {
            for (const auto& start : scripts->missionStarts)
                if (start.mission >= missions.size())
                    throw std::runtime_error("Out-of-range mission ID in retail script " +
                        std::to_string(start.script) + ", command " + std::to_string(start.command));
            std::cout << scripts->missionStarts.size() << " MissionStart references validated\n";
        }
    }

    size_t containers = 0, particleScenes = 0, particleTracks = 0, simulatedEmitters = 0;
    std::map<CityMaterialMode, size_t> materialModes;
    size_t unsupportedMaterials = 0, twoStageMaterials = 0;
    size_t meshesWithNormals = 0, authoredNormals = 0, authoredColors = 0;
    size_t nodesWithProperties = 0, billboardNodes = 0, lodNodes = 0;
    size_t auxiliaryUvMeshes = 0, auxiliaryUvCount = 0;
    size_t secondaryColorMeshes = 0, secondaryColorCount = 0;
    std::map<std::string, SceneIndex> scenes;
    for (const auto& path : paths) {
        try {
            validateBresContainer(assets, path);
            ++containers;
        } catch (const std::exception& error) {
            std::cerr << "Container: " << path << " (" << error.what() << ")\n";
            continue;
        }
        try {
            const auto scene = loadCityScene(assets, path);
            auditPrimaryColorLanes(scene);
            for (const auto& [name, material] : scene.materials) {
                ++materialModes[material.mode];
                if ((material.mode == CityMaterialMode::lightmap || material.mode == CityMaterialMode::reflection ||
                     material.mode == CityMaterialMode::transparentReflection) &&
                    !material.additionalTextures.empty() && !material.additionalTextures[0].empty()) ++twoStageMaterials;
                if (!cityMaterialModeSupported(material.mode)) {
                    ++unsupportedMaterials;
                    std::cerr << "Unsupported material mode " << static_cast<uint32_t>(material.mode)
                              << ": " << path << '#' << name << '\n';
                }
            }
            if (!scene.particleEmitters.empty()) {
                ++particleScenes;
                for (const auto& emitter : scene.particleEmitters) {
                    auditParticleSimulation(scene, emitter);
                    ++simulatedEmitters;
                    particleTracks += emitter.tracks.size();
                }
            }
            for (const auto& mesh : scene.meshes) {
                if (!mesh.colors.empty() && mesh.colors.size() != mesh.positions.size())
                    throw std::runtime_error("Mismatched authored mesh colors.");
                authoredColors += mesh.colors.size();
                if (!mesh.secondaryColors.empty()) {
                    if (mesh.secondaryColors.size() != mesh.positions.size())
                        throw std::runtime_error("Mismatched secondary mesh colors.");
                    ++secondaryColorMeshes;
                    secondaryColorCount += mesh.secondaryColors.size();
                }
                if (!mesh.auxiliaryTexCoords.empty()) {
                    if (mesh.auxiliaryTexCoords.size() != mesh.positions.size())
                        throw std::runtime_error("Mismatched auxiliary texture coordinates.");
                    ++auxiliaryUvMeshes;
                    auxiliaryUvCount += mesh.auxiliaryTexCoords.size();
                }
                if (mesh.normals.empty()) continue;
                if (mesh.normals.size() != mesh.positions.size())
                    throw std::runtime_error("Mismatched authored mesh normals.");
                ++meshesWithNormals;
                authoredNormals += mesh.normals.size();
            }
            for (const auto& node : scene.nodes) {
                nodesWithProperties += !node.userProperties.empty();
                billboardNodes += node.property("billboard") == "1";
                lodNodes += node.property("lod").has_value();
            }
            scenes.emplace(resourceKey(path), indexScene(scene));
        } catch (const std::exception& error) {
            std::cerr << "Scene: " << path << " (" << error.what() << ")\n";
        }
    }
    std::cout << containers << '/' << paths.size() << " BRES headers and relocation tables accepted\n";
    std::cout << scenes.size() << '/' << paths.size() << " accepted by the current scene decoder\n";
    std::cout << particleScenes << " BRES particle scenes with decoded emitters and "
              << particleTracks << " animated parameters, " << simulatedEmitters << " emitters simulated\n";
    std::cout << authoredColors << " authored literal primary RGBA vertex colors validated against raw records\n";
    for (const auto mode : {CityMaterialMode::lightmap, CityMaterialMode::reflection,
                            CityMaterialMode::transparentReflection}) materialModes.try_emplace(mode, 0);
    for (const auto& [mode, count] : materialModes)
        std::cout << count << " original material records select mode " << static_cast<uint32_t>(mode) << '\n';
    std::cout << twoStageMaterials << " material records select a two-stage renderer with a stage1 image\n";
    std::cout << secondaryColorCount << " secondary BGRA vertex colors decoded across "
              << secondaryColorMeshes << " meshes\n";
    std::cout << auxiliaryUvCount << " auxiliary texture coordinates decoded across "
              << auxiliaryUvMeshes << " meshes\n";
    std::cout << authoredNormals << " authored vertex normals decoded across " << meshesWithNormals << " meshes\n";
    std::cout << nodesWithProperties << " BRES nodes with user properties, " << billboardNodes
              << " billboards and " << lodNodes << " LOD-tagged nodes\n";
    std::set<std::string> referencedTextures;
    for (const auto& [path, scene] : scenes) {
        referencedTextures.insert(scene.materialTextures.begin(), scene.materialTextures.end());
        for (const auto& image : scene.images)
            if (!image.empty()) referencedTextures.insert(resourceKey(image));
    }
    std::set<std::string> listedTextures;
    for (const auto& path : textures) listedTextures.insert(resourceKey(path));
    auto texturePaths = listedTextures;
    texturePaths.insert(referencedTextures.begin(), referencedTextures.end());
    std::set<std::string> decoded;
    size_t absentReferences = 0, rejectedReferences = 0, decodedListed = 0;
    for (const auto& path : texturePaths) {
        const bool referenced = referencedTextures.count(path) != 0;
        try {
            const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
            if (!file) {
                if (referenced) ++absentReferences;
                continue;
            }
            readTexture(*file, true);
            decoded.insert(path);
            if (listedTextures.count(path)) ++decodedListed;
        } catch (const std::exception& error) {
            if (referenced) ++rejectedReferences;
            std::cerr << "Texture: " << path << " (" << error.what() << ")\n";
        }
    }
    std::cout << decodedListed << '/' << listedTextures.size() << " BMP/DDS/PNG/JPEG/PCX/PPM/PGM/PBM/TGA/PSD images decoded\n";
    std::cout << absentReferences << " distinct referenced texture paths could not be opened, "
              << rejectedReferences << " opened but failed to decode, and "
              << decoded.size() - decodedListed << " decoded outside the extension-based inventory\n";
    const auto referencesOk = auditReferences(scenes, decoded);
    const auto audioOk = auditAudio(assets, audio);
    const bool soundIndexOk = !soundIndexPresent || auditSoundIndex(assets, audio);
    const auto textOk = auditText(assets, text);
    const bool radioOk = !radioPresent || auditRadio(assets);
    const bool districtsOk = !std::filesystem::exists(root / "cityparts.array") ||
                             auditDistrictMaterials(assets, decoded, scenes, radioPresent, scripts ? &*scripts : nullptr);
    size_t constantTables = 0, constantEntries = 0, constantStrings = 0;
    for (const auto& path : constants) {
        try {
            const auto table = readRetailConstants(assets, path.c_str());
            ++constantTables;
            for (const auto& group : table) {
                constantEntries += group.second.size();
                for (const auto& entry : group.second)
                    constantStrings += std::holds_alternative<std::string>(entry.second);
            }
        } catch (const std::exception& error) {
            std::cerr << "Constants: " << path << " (" << error.what() << ")\n";
        }
    }
    std::cout << constantTables << '/' << constants.size() << " constant tables decoded, " << constantEntries
              << " entries including " << constantStrings << " strings\n";
    return scenes.size() == paths.size() && unsupportedMaterials == 0 && referencesOk && districtsOk && audioOk && soundIndexOk && radioOk && textOk &&
           constantTables == constants.size() && spriteSheets == spritePaths.size() &&
           absentReferences == 0 && rejectedReferences == 0 && decodedListed == listedTextures.size() ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
