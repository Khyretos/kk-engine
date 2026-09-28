#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/BodyShape.h"
#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Equipment: things a character holds or wears, on named places of its
// skeleton, the way Unreal's skeletal-mesh sockets and the slot grids of
// Escape from Tarkov or an ARPG's paper doll work:
//   - a Socket is a frame on a bone (the palm of the right hand, the
//     middle of the back, a hip);
//   - an EquipSlot is a place something can go (right hand, back, ...),
//     each with its socket;
//   - an Equippable says which slots it fits and how it is held: its
//     grips (where on it a palm goes, how thick the handle is) and its
//     shape (capsules, so the arm IK keeps it out of the body).
// A hand holds an item by its handle in the palm, not at the wrist: the
// palm socket sits on the palm's skin where a handle lies (diagonally,
// from the heel of the hand toward the index knuckle), and the fingers
// then close round the handle until they touch it (wrapFingers).
//
// Pure CPU, unit-tested in tests/test_body_shape.cpp; docs/EQUIPMENT.md
// explains it with the tennis racket and climbing holds.

// A frame on a bone: `local` is in the bone's own space.
struct Socket {
    int bone = -1;
    glm::mat4 local{1.0f};
    bool valid() const { return bone >= 0; }
    glm::mat4 world(const std::vector<glm::mat4>& bones) const { return bones[static_cast<size_t>(bone)] * local; }
};

// A hand: its bones and its palm, measured from the rest pose (and the
// skinned mesh, when the model has one).
struct HandRig {
    bool left = false;
    int hand = -1;                   // the hand bone (the wrist)
    int finger[4][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { -1, -1, -1, -1 } }; // index, middle, ring, pinky; _01.._03, tip
    int thumb[4] = { -1, -1, -1, -1 };
    // In the hand bone's space:
    glm::mat4 palm{1.0f};            // origin on the palm's skin where a handle lies; +Y along the handle toward the thumb side, +Z out of the palm
    glm::vec3 fingers{0.0f, 1.0f, 0.0f};   // wrist to middle knuckle
    glm::vec3 thumbSide{1.0f, 0.0f, 0.0f}; // pinky to index knuckle
    glm::vec3 palmNormal{0.0f, 0.0f, 1.0f};
    float knuckles = 0.1f;           // m from the wrist to the middle knuckle
    float fingerRadius = 0.009f;     // m
    float fingerTip[4] = { 0.02f, 0.02f, 0.02f, 0.02f }; // m of the last segment past its joint (no tip bone)
    float thumbTip = 0.025f;
    bool valid() const { return hand >= 0; }
    Socket palmSocket() const { return { hand, palm }; }
};
HandRig makeHandRig(const ModelData& model, bool left);

// What the fingers close on, in model space: handles (capsules) and flat
// surfaces (a rock face, a table: the fingers don't go through them).
struct GripSurface {
    struct Plane {
        glm::vec3 point{0.0f}, normal{0.0f, 1.0f, 0.0f}; // normal out of the solid
    };
    std::vector<Capsule> capsules;
    std::vector<Plane> planes;
};
// Curls each finger joint toward the palm, knuckle first, until that
// finger touches the surface or reaches the joint's range (a person's:
// about 90 degrees at the knuckle, 105 in the middle, 75 at the tip);
// the thumb closes the same way, less far. `close` 0 = as animated,
// 1 = closed to contact; `thumb` scales the thumb's share.
void wrapFingers(const ModelData& model, Pose& pose, const HandRig& hand, const GripSurface& surface, float close = 1.0f, float thumb = 1.0f);

// ---------------------------------------------------------------------
enum class EquipSlot : uint8_t { LeftHand, RightHand, Back, HipLeft, HipRight, Head, Count };
constexpr size_t kEquipSlots = static_cast<size_t>(EquipSlot::Count);
const char* equipSlotName(EquipSlot slot);
constexpr uint32_t slotBit(EquipSlot slot) { return 1u << static_cast<uint32_t>(slot); }
constexpr uint32_t kBothHands = slotBit(EquipSlot::LeftHand) | slotBit(EquipSlot::RightHand);

// Where on an item a hand holds it.
struct ItemGrip {
    std::string name;            // "main", "support", "throat", "foregrip"
    // Item space: origin on the handle's axis where the palm's middle
    // goes; +Y along the handle toward the item's working end (a racket's
    // head, a sword's blade, a rifle's muzzle); +Z the way the palm faces
    // through the handle (a racket's strings in a forehand grip).
    glm::mat4 frame{1.0f};
    float radius = 0.016f;       // m: the handle's
    float halfLength = 0.06f;    // m of handle either side of the palm
};

struct Equippable {
    std::string name;
    uint32_t slots = kBothHands;       // where it may go
    std::vector<ItemGrip> grips;       // [0] = how the hand it is equipped to holds it
    std::vector<Capsule> shape;        // item space, for keeping it out of the body
    // Worn rather than held (back, hips, head): the item's frame in the
    // slot's socket frame.
    std::array<glm::mat4, kEquipSlots> worn{};
    Equippable();
    int grip(const std::string& name) const; // -1 = none
};

class Equipment {
public:
    Equipment() = default;
    // Sockets from the rest pose: the palms, the back (spine_03), the hips
    // (pelvis, out to each side), the head.
    explicit Equipment(const ModelData& model);
    bool valid() const { return m_hands[0].valid() || m_hands[1].valid(); }

    const HandRig& hand(int side) const { return m_hands[side & 1]; } // 0 left, 1 right
    const Socket& socket(EquipSlot slot) const { return m_sockets[static_cast<size_t>(slot)]; }
    void setSocket(EquipSlot slot, const Socket& s) { m_sockets[static_cast<size_t>(slot)] = s; }

    // Puts `item` (kept by the caller) in `slot`, held by grip `grip` when
    // the slot is a hand. A second hand on an item the other hand holds
    // (a two-handed backhand, a rifle's foregrip) is the same item equipped
    // to that hand with another grip: the first hand places it.
    bool equip(EquipSlot slot, const Equippable& item, int grip = 0);
    void unequip(EquipSlot slot);
    const Equippable* item(EquipSlot slot) const { return m_items[static_cast<size_t>(slot)].item; }
    int gripIndex(EquipSlot slot) const { return m_items[static_cast<size_t>(slot)].grip; }

    // Model space, for these posed bones: where the item in `slot` is.
    // For an item in both hands, the hand listed first in EquipSlot's
    // order that holds it by its grips[0] places it.
    glm::mat4 itemTransform(EquipSlot slot, const std::vector<glm::mat4>& world) const;
    // Where the hand bone must be (model space) for hand `side` to hold
    // `item` by grip `grip` with the item at `itemModel`: what arm IK aims
    // at (solveHumanArm's hand and handRotation).
    glm::mat4 handFor(int side, const Equippable& item, int grip, const glm::mat4& itemModel) const;
    ArmGoal armGoal(int side, const Equippable& item, int grip, const glm::mat4& itemModel) const;
    // The item in hand `side`'s bone space (constant while it is held),
    // and its shape there, for BodyAvoid::held.
    glm::mat4 inHand(int side, const Equippable& item, int grip) const;
    std::vector<Capsule> heldShape(int side) const;
    // Closes hand `side` round what it holds (after the arm IK).
    void closeHand(const ModelData& model, Pose& pose, int side, float close = 1.0f) const;

private:
    struct Held {
        const Equippable* item = nullptr;
        int grip = 0;
    };
    std::array<HandRig, 2> m_hands;
    std::array<Socket, kEquipSlots> m_sockets;
    std::array<Held, kEquipSlots> m_items;
};

} // namespace kke
