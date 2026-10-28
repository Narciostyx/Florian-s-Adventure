#include "fa/tilemap/TilemapWriter.hpp"

#include <nlohmann/json.hpp>

#include "fa/render/RenderTypes.hpp"

namespace fa::tilemap {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string_view renderLayerName(render::RenderLayer layer) noexcept {
    switch (layer) {
        case render::RenderLayer::Background:
            return "background";
        case render::RenderLayer::Ground:
            return "ground";
        case render::RenderLayer::GroundDetail:
            return "ground_detail";
        case render::RenderLayer::Objects:
            return "objects";
        case render::RenderLayer::Actors:
            return "actors";
        case render::RenderLayer::Foreground:
            return "foreground";
        case render::RenderLayer::Weather:
            return "weather";
        case render::RenderLayer::UI:
            return "ui";
        default:
            return "ground";
    }
}

[[nodiscard]] Json boundsJson(const TileBounds& bounds) {
    return Json::array({bounds.x, bounds.y, bounds.width, bounds.height});
}

}  // namespace

std::string saveTilemapToJson(const Tilemap& map) {
    Json document;
    document["schema_version"] = 1;
    document["width"] = map.width();
    document["height"] = map.height();
    document["tile_size"] = map.tileSize();
    document["origin"] = Json::array({map.origin().x, map.origin().y});
    document["outside_solid"] = map.outsideIsSolid();

    Json tiles = Json::array();
    for (const TileDef& def : map.tileDefs()) {
        Json entry;
        entry["id"] = def.id.value;
        entry["name"] = def.name;
        entry["texture"] = def.textureName;
        entry["source"] = Json::array({def.source.x, def.source.y, def.source.w, def.source.h});
        entry["walkable"] = def.walkable;
        entry["solid"] = def.solid;
        entry["water"] = def.water;
        entry["tillable"] = def.tillable;
        entry["tag"] = def.tag;
        tiles.push_back(std::move(entry));
    }
    document["tiles"] = std::move(tiles);

    Json layers = Json::array();
    for (const TileLayer& layer : map.layers()) {
        Json entry;
        entry["name"] = layer.name;
        entry["z_order"] = layer.zOrder;
        entry["render_layer"] = renderLayerName(layer.renderLayer);
        entry["visible"] = layer.visible;

        Json cells = Json::array();
        for (const TileId tile : layer.tiles) {
            cells.push_back(tile.value);
        }
        entry["tiles"] = std::move(cells);
        layers.push_back(std::move(entry));
    }
    document["layers"] = std::move(layers);

    Json areas = Json::array();
    for (const MapArea& area : map.areas()) {
        Json entry;
        entry["id"] = area.areaId;
        entry["kind"] = toString(area.kind);
        entry["bounds"] = boundsJson(area.bounds);
        areas.push_back(std::move(entry));
    }
    document["areas"] = std::move(areas);

    Json portals = Json::array();
    for (const MapPortal& portal : map.portals()) {
        Json entry;
        entry["id"] = portal.id;
        entry["bounds"] = boundsJson(portal.bounds);
        entry["target_map"] = portal.targetMap;
        entry["target_tile"] = Json::array({portal.targetTile.x, portal.targetTile.y});
        portals.push_back(std::move(entry));
    }
    document["portals"] = std::move(portals);

    const MapAmbient& ambient = map.ambient();
    document["ambient"] =
        Json{{"indoor", ambient.indoor},
             {"light", Json::array({ambient.light.r, ambient.light.g, ambient.light.b, ambient.light.a})},
             {"music", ambient.music}};

    return document.dump(2);
}

}  // namespace fa::tilemap
