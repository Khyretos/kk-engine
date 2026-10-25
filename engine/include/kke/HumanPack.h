#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// MakeHuman's CC0 human as tools/makehuman/cook_human.py cooks it
// (docs/HUMANS.md): the base mesh, every morph target, the modifier
// lists, the skeleton and its skin weights. Read-only data; kke::Human
// (kke/Human.h) turns slider values into a body.
//
// Units are converted on load: MakeHuman works in decimetres, the pack's
// positions and offsets here are metres (+Y up, the figure faces +Z).
// Pure CPU, unit-tested in tests/test_human.cpp.

struct HumanTarget {
    std::string name;              // "nose/nose-hump-incr"
    std::vector<uint16_t> verts;   // the vertices it moves, ascending
    std::vector<glm::vec3> offsets; // metres, one per vert
};

struct HumanModifier {
    std::string group;   // "nose", "macrodetails", "measure", ...
    std::string name;    // MakeHuman's own: "nose/nose-hump-decr|incr", "macrodetails/Age"
    std::string macro;   // "Age", "Gender", ... for the body-type sliders (no targets then)
    // One-sided: one target, 0..1. Two-sided: below 0 the first target,
    // above 0 the second, -1..1.
    std::vector<int> targets; // into HumanPack::targets()
    float min = 0.0f, max = 1.0f, defaultValue = 0.0f;
};

struct HumanQuad {
    std::array<uint32_t, 4> verts{}; // a triangle repeats its last corner
    std::array<uint32_t, 4> uvs{};
    uint16_t group = 0;
};

struct HumanGroup {
    std::string name; // "body", "helper-l-eye", "joint-head", ...
    uint32_t firstFace = 0, faceCount = 0;
};

struct HumanBoneDef {
    std::string name; // MakeHuman's default rig: "root", "spine05", "upperarm01.L", ...
    int parent = -1;  // earlier in the list
    std::string head, tail, plane; // joint names; plane may be empty
};

class HumanPack {
public:
    // Throws std::runtime_error with the path and the reason.
    static HumanPack load(const std::string& path);
    static HumanPack parse(const std::vector<uint8_t>& bytes, const std::string& name);

    const std::vector<glm::vec3>& positions() const { return m_positions; }
    const std::vector<glm::vec2>& uvs() const { return m_uvs; }
    const std::vector<HumanQuad>& quads() const { return m_quads; }
    const std::vector<HumanGroup>& groups() const { return m_groups; }
    int findGroup(const std::string& name) const; // -1 = none

    const std::vector<HumanTarget>& targets() const { return m_targets; }
    int findTarget(const std::string& name) const; // -1 = none

    const std::vector<HumanModifier>& modifiers() const { return m_modifiers; }
    int findModifier(const std::string& name) const; // -1 = none
    // MakeHuman's description of a modifier ("" when it has none).
    std::string description(const std::string& modifier) const;

    const std::vector<HumanBoneDef>& bones() const { return m_bones; }
    // The vertices whose centre is each joint ("spine05____head").
    const std::map<std::string, std::vector<uint32_t>>& joints() const { return m_joints; }
    // The three joints whose plane sets a bone's roll ("upperarm01.L____plane").
    const std::map<std::string, std::array<std::string, 3>>& planes() const { return m_planes; }
    struct BoneWeights {
        std::vector<uint16_t> verts;
        std::vector<float> weights;
    };
    // By bone name.
    const std::map<std::string, BoneWeights>& weights() const { return m_weights; }

    // Where the data came from ({"repo", "commit"}), for credits and bug reports.
    const std::map<std::string, std::string>& source() const { return m_source; }
    const std::string& licence() const { return m_licence; }

private:
    std::vector<glm::vec3> m_positions;
    std::vector<glm::vec2> m_uvs;
    std::vector<HumanQuad> m_quads;
    std::vector<HumanGroup> m_groups;
    std::vector<HumanTarget> m_targets;
    std::map<std::string, int> m_targetIndex;
    std::vector<HumanModifier> m_modifiers;
    std::map<std::string, int> m_modifierIndex;
    std::map<std::string, std::string> m_descriptions;
    std::vector<HumanBoneDef> m_bones;
    std::map<std::string, std::vector<uint32_t>> m_joints;
    std::map<std::string, std::array<std::string, 3>> m_planes;
    std::map<std::string, BoneWeights> m_weights;
    std::map<std::string, std::string> m_source;
    std::string m_licence;
};

} // namespace kke
