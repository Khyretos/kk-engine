#include "kke/RigidWorld.h"

#include "ClothSystem.h"

#include "kke/ProceduralAnim.h"
#include "kke/Ragdoll.h"
#include "kke/Tyre.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cmath>

namespace kke {

namespace {

// Object layers: things that never move, and everything else; static vs
// static pairs are never tested. Cloth (soft bodies) collides with both
// and with cloth colliders (a character's inner body, clothOnly bodies),
// which nothing else sees. kQuery is never a body's layer: it's what
// rays, overlap tests and characters ask with, so they see the solid
// world and neither cloth nor cloth colliders (a curtain doesn't stop a
// ray or a player; it moves out of the way). kDebris is falling rubble
// (BodyDesc::debris): it lands on the level, other bodies and itself, but
// characters walk through it and rays don't see it, so a crumbling floor
// drops the player instead of throwing them.
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCloth = 2;
constexpr JPH::ObjectLayer kClothCollider = 3;
constexpr JPH::ObjectLayer kQuery = 4;
constexpr JPH::ObjectLayer kDebris = 5;
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
        return layer == Layers::kStatic ? BroadLayers::kStatic : BroadLayers::kMoving; // cloth and its colliders move
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
        if (layer == Layers::kStatic || layer == Layers::kClothCollider) return broad == BroadLayers::kMoving;
        return true;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a > b) std::swap(a, b);
        switch (a) {
        case Layers::kStatic: return b == Layers::kMoving || b == Layers::kCloth || b == Layers::kQuery || b == Layers::kDebris;
        case Layers::kMoving: return b == Layers::kMoving || b == Layers::kCloth || b == Layers::kQuery || b == Layers::kDebris;
        case Layers::kCloth: return b == Layers::kClothCollider;
        case Layers::kDebris: return b == Layers::kDebris;
        default: return false;
        }
    }
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
        bool manual = false; // stepped by stepCharacter() only (input replay)
        double time = 0.0;   // seconds simulated (characterTime)
        glm::vec3 drawFrom{0.0f}; // feet before the latest physics step (characterDrawPosition)
    };
    void stepCharacter(Character& c, float dt);
    std::unordered_map<CharacterId, Character> characters;
    CharacterId nextCharacter = 1;
    struct Ragdoll {
        std::vector<BodyId> bodies;
        std::vector<JPH::Ref<JPH::Constraint>> joints; // per RagdollDesc joint
        std::vector<float> mass;                         // per body
        std::vector<std::pair<int, int>> jointBodies;    // per joint: bodyA, bodyB
        // driveRagdoll's assist, applied before every step until the next drive.
        std::vector<std::pair<int, glm::mat4>> assisted; // body, target
        float assist = 0.0f;
        JPH::Ref<JPH::GroupFilterTable> filter;
    };
    std::unordered_map<RagdollId, Ragdoll> ragdolls;
    void applyAssist(Ragdoll& rd, float dt);
    RagdollId nextRagdoll = 1;
    struct Vehicle {
        BodyId body = kNoBody;
        JPH::Ref<JPH::VehicleConstraint> constraint;
        JPH::WheeledVehicleController* controller = nullptr; // owned by the constraint
        std::vector<float> grip;  // per wheel (setVehicleWheelGrip), read by the tyre callback during step()
        std::vector<float> slipLoss; // per wheel: VehicleWheelDesc::combinedSlipLoss
        float torque = 0.0f;      // VehicleDesc::maxTorque (setVehiclePower scales it)
        bool manual = false;
        // Tyres (kke/Tyre.h): heat, wear, air, what's left; the ground each
        // wheel was on last step; the friction curves' peaks; the wheel
        // settings (the constraint holds them) whose radius a flat lowers.
        std::vector<Tyre> tyres;
        std::vector<GroundGrip> ground;
        std::vector<uint32_t> groundMaterial;
        std::vector<float> peakLong, peakLat, radius;
        std::vector<JPH::WheelSettingsWV*> settings;
        float nominalLoad = 0.0f; // N per wheel with the car at rest
        float mass = 1.0f;
    };
    void updateTyres(Vehicle& v, float dt);
    std::unordered_map<uint32_t, GroundGrip> groundGrip; // setGroundGrip, by body material
    float ambient = 20.0f;
    std::unordered_map<VehicleId, Vehicle> vehicles;
    VehicleId nextVehicle = 1;
    JPH::Ref<JPH::VehicleCollisionTester> wheelTester; // shared by every vehicle
    std::mutex contactMutex;
    std::vector<Contact> contacts;
    double stepMs = 0.0;
    double simulatedTime = 0.0;
    float lastDt = 1.0f / 60.0f; // the last step (a wheel's load is its suspension impulse over it)
    std::unique_ptr<detail::ClothSystem> cloth; // made on the first addCloth (it adds a step listener)
    std::shared_ptr<ClothGpu> clothGpu;
    detail::ClothSystem& clothSystem() {
        if (!cloth) {
            cloth = std::make_unique<detail::ClothSystem>(system, Layers::kCloth, *temp, jobs.get(), settings.clothSubsteps);
            cloth->setGpu(clothGpu);
        }
        return *cloth;
    }

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
    for (auto& [id, v] : m->vehicles) {
        m->system.RemoveStepListener(v.constraint);
        m->system.RemoveConstraint(v.constraint);
    }
    m->vehicles.clear();
    m->cloth.reset();
    m->characters.clear();
    for (auto& [id, rd] : m->ragdolls)
        for (auto& c : rd.joints) m->system.RemoveConstraint(c);
    m->ragdolls.clear();
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
    // A cloth-only collider moves only when told to (nothing else can push it).
    const JPH::EMotionType bodyMotion = d.clothOnly && motion == JPH::EMotionType::Dynamic ? JPH::EMotionType::Kinematic : motion;
    const JPH::ObjectLayer layer = d.clothOnly                      ? Layers::kClothCollider
                                   : d.motion == Motion::Static      ? Layers::kStatic
                                   : d.debris                         ? Layers::kDebris
                                                                     : Layers::kMoving;
    JPH::BodyCreationSettings bcs(shape, toJR(d.position), toJ(glm::normalize(d.rotation)), bodyMotion, layer);
    bcs.mFriction = d.friction;
    bcs.mRestitution = d.restitution;
    bcs.mLinearVelocity = toJ(d.velocity);
    bcs.mAngularVelocity = toJ(d.angularVelocity);
    bcs.mUserData = d.material;
    if (bodyMotion == JPH::EMotionType::Dynamic) bcs.mMotionQuality = JPH::EMotionQuality::LinearCast; // no tunnelling for fast rocks
    if (d.mass > 0.0f && dynamic && !d.clothOnly) {
        bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = d.mass;
    }
    JPH::BodyID id = m->bodies().CreateAndAddBody(bcs, d.motion == Motion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    return id.IsInvalid() ? kNoBody : id.GetIndexAndSequenceNumber();
}

void RigidWorld::remove(BodyId body) {
    JPH::BodyID id(body);
    if (body == kNoBody || !m->bodies().IsAdded(id)) return;
    m->bodies().RemoveBody(id);
    m->bodies().DestroyBody(id);
}

// Rigid bodies only: characters' cloth colliders and the cloth itself aren't counted.
size_t RigidWorld::bodyCount() const { return m->system.GetNumBodies() - m->characters.size() - (m->cloth ? m->cloth->count() : 0); }
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
glm::vec3 RigidWorld::angularVelocity(BodyId body) const { return toG(m->bodies().GetAngularVelocity(JPH::BodyID(body))); }
void RigidWorld::setAngularVelocity(BodyId body, const glm::vec3& w) { m->bodies().SetAngularVelocity(JPH::BodyID(body), toJ(w)); }
void RigidWorld::addImpulse(BodyId body, const glm::vec3& impulse, const glm::vec3& point) {
    m->bodies().AddImpulse(JPH::BodyID(body), toJ(impulse), toJR(point));
}
void RigidWorld::addVelocity(BodyId body, const glm::vec3& dv) {
    JPH::BodyID id(body);
    if (body == kNoBody || !m->bodies().IsAdded(id)) return;
    m->bodies().AddLinearVelocity(id, toJ(dv));
}
void RigidWorld::moveKinematic(BodyId body, const glm::vec3& position, const glm::quat& rotation, float dt) {
    m->bodies().MoveKinematic(JPH::BodyID(body), toJR(position), toJ(glm::normalize(rotation)), dt);
}
void RigidWorld::setMotion(BodyId body, Motion motion) {
    JPH::BodyID id(body);
    if (body == kNoBody || !m->bodies().IsAdded(id) || motion == Motion::Static) return;
    if (m->bodies().GetMotionType(id) == JPH::EMotionType::Static) return; // static bodies stay static (Jolt needs them created movable)
    m->bodies().SetMotionType(id, motion == Motion::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
}
void RigidWorld::setTransform(BodyId body, const glm::vec3& position, const glm::quat& rotation) {
    JPH::BodyID id(body);
    if (body == kNoBody || !m->bodies().IsAdded(id)) return;
    m->bodies().SetPositionAndRotation(id, toJR(position), toJ(glm::normalize(rotation)), JPH::EActivation::Activate);
}

void RigidWorld::bodiesInBox(const glm::vec3& min, const glm::vec3& max, std::vector<BodyBox>& out) const {
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> hits;
    m->system.GetBroadPhaseQuery().CollideAABox(JPH::AABox(toJ(min), toJ(max)), hits, m->system.GetDefaultBroadPhaseLayerFilter(Layers::kQuery),
                                                m->system.GetDefaultLayerFilter(Layers::kQuery));
    for (const JPH::BodyID& id : hits.mHits) {
        JPH::BodyLockRead lock(m->system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) continue;
        const JPH::Body& b = lock.GetBody();
        const JPH::Shape* shape = b.GetShape();
        if (shape->GetSubType() == JPH::EShapeSubType::Mesh) continue;
        BodyBox box;
        box.id = id.GetIndexAndSequenceNumber();
        box.motion = b.IsStatic() ? Motion::Static : b.IsKinematic() ? Motion::Kinematic : Motion::Dynamic;
        box.rotation = toG(b.GetRotation());
        if (shape->GetSubType() == JPH::EShapeSubType::Box) {
            box.center = toG(JPH::Vec3(b.GetCenterOfMassPosition()));
            box.halfExtents = toG(static_cast<const JPH::BoxShape*>(shape)->GetHalfExtent());
        } else {
            const JPH::AABox local = shape->GetLocalBounds();
            box.center = toG(JPH::Vec3(b.GetCenterOfMassTransform() * local.GetCenter()));
            box.halfExtents = toG(local.GetExtent());
        }
        if (box.motion == Motion::Dynamic) {
            box.velocity = toG(b.GetLinearVelocity());
            box.angularVelocity = toG(b.GetAngularVelocity());
            const float inv = b.GetMotionProperties()->GetInverseMass();
            box.mass = inv > 0.0f ? 1.0f / inv : 0.0f;
        } else if (box.motion == Motion::Kinematic) {
            box.velocity = toG(b.GetLinearVelocity());
            box.angularVelocity = toG(b.GetAngularVelocity());
        }
        out.push_back(box);
    }
}

RigidWorld::RayHit RigidWorld::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
    return raycast(origin, direction, maxDistance, {});
}

namespace {
class AcceptBodies final : public JPH::BodyFilter {
public:
    explicit AcceptBodies(const std::function<bool(RigidWorld::BodyId, RigidWorld::Motion)>& accept) : m_accept(accept) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        const RigidWorld::Motion motion = body.IsStatic() ? RigidWorld::Motion::Static
                                          : body.IsKinematic() ? RigidWorld::Motion::Kinematic : RigidWorld::Motion::Dynamic;
        return m_accept(body.GetID().GetIndexAndSequenceNumber(), motion);
    }

private:
    const std::function<bool(RigidWorld::BodyId, RigidWorld::Motion)>& m_accept;
};
} // namespace

RigidWorld::RayHit RigidWorld::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                       const std::function<bool(BodyId, Motion)>& accept) const {
    RayHit out;
    glm::vec3 dir = glm::length(direction) > 1e-9f ? glm::normalize(direction) : glm::vec3(0, -1, 0);
    JPH::RRayCast ray(toJR(origin), toJ(dir * maxDistance));
    // Hit both sides of triangles, like the character does: Synty meshes
    // are often open or flipped, and a ray that slips through a back face
    // would say "nothing there" in front of a wall the capsule can't pass.
    JPH::RayCastSettings settings;
    settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> closest;
    const auto broad = m->system.GetDefaultBroadPhaseLayerFilter(Layers::kQuery);
    const auto layers = m->system.GetDefaultLayerFilter(Layers::kQuery);
    if (accept) m->system.GetNarrowPhaseQuery().CastRay(ray, settings, closest, broad, layers, AcceptBodies(accept));
    else m->system.GetNarrowPhaseQuery().CastRay(ray, settings, closest, broad, layers);
    if (!closest.HadHit()) return out;
    const JPH::RayCastResult& r = closest.mHit;
    out.hit = true;
    out.body = r.mBodyID.GetIndexAndSequenceNumber();
    out.distance = r.mFraction * maxDistance;
    out.point = origin + dir * out.distance;
    JPH::BodyLockRead lock(m->system.GetBodyLockInterface(), r.mBodyID);
    if (lock.Succeeded()) {
        out.normal = toG(lock.GetBody().GetWorldSpaceSurfaceNormal(r.mSubShapeID2, ray.GetPointOnRay(r.mFraction)));
        out.material = static_cast<uint32_t>(lock.GetBody().GetUserData());
    }
    if (glm::dot(out.normal, dir) > 0.0f) out.normal = -out.normal; // a back face: the side the ray came from
    return out;
}

namespace {
// World bounds of an oriented box (rotation + center in `t`).
void orientedBounds(const glm::mat4& t, const glm::vec3& half, glm::vec3& lo, glm::vec3& hi) {
    glm::vec3 ext(0.0f);
    for (int c = 0; c < 3; ++c) ext += glm::abs(glm::vec3(t[c])) * half[c];
    lo = glm::vec3(t[3]) - ext;
    hi = glm::vec3(t[3]) + ext;
}

glm::quat rotationOf(const glm::mat4& t) {
    glm::mat3 r(t);
    for (int c = 0; c < 3; ++c) {
        const float len = glm::length(r[c]);
        r[c] = len > 1e-12f ? r[c] / len : glm::vec3(0.0f);
    }
    return glm::normalize(glm::quat_cast(r));
}

// Any unit vector perpendicular to `v` (unit).
glm::vec3 perpendicular(const glm::vec3& v) {
    glm::vec3 p = glm::cross(v, std::fabs(v.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
    return glm::normalize(p);
}

// Shortest rotation taking unit `a` to unit `b`.
glm::quat fromTo(const glm::vec3& a, const glm::vec3& b) {
    const float d = glm::dot(a, b);
    if (d < -0.9999f) return glm::angleAxis(glm::pi<float>(), perpendicular(a));
    const glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::vec3 unitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-6f ? v / len : fallback;
}
} // namespace

RigidWorld::RagdollId RigidWorld::addRagdoll(const RagdollDesc& desc, const glm::vec3& initialVelocity) {
    const int n = static_cast<int>(desc.bodies.size());
    if (n == 0) return 0;
    for (const RagdollJoint& j : desc.joints)
        if (j.bodyA < 0 || j.bodyB < 0 || j.bodyA >= n || j.bodyB >= n || j.bodyA == j.bodyB) return 0;
    const RagdollId id = m->nextRagdoll++;
    Impl::Ragdoll rd;
    rd.filter = new JPH::GroupFilterTable(static_cast<uint32_t>(n));
    for (const RagdollJoint& j : desc.joints) rd.filter->DisableCollision(static_cast<JPH::CollisionGroup::SubGroupID>(j.bodyA),
                                                                         static_cast<JPH::CollisionGroup::SubGroupID>(j.bodyB));
    // Limbs that already overlap (an arm resting against the torso) would
    // be pushed apart violently: they don't collide either.
    std::vector<glm::vec3> lo(n), hi(n);
    for (int i = 0; i < n; ++i) orientedBounds(desc.bodies[i].transform, desc.bodies[i].halfExtents * 0.9f, lo[i], hi[i]);
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b)
            if (glm::all(glm::lessThan(lo[a], hi[b])) && glm::all(glm::lessThan(lo[b], hi[a])))
                rd.filter->DisableCollision(static_cast<JPH::CollisionGroup::SubGroupID>(a), static_cast<JPH::CollisionGroup::SubGroupID>(b));

    auto fail = [&](const char*) {
        for (auto& c : rd.joints) m->system.RemoveConstraint(c);
        for (BodyId b : rd.bodies) remove(b);
        return RagdollId(0);
    };
    for (int i = 0; i < n; ++i) {
        const RagdollBody& b = desc.bodies[i];
        JPH::BoxShapeSettings box(toJ(glm::max(b.halfExtents, glm::vec3(0.01f))),
                                  std::min(0.05f, glm::min(glm::min(b.halfExtents.x, b.halfExtents.y), b.halfExtents.z) * 0.5f));
        JPH::ShapeSettings::ShapeResult shape = box.Create();
        if (shape.HasError()) return fail("shape");
        JPH::BodyCreationSettings bcs(shape.Get(), toJR(glm::vec3(b.transform[3])), toJ(rotationOf(b.transform)), JPH::EMotionType::Dynamic,
                                      Layers::kMoving);
        bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = std::max(0.1f, b.mass);
        bcs.mLinearVelocity = toJ(initialVelocity);
        bcs.mFriction = 0.7f;
        bcs.mRestitution = 0.05f;
        bcs.mCollisionGroup = JPH::CollisionGroup(rd.filter, static_cast<JPH::CollisionGroup::GroupID>(id),
                                                  static_cast<JPH::CollisionGroup::SubGroupID>(i));
        bcs.mMotionQuality = JPH::EMotionQuality::LinearCast; // thrown limbs don't tunnel through thin walls
        JPH::BodyID body = m->bodies().CreateAndAddBody(bcs, JPH::EActivation::Activate);
        if (body.IsInvalid()) return fail("body");
        rd.bodies.push_back(body.GetIndexAndSequenceNumber());
        rd.mass.push_back(std::max(0.1f, b.mass));
    }
    for (const RagdollJoint& j : desc.joints) {
        const glm::vec3 centerB(desc.bodies[j.bodyB].transform[3]);
        const glm::vec3 along = unitOr(centerB - j.anchor, glm::vec3(0, -1, 0));
        // Muscle tone: auto scales with the lighter body, so small animals
        // aren't stiff and big ones aren't floppy.
        const float friction = j.frictionTorque >= 0.0f
                                   ? j.frictionTorque
                                   : 0.5f + 0.4f * std::min(desc.bodies[j.bodyA].mass, desc.bodies[j.bodyB].mass);
        JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
        if (j.hinge) {
            JPH::HingeConstraintSettings* h = new JPH::HingeConstraintSettings;
            settings = h;
            const glm::vec3 axis = unitOr(j.hingeAxis, glm::vec3(1, 0, 0));
            glm::vec3 normal = along - axis * glm::dot(along, axis);
            normal = glm::length(normal) > 1e-4f ? glm::normalize(normal) : perpendicular(axis);
            h->mSpace = JPH::EConstraintSpace::WorldSpace;
            h->mPoint1 = h->mPoint2 = toJR(j.anchor);
            h->mHingeAxis1 = h->mHingeAxis2 = toJ(axis);
            h->mNormalAxis1 = h->mNormalAxis2 = toJ(normal);
            if (j.limited) {
                const float lo = glm::radians(std::min(j.hingeMinDegrees, j.hingeMaxDegrees));
                const float hi = glm::radians(std::max(j.hingeMinDegrees, j.hingeMaxDegrees));
                h->mLimitsMin = std::clamp(lo, -glm::pi<float>(), 0.0f);
                h->mLimitsMax = std::clamp(hi, 0.0f, glm::pi<float>());
            } else {
                h->mLimitsMin = -glm::pi<float>();
                h->mLimitsMax = glm::pi<float>();
            }
            h->mMaxFrictionTorque = friction;
        } else {
            JPH::SwingTwistConstraintSettings* st = new JPH::SwingTwistConstraintSettings;
            settings = st;
            const glm::vec3 center = unitOr(j.swingAxis, along);
            // Plane axis = what the main swing turns about (a hip's side
            // axis). Jolt's constraint space is X = twist, Y = plane x twist
            // (the "normal"), Z = plane: mPlaneHalfConeAngle limits turning
            // about Y (swinging within the plane), mNormalHalfConeAngle
            // turning about Z = the plane axis, i.e. our main swing.
            const glm::vec3 bend = j.swingBendAxis - center * glm::dot(j.swingBendAxis, center);
            const glm::vec3 plane1 = glm::length(bend) > 1e-3f ? glm::normalize(bend) : perpendicular(center);
            // Same frame on B, turned the way B already points.
            const glm::vec3 plane2 = fromTo(center, along) * plane1;
            st->mSpace = JPH::EConstraintSpace::WorldSpace;
            st->mPosition1 = st->mPosition2 = toJR(j.anchor);
            st->mTwistAxis1 = toJ(center);
            st->mPlaneAxis1 = toJ(plane1);
            st->mTwistAxis2 = toJ(along);
            st->mPlaneAxis2 = toJ(glm::normalize(plane2 - along * glm::dot(plane2, along)));
            const float limitMax = j.limited ? 179.0f : 180.0f;
            auto angle = [&](float degrees) { return glm::radians(j.limited ? std::clamp(degrees, 0.0f, limitMax) : limitMax); };
            st->mNormalHalfConeAngle = angle(j.swingDegrees);
            st->mPlaneHalfConeAngle = angle(j.swingSideDegrees < 0.0f ? j.swingDegrees : j.swingSideDegrees);
            const float twist = angle(j.twistDegrees);
            st->mTwistMinAngle = -twist;
            st->mTwistMaxAngle = twist;
            st->mMaxFrictionTorque = friction;
        }
        // Heavy bodies on light limbs (a horse's chest on its shins) need
        // more solver passes, or limits give on impact.
        settings->mNumVelocityStepsOverride = 20;
        settings->mNumPositionStepsOverride = 8;
        JPH::TwoBodyConstraint* c = m->bodies().CreateConstraint(settings, JPH::BodyID(rd.bodies[j.bodyA]), JPH::BodyID(rd.bodies[j.bodyB]));
        if (!c) return fail("joint");
        rd.joints.emplace_back(c);
        rd.jointBodies.emplace_back(j.bodyA, j.bodyB);
        m->system.AddConstraint(c);
    }
    m->ragdolls.emplace(id, std::move(rd));
    return id;
}

void RigidWorld::removeRagdoll(RagdollId id) {
    auto it = m->ragdolls.find(id);
    if (it == m->ragdolls.end()) return;
    for (auto& c : it->second.joints) m->system.RemoveConstraint(c);
    for (BodyId b : it->second.bodies) remove(b);
    m->ragdolls.erase(it);
}

bool RigidWorld::ragdollTransforms(RagdollId id, std::vector<glm::mat4>& out) const {
    auto it = m->ragdolls.find(id);
    if (it == m->ragdolls.end()) return false;
    out.resize(it->second.bodies.size());
    for (size_t i = 0; i < out.size(); ++i) out[i] = transform(it->second.bodies[i]);
    return true;
}

std::vector<RigidWorld::BodyId> RigidWorld::ragdollBodies(RagdollId id) const {
    auto it = m->ragdolls.find(id);
    return it == m->ragdolls.end() ? std::vector<BodyId>{} : it->second.bodies;
}

size_t RigidWorld::ragdollCount() const { return m->ragdolls.size(); }

float RigidWorld::ragdollHingeAngle(RagdollId id, int joint) const {
    auto it = m->ragdolls.find(id);
    if (it == m->ragdolls.end() || joint < 0 || joint >= static_cast<int>(it->second.joints.size())) return 0.0f;
    const JPH::Constraint* c = it->second.joints[joint].GetPtr();
    if (c->GetSubType() != JPH::EConstraintSubType::Hinge) return 0.0f;
    return glm::degrees(static_cast<const JPH::HingeConstraint*>(c)->GetCurrentAngle());
}

bool RigidWorld::driveRagdoll(RagdollId id, const RagdollDrive& drive) {
    auto it = m->ragdolls.find(id);
    if (it == m->ragdolls.end()) return false;
    Impl::Ragdoll& rd = it->second;
    const size_t nb = rd.bodies.size(), nj = rd.joints.size();
    if (drive.targets.size() != nb || drive.jointStrength.size() != nj) return false;
    bool anyMotor = false;
    for (size_t j = 0; j < nj; ++j) {
        const float strength = std::clamp(drive.jointStrength[j], 0.0f, 1.0f);
        const auto [a, b] = rd.jointBodies[j];
        // Body B relative to body A, as the animation has them (Jolt's
        // SetTargetOrientationBS: R_B = R_A * q), like JPH::Ragdoll does.
        const glm::quat q = glm::normalize(glm::inverse(rotationOf(drive.targets[a])) * rotationOf(drive.targets[b]));
        const float torque = drive.torquePerKg * std::max(rd.mass[a], rd.mass[b]) * strength;
        const JPH::EMotorState state = strength > 1e-3f ? JPH::EMotorState::Position : JPH::EMotorState::Off;
        anyMotor = anyMotor || state != JPH::EMotorState::Off;
        JPH::Constraint* c = rd.joints[j].GetPtr();
        auto setMotor = [&](JPH::MotorSettings& ms) {
            ms.mSpringSettings.mFrequency = std::max(0.1f, drive.frequency);
            ms.mSpringSettings.mDamping = 1.0f;
            ms.SetTorqueLimit(torque);
        };
        if (c->GetSubType() == JPH::EConstraintSubType::SwingTwist) {
            auto* st = static_cast<JPH::SwingTwistConstraint*>(c);
            setMotor(st->GetSwingMotorSettings());
            setMotor(st->GetTwistMotorSettings());
            st->SetSwingMotorState(state);
            st->SetTwistMotorState(state);
            if (state != JPH::EMotorState::Off) st->SetTargetOrientationBS(toJ(q));
        } else if (c->GetSubType() == JPH::EConstraintSubType::Hinge) {
            auto* h = static_cast<JPH::HingeConstraint*>(c);
            setMotor(h->GetMotorSettings());
            h->SetMotorState(state);
            if (state != JPH::EMotorState::Off) h->SetTargetOrientationBS(toJ(q));
        }
    }
    // The assist is applied before every physics step (RigidWorld::step),
    // so it pulls as hard at 20 fps as at 144.
    const float assist = std::clamp(drive.assist, 0.0f, 1.0f);
    rd.assist = assist;
    rd.assisted.clear();
    if (assist > 0.0f)
        for (int b : drive.assistBodies)
            if (b >= 0 && static_cast<size_t>(b) < nb) rd.assisted.emplace_back(b, drive.targets[b]);
    if (anyMotor || assist > 0.0f)
        for (BodyId b : rd.bodies) m->bodies().ActivateBody(JPH::BodyID(b));
    return true;
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
    // An inner body only cloth collides with: walking into a curtain
    // pushes it aside (nothing else sees it, see Layers).
    s.mInnerBodyShape = s.mShape;
    s.mInnerBodyLayer = Layers::kClothCollider;
    Impl::Character c;
    c.ch = new JPH::CharacterVirtual(&s, toJR(d.position), JPH::Quat::sIdentity(), 0, &m->system);
    c.desc = d;
    c.drawFrom = d.position;
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
    if (!c.ch->SetShape(characterShape(height, c.desc.radius), tolerance, m->system.GetDefaultBroadPhaseLayerFilter(Layers::kQuery),
                        m->system.GetDefaultLayerFilter(Layers::kQuery), {}, {}, *m->temp))
        return false;
    c.ch->SetInnerBodyShape(c.ch->GetShape());
    c.desc.height = height;
    return true;
}

float RigidWorld::characterHeight(CharacterId id) const {
    auto it = m->characters.find(id);
    return it == m->characters.end() ? 0.0f : it->second.desc.height;
}

float RigidWorld::characterRadius(CharacterId id) const {
    auto it = m->characters.find(id);
    return it == m->characters.end() ? 0.0f : it->second.desc.radius;
}

std::vector<RigidWorld::CharacterId> RigidWorld::characterIds() const {
    std::vector<CharacterId> ids;
    ids.reserve(m->characters.size());
    for (const auto& [id, c] : m->characters) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

void RigidWorld::setCharacterInput(CharacterId id, const CharacterInput& input) {
    auto it = m->characters.find(id);
    if (it == m->characters.end()) return;
    CharacterInput& cur = it->second.input;
    // A jump not yet taken by a step stays (see CharacterInput::jump).
    const bool pending = cur.jump && !input.jump;
    const float speed = cur.jumpSpeed;
    const bool inAir = cur.jumpInAir;
    cur = input;
    if (pending) {
        cur.jump = true;
        cur.jumpSpeed = speed;
        cur.jumpInAir = inAir;
    }
}

glm::vec3 RigidWorld::characterPosition(CharacterId id) const {
    auto it = m->characters.find(id);
    if (it == m->characters.end()) return glm::vec3(0.0f);
    JPH::RVec3 p = it->second.ch->GetPosition();
    return glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
}

glm::vec3 RigidWorld::characterDrawPosition(CharacterId id, float alpha) const {
    auto it = m->characters.find(id);
    if (it == m->characters.end()) return glm::vec3(0.0f);
    const JPH::RVec3 p = it->second.ch->GetPosition();
    const glm::vec3 now(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
    return glm::mix(it->second.drawFrom, now, std::clamp(alpha, 0.0f, 1.0f));
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
    it->second.drawFrom = feet; // no sweep from where it was
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
    if (it == m->characters.end()) return;
    const JPH::RVec3 was = it->second.ch->GetPosition();
    it->second.ch->SetPosition(toJR(feet));
    // Moved as it is (a snap to the floor, a kinematic climb): the drawn
    // position moves with it, keeping whatever step it was between.
    it->second.drawFrom += feet - glm::vec3(float(was.GetX()), float(was.GetY()), float(was.GetZ()));
}

void RigidWorld::setCharacterVelocity(CharacterId id, const glm::vec3& velocity) {
    auto it = m->characters.find(id);
    if (it != m->characters.end()) it->second.ch->SetLinearVelocity(toJ(velocity));
}

void RigidWorld::setCharacterManual(CharacterId id, bool manual) {
    auto it = m->characters.find(id);
    if (it != m->characters.end()) it->second.manual = manual;
}

bool RigidWorld::characterManual(CharacterId id) const {
    auto it = m->characters.find(id);
    return it != m->characters.end() && it->second.manual;
}

void RigidWorld::stepCharacter(CharacterId id, float dt) {
    auto it = m->characters.find(id);
    if (it == m->characters.end() || dt <= 0.0f) return;
    m->stepCharacter(it->second, dt);
    // Stepped by the caller (on its own clock): drawn where it is.
    const JPH::RVec3 p = it->second.ch->GetPosition();
    it->second.drawFrom = glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
}

double RigidWorld::characterTime(CharacterId id) const {
    auto it = m->characters.find(id);
    return it == m->characters.end() ? 0.0 : it->second.time;
}

RigidWorld::CharacterState RigidWorld::characterState(CharacterId id) const {
    CharacterState out;
    auto it = m->characters.find(id);
    if (it == m->characters.end()) return out;
    const Impl::Character& c = it->second;
    JPH::StateRecorderImpl rec;
    c.ch->SaveState(rec);
    out.jolt = rec.GetData();
    out.input = c.input;
    out.height = c.desc.height;
    out.kinematic = c.kinematic;
    out.time = c.time;
    return out;
}

void RigidWorld::setCharacterState(CharacterId id, const CharacterState& state) {
    auto it = m->characters.find(id);
    if (it == m->characters.end() || state.jolt.empty()) return;
    Impl::Character& c = it->second;
    // The capsule first (a crouch), with no room check: it was this size there.
    if (std::abs(state.height - c.desc.height) >= 1e-4f) {
        c.ch->SetShape(characterShape(state.height, c.desc.radius), FLT_MAX, m->system.GetDefaultBroadPhaseLayerFilter(Layers::kQuery),
                       m->system.GetDefaultLayerFilter(Layers::kQuery), {}, {}, *m->temp);
        c.ch->SetInnerBodyShape(c.ch->GetShape());
        c.desc.height = state.height;
    }
    JPH::StateRecorderImpl rec;
    rec.WriteBytes(state.jolt.data(), state.jolt.size());
    rec.Rewind();
    c.ch->RestoreState(rec);
    c.input = state.input;
    c.kinematic = state.kinematic;
    c.time = state.time;
}

bool RigidWorld::capsuleFits(const glm::vec3& feet, float height, float radius) const {
    JPH::RefConst<JPH::Shape> shape = characterShape(height, radius);
    // CollideShape places the shape by its centre of mass, not its origin.
    JPH::CollideShapeSettings settings;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
    settings.mMaxSeparationDistance = 0.0f;
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> hit;
    m->system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(toJR(feet) + shape->GetCenterOfMass()), settings, JPH::RVec3::sZero(),
                                                  hit, m->system.GetDefaultBroadPhaseLayerFilter(Layers::kQuery),
                                                  m->system.GetDefaultLayerFilter(Layers::kQuery));
    return !hit.HadHit();
}

void RigidWorld::Impl::stepCharacter(Character& c, float dt) {
    c.time += dt;
    if (c.kinematic) {
        c.input.jump = false; // placed by the caller (vaults, climbs): no jump left over for later
        return;
    }
    const JPH::Vec3 gravity = system.GetGravity();
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
        // A jump the caller allows off the ground (coyote time) replaces
        // the fall; any other jump in the air does nothing.
        const float vy = c.input.jump && c.input.jumpInAir ? std::max(current.GetY(), 0.0f) + c.input.jumpSpeed : current.GetY();
        v = horizontal + JPH::Vec3(0.0f, vy, 0.0f);
    }
    // Standing on walkable ground, only gravity's push *into* the
    // ground applies: its slope-parallel part would make an idle
    // character creep downhill (~5 cm/s on a 24 degree ramp).
    if (grounded && !c.input.jump) {
        const JPH::Vec3 n = ch.GetGroundNormal();
        v += n * n.Dot(gravity) * dt;
    } else {
        v += gravity * dt;
    }
    ch.SetLinearVelocity(v);
    JPH::CharacterVirtual::ExtendedUpdateSettings eus;
    eus.mWalkStairsStepUp = JPH::Vec3(0.0f, c.desc.stepUp, 0.0f);
    eus.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
    ch.ExtendedUpdate(dt, gravity, eus, system.GetDefaultBroadPhaseLayerFilter(Layers::kQuery),
                      system.GetDefaultLayerFilter(Layers::kQuery), {}, {}, *temp);
    c.input.jump = false; // one jump per press
}

// Each assisted body's velocity pulled toward what reaches its target in
// `reach` seconds (what keeps a hit character standing), plus what
// gravity takes away in a step, so it holds its height instead of sagging.
void RigidWorld::Impl::applyAssist(Ragdoll& rd, float dt) {
    if (rd.assist <= 0.0f) return;
    constexpr float reach = 0.15f, share = 0.35f;
    const glm::vec3 gravity = toG(system.GetGravity());
    for (const auto& [index, target] : rd.assisted) {
        const JPH::BodyID body(rd.bodies[index]);
        JPH::RVec3 pos;
        JPH::Quat rot;
        bodies().GetPositionAndRotation(body, pos, rot);
        const glm::vec3 p(float(pos.GetX()), float(pos.GetY()), float(pos.GetZ()));
        const glm::quat r(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        const glm::vec3 wantV = (glm::vec3(target[3]) - p) / reach - gravity * dt / share;
        glm::quat err = glm::normalize(rotationOf(target) * glm::inverse(r));
        if (err.w < 0.0f) err = -err;
        const float angle = 2.0f * std::acos(std::clamp(err.w, -1.0f, 1.0f));
        const glm::vec3 axis(err.x, err.y, err.z);
        const glm::vec3 wantW = glm::length(axis) > 1e-6f ? glm::normalize(axis) * (angle / reach) : glm::vec3(0.0f);
        const glm::vec3 v = toG(bodies().GetLinearVelocity(body)), w = toG(bodies().GetAngularVelocity(body));
        bodies().SetLinearAndAngularVelocity(body, toJ(glm::mix(v, wantV, rd.assist * share)), toJ(glm::mix(w, wantW, rd.assist * share)));
    }
}

void RigidWorld::step(float dt) {
    if (dt <= 0.0f) return;
    auto t0 = std::chrono::steady_clock::now();
    for (auto& [id, rd] : m->ragdolls) m->applyAssist(rd, dt);
    for (auto& [id, c] : m->characters) {
        if (c.manual) continue;
        const JPH::RVec3 p = c.ch->GetPosition();
        c.drawFrom = glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
        m->stepCharacter(c, dt);
    }
    // One collision step per 1/60 s (more for bigger steps), cut in
    // sub-steps while cloth is being kept from going through cloth
    // (ClothSystem::beginStep).
    int collisionSteps = std::max(1, static_cast<int>(std::ceil(dt * 60.0f - 0.01f)));
    if (m->cloth) collisionSteps *= m->cloth->beginStep();
    m->system.Update(dt, collisionSteps, m->temp.get(), m->jobs.get());
    if (m->cloth) m->cloth->endStep();
    m->lastDt = dt;
    for (auto& [id, v] : m->vehicles) m->updateTyres(v, dt);
    m->simulatedTime += dt;
    m->stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---- vehicles (kke/Vehicle.h)

namespace {

glm::mat4 toG(JPH::RMat44Arg t) {
    glm::mat4 out(1.0f);
    for (int c = 0; c < 3; ++c) out[c] = glm::vec4(toG(t.GetColumn3(c)), 0.0f);
    const JPH::RVec3 p = t.GetTranslation();
    out[3] = glm::vec4(float(p.GetX()), float(p.GetY()), float(p.GetZ()), 1.0f);
    return out;
}

} // namespace

RigidWorld::VehicleId RigidWorld::addVehicle(const VehicleDesc& d) {
    if (d.wheels.empty()) return 0;
    // The chassis: a box or a hull, its centre of mass moved (low: a car
    // that corners instead of rolling over).
    JPH::ShapeRefC shape;
    if (d.hull.size() >= 4) {
        JPH::Array<JPH::Vec3> pts;
        pts.reserve(d.hull.size());
        for (const glm::vec3& p : d.hull) pts.push_back(toJ(p));
        JPH::ShapeSettings::ShapeResult r = JPH::ConvexHullShapeSettings(pts, 0.02f).Create();
        if (!r.HasError()) shape = r.Get();
    }
    if (!shape) {
        JPH::RotatedTranslatedShapeSettings box(toJ(d.boxOffset), JPH::Quat::sIdentity(),
                                                new JPH::BoxShapeSettings(toJ(glm::max(d.halfExtents, glm::vec3(0.05f))), 0.05f));
        JPH::ShapeSettings::ShapeResult r = box.Create();
        if (r.HasError()) return 0;
        shape = r.Get();
    }
    JPH::ShapeSettings::ShapeResult offset = JPH::OffsetCenterOfMassShapeSettings(toJ(d.centerOfMassOffset), shape).Create();
    if (offset.HasError()) return 0;
    JPH::BodyCreationSettings bcs(offset.Get(), toJR(d.position), toJ(glm::normalize(d.rotation)), JPH::EMotionType::Dynamic, Layers::kMoving);
    bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    bcs.mMassPropertiesOverride.mMass = std::max(1.0f, d.mass);
    bcs.mFriction = d.friction;
    bcs.mRestitution = d.restitution;
    bcs.mUserData = d.material;
    bcs.mMotionQuality = JPH::EMotionQuality::LinearCast; // 60 m/s into a wall: no tunnelling
    JPH::Body* body = m->bodies().CreateBody(bcs);
    if (!body) return 0;
    m->bodies().AddBody(body->GetID(), JPH::EActivation::Activate);

    JPH::VehicleConstraintSettings vcs;
    vcs.mUp = JPH::Vec3::sAxisY();
    vcs.mForward = JPH::Vec3::sAxisZ();
    std::vector<JPH::WheelSettingsWV*> wheelSettings;
    for (const VehicleWheelDesc& w : d.wheels) {
        JPH::WheelSettingsWV* ws = new JPH::WheelSettingsWV;
        wheelSettings.push_back(ws);
        ws->mPosition = toJ(w.position);
        ws->mSuspensionDirection = JPH::Vec3(0.0f, -1.0f, 0.0f);
        ws->mRadius = std::max(0.05f, w.radius);
        ws->mWidth = std::max(0.02f, w.width);
        ws->mSuspensionMinLength = std::max(0.0f, w.suspensionMin);
        ws->mSuspensionMaxLength = std::max(ws->mSuspensionMinLength + 0.01f, w.suspensionMax);
        ws->mSuspensionSpring.mFrequency = w.suspensionFrequency;
        ws->mSuspensionSpring.mDamping = w.suspensionDamping;
        ws->mMaxSteerAngle = glm::radians(w.maxSteerDegrees);
        ws->mMaxBrakeTorque = w.maxBrakeTorque;
        ws->mMaxHandBrakeTorque = w.maxHandBrakeTorque;
        const float slide = glm::clamp(w.slideGrip, 0.05f, 1.0f);
        ws->mLongitudinalFriction.Clear();
        ws->mLongitudinalFriction.Reserve(4);
        ws->mLongitudinalFriction.AddPoint(0.0f, 0.0f);
        ws->mLongitudinalFriction.AddPoint(0.06f, w.longitudinalGrip);
        ws->mLongitudinalFriction.AddPoint(0.2f, w.longitudinalGrip * (0.5f + 0.5f * slide));
        ws->mLongitudinalFriction.AddPoint(1.0f, w.longitudinalGrip * slide);
        ws->mLateralFriction.Clear();
        ws->mLateralFriction.Reserve(4);
        ws->mLateralFriction.AddPoint(0.0f, 0.0f);
        ws->mLateralFriction.AddPoint(3.0f, w.lateralGrip);
        ws->mLateralFriction.AddPoint(20.0f, w.lateralGrip * (0.5f + 0.5f * slide));
        ws->mLateralFriction.AddPoint(90.0f, w.lateralGrip * slide);
        vcs.mWheels.push_back(ws);
    }
    const int pairs = static_cast<int>(d.wheels.size()) / 2;
    if (d.antiRollStiffness > 0.0f)
        for (int a = 0; a < pairs; ++a) {
            JPH::VehicleAntiRollBar bar;
            bar.mLeftWheel = 2 * a;
            bar.mRightWheel = 2 * a + 1;
            bar.mStiffness = d.antiRollStiffness;
            vcs.mAntiRollBars.push_back(bar);
        }
    JPH::WheeledVehicleControllerSettings* cs = new JPH::WheeledVehicleControllerSettings;
    cs->mEngine.mMaxTorque = d.maxTorque;
    cs->mEngine.mMinRPM = d.minRpm;
    cs->mEngine.mMaxRPM = std::max(d.minRpm + 100.0f, d.maxRpm);
    cs->mTransmission.mMode = d.manualGearbox ? JPH::ETransmissionMode::Manual : JPH::ETransmissionMode::Auto;
    cs->mTransmission.mGearRatios.clear();
    for (float g : d.gearRatios) cs->mTransmission.mGearRatios.push_back(g);
    if (cs->mTransmission.mGearRatios.empty()) cs->mTransmission.mGearRatios.push_back(1.0f);
    cs->mTransmission.mReverseGearRatios.clear();
    cs->mTransmission.mReverseGearRatios.push_back(d.reverseRatio);
    cs->mTransmission.mShiftUpRPM = d.shiftUpRpm;
    cs->mTransmission.mShiftDownRPM = d.shiftDownRpm;
    cs->mTransmission.mSwitchTime = d.gearSwitchSeconds;
    cs->mTransmission.mClutchReleaseTime = d.gearSwitchSeconds * 0.6f;
    cs->mTransmission.mSwitchLatency = d.gearSwitchSeconds;
    std::vector<int> driven = d.drivenAxles;
    if (driven.empty()) driven.push_back(std::max(0, pairs - 1));
    for (int a : driven) {
        if (a < 0 || a >= pairs) continue;
        JPH::VehicleDifferentialSettings diff;
        diff.mLeftWheel = 2 * a;
        diff.mRightWheel = 2 * a + 1;
        diff.mDifferentialRatio = d.differentialRatio;
        diff.mLimitedSlipRatio = d.limitedSlipRatio;
        cs->mDifferentials.push_back(diff);
    }
    for (JPH::VehicleDifferentialSettings& diff : cs->mDifferentials)
        diff.mEngineTorqueRatio = 1.0f / static_cast<float>(cs->mDifferentials.size());
    vcs.mController = cs;

    const VehicleId id = m->nextVehicle++;
    Impl::Vehicle& v = m->vehicles[id];
    v.body = body->GetID().GetIndexAndSequenceNumber();
    v.constraint = new JPH::VehicleConstraint(*body, vcs);
    v.controller = static_cast<JPH::WheeledVehicleController*>(v.constraint->GetController());
    v.grip.assign(d.wheels.size(), 1.0f);
    v.slipLoss.clear();
    for (const VehicleWheelDesc& w : d.wheels) v.slipLoss.push_back(glm::clamp(w.combinedSlipLoss, 0.0f, 0.95f));
    v.torque = d.maxTorque;
    v.manual = d.manualGearbox;
    v.settings = wheelSettings;
    for (const VehicleWheelDesc& w : d.wheels) {
        v.tyres.emplace_back(w.tyre, m->ambient);
        v.peakLong.push_back(w.longitudinalGrip);
        v.peakLat.push_back(w.lateralGrip);
        v.radius.push_back(std::max(0.05f, w.radius));
    }
    v.ground.assign(d.wheels.size(), GroundGrip::tarmac());
    v.groundMaterial.assign(d.wheels.size(), 0);
    v.mass = std::max(1.0f, d.mass);
    v.nominalLoad = v.mass * 9.81f / static_cast<float>(d.wheels.size());
    // Cylinders (not rays): the tyre's width meets kerbs and bumps.
    // kQuery sees the solid world and other vehicles, not cloth.
    if (!m->wheelTester) m->wheelTester = new JPH::VehicleCollisionTesterCastCylinder(Layers::kQuery);
    v.constraint->SetVehicleCollisionTester(m->wheelTester);
    const Impl::Vehicle* vp = &v; // stable: unordered_map nodes don't move
    v.controller->SetTireMaxImpulseCallback([vp](JPH::uint wheel, float& outLongitudinal, float& outLateral, float suspensionImpulse,
                                                 float longitudinalFriction, float lateralFriction, float longitudinalSlip, float lateralSlip, float deltaTime) {
        if (wheel >= vp->grip.size()) {
            outLongitudinal = longitudinalFriction * suspensionImpulse;
            outLateral = lateralFriction * suspensionImpulse;
            return;
        }
        const Tyre& tyre = vp->tyres[wheel];
        const GroundGrip& ground = vp->ground[wheel];
        // The grip now: the game's scale, the tyre's heat, air and wear,
        // the ground, and the load (twice the weight, less than twice the grip).
        const float load = deltaTime > 0.0f ? suspensionImpulse / deltaTime : 0.0f;
        const float g = vp->grip[wheel] * tyre.grip() * ground.grip * tyre.loadFactor(load, vp->nominalLoad);
        // Loose ground: a sliding tyre keeps most of its peak (it digs in).
        if (ground.loose > 0.0f && tyre.condition() == TyreCondition::Inflated) {
            if (std::fabs(longitudinalSlip) > 0.06f) longitudinalFriction += ground.loose * std::max(0.0f, vp->peakLong[wheel] - longitudinalFriction);
            if (std::fabs(lateralSlip) > glm::radians(3.0f)) lateralFriction += ground.loose * std::max(0.0f, vp->peakLat[wheel] - lateralFriction);
        }
        // Past the grip peak (~10% slip) sideways grip fades, all the loss
        // by 70% (a locked or spinning wheel).
        const float loss = vp->slipLoss[wheel];
        const float spin = glm::clamp((std::fabs(longitudinalSlip) - 0.1f) / 0.6f, 0.0f, 1.0f);
        outLongitudinal = longitudinalFriction * suspensionImpulse * g;
        outLateral = lateralFriction * suspensionImpulse * g * (1.0f - loss * spin);
    });
    m->system.AddConstraint(v.constraint);
    m->system.AddStepListener(v.constraint);
    return id;
}

// After each step: what every tyre did (its load, the forces the ground
// gave and how fast the tread slid over it) heats, wears and deflates it;
// the ground under it sets the next step's grip; rolling resistance (the
// ground's, more on a flat) slows the car; a flat or missing tyre lowers
// the wheel.
void RigidWorld::Impl::updateTyres(Vehicle& v, float dt) {
    const JPH::BodyID body(v.body);
    for (size_t i = 0; i < v.tyres.size(); ++i) {
        const JPH::WheelWV& w = *static_cast<const JPH::WheelWV*>(v.constraint->GetWheel(static_cast<JPH::uint>(i)));
        Tyre& tyre = v.tyres[i];
        Tyre::Step s;
        s.dt = dt;
        if (w.HasContact()) {
            const JPH::BodyID groundId = w.GetContactBodyID();
            const uint32_t material = static_cast<uint32_t>(bodies().GetUserData(groundId));
            v.groundMaterial[i] = material;
            const auto g = groundGrip.find(material);
            v.ground[i] = g == groundGrip.end() ? GroundGrip::tarmac() : g->second;
            const JPH::Vec3 rel = bodies().GetPointVelocity(body, w.GetContactPosition()) - w.GetContactPointVelocity();
            const float vLong = rel.Dot(w.GetContactLongitudinal());
            const float vLat = rel.Dot(w.GetContactLateral());
            s.load = std::max(0.0f, w.GetSuspensionLambda() / dt);
            s.speed = rel.Length();
            s.longitudinalForce = w.GetLongitudinalLambda() / dt;
            s.lateralForce = w.GetLateralLambda() / dt;
            s.longitudinalSlipSpeed = std::fabs(w.GetAngularVelocity() * w.GetSettings()->mRadius - vLong);
            s.lateralSlipSpeed = std::fabs(vLat);
            s.groundHeat = v.ground[i].heat;
            // Rolling resistance against the way it rolls, never more than
            // stops it (a quarter of its speed per step at most).
            if (std::fabs(vLong) > 0.3f) {
                const float force = v.ground[i].rolling * tyre.dragScale() * s.load;
                const float impulse = std::min(force * dt, 0.25f * std::fabs(vLong) * v.mass / static_cast<float>(v.tyres.size()));
                bodies().AddImpulse(body, w.GetContactLongitudinal() * (vLong > 0.0f ? -impulse : impulse), w.GetContactPosition());
            }
        }
        tyre.update(s);
        const float radius = v.radius[i] * tyre.radiusScale();
        if (std::fabs(v.settings[i]->mRadius - radius) > 0.001f) v.settings[i]->mRadius = radius;
    }
}

void RigidWorld::setGroundGrip(uint32_t material, const GroundGrip& grip) { m->groundGrip[material] = grip; }

void RigidWorld::setAmbientTemperature(float celsius) {
    m->ambient = celsius;
    for (auto& [id, v] : m->vehicles)
        for (Tyre& t : v.tyres) t.setAmbient(celsius);
}

void RigidWorld::setVehicleTyre(VehicleId id, int wheel, TyreCondition condition) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end() || wheel < 0 || wheel >= static_cast<int>(it->second.tyres.size())) return;
    it->second.tyres[static_cast<size_t>(wheel)].setCondition(condition);
    m->bodies().ActivateBody(JPH::BodyID(it->second.body));
}

void RigidWorld::punctureVehicleTyre(VehicleId id, int wheel, float leak) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end() || wheel < 0 || wheel >= static_cast<int>(it->second.tyres.size())) return;
    it->second.tyres[static_cast<size_t>(wheel)].puncture(leak);
}

void RigidWorld::setVehicleTyreTemperature(VehicleId id, int wheel, float surface, float core) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end()) return;
    for (size_t i = 0; i < it->second.tyres.size(); ++i)
        if (wheel < 0 || static_cast<size_t>(wheel) == i) it->second.tyres[i].setTemperature(surface, core);
}

void RigidWorld::replaceVehicleTyres(VehicleId id) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end()) return;
    for (Tyre& t : it->second.tyres) t.replace();
}

void RigidWorld::removeVehicle(VehicleId id) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end()) return;
    m->system.RemoveStepListener(it->second.constraint);
    m->system.RemoveConstraint(it->second.constraint);
    const BodyId body = it->second.body;
    m->vehicles.erase(it);
    remove(body);
}

size_t RigidWorld::vehicleCount() const { return m->vehicles.size(); }

RigidWorld::BodyId RigidWorld::vehicleBody(VehicleId id) const {
    auto it = m->vehicles.find(id);
    return it == m->vehicles.end() ? kNoBody : it->second.body;
}

void RigidWorld::setVehicleInput(VehicleId id, const VehicleInput& in) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end()) return;
    Impl::Vehicle& v = it->second;
    // Jolt steers positive = right on the input (the wheels' angle is positive to the left).
    float forward = glm::clamp(in.throttle, -1.0f, 1.0f);
    if (v.manual) {
        const int gears = static_cast<int>(v.controller->GetTransmission().mGearRatios.size());
        forward = std::max(0.0f, forward);
        // Braking off the throttle the driver holds the clutch in: an engine
        // at idle in gear would otherwise creep the car through its brakes.
        const float clutch = forward == 0.0f && in.brake > 0.0f ? 0.0f : 1.0f;
        v.controller->GetTransmission().Set(glm::clamp(in.gear, -1, gears), clutch);
    }
    v.controller->SetDriverInput(forward, glm::clamp(in.steer, -1.0f, 1.0f), glm::clamp(in.brake, 0.0f, 1.0f), glm::clamp(in.handBrake, 0.0f, 1.0f));
    if (forward != 0.0f || in.steer != 0.0f || in.brake != 0.0f || in.handBrake != 0.0f) m->bodies().ActivateBody(JPH::BodyID(v.body));
}

bool RigidWorld::vehicleState(VehicleId id, VehicleState& out) const {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end()) return false;
    const Impl::Vehicle& v = it->second;
    const JPH::VehicleConstraint& c = *v.constraint;
    const JPH::BodyID body(v.body);
    const JPH::Vec3 forward = m->bodies().GetRotation(body) * JPH::Vec3::sAxisZ();
    out.speed = m->bodies().GetLinearVelocity(body).Dot(forward);
    out.rpm = v.controller->GetEngine().GetCurrentRPM();
    out.gear = v.controller->GetTransmission().GetCurrentGear();
    out.switchingGear = v.controller->GetTransmission().IsSwitchingGear();
    const JPH::uint n = static_cast<JPH::uint>(c.GetWheels().size());
    out.wheels.resize(n);
    for (JPH::uint i = 0; i < n; ++i) {
        const JPH::WheelWV& w = *static_cast<const JPH::WheelWV*>(c.GetWheel(i));
        const JPH::WheelSettings& ws = *w.GetSettings();
        VehicleWheelState& o = out.wheels[i];
        // Wheel meshes are modelled in the car's own axes, centred on the
        // wheel: the car's right (-X) is the wheel's right, on both sides.
        o.transform = toG(c.GetWheelWorldTransform(i, -JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY()));
        o.contact = w.HasContact();
        o.groundBody = o.contact ? w.GetContactBodyID().GetIndexAndSequenceNumber() : kNoBody;
        if (o.contact) {
            const JPH::RVec3 p = w.GetContactPosition();
            o.contactPoint = glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
        }
        o.longitudinalSlip = w.mLongitudinalSlip;
        o.lateralSlip = glm::degrees(w.mLateralSlip);
        o.angularVelocity = w.GetAngularVelocity();
        const float range = ws.mSuspensionMaxLength - ws.mSuspensionMinLength;
        o.suspension = range > 0.0f ? glm::clamp((ws.mSuspensionMaxLength - w.GetSuspensionLength()) / range, 0.0f, 1.0f) : 0.0f;
        o.steerDegrees = glm::degrees(w.GetSteerAngle());
        if (i < v.tyres.size()) {
            const Tyre& t = v.tyres[i];
            o.load = o.contact ? std::max(0.0f, w.GetSuspensionLambda() / m->lastDt) : 0.0f;
            o.surfaceTemp = t.surfaceTemp();
            o.coreTemp = t.coreTemp();
            o.pressure = t.pressure();
            o.wear = t.wear();
            o.condition = t.condition();
            o.slidePower = t.slidePower();
            o.groundMaterial = v.groundMaterial[i];
            o.grip = v.grip[i] * t.grip() * v.ground[i].grip * t.loadFactor(o.load, v.nominalLoad);
            o.radius = ws.mRadius;
        }
    }
    return true;
}

void RigidWorld::setVehiclePower(VehicleId id, float scale) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end()) return;
    it->second.controller->GetEngine().mMaxTorque = it->second.torque * std::max(0.0f, scale);
}

void RigidWorld::setVehicleWheelGrip(VehicleId id, int wheel, float scale) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end() || wheel < 0 || wheel >= static_cast<int>(it->second.grip.size())) return;
    it->second.grip[static_cast<size_t>(wheel)] = std::max(0.0f, scale);
}

void RigidWorld::resetVehicle(VehicleId id) {
    auto it = m->vehicles.find(id);
    if (it == m->vehicles.end()) return;
    Impl::Vehicle& v = it->second;
    for (JPH::Wheel* w : v.constraint->GetWheels()) w->SetAngularVelocity(0.0f);
    v.controller->GetEngine().SetCurrentRPM(v.controller->GetEngine().mMinRPM);
    v.controller->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
    const JPH::BodyID body(v.body);
    m->bodies().SetLinearAndAngularVelocity(body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    m->bodies().ActivateBody(body);
}

double RigidWorld::lastStepMs() const { return m->stepMs; }

RigidWorld::ClothId RigidWorld::addCloth(const ClothDesc& desc) { return m->clothSystem().add(desc); }
void RigidWorld::removeCloth(ClothId id) {
    if (m->cloth) m->cloth->remove(id);
}
size_t RigidWorld::clothCount() const { return m->cloth ? m->cloth->clothCount() : 0; }
bool RigidWorld::clothPositions(ClothId id, std::vector<glm::vec3>& out) const { return m->cloth && m->cloth->positions(id, out); }
void RigidWorld::setClothJoints(ClothId id, const std::vector<glm::mat4>& joints) {
    if (m->cloth) m->cloth->setJoints(id, joints);
}
void RigidWorld::setClothProtection(ClothId id, ClothProtection level) {
    if (m->cloth) m->cloth->setProtection(id, level);
}
ClothProtection RigidWorld::clothProtection(ClothId id) const { return m->cloth ? m->cloth->protection(id) : ClothProtection::Off; }
void RigidWorld::resetCloth(ClothId id) {
    if (m->cloth) m->cloth->reset(id);
}
ClothStats RigidWorld::clothStats(ClothId id) const { return m->cloth ? m->cloth->stats(id) : ClothStats{}; }
void RigidWorld::setWind(const glm::vec3& velocity) { m->clothSystem().setWind(velocity); }
glm::vec3 RigidWorld::wind() const { return m->cloth ? m->cloth->wind() : glm::vec3(0.0f); }
double RigidWorld::lastClothMs() const { return m->cloth ? m->cloth->lastMs() : 0.0; }
void RigidWorld::setClothGpu(std::shared_ptr<ClothGpu> gpu) {
    m->clothGpu = std::move(gpu);
    if (m->cloth) m->cloth->setGpu(m->clothGpu);
}
RigidWorld::HairId RigidWorld::addHair(const HairDesc& desc) { return m->clothSystem().addHair(desc); }
void RigidWorld::removeHair(HairId id) {
    if (m->cloth) m->cloth->removeHair(id);
}
size_t RigidWorld::hairCount() const { return m->cloth ? m->cloth->hairCount() : 0; }
bool RigidWorld::hairPositions(HairId id, std::vector<glm::vec3>& out) const { return m->cloth && m->cloth->hairPositions(id, out); }
void RigidWorld::setHairMotion(HairId id, float motion) {
    if (m->cloth) m->cloth->setHairMotion(id, motion);
}
float RigidWorld::hairMotion(HairId id) const { return m->cloth ? m->cloth->hairMotion(id) : 1.0f; }
void RigidWorld::setHairJoint(HairId id, const glm::mat4& head) {
    if (m->cloth) m->cloth->setHairJoint(id, head);
}
void RigidWorld::resetHair(HairId id) {
    if (m->cloth) m->cloth->resetHair(id);
}
HairStats RigidWorld::hairStats(HairId id) const { return m->cloth ? m->cloth->hairStats(id) : HairStats{}; }
double RigidWorld::simulatedTime() const { return m->simulatedTime; }

std::vector<RigidWorld::Contact> RigidWorld::takeContacts() {
    std::lock_guard<std::mutex> lock(m->contactMutex);
    std::vector<Contact> out;
    out.swap(m->contacts);
    return out;
}

} // namespace kke
