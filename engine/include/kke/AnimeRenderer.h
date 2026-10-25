#pragma once

#include "kke/Buffer.h"
#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/Renderer.h"
#include "kke/Vrm.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace kke {

class Application;
class Texture;

// Draws a VRM avatar the way anime games do (docs/ANIME.md): VRM's MToon
// materials (lit colour, shade colour, where and how hard light turns to
// shade, rim light, matcap, emission), pushed towards the gacha look
// (Genshin Impact, Blue Protocol):
//
// - Hard two-tone light: the shade line is crisp, its colour a warm or
//   cool tone of the material's own, never grey.
// - The face lit as one smooth shape (its normals bent towards a sphere
//   round the head), so the shadow line slides across it as the light
//   turns, with no blotches from the nose and lips.
// - An "angel ring" on the hair: a band of light that stays round the
//   head as the camera moves.
// - Outlines (the inverted hull, along normals averaged across seams so
//   the line never splits), in a dark tone of the surface's colour,
//   thinner on the face.
//
// The vertices come from VrmAvatar::pose (skinning and expressions on the
// CPU) each frame:
//
//   AnimeRenderer anime(app);
//   anime.build(avatar);
//   avatar.pose(locals, weights, posed);
//   anime.update(posed, figure, head);   // head: the head bone's model-space transform
//   anime.draw(ctx);  anime.drawShadow(shadowCtx);
//
// While a frame may still draw it, destroy it through renderer().retire().
class AnimeRenderer {
public:
    // The style on top of the avatar's own materials.
    struct Look {
        float toony = 0.92f;        // at least this hard a shade line (MToon's shadeToony is the file's)
        float faceSmooth = 0.75f;   // 0 = the face's own normals, 1 = a sphere's
        float outline = 0.12f;      // outline width in cm where the material has none (eyes never)
        float faceOutline = 0.5f;   // the face's outline, a share of that
        float ring = 0.6f;          // the hair's angel ring, 0 = none
        float rim = 0.35f;          // rim light
        glm::vec3 tint{ 1.0f };     // multiplies every material's lit colour
    };

    explicit AnimeRenderer(Application& app);
    ~AnimeRenderer();
    AnimeRenderer(const AnimeRenderer&) = delete;
    AnimeRenderer& operator=(const AnimeRenderer&) = delete;

    void build(const VrmAvatar& avatar);
    void setLook(const Look& look) { m_look = look; }
    const Look& look() const { return m_look; }
    // A material's lit and shade colours multiplied (a hair or eye colour
    // slider): `name` matches materials whose name contains it ("HAIR").
    void tintMaterials(const std::string& name, const glm::vec3& tint);
    // posed: VrmAvatar::pose's output (model space); figure: model -> world;
    // head: the head bone's model-space transform (for the face's sphere).
    void update(const std::vector<std::vector<ModelVertex>>& posed, const glm::mat4& figure, const glm::mat4& head);
    void setVisible(bool visible) { m_visible = visible; }
    bool visible() const { return m_visible; }

    void draw(const RenderContext& ctx);
    void drawShadow(const ShadowRenderContext& ctx);

private:
    enum class Kind : uint32_t { Other = 0, Face = 1, Hair = 2, Eye = 3, FacePart = 4 };
    struct Part {
        uint32_t mesh = 0, material = 0;
        uint32_t indexCount = 0;
        std::unique_ptr<Buffer> indices;
        uint64_t vertexOffset = 0; // bytes into the frame's vertex buffer
    };
    struct Material {
        VrmToon toon;
        Kind kind = Kind::Other;
        glm::vec3 tint{ 1.0f };
        std::unique_ptr<Buffer> params;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct FrameData {
        std::unique_ptr<Buffer> vertices;
        size_t capacity = 0;
        uint64_t version = 0;
    };
    struct Shared;
    void writeParams(Material& m);
    void upload(uint32_t frame);

    Application& m_app;
    std::shared_ptr<Shared> m_shared;
    Look m_look;
    std::vector<Part> m_parts;
    std::vector<Material> m_materials;
    std::vector<std::vector<uint32_t>> m_weld; // per mesh: each vertex's first twin at the same place (outline normals)
    std::vector<uint8_t> m_bytes;    // this frame's vertices (kke::Vertex), as uploaded
    std::vector<glm::vec3> m_smooth; // scratch
    uint64_t m_version = 0;
    FrameData m_frames[Renderer::kMaxFramesInFlight];
    glm::mat4 m_figure{ 1.0f };
    glm::vec3 m_headCentre{ 0.0f }, m_headForward{ 0.0f, 0.0f, 1.0f };
    bool m_visible = true;
    bool m_built = false;
};

} // namespace kke
