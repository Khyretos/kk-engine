#pragma once

#include "kke/FracturePattern.h"
#include "kke/Material.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kke {

class Application;

// Any prop from a model pack, bendable and breakable: a Synty crate
// splinters when a ragdoll lands on it, a lamp post bends, a window
// shatters. The prop's own triangles are voxelized into tetrahedra
// (kke::voxelizeToTets), cut into pieces by its material's fracture
// pattern (kke/VoronoiFracture.h), simulated by FEMFX (PhysicsModule),
// and the prop's own mesh, with its UVs and texture, is glued to the
// tets and redrawn from them while it moves. See docs/BREAKABLES.md.
//
//   m_breakables = std::make_unique<kke::Breakables>(app);          // init()
//   kke::BreakKind kind;
//   if (kke::guessBreakKind(assetPath, kind)) {                       // "SM_Prop_Crate_01" -> wood
//       kke::Breakables::Options o;
//       o.kind = kind;
//       m_breakables->make(instance, transform, o);
//   }
//   m_breakables->update();                                           // update(), every frame
//
// Without FEMFX in the build (Android, the default preset) make() returns
// 0 and the prop stays as it was.

// What a prop is made of, which decides how it gives: wood splinters,
// stone breaks into chunks, glass shatters from where it was hit, ceramic
// breaks into shards, metal bends and stays bent (FEMFX plasticity).
enum class BreakKind : uint8_t { Wood, Stone, Glass, Ceramic, Metal };
constexpr int kBreakKindCount = 5;

struct BreakPreset {
    const char* id;      // "wood": kke.scene's "breakable" value
    const char* label;   // "Wood (splinters)"
    Material material;
    FracturePattern pattern;
    float chunkSize;     // rough piece size, metres
    int cellsPerCluster; // > 0: pieces are clusters of cells (stone)
    bool plastic;        // bends and stays bent
};
const BreakPreset& breakPreset(BreakKind kind);
// "wood", "stone", ... or a preset's label. False for anything else.
bool breakKindFromId(const std::string& id, BreakKind& out);
// A guess from an asset's name (a Synty file name, a mesh name):
// SM_Prop_Crate_01 is wood, SM_Veh_Car_Sedan_01 metal, SM_Prop_Bottle_01
// glass, SM_Env_Rock_03 stone, SM_Prop_Vase_02 ceramic. False when the name
// says nothing, or names something that shouldn't break (ground, floor,
// road, terrain, sky, water, a building's shell): leave those static.
bool guessBreakKind(const std::string& name, BreakKind& out);

class Breakables {
public:
    explicit Breakables(Application& app);
    ~Breakables();
    Breakables(const Breakables&) = delete;
    Breakables& operator=(const Breakables&) = delete;

    struct Options {
        BreakKind kind = BreakKind::Wood;
        bool overridePattern = false;    // use `pattern` instead of the kind's own
        FracturePattern pattern = FracturePattern::Voronoi;
        float chunkScale = 1.0f;         // x the kind's piece size
        float toughness = 1.0f;          // x the fracture threshold
        size_t maxCells = 80;            // voxel budget (6 tets each); 40-80 is a good prop on min-spec
        uint32_t seed = 1;               // same seed, same pieces (kke::fractureSeed)
        std::string texture;             // the instance's texture override, if it has one
        float armAfterSeconds = 2.0f;    // settle under its own weight before it can break (BUG-043)
        float maxSize = 0.0f;            // > 0: refuse props bigger than this (metres, longest side)
        // Tall, thin props (a lamp post, a sign, a flag pole: taller than
        // 2.5x their footprint) would just topple: they are anchored at
        // their foot, so they bend there when hit. Pieces can't be pinned,
        // so a tall prop that breaks into pieces (a tree as splintering
        // wood) is refused unless `allowToppling`.
        bool anchorTall = true;
        bool allowToppling = false;
    };

    // Turns a placed, non-skinned instance into a breakable at `transform`
    // (where it stands now). Returns the physics handle, 0 when it can't
    // (no FEMFX, physics full, a skinned or empty model, too big). `stats`,
    // if given, gets a line for a log or a panel. Static colliders the game
    // gave the prop are the game's to remove: they'd be in the way.
    uint32_t make(ModelModule::InstanceId instance, const glm::mat4& transform, const Options& options, std::string* stats = nullptr);
    // Back to a plain prop (drawn from its mesh at its transform).
    void restore(ModelModule::InstanceId instance);
    // Forget it (the instance is being removed): physics goes, nothing drawn.
    void remove(ModelModule::InstanceId instance);
    // Every breakable's physics goes (from a module's shutdown(), while
    // PhysicsModule is still there; the destructor leaves physics alone).
    void clear();

    bool has(ModelModule::InstanceId instance) const;
    uint32_t handle(ModelModule::InstanceId instance) const;
    size_t count() const;

    // Redraws every breakable that moved from its tets (props asleep and
    // already drawn cost nothing). Call every frame from update().
    void update();
    // Instances the physics dropped (its runaway guard) during the last
    // update(): they are drawn as plain props again; the game may want to
    // give them their collider back.
    const std::vector<ModelModule::InstanceId>& lost() const { return m_lost; }

private:
    struct Entry;
    Application& m_app;
    std::vector<std::unique_ptr<Entry>> m_entries;
    std::vector<ModelModule::InstanceId> m_lost;
    Entry* find(ModelModule::InstanceId instance) const;
};

} // namespace kke
