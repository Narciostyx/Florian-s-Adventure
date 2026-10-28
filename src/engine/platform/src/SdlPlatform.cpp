#include "SdlPlatform.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include <fa/platform/PlatformError.hpp>

namespace fa::platform::detail {
namespace {

MouseButton toMouseButton(std::uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return MouseButton::Left;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::Middle;
        case SDL_BUTTON_RIGHT:
            return MouseButton::Right;
        case SDL_BUTTON_X1:
            return MouseButton::X1;
        case SDL_BUTTON_X2:
            return MouseButton::X2;
        default:
            return MouseButton::None;
    }
}

/// The only place in the engine that knows SDL's scancode numbering. Everything
/// downstream sees fa::platform::Scancode (docs/modules/07-input.md).
Scancode toScancode(SDL_Scancode code) {
    switch (code) {
        case SDL_SCANCODE_A:
            return Scancode::A;
        case SDL_SCANCODE_B:
            return Scancode::B;
        case SDL_SCANCODE_C:
            return Scancode::C;
        case SDL_SCANCODE_D:
            return Scancode::D;
        case SDL_SCANCODE_E:
            return Scancode::E;
        case SDL_SCANCODE_F:
            return Scancode::F;
        case SDL_SCANCODE_G:
            return Scancode::G;
        case SDL_SCANCODE_H:
            return Scancode::H;
        case SDL_SCANCODE_I:
            return Scancode::I;
        case SDL_SCANCODE_J:
            return Scancode::J;
        case SDL_SCANCODE_K:
            return Scancode::K;
        case SDL_SCANCODE_L:
            return Scancode::L;
        case SDL_SCANCODE_M:
            return Scancode::M;
        case SDL_SCANCODE_N:
            return Scancode::N;
        case SDL_SCANCODE_O:
            return Scancode::O;
        case SDL_SCANCODE_P:
            return Scancode::P;
        case SDL_SCANCODE_Q:
            return Scancode::Q;
        case SDL_SCANCODE_R:
            return Scancode::R;
        case SDL_SCANCODE_S:
            return Scancode::S;
        case SDL_SCANCODE_T:
            return Scancode::T;
        case SDL_SCANCODE_U:
            return Scancode::U;
        case SDL_SCANCODE_V:
            return Scancode::V;
        case SDL_SCANCODE_W:
            return Scancode::W;
        case SDL_SCANCODE_X:
            return Scancode::X;
        case SDL_SCANCODE_Y:
            return Scancode::Y;
        case SDL_SCANCODE_Z:
            return Scancode::Z;

        case SDL_SCANCODE_1:
            return Scancode::Digit1;
        case SDL_SCANCODE_2:
            return Scancode::Digit2;
        case SDL_SCANCODE_3:
            return Scancode::Digit3;
        case SDL_SCANCODE_4:
            return Scancode::Digit4;
        case SDL_SCANCODE_5:
            return Scancode::Digit5;
        case SDL_SCANCODE_6:
            return Scancode::Digit6;
        case SDL_SCANCODE_7:
            return Scancode::Digit7;
        case SDL_SCANCODE_8:
            return Scancode::Digit8;
        case SDL_SCANCODE_9:
            return Scancode::Digit9;
        case SDL_SCANCODE_0:
            return Scancode::Digit0;

        case SDL_SCANCODE_ESCAPE:
            return Scancode::Escape;
        case SDL_SCANCODE_RETURN:
            return Scancode::Enter;
        case SDL_SCANCODE_BACKSPACE:
            return Scancode::Backspace;
        case SDL_SCANCODE_TAB:
            return Scancode::Tab;
        case SDL_SCANCODE_SPACE:
            return Scancode::Space;
        case SDL_SCANCODE_MINUS:
            return Scancode::Minus;
        case SDL_SCANCODE_EQUALS:
            return Scancode::Equals;
        case SDL_SCANCODE_LEFTBRACKET:
            return Scancode::LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET:
            return Scancode::RightBracket;
        case SDL_SCANCODE_BACKSLASH:
            return Scancode::Backslash;
        case SDL_SCANCODE_SEMICOLON:
            return Scancode::Semicolon;
        case SDL_SCANCODE_APOSTROPHE:
            return Scancode::Apostrophe;
        case SDL_SCANCODE_GRAVE:
            return Scancode::Grave;
        case SDL_SCANCODE_COMMA:
            return Scancode::Comma;
        case SDL_SCANCODE_PERIOD:
            return Scancode::Period;
        case SDL_SCANCODE_SLASH:
            return Scancode::Slash;
        case SDL_SCANCODE_CAPSLOCK:
            return Scancode::CapsLock;

        case SDL_SCANCODE_F1:
            return Scancode::F1;
        case SDL_SCANCODE_F2:
            return Scancode::F2;
        case SDL_SCANCODE_F3:
            return Scancode::F3;
        case SDL_SCANCODE_F4:
            return Scancode::F4;
        case SDL_SCANCODE_F5:
            return Scancode::F5;
        case SDL_SCANCODE_F6:
            return Scancode::F6;
        case SDL_SCANCODE_F7:
            return Scancode::F7;
        case SDL_SCANCODE_F8:
            return Scancode::F8;
        case SDL_SCANCODE_F9:
            return Scancode::F9;
        case SDL_SCANCODE_F10:
            return Scancode::F10;
        case SDL_SCANCODE_F11:
            return Scancode::F11;
        case SDL_SCANCODE_F12:
            return Scancode::F12;

        case SDL_SCANCODE_PRINTSCREEN:
            return Scancode::PrintScreen;
        case SDL_SCANCODE_SCROLLLOCK:
            return Scancode::ScrollLock;
        case SDL_SCANCODE_PAUSE:
            return Scancode::Pause;
        case SDL_SCANCODE_INSERT:
            return Scancode::Insert;
        case SDL_SCANCODE_HOME:
            return Scancode::Home;
        case SDL_SCANCODE_PAGEUP:
            return Scancode::PageUp;
        case SDL_SCANCODE_DELETE:
            return Scancode::Delete;
        case SDL_SCANCODE_END:
            return Scancode::End;
        case SDL_SCANCODE_PAGEDOWN:
            return Scancode::PageDown;
        case SDL_SCANCODE_RIGHT:
            return Scancode::Right;
        case SDL_SCANCODE_LEFT:
            return Scancode::Left;
        case SDL_SCANCODE_DOWN:
            return Scancode::Down;
        case SDL_SCANCODE_UP:
            return Scancode::Up;

        case SDL_SCANCODE_NUMLOCKCLEAR:
            return Scancode::NumLock;
        case SDL_SCANCODE_KP_DIVIDE:
            return Scancode::KeypadDivide;
        case SDL_SCANCODE_KP_MULTIPLY:
            return Scancode::KeypadMultiply;
        case SDL_SCANCODE_KP_MINUS:
            return Scancode::KeypadMinus;
        case SDL_SCANCODE_KP_PLUS:
            return Scancode::KeypadPlus;
        case SDL_SCANCODE_KP_ENTER:
            return Scancode::KeypadEnter;
        case SDL_SCANCODE_KP_1:
            return Scancode::Keypad1;
        case SDL_SCANCODE_KP_2:
            return Scancode::Keypad2;
        case SDL_SCANCODE_KP_3:
            return Scancode::Keypad3;
        case SDL_SCANCODE_KP_4:
            return Scancode::Keypad4;
        case SDL_SCANCODE_KP_5:
            return Scancode::Keypad5;
        case SDL_SCANCODE_KP_6:
            return Scancode::Keypad6;
        case SDL_SCANCODE_KP_7:
            return Scancode::Keypad7;
        case SDL_SCANCODE_KP_8:
            return Scancode::Keypad8;
        case SDL_SCANCODE_KP_9:
            return Scancode::Keypad9;
        case SDL_SCANCODE_KP_0:
            return Scancode::Keypad0;
        case SDL_SCANCODE_KP_PERIOD:
            return Scancode::KeypadPeriod;

        case SDL_SCANCODE_LCTRL:
            return Scancode::LeftCtrl;
        case SDL_SCANCODE_LSHIFT:
            return Scancode::LeftShift;
        case SDL_SCANCODE_LALT:
            return Scancode::LeftAlt;
        case SDL_SCANCODE_LGUI:
            return Scancode::LeftGui;
        case SDL_SCANCODE_RCTRL:
            return Scancode::RightCtrl;
        case SDL_SCANCODE_RSHIFT:
            return Scancode::RightShift;
        case SDL_SCANCODE_RALT:
            return Scancode::RightAlt;
        case SDL_SCANCODE_RGUI:
            return Scancode::RightGui;

        default:
            return Scancode::Unknown;
    }
}

SDL_MessageBoxFlags toMessageBoxFlags(MessageBoxKind kind) {
    switch (kind) {
        case MessageBoxKind::Warning:
            return SDL_MESSAGEBOX_WARNING;
        case MessageBoxKind::Error:
            return SDL_MESSAGEBOX_ERROR;
        case MessageBoxKind::Information:
        default:
            return SDL_MESSAGEBOX_INFORMATION;
    }
}

/// \brief Translates one SDL event into \p out.
/// \return false for event types the engine does not consume.
bool translateEvent(const SDL_Event& source, PlatformEvent& out) {
    switch (source.type) {
        case SDL_EVENT_QUIT:
            out.type = PlatformEventType::Quit;
            return true;

        case SDL_EVENT_WINDOW_RESIZED:
            out.type = PlatformEventType::WindowResized;
            out.width = static_cast<std::int32_t>(source.window.data1);
            out.height = static_cast<std::int32_t>(source.window.data2);
            return true;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            out.type = PlatformEventType::FocusGained;
            return true;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            out.type = PlatformEventType::FocusLost;
            return true;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            out.type = source.type == SDL_EVENT_KEY_DOWN ? PlatformEventType::KeyDown : PlatformEventType::KeyUp;
            out.scancode = toScancode(source.key.scancode);
            out.keycode = static_cast<std::int32_t>(source.key.key);
            out.repeat = source.key.repeat;
            return true;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            out.type =
                source.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PlatformEventType::MouseDown : PlatformEventType::MouseUp;
            out.button = toMouseButton(source.button.button);
            out.x = source.button.x;
            out.y = source.button.y;
            return true;

        case SDL_EVENT_MOUSE_MOTION:
            out.type = PlatformEventType::MouseMove;
            out.x = source.motion.x;
            out.y = source.motion.y;
            out.dx = source.motion.xrel;
            out.dy = source.motion.yrel;
            return true;

        case SDL_EVENT_MOUSE_WHEEL:
            out.type = PlatformEventType::MouseWheel;
            out.dx = source.wheel.x;
            out.dy = source.wheel.y;
            out.x = source.wheel.mouse_x;
            out.y = source.wheel.mouse_y;
            return true;

        case SDL_EVENT_TEXT_INPUT:
            out.type = PlatformEventType::TextInput;
            if (source.text.text != nullptr) {
                out.text = source.text.text;
            }
            return true;

        case SDL_EVENT_DROP_FILE:
            out.type = PlatformEventType::FileDrop;
            if (source.drop.data != nullptr) {
                out.text = source.drop.data;
            }
            return true;

        default:
            return false;
    }
}

}  // namespace

SdlPlatform::SdlPlatform() : m_glContext(m_window) {}

SdlPlatform::~SdlPlatform() {
    shutdown();
}

fa::core::Status SdlPlatform::initialize(const WindowDesc& desc) {
    if (!m_initialized) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
            return fa::core::errorStatus(platform_error::kSdlInitFailed,
                                         std::string("SDL_Init failed: ") + SDL_GetError());
        }
        m_initialized = true;
        m_quitRequested = false;
    }

    if (m_window.raw() == nullptr && !m_window.create(desc)) {
        return fa::core::errorStatus(platform_error::kWindowCreationFailed,
                                     std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    return fa::core::okStatus();
}

void SdlPlatform::shutdown() {
    /// The audio stream must stop before SDL_Quit tears the subsystem down.
    m_audio.shutdown();
    // The context is bound to the window, so it has to go first.
    m_glContext.destroyContext();
    m_window.destroy();
    if (m_initialized) {
        SDL_Quit();
        m_initialized = false;
    }
    m_quitRequested = false;
}

bool SdlPlatform::pumpEvents(std::vector<PlatformEvent>& outEvents) {
    SDL_Event source;
    while (SDL_PollEvent(&source)) {
        PlatformEvent event;
        if (!translateEvent(source, event)) {
            continue;
        }
        if (event.type == PlatformEventType::Quit) {
            m_quitRequested = true;
        }
        outEvents.push_back(std::move(event));
    }
    return !m_quitRequested;
}

bool SdlPlatform::quitRequested() const {
    return m_quitRequested;
}

IWindow& SdlPlatform::window() {
    return m_window;
}

IFileSystem& SdlPlatform::files() {
    return m_files;
}

IGlContext* SdlPlatform::glContext() {
    return &m_glContext;
}

IAudioDevice* SdlPlatform::audio() {
    return &m_audio;
}

double SdlPlatform::monotonicSeconds() const {
    if (!m_initialized) {
        return 0.0;
    }
    return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0;
}

void SdlPlatform::sleepSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return;
    }
    constexpr double kMaxSleepSeconds = 1.0;
    const double clamped = seconds > kMaxSleepSeconds ? kMaxSleepSeconds : seconds;
    SDL_DelayNS(static_cast<Uint64>(clamped * 1'000'000'000.0));
}

fa::core::Status SdlPlatform::showMessageBox(MessageBoxKind kind, std::string_view title, std::string_view message) {
    const std::string titleText(title);
    const std::string messageText(message);

    if (SDL_ShowSimpleMessageBox(toMessageBoxFlags(kind), titleText.c_str(), messageText.c_str(), m_window.raw())) {
        return fa::core::okStatus();
    }

    std::fprintf(stderr, "[messagebox] %s: %s\n", titleText.c_str(), messageText.c_str());
    return fa::core::errorStatus(platform_error::kMessageBoxFailed,
                                 std::string("SDL_ShowSimpleMessageBox failed: ") + SDL_GetError());
}

}  // namespace fa::platform::detail

namespace fa::platform {

std::unique_ptr<IPlatform> createPlatform() {
    return std::make_unique<detail::SdlPlatform>();
}

}  // namespace fa::platform
