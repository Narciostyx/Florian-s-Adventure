#include "fa/tilemap/TilemapLoader.hpp"

#include "fa/tilemap/TilemapError.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fa::tilemap {

namespace {

using Json = nlohmann::json;

[[nodiscard]] core::Error jsonError(int code, std::string message) {
    return core::makeError(code, std::move(message));
}

[[nodiscard]] bool readIntValue(const Json& value, int& out) {
    if (value.is_number_unsigned()) {
        const unsigned long long raw = value.get<unsigned long long>();
        if (raw > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(raw);
        return true;
    }
    if (value.is_number_integer()) {
        const long long raw = value.get<long long>();
        if (raw < static_cast<long long>(std::numeric_limits<int>::min()) ||
            raw > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(raw);
        return true;
    }
    return false;
}

[[nodiscard]] bool readFloatValue(const Json& value, float& out) {
    if (!value.is_number()) {
        return false;
    }
    out = value.get<float>();
    return true;
}

[[nodiscard]] bool readStringValue(const Json& value, std::string& out) {
    if (!value.is_string()) {
        return false;
    }
    out = value.get<std::string>();
    return true;
}

[[nodiscard]] bool readBoolValue(const Json& value, bool& out) {
    if (!value.is_boolean()) {
        return false;
    }
    out = value.get<bool>();
    return true;
}

[[nodiscard]] bool readIntField(const Json& object, const char* key, int& out) {
    const auto found = object.find(key);
    return found != object.end() && readIntValue(*found, out);
}

[[nodiscard]] bool readFloatField(const Json& object, const char* key, float& out) {
    const auto found = object.find(key);
    return found != object.end() && readFloatValue(*found, out);
}

[[nodiscard]] bool readStringField(const Json& object, const char* key, std::string& out) {
    const auto found = object.find(key);
    return found != object.end() && readStringValue(*found, out);
}

[[nodiscard]] bool readBoolField(const Json& object, const char* key, bool& out) {
    const auto found = object.find(key);
    return found != object.end() && readBoolValue(*found, out);
}

[[nodiscard]] bool readCoordValue(const Json& value, TileCoord& out) {
    if (!value.is_array() || value.size() != 2) {
        return false;
    }
    int x = 0;
    int y = 0;
    if (!readIntValue(value[0], x) || !readIntValue(value[1], y)) {
        return false;
    }
    out = TileCoord{x, y};
    return true;
}

[[nodiscard]] bool readVec2Value(const Json& value, core::Vec2& out) {
    if (!value.is_array() || value.size() != 2) {
        return false;
    }
    float x = 0.0f;
    float y = 0.0f;
    if (!readFloatValue(value[0], x) || !readFloatValue(value[1], y)) {
        return false;
    }
    out = core::Vec2{x, y};
    return true;
}

[[nodiscard]] bool readBoundsValue(const Json& value, TileBounds& out) {
    if (!value.is_array() || value.size() != 4) {
        return false;
    }
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    if (!readIntValue(value[0], x) || !readIntValue(value[1], y) || !readIntValue(value[2], width) ||
        !readIntValue(value[3], height)) {
        return false;
    }
    out = TileBounds{x, y, width, height};
    return true;
}

[[nodiscard]] bool readSourceValue(const Json& value, core::Rect& out) {
    if (!value.is_array() || value.size() != 4) {
        return false;
    }
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (!readFloatValue(value[0], x) || !readFloatValue(value[1], y) || !readFloatValue(value[2], width) ||
        !readFloatValue(value[3], height)) {
        return false;
    }
    if (width < 0.0f || height < 0.0f) {
        return false;
    }
    out = core::makeRect(x, y, width, height);
    return true;
}

[[nodiscard]] bool readColorValue(const Json& value, core::Color& out) {
    if (!value.is_array() || value.size() != 4) {
        return false;
    }
    float channels[4]{};
    for (std::size_t index = 0; index < 4; ++index) {
        float channel = 0.0f;
        if (!readFloatValue(value[index], channel) || channel < 0.0f || channel > 1.0f) {
            return false;
        }
        channels[index] = channel;
    }
    out = core::Color{channels[0], channels[1], channels[2], channels[3]};
    return true;
}

[[nodiscard]] bool readRenderLayer(const Json& object, render::RenderLayer& out) {
    const auto found = object.find("render_layer");
    if (found == object.end()) {
        return true;
    }
    std::string name;
    if (!readStringValue(*found, name)) {
        return false;
    }
    if (name == "background") {
        out = render::RenderLayer::Background;
    } else if (name == "ground") {
        out = render::RenderLayer::Ground;
    } else if (name == "ground_detail" || name == "groundDetail") {
        out = render::RenderLayer::GroundDetail;
    } else if (name == "objects") {
        out = render::RenderLayer::Objects;
    } else if (name == "actors") {
        out = render::RenderLayer::Actors;
    } else if (name == "foreground") {
        out = render::RenderLayer::Foreground;
    } else if (name == "weather") {
        out = render::RenderLayer::Weather;
    } else if (name == "ui") {
        out = render::RenderLayer::UI;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] bool readAreaKind(const Json& object, AreaKind& out) {
    std::string name;
    if (!readStringField(object, "kind", name)) {
        return false;
    }
    if (name == "outdoor") {
        out = AreaKind::Outdoor;
        return true;
    }
    if (name == "greenhouse") {
        out = AreaKind::Greenhouse;
        return true;
    }
    return false;
}

[[nodiscard]] bool boundsFit(const TileBounds& bounds, int width, int height) {
    if (bounds.isEmpty()) {
        return false;
    }
    const long long right = static_cast<long long>(bounds.x) + static_cast<long long>(bounds.width);
    const long long bottom = static_cast<long long>(bounds.y) + static_cast<long long>(bounds.height);
    return bounds.x >= 0 && bounds.y >= 0 && right <= static_cast<long long>(width) &&
           bottom <= static_cast<long long>(height);
}

}  // namespace

core::Result<Tilemap> loadTilemapFromJson(std::string_view json) {
    const Json document = Json::parse(json.begin(), json.end(), nullptr, false);
    if (document.is_discarded()) {
        return core::Result<Tilemap>::err(jsonError(tilemap_error::kParseFailed, "map document is not valid JSON"));
    }
    if (!document.is_object()) {
        return core::Result<Tilemap>::err(
            jsonError(tilemap_error::kInvalidDocument, "map document root must be an object"));
    }

    int schemaVersion = 0;
    if (!readIntField(document, "schema_version", schemaVersion)) {
        return core::Result<Tilemap>::err(
            jsonError(tilemap_error::kInvalidDocument, "map document needs an integer \"schema_version\""));
    }
    if (schemaVersion != 1) {
        return core::Result<Tilemap>::err(jsonError(tilemap_error::kUnsupportedSchema,
                                                    "unsupported map schema version " + std::to_string(schemaVersion)));
    }

    int width = 0;
    int height = 0;
    if (!readIntField(document, "width", width) || !readIntField(document, "height", height)) {
        return core::Result<Tilemap>::err(
            jsonError(tilemap_error::kInvalidDimensions, "map document needs integer \"width\" and \"height\""));
    }

    int tileSize = render::kTileSize;
    if (document.contains("tile_size") && !readIntField(document, "tile_size", tileSize)) {
        return core::Result<Tilemap>::err(
            jsonError(tilemap_error::kInvalidTileSize, "\"tile_size\" must be a positive integer"));
    }

    Tilemap map;
    if (const core::Status reset = map.reset(width, height, tileSize); reset.isError()) {
        return core::Result<Tilemap>::err(reset.error());
    }

    if (const auto origin = document.find("origin"); origin != document.end()) {
        core::Vec2 originPixels{};
        if (!readVec2Value(*origin, originPixels)) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidDocument, "\"origin\" must be a two-number array"));
        }
        map.setOrigin(originPixels);
    }

    bool outsideSolid = true;
    if (document.contains("outside_solid") && !readBoolField(document, "outside_solid", outsideSolid)) {
        return core::Result<Tilemap>::err(
            jsonError(tilemap_error::kInvalidDocument, "\"outside_solid\" must be a boolean"));
    }
    map.setOutsideIsSolid(outsideSolid);

    if (const auto tiles = document.find("tiles"); tiles != document.end()) {
        if (!tiles->is_array()) {
            return core::Result<Tilemap>::err(jsonError(tilemap_error::kInvalidDocument, "\"tiles\" must be an array"));
        }
        for (const Json& entry : *tiles) {
            if (!entry.is_object()) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "every tile definition must be an object"));
            }
            int id = 0;
            if (!readIntField(entry, "id", id) || id <= 0 || id > static_cast<int>(kMaxTileId)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile definition needs an \"id\" in 1..65535"));
            }

            TileDef def;
            def.id = TileId{static_cast<std::uint16_t>(id)};
            if (entry.contains("name") && !readStringField(entry, "name", def.name)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile \"name\" must be a string"));
            }
            if (entry.contains("texture") && !readStringField(entry, "texture", def.textureName)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile \"texture\" must be a string"));
            }
            if (entry.contains("tag") && !readStringField(entry, "tag", def.tag)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile \"tag\" must be a string"));
            }
            if (entry.contains("source")) {
                if (!readSourceValue(*entry.find("source"), def.source)) {
                    return core::Result<Tilemap>::err(
                        jsonError(tilemap_error::kInvalidDocument, "tile \"source\" must be a four-number array"));
                }
            }
            if (entry.contains("walkable") && !readBoolField(entry, "walkable", def.walkable)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile \"walkable\" must be a boolean"));
            }
            if (entry.contains("solid") && !readBoolField(entry, "solid", def.solid)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile \"solid\" must be a boolean"));
            }
            if (entry.contains("water") && !readBoolField(entry, "water", def.water)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile \"water\" must be a boolean"));
            }
            if (entry.contains("tillable") && !readBoolField(entry, "tillable", def.tillable)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidDocument, "tile \"tillable\" must be a boolean"));
            }
            if (!map.addTileDef(std::move(def))) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kDuplicateTileId, "duplicate tile id " + std::to_string(id)));
            }
        }
    }

    const auto layers = document.find("layers");
    if (layers == document.end() || !layers->is_array() || layers->empty()) {
        return core::Result<Tilemap>::err(
            jsonError(tilemap_error::kInvalidDocument, "map document needs a non-empty \"layers\" array"));
    }

    const std::size_t expectedCells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (const Json& entry : *layers) {
        if (!entry.is_object()) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidDocument, "every layer must be an object"));
        }

        TileLayer layer;
        if (!readStringField(entry, "name", layer.name) || layer.name.empty()) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidLayer, "layer needs a non-empty \"name\""));
        }
        if (entry.contains("z_order") && !readIntField(entry, "z_order", layer.zOrder)) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidLayer, "layer \"z_order\" must be an integer"));
        }
        if (!readRenderLayer(entry, layer.renderLayer)) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidLayer, "layer has an unknown \"render_layer\""));
        }
        if (entry.contains("visible") && !readBoolField(entry, "visible", layer.visible)) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidLayer, "layer \"visible\" must be a boolean"));
        }

        const auto tiles = entry.find("tiles");
        if (tiles == entry.end() || !tiles->is_array()) {
            return core::Result<Tilemap>::err(jsonError(tilemap_error::kInvalidLayer, "layer needs a \"tiles\" array"));
        }
        if (tiles->size() != expectedCells) {
            return core::Result<Tilemap>::err(jsonError(tilemap_error::kLayerSizeMismatch,
                                                        "layer \"" + layer.name + "\" has " +
                                                            std::to_string(tiles->size()) + " tiles, expected " +
                                                            std::to_string(expectedCells)));
        }

        layer.tiles.reserve(expectedCells);
        for (const Json& value : *tiles) {
            int id = 0;
            if (!readIntValue(value, id) || id < 0 || id > static_cast<int>(kMaxTileId)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kUnknownTileId, "layer \"" + layer.name + "\" has an invalid tile id"));
            }
            const TileId tile{static_cast<std::uint16_t>(id)};
            if (tile.isValid() && map.tileDef(tile) == nullptr) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kUnknownTileId,
                              "layer \"" + layer.name + "\" uses undeclared tile id " + std::to_string(id)));
            }
            layer.tiles.push_back(tile);
        }

        if (map.addLayer(std::move(layer)) < 0) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidLayer, "layer is duplicated or has an unusable render layer"));
        }
    }

    if (const auto areas = document.find("areas"); areas != document.end()) {
        if (!areas->is_array()) {
            return core::Result<Tilemap>::err(jsonError(tilemap_error::kInvalidRegion, "\"areas\" must be an array"));
        }
        std::vector<std::uint32_t> areaIds;
        for (const Json& entry : *areas) {
            if (!entry.is_object()) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "every area must be an object"));
            }
            int areaId = 0;
            if (!readIntField(entry, "id", areaId) || areaId <= 0) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "area needs a positive integer \"id\""));
            }
            MapArea area;
            area.areaId = static_cast<std::uint32_t>(areaId);
            if (std::find(areaIds.begin(), areaIds.end(), area.areaId) != areaIds.end()) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "duplicate area id " + std::to_string(areaId)));
            }
            if (!readAreaKind(entry, area.kind)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "area has an unknown \"kind\" (outdoor or greenhouse)"));
            }
            const auto bounds = entry.find("bounds");
            if (bounds == entry.end() || !readBoundsValue(*bounds, area.bounds) ||
                !boundsFit(area.bounds, width, height)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "area \"bounds\" must be a non-empty in-map rectangle"));
            }
            areaIds.push_back(area.areaId);
            map.addArea(std::move(area));
        }
    }

    if (const auto portals = document.find("portals"); portals != document.end()) {
        if (!portals->is_array()) {
            return core::Result<Tilemap>::err(jsonError(tilemap_error::kInvalidRegion, "\"portals\" must be an array"));
        }
        for (const Json& entry : *portals) {
            if (!entry.is_object()) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "every portal must be an object"));
            }
            MapPortal portal;
            if (!readStringField(entry, "id", portal.id) || portal.id.empty()) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "portal needs a non-empty \"id\""));
            }
            const auto bounds = entry.find("bounds");
            if (bounds == entry.end() || !readBoundsValue(*bounds, portal.bounds) ||
                !boundsFit(portal.bounds, width, height)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "portal \"bounds\" must be a non-empty in-map rectangle"));
            }
            if (!readStringField(entry, "target_map", portal.targetMap) || portal.targetMap.empty()) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "portal needs a non-empty \"target_map\""));
            }
            const auto targetTile = entry.find("target_tile");
            if (targetTile == entry.end() || !readCoordValue(*targetTile, portal.targetTile)) {
                return core::Result<Tilemap>::err(
                    jsonError(tilemap_error::kInvalidRegion, "portal needs a two-integer \"target_tile\""));
            }
            map.addPortal(std::move(portal));
        }
    }

    if (const auto ambient = document.find("ambient"); ambient != document.end()) {
        if (!ambient->is_object()) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidDocument, "\"ambient\" must be an object"));
        }
        MapAmbient values;
        if (ambient->contains("indoor") && !readBoolField(*ambient, "indoor", values.indoor)) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidDocument, "\"indoor\" must be a boolean"));
        }
        if (const auto light = ambient->find("light");
            light != ambient->end() && !readColorValue(*light, values.light)) {
            return core::Result<Tilemap>::err(
                jsonError(tilemap_error::kInvalidDocument, "\"light\" must be four numbers in 0..1"));
        }
        if (ambient->contains("music") && !readStringField(*ambient, "music", values.music)) {
            return core::Result<Tilemap>::err(jsonError(tilemap_error::kInvalidDocument, "\"music\" must be a string"));
        }
        map.setAmbient(std::move(values));
    }

    return core::Result<Tilemap>::ok(std::move(map));
}

}  // namespace fa::tilemap
