#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <fa/save/SaveArchive.hpp>

namespace fa::save {

/// \brief Current save schema. Bump this when a migration is required.
///
/// v2 only normalises the optional P0-3/P2-1 sections (`world.objects`,
/// `world.debris`, `world.ai`, `quests.*`, `economy.*`) so old files gain empty
/// containers. Readers already treat missing sections as empty.
inline constexpr int kSaveVersion = 2;

/// \brief File header magic; distinguishes a save from arbitrary JSON.
inline constexpr std::string_view kSaveMagic = "FA_SAVE";

/// \brief Header shown by the load menu and written before the world sections.
struct SaveMetadata {
    int saveVersion = kSaveVersion;
    std::string gameVersion;
    std::string slotName;
    std::int64_t timestampUtc = 0;
    std::string sceneId;
    int inGameDay = 0;
    double playSeconds = 0.0;
    /// \brief False only for metadata entries produced by a corrupt file.
    bool readable = true;
};

/// \brief Everything the caller wants to persist in one save.
struct SaveContext {
    std::string slotName;
    std::string sceneId;
    int inGameDay = 0;
    double playSeconds = 0.0;
    SaveArchive world = SaveArchive::object();
    SaveArchive player = SaveArchive::object();
    SaveArchive inventories = SaveArchive::object();
    SaveArchive quests = SaveArchive::object();
    SaveArchive economy = SaveArchive::object();
};

/// \brief Parsed save plus the replay-relevant sections.
struct SaveFile {
    SaveMetadata meta;
    SaveArchive world;
    SaveArchive player;
    SaveArchive inventories;
    SaveArchive quests;
    SaveArchive economy;
    /// \brief True when `load()` recovered the data from the `.bak` file.
    bool recoveredFromBackup = false;
};

}  // namespace fa::save
