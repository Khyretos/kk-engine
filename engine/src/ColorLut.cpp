#include "kke/ColorLut.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace kke {

ColorLut ColorLut::identity(int size) {
    ColorLut lut;
    lut.size = size;
    lut.table.resize(static_cast<size_t>(size * size * size));
    const float d = 1.0f / static_cast<float>(size - 1);
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r)
                lut.table[static_cast<size_t>((b * size + g) * size + r)] =
                    glm::vec3(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)) * d;
    return lut;
}

glm::vec3 ColorLut::apply(const glm::vec3& c) const {
    if (size < 2) return c;
    const glm::vec3 p = glm::clamp(c, 0.0f, 1.0f) * static_cast<float>(size - 1);
    const glm::ivec3 i = glm::min(glm::ivec3(p), glm::ivec3(size - 2));
    const glm::vec3 f = p - glm::vec3(i);
    auto at = [&](int dr, int dg, int db) { return table[static_cast<size_t>(((i.z + db) * size + i.y + dg) * size + i.x + dr)]; };
    // The tetrahedron of the cell's six that holds f (Kasson et al. 1995), by the order of its coordinates.
    const glm::vec3 c000 = at(0, 0, 0), c111 = at(1, 1, 1);
    if (f.x >= f.y) {
        if (f.y >= f.z) return c000 + (at(1, 0, 0) - c000) * f.x + (at(1, 1, 0) - at(1, 0, 0)) * f.y + (c111 - at(1, 1, 0)) * f.z;
        if (f.x >= f.z) return c000 + (at(1, 0, 0) - c000) * f.x + (at(1, 0, 1) - at(1, 0, 0)) * f.z + (c111 - at(1, 0, 1)) * f.y;
        return c000 + (at(0, 0, 1) - c000) * f.z + (at(1, 0, 1) - at(0, 0, 1)) * f.x + (c111 - at(1, 0, 1)) * f.y;
    }
    if (f.z >= f.y) return c000 + (at(0, 0, 1) - c000) * f.z + (at(0, 1, 1) - at(0, 0, 1)) * f.y + (c111 - at(0, 1, 1)) * f.x;
    if (f.z >= f.x) return c000 + (at(0, 1, 0) - c000) * f.y + (at(0, 1, 1) - at(0, 1, 0)) * f.z + (c111 - at(0, 1, 1)) * f.x;
    return c000 + (at(0, 1, 0) - c000) * f.y + (at(1, 1, 0) - at(0, 1, 0)) * f.x + (c111 - at(1, 1, 0)) * f.z;
}

bool parseCubeLut(const std::string& text, ColorLut& out, std::string& error) {
    std::istringstream in(text);
    std::string line;
    ColorLut lut;
    glm::vec3 lo(0.0f), hi(1.0f);
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream ls(line.substr(first));
        std::string word;
        ls >> word;
        if (word == "TITLE") {
            const size_t q0 = line.find('"'), q1 = line.rfind('"');
            if (q0 != std::string::npos && q1 > q0) lut.title = line.substr(q0 + 1, q1 - q0 - 1);
        } else if (word == "LUT_3D_SIZE") {
            if (!(ls >> lut.size) || lut.size < 2 || lut.size > 65) {
                error = "line " + std::to_string(lineNo) + ": LUT_3D_SIZE must be 2..65";
                return false;
            }
            lut.table.reserve(static_cast<size_t>(lut.size * lut.size * lut.size));
        } else if (word == "DOMAIN_MIN") {
            ls >> lo.x >> lo.y >> lo.z;
        } else if (word == "DOMAIN_MAX") {
            ls >> hi.x >> hi.y >> hi.z;
        } else if (word == "LUT_1D_SIZE") {
            error = "line " + std::to_string(lineNo) + ": 1D LUTs aren't supported, only 3D (LUT_3D_SIZE)";
            return false;
        } else if (std::isdigit(static_cast<unsigned char>(word[0])) || word[0] == '-' || word[0] == '.') {
            glm::vec3 v;
            std::istringstream vs(line.substr(first));
            if (!(vs >> v.x >> v.y >> v.z)) {
                error = "line " + std::to_string(lineNo) + ": three numbers expected";
                return false;
            }
            lut.table.push_back((v - lo) / glm::max(hi - lo, glm::vec3(1e-6f)));
        } else {
            error = "line " + std::to_string(lineNo) + ": unknown keyword '" + word + "'";
            return false;
        }
    }
    if (lut.size == 0) {
        error = "no LUT_3D_SIZE";
        return false;
    }
    if (lut.table.size() != static_cast<size_t>(lut.size * lut.size * lut.size)) {
        error = "LUT_3D_SIZE " + std::to_string(lut.size) + " needs " + std::to_string(lut.size * lut.size * lut.size) + " entries, the file has " +
                std::to_string(lut.table.size());
        return false;
    }
    out = std::move(lut);
    return true;
}

} // namespace kke
