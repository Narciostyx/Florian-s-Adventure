#include "fa/editor/VisualEditor.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "fa/tilemap/Tilemap.hpp"
#include "fa/ui/Ui.hpp"

namespace fa::editor {
namespace {

using fa::core::makeRect;
using fa::core::Rect;
using fa::core::Vec2;

constexpr float kTileSize = 16.0f;

fa::ui::TextStyle labelStyle(const fa::ui::UiTheme& theme, float factor = 1.0f) {
    fa::ui::TextStyle style;
    style.color = theme.text;
    style.scale = theme.textScale * factor;
    return style;
}

fa::ui::TextStyle dimStyle(const fa::ui::UiTheme& theme, float factor = 0.95f) {
    fa::ui::TextStyle style;
    style.color = theme.textDisabled;
    style.scale = theme.textScale * factor;
    return style;
}

}  // namespace

VisualEditor::VisualEditor(tilemap::Tilemap& map) : m_history(128), m_editor(map, m_history) {
    if (!map.tileDefs().empty()) {
        m_selectedTile = map.tileDefs().front().id;
        m_editor.setBrush(m_selectedTile);
    }
}

void VisualEditor::setActive(bool active) noexcept {
    m_active = active;
    m_closeRequested = false;
    m_status.clear();
}

Rect VisualEditor::mapRect(Rect screen) noexcept {
    return makeRect(
        screen.x + 4.0f, screen.y + 24.0f, std::max(120.0f, screen.w * 0.67f), std::max(120.0f, screen.h - 58.0f));
}

Rect VisualEditor::panelRect(Rect screen) noexcept {
    const Rect map = mapRect(screen);
    const float x = map.x + map.w + 6.0f;
    return makeRect(x, map.y, std::max(120.0f, screen.x + screen.w - 4.0f - x), screen.h - 58.0f);
}

void VisualEditor::updateHover(const Vec2& cursor) {
    if (m_mapViewport.w <= 0.0f || m_mapViewport.h <= 0.0f) {
        return;
    }
    const tilemap::Tilemap& map = m_editor.map();
    if (map.empty()) {
        return;
    }
    m_tileScale = std::min(m_mapViewport.w / (static_cast<float>(map.width()) * kTileSize),
                           m_mapViewport.h / (static_cast<float>(map.height()) * kTileSize));
    m_tileScale = std::max(0.25f, m_tileScale);

    const float localX = (cursor.x - m_mapViewport.x) / (kTileSize * m_tileScale);
    const float localY = (cursor.y - m_mapViewport.y) / (kTileSize * m_tileScale);
    const int column = static_cast<int>(localX);
    const int row = static_cast<int>(localY);
    if (column < 0 || row < 0 || column >= map.width() || row >= map.height()) {
        return;
    }
    m_editor.setHover(tilemap::TileCoord{column, row});
}

void VisualEditor::update(const VisualEditorInput& input, Rect mapViewport) {
    if (!m_active) {
        return;
    }
    m_mapViewport = mapViewport;
    updateHover(input.cursorScreen);
    const tilemap::Tilemap& map = m_editor.map();
    const tilemap::TileCoord cell = m_editor.hover();
    if (cell.x < 0 || cell.y < 0 || cell.x >= map.width() || cell.y >= map.height()) {
        return;
    }
    if (input.primaryPressed) {
        const core::Status status = m_editor.applyPrimary();
        m_status = status.isOk() ? "EDIT OK" : status.error().message;
    } else if (input.secondaryPressed) {
        const core::Status status = m_editor.applySecondary();
        m_status = status.isOk() ? "ERASED" : status.error().message;
    }
}

void VisualEditor::resizeMap(int width, int height) {
    tilemap::Tilemap& map = m_editor.map();
    width = std::clamp(width, 1, tilemap::Tilemap::kMaxDimension);
    height = std::clamp(height, 1, tilemap::Tilemap::kMaxDimension);
    if (width == map.width() && height == map.height()) {
        return;
    }

    tilemap::Tilemap resized(width, height, map.tileSize());
    resized.setOrigin(map.origin());
    resized.setOutsideIsSolid(map.outsideIsSolid());
    resized.setAmbient(map.ambient());
    for (const tilemap::TileDef& def : map.tileDefs()) {
        resized.addTileDef(def);
    }
    for (const tilemap::TileLayer& layer : map.layers()) {
        tilemap::TileLayer copy = layer;
        copy.tiles.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), tilemap::TileId{});
        const int copyWidth = std::min(width, map.width());
        const int copyHeight = std::min(height, map.height());
        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                copy.tiles[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x)] =
                    layer.tiles[static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width()) +
                                static_cast<std::size_t>(x)];
            }
        }
        resized.addLayer(std::move(copy));
    }
    for (const tilemap::MapArea& area : map.areas()) {
        resized.addArea(area);
    }
    for (const tilemap::MapPortal& portal : map.portals()) {
        resized.addPortal(portal);
    }
    map = std::move(resized);
    m_history.clear();
    char message[64]{};
    std::snprintf(message, sizeof(message), "RESIZED %dx%d", width, height);
    m_status = message;
}

void VisualEditor::draw(ui::UiContext& ui,
                        Rect screen,
                        std::span<const std::string> textureIds,
                        const SaveHandler& save) {
    if (!m_active) {
        return;
    }
    // The editor packs many rows into the 640x360 virtual screen, so it uses a
    // denser text scale than the HUD; otherwise widget labels overflow their rows.
    const ui::UiTheme hudTheme = ui.theme();
    ui::UiTheme denseTheme = hudTheme;
    denseTheme.textScale = std::clamp(hudTheme.textScale * 0.55f, 1.25f, 1.6f);
    ui.setTheme(denseTheme);
    const ui::UiTheme& theme = ui.theme();

    const Rect map = mapRect(screen);
    m_mapViewport = map;
    ui.panel(map, fa::core::Color{0.05f, 0.06f, 0.09f, 0.55f});
    drawToolbar(ui, screen);
    drawPalette(ui, screen, textureIds);
    drawProperties(ui, screen, textureIds, save);
    const tilemap::TileCoord cell = m_editor.hover();
    if (!m_editor.map().empty() && cell.x >= 0 && cell.y >= 0) {
        const float step = kTileSize * m_tileScale;
        ui.border(
            makeRect(map.x + static_cast<float>(cell.x) * step, map.y + static_cast<float>(cell.y) * step, step, step),
            theme.accent,
            1.0f);
    }
    if (m_editor.map().empty()) {
        ui.text("EMPTY MAP", makeRect(map.x + 8.0f, map.y + 8.0f, 200.0f, 8.0f), dimStyle(theme));
    }
    ui.setTheme(hudTheme);
}

void VisualEditor::drawToolbar(ui::UiContext& ui, Rect screen) {
    const ui::UiTheme& theme = ui.theme();
    const Rect bar = makeRect(screen.x + 4.0f, screen.y + 4.0f, screen.w - 8.0f, 16.0f);
    ui.panel(bar, theme.panel);
    float x = bar.x + 4.0f;
    const auto button = [&](std::string_view id, std::string_view text, float width) {
        const Rect rect = makeRect(x, bar.y + 2.0f, width, 12.0f);
        const bool pressed = ui.button(ui::UiId::fromName(id), text, rect).pressed;
        x += width + 3.0f;
        return pressed;
    };

    if (button("ve.brush", "BRUSH", 44.0f)) {
        m_editor.setTool(EditorTool::Brush);
    }
    if (button("ve.erase", "ERASE", 42.0f)) {
        m_editor.setTool(EditorTool::Erase);
    }
    if (button("ve.fill", "FILL", 34.0f)) {
        m_editor.setTool(EditorTool::Fill);
    }
    if (button("ve.undo", "UNDO", 40.0f)) {
        static_cast<void>(m_history.undo(m_editor.map()));
        m_status = "UNDO";
    }
    if (button("ve.redo", "REDO", 40.0f)) {
        static_cast<void>(m_history.redo(m_editor.map()));
        m_status = "REDO";
    }
    if (button("ve.back", "BACK", 40.0f)) {
        m_closeRequested = true;
    }

    char info[96]{};
    std::snprintf(info,
                  sizeof(info),
                  "LAYER %d   BRUSH %u   %s",
                  m_editor.layer(),
                  m_editor.brush().value,
                  m_history.dirty() ? "DIRTY" : "SAVED");
    ui.text(info, makeRect(x + 4.0f, bar.y + 4.0f, screen.x + screen.w - x - 8.0f, 8.0f), dimStyle(theme));
}

void VisualEditor::drawPalette(ui::UiContext& ui, Rect screen, std::span<const std::string> textureIds) {
    static_cast<void>(textureIds);
    const ui::UiTheme& theme = ui.theme();
    const Rect panel = panelRect(screen);
    const Rect palette = makeRect(panel.x, panel.y, panel.w, panel.h * 0.43f);
    ui.panel(palette, theme.panel);
    ui.text("COMPONENTS", makeRect(palette.x + 6.0f, palette.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));
    ui.text("TILES", makeRect(palette.x + 6.0f, palette.y + 16.0f, 60.0f, 8.0f), dimStyle(theme));

    float y = palette.y + 28.0f;
    for (const tilemap::TileDef& def : m_editor.map().tileDefs()) {
        if (y + 14.0f > palette.y + palette.h) {
            break;
        }
        char id[32]{};
        std::snprintf(id, sizeof(id), "ve.tile.%u", def.id.value);
        char line[64]{};
        std::snprintf(line, sizeof(line), "%s  #%u", def.name.c_str(), def.id.value);
        const bool selected = def.id == m_editor.brush();
        const Rect rect = makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 13.0f);
        if (ui.button(ui::UiId::fromName(id), line, rect).pressed) {
            m_selectedTile = def.id;
            m_editor.setBrush(def.id);
            m_status = "TILE " + def.name;
        }
        if (selected) {
            ui.border(rect, theme.accent, 1.0f);
        }
        y += 15.0f;
    }
}

void VisualEditor::drawProperties(ui::UiContext& ui,
                                  Rect screen,
                                  std::span<const std::string> textureIds,
                                  const SaveHandler& save) {
    const ui::UiTheme& theme = ui.theme();
    const Rect panel = panelRect(screen);
    const Rect paletteHeight = makeRect(panel.x, panel.y, panel.w, panel.h * 0.43f);
    const Rect props =
        makeRect(panel.x, paletteHeight.y + paletteHeight.h + 4.0f, panel.w, panel.h - paletteHeight.h - 4.0f);
    ui.panel(props, theme.panel);
    ui.text("PROPERTIES", makeRect(props.x + 6.0f, props.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));

    tilemap::Tilemap& map = m_editor.map();
    float y = props.y + 16.0f;
    char line[96]{};
    std::snprintf(line, sizeof(line), "MAP %dx%d  @%dpx", map.width(), map.height(), map.tileSize());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 11.0f;

    const float small = 16.0f;
    if (ui.button(ui::UiId::fromName("ve.w-"), "W-", makeRect(props.x + 6.0f, y, small, 11.0f)).pressed) {
        resizeMap(map.width() - 1, map.height());
    }
    if (ui.button(ui::UiId::fromName("ve.w+"), "W+", makeRect(props.x + 24.0f, y, small, 11.0f)).pressed) {
        resizeMap(map.width() + 1, map.height());
    }
    if (ui.button(ui::UiId::fromName("ve.h-"), "H-", makeRect(props.x + 44.0f, y, small, 11.0f)).pressed) {
        resizeMap(map.width(), map.height() - 1);
    }
    if (ui.button(ui::UiId::fromName("ve.h+"), "H+", makeRect(props.x + 62.0f, y, small, 11.0f)).pressed) {
        resizeMap(map.width(), map.height() + 1);
    }
    y += 14.0f;

    bool outsideSolid = map.outsideIsSolid();
    if (ui.checkbox(ui::UiId::fromName("ve.outside"),
                    "OUTSIDE SOLID",
                    outsideSolid,
                    makeRect(props.x + 6.0f, y, props.w - 12.0f, 10.0f))
            .pressed) {
        map.setOutsideIsSolid(outsideSolid);
    }
    y += 12.0f;
    tilemap::MapAmbient ambient = map.ambient();
    if (ui.checkbox(ui::UiId::fromName("ve.indoor"),
                    "INDOOR",
                    ambient.indoor,
                    makeRect(props.x + 6.0f, y, props.w - 12.0f, 10.0f))
            .pressed) {
        map.setAmbient(ambient);
    }
    y += 14.0f;

    tilemap::TileDef* def = map.mutableTileDef(m_editor.brush());
    if (def == nullptr) {
        ui.text("NO TILE SELECTED", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    std::snprintf(line, sizeof(line), "TILE #%u  %s", def->id.value, def->name.c_str());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
    y += 11.0f;

    std::snprintf(line, sizeof(line), "TEX %s", def->textureName.empty() ? "-" : def->textureName.c_str());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 11.0f;
    if (!textureIds.empty()) {
        if (ui.button(ui::UiId::fromName("ve.tex.prev"), "<", makeRect(props.x + 6.0f, y, 14.0f, 11.0f)).pressed) {
            int index = 0;
            for (std::size_t i = 0; i < textureIds.size(); ++i) {
                if (textureIds[i] == def->textureName) {
                    index = static_cast<int>(i);
                }
            }
            index = (index + static_cast<int>(textureIds.size()) - 1) % static_cast<int>(textureIds.size());
            def->textureName = textureIds[static_cast<std::size_t>(index)];
            m_selectedTexture = index;
        }
        if (ui.button(ui::UiId::fromName("ve.tex.next"), ">", makeRect(props.x + 22.0f, y, 14.0f, 11.0f)).pressed) {
            int index = 0;
            for (std::size_t i = 0; i < textureIds.size(); ++i) {
                if (textureIds[i] == def->textureName) {
                    index = static_cast<int>(i);
                }
            }
            index = (index + 1) % static_cast<int>(textureIds.size());
            def->textureName = textureIds[static_cast<std::size_t>(index)];
            m_selectedTexture = index;
        }
        y += 13.0f;
    }

    const auto flag = [&](const char* id, const char* label, bool& value) {
        return ui.checkbox(ui::UiId::fromName(id), label, value, makeRect(props.x + 6.0f, y, props.w - 12.0f, 10.0f));
    };
    const auto applyFlag = [&](const fa::ui::UiResult& result, bool& value, bool& target) {
        if (result.pressed) {
            target = value;
        }
        y += 11.0f;
    };
    bool walkable = def->walkable;
    applyFlag(flag("ve.walk", "WALKABLE", walkable), walkable, def->walkable);
    bool solid = def->solid;
    applyFlag(flag("ve.solid", "SOLID", solid), solid, def->solid);
    bool tillable = def->tillable;
    applyFlag(flag("ve.till", "TILLABLE", tillable), tillable, def->tillable);
    bool water = def->water;
    applyFlag(flag("ve.water", "WATER", water), water, def->water);

    if (ui.button(ui::UiId::fromName("ve.save"),
                  "SAVE JSON",
                  makeRect(props.x + 6.0f, props.y + props.h - 26.0f, props.w - 12.0f, 12.0f))
            .pressed) {
        const core::Status saved = save(m_editor.mapText());
        if (saved.isOk()) {
            m_history.markSaved();
            m_status = "SAVED";
        } else {
            m_status = "SAVE FAILED: " + saved.error().message;
        }
    }

    if (!m_status.empty()) {
        ui.text(m_status.substr(0, 40),
                makeRect(props.x + 6.0f, props.y + props.h - 12.0f, props.w - 12.0f, 8.0f),
                dimStyle(theme));
    }
}

}  // namespace fa::editor