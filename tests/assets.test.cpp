/// \file
/// \brief Tests for M05: asset ids, the placeholder image builders, catalog
/// parsing and the cache in front of them.
///
/// Every texture in v1 is a coloured block, so almost all of what matters can be
/// checked without a GPU: a fake uploader stands in for the renderer, and a fake
/// decoder stands in for the PNG reader that will arrive with real art.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fa/assets/AssetCatalog.hpp>
#include <fa/assets/AssetError.hpp>
#include <fa/assets/AssetHandle.hpp>
#include <fa/assets/AssetId.hpp>
#include <fa/assets/AssetManager.hpp>
#include <fa/assets/AssetTypes.hpp>
#include <fa/assets/IImageDecoder.hpp>
#include <fa/assets/ITextureUploader.hpp>
#include <fa/assets/ImageData.hpp>
#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/events/JobSystem.hpp>
#include <fa/platform/FileSystem.hpp>

namespace {

using fa::assets::AssetCatalog;
using fa::assets::AssetEntry;
using fa::assets::AssetHandle;
using fa::assets::AssetId;
using fa::assets::AssetKind;
using fa::assets::AssetManagerDesc;
using fa::assets::AssetState;
namespace asset_error = fa::assets::asset_error;
using fa::assets::ImageData;
using fa::assets::ImageFormat;
using fa::assets::ITextureUploader;
using fa::assets::makeCheckerboardImage;
using fa::assets::makeSolidImage;
using fa::assets::parseColorText;
using fa::assets::TextureAsset;
using fa::assets::toColorText;
using fa::core::Color;

constexpr float kEpsilon = 1.0e-5f;

/// glm::vec4 operator== returns a bvec4, so colour equality is spelled out.
bool sameColor(const Color& lhs, const Color& rhs) {
    return std::abs(lhs.r - rhs.r) < kEpsilon && std::abs(lhs.g - rhs.g) < kEpsilon &&
           std::abs(lhs.b - rhs.b) < kEpsilon && std::abs(lhs.a - rhs.a) < kEpsilon;
}

/// In-memory IFileSystem: the cache only ever reads, so a map of path to bytes is
/// enough and keeps the tests off the disk.
class FakeFileSystem final : public fa::platform::IFileSystem {
public:
    bool exists(std::string_view path) const override { return m_files.count(std::string(path)) != 0; }

    bool isDirectory(std::string_view path) const override { return m_directories.count(std::string(path)) != 0; }

    fa::core::Result<std::string> readText(std::string_view path) override { return readBinary(path); }

    fa::core::Result<std::string> readBinary(std::string_view path) override {
        const auto it = m_files.find(std::string(path));
        if (it == m_files.end()) {
            return fa::core::Result<std::string>::err(2001, "no such file: " + std::string(path));
        }
        return fa::core::Result<std::string>::ok(it->second);
    }

    fa::core::Status writeText(std::string_view path, std::string_view content) override {
        m_files[std::string(path)] = std::string(content);
        return fa::core::okStatus();
    }

    fa::core::Status writeTextAtomic(std::string_view path, std::string_view content) override {
        return writeText(path, content);
    }

    fa::core::Status createDirectories(std::string_view path) override {
        m_directories.insert(std::string(path));
        return fa::core::okStatus();
    }

    fa::core::Result<std::vector<std::string>> listDirectory(std::string_view path) override {
        static_cast<void>(path);
        return fa::core::Result<std::vector<std::string>>::ok({});
    }

    fa::core::Status remove(std::string_view path) override {
        m_files.erase(std::string(path));
        return fa::core::okStatus();
    }

    void put(std::string path, std::string content) { m_files[std::move(path)] = std::move(content); }

private:
    std::map<std::string, std::string> m_files;
    std::set<std::string> m_directories;
};

/// Records what the cache asked the GPU to do and hands out sequential tokens.
class FakeUploader final : public ITextureUploader {
public:
    std::uint32_t upload(const ImageData& image, std::string_view debugName) override {
        ++uploadCount;
        lastWidth = image.width;
        lastHeight = image.height;
        lastDebugName = std::string(debugName);
        if (rejectAll) {
            return 0;
        }
        const std::uint32_t token = nextToken++;
        liveTokens.push_back(token);
        return token;
    }

    void release(std::uint32_t token) override {
        if (token == 0) {
            return;
        }
        ++releaseCount;
        for (auto it = liveTokens.begin(); it != liveTokens.end(); ++it) {
            if (*it == token) {
                liveTokens.erase(it);
                return;
            }
        }
    }

    bool rejectAll = false;
    int uploadCount = 0;
    int releaseCount = 0;
    int lastWidth = 0;
    int lastHeight = 0;
    std::string lastDebugName;
    std::vector<std::uint32_t> liveTokens;

private:
    std::uint32_t nextToken = 1;
};

/// Stand-in for a real image format: the file body is `WxH` and decodes to a white
/// block of that size.
class FakeDecoder final : public fa::assets::IImageDecoder {
public:
    bool supports(std::string_view path) const override {
        return path.size() >= 5 && path.substr(path.size() - 5) == ".fake";
    }

    fa::core::Result<ImageData> decode(std::string_view path, std::span<const std::uint8_t> bytes) override {
        if (bytes.empty()) {
            return fa::core::Result<ImageData>::err(3003, "empty source: " + std::string(path));
        }
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const std::size_t separator = text.find('x');
        if (separator == std::string::npos) {
            return fa::core::Result<ImageData>::err(3003, "not a fake image: " + std::string(path));
        }
        const int width = std::stoi(text.substr(0, separator));
        const int height = std::stoi(text.substr(separator + 1));
        if (width <= 0 || height <= 0) {
            return fa::core::Result<ImageData>::err(3003, "bad fake size: " + text);
        }
        return fa::core::Result<ImageData>::ok(makeSolidImage(width, height, Color{1.0f, 1.0f, 1.0f, 1.0f}));
    }
};

constexpr std::string_view kCatalogJson = R"json({
  "assets": [
    { "id": "tex.grass", "size": [16, 16], "color": "#4a7c3f" },
    { "id": "tex.soil", "size": [16, 16], "color": "#6b4a2f", "altColor": "#5a3d26", "tile": 8 },
    { "id": "tex.rock", "size": [8, 8], "color": "#8a8a8a" },
    { "id": "data.crops", "kind": "json", "path": "data/crops.json" }
  ]
})json";

AssetCatalog makeCatalog() {
    AssetCatalog catalog;
    const fa::core::Status status = catalog.loadFromText(kCatalogJson);
    REQUIRE(status.isOk());
    return catalog;
}

/// Wires a manager over the fakes with an optional job system.
struct Fixture {
    FakeFileSystem fileSystem;
    FakeUploader uploader;
    FakeDecoder decoder;

    fa::assets::IAssetManager* create(AssetCatalog& catalog,
                                      fa::events::JobSystem* jobs = nullptr,
                                      bool retainCpuPixels = true,
                                      bool withUploader = true) {
        AssetManagerDesc desc = baseDesc(catalog);
        desc.jobs = jobs;
        desc.uploader = withUploader ? &uploader : nullptr;
        desc.retainCpuPixels = retainCpuPixels;
        m_manager = fa::assets::createAssetManager(desc);
        REQUIRE(m_manager != nullptr);
        return m_manager.get();
    }

    /// A fully wired description, for tests that need to tweak one field.
    AssetManagerDesc baseDesc(AssetCatalog& catalog) {
        m_decoders = {&decoder};
        AssetManagerDesc desc;
        desc.fileSystem = &fileSystem;
        desc.catalog = &catalog;
        desc.uploader = &uploader;
        desc.decoders = m_decoders;
        desc.logEvents = false;
        desc.retainCpuPixels = true;
        return desc;
    }

private:
    std::vector<fa::assets::IImageDecoder*> m_decoders;
    std::unique_ptr<fa::assets::IAssetManager> m_manager;
};

}  // namespace

TEST_CASE("AssetId hashes the same name the same way", "[assets]") {
    const AssetId first = AssetId::fromName("tex.grass");
    const AssetId second = AssetId::fromName("tex.grass");
    REQUIRE(first == second);
    REQUIRE(first.hash() == second.hash());
    REQUIRE(first.isValid());
    REQUIRE(first.name() == "tex.grass");
}

TEST_CASE("AssetId separates different names", "[assets]") {
    const AssetId grass = AssetId::fromName("tex.grass");
    const AssetId rock = AssetId::fromName("tex.rock");
    REQUIRE(grass != rock);
    REQUIRE(grass.hash() != rock.hash());
}

TEST_CASE("AssetId is invalid when unnamed", "[assets]") {
    const AssetId empty;
    REQUIRE_FALSE(empty.isValid());
    REQUIRE(empty.name().empty());
    REQUIRE(empty.hash() == 0u);
    REQUIRE_FALSE(AssetId::fromName("").isValid());
}

TEST_CASE("AssetId ordering orders by hash then name", "[assets]") {
    const AssetId a = AssetId::fromName("a");
    const AssetId b = AssetId::fromName("b");
    const AssetId aAgain = AssetId::fromName("a");
    REQUIRE_FALSE(a < aAgain);
    const bool aBeforeB = a < b;
    REQUIRE(aBeforeB == (a.hash() < b.hash()));
    REQUIRE((b < a) == (b.hash() < a.hash()));
}

TEST_CASE("AssetId works as a map key", "[assets]") {
    std::map<AssetId, int> sorted;
    sorted[AssetId::fromName("one")] = 1;
    sorted[AssetId::fromName("two")] = 2;
    REQUIRE(sorted.size() == 2);
    REQUIRE(sorted.at(AssetId::fromName("one")) == 1);
    REQUIRE(std::hash<AssetId>{}(AssetId::fromName("one")) ==
            std::hash<std::uint64_t>{}(AssetId::fromName("one").hash()));
}

TEST_CASE("bytesPerPixel and rowPitch describe the packed layout", "[assets]") {
    REQUIRE(fa::assets::bytesPerPixel(ImageFormat::Rgba8) == 4);
    REQUIRE(fa::assets::bytesPerPixel(ImageFormat::R8) == 1);

    ImageData image;
    image.width = 8;
    image.height = 4;
    REQUIRE(image.rowPitch() == 32u);
    REQUIRE(image.requiredBytes() == 128u);

    image.format = ImageFormat::R8;
    REQUIRE(image.rowPitch() == 8u);
    REQUIRE(image.requiredBytes() == 32u);
}

TEST_CASE("ImageData rejects sizes and buffers that cannot hold pixels", "[assets]") {
    ImageData image;
    REQUIRE_FALSE(image.isValid());
    REQUIRE(image.requiredBytes() == 0u);

    image.width = 4;
    image.height = 0;
    REQUIRE_FALSE(image.isValid());

    image.height = 2;
    image.pixels.assign(31, 0);
    REQUIRE_FALSE(image.isValid());
    image.pixels.push_back(0);
    REQUIRE(image.isValid());
}

TEST_CASE("makeSolidImage writes the colour into every pixel", "[assets]") {
    const Color color{0.0f, 0.5f, 1.0f, 1.0f};
    const ImageData image = makeSolidImage(2, 2, color);
    REQUIRE(image.isValid());
    REQUIRE(image.width == 2);
    REQUIRE(image.height == 2);
    REQUIRE(image.pixels.size() == 16u);
    REQUIRE(image.pixels[0] == 0);
    REQUIRE(image.pixels[1] == 128);
    REQUIRE(image.pixels[2] == 255);
    REQUIRE(image.pixels[3] == 255);
    REQUIRE(image.pixels[12] == image.pixels[0]);
}

TEST_CASE("makeSolidImage clamps channels and honours alpha", "[assets]") {
    const ImageData image = makeSolidImage(1, 1, Color{2.0f, -1.0f, 0.0f, 0.25f});
    REQUIRE(image.isValid());
    REQUIRE(image.pixels[0] == 255);
    REQUIRE(image.pixels[1] == 0);
    REQUIRE(image.pixels[2] == 0);
    REQUIRE(image.pixels[3] == 64);
}

TEST_CASE("makeSolidImage in R8 stores a single channel", "[assets]") {
    const ImageData image = makeSolidImage(3, 2, Color{0.0f, 1.0f, 1.0f, 1.0f}, ImageFormat::R8);
    REQUIRE(image.isValid());
    REQUIRE(image.format == ImageFormat::R8);
    REQUIRE(image.pixels.size() == 6u);
    REQUIRE(image.pixels[0] == 0);
}

TEST_CASE("makeSolidImage with a non-positive size yields an invalid image", "[assets]") {
    REQUIRE_FALSE(makeSolidImage(0, 4, Color{}).isValid());
    REQUIRE_FALSE(makeSolidImage(4, -1, Color{}).isValid());
}

TEST_CASE("makeCheckerboardImage alternates per cell", "[assets]") {
    const Color a{1.0f, 0.0f, 0.0f, 1.0f};
    const Color b{0.0f, 0.0f, 1.0f, 1.0f};
    const ImageData image = makeCheckerboardImage(4, 4, a, b, 2);
    REQUIRE(image.isValid());
    const auto* pixels = image.pixels.data();
    const std::size_t pitch = image.rowPitch();
    REQUIRE(pixels[0] == 255);
    REQUIRE(pixels[1] == 0);
    REQUIRE(pixels[2] == 0);
    REQUIRE(pixels[pitch * 0 + 2 * 4 + 0] == 0);
    REQUIRE(pixels[pitch * 0 + 2 * 4 + 2] == 255);
    REQUIRE(pixels[pitch * 2 + 0 * 4 + 0] == 0);
    REQUIRE(pixels[pitch * 2 + 2 * 4 + 0] == 255);
}

TEST_CASE("makeCheckerboardImage treats a cell below one as a single pixel", "[assets]") {
    const ImageData image = makeCheckerboardImage(2, 2, Color{}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0);
    REQUIRE(image.isValid());
    REQUIRE(image.pixels[0] == 0);
    REQUIRE(image.pixels[4] == 255);
}

TEST_CASE("parseColorText accepts six and eight digit colours", "[assets]") {
    const fa::core::Result<Color> six = parseColorText("#4a7c3f");
    REQUIRE(six.isOk());
    REQUIRE(std::abs(six.value().r - 74.0f / 255.0f) < kEpsilon);
    REQUIRE(std::abs(six.value().g - 124.0f / 255.0f) < kEpsilon);
    REQUIRE(std::abs(six.value().b - 63.0f / 255.0f) < kEpsilon);
    REQUIRE(std::abs(six.value().a - 1.0f) < kEpsilon);

    const fa::core::Result<Color> eight = parseColorText("4a7c3f80");
    REQUIRE(eight.isOk());
    REQUIRE(std::abs(eight.value().a - 128.0f / 255.0f) < kEpsilon);
}

TEST_CASE("parseColorText accepts upper case hex", "[assets]") {
    const fa::core::Result<Color> upper = parseColorText("#FFFFFF");
    REQUIRE(upper.isOk());
    REQUIRE(std::abs(upper.value().r - 1.0f) < kEpsilon);
}

TEST_CASE("parseColorText rejects malformed text", "[assets]") {
    REQUIRE(parseColorText("").isError());
    REQUIRE(parseColorText("#fff").isError());
    REQUIRE(parseColorText("#zzzzzz").isError());
    REQUIRE(parseColorText("#1234567").isError());
    REQUIRE(parseColorText("#1234567g").isError());
    REQUIRE(parseColorText("#12345678").isOk());
}

TEST_CASE("toColorText round trips through parseColorText", "[assets]") {
    const fa::core::Result<Color> parsed = parseColorText("#4a7c3f80");
    REQUIRE(parsed.isOk());
    REQUIRE(toColorText(parsed.value()) == "#4a7c3f80");
    REQUIRE(toColorText(Color{1.0f, 1.0f, 1.0f, 1.0f}) == "#ffffffff");
}

TEST_CASE("AssetCatalog keeps entries in document order", "[assets]") {
    const AssetCatalog catalog = makeCatalog();
    REQUIRE(catalog.size() == 4u);
    REQUIRE_FALSE(catalog.empty());
    const std::span<const AssetEntry> entries = catalog.entries();
    REQUIRE(entries[0].id.name() == "tex.grass");
    REQUIRE(entries[3].id.name() == "data.crops");
}

TEST_CASE("AssetCatalog defaults a texture to kind Texture with a flat colour", "[assets]") {
    const AssetCatalog catalog = makeCatalog();
    const AssetEntry* grass = catalog.find("tex.grass");
    REQUIRE(grass != nullptr);
    REQUIRE(grass->kind == AssetKind::Texture);
    REQUIRE(grass->path.empty());
    REQUIRE(grass->width == 16);
    REQUIRE(grass->height == 16);
    REQUIRE(grass->tileSize == 0);
    REQUIRE(sameColor(grass->color, grass->altColor));
}

TEST_CASE("AssetCatalog reads the alternative colour and tile size", "[assets]") {
    const AssetCatalog catalog = makeCatalog();
    const AssetEntry* soil = catalog.find(AssetId::fromName("tex.soil"));
    REQUIRE(soil != nullptr);
    REQUIRE(soil->tileSize == 8);
    REQUIRE_FALSE(sameColor(soil->color, soil->altColor));
}

TEST_CASE("AssetCatalog marks a json entry as JsonText with a path", "[assets]") {
    const AssetCatalog catalog = makeCatalog();
    const AssetEntry* data = catalog.find("data.crops");
    REQUIRE(data != nullptr);
    REQUIRE(data->kind == AssetKind::JsonText);
    REQUIRE(data->path == "data/crops.json");
}

TEST_CASE("AssetCatalog reports an unknown name as absent", "[assets]") {
    const AssetCatalog catalog = makeCatalog();
    REQUIRE(catalog.find("tex.missing") == nullptr);
    REQUIRE(catalog.find(AssetId{}) == nullptr);
}

TEST_CASE("AssetCatalog rejects a document that is not JSON", "[assets]") {
    AssetCatalog catalog;
    const fa::core::Status status = catalog.loadFromText("{ not json");
    REQUIRE(status.isError());
    REQUIRE(status.error().code == asset_error::kCatalogParseFailed);
}

TEST_CASE("AssetCatalog requires a root object with an assets array", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText("[]").error().code == asset_error::kCatalogParseFailed);
    REQUIRE(catalog.loadFromText("{}").error().code == asset_error::kCatalogParseFailed);
    REQUIRE(catalog.loadFromText(R"({"assets": 3})").error().code == asset_error::kCatalogParseFailed);
}

TEST_CASE("AssetCatalog rejects an entry without an id", "[assets]") {
    AssetCatalog catalog;
    const fa::core::Status status = catalog.loadFromText(R"({"assets": [{"size": [8, 8]}]})");
    REQUIRE(status.isError());
    REQUIRE(status.error().code == asset_error::kCatalogParseFailed);
    REQUIRE(status.error().message.find("0") != std::string::npos);
}

TEST_CASE("AssetCatalog rejects a duplicate id", "[assets]") {
    AssetCatalog catalog;
    const fa::core::Status status =
        catalog.loadFromText(R"({"assets": [{"id": "tex.a", "size": [8, 8]}, {"id": "tex.a", "size": [8, 8]}]})");
    REQUIRE(status.isError());
    REQUIRE(status.error().code == asset_error::kCatalogParseFailed);
    REQUIRE(status.error().message.find("tex.a") != std::string::npos);
}

TEST_CASE("AssetCatalog rejects a path-less texture without a size", "[assets]") {
    AssetCatalog catalog;
    const fa::core::Status status = catalog.loadFromText(R"({"assets": [{"id": "tex.a"}]})");
    REQUIRE(status.isError());
    REQUIRE(status.error().code == asset_error::kCatalogParseFailed);
}

TEST_CASE("AssetCatalog rejects a json entry without a path", "[assets]") {
    AssetCatalog catalog;
    const fa::core::Status status = catalog.loadFromText(R"({"assets": [{"id": "data.a", "kind": "json"}]})");
    REQUIRE(status.isError());
    REQUIRE(status.error().code == asset_error::kCatalogParseFailed);
}

TEST_CASE("AssetCatalog rejects an unknown kind and a bad colour", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText(R"({"assets": [{"id": "tex.a", "kind": "video", "size": [8, 8]}]})").isError());
    REQUIRE(catalog.loadFromText(R"({"assets": [{"id": "tex.a", "size": [8, 8], "color": "green"}]})").isError());
}

TEST_CASE("AssetCatalog keeps its previous entries when a reload fails", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText(kCatalogJson).isOk());
    REQUIRE(catalog.loadFromText("{ broken").isError());
    REQUIRE(catalog.size() == 4u);
    REQUIRE(catalog.find("tex.grass") != nullptr);
}

TEST_CASE("AssetCatalog loads through the file system", "[assets]") {
    FakeFileSystem fileSystem;
    fileSystem.put("config/assets.json", std::string(kCatalogJson));
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromFile(fileSystem, "config/assets.json").isOk());
    REQUIRE(catalog.size() == 4u);
    REQUIRE(catalog.loadFromFile(fileSystem, "config/missing.json").error().code == asset_error::kLoadFailed);
}

TEST_CASE("AssetCatalog clear drops every entry", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    catalog.clear();
    REQUIRE(catalog.empty());
    REQUIRE(catalog.find("tex.grass") == nullptr);
}

TEST_CASE("createAssetManager refuses a description without a catalog or file system", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    FakeFileSystem fileSystem;

    AssetManagerDesc noCatalog;
    noCatalog.fileSystem = &fileSystem;
    REQUIRE(fa::assets::createAssetManager(noCatalog) == nullptr);

    AssetManagerDesc noFileSystem;
    noFileSystem.catalog = &catalog;
    REQUIRE(fa::assets::createAssetManager(noFileSystem) == nullptr);
}

TEST_CASE("loadTexture builds the placeholder block from the catalog", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.grass"));
    REQUIRE(loaded.isOk());
    const AssetHandle<TextureAsset> handle = loaded.value();
    REQUIRE(static_cast<bool>(handle));
    REQUIRE(handle.state() == AssetState::Ready);

    const TextureAsset* texture = handle.get();
    REQUIRE(texture != nullptr);
    REQUIRE(texture->placeholder);
    REQUIRE_FALSE(texture->uploadFailed);
    REQUIRE(texture->revision == 1u);
    REQUIRE(texture->image.width == 16);
    REQUIRE(texture->image.height == 16);
    REQUIRE(texture->image.isValid());
    REQUIRE(texture->gpuTexture == 1u);

    REQUIRE(fixture.uploader.uploadCount == 1);
    REQUIRE(fixture.uploader.lastWidth == 16);
    REQUIRE(fixture.uploader.lastDebugName == "tex.grass");
}

TEST_CASE("loadTexture draws a checkerboard when the entry sets a tile size", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.soil"));
    REQUIRE(loaded.isOk());
    const ImageData& image = loaded.value()->image;
    REQUIRE(image.isValid());
    REQUIRE(image.pixels[0] != image.pixels[image.rowPitch() * 0 + 8 * 4]);
}

TEST_CASE("loadTexture caches one slot per id", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto first = manager->loadTexture(AssetId::fromName("tex.grass"));
    const auto second = manager->loadTexture(AssetId::fromName("tex.grass"));
    REQUIRE(first.isOk());
    REQUIRE(second.isOk());
    REQUIRE(first.value().get() == second.value().get());
    REQUIRE(first.value().useCount() == 2);
    REQUIRE(fixture.uploader.uploadCount == 1);
    REQUIRE(manager->stats().textureCount == 1);
}

TEST_CASE("handles drop the reference count and allow eviction", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    auto loaded = manager->loadTexture(AssetId::fromName("tex.rock"));
    REQUIRE(loaded.isOk());
    REQUIRE(manager->stats().evictableCount == 0u);

    loaded.value().reset();
    REQUIRE(manager->stats().evictableCount == 1u);
    manager->purgeUnreferenced();
    REQUIRE(manager->stats().textureCount == 0u);
    REQUIRE(fixture.uploader.releaseCount == 1);
    REQUIRE(fixture.uploader.liveTokens.empty());
}

TEST_CASE("loadTexture rejects an id that is not in the catalog", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.missing"));
    REQUIRE(loaded.isError());
    REQUIRE(loaded.error().code == asset_error::kUnknownAsset);

    const auto empty = manager->loadTexture(AssetId{});
    REQUIRE(empty.isError());
    REQUIRE(empty.error().code == asset_error::kUnknownAsset);
}

TEST_CASE("loadTexture refuses an id of the wrong kind", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("data.crops"));
    REQUIRE(loaded.isError());
    REQUIRE(loaded.error().code == asset_error::kUnsupportedKind);
}

TEST_CASE("a missing file degrades to the placeholder instead of failing", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText(R"({"assets": [{"id": "tex.a", "path": "art/a.fake", "size": [4, 4]}]})").isOk());
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.a"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value()->placeholder);
    REQUIRE(loaded.value()->image.isValid());
}

TEST_CASE("a file with no decoder degrades to the placeholder", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText(R"({"assets": [{"id": "tex.a", "path": "art/a.png", "size": [4, 4]}]})").isOk());
    Fixture fixture;
    fixture.fileSystem.put("art/a.png", "png bytes");
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.a"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value()->placeholder);
}

TEST_CASE("a failed decode degrades to the placeholder", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText(R"({"assets": [{"id": "tex.a", "path": "art/a.fake", "size": [4, 4]}]})").isOk());
    Fixture fixture;
    fixture.fileSystem.put("art/a.fake", "not a size");
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.a"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value()->placeholder);
}

TEST_CASE("a decodable file wins over the placeholder", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText(R"({"assets": [{"id": "tex.a", "path": "art/a.fake", "size": [4, 4]}]})").isOk());
    Fixture fixture;
    fixture.fileSystem.put("art/a.fake", "3x2");
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.a"));
    REQUIRE(loaded.isOk());
    REQUIRE_FALSE(loaded.value()->placeholder);
    REQUIRE(loaded.value()->image.width == 3);
    REQUIRE(loaded.value()->image.height == 2);
}

TEST_CASE("the root path prefixes the entry path", "[assets]") {
    AssetCatalog catalog;
    REQUIRE(catalog.loadFromText(R"({"assets": [{"id": "tex.a", "path": "a.fake", "size": [4, 4]}]})").isOk());
    Fixture fixture;
    fixture.fileSystem.put("game/art/a.fake", "1x1");

    AssetManagerDesc desc = fixture.baseDesc(catalog);
    desc.rootPath = "game/art";
    auto manager = fa::assets::createAssetManager(desc);
    REQUIRE(manager != nullptr);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.a"));
    REQUIRE(loaded.isOk());
    REQUIRE_FALSE(loaded.value()->placeholder);
    REQUIRE(loaded.value()->image.width == 1);
}

TEST_CASE("a rejected upload keeps the pixels and flags the failure", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    fixture.uploader.rejectAll = true;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.grass"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value()->uploadFailed);
    REQUIRE(loaded.value()->gpuTexture == 0u);
    REQUIRE(loaded.value()->image.isValid());
    REQUIRE(manager->stats().liveGpuTextures == 0u);
}

TEST_CASE("a manager without an uploader still serves CPU pixels", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog, nullptr, true, false);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.grass"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value()->gpuTexture == 0u);
    REQUIRE_FALSE(loaded.value()->uploadFailed);
    REQUIRE(loaded.value()->image.isValid());
}

TEST_CASE("retainCpuPixels false frees the buffer after a successful upload", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog, nullptr, false);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.grass"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value()->gpuTexture == 1u);
    REQUIRE(loaded.value()->image.pixels.empty());
}

TEST_CASE("loadJson returns the file text", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    fixture.fileSystem.put("data/crops.json", R"({"crops": 1})");
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadJson(AssetId::fromName("data.crops"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value()->text == R"({"crops": 1})");
    REQUIRE(loaded.value()->revision == 1u);
    REQUIRE(manager->stats().jsonCount == 1u);
}

TEST_CASE("loadJson reports a missing file and has no placeholder", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadJson(AssetId::fromName("data.crops"));
    REQUIRE(loaded.isError());
    REQUIRE(loaded.error().code == asset_error::kLoadFailed);
    REQUIRE(manager->stats().failedCount == 1u);
}

TEST_CASE("loadJson refuses an id of the wrong kind", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadJson(AssetId::fromName("tex.grass"));
    REQUIRE(loaded.isError());
    REQUIRE(loaded.error().code == asset_error::kUnsupportedKind);
}

TEST_CASE("a failed slot stays failed until reload is called", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    REQUIRE(manager->loadJson(AssetId::fromName("data.crops")).isError());
    const auto again = manager->loadJson(AssetId::fromName("data.crops"));
    REQUIRE(again.isError());
    REQUIRE(again.error().code == asset_error::kLoadFailed);

    fixture.fileSystem.put("data/crops.json", "{}");
    REQUIRE(manager->reload(AssetId::fromName("data.crops")).isOk());
    const auto reloaded = manager->loadJson(AssetId::fromName("data.crops"));
    REQUIRE(reloaded.isOk());
    REQUIRE(reloaded.value()->text == "{}");
    REQUIRE(reloaded.value()->revision == 1u);
}

TEST_CASE("reload bumps the revision and keeps the same slot", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.grass"));
    REQUIRE(loaded.isOk());
    const TextureAsset* before = loaded.value().get();
    REQUIRE(before->revision == 1u);

    REQUIRE(manager->reload(AssetId::fromName("tex.grass")).isOk());
    REQUIRE(loaded.value().get() == before);
    REQUIRE(before->revision == 2u);
    REQUIRE(fixture.uploader.uploadCount == 2);
    REQUIRE(fixture.uploader.releaseCount == 1);
}

TEST_CASE("reload rejects an unknown id", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const fa::core::Status status = manager->reload(AssetId::fromName("tex.missing"));
    REQUIRE(status.isError());
    REQUIRE(status.error().code == asset_error::kUnknownAsset);
}

TEST_CASE("stats reports the cache contents", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    fixture.fileSystem.put("data/crops.json", "{}");
    auto* manager = fixture.create(catalog);

    REQUIRE(manager->loadTexture(AssetId::fromName("tex.grass")).isOk());
    REQUIRE(manager->loadJson(AssetId::fromName("data.crops")).isOk());

    const fa::assets::AssetStats stats = manager->stats();
    REQUIRE(stats.textureCount == 1u);
    REQUIRE(stats.jsonCount == 1u);
    REQUIRE(stats.placeholderCount == 1u);
    REQUIRE(stats.loadingCount == 0u);
    REQUIRE(stats.failedCount == 0u);
    REQUIRE(stats.liveGpuTextures == 1u);
    REQUIRE(stats.cpuBytes == 16u * 16u * 4u + 2u);
}

TEST_CASE("releaseGpuTextures hands every token back", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    REQUIRE(manager->loadTexture(AssetId::fromName("tex.grass")).isOk());
    REQUIRE(manager->loadTexture(AssetId::fromName("tex.rock")).isOk());
    REQUIRE(fixture.uploader.liveTokens.size() == 2u);

    manager->releaseGpuTextures();
    REQUIRE(fixture.uploader.releaseCount == 2);
    REQUIRE(fixture.uploader.liveTokens.empty());
    REQUIRE(manager->stats().liveGpuTextures == 0u);
}

TEST_CASE("releaseAll leaves stale handles readable", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.grass"));
    REQUIRE(loaded.isOk());
    const AssetHandle<TextureAsset> handle = loaded.value();
    REQUIRE(static_cast<bool>(handle));

    manager->releaseAll();
    REQUIRE(handle.state() == AssetState::Unloaded);
    REQUIRE(handle.get() == nullptr);
    REQUIRE(manager->stats().textureCount == 0u);
    REQUIRE(fixture.uploader.liveTokens.empty());
}

TEST_CASE("requestTexture loads inline when no job system is configured", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const AssetHandle<TextureAsset> handle = manager->requestTexture(AssetId::fromName("tex.grass"));
    REQUIRE(handle.isValid());
    REQUIRE(handle.state() == AssetState::Ready);
    REQUIRE(static_cast<bool>(handle));
}

TEST_CASE("requestTexture refuses an id that cannot be loaded", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    Fixture fixture;
    auto* manager = fixture.create(catalog);

    const AssetHandle<TextureAsset> missing = manager->requestTexture(AssetId::fromName("tex.missing"));
    REQUIRE_FALSE(missing.isValid());
    const AssetHandle<TextureAsset> wrongKind = manager->requestTexture(AssetId::fromName("data.crops"));
    REQUIRE_FALSE(wrongKind.isValid());
}

TEST_CASE("requestTexture finishes in update() when a job system is configured", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    fa::events::JobSystem jobs(1);
    Fixture fixture;
    auto* manager = fixture.create(catalog, &jobs);

    const AssetHandle<TextureAsset> handle = manager->requestTexture(AssetId::fromName("tex.grass"));
    REQUIRE(handle.isValid());
    // Only update() may finalize a background load, so this state is deterministic.
    REQUIRE(handle.state() == AssetState::Loading);
    REQUIRE(handle.get() == nullptr);
    REQUIRE(manager->stats().loadingCount == 1u);

    // update() never blocks, so wait out a real deadline instead of a spin count:
    // a Release build runs a fixed number of iterations faster than the worker starts.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (handle.state() == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
        manager->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(handle.state() == AssetState::Ready);
    REQUIRE(static_cast<bool>(handle));
    REQUIRE(handle.get()->placeholder);
    REQUIRE(fixture.uploader.uploadCount == 1);
}

TEST_CASE("a background request is completed by loadTexture without update()", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    fa::events::JobSystem jobs(1);
    Fixture fixture;
    auto* manager = fixture.create(catalog, &jobs);

    const AssetHandle<TextureAsset> pending = manager->requestTexture(AssetId::fromName("tex.rock"));
    REQUIRE(pending.state() == AssetState::Loading);

    const auto loaded = manager->loadTexture(AssetId::fromName("tex.rock"));
    REQUIRE(loaded.isOk());
    REQUIRE(loaded.value().get() == pending.get());
    REQUIRE(pending.state() == AssetState::Ready);
    REQUIRE(pending.useCount() == 2);
}

TEST_CASE("a pending load survives purgeUnreferenced", "[assets]") {
    AssetCatalog catalog = makeCatalog();
    fa::events::JobSystem jobs(1);
    Fixture fixture;
    auto* manager = fixture.create(catalog, &jobs);

    manager->requestTexture(AssetId::fromName("tex.grass"));
    manager->purgeUnreferenced();
    REQUIRE(manager->stats().textureCount == 1u);
}
