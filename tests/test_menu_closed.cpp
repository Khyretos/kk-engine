// A closed menu draws nothing. The game shell's menu document has a dimmed
// backdrop on its body; it was hidden before Show(), which makes a document
// visible again, so the backdrop stayed over the game and over every other
// panel while no menu was open (Kees, synty_demo, 2026-10-18: the Characters
// panel looked washed out, as if screen effects reached the UI).
// Run against a real RmlUi context with a renderer that counts draws.

#include "kke/modules/GameShellModule.h"

#include <RmlUi/Core.h>
#include <gtest/gtest.h>

namespace {

class CountingRenderer final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override { ++draws; }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    // Gradients are shaders in RmlUi 6.
    Rml::CompiledShaderHandle CompileShader(const Rml::String&, const Rml::Dictionary&) override { return 1; }
    void RenderShader(Rml::CompiledShaderHandle, Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override { ++draws; }
    void ReleaseShader(Rml::CompiledShaderHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String&) override { dims = { 1, 1 }; return 1; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    int draws = 0;
};

class QuietSystem final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type, const Rml::String&) override { return true; }
};

CountingRenderer s_renderer;
QuietSystem s_system;

// The shell's backdrop, without text (no font needed).
const char* kBackdrop = R"(<rml><head><style>
    body { width: 100%; height: 100%; decorator: radial-gradient(circle farthest-corner at 30% 50%, #05070e55 20%, #05070ee6 100%); }
</style></head><body></body></rml>)";

class MenuClosed : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Rml::SetRenderInterface(&s_renderer);
        Rml::SetSystemInterface(&s_system);
        ASSERT_TRUE(Rml::Initialise());
    }
    static void TearDownTestSuite() { Rml::Shutdown(); }
    void SetUp() override {
        m_context = Rml::CreateContext("menu_closed_test", { 800, 600 });
        ASSERT_NE(m_context, nullptr);
    }
    void TearDown() override { Rml::RemoveContext("menu_closed_test"); }

    int draws() {
        m_context->Update();
        s_renderer.draws = 0;
        m_context->Render();
        return s_renderer.draws;
    }
    Rml::Context* m_context = nullptr;
};

} // namespace

TEST_F(MenuClosed, ClosedMenuDrawsNoBackdrop) {
    Rml::ElementDocument* doc = m_context->LoadDocumentFromMemory(kBackdrop);
    ASSERT_NE(doc, nullptr);
    kke::GameShellModule::showClosed(doc);
    EXPECT_EQ(draws(), 0);
}

TEST_F(MenuClosed, HidingBeforeShowLeavesTheBackdrop) {
    // The old order, so the test above is known to see the backdrop when it is there.
    Rml::ElementDocument* doc = m_context->LoadDocumentFromMemory(kBackdrop);
    ASSERT_NE(doc, nullptr);
    doc->SetProperty("visibility", "hidden");
    doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    EXPECT_GT(draws(), 0);
}
