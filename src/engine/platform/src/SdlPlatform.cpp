#include "SdlPlatform.hpp"

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
            out.scancode = static_cast<std::int32_t>(source.key.scancode);
            out.keycode = static_cast<std::int32_t>(source.key.key);
            out.repeat = source.key.repeat;
            return true;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            out.type = source.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PlatformEventType::MouseDown
                                                                  : PlatformEventType::MouseUp;
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

SdlPlatform::~SdlPlatform() { shutdown(); }

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

bool SdlPlatform::quitRequested() const { return m_quitRequested; }

IWindow& SdlPlatform::window() { return m_window; }

IFileSystem& SdlPlatform::files() { return m_files; }

double SdlPlatform::monotonicSeconds() const {
    if (!m_initialized) {
        return 0.0;
    }
    return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0;
}

fa::core::Status SdlPlatform::showMessageBox(MessageBoxKind kind, std::string_view title,
                                             std::string_view message) {
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

std::unique_ptr<IPlatform> createPlatform() { return std::make_unique<detail::SdlPlatform>(); }

}  // namespace fa::platform
