#include "UiFontAtlas.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fa/assets/AssetError.hpp>
#include <fa/assets/ImageData.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/PlatformError.hpp>

#include "StbImageDecoder.hpp"

namespace fa::app {
namespace {

constexpr std::size_t kBytesPerPixel = 4;
constexpr int kSupportedVersion = 1;

[[nodiscard]] core::Result<ui::FontAtlas> fail(const int code, std::string message) {
    return core::Result<ui::FontAtlas>::err(code, std::move(message));
}

[[nodiscard]] std::string join(std::string_view root, std::string_view leaf) {
    std::string path(root);
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path.push_back('/');
    }
    path.append(leaf);
    return path;
}

[[nodiscard]] bool readRect(const nlohmann::json& entry, const char* key, core::Rect& out) {
    if (!entry.contains(key) || !entry.at(key).is_array() || entry.at(key).size() != 4) {
        return false;
    }
    const auto& box = entry.at(key);
    out = core::makeRect(box[0].get<float>(), box[1].get<float>(), box[2].get<float>(), box[3].get<float>());
    return true;
}

}  // namespace

core::Result<ui::FontAtlas> loadBakedUiFont(platform::IFileSystem& fileSystem, const std::string_view assetsRoot) {
    const std::string jsonPath = join(assetsRoot, "fonts/ui_atlas.json");
    const std::string pngPath = join(assetsRoot, "fonts/ui_atlas.png");

    core::Result<std::string> jsonText = fileSystem.readText(jsonPath);
    if (jsonText.isError()) {
        return fail(platform::platform_error::kFileOpenFailed, "cannot read " + jsonPath);
    }
    core::Result<std::string> pngBytes = fileSystem.readBinary(pngPath);
    if (pngBytes.isError()) {
        return fail(platform::platform_error::kFileOpenFailed, "cannot read " + pngPath);
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(jsonText.value());
    } catch (const nlohmann::json::exception& error) {
        return fail(assets::asset_error::kCatalogParseFailed, "ui_atlas.json: " + std::string(error.what()));
    }
    if (!doc.is_object() || !doc.contains("glyphs") || !doc.at("glyphs").is_array()) {
        return fail(assets::asset_error::kCatalogParseFailed, "ui_atlas.json has no glyph array");
    }
    if (doc.value("version", 0) != kSupportedVersion) {
        return fail(assets::asset_error::kCatalogParseFailed, "ui_atlas.json: unsupported version");
    }

    const std::string raw = std::move(pngBytes).take();
    std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    StbImageDecoder decoder;
    core::Result<assets::ImageData> decoded = decoder.decode(pngPath, bytes);
    if (decoded.isError()) {
        return fail(assets::asset_error::kDecodeFailed, "ui_atlas.png: " + decoded.error().message);
    }
    assets::ImageData image = std::move(decoded).take();

    ui::FontAtlas font;
    font.width = doc.value("width", 0);
    font.height = doc.value("height", 0);
    font.lineHeight = doc.value("line_height", 0.0f);
    if (font.width <= 0 || font.height <= 0 || font.lineHeight <= 0.0f) {
        return fail(assets::asset_error::kCatalogParseFailed, "ui_atlas.json: bad atlas dimensions");
    }
    if (image.width != font.width || image.height != font.height) {
        return fail(assets::asset_error::kInvalidImage, "ui_atlas.png size does not match ui_atlas.json");
    }
    if (image.format != assets::ImageFormat::Rgba8 || image.pixels.size() != static_cast<std::size_t>(font.width) *
                                                                                 static_cast<std::size_t>(font.height) *
                                                                                 kBytesPerPixel) {
        return fail(assets::asset_error::kInvalidImage, "ui_atlas.png is not tightly packed RGBA8");
    }
    font.pixels = std::move(image.pixels);

    // ASCII stays in the dense block so glyphFor() keeps its old indexing; everything
    // else goes to the sorted table that the binary search in glyphFor() reads.
    font.glyphs.resize(static_cast<std::size_t>(ui::kGlyphCount));
    std::vector<ui::ExtendedGlyph> extended;
    int widestAdvance = 0;
    int tallest = 0;
    for (const auto& entry : doc.at("glyphs")) {
        if (!entry.is_object() || !entry.contains("cp")) {
            return fail(assets::asset_error::kCatalogParseFailed, "ui_atlas.json: glyph entry is malformed");
        }
        core::Rect source{};
        if (!readRect(entry, "src", source)) {
            return fail(assets::asset_error::kCatalogParseFailed, "ui_atlas.json: glyph src is malformed");
        }

        ui::Glyph glyph;
        glyph.source = source;
        glyph.advance = entry.value("advance", 0.0f);
        glyph.bearingY = entry.value("bearing_y", 0.0f);
        // Every listed codepoint is "defined": a space carries no ink but still carries
        // an advance, and must not fall through to the '?' bitmap.
        glyph.defined = true;

        const auto codepoint = static_cast<char32_t>(entry.at("cp").get<std::uint32_t>());
        if (codepoint >= ui::kFirstCodepoint && codepoint <= ui::kLastCodepoint) {
            font.glyphs[static_cast<std::size_t>(codepoint - ui::kFirstCodepoint)] = glyph;
        } else {
            extended.push_back(ui::ExtendedGlyph{codepoint, glyph});
        }
        widestAdvance = std::max(widestAdvance, static_cast<int>(glyph.advance));
        tallest = std::max(tallest, static_cast<int>(source.h));
    }

    std::sort(extended.begin(), extended.end(), [](const ui::ExtendedGlyph& lhs, const ui::ExtendedGlyph& rhs) {
        return lhs.codepoint < rhs.codepoint;
    });
    font.extended = std::move(extended);
    font.glyphWidth = widestAdvance;
    font.glyphHeight = tallest;
    return core::Result<ui::FontAtlas>::ok(std::move(font));
}

}  // namespace fa::app
