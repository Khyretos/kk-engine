#include "kke/LogoIntro.h"
#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/VulkanCheck.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace kke {

namespace {

struct LogoPush {
    glm::mat4 mvp;
    glm::vec4 normalRows[3]; // rotation part of the model matrix, row-wise
    glm::vec4 params;        // x time, y sheen position, z brightness, w unused
};
static_assert(sizeof(LogoPush) == 128, "push constants must stay within the guaranteed 128 bytes");

struct BackdropPush {
    glm::vec4 params; // x time, y aspect, z fade alpha (fade mode) / glow (backdrop), w ray angle
    glm::vec4 mode;   // x: 0 backdrop, 1 fade to black
};

struct TextPush {
    glm::vec4 rect;   // NDC x0, y0, x1, y1
    glm::vec4 params; // x alpha, y line split (v), z sheen position (u), w unused
};

float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float progress(float t, float start, float length) { return clamp01((t - start) / length); }
float easeOutCubic(float x) { return 1.0f - std::pow(1.0f - x, 3.0f); }
float easeInOutCubic(float x) { return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f; }
float easeOutBack(float x) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(x - 1.0f, 3.0f) + c1 * std::pow(x - 1.0f, 2.0f);
}

bool readFile(const char* path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

// When each piece starts arriving, and from where. Index order follows
// logo::buildPieces(): needle_north, needle_south, k_right, k_left,
// crown_stem, crown_loop, ring_top, ring_right, ring_bottom, ring_left, hub.
struct Entrance {
    float start, length;
};
Entrance entranceFor(const std::string& name) {
    if (name == "ring_top") return { 0.30f, 0.85f };
    if (name == "ring_right") return { 0.42f, 0.85f };
    if (name == "ring_bottom") return { 0.54f, 0.85f };
    if (name == "ring_left") return { 0.66f, 0.85f };
    if (name == "k_right" || name == "k_left") return { 1.00f, 0.80f };
    if (name == "crown_loop" || name == "crown_stem") return { 1.25f, 0.75f };
    if (name == "hub") return { 1.55f, 0.45f };
    return { 1.55f, 1.40f }; // the needle: appears, spins, settles on north
}

// The needle's spin: several fast turns easing out, then a damped wobble
// around north, like a real compass settling.
float needleAngle(float t) {
    const Entrance e = entranceFor("needle_north");
    const float spinEnd = e.start + 0.95f;
    if (t < spinEnd) return 5.0f * glm::pi<float>() * (1.0f - easeOutCubic(progress(t, e.start, spinEnd - e.start)));
    const float s = t - spinEnd;
    return -0.32f * std::exp(-4.5f * s) * std::sin(13.0f * s);
}

} // namespace

LogoIntro::LogoIntro(Application& app) : m_app(app), m_device(app.device().device()) {
    for (auto& piece : logo::buildPieces()) {
        PieceGpu gpu;
        gpu.mesh = std::make_unique<Mesh>(app.device(), piece.vertices, piece.indices);
        gpu.name = piece.name;
        gpu.center = piece.center;
        m_pieces.push_back(std::move(gpu));
    }

    const VkRenderPass pass = app.renderer().renderPass();
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    PipelineConfig backdrop;
    backdrop.useVertexInput = false;
    backdrop.cullMode = VK_CULL_MODE_NONE;
    backdrop.depthTestEnable = false;
    backdrop.depthWriteEnable = false;
    backdrop.pushConstantRange = { stages, 0, sizeof(BackdropPush) };
    m_backdrop = std::make_unique<Pipeline>(app.device(), pass, "shaders/logo_backdrop.vert.spv", "shaders/logo_backdrop.frag.spv", backdrop);
    PipelineConfig fade = backdrop;
    fade.blendEnable = true;
    m_fade = std::make_unique<Pipeline>(app.device(), pass, "shaders/logo_backdrop.vert.spv", "shaders/logo_backdrop.frag.spv", fade);

    PipelineConfig logoConfig;
    // Counter-clockwise model triangles stay counter-clockwise on screen
    // (the projection's Y flip and framebuffer Y cancel; see BUG-040).
    logoConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    logoConfig.pushConstantRange = { stages, 0, sizeof(LogoPush) };
    m_logo = std::make_unique<Pipeline>(app.device(), pass, "shaders/logo.vert.spv", "shaders/logo.frag.spv", logoConfig);

    buildTitleTexture();
    if (m_titleTexture) {
        VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool));
        VkDescriptorSetLayout layout = app.materialTextureSetLayout();
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &alloc, &m_titleSet));
        VkDescriptorImageInfo image{ m_titleTexture->sampler(), m_titleTexture->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_titleSet;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

        PipelineConfig text = backdrop;
        text.blendEnable = true;
        text.premultipliedAlpha = true;
        text.pushConstantRange = { stages, 0, sizeof(TextPush) };
        text.descriptorSetLayouts = { layout };
        m_text = std::make_unique<Pipeline>(app.device(), pass, "shaders/logo_text.vert.spv", "shaders/logo_text.frag.spv", text);
    }
}

LogoIntro::~LogoIntro() {
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
}

// "KREATIVE KOMPAS" over a letter-spaced "ENGINE", rasterized once with
// stb_truetype into one white image whose alpha is the glyph coverage.
// The shader colours the two lines (see logo_text.frag).
void LogoIntro::buildTitleTexture() {
    std::vector<unsigned char> font;
    const char* fontPath = "assets/fonts/NotoSans-Bold.ttf";
    if (!readFile(fontPath, font)) {
        log::get("Intro")->warn("'{}' not found next to the executable; the intro plays without its title", fontPath);
        return;
    }
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, font.data(), stbtt_GetFontOffsetForIndex(font.data(), 0))) {
        log::get("Intro")->warn("'{}' is not a usable TrueType font; the intro plays without its title", fontPath);
        return;
    }

    struct Line { const char* text; float pixelHeight; float tracking; };
    const Line lines[2] = { { "KREATIVE KOMPAS", 120.0f, 0.02f }, { "ENGINE", 46.0f, 0.55f } };
    const int pad = 8, gap = 30;
    int widths[2] = {}, heights[2] = {};
    for (int l = 0; l < 2; ++l) {
        const float scale = stbtt_ScaleForPixelHeight(&info, lines[l].pixelHeight);
        float x = 0.0f;
        for (const char* c = lines[l].text; *c; ++c) {
            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&info, *c, &advance, &lsb);
            x += advance * scale + lines[l].tracking * lines[l].pixelHeight;
            if (c[1]) x += stbtt_GetCodepointKernAdvance(&info, c[0], c[1]) * scale;
        }
        widths[l] = static_cast<int>(std::ceil(x - lines[l].tracking * lines[l].pixelHeight));
        heights[l] = static_cast<int>(std::ceil(lines[l].pixelHeight));
    }
    const int width = std::max(widths[0], widths[1]) + 2 * pad;
    const int height = heights[0] + gap + heights[1] + 2 * pad;
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);
    for (size_t i = 0; i < rgba.size(); i += 4) rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;

    int top = pad;
    for (int l = 0; l < 2; ++l) {
        const float scale = stbtt_ScaleForPixelHeight(&info, lines[l].pixelHeight);
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
        // Caps only: centre the cap height in the line box.
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBox(&info, 'H', &x0, &y0, &x1, &y1);
        const float capHeight = y1 * scale;
        const float baseline = top + (heights[l] + capHeight) * 0.5f;
        float x = static_cast<float>(pad + (width - 2 * pad - widths[l]) / 2);
        for (const char* c = lines[l].text; *c; ++c) {
            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&info, *c, &advance, &lsb);
            int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
            stbtt_GetCodepointBitmapBox(&info, *c, scale, scale, &bx0, &by0, &bx1, &by1);
            const int gw = bx1 - bx0, gh = by1 - by0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> glyph(static_cast<size_t>(gw) * gh);
                stbtt_MakeCodepointBitmap(&info, glyph.data(), gw, gh, gw, scale, scale, *c);
                const int ox = static_cast<int>(std::lround(x)) + bx0, oy = static_cast<int>(std::lround(baseline)) + by0;
                for (int gy = 0; gy < gh; ++gy)
                    for (int gx = 0; gx < gw; ++gx) {
                        const int px = ox + gx, py = oy + gy;
                        if (px < 0 || py < 0 || px >= width || py >= height) continue;
                        uint8_t& a = rgba[(static_cast<size_t>(py) * width + px) * 4 + 3];
                        a = std::max(a, glyph[static_cast<size_t>(gy) * gw + gx]);
                    }
            }
            x += advance * scale + lines[l].tracking * lines[l].pixelHeight;
            if (c[1]) x += stbtt_GetCodepointKernAdvance(&info, c[0], c[1]) * scale;
        }
        top += heights[l] + gap;
    }
    m_titleSplit = (pad + heights[0] + gap * 0.5f) / static_cast<float>(height);
    m_titleSize = { static_cast<float>(width), static_cast<float>(height) };
    m_titleTexture = std::make_unique<Texture>(m_app.device(), rgba.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

bool LogoIntro::update(float dt) {
    m_time = std::min(m_time + std::max(dt, 0.0f), kDuration);
    return m_time < kDuration;
}

glm::mat4 LogoIntro::pieceTransform(const PieceGpu& piece, bool& visible) const {
    const float t = m_time;
    const Entrance e = entranceFor(piece.name);
    const float p = progress(t, e.start, e.length);
    visible = t >= e.start;
    const glm::vec3 center(piece.center, 0.0f);
    glm::mat4 m(1.0f);

    if (piece.name.rfind("ring_", 0) == 0) {
        // Fly in along the arc's outward direction, spinning about the
        // logo's axis and tumbling, overshooting slightly into place.
        const float k = easeOutBack(p);
        const glm::vec2 dir = glm::length(piece.center) > 0.0f ? glm::normalize(piece.center) : glm::vec2(0.0f, 1.0f);
        const glm::vec3 offset = glm::vec3(dir * 4.0f, -2.5f) * (1.0f - k);
        m = glm::translate(m, offset);
        m = glm::rotate(m, (1.0f - k) * glm::pi<float>(), glm::vec3(0, 0, 1));
        m = glm::translate(m, center);
        m = glm::rotate(m, (1.0f - easeOutCubic(p)) * 2.0f, glm::normalize(glm::vec3(-dir.y, dir.x, 0.0f)));
        m = glm::translate(m, -center);
    } else if (piece.name.rfind("k_", 0) == 0) {
        // Swing in from the side like a door on the needle's axis.
        const float side = piece.name == "k_right" ? 1.0f : -1.0f;
        const float k = easeOutBack(p);
        m = glm::translate(m, glm::vec3(side * 2.5f * (1.0f - easeOutCubic(p)), 0.0f, 0.0f));
        m = glm::rotate(m, side * (1.0f - k) * glm::half_pi<float>() * 1.4f, glm::vec3(0, 1, 0));
    } else if (piece.name.rfind("crown", 0) == 0) {
        // Drop from above with a small bounce.
        const float drop = 1.0f - easeOutBack(p);
        m = glm::translate(m, glm::vec3(0.0f, drop * 2.2f, 0.0f));
    } else if (piece.name == "hub") {
        const float s = std::max(easeOutBack(p), 0.001f);
        m = glm::scale(m, glm::vec3(s, s, s));
    } else { // needle halves
        const float s = std::max(easeOutBack(progress(t, e.start, 0.35f)), 0.001f);
        m = glm::rotate(m, needleAngle(t), glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(s, s, 1.0f));
    }
    return m;
}

void LogoIntro::render(VkCommandBuffer cmd) {
    const float t = m_time;
    const float aspect = m_app.renderer().aspectRatio();
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Backdrop: dark violet with a glow that swells when the needle settles.
    const float settle = entranceFor("needle_north").start + 0.95f;
    const float glow = 0.35f * easeInOutCubic(progress(t, 0.2f, 1.6f)) +
                       0.65f * easeOutCubic(progress(t, settle - 0.1f, 0.5f)) -
                       0.2f * easeInOutCubic(progress(t, settle + 0.4f, 1.0f));
    BackdropPush bg{ { t, aspect, glow, -0.08f * t }, { 0.0f, 0.0f, 0.0f, 0.0f } };
    m_backdrop->bind(cmd);
    vkCmdPushConstants(cmd, m_backdrop->layout(), stages, 0, sizeof(bg), &bg);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Camera and the whole emblem's motion: it arrives turned away and
    // pulled back, turns to face the viewer, then breathes gently.
    const float approach = easeInOutCubic(progress(t, 0.0f, 2.8f));
    glm::mat4 proj = glm::perspective(glm::radians(30.0f), aspect, 0.1f, 50.0f);
    proj[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 7.2f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    glm::mat4 group(1.0f);
    group = glm::translate(group, glm::vec3(0.0f, 0.52f, -2.5f * (1.0f - approach)));
    group = glm::rotate(group, -0.75f * (1.0f - approach) + 0.07f * std::sin(t * 1.3f) * approach, glm::vec3(0, 1, 0));
    group = glm::rotate(group, 0.18f * (1.0f - approach) + 0.03f * std::sin(t * 0.9f + 1.0f) * approach, glm::vec3(1, 0, 0));
    group = glm::scale(group, glm::vec3(1.0f));
    group = glm::translate(group, glm::vec3(0.0f, -0.24f, 0.0f)); // the logo's visual centre

    // A light sweep crosses the finished emblem.
    const float sheen = -2.5f + 5.0f * easeInOutCubic(progress(t, settle + 0.15f, 0.9f));
    const float brightness = 0.35f + 0.65f * easeOutCubic(progress(t, 0.0f, 1.2f));

    m_logo->bind(cmd);
    for (size_t i = 0; i < m_pieces.size(); ++i) {
        bool visible = false;
        const glm::mat4 model = group * pieceTransform(m_pieces[i], visible);
        if (!visible) continue;
        LogoPush push{};
        push.mvp = proj * view * model;
        const glm::mat3 rot = glm::mat3(model);
        for (int r = 0; r < 3; ++r) push.normalRows[r] = glm::vec4(rot[0][r], rot[1][r], rot[2][r], 0.0f);
        push.params = { t, sheen, brightness, 0.0f };
        vkCmdPushConstants(cmd, m_logo->layout(), stages, 0, sizeof(push), &push);
        m_pieces[i].mesh->bind(cmd);
        m_pieces[i].mesh->draw(cmd);
    }

    // The name, fading in and rising slightly.
    if (m_text) {
        const float a = easeOutCubic(progress(t, settle + 0.25f, 0.8f));
        if (a > 0.0f) {
            const float widthNdc = std::min(0.95f, 0.95f * 1.6f / aspect);
            const float heightNdc = widthNdc * aspect * m_titleSize.y / m_titleSize.x;
            const float top = 0.45f + 0.06f * (1.0f - a);
            TextPush text{ { -widthNdc * 0.5f, top, widthNdc * 0.5f, top + heightNdc },
                           { a, m_titleSplit, -0.3f + 1.6f * progress(t, settle + 0.4f, 1.2f), 0.0f } };
            m_text->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_text->layout(), 0, 1, &m_titleSet, 0, nullptr);
            vkCmdPushConstants(cmd, m_text->layout(), stages, 0, sizeof(text), &text);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }

    // Fade in from black.
    const float fade = 1.0f - easeOutCubic(progress(t, 0.0f, 0.7f));
    if (fade > 0.0f) {
        BackdropPush f{ { t, aspect, fade, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };
        m_fade->bind(cmd);
        vkCmdPushConstants(cmd, m_fade->layout(), stages, 0, sizeof(f), &f);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
}

} // namespace kke
