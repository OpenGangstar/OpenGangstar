#pragma once

#include "retail_text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class FileSystemBase;

struct CityMeshBuffer {
    std::string material;
    std::vector<uint16_t> indices;
};

struct CityMesh {
    std::string name;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 4>> colors;
    std::vector<std::array<float, 2>> texCoords;
    std::vector<CityMeshBuffer> buffers;
    std::vector<std::array<float, 2>> auxiliaryTexCoords;
    std::vector<std::array<float, 4>> secondaryColors;
    bool floatNormals = true; // Original software reflection texgen accepts only float32 normals.
    // Exact serialized SMesh+20 box, copied by original CColladaMesh construction.
    // Missing on hand-built mesh fixtures; do not substitute vertex extrema for scene binding.
    std::optional<std::array<std::array<float, 3>, 2>> rawBoundsCm;
};

struct CityGeometryRef {
    std::string sourcePath;
    std::string meshName;
    struct MaterialRef {
        std::string sourcePath;
        std::string name;
    };
    std::vector<MaterialRef> materials;
};

enum class CityMaterialMode : uint32_t {
    solid = 0, lightmap = 3, reflection = 12, additive = 13,
    alphaChannel = 14, alphaReference = 15, transparentReflection = 17,
    textureVertexAlpha = 25
};

// Decoded scalar selectors, not the pointer-containing original SMaterial layout.
struct CityMaterialSelection {
    bool reflectionImage;
    uint32_t sourceTransparency;
    bool additive;
    uint8_t effectTransparency;
    float opacity;
    uint8_t lightmap;
};
CityMaterialMode selectCityMaterialMode(const CityMaterialSelection& selection);
CityMaterialMode cityParticleMaterialMode(CityMaterialMode mode);
bool cityMaterialModeSupported(CityMaterialMode mode);
bool cityMaterialTransparent(CityMaterialMode mode);

using CityTextureTransform = std::array<float, 6>;
inline constexpr CityTextureTransform cityIdentityTextureTransform{1, 0, 0, 0, 1, 0};
CityTextureTransform cityTextureTransform(float rotation, const std::array<float, 2>& translation,
                                         const std::array<float, 2>& scale);
enum class CityStageOneAlpha { textureTimesPrevious, previousSquared };
struct CityMaterialStageState {
    CityStageOneAlpha alpha = CityStageOneAlpha::textureTimesPrevious;
    void bind(CityMaterialMode mode);
};

struct CityMaterial {
    uint32_t diffuse = 0xffffffff;
    std::string texture;
    CityMaterialMode mode = CityMaterialMode::solid;
    std::vector<std::string> additionalTextures;
    std::array<CityTextureTransform, 2> textureTransforms{
        cityIdentityTextureTransform, cityIdentityTextureTransform};
};

struct CityNodeInstance {
    uint32_t kind = 0;
    std::optional<size_t> geometry; // Index into this node's decoded geometry, when constructed.
    std::string sourcePath, target;
    bool localSpline = false; // Geometry kind1 returns NULL from original constructGeometry.
};

struct CityNode {
    std::string name;
    std::string jointName;
    int32_t parent;
    std::array<float, 3> translation;
    std::array<float, 4> rotation;
    std::array<float, 3> scale;
    std::vector<CityGeometryRef> geometry;
    std::vector<std::string> particleEmitters;
    std::string userProperties;
    uint32_t factorySelector = 0; // Original SNode+76, separate from user-property billboard selection.
    std::vector<CityNodeInstance> instances; // Authored instance order, including skipped nonmesh records.
    std::string displayName; // Original SNode+4; name above retains original UID at +0.

    std::optional<std::string_view> property(std::string_view key) const;
};

struct CitySkinJoint {
    size_t node;
    std::array<float, 16> inverseBind;
    std::string scope; // Original joint string; runtime skin attachment resolves scope, not UID.
};

struct CitySkinInfluence {
    uint16_t joint;
    float weight;
};

struct CitySkin {
    std::string meshName;
    std::array<float, 16> bindShape;
    std::vector<CitySkinJoint> joints;
    std::vector<std::vector<CitySkinInfluence>> vertices;
    std::string controllerUid; // SController+4, distinct from the underlying geometry UID.
    uint32_t controllerIndex = 0; // Fixed-width source table index, not live instance identity.
    // Original SSkin+108/+112 ordered joint boxes. Unknown hand-built metadata
    // differs from an authored empty table (the joint-translation branch).
    std::optional<std::vector<std::array<std::array<float, 3>, 2>>> jointBoundsCm;
};

enum class CityParticleParameter : uint32_t {
    birthRate = 28, targetSize = 29, sizeVariation = 30, speedVariation = 32, animationLength = 43
};

struct CityParticleKey { uint32_t timeMs; float value; };
struct CityDiffuseColorKey { uint32_t timeMs; std::array<float, 4> rgba; };

struct CityParticleEmitter {
    std::string name;
    uint32_t type, maxParticles;
    std::array<float, 3> dimensions{};
    float birthRate, life, lifeVariation, targetSize, sizeVariation, sizeGrowthTime, sizeFadeTime,
          speed, speedVariation;
    std::map<CityParticleParameter, std::vector<CityParticleKey>> tracks;
    std::array<float, 3> direction{};
    float directionVariation = 0;
    uint32_t animationMapping = 0;
    float animationOffset = 0, animationOffsetVariation = 0, animationLength = 1,
          animationScale = 1, animationScaleVariation = 0;
    float radius = 0, height = 0;
};

struct CityMorphController {
    struct Target {
        size_t mesh;
        float weight;
    };
    std::string name, source;
    uint32_t method;
    std::vector<Target> targets;
};

struct CityVisualSceneInstance {
    std::string uid, displayName;
    size_t firstNode = 0, nodeCount = 0; // Decoded preorder range for this active instance.
};

struct CityScene {
    std::string path;
    std::vector<unsigned char> bytes;
    uint32_t rootOffset = 0;
    std::vector<CityMesh> meshes;
    std::vector<CityNode> nodes;
    std::vector<std::string> images;
    std::map<std::string, CityMaterial> materials;
    std::vector<CityParticleEmitter> particleEmitters;
    std::vector<CityDiffuseColorKey> diffuseColors;
    std::vector<CityMorphController> morphControllers;
    std::vector<CityVisualSceneInstance> visualScenes;
    std::vector<std::string> lightUids; // SCollada+44, 24-byte SLight records.
    bool hasSceneAnimations = false; // Static scene binding rejects unported animator behavior.
};

struct CityLodGroup {
    struct Level {
        size_t node;
        float distanceSquared;
    };
    std::vector<Level> levels;
    std::optional<float> maximumSquared;

    std::optional<size_t> select(float distanceSquared) const;
};

CityLodGroup readCityLodGroup(const CityScene& scene);

struct CityVehicleDefinition {
    std::string name, model, node;
    std::array<int32_t, 4> objectFields;
    int32_t poolSize;
    std::array<float, 3> position, rotation, scale;
    std::optional<uint32_t> radioStation;
    // The recovered record uses 32-bit fields at offsets 72 through 224.
    // String slots live in textFields, keyed by their recovered offsets.
    std::array<int32_t, 39> vehicleWords;
    std::map<uint32_t, std::string> textFields;
};

struct CitySpawnPoint {
    std::string name, model, node;
    std::array<int32_t, 4> objectFields;
    int32_t poolSize;
    std::array<float, 3> position, rotation, scale;
    int32_t objectType;
    uint16_t terrainBuffer, terrainIndexOffset;
    uint8_t group = 0;
    std::vector<int32_t> values;

    std::optional<std::array<uint16_t, 3>> terrainTriangle(const CityMesh& heightMesh) const;
};

struct CityDistrict {
    uint32_t flags = 0;
    std::string objectsPath;
    std::vector<unsigned char> objects;
    std::vector<CityVehicleDefinition> vehicles;
    std::vector<CitySpawnPoint> spawnPoints;
    CityScene scene;
    CityScene lowDetailScene;
    uint32_t descriptorValue = 0;
    std::string additionalPath;
};

struct CitySpawnMetatype {
    int32_t objectType;
    std::string name, model, node;
    std::array<int32_t, 4> objectFields;
    int32_t poolSize;
    std::array<float, 3> position, rotation, scale;
};

struct CitySpawnNeighbor {
    std::array<float, 3> position;
    int32_t objectType;
    uint32_t flags;
};

struct CitySpawnTraffic {
    uint8_t roadFlags;
    std::array<float, 3> actorPosition;
    std::array<float, 3> actorForward;
    float actorSpeed;
};

struct CitySpawnContext {
    std::array<float, 3> innerCenter, outerCenter;
    std::array<unsigned, 4> activeCounts; // Moving vehicles, civilians, parked vehicles, prisoners.
    std::vector<CitySpawnNeighbor> nearbyObjects;
    std::optional<CitySpawnTraffic> traffic;
};

inline constexpr bool cityRoadAllowsExit(uint8_t flags, size_t edge)
{
    const auto code = edge + 1;
    return (flags & 3) != code && ((flags >> 2) & 3) != code;
}

inline constexpr float cityRoadSpeedPerFrame(uint8_t flags)
{
    const auto code = flags >> 6;
    return (code ? code : 1) * 20.f;
}

inline constexpr bool isCityPoliceMetatype(uint8_t metatype)
{
    return metatype == 38 || metatype == 48 || metatype == 161;
}

struct CitySpawnGroup {
    int32_t objectType;
    std::vector<uint8_t> metatypes;

    uint8_t choose(uint32_t random) const;
    std::optional<uint8_t> chooseForSpawn(uint32_t random, int spawnedCops, int copLimit,
                                          bool alternateCopContext) const;
    uint32_t spawnDelayMs(bool eligible, uint32_t random) const;
    size_t populationIndex() const;
    bool canSpawn(const CitySpawnPoint& point, const CitySpawnContext& context) const;
    std::array<std::array<float, 3>, 2> spawnCenters(const std::array<float, 3>& eye,
                                                   const std::array<float, 3>& target, float actorHeight,
                                                   std::optional<float> vehicleSpeed) const;
};

struct CityWeaponMetatype {
    std::string name;
    int32_t ammoType, clipSize, cone, damage, dryShootFrequency;
    std::string muzzle;
    bool doubleWeapon, twoHanded;
    int32_t range, shootFrequency, weaponType;
};

struct CityAssets {
    std::vector<CityDistrict> districts;
    std::map<std::string, CityScene> geometryLibraries;
    std::set<std::string> usableLodModels;
    std::map<uint8_t, CitySpawnGroup> spawnGroups;
    std::map<uint8_t, CitySpawnMetatype> spawnMetatypes;
    std::map<uint32_t, CityWeaponMetatype> weaponMetatypes;
};

struct CityCamera {
    std::array<float, 3> eye;
    std::array<float, 3> target;
    float farPlane;
    float fovDegrees;
    float nearPlane;
};

struct CityCameraMove {
    CityCamera first;
    CityCamera second;
    uint32_t startMs;
    uint32_t durationMs;
};

struct CityFade {
    uint32_t startMs;
    uint32_t durationMs;
    bool fadeIn;
};

enum class CityAudioAction { playOnce, playPriorityLoop, stop };

struct CityAudioCue {
    uint32_t timeMs;
    uint32_t soundId;
    CityAudioAction action;
};

struct CityAnimationKey {
    uint32_t timeMs;
    std::array<float, 4> value;
};

enum class CityAnimationTransform { translation, translationZ, rotation, scale };

struct CityAnimationChannel {
    std::optional<size_t> node;
    CityAnimationTransform transform;
    std::vector<CityAnimationKey> keys;
    std::string target;
};

struct CityAnimationSource {
    uint32_t index;
    int32_t startMs, endMs;
};

struct CityAnimationClip {
    std::string name;
    uint32_t durationMs;
    std::vector<CityAnimationChannel> channels;
    std::optional<CityAnimationSource> source; // Original indexed range, not a name-derived identity.
};

// Unclipped source data for the original plain animator. Times remain signed
// source milliseconds; neither clip rebasing nor a second animation clock is
// applied. Source order/UID and target UID are separate identities.
struct CityAnimationLibraryKey {
    int32_t timeMs;
    std::array<float, 4> value;
};

struct CityAnimationLibraryTrack {
    uint32_t sourceIndex;
    std::string sourceUid, target;
    CityAnimationTransform transform;
    uint32_t interpolationSelector;
    uint32_t inputSourceIndex, outputSourceIndex;
    std::vector<CityAnimationLibraryKey> keys;
};

struct CityAnimationLibrary {
    std::string path;
    // Exact SCollada+12 word. The original plain animator uses nonzero to
    // select shared key-index scratch; this is not an interpolation selector.
    uint32_t commonCacheMetadata;
    std::vector<CityAnimationLibraryTrack> tracks;
};

struct CityActorAnimation {
    uint32_t startMs;
    CityAnimationClip first;
    CityAnimationClip loop;
};

struct CityVehicleCharacterClips {
    CityAnimationClip open, enter, close, exit;
};

struct CityVehicleCamera {
    float setback, eyeHeight, targetHeight, targetForward, minimumSetback;
};

struct CityVehicleSuspension {
    float wheelMass, springRate, damping, lowerTravel, upperTravel;
};

struct CityVehicleLights {
    std::array<size_t, 2> mountNodes;
    std::array<CityScene, 2> effects;
};

struct CityVehicleDrive {
    float massKg;
    float forwardForce;
    float reverseForce;
    float topSpeedKmh;
};

struct CityActor {
    std::string name;
    CityScene model;
    std::array<float, 3> position;
    std::array<float, 3> rotationDegrees;
    std::array<float, 3> scale;
    uint32_t startMs;
    std::vector<CityActorAnimation> animations;
    std::vector<CitySkin> skins;
    uint32_t endMs = UINT32_MAX;
    std::optional<uint32_t> radioStation;
    std::array<std::vector<CityAnimationClip>, 2> doorClips;
    std::optional<int32_t> vehicleHealth;
    int32_t vehicleMaxHealth = 0;
    bool vehicleDamageEnabled = false;
    float collisionDamageScale = 0;
    std::optional<CityVehicleCamera> vehicleCamera;
    std::optional<CityVehicleLights> lights;
    std::optional<CityVehicleSuspension> suspension;
    std::optional<unsigned> gearProfile;
    std::optional<CityVehicleDrive> drive;
    std::optional<float> v6EngineSpeedRange;
    bool payphoneOnly = false;
    std::optional<CityAnimationClip> pursuitWalk;
    std::optional<CityAnimationClip> fightIdle;
    std::optional<int32_t> npcHealth;
    std::optional<CityWeaponMetatype> scriptedRangedWeapon;
    std::optional<CityAnimationClip> scriptedAim;
    std::optional<std::array<CityAnimationClip, 2>> handPunches;
    std::optional<CityActorAnimation> defeatAnimation;
};

struct CityDialogue {
    uint32_t startMs;
    uint32_t durationMs;
    std::string speaker;
    std::string text;
    int32_t portrait = -1;
};

struct CityCheckpoint {
    std::string name;
    CityScene model;
    std::array<float, 3> position;
    std::array<float, 3> rotationDegrees;
    std::array<float, 3> scale;
    float radius;
    CityDialogue tutorial;
    CityDialogue nextTutorial;
    std::optional<CityAnimationClip> animation;
};

struct CityCinematicPlacement {
    uint32_t timeMs;
    std::string actor;
    std::array<float, 3> position;
    std::array<float, 3> rotationDegrees;
    bool visible;
};

struct CityCinematicWalk {
    uint32_t startMs;
    uint32_t durationMs;
    std::string actor;
    std::array<float, 3> from;
    std::array<float, 3> to;
    std::vector<std::array<float, 3>> waypoints;
    std::string targetActor;
};

struct CityMissionTitle {
    uint32_t id;
    std::string sprite;
    uint32_t animation;
    std::string text;
    int32_t cashReward;
    std::string completionText;
};

struct CityMissionCinematic {
    std::vector<CityCameraMove> cameras;
    std::vector<CityFade> fades;
    std::vector<CityDialogue> dialogue;
    std::vector<CityAudioCue> audio;
    std::vector<CityCinematicPlacement> placements;
    std::map<std::string, int32_t, std::less<>> npcWeapons;
    std::vector<std::vector<uint8_t>> killTags;
    std::map<std::string, std::vector<CityActorAnimation>> actorAnimations;
    struct Motion {
        CityCinematicWalk travel;
        CityAnimationClip clip;
    };
    std::vector<Motion> motions;
    struct VehicleDrive {
        uint32_t startMs;
        std::string actor;
        std::array<float, 3> destination;
        float speedCmPerMs;
    };
    std::vector<VehicleDrive> vehicleDrives;
    std::vector<std::pair<uint32_t, std::string>> deferredDisables;
    uint32_t durationMs = 0;
    uint32_t nextScript = 0;
    std::optional<uint32_t> followCameraMs, missionOverMs;
    uint32_t followCameraTransitionMs = 0;
};

struct CityPostFightScene : CityMissionCinematic {
    struct WeaponShopItem {
        uint32_t price, category, index, labelId;
        std::string_view key;
        uint32_t flags;
        std::string label;
    };
    std::optional<uint32_t> unlockedScript;
    struct MissionCheckpoint {
        CityCheckpoint marker;
        std::vector<uint8_t> payload;
        uint32_t fadeMs;
        CityMissionTitle title;
        CityMissionCinematic cinematic;
        std::string vehicleObjective;
        std::string targetVehicle;
        std::vector<uint8_t> vehicleEntryWait;
        std::vector<uint8_t> vehicleRouteDelay;
        std::vector<uint8_t> vehicleTutorialCommand;
        std::vector<uint8_t> vehicleProtectionCommand;
        CityDialogue vehicleTutorial{};
        std::vector<uint8_t> vehicleCheckpointWait;
        std::optional<CityCheckpoint> vehicleCheckpoint;
        CityMissionCinematic pierCinematic;
        std::string weaponShopObjective;
        std::vector<uint8_t> weaponShopDriveWait;
        std::optional<CityCheckpoint> weaponShopDriveCheckpoint;
        CityMissionCinematic weaponShopCinematic;
        std::optional<CityCheckpoint> weaponShopApproach;
        std::vector<uint8_t> weaponShopApproachWait;
        std::string weaponShopEntrance;
        std::optional<CityCheckpoint> weaponShopEntranceMarker;
        std::vector<uint8_t> weaponShopUnlock;
        std::vector<uint8_t> weaponShopTutorialCommand;
        std::vector<uint8_t> weaponShopEntranceWait;
        CityDialogue weaponShopTutorial{};
        std::vector<WeaponShopItem> weaponShopInventory;
        std::string returnToHummerObjective, returnToLcObjective;
        std::vector<uint8_t> returnToHummerWait, returnToLcWait;
        std::optional<CityCheckpoint> returnToLcCheckpoint;
        uint32_t returnToLcFadeMs = 0, returnToLcWaitMs = 0;
        CityMissionCinematic lcClosingCinematic;
        std::optional<uint32_t> lcUnlockedScript;
        std::optional<CityMissionTitle> postLcTitle;
        std::vector<uint8_t> postLcCheckpointWait;
        uint32_t postLcFadeMs = 0;
        CityMissionCinematic postLcIntro;
        std::string postLcDriveObjective;
        std::vector<uint8_t> postLcVehicleWait, postLcVehicleProtection, postLcDriveWait;
        std::optional<CityCheckpoint> postLcDriveCheckpoint;
        uint32_t postLcArrivalFadeMs = 0;
        CityMissionCinematic postLcAmbush;
        std::string postLcAmbushObjective;
        CityMissionCinematic postLcTail;
        CityMissionCinematic postLcConfrontation;
        std::string postLcConfrontationObjective;
        std::array<std::optional<CityCheckpoint>, 3> postLcWalkCheckpoints;
        std::array<std::vector<uint8_t>, 3> postLcWalkWaits;
        CityMissionCinematic postLcPhone;
        CityMissionCinematic postLcFightIntro;
        std::string postLcFightObjective;
        std::optional<uint32_t> locoUnlockedScript;
        std::vector<uint8_t> locoNextCheckpointWait;
        uint32_t locoNextFadeMs = 0;
        std::optional<CityMissionTitle> locoNextTitle;
        CityMissionCinematic locoNextIntro;
        std::string locoNextObjective;
        CityMissionCinematic locoCooperIntro;
        std::string locoCooperObjective;
        std::optional<CityCinematicPlacement> cooperDriverPlacement;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> cooperEscapeCommands;
        std::optional<uint32_t> cooperUnlockedScript;
        std::vector<uint8_t> cooperNextCheckpointWait;
        uint32_t cooperNextFadeMs = 0;
        std::optional<CityMissionTitle> cooperNextTitle;
        CityMissionCinematic cooperNextIntro;
        std::string cooperNextDriveObjective;
        std::optional<CityCheckpoint> cooperNextDriveCheckpoint;
        uint32_t cooperNextArrivalFadeMs = 0;
        CityMissionCinematic cooperNextArrival;
        std::string cooperNextFightObjective;
        CityMissionCinematic cooperNextClosing;
        std::optional<uint32_t> brawlUnlockedScript;
        std::vector<uint8_t> brawlNextCheckpointWait;
        uint32_t brawlNextFadeMs = 0;
        std::optional<CityMissionTitle> brawlNextTitle;
        CityMissionCinematic brawlNextIntro;
        std::string brawlNextObjective;
        std::optional<CityCheckpoint> basketballCheckpoint;
        std::vector<uint8_t> basketballWait;
        CityMissionCinematic basketballArrival;
        CityMissionCinematic basketballClosing;
        uint32_t basketballTutorialHideMs = 0;
        uint32_t basketballMissionOverMs = 0;
        std::optional<uint32_t> basketballUnlockedScript;
        std::vector<uint8_t> postBasketballCheckpointWait;
        uint32_t postBasketballFadeMs = 0;
        std::optional<CityMissionTitle> postBasketballTitle;
        CityMissionCinematic postBasketballIntro;
        std::string postBasketballObjective;
        std::optional<CityCheckpoint> postBasketballDestination;
        std::vector<uint8_t> postBasketballDestinationWait;
        std::string postBasketballVehicle;
        std::vector<CityCinematicPlacement> postBasketballApproach;
        CityMissionCinematic postBasketballAmbush;
        std::vector<std::string> postBasketballAmbushTargets;
        std::string postBasketballAmbushObjective;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> postBasketballEscapeCommands;
        std::string postBasketballEscapeMarker;
        std::array<float, 3> postBasketballEscapePosition{};
        uint32_t postBasketballBriefingFadeMs = 0;
        CityMissionCinematic postBasketballBriefing;
        std::string repairDriveObjective;
        std::optional<CityCheckpoint> repairDriveCheckpoint;
        std::vector<uint8_t> repairDriveWait;
        CityMissionCinematic repairArrival;
        CityDialogue repairTutorial;
        std::optional<CityCheckpoint> repairShopApproach, repairShopEntrance;
        std::string repairPaymentPrompt;
        int32_t repairPriceAtZeroHealth = 0;
        std::string deliveryObjective;
        std::optional<CityCheckpoint> deliveryCheckpoint;
        std::vector<uint8_t> deliveryWait;
        CityMissionCinematic deliveryClosing;
        std::vector<uint32_t> deliveryUnlockedScripts;
        std::optional<CityCheckpoint> elToroCheckpoint;
        std::vector<uint8_t> elToroCheckpointWait;
        uint32_t elToroFadeMs = 0;
        std::optional<CityMissionTitle> elToroTitle;
        CityMissionCinematic elToroIntro;
        std::string elToroObjective;
        std::optional<CityCheckpoint> elToroDestination;
        std::vector<uint8_t> elToroDestinationWait;
        CityMissionCinematic elToroArrival;
        std::string elToroFightObjective;
        std::vector<std::string> elToroFightTargets;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> elToroFightCommands;
        CityMissionCinematic elToroInterlude;
        std::vector<std::string> elToroSecondFightTargets;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> elToroSecondFightCommands;
        CityMissionCinematic elToroDeparture;
        std::string elToroDriveObjective;
        std::optional<CityCheckpoint> elToroDriveCheckpoint;
        std::vector<uint8_t> elToroDriveWait, elToroCooperProtection;
        std::optional<std::array<CityVehicleCharacterClips, 2>> elToroCooperClips;
        std::optional<CityAnimationClip> elToroCooperSeated;
        uint32_t elToroClosingFadeMs = 0;
        CityMissionCinematic elToroClosing;
        std::optional<uint32_t> elToroUnlockedScript;
        std::vector<uint8_t> vanEntranceWait;
        uint32_t vanEntranceFadeMs = 0;
        std::optional<CityMissionTitle> vanMissionTitle;
        CityMissionCinematic vanMissionIntro;
        std::string vanMissionObjective;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> vanAcquisitionCommands;
        std::optional<std::array<CityVehicleCharacterClips, 2>> vanVehicleClips;
        std::optional<CityAnimationClip> vanVehicleSeated;
        CityMissionCinematic vanBriefing;
        std::string vanDriveObjective;
        std::optional<CityCheckpoint> vanDriveCheckpoint;
        std::vector<uint8_t> vanDriveWait;
        uint32_t vanArrivalFadeMs = 0;
        CityMissionCinematic vanArrival, vanReentryBriefing;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> vanReentryCommands;
        std::optional<CityCheckpoint> vanSecondDriveCheckpoint;
        std::vector<uint8_t> vanSecondDriveWait;
        uint32_t vanSecondArrivalFadeMs = 0;
        CityMissionCinematic vanSecondArrival;
        std::string vanProtectObjective;
        CityMissionCinematic vanProtectionApproach;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> vanProtectionCommands;
        CityMissionCinematic vanHandshake, vanHandshakeBriefing;
        std::optional<uint32_t> vanHandshakeEntryMs;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> vanHandshakeCommands;
        std::optional<CityCheckpoint> vanThirdDriveCheckpoint;
        std::vector<uint8_t> vanThirdDriveWait;
        uint32_t vanThirdArrivalFadeMs = 0;
        CityMissionCinematic vanThirdArrival, vanPoliceReveal;
        std::string vanPoliceWaitObjective, vanPoliceObjective;
        std::vector<uint8_t> vanPoliceWait;
        std::optional<int32_t> vanPoliceWantedLevel;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> vanPoliceEscapeCommands;
        std::string vanLosePoliceObjective, vanEscapeClearedObjective, vanFourthDriveObjective;
        uint32_t vanEscapeClearedObjectiveId = 0;
        CityMissionCinematic vanEscapeDebrief;
        std::optional<CityCheckpoint> vanFourthDriveCheckpoint;
        std::vector<uint8_t> vanFourthDriveWait, vanFinalEntryWait;
        uint32_t vanFourthArrivalFadeMs = 0;
        CityMissionCinematic vanFourthArrival;
        std::string vanFinalEntryObjective;
        uint32_t vanClosingFadeMs = 0;
        CityMissionCinematic vanClosing;
        uint32_t vanUnlockedScript = 0;
        std::vector<uint8_t> vanUnlockedWait;
        uint32_t script19EntranceFadeMs = 0;
        std::optional<CityMissionTitle> script19Title;
        CityMissionCinematic script19Intro, script19Arrival;
        uint32_t script19ArrivalFadeMs = 0, script19ArrivalWaitMs = 0;
        std::string script19FightObjective;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script19FightCommands;
        uint32_t script19InterludeFadeMs = 0;
        CityMissionCinematic script19Interlude, script19SecondDebrief;
        std::vector<CityCinematicPlacement> script19SecondFightPlacements;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script19SecondFightCommands;
        uint32_t script19SecondFightFadeMs = 0;
        std::vector<CityCinematicPlacement> script19ThirdFightPlacements;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script19ThirdFightCommands;
        std::optional<CityCheckpoint> script19ReturnCheckpoint;
        std::string script19ReturnObjective;
        CityMissionCinematic script19Closing;
        uint32_t script19ClosingFadeMs = 0, script19ClosingWaitMs = 0, script19MissionOverMs = 0;
        uint32_t script19UnlockedScript = 0;
        std::vector<uint8_t> script19UnlockedWait;
        uint32_t script20EntranceFadeMs = 0;
        std::optional<CityMissionTitle> script20Title;
        CityMissionCinematic script20Intro, script20Arrival, script20Interlude, script20Retreat, script20Closing;
        uint32_t script20UnlockedScript = 0;
        std::optional<CityCheckpoint> script32Checkpoint;
        std::string script32Objective;
        std::vector<uint8_t> script32CheckpointWait;
        uint32_t script32EntranceFadeMs = 0;
        CityMissionCinematic script32Intro;
        uint32_t script32UnlockedScript = 0;
        std::optional<CityCheckpoint> script33Checkpoint, script33DriveCheckpoint;
        std::vector<uint8_t> script33CheckpointWait;
        uint32_t script33EntranceFadeMs = 0;
        std::optional<CityMissionTitle> script33Title;
        CityMissionCinematic script33Intro, script33RaceIntro, script33Closing;
        std::array<uint32_t, 2> script33UnlockedScripts{};
        std::array<std::optional<CityCheckpoint>, 2> script33UnlockedCheckpoints;
        std::vector<uint8_t> script36EntranceWait, script36TravelWait;
        uint32_t script36EntranceFadeMs = 0, script36ArrivalFadeMs = 0;
        std::optional<CityMissionTitle> script36Title;
        CityMissionCinematic script36Intro, script36Arrival, script36Debrief, script36Reinforcements, script36Closing;
        uint32_t script36ClosingFadeMs = 0, script36UnlockedScript = 0;
        std::optional<CityCheckpoint> script37Checkpoint;
        std::string script36PickupObjective;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script36PickupCommands;
        std::optional<CityAnimationClip> pickupSeated;
        std::optional<std::array<CityVehicleCharacterClips, 2>> pickupClips;
        std::optional<CityCheckpoint> script36TravelCheckpoint;
        std::string script36TravelObjective, script36FightObjective, script36SecondFightObjective;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script36FightCommands, script36SecondFightCommands;
        uint32_t script33RaceFadeMs = 0;
        std::optional<CityCheckpoint> script33RaceMarker;
        std::vector<std::array<float, 3>> script33RaceCourse, script33RivalPath;
        std::string script33Rival;
        int32_t script33RivalSpeedKmh = 0;
        std::string script33EntryObjective, script33DriveObjective;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script33DriveCommands;
        std::optional<CityAnimationClip> lincolnSeated;
        std::optional<std::array<CityVehicleCharacterClips, 2>> lincolnClips;
        uint32_t script20ArrivalFadeMs = 0;
        std::string script20DriveObjective, script20FightObjective, script20RetreatObjective;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script20FightCommands, script20SecondFightCommands, script20RetreatCommands;
        std::optional<CityCheckpoint> script20DriveCheckpoint;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script20DriveCommands;
        std::string script19VehicleObjective, script19DriveObjective;
        std::optional<CityCheckpoint> script19DriveCheckpoint;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> script19DriveCommands;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> postBasketballVehicleCommands;
        std::optional<std::array<CityVehicleCharacterClips, 2>> postBasketballVehicleClips;
        std::optional<CityAnimationClip> postBasketballVehicleSeated;
        std::string basketballFightObjective;
        CityDialogue basketballTutorial;
        std::vector<std::string> basketballFightTargets;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> basketballFightCommands;
        std::vector<std::string> cooperFightTargets;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> cooperFightCommands;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> cooperNextDriveCommands;
        std::optional<CityCinematicPlacement> locoGuardPlacement;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> locoGuardCommands;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> postLcFightCommands;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> postLcTailConditions;
        std::optional<std::array<CityVehicleCharacterClips, 2>> vehicleCharacterClips;
        std::optional<CityAnimationClip> vehicleSeated;
    };
    std::optional<MissionCheckpoint> nextCheckpoint;
};

struct CityPayphoneRoute {
    CityCheckpoint destination;
    std::array<float, 3> departure;
    float departureRadius;
    uint32_t departureDelayMs;
    bool clearConditionsOnDeparture = false;
    std::vector<CityCameraMove> cinematicCameras;
    std::vector<CityFade> cinematicFades;
    std::vector<CityDialogue> cinematicDialogue;
    std::vector<CityAudioCue> cinematicAudio;
    uint32_t cinematicDurationMs = 0;
    std::vector<CityCinematicPlacement> cinematicPlacements;
    std::vector<CityActorAnimation> playerAnimations;
    std::optional<CityCinematicWalk> walk;
    uint32_t gameplayFadeMs = 0;
    std::string gameplayObjective;
    CityDialogue gameplayTutorial{};
    std::vector<uint8_t> lockCommandPayload;
    std::vector<uint8_t> attackCommandPayload;
    std::vector<uint8_t> killedCommandPayload;
    std::vector<uint8_t> disableCommandPayload;
    float handRangeCm = 0;
    int32_t handDamage = 0;
    CityPostFightScene postFight;
};

struct CityHighlight {
    CityScene model;
    CityAnimationClip animation;
    int32_t icon;
};

std::optional<float> cityTriangleHeight(const std::array<float, 3>& a, const std::array<float, 3>& b,
                                        const std::array<float, 3>& c, const std::array<float, 3>& position);

struct CityNavigation {
    enum class RouteMode { unrestricted, pedestrian, vehicle };
    enum class Material { road, sidewalk, park, buildingBase, intersection, cliff, water, deathWater };
    struct TriangleRef {
        uint16_t buffer, indexOffset;
        std::optional<std::array<uint16_t, 3>> vertices(const CityMesh& mesh) const;
    };
    struct Buffer {
        std::array<std::vector<uint16_t>, 8> neighbors;
        std::vector<uint8_t> roadFlags;
        std::vector<std::vector<TriangleRef>> overlaps;
    };
    std::vector<Buffer> buffers;
    std::array<uint32_t, 8> materialBuffers{};
    std::vector<uint16_t> cliffVertexKeys, cliffTriangles;

    std::optional<uint8_t> roadFlags(const CitySpawnPoint& point) const;
    std::optional<TriangleRef> neighbor(TriangleRef source, size_t edge) const;
    std::vector<TriangleRef> cliffFaces(uint16_t vertex) const;
    struct GroundHit {
        TriangleRef triangle;
        float height;
    };
    std::optional<GroundHit> sampleGround(const CityMesh& mesh, TriangleRef seed,
                                         const std::array<float, 3>& localPosition,
                                         float rise, float drop) const;
    std::vector<TriangleRef> findRoute(const CityMesh& mesh, TriangleRef start, TriangleRef goal,
                                       RouteMode mode = RouteMode::unrestricted,
                                       std::optional<std::array<float, 3>> localTarget = std::nullopt) const;
    static std::optional<std::array<float, 3>> edgeWaypoint(
        const CityMesh& mesh, TriangleRef source, TriangleRef target, const std::array<float, 3>& origin);
};

struct CityOpening {
    using MissionTitle = CityMissionTitle;
    struct VehicleProtection {
        uint32_t startMs;
        std::string target;
        int32_t failureCause;
        std::string failureTitle;
        std::string failurePrompt;
    };
    size_t district = 0;
    std::vector<CityCameraMove> cameras;
    std::map<std::string, CityCamera> namedCameras;
    std::vector<CityFade> fades;
    std::vector<CityAudioCue> audio;
    std::vector<CityActor> actors;
    std::vector<CityDialogue> dialogue;
    uint32_t durationMs = 0;
    uint32_t handoffMs = 0;
    uint32_t startupScript = 0;
    std::optional<MissionTitle> missionTitle;
    uint32_t nextScript = 0;
    std::optional<uint32_t> followCameraMs;
    uint32_t followCameraBlendMs = 0;
    std::optional<uint32_t> movementMs;
    std::optional<CityCheckpoint> checkpoint;
    std::optional<size_t> vehicleActor;
    std::optional<CityHighlight> vehicleObjective;
    std::optional<VehicleProtection> vehicleProtection;
    std::optional<std::array<CityVehicleCharacterClips, 2>> vehicleCharacterClips;
    std::optional<CityAnimationClip> vehicleSeated;
    std::optional<CityPayphoneRoute> payphone;
    CityScene heightMap;
    CityNavigation navigation;
    CityAnimationClip walk;
    float walkSpeed = 0;  // centimeters per second
};

std::vector<CityHighlight> loadCityHighlights(FileSystemBase& assets);
CityAssets loadCityAssets(FileSystemBase& assets);
struct CityMissionStart {
    uint32_t script, command, mission;
};
enum class CityMissionConditionType : uint32_t {
    protectNpc = 69, protectCar = 70, stayWithin = 71, beQuick = 73, stayBetween = 100, photographQuickly = 117
};
struct CityMissionConditionCommand {
    uint32_t script, command;
    CityMissionConditionType type;
    std::string target;
    int32_t argument;
    int32_t warningText = -1;
    int32_t minimumDistanceCm = 0;
};
struct CityScriptInventory {
    size_t scripts = 0, commands = 0, opcodes = 0, links = 0;
    std::vector<uint32_t> chapterStarts;
    std::vector<CityMissionStart> missionStarts;
    std::vector<CityMissionConditionCommand> conditions;
};
struct CityScriptCommand {
    uint32_t opcode;
    std::vector<uint8_t> payload;
};
using CityScriptProgram = std::vector<std::vector<CityScriptCommand>>;
std::optional<CityMissionConditionCommand> decodeCityMissionCondition(const CityScriptCommand& command);
uint32_t decodeCityMissionObjective(const CityScriptCommand& command);
CityAudioCue decodeCityScriptAudio(const CityScriptCommand& command);
struct CityScriptCamera {
    std::optional<std::string> scene;
    int32_t transitionMs;
};
CityScriptCamera decodeCityScriptCamera(const CityScriptCommand& command);
CityFade decodeCityScriptFade(const CityScriptCommand& command);
struct CityScriptTutorial {
    uint32_t title, text;
};
CityScriptTutorial decodeCityScriptTutorial(const CityScriptCommand& command);
enum class CityScriptActorAction { enable, disable, moveTo };
struct CityScriptActorPlacement {
    CityScriptActorAction action;
    std::string actor;
    std::string marker;
};
CityScriptActorPlacement decodeCityScriptActorPlacement(const CityScriptCommand& command);
struct CityScriptActorMovement {
    enum class Action { walkTo, runTo, runPath, walkPath };
    Action action;
    std::string actor;
    std::string target;
};
CityScriptActorMovement decodeCityScriptActorMovement(const CityScriptCommand& command);
struct CityScriptActorAnimation {
    std::string actor;
    std::string first;
    std::optional<std::string> loop;
};
CityScriptActorAnimation decodeCityScriptActorAnimation(const CityScriptCommand& command);
struct CityScriptDialogue {
    int32_t durationMs;
    int32_t speaker;
    uint32_t textId;
};
CityScriptDialogue decodeCityScriptDialogue(const CityScriptCommand& command);
struct CityScriptCheckpoint {
    std::string object;
    std::optional<std::string> vehicle;
};
CityScriptCheckpoint decodeCityScriptCheckpoint(const CityScriptCommand& command);
struct CityWaitEscape {
    std::string target;
    int32_t distanceCm;
};
CityWaitEscape decodeCityWaitEscape(const CityScriptCommand& command);
std::string decodeCityWaitStolen(const CityScriptCommand& command);
std::string decodeCityStopObject(const CityScriptCommand& command);
std::string decodeCityActorGetOut(const CityScriptCommand& command);
std::string decodeCityKillTarget(const CityScriptCommand& command);
std::string decodeCityFightSideActor(const CityScriptCommand& command);
std::string decodeCityWaitKilled(const CityScriptCommand& command);
std::string decodeCityWaitForLock(const CityScriptCommand& command);
std::string decodeCityDeferredDisable(const CityScriptCommand& command);
std::string decodeCityShopLock(const CityScriptCommand& command);
struct CityActorAttack {
    std::string attacker;
    std::string target;
};
CityActorAttack decodeCityActorAttack(const CityScriptCommand& command);
std::string decodeCityExplodeVehicle(const CityScriptCommand& command);
CityScriptProgram loadCityScriptProgram(FileSystemBase& assets);
CityScriptInventory auditCityScripts(FileSystemBase& assets);
std::optional<std::array<float, 3>> findCityMissionTargetPosition(const CityAssets& city, const std::string& name);
struct CityMissionDefinition {
    // The offsets follow the recovered Structs::Mission layout.
    int32_t field0, cashReward, completionTextId;
    std::string contact;
    int32_t field16, field20;
    std::string sprite;
    int32_t field28, field32;
};
std::vector<CityMissionDefinition> loadCityMissions(FileSystemBase& assets);
std::vector<CityVehicleDefinition> loadCityVehicleMetatypes(FileSystemBase& assets);
CityOpening loadCityOpening(FileSystemBase& assets, const CityAssets& city,
                            RetailLanguage language = RetailLanguage::english);
void validateBresContainer(FileSystemBase& assets, const std::string& path);
CityScene loadCityScene(FileSystemBase& assets, const std::string& path);
std::vector<CitySkin> loadCityActorSkins(const CityScene& model);
std::map<uint8_t, CityActor> loadCitySpawnActors(FileSystemBase& assets, const CityAssets& city);
CityAnimationClip readCityParticleClip(const CityScene& scene);
// Exact source-order/index catalogue. Names retain source case and duplicates;
// legacy script-name lookup remains separate and case-insensitive.
std::vector<CityAnimationClip> loadCityAnimationCatalog(FileSystemBase& assets, const std::string& path);
CityAnimationLibrary readCityAnimationLibrary(const CityScene& scene);
CityAnimationLibrary loadCityAnimationLibrary(FileSystemBase& assets, const std::string& path);
std::vector<CityAnimationClip> loadCityDoorClips(FileSystemBase& assets, const std::string& path);
void bindCityAnimationTracks(std::vector<CityAnimationClip>& clips, const CityScene& model);
const CityScene& cityGeometryLibrary(const CityAssets& city, const CityScene& scene,
                                     const CityGeometryRef& instance);
CityMaterial resolveCityMaterial(const CityAssets& city, const CityScene& scene,
                                  const CityScene& meshLibrary, const CityGeometryRef& instance,
                                  const CityMesh& mesh, size_t buffer);
