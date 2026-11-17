#include "retail_save.h"
#include "IFileStream.h"
#include "filesystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
constexpr size_t maxSaveBytes = 16 * 1024 * 1024;

class SaveReader {
public:
    explicit SaveReader(const SaveBytes& bytes) : bytes(bytes), limit(bytes.size()) {}

    size_t remaining() const { return limit - offset; }
    size_t position() const { return offset; }

    uint8_t byte()
    {
        if (!remaining()) fail();
        return bytes[offset++];
    }

    uint32_t word()
    {
        uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(byte()) << shift;
        return value;
    }

    int32_t integer()
    {
        const auto bits = word();
        int32_t value;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

    float scalar()
    {
        const auto bits = word();
        float value;
        std::memcpy(&value, &bits, sizeof value);
        if (!std::isfinite(value)) fail();
        return value;
    }

    size_t startBlock()
    {
        const auto size = word();
        if (size > remaining()) fail();
        const auto previous = limit;
        limit = offset + size;
        return previous;
    }

    void finishBlock(size_t previous)
    {
        if (remaining()) fail();
        limit = previous;
    }

    std::string string()
    {
        const auto length = word();
        if (length != remaining()) fail();
        const auto* text = reinterpret_cast<const char*>(bytes.data() + offset);
        offset += length;
        return std::string(text, length);
    }

    void skipBlock() { offset = limit; }

    SaveBytes unknown()
    {
        SaveBytes payload(bytes.begin() + offset, bytes.begin() + limit);
        offset = limit;
        return payload;
    }

    [[noreturn]] void fail() const
    {
        throw std::runtime_error("Invalid retail save block at byte " + std::to_string(offset) + '.');
    }

private:
    const SaveBytes& bytes;
    size_t limit;
    size_t offset = 0;
};

enum class Layout { integer, pair, ammo, weapon, position, text, opaque };

Layout layout(int32_t id)
{
    switch (id) {
    case 2: return Layout::weapon;
    case 3: return Layout::ammo;
    case 4: return Layout::position;
    case 5: return Layout::text;
    case 8: case 15: case 17: return Layout::pair;
    default: return id >= -1 && id <= 19 ? Layout::integer : Layout::opaque;
    }
}

SaveValue decode(SaveReader& reader, Layout type)
{
    switch (type) {
    case Layout::integer: return reader.integer();
    case Layout::pair: return SavePair{reader.integer(), reader.integer()};
    case Layout::ammo: return SaveAmmo{reader.integer(), reader.integer(), reader.integer()};
    case Layout::weapon:
        return SaveWeapon{reader.integer(), reader.integer(), reader.integer(), reader.integer(), reader.byte()};
    case Layout::position: return SavePosition{reader.scalar(), reader.scalar(), reader.scalar()};
    case Layout::text: return reader.string();
    case Layout::opaque: return reader.unknown();
    }
    throw std::logic_error("Unknown save block layout.");
}

void putWord(SaveBytes& bytes, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
}

SaveBytes encode(const SaveBlock& block)
{
    SaveBytes bytes;
    const auto put = [&](int32_t value) { putWord(bytes, static_cast<uint32_t>(value)); };
    switch (layout(block.id)) {
    case Layout::integer:
        put(std::get<int32_t>(block.value));
        break;
    case Layout::pair: {
        const auto value = std::get<SavePair>(block.value);
        put(value.first);
        put(value.second);
        break;
    }
    case Layout::ammo: {
        const auto value = std::get<SaveAmmo>(block.value);
        put(value.type);
        put(value.context);
        put(value.count);
        break;
    }
    case Layout::weapon: {
        const auto value = std::get<SaveWeapon>(block.value);
        put(value.id);
        put(value.clipSize);
        put(value.clip);
        put(value.context);
        bytes.push_back(value.locked);
        break;
    }
    case Layout::position:
        for (const auto value : std::get<SavePosition>(block.value)) {
            if (!std::isfinite(value)) throw std::runtime_error("Invalid save position.");
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            putWord(bytes, bits);
        }
        break;
    case Layout::text: {
        const auto& text = std::get<std::string>(block.value);
        if (text.size() > maxSaveBytes - 4) throw std::runtime_error("Save string is too large.");
        putWord(bytes, static_cast<uint32_t>(text.size()));
        bytes.insert(bytes.end(), text.begin(), text.end());
        break;
    }
    case Layout::opaque:
        return std::get<SaveBytes>(block.value);
    }
    return bytes;
}
}

RetailSavedState restoreRetailSavedState(const std::vector<SaveBlock>& blocks, int32_t resetContext)
{
    const auto checkContext = [](int32_t context) {
        if (context < 0 || context > 1) throw std::runtime_error("Invalid save player context.");
    };
    checkContext(resetContext);
    auto context = resetContext;
    RetailSavedState state;
    for (const auto& block : blocks) {
        const auto integer = [&] { return std::get<int32_t>(block.value); };
        const auto positive = [&] { return integer() > 0; };
        switch (block.id) {
        case 0: state.completedScripts.push_back({integer(), -1}); break;
        case 1: state.cash = integer(); break;
        case 2: {
            auto weapon = std::get<SaveWeapon>(block.value);
            const auto key = std::make_pair(weapon.context, weapon.id);
            const auto previous = state.weapons.find(key);
            if (previous != state.weapons.end() && !previous->second.locked) weapon.locked = 0;
            state.weapons.insert_or_assign(key, weapon);
            break;
        }
        case 3: {
            const auto ammo = std::get<SaveAmmo>(block.value);
            state.ammo[{ammo.context, ammo.type}] = ammo.count;
            break;
        }
        case 4: state.position = std::get<SavePosition>(block.value); break;
        case 6: state.callContactTutorial = positive(); break;
        case 7: state.wantedTutorial = positive(); break;
        case 8: {
            const auto stat = std::get<SavePair>(block.value);
            if (stat.first >= 0) state.statUpdates.push_back(stat);
            break;
        }
        case 9: state.deathCutscene = positive(); break;
        case 10: state.bustedCutscene = positive(); break;
        case 11: state.failedMissionCutscene = positive(); break;
        case 12:
            context = integer();
            checkContext(context);
            state.context = context;
            break;
        case 13: state.weaponLimit = integer(); break;
        case 14: state.playerFlag383 = true; break;
        case 15: {
            const auto script = std::get<SavePair>(block.value);
            state.completedScripts.push_back({script.first, script.second});
            break;
        }
        case 16: state.completedAchievements.push_back(integer()); break;
        case 17: state.armor[context] = std::min<int32_t>(200, std::get<SavePair>(block.value).second); break;
        case 18: state.cameraTutorial = positive(); break;
        case 19: state.sniperTutorial = positive(); break;
        }
    }
    return state;
}

std::vector<SaveBlock> decodeRetailSave(const SaveBytes& bytes)
{
    if (bytes.empty() || bytes.size() > maxSaveBytes)
        throw std::runtime_error("Invalid retail save size.");
    SaveReader reader(bytes);
    std::vector<SaveBlock> blocks;
    while (reader.remaining()) {
        const auto id = reader.integer();
        const auto previous = reader.startBlock();
        blocks.push_back({id, decode(reader, layout(id))});
        reader.finishBlock(previous);
    }
    return blocks;
}

std::vector<SaveBlock> sampleRetailSave(const RetailSavedState& state, int32_t version, int32_t resetContext)
{
    const auto activeContext = state.context.value_or(resetContext);
    if (resetContext < 0 || resetContext > 1 || activeContext < 0 || activeContext > 1)
        throw std::runtime_error("Invalid save player context.");
    std::vector<SaveBlock> blocks{{-1, version}};
    const auto add = [&](int32_t id, const auto& value) {
        if (value) blocks.push_back({id, *value});
    };
    const auto flag = [&](int32_t id, const std::optional<bool>& value) {
        if (value) blocks.push_back({id, int32_t(*value)});
    };
    for (const auto& script : state.completedScripts) {
        if (script.context == -1) blocks.push_back({0, script.script});
        else blocks.push_back({15, SavePair{script.script, script.context}});
    }
    add(1, state.cash);
    for (const auto& [key, weapon] : state.weapons) {
        if (key.first < 0 || key.first > 1 || key.first != weapon.context || key.second != weapon.id)
            throw std::runtime_error("Invalid save weapon key.");
    }
    for (const auto& [key, count] : state.ammo) {
        if (key.first < 0 || key.first > 1)
            throw std::runtime_error("Invalid save ammo context.");
    }
    auto context = resetContext;
    for (int32_t entry = 0; entry < 2; ++entry) {
        for (const auto& [key, count] : state.ammo)
            if (key.first == entry) blocks.push_back({3, SaveAmmo{key.second, entry, count}});
        for (const auto& [key, weapon] : state.weapons)
            if (key.first == entry) blocks.push_back({2, weapon});
        if (!state.armor[entry]) continue;
        // The retail reader applies armor to the active context, even when the pair names another.
        if (context != entry) {
            blocks.push_back({12, entry});
            context = entry;
        }
        blocks.push_back({17, SavePair{entry, *state.armor[entry]}});
    }
    add(4, state.position);
    flag(6, state.callContactTutorial);
    flag(7, state.wantedTutorial);
    for (const auto& stat : state.statUpdates) blocks.push_back({8, stat});
    flag(9, state.deathCutscene);
    flag(10, state.bustedCutscene);
    flag(11, state.failedMissionCutscene);
    if (state.context || context != activeContext) blocks.push_back({12, activeContext});
    add(13, state.weaponLimit);
    if (state.playerFlag383) blocks.push_back({14, int32_t{1}});
    for (const auto achievement : state.completedAchievements) blocks.push_back({16, achievement});
    flag(18, state.cameraTutorial);
    flag(19, state.sniperTutorial);
    return blocks;
}

static SaveBytes readSaveBytes(IFileStream& file)
{
    if (!file.size() || file.size() > maxSaveBytes) throw std::runtime_error("Invalid retail save size.");
    file.seek(0);
    SaveBytes bytes(static_cast<size_t>(file.size()));
    if (file.tell() != 0 || file.read(bytes.data(), bytes.size()) != bytes.size())
        throw std::runtime_error("Could not read retail save.");
    return bytes;
}

std::vector<SaveBlock> readRetailSave(IFileStream& file)
{
    return decodeRetailSave(readSaveBytes(file));
}

RetailAudioPreferences readRetailAudioPreferences(IFileStream& file)
{
    const auto bytes = readSaveBytes(file);
    SaveReader reader(bytes);
    RetailAudioPreferences preferences;
    while (reader.remaining()) {
        const auto id = reader.integer();
        const auto end = reader.startBlock();
        // Preferences::SampleAndSave writes effects as block 4 and music as block 5.
        if (id == 4 || id == 5) {
            const auto percent = reader.integer();
            const auto volume = static_cast<float>(std::clamp(percent, 0, 100)) / 100;
            (id == 4 ? preferences.effects : preferences.music) = volume;
        } else {
            reader.skipBlock();
        }
        reader.finishBlock(end);
    }
    return preferences;
}

namespace {
void updatePreferences(const ResourceFileSystem& files, const std::vector<std::pair<uint32_t, int32_t>>& updates)
{
    SaveBytes bytes;
    if (const std::unique_ptr<IFileStream> file{files.openSavedFile(retailPreferencesFilename)})
        bytes = readSaveBytes(*file);
    std::vector<bool> found(updates.size());
    SaveReader reader(bytes);
    while (reader.remaining()) {
        const auto id = reader.integer();
        const auto end = reader.startBlock();
        const auto entry = std::find_if(updates.begin(), updates.end(), [id](const auto& update) {
            return update.first == id;
        });
        if (entry != updates.end()) {
            const auto offset = reader.position();
            reader.integer();
            const auto value = static_cast<uint32_t>(entry->second);
            for (unsigned i = 0; i < 4; ++i)
                bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
            found[entry - updates.begin()] = true;
        } else {
            reader.skipBlock();
        }
        reader.finishBlock(end);
    }
    const auto append = [&](uint32_t id, int32_t value) {
        if (bytes.size() > maxSaveBytes - 12) throw std::runtime_error("Retail preferences exceed the supported size.");
        putWord(bytes, id);
        putWord(bytes, 4);
        putWord(bytes, static_cast<uint32_t>(value));
    };
    for (size_t i = 0; i < updates.size(); ++i)
        if (!found[i]) append(updates[i].first, updates[i].second);
    files.replaceSaveFile(retailPreferencesFilename, bytes);
}
}

void saveRetailAudioPreferences(const ResourceFileSystem& files, const RetailAudioPreferences& preferences)
{
    const auto percentage = [](float volume) -> int32_t {
        if (!std::isfinite(volume)) throw std::invalid_argument("Invalid retail audio volume.");
        return static_cast<int32_t>(std::lround(std::clamp(volume, 0.f, 1.f) * 100));
    };
    updatePreferences(files, {{4, percentage(preferences.effects)}, {5, percentage(preferences.music)}});
}

std::optional<RetailLanguage> readRetailLanguagePreference(IFileStream& file)
{
    const auto bytes = readSaveBytes(file);
    SaveReader reader(bytes);
    std::optional<RetailLanguage> language;
    while (reader.remaining()) {
        const auto id = reader.integer();
        const auto end = reader.startBlock();
        if (id == 2) {
            const auto pack = reader.word();
            language = pack < retailLanguagePacks.size() ?
                std::optional{retailLanguagePacks[pack]} : std::nullopt;
        } else {
            reader.skipBlock();
        }
        reader.finishBlock(end);
    }
    return language;
}

void saveRetailLanguagePreference(const ResourceFileSystem& files, RetailLanguage language)
{
    const auto pack = std::find(retailLanguagePacks.begin(), retailLanguagePacks.end(), language);
    if (pack == retailLanguagePacks.end()) throw std::invalid_argument("Unknown retail language pack.");
    updatePreferences(files, {{2, static_cast<int32_t>(pack - retailLanguagePacks.begin())}});
}

SaveBytes encodeRetailSave(const std::vector<SaveBlock>& blocks)
{
    if (blocks.empty()) throw std::runtime_error("A retail save must contain blocks.");
    SaveBytes bytes;
    for (const auto& block : blocks) {
        const auto payload = encode(block);
        if (bytes.size() > maxSaveBytes - 8 || payload.size() > maxSaveBytes - 8 - bytes.size())
            throw std::runtime_error("Retail save exceeds the supported size.");
        putWord(bytes, static_cast<uint32_t>(block.id));
        putWord(bytes, static_cast<uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    return bytes;
}

void writeRetailSave(const ResourceFileSystem& files, const RetailSavedState& state,
                     int32_t version, int32_t resetContext)
{
    files.replaceSaveFile(retailSaveFilename, encodeRetailSave(sampleRetailSave(state, version, resetContext)));
}

void writeWindowsOpeningCheckpoint(const ResourceFileSystem& files, const RetailSavedState& state)
{
    files.replaceSaveFile(windowsCheckpointFilename,
                          encodeRetailSave(sampleRetailSave(state, windowsOpeningCheckpointVersion, 0)));
}

std::optional<RetailSavedState> readWindowsOpeningCheckpoint(const ResourceFileSystem& files)
{
    const std::unique_ptr<IFileStream> file(files.openSavedFile(windowsCheckpointFilename));
    if (!file) return std::nullopt;
    const auto blocks = readRetailSave(*file);
    auto state = restoreRetailSavedState(blocks, 0);
    if (blocks.empty() || blocks.front().id != -1 ||
        std::get<int32_t>(blocks.front().value) != windowsOpeningCheckpointVersion ||
        state.context != 0 || !state.position || !state.cash || *state.cash < 0 ||
        !state.armor[0] || *state.armor[0] < 0 || *state.armor[0] > 200 ||
        !std::all_of(state.position->begin(), state.position->end(), [](float value) {
            return std::isfinite(value);
        }) || state.completedScripts.size() != 1 ||
        (state.completedScripts.front().script != windowsOpeningCheckpointScript &&
         state.completedScripts.front().script != windowsPostLcCheckpointScript &&
         state.completedScripts.front().script != windowsPostLocoCheckpointScript) ||
        state.completedScripts.front().context != -1)
        throw std::runtime_error("Unsupported Windows opening checkpoint.");
    return state;
}
