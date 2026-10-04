// Puppets: an animated character in the world with what it holds and
// wears (weapon in the right hand, a bow or shield in the left, a hat,
// a quiver on the back), each prop following its bone every frame.

#include "HordeModule.h"

#include <glm/gtc/matrix_transform.hpp>

namespace horde {

void HordeModule::dressPuppet(Puppet& p, const Look* look, const glm::vec3& color, float scale) {
    undressPuppet(p);
    (void)scale;
    p.look = look;
    p.tint = color;
    if (!look || !look->model || !look->rig) return;
    p.model = m_models->spawn(look->model, glm::mat4(1.0f));
    m_models->setOverlayEnabled(p.model, false);
    m_models->setTint(p.model, color);
    p.anim = std::make_unique<kke::Animator>(*look->rig->set);
    const Rig& r = *look->rig;
    kke::Animator& a = *p.anim;
    auto clipState = [&](const char* state, std::initializer_list<const char*> names, bool loop, float seconds = 0.0f) {
        std::vector<std::string> list(names.begin(), names.end());
        const int c = r.clip(list);
        if (c < 0) return -1;
        const float speed = seconds > 0.0f ? r.set->duration(c) / seconds : 1.0f;
        return a.addClipState(state, c, loop, speed);
    };
    auto idle = r.clip(std::vector<std::string>{ "Sword_Idle", "Idle_Loop" });
    p.idle = idle >= 0 ? a.addClipState("idle", idle, true) : -1;
    const int walk = r.clip("Walk_Loop"), jog = r.clip("Jog_Fwd_Loop"), sprint = r.clip(std::vector<std::string>{ "Sprint_Loop", "Jog_Fwd_Loop" });
    if (idle >= 0 && walk >= 0 && jog >= 0)
        p.move = a.addBlendState("move", { { { idle, 0.0f }, { walk, 1.4f }, { jog, 3.4f }, { sprint >= 0 ? sprint : jog, 5.5f } } });
    else p.move = p.idle;
    p.hit = clipState("hit", { "Hit_Chest", "Hit_Head", "Hit_Stomach" }, false, 0.4f);
    p.knock = clipState("knock", { "Hit_Knockback", "Death01" }, false, 0.8f);
    p.getUp = clipState("get_up", { "LayToIdle", "KipUp", "Idle_Loop" }, false, 0.8f);
    p.death = clipState("death", { "Death01", "Death02" }, false);
    p.block = clipState("block", { "Sword_Block", "Idle_Shield_Loop" }, false, 0.6f);
    p.roll = clipState("roll", { "Roll" }, false, 0.75f);
    p.cheer = clipState("cheer", { "Celebration", "Idle_Loop" }, true);
    p.strafeL = clipState("strafe_l", { "Jog_Left_Loop", "Walk_L_Loop" }, true);
    p.strafeR = clipState("strafe_r", { "Jog_Right_Loop", "Walk_R_Loop" }, true);
    p.back_ = clipState("back", { "Jog_Bwd_Loop", "Walk_Bwd_Loop" }, true);
    if (p.move >= 0) a.play(p.move, 0.0f);
}

void HordeModule::undressPuppet(Puppet& p) {
    for (kke::ModelModule::InstanceId* i : { &p.model, &p.right, &p.left, &p.head, &p.back })
        if (*i) {
            m_models->remove(*i);
            *i = 0;
        }
    p.anim.reset();
    p.moves.clear();
    p.look = nullptr;
    p.lastState = -1;
    p.twoHanded = false;
}

void HordeModule::posePuppet(Puppet& p, const kke::Pose& pose, const glm::mat4& xf) {
    if (!p.model || !p.look || !p.look->rig) return;
    const Rig& r = *p.look->rig;
    p.xf = xf;
    m_models->setTransform(p.model, xf);
    if (std::vector<glm::mat4>* locals = m_models->boneLocals(p.model)) kke::poseToLocals(pose, *locals);
    p.bones = kke::poseToModel(r.data, pose);
    auto bone = [&](int b) { return b >= 0 && static_cast<size_t>(b) < p.bones.size() ? xf * p.bones[static_cast<size_t>(b)] : xf; };
    if (r.head >= 0) p.headPos = glm::vec3(bone(r.head) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    if (r.handR >= 0) p.handPos = glm::vec3(bone(r.handR)[3]);
    if (r.handL >= 0) p.offhandPos = glm::vec3(bone(r.handL)[3]);
    p.rightAt = bone(r.handR) * p.rightGrip;
    if (p.right && r.handR >= 0) m_models->setTransform(p.right, p.rightAt);
    if (p.left && r.handL >= 0) m_models->setTransform(p.left, bone(r.handL) * p.leftGrip);
    if (p.head && r.head >= 0) m_models->setTransform(p.head, bone(r.head) * p.headAt);
    if (p.back && r.chest >= 0) m_models->setTransform(p.back, bone(r.chest) * p.backAt);
}

// The upper body (spine and up) from `upper`, the legs from `base`.
kke::Pose HordeModule::overlay(const Rig& rig, const kke::Pose& base, const kke::Pose& upper, float weight) const {
    kke::Pose out = base;
    if (rig.spine < 0 || weight <= 0.0f) return out;
    std::vector<char> above(rig.data.bones.size(), 0);
    for (size_t b = 0; b < rig.data.bones.size() && b < out.size() && b < upper.size(); ++b) {
        const int parent = rig.data.bones[b].parent;
        above[b] = static_cast<int>(b) == rig.spine || (parent >= 0 && above[static_cast<size_t>(parent)]);
        if (!above[b]) continue;
        out[b].t = glm::mix(out[b].t, upper[b].t, weight);
        out[b].r = glm::slerp(out[b].r, upper[b].r, weight);
        out[b].s = glm::mix(out[b].s, upper[b].s, weight);
    }
    return out;
}

} // namespace horde
