#pragma once

#include "retail_text.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

class IFileStream;
class ResourceFileSystem;

struct SavePair { int32_t first, second; };
struct SaveAmmo { int32_t type, context, count; };
struct SaveWeapon { int32_t id, clipSize, clip, context; uint8_t locked; };
using SavePosition = std::array<float, 3>;
using SaveBytes = std::vector<uint8_t>;
using SaveValue = std::variant<int32_t, SavePair, SaveAmmo, SaveWeapon, SavePosition, std::string, SaveBytes>;

struct SaveBlock {
    int32_t id;
    SaveValue value;
};

struct SaveScriptCompletion { int32_t script, context; };

// State supplied by the save after the retail reset, with absent fields left unset.
struct RetailSavedState {
    std::optional<int32_t> cash, context, weaponLimit;
    std::optional<SavePosition> position;
    std::optional<bool> callContactTutorial, wantedTutorial, deathCutscene,
        bustedCutscene, failedMissionCutscene, cameraTutorial, sniperTutorial;
    bool playerFlag383 = false;
    std::array<std::optional<int32_t>, 2> armor;
    // Keys are (context, weapon ID) and (context, ammo type).
    std::map<std::pair<int32_t, int32_t>, SaveWeapon> weapons;
    std::map<std::pair<int32_t, int32_t>, int32_t> ammo;
    std::vector<SaveScriptCompletion> completedScripts;
    std::vector<SavePair> statUpdates;
    std::vector<int32_t> completedAchievements;
};

struct RetailAudioPreferences {
    float effects = 0.8f;
    float music = 0.8f;
};

inline constexpr char retailPreferencesFilename[] = "Gangstar2.preferences";
inline constexpr char retailSaveFilename[] = "Gangstar2.sav";
inline constexpr char windowsCheckpointFilename[] = "OpenGangstar.sav";
inline constexpr int32_t windowsOpeningCheckpointScript = 18;
inline constexpr int32_t windowsPostLcCheckpointScript = 25;
inline constexpr int32_t windowsPostLocoCheckpointScript = 26;
inline constexpr int32_t windowsPostCooperCheckpointScript = 27;
inline constexpr int32_t windowsPostBrawlCheckpointScript = 28;
inline constexpr int32_t windowsPostBasketballCheckpointScript = 29;
inline constexpr int32_t windowsPostDeliveryCheckpointScript = 30;
inline constexpr int32_t windowsPostCooperDeliveryCheckpointScript = 31;
inline constexpr int32_t windowsPostVanCheckpointScript = 19;
inline constexpr int32_t windowsPostScript19CheckpointScript = 20;
inline constexpr int32_t windowsPostScript20CheckpointScript = 32;
inline constexpr int32_t windowsOpeningCheckpointVersion = 0x30500;
RetailAudioPreferences readRetailAudioPreferences(IFileStream& file);
std::optional<RetailLanguage> readRetailLanguagePreference(IFileStream& file);
void saveRetailLanguagePreference(const ResourceFileSystem& files, RetailLanguage language);
void saveRetailAudioPreferences(const ResourceFileSystem& files, const RetailAudioPreferences& preferences);

std::vector<SaveBlock> decodeRetailSave(const SaveBytes& bytes);
std::vector<SaveBlock> readRetailSave(IFileStream& file);
SaveBytes encodeRetailSave(const std::vector<SaveBlock>& blocks);
RetailSavedState restoreRetailSavedState(const std::vector<SaveBlock>& blocks, int32_t resetContext);
std::vector<SaveBlock> sampleRetailSave(const RetailSavedState& state, int32_t version, int32_t resetContext);
void writeRetailSave(const ResourceFileSystem& files, const RetailSavedState& state,
                     int32_t version, int32_t resetContext);
void writeWindowsOpeningCheckpoint(const ResourceFileSystem& files, const RetailSavedState& state);
std::optional<RetailSavedState> readWindowsOpeningCheckpoint(const ResourceFileSystem& files);
