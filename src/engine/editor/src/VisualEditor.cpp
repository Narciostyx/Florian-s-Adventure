#include "fa/editor/VisualEditor.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    for (const tilemap::TileDef& def : map.tileDefs()) {
        if (isComponentDef(def)) {
            m_decorBrush = def.id;
            break;
        }
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

Rect VisualEditor::propsRect(Rect screen) noexcept {
    const Rect panel = panelRect(screen);
    const float paletteHeight = panel.h * 0.43f;
    return makeRect(panel.x, panel.y + paletteHeight + 4.0f, panel.w, panel.h - paletteHeight - 4.0f);
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
    // The object scopes edit their own data and must not paint tiles underneath.
    if (m_scope == EditorScope::Placements) {
        if (input.primaryPressed) {
            applyPlacementClick(cell);
        } else if (input.secondaryPressed) {
            applyPlacementErase(cell);
        }
        return;
    }
    // Components stand on top of the ground: this scope paints the decor layer
    // only, and only over grass, so a placement never replaces the tile below.
    if (m_scope == EditorScope::Decor) {
        if (input.primaryPressed) {
            applyDecorClick(cell);
        } else if (input.secondaryPressed) {
            applyDecorErase(cell);
        }
        return;
    }
    // Regions: one scope, two tools. The palette toggle decides whether the two
    // clicks build a portal trigger or a collision zone.
    if (m_scope == EditorScope::Areas) {
        if (input.primaryPressed) {
            if (m_areaCreatePortal) {
                applyPortalClick(cell);
            } else {
                applyBlockerClick(cell);
            }
        } else if (input.secondaryPressed) {
            m_portalStart.reset();
            m_blockerStart.reset();
            m_status = "已取消";
        }
        return;
    }
    if (m_scope == EditorScope::Entities) {
        // Read-only scope: a click only moves the selection, nothing is authored here.
        if (input.primaryPressed) {
            m_selectedEntity = entityIndexAt(cell);
            if (m_selectedEntity >= 0) {
                m_status = "实体 " + m_entityInfos[static_cast<std::size_t>(m_selectedEntity)].id;
            }
        } else if (input.secondaryPressed) {
            m_selectedEntity = -1;
        }
        return;
    }
    // Age the pending first click once per update; the double-click window is
    // measured in frames because `update` runs on the render tick.
    if (m_framesSinceClick >= 0) {
        ++m_framesSinceClick;
    }
    if (input.primaryPressed) {
        if (m_editor.tool() == EditorTool::Brush) {
            // Single click inspects the tile under the cursor; a second click on the
            // same cell within the double-click window replaces it with the brush.
            const bool secondClick =
                m_framesSinceClick >= 0 && m_framesSinceClick <= kDoubleClickFrames && cell == m_lastClickCell;
            if (secondClick) {
                m_framesSinceClick = -1;
                const core::Status status = m_editor.applyPrimary();
                m_status = status.isOk() ? "已替换瓦片" : status.error().message;
                m_inspectedTile = m_editor.brush();
                m_inspectedCell = cell;
                m_hasInspected = true;
            } else {
                m_framesSinceClick = 0;
                m_lastClickCell = cell;
                m_inspectedTile = map.tileAt(cell.x, cell.y, m_editor.layer());
                m_inspectedCell = cell;
                m_hasInspected = true;
                m_status = "查看瓦片属性";
            }
        } else {
            const core::Status status = m_editor.applyPrimary();
            m_status = status.isOk() ? "编辑完成" : status.error().message;
        }
    } else if (input.secondaryPressed) {
        const core::Status status = m_editor.applySecondary();
        m_status = status.isOk() ? "已擦除" : status.error().message;
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
    for (const tilemap::MapBlocker& blocker : map.blockers()) {
        resized.addBlocker(blocker);
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
    // The editor packs many rows into the 640x360 virtual screen. With the 10 px baked
    // atlas there is only one usable widget scale (1x), and the default 5 px padding would
    // leave the 20 px size buttons no inner width at all, so the editor draws denser.
    const ui::UiTheme hudTheme = ui.theme();
    ui::UiTheme denseTheme = hudTheme;
    denseTheme.textScale = 1.0f;
    denseTheme.padding = 2.0f;
    ui.setTheme(denseTheme);
    const ui::UiTheme& theme = ui.theme();

    const Rect map = mapRect(screen);
    m_mapViewport = map;
    ui.panel(map, fa::core::Color{0.05f, 0.06f, 0.09f, 0.55f});

    // Portal triggers and placed objects sit on top of the map so the editor shows what the
    // map will actually ask the game to spawn. They are UI quads, so the tile sprites (an
    // earlier render layer) stay underneath them.
    if (!m_editor.map().empty()) {
        const float step = kTileSize * m_tileScale;
        for (const tilemap::MapBlocker& blocker : m_editor.map().blockers()) {
            const Rect box = makeRect(map.x + static_cast<float>(blocker.bounds.x) * step,
                                      map.y + static_cast<float>(blocker.bounds.y) * step,
                                      static_cast<float>(blocker.bounds.width) * step,
                                      static_cast<float>(blocker.bounds.height) * step);
            // Reddish: blocked ground. A zone reopened as a door is drawn hollow.
            ui.panel(box,
                     blocker.passable ? fa::core::Color{0.30f, 0.50f, 0.80f, 0.18f}
                                      : fa::core::Color{0.72f, 0.28f, 0.24f, 0.32f});
            ui.border(box, fa::core::Color{0.85f, 0.40f, 0.32f, 0.90f}, 1.0f);
        }
        for (const tilemap::MapPortal& portal : m_editor.map().portals()) {
            const Rect box = makeRect(map.x + static_cast<float>(portal.bounds.x) * step,
                                      map.y + static_cast<float>(portal.bounds.y) * step,
                                      static_cast<float>(portal.bounds.width) * step,
                                      static_cast<float>(portal.bounds.height) * step);
            ui.panel(box, fa::core::Color{0.20f, 0.55f, 0.30f, 0.35f});
            ui.border(box, theme.accent, 1.0f);
        }
        if (m_placements != nullptr) {
            for (const MapPlacement& row : *m_placements) {
                ui.panel(makeRect(map.x + static_cast<float>(row.cell.x) * step + 3.0f,
                                  map.y + static_cast<float>(row.cell.y) * step + 3.0f,
                                  step - 6.0f,
                                  step - 6.0f),
                         fa::core::Color{0.92f, 0.60f, 0.16f, 0.90f});
            }
        }
        if (m_scope == EditorScope::Entities) {
            for (std::size_t index = 0; index < m_entityInfos.size(); ++index) {
                const EntityInfoRow& row = m_entityInfos[index];
                if (!row.hasCell) {
                    continue;
                }
                const Rect box = makeRect(map.x + static_cast<float>(row.cell.x) * step + 2.0f,
                                          map.y + static_cast<float>(row.cell.y) * step + 2.0f,
                                          step - 4.0f,
                                          step - 4.0f);
                ui.panel(box, fa::core::Color{0.20f, 0.70f, 0.90f, 0.55f});
                if (m_selectedEntity == static_cast<int>(index)) {
                    ui.border(box, fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
                }
            }
        }
    }

    drawToolbar(ui, screen);
    drawPalette(ui, screen, textureIds);
    drawProperties(ui, screen, textureIds, save);
    drawFooter(ui, screen, save);
    const tilemap::TileCoord cell = m_editor.hover();
    if (!m_editor.map().empty() && cell.x >= 0 && cell.y >= 0) {
        const float step = kTileSize * m_tileScale;
        ui.border(
            makeRect(map.x + static_cast<float>(cell.x) * step, map.y + static_cast<float>(cell.y) * step, step, step),
            theme.accent,
            1.0f);
    }
    // White box marks the inspected (single-clicked) tile.
    if (m_scope == EditorScope::Tiles && m_hasInspected && !m_editor.map().empty() && m_inspectedCell.x >= 0) {
        const float step = kTileSize * m_tileScale;
        ui.border(makeRect(map.x + static_cast<float>(m_inspectedCell.x) * step,
                           map.y + static_cast<float>(m_inspectedCell.y) * step,
                           step,
                           step),
                  fa::core::Color{1.0f, 1.0f, 1.0f, 0.9f},
                  1.0f);
    }
    if (m_editor.map().empty()) {
        ui.text("空地图", makeRect(map.x + 8.0f, map.y + 8.0f, 200.0f, 8.0f), dimStyle(theme));
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

    // Scope first: what the canvas edits decides which tools are even meaningful.
    const bool tileScope = m_scope == EditorScope::Tiles;
    if (button("ve.scope.tiles", "瓦片", 36.0f)) {
        setScope(EditorScope::Tiles);
    }
    if (button("ve.scope.decor", "组件", 36.0f)) {
        setScope(EditorScope::Decor);
    }
    if (button("ve.scope.areas", "区域", 36.0f)) {
        setScope(EditorScope::Areas);
    }
    if (button("ve.scope.objects", "对象", 36.0f)) {
        setScope(EditorScope::Placements);
    }
    if (button("ve.scope.entities", "实体", 36.0f)) {
        setScope(EditorScope::Entities);
    }
    x += 8.0f;

    if (tileScope) {
        if (button("ve.brush", "笔刷", 44.0f)) {
            m_editor.setTool(EditorTool::Brush);
        }
        if (button("ve.erase", "橡皮", 42.0f)) {
            m_editor.setTool(EditorTool::Erase);
        }
        if (button("ve.fill", "填充", 34.0f)) {
            m_editor.setTool(EditorTool::Fill);
        }
        x += 8.0f;
    }
    // Undo/redo walk the same tile history wherever the change came from: tile
    // brushes and decor placements are both tile commands.
    if (tileScope || m_scope == EditorScope::Decor) {
        if (button("ve.undo", "撤销", 40.0f)) {
            static_cast<void>(m_history.undo(m_editor.map()));
            m_status = "已撤销";
        }
        if (button("ve.redo", "重做", 40.0f)) {
            static_cast<void>(m_history.redo(m_editor.map()));
            m_status = "已重做";
        }
    }
    if (button("ve.back", "返回", 40.0f)) {
        m_closeRequested = true;
    }

    char info[96]{};
    switch (m_scope) {
        case EditorScope::Decor:
            if (m_activeComponentKind >= 0) {
                std::snprintf(info,
                              sizeof(info),
                              "组件 %s  %s",
                              m_componentKinds[static_cast<std::size_t>(m_activeComponentKind)].label.c_str(),
                              m_history.dirty() ? "未保存" : "已保存");
            } else {
                std::snprintf(info,
                              sizeof(info),
                              "组件 #%u  仅草地  %s",
                              m_decorBrush.value,
                              m_history.dirty() ? "未保存" : "已保存");
            }
            break;
        case EditorScope::Areas:
            std::snprintf(info,
                          sizeof(info),
                          "区域 传送门%d 碰撞区%d  %s",
                          static_cast<int>(m_editor.map().portals().size()),
                          static_cast<int>(m_editor.map().blockers().size()),
                          m_history.dirty() ? "未保存" : "已保存");
            break;
        case EditorScope::Placements:
            std::snprintf(info,
                          sizeof(info),
                          "对象 %d 个  %s",
                          m_placements != nullptr ? static_cast<int>(m_placements->size()) : 0,
                          m_history.dirty() ? "未保存" : "已保存");
            break;
        case EditorScope::Entities:
            std::snprintf(info, sizeof(info), "实体 %d 个  只读", static_cast<int>(m_entityInfos.size()));
            break;
        default:
            std::snprintf(info,
                          sizeof(info),
                          "图层 %d  笔刷 %u  %s",
                          m_editor.layer(),
                          m_editor.brush().value,
                          m_history.dirty() ? "未保存" : "已保存");
            break;
    }
    ui.text(info, makeRect(x + 4.0f, bar.y + 4.0f, screen.x + screen.w - x - 8.0f, 8.0f), dimStyle(theme));
}

void VisualEditor::drawPalette(ui::UiContext& ui, Rect screen, std::span<const std::string> textureIds) {
    static_cast<void>(textureIds);
    const ui::UiTheme& theme = ui.theme();
    const Rect panel = panelRect(screen);
    const Rect palette = makeRect(panel.x, panel.y, panel.w, panel.h * 0.43f);
    if (m_scope == EditorScope::Entities) {
        drawEntityPalette(ui, palette);
        return;
    }
    if (m_scope == EditorScope::Decor) {
        drawDecorPalette(ui, palette);
        return;
    }
    if (m_scope == EditorScope::Areas) {
        drawAreaPalette(ui, palette);
        return;
    }
    if (m_scope != EditorScope::Tiles) {
        drawObjectPalette(ui, palette);
        return;
    }
    ui.panel(palette, theme.panel);
    // The palette header names the scope itself: tiles are not "components".
    ui.text("瓦片", makeRect(palette.x + 6.0f, palette.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));

    float y = palette.y + 16.0f;
    for (const tilemap::TileDef& def : m_editor.map().tileDefs()) {
        // Grass/flower/tree-like overlays are components, not tiles: they live
        // in the 组件 scope so painting one can never replace the ground tile.
        if (isComponentDef(def)) {
            continue;
        }
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
            m_hasInspected = false;
            m_framesSinceClick = -1;
            m_status = "瓦片 " + def.name;
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
    const Rect props = propsRect(screen);
    ui.panel(props, theme.panel);
    ui.text("属性", makeRect(props.x + 6.0f, props.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));
    if (m_scope == EditorScope::Entities) {
        drawEntityProperties(ui, props);
        return;
    }
    if (m_scope == EditorScope::Decor) {
        drawDecorProperties(ui, props);
        return;
    }
    if (m_scope == EditorScope::Areas) {
        if (m_selectedPortal >= 0) {
            drawPortalProperties(ui, props);
        } else if (m_selectedBlocker >= 0) {
            drawBlockerProperties(ui, props, textureIds);
        } else {
            ui.text(
                "选择或点两格新建", makeRect(props.x + 6.0f, props.y + 16.0f, props.w - 12.0f, 8.0f), dimStyle(theme));
        }
        return;
    }
    if (m_scope != EditorScope::Tiles) {
        drawObjectProperties(ui, props);
        return;
    }

    tilemap::Tilemap& map = m_editor.map();
    float y = props.y + 16.0f;
    char line[96]{};
    std::snprintf(line, sizeof(line), "地图 %dx%d  @%dpx", map.width(), map.height(), map.tileSize());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 11.0f;

    // Comfort grid: quarter-width resize buttons, half-width checkbox rows.
    const float inner = props.w - 12.0f;
    const float gap = 3.0f;
    const float quarter = (inner - 3.0f * gap) / 4.0f;
    const float half = (inner - 6.0f) / 2.0f;
    const float rowH = 13.0f;
    const auto quarterButton = [&](const char* name, std::string_view text, int index) {
        return ui
            .button(ui::UiId::fromName(name),
                    text,
                    makeRect(props.x + 6.0f + static_cast<float>(index) * (quarter + gap), y, quarter, rowH))
            .pressed;
    };

    if (quarterButton("ve.w-", "宽-", 0)) {
        resizeMap(map.width() - 1, map.height());
    }
    if (quarterButton("ve.w+", "宽+", 1)) {
        resizeMap(map.width() + 1, map.height());
    }
    if (quarterButton("ve.h-", "高-", 2)) {
        resizeMap(map.width(), map.height() - 1);
    }
    if (quarterButton("ve.h+", "高+", 3)) {
        resizeMap(map.width(), map.height() + 1);
    }
    y += rowH + 4.0f;

    // Outside-solid and indoor share one row, half width each.
    bool outsideSolid = map.outsideIsSolid();
    if (ui.checkbox(ui::UiId::fromName("ve.outside"), "界外阻挡", outsideSolid, makeRect(props.x + 6.0f, y, half, rowH))
            .pressed) {
        map.setOutsideIsSolid(outsideSolid);
    }
    tilemap::MapAmbient ambient = map.ambient();
    if (ui.checkbox(ui::UiId::fromName("ve.indoor"),
                    "室内",
                    ambient.indoor,
                    makeRect(props.x + 6.0f + half + 6.0f, y, half, rowH))
            .pressed) {
        map.setAmbient(ambient);
    }
    y += rowH + 5.0f;

    // The inspector shows the single-clicked tile; the palette brush is the fallback.
    const tilemap::TileId shownTile = m_hasInspected ? m_inspectedTile : m_editor.brush();
    tilemap::TileDef* def = map.mutableTileDef(shownTile);
    if (def == nullptr) {
        ui.text("未选中瓦片", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    std::snprintf(line, sizeof(line), "瓦片 #%u  %s", def->id.value, def->name.c_str());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
    y += 12.0f;

    std::snprintf(line, sizeof(line), "贴图 %s", def->textureName.empty() ? "-" : def->textureName.c_str());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 12.0f;
    if (!textureIds.empty()) {
        if (ui.button(ui::UiId::fromName("ve.tex.prev"), "<", makeRect(props.x + 6.0f, y, quarter, rowH)).pressed) {
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
        if (ui.button(
                  ui::UiId::fromName("ve.tex.next"), ">", makeRect(props.x + 6.0f + quarter + gap, y, quarter, rowH))
                .pressed) {
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
        y += rowH + 4.0f;
    }

    // Flag grid, 2 x 2. Walkable and solid are mutually exclusive: a tile that
    // bodies may stand on cannot block them, and vice versa. Conditional passage
    // (a boat crossing water) is handled by gameplay code exempting `water` from
    // the solid test, not by a walkable+solid combination.
    const auto halfFlag = [&](const char* id, const char* label, bool& value, int column) {
        return ui
            .checkbox(ui::UiId::fromName(id),
                      label,
                      value,
                      makeRect(props.x + 6.0f + static_cast<float>(column) * (half + 6.0f), y, half, rowH))
            .pressed;
    };
    bool walkable = def->walkable;
    bool solid = def->solid;
    const bool walkPressed = halfFlag("ve.walk", "可通行", walkable, 0);
    const bool solidPressed = halfFlag("ve.solid", "阻挡", solid, 1);
    if (walkPressed) {
        def->walkable = walkable;
        if (walkable) {
            def->solid = false;
        }
    }
    if (solidPressed) {
        def->solid = solid;
        if (solid) {
            def->walkable = false;
        }
    }
    y += rowH + 3.0f;
    bool tillable = def->tillable;
    if (halfFlag("ve.till", "可耕", tillable, 0)) {
        def->tillable = tillable;
    }
    bool water = def->water;
    if (halfFlag("ve.water", "水", water, 1)) {
        def->water = water;
    }
    y += rowH + 3.0f;
    ui.text("可通行与阻挡互斥；水+船可过", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
}

void VisualEditor::setPlacements(std::vector<MapPlacement>* placements, std::vector<PlacementKind> kinds) {
    m_placements = placements;
    m_kinds = std::move(kinds);
    m_activeKind = 0;
    m_selectedPlacement = -1;
}

void VisualEditor::setComponentKinds(std::vector<PlacementKind> kinds) {
    m_componentKinds = std::move(kinds);
    m_activeComponentKind = -1;
}

bool VisualEditor::selectComponent(std::string_view kind) noexcept {
    for (std::size_t index = 0; index < m_componentKinds.size(); ++index) {
        if (m_componentKinds[index].kind == kind) {
            m_activeComponentKind = static_cast<int>(index);
            return true;
        }
    }
    return false;
}

void VisualEditor::setPlacementSave(PlacementSaveHandler handler) {
    m_placementSave = std::move(handler);
}

void VisualEditor::setPortalTargets(std::vector<std::string> targets) {
    m_portalTargets = std::move(targets);
}

void VisualEditor::setEntityInfos(std::vector<EntityInfoRow> rows) {
    m_entityInfos = std::move(rows);
    m_selectedEntity = m_entityInfos.empty() ? -1 : 0;
}

int VisualEditor::entityIndexAt(const tilemap::TileCoord cell) const noexcept {
    for (std::size_t index = 0; index < m_entityInfos.size(); ++index) {
        if (m_entityInfos[index].hasCell && m_entityInfos[index].cell == cell) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void VisualEditor::setScope(EditorScope scope) noexcept {
    if (m_scope == scope) {
        return;
    }
    m_scope = scope;
    m_portalStart.reset();
    m_blockerStart.reset();
    m_activeComponentKind = -1;
    m_selectedPlacement = -1;
    m_selectedPortal = -1;
    m_selectedBlocker = -1;
    m_selectedEntity = m_entityInfos.empty() ? -1 : 0;
    m_hasInspected = false;
    m_framesSinceClick = -1;
}

std::span<const MapPlacement> VisualEditor::placements() const noexcept {
    if (m_placements == nullptr) {
        return {};
    }
    return std::span<const MapPlacement>{*m_placements};
}

std::span<const tilemap::MapBlocker> VisualEditor::blockers() const noexcept {
    return m_editor.map().blockers();
}

int VisualEditor::placementIndexAt(const tilemap::TileCoord cell) const noexcept {
    if (m_placements == nullptr) {
        return -1;
    }
    for (std::size_t index = 0; index < m_placements->size(); ++index) {
        if ((*m_placements)[index].cell == cell) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int VisualEditor::portalIndexAt(const tilemap::TileCoord cell) const noexcept {
    const tilemap::Tilemap& map = m_editor.map();
    const std::span<const tilemap::MapPortal> portals = map.portals();
    for (std::size_t index = 0; index < portals.size(); ++index) {
        if (portals[index].bounds.contains(cell)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void VisualEditor::applyPlacementClick(const tilemap::TileCoord cell) {
    if (m_placements == nullptr || m_kinds.empty()) {
        m_status = "没有可放置的对象";
        return;
    }
    const int existing = placementIndexAt(cell);
    if (existing >= 0) {
        m_selectedPlacement = existing;
        m_status = "已选中对象";
        return;
    }
    const PlacementKind& kind =
        m_kinds[static_cast<std::size_t>(std::clamp(m_activeKind, 0, static_cast<int>(m_kinds.size()) - 1))];
    MapPlacement placement;
    placement.kind = kind.kind;
    placement.cell = cell;
    // Ids are generated, not typed: M09 has no text field, and the id is only a stable key
    // inside entities.json.
    std::string candidate;
    for (int serial = 1;; ++serial) {
        candidate = kind.kind + "_" + std::to_string(serial);
        const bool taken = std::any_of(m_placements->begin(),
                                       m_placements->end(),
                                       [&candidate](const MapPlacement& row) { return row.id == candidate; });
        if (!taken) {
            break;
        }
    }
    placement.id = std::move(candidate);
    if (!kind.targets.empty()) {
        placement.target = kind.targets.front();
    }
    m_placements->push_back(std::move(placement));
    m_selectedPlacement = static_cast<int>(m_placements->size()) - 1;
    m_history.markDirty();
    m_status = "已放置 " + kind.label;
}

void VisualEditor::applyPlacementErase(const tilemap::TileCoord cell) {
    const int index = placementIndexAt(cell);
    if (index < 0 || m_placements == nullptr) {
        m_status = "这里没有对象";
        return;
    }
    m_status = "已删除 " + (*m_placements)[static_cast<std::size_t>(index)].id;
    m_placements->erase(m_placements->begin() + index);
    m_selectedPlacement = -1;
    m_history.markDirty();
}

void VisualEditor::applyPortalClick(const tilemap::TileCoord cell) {
    if (m_portalStart.has_value()) {
        const tilemap::TileCoord from = *m_portalStart;
        const tilemap::TileCoord to = cell;
        tilemap::MapPortal portal;
        portal.bounds = tilemap::TileBounds{
            std::min(from.x, to.x), std::min(from.y, to.y), std::abs(from.x - to.x) + 1, std::abs(from.y - to.y) + 1};
        portal.id = "portal_" + std::to_string(m_editor.map().portals().size() + 1);
        if (!m_portalTargets.empty()) {
            portal.targetMap = m_portalTargets.front();
        }
        const int portalWidth = portal.bounds.width;
        const int portalHeight = portal.bounds.height;
        m_editor.map().addPortal(std::move(portal));
        m_portalStart.reset();
        m_selectedPortal = static_cast<int>(m_editor.map().portals().size()) - 1;
        m_history.markDirty();
        char line[64]{};
        std::snprintf(line, sizeof(line), "已创建传送门 %dx%d", portalWidth, portalHeight);
        m_status = line;
        return;
    }
    const int existing = portalIndexAt(cell);
    if (existing >= 0) {
        m_selectedPortal = existing;
        m_status = "已选中传送门";
        return;
    }
    m_portalStart = cell;
    m_status = "再点一次确定对角";
}

void VisualEditor::saveObjects() {
    if (m_placementSave == nullptr || m_placements == nullptr) {
        return;
    }
    const core::Status saved = m_placementSave(*m_placements);
    if (saved.isError()) {
        m_status = "对象保存失败：" + saved.error().message;
    }
}

void VisualEditor::drawObjectPalette(ui::UiContext& ui, Rect palette) {
    const ui::UiTheme& theme = ui.theme();
    ui.panel(palette, theme.panel);
    // Objects are the characters: NPC, merchant, enemies. Furniture and hazards
    // moved to the 组件 scope; regions (portals/zones) to the 区域 scope.
    ui.text("对象", makeRect(palette.x + 6.0f, palette.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));

    float y = palette.y + 16.0f;
    if (m_placements == nullptr || m_kinds.empty()) {
        ui.text("无可用对象", makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    for (std::size_t index = 0; index < m_kinds.size(); ++index) {
        if (y + 13.0f > palette.y + palette.h) {
            break;
        }
        char id[32]{};
        std::snprintf(id, sizeof(id), "ve.kind.%zu", index);
        const Rect rect = makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 13.0f);
        if (ui.button(ui::UiId::fromName(id), m_kinds[index].label, rect).pressed) {
            m_activeKind = static_cast<int>(index);
            m_status = "选择：" + m_kinds[index].label;
        }
        if (m_activeKind == static_cast<int>(index)) {
            ui.border(rect, theme.accent, 1.0f);
        }
        y += 15.0f;
    }
}

void VisualEditor::drawAreaPalette(ui::UiContext& ui, Rect palette) {
    const ui::UiTheme& theme = ui.theme();
    ui.panel(palette, theme.panel);
    ui.text("区域", makeRect(palette.x + 6.0f, palette.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));

    float y = palette.y + 16.0f;
    // Create-tool toggle: which region type the next two clicks build.
    const float half = (palette.w - 12.0f - 3.0f) / 2.0f;
    const Rect portalTool = makeRect(palette.x + 6.0f, y, half, 13.0f);
    if (ui.button(ui::UiId::fromName("ve.area.tool.portal"), "传送门", portalTool).pressed) {
        m_areaCreatePortal = true;
        m_status = "新建：传送门";
    }
    if (m_areaCreatePortal) {
        ui.border(portalTool, theme.accent, 1.0f);
    }
    const Rect zoneTool = makeRect(palette.x + 6.0f + half + 3.0f, y, half, 13.0f);
    if (ui.button(ui::UiId::fromName("ve.area.tool.blocker"), "碰撞区", zoneTool).pressed) {
        m_areaCreatePortal = false;
        m_status = "新建：碰撞区";
    }
    if (!m_areaCreatePortal) {
        ui.border(zoneTool, theme.accent, 1.0f);
    }
    y += 15.0f;

    // One list: portals first, then collision zones. Selecting a row clears the
    // other selection so the inspector always edits exactly one region.
    const std::span<const tilemap::MapPortal> portals = m_editor.map().portals();
    for (std::size_t index = 0; index < portals.size(); ++index) {
        if (y + 13.0f > palette.y + palette.h) {
            return;
        }
        char id[32]{};
        std::snprintf(id, sizeof(id), "ve.area.portal.%zu", index);
        char line[64]{};
        std::snprintf(line,
                      sizeof(line),
                      "%s  %dx%d  门",
                      portals[index].id.c_str(),
                      portals[index].bounds.width,
                      portals[index].bounds.height);
        const Rect rect = makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 13.0f);
        if (ui.button(ui::UiId::fromName(id), line, rect).pressed) {
            m_selectedPortal = static_cast<int>(index);
            m_selectedBlocker = -1;
            m_portalStart.reset();
        }
        if (m_selectedPortal == static_cast<int>(index)) {
            ui.border(rect, theme.accent, 1.0f);
        }
        y += 15.0f;
    }
    const std::span<const tilemap::MapBlocker> blockers = m_editor.map().blockers();
    for (std::size_t index = 0; index < blockers.size(); ++index) {
        if (y + 13.0f > palette.y + palette.h) {
            return;
        }
        char id[32]{};
        std::snprintf(id, sizeof(id), "ve.area.blocker.%zu", index);
        char line[64]{};
        std::snprintf(line,
                      sizeof(line),
                      "%s  %dx%d  区",
                      blockers[index].id.c_str(),
                      blockers[index].bounds.width,
                      blockers[index].bounds.height);
        const Rect rect = makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 13.0f);
        if (ui.button(ui::UiId::fromName(id), line, rect).pressed) {
            m_selectedBlocker = static_cast<int>(index);
            m_selectedPortal = -1;
            m_blockerStart.reset();
        }
        if (m_selectedBlocker == static_cast<int>(index)) {
            ui.border(rect, theme.accent, 1.0f);
        }
        y += 15.0f;
    }
    if (portals.empty() && blockers.empty()) {
        ui.text("无 - 选类型后点两格", makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 8.0f), dimStyle(theme));
    }
}

void VisualEditor::drawPortalProperties(ui::UiContext& ui, Rect props) {
    const ui::UiTheme& theme = ui.theme();
    constexpr float small = 20.0f;
    constexpr float rowPitch = 13.0f;
    float y = props.y + 16.0f;
    char id[32]{};
    char line[96]{};

    const auto stepButton = [&](const char* name, std::string_view text, float x) {
        return ui.button(ui::UiId::fromName(name), text, makeRect(x, y, small, 11.0f)).pressed;
    };

    const std::span<tilemap::MapPortal> portals = m_editor.map().mutablePortals();
    if (m_selectedPortal < 0 || m_selectedPortal >= static_cast<int>(portals.size())) {
        ui.text("选择或点两格新建", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    tilemap::MapPortal& portal = portals[static_cast<std::size_t>(m_selectedPortal)];
    std::snprintf(line, sizeof(line), "%s  触发 %dx%d", portal.id.c_str(), portal.bounds.width, portal.bounds.height);
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
    y += rowPitch;

    ui.text("通往", makeRect(props.x + 6.0f, y, 34.0f, 8.0f), dimStyle(theme));
    if (!m_portalTargets.empty() && portal.targetMap.size() < sizeof(line)) {
        std::snprintf(line, sizeof(line), "< %s >", portal.targetMap.c_str());
        if (ui.button(ui::UiId::fromName("ve.portal.map"), line, makeRect(props.x + 44.0f, y, props.w - 50.0f, 11.0f))
                .pressed) {
            // Cycle rather than type: the map registry belongs to the composition root.
            const auto found = std::find(m_portalTargets.begin(), m_portalTargets.end(), portal.targetMap);
            const std::size_t next =
                found == m_portalTargets.end()
                    ? 0
                    : (static_cast<std::size_t>(found - m_portalTargets.begin()) + 1) % m_portalTargets.size();
            portal.targetMap = m_portalTargets[next];
            m_history.markDirty();
        }
        y += rowPitch;
    } else {
        std::snprintf(line, sizeof(line), "通往 %s", portal.targetMap.empty() ? "-" : portal.targetMap.c_str());
        ui.text(line, makeRect(props.x + 44.0f, y, props.w - 50.0f, 8.0f), dimStyle(theme));
        y += rowPitch;
    }

    std::snprintf(line, sizeof(line), "落点 %d,%d", portal.targetTile.x, portal.targetTile.y);
    ui.text(line, makeRect(props.x + 6.0f, y, 90.0f, 8.0f), dimStyle(theme));
    if (stepButton("ve.portal.y-", "高-", props.x + 96.0f)) {
        --portal.targetTile.y;
        m_history.markDirty();
    }
    if (stepButton("ve.portal.y+", "高+", props.x + 118.0f)) {
        ++portal.targetTile.y;
        m_history.markDirty();
    }
    if (stepButton("ve.portal.x-", "宽-", props.x + 140.0f)) {
        --portal.targetTile.x;
        m_history.markDirty();
    }
    if (stepButton("ve.portal.x+", "宽+", props.x + 162.0f)) {
        ++portal.targetTile.x;
        m_history.markDirty();
    }
    y += rowPitch;

    std::snprintf(id, sizeof(id), "ve.portal.delete.%d", m_selectedPortal);
    if (ui.button(ui::UiId::fromName(id), "删除", makeRect(props.x + 6.0f, y, props.w - 12.0f, 11.0f)).pressed) {
        m_editor.map().removePortal(static_cast<std::size_t>(m_selectedPortal));
        m_selectedPortal = -1;
        m_history.markDirty();
        m_status = "已删除传送门";
    }
}

void VisualEditor::drawObjectProperties(ui::UiContext& ui, Rect props) {
    const ui::UiTheme& theme = ui.theme();
    constexpr float small = 20.0f;
    constexpr float rowPitch = 13.0f;
    float y = props.y + 16.0f;
    char id[32]{};
    char line[96]{};

    const auto stepButton = [&](const char* name, std::string_view text, float x) {
        return ui.button(ui::UiId::fromName(name), text, makeRect(x, y, small, 11.0f)).pressed;
    };

    if (m_placements == nullptr) {
        ui.text("没有对象表", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    std::snprintf(line, sizeof(line), "对象 %d 个", static_cast<int>(m_placements->size()));
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += rowPitch;
    if (m_placements->empty()) {
        ui.text("左键放置，右键删除", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    if (m_selectedPlacement >= static_cast<int>(m_placements->size())) {
        m_selectedPlacement = static_cast<int>(m_placements->size()) - 1;
    }

    for (std::size_t index = 0; index < m_placements->size(); ++index) {
        if (y + rowPitch > props.y + props.h - 14.0f) {
            break;
        }
        const MapPlacement& row = (*m_placements)[index];
        std::snprintf(id, sizeof(id), "ve.pick.%zu", index);
        std::snprintf(line, sizeof(line), "%s %d,%d", row.id.c_str(), row.cell.x, row.cell.y);
        const Rect rect = makeRect(props.x + 6.0f, y, props.w - 12.0f, 11.0f);
        if (ui.button(ui::UiId::fromName(id), line, rect).pressed) {
            m_selectedPlacement = static_cast<int>(index);
        }
        if (m_selectedPlacement == static_cast<int>(index)) {
            ui.border(rect, theme.accent, 1.0f);
        }
        y += rowPitch;
    }

    if (m_selectedPlacement < 0) {
        return;
    }
    MapPlacement& selected = (*m_placements)[static_cast<std::size_t>(m_selectedPlacement)];
    const PlacementKind* kind = nullptr;
    for (const PlacementKind& option : m_kinds) {
        if (option.kind == selected.kind) {
            kind = &option;
            break;
        }
    }
    std::snprintf(line,
                  sizeof(line),
                  "%s  %d,%d",
                  kind != nullptr ? kind->label.c_str() : selected.kind.c_str(),
                  selected.cell.x,
                  selected.cell.y);
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
    y += rowPitch;

    if (stepButton("ve.place.y-", "高-", props.x + 6.0f)) {
        --selected.cell.y;
        m_history.markDirty();
    }
    if (stepButton("ve.place.y+", "高+", props.x + 28.0f)) {
        ++selected.cell.y;
        m_history.markDirty();
    }
    if (stepButton("ve.place.x-", "宽-", props.x + 50.0f)) {
        --selected.cell.x;
        m_history.markDirty();
    }
    if (stepButton("ve.place.x+", "宽+", props.x + 72.0f)) {
        ++selected.cell.x;
        m_history.markDirty();
    }
    if (kind != nullptr && !kind->targets.empty()) {
        const auto found = std::find(kind->targets.begin(), kind->targets.end(), selected.target);
        const std::size_t next =
            found == kind->targets.end()
                ? 0
                : (static_cast<std::size_t>(found - kind->targets.begin()) + 1) % kind->targets.size();
        std::snprintf(line, sizeof(line), "目标 < %s >", selected.target.empty() ? "-" : selected.target.c_str());
        if (ui.button(
                  ui::UiId::fromName("ve.place.target"), line, makeRect(props.x + 96.0f, y, props.w - 102.0f, 11.0f))
                .pressed) {
            selected.target = kind->targets[next];
            m_history.markDirty();
        }
    }
    y += rowPitch;

    std::snprintf(id, sizeof(id), "ve.place.delete.%d", m_selectedPlacement);
    if (ui.button(ui::UiId::fromName(id), "删除", makeRect(props.x + 6.0f, y, props.w - 12.0f, 11.0f)).pressed) {
        m_status = "已删除 " + selected.id;
        m_placements->erase(m_placements->begin() + m_selectedPlacement);
        m_selectedPlacement = -1;
        m_history.markDirty();
    }
}

void VisualEditor::drawFooter(ui::UiContext& ui, Rect screen, const SaveHandler& save) {
    const ui::UiTheme& theme = ui.theme();
    const Rect props = propsRect(screen);

    if (ui.button(ui::UiId::fromName("ve.save"),
                  "保存",
                  makeRect(props.x + 6.0f, props.y + props.h - 26.0f, props.w - 12.0f, 12.0f))
            .pressed) {
        const core::Status saved = save(m_editor.mapText());
        if (saved.isOk()) {
            m_history.markSaved();
            m_status = "已保存";
            // The map and the placement table are one editing session, so 保存 writes both.
            // Runs last: its failure message must not be overwritten.
            saveObjects();
        } else {
            m_status = "保存失败：" + saved.error().message;
        }
    }

    if (!m_status.empty()) {
        ui.text(fa::ui::fitToCodepoints(m_status, 40),
                makeRect(props.x + 6.0f, props.y + props.h - 12.0f, props.w - 12.0f, 8.0f),
                dimStyle(theme));
    }
}

void VisualEditor::drawEntityPalette(ui::UiContext& ui, Rect palette) {
    const ui::UiTheme& theme = ui.theme();
    ui.panel(palette, theme.panel);
    ui.text("实体（只读）", makeRect(palette.x + 6.0f, palette.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));

    float y = palette.y + 16.0f;
    if (m_entityInfos.empty()) {
        ui.text("无实体数据", makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    for (std::size_t index = 0; index < m_entityInfos.size(); ++index) {
        if (y + 13.0f > palette.y + palette.h) {
            break;
        }
        const EntityInfoRow& row = m_entityInfos[index];
        char id[40]{};
        std::snprintf(id, sizeof(id), "ve.entity.%zu", index);
        char line[80]{};
        std::snprintf(line, sizeof(line), "%s  %s", row.id.c_str(), row.kind.c_str());
        const Rect rect = makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 13.0f);
        if (ui.button(ui::UiId::fromName(id), line, rect).pressed) {
            m_selectedEntity = static_cast<int>(index);
        }
        if (m_selectedEntity == static_cast<int>(index)) {
            ui.border(rect, theme.accent, 1.0f);
        }
        y += 15.0f;
    }
}

void VisualEditor::drawEntityProperties(ui::UiContext& ui, Rect props) {
    const ui::UiTheme& theme = ui.theme();
    float y = props.y + 16.0f;
    if (m_selectedEntity < 0 || m_selectedEntity >= static_cast<int>(m_entityInfos.size())) {
        ui.text("点击地图青色块或列表选择实体", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    const EntityInfoRow& row = m_entityInfos[static_cast<std::size_t>(m_selectedEntity)];
    char line[128]{};
    std::snprintf(line, sizeof(line), "%s  [%s]", row.id.c_str(), row.kind.c_str());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
    y += 12.0f;
    for (const auto& [key, value] : row.properties) {
        if (y + 9.0f > props.y + props.h - 14.0f) {
            ui.text("...", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
            break;
        }
        std::snprintf(line, sizeof(line), "%s: %s", key.c_str(), value.c_str());
        ui.text(fa::ui::fitToCodepoints(line, 34), makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        y += 11.0f;
    }
}

bool VisualEditor::isComponentDef(const tilemap::TileDef& def) noexcept {
    return def.tag == "decor" || def.tag == "obstacle" || def.renderHeight > 1;
}

int VisualEditor::decorLayerIndex() {
    tilemap::Tilemap& map = m_editor.map();
    int index = map.findLayer("decor");
    if (index < 0) {
        for (int i = 0; i < map.layerCount(); ++i) {
            const tilemap::TileLayer* layer = map.layerAt(i);
            if (layer != nullptr && layer->renderLayer == render::RenderLayer::GroundDetail) {
                index = i;
                break;
            }
        }
    }
    if (index >= 0) {
        return index;
    }
    // A map authored without a decor layer gets one on the first placement.
    int topOrder = 0;
    for (const tilemap::TileLayer& layer : map.layers()) {
        topOrder = std::max(topOrder, layer.zOrder);
    }
    tilemap::TileLayer decor;
    decor.name = "decor";
    decor.zOrder = topOrder + 1;
    decor.renderLayer = render::RenderLayer::GroundDetail;
    decor.tiles.assign(map.cellCount(), tilemap::TileId{});
    return map.addLayer(std::move(decor));
}

bool VisualEditor::isGrassGround(const tilemap::TileCoord cell) const {
    const tilemap::Tilemap& map = m_editor.map();
    int ground = map.findLayer("ground");
    if (ground < 0) {
        ground = 0;
    }
    const tilemap::TileDef* def = map.tileDef(map.tileAt(cell.x, cell.y, ground));
    return def != nullptr && (def->name == "grass" || def->tag == "grass");
}

void VisualEditor::applyDecorClick(const tilemap::TileCoord cell) {
    // Every component - tile decor and furniture alike - needs its ground type
    // underneath; the player world's only supported base is grass.
    if (!isGrassGround(cell)) {
        m_status = "只能放在草地上";
        return;
    }
    // Entity components (bed, chest, hazard) write placement rows so the real
    // interactive entity spawns in game; tile components paint the decor layer.
    if (m_activeComponentKind >= 0) {
        if (m_placements == nullptr || m_kinds.empty()) {
            m_status = "没有可放置的对象";
            return;
        }
        const std::string& kindName = m_componentKinds[static_cast<std::size_t>(m_activeComponentKind)].kind;
        for (std::size_t index = 0; index < m_kinds.size(); ++index) {
            if (m_kinds[index].kind == kindName) {
                m_activeKind = static_cast<int>(index);
                break;
            }
        }
        applyPlacementClick(cell);
        return;
    }
    if (!m_decorBrush.isValid()) {
        m_status = "先在组件面板选择";
        return;
    }
    if (!isGrassGround(cell)) {
        m_status = "只能放在草地上";
        return;
    }
    const int layer = decorLayerIndex();
    if (layer < 0) {
        m_status = "无法创建装饰图层";
        return;
    }
    const core::Status status = m_editor.paintAt(cell, m_decorBrush, layer);
    if (status.isOk()) {
        const tilemap::TileDef* def = m_editor.map().tileDef(m_decorBrush);
        m_status = def != nullptr ? "已放置 " + def->name : "已放置组件";
    } else {
        m_status = status.error().message;
    }
}

void VisualEditor::applyDecorErase(const tilemap::TileCoord cell) {
    // A placement row under the cursor is deleted first: entity components live in
    // the placement table, tile components in the decor layer.
    const int placement = placementIndexAt(cell);
    if (placement >= 0) {
        applyPlacementErase(cell);
        return;
    }
    tilemap::Tilemap& map = m_editor.map();
    int layer = map.findLayer("decor");
    if (layer < 0) {
        for (int i = 0; i < map.layerCount(); ++i) {
            const tilemap::TileLayer* candidate = map.layerAt(i);
            if (candidate != nullptr && candidate->renderLayer == render::RenderLayer::GroundDetail) {
                layer = i;
                break;
            }
        }
    }
    if (layer < 0 || !map.tileAt(cell.x, cell.y, layer).isValid()) {
        m_status = "这里没有组件";
        return;
    }
    const core::Status status = m_editor.paintAt(cell, tilemap::kEmptyTile, layer);
    m_status = status.isOk() ? "已清除组件" : status.error().message;
}

void VisualEditor::applyBlockerClick(const tilemap::TileCoord cell) {
    if (m_blockerStart.has_value()) {
        const tilemap::TileCoord from = *m_blockerStart;
        const tilemap::TileCoord to = cell;
        tilemap::MapBlocker blocker;
        blocker.bounds = tilemap::TileBounds{
            std::min(from.x, to.x), std::min(from.y, to.y), std::abs(from.x - to.x) + 1, std::abs(from.y - to.y) + 1};
        blocker.id = "zone_" + std::to_string(m_editor.map().blockers().size() + 1);
        // Default: the texture exactly covers the box. The inspector lifts it
        // higher; collision never leaves the box.
        blocker.renderHeight = blocker.bounds.height;
        m_editor.map().addBlocker(blocker);
        m_blockerStart.reset();
        m_selectedBlocker = static_cast<int>(m_editor.map().blockers().size()) - 1;
        m_history.markDirty();
        char line[64]{};
        std::snprintf(line, sizeof(line), "已创建碰撞区 %dx%d", blocker.bounds.width, blocker.bounds.height);
        m_status = line;
        return;
    }
    const int existing = blockerIndexAt(cell);
    if (existing >= 0) {
        m_selectedBlocker = existing;
        m_status = "已选中碰撞区";
        return;
    }
    m_blockerStart = cell;
    m_status = "再点一次确定对角";
}

int VisualEditor::blockerIndexAt(const tilemap::TileCoord cell) const noexcept {
    const std::span<const tilemap::MapBlocker> blockers = m_editor.map().blockers();
    for (std::size_t index = 0; index < blockers.size(); ++index) {
        if (blockers[index].bounds.contains(cell)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void VisualEditor::drawDecorPalette(ui::UiContext& ui, Rect palette) {
    const ui::UiTheme& theme = ui.theme();
    ui.panel(palette, theme.panel);
    // No "花草树" subtitle: the component list grows past those three.
    ui.text("组件", makeRect(palette.x + 6.0f, palette.y + 4.0f, 180.0f, 8.0f), labelStyle(theme));

    float y = palette.y + 16.0f;
    int shown = 0;
    for (const tilemap::TileDef& def : m_editor.map().tileDefs()) {
        if (!isComponentDef(def)) {
            continue;
        }
        if (y + 14.0f > palette.y + palette.h) {
            break;
        }
        char id[32]{};
        std::snprintf(id, sizeof(id), "ve.decor.%u", def.id.value);
        char line[64]{};
        std::snprintf(line, sizeof(line), "%s  #%u", def.name.c_str(), def.id.value);
        const Rect rect = makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 13.0f);
        if (ui.button(ui::UiId::fromName(id), line, rect).pressed) {
            m_decorBrush = def.id;
            m_activeComponentKind = -1;
            m_status = "组件 " + def.name;
        }
        if (def.id == m_decorBrush && m_activeComponentKind < 0) {
            ui.border(rect, theme.accent, 1.0f);
        }
        ++shown;
        y += 15.0f;
    }
    // Entity components: bed/chest place a spawn row, not a decor tile.
    for (std::size_t index = 0; index < m_componentKinds.size(); ++index) {
        if (y + 14.0f > palette.y + palette.h) {
            break;
        }
        char id[40]{};
        std::snprintf(id, sizeof(id), "ve.decor.kind.%zu", index);
        const Rect rect = makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 13.0f);
        if (ui.button(ui::UiId::fromName(id), m_componentKinds[index].label, rect).pressed) {
            m_activeComponentKind = static_cast<int>(index);
            m_status = "组件 " + m_componentKinds[index].label;
        }
        if (m_activeComponentKind == static_cast<int>(index)) {
            ui.border(rect, theme.accent, 1.0f);
        }
        ++shown;
        y += 15.0f;
    }
    if (shown == 0) {
        ui.text("无组件定义", makeRect(palette.x + 6.0f, y, palette.w - 12.0f, 8.0f), dimStyle(theme));
    }
}

void VisualEditor::drawDecorProperties(ui::UiContext& ui, Rect props) {
    const ui::UiTheme& theme = ui.theme();
    float y = props.y + 16.0f;
    char line[96]{};
    if (m_activeComponentKind >= 0) {
        const PlacementKind& kind = m_componentKinds[static_cast<std::size_t>(m_activeComponentKind)];
        std::snprintf(line, sizeof(line), "组件  %s", kind.label.c_str());
        ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
        y += 12.0f;
        ui.text("左键放置，右键删除", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        y += 11.0f;
        ui.text("需草地；游戏内可交互", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    const tilemap::TileDef* def = m_editor.map().tileDef(m_decorBrush);
    if (def == nullptr) {
        ui.text("在组件面板选择花/草/树", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    std::snprintf(line, sizeof(line), "组件 #%u  %s", def->id.value, def->name.c_str());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
    y += 12.0f;
    std::snprintf(line,
                  sizeof(line),
                  "%s  %s",
                  def->solid ? "阻挡" : "可通行",
                  def->renderHeight > 1 ? ("高" + std::to_string(def->renderHeight) + "格").c_str() : "1格高");
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 12.0f;
    ui.text("左键放置/右键清除", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 11.0f;
    ui.text("只叠在草地上，不替换瓦片", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
}

void VisualEditor::drawBlockerProperties(ui::UiContext& ui, Rect props, std::span<const std::string> textureIds) {
    const ui::UiTheme& theme = ui.theme();
    float y = props.y + 16.0f;
    char id[32]{};
    char line[96]{};
    const std::span<tilemap::MapBlocker> blockers = m_editor.map().mutableBlockers();
    if (m_selectedBlocker < 0 || m_selectedBlocker >= static_cast<int>(blockers.size())) {
        ui.text("选择或点两格新建", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
        return;
    }
    tilemap::MapBlocker& blocker = blockers[static_cast<std::size_t>(m_selectedBlocker)];
    std::snprintf(
        line, sizeof(line), "%s  判定 %dx%d 格", blocker.id.c_str(), blocker.bounds.width, blocker.bounds.height);
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), labelStyle(theme));
    y += 12.0f;

    // Texture picker, like the tile inspector: the map stores the name and the
    // app rebinds the GPU handle on save, so the editor preview updates live.
    std::snprintf(line, sizeof(line), "贴图 %s", blocker.textureName.empty() ? "-" : blocker.textureName.c_str());
    ui.text(line, makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 12.0f;
    if (!textureIds.empty()) {
        const float half = (props.w - 12.0f - 3.0f) / 2.0f;
        const float rowH = 13.0f;
        const auto cycle = [&](int direction) {
            int index = 0;
            for (std::size_t i = 0; i < textureIds.size(); ++i) {
                if (textureIds[i] == blocker.textureName) {
                    index = static_cast<int>(i);
                }
            }
            index = (index + static_cast<int>(textureIds.size()) + direction) % static_cast<int>(textureIds.size());
            blocker.textureName = textureIds[static_cast<std::size_t>(index)];
            m_selectedTexture = index;
            m_history.markDirty();
        };
        if (ui.button(ui::UiId::fromName("ve.blocker.tex.prev"), "<", makeRect(props.x + 6.0f, y, half, rowH))
                .pressed) {
            cycle(-1);
        }
        if (ui.button(
                  ui::UiId::fromName("ve.blocker.tex.next"), ">", makeRect(props.x + 6.0f + half + 3.0f, y, half, rowH))
                .pressed) {
            cycle(1);
        }
        y += rowH + 4.0f;
    }

    // Render height in tiles: the quad grows upward from the box's bottom edge.
    constexpr float small = 20.0f;
    std::snprintf(line, sizeof(line), "贴图高 %d 格", blocker.renderHeight);
    ui.text(line, makeRect(props.x + 6.0f, y, 90.0f, 8.0f), dimStyle(theme));
    if (ui.button(ui::UiId::fromName("ve.blocker.h-"), "-", makeRect(props.x + 100.0f, y, small, 11.0f)).pressed &&
        blocker.renderHeight > 1) {
        --blocker.renderHeight;
        m_history.markDirty();
    }
    if (ui.button(ui::UiId::fromName("ve.blocker.h+"), "+", makeRect(props.x + 122.0f, y, small, 11.0f)).pressed &&
        blocker.renderHeight < 256) {
        ++blocker.renderHeight;
        m_history.markDirty();
    }
    y += 14.0f;

    bool passable = blocker.passable;
    if (ui.checkbox(ui::UiId::fromName("ve.blocker.pass"),
                    "门/可进入",
                    passable,
                    makeRect(props.x + 6.0f, y, props.w - 12.0f, 13.0f))
            .pressed) {
        blocker.passable = passable;
        m_history.markDirty();
    }
    y += 14.0f;
    ui.text("判定只在框选区；传送门处可入", makeRect(props.x + 6.0f, y, props.w - 12.0f, 8.0f), dimStyle(theme));
    y += 12.0f;

    std::snprintf(id, sizeof(id), "ve.blocker.delete.%d", m_selectedBlocker);
    if (ui.button(ui::UiId::fromName(id), "删除", makeRect(props.x + 6.0f, y, props.w - 12.0f, 11.0f)).pressed) {
        m_editor.map().removeBlocker(static_cast<std::size_t>(m_selectedBlocker));
        m_selectedBlocker = -1;
        m_history.markDirty();
        m_status = "已删除碰撞区";
    }
}

}  // namespace fa::editor
