#pragma once

// The party's characters: jelly beans the engine makes itself (no art
// packs), so every build has them. A look is a colour, a pattern, a face
// and a hat, picked in the start menu (README.md "Your bean"). The body
// is one mesh per bean, built once per look; hands and feet are one small
// ball drawn four times, so they can waddle and wave.

#include "kke/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace party {

struct BeanLook {
    int colour = 0, pattern = 0, face = 0, hat = 0;
    bool operator==(const BeanLook& o) const { return colour == o.colour && pattern == o.pattern && face == o.face && hat == o.hat; }
    bool operator!=(const BeanLook& o) const { return !(*this == o); }
};

struct NamedColour {
    const char* name;
    glm::vec3 rgb;
};
const std::vector<NamedColour>& beanColours();
const std::vector<std::string>& beanPatterns(); // Plain, Stripes, Spots, Two-tone, Belly, Stars
const std::vector<std::string>& beanFaces();    // Happy, Sleepy, Fierce, Shades, Surprised
const std::vector<std::string>& beanHats();     // None, Crown, Cap, Top hat, Beanie, Horns, Party hat, Propeller, Bow
glm::vec3 beanColour(int colour);

// Sizes: the capsule the physics moves (feet at the origin).
constexpr float kBeanHeight = 1.3f;
constexpr float kBeanRadius = 0.42f;

// The body (with its face and hat), feet at the origin, facing -Z.
void buildBean(const BeanLook& look, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);
// A hand or foot: a unit sphere in the bean's colour (scaled when drawn).
void buildLimb(const BeanLook& look, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);

// Primitives the minigames build their levels from too (flat-shaded
// boxes, smooth spheres and cylinders), appended to a mesh.
struct MeshBuilder {
    std::vector<kke::Vertex>& v;
    std::vector<uint32_t>& idx;
    void box(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, const glm::mat3& rot = glm::mat3(1.0f));
    void ellipsoid(const glm::vec3& center, const glm::vec3& radii, const glm::vec3& color, int slices = 16, int stacks = 10);
    // Along +Y from `base`; `topRadius` 0 = a cone.
    void cylinder(const glm::vec3& base, float radius, float topRadius, float height, const glm::vec3& color, int slices = 20, bool caps = true);
    // A hexagonal prism (the falling tiles), flat top, pointy along X.
    void hexPrism(const glm::vec3& center, float radius, float halfHeight, const glm::vec3& top, const glm::vec3& side);
    // Transform what was added since `from` (a part built in place, then turned).
    void transform(size_t from, const glm::mat4& m);
};

} // namespace party
