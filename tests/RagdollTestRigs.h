#pragma once
// Skeletons for ragdoll tests, built from world positions (each bone a
// pure translation from its parent).
#include "kke/ModelAsset.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <utility>
#include <vector>

namespace kke_test {

// A horse-shaped Quaternius rig (Farm Animals naming, +Z forward, +X
// left), bone positions as in Horse.fbx. `tail = false` is a pug-like one.
inline kke::ModelData quadrupedSkeleton(bool tail = true) {
    kke::ModelData m;
    std::vector<glm::vec3> world;
    auto add = [&](const char* name, int parent, glm::vec3 at) {
        kke::ModelBone b;
        b.name = name;
        b.parent = parent;
        b.localRest = glm::translate(glm::mat4(1.0f), parent >= 0 ? at - world[parent] : at);
        m.bones.push_back(b);
        world.push_back(at);
        return static_cast<int>(m.bones.size()) - 1;
    };
    const int root = add("root", -1, { 0, 0, 0 });
    const int body = add("Body", root, { 0, 2.3f, 0 });
    for (int side : { -1, 1 }) {
        const float x = 0.9f * static_cast<float>(side);
        const std::string s = side > 0 ? ".L" : ".R";
        const int fl = add(("FrontLeg" + s).c_str(), body, { x * 0.35f, 3.14f, 1.86f });
        const int fu = add(("FrontUpLeg" + s).c_str(), fl, { x, 3.14f, 1.86f });
        add(("FrontLowLeg" + s).c_str(), fu, { x, 1.68f, 2.09f });
        const int bl = add(("BackLeg" + s).c_str(), body, { x * 0.35f, 3.14f, -2.2f });
        const int bu = add(("BackUpLeg" + s).c_str(), bl, { x * 0.85f, 3.14f, -2.2f });
        add(("BackLowLeg" + s).c_str(), bu, { x * 0.85f, 2.26f, -2.83f });
        add(("FrontFoot" + s).c_str(), root, { x, 0.1f, 1.93f });
        add(("BackFoot" + s).c_str(), root, { x * 0.85f, 0.1f, -2.6f });
    }
    if (tail) {
        const int back = add("Back", body, { 0, 3.71f, -2.42f });
        const int t1 = add("Tail1", back, { 0, 4.79f, -2.68f });
        const int t2 = add("Tail2", t1, { 0, 4.76f, -3.52f });
        const int t3 = add("Tail3", t2, { 0, 3.9f, -3.7f });
        add("Tail4", t3, { 0, 3.2f, -3.63f });
    }
    const int shoulders = add("Shoulders", body, { 0, 4.11f, 1.83f });
    const int neck = add("Neck", shoulders, { 0, 5.22f, 2.54f });
    add("Head", neck, { 0, 6.22f, 3.46f });
    const int hips = add("Hips", body, { 0, 3.99f, -1.49f });
    add("Torso", hips, { 0, 3.94f, -0.03f });
    return m;
}

} // namespace kke_test
