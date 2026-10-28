#include <fa/assets/AssetManager.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fa/assets/AssetError.hpp>
#include <fa/core/Log.hpp>
#include <fa/events/JobSystem.hpp>
#include <fa/platform/FileSystem.hpp>

namespace fa::assets {

namespace {

constexpr fa::core::LogCategory kLogCategory = fa::core::LogCategory::Asset;

/// \brief What loadFromSource() produced, before any GPU work.
struct ImagePayload {
    ImageData image;
    /// \brief True when the pixels did not come from the file.
    bool placeholder = false;
    /// \brief Why the placeholder was used; empty for a real load.
    std::string note;
};

/// \brief Cache slot for one texture.
///
/// The slot owns the value and the reference count; handles only see the erased base
/// class. Everything except `m_pending` is frame-thread state: the job body writes
/// `m_pending`, and the frame thread reads it only after `isComplete()`.
class TextureSlot final : public detail::AssetSlotBase {
public:
    void addRef() noexcept override { ++m_refs; }

    void release() noexcept override {
        if (m_refs > 0) {
            --m_refs;
        }
    }

    int refCount() const noexcept override { return m_refs; }
    AssetState state() const noexcept override { return m_state; }
    const AssetId& id() const noexcept override { return m_id; }
    void* objectRaw() noexcept override { return &m_value; }

    AssetId m_id;
    TextureAsset m_value;
    AssetState m_state = AssetState::Unloaded;
    int m_refs = 0;
    events::JobHandle m_job;
    std::optional<ImagePayload> m_pending;
    std::optional<core::Error> m_pendingError;
};

/// \brief Cache slot for one JSON document.
///
/// JSON never loads in the background: a document is small enough that a job would
/// only add ordering problems, and the caller always wants the text immediately.
class JsonSlot final : public detail::AssetSlotBase {
public:
    void addRef() noexcept override { ++m_refs; }

    void release() noexcept override {
        if (m_refs > 0) {
            --m_refs;
        }
    }

    int refCount() const noexcept override { return m_refs; }
    AssetState state() const noexcept override { return m_state; }
    const AssetId& id() const noexcept override { return m_id; }
    void* objectRaw() noexcept override { return &m_value; }

    AssetId m_id;
    JsonAsset m_value;
    AssetState m_state = AssetState::Unloaded;
    int m_refs = 0;
};

}  // namespace

/// \brief The cache behind `createAssetManager()`.
///
/// The name matches the `friend` declaration in `AssetHandle`, which is the only way
/// a handle can be built from a slot without making that constructor public.
class AssetManager final : public IAssetManager {
public:
    explicit AssetManager(const AssetManagerDesc& desc);
    ~AssetManager() override;

    core::Result<AssetHandle<TextureAsset>> loadTexture(const AssetId& id) override;
    AssetHandle<TextureAsset> requestTexture(const AssetId& id) override;
    core::Result<AssetHandle<JsonAsset>> loadJson(const AssetId& id) override;
    core::Status reload(const AssetId& id) override;
    void update() override;
    void purgeUnreferenced() override;
    void releaseGpuTextures() override;
    void releaseAll() override;
    AssetStats stats() const override;
    const AssetCatalog& catalog() const override { return *m_desc.catalog; }

private:
    const AssetEntry* entryFor(const AssetId& id) const noexcept;
    TextureSlot* findTextureSlot(const AssetId& id) const noexcept;
    JsonSlot* findJsonSlot(const AssetId& id) const noexcept;
    TextureSlot& ensureTextureSlot(const AssetId& id);
    JsonSlot& ensureJsonSlot(const AssetId& id);

    std::string resolvePath(const AssetEntry& entry) const;
    IImageDecoder* findDecoder(std::string_view path) const noexcept;
    ImagePayload buildPlaceholder(const AssetEntry& entry) const;
    core::Result<ImagePayload> loadFromSource(const AssetEntry& entry) const;

    void applyImage(TextureSlot& slot, ImagePayload payload);
    void logLoadResult(const AssetEntry& entry, bool placeholder, std::string_view note) const;
    core::Status runTextureLoad(TextureSlot& slot, const AssetEntry& entry);
    void startTextureLoad(TextureSlot& slot, const AssetEntry& entry);
    void finalizeTextureLoad(TextureSlot& slot);
    core::Status settleTexture(TextureSlot& slot);
    core::Status runJsonLoad(JsonSlot& slot, const AssetEntry& entry);
    void releaseSlotGpu(TextureSlot& slot) noexcept;

    AssetManagerDesc m_desc;
    std::vector<IImageDecoder*> m_decoders;
    std::unordered_map<AssetId, std::unique_ptr<TextureSlot>> m_textures;
    std::unordered_map<AssetId, std::unique_ptr<JsonSlot>> m_json;
    /// \brief Slots retired by releaseAll(): kept alive so stale handles read
    /// `Unloaded` instead of touching freed memory.
    std::vector<std::unique_ptr<detail::AssetSlotBase>> m_retired;
};

AssetManager::AssetManager(const AssetManagerDesc& desc) : m_desc(desc) {
    m_decoders.assign(desc.decoders.begin(), desc.decoders.end());
}

AssetManager::~AssetManager() {
    // A worker may still be writing into a slot; joining first is what keeps the
    // destroyed cache from being a use-after-free. The job system has to outlive us.
    if (m_desc.jobs != nullptr) {
        for (auto& [id, slot] : m_textures) {
            if (slot->m_state == AssetState::Loading && slot->m_job.isValid()) {
                static_cast<void>(m_desc.jobs->wait(slot->m_job));
            }
        }
    }
    for (auto& [id, slot] : m_textures) {
        releaseSlotGpu(*slot);
    }
    m_textures.clear();
    m_json.clear();
    m_retired.clear();
}

const AssetEntry* AssetManager::entryFor(const AssetId& id) const noexcept {
    if (m_desc.catalog == nullptr) {
        return nullptr;
    }
    return m_desc.catalog->find(id);
}

TextureSlot* AssetManager::findTextureSlot(const AssetId& id) const noexcept {
    const auto it = m_textures.find(id);
    return it == m_textures.end() ? nullptr : it->second.get();
}

JsonSlot* AssetManager::findJsonSlot(const AssetId& id) const noexcept {
    const auto it = m_json.find(id);
    return it == m_json.end() ? nullptr : it->second.get();
}

TextureSlot& AssetManager::ensureTextureSlot(const AssetId& id) {
    if (TextureSlot* existing = findTextureSlot(id); existing != nullptr) {
        return *existing;
    }
    auto owned = std::make_unique<TextureSlot>();
    owned->m_id = id;
    TextureSlot* slot = owned.get();
    m_textures.emplace(id, std::move(owned));
    return *slot;
}

JsonSlot& AssetManager::ensureJsonSlot(const AssetId& id) {
    if (JsonSlot* existing = findJsonSlot(id); existing != nullptr) {
        return *existing;
    }
    auto owned = std::make_unique<JsonSlot>();
    owned->m_id = id;
    JsonSlot* slot = owned.get();
    m_json.emplace(id, std::move(owned));
    return *slot;
}

std::string AssetManager::resolvePath(const AssetEntry& entry) const {
    if (m_desc.rootPath.empty()) {
        return entry.path;
    }
    std::string path = m_desc.rootPath;
    const char last = path.back();
    if (last != '/' && last != '\\') {
        path.push_back('/');
    }
    path += entry.path;
    return path;
}

IImageDecoder* AssetManager::findDecoder(std::string_view path) const noexcept {
    for (IImageDecoder* decoder : m_decoders) {
        if (decoder != nullptr && decoder->supports(path)) {
            return decoder;
        }
    }
    return nullptr;
}

ImagePayload AssetManager::buildPlaceholder(const AssetEntry& entry) const {
    ImagePayload payload;
    payload.placeholder = true;
    if (entry.width <= 0 || entry.height <= 0) {
        payload.note = "placeholder size is not positive";
        return payload;
    }
    if (entry.tileSize > 0) {
        payload.image = makeCheckerboardImage(entry.width, entry.height, entry.color, entry.altColor, entry.tileSize);
    } else {
        payload.image = makeSolidImage(entry.width, entry.height, entry.color);
    }
    return payload;
}

core::Result<ImagePayload> AssetManager::loadFromSource(const AssetEntry& entry) const {
    auto fallback = [this, &entry](std::string note) -> core::Result<ImagePayload> {
        ImagePayload payload = buildPlaceholder(entry);
        payload.note = std::move(note);
        if (!payload.image.isValid()) {
            return core::Result<ImagePayload>::err(asset_error::kInvalidImage,
                                                   payload.note + "; no usable placeholder either");
        }
        return core::Result<ImagePayload>::ok(std::move(payload));
    };

    if (entry.path.empty()) {
        return fallback("catalog declares no source path");
    }

    const std::string path = resolvePath(entry);
    core::Result<std::string> bytes = m_desc.fileSystem->readBinary(path);
    if (bytes.isError()) {
        return fallback("cannot read " + path + ": " + bytes.error().message);
    }

    IImageDecoder* decoder = findDecoder(entry.path);
    if (decoder == nullptr) {
        return fallback("no decoder handles " + entry.path);
    }

    const std::string& content = bytes.value();
    const auto* raw = reinterpret_cast<const std::uint8_t*>(content.data());
    core::Result<ImageData> decoded = decoder->decode(path, std::span<const std::uint8_t>(raw, content.size()));
    if (decoded.isError()) {
        return fallback("decoder failed on " + path + ": " + decoded.error().message);
    }
    if (!decoded.value().isValid()) {
        return fallback("decoder produced an unusable image for " + path);
    }

    ImagePayload payload;
    payload.image = std::move(decoded).take();
    if (entry.width > 0 && entry.height > 0 &&
        (entry.width != payload.image.width || entry.height != payload.image.height)) {
        core::logf(kLogCategory,
                   core::LogLevel::Warn,
                   "%.*s: decoded %dx%d but the catalog declares %dx%d",
                   static_cast<int>(entry.id.name().size()),
                   entry.id.name().data(),
                   payload.image.width,
                   payload.image.height,
                   entry.width,
                   entry.height);
    }
    return core::Result<ImagePayload>::ok(std::move(payload));
}

void AssetManager::releaseSlotGpu(TextureSlot& slot) noexcept {
    if (slot.m_value.gpuTexture != 0) {
        if (m_desc.uploader != nullptr) {
            m_desc.uploader->release(slot.m_value.gpuTexture);
        }
        slot.m_value.gpuTexture = 0;
    }
}

void AssetManager::applyImage(TextureSlot& slot, ImagePayload payload) {
    releaseSlotGpu(slot);
    slot.m_value.image = std::move(payload.image);
    slot.m_value.placeholder = payload.placeholder;
    slot.m_value.uploadFailed = false;
    slot.m_value.gpuTexture = 0;

    if (m_desc.uploader != nullptr) {
        const std::uint32_t token = m_desc.uploader->upload(slot.m_value.image, slot.m_id.name());
        if (token == 0) {
            slot.m_value.uploadFailed = true;
            core::logf(kLogCategory,
                       core::LogLevel::Warn,
                       "%.*s: texture upload failed, keeping CPU pixels only",
                       static_cast<int>(slot.m_id.name().size()),
                       slot.m_id.name().data());
        } else {
            slot.m_value.gpuTexture = token;
        }
    }

    // Dropping the pixels is only safe once a GPU copy exists; otherwise a failed
    // upload would leave the asset with no content at all.
    if (!m_desc.retainCpuPixels && slot.m_value.gpuTexture != 0) {
        slot.m_value.image = ImageData{};
    }

    ++slot.m_value.revision;
    slot.m_state = AssetState::Ready;
}

void AssetManager::logLoadResult(const AssetEntry& entry, bool placeholder, std::string_view note) const {
    if (!m_desc.logEvents) {
        return;
    }
    const int nameLength = static_cast<int>(entry.id.name().size());
    if (!placeholder) {
        core::logf(kLogCategory,
                   core::LogLevel::Debug,
                   "%.*s: loaded from %.*s",
                   nameLength,
                   entry.id.name().data(),
                   static_cast<int>(entry.path.size()),
                   entry.path.data());
        return;
    }
    const char* const reason = note.empty() ? "" : note.data();
    core::logf(kLogCategory,
               core::LogLevel::Warn,
               "%.*s: using placeholder (%.*s)",
               nameLength,
               entry.id.name().data(),
               static_cast<int>(note.size()),
               reason);
}

core::Status AssetManager::runTextureLoad(TextureSlot& slot, const AssetEntry& entry) {
    core::Result<ImagePayload> payload = loadFromSource(entry);
    if (payload.isError()) {
        slot.m_state = AssetState::Failed;
        core::logf(kLogCategory,
                   core::LogLevel::Error,
                   "%.*s: load failed: %s",
                   static_cast<int>(slot.m_id.name().size()),
                   slot.m_id.name().data(),
                   payload.error().message.c_str());
        return payload.ignoreValue();
    }

    const bool placeholder = payload.value().placeholder;
    const std::string note = payload.value().note;
    applyImage(slot, std::move(payload).take());
    logLoadResult(entry, placeholder, note);
    return core::okStatus();
}

void AssetManager::startTextureLoad(TextureSlot& slot, const AssetEntry& entry) {
    if (m_desc.jobs == nullptr) {
        static_cast<void>(runTextureLoad(slot, entry));
        return;
    }

    slot.m_state = AssetState::Loading;
    slot.m_pending.reset();
    slot.m_pendingError.reset();

    // The job only does file IO and decoding; the upload waits for the frame thread,
    // which is the whole reason `update()` exists.
    TextureSlot* const slotPtr = &slot;
    const AssetEntry* const entryPtr = &entry;
    events::JobDesc job;
    job.name = "asset:" + entry.id.name();
    job.run = [this, slotPtr, entryPtr]() -> core::Status {
        core::Result<ImagePayload> payload = loadFromSource(*entryPtr);
        if (payload.isError()) {
            slotPtr->m_pendingError = payload.error();
            return payload.ignoreValue();
        }
        slotPtr->m_pending = std::move(payload).take();
        return core::okStatus();
    };
    slot.m_job = m_desc.jobs->submit(std::move(job));
}

void AssetManager::finalizeTextureLoad(TextureSlot& slot) {
    if (slot.m_pendingError.has_value()) {
        slot.m_state = AssetState::Failed;
        core::logf(kLogCategory,
                   core::LogLevel::Error,
                   "%.*s: background load failed: %s",
                   static_cast<int>(slot.m_id.name().size()),
                   slot.m_id.name().data(),
                   slot.m_pendingError->message.c_str());
        slot.m_pendingError.reset();
        return;
    }
    if (!slot.m_pending.has_value()) {
        // The job reported success but left nothing behind, which only happens when a
        // decoder returned an empty payload; treat it as a failure, not as Ready.
        slot.m_state = AssetState::Failed;
        core::logf(kLogCategory,
                   core::LogLevel::Error,
                   "%.*s: background load produced no image",
                   static_cast<int>(slot.m_id.name().size()),
                   slot.m_id.name().data());
        return;
    }

    const bool placeholder = slot.m_pending->placeholder;
    const std::string note = slot.m_pending->note;
    ImagePayload payload = std::move(*slot.m_pending);
    slot.m_pending.reset();
    applyImage(slot, std::move(payload));
    if (const AssetEntry* entry = entryFor(slot.m_id); entry != nullptr) {
        logLoadResult(*entry, placeholder, note);
    }
}

core::Status AssetManager::settleTexture(TextureSlot& slot) {
    if (slot.m_state == AssetState::Failed) {
        return core::errorStatus(asset_error::kLoadFailed, "asset " + slot.m_id.name() + " failed earlier");
    }
    if (slot.m_state != AssetState::Loading) {
        return core::okStatus();
    }
    if (!slot.m_job.isValid() || m_desc.jobs == nullptr) {
        return core::okStatus();
    }
    static_cast<void>(m_desc.jobs->wait(slot.m_job));
    finalizeTextureLoad(slot);
    if (slot.m_state == AssetState::Failed) {
        return core::errorStatus(asset_error::kLoadFailed, "asset " + slot.m_id.name() + " failed to load");
    }
    return core::okStatus();
}

core::Status AssetManager::runJsonLoad(JsonSlot& slot, const AssetEntry& entry) {
    if (entry.path.empty()) {
        slot.m_state = AssetState::Failed;
        return core::errorStatus(asset_error::kLoadFailed, "asset " + entry.id.name() + " has no source path");
    }
    const std::string path = resolvePath(entry);
    core::Result<std::string> text = m_desc.fileSystem->readText(path);
    if (text.isError()) {
        slot.m_state = AssetState::Failed;
        const std::string message = "cannot read " + path + ": " + text.error().message;
        core::logf(kLogCategory,
                   core::LogLevel::Error,
                   "%.*s: %s",
                   static_cast<int>(entry.id.name().size()),
                   entry.id.name().data(),
                   message.c_str());
        return core::errorStatus(asset_error::kLoadFailed, message);
    }
    slot.m_value.text = std::move(text).take();
    ++slot.m_value.revision;
    slot.m_state = AssetState::Ready;
    return core::okStatus();
}

core::Result<AssetHandle<TextureAsset>> AssetManager::loadTexture(const AssetId& id) {
    if (!id.isValid()) {
        return core::Result<AssetHandle<TextureAsset>>::err(asset_error::kUnknownAsset, "empty asset id");
    }
    const AssetEntry* entry = entryFor(id);
    if (entry == nullptr) {
        return core::Result<AssetHandle<TextureAsset>>::err(asset_error::kUnknownAsset,
                                                            "asset " + id.name() + " is not in the catalog");
    }
    if (entry->kind != AssetKind::Texture) {
        return core::Result<AssetHandle<TextureAsset>>::err(asset_error::kUnsupportedKind,
                                                            "asset " + id.name() + " is not a texture");
    }
    if (m_desc.fileSystem == nullptr) {
        return core::Result<AssetHandle<TextureAsset>>::err(asset_error::kNotInitialized, "no file system configured");
    }

    TextureSlot& slot = ensureTextureSlot(id);
    if (slot.m_state == AssetState::Loading) {
        const core::Status settled = settleTexture(slot);
        if (settled.isError()) {
            return core::Result<AssetHandle<TextureAsset>>::err(settled.error().code, settled.error().message);
        }
    } else if (slot.m_state == AssetState::Failed) {
        return core::Result<AssetHandle<TextureAsset>>::err(
            asset_error::kLoadFailed, "asset " + id.name() + " failed earlier; call reload() to retry");
    } else if (slot.m_state == AssetState::Unloaded) {
        const core::Status status = runTextureLoad(slot, *entry);
        if (status.isError()) {
            return core::Result<AssetHandle<TextureAsset>>::err(status.error().code, status.error().message);
        }
    }

    slot.addRef();
    return core::Result<AssetHandle<TextureAsset>>::ok(AssetHandle<TextureAsset>(&slot));
}

AssetHandle<TextureAsset> AssetManager::requestTexture(const AssetId& id) {
    const AssetEntry* entry = entryFor(id);
    if (entry == nullptr || entry->kind != AssetKind::Texture || m_desc.fileSystem == nullptr) {
        core::logf(kLogCategory,
                   core::LogLevel::Error,
                   "%.*s: requested but not loadable",
                   static_cast<int>(id.name().size()),
                   id.name().data());
        return AssetHandle<TextureAsset>{};
    }

    TextureSlot& slot = ensureTextureSlot(id);
    if (slot.m_state == AssetState::Unloaded) {
        startTextureLoad(slot, *entry);
    }
    slot.addRef();
    return AssetHandle<TextureAsset>(&slot);
}

core::Result<AssetHandle<JsonAsset>> AssetManager::loadJson(const AssetId& id) {
    if (!id.isValid()) {
        return core::Result<AssetHandle<JsonAsset>>::err(asset_error::kUnknownAsset, "empty asset id");
    }
    const AssetEntry* entry = entryFor(id);
    if (entry == nullptr) {
        return core::Result<AssetHandle<JsonAsset>>::err(asset_error::kUnknownAsset,
                                                         "asset " + id.name() + " is not in the catalog");
    }
    if (entry->kind != AssetKind::JsonText) {
        return core::Result<AssetHandle<JsonAsset>>::err(asset_error::kUnsupportedKind,
                                                         "asset " + id.name() + " is not a json document");
    }
    if (m_desc.fileSystem == nullptr) {
        return core::Result<AssetHandle<JsonAsset>>::err(asset_error::kNotInitialized, "no file system configured");
    }

    JsonSlot& slot = ensureJsonSlot(id);
    if (slot.m_state == AssetState::Unloaded) {
        const core::Status status = runJsonLoad(slot, *entry);
        if (status.isError()) {
            return core::Result<AssetHandle<JsonAsset>>::err(status.error().code, status.error().message);
        }
    } else if (slot.m_state == AssetState::Failed) {
        return core::Result<AssetHandle<JsonAsset>>::err(
            asset_error::kLoadFailed, "asset " + id.name() + " failed earlier; call reload() to retry");
    }

    slot.addRef();
    return core::Result<AssetHandle<JsonAsset>>::ok(AssetHandle<JsonAsset>(&slot));
}

core::Status AssetManager::reload(const AssetId& id) {
    const AssetEntry* entry = entryFor(id);
    if (entry == nullptr) {
        return core::errorStatus(asset_error::kUnknownAsset, "asset " + id.name() + " is not in the catalog");
    }
    if (m_desc.fileSystem == nullptr) {
        return core::errorStatus(asset_error::kNotInitialized, "no file system configured");
    }

    if (entry->kind == AssetKind::Texture) {
        TextureSlot& slot = ensureTextureSlot(id);
        if (slot.m_state == AssetState::Loading) {
            // Retrying while a job is in flight would leave two results racing for one
            // slot; drain the job first and report it as the reload's outcome.
            const core::Status settled = settleTexture(slot);
            if (settled.isError()) {
                return settled;
            }
        }
        // A reload always goes through the synchronous path: this is the one call site
        // where the caller wants the result before continuing.
        return runTextureLoad(slot, *entry);
    }

    JsonSlot& slot = ensureJsonSlot(id);
    return runJsonLoad(slot, *entry);
}

void AssetManager::update() {
    for (auto& [id, slot] : m_textures) {
        if (slot->m_state != AssetState::Loading) {
            continue;
        }
        if (!slot->m_job.isValid() || !slot->m_job.isComplete()) {
            continue;
        }
        finalizeTextureLoad(*slot);
    }
}

void AssetManager::purgeUnreferenced() {
    for (auto it = m_textures.begin(); it != m_textures.end();) {
        TextureSlot& slot = *it->second;
        // A loading slot is still referenced by its own running job.
        if (slot.refCount() == 0 && slot.m_state != AssetState::Loading) {
            releaseSlotGpu(slot);
            it = m_textures.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_json.begin(); it != m_json.end();) {
        if (it->second->refCount() == 0) {
            it = m_json.erase(it);
        } else {
            ++it;
        }
    }
}

void AssetManager::releaseGpuTextures() {
    for (auto& [id, slot] : m_textures) {
        releaseSlotGpu(*slot);
    }
}

void AssetManager::releaseAll() {
    for (auto& [id, slot] : m_textures) {
        releaseSlotGpu(*slot);
        slot->m_value = TextureAsset{};
        slot->m_state = AssetState::Unloaded;
        m_retired.push_back(std::move(slot));
    }
    m_textures.clear();
    for (auto& [id, slot] : m_json) {
        slot->m_value = JsonAsset{};
        slot->m_state = AssetState::Unloaded;
        m_retired.push_back(std::move(slot));
    }
    m_json.clear();
}

AssetStats AssetManager::stats() const {
    AssetStats result;
    for (const auto& [id, slot] : m_textures) {
        ++result.textureCount;
        if (slot->m_value.placeholder) {
            ++result.placeholderCount;
        }
        if (slot->m_state == AssetState::Loading) {
            ++result.loadingCount;
        }
        if (slot->m_state == AssetState::Failed) {
            ++result.failedCount;
        }
        if (slot->refCount() == 0) {
            ++result.evictableCount;
        }
        result.cpuBytes += slot->m_value.image.pixels.size();
        if (slot->m_value.gpuTexture != 0) {
            ++result.liveGpuTextures;
        }
    }
    for (const auto& [id, slot] : m_json) {
        ++result.jsonCount;
        if (slot->m_state == AssetState::Failed) {
            ++result.failedCount;
        }
        if (slot->refCount() == 0) {
            ++result.evictableCount;
        }
        result.cpuBytes += slot->m_value.text.size();
    }
    return result;
}

std::unique_ptr<IAssetManager> createAssetManager(const AssetManagerDesc& desc) {
    if (desc.catalog == nullptr) {
        fa::core::logf(kLogCategory, fa::core::LogLevel::Error, "asset manager needs a catalog");
        return nullptr;
    }
    if (desc.fileSystem == nullptr) {
        fa::core::logf(kLogCategory, fa::core::LogLevel::Error, "asset manager needs a file system");
        return nullptr;
    }
    return std::make_unique<AssetManager>(desc);
}

}  // namespace fa::assets
