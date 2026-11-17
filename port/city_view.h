#pragma once

#include "city_assets.h"
#include "city_particles.h"
#include "retail_mission.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct CityAssets;
struct CityActor;
struct CityVehicleCamera;
struct CityCheckpoint;
struct CityOpening;
struct CityAnimationClip;
using CityCameraMatrix = std::array<std::array<float, 4>, 4>;
using CityTriangle = std::array<std::array<float, 3>, 3>;

class CitySpawnPool {
public:
    explicit CitySpawnPool(const std::map<uint8_t, CitySpawnGroup>& groups);

    std::optional<size_t> acquire(uint8_t metatype);
    bool release(size_t slot);
    int spawnedCops() const { return activeCops; }
    size_t capacity() const { return slots.size(); }
    uint8_t metatype(size_t slot) const { return slots.at(slot).metatype; }

private:
    struct Slot {
        uint8_t metatype;
        bool active = false;
    };
    std::vector<Slot> slots;
    int activeCops = 0;
};

class CitySpawnTimer {
public:
    CitySpawnTimer(const CitySpawnGroup& group, uint32_t initialDraw);

    void advance(uint32_t elapsedMs, const CitySpawnGroup& group, bool eligible,
                 CitySpawnPool& pool, int copLimit, bool alternateCopContext,
                 const std::function<uint32_t()>& random,
                 const std::function<void(size_t, uint8_t)>& spawn);
    uint32_t remainingMs() const { return remaining; }

private:
    uint32_t remaining;
};

struct CityBounds;
struct CityCameraPose;

// The retail visibility pass advances timers only for spawn points in its query box.
class CitySpawnScheduler {
public:
    using Spawn = std::function<bool(size_t, uint8_t, const CitySpawnPoint&)>;

    CitySpawnScheduler(const CityAssets& city, const CityNavigation& navigation,
                       const std::function<uint32_t()>& random);
    void advance(uint32_t elapsedMs, const CityBounds& query, const CityCameraPose& camera,
                 float actorHeight, std::optional<float> vehicleSpeed, CitySpawnContext context,
                 int copLimit, bool alternateCopContext, const std::function<uint32_t()>& random,
                 const Spawn& spawn);
    bool release(size_t slot) { return pool.release(slot); }
    const CitySpawnPool& spawnPool() const { return pool; }

private:
    struct Entry {
        CitySpawnPoint point;
        CitySpawnTimer timer;
        std::optional<uint8_t> roadFlags;
    };
    std::map<uint8_t, CitySpawnGroup> groups;
    CitySpawnPool pool;
    std::vector<Entry> entries;
};

struct CityTerrain {
    struct VehicleFootprint {
        float lateralCenter = 0;
        float radius = 94;
        std::array<float, 3> longitudinalCenters{-150, 0, 150};
    };
    std::vector<CityTriangle> ground;
    std::vector<CityNavigation::TriangleRef> groundNavigation;
    std::vector<CityTriangle> walls;
    std::vector<std::array<CityTriangle, 2>> jumpRamps;
    VehicleFootprint vehicleFootprint;

    struct GroundContact {
        size_t triangle;
        float height;
    };

    struct RampContact {
        size_t ramp;
        float height;
    };

    std::optional<RampContact> rampContact(const std::array<float, 3>& position, float rise, float drop) const;
    void indexGround(const CityNavigation* navigation = nullptr);
    void indexWalls();
    std::optional<size_t> pedestrianNeighbor(size_t triangle, std::optional<size_t> previous,
                                             uint32_t draw) const;
    std::optional<size_t> vehicleNeighbor(size_t triangle, std::optional<size_t> previous,
                                          const std::array<float, 3>& position, float yaw) const;
    std::optional<float> vehicleRoadSpeed(size_t triangle, int32_t frameRate) const;
    std::optional<std::vector<size_t>> wallCandidates(float left, float bottom, float right, float top) const;
    std::optional<GroundContact> groundContact(const std::array<float, 3>& position,
        std::optional<size_t> preferred = std::nullopt, float rise = 42, float drop = 80) const;
    std::optional<float> groundHeight(const std::array<float, 3>& position, float rise = 42,
                                      float drop = 80) const;

private:
    std::map<std::pair<int, int>, std::vector<size_t>> groundCells;
    std::vector<std::vector<size_t>> groundLinks;
    std::vector<std::array<std::optional<size_t>, 3>> sidewalkEdges, roadEdges;
    std::vector<uint8_t> roadFlags;
    std::map<std::pair<int, int>, std::vector<size_t>> wallCells;
};

struct CityVertex {
    std::array<float, 3> position;
    std::array<float, 2> uv;
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 3> normal{};
};

struct CityBounds {
    std::array<float, 3> minimum, maximum;
};

using CityFrustum = std::array<std::array<float, 4>, 6>;
CityFrustum cityFrustum(const CityCameraMatrix& camera);
bool cityBoundsVisible(const CityBounds& bounds, const CityFrustum& frustum);

struct CityLodLimit {
    std::array<float, 3> origin;
    float distanceSquared;
    std::optional<float> minimumSquared;
    bool visible(const std::array<float, 3>& eye) const;
};

struct CitySurface {
    uint32_t firstIndex;
    uint32_t indexCount;
    std::string texture;
    std::array<float, 4> tintAndCutoff;
    uint32_t startMs;
    uint32_t endMs;
    bool player;
    bool vehicleInterior = false;
    bool vehicleExterior = false;
    bool vehicle = false;
    std::optional<CityBounds> bounds;
    std::optional<CityLodLimit> lodLimit;
    bool lodAlternative = false;
    std::optional<size_t> payphoneActor;
};

struct CitySkinRange {
    size_t actor;
    size_t skin;
    uint32_t firstVertex;
};

struct CityWheelRange {
    size_t firstVertex, vertexCount;
    std::array<float, 3> pivot;
    float radius;
    bool steers;
    bool left;
};

struct CityVehicleMorphRange {
    size_t firstVertex, mesh, controller;
    std::array<std::array<float, 3>, 4> bindTransform;
};

struct CityCheckpointMesh {
    size_t node, firstVertex;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
};

struct CityVehicleLightMesh {
    size_t firstVertex, firstSurface, surfaceCount;
    std::vector<CityVertex> source;
};

struct CityBillboard {
    size_t firstVertex;
    std::array<float, 3> origin;
    std::array<float, 2> right;
    std::vector<CityVertex> source;
};

void poseCityBillboard(const CityBillboard& billboard, const CityCameraMatrix& camera, CityVertex* vertices);

struct CityVehicleGeometry {
    uint32_t vehicleFirstVertex = 0;
    uint32_t vehicleVertexCount = 0;
    std::vector<CityWheelRange> wheels;
    std::vector<CityVehicleMorphRange> vehicleMorphs;
};

struct CityDrawData : CityVehicleGeometry {
    std::vector<CityVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<CitySurface> surfaces;
    std::vector<CitySkinRange> skins;
    CityTerrain terrain;
    std::vector<CityBillboard> billboards;
    size_t districtSurfaceCount = 0;
    size_t lowDetailEndSurface = 0;
    std::optional<size_t> checkpointFirstSurface;
    std::optional<size_t> payphoneFirstSurface;
    std::optional<size_t> vehicleObjectiveFirstSurface;
    std::optional<size_t> nextMissionFirstSurface;
    std::optional<size_t> hummerRouteFirstSurface;
    std::optional<size_t> weaponShopDriveFirstSurface;
    std::optional<size_t> weaponShopApproachFirstSurface;
    std::optional<size_t> weaponShopEntranceFirstSurface;
    std::optional<size_t> returnToLcFirstSurface;
    std::optional<size_t> postLcDriveFirstSurface, cooperNextDriveFirstSurface, basketballFirstSurface, postBasketballFirstSurface, repairDriveFirstSurface, repairShopFirstSurface, deliveryFirstSurface, elToroFirstSurface, elToroDestinationFirstSurface, elToroDriveFirstSurface, vanDriveFirstSurface, vanSecondDriveFirstSurface, vanThirdDriveFirstSurface;
    std::array<std::optional<size_t>, 3> postLcWalkFirstSurfaces;
    std::optional<CityVehicleGeometry> nextVehicleGeometry, cooperGeometry, postBasketballVehicleGeometry, vanGeometry;
    std::optional<size_t> cooperActor;
    std::optional<CityTerrain::VehicleFootprint> nextVehicleFootprint, postBasketballVehicleFootprint, cooperFootprint, vanFootprint;
    std::array<std::vector<CityCheckpointMesh>, 25> checkpointMeshes;
    std::vector<CityVehicleLightMesh> vehicleLights;
};

struct CityPlayerPose {
    std::array<float, 3> position;
    float yaw;
    double clipTimeMs = 0;
    bool walking = false;
    std::optional<float> followCameraYaw;
    float followCameraDistance = 450;
    float followCameraHeight = 150;
    std::optional<size_t> groundTriangle;
};

struct CityPedestrianWalk {
    std::optional<size_t> destination, previous;
    std::optional<std::array<float, 3>> sharedEdge;
    void advance(const CityTerrain& terrain, CityPlayerPose& pose, float speed, uint32_t elapsedMs,
                 const std::function<uint32_t()>& random);
};

struct CityTrafficObstacle {
    std::array<float, 3> position;
    float yaw;
    CityTerrain::VehicleFootprint footprint;
    std::optional<size_t> spawnSlot;
};

struct CityAmbientDrive {
    std::optional<size_t> destination, previous;
    std::optional<std::array<float, 3>> sharedEdge;
    void advance(const CityTerrain& terrain, CityPlayerPose& pose, const CityTerrain::VehicleFootprint& footprint,
                 int32_t frameRate, uint32_t elapsedMs, const std::vector<CityTrafficObstacle>& obstacles = {},
                 std::optional<size_t> ownSlot = std::nullopt);
};

struct CityVehiclePose {
    static constexpr unsigned explosionSoundId = 110;
    static constexpr unsigned fireSoundId = 119;
    enum class DamageEffect { none, smoke, fire };
    std::array<float, 3> position;
    float yaw;
    float speed = 0;
    bool occupied = false;
    struct DoorAction {
        size_t side;
        bool exiting;
        double elapsedMs;
    };
    std::optional<DoorAction> doorAction;
    size_t entrySide = 0;
    double seatedMs = 0;
    double wheelTravel = 0;
    float verticalSpeed = 0;
    bool onRamp = false;
    bool airborne = false;
    int32_t jumpDistanceCm = 0;
    float steering = 0;
    int32_t health = 0;
    int32_t maxHealth = 0;
    bool exploded = false;
    std::array<bool, 2> lightEffects{};
    double lightTimeMs = 0;
    bool damageEnabled = false;
    double destroyedMs = 0;
    float collisionDamageScale = 0;
    int32_t peakCollisionDamage = 0;
    float rearDeformation = 0;
    float frontDeformation = 0;
    int gear = 1;
    struct WheelSuspension {
        float height = 0, speed = 0, lastGround = 0;
    };
    std::vector<WheelSuspension> suspension;
    std::array<float, 2> chassisTilt{};
    struct CameraState {
        std::array<float, 3> eye;
        float setback;
        float previousSpeed;
        float eyeHeight;
    };
    std::optional<CameraState> camera;
    std::optional<size_t> groundTriangle;
    void updateCamera(const CityVehicleCamera& offsets, double deltaMs);
    void updateLights(bool braking, double deltaMs = 0);
    void shiftGears(unsigned profile);
    int v6EngineRpm(unsigned profile, float speedRange) const;
    void applyDamage(int32_t amount);
    void applyWallImpact(const std::array<float, 3>& normalVelocity);
    void updateExplosion(double deltaMs);
    DamageEffect damageEffect() const;
};

struct CityDoorSounds {
    static constexpr unsigned openId = 122;
    static constexpr unsigned closeId = 113;
    bool opening = false;
    bool closed = false;
};

struct CityCameraPose {
    std::array<float, 3> eye, target;
};

struct CityCameraTransition {
    CityCameraPose from, to;
    double startMs, durationMs;
    bool active(double elapsedMs) const;
    CityCameraPose sample(double elapsedMs) const;
};

struct CityCameraShake {
    float remainingMs = 0;
    std::array<float, 3> offset{};
    uint64_t randomState = 0x1234abcd330e;

    void update(double deltaMs, bool explosion, bool suspended);
};

enum class CityShopService { health, vest };

struct CityFightOpponent {
    CityPlayerPose pose;
    std::optional<double> punchMs;
    size_t punchIndex = 0;
    int32_t health = 100;
    double defeatMs = 0;
    bool attacking = true;
    bool taggedForKill = false;
    std::optional<std::string> rangedTarget;
    double shotCooldownMs = 0;
    bool disablePending = false;
    bool disabled = false;
};

struct CityPlayback {
    double elapsedMs = 0;
    size_t nextDialogue = 0;
    std::optional<size_t> activeDialogue;
    std::vector<double> dialogueHoldMs;
    std::optional<CityPlayerPose> player;
    std::optional<double> timeAfterCheckpointMs;
    std::optional<CityVehiclePose> vehicle;
    std::optional<CityCameraTransition> cameraTransition;
    CityCameraShake cameraShake;
    std::optional<uint64_t> lastCameraTapMs;
    bool enteredVehicle = false;
    double timeAfterEntryMs = 0;
    bool payphoneActive = false;
    bool payphoneReached = false;
    bool scriptedVehicleExit = false;
    std::optional<CityActorAttack> payphoneAttack;
    std::optional<CityPlayerPose> payphonePursuer;
    std::optional<double> payphonePunchMs;
    size_t payphonePunchIndex = 0;
    static constexpr int32_t playerMaxHealth = 300;
    static constexpr int32_t playerMaxArmor = 200;
    int32_t playerHealth = playerMaxHealth;
    int32_t playerArmor = 0;
    int32_t playerCash = 0;
    std::array<bool, 8> openingMissionsPaid{};
    int32_t payphoneOpponentHealth = 100;
    double payphoneDefeatMs = 0;
    std::map<std::string, CityFightOpponent, std::less<>> locoOpponents;
    std::optional<std::string> locoLockTarget;
    bool payphoneDisablePending = false;
    bool payphoneOpponentDisabled = false;
    bool nextMissionVehicleActive = false;
    bool postLcVehicleActive = false;
    bool nextMissionVehicleAcquired = false;
    std::optional<double> playerPunchMs;
    size_t playerPunchIndex = 0;
    RetailMissionConditions missionConditions;
    bool protectionRegistered = false;
    std::optional<int32_t> missionFailure;
    uint32_t missionFailureCount = 0;
    int32_t longestJumpMeters = 0;
    uint32_t visitedJumpRamps = 0;
    unsigned highestVehicleSpeedKmh = 0;
    int32_t drivenCentimeters = 0;
    uint32_t drivenKilometers = 0;
    double failureElapsedMs = 0;

    bool acknowledgeFailure(const CityOpening& opening, double deltaMs, bool pressed, bool animationFinished);
    void advance(const CityOpening& opening, double deltaMs, bool pressed);
    void advance(const std::vector<CityDialogue>& dialogue, uint32_t durationMs, double deltaMs, bool pressed);
    void walk(const CityOpening& opening, const CityTerrain& terrain,
              std::array<float, 2> input, double deltaMs);
    bool canEnterShop(const CityCheckpoint& entrance) const;
    bool creditOpeningMission(size_t mission, int32_t reward);
    bool buyShopService(CityShopService service, uint32_t price);
    void damagePlayer(int32_t amount, bool bypassArmor = false);
    CityDoorSounds drive(const CityOpening& opening, const CityTerrain& terrain,
                         std::array<float, 2> input, double deltaMs, bool interact);
    bool spawnVehicle(const CityActor& actor, const CityTerrain& terrain,
                      std::array<float, 3> position, float yaw);
    void orbitCamera(const CityOpening& opening, std::array<float, 2> drag, double deltaMs);
    void releaseCameraTap(const CityOpening& opening, uint64_t releasedMs);
    void recenterVehicleCamera(const CityOpening& opening);
    void advancePayphone(const CityOpening& opening, double deltaMs);
    void spawnFightOpponent(const CityOpening& opening, const CityTerrain& terrain,
                            const CityCinematicPlacement& placement, bool attacking);
    void punchLocoOpponent(const CityOpening& opening, const CityTerrain& terrain,
                          double deltaMs, bool pressed, bool lockPressed);
    void updateLocoFight(const CityOpening& opening, const CityTerrain& terrain, double deltaMs);
    void pursuePayphoneTarget(const CityOpening& opening, const CityTerrain& terrain, double deltaMs);
    void punchPayphoneOpponent(const CityOpening& opening, const CityTerrain& terrain,
                               double deltaMs, bool pressed, bool locked);
    void updateMission(const CityOpening& opening, double deltaMs);
    std::optional<float> payphoneBearing(const CityOpening& opening) const;
    std::optional<std::array<float, 3>> vehicleObjectivePosition(const CityOpening& opening) const;
};

class CityPayphonePlayback {
public:
    bool advance(const CityPayphoneRoute& route, double deltaMs, bool pressed,
                 const std::function<void(const CityAudioCue&)>& audio);
    bool advance(const CityMissionCinematic& scene, double deltaMs, bool pressed,
                 const std::function<void(const CityAudioCue&)>& audio);
    double elapsedMs() const { return timeline.elapsedMs; }
    std::optional<size_t> activeDialogue() const { return timeline.activeDialogue; }

private:
    bool advance(const std::vector<CityDialogue>& dialogue, const std::vector<CityAudioCue>& cues,
                 uint32_t durationMs, double deltaMs, bool pressed,
                 const std::function<void(const CityAudioCue&)>& audio);
    CityPlayback timeline;
    size_t nextAudio = 0;
};

class CityStartupPlayback {
public:
    explicit CityStartupPlayback(CityScriptProgram program, uint32_t startupScript);
    bool advance(const CityOpening& opening, double deltaMs, const std::function<void(const CityAudioCue&)>& audio,
                 bool skipRequested = false);
    const std::optional<CityCameraMove>& camera() const { return selectedCamera; }
    double elapsedMs() const { return timeMs; }
    float brightness() const { return 1.f - opacity / 255.f; }
    bool showsBars() const { return barsVisible; }
    bool canSkip() const { return phase == RetailCinematicPhase::startup; }
    uint32_t beginMission(uint32_t script);

private:
    RetailScriptExecution scripts;
    uint32_t startupScript;
    RetailCinematicPhase phase = RetailCinematicPhase::gameplay;
    std::optional<CityCameraMove> selectedCamera;
    double timeMs = 0;
    uint8_t opacity = 255;
    bool barsVisible = false;
};

CityDrawData prepareCityView(const CityAssets& city, const CityOpening& opening);
struct CitySpawnMesh {
    size_t firstVertex, firstSurface, surfaceCount;
    std::shared_ptr<const CityActor> actor;
    std::vector<CityVertex> source;
    std::vector<CitySkinRange> skins;
    CityTerrain::VehicleFootprint footprint;
};
std::vector<CitySpawnMesh> appendCitySpawnMeshes(CityDrawData& draw, const CityAssets& city,
                                                const CitySpawnPool& pool, FileSystemBase& assets);
void poseCitySpawnMesh(const CitySpawnMesh& mesh, const std::array<float, 3>& position,
                       const std::array<float, 3>& rotation, double elapsedMs, CityVertex* target, bool walking = false);
bool citySurfaceVisible(const CitySurface& surface, const CityOpening& opening, const CityPlayback& playback,
                        std::optional<double> payphoneMs = {}, std::optional<double> missionMs = {},
                        const CityMissionCinematic* missionScene = nullptr);
bool citySurfaceCullBackfaces(const CitySurface& surface, const CityPlayback& playback);
void poseCityVehicleObjective(const CityOpening& opening, const CityVehiclePose* vehicle,
                              const std::vector<CityCheckpointMesh>& meshes, double elapsedMs,
                              CityVertex* vertices);
void poseCityCheckpoint(const CityCheckpoint& checkpoint, const std::vector<CityCheckpointMesh>& meshes,
                        double elapsedMs, CityVertex* vertices);
void poseCityActors(const CityOpening& opening, const std::vector<CitySkinRange>& skins,
                    double elapsedMs, CityVertex* vertices);
void poseCityActors(const CityOpening& opening, const std::vector<CitySkinRange>& skins,
                    const CityPlayback& playback, CityVertex* vertices, std::optional<double> payphoneMs = {},
                    std::optional<double> missionMs = {}, const CityMissionCinematic* missionScene = nullptr,
                    const CityTerrain* terrain = nullptr);
const CityCinematicPlacement* cityPayphonePlacement(const CityPayphoneRoute& route,
                                                  std::string_view actor, double elapsedMs);
const CityMissionCinematic::Motion* cityCinematicMotion(const CityMissionCinematic& scene,
                                                       std::string_view actor, double elapsedMs);
std::optional<std::array<float, 3>> cityTailingActorPosition(const CityMissionCinematic& scene,
    std::string_view actor, const CityTerrain& terrain, double elapsedMs);
void pursueCityVehicle(CityVehiclePose& vehicle, const CityTerrain& terrain,
                       const std::array<float, 3>& target, float cruiseSpeedCmPerMs, double elapsedMs);
std::optional<CityVehiclePose> cityStagedVehiclePose(const CityMissionCinematic& scene,
    std::string_view actor, const CityTerrain& terrain, double elapsedMs);
std::optional<CityVehiclePose> cityCinematicVehiclePose(const CityMissionCinematic& scene,
    std::string_view actor, const CityVehiclePose& initial, const CityTerrain& terrain, double elapsedMs);
std::array<float, 3> cityCinematicTravelPosition(const CityCinematicWalk& travel, double elapsedMs);
CityCameraMatrix cityCameraMatrix(const CityOpening& opening, float aspect, double elapsedMs,
                                  const CityPlayerPose* player = nullptr, const CityVehiclePose* vehicle = nullptr,
                                  const std::optional<CityCameraTransition>& transition = {},
                                  const std::array<float, 3>& shake = {},
                                  const CityStartupPlayback* startup = nullptr,
                                  std::array<float, 3>* cameraEye = nullptr,
                                  std::optional<CityCameraMatrix>* distantCamera = nullptr,
                                  std::optional<double> payphoneMs = {}, std::optional<double> missionMs = {},
                                  const CityMissionCinematic* missionScene = nullptr,
                                  CityCameraPose* spawnCamera = nullptr, CityBounds* spawnQuery = nullptr);
void poseCityVehicle(const CityActor& actor, const CityVehiclePose& vehicle,
                     const std::vector<CityVertex>& source, const std::vector<CityWheelRange>& wheels,
                     const std::vector<CityVehicleMorphRange>& morphs, CityVertex* target,
                     const std::vector<float>& wheelOffsets = {});
std::vector<float> cityVehicleWheelOffsets(const CityActor& actor, const CityVehiclePose& vehicle,
                                          const std::vector<CityWheelRange>& wheels, const CityTerrain& terrain);
void updateCityVehicleSuspension(const CityActor& actor, CityVehiclePose& vehicle,
                                  const std::vector<CityWheelRange>& wheels, const CityTerrain& terrain,
                                  double deltaMs);
float cityOpeningBrightness(const CityOpening& opening, double elapsedMs);
float cityTrackBrightness(const std::vector<CityFade>& fades, double elapsedMs);
float retailMissionRevealBrightness(double elapsedMs);
std::optional<CityParticleFrame> cityVehicleSmokeFrame(const CityActor& actor, const CityVehiclePose& vehicle);
std::optional<std::array<CityParticleFrame, 2>> cityVehicleLightFrames(const CityActor& actor,
    const CityVehiclePose& vehicle, const std::vector<CityWheelRange>& wheels = {},
    const std::vector<float>& wheelOffsets = {});
void poseCityVehicleLight(const CityVehicleLightMesh& mesh, const CityParticleFrame& frame,
                          float opacity, CityVertex* vertices);
CityParticleFrame cityParticleFrame(const CityScene& scene, size_t node, const CityAnimationClip& clip,
                                    double elapsedMs, const CityParticleFrame& parent);
void poseCityParticles(const std::vector<CityParticle>& particles, const CityCameraMatrix& camera,
                       CityVertex* vertices, const CityScene* scene = nullptr, float emitterTimeMs = 0);
