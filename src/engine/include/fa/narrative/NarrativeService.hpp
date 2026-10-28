#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fa/core/Result.hpp"
#include "fa/narrative/NarrativeDatabase.hpp"
#include "fa/narrative/NarrativeTypes.hpp"

namespace fa::inventory {
class Inventory;
}

namespace fa::narrative {

/// \file
/// Dialogue, affinity and quest runtime for M23.
///
/// Pure logic over a borrowed database and inventory; no ECS, UI or file access.
/// Conditions are typed values rather than a scripting language. The composition
/// root starts dialogues, routes choices and reports gameplay events.

/// \brief NPC affinity, dialogue graph execution and quest progress.
class NarrativeService {
public:
    explicit NarrativeService(const NarrativeDatabase* database = nullptr);

    void setDatabase(const NarrativeDatabase* database) noexcept { m_database = database; }
    [[nodiscard]] const NarrativeDatabase* database() const noexcept { return m_database; }
    void setInventory(inventory::Inventory* inventory) noexcept { m_inventory = inventory; }
    void setSeason(content::Season season) noexcept { m_season = season; }
    [[nodiscard]] content::Season season() const noexcept { return m_season; }

    [[nodiscard]] int affinity(const content::ContentId& npc) const noexcept;

    /// \brief Gives one liked/normal item; consumes it and raises affinity once per day.
    [[nodiscard]] core::Status
    giveGift(const content::ContentId& npc, const content::ContentId& item, inventory::Inventory& inventory);

    /// \brief Resets the daily talk/gift caps.
    void onDayChanged() noexcept;

    [[nodiscard]] core::Status startDialogue(const content::ContentId& dialogue, scene::EntityId speaker = {});
    [[nodiscard]] bool dialogueActive() const noexcept { return m_active; }
    [[nodiscard]] const DialogueDef* activeDialogue() const noexcept;
    [[nodiscard]] const DialogueNode* currentNode() const noexcept;

    /// \brief Display indices of choices whose conditions currently pass.
    [[nodiscard]] std::vector<std::size_t> availableChoices() const;

    [[nodiscard]] core::Status choose(std::size_t displayedIndex);
    [[nodiscard]] core::Status advance();
    [[nodiscard]] core::Status endDialogue();

    [[nodiscard]] core::Status startQuest(const content::ContentId& quest);
    [[nodiscard]] core::Status completeQuest(const content::ContentId& quest);
    void reportEvent(const QuestEvent& event);

    [[nodiscard]] const QuestState* questState(const content::ContentId& quest) const noexcept;
    [[nodiscard]] bool isQuestActive(const content::ContentId& quest) const noexcept;
    [[nodiscard]] bool isQuestCompleted(const content::ContentId& quest) const noexcept;
    [[nodiscard]] std::vector<QuestState> activeQuests() const;
    /// \brief Every tracked quest, including completed ones, for the quest log.
    [[nodiscard]] std::vector<QuestState> questStates() const;

    [[nodiscard]] std::span<const NarrativeEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }

    [[nodiscard]] NarrativeStats stats() const noexcept;

    /// \brief Copy of the state a save must persist.
    [[nodiscard]] NarrativeSnapshot snapshot() const;

    /// \brief Replaces affinity/flags/daily caps/quests and ends any active dialogue.
    ///
    /// Totals and the content database stay untouched so debug counters keep the
    /// current process history; only the save-facing runtime state is replaced.
    void restore(const NarrativeSnapshot& snapshot);

    void clear() noexcept;

private:
    [[nodiscard]] bool evaluate(const DialogueCondition& condition) const noexcept;
    void apply(const DialogueAction& action);
    void enterNode(std::string_view nodeId);
    void record(NarrativeEventKind kind, const content::ContentId& item = {}, int amount = 0, std::string text = {});

    const NarrativeDatabase* m_database = nullptr;
    inventory::Inventory* m_inventory = nullptr;
    content::Season m_season = content::Season::Spring;
    std::map<content::ContentId, int> m_affinity;
    std::map<content::ContentId, bool> m_flags;
    std::map<content::ContentId, bool> m_talkedToday;
    std::map<content::ContentId, bool> m_giftedToday;
    std::map<content::ContentId, QuestState> m_quests;
    bool m_active = false;
    content::ContentId m_dialogue;
    content::ContentId m_npc;
    scene::EntityId m_speaker{};
    std::string m_node;
    std::vector<NarrativeEvent> m_events;
    NarrativeStats m_totals;
};

}  // namespace fa::narrative
