#pragma once

#include "kke/Module.h"
#include "kke/Pipeline.h"

#include <memory>

namespace kke {

// Draws a fading, anti-aliased reference grid on the Y=0 plane. Exists
// mostly so you have *some* fixed reference in the scene to judge scale,
// rotation, and perspective by — point at this module if you want an
// example of the minimal shape a rendering module takes: own a Pipeline,
// implement render(), draw with no vertex/index buffers at all.
class GridModule : public Module {
public:
    const char* name() const override { return "Grid"; }

    void init(Application& app) override;
    void render(const RenderContext& ctx) override;

private:
    std::unique_ptr<Pipeline> m_pipeline;
};

} // namespace kke
