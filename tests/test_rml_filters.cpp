// ENG-G09: the CPU half of the RmlUi Vulkan backend's layers and filters
// (kke/RmlFilters.h). The GPU half (layer targets, shaders/rml_filter.frag)
// is checked by the picture check on rmlui_demo; the shader's maths are
// line-for-line copies of the functions tested here.

#include "kke/RmlFilters.h"

#include <RmlUi/Core.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

using namespace kke;

namespace {

Rml::Dictionary value(float v) {
    return Rml::Dictionary{ { "value", Rml::Variant(v) } };
}

void expectNear(glm::vec4 a, glm::vec4 b, float eps = 1e-4f) {
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(a[i], b[i], eps) << "channel " << i;
}

} // namespace

TEST(RmlFilters, UnknownNameIsInvalid) {
    EXPECT_EQ(compileRmlFilter("wobble", {}).type, RmlFilterType::Invalid);
}

TEST(RmlFilters, OpacityIsClampedToOne) {
    const RmlFilter f = compileRmlFilter("opacity", value(1.5f));
    EXPECT_EQ(f.type, RmlFilterType::Opacity);
    EXPECT_FLOAT_EQ(f.opacity, 1.0f);
    EXPECT_FLOAT_EQ(compileRmlFilter("opacity", value(0.25f)).opacity, 0.25f);
}

TEST(RmlFilters, BlurAndDropShadowKeepTheirParameters) {
    const RmlFilter blur = compileRmlFilter("blur", Rml::Dictionary{ { "sigma", Rml::Variant(4.0f) } });
    EXPECT_EQ(blur.type, RmlFilterType::Blur);
    EXPECT_FLOAT_EQ(blur.sigma, 4.0f);

    const RmlFilter shadow = compileRmlFilter("drop-shadow", Rml::Dictionary{ { "sigma", Rml::Variant(2.0f) },
                                                                              { "color", Rml::Variant(Rml::Colourb(255, 0, 0, 128)) },
                                                                              { "offset", Rml::Variant(Rml::Vector2f(3.0f, -4.0f)) } });
    EXPECT_EQ(shadow.type, RmlFilterType::DropShadow);
    EXPECT_FLOAT_EQ(shadow.sigma, 2.0f);
    expectNear(shadow.color, glm::vec4(128.0f / 255.0f, 0.0f, 0.0f, 128.0f / 255.0f));
    EXPECT_FLOAT_EQ(shadow.offset.x, 3.0f);
    EXPECT_FLOAT_EQ(shadow.offset.y, -4.0f);
}

TEST(RmlFilters, LinearPremultipliedDecodesSrgb) {
    // sRGB 128 is linear 0.2158.
    expectNear(rmlLinearPremultiplied(Rml::Colourb(128, 128, 128, 255)), glm::vec4(0.2158f, 0.2158f, 0.2158f, 1.0f));
    expectNear(rmlLinearPremultiplied(Rml::Colourb(255, 255, 255, 0)), glm::vec4(0.0f));
}

TEST(RmlFilters, IdentityValuesLeaveColoursAlone) {
    const glm::vec4 c(0.3f, 0.1f, 0.05f, 0.8f);
    for (const char* name : { "brightness", "contrast", "saturate" }) {
        const RmlFilter f = compileRmlFilter(name, value(1.0f));
        ASSERT_EQ(f.type, RmlFilterType::ColorMatrix) << name;
        expectNear(rmlApplyColorMatrix(f.matrix, c), c);
    }
    for (const char* name : { "invert", "grayscale", "sepia", "hue-rotate" }) {
        const RmlFilter f = compileRmlFilter(name, value(0.0f));
        ASSERT_EQ(f.type, RmlFilterType::ColorMatrix) << name;
        expectNear(rmlApplyColorMatrix(f.matrix, c), c);
    }
}

TEST(RmlFilters, MatricesWorkInSrgbSpace) {
    // brightness(0.5) halves the sRGB value: white becomes sRGB 0.5, linear 0.2140.
    const RmlFilter dim = compileRmlFilter("brightness", value(0.5f));
    expectNear(rmlApplyColorMatrix(dim.matrix, glm::vec4(1.0f)), glm::vec4(0.2140f, 0.2140f, 0.2140f, 1.0f));
    // invert(1) turns black into white and keeps alpha.
    const RmlFilter inv = compileRmlFilter("invert", value(1.0f));
    expectNear(rmlApplyColorMatrix(inv.matrix, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f)), glm::vec4(0.5f, 0.5f, 0.5f, 0.5f));
    // grayscale(1): pure red becomes its luminance weight in sRGB.
    const RmlFilter grey = compileRmlFilter("grayscale", value(1.0f));
    const glm::vec4 g = rmlApplyColorMatrix(grey.matrix, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    const float expected = std::pow((0.2126f + 0.055f) / 1.055f, 2.4f);
    expectNear(g, glm::vec4(expected, expected, expected, 1.0f));
    // A transparent pixel stays transparent whatever the matrix adds.
    expectNear(rmlApplyColorMatrix(inv.matrix, glm::vec4(0.0f)), glm::vec4(0.0f));
}

TEST(RmlFilters, BlurPlanSplitsWideBlurs) {
    EXPECT_EQ(rmlBlurPlan(0.0f).passes, 0);
    const RmlBlurPlan small = rmlBlurPlan(2.0f);
    EXPECT_EQ(small.passes, 1);
    EXPECT_FLOAT_EQ(small.sigma, 2.0f);
    EXPECT_EQ(small.radius, 6);
    const RmlBlurPlan wide = rmlBlurPlan(20.0f);
    EXPECT_GT(wide.passes, 1);
    EXPECT_LE(wide.radius, kRmlMaxBlurRadius);
    // The passes add up to the asked sigma: sigma^2 adds per Gaussian pass.
    EXPECT_NEAR(wide.sigma * wide.sigma * static_cast<float>(wide.passes), 400.0f, 1e-2f);
}

TEST(RmlFilters, BlurWeightsSumToOneAndFallOff) {
    const std::vector<float> w = rmlBlurWeights(3.0f, 9);
    ASSERT_EQ(w.size(), 10u);
    float total = w[0];
    for (size_t i = 1; i < w.size(); ++i) {
        total += 2.0f * w[i];
        EXPECT_LT(w[i], w[i - 1]);
    }
    EXPECT_NEAR(total, 1.0f, 1e-5f);
}

TEST(RmlFilters, PushBlockMatchesTheShader) {
    EXPECT_EQ(offsetof(RmlFilterPush, color), 64u);
    EXPECT_EQ(offsetof(RmlFilterPush, offset), 80u);
    EXPECT_EQ(offsetof(RmlFilterPush, uvMin), 88u);
    EXPECT_EQ(offsetof(RmlFilterPush, uvMax), 96u);
    EXPECT_EQ(offsetof(RmlFilterPush, sigma), 104u);
    EXPECT_EQ(offsetof(RmlFilterPush, radius), 108u);
    EXPECT_EQ(offsetof(RmlFilterPush, mode), 112u);
    EXPECT_EQ(offsetof(RmlFilterPush, opacity), 116u);
}

namespace {

// Records the layer calls RmlUi makes and compiles its filters with
// compileRmlFilter, the way RmlVulkanRenderInterface does.
class LayerRecorder final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String&) override { dims = { 1, 1 }; return 1; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    void EnableClipMask(bool) override {}
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle, Rml::Vector2f) override {}
    Rml::LayerHandle PushLayer() override {
        ++pushes;
        return static_cast<Rml::LayerHandle>(++depth);
    }
    void CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode, Rml::Span<const Rml::CompiledFilterHandle>) override { ++composites; }
    void PopLayer() override { --depth; }
    Rml::TextureHandle SaveLayerAsTexture() override {
        ++savedTextures;
        return 2;
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override {
        names.push_back(name);
        if (compileRmlFilter(name, parameters).type == RmlFilterType::Invalid) invalid.push_back(name);
        return 1;
    }
    void ReleaseFilter(Rml::CompiledFilterHandle) override {}

    int pushes = 0, depth = 0, composites = 0, savedTextures = 0;
    std::vector<std::string> names, invalid;
};

} // namespace

TEST(RmlFilters, EveryCssFilterRmlUiSendsCompiles) {
    LayerRecorder recorder;
    Rml::SetRenderInterface(&recorder);
    ASSERT_TRUE(Rml::Initialise());
    const std::filesystem::path sourceFont = KKE_SOURCE_DIR "/assets/fonts/NotoSans-Regular.ttf";
    const std::string font = std::filesystem::exists(sourceFont) ? sourceFont.string() : "assets/fonts/NotoSans-Regular.ttf";
    ASSERT_TRUE(Rml::LoadFontFace(font));
    Rml::Context* context = Rml::CreateContext("filters_test", { 400, 300 });
    ASSERT_NE(context, nullptr);
    const char* rml = R"(<rml><head><style>
        body { font-family: "Noto Sans"; font-size: 14px; }
        div { width: 50px; height: 20px; background-color: #336699; }
        #a { filter: opacity(0.5) blur(2px) drop-shadow(#000a 2px 3px 4px) brightness(1.2) contrast(0.8); }
        #b { filter: invert(0.3) grayscale(0.5) sepia(0.2) hue-rotate(30deg) saturate(1.5); }
        #c { backdrop-filter: blur(6px); }
        #d { box-shadow: #0008 4px 4px 8px 2px; }
        </style></head><body><div id="a"/><div id="b"/><div id="c"/><div id="d"/></body></rml>)";
    Rml::ElementDocument* doc = context->LoadDocumentFromMemory(rml);
    ASSERT_NE(doc, nullptr);
    doc->Show();
    context->Update();
    context->Render();

    EXPECT_TRUE(recorder.invalid.empty()) << "not compiled: " << recorder.invalid.front();
    for (const char* name : { "opacity", "blur", "drop-shadow", "brightness", "contrast", "invert", "grayscale", "sepia", "hue-rotate", "saturate" }) {
        EXPECT_NE(std::find(recorder.names.begin(), recorder.names.end(), name), recorder.names.end()) << name << " never compiled";
    }
    EXPECT_GT(recorder.pushes, 0);
    EXPECT_EQ(recorder.depth, 0) << "every PushLayer has its PopLayer";
    EXPECT_GT(recorder.composites, 0);
    EXPECT_EQ(recorder.savedTextures, 1) << "the box-shadow is drawn into a saved layer";

    Rml::RemoveContext("filters_test");
    Rml::Shutdown();
}
