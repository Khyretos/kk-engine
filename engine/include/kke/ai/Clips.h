#pragma once

#include "kke/ModelAsset.h"

#include <string>

namespace kke::ai {

// Which of a model's clips shows an AiWorld animation name (Agent::anim:
// "idle", "walk", "run", "eat", "drink", "rest", "attack", "alert",
// "sniff", "bark"), so any animal model plays its AI without a hand-made
// table.
//
// The search goes in this order:
//   1. a clip with exactly that name;
//   2. the usual pack names for it, compared after any "Armature|" prefix
//      and without case: Quaternius "WalkSlow" / "Walk" / "Run" / "Eat",
//      Synty "_Locomotion_Walking", "Sleep", "Bite", and so on;
//   3. a stand-in. A walk with no walk clip hops on "Jump" at 0.7 speed,
//      as Quaternius' sheep and pigs do. Otherwise it falls back to a walk
//      or run clip, then idle.
// `clip` is -1 when the model has no clips at all.
struct ClipChoice {
    int clip = -1;
    float speed = 1.0f;
};
ClipChoice clipForAnim(const ModelData& model, const std::string& anim);

} // namespace kke::ai
