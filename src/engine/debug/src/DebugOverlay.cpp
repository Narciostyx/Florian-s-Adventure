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

constexpr float kLineHeight = 8.0f;

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
        const float height = 8.0f + static_cast<float>(lines.size()) * kLineHeight;
        const core::Rect panel = core::makeRect(viewport.w - kWidth - 8.0f, 8.0f, kWidth, height);
        ui.panel(panel, theme.panel);
        ui.border(panel, theme.panelBorder);

        float y = panel.y + 4.0f;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            ui.text(lines[index],
                    core::makeRect(panel.x + 4.0f, y, kWidth - 8.0f, 7.0f),
                    index == 0 ? titleStyle : textStyle);
            y += kLineHeight;
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
        constexpr std::size_t kVisibleLines = 10;
        const std::size_t start = lines.size() > kVisibleLines ? lines.size() - kVisibleLines : 0;
        float y = panel.y + 4.0f;
        for (std::size_t index = start; index < lines.size(); ++index) {
            ui.text(lines[index], core::makeRect(panel.x + 4.0f, y, width - 8.0f, 7.0f), textStyle);
            y += kLineHeight;
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

        constexpr std::size_t kVisibleLines = 8;
        const std::size_t start = lines.size() > kVisibleLines ? lines.size() - kVisibleLines : 0;
        float y = panel.y + 4.0f;
        for (std::size_t index = start; index < lines.size(); ++index) {
            ui::TextStyle lineStyle = textStyle;
            lineStyle.color = logColor(lines[index].level);
            const std::string text = formatLogLine(lines[index]);
            ui.text(text, core::makeRect(panel.x + 4.0f, y, kWidth - 8.0f, 7.0f), lineStyle);
            y += kLineHeight;
        }
    }
}

}  // namespace fa::debug
