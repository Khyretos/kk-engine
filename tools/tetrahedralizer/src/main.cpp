// kke_tetrahedralizer — converts a surface mesh into a tetrahedral
// mesh for use with the engine's FEMFX-backed PhysicsModule. See
// README "Content pipeline: CGAL tetrahedralization" for the full
// architecture and why this tool exists as a separate target.
//
// USAGE:
//   kke_tetrahedralizer <input.off> <output.ktet.json> [facet_size] [cell_size]
//
// Input must currently be OFF format (a simple, well-supported CGAL
// input format) — see the README's "What's not done yet" for the real
// gap this leaves (no OBJ/FBX/glTF/etc. support yet; that needs a
// separate model-import step, most likely via assimp, feeding into
// this same CGAL pipeline rather than replacing it).
//
// This uses CGAL's *polyhedral domain* pipeline (direct constrained
// Delaunay tetrahedralization on the input surface) — not the more
// robust voxel-grid path (rasterize to a binary occupancy grid, which
// is indifferent to whether the source was watertight at all). What
// IS implemented, and what makes this genuinely robust to real-world
// imperfect input rather than just the already-clean case: input is
// read as a raw polygon soup, then repaired before any meshing step
// even sees it — CGAL::Polygon_mesh_processing's own repair_polygon_soup
// (removes duplicate/degenerate elements), orient_polygon_soup (fixes
// inconsistent face winding — confirmed by testing to be a real,
// common defect: a hand-built test mesh with mismatched winding
// failed to even parse before this fix), and stitch_borders (closes
// small gaps by merging matching boundary edges) all run first. The
// full voxel-grid path for genuinely large gaps or non-manifold
// topology stitch_borders can't close is still real future work — see
// README "Content pipeline: CGAL tetrahedralization."

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/IO/Polyhedron_iostream.h>
#include <CGAL/IO/polygon_soup_io.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <CGAL/Polyhedral_mesh_domain_3.h>
#include <CGAL/Mesh_triangulation_3.h>
#include <CGAL/Mesh_complex_3_in_triangulation_3.h>
#include <CGAL/Mesh_criteria_3.h>
#include <CGAL/make_mesh_3.h>

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <array>

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Polyhedron_3<K> Polyhedron;
typedef CGAL::Polyhedral_mesh_domain_3<Polyhedron, K> Mesh_domain;
typedef CGAL::Mesh_triangulation_3<Mesh_domain>::type Tr;
typedef CGAL::Mesh_complex_3_in_triangulation_3<Tr> C3t3;
typedef CGAL::Mesh_criteria_3<Tr> Mesh_criteria;
typedef Tr::Vertex_handle Vertex_handle;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: kke_tetrahedralizer <input.off> <output.ktet.json> [facet_size] [cell_size]\n";
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string outputPath = argv[2];
    const double facetSize = (argc > 3) ? std::stod(argv[3]) : 0.5;
    const double cellSize = (argc > 4) ? std::stod(argv[4]) : 0.5;

    std::vector<K::Point_3> soupPoints;
    std::vector<std::vector<std::size_t>> soupPolygons;
    if (!CGAL::IO::read_polygon_soup(inputPath, soupPoints, soupPolygons)) {
        std::cerr << "Could not read " << inputPath << " as a polygon soup\n";
        return 1;
    }
    std::cout << "Loaded " << soupPoints.size() << " points, " << soupPolygons.size()
              << " polygons from " << inputPath << " (raw, unrepaired)\n";

    // Repair BEFORE any meshing step sees this data — this is the
    // actual point of reading as a soup rather than loading straight
    // into a Polyhedron_3 (which requires the input to already be a
    // valid oriented manifold, and hard-fails otherwise; confirmed by
    // testing, not assumed: a hand-built test mesh with inconsistent
    // face winding failed at that exact step before this fix existed).
    CGAL::Polygon_mesh_processing::repair_polygon_soup(soupPoints, soupPolygons);
    if (!CGAL::Polygon_mesh_processing::orient_polygon_soup(soupPoints, soupPolygons)) {
        std::cerr << "Warning: orient_polygon_soup could not fully orient " << inputPath
                  << " -- proceeding anyway, but this mesh may have deeper topological problems\n";
    }

    Polyhedron polyhedron;
    CGAL::Polygon_mesh_processing::polygon_soup_to_polygon_mesh(soupPoints, soupPolygons, polyhedron);
    if (polyhedron.empty()) {
        std::cerr << "Repair pipeline produced an empty mesh from " << inputPath << " -- input may be too degenerate to use\n";
        return 1;
    }

    // Closes small gaps (like a single missing face, or slightly
    // mismatched border edges from separately-authored parts of a
    // model) by merging matching boundary edges. Does NOT fix large
    // holes or genuinely non-manifold topology -- that's what the
    // still-unbuilt voxel-grid path is for.
    CGAL::Polygon_mesh_processing::stitch_borders(polyhedron);

    // Mesh_3's polyhedral domain requires a fully triangulated
    // polyhedron — confirmed by testing, not assumed: a quad-faced
    // test box (a genuinely common real-world case, not a contrived
    // one) hit `CGAL::Assertion_exception: "Your input polyhedron must
    // be triangulated!"` at the meshing step before this was added.
    if (!CGAL::is_triangle_mesh(polyhedron)) {
        CGAL::Polygon_mesh_processing::triangulate_faces(polyhedron);
    }

    std::cout << "After repair: " << polyhedron.size_of_vertices() << " verts, "
              << polyhedron.size_of_facets() << " facets";
    std::cout << (polyhedron.is_closed() ? " (closed)\n" : " (still has boundary -- stitch_borders couldn't close every gap)\n");

    if (!polyhedron.is_closed()) {
        // Confirmed by testing, not assumed: feeding a non-closed
        // polyhedron into Mesh_3's polyhedral domain doesn't fail
        // cleanly, it segfaults deep inside CGAL's own CDT code. A
        // genuine hole (missing geometry, not just a fixable winding
        // or triangulation defect) is exactly the case the still-
        // unbuilt voxel-grid path exists for — refusing clearly here
        // is far better than crashing mysteriously three layers down.
        std::cerr << "Input mesh still has open boundary after repair -- "
                     "stitch_borders can only close gaps where matching border "
                     "edges already exist, not genuinely missing geometry. "
                     "The polyhedral (direct-CDT) pipeline requires a fully "
                     "closed mesh and will crash, not just misbehave, if given "
                     "an open one -- refusing rather than risking that. "
                     "This is exactly the case the voxel-grid pipeline "
                     "(not yet implemented -- see README 'Content pipeline: "
                     "CGAL tetrahedralization') is meant to handle.\n";
        return 1;
    }

    Mesh_domain domain(polyhedron);
    // Quality parameters follow CGAL's own documented meaning
    // directly, not tuned blindly -- see the thesis this project's
    // README cites (README "Content pipeline") for the real-world
    // trade-offs these control (facet_distance vs. boundary accuracy,
    // cell_radius_edge_ratio vs. sliver tetrahedra).
    Mesh_criteria criteria(
        CGAL::parameters::facet_angle = 25,
        CGAL::parameters::facet_size = facetSize,
        CGAL::parameters::facet_distance = facetSize * 0.1,
        CGAL::parameters::cell_radius_edge_ratio = 3,
        CGAL::parameters::cell_size = cellSize
    );

    std::cout << "Tetrahedralizing (facet_size=" << facetSize << ", cell_size=" << cellSize << ")...\n";
    C3t3 c3t3 = CGAL::make_mesh_3<C3t3>(domain, criteria);

    // Build a contiguous 0-based vertex index for our own output
    // format -- CGAL's own vertex handles aren't meaningful outside
    // this program.
    std::map<Vertex_handle, int> vertexIndex;
    std::vector<K::Point_3> vertexPositions;
    std::vector<std::array<int, 4>> tets;

    for (auto cellIt = c3t3.cells_in_complex_begin(); cellIt != c3t3.cells_in_complex_end(); ++cellIt) {
        std::array<int, 4> tet;
        for (int i = 0; i < 4; ++i) {
            Vertex_handle v = cellIt->vertex(i);
            auto found = vertexIndex.find(v);
            int idx;
            if (found == vertexIndex.end()) {
                idx = static_cast<int>(vertexPositions.size());
                vertexIndex[v] = idx;
                vertexPositions.push_back(v->point().point());
            } else {
                idx = found->second;
            }
            tet[i] = idx;
        }
        tets.push_back(tet);
    }

    std::cout << "Result: " << vertexPositions.size() << " verts, " << tets.size() << " tets\n";
    if (tets.empty()) {
        std::cerr << "Tetrahedralization produced zero tets -- refusing to write an empty/useless output file\n";
        return 1;
    }

    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "Could not open output file for writing: " << outputPath << "\n";
        return 1;
    }

    // Deliberately hand-written, not via a JSON library -- this format
    // is simple and fixed enough that adding a dependency (even to
    // this standalone, non-shipping tool) isn't worth it. The runtime
    // loader (engine-side, no CGAL) uses the engine's own already-
    // present nlohmann_json to read this same file.
    out << "{\n  \"vertices\": [\n";
    for (size_t i = 0; i < vertexPositions.size(); ++i) {
        const auto& p = vertexPositions[i];
        out << "    [" << p.x() << ", " << p.y() << ", " << p.z() << "]";
        out << (i + 1 < vertexPositions.size() ? ",\n" : "\n");
    }
    out << "  ],\n  \"tets\": [\n";
    for (size_t i = 0; i < tets.size(); ++i) {
        const auto& t = tets[i];
        out << "    [" << t[0] << ", " << t[1] << ", " << t[2] << ", " << t[3] << "]";
        out << (i + 1 < tets.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";

    std::cout << "Wrote " << outputPath << "\n";
    return 0;
}
