#include "kke/modules/UiModule.h"
#include "kke/Application.h"
#include "kke/Log.h"

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

} // namespace

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
    m_renderInterface = std::make_unique<RmlVulkanRenderInterface>(app.device(), app.renderer().renderPass());

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

void UiModule::update(const UpdateContext& /*ctx*/) {
    if (m_context) {
        // By this point in the frame the swapchain has already been
        // recreated if a resize happened — see the class comment for
        // why this per-frame check, not the SDL resize event directly.
        VkExtent2D extent = m_app->renderer().extent();
        Rml::Vector2i currentSize = m_context->GetDimensions();
        if (currentSize.x != static_cast<int>(extent.width) || currentSize.y != static_cast<int>(extent.height)) {
            m_context->SetDimensions(Rml::Vector2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
        }
        m_context->Update();
    }
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

    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            m_context->ProcessMouseMove(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y), modifiers);
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
                if (Rml::Element* hover = m_context->GetHoverElement()) {
                    if (hover->GetTagName() == "input" && hover->GetClassNames().find("range") != Rml::String::npos) {
                        Rml::Vector2f topLeft = hover->GetAbsoluteOffset(Rml::BoxArea::Content);
                        Rml::Vector2f size = hover->GetBox().GetSize(Rml::BoxArea::Content);
                        if (size.x > 0.0f) {
                            float fraction = (static_cast<float>(event.button.x) - topLeft.x) / size.x;
                            fraction = std::clamp(fraction, 0.0f, 1.0f);
                            float minValue = hover->GetAttribute<float>("min", 0.0f);
                            float maxValue = hover->GetAttribute<float>("max", 100.0f);
                            float step = hover->GetAttribute<float>("step", 1.0f);
                            float rawValue = minValue + fraction * (maxValue - minValue);
                            if (step > 0.0f) {
                                rawValue = minValue + std::round((rawValue - minValue) / step) * step;
                            }
                            if (auto* control = dynamic_cast<Rml::ElementFormControl*>(hover)) {
                                control->SetValue(std::to_string(rawValue));
                            }
                        }
                    }
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int button = sdlButtonToRmlButton(event.button.button);
            if (button >= 0) m_context->ProcessMouseButtonUp(button, modifiers);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            m_context->ProcessMouseWheel(Rml::Vector2f(-event.wheel.x, -event.wheel.y), modifiers);
            break;
        case SDL_EVENT_KEY_DOWN: {
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
