#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string_view>
#include <vector>

#include "fa/core/Result.hpp"
#include "fa/narrative/NarrativeTypes.hpp"

namespace fa::narrative {

/// \file
/// Read-only narrative content: NPCs, dialogue graphs and quests.
///
/// v1 loads its own document instead of extending M15's `ContentDatabase`; the
/// composition root passes JSON text, exactly like M15. Item references are checked
/// syntactically (`item.`) but not against M15 tables here.

/// \brief Narrative definitions and lookups.
class NarrativeDatabase {
public:
    NarrativeDatabase() = default;

    /// \brief Parses one document and merges it by id.
    [[nodiscard]] core::Status loadDocument(std::string_view json);

    void clear() noexcept;

    [[nodiscard]] const NpcDef* npc(const content::ContentId& id) const noexcept;
    [[nodiscard]] const DialogueDef* dialogue(const content::ContentId& id) const noexcept;
    [[nodiscard]] const QuestDef* quest(const content::ContentId& id) const noexcept;

    [[nodiscard]] std::span<const NpcDef> npcs() const noexcept { return m_npcs; }
    [[nodiscard]] std::span<const DialogueDef> dialogues() const noexcept { return m_dialogues; }
    [[nodiscard]] std::span<const QuestDef> quests() const noexcept { return m_quests; }

    [[nodiscard]] NarrativeStats stats() const noexcept;
    [[nodiscard]] int schemaVersion() const noexcept { return m_schemaVersion; }

private:
    std::vector<NpcDef> m_npcs;
    std::vector<DialogueDef> m_dialogues;
    std::vector<QuestDef> m_quests;
    std::map<content::ContentId, std::size_t> m_npcIndex;
    std::map<content::ContentId, std::size_t> m_dialogueIndex;
    std::map<content::ContentId, std::size_t> m_questIndex;
    int m_schemaVersion = 0;
};

}  // namespace fa::narrative
