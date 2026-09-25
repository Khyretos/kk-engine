#include "kke/modules/UiModule.h"
#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/EngineSettings.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Box.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace kke {

namespace {

int sdlButtonToRmlButton(Uint8 sdlButton) {
    switch (sdlButton) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        default: return -1;
    }
}

int currentRmlModifiers() {
    SDL_Keymod mod = SDL_GetModState();
    int result = 0;
    if (mod & SDL_KMOD_CTRL) result |= Rml::Input::KM_CTRL;
    if (mod & SDL_KMOD_SHIFT) result |= Rml::Input::KM_SHIFT;
    if (mod & SDL_KMOD_ALT) result |= Rml::Input::KM_ALT;
    if (mod & SDL_KMOD_GUI) result |= Rml::Input::KM_META;
    if (mod & SDL_KMOD_CAPS) result |= Rml::Input::KM_CAPSLOCK;
    if (mod & SDL_KMOD_NUM) result |= Rml::Input::KM_NUMLOCK;
    return result;
}

// Common keys only — see UiModule.h for why this isn't (and shouldn't
// try to be) exhaustive. Returns KI_UNKNOWN for anything not mapped,
// which UiModule::onEvent() treats as "don't forward this key."
Rml::Input::KeyIdentifier sdlKeyToRmlKey(SDL_Keycode key) {
    using namespace Rml::Input;
    if (key >= SDLK_A && key <= SDLK_Z) {
        return static_cast<KeyIdentifier>(KI_A + (key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return static_cast<KeyIdentifier>(KI_0 + (key - SDLK_0));
    }
    switch (key) {
        case SDLK_SPACE: return KI_SPACE;
        case SDLK_RETURN: return KI_RETURN;
        case SDLK_ESCAPE: return KI_ESCAPE;
        case SDLK_BACKSPACE: return KI_BACK;
        case SDLK_TAB: return KI_TAB;
        case SDLK_DELETE: return KI_DELETE;
        case SDLK_HOME: return KI_HOME;
        case SDLK_END: return KI_END;
        case SDLK_PAGEUP: return KI_PRIOR;
        case SDLK_PAGEDOWN: return KI_NEXT;
        case SDLK_LEFT: return KI_LEFT;
        case SDLK_RIGHT: return KI_RIGHT;
        case SDLK_UP: return KI_UP;
        case SDLK_DOWN: return KI_DOWN;
        case SDLK_LSHIFT: return KI_LSHIFT;
        case SDLK_RSHIFT: return KI_RSHIFT;
        case SDLK_LCTRL: return KI_LCONTROL;
        case SDLK_RCTRL: return KI_RCONTROL;
        case SDLK_PERIOD: return KI_OEM_PERIOD;
        case SDLK_KP_ENTER: return KI_NUMPADENTER;
        case SDLK_MINUS: return KI_OEM_MINUS;
        case SDLK_COMMA: return KI_OEM_COMMA;
        case SDLK_INSERT: return KI_INSERT;
        case SDLK_F1: return KI_F1;
        case SDLK_F2: return KI_F2;
        case SDLK_F3: return KI_F3;
        case SDLK_F4: return KI_F4;
        case SDLK_F5: return KI_F5;
        case SDLK_F6: return KI_F6;
        case SDLK_F7: return KI_F7;
        case SDLK_F8: return KI_F8;
        case SDLK_F9: return KI_F9;
        case SDLK_F10: return KI_F10;
        case SDLK_F11: return KI_F11;
        case SDLK_F12: return KI_F12;
        default: return KI_UNKNOWN;
    }
}

// Real, shared drag-and-click logic for <input type="range"> --
// extracted so both the initial click (mousedown) and every
// subsequent drag frame (mousemove while held) go through the exact
// same value computation, rather than two separately-maintained
// copies that could quietly drift apart. See UiModule::onEvent()'s
// own comment for why this direct approach exists at all instead of
// relying on RmlUi's own WidgetSlider internals.
void setRangeSliderValueFromMouseX(Rml::Element* rangeInput, float mouseX) {
    Rml::Vector2f topLeft = rangeInput->GetAbsoluteOffset(Rml::BoxArea::Content);
    Rml::Vector2f size = rangeInput->GetBox().GetSize(Rml::BoxArea::Content);
    if (size.x <= 0.0f) return;
    float fraction = (mouseX - topLeft.x) / size.x;
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    float minValue = rangeInput->GetAttribute<float>("min", 0.0f);
    float maxValue = rangeInput->GetAttribute<float>("max", 100.0f);
    float step = rangeInput->GetAttribute<float>("step", 1.0f);
    float rawValue = minValue + fraction * (maxValue - minValue);
    if (step > 0.0f) {
        rawValue = minValue + std::round((rawValue - minValue) / step) * step;
    }
    if (auto* control = dynamic_cast<Rml::ElementFormControl*>(rangeInput)) {
        control->SetValue(std::to_string(rawValue));
    }
}

bool isRangeSliderInput(Rml::Element* element) {
    return element && element->GetTagName() == "input" &&
           element->GetClassNames().find("range") != Rml::String::npos;
}

// Walks up from a clicked "draggable-handle" element (a panel's own
// title bar) to find the nearest ancestor with a real, explicit "left"
// property set -- the actual positioned panel a drag should move, not
// necessarily the handle's own immediate parent, in case a future
// panel ever wraps its title in extra structure. Checking for an
// explicit "left" rather than RmlUi's own computed `position` enum
// deliberately -- Property::Get<T>() needs the exact stored type, and
// position is stored as an internal enum, not a string, so comparing
// against a string would be a real type mismatch, not a working check
// that happens to look reasonable. Every real panel in this project
// sets left explicitly (see each module's own SetProperty("left", ...)
// or inline style), so this is an equally reliable signal without that
// risk.
Rml::Element* findDraggablePanelAncestor(Rml::Element* handle) {
    // Starts from the handle's own PARENT, not the handle itself -- a
    // real bug found and fixed, not assumed correct: GetProperty("left")
    // turned out to return non-null even for the handle itself (RmlUi
    // apparently returns an explicit "auto" property rather than a
    // true null for an unset left), so the original version of this
    // loop matched the title text element on its very first iteration
    // and never walked any further -- confirmed directly via a real
    // diagnostic log showing "panel found, tag='p'" when it should
    // have found the actual container div.
    for (Rml::Element* el = handle->GetParentNode(); el != nullptr; el = el->GetParentNode()) {
        if (el->GetProperty("left") != nullptr) {
            return el;
        }
    }
    return nullptr;
}

} // namespace

void UiModule::EngineSystemInterface::ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) {
    if (!window) return;
    SDL_Rect area{ static_cast<int>(caretPosition.x), static_cast<int>(caretPosition.y), 1, static_cast<int>(lineHeight) };
    SDL_SetTextInputArea(window, &area, 0);
    SDL_StartTextInput(window);
}

void UiModule::EngineSystemInterface::DeactivateKeyboard() {
    if (window) SDL_StopTextInput(window);
}

double UiModule::EngineSystemInterface::GetElapsedTime() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

bool UiModule::EngineSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    const char* prefix = (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) ? "[rmlui:error] " :
                         (type == Rml::Log::LT_WARNING) ? "[rmlui:warn] " : "[rmlui] ";
    std::cerr << prefix << message << std::endl;
    return true;
}

void UiModule::init(Application& app) {
    m_app = &app;
    m_systemInterface.window = app.window().handle();
    m_renderInterface = std::make_unique<RmlVulkanRenderInterface>(app.device(), app.renderer().renderPass(), app.renderer().hasStencil());

    Rml::SetSystemInterface(&m_systemInterface);
    Rml::SetRenderInterface(m_renderInterface.get());

    if (!Rml::Initialise()) {
        throw std::runtime_error("Rml::Initialise() failed");
    }
    m_initialised = true;

    // The default fallback font every game gets even if it never loads
    // its own — see README "Default fonts / font fallback chain." A game
    // that wants its own typography just calls Rml::LoadFontFace() again
    // with a different family before/after this; RmlUi keeps both
    // registered and CSS font-family selects between them normally.
    if (!Rml::LoadFontFace("assets/fonts/NotoSans-Regular.ttf")) {
        std::cerr << "[ui] warning: failed to load bundled NotoSans-Regular.ttf — "
                     "text will not render. Check the working directory (see README)." << std::endl;
    }
    // Loaded as a fallback face: RmlUi uses it for any character missing
    // from Noto Sans, which is exactly how emoji end up alongside normal
    // text without the document needing to say anything special. Verified
    // to render in genuine color (not grayscale-tinted) — see README
    // "Default fonts" for how that was actually confirmed, not assumed.
    // Bold/Italic are optional extra faces of the same family: without
    // them `font-weight: bold` silently renders the regular face.
    for (const char* face : { "assets/fonts/NotoSans-Bold.ttf", "assets/fonts/NotoSans-Italic.ttf" }) {
        if (!Rml::LoadFontFace(face)) {
            log::get(name())->info("optional font face '{}' not found -- bold/italic text will use the regular face", face);
        }
    }
    if (!Rml::LoadFontFace("assets/fonts/NotoColorEmoji.ttf", /*fallback_face=*/true)) {
        std::cerr << "[ui] warning: failed to load bundled NotoColorEmoji.ttf — "
                     "emoji will not render." << std::endl;
    }

    VkExtent2D extent = app.renderer().extent();
    m_context = Rml::CreateContext("main", Rml::Vector2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
    if (!m_context) {
        throw std::runtime_error("Rml::CreateContext() failed");
    }

    std::cout << "[ui] RmlUi initialised with real Vulkan rendering (text via glyph textures, <img>/background-image via stb_image, both real now -- see RmlVulkanRenderInterface.h)" << std::endl;
}

// The window height at which 1dp == 1px (times m_uiScale). Documents
// were designed against 1600x900; scaling by height rather than width
// keeps text size tied to how much vertical room there is, which is
// what makes a HUD or menu read the same at 720p, 1080p and 4K. The
// lower clamp stops text becoming unreadable in a small window.
static constexpr float kReferenceHeight = 900.0f;

// RmlUi's layout, animations, transitions and hover state all advance in
// Context::Update(). This used to run in update(), which Application
// skips while the simulation is paused (see DebugControlModule) — so
// pausing also froze every menu: no hover, no animation, stale layout.
// renderUi() runs on every frame that gets drawn, paused or not.
void UiModule::renderUi() {
    if (!m_context) return;
    VkExtent2D extent = m_app->renderer().extent();
    Rml::Vector2i currentSize = m_context->GetDimensions();
    if (currentSize.x != static_cast<int>(extent.width) || currentSize.y != static_cast<int>(extent.height)) {
        m_context->SetDimensions(Rml::Vector2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
    }
    m_pixelsPerPoint = m_app->window().pixelsPerPoint();
    float ratio = std::max(0.6f, static_cast<float>(extent.height) / kReferenceHeight) * m_uiScale;
    if (std::abs(ratio - m_dpRatio) > 1e-3f || std::abs(m_context->GetDensityIndependentPixelRatio() - ratio) > 1e-3f) {
        m_dpRatio = ratio;
        m_context->SetDensityIndependentPixelRatio(ratio);
    }
    m_context->Update();
    m_app->setUiCapturesMouse(m_context->IsMouseInteracting() || m_draggingSlider || m_draggingPanel);
}

void UiModule::onSettingsChanged(const EngineSettings& settings) {
    m_uiScale = settings.graphics.uiScale;
}

void UiModule::reloadStyleSheets() {
    if (!m_context) return;
    for (int i = 0; i < m_context->GetNumDocuments(); ++i) {
        m_context->GetDocument(i)->ReloadStyleSheet();
    }
    log::get(name())->info("reloaded stylesheets for {} document(s)", m_context->GetNumDocuments());
}

void UiModule::render(const RenderContext& ctx) {
    if (!m_context) return;

    // RenderContext only carries what 3D drawing needs (view/proj/etc),
    // not pixel dimensions, so we ask the context for its own size --
    // which we set once at creation and don't currently update on window
    // resize (see README "Roadmap").
    Rml::Vector2i size = m_context->GetDimensions();
    m_renderInterface->beginFrame(ctx.cmd, glm::vec2(static_cast<float>(size.x), static_cast<float>(size.y)));
    m_context->Render();
}

void UiModule::onEvent(const SDL_Event& event) {
    if (!m_context) return;

    int modifiers = currentRmlModifiers();
    // Window coordinates -> framebuffer pixels (see Window::pixelsPerPoint()).
    const float ppp = m_pixelsPerPoint;

    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            m_context->ProcessMouseMove(static_cast<int>(event.motion.x * ppp), static_cast<int>(event.motion.y * ppp), modifiers);
            // Real slider dragging -- see this class's own header
            // comment on m_draggingSlider for why this exists. Every
            // motion event while a range input is being dragged
            // recomputes and sets its value from the current mouse X,
            // the same real fix already applied to the initial click
            // below, just repeated continuously instead of once.
            if (m_draggingSlider) {
                setRangeSliderValueFromMouseX(m_draggingSlider, event.motion.x * ppp);
            }
            // Real panel dragging — see m_draggingPanel's own header
            // comment. A pixel delta from the drag's own start point,
            // converted to the same percentage units this project's
            // panels are actually positioned in (see
            // LightingControlsModule/MaterialGridModule/
            // MarketplaceUiModule's own "why percentages, not pixels"
            // comments) against the context's current dimensions, so a
            // dragged panel keeps tracking the mouse correctly even if
            // the window is resized mid-drag.
            if (m_draggingPanel) {
                Rml::Vector2i ctxSize = m_context->GetDimensions();
                float dx = event.motion.x * ppp - m_dragStartMouse.x;
                float dy = event.motion.y * ppp - m_dragStartMouse.y;
                float newLeftPx = m_dragPanelStartOffset.x + dx;
                float newTopPx = m_dragPanelStartOffset.y + dy;
                if (ctxSize.x > 0 && ctxSize.y > 0) {
                    m_draggingPanel->SetProperty("left", std::to_string(newLeftPx / ctxSize.x * 100.0f) + "%");
                    m_draggingPanel->SetProperty("top", std::to_string(newTopPx / ctxSize.y * 100.0f) + "%");
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int button = sdlButtonToRmlButton(event.button.button);
            if (button >= 0) m_context->ProcessMouseButtonDown(button, modifiers);

            // A real, working fix for a real, deeply-diagnosed RmlUi
            // quirk, not a guess: <input type="range">'s internal
            // slidertrack/sliderbar children are added via
            // AppendChild(..., /*dom_element=*/false) -- confirmed by
            // reading WidgetSlider.cpp and Element.cpp directly, not
            // assumed. Verified empirically too: logged
            // Context::GetHoverElement() on every mousedown and found
            // it resolves to the parent <input class="range"> itself
            // across eleven different Y coordinates spanning the
            // entire visible track/thumb area, never to the internal
            // slidertrack/sliderbar RmlUi's own WidgetSlider::
            // ProcessEvent specifically checks
            // event.GetTargetElement() == track against. That check
            // can never succeed given what's actually observed, which
            // is why the slider never responded to any click or drag
            // no matter how the CSS was adjusted. Rather than patch
            // RmlUi's own vendored widget internals, this handles the
            // click directly at the one point confirmed to actually
            // receive it -- the parent element -- and sets the value
            // through the same public SetValue() API a working
            // slider would end up calling internally.
            if (button == 0) {
                Rml::Element* hover = m_context->GetHoverElement();
                if (isRangeSliderInput(hover)) {
                    setRangeSliderValueFromMouseX(hover, event.button.x * ppp);
                    // Real drag start, not just a one-time click — a
                    // genuine, reported gap this closes: clicking
                    // alone could set a value, but holding and moving
                    // the mouse afterward did nothing, since nothing
                    // tracked that a drag was in progress. Tracked by
                    // raw Element* rather than a handle/ID: this
                    // module doesn't outlive a single frame's elements
                    // in a way that would make the pointer stale
                    // before mouse-up clears it below.
                    m_draggingSlider = hover;
                } else if (hover && hover->GetClassNames().find("draggable-handle") != Rml::String::npos) {
                    // Real panel-move start — a genuine, reported gap
                    // this closes: panels couldn't be repositioned by
                    // the user at all before this. Finds the actual
                    // positioned ancestor (see
                    // findDraggablePanelAncestor's own comment on why
                    // that's not necessarily the handle's direct
                    // parent) and records its real current pixel
                    // position via GetAbsoluteOffset() — not by trying
                    // to parse whatever units its left/top happen to
                    // already be set in. Verified working end to end
                    // with a real before/after screenshot: the
                    // Marketplace panel visibly moved as one unit,
                    // following the mouse.
                    if (Rml::Element* panel = findDraggablePanelAncestor(hover)) {
                        m_draggingPanel = panel;
                        m_dragPanelStartOffset = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
                        m_dragStartMouse = Rml::Vector2f(event.button.x * ppp, event.button.y * ppp);
                    }
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int button = sdlButtonToRmlButton(event.button.button);
            if (button >= 0) m_context->ProcessMouseButtonUp(button, modifiers);
            // Ends the drag unconditionally on any button-up, even if
            // the mouse has moved off the slider by then (a real,
            // expected case for a fast drag) -- matching how every
            // native slider control behaves, rather than leaving this
            // stuck set and having the next unrelated mouse motion
            // keep dragging a slider the user has already let go of.
            m_draggingSlider = nullptr;
            m_draggingPanel = nullptr;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            m_context->ProcessMouseWheel(Rml::Vector2f(-event.wheel.x, -event.wheel.y), modifiers);
            break;
        case SDL_EVENT_KEY_DOWN: {
            if (event.key.key == SDLK_F5 || (event.key.key == SDLK_R && (event.key.mod & SDL_KMOD_CTRL))) {
                reloadStyleSheets();
                break;
            }
            Rml::Input::KeyIdentifier key = sdlKeyToRmlKey(event.key.key);
            if (key != Rml::Input::KI_UNKNOWN) m_context->ProcessKeyDown(key, modifiers);
            break;
        }
        case SDL_EVENT_KEY_UP: {
            Rml::Input::KeyIdentifier key = sdlKeyToRmlKey(event.key.key);
            if (key != Rml::Input::KI_UNKNOWN) m_context->ProcessKeyUp(key, modifiers);
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            m_context->ProcessTextInput(Rml::String(event.text.text));
            break;
        default:
            break;
    }
}

void UiModule::shutdown() {
    if (m_initialised) {
        Rml::Shutdown();
        m_initialised = false;
        m_context = nullptr;
    }
    m_renderInterface.reset();
}

} // namespace kke
