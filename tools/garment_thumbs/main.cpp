// kke_garment_thumbs <makehuman.kkhpack> <out dir> [garment id...]
//
// The pictures in the character creator's clothes browser
// (assets/ui/character_creator/thumbs/<id>.png): every garment of
// kke::garments() cut for the default body, drawn alone from the front,
// lit from the upper left, in its own colour, on a transparent ground. A
// small software rasteriser (no GPU, so it runs in CI and gives the same
// picture everywhere). Re-run it when the wardrobe changes:
//   build/bin/kke_garment_thumbs assets/humans/makehuman.kkhpack assets/ui/character_creator/thumbs
#include "kke/Human.h"
#include "kke/HumanClothes.h"
#include "kke/HumanPack.h"

#include <glm/glm.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 112;  // the picture, px
constexpr int kSuper = 3;   // samples per px each way
constexpr float kPad = 0.07f;

std::string fileName(const std::string& id) {
    std::string out = id;
    for (char& c : out)
        if (c == ' ') c = '_';
    return out + ".png";
}

struct Canvas {
    int w, h;
    std::vector<float> depth; // nearest z (bigger = nearer the viewer)
    std::vector<glm::vec3> colour;
    Canvas(int width, int height) : w(width), h(height), depth(static_cast<size_t>(width) * height, -1e9f), colour(depth.size(), glm::vec3(0.0f)) {}
};

void triangle(Canvas& c, const glm::vec3 p[3], const glm::vec3 n[3], const glm::vec3& base) {
    const float minX = std::min({ p[0].x, p[1].x, p[2].x }), maxX = std::max({ p[0].x, p[1].x, p[2].x });
    const float minY = std::min({ p[0].y, p[1].y, p[2].y }), maxY = std::max({ p[0].y, p[1].y, p[2].y });
    const float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (std::abs(area) < 1e-9f) return;
    const glm::vec3 light = glm::normalize(glm::vec3(-0.45f, 0.55f, 0.7f));
    for (int y = std::max(0, static_cast<int>(std::floor(minY))); y <= std::min(c.h - 1, static_cast<int>(std::ceil(maxY))); ++y)
        for (int x = std::max(0, static_cast<int>(std::floor(minX))); x <= std::min(c.w - 1, static_cast<int>(std::ceil(maxX))); ++x) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const float w0 = ((p[1].x - fx) * (p[2].y - fy) - (p[2].x - fx) * (p[1].y - fy)) / area;
            const float w1 = ((p[2].x - fx) * (p[0].y - fy) - (p[0].x - fx) * (p[2].y - fy)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            const float z = w0 * p[0].z + w1 * p[1].z + w2 * p[2].z;
            float& d = c.depth[static_cast<size_t>(y) * c.w + x];
            if (z <= d) continue;
            d = z;
            glm::vec3 nrm = glm::normalize(w0 * n[0] + w1 * n[1] + w2 * n[2]);
            if (nrm.z < 0.0f) nrm = -nrm; // the inside of a skirt seen through its open hem
            const float lit = 0.38f + 0.62f * std::max(0.0f, glm::dot(nrm, light));
            const float rim = std::pow(1.0f - std::max(0.0f, nrm.z), 3.0f) * 0.25f; // a lighter edge keeps the silhouette readable
            c.colour[static_cast<size_t>(y) * c.w + x] = glm::min(base * lit + glm::vec3(rim), glm::vec3(1.0f));
        }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <makehuman.kkhpack> <out dir> [garment id...]\n", argv[0]);
        return 2;
    }
    try {
        const kke::HumanPack pack = kke::HumanPack::load(argv[1]);
        std::filesystem::create_directories(argv[2]);
        kke::Human human(pack);
        human.set("macrodetails/Gender", 0.0f);
        human.set("macrodetails-proportions/BodyProportions", 0.7f);
        kke::Human::BakeOptions options;
        options.groups = { "body" };
        options.skeleton = false;
        const kke::Human::Bake body = human.bake(options);

        std::vector<std::string> ids;
        for (int i = 3; i < argc; ++i) ids.emplace_back(argv[i]);
        if (ids.empty())
            for (const kke::GarmentInfo& g : kke::garments()) ids.push_back(g.id);

        int made = 0;
        for (const std::string& id : ids) {
            const kke::GarmentInfo* info = kke::findGarment(id);
            if (!info) {
                std::fprintf(stderr, "no garment '%s'\n", id.c_str());
                return 1;
            }
            kke::ModelData model = body.model;
            const size_t first = model.meshes.size();
            kke::dress(model, human, { { id, info->colour } }, false);
            if (model.meshes.size() == first) {
                std::fprintf(stderr, "'%s' has no pieces\n", id.c_str());
                return 1;
            }
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (size_t m = first; m < model.meshes.size(); ++m)
                for (const kke::ModelVertex& v : model.meshes[m].vertices) {
                    lo = glm::min(lo, glm::vec2(v.position));
                    hi = glm::max(hi, glm::vec2(v.position));
                }
            const int big = kSize * kSuper;
            const float span = std::max(hi.x - lo.x, hi.y - lo.y);
            const float scale = static_cast<float>(big) * (1.0f - 2.0f * kPad) / std::max(span, 1e-3f);
            const glm::vec2 mid = 0.5f * (lo + hi);
            auto toScreen = [&](const glm::vec3& v) { return glm::vec3(0.5f * big + (v.x - mid.x) * scale, 0.5f * big - (v.y - mid.y) * scale, v.z); };

            Canvas canvas(big, big);
            for (size_t m = first; m < model.meshes.size(); ++m) {
                const kke::ModelMesh& mesh = model.meshes[m];
                const glm::vec3 base = model.materials[mesh.material].baseColor;
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    glm::vec3 p[3], n[3];
                    for (int k = 0; k < 3; ++k) {
                        const kke::ModelVertex& v = mesh.vertices[mesh.indices[i + static_cast<size_t>(k)]];
                        p[k] = toScreen(v.position);
                        n[k] = v.normal;
                    }
                    triangle(canvas, p, n, base);
                }
            }
            std::vector<unsigned char> rgba(static_cast<size_t>(kSize) * kSize * 4, 0);
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x) {
                    glm::vec3 sum(0.0f);
                    float covered = 0.0f;
                    for (int sy = 0; sy < kSuper; ++sy)
                        for (int sx = 0; sx < kSuper; ++sx) {
                            const size_t at = static_cast<size_t>(y * kSuper + sy) * big + (x * kSuper + sx);
                            if (canvas.depth[at] > -1e8f) {
                                sum += canvas.colour[at];
                                covered += 1.0f;
                            }
                        }
                    if (covered <= 0.0f) continue;
                    const glm::vec3 c = glm::clamp(sum / covered, 0.0f, 1.0f); // the model's colours are sRGB already
                    unsigned char* px = &rgba[(static_cast<size_t>(y) * kSize + x) * 4];
                    for (int k = 0; k < 3; ++k) px[k] = static_cast<unsigned char>(std::lround(c[k] * 255.0f));
                    px[3] = static_cast<unsigned char>(std::lround(255.0f * covered / (kSuper * kSuper)));
                }
            const std::string path = (std::filesystem::path(argv[2]) / fileName(id)).string();
            if (!stbi_write_png(path.c_str(), kSize, kSize, 4, rgba.data(), kSize * 4)) {
                std::fprintf(stderr, "cannot write %s\n", path.c_str());
                return 1;
            }
            ++made;
        }
        std::printf("%d thumbnail(s) in %s\n", made, argv[2]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
