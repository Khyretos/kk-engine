#pragma once

namespace kke {

// The shared, engine-level material concept every deformable/destructible
// object reads from — not FEMFX-specific vocabulary, deliberately, so a
// future second physics backend (or the melting/MPM track — see README
// "Physics: AMD FEMFX integration") shares the same idea of "what a
// material's numbers mean" rather than each system inventing its own.
//
// Density in particular ties directly into the original ask this was
// built for: wood, iron, and rubber should behave differently under
// identical impacts because their density/stiffness/toughness genuinely
// differ, not because a game script hand-picks different behavior per
// object. Reasonable real-world-ish starting values given as defaults;
// treat them as a starting point to tune, not a physically exact model.
struct Material {
    // kg/m^3. Wood ~500-800, water 1000, iron ~7870, gold ~19300.
    float density = 1000.0f;

    // Resistance to elastic deformation (Young's-modulus-like — higher
    // is stiffer). Rubber is soft (low), steel is very stiff (high).
    float stiffness = 1.0e6f;

    // Poisson's ratio: how much a material bulges sideways when
    // compressed lengthwise. 0 = no sideways bulge, ~0.5 = nearly
    // incompressible (rubber-like). Physically valid range is roughly
    // [-1, 0.5]; most real solids sit around 0.2-0.35.
    float poissonsRatio = 0.3f;

    // Stress level at which fracture begins — this is the actual "how
    // things break" knob. Low = brittle (glass, dry wood snapping),
    // high = tough (rubber, most metals bending long before breaking).
    float fractureStressThreshold = 1.0e5f;

    // Stress level at which the material stops being purely elastic and
    // starts permanently deforming (denting) rather than springing back.
    // Low relative to fractureStressThreshold = a material that dents
    // easily but rarely breaks (soft metal); high and close to the
    // fracture threshold = a material that stays rigid until it
    // suddenly breaks rather than bending first (glass, ceramic).
    float plasticYieldThreshold = 5.0e4f;

    // How much permanent deformation accumulates once past the yield
    // threshold, per unit of excess stress. Higher = dents more easily
    // once yielding starts.
    float plasticCreep = 0.3f;
};

} // namespace kke
