#include "fa/save/SaveService.hpp"

#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>

#include <fa/core/Version.hpp>
#include <fa/platform/FileSystem.hpp>

namespace fa::save {
namespace {

void readMetadata(const SaveArchive& archive, SaveMetadata& metadata) {
    static_cast<void>(archive.getInt("saveVersion", metadata.saveVersion));
    static_cast<void>(archive.getString("gameVersion", metadata.gameVersion));
    static_cast<void>(archive.getString("slotName", metadata.slotName));
    static_cast<void>(archive.getInt64("timestampUtc", metadata.timestampUtc));
    static_cast<void>(archive.getString("sceneId", metadata.sceneId));
    static_cast<void>(archive.getInt("inGameDay", metadata.inGameDay));
    static_cast<void>(archive.getDouble("playSeconds", metadata.playSeconds));
}

void writeMetadata(const SaveMetadata& metadata, SaveArchive& archive) {
    archive.setInt("saveVersion", metadata.saveVersion);
    archive.setString("gameVersion", metadata.gameVersion);
    archive.setString("slotName", metadata.slotName);
    archive.setInt64("timestampUtc", metadata.timestampUtc);
    archive.setString("sceneId", metadata.sceneId);
    archive.setInt("inGameDay", metadata.inGameDay);
    archive.setDouble("playSeconds", metadata.playSeconds);
}

}  // namespace

SaveService::SaveService(platform::IFileSystem& files, std::string directory, int currentVersion)
    : m_files(files), m_directory(std::move(directory)),
      m_currentVersion(currentVersion > 0 ? currentVersion : kSaveVersion) {}

core::Status SaveService::save(std::string_view slot, const SaveContext& context) {
    if (!isValidSlotName(slot)) {
        return core::errorStatus(save_error::kInvalidSlot, "slot names must be 1..64 of [A-Za-z0-9_-]");
    }
    if (const core::Status created = m_files.createDirectories(m_directory); created.isError()) {
        return core::errorStatus(save_error::kIoFailure, created.error().message);
    }

    SaveMetadata metadata;
    metadata.saveVersion = m_currentVersion;
    metadata.gameVersion = std::string(core::engineVersion());
    metadata.slotName = std::string(slot);
    metadata.timestampUtc = static_cast<std::int64_t>(std::time(nullptr));
    metadata.sceneId = context.sceneId;
    metadata.inGameDay = context.inGameDay;
    metadata.playSeconds = context.playSeconds;

    SaveArchive root = SaveArchive::object();
    root.setString("magic", kSaveMagic);
    root.setInt("version", m_currentVersion);
    SaveArchive metaArchive = SaveArchive::object();
    writeMetadata(metadata, metaArchive);
    root.setChild("meta", std::move(metaArchive));
    root.setChild("world", context.world);
    root.setChild("player", context.player);
    root.setChild("inventories", context.inventories);
    root.setChild("quests", context.quests);
    root.setChild("economy", context.economy);

    const std::string path = slotPath(slot);
    const std::string backup = backupPath(slot);
    if (m_files.exists(path)) {
        const core::Result<std::string> previous = m_files.readText(path);
        if (previous.isOk()) {
            const core::Status backedUp = m_files.writeTextAtomic(backup, previous.value());
            if (backedUp.isError()) {
                return core::errorStatus(save_error::kBackupFailed, backedUp.error().message);
            }
        }
    }

    const core::Status written = m_files.writeTextAtomic(path, root.dump());
    if (written.isError()) {
        return core::errorStatus(save_error::kIoFailure, written.error().message);
    }
    return core::okStatus();
}

core::Result<SaveFile> SaveService::load(std::string_view slot) {
    if (!isValidSlotName(slot)) {
        return core::Result<SaveFile>::err(save_error::kInvalidSlot, "slot names must be 1..64 of [A-Za-z0-9_-]");
    }

    const std::string path = slotPath(slot);
    const core::Result<std::string> primary = m_files.readText(path);
    core::Result<SaveFile> primaryResult =
        core::Result<SaveFile>::err(save_error::kSlotNotFound, "slot does not exist");
    if (primary.isOk()) {
        primaryResult = parseSave(primary.value(), false);
        if (primaryResult.isOk()) {
            return primaryResult;
        }
        /// A newer or unmigratable save will not become readable from its backup.
        if (primaryResult.error().code == save_error::kUnsupportedVersion ||
            primaryResult.error().code == save_error::kMigrationFailed) {
            return primaryResult;
        }
    }

    const std::string backup = backupPath(slot);
    if (m_files.exists(backup)) {
        const core::Result<std::string> backupText = m_files.readText(backup);
        if (backupText.isOk()) {
            core::Result<SaveFile> parsed = parseSave(backupText.value(), true);
            if (parsed.isOk()) {
                return parsed;
            }
        }
    }

    /// Preserve the primary's specific parse/corruption error when there is no
    /// usable backup; do not collapse everything to kCorruptSave.
    return primaryResult;
}

void SaveService::apply(const SaveFile& file, SaveContext& context) {
    context.slotName = file.meta.slotName;
    context.sceneId = file.meta.sceneId;
    context.inGameDay = file.meta.inGameDay;
    context.playSeconds = file.meta.playSeconds;
    context.world = file.world;
    context.player = file.player;
    context.inventories = file.inventories;
    context.quests = file.quests;
    context.economy = file.economy;
}

std::vector<SaveMetadata> SaveService::listSlots() {
    std::vector<SaveMetadata> result;
    static_cast<void>(m_files.createDirectories(m_directory));
    const core::Result<std::vector<std::string>> listed = m_files.listDirectory(m_directory);
    if (listed.isError()) {
        return result;
    }

    for (const std::string& name : listed.value()) {
        if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".json") != 0) {
            continue;
        }
        const std::string slot = name.substr(0, name.size() - 5);
        SaveMetadata metadata;
        metadata.slotName = slot;
        const core::Result<std::string> text = m_files.readText(slotPath(slot));
        if (text.isOk()) {
            core::Result<SaveFile> parsed = parseSave(text.value(), false);
            if (parsed.isOk()) {
                metadata = parsed.value().meta;
                metadata.readable = true;
            } else {
                metadata.readable = false;
            }
        } else {
            metadata.readable = false;
        }
        result.push_back(std::move(metadata));
    }
    return result;
}

core::Status SaveService::deleteSlot(std::string_view slot) {
    if (!isValidSlotName(slot)) {
        return core::errorStatus(save_error::kInvalidSlot, "slot names must be 1..64 of [A-Za-z0-9_-]");
    }

    bool removed = false;
    const std::string path = slotPath(slot);
    if (m_files.exists(path)) {
        const core::Status status = m_files.remove(path);
        if (status.isError()) {
            return core::errorStatus(save_error::kIoFailure, status.error().message);
        }
        removed = true;
    }

    const std::string backup = backupPath(slot);
    if (m_files.exists(backup)) {
        const core::Status status = m_files.remove(backup);
        if (status.isError()) {
            return core::errorStatus(save_error::kIoFailure, status.error().message);
        }
        removed = true;
    }

    return removed ? core::okStatus() : core::errorStatus(save_error::kSlotNotFound, "slot does not exist");
}

void SaveService::registerMigration(int fromVersion, Migration migration) {
    if (migration) {
        m_migrations.emplace_back(fromVersion, std::move(migration));
    }
}

namespace {

void ensureObjectChild(SaveArchive& parent, std::string_view key) {
    if (!parent.child(key).isObject()) {
        parent.setChild(key, SaveArchive::object());
    }
}

void ensureArrayChild(SaveArchive& parent, std::string_view key) {
    if (!parent.child(key).isArray()) {
        parent.setChild(key, SaveArchive::array());
    }
}

/// \brief v1 → v2: create the containers added by P0-3/P2-1 without touching values.
core::Status normalizeV1ToV2(SaveArchive& root) {
    SaveArchive world = root.child("world");
    if (!world.isObject()) {
        world = SaveArchive::object();
    }
    ensureArrayChild(world, "objects");
    ensureArrayChild(world, "debris");
    ensureArrayChild(world, "loot");
    // P0-3 briefly wrote one object here; keep it, otherwise start an empty array.
    if (world.child("ai").isNull()) {
        world.setChild("ai", SaveArchive::array());
    }
    root.setChild("world", std::move(world));

    SaveArchive player = root.child("player");
    if (!player.isObject()) {
        player = SaveArchive::object();
    }
    root.setChild("player", std::move(player));

    SaveArchive quests = root.child("quests");
    if (!quests.isObject()) {
        quests = SaveArchive::object();
    }
    ensureObjectChild(quests, "affinity");
    ensureObjectChild(quests, "flags");
    ensureObjectChild(quests, "talkedToday");
    ensureObjectChild(quests, "giftedToday");
    ensureArrayChild(quests, "quests");
    root.setChild("quests", std::move(quests));

    SaveArchive economy = root.child("economy");
    if (!economy.isObject()) {
        economy = SaveArchive::object();
    }
    ensureArrayChild(economy, "shipping");
    ensureArrayChild(economy, "jobs");
    ensureArrayChild(economy, "stock");
    root.setChild("economy", std::move(economy));

    SaveArchive inventories = root.child("inventories");
    if (!inventories.isObject()) {
        inventories = SaveArchive::object();
    }
    root.setChild("inventories", std::move(inventories));
    return core::okStatus();
}

}  // namespace

void registerDefaultMigrations(SaveService& service) {
    service.registerMigration(1, normalizeV1ToV2);
}

bool SaveService::isValidSlotName(std::string_view slot) noexcept {
    if (slot.empty() || slot.size() > 64) {
        return false;
    }
    for (const char character : slot) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value) == 0 && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

std::string SaveService::slotPath(std::string_view slot) const {
    return m_directory + "/" + std::string(slot) + ".json";
}

std::string SaveService::backupPath(std::string_view slot) const {
    return slotPath(slot) + ".bak";
}

core::Result<SaveFile> SaveService::parseSave(std::string_view text, bool fromBackup) const {
    core::Result<SaveArchive> parsed = SaveArchive::parse(text);
    if (parsed.isError()) {
        return core::Result<SaveFile>::err(save_error::kParseFailed, parsed.error().message);
    }
    SaveArchive root = std::move(parsed).take();

    std::string magic;
    if (!root.getString("magic", magic) || magic != kSaveMagic) {
        return core::Result<SaveFile>::err(save_error::kCorruptSave, "missing save header");
    }

    int version = 0;
    if (!root.getInt("version", version) || version <= 0) {
        return core::Result<SaveFile>::err(save_error::kCorruptSave, "missing save version");
    }
    if (version > m_currentVersion) {
        return core::Result<SaveFile>::err(save_error::kUnsupportedVersion,
                                           "save version " + std::to_string(version) + " is newer than " +
                                               std::to_string(m_currentVersion));
    }

    for (int from = version; from < m_currentVersion; ++from) {
        const Migration* migration = nullptr;
        for (const auto& entry : m_migrations) {
            if (entry.first == from) {
                migration = &entry.second;
                break;
            }
        }
        if (migration == nullptr) {
            return core::Result<SaveFile>::err(save_error::kMigrationFailed,
                                               "no migration from version " + std::to_string(from));
        }
        const core::Status status = (*migration)(root);
        if (status.isError()) {
            return core::Result<SaveFile>::err(save_error::kMigrationFailed, status.error().message);
        }
    }
    if (version < m_currentVersion) {
        root.setInt("version", m_currentVersion);
    }

    SaveFile file;
    readMetadata(root.child("meta"), file.meta);
    file.meta.saveVersion = m_currentVersion;
    file.world = root.child("world");
    file.player = root.child("player");
    file.inventories = root.child("inventories");
    file.quests = root.child("quests");
    file.economy = root.child("economy");
    file.recoveredFromBackup = fromBackup;
    return core::Result<SaveFile>::ok(std::move(file));
}

}  // namespace fa::save
