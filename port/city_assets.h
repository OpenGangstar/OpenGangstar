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

struct CityMaterial {
    uint32_t diffuse = 0xffffffff;
    std::string texture;
    bool alphaTest = false;
    std::vector<std::string> additionalTextures;
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

    std::optional<std::string_view> property(std::string_view key) const;
};

struct CitySkinJoint {
    size_t node;
    std::array<float, 16> inverseBind;
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

struct CitySpawnGroup {
    int32_t objectType;
    std::vector<uint8_t> metatypes;

    uint8_t choose(uint32_t random) const;
    std::optional<uint8_t> chooseForSpawn(uint32_t random, int spawnedCops, int copLimit,
                                          bool alternateCopContext) const;
    uint32_t spawnDelayMs(bool eligible, uint32_t random) const;
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

struct CityAnimationClip {
    std::string name;
    uint32_t durationMs;
    std::vector<CityAnimationChannel> channels;
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
    std::optional<uint32_t> followCameraMs;
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
    std::optional<uint32_t> missionOverMs;
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
        std::string basketballFightObjective;
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
std::string decodeCityActorGetOut(const CityScriptCommand& command);
std::string decodeCityKillTarget(const CityScriptCommand& command);
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
CityAnimationClip readCityParticleClip(const CityScene& scene);
std::vector<CityAnimationClip> loadCityDoorClips(FileSystemBase& assets, const std::string& path);
void bindCityAnimationTracks(std::vector<CityAnimationClip>& clips, const CityScene& model);
const CityScene& cityGeometryLibrary(const CityAssets& city, const CityScene& scene,
                                     const CityGeometryRef& instance);
CityMaterial resolveCityMaterial(const CityAssets& city, const CityScene& scene,
                                  const CityScene& meshLibrary, const CityGeometryRef& instance,
                                  const CityMesh& mesh, size_t buffer);
