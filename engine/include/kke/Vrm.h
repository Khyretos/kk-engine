#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// A VRM avatar (docs/ANIME.md): the glTF-based format VRoid Studio and
// most anime-style character tools export. VRM 0.x is read: the mesh and
// skeleton (through the glTF loader), the face's morph targets and the
// expressions built from them (blink, the vowels, joy, ...), each
// material's MToon toon-shading settings, the spring bones that swing
// hair and skirts, and the humanoid bone map.
//
// The avatar is turned to face +Z (VRM 0.x faces -Z), the way the
// engine's other characters face, and its bones keep their names, which
// kke::AnimRig already pairs with every other rig's.

// One morph target: a position (and normal) offset per vertex of one mesh.
struct VrmMorph {
    uint32_t mesh = 0; // index into ModelData::meshes
    std::vector<uint32_t> vertices;
    std::vector<glm::vec3> positions, normals; // the offsets, one per entry in `vertices`
};

// A named face (or body) expression: morph targets at given weights.
struct VrmExpression {
    std::string name;   // as the file names it ("Joy", "Blink_L")
    std::string preset; // VRM's preset ("joy", "blink_l", "a", ...), lower case; "unknown" for custom ones
    std::vector<std::pair<uint32_t, float>> binds; // VrmAvatar::morphs index, weight 0..1
    bool binary = false; // snaps to 0 or 1
};

// MToon (VRM's toon shader): what shades a material.
struct VrmToon {
    enum class Alpha : uint8_t { Opaque, Cutout, Blend };
    Alpha alpha = Alpha::Opaque;
    float cutoff = 0.5f;
    bool doubleSided = false;
    glm::vec4 color{ 1.0f };       // lit colour (sRGB), alpha
    glm::vec3 shadeColor{ 0.7f };  // the colour in shadow (sRGB)
    std::string mainTexture, shadeTexture, normalTexture, matcapTexture, emissionTexture, rimTexture, outlineWidthTexture;
    float normalScale = 1.0f;
    float shadeShift = 0.0f;       // -1..1: where light turns to shade
    float shadeToony = 0.9f;       // 0 = soft, 1 = a hard line
    float receiveShadow = 1.0f;    // how much cast shadows darken it
    float lightColorAttenuation = 0.0f;
    float giIntensity = 0.1f;      // the sky's light
    glm::vec3 emissionColor{ 0.0f };
    glm::vec3 rimColor{ 0.0f };
    float rimFresnelPower = 1.0f, rimLift = 0.0f, rimLightingMix = 0.0f;
    float outlineWidth = 0.0f;     // in centimetres (world) as VRM 0.x has it, 0 = none
    bool outlineScreenWidth = false; // width in percent of the screen instead
    glm::vec3 outlineColor{ 0.0f };
    bool outlineFixedColor = true;   // false: the outline is shaded like the surface
    float outlineLightingMix = 1.0f;
    int renderQueue = 2000;          // draw order among blended materials
};

// Spring bones: a chain of bones that swings like hair (VRM 0.x
// secondaryAnimation; kke::VrmSprings simulates them).
struct VrmSpringGroup {
    std::vector<int> roots;          // bones whose children (all of them, down the chains) swing
    float stiffness = 1.0f;
    float gravityPower = 0.0f;
    glm::vec3 gravityDir{ 0.0f, -1.0f, 0.0f };
    float drag = 0.4f;
    float hitRadius = 0.02f;
    std::vector<uint32_t> colliderGroups; // VrmAvatar::colliderGroups
};

struct VrmCollider {
    glm::vec3 offset{ 0.0f }; // in the bone's space
    float radius = 0.05f;
};

struct VrmColliderGroup {
    int bone = -1;
    std::vector<VrmCollider> colliders;
};

struct VrmMeta {
    std::string title, author, version, licence, contact, reference;
    std::string allowedUsers, violence, sexual, commercial, otherPermissionUrl, otherLicenseUrl;
};

struct VrmAvatar {
    ModelData model;
    std::vector<VrmToon> toon;   // one per ModelData material
    std::vector<VrmMorph> morphs;
    std::vector<VrmExpression> expressions;
    std::vector<VrmSpringGroup> springs;
    std::vector<VrmColliderGroup> colliderGroups;
    std::map<std::string, int> humanoid; // VRM bone name ("hips", "leftUpperLeg") -> ModelData bone
    VrmMeta meta;
    float eyeHeight = 1.5f;              // the first-person camera's height (VRM's firstPersonBoneOffset + head)

    int expression(const std::string& nameOrPreset) const; // -1 if none (case-insensitive)
    int bone(const std::string& humanoidName) const;        // -1 if none

    // Each mesh's vertices for a pose: `locals` = every bone's local
    // transform (computeRestPose's locals, an animation frame...),
    // `morphWeights` = one weight per morph (empty = none; see weights()).
    // Positions and normals in model space; the rest of each vertex as loaded.
    void pose(const std::vector<glm::mat4>& locals, const std::vector<float>& morphWeights,
              std::vector<std::vector<ModelVertex>>& out) const;
    // Expression weights (index = expressions) to morph weights.
    std::vector<float> weights(const std::vector<float>& expressionWeights) const;
    // Every bone's rest local transform.
    std::vector<glm::mat4> restLocals() const;
};

// Throws std::runtime_error with the path and the reason on failure.
VrmAvatar loadVrm(const std::string& path);

} // namespace kke
