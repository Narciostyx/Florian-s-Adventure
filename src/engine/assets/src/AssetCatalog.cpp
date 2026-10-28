#include <fa/assets/AssetCatalog.hpp>

#include <cstddef>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fa/assets/AssetError.hpp>
#include <fa/platform/FileSystem.hpp>

namespace fa::assets {

namespace {

using Json = nlohmann::json;

core::Status parseFailure(std::size_t index, std::string_view reason) {
    std::string message = "catalog entry ";
    message += std::to_string(index);
    message += ": ";
    message += reason;
    return core::errorStatus(asset_error::kCatalogParseFailed, std::move(message));
}

core::Status readColor(const Json& object, const char* key, core::Color& out, std::size_t index) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return core::okStatus();
    }
    if (!it->is_string()) {
        return parseFailure(index, std::string("\"") + key + "\" must be a string");
    }
    core::Result<core::Color> parsed = parseColorText(it->get<std::string>());
    if (parsed.isError()) {
        return parseFailure(index, std::string("\"") + key + "\" " + parsed.error().message);
    }
    out = parsed.value();
    return core::okStatus();
}

}  // namespace

core::Status AssetCatalog::loadFromText(std::string_view json) {
    // The non-throwing overload keeps the whole module free of try/catch; a malformed
    // document arrives as `discarded` instead of an exception.
    const Json document = Json::parse(json.begin(), json.end(), nullptr, false);
    if (document.is_discarded()) {
        return core::errorStatus(asset_error::kCatalogParseFailed, "catalog is not valid JSON");
    }
    if (!document.is_object()) {
        return core::errorStatus(asset_error::kCatalogParseFailed, "catalog root must be an object");
    }
    const auto assets = document.find("assets");
    if (assets == document.end() || !assets->is_array()) {
        return core::errorStatus(asset_error::kCatalogParseFailed, "catalog needs an \"assets\" array");
    }

    std::vector<AssetEntry> parsed;
    parsed.reserve(assets->size());

    for (std::size_t index = 0; index < assets->size(); ++index) {
        const Json& item = (*assets)[index];
        if (!item.is_object()) {
            return parseFailure(index, "entry must be an object");
        }

        AssetEntry entry;

        const auto idIt = item.find("id");
        if (idIt == item.end() || !idIt->is_string() || idIt->get<std::string>().empty()) {
            return parseFailure(index, "missing or empty \"id\"");
        }
        entry.id = AssetId::fromName(idIt->get<std::string>());

        const auto kindIt = item.find("kind");
        if (kindIt != item.end()) {
            if (!kindIt->is_string()) {
                return parseFailure(index, "\"kind\" must be a string");
            }
            const std::string kind = kindIt->get<std::string>();
            if (kind == "texture") {
                entry.kind = AssetKind::Texture;
            } else if (kind == "json" || kind == "jsonText") {
                entry.kind = AssetKind::JsonText;
            } else {
                return parseFailure(index, "unknown \"kind\" " + kind);
            }
        }

        const auto pathIt = item.find("path");
        if (pathIt != item.end()) {
            if (!pathIt->is_string()) {
                return parseFailure(index, "\"path\" must be a string");
            }
            entry.path = pathIt->get<std::string>();
        }

        const auto sizeIt = item.find("size");
        if (sizeIt != item.end()) {
            if (!sizeIt->is_array() || sizeIt->size() != 2) {
                return parseFailure(index, "\"size\" must be [width, height]");
            }
            const Json& width = (*sizeIt)[0];
            const Json& height = (*sizeIt)[1];
            if (!width.is_number_integer() || !height.is_number_integer()) {
                return parseFailure(index, "\"size\" entries must be integers");
            }
            entry.width = width.get<int>();
            entry.height = height.get<int>();
            if (entry.width < 0 || entry.height < 0) {
                return parseFailure(index, "\"size\" must not be negative");
            }
        }

        const auto tileIt = item.find("tile");
        if (tileIt != item.end()) {
            if (!tileIt->is_number_integer()) {
                return parseFailure(index, "\"tile\" must be an integer");
            }
            entry.tileSize = tileIt->get<int>();
        }

        // A flat block is the default look, so the second colour starts equal.
        entry.color = core::Color{1.0f, 0.0f, 1.0f, 1.0f};
        entry.altColor = entry.color;

        core::Status status = readColor(item, "color", entry.color, index);
        if (status.isError()) {
            return status;
        }
        entry.altColor = entry.color;
        status = readColor(item, "altColor", entry.altColor, index);
        if (status.isError()) {
            return status;
        }

        // Without a file the placeholder is the only content, so its size cannot be
        // guessed; with a file the decoder decides and the size stays optional.
        if (entry.kind == AssetKind::Texture && entry.path.empty() && (entry.width <= 0 || entry.height <= 0)) {
            return parseFailure(index, "a path-less texture needs a positive \"size\"");
        }
        if (entry.kind == AssetKind::JsonText && entry.path.empty()) {
            return parseFailure(index, "a json asset needs a \"path\"");
        }

        for (const AssetEntry& existing : parsed) {
            if (existing.id == entry.id) {
                return parseFailure(index, "duplicate id " + entry.id.name());
            }
        }

        parsed.push_back(std::move(entry));
    }

    m_entries = std::move(parsed);
    return core::okStatus();
}

core::Status AssetCatalog::loadFromFile(platform::IFileSystem& fileSystem, std::string_view path) {
    core::Result<std::string> text = fileSystem.readText(path);
    if (text.isError()) {
        return core::errorStatus(asset_error::kLoadFailed,
                                 "cannot read catalog " + std::string(path) + ": " + text.error().message);
    }
    return loadFromText(text.value());
}

void AssetCatalog::clear() {
    m_entries.clear();
}

const AssetEntry* AssetCatalog::find(const AssetId& id) const noexcept {
    for (const AssetEntry& entry : m_entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const AssetEntry* AssetCatalog::find(std::string_view name) const noexcept {
    for (const AssetEntry& entry : m_entries) {
        if (entry.id.name() == name) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace fa::assets
