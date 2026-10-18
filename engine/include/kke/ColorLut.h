#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace kke {

// LUT colour grading (ENG-F10 part 2): a 3D lookup table in the .cube
// format colourists export from DaVinci Resolve, Photoshop or any grading
// tool, applied after the tone map to the display colour. A mood names one
// (`lut: teal_orange`, assets/luts/<name>.cube). Read with tetrahedral
// interpolation (each cell cut into six tetrahedra, four corners weighed
// per lookup), as colour tools do, rather than trilinear: a trilinear read
// bends straight lines through the table and shifts greys; a tetrahedral
// one keeps the diagonal (the greys) exact.
struct ColorLut {
    int size = 0;                  // samples per axis (2..65)
    std::vector<glm::vec3> table;  // size^3 entries, red fastest, then green, then blue (the .cube order)
    std::string title;

    // The table that changes nothing.
    static ColorLut identity(int size);
    // `c` (0..1 per channel, display-encoded) through the table.
    glm::vec3 apply(const glm::vec3& c) const;
};

// Reads .cube text (LUT_3D_SIZE, optional TITLE and DOMAIN_MIN/MAX, then
// size^3 lines of three numbers). False with a message on a bad file.
bool parseCubeLut(const std::string& text, ColorLut& out, std::string& error);

} // namespace kke
