#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fa/content/ContentError.hpp"
#include "fa/content/ContentTypes.hpp"
#include "fa/core/Result.hpp"

namespace fa::content {

/// \file
/// Read-only content tables plus load-time validation.
///
/// Loading takes JSON text, not a file path: M05 owns file access, and this module
/// stays linkable without a platform or a renderer. The composition root reads each
/// content document through the asset manager/file system and calls
/// `loadDocument()`; multiple calls merge by id, and duplicates are reported.
///
/// `loadDocument()` parses structurally and then runs `validate()`. All problems are
/// collected in `errors()`; the returned `Status` is the summary. On failure the
/// previously loaded entries remain (a reload can be retried after fixing the file),
/// but the content version does not advance.

/// \brief All content definitions and lookups.
class ContentDatabase {
public:
    ContentDatabase() = default;

    /// \brief Parses one document and merges its tables, then validates references.
    ///
    /// \return `kParseFailed`/`kUnsupportedSchema` for a bad document, or
    /// `kValidationFailed` when any structural/reference error was collected.
    [[nodiscard]] core::Status loadDocument(std::string_view json);

    /// \brief Re-runs reference validation against the current tables.
    ///
    /// Keeps structural errors from the last load and replaces reference errors.
    [[nodiscard]] core::Status validate();

    /// \brief Drops every definition and error, keeping the object usable.
    void clear() noexcept;

    [[nodiscard]] const ItemDef* item(const ContentId& id) const noexcept;
    [[nodiscard]] const ItemDef* item(std::string_view id) const noexcept;
    [[nodiscard]] const CropDef* crop(const ContentId& id) const noexcept;
    [[nodiscard]] const CropDef* crop(std::string_view id) const noexcept;
    [[nodiscard]] const RecipeDef* recipe(const ContentId& id) const noexcept;
    [[nodiscard]] const RecipeDef* recipe(std::string_view id) const noexcept;
    [[nodiscard]] const EnemyDef* enemy(const ContentId& id) const noexcept;
    [[nodiscard]] const EnemyDef* enemy(std::string_view id) const noexcept;
    [[nodiscard]] const LootTableDef* lootTable(const ContentId& id) const noexcept;
    [[nodiscard]] const LootTableDef* lootTable(std::string_view id) const noexcept;

    [[nodiscard]] std::span<const ItemDef> items() const noexcept { return m_items; }
    [[nodiscard]] std::span<const CropDef> crops() const noexcept { return m_crops; }
    [[nodiscard]] std::span<const RecipeDef> recipes() const noexcept { return m_recipes; }
    [[nodiscard]] std::span<const EnemyDef> enemies() const noexcept { return m_enemies; }
    [[nodiscard]] std::span<const LootTableDef> lootTables() const noexcept { return m_lootTables; }

    /// \brief Items whose `tags` contain \p tag, in insertion order.
    [[nodiscard]] std::vector<const ItemDef*> itemsWithTag(std::string_view tag) const;
    [[nodiscard]] std::vector<const ItemDef*> itemsWithCategory(std::string_view category) const;

    /// \brief Every collected error, structural first, then reference errors.
    [[nodiscard]] std::span<const ContentError> errors() const noexcept { return m_allErrors; }

    [[nodiscard]] ContentStats stats() const noexcept;
    [[nodiscard]] int schemaVersion() const noexcept { return m_schemaVersion; }
    [[nodiscard]] std::uint64_t contentVersion() const noexcept { return m_contentVersion; }

private:
    std::vector<ItemDef> m_items;
    std::vector<CropDef> m_crops;
    std::vector<RecipeDef> m_recipes;
    std::vector<EnemyDef> m_enemies;
    std::vector<LootTableDef> m_lootTables;

    std::map<ContentId, std::size_t> m_itemIndex;
    std::map<ContentId, std::size_t> m_cropIndex;
    std::map<ContentId, std::size_t> m_recipeIndex;
    std::map<ContentId, std::size_t> m_enemyIndex;
    std::map<ContentId, std::size_t> m_lootIndex;

    std::vector<ContentError> m_parseErrors;
    std::vector<ContentError> m_referenceErrors;
    std::vector<ContentError> m_allErrors;

    int m_schemaVersion = 0;
    std::uint64_t m_contentVersion = 0;
};

}  // namespace fa::content
