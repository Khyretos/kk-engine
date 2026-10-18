#include "kke/Smaa.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// The run of edges through (x, y) along one axis, and the heights of its two ends.
struct Run {
    int before = 0, after = 0;
    float startEnd = 0.0f, finishEnd = 0.0f;
};

float endHeight(bool farSide, bool nearSide) { return farSide == nearSide ? 0.0f : (farSide ? 0.5f : -0.5f); }

} // namespace

float smaaLuma(const glm::vec3& light) {
    const glm::vec3 t = glm::max(light, glm::vec3(0.0f)) / (glm::vec3(1.0f) + glm::max(light, glm::vec3(0.0f)));
    return std::sqrt(glm::dot(t, glm::vec3(0.2126f, 0.7152f, 0.0722f)));
}

glm::vec2 smaaLineArea(int before, int after, float leftEnd, float rightEnd) {
    const float length = static_cast<float>(before + after + 1);
    auto height = [&](float t) {
        if (leftEnd * rightEnd < 0.0f) return leftEnd + (rightEnd - leftEnd) * t / length; // a step of a slanted line
        return t <= 0.5f * length ? leftEnd * (1.0f - 2.0f * t / length) : rightEnd * (2.0f * t / length - 1.0f); // L or U
    };
    glm::vec2 area(0.0f); // (far side, this side)
    auto piece = [&](float s, float e) {
        const float y0 = height(s), y1 = height(e), w = e - s;
        if (y0 >= 0.0f && y1 >= 0.0f) {
            area.x += 0.5f * (y0 + y1) * w;
        } else if (y0 <= 0.0f && y1 <= 0.0f) {
            area.y -= 0.5f * (y0 + y1) * w;
        } else {
            const float c = y0 / (y0 - y1); // where the line crosses the edge, 0..1 across the piece
            const float a = 0.5f * y0 * c * w, b = 0.5f * y1 * (1.0f - c) * w;
            area.x += std::max(a, 0.0f) + std::max(b, 0.0f);
            area.y -= std::min(a, 0.0f) + std::min(b, 0.0f);
        }
    };
    const float s = static_cast<float>(before), e = s + 1.0f, mid = 0.5f * length;
    if (mid > s && mid < e) {
        piece(s, mid);
        piece(mid, e);
    } else {
        piece(s, e);
    }
    return area;
}

std::vector<glm::bvec2> smaaEdges(const SmaaImage& image, const SmaaSettings& settings) {
    std::vector<glm::bvec2> edges(static_cast<size_t>(image.width * image.height), glm::bvec2(false));
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const float l = smaaLuma(image.light(x, y));
            glm::bvec2& e = edges[static_cast<size_t>(y * image.width + x)];
            if (x > 0) e.x = std::abs(l - smaaLuma(image.light(x - 1, y))) > settings.threshold;
            if (y > 0) e.y = std::abs(l - smaaLuma(image.light(x, y - 1))) > settings.threshold;
        }
    return edges;
}

std::vector<glm::vec4> smaaWeights(int width, int height, const std::vector<glm::bvec2>& edges, const SmaaSettings& settings) {
    auto at = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return glm::bvec2(false);
        return edges[static_cast<size_t>(y * width + x)];
    };
    // A horizontal edge on top of row `row` through column x (between rows row - 1 and row).
    auto across = [&](int x, int row) {
        Run r;
        while (r.before < settings.maxSearch && at(x - r.before - 1, row).y) ++r.before;
        while (r.after < settings.maxSearch && at(x + r.after + 1, row).y) ++r.after;
        const int x0 = x - r.before, x1 = x + r.after + 1; // the boundaries left of the run and right of it
        if (r.before < settings.maxSearch) r.startEnd = endHeight(at(x0, row - 1).x, at(x0, row).x);
        if (r.after < settings.maxSearch) r.finishEnd = endHeight(at(x1, row - 1).x, at(x1, row).x);
        return smaaLineArea(r.before, r.after, r.startEnd, r.finishEnd);
    };
    // A vertical edge left of column `col` through row y (between columns col - 1 and col).
    auto down = [&](int col, int y) {
        Run r;
        while (r.before < settings.maxSearch && at(col, y - r.before - 1).x) ++r.before;
        while (r.after < settings.maxSearch && at(col, y + r.after + 1).x) ++r.after;
        const int y0 = y - r.before, y1 = y + r.after + 1;
        if (r.before < settings.maxSearch) r.startEnd = endHeight(at(col - 1, y0).y, at(col, y0).y);
        if (r.after < settings.maxSearch) r.finishEnd = endHeight(at(col - 1, y1).y, at(col, y1).y);
        return smaaLineArea(r.before, r.after, r.startEnd, r.finishEnd);
    };
    std::vector<glm::vec4> weights(static_cast<size_t>(width * height), glm::vec4(0.0f));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            glm::vec4 w(0.0f);
            if (at(x, y).x) w.x = down(x, y).y;          // the left neighbour reaches into this pixel
            if (at(x, y).y) w.y = across(x, y).y;        // the top one
            if (at(x + 1, y).x) w.z = down(x + 1, y).x;  // the right one
            if (at(x, y + 1).y) w.w = across(x, y + 1).x; // the bottom one
            const float sum = w.x + w.y + w.z + w.w;
            weights[static_cast<size_t>(y * width + x)] = sum > 1.0f ? w / sum : w;
        }
    return weights;
}

std::vector<glm::vec3> smaaBlend(const SmaaImage& image, const std::vector<glm::vec4>& weights) {
    std::vector<glm::vec3> out(static_cast<size_t>(image.width * image.height));
    auto light = [&](int x, int y) { return image.light(std::clamp(x, 0, image.width - 1), std::clamp(y, 0, image.height - 1)); };
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const glm::vec4 w = weights[static_cast<size_t>(y * image.width + x)];
            out[static_cast<size_t>(y * image.width + x)] = light(x, y) * (1.0f - w.x - w.y - w.z - w.w) + light(x - 1, y) * w.x +
                                                          light(x, y - 1) * w.y + light(x + 1, y) * w.z + light(x, y + 1) * w.w;
        }
    return out;
}

} // namespace kke
