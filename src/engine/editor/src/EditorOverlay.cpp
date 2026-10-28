#include "fa/editor/EditorOverlay.hpp"

#include <cstdio>

#include "fa/tilemap/Tilemap.hpp"
#include "fa/ui/Ui.hpp"

namespace fa::editor {
namespace {

constexpr core::Rect kPanel{330.0f, 8.0f, 300.0f, 62.0f};

}  // namespace

EditorOverlay::EditorOverlay(tilemap::Tilemap& map) : m_editor(map, m_history) {}

void EditorOverlay::update(const EditorFrameInput& input) {
    m_input = input;
    if (!m_active) {
        return;
    }
    m_editor.setHover(m_editor.map().worldToGrid(input.cursorWorld));
    if (overPanel(input.cursorScreen)) {
        return;
    }
    if (input.primaryPressed) {
        static_cast<void>(m_editor.applyPrimary());
    }
    if (input.secondaryPressed) {
        static_cast<void>(m_editor.applySecondary());
    }
}

void EditorOverlay::draw(ui::UiContext& ui) const {
    if (!m_active) {
        return;
    }
    const ui::UiTheme& theme = ui.theme();
    const core::Rect panel = panelRect();
    ui.panel(panel, theme.panel);
    ui.border(panel, theme.panelBorder);

    ui::TextStyle style;
    style.color = theme.text;
    style.scale = 1.0f;

    const EditorStats info = stats();
    char line[160]{};
    std::snprintf(line,
                  sizeof(line),
                  "EDITOR %s  L%d  BRUSH %u",
                  toString(info.tool).data(),
                  info.layer,
                  static_cast<unsigned>(info.brush.value));
    ui.text(line, core::makeRect(panel.x + 6.0f, panel.y + 3.0f, panel.w - 12.0f, 7.0f), style);

    ui.text("1 BRUSH  2 RECT  3 FILL  4 ERASE  5 PICK",
            core::makeRect(panel.x + 6.0f, panel.y + 13.0f, panel.w - 12.0f, 7.0f),
            style);
    ui.text("[ ] LAYER  Z UNDO  Y REDO  S SAVE  L LOAD",
            core::makeRect(panel.x + 6.0f, panel.y + 23.0f, panel.w - 12.0f, 7.0f),
            style);

    std::snprintf(line,
                  sizeof(line),
                  "UNDO %zu  REDO %zu  CMDS %llu  DIRTY %s",
                  info.undoDepth,
                  info.redoDepth,
                  static_cast<unsigned long long>(info.commands),
                  info.dirty ? "YES" : "NO");
    ui.text(line, core::makeRect(panel.x + 6.0f, panel.y + 33.0f, panel.w - 12.0f, 7.0f), style);

    std::snprintf(line, sizeof(line), "MAP %dx%d  PREVIEW %zu", info.mapWidth, info.mapHeight, info.previewCells);
    ui.text(line, core::makeRect(panel.x + 6.0f, panel.y + 43.0f, panel.w - 12.0f, 7.0f), style);
}

void EditorOverlay::undo() {
    static_cast<void>(m_history.undo(m_editor.map()));
}

void EditorOverlay::redo() {
    static_cast<void>(m_history.redo(m_editor.map()));
}

EditorStats EditorOverlay::stats() const {
    EditorStats info = m_editor.stats();
    info.active = m_active;
    return info;
}

bool EditorOverlay::overPanel(core::Vec2 screen) const noexcept {
    return core::rectContains(panelRect(), screen);
}

core::Rect EditorOverlay::panelRect() const noexcept {
    return kPanel;
}

}  // namespace fa::editor
