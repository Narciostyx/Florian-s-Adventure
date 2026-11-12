#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "fa/core/Math.hpp"
#include "fa/render/RenderQueue.hpp"
#include "fa/render/RenderTypes.hpp"
#include "fa/ui/Font.hpp"
#include "fa/ui/Text.hpp"

namespace fa::ui {

/// \file
/// Immediate-mode UI: panels, labels, buttons, checkboxes, sliders and bars.
///
/// UI is not an ECS system and does not own gameplay state. The caller passes a
/// per-frame input snapshot, calls widgets, then `submit()`s the queued draw items
/// into the world render queue. `RenderLayer::UI` makes them draw last.
///
/// Buttons carry a keyboard focus chain (`up`/`down` navigate, `activate` presses
/// the focused button); mouse clicks take focus and leave keyboard mode. Layout
/// groups, scrolling panels and text fields remain follow-ups.

/// \brief Stable widget identity for press/drag state.
struct UiId {
    std::uint32_t value = 0;

    [[nodiscard]] static UiId fromName(std::string_view name) noexcept;
    friend constexpr bool operator==(UiId lhs, UiId rhs) noexcept { return lhs.value == rhs.value; }
    friend constexpr bool operator!=(UiId lhs, UiId rhs) noexcept { return lhs.value != rhs.value; }
};

/// \brief Mouse snapshot in virtual pixels.
struct UiInputState {
    core::Vec2 mousePosition{};
    bool mouseDown = false;
    bool mousePressed = false;
    bool mouseReleased = false;
    /// \brief Keyboard focus navigation: `up`/`down` move the focus, `activate`
    /// presses the focused button. Edge-triggered (one press = one frame true).
    bool upPressed = false;
    bool downPressed = false;
    bool activatePressed = false;
};

/// \brief What one widget did this frame.
struct UiResult {
    bool hovered = false;
    /// \brief A click completed (press then release inside the widget).
    bool pressed = false;
    /// \brief A value changed (checkbox/slider).
    bool changed = false;
};

/// \brief Colours and text scale shared by every widget.
struct UiTheme {
    core::Color panel{0.07f, 0.08f, 0.12f, 0.88f};
    core::Color panelBorder{0.28f, 0.33f, 0.44f, 1.0f};
    core::Color text{0.92f, 0.94f, 0.98f, 1.0f};
    core::Color textDisabled{0.45f, 0.48f, 0.55f, 1.0f};
    core::Color button{0.18f, 0.22f, 0.30f, 1.0f};
    core::Color buttonHover{0.25f, 0.31f, 0.42f, 1.0f};
    core::Color buttonPressed{0.14f, 0.18f, 0.24f, 1.0f};
    core::Color accent{0.35f, 0.80f, 0.50f, 1.0f};
    core::Color track{0.12f, 0.14f, 0.20f, 1.0f};
    float textScale = 2.0f;
    float padding = 4.0f;
};

/// \brief Widget state machine and draw-item builder.
class UiContext {
public:
    /// \brief \param font Borrowed and must outlive this context.
    /// \param whiteTexture A valid 1x1 white texture used for solid quads.
    UiContext(const FontAtlas& font, render::TextureHandle whiteTexture, UiTheme theme = {});

    /// \brief World-space position of virtual (0, 0); normally the camera's top-left.
    void setOrigin(core::Vec2 origin) noexcept { m_origin = origin; }
    [[nodiscard]] core::Vec2 origin() const noexcept { return m_origin; }

    void setTheme(const UiTheme& theme) noexcept { m_theme = theme; }
    [[nodiscard]] const UiTheme& theme() const noexcept { return m_theme; }

    /// \brief Clears last frame's draw items and takes the input snapshot.
    void beginFrame(const UiInputState& input) noexcept;

    /// \brief Marks the frame complete. Kept explicit for the M09 begin/end contract.
    void endFrame() noexcept {}

    /// \brief Moves the keyboard focus to \p id (`UiId{0}` clears it) and enters
    /// keyboard mode, so the next `activate` presses the new focus.
    void setFocus(UiId id) noexcept;
    /// \brief The focused widget id; `UiId{0}` when nothing has focus.
    [[nodiscard]] UiId focus() const noexcept { return UiId{m_focus}; }

    /// \brief Filled rectangle on the UI layer; the panel background.
    void panel(core::Rect rect, core::Color color);
    /// \brief Draws \p texture over \p rect on the UI layer.
    ///
    /// Item icons use this. Pixel art only stays crisp when the destination is an
    /// integer multiple of the source size, so callers pass 16 -> 16 or 32 px.
    /// An invalid texture or an empty rect is a no-op.
    void icon(core::Rect rect, render::TextureHandle texture, core::Color tint = {1.0f, 1.0f, 1.0f, 1.0f});
    /// \brief Four thin quads outlining \p rect.
    void border(core::Rect rect, core::Color color, float thickness = 1.0f);
    /// \brief Track + fill bar; the value is clamped to `[minimum, maximum]`.
    void progressBar(core::Rect rect, float value, float minimum, float maximum, core::Color fill);
    /// \brief One text run, wrapped to \p rect's width; colour/scale/align from \p style.
    void text(std::string_view text, core::Rect rect, const TextStyle& style = {});

    /// \brief Push button. With a valid `background` texture it replaces the flat
    /// theme quad (hover brightens, press darkens the tint); the label still uses
    /// the theme text colour. Invalid texture keeps the classic look.
    UiResult button(UiId id, std::string_view label, core::Rect rect, render::TextureHandle background = {});
    /// \brief Toggle: flips \p value on click.
    UiResult checkbox(UiId id, std::string_view label, bool& value, core::Rect rect);
    /// \brief Drag handle writing \p value within `[minimum, maximum]`.
    UiResult slider(UiId id, float& value, float minimum, float maximum, core::Rect rect);

    /// \brief Appends every queued item to \p out.
    void submit(render::RenderQueue& out) const;

    /// \brief Queued draw items this frame.
    [[nodiscard]] std::size_t itemCount() const noexcept { return m_queue.size(); }
    /// \brief The font this context draws text with.
    [[nodiscard]] const FontAtlas& font() const noexcept { return m_font; }

private:
    /// \brief Whether the cursor is inside \p rect (virtual pixels).
    [[nodiscard]] bool hovered(core::Rect rect) const noexcept;
    /// \brief Moves \p rect from UI space into world space via the origin.
    [[nodiscard]] core::Rect offset(core::Rect rect) const noexcept;
    /// \brief Queues one solid white-texture quad.
    void quad(core::Rect rect, core::Color color);

    const FontAtlas& m_font;
    render::TextureHandle m_white;
    UiTheme m_theme;
    UiInputState m_input;
    core::Vec2 m_origin{};
    std::uint32_t m_active = 0;
    std::uint32_t m_nextEntityId = 1;
    render::RenderQueue m_queue;
    /// \brief Focusable ids in last-submitted order (previous frame) and the
    /// widget list currently being recorded.
    std::vector<std::uint32_t> m_navPrev;
    std::vector<std::uint32_t> m_navCur;
    std::uint32_t m_focus = 0;
    /// \brief False while a mouse click owns the focus, so the ring and Enter
    /// only apply to keyboard navigation.
    bool m_navKeyboard = false;
    bool m_navActivate = false;
};

}  // namespace fa::ui
