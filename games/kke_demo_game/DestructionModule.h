#pragma once

#include "kke/Module.h"
#include "kke/Capabilities.h"
#include "kke/Pipeline.h"
#include "kke/Mesh.h"

#include <cstdint>
#include <memory>

namespace kke_demo {

// Demonstrates the "generate from a seed, replicate the seed instead of
// the result" pattern. When triggered, this "shatters" into a fixed
// number of fragments whose positions are a *pure function* of
// (seed, fragment index, elapsed simulation time since the trigger tick)
// — nothing about a fragment's transform is stored per-fragment or
// integrated frame-to-frame with any hidden state. That's what makes
// re-deriving the whole effect from just {seed, triggerTick} correct on
// a peer that receives only those two numbers.
//
// Implements kke::INetworkReplicable so a NetworkModule, if one exists in
// the same Application, can discover and use this without either module
// including the other's header. Declares no dependencies() — it works
// identically whether or not anything else is present.
class DestructionModule : public kke::Module, public kke::INetworkReplicable {
public:
    explicit DestructionModule(uint32_t fragmentCount = 16, uint64_t seed = 1234);

    // kke::Module
    const char* name() const override { return "Destruction"; }
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderUi() override;

    // kke::INetworkReplicable — this is the entire "over the wire" payload:
    // 8 bytes seed + 1 byte triggered flag + 8 bytes trigger tick, however
    // many fragments there are or however complex their motion looks.
    std::string replicationChannelName() const override { return "destruction.demo"; }
    std::vector<uint8_t> serializeReplicatedState() override;
    void deserializeReplicatedState(const std::vector<uint8_t>& data) override;

private:
    void trigger(uint64_t atTick);

    uint32_t m_fragmentCount;
    uint64_t m_seed;

    bool m_triggered = false;
    uint64_t m_triggerTick = 0;
    uint64_t m_currentTick = 0;
    float m_fixedDt = 1.0f / 60.0f;

    std::unique_ptr<kke::Pipeline> m_pipeline;
    std::unique_ptr<kke::Mesh> m_fragmentMesh;
};

} // namespace kke_demo
