#include "city_assets.h"
#include "filesystem.h"

#include "FileSystemBase.h"
#include "IFileStream.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace {
std::vector<unsigned char> readAsset(FileSystemBase& assets, const std::string& path, uint32_t limit)
{
    const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
    if (!file) throw std::runtime_error("Missing city asset " + path);
    if (!file->size() || file->size() > limit) throw std::runtime_error("Invalid city asset size for " + path);
    std::vector<unsigned char> bytes(static_cast<size_t>(file->size()));
    if (file->read(bytes.data(), bytes.size()) != bytes.size())
        throw std::runtime_error("Could not read city asset " + path);
    return bytes;
}

class CityDataReader {
public:
    CityDataReader(const std::string& path, const std::vector<unsigned char>& bytes)
        : path(path), bytes(bytes), end(bytes.size()) {}

    uint32_t integer()
    {
        require(4);
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i) result = (result << 8) | bytes[offset++];
        return result;
    }

    std::string string()
    {
        const auto length = integer();
        require(length);
        std::string value(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
        // Retail DataStream strings sometimes store their terminator in the length.
        if (!value.empty() && value.back() == '\0') value.pop_back();
        if (value.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid string in " + path);
        return value;
    }

    void finish(size_t padding = 0) const
    {
        if (end - offset > padding ||
            std::any_of(bytes.begin() + offset, bytes.begin() + end, [](unsigned char byte) { return byte != 0; }))
            throw std::runtime_error("Unexpected data in " + path);
    }

    void skip(size_t count)
    {
        require(count);
        offset += count;
    }

    size_t position() const { return offset; }
    size_t remaining() const { return end - offset; }

    std::vector<uint8_t> remainingBytes() const
    {
        return {bytes.begin() + offset, bytes.begin() + end};
    }

    CityDataReader record()
    {
        const auto size = integer();
        CityDataReader bounded = *this;
        skip(size);
        bounded.end = offset;
        return bounded;
    }

    int32_t signedInteger()
    {
        const auto bits = integer();
        int32_t signedBits;
        std::memcpy(&signedBits, &bits, sizeof signedBits);
        return signedBits;
    }

private:
    void require(size_t count) const
    {
        if (count > end - offset) throw std::runtime_error("Truncated " + path);
    }

    const std::string& path;
    const std::vector<unsigned char>& bytes;
    size_t offset = 0;
    size_t end;
};

constexpr char chapterStartupPath[] = "ScriptChapterStartup.array";
constexpr char scriptMapPath[] = "allscripts.gmap";

std::optional<CityMissionConditionCommand> readMissionCondition(CityDataReader& command, uint32_t opcode,
                                                               uint32_t script = 0, uint32_t index = 0)
{
    using Type = CityMissionConditionType;
    const auto type = static_cast<Type>(opcode);
    CityMissionConditionCommand result{script, index, type, {}, 0};
    switch (type) {
    case Type::protectNpc:
        // ProtectNPC stores its failure cause before the object name.
        result.argument = command.signedInteger();
        result.target = command.string();
        break;
    case Type::protectCar:
    case Type::photographQuickly:
    case Type::stayWithin:
        result.target = command.string();
        result.argument = command.signedInteger();
        if (type == Type::stayWithin) result.warningText = command.signedInteger();
        break;
    case Type::beQuick:
        result.argument = command.signedInteger();
        break;
    default:
        return std::nullopt;
    }
    command.finish();
    return result;
}

CityAudioCue readAudioCommand(CityDataReader& command, uint32_t opcode, uint32_t timeMs)
{
    const auto sound = command.integer();
    command.finish();
    const auto action = opcode == 60 ? CityAudioAction::playOnce :
        opcode == 62 ? CityAudioAction::playPriorityLoop : CityAudioAction::stop;
    return {timeMs, sound, action};
}

uint32_t readMissionObjective(CityDataReader& command)
{
    const auto textId = command.integer();
    command.finish();
    return textId;
}

CityScriptCamera readCameraCommand(CityDataReader& command, uint32_t opcode)
{
    std::optional<std::string> scene;
    if (opcode == 12) {
        scene = command.string();
        if (scene->empty()) throw std::runtime_error("Empty retail scene camera name.");
    }
    const auto milliseconds = command.signedInteger();
    command.finish();
    return {std::move(scene), milliseconds};
}

CityFade readFadeCommand(CityDataReader& command, uint32_t opcode, uint32_t timeMs)
{
    const auto duration = command.signedInteger();
    command.finish();
    return {timeMs, static_cast<uint32_t>(std::max(0, duration)), opcode == 41};
}

CityScriptActorPlacement readActorPlacement(CityDataReader& command, uint32_t opcode)
{
    const auto action = opcode == 21 ? CityScriptActorAction::enable :
        opcode == 22 ? CityScriptActorAction::disable : CityScriptActorAction::moveTo;
    CityScriptActorPlacement placement{action, {}, {}};
    if (action != CityScriptActorAction::disable) placement.marker = command.string();
    placement.actor = command.string();
    command.finish();
    return placement;
}

CityScriptActorAnimation readActorAnimation(CityDataReader& command, uint32_t opcode)
{
    CityScriptActorAnimation animation;
    animation.first = command.string();
    if (opcode == 7) animation.loop = command.string();
    animation.actor = command.string();
    command.finish();
    return animation;
}

CityScriptDialogue readDialogueCommand(CityDataReader& command)
{
    CityScriptDialogue dialogue{command.signedInteger(), command.signedInteger(), command.integer()};
    command.finish();
    return dialogue;
}

CityScriptCheckpoint readCheckpointCommand(CityDataReader& command, uint32_t opcode)
{
    CityScriptCheckpoint result{command.string(), std::nullopt};
    if (opcode == 105) result.vehicle = command.string();
    command.finish();
    if (result.object.empty() || (result.vehicle && result.vehicle->empty()))
        throw std::runtime_error("Empty retail script checkpoint target.");
    return result;
}

CityWaitEscape readWaitEscape(CityDataReader& command)
{
    CityWaitEscape result{command.string(), command.signedInteger()};
    command.finish();
    if (result.target.empty()) throw std::runtime_error("Empty retail WaitEscape target.");
    return result;
}

CityScriptTutorial readScriptTutorial(CityDataReader& command)
{
    const auto text = command.integer();
    const auto title = command.integer();
    command.finish();
    return {title, text};
}

std::string readScriptObject(CityDataReader& command)
{
    auto object = command.string();
    command.finish();
    if (object.empty()) throw std::runtime_error("Empty retail script object name.");
    return object;
}

CityActorAttack readActorAttack(CityDataReader& command)
{
    CityActorAttack attack{command.string(), command.string()};
    command.finish();
    if (attack.attacker.empty() || attack.target.empty())
        throw std::runtime_error("Empty retail ActorAttack object name.");
    return attack;
}

struct CityHighlightEntry {
    std::string model;
    int32_t icon = -1;
};

std::vector<CityHighlightEntry> readHighlights(FileSystemBase& assets)
{
    const std::string path = "Highlights.array";
    const auto bytes = readAsset(assets, path, 64 * 1024);
    CityDataReader stream(path, bytes);
    const auto count = stream.integer();
    constexpr size_t minimumBytesPerEntry = 8;
    if (bytes.size() < 8 || count > (bytes.size() - 8) / minimumBytesPerEntry)
        throw std::runtime_error("Invalid retail highlight model count.");
    std::vector<CityHighlightEntry> entries(count);
    for (auto& entry : entries) {
        entry.model = stream.string();
        if (entry.model.empty()) throw std::runtime_error("Empty retail highlight model path.");
    }
    if (stream.integer() != count) throw std::runtime_error("Invalid retail highlight icon count.");
    for (auto& entry : entries) entry.icon = stream.signedInteger();
    stream.finish();
    return entries;
}

class CityScriptMap {
public:
    explicit CityScriptMap(FileSystemBase& assets)
        : path(scriptMapPath), bytes(readAsset(assets, path, 16 * 1024 * 1024))
    {
        CityDataReader stream(path, bytes);
        const auto count = stream.integer();
        if (!count || count > (bytes.size() - 4) / 4)
            throw std::runtime_error("Invalid retail script count.");
        starts.reserve(count);
        std::set<uint32_t> opcodes;
        for (uint32_t script = 0; script < count; ++script) {
            starts.push_back(stream.position());
            const auto commands = stream.integer();
            for (uint32_t i = 0; i < commands; ++i) {
                auto command = stream.record();
                const auto opcode = command.integer();
                ++inventory.commands;
                opcodes.insert(opcode);
                if (opcode >= 1 && opcode <= 3) { // PlayScript, LockScript, and UnlockScript.
                    if (command.integer() >= count)
                        throw std::runtime_error("Out-of-range script link in retail script " + std::to_string(script));
                    command.finish();
                    ++inventory.links;
                }
                if (opcode == 4) command.finish(); // New-game startup marker.
                if (opcode == 6 || opcode == 7) readActorAnimation(command, opcode);
                if (opcode == 12 || opcode == 13) readCameraCommand(command, opcode);
                if (opcode >= 15 && opcode <= 18) command.finish(); // Cinematic transitions.
                if (opcode == 21 || opcode == 22 || opcode == 24) readActorPlacement(command, opcode);
                if (opcode == 29) readActorAttack(command);
                if (opcode == 41 || opcode == 42) readFadeCommand(command, opcode, 0);
                if (opcode == 43) {
                    command.signedInteger(); // Cinematic animation index.
                    command.finish();
                }
                if (opcode == 47 || opcode == 112) readScriptTutorial(command);
                if (opcode == 48) command.finish(); // HideTutorial.
                if (opcode == 56) readDialogueCommand(command);
                if (opcode == 80) {
                    command.signedInteger(); // Limit weapon HUD.
                    command.finish();
                }
                if (opcode == 81) command.finish(); // Release weapon HUD limit.
                if (opcode == 57) { // Wait
                    command.signedInteger();
                    command.finish();
                }
                if (opcode == 64 || opcode == 105) readCheckpointCommand(command, opcode);
                if (opcode == 23 || opcode == 76 || opcode == 77 || opcode == 87 || opcode == 89 ||
                    opcode == 99 || opcode == 106 || opcode == 108)
                    readScriptObject(command);
                if (opcode == 111) {
                    command.signedInteger(); // HUD blink target.
                    command.finish();
                }
                if (opcode == 113) command.finish(); // Record chapter completion.
                if (opcode == 101) readWaitEscape(command);
                if (opcode >= 60 && opcode <= 63) readAudioCommand(command, opcode, 0);
                if (opcode == 65) { // MissionStart.
                    inventory.missionStarts.push_back({script, i, command.integer()});
                    command.finish();
                }
                if (opcode == 66) readMissionObjective(command);
                if (opcode == 67 || opcode == 68) command.finish(); // MissionOver and MissionClearConditions.
                if (auto condition = readMissionCondition(command, opcode, script, i))
                    inventory.conditions.push_back(std::move(*condition));
            }
        }
        stream.finish(2);
        inventory.scripts = starts.size();
        inventory.opcodes = opcodes.size();

        const std::string chaptersPath = chapterStartupPath;
        const auto chapterBytes = readAsset(assets, chaptersPath, 64 * 1024);
        CityDataReader chapters(chaptersPath, chapterBytes);
        const auto chapterCount = chapters.integer();
        if (!chapterCount) throw std::runtime_error("No retail chapter startup scripts.");
        for (uint32_t i = 0; i < chapterCount; ++i) {
            const auto script = chapters.integer();
            if (script >= count) throw std::runtime_error("Invalid retail chapter startup script.");
            inventory.chapterStarts.push_back(script);
        }
        chapters.finish();
    }

    CityDataReader script(uint32_t id) const
    {
        if (id >= starts.size()) throw std::runtime_error("Invalid retail script index.");
        CityDataReader reader(path, bytes);
        reader.skip(starts[id]);
        return reader;
    }

    CityScriptInventory inventory;

private:
    const std::string path;
    const std::vector<unsigned char> bytes;
    std::vector<size_t> starts;
};

template<class Object>
void readBasicObject(CityDataReader& record, Object& object)
{
    object.name = record.string();
    for (auto& field : object.objectFields) field = record.signedInteger();
    object.model = record.string();
    object.node = record.string();
    object.poolSize = record.signedInteger();
    for (auto& coordinate : object.position) coordinate = record.signedInteger();
    for (auto& angle : object.rotation) angle = record.signedInteger();
    for (auto& factor : object.scale) factor = record.signedInteger() * 0.01f;
}

std::vector<CitySpawnPoint> readSpawnPoints(const std::string& path, const std::vector<unsigned char>& bytes)
{
    CityDataReader map(path, bytes);
    const auto count = map.integer();
    std::vector<CitySpawnPoint> points;
    for (uint32_t i = 0; i < count; ++i) {
        auto record = map.record();
        if (record.integer() != 9) continue;
        CitySpawnPoint point{};
        readBasicObject(record, point);
        point.terrainBuffer = static_cast<uint16_t>(record.integer());
        const auto values = record.integer();
        constexpr size_t trailerSize = 8;
        if (record.remaining() < trailerSize || values > (record.remaining() - trailerSize) / 4)
            throw std::runtime_error("Invalid spawn point value count in " + path);
        point.values.reserve(values);
        for (uint32_t value = 0; value < values; ++value) point.values.push_back(record.signedInteger());
        point.objectType = record.signedInteger();
        point.terrainIndexOffset = static_cast<uint16_t>(record.integer());
        // SpawnPoint::Load clears both indices for an out-of-range buffer sentinel.
        if (point.terrainBuffer > 59999) point.terrainBuffer = point.terrainIndexOffset = 0;
        record.finish();
        points.push_back(std::move(point));
    }
    return points;
}

void bindSpawnGroups(FileSystemBase& assets, CityAssets& city)
{
    const std::string path = "metatypes.gmap";
    const auto bytes = readAsset(assets, path, 64 * 1024 * 1024);
    CityDataReader map(path, bytes);
    const auto count = map.integer();
    if (count == 0 || count > 256) throw std::runtime_error("Invalid retail spawn metatype count.");
    std::vector<std::string> names(count);
    std::map<uint8_t, CitySpawnMetatype> definitions;
    const auto lowerAscii = [](std::string value) {
        for (auto& letter : value)
            if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
        return value;
    };
    for (uint32_t id = 0; id < count; ++id) {
        auto record = map.record();
        const auto type = record.signedInteger();
        if (type == -1) continue;
        if (type == 3 || type == 5) {
            CitySpawnMetatype definition{};
            definition.objectType = type;
            readBasicObject(record, definition);
            names[id] = lowerAscii(definition.name);
            definitions.emplace(static_cast<uint8_t>(id), std::move(definition));
        } else if (type == 4) {
            CitySpawnMetatype basic{};
            readBasicObject(record, basic);
            CityWeaponMetatype weapon{basic.name, record.signedInteger(), record.signedInteger(),
                record.signedInteger(), record.signedInteger(), record.signedInteger(), record.string()};
            const auto flags = record.remainingBytes();
            if (flags.size() != 14 || flags[0] > 1 || flags[1] > 1)
                throw std::runtime_error("Invalid retail weapon metatype flags.");
            weapon.doubleWeapon = flags[0] != 0;
            weapon.twoHanded = flags[1] != 0;
            record.skip(2);
            weapon.range = record.signedInteger();
            weapon.shootFrequency = record.signedInteger();
            weapon.weaponType = record.signedInteger();
            record.finish();
            names[id] = lowerAscii(basic.name);
            city.weaponMetatypes.emplace(id, std::move(weapon));
        } else {
            names[id] = lowerAscii(record.string());
        }
    }
    const auto bindMetatype = [&](int32_t id, int32_t type) {
        if (id < 0 || id > UINT8_MAX) throw std::runtime_error("Invalid retail spawn metatype ID.");
        const auto key = static_cast<uint8_t>(id);
        const auto definition = definitions.find(key);
        if (definition == definitions.end() || definition->second.objectType != type || definition->second.model.empty())
            throw std::runtime_error("Missing retail spawn model definition for metatype " + std::to_string(id));
        city.spawnMetatypes.try_emplace(key, definition->second);
    };
    for (auto& district : city.districts) {
        for (auto& point : district.spawnPoints) {
            const auto name = lowerAscii(point.name);
            size_t longest = 0;
            for (size_t index = 0; index < names.size(); ++index) {
                const auto& prefix = names[index];
                if (prefix.size() <= longest || name.compare(0, prefix.size(), prefix) != 0) continue;
                longest = prefix.size();
                point.group = static_cast<uint8_t>(index);
            }
            if (!longest) throw std::runtime_error("Unknown retail spawn group: " + point.name);
            // SpawnPoint::Load initializes each group's table on its first occurrence.
            const auto [group, inserted] = city.spawnGroups.try_emplace(point.group, CitySpawnGroup{
                point.objectType, {}
            });
            if (!inserted) continue;
            if (point.values.empty() || point.values.size() > UINT8_MAX)
                throw std::runtime_error("Invalid metatype list for retail spawn group: " + point.name);
            for (const auto metatype : point.values) {
                if (metatype < 0 || static_cast<size_t>(metatype) >= names.size() || names[metatype].empty())
                    throw std::runtime_error("Unknown metatype in retail spawn group: " + point.name);
                const auto entityType = point.objectType == 3 || point.objectType == 11 ? 3 : 5;
                bindMetatype(metatype, entityType);
                group->second.metatypes.push_back(static_cast<uint8_t>(metatype));
            }
        }
    }
    // SpawnPoint::spawn may substitute these IDs at a nonzero wanted level.
    bindMetatype(38, 3);
    bindMetatype(48, 5);
    bindMetatype(161, 5);
}

std::vector<CityVehicleDefinition> readVehicleRecords(const std::string& path,
                                                     const std::vector<unsigned char>& bytes)
{
    CityDataReader map(path, bytes);
    const auto count = map.integer();
    std::vector<CityVehicleDefinition> vehicles;
    for (uint32_t i = 0; i < count; ++i) {
        auto record = map.record();
        if (record.integer() != 3) continue;
        CityVehicleDefinition vehicle{};
        readBasicObject(record, vehicle);

        for (uint32_t offset = 72; offset <= 224; offset += 4) {
            switch (offset) {
            case 84: case 108: case 116: case 132: case 172:
                vehicle.textFields.emplace(offset, record.string());
                break;
            default:
                vehicle.vehicleWords[(offset - 72) / 4] = record.signedInteger();
                break;
            }
        }
        // Vehicle::Load takes its default radio station from offset 96.
        const auto station = vehicle.vehicleWords[(96 - 72) / 4];
        if (station != -1) vehicle.radioStation = static_cast<uint32_t>(station);
        record.finish();
        vehicles.push_back(std::move(vehicle));
    }
    return vehicles;
}

class BresReader {
public:
    explicit BresReader(const CityScene& scene) : path(scene.path), bytes(scene.bytes) {}

    void checkSpan(uint32_t offset, uint32_t count, uint32_t stride) const
    {
        if (offset > bytes.size() || count > (bytes.size() - offset) / stride) fail();
    }

    unsigned char byte(uint32_t offset) const
    {
        checkSpan(offset, 1, 1);
        return bytes[offset];
    }

    uint16_t half(uint32_t offset) const
    {
        checkSpan(offset, 1, 2);
        return uint16_t(bytes[offset] | unsigned(bytes[offset + 1]) << 8);
    }

    uint32_t word(uint32_t offset) const
    {
        checkSpan(offset, 1, 4);
        if (offset % 4) fail();
        return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 |
               uint32_t(bytes[offset + 2]) << 16 | uint32_t(bytes[offset + 3]) << 24;
    }

    float scalar(uint32_t offset) const
    {
        const auto bits = word(offset);
        float value;
        std::memcpy(&value, &bits, sizeof value);
        if (!std::isfinite(value)) fail();
        return value;
    }

    std::string string(uint32_t offset) const
    {
        checkSpan(offset, 1, 1);
        const auto* start = reinterpret_cast<const char*>(bytes.data() + offset);
        const auto* end = static_cast<const char*>(std::memchr(start, 0, bytes.size() - offset));
        if (!end) fail();
        return std::string(start, end);
    }

    [[noreturn]] void fail() const { throw std::runtime_error("Invalid BRES scene " + path); }

private:
    const std::string& path;
    const std::vector<unsigned char>& bytes;
};

CityMesh readMesh(const BresReader& bres, uint32_t geometry)
{
    CityMesh result;
    result.name = bres.string(bres.word(geometry));
    const auto mesh = bres.word(geometry + 12);
    bres.checkSpan(mesh, 1, 44);
    // City SMesh records store their vertex streams in one interleaved buffer.
    if (bres.word(mesh) != 1) bres.fail();
    const auto vertexCount = bres.word(mesh + 4);
    const auto streams = bres.word(mesh + 8);
    const auto bufferCount = bres.word(mesh + 12);
    const auto bufferTable = bres.word(mesh + 16);
    bres.checkSpan(streams, 1, 32);
    bres.checkSpan(bufferTable, bufferCount, 60);
    if (!vertexCount || vertexCount > 65536 || !bufferCount) bres.fail();

    const auto stride = bres.word(streams);
    const auto streamCount = bres.word(streams + 4);
    const auto offsets = bres.word(streams + 8);
    const auto types = bres.word(streams + 16);
    const auto data = bres.word(streams + 28);
    const auto positionStream = bres.byte(bufferTable + 12);
    if (stride < 12 || stride % 4 || positionStream >= streamCount) bres.fail();
    bres.checkSpan(data, vertexCount, stride);
    bres.checkSpan(offsets, streamCount, 4);
    bres.checkSpan(types, streamCount, 4);
    const auto xyz = bres.word(offsets + positionStream * 4);
    if (xyz > stride - 12 || bres.word(types + positionStream * 4) != 6) bres.fail();
    result.positions.reserve(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        const auto vertex = data + i * stride + xyz;
        result.positions.push_back({bres.scalar(vertex), bres.scalar(vertex + 4), bres.scalar(vertex + 8)});
    }

    const auto normalStream = bres.byte(bufferTable + 13);
    if (normalStream != 255) {
        if (normalStream >= streamCount) bres.fail();
        const auto offset = bres.word(offsets + normalStream * 4);
        const auto type = bres.word(types + normalStream * 4);
        const uint32_t size = type == 0 ? 3 : 12;
        if ((type != 0 && type != 6) || offset > stride || size > stride - offset) bres.fail();
        result.normals.reserve(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) {
            const auto normal = data + i * stride + offset;
            if (type == 0) {
                const auto component = [&](uint32_t at) {
                    const int bits = bres.byte(at);
                    const int value = bits >= 128 ? bits - 256 : bits;
                    return std::max(-1.f, value / 127.f);
                };
                result.normals.push_back({component(normal), component(normal + 1), component(normal + 2)});
            } else {
                result.normals.push_back({bres.scalar(normal), bres.scalar(normal + 4), bres.scalar(normal + 8)});
            }
        }
    }

    const auto readTexCoords = [&](uint8_t texCoordStream, auto& coordinates) {
        if (texCoordStream == 255) return;
        if (texCoordStream >= streamCount) bres.fail();
        const auto offset = bres.word(offsets + texCoordStream * 4);
        const auto type = bres.word(types + texCoordStream * 4);
        const uint32_t size = type == 2 ? 4 : 8;
        if ((type != 2 && type != 6) || offset > stride || size > stride - offset) bres.fail();
        std::array<float, 2> scale{}, bias{};
        if (type == 2) {
            const auto transforms = bres.word(streams + 24);
            bres.checkSpan(transforms, streamCount, 4);
            const auto transform = bres.word(transforms + texCoordStream * 4);
            bres.checkSpan(transform, 1, 24);
            for (uint32_t i = 0; i < 2; ++i) {
                scale[i] = bres.scalar(transform + i * 4);
                bias[i] = bres.scalar(transform + 12 + i * 4);
            }
        }
        coordinates.reserve(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) {
            const auto uv = data + i * stride + offset;
            if (type == 2) {
                const auto signedShort = [&](uint32_t at) {
                    const auto bits = bres.half(at);
                    return bits >= 32768 ? int(bits) - 65536 : int(bits);
                };
                coordinates.push_back({signedShort(uv) * scale[0] + bias[0],
                                       signedShort(uv + 2) * scale[1] + bias[1]});
            } else {
                coordinates.push_back({bres.scalar(uv), bres.scalar(uv + 4)});
            }
        }
    };
    const auto texCoordStream = bres.byte(bufferTable + 16);
    const auto auxiliaryStream = bres.byte(bufferTable + 19);
    readTexCoords(texCoordStream, result.texCoords);
    // CColladaMeshBuffer binds the auxiliary coordinate array from byte 19.
    readTexCoords(auxiliaryStream, result.auxiliaryTexCoords);

    const auto readColors = [&](uint8_t colorStream, auto& colors) {
        if (colorStream == 255) return;
        if (colorStream >= streamCount) bres.fail();
        const auto offset = bres.word(offsets + colorStream * 4);
        if (offset > stride - 4 || bres.word(types + colorStream * 4) != 1) bres.fail();
        colors.reserve(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) {
            const auto color = data + i * stride + offset;
            colors.push_back({bres.byte(color + 2) / 255.f, bres.byte(color + 1) / 255.f,
                              bres.byte(color) / 255.f, bres.byte(color + 3) / 255.f});
        }
    };
    // CColladaMeshBuffer binds byte 22 to its color-0 accessor.
    const auto colorStream = bres.byte(bufferTable + 22);
    const auto secondaryColorStream = bres.byte(bufferTable + 23);
    readColors(colorStream, result.colors);
    readColors(secondaryColorStream, result.secondaryColors);

    result.buffers.reserve(bufferCount);
    for (uint32_t i = 0; i < bufferCount; ++i) {
        const auto buffer = bufferTable + i * 60;
        const auto indexCount = bres.word(buffer + 24);
        const auto indices = bres.word(buffer + 28);
        if (bres.word(buffer) != 0 || bres.byte(buffer + 12) != positionStream ||
            bres.byte(buffer + 13) != normalStream || bres.byte(buffer + 16) != texCoordStream ||
            bres.byte(buffer + 19) != auxiliaryStream || bres.byte(buffer + 22) != colorStream ||
            bres.byte(buffer + 23) != secondaryColorStream ||
            !indexCount || indexCount % 3 || bres.word(buffer + 8) != indexCount / 3)
            bres.fail();
        bres.checkSpan(indices, indexCount, 2);
        CityMeshBuffer decoded;
        if (const auto material = bres.word(buffer + 4)) decoded.material = bres.string(material);
        decoded.indices.reserve(indexCount);
        for (uint32_t j = 0; j < indexCount; ++j) {
            const auto index = bres.half(indices + j * 2);
            if (index >= vertexCount) bres.fail();
            decoded.indices.push_back(index);
        }
        const auto [first, last] = std::minmax_element(decoded.indices.begin(), decoded.indices.end());
        if (*first != bres.half(buffer + 32) || *last != bres.half(buffer + 34)) bres.fail();
        result.buffers.push_back(std::move(decoded));
    }
    return result;
}

std::string readFragment(const BresReader& bres, uint32_t offset)
{
    auto url = bres.string(offset);
    if (url.size() < 2 || url.front() != '#') bres.fail();
    return url.substr(1);
}

void readMaterials(const BresReader& bres, CityScene& scene)
{
    const auto root = scene.rootOffset;
    const auto imageCount = bres.word(root + 52);
    const auto imageTable = bres.word(root + 56);
    const auto effectCount = bres.word(root + 60);
    const auto effectTable = bres.word(root + 64);
    const auto materialCount = bres.word(root + 68);
    const auto materialTable = bres.word(root + 72);
    bres.checkSpan(imageTable, imageCount, 20);
    bres.checkSpan(effectTable, effectCount, 88);
    bres.checkSpan(materialTable, materialCount, 64);
    scene.images.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        const auto uri = bres.word(imageTable + i * 20 + 8);
        scene.images.push_back(uri ? bres.string(uri) : std::string{});
    }
    std::map<std::string, CityMaterial> effects;
    for (uint32_t i = 0; i < effectCount; ++i) {
        const auto effect = effectTable + i * 88;
        CityMaterial material;
        const auto diffuse = bres.word(effect + 20);
        const auto type = bres.byte(effect + 16);
        if (diffuse && type == 0) {
            material.diffuse = bres.word(diffuse);
        } else if (diffuse && type == 1) {
            const auto textureCount = bres.word(effect + 80);
            const auto textures = bres.word(effect + 84);
            bres.checkSpan(textures, textureCount, 4);
            for (uint32_t slot = 0; slot < textureCount; ++slot) {
                const auto image = bres.word(textures + slot * 4);
                if (image >= imageCount) bres.fail();
                if (slot == 0) material.texture = scene.images[image];
                else material.additionalTextures.push_back(scene.images[image]);
            }
            material.alphaTest = textureCount != 0 && bres.byte(effect + 48) != 0;
        }
        effects.emplace(bres.string(bres.word(effect)), std::move(material));
    }
    for (uint32_t i = 0; i < materialCount; ++i) {
        const auto material = materialTable + i * 64;
        const auto target = bres.word(material + 12);
        if (!target) continue;
        const auto effect = effects.find(readFragment(bres, target));
        if (effect != effects.end()) scene.materials.emplace(bres.string(bres.word(material)), effect->second);
    }
}

void readNodes(const BresReader& bres, CityScene& scene, uint32_t count, uint32_t table,
               int32_t parent, unsigned depth, const std::map<std::string, std::string>& controllers,
               const std::set<std::string>& splines)
{
    constexpr uint32_t nodeSize = 80;
    constexpr size_t maxNodes = 65536;
    if (depth > 64 || count > maxNodes - scene.nodes.size()) bres.fail();
    bres.checkSpan(table, count, nodeSize);
    for (uint32_t i = 0; i < count; ++i) {
        const auto node = table + i * nodeSize;
        CityNode result{};
        result.name = bres.string(bres.word(node));
        if (!controllers.empty()) {
            if (const auto joint = bres.word(node + 8)) result.jointName = bres.string(joint);
        }
        result.parent = parent;
        // SNode points to a user-property record whose first field is the text pointer.
        if (const auto properties = bres.word(node + 72)) {
            if (const auto text = bres.word(properties)) result.userProperties = bres.string(text);
        }
        for (uint32_t j = 0; j < 3; ++j) {
            result.translation[j] = bres.scalar(node + 12 + j * 4);
            result.scale[j] = bres.scalar(node + 40 + j * 4);
        }
        for (uint32_t j = 0; j < 4; ++j)
            result.rotation[j] = bres.scalar(node + 24 + j * 4);

        const auto instanceCount = bres.word(node + 64);
        const auto instances = bres.word(node + 68);
        bres.checkSpan(instances, instanceCount, 8);
        for (uint32_t j = 0; j < instanceCount; ++j) {
            const auto instance = instances + j * 8;
            const auto kind = bres.word(instance);
            if (kind == 9) { // SInstanceEmitter
                const auto reference = bres.word(instance + 4);
                bres.checkSpan(reference, 1, 8);
                result.particleEmitters.push_back(readFragment(bres, bres.word(reference + 4)));
                continue;
            }
            if (kind != 3 && (kind != 2 || controllers.empty())) continue;
            const auto reference = bres.word(instance + 4);
            bres.checkSpan(reference, 1, 20);
            CityGeometryRef geometry;
            if (const auto library = bres.word(reference)) geometry.sourcePath = bres.string(library);
            geometry.meshName = readFragment(bres, bres.word(reference + 4));
            if (kind == 3 && geometry.sourcePath.empty() && splines.count(geometry.meshName)) continue;
            if (kind == 2) {  // SInstanceController
                // Follow skin and morph sources to the bind-pose mesh.
                size_t remaining = controllers.size();
                auto source = controllers.find(geometry.meshName);
                while (source != controllers.end()) {
                    if (!remaining--) bres.fail();
                    geometry.meshName = source->second;
                    source = controllers.find(geometry.meshName);
                }
            }
            const auto materialCount = bres.word(reference + 12);
            const auto materialTable = bres.word(reference + 16);
            bres.checkSpan(materialTable, materialCount, 20);
            for (uint32_t k = 0; k < materialCount; ++k) {
                const auto material = materialTable + k * 20;
                const auto source = bres.word(material);
                geometry.materials.push_back({source ? bres.string(source) : std::string{},
                    readFragment(bres, bres.word(material + 4))});
            }
            if (geometry.sourcePath.empty() &&
                std::none_of(scene.meshes.begin(), scene.meshes.end(), [&](const CityMesh& mesh) {
                    return mesh.name == geometry.meshName;
                }))
                bres.fail();
            result.geometry.push_back(std::move(geometry));
        }
        if (scene.nodes.size() == maxNodes) bres.fail();
        const auto index = static_cast<int32_t>(scene.nodes.size());
        scene.nodes.push_back(std::move(result));
        const auto children = bres.word(node + 56);
        if (children) readNodes(bres, scene, children, bres.word(node + 60), index, depth + 1, controllers, splines);
    }
}

void readMorphController(const BresReader& bres, CityScene& scene, uint32_t data,
                         const std::string& name, const std::string& source)
{
    bres.checkSpan(data, 1, 32);
    const auto count = bres.word(data + 8);
    const auto names = bres.word(data + 12);
    const auto indices = bres.word(data + 20);
    const auto weights = bres.word(data + 28);
    if (count != bres.word(data + 16) || count != bres.word(data + 24)) bres.fail();
    bres.checkSpan(names, count, 4);
    bres.checkSpan(indices, count, 4);
    bres.checkSpan(weights, count, 4);
    const auto geometryCount = bres.word(scene.rootOffset + 76);
    const auto geometryTable = bres.word(scene.rootOffset + 80);
    CityMorphController morph{name, source, bres.word(data + 4), {}};
    for (uint32_t i = 0; i < count; ++i) {
        const auto index = bres.word(indices + i * 4);
        if (index >= geometryCount) bres.fail();
        const auto geometry = geometryTable + index * 16;
        const auto targetName = bres.string(bres.word(names + i * 4));
        if (bres.word(geometry + 8) != 0 || bres.string(bres.word(geometry)) != targetName) bres.fail();
        const auto mesh = std::find_if(scene.meshes.begin(), scene.meshes.end(),
            [&](const CityMesh& candidate) { return candidate.name == targetName; });
        if (mesh == scene.meshes.end()) bres.fail();
        morph.targets.push_back({static_cast<size_t>(mesh - scene.meshes.begin()), bres.scalar(weights + i * 4)});
    }
    scene.morphControllers.push_back(std::move(morph));
}

void readVisualScenes(const BresReader& bres, CityScene& scene, bool includeControllers,
                      const std::set<std::string>& splines)
{
    const auto root = scene.rootOffset;
    bres.checkSpan(root, 1, 124);
    std::map<std::string, std::string> controllers;
    if (includeControllers) {
        const auto count = bres.word(root + 84);
        const auto table = bres.word(root + 88);
        bres.checkSpan(table, count, 12);
        for (uint32_t i = 0; i < count; ++i) {
            const auto record = table + i * 12;
            const auto type = bres.word(record);
            if (type > 1) bres.fail();
            const auto name = bres.string(bres.word(record + 4));
            const auto data = bres.word(record + 8);
            const auto source = readFragment(bres, bres.word(data));
            controllers.emplace(name, source);
            if (type == 1) readMorphController(bres, scene, data, name, source);
        }
    }
    const auto sceneCount = bres.word(root + 108);
    const auto sceneTable = bres.word(root + 112);
    const auto instanceCount = bres.word(root + 116);
    const auto instanceTable = bres.word(root + 120);
    bres.checkSpan(sceneTable, sceneCount, 16);
    bres.checkSpan(instanceTable, instanceCount, 8);
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const auto instance = instanceTable + i * 8;
        if (bres.word(instance) != 6) continue;  // SInstanceVisualScene
        const auto reference = bres.word(instance + 4);
        bres.checkSpan(reference, 1, 8);
        const auto name = readFragment(bres, bres.word(reference + 4));
        uint32_t j = 0;
        for (; j < sceneCount; ++j) {
            const auto record = sceneTable + j * 16;
            if (bres.string(bres.word(record)) != name) continue;
            readNodes(bres, scene, bres.word(record + 8), bres.word(record + 12), -1, 0, controllers, splines);
            break;
        }
        if (j == sceneCount) bres.fail();
    }
}

CityScene loadBres(FileSystemBase& assets, const std::string& path)
{
    CityScene scene{path, readAsset(assets, path, 64 * 1024 * 1024)};
    const auto& bytes = scene.bytes;
    const BresReader bres(scene);
    if (bytes.size() < 32 || std::memcmp(bytes.data(), "BRES", 4) != 0)
        bres.fail();
    if (bres.word(4) != 0xfffe || bres.word(8) != 32 || bres.word(12) != bytes.size()) bres.fail();
    const auto relocationCount = bres.word(16);
    const auto relocationOffset = bres.word(20);
    const auto dataOffset = bres.word(24);
    scene.rootOffset = bres.word(28);
    if (relocationOffset != 32 || relocationCount > (bytes.size() - relocationOffset) / 4 ||
        dataOffset != relocationOffset + relocationCount * 4 ||
        scene.rootOffset < dataOffset || scene.rootOffset >= bytes.size())
        bres.fail();
    for (uint32_t i = 0; i < relocationCount; ++i) {
        const auto field = bres.word(relocationOffset + i * 4);
        if ((field < dataOffset && field != 20 && field != 24 && field != 28) ||
            bres.word(field) >= bytes.size())
            bres.fail();
    }
    return scene;
}

void readParticleEmitters(const BresReader& bres, CityScene& scene, bool standaloneDiffuse = false)
{
    const auto root = scene.rootOffset;
    bres.checkSpan(root, 1, 100);
    const auto count = bres.word(root + 92);
    if (!count && !standaloneDiffuse) return;
    const auto table = bres.word(root + 96);
    // CColladaDatabase::getEmitter indexes this table in 144-byte records.
    bres.checkSpan(table, count, 144);
    for (uint32_t i = 0; i < count; ++i) {
        const auto row = table + i * 144;
        CityParticleEmitter emitter{
            bres.string(bres.word(row)), bres.word(row + 8), bres.word(row + 24), {},
            bres.scalar(row + 32), bres.scalar(row + 40), bres.scalar(row + 44),
            bres.scalar(row + 48), bres.scalar(row + 52), bres.scalar(row + 56),
            bres.scalar(row + 60), bres.scalar(row + 64), bres.scalar(row + 68), {}
        };
        if (emitter.name.empty()) bres.fail();
        if (emitter.type <= 2) {
            const auto dimensions = bres.word(row + 12);
            if (!dimensions) bres.fail();
            if (emitter.type == 0) {
                bres.checkSpan(dimensions, 1, 12);
                emitter.dimensions = {bres.scalar(dimensions), bres.scalar(dimensions + 4),
                                      bres.scalar(dimensions + 8)};
            } else {
                bres.checkSpan(dimensions, 1, emitter.type == 1 ? 4 : 8);
                emitter.radius = bres.scalar(dimensions);
                if (emitter.type == 2) emitter.height = bres.scalar(dimensions + 4);
            }
        }
        // initParticleSystem assigns Direction and DirectionVariation from the motion block.
        const auto motionType = bres.word(row + 72);
        const auto motion = bres.word(row + 76);
        if (motionType == 1) {
            if (!motion) bres.fail();
            bres.checkSpan(motion, 1, 16);
            emitter.direction = {bres.scalar(motion), bres.scalar(motion + 4), bres.scalar(motion + 8)};
            emitter.directionVariation = bres.scalar(motion + 12);
        } else if (motionType == 2) {
            if (!motion) bres.fail();
            bres.checkSpan(motion, 1, 8);
            emitter.directionVariation = bres.scalar(motion + 4);
        }
        emitter.animationOffset = bres.scalar(row + 92);
        emitter.animationOffsetVariation = bres.scalar(row + 96);
        emitter.animationMapping = bres.word(row + 100);
        emitter.animationLength = bres.scalar(row + 104);
        emitter.animationScale = bres.scalar(row + 112);
        emitter.animationScaleVariation = bres.scalar(row + 116);
        scene.particleEmitters.push_back(std::move(emitter));
    }

    const auto animationCount = bres.word(root + 16);
    const auto animations = bres.word(root + 20);
    bres.checkSpan(animations, animationCount, 36);
    for (uint32_t i = 0; i < animationCount; ++i) {
        const auto row = animations + i * 36;
        const auto channelCount = bres.word(row + 20);
        const auto channels = bres.word(row + 24);
        bres.checkSpan(channels, channelCount, 16);
        for (uint32_t j = 0; j < channelCount; ++j) {
            const auto channel = channels + j * 16;
            const auto target = bres.string(bres.word(channel + 4));
            const auto emitter = std::find_if(scene.particleEmitters.begin(), scene.particleEmitters.end(),
                [&](const CityParticleEmitter& entry) { return entry.name == target; });
            const auto property = bres.word(channel + 8);
            const auto parameter = static_cast<CityParticleParameter>(property);
            const bool diffuse = (property == 21 || property == 25) &&
                bres.string(bres.word(row)) == "__irrlicht_Diffuse_color";
            if (!diffuse && emitter == scene.particleEmitters.end()) continue;
            if (!diffuse) switch (parameter) {
            case CityParticleParameter::birthRate:
            case CityParticleParameter::targetSize:
            case CityParticleParameter::sizeVariation:
            case CityParticleParameter::speedVariation:
            case CityParticleParameter::animationLength:
                break;
            default: continue;
            }
            const auto sampler = bres.word(row + 16);
            const auto sourceCount = bres.word(row + 4);
            const auto sources = bres.word(row + 8);
            if (bres.word(row + 12) != 1) bres.fail();
            bres.checkSpan(sampler, 1, 20);
            bres.checkSpan(sources, sourceCount, 12);
            const auto input = bres.word(sampler + 4);
            const auto output = bres.word(sampler + 8);
            if (input >= sourceCount || output >= sourceCount) bres.fail();
            const auto time = sources + input * 12;
            const auto value = sources + output * 12;
            const auto keyCount = bres.word(time + 4);
            const auto times = bres.word(time + 8);
            const auto values = bres.word(value + 8);
            if (diffuse) {
                const uint32_t bytesPerKey = property == 21 ? 4 : 1;
                if (!keyCount || bres.word(time) != 4 || bres.word(value) != 1 ||
                    bres.word(value + 4) != uint64_t(keyCount) * bytesPerKey ||
                    !scene.diffuseColors.empty()) bres.fail();
                bres.checkSpan(times, keyCount, 4);
                bres.checkSpan(values, keyCount, bytesPerKey);
                for (uint32_t k = 0; k < keyCount; ++k) {
                    const auto ms = bres.word(times + k * 4);
                    if (ms > INT32_MAX || (!scene.diffuseColors.empty() &&
                        ms <= scene.diffuseColors.back().timeMs)) bres.fail();
                    const auto at = values + k * bytesPerKey;
                    std::array<float, 4> rgba{1, 1, 1, bres.byte(at + bytesPerKey - 1) / 255.f};
                    if (bytesPerKey == 4)
                        for (uint32_t c = 0; c < 3; ++c) rgba[c] = bres.byte(at + 2 - c) / 255.f;
                    scene.diffuseColors.push_back({ms, rgba});
                }
                continue;
            }
            if (!keyCount || bres.word(time) != 4 || bres.word(value) != 6 ||
                bres.word(value + 4) != keyCount) bres.fail();
            bres.checkSpan(times, keyCount, 4);
            bres.checkSpan(values, keyCount, 4);
            auto [track, inserted] = emitter->tracks.try_emplace(parameter);
            if (!inserted) bres.fail();
            auto& keys = track->second;
            keys.reserve(keyCount);
            for (uint32_t k = 0; k < keyCount; ++k) {
                const auto ms = bres.word(times + k * 4);
                if (ms > INT32_MAX || (!keys.empty() && ms <= keys.back().timeMs)) bres.fail();
                keys.push_back({ms, bres.scalar(values + k * 4)});
            }
        }
    }
}

CityNavigation loadNavigation(FileSystemBase& assets, const CityMesh& heightMesh)
{
    const std::string path = "nav_mesh_arrays.bin";
    const auto bytes = readAsset(assets, path, 64 * 1024 * 1024);
    size_t offset = 0;
    const auto read = [&](unsigned size) {
        if (size > bytes.size() - offset) throw std::runtime_error("Truncated retail navigation arrays.");
        uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i) value |= uint32_t(bytes[offset++]) << (8 * i);
        return value;
    };
    const auto count = read(4);
    if (count != heightMesh.buffers.size())
        throw std::runtime_error("Retail navigation buffer count does not match the heightmap.");
    CityNavigation navigation;
    navigation.buffers.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const auto indices = read(2);
        if (indices != heightMesh.buffers[i].indices.size())
            throw std::runtime_error("Retail navigation index count does not match the heightmap.");
        navigation.buffers[i].roadFlags.resize(indices / 3);
    }
    // PhysicAttributes::LoadArrays reads eight neighbor arrays per buffer,
    // followed by a separate pass over the byte-per-triangle flags.
    for (auto& buffer : navigation.buffers) {
        for (auto& lane : buffer.neighbors) {
            lane.resize(buffer.roadFlags.size());
            for (auto& value : lane) value = static_cast<uint16_t>(read(2));
        }
    }
    for (auto& buffer : navigation.buffers)
        for (auto& flags : buffer.roadFlags) flags = static_cast<uint8_t>(read(1));
    for (auto& buffer : navigation.materialBuffers) {
        buffer = read(4);
        if (buffer >= count) throw std::runtime_error("Invalid retail navigation material buffer.");
    }
    const auto readCliffMap = [&](std::vector<uint16_t>& map) {
        map.resize(read(2));
        for (auto& value : map) value = static_cast<uint16_t>(read(2));
    };
    readCliffMap(navigation.cliffVertexKeys);
    readCliffMap(navigation.cliffTriangles);
    if (navigation.cliffVertexKeys.size() != heightMesh.positions.size() * 2)
        throw std::runtime_error("Retail cliff keys do not match the heightmap vertices.");
    const auto cliffBuffer = navigation.materialBuffers[static_cast<size_t>(CityNavigation::Material::cliff)];
    for (size_t key = 0; key < navigation.cliffVertexKeys.size(); key += 2) {
        const size_t start = navigation.cliffVertexKeys[key], length = navigation.cliffVertexKeys[key + 1];
        if (start + length > navigation.cliffTriangles.size())
            throw std::runtime_error("Invalid retail cliff face range.");
    }
    for (const auto index : navigation.cliffTriangles) {
        if (index != UINT16_MAX && (index % 3 || index >= heightMesh.buffers[cliffBuffer].indices.size()))
            throw std::runtime_error("Invalid retail cliff face offset.");
    }
    for (auto& buffer : navigation.buffers) {
        buffer.overlaps.resize(buffer.roadFlags.size());
        for (auto& overlaps : buffer.overlaps) {
            const auto words = read(2);
            if ((words & 1) || words > (bytes.size() - offset) / 2)
                throw std::runtime_error("Invalid retail navigation overlap count.");
            overlaps.reserve(words / 2);
            for (uint32_t word = 0; word < words; word += 2) {
                const auto targetBuffer = read(2), indexOffset = read(2);
                if (targetBuffer >= count || indexOffset % 3 ||
                    indexOffset >= heightMesh.buffers[targetBuffer].indices.size())
                    throw std::runtime_error("Invalid retail navigation overlap target.");
                overlaps.push_back({static_cast<uint16_t>(targetBuffer), static_cast<uint16_t>(indexOffset)});
            }
        }
    }
    if (offset != bytes.size()) throw std::runtime_error("Unexpected data after retail navigation arrays.");
    for (size_t buffer = 0; buffer < navigation.buffers.size(); ++buffer) {
        for (size_t triangle = 0; triangle < navigation.buffers[buffer].roadFlags.size(); ++triangle) {
            const CityNavigation::TriangleRef source{static_cast<uint16_t>(buffer),
                                                     static_cast<uint16_t>(triangle * 3)};
            for (size_t edge = 0; edge < 3; ++edge) navigation.neighbor(source, edge);
        }
    }
    return navigation;
}

CityScene loadScene(FileSystemBase& assets, const std::string& path, bool includeNodes = true, bool includeControllers = false)
{
    if (path.empty()) return {};
    auto scene = loadBres(assets, path);
    const BresReader bres(scene);
    // SCollada stores an irr::res::vector<SGeometry> at offset 76.
    bres.checkSpan(scene.rootOffset, 1, 84);
    const auto geometryCount = bres.word(scene.rootOffset + 76);
    const auto geometryTable = bres.word(scene.rootOffset + 80);
    bres.checkSpan(geometryTable, geometryCount, 16);
    scene.meshes.reserve(geometryCount);
    std::set<std::string> splines;
    for (uint32_t i = 0; i < geometryCount; ++i) {
        const auto geometry = geometryTable + i * 16;
        const auto type = bres.word(geometry + 8);
        if (type == 0) scene.meshes.push_back(readMesh(bres, geometry));
        else if (type == 1) splines.insert(bres.string(bres.word(geometry)));
        else bres.fail();
    }
    readMaterials(bres, scene);
    readParticleEmitters(bres, scene);
    if (includeNodes) readVisualScenes(bres, scene, includeControllers, splines);
    return scene;
}

int32_t signedBresWord(const BresReader& bres, uint32_t offset)
{
    const auto bits = bres.word(offset);
    int32_t value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

CityAnimationClip readClip(const CityScene& library, const CityScene* model, const std::string& name,
                           bool allowEmpty = false)
{
    const BresReader bres(library);
    const auto root = library.rootOffset;
    bres.checkSpan(root, 1, 36);
    const auto clipCount = bres.word(root + 28);
    const auto clips = bres.word(root + 32);
    bres.checkSpan(clips, clipCount, 12);
    int32_t begin = 0, end = 0;
    for (uint32_t i = 0; i < clipCount; ++i) {
        const auto clip = clips + i * 12;
        // The script's crackingHands cue differs in case from the animation library.
        if (_stricmp(bres.string(bres.word(clip)).c_str(), name.c_str()) == 0) {
            begin = signedBresWord(bres, clip + 4);
            end = signedBresWord(bres, clip + 8);
            break;
        }
    }
    if (end <= begin) throw std::runtime_error("Missing or empty retail animation clip " + name);

    CityAnimationClip result{name, static_cast<uint32_t>(int64_t(end) - begin), {}};
    const auto animations = bres.word(root + 20);
    const auto animationCount = bres.word(root + 16);
    bres.checkSpan(animations, animationCount, 36);
    for (uint32_t i = 0; i < animationCount; ++i) {
        const auto animation = animations + i * 36;
        // The in-game library stores one sampler and one target per animation.
        if (bres.word(animation + 12) != 1 || bres.word(animation + 20) != 1 ||
            bres.word(animation + 28) != 0) bres.fail();
        const auto channel = bres.word(animation + 24);
        bres.checkSpan(channel, 1, 16);
        const auto target = bres.string(bres.word(channel + 4));
        std::optional<size_t> nodeIndex;
        if (model) {
            const auto node = std::find_if(model->nodes.begin(), model->nodes.end(),
                                           [&](const CityNode& entry) { return entry.name == target; });
            if (node == model->nodes.end()) continue;
            nodeIndex = static_cast<size_t>(node - model->nodes.begin());
        }
        const auto transform = bres.word(channel + 8);
        CityAnimationTransform property;
        uint32_t components;
        switch (transform) {
        case 1: property = CityAnimationTransform::translation; components = 3; break;
        case 4: property = CityAnimationTransform::translationZ; components = 1; break;
        case 5: property = CityAnimationTransform::rotation; components = 4; break;
        case 10: property = CityAnimationTransform::scale; components = 3; break;
        default: continue;
        }
        const auto sampler = bres.word(animation + 16);
        bres.checkSpan(sampler, 1, 20);
        const auto sourceCount = bres.word(animation + 4);
        const auto sources = bres.word(animation + 8);
        bres.checkSpan(sources, sourceCount, 12);
        const auto input = bres.word(sampler + 4);
        const auto output = bres.word(sampler + 8);
        if (input >= sourceCount || output >= sourceCount) bres.fail();
        const auto time = sources + input * 12;
        const auto value = sources + output * 12;
        if (bres.word(time) != 4 || bres.word(value) != 6) bres.fail();
        const auto count = bres.word(time + 4);
        const auto times = bres.word(time + 8);
        const auto values = bres.word(value + 8);
        bres.checkSpan(times, count, 4);
        bres.checkSpan(values, count, components * 4);
        if (bres.word(value + 4) != count * components) bres.fail();

        CityAnimationChannel track{nodeIndex, property, {}, target};
        for (uint32_t k = 0; k < count; ++k) {
            const auto ms = signedBresWord(bres, times + k * 4);
            if (ms < begin) continue;
            if (ms > end) break;
            CityAnimationKey key{static_cast<uint32_t>(int64_t(ms) - begin), {}};
            if (!track.keys.empty() && key.timeMs <= track.keys.back().timeMs) bres.fail();
            for (uint32_t c = 0; c < components; ++c)
                key.value[c] = bres.scalar(values + (k * components + c) * 4);
            track.keys.push_back(key);
        }
        // Some retail clips have sparse channels. The sampler holds the nearest
        // in-range key when a channel does not key the exact clip boundary.
        if (!track.keys.empty()) {
            result.channels.push_back(std::move(track));
        }
    }
    if (result.channels.empty() && !allowEmpty)
        throw std::runtime_error("No matching animation tracks for " + name);
    return result;
}

std::vector<CitySkin> readActorSkins(const CityScene& model)
{
    const BresReader bres(model);
    const auto count = bres.word(model.rootOffset + 84);
    const auto table = bres.word(model.rootOffset + 88);
    bres.checkSpan(table, count, 12);
    std::vector<CitySkin> skins;
    for (uint32_t i = 0; i < count; ++i) {
        const auto controller = table + i * 12;
        if (bres.word(controller) != 0) continue;  // SController::Skin
        const auto data = bres.word(controller + 8);
        bres.checkSpan(data, 1, 108);
        CitySkin skin{};
        skin.meshName = readFragment(bres, bres.word(data));
        // Vehicle skins can target a morph controller rather than a base mesh.
        // Follow the controller to the mesh used for bind-pose geometry.
        for (uint32_t remaining = count; remaining > 0; --remaining) {
            bool found = false;
            for (uint32_t j = 0; j < count; ++j) {
                const auto source = table + j * 12;
                if (bres.word(source) != 1 || bres.string(bres.word(source + 4)) != skin.meshName) continue;
                skin.meshName = readFragment(bres, bres.word(bres.word(source + 8)));
                found = true;
                break;
            }
            if (!found) break;
            if (remaining == 1) bres.fail();
        }
        const auto mesh = std::find_if(model.meshes.begin(), model.meshes.end(),
                                       [&](const CityMesh& entry) { return entry.name == skin.meshName; });
        if (mesh == model.meshes.end()) bres.fail();
        for (uint32_t component = 0; component < skin.bindShape.size(); ++component)
            skin.bindShape[component] = bres.scalar(data + 4 + component * 4);

        const auto jointCount = bres.word(data + 68);
        const auto joints = bres.word(data + 72);
        const auto inverseBinds = bres.word(data + 80);
        bres.checkSpan(joints, jointCount, 4);
        bres.checkSpan(inverseBinds, jointCount, 64);
        if (bres.word(data + 76) != jointCount * 16) bres.fail();
        for (uint32_t j = 0; j < jointCount; ++j) {
            const auto name = bres.string(bres.word(joints + j * 4));
            const auto node = std::find_if(model.nodes.begin(), model.nodes.end(),
                                           [&](const CityNode& entry) { return entry.jointName == name; });
            if (node == model.nodes.end()) bres.fail();
            CitySkinJoint joint{static_cast<size_t>(node - model.nodes.begin()), {}};
            for (uint32_t component = 0; component < joint.inverseBind.size(); ++component)
                joint.inverseBind[component] = bres.scalar(inverseBinds + j * 64 + component * 4);
            skin.joints.push_back(joint);
        }

        const auto weightCount = bres.word(data + 84);
        const auto weights = bres.word(data + 88);
        const auto vertexCount = bres.word(data + 92);
        const auto counts = bres.word(data + 96);
        const auto indexCount = bres.word(data + 100);
        const auto indices = bres.word(data + 104);
        bres.checkSpan(weights, weightCount, 4);
        bres.checkSpan(counts, vertexCount, 1);
        bres.checkSpan(indices, indexCount, 2);
        if (vertexCount != mesh->positions.size()) bres.fail();
        skin.vertices.reserve(vertexCount);
        uint32_t index = 0;
        for (uint32_t v = 0; v < vertexCount; ++v) {
            const auto influences = bres.byte(counts + v);
            if (!influences || influences > (indexCount - index) / 2) bres.fail();
            auto& vertex = skin.vertices.emplace_back();
            float total = 0;
            for (uint32_t k = 0; k < influences; ++k) {
                const auto joint = bres.half(indices + index * 2);
                const auto weightIndex = bres.half(indices + (index + 1) * 2);
                if (joint >= jointCount || weightIndex >= weightCount) bres.fail();
                const auto weight = bres.scalar(weights + weightIndex * 4);
                if (weight < 0 || weight > 1) bres.fail();
                vertex.push_back({joint, weight});
                total += weight;
                index += 2;
            }
            if (std::abs(total - 1) > 0.001f) bres.fail();
        }
        if (index != indexCount) bres.fail();
        skins.push_back(std::move(skin));
    }
    return skins;
}
}

void validateBresContainer(FileSystemBase& assets, const std::string& path)
{
    loadBres(assets, path);
}

CityScene loadCityScene(FileSystemBase& assets, const std::string& path)
{
    return loadScene(assets, path, true, true);
}

std::vector<CityHighlight> loadCityHighlights(FileSystemBase& assets)
{
    const auto entries = readHighlights(assets);
    std::vector<CityHighlight> highlights;
    highlights.reserve(entries.size());
    for (const auto& entry : entries) {
        auto model = loadCityScene(assets, entry.model);
        auto animation = readClip(model, &model, "AUTO_SceneAnims_Range");
        highlights.push_back({std::move(model), std::move(animation), entry.icon});
    }
    return highlights;
}

CityAnimationClip readCityParticleClip(const CityScene& scene)
{
    return readClip(scene, &scene, "AUTO_SceneAnims_Range", true);
}

std::vector<CityAnimationClip> loadCityDoorClips(FileSystemBase& assets, const std::string& path)
{
    const auto library = loadBres(assets, path);
    const BresReader bres(library);
    bres.checkSpan(library.rootOffset, 1, 36);
    const auto count = bres.word(library.rootOffset + 28);
    const auto clips = bres.word(library.rootOffset + 32);
    bres.checkSpan(clips, count, 12);
    if (!count) bres.fail();
    std::vector<CityAnimationClip> result;
    result.reserve(count);
    std::set<std::string> names;
    for (uint32_t i = 0; i < count; ++i) {
        const auto name = bres.string(bres.word(clips + i * 12));
        if (!names.insert(name).second) bres.fail();
        result.push_back(readClip(library, nullptr, name));
    }
    return result;
}

void bindCityAnimationTracks(std::vector<CityAnimationClip>& clips, const CityScene& model)
{
    for (auto& clip : clips) {
        for (auto& channel : clip.channels) {
            const auto node = std::find_if(model.nodes.begin(), model.nodes.end(),
                [&](const CityNode& entry) { return entry.name == channel.target; });
            channel.node.reset();
            if (node != model.nodes.end()) channel.node = static_cast<size_t>(node - model.nodes.begin());
        }
    }
}

const CityScene& cityGeometryLibrary(const CityAssets& city, const CityScene& scene,
                                     const CityGeometryRef& instance)
{
    return instance.sourcePath.empty() ? scene : city.geometryLibraries.at(instance.sourcePath);
}

CityMaterial resolveCityMaterial(const CityAssets& city, const CityScene& scene,
                                  const CityScene& meshLibrary, const CityGeometryRef& instance,
                                  const CityMesh& mesh, size_t buffer)
{
    const auto* binding = buffer < instance.materials.size() ? &instance.materials[buffer] : nullptr;
    const auto& name = binding ? binding->name : mesh.buffers.at(buffer).material;
    const auto& materials = !binding ? meshLibrary.materials : binding->sourcePath.empty()
        ? scene.materials : city.geometryLibraries.at(binding->sourcePath).materials;
    const auto found = materials.find(name);
    return found == materials.end() ? CityMaterial{} : found->second;
}

std::vector<CityVehicleDefinition> loadCityVehicleMetatypes(FileSystemBase& assets)
{
    const std::string path = "metatypes.gmap";
    return readVehicleRecords(path, readAsset(assets, path, 64 * 1024 * 1024));
}

std::optional<std::string_view> CityNode::property(std::string_view key) const
{
    const auto trim = [](std::string_view value) {
        constexpr std::string_view whitespace = " \t\r";
        const auto first = value.find_first_not_of(whitespace);
        if (first == std::string_view::npos) return std::string_view{};
        return value.substr(first, value.find_last_not_of(whitespace) - first + 1);
    };
    constexpr std::string_view whitespace = " \t\r";
    std::string_view remaining = userProperties;
    while (!remaining.empty()) {
        const auto end = remaining.find_first_of(";\n");
        auto entry = trim(remaining.substr(0, end));
        while (!entry.empty()) {
            const auto separator = entry.find('=');
            if (separator == std::string_view::npos) break;
            const auto name = trim(entry.substr(0, separator));
            const auto value = entry.substr(separator + 1);
            size_t next = value.size();
            const auto nextSeparator = value.find('=');
            if (nextSeparator != std::string_view::npos) {
                const auto nameEnd = value.find_last_not_of(whitespace, nextSeparator - 1);
                if (nextSeparator > 0 && nameEnd != std::string_view::npos) {
                    const auto gap = value.find_last_of(whitespace, nameEnd);
                    if (gap != std::string_view::npos && !trim(value.substr(0, gap)).empty()) next = gap;
                }
            }
            if (name == key) return trim(value.substr(0, next));
            entry = trim(value.substr(next));
        }
        if (end == std::string_view::npos) break;
        remaining.remove_prefix(end + 1);
    }
    return std::nullopt;
}

CityLodGroup readCityLodGroup(const CityScene& scene)
{
    const auto squaredDistance = [](std::string_view text) -> std::optional<float> {
        int meters = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), meters);
        if (result.ec != std::errc{} || meters < 0) return std::nullopt;
        const auto distance = static_cast<float>(meters) * 100;
        return distance * distance;
    };
    CityLodGroup group;
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto& node = scene.nodes[i];
        if (const auto lod = node.property("lod")) {
            if (const auto distance = squaredDistance(*lod)) group.levels.push_back({i, *distance});
        }
        if (const auto maximum = node.property("lodmax")) {
            if (const auto distance = squaredDistance(*maximum)) group.maximumSquared = *distance;
        }
    }
    std::stable_sort(group.levels.begin(), group.levels.end(), [](const auto& a, const auto& b) {
        return a.distanceSquared < b.distanceSquared;
    });
    return group;
}

std::optional<size_t> CityLodGroup::select(float distanceSquared) const
{
    if (levels.empty() || (maximumSquared && distanceSquared > *maximumSquared)) return std::nullopt;
    const auto next = std::lower_bound(levels.begin(), levels.end(), distanceSquared,
        [](const Level& level, float distance) { return level.distanceSquared < distance; });
    return (next == levels.begin() ? next : std::prev(next))->node;
}

std::optional<float> cityTriangleHeight(const std::array<float, 3>& a, const std::array<float, 3>& b,
                                        const std::array<float, 3>& c, const std::array<float, 3>& position)
{
    if (position[0] < std::min({a[0], b[0], c[0]}) || position[0] > std::max({a[0], b[0], c[0]}) ||
        position[1] < std::min({a[1], b[1], c[1]}) || position[1] > std::max({a[1], b[1], c[1]}))
        return std::nullopt;
    const float ux = b[0] - a[0], uy = b[1] - a[1];
    const float vx = c[0] - a[0], vy = c[1] - a[1];
    const float determinant = ux * vy - uy * vx;
    if (std::abs(determinant) <= 0.001f) return std::nullopt;
    const float x = position[0] - a[0], y = position[1] - a[1];
    const float s = (x * vy - y * vx) / determinant, t = (ux * y - uy * x) / determinant;
    if (s < 0 || t < 0 || s + t > 1) return std::nullopt;
    return a[2] + (b[2] - a[2]) * s + (c[2] - a[2]) * t;
}

std::optional<CityNavigation::GroundHit> CityNavigation::sampleGround(
    const CityMesh& mesh, TriangleRef seed, const std::array<float, 3>& localPosition, float rise, float drop) const
{
    if (seed.buffer >= buffers.size() || seed.indexOffset % 3) return std::nullopt;
    const auto& overlaps = buffers[seed.buffer].overlaps;
    const size_t triangle = seed.indexOffset / 3;
    if (triangle >= overlaps.size()) return std::nullopt;
    std::optional<GroundHit> result;
    const auto sample = [&](TriangleRef reference) {
        const auto vertices = reference.vertices(mesh);
        if (!vertices) return;
        const auto height = cityTriangleHeight(mesh.positions.at((*vertices)[0]), mesh.positions.at((*vertices)[1]),
                                               mesh.positions.at((*vertices)[2]), localPosition);
        if (!height) return;
        const float difference = *height - localPosition[2];
        if (difference < -drop || difference > rise) return;
        if (!result || std::abs(difference) < std::abs(result->height - localPosition[2]))
            result = GroundHit{reference, *height};
    };
    sample(seed);
    for (const auto& candidate : overlaps[triangle]) sample(candidate);
    return result;
}

std::optional<CityNavigation::TriangleRef> CityNavigation::neighbor(TriangleRef source, size_t edge) const
{
    if (edge >= 3 || source.buffer >= buffers.size() || source.indexOffset % 3)
        return std::nullopt;
    const auto& buffer = buffers[source.buffer];
    const size_t triangle = source.indexOffset / 3;
    if (triangle >= buffer.roadFlags.size()) return std::nullopt;
    const auto offset = buffer.neighbors[edge][triangle];
    if (offset == UINT16_MAX) return std::nullopt;
    const auto target = buffer.neighbors[edge + 3][triangle];
    if (target >= buffers.size() || offset % 3 || offset / 3 >= buffers[target].roadFlags.size())
        throw std::runtime_error("Invalid retail navigation neighbor target.");
    return TriangleRef{target, offset};
}

std::vector<CityNavigation::TriangleRef> CityNavigation::cliffFaces(uint16_t vertex) const
{
    const size_t key = size_t(vertex) * 2;
    if (key + 1 >= cliffVertexKeys.size()) return {};
    const size_t start = cliffVertexKeys[key], count = cliffVertexKeys[key + 1];
    const auto buffer = static_cast<uint16_t>(materialBuffers[static_cast<size_t>(Material::cliff)]);
    std::vector<TriangleRef> faces;
    faces.reserve(count);
    for (size_t i = start; i < start + count; ++i) {
        const auto offset = cliffTriangles.at(i);
        if (offset != UINT16_MAX) faces.push_back({buffer, offset});
    }
    return faces;
}

std::vector<CityNavigation::TriangleRef> CityNavigation::findRoute(
    const CityMesh& mesh, TriangleRef start, TriangleRef goal, RouteMode mode,
    std::optional<std::array<float, 3>> localTarget) const
{
    const auto center = [&](TriangleRef triangle) -> std::optional<std::array<float, 3>> {
        if (triangle.buffer >= buffers.size() ||
            triangle.indexOffset / 3 >= buffers[triangle.buffer].roadFlags.size())
            return std::nullopt;
        const auto vertices = triangle.vertices(mesh);
        if (!vertices) return std::nullopt;
        const auto& a = mesh.positions.at((*vertices)[0]);
        const auto& b = mesh.positions.at((*vertices)[1]);
        const auto& c = mesh.positions.at((*vertices)[2]);
        return std::array{(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3,
                          (a[2] + b[2] + c[2]) / 3};
    };
    const auto passable = [&](uint16_t buffer) {
        const auto matches = [&](Material material) {
            return materialBuffers[static_cast<size_t>(material)] == buffer;
        };
        if (mode == RouteMode::vehicle) return matches(Material::road) || matches(Material::intersection);
        if (mode == RouteMode::pedestrian) return matches(Material::sidewalk);
        return true;
    };
    const auto startCenter = center(start), goalCenter = center(goal);
    if (!startCenter || !goalCenter) return {};
    const auto distanceScore = [&](const auto& a, const auto& b) {
        if (mode == RouteMode::unrestricted)
            return std::hypot(double(a[0]) - b[0], double(a[1]) - b[1], double(a[2]) - b[2]);
        // ComputeShortestPath accumulates squared center distances with material cost 1.
        const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
        return static_cast<double>(x * x + y * y + z * z);
    };
    const auto target = localTarget.value_or(*goalCenter);
    const auto key = [](TriangleRef triangle) {
        return std::pair{triangle.buffer, triangle.indexOffset};
    };
    struct Visit { double cost; TriangleRef previous; };
    struct Candidate {
        double estimate, cost;
        TriangleRef triangle;
    };
    const auto later = [&](const Candidate& a, const Candidate& b) {
        if (a.estimate != b.estimate) return a.estimate > b.estimate;
        return key(a.triangle) > key(b.triangle);
    };
    std::priority_queue<Candidate, std::vector<Candidate>, decltype(later)> queue(later);
    std::vector<Candidate> retailFrontier;
    const bool retail = mode != RouteMode::unrestricted;
    const auto enqueue = [&](const Candidate& candidate) {
        if (!retail) {
            queue.push(candidate);
            return;
        }
        const auto existing = std::find_if(retailFrontier.begin(), retailFrontier.end(),
            [&](const Candidate& entry) { return key(entry.triangle) == key(candidate.triangle); });
        if (existing != retailFrontier.end()) *existing = candidate;
        else retailFrontier.push_back(candidate);
    };
    const auto take = [&]() -> std::optional<Candidate> {
        if (!retail) {
            if (queue.empty()) return std::nullopt;
            const auto candidate = queue.top();
            queue.pop();
            return candidate;
        }
        auto selected = retailFrontier.end();
        float cutoff = static_cast<float>(999999999);
        // FindLowerCostNode scans in insertion order and truncates each new best to an integer.
        for (auto it = retailFrontier.begin(); it != retailFrontier.end(); ++it) {
            if (it->estimate >= cutoff) continue;
            selected = it;
            cutoff = static_cast<float>(std::trunc(it->estimate));
        }
        if (selected == retailFrontier.end()) return std::nullopt;
        const auto candidate = *selected;
        retailFrontier.erase(selected);
        return candidate;
    };
    std::map<std::pair<uint16_t, uint16_t>, Visit> visits;
    visits.emplace(key(start), Visit{0, start});
    enqueue({retail ? 0 : distanceScore(*startCenter, target), 0, start});
    while (const auto candidate = take()) {
        const auto& current = *candidate;
        if (current.cost != visits.at(key(current.triangle)).cost) continue;
        if (key(current.triangle) == key(goal)) {
            std::vector<TriangleRef> route{goal};
            while (key(route.back()) != key(start))
                route.push_back(visits.at(key(route.back())).previous);
            std::reverse(route.begin(), route.end());
            return route;
        }
        const auto currentCenter = center(current.triangle);
        for (size_t edge = 0; edge < 3; ++edge) {
            const auto next = neighbor(current.triangle, edge);
            if (!next || !passable(next->buffer)) continue;
            if (mode == RouteMode::vehicle) {
                const auto flags = buffers[current.triangle.buffer].roadFlags[current.triangle.indexOffset / 3];
                const auto edgeCode = edge + 1;
                if ((flags & 3) == edgeCode || ((flags >> 2) & 3) == edgeCode) continue;
            }
            const auto nextCenter = center(*next);
            if (!nextCenter) continue;
            auto cost = current.cost + distanceScore(*currentCenter, *nextCenter);
            if (mode != RouteMode::unrestricted) cost = static_cast<float>(cost);
            const auto previous = visits.find(key(*next));
            if (previous != visits.end() && previous->second.cost <= cost) continue;
            visits.insert_or_assign(key(*next), Visit{cost, current.triangle});
            auto estimate = cost + distanceScore(*nextCenter, target);
            if (mode != RouteMode::unrestricted) estimate = static_cast<float>(estimate);
            enqueue({estimate, cost, *next});
        }
    }
    return {};
}

std::optional<uint8_t> CityNavigation::roadFlags(const CitySpawnPoint& point) const
{
    if (point.terrainBuffer >= buffers.size() || point.terrainIndexOffset % 3 != 0) return std::nullopt;
    const auto& flags = buffers[point.terrainBuffer].roadFlags;
    const size_t triangle = point.terrainIndexOffset / 3;
    return triangle < flags.size() ? std::optional<uint8_t>{flags[triangle]} : std::nullopt;
}

std::optional<std::array<uint16_t, 3>> CityNavigation::TriangleRef::vertices(const CityMesh& mesh) const
{
    if (buffer >= mesh.buffers.size() || indexOffset % 3 != 0) return std::nullopt;
    const auto& indices = mesh.buffers[buffer].indices;
    const size_t offset = indexOffset;
    if (offset + 3 > indices.size()) return std::nullopt;
    return std::array{indices[offset], indices[offset + 1], indices[offset + 2]};
}

std::optional<std::array<float, 3>> CityNavigation::edgeWaypoint(
    const CityMesh& mesh, TriangleRef source, TriangleRef target, const std::array<float, 3>& origin)
{
    const auto first = source.vertices(mesh), second = target.vertices(mesh);
    if (!first || !second) return std::nullopt;
    std::optional<uint16_t> shared;
    for (const auto index : *first) {
        if (std::find(second->begin(), second->end(), index) == second->end()) continue;
        if (!shared) {
            shared = index;
            continue;
        }
        const auto& a = mesh.positions.at(*shared);
        const auto& b = mesh.positions.at(index);
        return std::array{(a[0] + b[0]) * 0.5f + origin[0],
                          (a[1] + b[1]) * 0.5f + origin[1],
                          (a[2] + b[2]) * 0.5f + origin[2] + 3.f};
    }
    return std::nullopt;
}

std::optional<std::array<uint16_t, 3>> CitySpawnPoint::terrainTriangle(const CityMesh& heightMesh) const
{
    return CityNavigation::TriangleRef{terrainBuffer, terrainIndexOffset}.vertices(heightMesh);
}

uint8_t CitySpawnGroup::choose(uint32_t random) const
{
    if (metatypes.empty()) throw std::runtime_error("Cannot choose from an empty retail spawn group.");
    return metatypes[random % metatypes.size()];
}

std::optional<uint8_t> CitySpawnGroup::chooseForSpawn(uint32_t random, int spawnedCops, int copLimit,
                                                      bool alternateCopContext) const
{
    constexpr uint8_t policeCar = 38, policeOfficer = 48, alternateOfficer = 161;
    const auto metatype = choose(random);
    if (spawnedCops < copLimit) {
        if (random & 1) {
            if (objectType == 3) return policeCar;
            if (objectType != 11 && objectType != 16) return alternateCopContext ? alternateOfficer : policeOfficer;
        }
    } else if (metatype == policeCar || metatype == policeOfficer || metatype == alternateOfficer) {
        return std::nullopt;
    }
    return metatype;
}

uint32_t CitySpawnGroup::spawnDelayMs(bool eligible, uint32_t random) const
{
    // SpawnPoint uses the car timings for moving and parked vehicles.
    const bool vehicle = objectType == 3 || objectType == 11;
    if (eligible) return vehicle ? 2000 + random % 2000 : 5000 + random % 3000;
    return vehicle ? 500 + random % 500 : 1000 + random % 1000;
}

std::array<std::array<float, 3>, 2> CitySpawnGroup::spawnCenters(
    const std::array<float, 3>& eye, const std::array<float, 3>& target, float actorHeight,
    std::optional<float> vehicleSpeed) const
{
    const float x = target[0] - eye[0], y = target[1] - eye[1], z = target[2] - eye[2];
    const float length = std::sqrt(x * x + y * y + z * z);
    const float inverse = length > 0 ? 1.f / length : 0.f;
    const bool isVehicle = objectType == 3 || objectType == 11;
    const float lead = isVehicle && vehicleSpeed && *vehicleSpeed >= 100 ? 1000.f : 0.f;
    const float inner = objectType == 3 ? 3039.f : objectType == 11 ? 1500.f : 1000.f;
    const float outer = objectType == 3 ? 3900.f : objectType == 11 ? 3000.f : 1700.f;
    const auto center = [&](float distance) -> std::array<float, 3> {
        return {eye[0] + x * inverse * distance, eye[1] + y * inverse * distance, actorHeight};
    };
    return {center(inner + lead), center(outer + lead)};
}

bool CitySpawnGroup::canSpawn(const CitySpawnPoint& point, const CitySpawnContext& context) const
{
    struct SpawnLimits { size_t countIndex; unsigned cap; float innerSquared, outerSquared; };
    const auto limits = [&]() -> SpawnLimits {
        switch (objectType) {
        case 3: return {0, 3, 6250000, 32397042};
        case 11: return {2, 2, 6250000, 10240000};
        case 16: return {3, 5, 4000000, 9000000};
        default: return {1, 3, 4000000, 9000000};
        }
    }();
    const auto& counts = context.activeCounts;
    if (counts[limits.countIndex] >= limits.cap || counts[0] + counts[1] + counts[2] + counts[3] > 5)
        return false;

    const auto distanceSquared = [&](const std::array<float, 3>& center) {
        const auto x = center[0] - point.position[0], y = center[1] - point.position[1];
        const auto z = center[2] - point.position[2];
        return x * x + y * y + z * z;
    };
    if (std::abs(context.innerCenter[2] - point.position[2]) >= 300 ||
        distanceSquared(context.innerCenter) < limits.innerSquared ||
        distanceSquared(context.outerCenter) > limits.outerSquared)
        return false;

    if (objectType == 3 || objectType == 11) {
        // SpawnPoint::hasCloseNeighbors scans active objects, excluding checkpoints.
        for (const auto& neighbor : context.nearbyObjects) {
            if (neighbor.objectType == 6 || !(neighbor.flags & 1)) continue;
            const auto x = neighbor.position[0] - point.position[0];
            const auto y = neighbor.position[1] - point.position[1];
            if (x * x + y * y < 1000000.f) return false;
        }
    }
    if (objectType != 3) return true;
    if (!context.traffic) return false;
    const auto& traffic = *context.traffic;
    const auto roadSpeed = std::max(1, traffic.roadFlags >> 6) * 20.f;
    constexpr float radiansPerDegree = 0.0174532925f;
    const float x = point.rotation[0] * radiansPerDegree;
    const float y = point.rotation[1] * radiansPerDegree;
    const float z = point.rotation[2] * radiansPerDegree;
    const float sx = std::sin(x), cx = std::cos(x);
    const float sy = std::sin(y), sz = std::sin(z), cz = std::cos(z);
    // Apply the spawn rotation to the retail forward vector (0, -1, 0).
    const std::array<float, 3> forward{cx * sz - sx * sy * cz,
                                      -cx * cz - sx * sy * sz,
                                      -sx * std::cos(y)};
    float closing = 0;
    for (size_t i = 0; i < forward.size(); ++i) {
        const float relativeVelocity = roadSpeed * forward[i] - traffic.actorSpeed * traffic.actorForward[i];
        closing += (point.position[i] - traffic.actorPosition[i]) * relativeVelocity;
    }
    return closing <= 0;
}

CityAssets loadCityAssets(FileSystemBase& assets)
{
    const std::string listPath = "cityparts.array";
    const auto listBytes = readAsset(assets, listPath, 64 * 1024);
    CityDataReader list(listPath, listBytes);
    const auto count = list.integer();
    if (count == 0 || count > 128) throw std::runtime_error("Invalid city part count.");
    CityAssets city;
    city.districts.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        // Matches the field order in Structs::WorldDescriptor::Read.
        const auto path = list.string();
        const auto bytes = readAsset(assets, path, 64 * 1024);
        CityDataReader descriptor(path, bytes);
        CityDistrict district;
        district.flags = descriptor.integer();
        district.objectsPath = descriptor.string();
        const auto scene = descriptor.string();
        const auto lowDetailScene = descriptor.string();
        district.descriptorValue = descriptor.integer();
        district.additionalPath = descriptor.string();
        descriptor.finish();
        district.objects = readAsset(assets, district.objectsPath, 64 * 1024 * 1024);
        district.vehicles = readVehicleRecords(district.objectsPath, district.objects);
        district.spawnPoints = readSpawnPoints(district.objectsPath, district.objects);
        district.scene = loadScene(assets, scene);
        district.lowDetailScene = loadScene(assets, lowDetailScene);
        city.districts.push_back(std::move(district));
    }
    // The retail city index ends with two zero padding bytes.
    list.finish(2);
    if (std::any_of(city.districts.begin(), city.districts.end(),
                    [](const CityDistrict& district) { return !district.spawnPoints.empty(); }))
        bindSpawnGroups(assets, city);
    const auto getLibrary = [&](const std::string& path) -> const CityScene& {
        auto [library, inserted] = city.geometryLibraries.try_emplace(path);
        if (inserted) {
            auto& model = library->second;
            model = loadCityScene(assets, path);
            const auto lod = readCityLodGroup(model);
            if (lod.levels.size() > 1 && std::all_of(lod.levels.begin(), lod.levels.end(), [&](const auto& level) {
                const auto& node = model.nodes[level.node];
                return node.parent < 0 && !node.geometry.empty() &&
                    std::all_of(node.geometry.begin(), node.geometry.end(), [&](const auto& reference) {
                        if (!reference.sourcePath.empty()) return false;
                        const auto mesh = std::find_if(model.meshes.begin(), model.meshes.end(),
                            [&](const CityMesh& entry) { return entry.name == reference.meshName; });
                        if (mesh == model.meshes.end()) return false;
                        for (size_t i = 0; i < mesh->buffers.size(); ++i) {
                            if (i < reference.materials.size() && !reference.materials[i].sourcePath.empty())
                                return false;
                            const auto material = resolveCityMaterial(city, model, model, reference, *mesh, i);
                            if (material.texture.empty()) continue;
                            const std::unique_ptr<IFileStream> file(assets.openResource(material.texture.c_str()));
                            if (!file) return false;
                        }
                        return true;
                    });
            })) city.usableLodModels.insert(path);
        }
        return library->second;
    };
    for (const auto& district : city.districts) {
        for (const auto* scene : {&district.scene, &district.lowDetailScene}) {
            for (const auto& node : scene->nodes) {
                for (const auto& geometry : node.geometry) {
                    if (!geometry.sourcePath.empty()) {
                        const auto& meshes = getLibrary(geometry.sourcePath).meshes;
                        if (std::none_of(meshes.begin(), meshes.end(), [&](const CityMesh& mesh) {
                                return mesh.name == geometry.meshName;
                            }))
                            throw std::runtime_error("Missing mesh " + geometry.meshName + " in " + geometry.sourcePath);
                    }
                    for (const auto& material : geometry.materials)
                        if (!material.sourcePath.empty()) getLibrary(material.sourcePath);
                }
            }
        }
    }
    return city;
}

CityScriptInventory auditCityScripts(FileSystemBase& assets)
{
    return CityScriptMap(assets).inventory;
}

CityScriptCheckpoint decodeCityScriptCheckpoint(const CityScriptCommand& command)
{
    if (command.opcode != 64 && command.opcode != 105)
        throw std::runtime_error("Expected a retail checkpoint opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readCheckpointCommand(reader, command.opcode);
}

CityWaitEscape decodeCityWaitEscape(const CityScriptCommand& command)
{
    if (command.opcode != 101) throw std::runtime_error("Expected retail WaitEscape opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readWaitEscape(reader);
}

std::string decodeCityWaitStolen(const CityScriptCommand& command)
{
    if (command.opcode != 89) throw std::runtime_error("Expected retail WaitStolen opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readScriptObject(reader);
}

std::string decodeCityActorGetOut(const CityScriptCommand& command)
{
    if (command.opcode != 99) throw std::runtime_error("Expected the retail ActorGetOut opcode.");
    const std::string path = "retail ActorGetOut";
    CityDataReader reader(path, command.payload);
    return readScriptObject(reader);
}

std::string decodeCityWaitKilled(const CityScriptCommand& command)
{
    if (command.opcode != 87) throw std::runtime_error("Expected the retail WaitKilled opcode.");
    const std::string path = "retail WaitKilled";
    CityDataReader reader(path, command.payload);
    return readScriptObject(reader);
}

std::string decodeCityWaitForLock(const CityScriptCommand& command)
{
    if (command.opcode != 108) throw std::runtime_error("Expected the retail WaitForLock opcode.");
    const std::string path = "retail WaitForLock";
    CityDataReader reader(path, command.payload);
    return readScriptObject(reader);
}

std::string decodeCityDeferredDisable(const CityScriptCommand& command)
{
    if (command.opcode != 23) throw std::runtime_error("Expected retail deferred actor disable opcode.");
    const std::string path = "retail deferred actor disable";
    CityDataReader reader(path, command.payload);
    return readScriptObject(reader);
}

std::string decodeCityShopLock(const CityScriptCommand& command)
{
    if (command.opcode != 76 && command.opcode != 77)
        throw std::runtime_error("Expected a retail shop lock opcode.");
    const std::string path = "retail shop lock";
    CityDataReader reader(path, command.payload);
    return readScriptObject(reader);
}

CityActorAttack decodeCityActorAttack(const CityScriptCommand& command)
{
    if (command.opcode != 29) throw std::runtime_error("Expected the retail ActorAttack opcode.");
    const std::string path = "retail ActorAttack";
    CityDataReader reader(path, command.payload);
    return readActorAttack(reader);
}

CityScriptTutorial decodeCityScriptTutorial(const CityScriptCommand& command)
{
    if (command.opcode != 47 && command.opcode != 112)
        throw std::runtime_error("Expected a retail tutorial opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readScriptTutorial(reader);
}

std::string decodeCityExplodeVehicle(const CityScriptCommand& command)
{
    if (command.opcode != 106) throw std::runtime_error("Expected the retail vehicle explosion opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readScriptObject(reader);
}

CityAudioCue decodeCityScriptAudio(const CityScriptCommand& command)
{
    if (command.opcode < 60 || command.opcode > 63)
        throw std::runtime_error("Expected a retail audio opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readAudioCommand(reader, command.opcode, 0);
}

CityScriptCamera decodeCityScriptCamera(const CityScriptCommand& command)
{
    if (command.opcode != 12 && command.opcode != 13)
        throw std::runtime_error("Expected a retail camera opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readCameraCommand(reader, command.opcode);
}

CityFade decodeCityScriptFade(const CityScriptCommand& command)
{
    if (command.opcode != 41 && command.opcode != 42)
        throw std::runtime_error("Expected a retail fade opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readFadeCommand(reader, command.opcode, 0);
}

CityScriptActorPlacement decodeCityScriptActorPlacement(const CityScriptCommand& command)
{
    if (command.opcode != 21 && command.opcode != 22 && command.opcode != 24)
        throw std::runtime_error("Expected a retail actor placement opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readActorPlacement(reader, command.opcode);
}

CityScriptActorAnimation decodeCityScriptActorAnimation(const CityScriptCommand& command)
{
    if (command.opcode != 6 && command.opcode != 7)
        throw std::runtime_error("Expected a retail actor animation opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readActorAnimation(reader, command.opcode);
}

CityScriptDialogue decodeCityScriptDialogue(const CityScriptCommand& command)
{
    if (command.opcode != 56) throw std::runtime_error("Expected a retail Dialog opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readDialogueCommand(reader);
}

std::optional<CityMissionConditionCommand> decodeCityMissionCondition(const CityScriptCommand& command)
{
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readMissionCondition(reader, command.opcode);
}

uint32_t decodeCityMissionObjective(const CityScriptCommand& command)
{
    if (command.opcode != 66) throw std::runtime_error("Expected retail MissionObjective opcode.");
    const std::string path = scriptMapPath;
    CityDataReader reader(path, command.payload);
    return readMissionObjective(reader);
}

CityScriptProgram loadCityScriptProgram(FileSystemBase& assets)
{
    const CityScriptMap scripts(assets);
    CityScriptProgram program(scripts.inventory.scripts);
    for (uint32_t id = 0; id < program.size(); ++id) {
        auto stream = scripts.script(id);
        const auto count = stream.integer();
        auto& commands = program[id];
        commands.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            auto command = stream.record();
            const auto opcode = command.integer();
            commands.push_back({opcode, command.remainingBytes()});
        }
    }
    return program;
}

std::optional<std::array<float, 3>> findCityMissionTargetPosition(const CityAssets& city, const std::string& name)
{
    for (const auto& district : city.districts) {
        CityDataReader map(district.objectsPath, district.objects);
        const auto count = map.integer();
        size_t vehicleIndex = 0;
        for (uint32_t i = 0; i < count; ++i) {
            auto record = map.record();
            const auto type = record.integer();
            if (type == 3) {
                const auto& vehicle = district.vehicles.at(vehicleIndex++);
                if (vehicle.name == name) return vehicle.position;
                continue;
            }
            if ((type != 5 && type != 6 && type != 10) || record.string() != name) continue;
            record.skip(16); // Flags, list, attribute, and highlight.
            record.string(); // Scene name.
            record.string(); // Node name.
            record.skip(4); // Pool size.
            std::array<float, 3> position{};
            for (auto& axis : position) axis = record.signedInteger();
            return position;
        }
    }
    return std::nullopt;
}

std::vector<CityMissionDefinition> loadCityMissions(FileSystemBase& assets)
{
    const std::string path = "Missions.gmap";
    const auto bytes = readAsset(assets, path, 1024 * 1024);
    CityDataReader map(path, bytes);
    const auto count = map.integer();
    constexpr size_t minimumRecordSize = 40;
    if (!count || count > (bytes.size() - 4) / minimumRecordSize)
        throw std::runtime_error("Invalid retail mission count.");
    std::vector<CityMissionDefinition> missions;
    missions.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto entry = map.record();
        missions.push_back({entry.signedInteger(), entry.signedInteger(), entry.signedInteger(),
                            entry.string(), entry.signedInteger(), entry.signedInteger(),
                            entry.string(), entry.signedInteger(), entry.signedInteger()});
        entry.finish();
    }
    map.finish(2);
    return missions;
}

CityOpening loadCityOpening(FileSystemBase& assets, const CityAssets& city, RetailLanguage language)
{
    const CityScriptMap scripts(assets);
    CityOpening opening;
    struct NamedMove {
        std::string first;
        std::string second;
        uint32_t startMs;
        uint32_t durationMs;
    };
    std::vector<NamedMove> moves;
    std::vector<NamedMove> payphoneMoves;
    std::vector<NamedMove> postFightMoves;
    std::vector<NamedMove> nextMissionMoves;
    std::vector<NamedMove> pierMoves;
    std::vector<NamedMove> shopMoves;
    std::vector<NamedMove> lcClosingMoves;
    std::vector<NamedMove> postLcMoves;
    std::vector<NamedMove> postLcAmbushMoves;
    std::vector<CityFade> payphoneFades;
    struct ActorPlacement {
        std::string marker;
        std::string actor;
        uint32_t startMs;
    };
    std::vector<ActorPlacement> placements;
    std::map<std::string, uint32_t> disabledActors;
    struct AnimationCommand {
        std::string first;
        std::string loop;
        std::string actor;
        uint32_t startMs;
    };
    std::vector<AnimationCommand> animations;
    std::vector<AnimationCommand> payphoneAnimations;
    std::vector<AnimationCommand> postFightAnimations;
    std::vector<AnimationCommand> nextMissionAnimations;
    std::vector<AnimationCommand> pierAnimations;
    std::vector<AnimationCommand> lcClosingAnimations;
    std::vector<AnimationCommand> postLcAnimations;
    std::vector<AnimationCommand> postLcAmbushAnimations;
    struct MotionCommand {
        uint32_t time;
        std::string actor;
        std::string marker;
        bool running;
        bool followsPath;
    };
    std::vector<MotionCommand> pierMotions, shopMotions, lcClosingMotions, postLcMotions, postLcAmbushMotions;
    struct VehicleDriveCommand {
        uint32_t time;
        std::string actor, marker;
        int32_t speedKmh = 0;
    };
    std::vector<VehicleDriveCommand> postLcAmbushDrives;
    const auto readCinematicAnimation = [](CityDataReader& command, uint32_t opcode, uint32_t time) {
        auto cue = readActorAnimation(command, opcode);
        // Retail scripts differ from anim_ingame.bdae in the casing of these names.
        const auto normalize = [](std::string name) {
            if (name == "cut_cellphoneTalk_idle") return std::string("cut_cellphonetalk_idle");
            if (name == "cut_gesture_pointFront") return std::string("cut_gesture_pointfront");
            if (name == "cut_gesture_comeHere") return std::string("cut_gesture_comehere");
            if (name == "cut_gesture_pointRight") return std::string("cut_gesture_pointright");
            return name;
        };
        return AnimationCommand{normalize(cue.first), normalize(cue.loop.value_or(cue.first)), cue.actor, time};
    };
    struct DialogueCommand {
        uint32_t startMs;
        uint32_t durationMs;
        uint32_t textId;
        int32_t portrait;
    };
    std::vector<DialogueCommand> dialogue;
    std::vector<DialogueCommand> payphoneDialogue;
    std::vector<DialogueCommand> postFightDialogue;
    std::vector<DialogueCommand> nextMissionDialogue;
    std::vector<DialogueCommand> pierDialogue;
    std::vector<DialogueCommand> shopDialogue;
    std::vector<DialogueCommand> lcClosingDialogue;
    std::vector<DialogueCommand> postLcDialogue;
    std::vector<DialogueCommand> postLcAmbushDialogue;
    std::vector<CityAudioCue> payphoneAudio;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> payphonePlacements;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> postFightPlacements;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> nextMissionPlacements;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> pierPlacements;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> shopPlacements;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> lcClosingPlacements;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> postLcPlacements;
    std::vector<std::pair<uint32_t, CityScriptActorPlacement>> postLcAmbushPlacements;
    struct TutorialCommand {
        uint32_t startMs;
        uint32_t title;
        uint32_t text;
    };
    std::vector<TutorialCommand> tutorials;
    std::string checkpointName;
    std::string vehicleName;
    std::string payphoneName, departureName;
    uint32_t vehicleMs = 0, departureMs = 0;
    std::optional<uint32_t> payphoneObjectiveId;
    std::optional<uint32_t> openingMissionId;
    float departureRadius = 0;
    bool clearConditionsOnDeparture = false;
    uint32_t checkpointMs = 0;
    std::set<std::string> names;
    std::string previous;
    uint32_t elapsed = 0;
    CityPostFightScene postFight;
    std::string previousCinematicShot;
    std::optional<uint32_t> nextMissionObjectiveId;
    std::string weaponShopEntrance;
    const auto readCinematic = [&](const auto& read, CityDataReader& stream, uint32_t remaining, unsigned depth,
                                   CityMissionCinematic& scene, auto& moves, auto& lines,
                                   auto& placements, std::vector<AnimationCommand>* animations,
                                   std::vector<MotionCommand>* motions,
                                   CityPostFightScene* postFightTail, uint32_t stopOpcode,
                                   std::vector<VehicleDriveCommand>* drives = nullptr) -> void {
        if (depth > 8) throw std::runtime_error("Invalid retail cinematic script nesting.");
        for (uint32_t i = 0; i < remaining; ++i) {
            auto command = stream.record();
            const auto opcode = command.integer();
            const auto time = scene.durationMs;
            uint32_t duration = 0;
            if (opcode == 1) {
                const auto script = command.integer();
                command.finish();
                if (!depth) scene.nextScript = script;
                auto child = scripts.script(script);
                const auto commands = child.integer();
                read(read, child, commands, depth + 1, scene, moves, lines, placements, animations, motions,
                     postFightTail, stopOpcode, drives);
                continue;
            }
            if (!depth && postFightTail && opcode == 3) {
                postFightTail->unlockedScript = command.integer();
                command.finish();
                return;
            }
            if (!depth && !postFightTail && opcode == stopOpcode) {
                if (opcode == 66) nextMissionObjectiveId = readMissionObjective(command);
                else if (opcode == 76) weaponShopEntrance = readScriptObject(command);
                else command.finish();
                return;
            }
            if (opcode == 12) {
                const auto cue = readCameraCommand(command, opcode);
                const auto& shot = *cue.scene;
                const auto blend = static_cast<uint32_t>(std::max(0, cue.transitionMs));
                names.insert(shot);
                moves.push_back({blend && !previousCinematicShot.empty() ? previousCinematicShot : shot,
                                          shot, time, blend});
                previousCinematicShot = shot;
            } else if (opcode == 13) {
                readCameraCommand(command, opcode);
                scene.followCameraMs = time;
            } else if (opcode == 21 || opcode == 22 || opcode == 24) {
                placements.emplace_back(time, readActorPlacement(command, opcode));
            } else if (opcode == 23 && stopOpcode == 67) {
                placements.emplace_back(time, CityScriptActorPlacement{
                    CityScriptActorAction::disable, readScriptObject(command), {}});
            } else if ((opcode == 25 || opcode == 26 || opcode == 34) && motions) {
                motions->push_back({time, command.string(), command.string(), opcode != 25, opcode == 34});
                command.finish();
            } else if (opcode == 52 && drives) {
                drives->push_back({time, command.string(), command.string()});
                command.finish();
            } else if (opcode == 53 && drives) {
                const auto actor = command.string();
                const auto speed = command.signedInteger();
                command.finish();
                const auto drive = std::find_if(drives->rbegin(), drives->rend(), [&](const auto& cue) {
                    return cue.actor == actor && cue.time == time;
                });
                if (drive == drives->rend() || speed <= 0)
                    throw std::runtime_error("Invalid retail cinematic vehicle speed for " + actor);
                drive->speedKmh = speed;
            } else if (opcode == 6 || opcode == 7) {
                auto cue = readCinematicAnimation(command, opcode, time);
                if (animations) animations->push_back(std::move(cue));
            } else if (opcode == 41 || opcode == 42) {
                const auto fade = readFadeCommand(command, opcode, time);
                scene.fades.push_back(fade);
                duration = fade.durationMs;
            } else if (opcode == 57) {
                duration = command.integer();
                command.finish();
            } else if (opcode == 56) {
                const auto cue = readDialogueCommand(command);
                lines.push_back({time, static_cast<uint32_t>(std::max(0, cue.durationMs)),
                                             cue.textId, cue.speaker});
            } else if (opcode >= 60 && opcode <= 63) {
                scene.audio.push_back(readAudioCommand(command, opcode, time));
            } else if (!depth && postFightTail && opcode == 67) {
                command.finish();
                postFightTail->missionOverMs = time;
            }
            if (duration > UINT32_MAX - time)
                throw std::runtime_error("Retail cinematic timeline overflow.");
            scene.durationMs += duration;
        }
        if (!depth) throw std::runtime_error("Missing retail cinematic handoff.");
    };
    std::optional<std::tuple<uint32_t, std::string, std::string>> payphoneWalk;
    uint32_t payphoneGameplayFadeMs = 0;
    std::optional<uint32_t> fightObjectiveId;
    std::optional<CityScriptTutorial> fightTutorial;
    std::vector<uint8_t> fightLockPayload;
    std::vector<uint8_t> fightAttackPayload;
    std::vector<uint8_t> fightKilledPayload;
    std::vector<uint8_t> fightDisablePayload;
    const auto readPayphoneCinematic = [&](CityDataReader& stream, uint32_t remaining) {
        uint32_t cinematicMs = 0;
        std::string previousShot;
        for (uint32_t i = 0; i < remaining; ++i) {
            auto command = stream.record();
            const auto opcode = command.integer();
            if (opcode == 16) { // Leave the mission cinematic.
                command.finish();
                bool followCamera = false;
                for (++i; i < remaining; ++i) {
                    auto handoff = stream.record();
                    const auto handoffOpcode = handoff.integer();
                    if (handoffOpcode == 13) {
                        readCameraCommand(handoff, handoffOpcode);
                        followCamera = true;
                    } else if (handoffOpcode == 24) {
                        payphonePlacements.emplace_back(cinematicMs, readActorPlacement(handoff, handoffOpcode));
                    } else if (handoffOpcode == 7) {
                        const auto cue = readActorAnimation(handoff, handoffOpcode);
                        payphoneAnimations.push_back({cue.first, cue.loop.value_or(cue.first), cue.actor, cinematicMs});
                    } else if (handoffOpcode == 41 && followCamera) {
                        payphoneGameplayFadeMs = readFadeCommand(handoff, handoffOpcode, 0).durationMs;
                        if (remaining - i < 9)
                            throw std::runtime_error("Missing retail payphone fight objective and tutorial.");
                        auto objective = stream.record();
                        auto tutorial = stream.record();
                        if (objective.integer() != 66 || tutorial.integer() != 47)
                            throw std::runtime_error("Invalid retail payphone fight objective and tutorial.");
                        fightObjectiveId = readMissionObjective(objective);
                        fightTutorial = readScriptTutorial(tutorial);
                        auto blink = stream.record();
                        auto lock = stream.record();
                        auto attack = stream.record();
                        if (blink.integer() != 111 || lock.integer() != 108 || attack.integer() != 29)
                            throw std::runtime_error("Invalid retail payphone lock sequence.");
                        fightLockPayload = lock.remainingBytes();
                        readScriptObject(lock);
                        fightAttackPayload = attack.remainingBytes();
                        readActorAttack(attack);
                        auto killed = stream.record();
                        if (killed.integer() != 87) throw std::runtime_error("Missing retail fight WaitKilled.");
                        fightKilledPayload = killed.remainingBytes();
                        readScriptObject(killed);
                        auto hideTutorial = stream.record();
                        auto disable = stream.record();
                        if (hideTutorial.integer() != 48 || disable.integer() != 23)
                            throw std::runtime_error("Invalid retail fight cleanup.");
                        hideTutorial.finish();
                        fightDisablePayload = disable.remainingBytes();
                        readScriptObject(disable);
                        readCinematic(readCinematic, stream, remaining - i - 9, 0, postFight,
                                      postFightMoves, postFightDialogue, postFightPlacements,
                                      &postFightAnimations, nullptr, &postFight, 3);
                        return cinematicMs;
                    } else if (handoffOpcode == 66) {
                        break;
                    }
                }
                throw std::runtime_error("Missing retail payphone gameplay reveal.");
            }
            uint32_t duration = 0;
            if (opcode == 12) {
                const auto cue = readCameraCommand(command, opcode);
                const auto& shot = *cue.scene;
                const auto blend = static_cast<uint32_t>(std::max(0, cue.transitionMs));
                names.insert(shot);
                payphoneMoves.push_back({blend && !previousShot.empty() ? previousShot : shot, shot,
                                         cinematicMs, blend});
                previousShot = shot;
            } else if (opcode == 21 || opcode == 22 || opcode == 24) {
                payphonePlacements.emplace_back(cinematicMs, readActorPlacement(command, opcode));
            } else if (opcode == 25) {
                auto actor = command.string();
                auto marker = command.string();
                command.finish();
                if (payphoneWalk) throw std::runtime_error("Unexpected second retail payphone walk.");
                payphoneWalk.emplace(cinematicMs, std::move(actor), std::move(marker));
            } else if (opcode == 6 || opcode == 7) {
                payphoneAnimations.push_back(readCinematicAnimation(command, opcode, cinematicMs));
            } else if (opcode == 41 || opcode == 42) {
                const auto fade = readFadeCommand(command, opcode, cinematicMs);
                payphoneFades.push_back(fade);
                duration = fade.durationMs;
            } else if (opcode == 57) {
                duration = command.integer();
                command.finish();
            } else if (opcode == 56) {
                const auto cue = readDialogueCommand(command);
                payphoneDialogue.push_back({cinematicMs, static_cast<uint32_t>(std::max(0, cue.durationMs)),
                                            cue.textId, cue.speaker});
            } else if (opcode >= 60 && opcode <= 63) {
                payphoneAudio.push_back(readAudioCommand(command, opcode, cinematicMs));
            }
            if (duration > UINT32_MAX - cinematicMs)
                throw std::runtime_error("Retail payphone cinematic timeline overflow.");
            cinematicMs += duration;
        }
        throw std::runtime_error("Missing retail payphone cinematic end.");
    };
    uint32_t payphoneCinematicMs = 0;
    const auto advance = [&](uint32_t duration) {
        if (duration > UINT32_MAX - elapsed) throw std::runtime_error("Retail intro timeline overflow.");
        elapsed += duration;
    };
    const auto readTrack = [&](const auto& read, uint32_t index, unsigned depth, bool linked) -> void {
        if (depth > 8) throw std::runtime_error("Invalid retail intro script call.");
        auto stream = scripts.script(index);
        const auto count = stream.integer();
        for (uint32_t j = 0; j < count; ++j) {
            auto command = stream.record();
            const auto commandType = command.integer();
            switch (commandType) {
            case 12: {  // CamSet
                const auto cue = readCameraCommand(command, commandType);
                const auto& camera = *cue.scene;
                const auto duration = static_cast<uint32_t>(std::max(0, cue.transitionMs));
                names.insert(camera);
                moves.push_back({duration && !previous.empty() ? previous : camera, camera, elapsed, duration});
                previous = camera;
                break;
            }
            case 21:  // Enable
            case 24: { // MoveTo
                if (linked) {
                    auto cue = readActorPlacement(command, commandType);
                    placements.push_back({std::move(cue.marker), std::move(cue.actor), elapsed});
                }
                break;
            }
            case 6:   // SetAnim
            case 7:   // SetAnim2
                if (linked) {
                    auto cue = readActorAnimation(command, commandType);
                    if (std::any_of(placements.begin(), placements.end(),
                                    [&](const ActorPlacement& placement) { return placement.actor == cue.actor; }))
                        animations.push_back({cue.first, cue.loop.value_or(cue.first), std::move(cue.actor), elapsed});
                }
                break;
            case 22:  // Disable
                if (linked) {
                    auto cue = readActorPlacement(command, commandType);
                    disabledActors.try_emplace(std::move(cue.actor), elapsed);
                }
                break;
            case 13:  // CamDefault
                if (linked) {
                    const auto cue = readCameraCommand(command, commandType);
                    opening.followCameraMs = elapsed;
                    opening.followCameraBlendMs = static_cast<uint32_t>(std::max(0, cue.transitionMs));
                }
                break;
            case 1:   // PlayScript
                if (linked) {
                    const auto subscript = command.integer();
                    command.finish();
                    read(read, subscript, depth + 1, true);
                    if (depth == 0 && opening.followCameraMs && !opening.movementMs)
                        opening.movementMs = elapsed;
                }
                break;
            case 47: { // ShowTutorial
                const auto tutorial = readScriptTutorial(command);
                if (depth == 0 && opening.movementMs)
                    tutorials.push_back({elapsed, tutorial.title, tutorial.text});
                break;
            }
            case 70: // ProtectCar.
                if (linked && depth == 0) {
                    auto condition = readMissionCondition(command, commandType);
                    opening.vehicleProtection = CityOpening::VehicleProtection{
                        elapsed, std::move(condition->target), condition->argument};
                }
                break;
            case 68: // MissionClearConditions.
                command.finish();
                if (linked && depth == 0 && !departureName.empty()) clearConditionsOnDeparture = true;
                break;
            case 89:  // WaitStolen
                if (depth == 0 && !checkpointName.empty() && tutorials.size() == 2) {
                    vehicleName = readScriptObject(command);
                    vehicleMs = elapsed;
                }
                break;
            case 101: // WaitEscape
                if (depth == 0 && !vehicleName.empty()) {
                    auto escape = readWaitEscape(command);
                    departureName = std::move(escape.target);
                    departureRadius = static_cast<float>(escape.distanceCm);
                    departureMs = elapsed;
                }
                break;
            case 66:  // MissionObjective
                if (linked && depth == 0 && !departureName.empty())
                    payphoneObjectiveId = readMissionObjective(command);
                break;
            case 65: // MissionStart
                if (linked && depth == 0 && !openingMissionId) {
                    openingMissionId = command.integer();
                    command.finish();
                }
                break;
            case 64:  // WaitCheckpoint
                if (depth == 0 && opening.movementMs) {
                    if (!checkpointName.empty()) {
                        payphoneName = command.string();
                        command.finish();
                        if (j + 1 >= count)
                            throw std::runtime_error("Missing ActorGetOut after the retail payphone checkpoint.");
                        auto exit = stream.record();
                        if (exit.integer() != 99 || readScriptObject(exit) != "Player")
                            throw std::runtime_error("Invalid ActorGetOut after the retail payphone checkpoint.");
                        payphoneCinematicMs = readPayphoneCinematic(stream, count - j - 2);
                        return;
                    }
                    checkpointName = command.string();
                    command.finish();
                    checkpointMs = elapsed;
                }
                break;
            case 41:   // FadeIn
            case 42: { // FadeOut
                const auto fade = readFadeCommand(command, commandType, elapsed);
                opening.fades.push_back(fade);
                advance(fade.durationMs);
                break;
            }
            case 57:   // Wait
                advance(command.integer());
                command.finish();
                break;
            case 60:  // Play a one-shot sound.
            case 61:  // Stop sound.
            case 62:  // Start a priority loop.
            case 63: // Stop priority sound.
                if (linked) opening.audio.push_back(readAudioCommand(command, commandType, elapsed));
                break;
            case 56: { // Dialog
                const auto cue = readDialogueCommand(command);
                if (linked) dialogue.push_back({elapsed, static_cast<uint32_t>(std::max(0, cue.durationMs)),
                                                cue.textId, cue.speaker});
                break;
            }
            case 3:    // UnlockScript
                if (!linked) {
                    opening.nextScript = command.integer();
                    command.finish();
                    opening.handoffMs = elapsed;
                    return;
                }
                break;
            default:
                break;
            }
        }
    };
    opening.startupScript = scripts.inventory.chapterStarts.front();
    readTrack(readTrack, opening.startupScript, 0, false);
    if (!opening.handoffMs || moves.empty()) throw std::runtime_error("The retail intro handoff is missing.");
    readTrack(readTrack, opening.nextScript, 0, true);
    if (elapsed <= opening.handoffMs) throw std::runtime_error("The linked intro shot is missing.");
    opening.durationMs = checkpointName.empty() ? elapsed : checkpointMs;
    const auto chapterText = readLocalizedStrings(assets, "ch2", language);
    const auto missionTitle = [&](uint32_t id) {
        const auto catalog = loadCityMissions(assets);
        const auto& mission = catalog.at(id);
        const auto titleIndex = static_cast<uint32_t>(mission.field28) & 0xfff;
        const auto completionId = static_cast<uint32_t>(mission.completionTextId);
        const auto completionIndex = completionId & 0xfff;
        const auto completion = (completionId >> 12 & 0xf) == 4 && completionIndex < chapterText.size()
            ? chapterText[completionIndex] : std::string{};
        if (mission.field0 < 0 || (static_cast<uint32_t>(mission.field28) >> 12 & 0xf) != 4 ||
            titleIndex >= chapterText.size())
            throw std::runtime_error("Invalid retail mission title.");
        return CityMissionTitle{id, mission.sprite,
            static_cast<uint32_t>(mission.field0), chapterText[titleIndex], mission.cashReward, completion};
    };
    if (openingMissionId) opening.missionTitle = missionTitle(*openingMissionId);
    const auto speakers = dialogue.empty() && payphoneDialogue.empty() && postFightDialogue.empty()
        ? std::vector<std::string>{} : readRetailStrings(assets, "ch2.character");
    const auto resolveDialogue = [&](const std::vector<DialogueCommand>& commands) {
        std::vector<CityDialogue> resolved;
        resolved.reserve(commands.size());
        for (const auto& [startMs, durationMs, textId, portrait] : commands) {
            // StringManager uses bits 12–15 for the sheet and the low 12 bits for its entry.
            constexpr uint32_t chapterTwoSheet = 4;
            const auto entry = textId & 0xfff;
            if ((textId >> 12 & 0xf) != chapterTwoSheet || chapterText.empty()) continue;
            if (entry >= chapterText.size() || (!speakers.empty() && entry >= speakers.size()))
                throw std::runtime_error("Invalid retail dialogue text ID " + std::to_string(textId));
            resolved.push_back({startMs, durationMs, speakers.empty() ? std::string{} : speakers[entry],
                                chapterText[entry], portrait});
        }
        return resolved;
    };
    opening.dialogue = resolveDialogue(dialogue);

    struct PlacementObject {
        uint32_t type;
        std::string model;
        std::array<float, 3> position;
        std::array<float, 3> rotation;
        std::array<float, 3> scale;
        float radius;
        std::optional<uint32_t> radioStation;
        const CityVehicleDefinition* vehicle = nullptr;
        std::vector<std::array<float, 3>> waypoints;
    };
    const auto findObject = [&](const std::string& name) {
        for (const auto& district : city.districts) {
            CityDataReader map(district.objectsPath, district.objects);
            const auto count = map.integer();
            size_t vehicleIndex = 0;
            for (uint32_t j = 0; j < count; ++j) {
                auto record = map.record();
                const auto type = record.integer();
                if (type == 3) {
                    const auto& vehicle = district.vehicles.at(vehicleIndex++);
                    if (vehicle.name == name)
                        return PlacementObject{3, vehicle.model, vehicle.position, vehicle.rotation, vehicle.scale,
                                               0.f, vehicle.radioStation, &vehicle};
                    continue;
                }
                if (type != 5 && type != 6 && type != 10 && type != 13 && type != 14) continue;
                if (record.string() != name) continue;
                PlacementObject object{};
                object.type = type;
                record.skip(16);
                object.model = record.string();
                record.string();  // nodeName
                record.skip(4);   // poolSize
                for (auto& coordinate : object.position) coordinate = record.signedInteger();
                for (auto& angle : object.rotation) angle = record.signedInteger();
                for (auto& factor : object.scale) factor = record.signedInteger() * 0.01f;
                if (type == 6 || type == 13) {
                    record.skip(16);  // Marker fields followed by the two checkpoint IDs.
                    object.radius = record.signedInteger();
                    record.skip(1);
                    record.finish();
                }
                if (type == 14) {
                    // Path::Load consumes a scalar count followed by signed XYZ triples.
                    const auto scalarCount = record.integer();
                    if (scalarCount < 3 || scalarCount % 3 || scalarCount != record.remaining() / 4)
                        throw std::runtime_error("Invalid retail path " + name);
                    object.waypoints.resize(scalarCount / 3);
                    for (auto& point : object.waypoints)
                        for (auto& coordinate : point) coordinate = record.signedInteger();
                    record.finish();
                }
                return object;
            }
        }
        throw std::runtime_error("Missing retail intro object " + name);
    };
    if (!checkpointName.empty()) {
        const auto makeCheckpoint = [&](const std::string& name) {
            const auto marker = findObject(name);
            if ((marker.type != 6 && marker.type != 13) || marker.model.empty() || marker.radius <= 0)
                throw std::runtime_error("Invalid retail checkpoint " + name);
            CityCheckpoint checkpoint{name, loadScene(assets, marker.model),
                                      marker.position, marker.rotation, marker.scale, marker.radius, {}, {}};
            const auto modelName = std::filesystem::u8path(marker.model).filename();
            if (modelName == "checkpoint_arrow.bdae" || modelName == "checkpoint_mission.bdae" ||
                modelName == "checkpoint_drive.bdae")
                checkpoint.animation = readClip(checkpoint.model, &checkpoint.model, "AUTO_SceneAnims_Range");
            return checkpoint;
        };
        if (tutorials.size() < 2) throw std::runtime_error("Missing retail movement tutorials.");
        const auto global = readLocalizedStrings(assets, "global", language);
        const auto label = [&](uint32_t id) {
            constexpr uint32_t globalSheet = 3;
            const auto index = id & 0xfff;
            if ((id >> 12 & 0xf) != globalSheet || index >= global.size()) return std::string{};
            return global[index];
        };
        const auto& walking = tutorials.front();
        const auto& entering = tutorials[1];
        opening.checkpoint = makeCheckpoint(checkpointName);
        opening.checkpoint->tutorial = {walking.startMs, 0, label(walking.title),
                                        "Use WASD or the arrow keys to walk to the marker."};
        opening.checkpoint->nextTutorial = {entering.startMs - checkpointMs, 0, label(entering.title),
                                             "Approach the Lincoln and press E to enter or exit."};
        if (!payphoneName.empty()) {
            if (tutorials.size() != 4 || departureRadius <= 0 || departureMs < vehicleMs || !payphoneObjectiveId)
                throw std::runtime_error("Invalid retail payphone route.");
            const auto departure = findObject(departureName);
            auto destination = makeCheckpoint(payphoneName);
            const auto& driving = tutorials[2];
            destination.tutorial = {driving.startMs - vehicleMs, 0, label(driving.title),
                                    "Use W/S to accelerate or brake. Use A/D to steer."};
            const auto entry = *payphoneObjectiveId & 0xfff;
            if ((*payphoneObjectiveId >> 12 & 0xf) != 4 || entry >= chapterText.size())
                throw std::runtime_error("Invalid retail payphone objective text ID.");
            const auto& miniMap = tutorials[3];
            if (miniMap.startMs != departureMs)
                throw std::runtime_error("Invalid retail mini-map tutorial timing.");
            destination.nextTutorial = {0, 0, label(miniMap.title), chapterText[entry]};
            opening.payphone = CityPayphoneRoute{std::move(destination), departure.position,
                                                 departureRadius, departureMs - vehicleMs,
                                                 clearConditionsOnDeparture, {}, std::move(payphoneFades),
                                                 resolveDialogue(payphoneDialogue), std::move(payphoneAudio),
                                                 payphoneCinematicMs, {}, {}, {}, payphoneGameplayFadeMs};
            if (!fightObjectiveId || !fightTutorial)
                throw std::runtime_error("Missing retail payphone fight text.");
            const auto fightEntry = *fightObjectiveId & 0xfff;
            if ((*fightObjectiveId >> 12 & 0xf) != 4 || fightEntry >= chapterText.size())
                throw std::runtime_error("Invalid retail payphone fight objective text ID.");
            opening.payphone->gameplayObjective = chapterText[fightEntry];
            opening.payphone->gameplayTutorial = {0, 0, label(fightTutorial->title), label(fightTutorial->text)};
            opening.payphone->lockCommandPayload = std::move(fightLockPayload);
            opening.payphone->attackCommandPayload = std::move(fightAttackPayload);
            opening.payphone->killedCommandPayload = std::move(fightKilledPayload);
            opening.payphone->disableCommandPayload = std::move(fightDisablePayload);
            postFight.dialogue = resolveDialogue(postFightDialogue);
            if (postFight.unlockedScript) {
                auto next = scripts.script(*postFight.unlockedScript);
                const auto nextLength = next.integer();
                if (nextLength < 4) throw std::runtime_error("Incomplete retail next mission script.");
                auto command = next.record();
                if (command.integer() != 64) throw std::runtime_error("Missing retail next mission checkpoint.");
                auto payload = command.remainingBytes();
                const auto checkpoint = readCheckpointCommand(command, 64);
                auto fade = next.record();
                auto start = next.record();
                if (fade.integer() != 42 || start.integer() != 65)
                    throw std::runtime_error("Missing retail next mission title transition.");
                const auto fadeMs = readFadeCommand(fade, 42, 0).durationMs;
                const auto title = missionTitle(start.integer());
                start.finish();
                postFight.nextCheckpoint = CityPostFightScene::MissionCheckpoint{
                    makeCheckpoint(checkpoint.object), std::move(payload), fadeMs, title};
                previousCinematicShot.clear();
                auto& cinematic = postFight.nextCheckpoint->cinematic;
                readCinematic(readCinematic, next, nextLength - 3, 0, cinematic, nextMissionMoves,
                              nextMissionDialogue, nextMissionPlacements, &nextMissionAnimations, nullptr, nullptr, 66);
                cinematic.dialogue = resolveDialogue(nextMissionDialogue);
                if (!nextMissionObjectiveId || (*nextMissionObjectiveId >> 12 & 0xf) != 4 ||
                    (*nextMissionObjectiveId & 0xfff) >= chapterText.size() || next.remaining() < 8)
                    throw std::runtime_error("Invalid retail next mission vehicle objective.");
                auto stolen = next.record();
                if (stolen.integer() != 89) throw std::runtime_error("Missing retail next mission WaitStolen.");
                postFight.nextCheckpoint->vehicleEntryWait = stolen.remainingBytes();
                postFight.nextCheckpoint->targetVehicle = readScriptObject(stolen);
                postFight.nextCheckpoint->vehicleObjective = chapterText[*nextMissionObjectiveId & 0xfff];
                auto delay = next.record();
                auto tutorial = next.record();
                auto protect = next.record();
                auto drive = next.record();
                if (delay.integer() != 57 || tutorial.integer() != 112 || protect.integer() != 70 ||
                    drive.integer() != 105)
                    throw std::runtime_error("Missing retail Hummer checkpoint sequence.");
                postFight.nextCheckpoint->vehicleRouteDelay = delay.remainingBytes();
                delay.signedInteger();
                delay.finish();
                postFight.nextCheckpoint->vehicleTutorialCommand = tutorial.remainingBytes();
                const auto radioTutorial = readScriptTutorial(tutorial);
                postFight.nextCheckpoint->vehicleTutorial = {0, 0, label(radioTutorial.title),
                    "Press [ or ] or use the D-pad to change radio stations. Press E or A to continue."};
                postFight.nextCheckpoint->vehicleProtectionCommand = protect.remainingBytes();
                const auto protection = readMissionCondition(protect, 70);
                if (!protection || protection->target != postFight.nextCheckpoint->targetVehicle)
                    throw std::runtime_error("Retail Hummer protection names a different vehicle.");
                postFight.nextCheckpoint->vehicleCheckpointWait = drive.remainingBytes();
                const auto destination = readCheckpointCommand(drive, 105);
                if (destination.vehicle != postFight.nextCheckpoint->targetVehicle)
                    throw std::runtime_error("Retail Hummer checkpoint names a different vehicle.");
                postFight.nextCheckpoint->vehicleCheckpoint = makeCheckpoint(destination.object);
                auto hide = next.record();
                auto exit = next.record();
                if (hide.integer() != 48 || exit.integer() != 99 ||
                    readScriptObject(exit) != postFight.nextCheckpoint->targetVehicle)
                    throw std::runtime_error("Invalid retail Hummer checkpoint exit.");
                hide.finish();
                previousCinematicShot.clear();
                auto& pier = postFight.nextCheckpoint->pierCinematic;
                readCinematic(readCinematic, next, nextLength, 0, pier, pierMoves,
                              pierDialogue, pierPlacements, &pierAnimations, &pierMotions, nullptr, 66);
                pier.dialogue = resolveDialogue(pierDialogue);
                if (!nextMissionObjectiveId || (*nextMissionObjectiveId >> 12 & 0xf) != 4 ||
                    (*nextMissionObjectiveId & 0xfff) >= chapterText.size())
                    throw std::runtime_error("Invalid retail weapon shop objective.");
                auto& route = *postFight.nextCheckpoint;
                route.weaponShopObjective = chapterText[*nextMissionObjectiveId & 0xfff];
                auto driveToShop = next.record();
                if (driveToShop.integer() != 105)
                    throw std::runtime_error("Missing retail weapon shop drive checkpoint.");
                route.weaponShopDriveWait = driveToShop.remainingBytes();
                const auto shopDestination = readCheckpointCommand(driveToShop, 105);
                if (shopDestination.vehicle != route.targetVehicle)
                    throw std::runtime_error("Retail weapon shop route names a different vehicle.");
                route.weaponShopDriveCheckpoint = makeCheckpoint(shopDestination.object);
                auto shopExit = next.record();
                if (shopExit.integer() != 99 || readScriptObject(shopExit) != route.targetVehicle)
                    throw std::runtime_error("Invalid retail weapon shop exit.");
                previousCinematicShot.clear();
                readCinematic(readCinematic, next, nextLength, 0, route.weaponShopCinematic,
                              shopMoves, shopDialogue, shopPlacements, nullptr, &shopMotions, nullptr, 76);
                route.weaponShopCinematic.dialogue = resolveDialogue(shopDialogue);
                auto approach = next.record();
                auto shopTutorial = next.record();
                if (approach.integer() != 64 || shopTutorial.integer() != 47)
                    throw std::runtime_error("Missing retail weapon shop approach sequence.");
                route.weaponShopEntrance = std::move(weaponShopEntrance);
                route.weaponShopEntranceMarker = makeCheckpoint(route.weaponShopEntrance);
                route.weaponShopApproachWait = approach.remainingBytes();
                route.weaponShopApproach = makeCheckpoint(readCheckpointCommand(approach, 64).object);
                route.weaponShopTutorialCommand = shopTutorial.remainingBytes();
                const auto shopHelp = readScriptTutorial(shopTutorial);
                route.weaponShopTutorial = {0, 0, label(shopHelp.title), label(shopHelp.text)};
                // MenuManager::s_WeaponShopData in the supported retail library.
                route.weaponShopInventory = {
                    {100, 73, 2, 0x3045, "health", 0, label(0x3045)},
                    {200, 73, 1, 0x3046, "vest", 1, label(0x3046)},
                    {250, 163, 1, 0x303f, "beretta", 0, label(0x303f)},
                    {750, 163, 5, 0x3040, "uzi", 0, label(0x3040)},
                    {1000, 163, 4, 0x3041, "shotgun", 0, label(0x3041)},
                    {1500, 163, 3, 0x3042, "ak47", 1, label(0x3042)},
                    {2500, 163, 8, 0x3043, "sniper", 1, label(0x3043)},
                    {5000, 163, 7, 0x3044, "rpg", 1, label(0x3044)},
                };
                auto unlockShop = next.record();
                auto blink = next.record();
                auto enterShop = next.record();
                if (unlockShop.integer() != 77 || blink.integer() != 111 || enterShop.integer() != 64)
                    throw std::runtime_error("Missing retail weapon shop unlock and entrance gate.");
                route.weaponShopUnlock = unlockShop.remainingBytes();
                route.weaponShopEntranceWait = enterShop.remainingBytes();
                if (readScriptObject(unlockShop) != route.weaponShopEntrance ||
                    readCheckpointCommand(enterShop, 64).object != route.weaponShopEntrance)
                    throw std::runtime_error("Retail weapon shop entrance commands name different shops.");
                auto hideShop = next.record();
                if (hideShop.integer() != 48) throw std::runtime_error("Missing retail shop tutorial dismissal.");
                hideShop.finish();
                const auto readReturnObjective = [&] {
                    auto command = next.record();
                    if (command.integer() != 66) throw std::runtime_error("Missing retail LC return objective.");
                    const auto id = readMissionObjective(command);
                    if ((id >> 12 & 0xf) != 4 || (id & 0xfff) >= chapterText.size())
                        throw std::runtime_error("Invalid retail LC return objective.");
                    return chapterText[id & 0xfff];
                };
                route.returnToHummerObjective = readReturnObjective();
                auto returnToHummer = next.record();
                if (returnToHummer.integer() != 89)
                    throw std::runtime_error("Missing retail return to Hummer gate.");
                route.returnToHummerWait = returnToHummer.remainingBytes();
                if (readScriptObject(returnToHummer) != route.targetVehicle)
                    throw std::runtime_error("Retail return gate names a different vehicle.");
                route.returnToLcObjective = readReturnObjective();
                auto returnToLc = next.record();
                if (returnToLc.integer() != 105)
                    throw std::runtime_error("Missing retail drive to LC gate.");
                route.returnToLcWait = returnToLc.remainingBytes();
                const auto lcDestination = readCheckpointCommand(returnToLc, 105);
                if (lcDestination.vehicle != route.targetVehicle)
                    throw std::runtime_error("Retail LC checkpoint names a different vehicle.");
                route.returnToLcCheckpoint = makeCheckpoint(lcDestination.object);
                auto arrivalFade = next.record();
                auto wait = next.record();
                auto exitLc = next.record();
                auto clearConditions = next.record();
                auto startScene = next.record();
                if (arrivalFade.integer() != 42 || wait.integer() != 57 || exitLc.integer() != 99 ||
                    clearConditions.integer() != 68 || startScene.integer() != 15 ||
                    readScriptObject(exitLc) != route.targetVehicle)
                    throw std::runtime_error("Invalid retail LC arrival sequence.");
                route.returnToLcFadeMs = readFadeCommand(arrivalFade, 42, 0).durationMs;
                if (!route.returnToLcFadeMs) throw std::runtime_error("Missing retail LC arrival fade duration.");
                route.returnToLcWaitMs = wait.integer();
                wait.finish();
                clearConditions.finish();
                startScene.finish();
                previousCinematicShot.clear();
                auto& closing = route.lcClosingCinematic;
                closing.fades.push_back({0, 0, false});
                readCinematic(readCinematic, next, nextLength, 0, closing, lcClosingMoves,
                              lcClosingDialogue, lcClosingPlacements, &lcClosingAnimations,
                              &lcClosingMotions, nullptr, 67);
                closing.dialogue = resolveDialogue(lcClosingDialogue);
                auto unlockNext = next.record();
                if (unlockNext.integer() != 3) throw std::runtime_error("Missing retail LC mission handoff.");
                route.lcUnlockedScript = unlockNext.integer();
                unlockNext.finish();
                auto postLc = scripts.script(*route.lcUnlockedScript);
                const auto postLcLength = postLc.integer();
                if (postLcLength < 3) throw std::runtime_error("Incomplete retail post-LC mission script.");
                auto gate = postLc.record();
                auto postLcFade = postLc.record();
                auto postLcStart = postLc.record();
                if (gate.integer() != 64 || postLcFade.integer() != 42 || postLcStart.integer() != 65)
                    throw std::runtime_error("Invalid retail post-LC mission entrance.");
                route.postLcCheckpointWait = gate.remainingBytes();
                if (readCheckpointCommand(gate, 64).object != route.marker.name)
                    throw std::runtime_error("Retail post-LC mission entrance changed location.");
                route.postLcFadeMs = readFadeCommand(postLcFade, 42, 0).durationMs;
                route.postLcTitle = missionTitle(postLcStart.integer());
                postLcStart.finish();
                previousCinematicShot.clear();
                route.postLcIntro.fades.push_back({0, 0, false});
                readCinematic(readCinematic, postLc, postLcLength - 3, 0, route.postLcIntro, postLcMoves,
                              postLcDialogue, postLcPlacements, &postLcAnimations, &postLcMotions, nullptr, 66);
                route.postLcIntro.dialogue = resolveDialogue(postLcDialogue);
                if (!nextMissionObjectiveId || (*nextMissionObjectiveId >> 12 & 0xf) != 4 ||
                    (*nextMissionObjectiveId & 0xfff) >= chapterText.size())
                    throw std::runtime_error("Invalid retail post-LC drive objective.");
                route.postLcDriveObjective = chapterText[*nextMissionObjectiveId & 0xfff];
                auto vehicleWait = postLc.record();
                auto protectVehicle = postLc.record();
                auto driveWait = postLc.record();
                if (vehicleWait.integer() != 89 || protectVehicle.integer() != 70 || driveWait.integer() != 105)
                    throw std::runtime_error("Invalid retail post-LC drive sequence.");
                route.postLcVehicleWait = vehicleWait.remainingBytes();
                route.postLcVehicleProtection = protectVehicle.remainingBytes();
                route.postLcDriveWait = driveWait.remainingBytes();
                const auto postLcProtection = readMissionCondition(protectVehicle, 70);
                const auto postLcDestination = readCheckpointCommand(driveWait, 105);
                if (readScriptObject(vehicleWait) != route.targetVehicle || !postLcProtection ||
                    postLcProtection->target != route.targetVehicle || postLcDestination.vehicle != route.targetVehicle)
                    throw std::runtime_error("Retail post-LC drive names a different vehicle.");
                route.postLcDriveCheckpoint = makeCheckpoint(postLcDestination.object);
                auto exitPostLc = postLc.record();
                if (exitPostLc.integer() != 99 || readScriptObject(exitPostLc) != route.targetVehicle)
                    throw std::runtime_error("Invalid retail post-LC vehicle exit.");
                auto postLcArrivalFade = postLc.record();
                if (postLcArrivalFade.integer() != 42)
                    throw std::runtime_error("Missing retail Loco scene fade.");
                route.postLcArrivalFadeMs = readFadeCommand(postLcArrivalFade, 42, 0).durationMs;
                if (!route.postLcArrivalFadeMs) throw std::runtime_error("Invalid retail Loco scene fade duration.");
                previousCinematicShot.clear();
                route.postLcAmbush.fades.push_back({0, 0, false});
                readCinematic(readCinematic, postLc, postLcLength, 0, route.postLcAmbush, postLcAmbushMoves,
                              postLcAmbushDialogue, postLcAmbushPlacements, &postLcAmbushAnimations, &postLcAmbushMotions, nullptr, 66, &postLcAmbushDrives);
                for (const auto& drive : postLcAmbushDrives) {
                    const auto marker = findObject(drive.marker);
                    if (marker.type != 10 || drive.actor != route.targetVehicle || !drive.speedKmh)
                        throw std::runtime_error("Invalid retail cinematic vehicle destination.");
                    // Convert the retail km/h argument to centimetres per millisecond.
                    route.postLcAmbush.vehicleDrives.push_back(
                        {drive.time, drive.actor, marker.position, drive.speedKmh / 36.f});
                }
                route.postLcAmbush.dialogue = resolveDialogue(postLcAmbushDialogue);
                if (!nextMissionObjectiveId || (*nextMissionObjectiveId >> 12 & 0xf) != 4 ||
                    (*nextMissionObjectiveId & 0xfff) >= chapterText.size())
                    throw std::runtime_error("Invalid retail Loco objective.");
                route.postLcAmbushObjective = chapterText[*nextMissionObjectiveId & 0xfff];
            }
            opening.payphone->postFight = std::move(postFight);
            // WeaponManager::createWeapon maps unarmed weapon type 0 to metatype 50.
            const auto fists = city.weaponMetatypes.find(50);
            if (fists == city.weaponMetatypes.end() || fists->second.weaponType != 0 || fists->second.range <= 0)
                throw std::runtime_error("Missing retail Fists weapon range.");
            opening.payphone->handRangeCm = static_cast<float>(fists->second.range);
            opening.payphone->handDamage = fists->second.damage;
            const auto resolvePlacements = [&](const auto& commands, auto& placements) {
                for (const auto& [timeMs, cue] : commands) {
                    CityCinematicPlacement placement{timeMs, cue.actor, {}, {},
                        cue.action != CityScriptActorAction::disable};
                    if (placement.visible) {
                        const auto marker = findObject(cue.marker);
                        if (marker.type == 3 && cue.action == CityScriptActorAction::moveTo) {
                            const auto target = std::find_if(placements.rbegin(), placements.rend(), [&](const auto& previous) {
                                return previous.actor == cue.marker && previous.visible;
                            });
                            placement.position = target == placements.rend() ? marker.position : target->position;
                            placement.rotationDegrees = target == placements.rend() ? marker.rotation : target->rotationDegrees;
                        } else if (marker.type != 10) {
                            throw std::runtime_error("Invalid retail cinematic actor marker " + cue.marker);
                        } else {
                            placement.position = marker.position;
                            placement.rotationDegrees = marker.rotation;
                        }
                    }
                    placements.push_back(std::move(placement));
                }
            };
            resolvePlacements(payphonePlacements, opening.payphone->cinematicPlacements);
            resolvePlacements(postFightPlacements, opening.payphone->postFight.placements);
            if (auto& next = opening.payphone->postFight.nextCheckpoint) {
                resolvePlacements(nextMissionPlacements, next->cinematic.placements);
                resolvePlacements(pierPlacements, next->pierCinematic.placements);
                resolvePlacements(shopPlacements, next->weaponShopCinematic.placements);
                resolvePlacements(lcClosingPlacements, next->lcClosingCinematic.placements);
                resolvePlacements(postLcPlacements, next->postLcIntro.placements);
                resolvePlacements(postLcAmbushPlacements, next->postLcAmbush.placements);
            }
        }
    }
    const auto bindVehicle = [&](CityActor& placed, const PlacementObject& definition) {
        placed.radioStation = definition.radioStation;
        if (!definition.vehicle) return;
        // Vehicle::Load copies the signed word at offset 128 into health.
        placed.vehicleHealth = definition.vehicle->vehicleWords[(128 - 72) / 4];
        placed.vehicleMaxHealth = definition.vehicle->vehicleWords[(156 - 72) / 4];
        // Vehicle::Load copies these fields into the offsets used by VehicleCamera::init.
        const auto vehicleWord = [&](uint32_t offset) {
            return static_cast<float>(definition.vehicle->vehicleWords[(offset - 72) / 4]);
        };
        if (const auto profile = vehicleWord(124); profile >= 0 && profile < 4)
            placed.gearProfile = static_cast<unsigned>(profile);
        // Sound type 1 selects Vehicle::InitV6naSounds.
        if (vehicleWord(176) == 1 && vehicleWord(160) > 0)
            placed.v6EngineSpeedRange = vehicleWord(160);
        if (const auto distance = vehicleWord(152); distance > 0)
            placed.vehicleCamera = CityVehicleCamera{
                distance, vehicleWord(100), vehicleWord(144), vehicleWord(140),
                std::clamp(vehicleWord(164), 1.f, distance)};
        placed.vehicleDamageEnabled = (definition.vehicle->objectFields[0] & 4) != 0;
        // Vehicle::Load copies the fixed-point field at offset 208 into the impact multiplier.
        placed.collisionDamageScale = definition.vehicle->vehicleWords[(208 - 72) / 4] * (1.f / 65536);
        // Vehicle::Load decodes the travel limits, and Vehicle::CreateBody assigns one quarter of the mass to each wheel.
        const float mass = vehicleWord(148);
        if (mass > 0 && vehicleWord(104) > 0 && vehicleWord(160) > 0)
            placed.drive = CityVehicleDrive{mass, vehicleWord(104), vehicleWord(76), vehicleWord(160)};
        constexpr float fixed16 = 1.f / 65536;
        if (mass > 0 && vehicleWord(192) >= 0 && vehicleWord(180) >= 0 &&
            vehicleWord(160) >= 0 && vehicleWord(184) >= 0)
            placed.suspension = CityVehicleSuspension{mass * 0.25f, vehicleWord(192) * fixed16 * 1000,
                vehicleWord(180) * fixed16, vehicleWord(160) * 1000 / 3600, vehicleWord(184) * fixed16};
        constexpr std::array<uint32_t, 2> doorLibraryOffsets{132, 172};
        for (size_t side = 0; side < doorLibraryOffsets.size(); ++side) {
            const auto& path = definition.vehicle->textFields.at(doorLibraryOffsets[side]);
            if (path.empty()) continue;
            placed.doorClips[side] = loadCityDoorClips(assets, path);
            bindCityAnimationTracks(placed.doorClips[side], placed.model);
        }
        if (!placed.doorClips[0].empty() || !placed.doorClips[1].empty())
            placed.skins = readActorSkins(placed.model);
    };
    const CityVehicleDefinition* openingVehicle = nullptr;
    for (const auto& placement : placements) {
        const auto marker = findObject(placement.marker);
        // PlayerTest::switchContext uses char_pthug for the city context.
        const auto actor = placement.actor == "Player" ?
            PlacementObject{5, "./char_pthug.bdae", {}, {}, {1, 1, 1}} : findObject(placement.actor);
        if (marker.type != 10 || actor.model.empty())
            throw std::runtime_error("Invalid retail intro placement for " + placement.actor);
        opening.actors.push_back({placement.actor, loadScene(assets, actor.model, true, true),
            marker.position, marker.rotation, actor.scale, placement.startMs});
        auto& placed = opening.actors.back();
        bindVehicle(placed, actor);
        if (const auto disabled = disabledActors.find(placement.actor); disabled != disabledActors.end())
            opening.actors.back().endMs = disabled->second;
        if (placement.actor == vehicleName) {
            opening.vehicleActor = opening.actors.size() - 1;
            constexpr size_t waitStolenHighlight = 1; // LevelObject::showHighlight(1).
            auto highlights = loadCityHighlights(assets);
            if (waitStolenHighlight >= highlights.size())
                throw std::runtime_error("Missing retail WaitStolen highlight.");
            opening.vehicleObjective = std::move(highlights[waitStolenHighlight]);
            const auto findMount = [&](std::string_view name) -> std::optional<size_t> {
                const auto mount = std::find_if(placed.model.nodes.begin(), placed.model.nodes.end(),
                    [&](const CityNode& node) { return node.name == name; });
                if (mount == placed.model.nodes.end()) return std::nullopt;
                return static_cast<size_t>(mount - placed.model.nodes.begin());
            };
            const auto firstLight = findMount("dummy_light_01-node");
            const auto secondLight = findMount("dummy_light_02-node");
            if (firstLight && secondLight) {
                const auto loadLight = [&](const char* path) {
                    auto scene = loadCityScene(assets, path);
                    if (scene.particleEmitters.empty()) readParticleEmitters(BresReader(scene), scene, true);
                    // These meshes have UVs and an image entry separate from the diffuse effect.
                    if (scene.images.size() == 1 && scene.materials.size() == 1) {
                        auto& material = scene.materials.begin()->second;
                        if (material.texture.empty()) material.texture = scene.images.front();
                    }
                    return scene;
                };
                placed.lights = CityVehicleLights{{*firstLight, *secondLight},
                    {loadLight("car_light_00.bdae"), loadLight("car_light_01.bdae")}};
            }
            openingVehicle = actor.vehicle;
        }
    }
    if (opening.vehicleProtection && (!opening.vehicleActor ||
        opening.actors[*opening.vehicleActor].name != opening.vehicleProtection->target ||
        !opening.actors[*opening.vehicleActor].vehicleHealth))
        throw std::runtime_error("Missing opening mission vehicle " + opening.vehicleProtection->target);
    if (opening.vehicleProtection) {
        // GSFailure::draw2d uses string ID 0x304d for a mission failure.
        constexpr size_t titleIndex = 0x04d;
        const auto text = readLocalizedStrings(assets, "global", language);
        if (titleIndex < text.size()) opening.vehicleProtection->failureTitle = text[titleIndex];
        // The failure screen's input prompt is string ID 0x5001.
        const auto init = readLocalizedStrings(assets, "init", language);
        if (init.size() > 1) opening.vehicleProtection->failurePrompt = init[1];
    }
    if (!animations.empty() || !payphoneAnimations.empty()) {
        const auto library = loadBres(assets, "anim_ingame.bdae");
        for (const auto& command : animations) {
            const auto actor = std::find_if(opening.actors.begin(), opening.actors.end(),
                                            [&](const CityActor& entry) { return entry.name == command.actor; });
            if (actor == opening.actors.end())
                throw std::runtime_error("Missing retail animation actor " + command.actor);
            if (actor->animations.empty()) actor->skins = readActorSkins(actor->model);
            if (actor->skins.empty())
                throw std::runtime_error("Missing retail skin for " + command.actor);
            actor->animations.push_back({command.startMs, readClip(library, &actor->model, command.first),
                                          readClip(library, &actor->model, command.loop)});
        }
        const auto player = std::find_if(opening.actors.begin(), opening.actors.end(),
                                         [](const CityActor& actor) { return actor.name == "Player"; });
        const auto bindVehiclePlayer = [&](const CityVehicleDefinition& vehicle, const CityScene& playerModel,
                                           auto& seated, auto& characterClips) {
            const bool low = vehicle.textFields.at(108) == "car_enter_car_low";
            seated = readClip(library, &playerModel, low ? "car_seated_idle_low" : "car_seated_idle_hi");
            const std::string height = low ? "low_" : "hi_";
            auto& clips = characterClips.emplace();
            for (size_t side = 0; side < clips.size(); ++side) {
                const std::string suffix = side == 0 ? "left" : "right";
                clips[side] = {
                    readClip(library, &playerModel, "car_open_door_" + suffix),
                    readClip(library, &playerModel, "car_enter_car_" + height + suffix),
                    readClip(library, &playerModel, "car_close_door_from_inside_" + height + suffix),
                    readClip(library, &playerModel, "car_exit_" + height + suffix)
                };
            }
        };
        if (openingVehicle && player != opening.actors.end())
            bindVehiclePlayer(*openingVehicle, player->model, opening.vehicleSeated, opening.vehicleCharacterClips);
        if (opening.followCameraMs && player != opening.actors.end()) {
            opening.walk = readClip(library, &player->model, "move_walk");
            for (const auto& channel : opening.walk.channels) {
                if (channel.transform != CityAnimationTransform::translation || channel.target != "Bip01-node") continue;
                const auto& first = channel.keys.front().value;
                const auto& last = channel.keys.back().value;
                opening.walkSpeed = std::hypot(last[0] - first[0], last[1] - first[1]) * 1000 / opening.walk.durationMs;
            }
            if (opening.walkSpeed <= 0) throw std::runtime_error("Missing retail walk root motion.");
        }
        if (opening.payphone) {
            auto& route = *opening.payphone;
            if (payphoneWalk) {
                const auto& [start, actor, markerName] = *payphoneWalk;
                const auto marker = findObject(markerName);
                const auto from = std::find_if(route.cinematicPlacements.rbegin(), route.cinematicPlacements.rend(),
                    [&](const auto& cue) { return cue.timeMs <= start && cue.actor == actor && cue.visible; });
                if (marker.type != 10 || from == route.cinematicPlacements.rend() || opening.walkSpeed <= 0)
                    throw std::runtime_error("Invalid retail payphone walk.");
                const auto distance = std::hypot(marker.position[0] - from->position[0],
                                                 marker.position[1] - from->position[1]);
                route.walk = CityCinematicWalk{start, static_cast<uint32_t>(std::ceil(distance * 1000 / opening.walkSpeed)),
                                              actor, from->position, marker.position};
                const auto delay = route.walk->durationMs;
                if (delay > UINT32_MAX - route.cinematicDurationMs)
                    throw std::runtime_error("Retail payphone walk timeline overflow.");
                route.cinematicDurationMs += delay;
                const auto shift = [&](uint32_t& time) { if (time > start) time += delay; };
                for (auto& cue : route.cinematicPlacements) shift(cue.timeMs);
                for (auto& cue : route.cinematicFades) shift(cue.startMs);
                for (auto& cue : route.cinematicAudio) shift(cue.timeMs);
                for (auto& cue : route.cinematicDialogue) shift(cue.startMs);
                for (auto& cue : payphoneMoves) shift(cue.startMs);
                for (auto& cue : payphoneAnimations) {
                    if (cue.startMs == start && cue.actor == actor) cue.startMs += delay;
                    else shift(cue.startMs);
                }
            }
            if (player != opening.actors.end()) {
                player->handPunches = std::array{
                    readClip(library, &player->model, "combat_hand_rightpunch"),
                    readClip(library, &player->model, "combat_hand_leftpunch")};
                for (const auto& command : payphoneAnimations) {
                    if (command.actor != player->name) continue;
                    opening.payphone->playerAnimations.push_back({
                        command.startMs, readClip(library, &player->model, command.first),
                        readClip(library, &player->model, command.loop)});
                }
            }
            for (const auto& cue : opening.payphone->cinematicPlacements) {
                if (!cue.visible || cue.actor == "Player" ||
                    std::any_of(opening.actors.begin(), opening.actors.end(), [&](const CityActor& actor) {
                        return actor.payphoneOnly && actor.name == cue.actor;
                    })) continue;
                const auto definition = findObject(cue.actor);
                if (definition.model.empty()) throw std::runtime_error("Missing retail payphone actor " + cue.actor);
                opening.actors.push_back({cue.actor, loadScene(assets, definition.model, true, true),
                    cue.position, cue.rotationDegrees, definition.scale, 0});
                auto& actor = opening.actors.back();
                actor.payphoneOnly = true;
                if (cue.actor == "Loco_00") {
                    actor.pursuitWalk = readClip(library, &actor.model, "move_walk");
                    actor.handPunches = std::array{
                        readClip(library, &actor.model, "combat_hand_rightpunch"),
                        readClip(library, &actor.model, "combat_hand_leftpunch")};
                    actor.defeatAnimation = CityActorAnimation{0,
                        readClip(library, &actor.model, "move_idle_to_dead"),
                        readClip(library, &actor.model, "move_dead_idle")};
                }
                actor.skins = readActorSkins(actor.model);
                if (actor.skins.empty()) throw std::runtime_error("Missing retail payphone skin for " + cue.actor);
                for (const auto& command : payphoneAnimations) {
                    if (command.actor != cue.actor) continue;
                    actor.animations.push_back({command.startMs, readClip(library, &actor.model, command.first),
                                               readClip(library, &actor.model, command.loop)});
                }
                if (actor.animations.empty() || actor.animations.front().startMs > cue.timeMs) {
                    const auto idle = readClip(library, &actor.model, "move_idle");
                    actor.animations.insert(actor.animations.begin(), {cue.timeMs, idle, idle});
                }
                if (route.walk && route.walk->actor == actor.name) {
                    const auto walk = readClip(library, &actor.model, "move_walk");
                    const auto next = std::lower_bound(actor.animations.begin(), actor.animations.end(),
                        route.walk->startMs, [](const auto& animation, auto time) { return animation.startMs < time; });
                    actor.animations.insert(next, {route.walk->startMs, walk, walk});
                }
            }
            const auto findCinematicActor = [&](std::string_view name) {
                return std::find_if(opening.actors.begin(), opening.actors.end(), [&](const CityActor& entry) {
                    return entry.name == name && (entry.name == "Player" || entry.payphoneOnly);
                });
            };
            if (route.postFight.nextCheckpoint) {
                auto& next = *route.postFight.nextCheckpoint;
                const auto retailVehicle = findObject(next.targetVehicle).vehicle;
                const auto playerActor = findCinematicActor("Player");
                if (!retailVehicle || playerActor == opening.actors.end())
                    throw std::runtime_error("Missing next mission vehicle or player model.");
                bindVehiclePlayer(*retailVehicle, playerActor->model, next.vehicleSeated, next.vehicleCharacterClips);
                const auto loadCinematicActors = [&](const CityMissionCinematic& scene) {
                    for (const auto& cue : scene.placements) {
                        if (!cue.visible || findCinematicActor(cue.actor) != opening.actors.end()) continue;
                        const auto definition = findObject(cue.actor);
                        if (definition.model.empty())
                            throw std::runtime_error("Missing retail cinematic model " + cue.actor);
                        opening.actors.push_back({cue.actor, loadScene(assets, definition.model, true, true),
                            cue.position, cue.rotationDegrees, definition.scale, 0});
                        auto& actor = opening.actors.back();
                        actor.payphoneOnly = true;
                        bindVehicle(actor, definition);
                        if (!definition.vehicle) {
                            actor.skins = readActorSkins(actor.model);
                            if (actor.skins.empty())
                                throw std::runtime_error("Missing retail cinematic skin " + cue.actor);
                        }
                    }
                };
                loadCinematicActors(next.cinematic);
                loadCinematicActors(next.pierCinematic);
                loadCinematicActors(next.weaponShopCinematic);
                loadCinematicActors(next.lcClosingCinematic);
                loadCinematicActors(next.postLcIntro);
                loadCinematicActors(next.postLcAmbush);
            }
            const auto bindCinematic = [&](CityMissionCinematic& scene, const auto& commands) {
                for (const auto& command : commands) {
                    const auto actor = findCinematicActor(command.actor);
                    if (actor == opening.actors.end()) {
                        if (std::any_of(scene.placements.begin(), scene.placements.end(),
                            [&](const auto& cue) { return cue.actor == command.actor && cue.visible; }))
                            throw std::runtime_error("Missing retail cinematic actor " + command.actor);
                        continue;
                    }
                    scene.actorAnimations[command.actor].push_back({
                        command.startMs, readClip(library, &actor->model, command.first),
                        readClip(library, &actor->model, command.loop)});
                }
            };
            bindCinematic(route.postFight, postFightAnimations);
            if (auto& next = route.postFight.nextCheckpoint) {
                bindCinematic(next->cinematic, nextMissionAnimations);
                bindCinematic(next->pierCinematic, pierAnimations);
                bindCinematic(next->lcClosingCinematic, lcClosingAnimations);
                bindCinematic(next->postLcIntro, postLcAnimations);
                bindCinematic(next->postLcAmbush, postLcAmbushAnimations);
                for (const auto& cue : next->weaponShopCinematic.placements) {
                    if (!cue.visible || cue.actor == next->targetVehicle) continue;
                    const auto actor = findCinematicActor(cue.actor);
                    if (actor == opening.actors.end())
                        throw std::runtime_error("Missing retail weapon shop actor " + cue.actor);
                    auto idle = readClip(library, &actor->model, "move_idle");
                    next->weaponShopCinematic.actorAnimations[cue.actor].push_back({cue.timeMs, idle, idle});
                }
                const auto bindMotions = [&](CityMissionCinematic& scene, const std::vector<MotionCommand>& commands) {
                    for (const auto& motion : commands) {
                        const auto actor = findCinematicActor(motion.actor);
                        const auto destination = findObject(motion.marker);
                        const auto origin = std::find_if(scene.placements.rbegin(), scene.placements.rend(),
                            [&](const auto& placement) {
                                return placement.timeMs <= motion.time && placement.actor == motion.actor && placement.visible;
                            });
                        if (actor == opening.actors.end() ||
                            (motion.followsPath ? destination.type != 14 : destination.type != 10 && destination.type != 6) ||
                            origin == scene.placements.rend())
                            throw std::runtime_error("Invalid retail cinematic movement for " + motion.actor);
                        auto clip = readClip(library, &actor->model, motion.running ? "move_run" : "move_walk");
                        const auto root = std::find_if(clip.channels.begin(), clip.channels.end(), [](const auto& channel) {
                            return channel.transform == CityAnimationTransform::translation && channel.target == "Bip01-node";
                        });
                        if (root == clip.channels.end())
                            throw std::runtime_error("Missing retail locomotion root track.");
                        const auto& first = root->keys.front().value;
                        const auto& last = root->keys.back().value;
                        const auto stride = std::hypot(last[0] - first[0], last[1] - first[1]);
                        if (stride <= 0) throw std::runtime_error("Invalid retail locomotion stride.");
                        auto waypoints = destination.waypoints;
                        const auto endpoint = motion.followsPath ? waypoints.back() : destination.position;
                        auto previous = origin->position;
                        float distance = 0;
                        const auto accumulate = [&](const auto& point) {
                            distance += std::hypot(point[0] - previous[0], point[1] - previous[1]);
                            previous = point;
                        };
                        if (motion.followsPath) {
                            for (const auto& point : waypoints) accumulate(point);
                        } else accumulate(endpoint);
                        scene.motions.push_back({{motion.time, static_cast<uint32_t>(std::ceil(distance * clip.durationMs / stride)),
                                                 motion.actor, origin->position, endpoint, std::move(waypoints)}, std::move(clip)});
                    }
                };
                bindMotions(next->pierCinematic, pierMotions);
                bindMotions(next->weaponShopCinematic, shopMotions);
                bindMotions(next->lcClosingCinematic, lcClosingMotions);
                bindMotions(next->postLcIntro, postLcMotions);
                bindMotions(next->postLcAmbush, postLcAmbushMotions);
            }
        }
    }

    for (size_t i = 0; i < city.districts.size(); ++i) {
        const auto& district = city.districts[i];
        CityDataReader map(district.objectsPath, district.objects);
        const auto count = map.integer();
        std::map<std::string, CityCamera> shots;
        for (uint32_t j = 0; j < count; ++j) {
            auto record = map.record();
            if (record.integer() != 7) continue;  // SceneCamera
            const auto name = record.string();
            if (!names.count(name)) continue;
            CityCamera shot{};
            record.skip(16);  // GameObjectBasic flags, list, and highlight fields.
            record.string();  // sceneName
            record.string();  // nodeName
            record.skip(4);   // poolSize
            for (auto& coordinate : shot.eye) coordinate = record.signedInteger();
            record.skip(25);  // Rotation, scale, and the SceneCamera boolean.
            shot.farPlane = record.signedInteger() / 65536.f;
            shot.fovDegrees = record.signedInteger() / 65536.f;
            shot.nearPlane = record.signedInteger() / 65536.f;
            for (auto& coordinate : shot.target) coordinate = record.signedInteger();
            if (shot.nearPlane <= 0 || shot.farPlane <= shot.nearPlane ||
                shot.fovDegrees <= 0 || shot.fovDegrees >= 180)
                throw std::runtime_error("Invalid retail opening camera projection.");
            shots.emplace(name, shot);
            if (shots.size() == names.size()) {
                opening.district = i;
                if (opening.followCameraMs) {
                    // GS3DStuff::load uses one named heightmap for the world.
                    opening.heightMap = loadScene(assets, "santa_monica_heightmap.bdae");
                    if (opening.heightMap.meshes.size() != 1)
                        throw std::runtime_error("Expected one retail world navigation heightmap mesh.");
                    opening.navigation = loadNavigation(assets, opening.heightMap.meshes.front());
                }
                for (const auto& move : moves)
                    opening.cameras.push_back({shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                if (opening.payphone) {
                    for (const auto& move : payphoneMoves)
                        opening.payphone->cinematicCameras.push_back(
                            {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                    for (const auto& move : postFightMoves)
                        opening.payphone->postFight.cameras.push_back(
                            {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                    if (auto& next = opening.payphone->postFight.nextCheckpoint) {
                        for (const auto& move : nextMissionMoves)
                            next->cinematic.cameras.push_back(
                                {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                        for (const auto& move : pierMoves)
                            next->pierCinematic.cameras.push_back(
                                {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                        for (const auto& move : shopMoves)
                            next->weaponShopCinematic.cameras.push_back(
                                {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                        for (const auto& move : lcClosingMoves)
                            next->lcClosingCinematic.cameras.push_back(
                                {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                        for (const auto& move : postLcMoves)
                            next->postLcIntro.cameras.push_back(
                                {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                        for (const auto& move : postLcAmbushMoves)
                            next->postLcAmbush.cameras.push_back(
                                {shots.at(move.first), shots.at(move.second), move.startMs, move.durationMs});
                    }
                }
                opening.namedCameras = std::move(shots);
                return opening;
            }
        }
    }
    throw std::runtime_error("Could not resolve the opening cameras in a retail city district.");
}
