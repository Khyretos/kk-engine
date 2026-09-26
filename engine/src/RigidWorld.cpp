#include "kke/RigidWorld.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace kke {

namespace {

// Two object layers: things that never move, and everything else.
// Static vs static pairs are never tested.
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
} // namespace Layers
namespace BroadLayers {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr uint32_t kCount = 2;
} // namespace BroadLayers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    uint32_t GetNumBroadPhaseLayers() const override { return BroadLayers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::kStatic ? BroadLayers::kStatic : BroadLayers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadLayers::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        return layer == Layers::kMoving || broad == BroadLayers::kMoving;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == Layers::kMoving || b == Layers::kMoving; }
};

JPH::Vec3 toJ(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
JPH::RVec3 toJR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
JPH::Quat toJ(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
glm::vec3 toG(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
#ifdef JPH_DOUBLE_PRECISION
glm::vec3 toG(JPH::RVec3Arg v) { return glm::vec3(float(v.GetX()), float(v.GetY()), float(v.GetZ())); }
#endif
glm::quat toG(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

void initJoltOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory(); // process lifetime
        JPH::RegisterTypes();
    });
}

} // namespace

struct RigidWorld::Impl : public JPH::ContactListener {
    Settings settings;
    BroadPhaseLayers broadLayers;
    ObjectVsBroadPhase objectVsBroad;
    ObjectPairs objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystem> jobs;
    JPH::PhysicsSystem system;
    struct Character {
        JPH::Ref<JPH::CharacterVirtual> ch;
        CharacterDesc desc;
        CharacterInput input;
        bool kinematic = false;
    };
    std::unordered_map<CharacterId, Character> characters;
    CharacterId nextCharacter = 1;
    std::mutex contactMutex;
    std::vector<Contact> contacts;
    double stepMs = 0.0;

    JPH::BodyInterface& bodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& bodies() const { return system.GetBodyInterface(); }

    // Called from worker threads during step().
    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override {
        JPH::RVec3 p = manifold.GetWorldSpaceContactPointOn1(0);
        JPH::Vec3 rel = b2.GetPointVelocity(p) - b1.GetPointVelocity(p);
        float speed = std::fabs(rel.Dot(manifold.mWorldSpaceNormal));
        if (speed < settings.contactReportSpeed) return;
        Contact c;
        c.a = b1.GetID().GetIndexAndSequenceNumber();
        c.b = b2.GetID().GetIndexAndSequenceNumber();
        c.point = glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
        c.normal = toG(manifold.mWorldSpaceNormal);
        c.speed = speed;
        c.materialA = static_cast<uint32_t>(b1.GetUserData());
        c.materialB = static_cast<uint32_t>(b2.GetUserData());
        std::lock_guard<std::mutex> lock(contactMutex);
        if (contacts.size() < 4096) contacts.push_back(c); // budget: a pile settling can report thousands
    }
};

RigidWorld::RigidWorld() : RigidWorld(Settings{}) {}

RigidWorld::RigidWorld(const Settings& settings) : m(std::make_unique<Impl>()) {
    initJoltOnce();
    m->settings = settings;
    m->temp = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    int threads = settings.threads;
    if (threads < 0) threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    if (threads == 0) m->jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    else m->jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    m->system.Init(settings.maxBodies, 0, settings.maxBodies, settings.maxBodies / 4, m->broadLayers, m->objectVsBroad, m->objectPairs);
    m->system.SetGravity(toJ(settings.gravity));
    m->system.SetContactListener(m.get());
}

RigidWorld::~RigidWorld() {
    m->characters.clear();
    JPH::BodyIDVector ids;
    m->system.GetBodies(ids);
    for (JPH::BodyID id : ids) {
        m->bodies().RemoveBody(id);
        m->bodies().DestroyBody(id);
    }
}

RigidWorld::BodyId RigidWorld::add(const BodyDesc& d) {
    JPH::ShapeRefC shape;
    auto fromSettings = [&](const JPH::ShapeSettings& s) -> JPH::ShapeRefC {
        JPH::ShapeSettings::ShapeResult r = s.Create();
        return r.HasError() ? nullptr : r.Get();
    };
    const bool dynamic = d.motion == Motion::Dynamic;
    switch (d.shape) {
    case Shape::Box: {
        JPH::BoxShapeSettings s(toJ(glm::max(d.halfExtents, glm::vec3(0.01f))), std::min(0.05f, glm::min(glm::min(d.halfExtents.x, d.halfExtents.y), d.halfExtents.z) * 0.5f));
        s.mDensity = d.density;
        shape = fromSettings(s);
        break;
    }
    case Shape::Sphere: {
        JPH::SphereShapeSettings s(std::max(0.005f, d.radius));
        s.mDensity = d.density;
        shape = fromSettings(s);
        break;
    }
    case Shape::Capsule: {
        JPH::CapsuleShapeSettings s(std::max(0.005f, d.halfHeight), std::max(0.005f, d.radius));
        s.mDensity = d.density;
        shape = fromSettings(s);
        break;
    }
    case Shape::Mesh:
        if (!dynamic) {
            JPH::VertexList verts;
            verts.reserve(d.points.size());
            for (const glm::vec3& p : d.points) verts.push_back(JPH::Float3(p.x, p.y, p.z));
            JPH::IndexedTriangleList tris;
            for (size_t i = 0; i + 2 < d.indices.size(); i += 3) tris.push_back(JPH::IndexedTriangle(d.indices[i], d.indices[i + 1], d.indices[i + 2]));
            shape = fromSettings(JPH::MeshShapeSettings(verts, tris));
            break;
        }
        [[fallthrough]]; // a moving triangle mesh can't collide robustly: use its hull
    case Shape::ConvexHull: {
        JPH::Array<JPH::Vec3> pts;
        pts.reserve(d.points.size());
        for (const glm::vec3& p : d.points) pts.push_back(toJ(p));
        JPH::ConvexHullShapeSettings s(pts, 0.01f);
        s.mDensity = d.density;
        shape = fromSettings(s);
        break;
    }
    }
    if (!shape) return kNoBody;
    const JPH::EMotionType motion = d.motion == Motion::Static ? JPH::EMotionType::Static
                                    : d.motion == Motion::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings bcs(shape, toJR(d.position), toJ(glm::normalize(d.rotation)), motion,
                                  d.motion == Motion::Static ? Layers::kStatic : Layers::kMoving);
    bcs.mFriction = d.friction;
    bcs.mRestitution = d.restitution;
    bcs.mLinearVelocity = toJ(d.velocity);
    bcs.mAngularVelocity = toJ(d.angularVelocity);
    bcs.mUserData = d.material;
    if (d.motion == Motion::Dynamic) bcs.mMotionQuality = JPH::EMotionQuality::LinearCast; // no tunnelling for fast rocks
    JPH::BodyID id = m->bodies().CreateAndAddBody(bcs, d.motion == Motion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    return id.IsInvalid() ? kNoBody : id.GetIndexAndSequenceNumber();
}

void RigidWorld::remove(BodyId body) {
    JPH::BodyID id(body);
    if (body == kNoBody || !m->bodies().IsAdded(id)) return;
    m->bodies().RemoveBody(id);
    m->bodies().DestroyBody(id);
}

size_t RigidWorld::bodyCount() const { return m->system.GetNumBodies(); }
size_t RigidWorld::activeBodyCount() const { return m->system.GetNumActiveBodies(JPH::EBodyType::RigidBody); }
bool RigidWorld::isActive(BodyId body) const { return m->bodies().IsActive(JPH::BodyID(body)); }

glm::vec3 RigidWorld::position(BodyId body) const {
    JPH::RVec3 p = m->bodies().GetPosition(JPH::BodyID(body));
    return glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
}
glm::quat RigidWorld::rotation(BodyId body) const { return toG(m->bodies().GetRotation(JPH::BodyID(body))); }
glm::mat4 RigidWorld::transform(BodyId body) const {
    return glm::translate(glm::mat4(1.0f), position(body)) * glm::mat4_cast(rotation(body));
}
glm::vec3 RigidWorld::velocity(BodyId body) const { return toG(m->bodies().GetLinearVelocity(JPH::BodyID(body))); }
void RigidWorld::setVelocity(BodyId body, const glm::vec3& v) { m->bodies().SetLinearVelocity(JPH::BodyID(body), toJ(v)); }
void RigidWorld::addImpulse(BodyId body, const glm::vec3& impulse, const glm::vec3& point) {
    m->bodies().AddImpulse(JPH::BodyID(body), toJ(impulse), toJR(point));
}
void RigidWorld::moveKinematic(BodyId body, const glm::vec3& position, const glm::quat& rotation, float dt) {
    m->bodies().MoveKinematic(JPH::BodyID(body), toJR(position), toJ(glm::normalize(rotation)), dt);
}

RigidWorld::RayHit RigidWorld::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
    RayHit out;
    glm::vec3 dir = glm::length(direction) > 1e-9f ? glm::normalize(direction) : glm::vec3(0, -1, 0);
    JPH::RRayCast ray(toJR(origin), toJ(dir * maxDistance));
    // Hit both sides of triangles, like the character does: Synty meshes
    // are often open or flipped, and a ray that slips through a back face
    // would say "nothing there" in front of a wall the capsule can't pass.
    JPH::RayCastSettings settings;
    settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> closest;
    m->system.GetNarrowPhaseQuery().CastRay(ray, settings, closest);
    if (!closest.HadHit()) return out;
    const JPH::RayCastResult& r = closest.mHit;
    out.hit = true;
    out.body = r.mBodyID.GetIndexAndSequenceNumber();
    out.distance = r.mFraction * maxDistance;
    out.point = origin + dir * out.distance;
    JPH::BodyLockRead lock(m->system.GetBodyLockInterface(), r.mBodyID);
    if (lock.Succeeded()) out.normal = toG(lock.GetBody().GetWorldSpaceSurfaceNormal(r.mSubShapeID2, ray.GetPointOnRay(r.mFraction)));
    if (glm::dot(out.normal, dir) > 0.0f) out.normal = -out.normal; // a back face: the side the ray came from
    return out;
}

namespace {
// A capsule standing on the origin (the character's position is its feet).
JPH::RefConst<JPH::Shape> characterShape(float height, float radius) {
    const float halfCylinder = std::max(0.01f, height * 0.5f - radius);
    JPH::RotatedTranslatedShapeSettings shapeSettings(JPH::Vec3(0.0f, halfCylinder + radius, 0.0f), JPH::Quat::sIdentity(),
                                                      new JPH::CapsuleShape(halfCylinder, radius));
    return shapeSettings.Create().Get();
}
} // namespace

RigidWorld::CharacterId RigidWorld::addCharacter(const CharacterDesc& d) {
    JPH::CharacterVirtualSettings s;
    s.mShape = characterShape(d.height, d.radius);
    s.mMaxSlopeAngle = JPH::DegreesToRadians(d.maxSlopeDegrees);
    s.mMaxStrength = d.pushStrength;
    s.mMass = d.mass;
    s.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -d.radius); // only the bottom sphere counts as feet
    Impl::Character c;
    c.ch = new JPH::CharacterVirtual(&s, toJR(d.position), JPH::Quat::sIdentity(), 0, &m->system);
    c.desc = d;
    CharacterId id = m->nextCharacter++;
    m->characters.emplace(id, std::move(c));
    return id;
}

void RigidWorld::removeCharacter(CharacterId id) { m->characters.erase(id); }

bool RigidWorld::setCharacterHeight(CharacterId id, float height) {
    auto it = m->characters.find(id);
    if (it == m->characters.end()) return false;
    Impl::Character& c = it->second;
    height = std::max(height, 2.0f * c.desc.radius + 0.02f);
    if (std::abs(height - c.desc.height) < 1e-4f) return true;
    // Growing: refuse if the taller capsule would overlap anything
    // (a tiny tolerance so resting contacts don't count).
    const float tolerance = height > c.desc.height ? 0.01f : FLT_MAX;
    if (!c.ch->SetShape(characterShape(height, c.desc.radius), tolerance, m->system.GetDefaultBroadPhaseLayerFilter(Layers::kMoving),
                        m->system.GetDefaultLayerFilter(Layers::kMoving), {}, {}, *m->temp))
        return false;
    c.desc.height = height;
    return true;
}

float RigidWorld::characterHeight(CharacterId id) const {
    auto it = m->characters.find(id);
    return it == m->characters.end() ? 0.0f : it->second.desc.height;
}

void RigidWorld::setCharacterInput(CharacterId id, const CharacterInput& input) {
    auto it = m->characters.find(id);
    if (it != m->characters.end()) it->second.input = input;
}

glm::vec3 RigidWorld::characterPosition(CharacterId id) const {
    auto it = m->characters.find(id);
    if (it == m->characters.end()) return glm::vec3(0.0f);
    JPH::RVec3 p = it->second.ch->GetPosition();
    return glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
}

glm::vec3 RigidWorld::characterVelocity(CharacterId id) const {
    auto it = m->characters.find(id);
    return it == m->characters.end() ? glm::vec3(0.0f) : toG(it->second.ch->GetLinearVelocity());
}

bool RigidWorld::characterOnGround(CharacterId id) const {
    auto it = m->characters.find(id);
    return it != m->characters.end() && it->second.ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

void RigidWorld::teleportCharacter(CharacterId id, const glm::vec3& feet) {
    auto it = m->characters.find(id);
    if (it == m->characters.end()) return;
    it->second.ch->SetPosition(toJR(feet));
    it->second.ch->SetLinearVelocity(JPH::Vec3::sZero());
}

void RigidWorld::setCharacterKinematic(CharacterId id, bool kinematic) {
    auto it = m->characters.find(id);
    if (it != m->characters.end()) it->second.kinematic = kinematic;
}

bool RigidWorld::characterKinematic(CharacterId id) const {
    auto it = m->characters.find(id);
    return it != m->characters.end() && it->second.kinematic;
}

void RigidWorld::moveCharacter(CharacterId id, const glm::vec3& feet) {
    auto it = m->characters.find(id);
    if (it != m->characters.end()) it->second.ch->SetPosition(toJR(feet));
}

void RigidWorld::setCharacterVelocity(CharacterId id, const glm::vec3& velocity) {
    auto it = m->characters.find(id);
    if (it != m->characters.end()) it->second.ch->SetLinearVelocity(toJ(velocity));
}

bool RigidWorld::capsuleFits(const glm::vec3& feet, float height, float radius) const {
    JPH::RefConst<JPH::Shape> shape = characterShape(height, radius);
    // CollideShape places the shape by its centre of mass, not its origin.
    JPH::CollideShapeSettings settings;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
    settings.mMaxSeparationDistance = 0.0f;
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> hit;
    m->system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(toJR(feet) + shape->GetCenterOfMass()), settings, JPH::RVec3::sZero(),
                                                  hit, m->system.GetDefaultBroadPhaseLayerFilter(Layers::kMoving),
                                                  m->system.GetDefaultLayerFilter(Layers::kMoving));
    return !hit.HadHit();
}

void RigidWorld::step(float dt) {
    if (dt <= 0.0f) return;
    auto t0 = std::chrono::steady_clock::now();
    const JPH::Vec3 gravity = m->system.GetGravity();
    for (auto& [id, c] : m->characters) {
        if (c.kinematic) continue; // placed by the caller (vaults, climbs)
        JPH::CharacterVirtual& ch = *c.ch;
        ch.UpdateGroundVelocity();
        const bool grounded = ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        const JPH::Vec3 current = ch.GetLinearVelocity();
        JPH::Vec3 move(c.input.move.x, 0.0f, c.input.move.z);
        JPH::Vec3 v;
        if (grounded) {
            // On the ground: walk with it (moving platforms), jump off it.
            v = ch.GetGroundVelocity() + move;
            if (c.input.jump) v += JPH::Vec3(0.0f, c.input.jumpSpeed, 0.0f);
        } else {
            // In the air: keep the fall, some steering (airSteer).
            JPH::Vec3 horizontal(current.GetX(), 0.0f, current.GetZ());
            horizontal = horizontal + (move - horizontal) * std::min(1.0f, c.input.airSteer * dt);
            v = horizontal + JPH::Vec3(0.0f, current.GetY(), 0.0f);
        }
        v += gravity * dt;
        ch.SetLinearVelocity(v);
        JPH::CharacterVirtual::ExtendedUpdateSettings eus;
        eus.mWalkStairsStepUp = JPH::Vec3(0.0f, c.desc.stepUp, 0.0f);
        eus.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
        ch.ExtendedUpdate(dt, gravity, eus, m->system.GetDefaultBroadPhaseLayerFilter(Layers::kMoving),
                          m->system.GetDefaultLayerFilter(Layers::kMoving), {}, {}, *m->temp);
        c.input.jump = false; // one jump per press
    }
    // One collision step per 1/60 s (more for bigger steps).
    const int collisionSteps = std::max(1, static_cast<int>(std::ceil(dt * 60.0f - 0.01f)));
    m->system.Update(dt, collisionSteps, m->temp.get(), m->jobs.get());
    m->stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

double RigidWorld::lastStepMs() const { return m->stepMs; }

std::vector<RigidWorld::Contact> RigidWorld::takeContacts() {
    std::lock_guard<std::mutex> lock(m->contactMutex);
    std::vector<Contact> out;
    out.swap(m->contacts);
    return out;
}

} // namespace kke
