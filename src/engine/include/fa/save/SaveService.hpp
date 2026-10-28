#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fa/core/Result.hpp>
#include <fa/save/SaveError.hpp>
#include <fa/save/SaveTypes.hpp>

namespace fa::platform {
class IFileSystem;
}

namespace fa::save {

/// \brief Save slot operations used by the composition root and M25.
class ISaveService {
public:
    virtual ~ISaveService() = default;

    [[nodiscard]] virtual core::Status save(std::string_view slot, const SaveContext& context) = 0;
    [[nodiscard]] virtual core::Result<SaveFile> load(std::string_view slot) = 0;
    virtual void apply(const SaveFile& file, SaveContext& context) = 0;
    [[nodiscard]] virtual std::vector<SaveMetadata> listSlots() = 0;
    [[nodiscard]] virtual core::Status deleteSlot(std::string_view slot) = 0;
};

/// \brief JSON save slots with atomic writes, `.bak` recovery and a migration chain.
///
/// File layout: `<directory>/<slot>.json` plus `<slot>.json.bak`. The primary is
/// written with IFileSystem::writeTextAtomic (`.tmp` + rename); the previous primary
/// is first copied to the backup. `load()` falls back to the backup when the primary
/// is missing or unreadable.
class SaveService final : public ISaveService {
public:
    using Migration = std::function<core::Status(SaveArchive&)>;

    /// \param directory Root directory for slots; created on first use.
    /// \param currentVersion Overridable for tests and future schema bumps.
    explicit SaveService(platform::IFileSystem& files,
                         std::string directory = "saves",
                         int currentVersion = kSaveVersion);

    [[nodiscard]] core::Status save(std::string_view slot, const SaveContext& context) override;
    [[nodiscard]] core::Result<SaveFile> load(std::string_view slot) override;
    void apply(const SaveFile& file, SaveContext& context) override;
    [[nodiscard]] std::vector<SaveMetadata> listSlots() override;
    [[nodiscard]] core::Status deleteSlot(std::string_view slot) override;

    /// \brief Registers a migration from `fromVersion` to `fromVersion + 1`.
    void registerMigration(int fromVersion, Migration migration);

    /// \brief Letters, digits, `_` and `-`, length 1..64.
    [[nodiscard]] static bool isValidSlotName(std::string_view slot) noexcept;

    [[nodiscard]] std::string slotPath(std::string_view slot) const;
    [[nodiscard]] std::string backupPath(std::string_view slot) const;
    [[nodiscard]] int currentVersion() const noexcept { return m_currentVersion; }

private:
    [[nodiscard]] core::Result<SaveFile> parseSave(std::string_view text, bool fromBackup) const;

    platform::IFileSystem& m_files;
    std::string m_directory;
    int m_currentVersion = kSaveVersion;
    std::vector<std::pair<int, Migration>> m_migrations;
};

/// \brief Registers the shipped migration chain (currently v1 → v2).
///
/// The migration only creates missing containers; it never overwrites values, so
/// it is safe to run on partially written or already-normalised saves.
void registerDefaultMigrations(SaveService& service);

}  // namespace fa::save
