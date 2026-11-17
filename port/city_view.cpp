#include "city_view.h"
#include "city_assets.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
using Vec3 = std::array<float, 3>;
using Transform = std::array<Vec3, 4>;
constexpr Transform identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {}}};
constexpr float radians = 0.01745329252f;
constexpr CityVehicleCamera defaultVehicleCamera{720, 390, 130, 0, 720};

struct VehicleGearProfile {
    int gearCount;
    std::array<unsigned, 4> shiftSpeeds;
    std::array<float, 5> power;
};

// gameloft::vehicle::gear_profiles, with shift speeds in km/h.
constexpr std::array<VehicleGearProfile, 4> gearProfiles{{
    {5, {50, 90, 130, 160}, {1.7f, 1.5f, 0.9f, 0.5f, 0.25f}},
    {5, {45, 75, 100, 130}, {1.4f, 1.2f, 0.9f, 0.6f, 0.4f}},
    {4, {40, 65, 90}, {0.75f, 0.75f, 1.25f, 0.75f, 0.75f}},
    {5, {40, 75, 110, 130}, {1.3f, 1.3f, 1.f, 0.8f, 0.4f}}
}};

float forwardAcceleration(const CityVehicleDrive& drive, unsigned profile, int gear, float speed)
{
    const auto& settings = gearProfiles.at(profile);
    const auto index = std::clamp(gear, 1, settings.gearCount) - 1;
    float power = settings.power.at(index);
    if (index + 1 < settings.gearCount) {
        const float lower = index == 0 ? 0.f : settings.shiftSpeeds.at(index - 1);
        const float upper = settings.shiftSpeeds.at(index);
        const float fraction = std::clamp((speed * 0.036f - lower) / (upper - lower), 0.f, 1.f);
        power += (settings.power.at(index + 1) - power) * fraction;
    }
    // Approximate the retail rear wheel force at the chassis in cm/s^2.
    return 200.f * drive.forwardForce * power / drive.massKg;
}

unsigned vehicleSpeedKmh(float cmPerSecond)
{
    return static_cast<unsigned>(std::abs(cmPerSecond) / 100000 * 3600);
}

Vec3 add(const Vec3& a, const Vec3& b)
{
    return {a[0]+b[0], a[1]+b[1], a[2]+b[2]};
}

Vec3 scale(const Vec3& v, float s)
{
    return {v[0]*s, v[1]*s, v[2]*s};
}

float dot(const Vec3& a, const Vec3& b)
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

void morphVehicleVertex(const CityScene& model, const CityMorphController& morph,
                        const CityVehiclePose& vehicle, size_t vertex, Vec3& position, Vec3& normal)
{
    // CColladaMorphingMesh starts method 0 at one minus the sum of the target weights.
    const auto basePosition = position, baseNormal = normal;
    const std::array damage{vehicle.rearDeformation, vehicle.frontDeformation};
    for (size_t i = 0; i < morph.targets.size(); ++i) {
        const auto& target = morph.targets[i];
        const float weight = i < damage.size() ? damage[i] : target.weight;
        if (weight == 0) continue;
        const auto& mesh = model.meshes.at(target.mesh);
        const auto reference = morph.method == 0 ? basePosition : Vec3{};
        position = add(position, scale(add(mesh.positions.at(vertex), scale(reference, -1)), weight));
        if (!mesh.normals.empty()) {
            const auto referenceNormal = morph.method == 0 ? baseNormal : Vec3{};
            normal = add(normal, scale(add(mesh.normals.at(vertex), scale(referenceNormal, -1)), weight));
        }
    }
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}

Vec3 direction(const Transform& transform, const Vec3& vector)
{
    return add(add(scale(transform[0], vector[0]), scale(transform[1], vector[1])),
               scale(transform[2], vector[2]));
}

Vec3 point(const Transform& transform, const Vec3& vector)
{
    return add(direction(transform, vector), transform[3]);
}

Transform normalTransform(const Transform& transform)
{
    const auto x = cross(transform[1], transform[2]);
    const auto determinant = dot(transform[0], x);
    if (std::abs(determinant) < 1e-12f) return {};
    const auto inverse = 1.f / determinant;
    return {scale(x, inverse), scale(cross(transform[2], transform[0]), inverse),
            scale(cross(transform[0], transform[1]), inverse), {}};
}

std::optional<Transform> inverseTransform(const Transform& transform)
{
    if (std::abs(dot(transform[0], cross(transform[1], transform[2]))) < 1e-12f)
        return std::nullopt;
    const auto normals = normalTransform(transform);
    Transform inverse{};
    for (size_t column = 0; column < 3; ++column)
        for (size_t row = 0; row < 3; ++row)
            inverse[column][row] = normals[row][column];
    inverse[3] = scale(direction(inverse, transform[3]), -1);
    return inverse;
}

Transform compose(const Transform& parent, const Transform& child)
{
    return {direction(parent, child[0]), direction(parent, child[1]),
            direction(parent, child[2]), point(parent, child[3])};
}

Transform nodeTransform(const CityNode& node, bool skeleton = false)
{
    const Vec3 axis{node.rotation[0], node.rotation[1], node.rotation[2]};
    // Retail skeleton quaternions rotate in the opposite direction to our column vectors.
    const float weight = skeleton ? -node.rotation[3] : node.rotation[3];
    const float magnitude = dot(axis, axis) + weight * weight;
    if (magnitude < 0.001f) throw std::runtime_error("Invalid city node rotation.");
    Transform result{};
    for (size_t i = 0; i < 3; ++i) {
        Vec3 basis{};
        basis[i] = node.scale[i];
        const auto crossAxis = cross(axis, basis);
        result[i] = add(basis, scale(add(scale(crossAxis, weight), cross(axis, crossAxis)), 2 / magnitude));
    }
    result[3] = node.translation;
    return result;
}

Transform placementTransform(const Vec3& position, const Vec3& angles, const Vec3& factors, float yaw)
{
    const float sx = std::sin(angles[0] * radians), cx = std::cos(angles[0] * radians);
    const float sy = std::sin(angles[1] * radians), cy = std::cos(angles[1] * radians);
    const float sz = std::sin(yaw * radians), cz = std::cos(yaw * radians);
    return {scale({cy*cz, cy*sz, -sy}, factors[0]),
            scale({sx*sy*cz-cx*sz, sx*sy*sz+cx*cz, sx*cy}, factors[1]),
            scale({cx*sy*cz+sx*sz, cx*sy*sz-sx*cz, cx*cy}, factors[2]), position};
}

Transform vehicleTransform(const CityActor& actor, const CityVehiclePose& vehicle)
{
    auto angles = actor.rotationDegrees;
    angles[0] += vehicle.chassisTilt[0];
    angles[1] += vehicle.chassisTilt[1];
    return placementTransform(vehicle.position, angles, actor.scale, vehicle.yaw);
}

Transform actorTransform(const CityActor& actor, const CityPlayerPose* player = nullptr)
{
    return placementTransform(player ? player->position : actor.position, actor.rotationDegrees, actor.scale,
                              player ? player->yaw : actor.rotationDegrees[2]);
}

const CityActor& openingPlayer(const CityOpening& opening)
{
    const auto player = std::find_if(opening.actors.begin(), opening.actors.end(),
                                     [](const CityActor& actor) { return actor.name == "Player"; });
    if (player == opening.actors.end()) throw std::runtime_error("Missing player for the opening.");
    return *player;
}

CityCameraPose followCamera(const CityOpening& opening, const CityPlayerPose* player, const CityVehiclePose* vehicle)
{
    Vec3 eye, target;
    const auto& actor = openingPlayer(opening);
    const auto cameraYaw = player ? player->followCameraYaw.value_or(actor.rotationDegrees[2]) :
        actor.rotationDegrees[2];
    const auto angle = cameraYaw * radians;
    // FollowCamera::init looks at the character position from 450 cm behind and 150 cm above.
    target = player ? player->position : actor.position;
    const auto distance = player ? player->followCameraDistance : 450.f;
    const auto height = player ? player->followCameraHeight : 150.f;
    eye = add(target, {-distance * std::sin(angle), distance * std::cos(angle), height});
    if (vehicle && vehicle->occupied && opening.vehicleActor) {
        const auto yaw = vehicle->yaw * radians;
        const Vec3 forward{std::sin(yaw), -std::cos(yaw), 0};
        const auto& actor = opening.actors.at(*opening.vehicleActor);
        const auto offsets = actor.vehicleCamera.value_or(defaultVehicleCamera);
        target = add(add(vehicle->position, scale(forward, offsets.targetForward)), {0, 0, offsets.targetHeight});
        eye = vehicle->camera ? vehicle->camera->eye :
            add(add(vehicle->position, scale(forward, -offsets.setback)), {0, 0, offsets.eyeHeight});
    }
    return {eye, target};
}
}

bool CityCameraTransition::active(double elapsedMs) const
{
    return elapsedMs >= startMs && elapsedMs < startMs + durationMs;
}

CityCameraPose CityCameraTransition::sample(double elapsedMs) const
{
    const auto blend = durationMs > 0 ? static_cast<float>(
        std::clamp((elapsedMs - startMs) / durationMs, 0., 1.)) : 1.f;
    const auto interpolate = [blend](const Vec3& first, const Vec3& second) {
        return add(first, scale(add(second, scale(first, -1)), blend));
    };
    const CityCameraPose pose{interpolate(from.eye, to.eye), interpolate(from.target, to.target)};
    return pose;
}

CityCameraMatrix cityCameraMatrix(const CityOpening& opening, float aspect, double elapsedMs,
                                  const CityPlayerPose* player, const CityVehiclePose* vehicle,
                                  const std::optional<CityCameraTransition>& transition,
                                  const std::array<float, 3>& shake, const CityStartupPlayback* startup,
                                  std::array<float, 3>* cameraEye,
                                  std::optional<CityCameraMatrix>* distantCamera,
                                  std::optional<double> payphoneMs, std::optional<double> missionMs,
                                  const CityMissionCinematic* missionScene,
                                  CityCameraPose* spawnCamera, CityBounds* spawnQuery)
{
    const auto sceneMs = missionMs ? missionMs : payphoneMs;
    const auto* cinematic = missionScene ? &missionScene->cameras :
        opening.payphone && payphoneMs ? &opening.payphone->cinematicCameras : nullptr;
    const bool restoredFollow = missionMs && missionScene && missionScene->followCameraMs &&
        *missionMs >= *missionScene->followCameraMs;
    const bool blendingFollow = restoredFollow && missionScene->followCameraTransitionMs &&
        *missionMs - *missionScene->followCameraMs < missionScene->followCameraTransitionMs;
    const bool cinematicShot = sceneMs && cinematic && (!restoredFollow || blendingFollow) && !cinematic->empty() &&
        *sceneMs >= cinematic->front().startMs;
    const bool follow = !cinematicShot && opening.followCameraMs && elapsedMs >= *opening.followCameraMs;
    const auto scriptedMs = cinematicShot ? (blendingFollow ? double(*missionScene->followCameraMs) : *sceneMs) :
        startup ? startup->elapsedMs() :
        follow ? double(*opening.followCameraMs) : elapsedMs;
    const auto& shots = cinematicShot ? *cinematic : opening.cameras;
    const auto next = std::upper_bound(shots.begin(), shots.end(), scriptedMs,
        [](double time, const CityCameraMove& move) { return time < move.startMs; });
    const auto& camera = startup && startup->camera() ? *startup->camera() :
        next == shots.begin() ? shots.front() : *std::prev(next);
    const auto amount = camera.durationMs ? static_cast<float>(
        std::clamp((scriptedMs - camera.startMs) / camera.durationMs, 0., 1.)) : 1.f;
    const auto blend = [amount](float first, float second) { return first + (second - first) * amount; };
    Vec3 eye{}, target{};
    for (size_t i = 0; i < 3; ++i) {
        eye[i] = blend(camera.first.eye[i], camera.second.eye[i]);
        target[i] = blend(camera.first.target[i], camera.second.target[i]);
    }
    if (cinematicShot && blendingFollow) {
        const CityCameraTransition handoff{{eye, target}, followCamera(opening, player, vehicle),
            double(*missionScene->followCameraMs), double(missionScene->followCameraTransitionMs)};
        const auto pose = handoff.sample(*missionMs);
        eye = pose.eye;
        target = pose.target;
    } else if (follow) {
        const double handoffEnd = scriptedMs + opening.followCameraBlendMs;
        if (transition && transition->active(elapsedMs)) {
            const auto pose = transition->sample(elapsedMs);
            eye = pose.eye;
            target = pose.target;
        } else if (elapsedMs < handoffEnd) {
            const CityCameraTransition handoff{
                {eye, target}, followCamera(opening, nullptr, nullptr),
                scriptedMs, double(opening.followCameraBlendMs)};
            const auto pose = handoff.sample(elapsedMs);
            eye = pose.eye;
            target = pose.target;
        } else {
            const auto pose = followCamera(opening, player, vehicle);
            eye = add(pose.eye, shake);
            target = add(pose.target, shake);
        }
    }
    const auto unit = [](const Vec3& v) {
        const auto length = std::sqrt(dot(v, v));
        if (length <= 0.001f) throw std::runtime_error("Degenerate retail camera orientation.");
        return scale(v, 1 / length);
    };
    if (cameraEye) *cameraEye = eye;
    const auto forward = unit(add(target, scale(eye, -1)));
    auto horizontal = cross(forward, {0, 0, 1});
    if (dot(horizontal, horizontal) <= 0.000001f)
        horizontal = cross(forward, {0, 1, 0});
    const auto right = unit(horizontal);
    const auto down = cross(forward, right);
    constexpr float halfDegreesToRadians = 0.00872664626f;
    const auto focal = 1 / std::tan(blend(camera.first.fovDegrees, camera.second.fovDegrees) * halfDegreesToRadians);
    const auto nearPlane = blend(camera.first.nearPlane, camera.second.nearPlane);
    const auto farPlane = blend(camera.first.farPlane, camera.second.farPlane);
    if (spawnCamera) *spawnCamera = {eye, target};
    if (spawnQuery) {
        *spawnQuery = {eye, eye};
        // Irrlicht's view-frustum box encloses the eye and the four far-plane corners.
        const auto farCenter = add(eye, scale(forward, farPlane));
        for (float x : {-1.f, 1.f}) {
            for (float y : {-1.f, 1.f}) {
                const auto corner = add(add(farCenter, scale(right, x * farPlane * aspect / focal)),
                                        scale(down, y * farPlane / focal));
                for (size_t axis = 0; axis < corner.size(); ++axis) {
                    spawnQuery->minimum[axis] = std::min(spawnQuery->minimum[axis], corner[axis]);
                    spawnQuery->maximum[axis] = std::max(spawnQuery->maximum[axis], corner[axis]);
                }
            }
        }
    }
    const auto depth = farPlane / (farPlane - nearPlane);
    const auto row = [&](const Vec3& axis, float bias = 0) {
        return std::array<float, 4>{axis[0], axis[1], axis[2], bias - dot(eye, axis)};
    };
    CityCameraMatrix cameraMatrix{row(scale(right, focal / aspect)), row(scale(down, focal)),
                                  row(scale(forward, depth), -nearPlane * depth), row(forward)};
    if (distantCamera) {
        distantCamera->reset();
        // CLowResCitySceneManager draws from the main far plane to 20,000 cm.
        constexpr float distantFarPlane = 20000.f;
        if (farPlane < distantFarPlane) {
            const auto distantDepth = distantFarPlane / (distantFarPlane - farPlane);
            *distantCamera = cameraMatrix;
            (*distantCamera)->at(2) = row(scale(forward, distantDepth), -farPlane * distantDepth);
        }
    }
    return cameraMatrix;
}

CityStartupPlayback::CityStartupPlayback(CityScriptProgram program, uint32_t startupScript)
    : scripts(std::move(program)), startupScript(startupScript)
{
    scripts.reset(0, false, startupScript);
}

uint32_t CityStartupPlayback::beginMission(uint32_t script)
{
    std::optional<uint32_t> selected;
    RetailMissionHooks host{};
    host.setFadeOpacity = [&](uint8_t alpha) { opacity = alpha; };
    host.missionState = [] { return 0; };
    host.startMission = [&](uint32_t id, int32_t) { selected = id; };
    scripts.execute(script, 0, 0, [](const CityScriptCommand&, int32_t) { return false; },
                    [] {}, [] {}, false, nullptr, {}, &host);
    if (!selected) throw std::runtime_error("Retail opening did not request a mission title.");
    return *selected;
}

bool CityStartupPlayback::advance(const CityOpening& opening, double deltaMs,
                                  const std::function<void(const CityAudioCue&)>& audio, bool skipRequested)
{
    const auto before = static_cast<int64_t>(timeMs);
    timeMs += std::clamp(deltaMs, 0., 1000.);
    const auto elapsed = static_cast<int32_t>(static_cast<int64_t>(timeMs) - before);
    using Phase = RetailCinematicPhase;
    // The retail manager advances these two phases on the next update, before running scripts.
    if (phase == Phase::enteringStartup) phase = Phase::startup;
    if (phase == Phase::leavingStartup) phase = Phase::gameplay;
    const bool skip = skipRequested && phase == Phase::startup;
    RetailMissionHooks host{};
    host.cinematicPhase = [&] { return phase; };
    host.setCinematicPhase = [&](Phase requested, bool skipping) {
        phase = skipping && requested == Phase::leavingStartup ? Phase::gameplay : requested;
    };
    host.selectCamera = [&](const CityScriptCamera& command) {
        if (!command.scene) throw std::runtime_error("Unexpected default camera during retail startup.");
        const auto& target = opening.namedCameras.at(*command.scene);
        const auto from = selectedCamera ? selectedCamera->second : target;
        selectedCamera = CityCameraMove{from, target, static_cast<uint32_t>(timeMs),
                                        static_cast<uint32_t>(std::max(0, command.transitionMs))};
    };
    host.setFadeOpacity = [&](uint8_t alpha) { opacity = alpha; };
    host.selectCinematicAnimation = [&](int32_t animation) {
        // BARS_DOWN and BARS_UP both reference a single BARS frame in the retail atlas.
        if (animation > 1) throw std::runtime_error("Unsupported startup cinematic animation " + std::to_string(animation));
        barsVisible = animation >= 0;
    };
    scripts.execute(startupScript, elapsed, 0, [](const CityScriptCommand& command, int32_t) -> bool {
        throw std::runtime_error("Unsupported retail startup opcode " + std::to_string(command.opcode));
    }, [] {}, [] {}, skip, nullptr, audio, &host);
    if (scripts.state(startupScript) != RetailScriptState::locked) return false;
    if (scripts.state(opening.nextScript) != RetailScriptState::active)
        throw std::runtime_error("Retail startup did not activate the opening mission script.");
    return true;
}

static CityParticleFrame vehicleAttachmentFrame(const CityActor& actor, const CityVehiclePose& vehicle,
                                                const CityNode& node)
{
    auto anchor = nodeTransform(node);
    for (auto parent = node.parent; parent >= 0; parent = actor.model.nodes.at(parent).parent)
        anchor = compose(nodeTransform(actor.model.nodes.at(parent)), anchor);
    const auto frame = compose(vehicleTransform(actor, vehicle), anchor);
    return {frame[3], {frame[0], frame[1], frame[2]}};
}

std::optional<std::array<CityParticleFrame, 2>> cityVehicleLightFrames(const CityActor& actor,
    const CityVehiclePose& vehicle, const std::vector<CityWheelRange>& wheels,
    const std::vector<float>& wheelOffsets)
{
    if (!actor.lights) return {};
    const auto& mounts = actor.lights->mountNodes;
    std::array frames{vehicleAttachmentFrame(actor, vehicle, actor.model.nodes.at(mounts[0])),
                      vehicleAttachmentFrame(actor, vehicle, actor.model.nodes.at(mounts[1]))};
    // Vehicle::UpdatePhysic adds the right and left rear wheel heights to mounts 01 and 02.
    const auto up = vehicleTransform(actor, vehicle)[2];
    const float length = std::sqrt(dot(up, up));
    for (size_t i = 0; i < wheelOffsets.size(); ++i) {
        const auto& wheel = wheels.at(i);
        if (!wheel.steers && length > 0) {
            auto& frame = frames[wheel.left ? 1 : 0];
            frame.origin = add(frame.origin, scale(up, wheelOffsets[i] / length));
        }
    }
    return frames;
}

void poseCityVehicleLight(const CityVehicleLightMesh& mesh, const CityParticleFrame& frame,
                          float opacity, CityVertex* vertices)
{
    const Transform transform{frame.axes[0], frame.axes[1], frame.axes[2], frame.origin};
    const auto normals = normalTransform(transform);
    for (size_t i = 0; i < mesh.source.size(); ++i) {
        const auto& source = mesh.source[i];
        auto& vertex = vertices[mesh.firstVertex + i];
        vertex.position = point(transform, source.position);
        vertex.normal = direction(normals, source.normal);
        vertex.color[3] = source.color[3] * opacity;
    }
}

std::optional<CityParticleFrame> cityVehicleSmokeFrame(const CityActor& actor, const CityVehiclePose& vehicle)
{
    const auto& nodes = actor.model.nodes;
    const auto marker = std::find_if(nodes.begin(), nodes.end(), [](const CityNode& node) {
        return node.name == "dummy_p_smoke_00-node";
    });
    if (marker == nodes.end()) return {};
    return vehicleAttachmentFrame(actor, vehicle, *marker);
}

void poseCityParticles(const std::vector<CityParticle>& particles, const CityCameraMatrix& camera,
                       CityVertex* vertices, const CityScene* scene, float emitterTimeMs)
{
    const auto axis = [&](size_t row) {
        const Vec3 vector{camera[row][0], camera[row][1], camera[row][2]};
        return scale(vector, 1 / std::sqrt(dot(vector, vector)));
    };
    const auto right = axis(0), down = axis(1);
    for (const auto& particle : particles) {
        const auto x = scale(right, particle.size * 0.5f);
        const auto y = scale(down, particle.size * 0.5f);
        const auto upper = add(particle.position, scale(y, -1));
        const auto lower = add(particle.position, y);
        const auto rgba = scene ? cityParticleColor(*scene, particle, emitterTimeMs) : std::array<float, 4>{1, 1, 1, 1};
        const CityVertex a{add(upper, scale(x, -1)), {0, 0}, rgba}, b{add(upper, x), {1, 0}, rgba};
        const CityVertex c{add(lower, scale(x, -1)), {0, 1}, rgba}, d{add(lower, x), {1, 1}, rgba};
        for (const auto& vertex : {a, b, c, c, b, d}) *vertices++ = vertex;
    }
}

float retailMissionRevealBrightness(double elapsedMs)
{
    constexpr int durationMs = 1000;
    const auto remaining = durationMs - static_cast<int>(std::clamp(elapsedMs, 0., double(durationMs)));
    return (255 - remaining * 255 / durationMs) / 255.f;
}

float cityOpeningBrightness(const CityOpening& opening, double elapsedMs)
{
    return cityTrackBrightness(opening.fades, elapsedMs);
}

float cityTrackBrightness(const std::vector<CityFade>& fades, double elapsedMs)
{
    const auto next = std::upper_bound(fades.begin(), fades.end(), elapsedMs,
        [](double time, const CityFade& fade) { return time < fade.startMs; });
    if (next == fades.begin()) return 1;
    const auto& fade = *std::prev(next);
    const auto amount = fade.durationMs ? static_cast<float>(
        std::clamp((elapsedMs - fade.startMs) / fade.durationMs, 0., 1.)) : 1.f;
    return fade.fadeIn ? amount : 1 - amount;
}

namespace {
void appendStaticPlane(CityTerrain& terrain, const CityMesh& mesh, const Transform& transform)
{
    if (mesh.positions.size() != 4 || mesh.buffers.empty()) return;
    const auto& indices = mesh.buffers.front().indices;
    std::array<Vec3, 4> positions;
    for (size_t i = 0; i < positions.size(); ++i) positions[i] = point(transform, mesh.positions[i]);
    std::array<std::array<unsigned, 4>, 4> edges{};
    bool steep = false;
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
        const auto normal = cross(add(positions[b], scale(positions[a], -1)),
                                  add(positions[c], scale(positions[a], -1)));
        if (std::abs(normal[2]) < std::hypot(normal[0], normal[1]) * 0.5f) {
            terrain.walls.push_back({positions[a], positions[b], positions[c]});
            steep = true;
        }
        ++edges[std::min(a, b)][std::max(a, b)];
        ++edges[std::min(b, c)][std::max(b, c)];
        ++edges[std::min(c, a)][std::max(c, a)];
    }
    if (steep) return;

    // Retail creates an XY polygon. Give its perimeter finite height for the native wall solver.
    constexpr float halfHeight = 100.f;
    for (size_t i = 0; i < positions.size(); ++i) {
        for (size_t j = i + 1; j < positions.size(); ++j) {
            if (edges[i][j] != 1) continue;
            const auto lowerA = add(positions[i], {0, 0, -halfHeight});
            const auto lowerB = add(positions[j], {0, 0, -halfHeight});
            const auto upperA = add(positions[i], {0, 0, halfHeight});
            const auto upperB = add(positions[j], {0, 0, halfHeight});
            terrain.walls.push_back({lowerA, lowerB, upperB});
            terrain.walls.push_back({lowerA, upperB, upperA});
        }
    }
}

void appendJumpRamp(CityTerrain& terrain, const CityMesh& mesh, const Transform& transform)
{
    if (mesh.positions.size() != 4 || mesh.buffers.empty()) return;
    const auto& indices = mesh.buffers.front().indices;
    if (indices.size() != 6) return;
    auto& ramp = terrain.jumpRamps.emplace_back();
    for (size_t i = 0; i < indices.size(); ++i)
        ramp[i / 3][i % 3] = point(transform, mesh.positions[indices[i]]);
}

void appendModelCollision(CityTerrain& terrain, const CityScene& model, const Transform& placement)
{
    std::vector<Transform> transforms;
    transforms.reserve(model.nodes.size());
    for (const auto& node : model.nodes) {
        const auto& parent = node.parent >= 0 ? transforms.at(node.parent) : placement;
        const auto& transform = transforms.emplace_back(compose(parent, nodeTransform(node)));
        const bool jump = node.name.find("Jump") != std::string::npos;
        if (!jump && node.name.find("Plane") == std::string::npos) continue;
        for (const auto& reference : node.geometry) {
            if (!reference.sourcePath.empty()) continue;
            const auto mesh = std::find_if(model.meshes.begin(), model.meshes.end(),
                [&](const CityMesh& candidate) { return candidate.name == reference.meshName; });
            if (mesh == model.meshes.end()) continue;
            if (jump) appendJumpRamp(terrain, *mesh, transform);
            else appendStaticPlane(terrain, *mesh, transform);
        }
    }
}

void appendScene(CityDrawData& draw, const CityAssets& city, const CityScene& scene,
                 const Transform& root, uint32_t startMs, const CityActor* actor = nullptr,
                 size_t actorIndex = 0, CityVehicleGeometry* vehicle = nullptr,
                 std::vector<CityCheckpointMesh>* markerMeshes = nullptr,
                 bool district = false, std::optional<size_t> selectedNode = std::nullopt,
                 const Transform* placedBillboard = nullptr, bool staticCollision = false)
{
    std::vector<Transform> transforms;
    transforms.reserve(scene.nodes.size());
    enum class VehiclePart { other, interior, exterior };
    std::vector<VehiclePart> vehicleParts;
    if (vehicle) vehicleParts.reserve(scene.nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
        if (selectedNode && nodeIndex != *selectedNode) continue;
        const auto& node = scene.nodes[nodeIndex];
        const auto& parent = node.parent >= 0 ? transforms.at(node.parent) : root;
        const auto transform = compose(parent, nodeTransform(node));
        transforms.push_back(transform);
        const auto normals = normalTransform(transform);
        const auto* billboard = placedBillboard;
        if (!billboard && district && node.property("billboard") == "1") billboard = &transform;
        const auto firstNodeVertex = draw.vertices.size();
        std::optional<CityLodLimit> lodLimit;
        if (const auto property = node.property("lodmax")) {
            int meters = 0;
            const auto result = std::from_chars(property->data(), property->data() + property->size(), meters);
            if (result.ec == std::errc{} && result.ptr == property->data() + property->size()) {
                const auto distance = meters * 100.f;
                lodLimit = CityLodLimit{transform[3], distance * distance};
            }
        }
        auto part = VehiclePart::other;
        if (vehicle) {
            if (node.name == "interior-node") part = VehiclePart::interior;
            else if (node.name == "car_body-node") part = VehiclePart::exterior;
            else if (node.parent >= 0) part = vehicleParts.at(node.parent);
            vehicleParts.push_back(part);
        }
        std::set<std::string_view> collisionSources;
        for (const auto& geometry : node.geometry) {
            const auto& meshLibrary = cityGeometryLibrary(city, scene, geometry);
            const auto& library = meshLibrary.meshes;
            const auto mesh = std::find_if(library.begin(), library.end(), [&](const CityMesh& candidate) {
                return candidate.name == geometry.meshName;
            });
            if (mesh == library.end()) throw std::runtime_error("No city mesh " + geometry.meshName);
            // CreateStaticBodies hides authored Plane nodes and uses their quads as collision proxies.
            const bool jump = node.name.find("Jump") != std::string::npos;
            if (district && !selectedNode && (jump || node.name.find("Plane") != std::string::npos)) {
                if (staticCollision) {
                    if (jump) appendJumpRamp(draw.terrain, *mesh, transform);
                    else appendStaticPlane(draw.terrain, *mesh, transform);
                }
                continue;
            }
            // AddXRef builds collision from the placed external scene, including its hidden nodes.
            if (staticCollision && !geometry.sourcePath.empty() && collisionSources.insert(geometry.sourcePath).second)
                appendModelCollision(draw.terrain, meshLibrary, transform);
            const auto base = static_cast<uint32_t>(draw.vertices.size());
            const auto baseSurface = draw.surfaces.size();
            if (vehicle && geometry.sourcePath.empty()) {
                for (size_t i = 0; i < scene.morphControllers.size(); ++i) {
                    const auto& morph = scene.morphControllers[i];
                    if (morph.source != mesh->name) continue;
                    if (morph.method > 1 || std::any_of(morph.targets.begin(), morph.targets.end(),
                        [&](const CityMorphController::Target& target) {
                            const auto& shape = scene.meshes.at(target.mesh);
                            return shape.positions.size() != mesh->positions.size() ||
                                   shape.normals.size() != mesh->normals.size();
                        })) throw std::runtime_error("Unsupported vehicle morph layout " + morph.name);
                    vehicle->vehicleMorphs.push_back({base, static_cast<size_t>(mesh - library.begin()), i, transform});
                }
            }
            if (markerMeshes) markerMeshes->push_back({transforms.size() - 1, base, mesh->positions, mesh->normals});
            if (actor && geometry.sourcePath.empty()) {
                for (size_t i = 0; i < actor->skins.size(); ++i)
                    if (actor->skins[i].meshName == mesh->name) draw.skins.push_back({actorIndex, i, base});
            }
            for (size_t i = 0; i < mesh->positions.size(); ++i) {
                const auto uv = mesh->texCoords.empty() ? std::array<float, 2>{} : mesh->texCoords[i];
                CityVertex vertex{point(transform, mesh->positions[i]), uv};
                if (!mesh->colors.empty()) vertex.color = mesh->colors[i];
                if (!mesh->normals.empty()) vertex.normal = direction(normals, mesh->normals[i]);
                draw.vertices.push_back(vertex);
            }
            float billboardRadius = 0;
            if (billboard) {
                const auto& axis = (*billboard)[0];
                const auto& origin = (*billboard)[3];
                const auto width = std::hypot(axis[0], axis[1]);
                if (width > 0.001f) {
                    draw.billboards.push_back({base, origin, {axis[0] / width, axis[1] / width},
                        {draw.vertices.begin() + base, draw.vertices.end()}});
                    for (const auto& vertex : draw.billboards.back().source)
                        billboardRadius = std::max(billboardRadius, std::hypot(
                            vertex.position[0] - origin[0], vertex.position[1] - origin[1]));
                }
            }
            for (size_t i = 0; i < mesh->buffers.size(); ++i) {
                const auto& buffer = mesh->buffers[i];
                const auto material = resolveCityMaterial(city, scene, meshLibrary, geometry, *mesh, i);
                // The retail city loader assigns trans_alphach_ref to these atlases.
                const std::string_view texture = material.texture;
                const auto name = texture.substr(texture.find_last_of("/\\") + 1);
                const bool alphaTest = material.alphaTest || (district &&
                    (name == "props_alpha.tga" || name == "props_alpha_01.tga"));
                const auto channel = [&](unsigned shift) { return float((material.diffuse >> shift) & 255) / 255; };
                draw.surfaces.push_back({static_cast<uint32_t>(draw.indices.size()), static_cast<uint32_t>(buffer.indices.size()),
                    material.texture, {channel(16), channel(8), channel(0), alphaTest ? 0.5f : 0.f},
                    startMs, actor ? actor->endMs : UINT32_MAX, actor && actor->name == "Player",
                    part == VehiclePart::interior, part == VehiclePart::exterior, vehicle != nullptr, {}, lodLimit});
                if (actor && actor->payphoneOnly) draw.surfaces.back().payphoneActor = actorIndex;
                for (const auto index : buffer.indices) draw.indices.push_back(base + index);
                if (billboardRadius > 0) {
                    auto least = (*billboard)[3], greatest = least;
                    for (const auto index : buffer.indices) {
                        const auto height = draw.vertices[base + index].position[2];
                        least[2] = std::min(least[2], height);
                        greatest[2] = std::max(greatest[2], height);
                    }
                    for (size_t axis = 0; axis < 2; ++axis) {
                        least[axis] -= billboardRadius;
                        greatest[axis] += billboardRadius;
                    }
                    draw.surfaces.back().bounds = CityBounds{least, greatest};
                }
            }
            if (district && !selectedNode && city.usableLodModels.count(geometry.sourcePath)) {
                const auto lod = readCityLodGroup(meshLibrary);
                const auto& firstNode = meshLibrary.nodes[lod.levels.front().node];
                const auto inverseBase = inverseTransform(nodeTransform(firstNode));
                if (firstNode.geometry.size() == 1 && firstNode.geometry.front().meshName == geometry.meshName &&
                    lod.levels.front().distanceSquared == 0 && inverseBase) {
                    const auto lodRoot = compose(transform, *inverseBase);
                    auto groupLimit = lod.maximumSquared.value_or(std::numeric_limits<float>::infinity());
                    if (lodLimit) groupLimit = std::min(groupLimit, lodLimit->distanceSquared);
                    for (size_t level = 0; level < lod.levels.size(); ++level) {
                        const auto end = level + 1 < lod.levels.size() ? lod.levels[level + 1].distanceSquared : groupLimit;
                        const CityLodLimit limit{transform[3], std::min(groupLimit, end),
                            level ? std::optional<float>{lod.levels[level].distanceSquared} : std::nullopt};
                        const auto start = level ? draw.surfaces.size() : baseSurface;
                        if (level)
                            appendScene(draw, city, meshLibrary, lodRoot, startMs, nullptr, 0, nullptr, nullptr,
                                        true, lod.levels[level].node, billboard);
                        for (size_t i = start; i < draw.surfaces.size(); ++i) {
                            draw.surfaces[i].lodLimit = limit;
                            draw.surfaces[i].lodAlternative = level != 0;
                        }
                    }
                }
            }
        }
        const auto count = draw.vertices.size() - firstNodeVertex;
        const bool frontWheel = node.name.compare(0, 12, "wheel_front_") == 0;
        const bool backWheel = node.name.compare(0, 11, "wheel_back_") == 0;
        if (vehicle && count && (frontWheel || backWheel)) {
            const auto begin = draw.vertices.begin() + firstNodeVertex;
            const auto limits = std::minmax_element(begin, draw.vertices.end(),
                [](const CityVertex& a, const CityVertex& b) { return a.position[2] < b.position[2]; });
            const auto radius = (limits.second->position[2] - limits.first->position[2]) / 2;
            if (radius > 0) {
                // The mesh child cancels the wheel node translation to place its prepositioned vertices.
                const auto& pivot = node.parent >= 0 && node.name == scene.nodes.at(node.parent).name + "1"
                    ? parent[3] : transform[3];
                vehicle->wheels.push_back({firstNodeVertex, count, pivot, radius, frontWheel,
                                       node.name.find("_left") != std::string::npos});
            }
        }
    }
}

std::array<float, 4> sample(const CityAnimationChannel& channel, double time)
{
    const auto next = std::upper_bound(channel.keys.begin(), channel.keys.end(), time,
        [](double ms, const CityAnimationKey& key) { return ms < key.timeMs; });
    if (next == channel.keys.begin()) return next->value;
    const auto& previous = *std::prev(next);
    if (next == channel.keys.end()) return previous.value;
    const auto amount = static_cast<float>((time - previous.timeMs) / (next->timeMs - previous.timeMs));
    const auto& first = previous.value;
    const auto& second = next->value;
    const auto cosine = dot({first[0], first[1], first[2]}, {second[0], second[1], second[2]}) +
                        first[3] * second[3];
    const float sign = channel.transform == CityAnimationTransform::rotation && cosine < 0 ? -1.f : 1.f;
    std::array<float, 4> result{};
    for (size_t i = 0; i < result.size(); ++i) result[i] = first[i] + (sign * second[i] - first[i]) * amount;
    return result;
}

void applyAnimationChannel(CityNode& pose, const CityAnimationChannel& channel, double time)
{
    const auto value = sample(channel, time);
    switch (channel.transform) {
    case CityAnimationTransform::translation: pose.translation = {value[0], value[1], value[2]}; break;
    case CityAnimationTransform::translationZ: pose.translation[2] = value[0]; break;
    case CityAnimationTransform::rotation: pose.rotation = value; break;
    case CityAnimationTransform::scale: pose.scale = {value[0], value[1], value[2]}; break;
    }
}

Transform affine(const std::array<float, 16>& matrix)
{
    return {{{matrix[0], matrix[1], matrix[2]}, {matrix[4], matrix[5], matrix[6]},
             {matrix[8], matrix[9], matrix[10]}, {matrix[12], matrix[13], matrix[14]}}};
}

CityTerrain::VehicleFootprint measureVehicleFootprint(const CityActor& actor,
                                                      const std::vector<CityVertex>& vertices, size_t first)
{
    const float yaw = actor.rotationDegrees[2] * radians;
    const Vec3 lateral{std::cos(yaw), std::sin(yaw), 0};
    const Vec3 forward{lateral[1], -lateral[0], 0};
    float minX = std::numeric_limits<float>::infinity(), maxX = -minX;
    float minY = minX, maxY = -minX;
    for (size_t i = first; i < vertices.size(); ++i) {
        const auto relative = add(vertices[i].position, scale(actor.position, -1));
        const auto x = dot(relative, lateral), y = dot(relative, forward);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    if (minX >= maxX || minY >= maxY) return {};
    const auto radius = std::min(maxX - minX, maxY - minY) / 2;
    return {(minX + maxX) / 2, radius, {minY + radius, (minY + maxY) / 2, maxY - radius}};
}

const CityAnimationClip* doorClip(const CityActor& actor, size_t side, const char* name)
{
    const auto& sideClips = actor.doorClips[side];
    const auto clip = std::find_if(sideClips.begin(), sideClips.end(),
        [&](const CityAnimationClip& entry) { return entry.name == name; });
    return clip == sideClips.end() ? nullptr : &*clip;
}

std::pair<const CityAnimationClip*, double> vehicleDoorFrame(const CityActor& actor, const CityVehiclePose& car,
                                                           const CityOpening& opening)
{
    if (!car.doorAction) return {};
    const auto [side, exiting, elapsedMs] = *car.doorAction;
    const auto* first = doorClip(actor, side, exiting ? "car_exit_car" : "car_open_door");
    if (opening.vehicleCharacterClips) {
        const auto& player = (*opening.vehicleCharacterClips)[side];
        const double initialDuration = exiting ? player.exit.durationMs : player.open.durationMs;
        if (first && elapsedMs < initialDuration)
            return {first, elapsedMs * first->durationMs / initialDuration};
        if (exiting) return {};
        const auto enterTime = elapsedMs - initialDuration;
        if (first && enterTime < player.enter.durationMs) return {first, first->durationMs};
        const auto closeTime = enterTime - player.enter.durationMs;
        const auto* close = doorClip(actor, side, "car_close_door");
        if (close && closeTime < player.close.durationMs)
            return {close, closeTime * close->durationMs / player.close.durationMs};
        return {};
    }
    if (first && elapsedMs < first->durationMs) return {first, elapsedMs};
    if (exiting) return {};
    const auto closeTime = elapsedMs - (first ? first->durationMs : 0);
    const auto* close = doorClip(actor, side, "car_close_door");
    if (close && closeTime < close->durationMs) return {close, closeTime};
    return {};
}

std::vector<Transform> animatedSkeleton(const CityScene& model, const CityAnimationClip& clip,
                                       double time, bool removeRootMotion)
{
    std::vector<Transform> transforms;
    transforms.reserve(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& source = model.nodes[i];
        CityNode pose{};
        pose.translation = source.translation;
        pose.rotation = source.rotation;
        pose.scale = source.scale;
        for (const auto& channel : clip.channels) {
            if (channel.node != i) continue;
            applyAnimationChannel(pose, channel, time);
            // Movement already applies the walk cycle's net root displacement.
            if (removeRootMotion && channel.transform == CityAnimationTransform::translation &&
                source.name == "Bip01-node") {
                const auto fraction = float(time / clip.durationMs);
                for (size_t j = 0; j < 2; ++j)
                    pose.translation[j] -= (channel.keys.back().value[j] - channel.keys.front().value[j]) * fraction;
            }
        }
        const auto& parent = source.parent < 0 ? identity : transforms[source.parent];
        transforms.push_back(compose(parent, nodeTransform(pose, true)));
    }
    return transforms;
}

void poseSkinnedMesh(const CityScene& model, const CitySkin& skin, const std::vector<Transform>& transforms,
                     const Transform& world, const CityVehiclePose* vehicle, CityVertex* vertices)
{
    const auto bindShape = affine(skin.bindShape);
    std::vector<Transform> joints;
    joints.reserve(skin.joints.size());
    for (const auto& joint : skin.joints)
        joints.push_back(compose(transforms[joint.node], compose(affine(joint.inverseBind), bindShape)));
    const auto mesh = std::find_if(model.meshes.begin(), model.meshes.end(),
        [&](const CityMesh& entry) { return entry.name == skin.meshName; });
    std::vector<Transform> jointNormals;
    if (!mesh->normals.empty()) {
        jointNormals.reserve(joints.size());
        for (const auto& joint : joints) jointNormals.push_back(normalTransform(compose(world, joint)));
    }
    const auto morph = vehicle ? std::find_if(model.morphControllers.begin(), model.morphControllers.end(),
        [&](const CityMorphController& entry) { return entry.source == skin.meshName; })
        : model.morphControllers.end();
    for (size_t i = 0; i < skin.vertices.size(); ++i) {
        auto sourcePosition = mesh->positions[i];
        auto sourceNormal = mesh->normals.empty() ? Vec3{} : mesh->normals[i];
        if (morph != model.morphControllers.end())
            morphVehicleVertex(model, *morph, *vehicle, i, sourcePosition, sourceNormal);
        Vec3 position{}, normal{};
        for (const auto& influence : skin.vertices[i]) {
            position = add(position, scale(point(joints[influence.joint], sourcePosition), influence.weight));
            if (!jointNormals.empty()) {
                const auto transformed = direction(jointNormals[influence.joint], sourceNormal);
                normal = add(normal, scale(transformed, influence.weight));
            }
        }
        auto& vertex = vertices[i];
        vertex.position = point(world, position);
        vertex.normal = normal;
    }
}

std::pair<const CityAnimationClip*, double> vehicleCharacterFrame(const CityOpening& opening,
                                                                 const CityVehiclePose& vehicle)
{
    if (!opening.vehicleCharacterClips) return {};
    if (!vehicle.doorAction) {
        if (!vehicle.occupied || !opening.vehicleSeated) return {};
        const auto& seated = *opening.vehicleSeated;
        return {&seated, std::fmod(vehicle.seatedMs, seated.durationMs)};
    }
    const auto [side, exiting, ms] = *vehicle.doorAction;
    const auto& clips = (*opening.vehicleCharacterClips)[side];
    if (exiting) return {&clips.exit, ms};
    if (ms < clips.open.durationMs) return {&clips.open, ms};
    const auto enterTime = ms - clips.open.durationMs;
    if (enterTime < clips.enter.durationMs) return {&clips.enter, enterTime};
    return {&clips.close, enterTime - clips.enter.durationMs};
}

Transform vehicleCharacterTransform(const CityActor& player, const CityActor& car,
                                    const CityVehiclePose& vehicle, const CityVehicleCharacterClips& clips,
                                    const CityAnimationClip& clip)
{
    const auto side = vehicle.doorAction ? vehicle.doorAction->side : vehicle.entrySide;
    const auto* doorName = side == 0 ? "dummy_door_front_left-node" : "dummy_door_front_right-node";
    const auto door = std::find_if(car.model.nodes.begin(), car.model.nodes.end(),
        [&](const CityNode& node) { return node.name == doorName; });
    const auto carWorld = vehicleTransform(car, vehicle);
    Vec3 doorPosition{side == 0 ? 165.f : -165.f, 0, 0};
    if (door != car.model.nodes.end()) {
        doorPosition = door->translation;
        for (auto parent = door->parent; parent >= 0; parent = car.model.nodes[parent].parent)
            doorPosition = point(nodeTransform(car.model.nodes[parent]), doorPosition);
    }
    doorPosition[2] = 0;
    const auto enterYaw = side == 0 ? -90.f : 90.f;
    if (&clip == &clips.open || &clip == &clips.enter)
        return placementTransform(point(carWorld, doorPosition), {}, player.scale, vehicle.yaw + enterYaw);

    const auto root = std::find_if(clips.enter.channels.begin(), clips.enter.channels.end(),
        [](const CityAnimationChannel& channel) {
            return channel.transform == CityAnimationTransform::translation && channel.target == "Bip01-node";
        });
    if (root != clips.enter.channels.end()) {
        const auto& first = root->keys.front().value;
        const auto& last = root->keys.back().value;
        const auto orientation = placementTransform({}, {}, player.scale, enterYaw);
        doorPosition = add(doorPosition, direction(orientation, {last[0] - first[0], last[1] - first[1], 0}));
    }
    return placementTransform(point(carWorld, doorPosition), {}, player.scale,
                              vehicle.yaw + (&clip == &clips.exit ? -90.f : 0.f));
}
}

CityParticleFrame cityParticleFrame(const CityScene& scene, size_t node, const CityAnimationClip& clip,
                                    double elapsedMs, const CityParticleFrame& parent)
{
    auto local = identity;
    for (auto index = static_cast<int32_t>(node); index >= 0; index = scene.nodes.at(index).parent) {
        const auto& source = scene.nodes.at(index);
        CityNode pose{};
        pose.translation = source.translation;
        pose.rotation = source.rotation;
        pose.scale = source.scale;
        for (const auto& channel : clip.channels)
            if (channel.node == static_cast<size_t>(index)) applyAnimationChannel(pose, channel, elapsedMs);
        local = compose(nodeTransform(pose), local);
    }
    const auto world = compose({parent.axes[0], parent.axes[1], parent.axes[2], parent.origin}, local);
    return {world[3], {world[0], world[1], world[2]}};
}

CitySpawnPool::CitySpawnPool(const std::map<uint8_t, CitySpawnGroup>& groups)
{
    constexpr size_t maximumSlots = 256;
    std::array<unsigned, maximumSlots> counts{};
    for (const auto& entry : groups)
        for (const auto metatype : entry.second.metatypes)
            counts[metatype] = isCityPoliceMetatype(metatype) ? 3 : 1;
    // CreateAllSpawnInstances reserves metatype 161 even when no spawn group names it.
    counts[161] = 3;
    for (size_t metatype = 0; metatype < counts.size(); ++metatype) {
        if (slots.size() + counts[metatype] > maximumSlots)
            throw std::runtime_error("Retail spawn pool exceeds 256 slots.");
        slots.insert(slots.end(), counts[metatype], Slot{static_cast<uint8_t>(metatype)});
    }
}

std::optional<size_t> CitySpawnPool::acquire(uint8_t metatype)
{
    const auto slot = std::find_if(slots.begin(), slots.end(), [&](const Slot& candidate) {
        return !candidate.active && candidate.metatype == metatype;
    });
    if (slot == slots.end()) return std::nullopt;
    slot->active = true;
    if (isCityPoliceMetatype(metatype)) ++activeCops;
    return static_cast<size_t>(slot - slots.begin());
}

bool CitySpawnPool::release(size_t index)
{
    if (index >= slots.size() || !slots[index].active) return false;
    auto& slot = slots[index];
    slot.active = false;
    if (isCityPoliceMetatype(slot.metatype)) --activeCops;
    return true;
}

CitySpawnTimer::CitySpawnTimer(const CitySpawnGroup& group, uint32_t initialDraw)
    : remaining(group.spawnDelayMs(false, initialDraw))
{
}

void CitySpawnTimer::advance(uint32_t elapsedMs, const CitySpawnGroup& group, bool eligible,
                             CitySpawnPool& pool, int copLimit, bool alternateCopContext,
                             const std::function<uint32_t()>& random,
                             const std::function<void(size_t, uint8_t)>& spawn)
{
    if (remaining > 0) {
        remaining -= std::min(remaining, elapsedMs);
        return;
    }
    if (eligible) {
        const auto metatype = group.chooseForSpawn(random(), pool.spawnedCops(), copLimit, alternateCopContext);
        if (metatype) {
            const auto slot = pool.acquire(*metatype);
            if (slot) spawn(*slot, *metatype);
        }
    }
    remaining = group.spawnDelayMs(eligible, random());
}

CitySpawnScheduler::CitySpawnScheduler(const CityAssets& city, const CityNavigation& navigation,
                                       const std::function<uint32_t()>& random)
    : groups(city.spawnGroups), pool(groups)
{
    for (const auto& district : city.districts)
        for (const auto& point : district.spawnPoints)
            entries.push_back({point, CitySpawnTimer(groups.at(point.group), random()), navigation.roadFlags(point)});
}

void CitySpawnScheduler::advance(uint32_t elapsedMs, const CityBounds& query, const CityCameraPose& camera,
                                float actorHeight, std::optional<float> vehicleSpeed, CitySpawnContext context,
                                int copLimit, bool alternateCopContext, const std::function<uint32_t()>& random,
                                const Spawn& spawn)
{
    const auto traffic = context.traffic;
    for (auto& entry : entries) {
        const auto& point = entry.point;
        const auto& position = point.position;
        if (position[0] < query.minimum[0] || position[0] > query.maximum[0] ||
            position[1] < query.minimum[1] || position[1] > query.maximum[1] ||
            position[2] < query.minimum[2] || position[2] > query.maximum[2])
            continue;

        const auto& group = groups.at(point.group);
        bool eligible = false;
        if (entry.timer.remainingMs() == 0) {
            const auto centers = group.spawnCenters(camera.eye, camera.target, actorHeight, vehicleSpeed);
            context.innerCenter = centers[0];
            context.outerCenter = centers[1];
            context.traffic = traffic && entry.roadFlags ? traffic : std::nullopt;
            if (context.traffic) context.traffic->roadFlags = *entry.roadFlags;
            eligible = group.canSpawn(point, context);
        }
        entry.timer.advance(elapsedMs, group, eligible, pool, copLimit, alternateCopContext, random,
                            [&](size_t slot, uint8_t metatype) {
            if (!spawn(slot, metatype, point)) {
                pool.release(slot);
                return;
            }
            ++context.activeCounts[group.populationIndex()];
            context.nearbyObjects.push_back({position, group.objectType, 1});
        });
    }
}

std::vector<CitySpawnMesh> appendCitySpawnMeshes(CityDrawData& draw, const CityAssets& city,
                                                const CitySpawnPool& pool, FileSystemBase& assets)
{
    auto definitions = loadCitySpawnActors(assets, city);
    std::map<uint8_t, std::shared_ptr<const CityActor>> actors;
    std::vector<CitySpawnMesh> meshes;
    meshes.reserve(pool.capacity());
    for (size_t slot = 0; slot < pool.capacity(); ++slot) {
        const auto id = pool.metatype(slot);
        auto [entry, inserted] = actors.try_emplace(id);
        if (inserted) entry->second = std::make_shared<CityActor>(std::move(definitions.at(id)));
        const auto& actor = entry->second;
        const auto firstVertex = draw.vertices.size(), firstSurface = draw.surfaces.size();
        const auto firstSkin = draw.skins.size();
        appendScene(draw, city, actor->model, identity, 0, actor.get());
        if (draw.surfaces.size() == firstSurface)
            throw std::runtime_error("Missing spawn geometry for " + actor->name);
        auto& mesh = meshes.emplace_back(CitySpawnMesh{firstVertex, firstSurface,
            draw.surfaces.size() - firstSurface, actor,
            {draw.vertices.begin() + firstVertex, draw.vertices.end()},
            {draw.skins.begin() + firstSkin, draw.skins.end()}});
        draw.skins.resize(firstSkin);
        if (actor->fightIdle && mesh.skins.empty())
            throw std::runtime_error("Missing ambient skin geometry for " + actor->name);
        for (auto& skin : mesh.skins) skin.firstVertex -= static_cast<uint32_t>(firstVertex);
        if (!actor->fightIdle) {
            mesh.footprint = measureVehicleFootprint(*actor, draw.vertices, firstVertex);
            mesh.footprint.lateralCenter *= actor->scale[0];
            mesh.footprint.radius *= std::abs(actor->scale[0]);
            for (auto& center : mesh.footprint.longitudinalCenters) center *= actor->scale[1];
        }
    }
    return meshes;
}

void poseCitySpawnMesh(const CitySpawnMesh& mesh, const std::array<float, 3>& position,
                       const std::array<float, 3>& rotation, double elapsedMs, CityVertex* target, bool walking)
{
    const auto& actor = *mesh.actor;
    const auto transform = placementTransform(position, rotation, actor.scale, rotation[2]);
    const auto normals = normalTransform(transform);
    for (size_t i = 0; i < mesh.source.size(); ++i) {
        const auto& vertex = mesh.source[i];
        target[i] = vertex;
        target[i].position = point(transform, vertex.position);
        target[i].normal = direction(normals, vertex.normal);
    }
    if (!actor.fightIdle) return;
    const auto& clip = walking ? *actor.pursuitWalk : *actor.fightIdle;
    const auto transforms = animatedSkeleton(actor.model, clip, std::fmod(elapsedMs, clip.durationMs), walking);
    for (const auto& range : mesh.skins)
        poseSkinnedMesh(actor.model, actor.skins[range.skin], transforms, transform, nullptr,
                        target + range.firstVertex);
}

CityFrustum cityFrustum(const CityCameraMatrix& camera)
{
    const auto plane = [&](size_t axis, float sign) {
        auto result = camera[3];
        for (size_t i = 0; i < result.size(); ++i) result[i] += sign * camera[axis][i];
        return result;
    };
    // Vulkan clips X and Y to [-W, W] and Z to [0, W].
    return {plane(0, 1), plane(0, -1), plane(1, 1), plane(1, -1), camera[2], plane(2, -1)};
}

bool cityBoundsVisible(const CityBounds& bounds, const CityFrustum& frustum)
{
    for (const auto& plane : frustum) {
        auto distance = plane[3];
        for (size_t i = 0; i < 3; ++i) {
            const auto extent = plane[i] >= 0 ? bounds.maximum[i] : bounds.minimum[i];
            distance += plane[i] * extent;
        }
        if (distance < 0) return false;
    }
    return true;
}

void poseCityBillboard(const CityBillboard& billboard, const CityCameraMatrix& camera, CityVertex* vertices)
{
    const auto width = std::hypot(camera[0][0], camera[0][1]);
    if (width <= 0.001f) return;
    const float rightX = camera[0][0] / width, rightY = camera[0][1] / width;
    const auto& [oldX, oldY] = billboard.right;
    const float cosine = oldX * rightX + oldY * rightY, sine = oldX * rightY - oldY * rightX;
    const auto rotate = [&](const Vec3& v) -> Vec3 {
        return {cosine * v[0] - sine * v[1], sine * v[0] + cosine * v[1], v[2]};
    };
    for (size_t i = 0; i < billboard.source.size(); ++i) {
        const auto& source = billboard.source[i];
        auto& vertex = vertices[billboard.firstVertex + i];
        vertex = source;
        vertex.position = add(billboard.origin, rotate(add(source.position, scale(billboard.origin, -1))));
        vertex.normal = rotate(source.normal);
    }
}

bool CityLodLimit::visible(const std::array<float, 3>& eye) const
{
    const auto delta = add(eye, scale(origin, -1));
    const auto distance = dot(delta, delta);
    return distance <= distanceSquared && (!minimumSquared || distance > *minimumSquared);
}

CityDrawData prepareCityView(const CityAssets& city, const CityOpening& opening)
{
    CityDrawData draw;
    size_t openingFirstIndex = 0, openingEndIndex = 0;
    size_t openingFirstSurface = 0, openingEndSurface = 0;
    for (size_t i = 0; i < city.districts.size(); ++i) {
        const auto firstIndex = draw.indices.size();
        const auto firstSurface = draw.surfaces.size();
        appendScene(draw, city, city.districts[i].scene, identity, 0, nullptr, 0, nullptr, nullptr,
                    true, std::nullopt, nullptr, true);
        if (i == opening.district) {
            openingFirstIndex = firstIndex;
            openingEndIndex = draw.indices.size();
            openingFirstSurface = firstSurface;
            openingEndSurface = draw.surfaces.size();
        }
    }
    if (openingFirstIndex == openingEndIndex)
        throw std::runtime_error("The retail opening district has no triangles.");
    draw.districtSurfaceCount = draw.surfaces.size();
    for (const auto& district : city.districts)
        appendScene(draw, city, district.lowDetailScene, identity, 0, nullptr, 0, nullptr, nullptr, true);
    draw.lowDetailEndSurface = draw.surfaces.size();
    for (auto& surface : draw.surfaces) {
        if (surface.bounds) continue;
        const auto first = draw.vertices[draw.indices[surface.firstIndex]].position;
        CityBounds bounds{first, first};
        for (size_t i = 1; i < surface.indexCount; ++i) {
            const auto& point = draw.vertices[draw.indices[surface.firstIndex + i]].position;
            for (size_t axis = 0; axis < point.size(); ++axis) {
                bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
                bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
            }
        }
        surface.bounds = bounds;
    }
    if (opening.followCameraMs) {
        for (size_t surfaceIndex = openingFirstSurface; surfaceIndex < openingEndSurface; ++surfaceIndex) {
            const auto& surface = draw.surfaces[surfaceIndex];
            if (surface.lodAlternative) continue;
            for (size_t i = surface.firstIndex; i < size_t(surface.firstIndex) + surface.indexCount; i += 3) {
                const auto& a = draw.vertices[draw.indices[i]].position;
                const auto& b = draw.vertices[draw.indices[i+1]].position;
                const auto& c = draw.vertices[draw.indices[i+2]].position;
                const auto normal = cross(add(b, scale(a, -1)), add(c, scale(a, -1)));
                if (std::max({a[2], b[2], c[2]}) - std::min({a[2], b[2], c[2]}) >= 45 &&
                    std::abs(normal[2]) < std::hypot(normal[0], normal[1]) * 0.5f)
                    draw.terrain.walls.push_back({a, b, c});
            }
        }
    }
    for (size_t i = 0; i < opening.actors.size(); ++i) {
        const auto& actor = opening.actors[i];
        const auto first = static_cast<uint32_t>(draw.vertices.size());
        const auto* nextMission = opening.payphone && opening.payphone->postFight.nextCheckpoint
            ? &*opening.payphone->postFight.nextCheckpoint : nullptr;
        const bool nextVehicle = actor.payphoneOnly && nextMission && actor.name == nextMission->targetVehicle;
        const bool postBasketballVehicle = nextMission && actor.name == nextMission->postBasketballVehicle;
        const bool van = actor.payphoneOnly && nextMission && actor.name == "aVan_00";
        const bool lincoln = actor.payphoneOnly && nextMission && actor.name == "aLincoln_00";
        const bool cooper = actor.payphoneOnly && nextMission && !nextMission->locoCooperIntro.vehicleDrives.empty() &&
            actor.name == nextMission->locoCooperIntro.vehicleDrives.front().actor;
        CityVehicleGeometry* vehicle = opening.vehicleActor == i ? &draw :
            nextVehicle ? &draw.nextVehicleGeometry.emplace() : cooper ? &draw.cooperGeometry.emplace() :
            postBasketballVehicle ? &draw.postBasketballVehicleGeometry.emplace() :
            van ? &draw.vanGeometry.emplace() : lincoln ? &draw.lincolnGeometry.emplace() : nullptr;
        if (cooper) draw.cooperActor = i;
        appendScene(draw, city, actor.model, actorTransform(actor), actor.startMs, &actor, i, vehicle);
        if (vehicle) {
            vehicle->vehicleFirstVertex = first;
            vehicle->vehicleVertexCount = static_cast<uint32_t>(draw.vertices.size()) - first;
            const auto footprint = measureVehicleFootprint(actor, draw.vertices, first);
            if (nextVehicle) draw.nextVehicleFootprint = footprint;
            else if (postBasketballVehicle) draw.postBasketballVehicleFootprint = footprint;
            else if (cooper) draw.cooperFootprint = footprint;
            else if (van) draw.vanFootprint = footprint;
            else if (lincoln) draw.lincolnFootprint = footprint;
            else draw.terrain.vehicleFootprint = footprint;
            for (auto& wheel : vehicle->wheels) wheel.firstVertex -= first;
            for (auto& morph : vehicle->vehicleMorphs) morph.firstVertex -= first;
        }
    }
    const auto appendCheckpoint = [&](const CityCheckpoint& checkpoint, size_t index) {
        const auto transform = placementTransform(checkpoint.position, checkpoint.rotationDegrees,
                                                   checkpoint.scale, checkpoint.rotationDegrees[2]);
        const auto firstSurface = draw.surfaces.size();
        appendScene(draw, city, checkpoint.model, transform, opening.durationMs, nullptr, 0, nullptr,
                    checkpoint.animation ? &draw.checkpointMeshes[index] : nullptr);
        return firstSurface;
    };
    if (opening.checkpoint) draw.checkpointFirstSurface = appendCheckpoint(*opening.checkpoint, 0);
    if (opening.payphone) draw.payphoneFirstSurface = appendCheckpoint(opening.payphone->destination, 1);
    if (opening.vehicleObjective && opening.vehicleActor) {
        const auto& actor = opening.actors.at(*opening.vehicleActor);
        draw.vehicleObjectiveFirstSurface = draw.surfaces.size();
        appendScene(draw, city, opening.vehicleObjective->model, actorTransform(actor),
                    opening.durationMs, nullptr, 0, nullptr, &draw.checkpointMeshes[2]);
    }
    if (opening.payphone && opening.payphone->postFight.nextCheckpoint) {
        const auto& next = *opening.payphone->postFight.nextCheckpoint;
        draw.nextMissionFirstSurface = appendCheckpoint(next.marker, 3);
        if (next.vehicleCheckpoint)
            draw.hummerRouteFirstSurface = appendCheckpoint(*next.vehicleCheckpoint, 4);
        if (next.weaponShopDriveCheckpoint)
            draw.weaponShopDriveFirstSurface = appendCheckpoint(*next.weaponShopDriveCheckpoint, 5);
        if (next.weaponShopApproach)
            draw.weaponShopApproachFirstSurface = appendCheckpoint(*next.weaponShopApproach, 6);
        if (next.weaponShopEntranceMarker)
            draw.weaponShopEntranceFirstSurface = appendCheckpoint(*next.weaponShopEntranceMarker, 7);
        if (next.returnToLcCheckpoint)
            draw.returnToLcFirstSurface = appendCheckpoint(*next.returnToLcCheckpoint, 8);
        if (next.postLcDriveCheckpoint)
            draw.postLcDriveFirstSurface = appendCheckpoint(*next.postLcDriveCheckpoint, 9);
        for (size_t i = 0; i < next.postLcWalkCheckpoints.size(); ++i)
            if (next.postLcWalkCheckpoints[i])
                draw.postLcWalkFirstSurfaces[i] = appendCheckpoint(*next.postLcWalkCheckpoints[i], 10 + i);
        if (next.cooperNextDriveCheckpoint)
            draw.cooperNextDriveFirstSurface = appendCheckpoint(*next.cooperNextDriveCheckpoint, 13);
        if (next.basketballCheckpoint)
            draw.basketballFirstSurface = appendCheckpoint(*next.basketballCheckpoint, 14);
        if (next.postBasketballDestination)
            draw.postBasketballFirstSurface = appendCheckpoint(*next.postBasketballDestination, 15);
        if (next.repairDriveCheckpoint)
            draw.repairDriveFirstSurface = appendCheckpoint(*next.repairDriveCheckpoint, 16);
        if (next.repairShopApproach)
            draw.repairShopFirstSurface = appendCheckpoint(*next.repairShopApproach, 17);
        if (next.deliveryCheckpoint)
            draw.deliveryFirstSurface = appendCheckpoint(*next.deliveryCheckpoint, 18);
        if (next.elToroCheckpoint)
            draw.elToroFirstSurface = appendCheckpoint(*next.elToroCheckpoint, 19);
        if (next.elToroDestination)
            draw.elToroDestinationFirstSurface = appendCheckpoint(*next.elToroDestination, 20);
        if (next.elToroDriveCheckpoint)
            draw.elToroDriveFirstSurface = appendCheckpoint(*next.elToroDriveCheckpoint, 21);
        if (next.vanDriveCheckpoint)
            draw.vanDriveFirstSurface = appendCheckpoint(*next.vanDriveCheckpoint, 22);
        if (next.vanSecondDriveCheckpoint)
            draw.vanSecondDriveFirstSurface = appendCheckpoint(*next.vanSecondDriveCheckpoint, 23);
        if (next.vanThirdDriveCheckpoint)
            draw.vanThirdDriveFirstSurface = appendCheckpoint(*next.vanThirdDriveCheckpoint, 24);
        if (next.vanFourthDriveCheckpoint)
            draw.vanFourthDriveFirstSurface = appendCheckpoint(*next.vanFourthDriveCheckpoint, 25);
        if (next.script19DriveCheckpoint)
            draw.script19DriveFirstSurface = appendCheckpoint(*next.script19DriveCheckpoint, 26);
        if (next.script19ReturnCheckpoint)
            draw.script19ReturnFirstSurface = appendCheckpoint(*next.script19ReturnCheckpoint, 27);
        if (next.script20DriveCheckpoint)
            draw.script20DriveFirstSurface = appendCheckpoint(*next.script20DriveCheckpoint, 28);
        if (next.script32Checkpoint)
            draw.script32FirstSurface = appendCheckpoint(*next.script32Checkpoint, 29);
        if (next.script33Checkpoint)
            draw.script33FirstSurface = appendCheckpoint(*next.script33Checkpoint, 30);
        if (next.script33DriveCheckpoint)
            draw.script33DriveFirstSurface = appendCheckpoint(*next.script33DriveCheckpoint, 31);
    }
    if (opening.vehicleActor) {
        const auto& actor = opening.actors.at(*opening.vehicleActor);
        if (actor.lights) for (const auto& effect : actor.lights->effects)
            for (size_t mount = 0; mount < actor.lights->mountNodes.size(); ++mount) {
                const auto vertex = draw.vertices.size();
                const auto surface = draw.surfaces.size();
                appendScene(draw, city, effect, identity, 0);
                draw.vehicleLights.push_back({vertex, surface, draw.surfaces.size() - surface,
                    {draw.vertices.begin() + vertex, draw.vertices.end()}});
            }
    }
    if (opening.followCameraMs) {
        CityDrawData terrain;
        appendScene(terrain, city, opening.heightMap, identity, 0);
        for (size_t i = 0; i < terrain.indices.size(); i += 3) {
            draw.terrain.ground.push_back({terrain.vertices[terrain.indices[i]].position,
                                   terrain.vertices[terrain.indices[i+1]].position,
                                   terrain.vertices[terrain.indices[i+2]].position});
        }
        if (draw.terrain.ground.empty()) throw std::runtime_error("Missing retail walking terrain.");
        if (!opening.navigation.buffers.empty()) {
            if (terrain.surfaces.size() != opening.navigation.buffers.size())
                throw std::runtime_error("Heightmap surfaces do not match retail navigation buffers.");
            for (size_t buffer = 0; buffer < terrain.surfaces.size(); ++buffer) {
                const auto& surface = terrain.surfaces[buffer];
                if (surface.firstIndex != draw.terrain.groundNavigation.size() * 3 ||
                    surface.indexCount != opening.navigation.buffers[buffer].roadFlags.size() * 3)
                    throw std::runtime_error("Heightmap triangles do not match retail navigation offsets.");
                for (uint32_t offset = 0; offset < surface.indexCount; offset += 3)
                    draw.terrain.groundNavigation.push_back({static_cast<uint16_t>(buffer),
                                                             static_cast<uint16_t>(offset)});
            }
            const auto cliffBuffer = opening.navigation.materialBuffers[
                static_cast<size_t>(CityNavigation::Material::cliff)];
            const auto& cliff = terrain.surfaces.at(cliffBuffer);
            std::vector<bool> used(cliff.indexCount / 3);
            for (const auto offset : opening.navigation.cliffTriangles)
                if (offset != UINT16_MAX) used.at(offset / 3) = true;
            std::vector<CityTriangle> walls;
            walls.reserve(used.size() + draw.terrain.walls.size());
            for (size_t i = 0; i < used.size(); ++i)
                if (used[i]) walls.push_back(draw.terrain.ground[cliff.firstIndex / 3 + i]);
            // Keep the visual collision for obstacles outside the authored cliff table.
            walls.insert(walls.end(), draw.terrain.walls.begin(), draw.terrain.walls.end());
            draw.terrain.walls = std::move(walls);
        }
        draw.terrain.indexGround(&opening.navigation);
        draw.terrain.indexWalls();
    }
    return draw;
}

static const CityCinematicPlacement* cinematicPlacement(const std::vector<CityCinematicPlacement>& placements,
                                                        std::string_view actor, double elapsedMs)
{
    const auto found = std::find_if(placements.rbegin(), placements.rend(), [&](const auto& placement) {
        return placement.timeMs <= elapsedMs && placement.actor == actor;
    });
    return found == placements.rend() ? nullptr : &*found;
}

bool citySurfaceVisible(const CitySurface& surface, const CityOpening& opening, const CityPlayback& playback,
                        std::optional<double> payphoneMs, std::optional<double> missionMs,
                        const CityMissionCinematic* missionScene)
{
    if (surface.payphoneActor) {
        if (!payphoneMs || !opening.payphone) return false;
        if (surface.vehicleInterior)
            return playback.nextMissionVehicleActive && opening.vehicleActor == surface.payphoneActor &&
                playback.vehicle && playback.vehicle->doorAction;
        const auto& actor = opening.actors.at(*surface.payphoneActor);
        const auto loco = playback.locoOpponents.find(actor.name);
        if (playback.payphoneOpponentDisabled && playback.payphoneAttack &&
            actor.name == playback.payphoneAttack->attacker) return false;
        if (missionMs && missionScene) {
            if (const auto* cue = cinematicPlacement(missionScene->placements, actor.name, *missionMs))
                return cue->visible;
        }
        const auto& next = opening.payphone->postFight.nextCheckpoint;
        if ((!missionScene || (next && (missionScene == &next->locoCooperIntro || missionScene == &next->elToroInterlude ||
                                      missionScene == &next->elToroDeparture || missionScene == &next->vanHandshake || missionScene == &next->vanPoliceReveal))) &&
            loco != playback.locoOpponents.end()) return !loco->second.disabled;
        if (playback.nextMissionVehicleActive && next &&
            (actor.name == next->postBasketballVehicle || actor.name == "aCooper_00" ||
             actor.name == "aVan_00" || actor.name == "aLincoln_00"))
            return opening.vehicleActor == surface.payphoneActor && playback.vehicle.has_value();
        if (playback.nextMissionVehicleActive && next && actor.name == next->targetVehicle)
            return (!opening.vehicleActor || !opening.actors[*opening.vehicleActor].payphoneOnly ||
                    opening.vehicleActor == surface.payphoneActor) &&
                (!playback.openingMissionsPaid[1] || playback.postLcVehicleActive);
        const auto* placement = cityPayphonePlacement(*opening.payphone,
            actor.name, *payphoneMs);
        return placement && placement->visible;
    }
    if (playback.elapsedMs < surface.startMs || playback.elapsedMs >= surface.endMs) return false;
    const auto* vehicle = playback.vehicle ? &*playback.vehicle : nullptr;
    const bool doorMoving = vehicle && vehicle->doorAction.has_value();
    if (surface.vehicleInterior) return doorMoving && !playback.nextMissionVehicleActive;
    return !surface.player || !vehicle || !vehicle->occupied || doorMoving || opening.vehicleSeated.has_value();
}

bool citySurfaceCullBackfaces(const CitySurface& surface, const CityPlayback& playback)
{
    const bool active = surface.payphoneActor.has_value() == playback.nextMissionVehicleActive;
    return surface.vehicleExterior && (!active || !playback.vehicle || !playback.vehicle->doorAction);
}

static void poseCityMarker(const CityScene& model, const CityAnimationClip& clip, const Transform& world,
                           const std::vector<CityCheckpointMesh>& meshes, double elapsedMs, CityVertex* vertices)
{
    const auto time = std::fmod(std::max(0., elapsedMs), clip.durationMs);
    std::vector<Transform> transforms;
    transforms.reserve(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        CityNode pose{};
        pose.translation = node.translation;
        pose.rotation = node.rotation;
        pose.scale = node.scale;
        for (const auto& channel : clip.channels)
            if (channel.node == i) applyAnimationChannel(pose, channel, time);
        const auto& parent = node.parent >= 0 ? transforms.at(node.parent) : world;
        transforms.push_back(compose(parent, nodeTransform(pose)));
    }
    for (const auto& mesh : meshes) {
        const auto& transform = transforms.at(mesh.node);
        const auto normals = normalTransform(transform);
        for (size_t i = 0; i < mesh.positions.size(); ++i) {
            auto& vertex = vertices[mesh.firstVertex + i];
            vertex.position = point(transform, mesh.positions[i]);
            if (!mesh.normals.empty()) vertex.normal = direction(normals, mesh.normals[i]);
        }
    }
}

void poseCityCheckpoint(const CityCheckpoint& checkpoint, const std::vector<CityCheckpointMesh>& meshes,
                        double elapsedMs, CityVertex* vertices)
{
    if (!checkpoint.animation || meshes.empty()) return;
    const auto world = placementTransform(checkpoint.position, checkpoint.rotationDegrees,
                                          checkpoint.scale, checkpoint.rotationDegrees[2]);
    poseCityMarker(checkpoint.model, *checkpoint.animation, world, meshes, elapsedMs, vertices);
}

void poseCityVehicleObjective(const CityOpening& opening, const CityVehiclePose* vehicle,
                              const std::vector<CityCheckpointMesh>& meshes, double elapsedMs,
                              CityVertex* vertices)
{
    if (!opening.vehicleObjective || !opening.vehicleActor || meshes.empty()) return;
    const auto& actor = opening.actors.at(*opening.vehicleActor);
    const auto world = vehicle
        ? placementTransform(vehicle->position, actor.rotationDegrees, actor.scale, vehicle->yaw)
        : actorTransform(actor);
    const auto& highlight = *opening.vehicleObjective;
    poseCityMarker(highlight.model, highlight.animation, world, meshes, elapsedMs, vertices);
}

namespace {
std::optional<std::pair<int, int>> groundCell(float x, float y)
{
    constexpr float cellSize = 512;
    const auto column = std::floor(double(x) / cellSize);
    const auto row = std::floor(double(y) / cellSize);
    if (!(column > INT32_MIN && column < INT32_MAX && row > INT32_MIN && row < INT32_MAX))
        return std::nullopt;
    return std::pair{static_cast<int>(column), static_cast<int>(row)};
}
void indexTriangles(const std::vector<CityTriangle>& triangles,
                    std::map<std::pair<int, int>, std::vector<size_t>>& cells)
{
    cells.clear();
    for (size_t index = 0; index < triangles.size(); ++index) {
        const auto& [a, b, c] = triangles[index];
        const auto lo = groundCell(std::min({a[0], b[0], c[0]}), std::min({a[1], b[1], c[1]}));
        const auto hi = groundCell(std::max({a[0], b[0], c[0]}), std::max({a[1], b[1], c[1]}));
        if (!lo || !hi || int64_t(hi->first) - lo->first > 128 || int64_t(hi->second) - lo->second > 128) {
            cells.clear();
            return;
        }
        for (int x = lo->first; x <= hi->first; ++x)
            for (int y = lo->second; y <= hi->second; ++y)
                cells[{x, y}].push_back(index);
    }
}
}

void CityTerrain::indexGround(const CityNavigation* navigation)
{
    groundLinks.clear();
    sidewalkEdges.clear();
    roadEdges.clear();
    roadFlags.clear();
    if (navigation && !groundNavigation.empty()) {
        std::vector<size_t> bufferStarts;
        size_t first = 0;
        for (const auto& buffer : navigation->buffers) {
            bufferStarts.push_back(first);
            first += buffer.roadFlags.size();
        }
        if (first != ground.size() || groundNavigation.size() != ground.size())
            throw std::runtime_error("Retail navigation does not cover the ground index.");
        const auto groundIndex = [&](CityNavigation::TriangleRef reference) {
            return bufferStarts.at(reference.buffer) + reference.indexOffset / 3;
        };
        groundLinks.resize(ground.size());
        sidewalkEdges.resize(ground.size());
        roadEdges.resize(ground.size());
        roadFlags.resize(ground.size());
        const auto road = navigation->materialBuffers[static_cast<size_t>(CityNavigation::Material::road)];
        const auto intersection = navigation->materialBuffers[static_cast<size_t>(CityNavigation::Material::intersection)];
        const auto isRoad = [&](uint16_t buffer) { return buffer == road || buffer == intersection; };
        const auto sidewalk = navigation->materialBuffers[static_cast<size_t>(CityNavigation::Material::sidewalk)];
        for (size_t i = 0; i < groundNavigation.size(); ++i) {
            const auto source = groundNavigation[i];
            auto& links = groundLinks[i];
            const auto flags = navigation->buffers[source.buffer].roadFlags[source.indexOffset / 3];
            roadFlags[i] = flags;
            for (size_t edge = 0; edge < 3; ++edge) {
                if (const auto neighbor = navigation->neighbor(source, edge)) {
                    const auto index = groundIndex(*neighbor);
                    links.push_back(index);
                    if (neighbor->buffer == sidewalk) sidewalkEdges[i][edge] = index;
                    if (isRoad(source.buffer) && isRoad(neighbor->buffer) && cityRoadAllowsExit(flags, edge))
                        roadEdges[i][edge] = index;
                }
            }
            const auto& overlaps = navigation->buffers.at(source.buffer).overlaps.at(source.indexOffset / 3);
            for (const auto target : overlaps) links.push_back(groundIndex(target));
        }
    }
    indexTriangles(ground, groundCells);
}

std::optional<size_t> CityTerrain::pedestrianNeighbor(size_t triangle, std::optional<size_t> previous,
                                                     uint32_t draw, const Vec3* target) const
{
    if (triangle >= sidewalkEdges.size()) return std::nullopt;
    std::array<size_t, 3> candidates{};
    size_t count = 0;
    for (const auto neighbor : sidewalkEdges[triangle])
        if (neighbor && neighbor != previous) candidates[count++] = *neighbor;
    if (count && target) {
        const auto distance = [&](size_t candidate) {
            const auto& [a, b, c] = ground[candidate];
            const auto delta = add(scale(add(add(a, b), c), 1.f / 3), scale(*target, -1));
            return dot(delta, delta);
        };
        return *std::min_element(candidates.begin(), candidates.begin() + count,
                                 [&](size_t a, size_t b) { return distance(a) < distance(b); });
    }
    if (count) return candidates[draw % count];
    for (const auto neighbor : sidewalkEdges[triangle])
        if (neighbor) return neighbor;
    return std::nullopt;
}

namespace {
Vec3 groundTriangleCenter(const CityTriangle& triangle)
{
    const auto& [a, b, c] = triangle;
    return scale(add(add(a, b), c), 1.f / 3);
}

std::optional<Vec3> groundSharedEdge(const CityTriangle& source, const CityTriangle& target)
{
    Vec3 middle{};
    size_t shared = 0;
    for (const auto& vertex : source) {
        if (std::find(target.begin(), target.end(), vertex) == target.end()) continue;
        middle = add(middle, vertex);
        ++shared;
    }
    return shared == 2 ? std::optional{scale(middle, 0.5f)} : std::nullopt;
}
}

std::optional<size_t> CityTerrain::vehicleNeighbor(size_t triangle, std::optional<size_t> previous,
                                                  const Vec3& position, float yaw) const
{
    if (triangle >= roadEdges.size()) return std::nullopt;
    const Vec3 forward{std::sin(yaw * radians), -std::cos(yaw * radians), 0};
    std::optional<size_t> selected;
    float bestAlignment = -1.f;
    for (const auto neighbor : roadEdges[triangle]) {
        if (!neighbor || neighbor == previous) continue;
        const auto& target = ground[*neighbor];
        auto delta = add(groundSharedEdge(ground[triangle], target).value_or(groundTriangleCenter(target)),
                         scale(position, -1));
        if (std::hypot(delta[0], delta[1]) < 1.f)
            delta = add(groundTriangleCenter(target), scale(position, -1));
        const auto distance = std::hypot(delta[0], delta[1]);
        if (distance < 1.f) continue;
        const auto alignment = dot(forward, delta) / distance;
        if (alignment <= bestAlignment) continue;
        bestAlignment = alignment;
        selected = neighbor;
    }
    return selected;
}

std::optional<float> CityTerrain::vehicleRoadSpeed(size_t triangle, int32_t frameRate) const
{
    if (triangle >= roadFlags.size()) return std::nullopt;
    return cityRoadSpeedPerFrame(roadFlags[triangle]) * frameRate;
}

void CityTerrain::indexWalls()
{
    indexTriangles(walls, wallCells);
}

std::optional<std::vector<size_t>> CityTerrain::wallCandidates(float left, float bottom,
                                                               float right, float top) const
{
    if (wallCells.empty()) return std::nullopt;
    const auto lo = groundCell(left, bottom), hi = groundCell(right, top);
    if (!lo || !hi || int64_t(hi->first) - lo->first > 128 || int64_t(hi->second) - lo->second > 128)
        return std::nullopt;
    std::vector<size_t> candidates;
    for (int x = lo->first; x <= hi->first; ++x) {
        for (int y = lo->second; y <= hi->second; ++y) {
            const auto cell = wallCells.find({x, y});
            if (cell != wallCells.end())
                candidates.insert(candidates.end(), cell->second.begin(), cell->second.end());
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::optional<CityTerrain::RampContact> CityTerrain::rampContact(const Vec3& position, float rise, float drop) const
{
    std::optional<RampContact> found;
    for (size_t i = 0; i < jumpRamps.size(); ++i) {
        for (const auto& [a, b, c] : jumpRamps[i]) {
            const auto height = cityTriangleHeight(a, b, c, position);
            if (height && *height >= position[2] - drop && *height <= position[2] + rise &&
                (!found || *height > found->height))
                found = RampContact{i, *height};
        }
    }
    return found;
}

std::optional<CityTerrain::GroundContact> CityTerrain::groundContact(
    const Vec3& position, std::optional<size_t> preferred, float rise, float drop) const
{
    std::optional<GroundContact> found;
    float closest = std::max(rise, drop) + 1;
    const auto sample = [&](size_t index) {
        const auto& [a, b, c] = ground[index];
        const auto height = cityTriangleHeight(a, b, c, position);
        if (!height) return;
        const auto difference = *height - position[2];
        if (difference >= -drop && difference <= rise && std::abs(difference) < closest) {
            found = GroundContact{index, *height};
            closest = std::abs(difference);
        }
    };
    if (preferred && *preferred < ground.size()) {
        sample(*preferred);
        if (!groundLinks.empty())
            for (const auto target : groundLinks[*preferred]) sample(target);
    }
    if (groundCells.empty()) {
        for (size_t index = 0; index < ground.size(); ++index) sample(index);
    } else if (const auto cell = groundCell(position[0], position[1])) {
        const auto foundCell = groundCells.find(*cell);
        if (foundCell != groundCells.end())
            for (const auto index : foundCell->second) sample(index);
    }
    return found;
}

std::optional<float> CityTerrain::groundHeight(const Vec3& position, float rise, float drop) const
{
    const auto hit = groundContact(position, std::nullopt, rise, drop);
    return hit ? std::optional<float>{hit->height} : std::nullopt;
}

namespace {
float distanceToSegmentSquared(const Vec3& point, const Vec3& first, const Vec3& second)
{
    const auto edge = add(second, scale(first, -1));
    const auto offset = add(point, scale(first, -1));
    const float length = edge[0]*edge[0] + edge[1]*edge[1];
    const auto along = length > 0 ? std::clamp((offset[0]*edge[0] + offset[1]*edge[1])/length, 0.f, 1.f) : 0.f;
    const auto x = offset[0] - along*edge[0], y = offset[1] - along*edge[1];
    return x*x + y*y;
}

bool crossesWall(const CityTerrain& terrain, const Vec3& start, const Vec3& end,
                 float radius, Vec3* normalTravel = nullptr)
{
    const float elevation = std::max(start[2], end[2]) + 55;
    const auto left = std::min(start[0], end[0]) - radius, right = std::max(start[0], end[0]) + radius;
    const auto bottom = std::min(start[1], end[1]) - radius, top = std::max(start[1], end[1]) + radius;
    const auto side = [](const Vec3& a, const Vec3& b, const Vec3& point) {
        return (b[0]-a[0])*(point[1]-a[1]) - (b[1]-a[1])*(point[0]-a[0]);
    };
    const auto candidates = terrain.wallCandidates(left, bottom, right, top);
    const auto& walls = terrain.walls;
    const size_t count = candidates ? candidates->size() : walls.size();
    for (size_t i = 0; i < count; ++i) {
        const auto& wall = walls[candidates ? (*candidates)[i] : i];
        const auto& [a, b, c] = wall;
        if (right < std::min({a[0], b[0], c[0]}) || left > std::max({a[0], b[0], c[0]}) ||
            top < std::min({a[1], b[1], c[1]}) || bottom > std::max({a[1], b[1], c[1]}) ||
            elevation <= std::min({a[2], b[2], c[2]}) || elevation > std::max({a[2], b[2], c[2]})) continue;
        // Slice the face at body height so the bridge deck stays clear of obstacles below it.
        std::array<Vec3, 2> edge{};
        size_t intersections = 0;
        for (size_t i = 0; i < 3; ++i) {
            const auto& first = wall[i];
            const auto& second = wall[(i+1)%3];
            if ((first[2] < elevation) == (second[2] < elevation)) continue;
            const auto ratio = (elevation - first[2]) / (second[2] - first[2]);
            edge[intersections++] = add(first, scale(add(second, scale(first, -1)), ratio));
        }
        if (intersections != 2) continue;
        const auto [first, second] = edge;
        const float startSide = side(first, second, start);
        const float endSide = side(first, second, end);
        const bool crosses = startSide * endSide < 0 &&
            side(start, end, first)*side(start, end, second) <= 0;
        const auto before = distanceToSegmentSquared(start, first, second);
        const auto after = distanceToSegmentSquared(end, first, second);
        // Allow an existing overlap to move away from the face.
        if (!crosses && !(after < radius*radius && after + 0.01f < before)) continue;
        if (!normalTravel) return true;
        const Vec3 normal{first[1] - second[1], second[0] - first[0], 0};
        const auto lengthSquared = dot(normal, normal);
        *normalTravel = lengthSquared > 0 ? scale(normal, (endSide - startSide) / lengthSquared) : Vec3{};
        return true;
    }
    return false;
}

Vec3 carCircleCenter(const CityTerrain::VehicleFootprint& footprint, const Vec3& position,
                     float degrees, float offset)
{
    const auto angle = degrees * radians;
    const auto cosine = std::cos(angle), sine = std::sin(angle);
    return add(position, {sine * offset + cosine * footprint.lateralCenter,
                         -cosine * offset + sine * footprint.lateralCenter, 0});
}

bool crossesWallInCar(const CityTerrain& terrain, const CityTerrain::VehicleFootprint& footprint,
                      const Vec3& start, float startYaw, const Vec3& next, float yaw, Vec3& normalTravel)
{
    // Three overlapping circles approximate the body and bumpers.
    for (const auto offset : footprint.longitudinalCenters) {
        if (crossesWall(terrain, carCircleCenter(footprint, start, startYaw, offset),
                         carCircleCenter(footprint, next, yaw, offset), footprint.radius, &normalTravel)) return true;
    }
    return false;
}

bool crossesTraffic(const CityTerrain::VehicleFootprint& footprint, const Vec3& start,
                    float startYaw, const Vec3& next, float yaw, const std::vector<CityTrafficObstacle>& obstacles,
                    std::optional<size_t> ownSlot)
{
    for (const auto& obstacle : obstacles) {
        if (ownSlot && obstacle.spawnSlot == ownSlot) continue;
        if (std::abs(obstacle.position[2] - next[2]) >= 120.f &&
            std::abs(obstacle.position[2] - start[2]) >= 120.f) continue;
        const auto radius = footprint.radius + obstacle.footprint.radius;
        for (const auto offset : footprint.longitudinalCenters) {
            const auto first = carCircleCenter(footprint, start, startYaw, offset);
            const auto last = carCircleCenter(footprint, next, yaw, offset);
            for (const auto otherOffset : obstacle.footprint.longitudinalCenters) {
                const auto center = carCircleCenter(obstacle.footprint, obstacle.position, obstacle.yaw, otherOffset);
                const auto separation = add(first, scale(center, -1));
                const auto travel = add(last, scale(first, -1));
                const auto before = separation[0] * separation[0] + separation[1] * separation[1];
                if (before < radius * radius &&
                    separation[0] * travel[0] + separation[1] * travel[1] >= 0) continue;
                if (distanceToSegmentSquared(center, first, last) < radius * radius) return true;
            }
        }
    }
    return false;
}
}

bool cityPoliceHasContact(const CityTerrain& terrain, const Vec3& officer, const Vec3& player)
{
    const auto delta = add(player, scale(officer, -1));
    // Both retail police controllers use a 3,000 cm alert radius.
    return dot(delta, delta) <= 9000000.f && !crossesWall(terrain, officer, player, 0);
}

void CityPedestrianWalk::advance(const CityTerrain& terrain, CityPlayerPose& pose, float speed,
                                 uint32_t elapsedMs, const std::function<uint32_t()>& random, const Vec3* target)
{
    if (target && std::hypot((*target)[0] - pose.position[0], (*target)[1] - pose.position[1]) < 100.f) {
        pose.clipTimeMs = (pose.walking ? 0 : pose.clipTimeMs) + elapsedMs;
        pose.walking = false;
        return;
    }
    if (!destination && pose.groundTriangle) {
        destination = terrain.pedestrianNeighbor(*pose.groundTriangle, previous, random(), target);
        if (destination) {
            previous = pose.groundTriangle;
            sharedEdge = groundSharedEdge(terrain.ground.at(*previous), terrain.ground.at(*destination));
        }
    }
    const auto arrive = [&] {
        if (sharedEdge) sharedEdge.reset();
        else destination.reset();
    };
    bool moved = false;
    if (destination) {
        const auto center = sharedEdge.value_or(groundTriangleCenter(terrain.ground.at(*destination)));
        const auto dx = center[0] - pose.position[0], dy = center[1] - pose.position[1];
        const auto distance = std::hypot(dx, dy);
        if (distance <= 1.f) {
            arrive();
        } else {
            const auto step = std::min(distance, speed * elapsedMs * 0.001f);
            auto next = add(pose.position, {step * dx / distance, step * dy / distance, 0});
            const auto ground = terrain.groundContact(next, pose.groundTriangle);
            if (ground) next[2] = ground->height;
            if (ground && !crossesWall(terrain, pose.position, next, 28)) {
                pose.position = next;
                pose.groundTriangle = ground->triangle;
                pose.yaw = std::atan2(dx, -dy) / radians;
                moved = true;
                if (step == distance) arrive();
            } else {
                previous = destination;
                destination.reset();
                sharedEdge.reset();
            }
        }
    }
    pose.clipTimeMs = (pose.walking == moved ? pose.clipTimeMs : 0) + elapsedMs;
    pose.walking = moved;
}

void CityAmbientDrive::advance(const CityTerrain& terrain, CityPlayerPose& pose,
                               const CityTerrain::VehicleFootprint& footprint, int32_t frameRate,
                               uint32_t elapsedMs, const std::vector<CityTrafficObstacle>& obstacles,
                               std::optional<size_t> ownSlot, const Vec3* pursuitTarget)
{
    if (!pose.groundTriangle) return;
    if (!destination) {
        const float heading = pursuitTarget
            ? std::atan2((*pursuitTarget)[0] - pose.position[0], pose.position[1] - (*pursuitTarget)[1]) / radians
            : pose.yaw;
        destination = terrain.vehicleNeighbor(*pose.groundTriangle, previous, pose.position, heading);
        if (!destination) return;
        previous = pose.groundTriangle;
        sharedEdge = groundSharedEdge(terrain.ground.at(*previous), terrain.ground.at(*destination));
    }
    if (sharedEdge && std::hypot((*sharedEdge)[0] - pose.position[0], (*sharedEdge)[1] - pose.position[1]) <= 1.f)
        sharedEdge.reset();
    const auto target = sharedEdge.value_or(groundTriangleCenter(terrain.ground.at(*destination)));
    const auto dx = target[0] - pose.position[0], dy = target[1] - pose.position[1];
    const auto distance = std::hypot(dx, dy);
    if (distance <= 1.f) {
        pose.groundTriangle = destination;
        destination.reset();
        return;
    }
    const auto speed = terrain.vehicleRoadSpeed(*pose.groundTriangle, frameRate);
    if (!speed) return;
    const auto step = std::min(distance, *speed * elapsedMs * 0.001f);
    auto next = add(pose.position, {dx * step / distance, dy * step / distance, 0});
    const auto ground = terrain.groundContact(next, pose.groundTriangle, 70, 100);
    if (!ground || (ground->triangle != previous && ground->triangle != destination)) return;
    next[2] = ground->height;
    const auto yaw = std::atan2(dx, -dy) / radians;
    Vec3 normalTravel;
    if (crossesWallInCar(terrain, footprint, pose.position, pose.yaw, next, yaw, normalTravel) ||
        crossesTraffic(footprint, pose.position, pose.yaw, next, yaw, obstacles, ownSlot)) return;
    pose.position = next;
    pose.groundTriangle = ground->triangle;
    pose.yaw = yaw;
    if (step == distance) {
        if (sharedEdge) sharedEdge.reset();
        else {
            pose.groundTriangle = destination;
            destination.reset();
        }
    }
}

void CityPlayback::walk(const CityOpening& opening, const CityTerrain& terrain,
                        std::array<float, 2> input, double deltaMs)
{
    const auto& actor = openingPlayer(opening);
    if (!player) {
        player = CityPlayerPose{actor.position, actor.rotationDegrees[2]};
        if (const auto hit = terrain.groundContact(player->position)) {
            player->position[2] = hit->height;
            player->groundTriangle = hit->triangle;
        }
    }
    auto& pose = *player;
    const auto length = std::hypot(input[0], input[1]);
    const auto elapsed = std::clamp(deltaMs, 0., 50.);
    bool moving = false;
    if (length > 0) {
        const auto yaw = pose.followCameraYaw.value_or(actor.rotationDegrees[2]) * radians;
        const auto x = (std::cos(yaw) * input[0] + std::sin(yaw) * input[1]) / length;
        const auto y = (std::sin(yaw) * input[0] - std::cos(yaw) * input[1]) / length;
        const auto distance = float(elapsed * 0.001) * opening.walkSpeed * std::min(length, 1.f);
        const auto step = [&](float dx, float dy) {
            if ((!dx && !dy) || distance <= 0) return false;
            auto next = add(pose.position, {dx*distance, dy*distance, 0});
            const auto hit = terrain.groundContact(next, pose.groundTriangle);
            if (!hit) return false;
            next[2] = hit->height;
            if (crossesWall(terrain, pose.position, next, 28)) return false;
            pose.position = next;
            pose.groundTriangle = hit->triangle;
            pose.yaw = std::atan2(dx, -dy) / radians;
            return true;
        };
        moving = step(x, y) || step(x, 0) || step(0, y);
    }
    pose.clipTimeMs = (pose.walking == moving ? pose.clipTimeMs : 0) + elapsed;
    pose.walking = moving;
    if (timeAfterCheckpointMs) {
        *timeAfterCheckpointMs += deltaMs;
    } else if (opening.checkpoint && elapsedMs >= opening.durationMs) {
        const auto offset = add(pose.position, scale(opening.checkpoint->position, -1));
        if (std::sqrt(dot(offset, offset)) < opening.checkpoint->radius)
            timeAfterCheckpointMs = 0;
    }
}

bool CityPlayback::canEnterShop(const CityCheckpoint& entrance) const
{
    if (!player || playerHealth <= 0 || playerPunchMs || (vehicle && (vehicle->occupied || vehicle->doorAction)))
        return false;
    const auto offset = add(player->position, scale(entrance.position, -1));
    return std::sqrt(dot(offset, offset)) < entrance.radius;
}

bool CityPlayback::creditOpeningMission(size_t mission, int32_t reward)
{
    if (mission >= openingMissionsPaid.size() || openingMissionsPaid[mission] || reward < 0) return false;
    playerCash += std::min(reward, INT32_MAX - playerCash);
    openingMissionsPaid[mission] = true;
    return true;
}

bool CityPlayback::buyShopService(CityShopService service, uint32_t price)
{
    if (playerHealth <= 0 || playerCash < 0 || price > static_cast<uint32_t>(playerCash)) return false;
    if (service == CityShopService::vest)
        playerArmor = playerMaxArmor;
    else
        playerHealth = playerMaxHealth;
    playerCash -= static_cast<int32_t>(price);
    return true;
}

void CityPlayback::damagePlayer(int32_t amount, bool bypassArmor)
{
    if (amount <= 0) return;
    if (!bypassArmor && playerArmor > 0) {
        const auto absorbed = std::min(amount, playerArmor);
        playerArmor -= absorbed;
        amount -= absorbed;
    }
    playerHealth -= std::min(amount, playerHealth);
}

bool CityPlayback::spawnVehicle(const CityActor& actor, const CityTerrain& terrain,
                                std::array<float, 3> position, float yaw)
{
    const auto hit = terrain.groundContact(position, std::nullopt, 120, 120);
    if (!hit) return false;
    position[2] = hit->height;
    vehicle = CityVehiclePose{position, yaw};
    vehicle->groundTriangle = hit->triangle;
    vehicle->health = actor.vehicleHealth.value_or(0);
    vehicle->maxHealth = actor.vehicleMaxHealth;
    vehicle->damageEnabled = actor.vehicleDamageEnabled;
    vehicle->collisionDamageScale = actor.collisionDamageScale;
    return true;
}

CityDoorSounds CityPlayback::drive(const CityOpening& opening, const CityTerrain& terrain,
                                  std::array<float, 2> input, double deltaMs, bool interact)
{
    if (!opening.vehicleActor || !opening.checkpoint || !player ||
        (nextMissionVehicleActive && openingMissionsPaid[1] && !postLcVehicleActive)) return {};
    const auto& actor = opening.actors[*opening.vehicleActor];
    if (!vehicle) {
        auto location = actor.position;
        if (!nextMissionVehicleActive) location[2] = opening.checkpoint->position[2];
        if (!spawnVehicle(actor, terrain, location, actor.rotationDegrees[2])) return {};
    }
    if (!nextMissionVehicleActive &&
        (!timeAfterCheckpointMs || *timeAfterCheckpointMs < opening.checkpoint->nextTutorial.startMs)) return {};
    auto& car = *vehicle;
    const auto cameraOffsets = actor.vehicleCamera.value_or(defaultVehicleCamera);
    if (car.exploded) return {};
    CityDoorSounds sounds;
    if (car.doorAction) {
        car.doorAction->elapsedMs += deltaMs;
        if (!vehicleDoorFrame(actor, car, opening).first) {
            const auto& door = *car.doorAction;
            sounds.closed = door.exiting || doorClip(actor, door.side, "car_close_door");
            car.doorAction.reset();
        }
        if (car.doorAction && opening.vehicleCharacterClips) return sounds;
    }
    const auto startDoorAction = [&](size_t side, bool exiting) {
        if (!doorClip(actor, side, exiting ? "car_exit_car" : "car_open_door")) return;
        car.doorAction = CityVehiclePose::DoorAction{side, exiting, 0};
        sounds.opening = true;
    };
    const auto beginCameraTransition = [&](const std::optional<CityCameraPose>& from, double durationMs) {
        if (from)
            cameraTransition = CityCameraTransition{
                *from, followCamera(opening, &*player, &car), elapsedMs, durationMs};
    };
    const auto currentCamera = [&]() -> std::optional<CityCameraPose> {
        if (!opening.followCameraMs || elapsedMs < *opening.followCameraMs) return std::nullopt;
        return cameraTransition && cameraTransition->active(elapsedMs) ? cameraTransition->sample(elapsedMs) :
            followCamera(opening, &*player, &car);
    };
    if (interact && !car.occupied) {
        const auto distance = add(player->position, scale(car.position, -1));
        if (std::hypot(distance[0], distance[1]) <= 350 && std::abs(distance[2]) <= 180) {
            const auto from = currentCamera();
            car.occupied = true;
            lastCameraTapMs.reset();
            enteredVehicle = true;
            const auto yaw = car.yaw * radians;
            const auto side = distance[0] * std::cos(yaw) + distance[1] * std::sin(yaw) >= 0 ? 0u : 1u;
            car.entrySide = side;
            car.seatedMs = 0;
            car.updateCamera(cameraOffsets, 0);
            // CControlManager::enterCar requests a 0.1 second transition.
            beginCameraTransition(from, 100);
            startDoorAction(side, false);
        }
        return sounds;
    }
    if (!car.occupied) return sounds;
    car.seatedMs += deltaMs;
    if (!car.airborne && (scriptedVehicleExit || (interact && std::abs(car.speed) < 90))) {
        const auto angle = car.yaw * radians;
        for (const float side : {-210.f, 210.f}) {
            auto exit = add(car.position, {std::cos(angle) * side, std::sin(angle) * side, 0});
            if (const auto hit = terrain.groundContact(exit, car.groundTriangle)) {
                exit[2] = hit->height;
                if (crossesWall(terrain, car.position, exit, 28)) continue;
                const auto from = currentCamera();
                *player = CityPlayerPose{exit, car.yaw};
                player->groundTriangle = hit->triangle;
                player->followCameraYaw = car.yaw;
                car.occupied = false;
                scriptedVehicleExit = false;
                lastCameraTapMs.reset();
                car.speed = 0;
                car.gear = 1;
                car.updateLights(false);
                car.camera.reset();
                // CControlManager::exitCar requests a 0.3 second transition.
                beginCameraTransition(from, 300);
                startDoorAction(side > 0 ? 0u : 1u, true);
                return sounds;
            }
        }
    }
    car.updateLights(input[1] < 0, deltaMs);
    const auto seconds = static_cast<float>(std::clamp(deltaMs, 0., 50.) * 0.001);
    car.steering = input[0] * 25.f;
    const auto coast = std::exp(-3.2f * seconds);
    if (input[1] == 0) {
        car.speed *= coast;
    } else if (input[1] > 0 && car.speed >= 0 && actor.drive && actor.gearProfile) {
        const auto acceleration = forwardAcceleration(*actor.drive, *actor.gearProfile, car.gear, car.speed);
        // Vehicle::UpdatePhysic reduces the force cutoff at or below half health.
        auto topSpeed = actor.drive->topSpeedKmh * (1000.f / 36.f);
        if (car.maxHealth > 0) {
            const auto healthRatio = static_cast<float>(car.health) / car.maxHealth;
            if (healthRatio <= 0.5f) topSpeed *= 2.f * std::max(healthRatio, 0.15f);
        }
        // Let the chassis coast to a newly lowered limit after a damaging hit.
        car.speed = car.speed > topSpeed ? std::max(topSpeed, car.speed * coast)
            : std::min(car.speed + input[1] * acceleration * seconds, topSpeed);
    } else {
        const bool braking = input[1] * car.speed < 0;
        // Vehicle::Update applies the reverse force against existing motion for opposite input.
        const float acceleration = actor.drive && actor.drive->reverseForce > 0
            ? 200.f * actor.drive->reverseForce / actor.drive->massKg : 520.f;
        const float nextSpeed = car.speed + input[1] * acceleration * seconds;
        if (actor.drive && braking)
            car.speed = car.speed > 0 ? std::max(0.f, nextSpeed) : std::min(0.f, nextSpeed);
        else
            car.speed = std::clamp(nextSpeed, -350.f, std::max(car.speed, 1050.f));
    }
    const auto yaw = car.yaw + input[0] * std::clamp(car.speed / 320, -1.f, 1.f) * 72 * seconds;
    const float angle = yaw * radians;
    const auto distance = car.speed * seconds;
    auto next = add(car.position, {std::sin(angle) * distance, -std::cos(angle) * distance, 0});
    auto hit = terrain.groundContact(next, car.groundTriangle, 70, 100);
    auto ramp = car.airborne ? std::nullopt : terrain.rampContact(next, 70, 100);
    if (ramp && hit && ramp->height < hit->height) ramp.reset();
    float verticalSpeed = 0;
    bool airborne = false;
    if (ramp) {
        next[2] = ramp->height;
        if (seconds > 0) verticalSpeed = (next[2] - car.position[2]) / seconds;
    } else if (car.airborne || (car.onRamp && (car.verticalSpeed > 0 || !hit))) {
        constexpr float gravity = 980.f;
        verticalSpeed = car.verticalSpeed - gravity * seconds;
        next[2] += (car.verticalSpeed + verticalSpeed) * 0.5f * seconds;
        const auto descent = car.position[2] - next[2];
        hit = descent >= 0 ? terrain.groundContact({next[0], next[1], car.position[2]},
                                                   car.groundTriangle, 0, descent) : std::nullopt;
        airborne = !hit;
    }
    if (ramp || hit || airborne) {
        if (!ramp && hit) {
            next[2] = hit->height;
            verticalSpeed = 0;
        }
        Vec3 normalTravel{};
        if (!crossesWallInCar(terrain, terrain.vehicleFootprint, car.position, car.yaw, next, yaw, normalTravel)) {
            const auto travel = add(next, scale(car.position, -1));
            const auto traveledCm = static_cast<int32_t>(std::sqrt(dot(travel, travel)));
            drivenCentimeters += traveledCm;
            if (drivenCentimeters >= 100000) {
                ++drivenKilometers;
                drivenCentimeters = 0;
            }
            highestVehicleSpeedKmh = std::max(highestVehicleSpeedKmh, vehicleSpeedKmh(car.speed));
            if (airborne) {
                car.jumpDistanceCm += traveledCm;
            } else if (car.airborne) {
                longestJumpMeters = std::max(longestJumpMeters, car.jumpDistanceCm / 100);
                car.jumpDistanceCm = 0;
            }
            car.position = next;
            car.groundTriangle = !ramp && hit ? std::optional<size_t>{hit->triangle} : std::nullopt;
            car.verticalSpeed = verticalSpeed;
            if (ramp && ramp->ramp < 32) visitedJumpRamps |= uint32_t{1} << ramp->ramp;
            car.onRamp = ramp.has_value();
            car.airborne = airborne;
            car.yaw = std::remainder(yaw, 360.f);
            car.wheelTravel += distance;
            if (actor.gearProfile) car.shiftGears(*actor.gearProfile);
            if (!cameraTransition || !cameraTransition->active(elapsedMs))
                car.updateCamera(cameraOffsets, deltaMs);
            return sounds;
        }
        if (seconds > 0) car.applyWallImpact(scale(normalTravel, 0.01f / seconds));
    }
    car.speed = 0;
    if (actor.gearProfile) car.shiftGears(*actor.gearProfile);
    if (!cameraTransition || !cameraTransition->active(elapsedMs))
        car.updateCamera(cameraOffsets, deltaMs);
    return sounds;
}

void CityVehiclePose::shiftGears(unsigned profile)
{
    if (speed < -0.001f) {
        gear = -1;
        return;
    }
    if (gear == -1) gear = 1;
    const auto& settings = gearProfiles.at(profile);
    const auto kmh = vehicleSpeedKmh(speed);
    if (gear < settings.gearCount && kmh >= settings.shiftSpeeds.at(gear - 1)) ++gear;
    else if (gear > 1 && kmh < settings.shiftSpeeds.at(gear - 2)) --gear;
}

int CityVehiclePose::v6EngineRpm(unsigned profile, float speedRange) const
{
    constexpr int idle = 1000, maximum = 5000;
    const auto kmh = vehicleSpeedKmh(speed);
    if (kmh <= 1 || speedRange <= 0) return idle;
    const auto& settings = gearProfiles.at(profile);
    const auto& thresholds = settings.shiftSpeeds;
    const auto current = std::clamp(gear, 1, settings.gearCount);
    const float start = current > 1 ? thresholds.at(current - 2) : 0;
    const float span = current < settings.gearCount ? thresholds.at(current - 1) - start
        : start - thresholds.at(current - 3);
    // _adjustEnginePitch subtracts the untraversed part of the current gear's speed interval.
    const float fraction = std::clamp((float(kmh) - start) / span, 0.f, 1.f);
    const float equivalent = std::max(0.f, float(kmh) - (1 - fraction) * span);
    return idle + static_cast<int>(equivalent / speedRange * (maximum - idle));
}

void CityVehiclePose::updateLights(bool braking, double deltaMs)
{
    // Vehicle::UpdatePhysic selects one pair using the cached result of getVelDirec.
    // With the brake held at rest or the rear deformed, retail retains the selected pair.
    constexpr float movingThreshold = 0.001f; // Retail 0.00001 m/s, converted to cm/s.
    if (!braking || exploded) {
        lightEffects = {};
        lightTimeMs = 0;
    } else if (static_cast<double>(rearDeformation) < 0.35 && std::abs(speed) > movingThreshold) {
        lightEffects = {speed > 0, speed < 0};
        // Retail jumps the selected timeline to its start and enables playback.
        lightTimeMs = 0;
    } else if (lightEffects[0] || lightEffects[1]) {
        lightTimeMs += std::max(0., deltaMs);
    }
}

void CityVehiclePose::updateCamera(const CityVehicleCamera& offsets, double deltaMs)
{
    if (!occupied) {
        camera.reset();
        return;
    }
    const auto angle = yaw * radians;
    const Vec3 forward{std::sin(angle), -std::cos(angle), 0};
    const Vec3 right{std::cos(angle), std::sin(angle), 0};
    const float speedKmh = std::abs(speed) * 0.036f;
    if (!camera) {
        camera = CameraState{
            add(add(position, scale(forward, -offsets.setback)), {0, 0, offsets.eyeHeight}),
            offsets.setback, speedKmh, offsets.eyeHeight};
        return;
    }

    // VehicleCamera::update uses the previous eye, projected onto the vehicle plane.
    auto rear = add(camera->eye, scale(position, -1));
    rear[2] = 0;
    const auto length = std::hypot(rear[0], rear[1]);
    rear = length > 0.001f ? scale(rear, 1 / length) : scale(forward, -1);
    const float alignment = -dot(rear, forward);
    const float seconds = static_cast<float>(std::max(deltaMs, 0.) * 0.001);
    const float speedDelta = (speedKmh - camera->previousSpeed) / 1000;
    if (alignment > -1 && static_cast<double>(alignment) < -0.9)
        camera->setback += offsets.setback * speedDelta * seconds;
    if (static_cast<double>(alignment) > 0.9 && alignment < 1)
        camera->setback -= offsets.minimumSetback * speedDelta * seconds;
    camera->setback = std::clamp(camera->setback, offsets.minimumSetback, offsets.setback);
    camera->previousSpeed = speedKmh;

    if ((speed > 0 && alignment > 0.5f) || (speed < 0 && alignment < -0.5f)) {
        const float sign = dot(rear, right) > 0 ? 1.f : -1.f;
        const float turn = std::copysign(speedKmh, speed) * 1.5f * seconds * sign * radians;
        const float cosine = std::cos(turn), sine = std::sin(turn);
        rear = {rear[0] * cosine - rear[1] * sine, rear[0] * sine + rear[1] * cosine, 0};
    }

    camera->eye = add(add(position, scale(rear, camera->setback)), {0, 0, camera->eyeHeight});
}

void CityVehiclePose::applyDamage(int32_t amount)
{
    if (!damageEnabled && !occupied) return;
    health = static_cast<int32_t>(std::clamp(int64_t(health) - amount,
        int64_t(std::numeric_limits<int32_t>::min()), int64_t(std::numeric_limits<int32_t>::max())));
}

void CityVehiclePose::applyWallImpact(const Vec3& normalVelocity)
{
    // ContactListener::Add passes the projected contact velocity to Vehicle::collision.
    const float impact = std::sqrt(dot(normalVelocity, normalVelocity)) / 1000 * 3600;
    if (impact <= 10) return;
    const float damage = (impact / 100) * collisionDamageScale;
    const auto healthLoss = static_cast<int32_t>(damage * 90);
    health -= healthLoss;
    // Vehicle::collision forwards the applied hit to recordDamage for the player car.
    if (occupied) peakCollisionDamage = std::max(peakCollisionDamage, healthLoss);
    const auto angle = yaw * radians;
    const Vec3 front{std::sin(angle), -std::cos(angle), 0};
    auto& deformation = dot(normalVelocity, front) < 0 ? rearDeformation : frontDeformation;
    deformation = std::min(1.f, deformation + damage);
}

void CityVehiclePose::updateExplosion(double deltaMs)
{
    if (health > 0 || exploded) return;
    // Vehicle::Update calls explode once the dead vehicle's timer reaches 5000 ms.
    destroyedMs += std::max(0., deltaMs);
    if (destroyedMs < 5000) return;
    exploded = true;
    speed = 0;
    steering = 0;
}

CityVehiclePose::DamageEffect CityVehiclePose::damageEffect() const
{
    if (exploded || maxHealth <= 0) return DamageEffect::none;
    if (health <= 0) return DamageEffect::fire;
    // Vehicle::Update compares the float health ratio against a double 0.3.
    const float ratio = static_cast<float>(health) / static_cast<float>(maxHealth);
    return static_cast<double>(ratio) < 0.3 ? DamageEffect::smoke : DamageEffect::none;
}

void CityCameraShake::update(double deltaMs, bool explosion, bool suspended)
{
    // Explosion::drawExplosion starts the timer. CameraManager pauses it during camera transitions.
    if (explosion) remainingMs = 1000;
    offset = {};
    if (suspended || remainingMs <= 0) return;
    remainingMs = std::max(0.f, remainingMs - static_cast<float>(std::max(deltaMs, 0.)));
    for (auto& axis : offset) {
        // Use the lrand48 recurrence with a camera-local stream on Windows.
        randomState = (randomState * 0x5deece66dULL + 0xb) & 0xffffffffffffULL;
        const auto step = static_cast<float>((randomState >> 17) % 100);
        axis = (remainingMs / 1000) * step;
    }
}

void CityPlayback::updateMission(const CityOpening& opening, double deltaMs)
{
    const bool alreadyExploded = vehicle && vehicle->exploded;
    if (vehicle) vehicle->updateExplosion(deltaMs);
    const bool followCameraActive = opening.followCameraMs &&
        elapsedMs >= double(*opening.followCameraMs) + opening.followCameraBlendMs;
    const bool cameraInTransition = cameraTransition && cameraTransition->active(elapsedMs);
    cameraShake.update(deltaMs, vehicle && vehicle->exploded && !alreadyExploded,
                       !followCameraActive || cameraInTransition);
    if (missionFailure || !opening.vehicleActor) return;
    const auto& actor = opening.actors.at(*opening.vehicleActor);
    if (!nextMissionVehicleActive) {
        if (!opening.vehicleProtection || elapsedMs < opening.vehicleProtection->startMs) return;
        const auto& protection = *opening.vehicleProtection;
        if (!protectionRegistered && !(payphoneActive && opening.payphone && opening.payphone->clearConditionsOnDeparture)) {
            missionConditions.add(RetailProtectionTarget::vehicle, protection.target, protection.failureCause);
            protectionRegistered = true;
        }
    }
    const auto lookup = [&](std::string_view name) -> std::optional<RetailMissionTargetState> {
        const auto npc = locoOpponents.find(name);
        if (npc != locoOpponents.end())
            return RetailMissionTargetState{npc->second.pose.position, npc->second.health <= 0};
        if (name != actor.name) return std::nullopt;
        return RetailMissionTargetState{vehicle ? vehicle->position : actor.position,
            (vehicle ? vehicle->health : actor.vehicleHealth.value_or(0)) <= 0, vehicle && vehicle->exploded};
    };
    const auto result = missionConditions.update(static_cast<int32_t>(std::clamp(deltaMs, 0., 1000.)),
                                                  activeDialogue.has_value(),
                                                  player ? player->position : actor.position, lookup);
    missionFailure = result.failureCause;
    if (missionFailure) ++missionFailureCount;
}

static std::vector<std::optional<float>> cityVehicleWheelContacts(const CityActor& actor, const CityVehiclePose& vehicle,
                                                                 const std::vector<CityWheelRange>& wheels,
                                                                 const CityTerrain& terrain)
{
    const auto angle = (vehicle.yaw - actor.rotationDegrees[2]) * radians;
    const auto s = std::sin(angle), c = std::cos(angle);
    std::vector<std::optional<float>> heights;
    heights.reserve(wheels.size());
    for (const auto& wheel : wheels) {
        const auto rest = add(wheel.pivot, scale(actor.position, -1));
        const Vec3 ground{vehicle.position[0] + c*rest[0] - s*rest[1],
                          vehicle.position[1] + s*rest[0] + c*rest[1], vehicle.position[2]};
        if (vehicle.airborne) {
            heights.push_back(std::nullopt);
            continue;
        }
        const auto hit = terrain.groundContact(ground, vehicle.groundTriangle, wheel.radius, wheel.radius);
        const auto ramp = terrain.rampContact(ground, wheel.radius, wheel.radius);
        if (ramp && (!hit || ramp->height >= hit->height))
            heights.push_back(ramp->height - ground[2]);
        else
            heights.push_back(hit ? std::optional<float>{hit->height - ground[2]} : std::nullopt);
    }
    return heights;
}

std::vector<float> cityVehicleWheelOffsets(const CityActor& actor, const CityVehiclePose& vehicle,
                                          const std::vector<CityWheelRange>& wheels, const CityTerrain& terrain)
{
    std::vector<float> heights;
    heights.reserve(wheels.size());
    if (vehicle.suspension.empty()) {
        for (const auto height : cityVehicleWheelContacts(actor, vehicle, wheels, terrain))
            heights.push_back(height.value_or(0));
    } else {
        for (const auto& wheel : vehicle.suspension) heights.push_back(wheel.height);
    }
    return heights;
}

void updateCityVehicleSuspension(const CityActor& actor, CityVehiclePose& vehicle,
                                  const std::vector<CityWheelRange>& wheels, const CityTerrain& terrain,
                                  double deltaMs)
{
    if (!actor.suspension || wheels.empty() || vehicle.exploded) return;
    const auto& settings = *actor.suspension;
    const auto ground = cityVehicleWheelContacts(actor, vehicle, wheels, terrain);
    if (wheels.size() == 4 && std::all_of(ground.begin(), ground.end(),
            [](const auto& height) { return height.has_value(); })) {
        // Vehicle::stepPhysic uses the differences of the front/rear and left/right wheel midpoints.
        Vec3 longitudinal{}, lateral{};
        const auto restYaw = actor.rotationDegrees[2] * radians;
        const auto c = std::cos(restYaw), s = std::sin(restYaw);
        for (size_t i = 0; i < wheels.size(); ++i) {
            const auto& wheel = wheels[i];
            const auto offset = add(wheel.pivot, scale(actor.position, -1));
            const Vec3 contact{c*offset[0] + s*offset[1], -s*offset[0] + c*offset[1], *ground[i]};
            longitudinal = add(longitudinal, scale(contact, wheel.steers ? -0.5f : 0.5f));
            lateral = add(lateral, scale(contact, wheel.left ? 0.5f : -0.5f));
        }
        const auto plane = cross(lateral, longitudinal);
        if (std::abs(plane[2]) > 0.001f)
            vehicle.chassisTilt = {-std::atan(plane[1] / plane[2]) / radians,
                                    std::atan(plane[0] / plane[2]) / radians};
    }
    vehicle.suspension.resize(wheels.size());
    const float dt = static_cast<float>(std::clamp(deltaMs, 0., 50.) * 0.001);
    for (size_t i = 0; i < wheels.size(); ++i) {
        auto& wheel = vehicle.suspension[i];
        // Vehicle::UpdatePhysic integrates velocity before position and applies the ground delta afterward.
        const float acceleration = (-settings.springRate * wheel.height) / settings.wheelMass
                                   - settings.damping * wheel.speed - 9.8f;
        wheel.speed += dt * acceleration;
        wheel.height += dt * wheel.speed;
        const float height = ground[i].value_or(wheel.lastGround);
        wheel.height = std::clamp(wheel.height - (height - wheel.lastGround),
                                  -settings.lowerTravel, settings.upperTravel);
        wheel.lastGround = height;
    }
}

void poseCityVehicle(const CityActor& actor, const CityVehiclePose& vehicle,
                     const std::vector<CityVertex>& source, const std::vector<CityWheelRange>& wheels,
                     const std::vector<CityVehicleMorphRange>& morphs, CityVertex* target,
                     const std::vector<float>& wheelOffsets)
{
    const auto angle = (vehicle.yaw - actor.rotationDegrees[2]) * radians;
    const auto s = std::sin(angle), c = std::cos(angle);
    for (size_t i = 0; i < source.size(); ++i) {
        const auto local = add(source[i].position, scale(actor.position, -1));
        target[i] = source[i];
        target[i].position = add(vehicle.position, {c*local[0] - s*local[1], s*local[0] + c*local[1], local[2]});
        const auto& normal = source[i].normal;
        target[i].normal = {c*normal[0] - s*normal[1], s*normal[0] + c*normal[1], normal[2]};
    }
    for (const auto& range : morphs) {
        const auto& mesh = actor.model.meshes.at(range.mesh);
        const auto& morph = actor.model.morphControllers.at(range.controller);
        auto live = range.bindTransform;
        const auto rotate = [&](const Vec3& v) -> Vec3 { return {c*v[0] - s*v[1], s*v[0] + c*v[1], v[2]}; };
        for (size_t axis = 0; axis < 3; ++axis) live[axis] = rotate(live[axis]);
        live[3] = add(vehicle.position, rotate(add(live[3], scale(actor.position, -1))));
        const auto normals = normalTransform(live);
        for (size_t i = 0; i < mesh.positions.size(); ++i) {
            auto position = mesh.positions[i];
            auto normal = mesh.normals.empty() ? Vec3{} : mesh.normals[i];
            morphVehicleVertex(actor.model, morph, vehicle, i, position, normal);
            auto& vertex = target[range.firstVertex + i];
            vertex.position = point(live, position);
            vertex.normal = direction(normals, normal);
        }
    }
    const auto restYaw = actor.rotationDegrees[2] * radians;
    const auto rs = std::sin(restYaw), rc = std::cos(restYaw);
    const auto newYaw = vehicle.yaw * radians;
    const auto ns = std::sin(newYaw), nc = std::cos(newYaw);
    for (size_t wheelIndex = 0; wheelIndex < wheels.size(); ++wheelIndex) {
        const auto& wheel = wheels[wheelIndex];
        const auto turn = wheel.steers ? vehicle.steering * radians : 0;
        const auto ts = std::sin(turn), tc = std::cos(turn);
        const auto roll = static_cast<float>(std::remainder(vehicle.wheelTravel / wheel.radius, 6.283185307179586));
        const auto ws = std::sin(roll), wc = std::cos(roll);
        const auto offset = add(wheel.pivot, scale(actor.position, -1));
        auto pivot = add(vehicle.position, {c*offset[0] - s*offset[1], s*offset[0] + c*offset[1], offset[2]});
        if (!wheelOffsets.empty()) pivot[2] += wheelOffsets.at(wheelIndex);
        for (size_t i = wheel.firstVertex; i < wheel.firstVertex + wheel.vertexCount; ++i) {
            const auto rotate = [&](const Vec3& delta) -> Vec3 {
                const float x = rc*delta[0] + rs*delta[1], y = -rs*delta[0] + rc*delta[1];
                const float rolledY = wc*y - ws*delta[2], rolledZ = ws*y + wc*delta[2];
                const float steeredX = tc*x - ts*rolledY, steeredY = ts*x + tc*rolledY;
                return {nc*steeredX - ns*steeredY, ns*steeredX + nc*steeredY, rolledZ};
            };
            target[i].position = add(pivot, rotate(add(source[i].position, scale(wheel.pivot, -1))));
            target[i].normal = rotate(source[i].normal);
        }
    }
    if (vehicle.chassisTilt != std::array<float, 2>{}) {
        const auto nominal = placementTransform({}, actor.rotationDegrees, {1, 1, 1}, vehicle.yaw);
        auto angles = actor.rotationDegrees;
        angles[0] += vehicle.chassisTilt[0];
        angles[1] += vehicle.chassisTilt[1];
        const auto tilted = placementTransform({}, angles, {1, 1, 1}, vehicle.yaw);
        const auto transform = compose(tilted, *inverseTransform(nominal));
        for (size_t i = 0; i < source.size(); ++i) {
            auto& vertex = target[i];
            vertex.position = add(vehicle.position, direction(transform, add(vertex.position, scale(vehicle.position, -1))));
            vertex.normal = direction(transform, vertex.normal);
        }
    }
}

void CityPlayback::orbitCamera(const CityOpening& opening, std::array<float, 2> drag, double deltaMs)
{
    if (drag == std::array<float, 2>{} || deltaMs <= 0 || missionFailure || !player || !opening.followCameraMs ||
        elapsedMs < double(*opening.followCameraMs) + opening.followCameraBlendMs ||
        (cameraTransition && cameraTransition->active(elapsedMs))) return;

    // Both retail updateAngles functions use 60 degrees/second over 480 pixels.
    // CameraManager doubles horizontal input on foot.
    const auto seconds = static_cast<float>(deltaMs * 0.001);
    const float yawDegrees = drag[0] * (60.f / 480) * seconds;
    const float polarDelta = drag[1] * (-60.f / 320) * seconds;
    if (vehicle && vehicle->occupied) {
        if (!vehicle->camera) return;
        auto& camera = *vehicle->camera;
        const auto relative = add(camera.eye, scale(vehicle->position, -1));
        const auto horizontal = std::hypot(relative[0], relative[1]);
        const auto heading = horizontal > 0.001f ? std::atan2(-relative[0], relative[1]) :
            vehicle->yaw * radians;
        const auto azimuth = heading + yawDegrees * radians;
        const auto orbitRadius = std::hypot(horizontal, relative[2]);
        const auto polar = std::atan2(horizontal, relative[2]) + polarDelta * radians;
        const auto distance = orbitRadius * std::sin(polar);
        camera.eyeHeight = orbitRadius * std::cos(polar);
        camera.eye = add(vehicle->position,
                         {-distance * std::sin(azimuth), distance * std::cos(azimuth), camera.eyeHeight});
        return;
    }

    const auto yaw = player->followCameraYaw.value_or(openingPlayer(opening).rotationDegrees[2]);
    player->followCameraYaw = std::remainder(yaw + 2 * yawDegrees, 360.f);
    const auto distance = player->followCameraDistance;
    const auto height = player->followCameraHeight;
    const float polar = std::atan2(distance, height) / radians + polarDelta;
    // FollowCamera::updatePosition checks the direction of motion outside 40–45 degrees.
    if (polarDelta != 0 && (polar >= 45 || polarDelta > 0) && (polar <= 40 || polarDelta < 0)) {
        const auto radius = std::hypot(distance, height);
        player->followCameraDistance = radius * std::sin(polar * radians);
        player->followCameraHeight = radius * std::cos(polar * radians);
    }
}

void CityPlayback::releaseCameraTap(const CityOpening& opening, uint64_t releasedMs)
{
    if (!vehicle || !vehicle->occupied || !opening.vehicleActor || missionFailure ||
        !opening.followCameraMs || elapsedMs < double(*opening.followCameraMs) + opening.followCameraBlendMs) {
        lastCameraTapMs.reset();
        return;
    }

    const bool doubleTap = lastCameraTapMs && releasedMs >= *lastCameraTapMs &&
        releasedMs - *lastCameraTapMs < 500;
    lastCameraTapMs = releasedMs;
    if (doubleTap) recenterVehicleCamera(opening);
}

void CityPlayback::recenterVehicleCamera(const CityOpening& opening)
{
    if (!vehicle || !vehicle->occupied || !opening.vehicleActor || missionFailure ||
        !opening.followCameraMs || elapsedMs < double(*opening.followCameraMs) + opening.followCameraBlendMs ||
        vehicle->exploded || vehicle->airborne || std::abs(vehicle->speed) > 0.001f ||
        (cameraTransition && cameraTransition->active(elapsedMs))) return;

    const auto from = followCamera(opening, player ? &*player : nullptr, &*vehicle);
    const auto offsets = opening.actors.at(*opening.vehicleActor).vehicleCamera.value_or(defaultVehicleCamera);
    vehicle->camera.reset();
    vehicle->updateCamera(offsets, 0);
    // A stationary double tap re-enters VehicleCamera with a 0.3 second handoff.
    cameraTransition = CityCameraTransition{
        from, followCamera(opening, player ? &*player : nullptr, &*vehicle), elapsedMs, 300};
}

void CityPlayback::advancePayphone(const CityOpening& opening, double deltaMs)
{
    if (!opening.payphone || !enteredVehicle || !player || payphoneReached) return;
    const auto& route = *opening.payphone;
    timeAfterEntryMs += deltaMs;
    const auto& position = vehicle && vehicle->occupied ? vehicle->position : player->position;
    // WaitEscape compares the three-dimensional distance against the authored threshold.
    if (!payphoneActive && timeAfterEntryMs >= route.departureDelayMs &&
        retailEscapeReached({position, route.departure}, route.departureRadius)) {
        if (route.clearConditionsOnDeparture) missionConditions.clear();
        payphoneActive = true;
    }
    if (payphoneActive) {
        const auto offset = add(position, scale(route.destination.position, -1));
        payphoneReached = std::sqrt(dot(offset, offset)) < route.destination.radius;
        if (payphoneReached) scriptedVehicleExit = vehicle && vehicle->occupied;
    }
}

static void pursueMeleeTarget(CityPlayback& playback, const CityOpening& opening, const CityTerrain& terrain,
                              const CityActor* actor, CityPlayerPose& pose, std::optional<double>& punchMs,
                              size_t& punchIndex, double deltaMs)
{
    const auto& route = *opening.payphone;
    if (playback.playerHealth == 0 && !punchMs) return;
    const auto& target = playback.vehicle && playback.vehicle->occupied ? playback.vehicle->position : playback.player->position;
    const auto dx = target[0] - pose.position[0], dy = target[1] - pose.position[1];
    const auto distance = std::hypot(dx, dy);
    if (distance > 0) pose.yaw = std::atan2(dx, -dy) / radians;
    const auto stepMs = std::clamp(deltaMs, 0., 50.);
    const float stoppingDistance = route.handRangeCm;
    if (actor && actor->handPunches) {
        const auto& clips = *actor->handPunches;
        const auto canReach = [&] {
            return distance <= stoppingDistance && std::abs(target[2] - pose.position[2]) <= stoppingDistance &&
                !(playback.vehicle && playback.vehicle->occupied) && !crossesWall(terrain, pose.position, target, 28);
        };
        if (punchMs) {
            const auto previous = *punchMs;
            *punchMs += stepMs;
            const auto duration = clips[punchIndex].durationMs;
            // Approximate contact until the retail punch collision query is ported.
            const double impactMs = duration * 0.5;
            if (previous < impactMs && *punchMs >= impactMs && canReach())
                playback.damagePlayer(route.handDamage);
            if (*punchMs < duration) return;
            punchMs.reset();
            punchIndex = (punchIndex + 1) % clips.size();
        }
        if (playback.playerHealth == 0) return;
        if (canReach()) {
            punchMs = 0;
            pose.walking = false;
            return;
        }
    }
    const auto travel = std::min(distance - stoppingDistance, float(stepMs * .001) * opening.walkSpeed);
    bool moved = false;
    if (travel > 0) {
        auto next = add(pose.position, {dx * travel / distance, dy * travel / distance, 0});
        if (const auto groundHit = terrain.groundContact(next, pose.groundTriangle)) {
            next[2] = groundHit->height;
            if (!crossesWall(terrain, pose.position, next, 28)) {
                pose.position = next;
                pose.groundTriangle = groundHit->triangle;
                moved = true;
            }
        }
    }
    pose.clipTimeMs = (pose.walking == moved ? pose.clipTimeMs : 0) + stepMs;
    pose.walking = moved;
}

void CityPlayback::pursuePayphoneTarget(const CityOpening& opening, const CityTerrain& terrain, double deltaMs)
{
    if (!opening.payphone || !payphoneAttack || !player || payphoneAttack->target != "Player") return;
    if (payphoneOpponentHealth == 0) {
        payphoneDefeatMs += std::max(0., deltaMs);
        return;
    }
    const auto& route = *opening.payphone;
    if (!payphonePursuer) {
        const auto* marker = cityPayphonePlacement(route, payphoneAttack->attacker, route.cinematicDurationMs);
        if (!marker || !marker->visible) return;
        payphonePursuer = CityPlayerPose{marker->position, marker->rotationDegrees[2]};
    }
    const auto actor = std::find_if(opening.actors.begin(), opening.actors.end(), [&](const CityActor& candidate) {
        return candidate.payphoneOnly && candidate.name == payphoneAttack->attacker;
    });
    pursueMeleeTarget(*this, opening, terrain, actor == opening.actors.end() ? nullptr : &*actor,
                      *payphonePursuer, payphonePunchMs, payphonePunchIndex, deltaMs);
}

void CityPlayback::spawnFightOpponent(const CityOpening& opening, const CityTerrain& terrain,
                                      const CityCinematicPlacement& placement, bool attacking)
{
    const auto actor = std::find_if(opening.actors.begin(), opening.actors.end(), [&](const auto& entry) {
        return entry.payphoneOnly && entry.name == placement.actor;
    });
    if (actor == opening.actors.end() || !actor->npcHealth)
        throw std::logic_error("Missing retail fight actor definition.");
    CityFightOpponent opponent{CityPlayerPose{placement.position, placement.rotationDegrees[2]}};
    opponent.health = *actor->npcHealth;
    opponent.attacking = attacking;
    if (const auto ground = terrain.groundContact(opponent.pose.position)) {
        opponent.pose.position[2] = ground->height;
        opponent.pose.groundTriangle = ground->triangle;
    }
    locoOpponents.insert_or_assign(placement.actor, std::move(opponent));
}

void CityPlayback::updateLocoFight(const CityOpening& opening, const CityTerrain& terrain, double deltaMs)
{
    if (!player || !opening.payphone) return;
    for (const auto& actor : opening.actors) {
        const auto entry = locoOpponents.find(actor.name);
        if (!actor.payphoneOnly || entry == locoOpponents.end() || entry->second.disabled) continue;
        auto& opponent = entry->second;
        if (opponent.health == 0) {
            opponent.defeatMs += std::max(0., deltaMs);
            continue;
        }
        using Side = CityFightOpponent::Side;
        if ((opponent.rangedTarget || opponent.side != Side::neutral) && actor.scriptedRangedWeapon) {
            opponent.pose.walking = false;
            opponent.pose.clipTimeMs += std::max(0., deltaMs);
            const auto& weapon = *actor.scriptedRangedWeapon;
            const Vec3* targetPosition = nullptr;
            int32_t* targetHealth = nullptr;
            auto nearest = std::numeric_limits<float>::max();
            const auto consider = [&](const Vec3& position, int32_t& health) {
                if (health <= 0) return;
                const auto offset = add(position, scale(opponent.pose.position, -1));
                const auto distance = dot(offset, offset);
                if (distance >= nearest) return;
                nearest = distance;
                targetPosition = &position;
                targetHealth = &health;
            };
            if (opponent.rangedTarget) {
                if (*opponent.rangedTarget == "Player") {
                    const auto& position = vehicle && vehicle->occupied ? vehicle->position : player->position;
                    consider(position, playerHealth);
                } else {
                    const auto target = locoOpponents.find(*opponent.rangedTarget);
                    if (target != locoOpponents.end()) consider(target->second.pose.position, target->second.health);
                }
            } else {
                if (opponent.side == Side::hostile) {
                    const auto& position = vehicle && vehicle->occupied ? vehicle->position : player->position;
                    consider(position, playerHealth);
                }
                for (auto& [name, candidate] : locoOpponents)
                    if (!candidate.disabled && candidate.side != Side::neutral && candidate.side != opponent.side)
                        consider(candidate.pose.position, candidate.health);
            }
            opponent.aiming = targetPosition != nullptr;
            if (!targetPosition) continue;
            opponent.shotCooldownMs = std::max(0., opponent.shotCooldownMs - deltaMs);
            const auto offset = add(*targetPosition, scale(opponent.pose.position, -1));
            if (offset[0] != 0 || offset[1] != 0)
                opponent.pose.yaw = std::atan2(offset[0], -offset[1]) / radians;
            if (opponent.shotCooldownMs == 0 && nearest <= float(weapon.range) * weapon.range &&
                !crossesWall(terrain, opponent.pose.position, *targetPosition, 28)) {
                if (targetHealth == &playerHealth) damagePlayer(weapon.damage);
                else *targetHealth -= std::min(*targetHealth, weapon.damage);
                opponent.shotCooldownMs = std::max(1, 1000 / weapon.shootFrequency);
            }
            continue;
        }
        if (!opponent.attacking) {
            opponent.pose.clipTimeMs += std::max(0., deltaMs);
            continue;
        }
        pursueMeleeTarget(*this, opening, terrain, &actor, opponent.pose, opponent.punchMs, opponent.punchIndex, deltaMs);
    }
}

static void punchActor(CityPlayback& playback, const CityOpening& opening, const CityTerrain& terrain,
                       const std::array<float, 3>& target, int32_t& health, double deltaMs, bool pressed, bool locked)
{
    const auto& clips = openingPlayer(opening).handPunches;
    if (!clips) return;
    const auto offset = add(target, scale(playback.player->position, -1));
    if (playback.playerPunchMs) {
        const auto previous = *playback.playerPunchMs;
        *playback.playerPunchMs += std::clamp(deltaMs, 0., 50.);
        const auto duration = (*clips)[playback.playerPunchIndex].durationMs;
        const double impact = duration * 0.5;
        const auto& route = *opening.payphone;
        if (previous < impact && *playback.playerPunchMs >= impact && locked &&
            dot(offset, offset) <= route.handRangeCm * route.handRangeCm &&
            !crossesWall(terrain, playback.player->position, target, 28))
            health -= std::clamp(route.handDamage, 0, health);
        if (*playback.playerPunchMs < duration) return;
        playback.playerPunchMs.reset();
        playback.playerPunchIndex = (playback.playerPunchIndex + 1) % clips->size();
    }
    if (pressed && health > 0) {
        if (locked && (offset[0] || offset[1])) playback.player->yaw = std::atan2(offset[0], -offset[1]) / radians;
        playback.player->walking = false;
        playback.playerPunchMs = 0;
    }
}

void CityPlayback::punchPayphoneOpponent(const CityOpening& opening, const CityTerrain& terrain,
                                         double deltaMs, bool pressed, bool locked)
{
    if (!opening.payphone || !payphonePursuer || !player || playerHealth == 0 ||
        (vehicle && vehicle->occupied)) {
        playerPunchMs.reset();
        return;
    }
    punchActor(*this, opening, terrain, payphonePursuer->position, payphoneOpponentHealth, deltaMs, pressed, locked);
}

void CityPlayback::punchLocoOpponent(const CityOpening& opening, const CityTerrain& terrain,
                                    double deltaMs, bool pressed, bool lockPressed)
{
    if (!opening.payphone || !player || playerHealth == 0 || (vehicle && vehicle->occupied)) {
        playerPunchMs.reset();
        locoLockTarget.reset();
        return;
    }
    auto nearest = locoOpponents.end();
    auto distanceSquared = std::numeric_limits<float>::max();
    for (auto it = locoOpponents.begin(); it != locoOpponents.end(); ++it) {
        if (it->second.disabled || it->second.health == 0 || it->second.side == CityFightOpponent::Side::friendly) continue;
        const auto offset = add(it->second.pose.position, scale(player->position, -1));
        const auto candidate = dot(offset, offset);
        if (candidate < distanceSquared) {
            distanceSquared = candidate;
            nearest = it;
        }
    }
    auto target = locoLockTarget ? locoOpponents.find(*locoLockTarget) : locoOpponents.end();
    if (target != locoOpponents.end() && (target->second.disabled || target->second.health == 0))
        target = locoOpponents.end();
    if (lockPressed) target = target != locoOpponents.end() ? locoOpponents.end() : nearest;
    locoLockTarget = target != locoOpponents.end() ? std::optional{target->first} : std::nullopt;
    const bool locked = target != locoOpponents.end();
    if (!locked) target = nearest;
    if (target == locoOpponents.end()) {
        playerPunchMs.reset();
        return;
    }
    auto& opponent = target->second;
    const auto before = opponent.health;
    punchActor(*this, opening, terrain, opponent.pose.position, opponent.health, deltaMs, pressed, locked);
    if (opponent.health < before) opponent.attacking = true;
}

std::optional<float> CityPlayback::payphoneBearing(const CityOpening& opening) const
{
    if (!opening.payphone || !player || !payphoneActive || payphoneReached) return {};
    const bool inCar = vehicle && vehicle->occupied;
    const auto& position = inCar ? vehicle->position : player->position;
    const auto facing = (inCar ? vehicle->yaw : openingPlayer(opening).rotationDegrees[2]) * radians;
    const auto direction = add(opening.payphone->destination.position, scale(position, -1));
    return std::atan2(direction[0], -direction[1]) - facing;
}

std::optional<std::array<float, 3>> CityPlayback::vehicleObjectivePosition(const CityOpening& opening) const
{
    if (!opening.vehicleActor || !opening.checkpoint || !timeAfterCheckpointMs || enteredVehicle ||
        *timeAfterCheckpointMs < opening.checkpoint->nextTutorial.startMs) return {};
    return vehicle ? vehicle->position : opening.actors.at(*opening.vehicleActor).position;
}

bool CityPlayback::acknowledgeFailure(const CityOpening& opening, double deltaMs,
                                      bool pressed, bool animationFinished)
{
    if (!missionFailure) return false;
    failureElapsedMs += deltaMs;
    // GSFailure holds input for one second and waits for the cinematic.
    if (!pressed || !animationFinished || failureElapsedMs < 1000) return false;
    if (nextMissionVehicleActive) {
        missionFailure.reset();
        failureElapsedMs = 0;
        missionConditions.clear();
        vehicle.reset();
        playerHealth = playerMaxHealth;
        nextMissionVehicleAcquired = false;
        return true;
    }
    const auto failures = missionFailureCount;
    *this = CityPlayback{};
    missionFailureCount = failures;
    elapsedMs = opening.handoffMs;
    nextDialogue = std::lower_bound(opening.dialogue.begin(), opening.dialogue.end(), elapsedMs,
        [](const CityDialogue& line, double time) { return line.startMs < time; }) - opening.dialogue.begin();
    dialogueHoldMs.resize(nextDialogue);
    return true;
}

void CityPlayback::advance(const CityOpening& opening, double deltaMs, bool pressed)
{
    advance(opening.dialogue, opening.durationMs, deltaMs, pressed);
}

void CityPlayback::advance(const std::vector<CityDialogue>& dialogue, uint32_t durationMs, double deltaMs, bool pressed)
{
    if (activeDialogue) {
        const auto index = *activeDialogue;
        auto& held = dialogueHoldMs[index];
        const auto duration = dialogue[index].durationMs;
        held += deltaMs;
        if ((!duration && !pressed) || (duration && held < duration)) return;
        activeDialogue.reset();
        deltaMs = duration ? held - duration : 0;
        if (duration) held = duration;
    }
    elapsedMs = std::min<double>(durationMs, elapsedMs + deltaMs);
    if (nextDialogue < dialogue.size() && elapsedMs >= dialogue[nextDialogue].startMs) {
        const auto start = dialogue[nextDialogue].startMs;
        dialogueHoldMs.push_back(elapsedMs - start);
        activeDialogue = nextDialogue++;
        elapsedMs = start;
    }
}

bool CityPayphonePlayback::advance(const CityPayphoneRoute& route, double deltaMs, bool pressed,
                                   const std::function<void(const CityAudioCue&)>& audio)
{
    return advance(route.cinematicDialogue, route.cinematicAudio, route.cinematicDurationMs, deltaMs, pressed, audio);
}

bool CityPayphonePlayback::advance(const CityMissionCinematic& scene, double deltaMs, bool pressed,
                                   const std::function<void(const CityAudioCue&)>& audio)
{
    return advance(scene.dialogue, scene.audio, scene.durationMs, deltaMs, pressed, audio);
}

bool CityPayphonePlayback::advance(const std::vector<CityDialogue>& dialogue, const std::vector<CityAudioCue>& cues,
                                   uint32_t durationMs, double deltaMs, bool pressed,
                                   const std::function<void(const CityAudioCue&)>& audio)
{
    timeline.advance(dialogue, durationMs, deltaMs, pressed);
    while (nextAudio < cues.size() && cues[nextAudio].timeMs <= elapsedMs()) {
        const auto& cue = cues[nextAudio++];
        if (audio) audio(cue);
    }
    return elapsedMs() >= durationMs && !activeDialogue();
}

const CityCinematicPlacement* cityPayphonePlacement(const CityPayphoneRoute& route,
                                                   std::string_view actor, double elapsedMs)
{
    return cinematicPlacement(route.cinematicPlacements, actor, elapsedMs);
}

const CityMissionCinematic::Motion* cityCinematicMotion(const CityMissionCinematic& scene,
                                                       std::string_view actor, double elapsedMs)
{
    const auto cue = std::find_if(scene.motions.rbegin(), scene.motions.rend(), [&](const auto& entry) {
        return entry.travel.actor == actor && entry.travel.startMs <= elapsedMs;
    });
    return cue == scene.motions.rend() ? nullptr : &*cue;
}

std::optional<std::array<float, 3>> cityTailingActorPosition(const CityMissionCinematic& scene,
    std::string_view actor, const CityTerrain& terrain, double elapsedMs)
{
    const auto* placement = cinematicPlacement(scene.placements, actor, elapsedMs);
    if (!placement || !placement->visible) return std::nullopt;
    const auto* motion = cityCinematicMotion(scene, actor, elapsedMs);
    auto position = motion ? cityCinematicTravelPosition(motion->travel, elapsedMs) : placement->position;
    if (const auto ground = terrain.groundContact(position, std::nullopt, 1500, 1500))
        position[2] = ground->height;
    return position;
}

void pursueCityVehicle(CityVehiclePose& vehicle, const CityTerrain& terrain,
                      const std::array<float, 3>& target, float cruiseSpeedCmPerMs, double elapsedMs)
{
    const auto offset = add(target, scale(vehicle.position, -1));
    const auto distance = std::hypot(offset[0], offset[1]);
    constexpr float stoppingDistance = 350;
    const float travel = std::min(distance - stoppingDistance,
                                  cruiseSpeedCmPerMs * static_cast<float>(std::clamp(elapsedMs, 0., 50.)));
    if (travel <= 0) return;

    const float desiredYaw = std::atan2(offset[0], -offset[1]) / radians;
    for (const float bias : {0.f, -40.f, 40.f, -75.f, 75.f}) {
        const float angle = (desiredYaw + bias) * radians;
        auto position = add(vehicle.position, {std::sin(angle) * travel, -std::cos(angle) * travel, 0});
        const auto ground = terrain.groundContact(position, vehicle.groundTriangle, 70, 100);
        if (!ground) continue;
        position[2] = ground->height;
        if (crossesWall(terrain, vehicle.position, position, 90)) continue;
        vehicle.position = position;
        vehicle.groundTriangle = ground->triangle;
        vehicle.yaw = desiredYaw + bias;
        vehicle.wheelTravel += travel;
        return;
    }
}

std::optional<CityVehiclePose> cityStagedVehiclePose(const CityMissionCinematic& scene,
    std::string_view actor, const CityTerrain& terrain, double elapsedMs)
{
    const auto* placement = cinematicPlacement(scene.placements, actor, elapsedMs);
    if (!placement || !placement->visible) return std::nullopt;
    CityVehiclePose initial{placement->position, placement->rotationDegrees[2]};
    if (const auto ground = terrain.groundContact(initial.position)) {
        initial.position[2] = ground->height;
        initial.groundTriangle = ground->triangle;
    }
    return cityCinematicVehiclePose(scene, actor, initial, terrain, elapsedMs).value_or(initial);
}

std::optional<CityVehiclePose> cityCinematicVehiclePose(const CityMissionCinematic& scene,
    std::string_view actor, const CityVehiclePose& initial, const CityTerrain& terrain, double elapsedMs)
{
    const auto drive = std::find_if(scene.vehicleDrives.rbegin(), scene.vehicleDrives.rend(), [&](const auto& cue) {
        return cue.actor == actor && cue.startMs <= elapsedMs;
    });
    if (drive == scene.vehicleDrives.rend()) return std::nullopt;
    const auto direction = add(drive->destination, scale(initial.position, -1));
    const auto distance = std::hypot(direction[0], direction[1]);
    auto vehicle = initial;
    if (distance <= 0) return vehicle;
    const auto travelled = static_cast<float>(std::min<double>(distance, (elapsedMs - drive->startMs) * drive->speedCmPerMs));
    vehicle.position = add(initial.position, scale(direction, travelled / distance));
    if (const auto ground = terrain.groundContact(vehicle.position, initial.groundTriangle, 150, 150))
        vehicle.position[2] = ground->height;
    vehicle.yaw = std::atan2(direction[0], -direction[1]) / radians;
    vehicle.wheelTravel += travelled;
    return vehicle;
}

static std::pair<std::array<float, 3>, std::array<float, 3>>
sampleCinematicTravel(const CityCinematicWalk& travel, double elapsedMs)
{
    const auto fraction = travel.durationMs ? static_cast<float>(
        std::clamp((elapsedMs - travel.startMs) / travel.durationMs, 0., 1.)) : 1.f;
    if (travel.waypoints.empty()) {
        const auto direction = add(travel.to, scale(travel.from, -1));
        return {add(travel.from, scale(direction, fraction)), direction};
    }

    auto previous = travel.from;
    float total = 0;
    for (const auto& point : travel.waypoints) {
        total += std::hypot(point[0] - previous[0], point[1] - previous[1]);
        previous = point;
    }
    auto remaining = fraction * total;
    previous = travel.from;
    std::array<float, 3> direction{};
    for (const auto& point : travel.waypoints) {
        const auto segment = add(point, scale(previous, -1));
        const auto distance = std::hypot(segment[0], segment[1]);
        if (distance > 0) {
            direction = segment;
            if (remaining <= distance)
                return {add(previous, scale(segment, remaining / distance)), direction};
            remaining -= distance;
        }
        previous = point;
    }
    return {travel.to, direction};
}

std::array<float, 3> cityCinematicTravelPosition(const CityCinematicWalk& travel, double elapsedMs)
{
    return sampleCinematicTravel(travel, elapsedMs).first;
}

void poseCityActors(const CityOpening& opening, const std::vector<CitySkinRange>& skins,
                    const CityPlayback& playback, CityVertex* vertices, std::optional<double> payphoneMs,
                    std::optional<double> missionMs, const CityMissionCinematic* missionScene, const CityTerrain* terrain)
{
    const auto* route = opening.payphone && opening.payphone->postFight.nextCheckpoint
        ? &*opening.payphone->postFight.nextCheckpoint : nullptr;
    const bool tailing = route && missionScene == &route->postLcTail;
    const bool showLiveOpponents = !missionScene || (route && (missionScene == &route->locoCooperIntro || missionScene == &route->elToroInterlude ||
                                                     missionScene == &route->elToroDeparture || missionScene == &route->vanHandshake || missionScene == &route->vanPoliceReveal));
    for (const auto& range : skins) {
        const auto& actor = opening.actors[range.actor];
        const auto elapsedMs = actor.payphoneOnly ? payphoneMs.value_or(-1) : playback.elapsedMs;
        const auto* player = playback.player && actor.name == "Player" ? &*playback.player : nullptr;
        if (elapsedMs < actor.startMs || elapsedMs >= actor.endMs) continue;
        const CityAnimationClip* clip = nullptr;
        double clipTime = 0;
        auto world = actorTransform(actor, player);
        const auto* missionPlacement = missionMs && missionScene && (player || actor.payphoneOnly)
            ? cinematicPlacement(missionScene->placements, actor.name, *missionMs) : nullptr;
        const auto* staging = missionPlacement ? missionPlacement : payphoneMs && opening.payphone &&
            (actor.payphoneOnly || (player && *payphoneMs < opening.payphone->cinematicDurationMs))
            ? cityPayphonePlacement(*opening.payphone, actor.name, *payphoneMs) : nullptr;
        const auto locoEntry = playback.locoOpponents.find(actor.name);
        const auto* loco = actor.payphoneOnly && showLiveOpponents && !missionPlacement && locoEntry != playback.locoOpponents.end() &&
            !locoEntry->second.disabled ? &locoEntry->second : nullptr;
        if (actor.payphoneOnly && !loco && opening.vehicleActor != range.actor && (!staging || !staging->visible)) continue;
        if (staging && staging->visible) {
            const auto position = tailing && missionPlacement && terrain
                ? cityTailingActorPosition(*missionScene, actor.name, *terrain, *missionMs).value_or(staging->position)
                : staging->position;
            world = placementTransform(position, staging->rotationDegrees, actor.scale, staging->rotationDegrees[2]);
        }
        const auto* pursuer = actor.payphoneOnly && playback.payphonePursuer && playback.payphoneAttack &&
            playback.payphoneAttack->attacker == actor.name ? &*playback.payphonePursuer : nullptr;
        if (loco) world = actorTransform(actor, &loco->pose);
        else if (pursuer) world = actorTransform(actor, pursuer);
        const auto* walk = !missionPlacement && actor.payphoneOnly && opening.payphone && opening.payphone->walk &&
            opening.payphone->walk->actor == actor.name && elapsedMs >= opening.payphone->walk->startMs
            ? &*opening.payphone->walk : nullptr;
        const auto* motion = missionPlacement ? cityCinematicMotion(*missionScene, actor.name, *missionMs) : nullptr;
        const auto* travel = motion ? &motion->travel : walk;
        const auto travelMs = motion ? *missionMs : elapsedMs;
        if (travel && travel->durationMs) {
            auto [position, direction] = sampleCinematicTravel(*travel, travelMs);
            if (tailing && terrain && motion) {
                position = cityTailingActorPosition(*missionScene, actor.name, *terrain, travelMs).value_or(position);
            } else if (terrain && !travel->waypoints.empty()) {
                if (const auto ground = terrain->groundContact(position, std::nullopt, 1500, 1500))
                    position[2] = ground->height;
            }
            const auto yaw = std::atan2(direction[0], -direction[1]) / radians;
            world = placementTransform(position, staging->rotationDegrees, actor.scale, yaw);
        }
        const CityActorAnimation* missionAnimation = nullptr;
        if (missionMs && missionScene && (missionPlacement || player)) {
            const auto track = missionScene->actorAnimations.find(actor.name);
            if (track != missionScene->actorAnimations.end()) {
                const auto& animations = track->second;
                const auto next = std::upper_bound(animations.begin(), animations.end(), *missionMs,
                    [](double time, const CityActorAnimation& animation) { return time < animation.startMs; });
                if (next != animations.begin()) missionAnimation = &*std::prev(next);
            }
        }
        if (opening.vehicleActor == range.actor) {
            if (!playback.vehicle) continue;
            const auto& vehicle = *playback.vehicle;
            const auto frame = vehicleDoorFrame(actor, vehicle, opening);
            clip = frame.first;
            clipTime = frame.second;
            if (!clip) continue;
            world = vehicleTransform(actor, vehicle);
        } else if (!missionPlacement && player && playback.vehicle && opening.vehicleCharacterClips &&
                   (playback.vehicle->doorAction || (playback.vehicle->occupied && opening.vehicleSeated))) {
            const auto& vehicle = *playback.vehicle;
            const auto frame = vehicleCharacterFrame(opening, vehicle);
            clip = frame.first;
            clipTime = frame.second;
            const auto side = vehicle.doorAction ? vehicle.doorAction->side : vehicle.entrySide;
            world = vehicleCharacterTransform(actor, opening.actors[*opening.vehicleActor], vehicle,
                                              (*opening.vehicleCharacterClips)[side], *clip);
        } else if (motion && travelMs < double(motion->travel.startMs) + motion->travel.durationMs) {
            clip = &motion->clip;
            clipTime = std::fmod(travelMs - motion->travel.startMs, clip->durationMs);
        } else if (missionAnimation) {
            const auto& animation = *missionAnimation;
            const auto time = *missionMs - animation.startMs;
            clip = time < animation.first.durationMs ? &animation.first : &animation.loop;
            clipTime = time < animation.first.durationMs ? time :
                std::fmod(time - animation.first.durationMs, animation.loop.durationMs);
        } else if (player && playback.playerPunchMs && actor.handPunches) {
            clip = &(*actor.handPunches)[playback.playerPunchIndex];
            clipTime = *playback.playerPunchMs;
        } else if (loco && loco->health == 0 && actor.defeatAnimation) {
            const auto& defeat = *actor.defeatAnimation;
            clip = loco->defeatMs < defeat.first.durationMs ? &defeat.first : &defeat.loop;
            clipTime = loco->defeatMs < defeat.first.durationMs ? loco->defeatMs :
                std::fmod(loco->defeatMs - defeat.first.durationMs, defeat.loop.durationMs);
        } else if (loco && (loco->rangedTarget || loco->aiming) && actor.scriptedAim) {
            clip = &*actor.scriptedAim;
            clipTime = std::fmod(loco->pose.clipTimeMs, clip->durationMs);
        } else if (loco && loco->punchMs && actor.handPunches) {
            clip = &(*actor.handPunches)[loco->punchIndex];
            clipTime = *loco->punchMs;
        } else if (loco && loco->pose.walking && actor.pursuitWalk) {
            clip = &*actor.pursuitWalk;
            clipTime = std::fmod(loco->pose.clipTimeMs, clip->durationMs);
        } else if (loco) {
            clip = &*actor.fightIdle;
            clipTime = std::fmod(loco->pose.clipTimeMs, clip->durationMs);
        } else if (pursuer && playback.payphoneOpponentHealth == 0 && actor.defeatAnimation) {
            const auto& defeat = *actor.defeatAnimation;
            const auto time = playback.payphoneDefeatMs;
            clip = time < defeat.first.durationMs ? &defeat.first : &defeat.loop;
            clipTime = time < defeat.first.durationMs ? time :
                std::fmod(time - defeat.first.durationMs, defeat.loop.durationMs);
        } else if (pursuer && playback.payphonePunchMs && actor.handPunches) {
            clip = &(*actor.handPunches)[playback.payphonePunchIndex];
            clipTime = *playback.payphonePunchMs;
        } else if (pursuer && pursuer->walking && actor.pursuitWalk) {
            clip = &*actor.pursuitWalk;
            clipTime = std::fmod(pursuer->clipTimeMs, clip->durationMs);
        } else if (player && !missionPlacement && staging && staging->visible) {
            const auto& animations = opening.payphone->playerAnimations;
            const auto next = std::upper_bound(animations.begin(), animations.end(), *payphoneMs,
                [](double time, const CityActorAnimation& animation) { return time < animation.startMs; });
            if (next == animations.begin()) {
                if (actor.animations.empty()) continue;
                clip = &actor.animations.back().loop;
                clipTime = std::fmod(*payphoneMs - staging->timeMs, clip->durationMs);
            } else {
                const auto& animation = *std::prev(next);
                const auto time = *payphoneMs - animation.startMs;
                clip = time < animation.first.durationMs ? &animation.first : &animation.loop;
                clipTime = time < animation.first.durationMs ? time :
                    std::fmod(time - animation.first.durationMs, animation.loop.durationMs);
            }
        } else {
            if (actor.animations.empty()) continue;
            const auto next = std::upper_bound(actor.animations.begin(), actor.animations.end(), elapsedMs,
                [](double time, const CityActorAnimation& animation) { return time < animation.startMs; });
            const bool entering = next == actor.animations.begin();
            const auto& animation = entering ? actor.animations.front() : *std::prev(next);
            const auto start = entering ? actor.startMs : animation.startMs;
            auto time = elapsedMs - start;
            if (!actor.payphoneOnly)
                for (size_t i = 0; i < playback.dialogueHoldMs.size(); ++i)
                    if (opening.dialogue[i].startMs >= start) time += playback.dialogueHoldMs[i];
            const bool loop = entering || time >= animation.first.durationMs;
            clip = player ? (player->walking ? &opening.walk : &actor.animations.back().loop)
                          : (loop ? &animation.loop : &animation.first);
            const auto loopTime = entering ? time : time - animation.first.durationMs;
            clipTime = player ? std::fmod(player->clipTimeMs, clip->durationMs)
                              : (loop ? std::fmod(loopTime, clip->durationMs) : time);
        }
        const bool removeRootMotion = (player && player->walking && clip == &opening.walk) ||
            ((walk || pursuer || loco) && clip->name == "move_walk") || (motion && clip == &motion->clip);
        const auto transforms = animatedSkeleton(actor.model, *clip, clipTime, removeRootMotion);
        const auto* vehicle = opening.vehicleActor == range.actor ? &*playback.vehicle : nullptr;
        poseSkinnedMesh(actor.model, actor.skins[range.skin], transforms, world, vehicle,
                        vertices + range.firstVertex);
    }
}

void poseCityActors(const CityOpening& opening, const std::vector<CitySkinRange>& skins,
                    double elapsedMs, CityVertex* vertices)
{
    CityPlayback playback;
    playback.elapsedMs = elapsedMs;
    poseCityActors(opening, skins, playback, vertices);
}
