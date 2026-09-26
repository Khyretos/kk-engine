#pragma once

#include "kke/LogoMesh.h"
#include "kke/Mesh.h"
#include "kke/Pipeline.h"
#include "kke/Texture.h"

#include <volk.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace kke {

class Application;

// The engine's startup intro: the Kreative Kompas logo, extruded into a
// bevelled 3D emblem, assembles itself piece by piece (the ring's arcs fly
// in, the Ks swing into place, the crown drops, the needle spins and
// settles on north), a light sweep crosses it and the name fades in
// underneath. About 4.5 seconds; any key, click or touch skips it. Application::run() plays it before any module's init(), so the last
// frame (logo and name) stays on screen while the game loads.
//
// Turn it off per game with Application::setIntroEnabled(false), or for
// one run with KKE_SKIP_INTRO=1 (benchmarks, automated screenshots).
class LogoIntro {
public:
    explicit LogoIntro(Application& app);
    ~LogoIntro();

    LogoIntro(const LogoIntro&) = delete;
    LogoIntro& operator=(const LogoIntro&) = delete;

    static constexpr float kDuration = 4.5f;

    // Advances the animation; false once it has finished.
    bool update(float dt);
    void skip() { m_time = kDuration; }
    float time() const { return m_time; }
    // Jump to a point in the animation (screenshots, tests).
    void setTime(float t) { m_time = t; }

    // Draws the whole screen (backdrop, logo, name) inside the scene render pass.
    void render(VkCommandBuffer cmd);

private:
    struct PieceGpu {
        std::unique_ptr<Mesh> mesh;
        std::string name;
        glm::vec2 center{0.0f};
    };

    // Model matrix of one piece at the current time; `visible` false before it arrives.
    glm::mat4 pieceTransform(const PieceGpu& piece, bool& visible) const;
    void buildTitleTexture();

    Application& m_app;
    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<PieceGpu> m_pieces;
    std::unique_ptr<Pipeline> m_backdrop, m_logo, m_text, m_fade;
    std::unique_ptr<Texture> m_titleTexture;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_titleSet = VK_NULL_HANDLE;
    glm::vec2 m_titleSize{0.0f}; // pixels
    float m_titleSplit = 0.5f;    // v coordinate between the two text lines
    float m_time = 0.0f;
};

} // namespace kke
