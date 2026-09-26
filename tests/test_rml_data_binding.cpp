// Regression tests for RmlUi data bindings the demos rely on, run against a
// real RmlUi context with a do-nothing renderer so every warning RmlUi logs
// is captured and must be zero.
//
// The bug these guard against: a `data-for` list with `data-class-*` (or
// any other data view) on the same element. When the list shrinks, RmlUi
// updated the rows' class views before the `data-for` view removed the
// stale rows, so each stale row read past the end of the array and logged
//   [rmlui:warn] Data array index out of bounds.
//   [rmlui:warn] Could not get value from data variable 'toasts[0].kind'.
// (spam "Loot" in the RmlUi demo's HUD, then wait for the toasts to
// expire). Fixed by cmake/patches/rmlui-data-for-sort-order.patch.

#include <RmlUi/Core.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

class NullRenderer final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String&) override { dims = { 1, 1 }; return 1; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

class CapturingSystem final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING) problems.push_back(message);
        return true;
    }
    std::vector<std::string> problems;
};

struct Toast {
    Rml::String text;
    Rml::String kind;
};

class RmlDataBinding : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Rml::SetRenderInterface(&s_renderer);
        Rml::SetSystemInterface(&s_system);
        ASSERT_TRUE(Rml::Initialise());
        ASSERT_TRUE(Rml::LoadFontFace(KKE_SOURCE_DIR "/assets/fonts/NotoSans-Regular.ttf"));
    }
    static void TearDownTestSuite() { Rml::Shutdown(); }

    void SetUp() override {
        s_system.problems.clear();
        m_context = Rml::CreateContext("data_binding_test", { 800, 600 });
        ASSERT_NE(m_context, nullptr);
        Rml::DataModelConstructor c = m_context->CreateDataModel("hud");
        if (auto t = c.RegisterStruct<Toast>()) {
            t.RegisterMember("text", &Toast::text);
            t.RegisterMember("kind", &Toast::kind);
        }
        c.RegisterArray<std::vector<Toast>>();
        c.Bind("toasts", &m_toasts);
        m_model = c.GetModelHandle();
    }
    void TearDown() override {
        Rml::RemoveContext("data_binding_test");
        EXPECT_TRUE(s_system.problems.empty()) << "first RmlUi warning: " << s_system.problems.front();
    }

    void load(const char* body) {
        std::string rml = std::string("<rml><head><style>body { font-family: \"Noto Sans\"; font-size: 14px; }</style></head><body data-model=\"hud\">") + body + "</body></rml>";
        Rml::ElementDocument* doc = m_context->LoadDocumentFromMemory(rml);
        ASSERT_NE(doc, nullptr);
        doc->Show();
        m_context->Update();
    }
    void set(std::vector<Toast> toasts) {
        m_toasts = std::move(toasts);
        m_model.DirtyVariable("toasts");
        m_context->Update();
    }
    // The rows RmlUi generated; the data-for element itself stays in the
    // document as a hidden template, so it is skipped.
    Rml::ElementList rowElements(const char* cls) {
        Rml::ElementList all, out;
        m_context->GetDocument(0)->GetElementsByClassName(all, cls);
        for (Rml::Element* e : all) {
            if (!e->HasAttribute("data-for")) out.push_back(e);
        }
        return out;
    }
    int rows(const char* cls) { return static_cast<int>(rowElements(cls).size()); }

    static NullRenderer s_renderer;
    static CapturingSystem s_system;
    Rml::Context* m_context = nullptr;
    Rml::DataModelHandle m_model;
    std::vector<Toast> m_toasts;
};

NullRenderer RmlDataBinding::s_renderer;
CapturingSystem RmlDataBinding::s_system;

// Same markup as games/rmlui_demo/ui/hud.rml's toast list.
constexpr const char* kToastRml =
    "<div class=\"toast\" data-for=\"t : toasts\" data-class-loot=\"t.kind == 'loot'\" "
    "data-class-warn=\"t.kind == 'warn'\">{{ t.text }}</div>";

TEST_F(RmlDataBinding, ShrinkingListToEmptyLogsNothing) {
    load(kToastRml);
    set({ { "a", "loot" }, { "b", "warn" }, { "c", "loot" } });
    EXPECT_EQ(rows("toast"), 3);
    set({});
    EXPECT_EQ(rows("toast"), 0);
}

TEST_F(RmlDataBinding, ShrinkingListByOneLogsNothing) {
    load(kToastRml);
    std::vector<Toast> five(5, Toast{ "x", "loot" });
    set(five);
    for (int n = 4; n >= 0; --n) {
        set(std::vector<Toast>(n, Toast{ "x", "warn" }));
        EXPECT_EQ(rows("toast"), n);
    }
}

TEST_F(RmlDataBinding, StyleAndAttrViewsOnRowsSurviveShrinking) {
    load("<div class=\"row\" data-for=\"t, i : toasts\" data-style-left=\"i + 'px'\" "
         "data-attr-kind=\"t.kind\" data-class-first=\"i == 0\">{{ t.text }}</div>");
    set({ { "a", "loot" }, { "b", "warn" } });
    EXPECT_EQ(rows("row"), 2);
    set({});
    EXPECT_EQ(rows("row"), 0);
}

TEST_F(RmlDataBinding, RowsTrackContentsAfterRegrowing) {
    load(kToastRml);
    set({ { "a", "loot" }, { "b", "warn" } });
    set({});
    set({ { "c", "warn" } });
    Rml::ElementList out = rowElements("toast");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_TRUE(out[0]->IsClassSet("warn"));
    EXPECT_FALSE(out[0]->IsClassSet("loot"));
}

} // namespace
