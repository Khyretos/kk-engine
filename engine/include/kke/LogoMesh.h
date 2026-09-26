#pragma once

#include "kke/Mesh.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace kke::logo {

// The Kreative Kompas logo as geometry: a pocket-watch compass (a ring cut
// into four arcs, a crown on top, a two-tone needle and two mirrored Ks
// meeting at the hub). The flat outlines come from
// assets/branding/kreative-kompas-logo.svg via
// tools/branding/make_logo_outlines.py (engine/src/LogoOutlines.inc).
// Units: the ring's outer radius is 1, the hub sits at the origin, +Y is
// up, the logo faces +Z. It spans roughly x in [-1, 1], y in [-1, 1.49].
//
// Used by the engine's startup intro (kke::LogoIntro).

// Brand colours from the logo SVG (sRGB).
inline constexpr glm::vec3 kOrange{242 / 255.0f, 146 / 255.0f, 30 / 255.0f}; // #F2921E
inline constexpr glm::vec3 kPurple{92 / 255.0f, 56 / 255.0f, 141 / 255.0f};  // #5C388D

// A flat outline: one simple polygon, counter-clockwise, not closed (the
// last point connects back to the first).
struct Outline {
    std::vector<glm::vec2> points;
};

// One separately animated part of the logo.
struct Piece {
    std::string name;       // "ring_top", "k_left", "needle_north", "hub", ...
    glm::vec3 color{1.0f};
    std::vector<Outline> outlines; // flat shape (front view)
    glm::vec2 center{0.0f};        // pivot for the intro's per-piece motion
    float frontZ = 0.0f, backZ = 0.0f;
    // Extruded, bevelled solid: triangle list, counter-clockwise front
    // faces, vertex colour = piece colour.
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Every piece, back to front (draw order doesn't matter: all are opaque).
std::vector<Piece> buildPieces();

// Extrudes `outline` between z = backZ and z = frontZ with a 45 degree
// chamfer of `bevel` around the front face, appending to `out`.
// Neighbouring edges closer than 35 degrees share smoothed normals (arcs
// read as round), sharper corners stay crisp. `roof` > 0 instead raises
// the front towards x = 0 by that much (no bevel): one facet of the
// needle's ridge, for an outline lying on one side of the axis.
void extrude(const Outline& outline, float backZ, float frontZ, float bevel, const glm::vec3& color,
             std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, float roof = 0.0f);

// Ear-clipping triangulation of a simple counter-clockwise polygon;
// returns indices into `points`. Exposed for tests.
std::vector<uint32_t> triangulate(const std::vector<glm::vec2>& points);

// Signed area (> 0 for counter-clockwise).
float signedArea(const std::vector<glm::vec2>& points);

} // namespace kke::logo
