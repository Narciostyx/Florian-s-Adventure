#include "fa/debug/DebugOverlay.hpp"

#include <string>
#include <vector>

#include <fa/debug/DebugConsole.hpp>
#include <fa/debug/DebugLog.hpp>
#include <fa/debug/DebugStats.hpp>
#include <fa/ui/Ui.hpp>

namespace fa::debug {
namespace {

[[nodiscard]] core::Color logColor(core::LogLevel level) {
    switch (level) {
        case core::LogLevel::Error:
        case core::LogLevel::Fatal:
            return core::Color{1.0f, 0.35f, 0.35f, 1.0f};
        case core::LogLevel::Warn:
            return core::Color{1.0f, 0.85f, 0.35f, 1.0f};
        default:
            return core::Color{0.85f, 0.90f, 1.0f, 1.0f};
    }
}

constexpr float kFallbackLineHeight = 8.0f;

/// \brief Shortens `text` so it fits `maxWidth` without wrapping: a wrapped debug
/// line would be drawn on the next row and collide with the line that lives there.
[[nodiscard]] std::string_view clipToFit(const ui::FontAtlas& font, std::string_view text, float maxWidth) {
    if (maxWidth <= 0.0f || ui::measureText(font, text, 0.0f).width <= maxWidth) {
        return text;
    }
    std::size_t total = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        total += (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 ? 1 : 0;
    }
    std::size_t lo = 0;
    std::size_t hi = total;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo + 1) / 2;
        const std::string_view candidate = ui::fitToCodepoints(text, mid);
        if (ui::measureText(font, candidate, 0.0f).width <= maxWidth) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return ui::fitToCodepoints(text, lo);
}

}  // namespace

void DebugOverlay::draw(ui::UiContext& ui,
                        core::Rect viewport,
                        const DebugStats& stats,
                        const DebugConsole& console,
                        const DebugLogSink& log,
                        const DebugOverlayState& state) const {
    const ui::UiTheme& theme = ui.theme();

    ui::TextStyle textStyle;
    textStyle.color = theme.text;
    textStyle.scale = 1.0f;

    ui::TextStyle titleStyle = textStyle;
    titleStyle.color = theme.accent;

    if (state.statsVisible) {
        const std::vector<std::string> lines = formatStats(stats);
        constexpr float kWidth = 214.0f;
        const float lineHeight = ui.font().lineHeight > 0.0f ? ui.font().lineHeight : kFallbackLineHeight;
        const float height = 8.0f + static_cast<float>(lines.size()) * lineHeight;
        const core::Rect panel = core::makeRect(viewport.w - kWidth - 8.0f, 8.0f, kWidth, height);
        ui.panel(panel, theme.panel);
        ui.border(panel, theme.panelBorder);

        float y = panel.y + 4.0f;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            ui.text(clipToFit(ui.font(), lines[index], kWidth - 8.0f),
                    core::makeRect(panel.x + 4.0f, y, kWidth - 8.0f, 7.0f),
                    index == 0 ? titleStyle : textStyle);
            y += lineHeight;
        }

        // Second hub: the facing-tile panel, stacked directly under the stats block.
        if (!stats.facingTileInfo.empty()) {
            const float secondHeight = 8.0f + static_cast<float>(stats.facingTileInfo.size()) * lineHeight;
            const core::Rect tilePanel = core::makeRect(panel.x, panel.y + height + 4.0f, kWidth, secondHeight);
            ui.panel(tilePanel, theme.panel);
            ui.border(tilePanel, theme.panelBorder);
            float ty = tilePanel.y + 4.0f;
            for (std::size_t index = 0; index < stats.facingTileInfo.size(); ++index) {
                ui.text(clipToFit(ui.font(), stats.facingTileInfo[index], kWidth - 8.0f),
                        core::makeRect(tilePanel.x + 4.0f, ty, kWidth - 8.0f, 7.0f),
                        index == 0 ? titleStyle : textStyle);
                ty += lineHeight;
            }
        }
    }

    if (console.open()) {
        constexpr float kPreferredWidth = 460.0f;
        const float width = viewport.w - 16.0f < kPreferredWidth ? viewport.w - 16.0f : kPreferredWidth;
        constexpr float kHeight = 110.0f;
        const core::Rect panel = core::makeRect(8.0f, viewport.h - kHeight - 8.0f, width, kHeight);
        ui.panel(panel, theme.panel);
        ui.border(panel, theme.panelBorder);

        const auto& lines = console.lines();
        // 10 px line height: eight rows stay clear of the input line at panel.y + 96.
        constexpr std::size_t kVisibleLines = 8;
        const std::size_t start = lines.size() > kVisibleLines ? lines.size() - kVisibleLines : 0;
        const float lineHeight = ui.font().lineHeight > 0.0f ? ui.font().lineHeight : kFallbackLineHeight;
        float y = panel.y + 4.0f;
        for (std::size_t index = start; index < lines.size(); ++index) {
            ui.text(clipToFit(ui.font(), lines[index], width - 8.0f),
                    core::makeRect(panel.x + 4.0f, y, width - 8.0f, 7.0f),
                    textStyle);
            y += lineHeight;
        }

        ui::TextStyle inputStyle = textStyle;
        inputStyle.color = theme.accent;
        const std::string inputText = "> " + console.input() + "_";
        ui.text(inputText, core::makeRect(panel.x + 4.0f, panel.y + kHeight - 14.0f, width - 8.0f, 7.0f), inputStyle);
    }

    if (state.logVisible) {
        const std::vector<DebugLogLine> lines = log.snapshot();
        constexpr float kWidth = 300.0f;
        constexpr float kHeight = 78.0f;
        const core::Rect panel = core::makeRect(8.0f, 78.0f, kWidth, kHeight);
        ui.panel(panel, theme.panel);
        ui.border(panel, theme.panelBorder);

        // Seven 10 px rows fit the 78 px panel.
        constexpr std::size_t kVisibleLines = 7;
        const std::size_t start = lines.size() > kVisibleLines ? lines.size() - kVisibleLines : 0;
        const float lineHeight = ui.font().lineHeight > 0.0f ? ui.font().lineHeight : kFallbackLineHeight;
        float y = panel.y + 4.0f;
        for (std::size_t index = start; index < lines.size(); ++index) {
            ui::TextStyle lineStyle = textStyle;
            lineStyle.color = logColor(lines[index].level);
            const std::string text = formatLogLine(lines[index]);
            ui.text(clipToFit(ui.font(), text, kWidth - 8.0f),
                    core::makeRect(panel.x + 4.0f, y, kWidth - 8.0f, 7.0f),
                    lineStyle);
            y += lineHeight;
        }
    }
}

}  // namespace fa::debug
