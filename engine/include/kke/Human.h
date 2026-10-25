#pragma once

#include "kke/HumanPack.h"
#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// A MakeHuman body made from slider values (docs/HUMANS.md). Set the
// sliders by MakeHuman's own names ("macrodetails/Age",
// "nose/nose-hump-decr|incr"), then bake() once into an ordinary skinned
// model: the engine's GPU skinning animates it, so a custom character
// costs the same per frame as any other model. reshape() updates a bake
// in place while a creator screen drags a slider.
//
// The body-type sliders (gender, age, muscle, weight, height,
// proportions, ancestry, breast size and firmness) blend MakeHuman's
// macro targets: each such target is named after the body it belongs to
// ("universal-female-young-averagemuscle-maxweight"), and its weight is
// how close the sliders are to that body, word by word.
//
// Pure CPU, unit-tested in tests/test_human.cpp. Not thread-safe: it
// keeps the body-type shape between calls (one Human per thread).
class Human {
public:
    explicit Human(const HumanPack& pack);

    const HumanPack& pack() const { return *m_pack; }

    // Clamped to the modifier's range. Setting one ancestry value scales
    // the other two so the three still add up to 1. Unknown names are
    // kept (a newer pack may know them) and change nothing.
    void set(const std::string& modifier, float value);
    float get(const std::string& modifier) const; // the default when unset
    void reset(); // everything back to the default body
    // All sliders at once (the rest at their defaults), each value as it
    // is but clamped; the ancestry three are scaled to add up to 1 only if
    // they don't. A loaded look uses this, so it bakes exactly as saved.
    void setAll(const std::map<std::string, float>& values);
    // Every value that differs from its default, by name (what a save holds).
    std::map<std::string, float> values() const;

    // Each target that moves the body now, with its weight.
    std::vector<std::pair<int, float>> targetWeights() const;
    // Every pack vertex, shaped by the sliders (metres), the soles on y = 0.
    std::vector<glm::vec3> shapedPositions() const;
    // Each joint's position on the shaped body: the centre of its vertices.
    glm::vec3 jointPosition(const std::string& joint, const std::vector<glm::vec3>& shaped) const;

    struct BakeOptions {
        // Groups drawn, each its own mesh. "body" is the skin; MakeHuman's
        // helper eyes, teeth and tongue stand in for eyes and a mouth until
        // the eye and teeth assets are in.
        std::vector<std::string> groups{ "body", "helper-l-eye", "helper-r-eye", "helper-upper-teeth",
                                         "helper-lower-teeth", "helper-tongue" };
        bool skeleton = true; // false: static meshes, no bones
    };
    struct Bake {
        ModelData model;
        // sourceVertex[mesh][vertex] = the pack vertex it was made from
        // (a pack vertex on a UV seam becomes several model vertices).
        std::vector<std::vector<uint32_t>> sourceVertex;
    };
    Bake bake() const { return bake(BakeOptions{}); }
    Bake bake(const BakeOptions& options) const;
    // Moves a bake's vertices, normals, bones and bounds to the current
    // sliders without rebuilding it.
    void reshape(Bake& bake) const;

private:
    struct MacroTarget {
        int target = -1;
        std::vector<std::pair<int, int>> words; // (variable, level) pairs
    };
    float macroWeight(const MacroTarget& m, const float* value) const; // value: per body-type variable
    void placeSkeleton(ModelData& model, const std::vector<glm::vec3>& shaped) const;

    const HumanPack* m_pack;
    std::map<std::string, float> m_values;
    std::vector<MacroTarget> m_macroTargets;
    std::vector<uint32_t> m_bodyVerts; // the skin's vertices, to find the floor
    // The base mesh plus the body-type targets, for these body-type values.
    mutable std::vector<float> m_macroKey;
    mutable std::vector<glm::vec3> m_macroShape;
};

} // namespace kke
