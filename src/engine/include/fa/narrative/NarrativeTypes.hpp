#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "fa/content/ContentId.hpp"
#include "fa/content/ContentTypes.hpp"
#include "fa/inventory/ItemStack.hpp"
#include "fa/scene/EntityId.hpp"

namespace fa::narrative {

/// \file
/// Value types for M23: NPCs, dialogue graphs, conditions/actions, quests and events.
///
/// Conditions are a whitelist of typed comparisons instead of a free-form script.
/// That keeps save data and authoring predictable and avoids turning the narrative
/// module into an expression engine.

namespace narrative_error {

/// \brief M23 owns the 7200 block.
inline constexpr int kNotLoaded = 7200;
inline constexpr int kUnknownNpc = 7201;
inline constexpr int kUnknownDialogue = 7202;
inline constexpr int kUnknownQuest = 7203;
inline constexpr int kUnknownNode = 7204;
inline constexpr int kDialogueInactive = 7205;
inline constexpr int kInvalidChoice = 7206;
inline constexpr int kConditionFailed = 7207;
inline constexpr int kNoInventory = 7208;
inline constexpr int kMissingItem = 7209;
inline constexpr int kAlreadyActive = 7210;
inline constexpr int kAlreadyCompleted = 7211;
inline constexpr int kNotActive = 7212;
inline constexpr int kInvalidDocument = 7213;
inline constexpr int kDuplicateId = 7214;
inline constexpr int kUnsupportedSchema = 7215;
inline constexpr int kAlreadyGifted = 7216;

}  // namespace narrative_error

/// \brief One typed condition. All conditions on a node/choice must pass.
struct DialogueCondition {
    enum class Kind : std::uint8_t {
        Always = 0,
        AffinityAtLeast,
        QuestActive,
        QuestCompleted,
        HasItem,
        Flag,
        Season,
        Count,
    };

    Kind kind = Kind::Always;
    /// \brief NPC, quest, item or flag id, depending on \ref kind.
    content::ContentId id;
    int value = 0;
    content::Season season = content::Season::Spring;
};

/// \brief One side effect of a dialogue node or choice.
struct DialogueAction {
    enum class Kind : std::uint8_t {
        GiveItem = 0,
        AddAffinity,
        StartQuest,
        CompleteQuest,
        SetFlag,
        OpenShop,
        Count,
    };

    Kind kind = Kind::GiveItem;
    /// \brief Item, NPC, quest or flag id, depending on \ref kind.
    content::ContentId id;
    int amount = 0;
    /// \brief Shop id for `OpenShop`, or a free-form note.
    std::string text;
};

/// \brief One selectable line.
struct DialogueChoice {
    std::string text;
    std::string next;
    std::vector<DialogueCondition> conditions;
    std::vector<DialogueAction> actions;
};

/// \brief One dialogue node. `next` empty ends the dialogue when there are no choices.
struct DialogueNode {
    std::string id;
    std::string speaker;
    std::string text;
    std::string next;
    std::vector<DialogueCondition> conditions;
    std::vector<DialogueAction> actions;
    std::vector<DialogueChoice> choices;
};

/// \brief One dialogue graph owned by an NPC.
struct DialogueDef {
    content::ContentId id;
    content::ContentId npc;
    std::string startNode;
    std::vector<DialogueNode> nodes;
};

/// \brief One NPC definition.
struct NpcDef {
    content::ContentId id;
    std::string name;
    std::string portrait;
    std::vector<content::ContentId> likedItems;
    std::string shop;
};

/// \brief What a quest objective listens for.
enum class QuestObjectiveKind : std::uint8_t {
    Collect = 0,
    Deliver,
    Kill,
    Plant,
    Talk,
    Count,
};

/// \brief One quest objective.
struct QuestObjective {
    QuestObjectiveKind kind = QuestObjectiveKind::Collect;
    content::ContentId target;
    int required = 1;
};

/// \brief One quest definition.
struct QuestDef {
    content::ContentId id;
    std::string title;
    std::string description;
    std::vector<QuestObjective> objectives;
    std::vector<inventory::ItemStack> rewards;
    int goldReward = 0;
    std::vector<DialogueCondition> conditions;
};

/// \brief Runtime progress for one quest.
struct QuestState {
    content::ContentId id;
    bool active = false;
    bool completed = false;
    std::vector<int> progress;
};

/// \brief Save-facing runtime state.
///
/// The active dialogue graph is intentionally not part of the snapshot: a load ends
/// any dialogue and the player talks to the NPC again. Affinity, flags, daily caps and
/// quest progress are the cross-session facts conditions and rewards depend on.
struct NarrativeSnapshot {
    std::map<content::ContentId, int> affinity;
    std::map<content::ContentId, bool> flags;
    std::map<content::ContentId, bool> talkedToday;
    std::map<content::ContentId, bool> giftedToday;
    std::map<content::ContentId, QuestState> quests;
};

/// \brief One gameplay event a quest can consume.
struct QuestEvent {
    QuestObjectiveKind kind = QuestObjectiveKind::Collect;
    content::ContentId target;
    int amount = 1;
};

/// \brief Recorded narrative transitions for the app and M27.
enum class NarrativeEventKind : std::uint8_t {
    DialogueStarted = 0,
    NodeEntered,
    ChoiceSelected,
    DialogueEnded,
    GiftGiven,
    QuestStarted,
    QuestProgress,
    QuestCompleted,
    ActionApplied,
    Count,
};

/// \brief Stable names for logs and tests.
[[nodiscard]] std::string_view toString(NarrativeEventKind kind) noexcept;
[[nodiscard]] std::string_view toString(QuestObjectiveKind kind) noexcept;
[[nodiscard]] std::string_view toString(DialogueCondition::Kind kind) noexcept;
[[nodiscard]] std::string_view toString(DialogueAction::Kind kind) noexcept;

/// \brief One recorded narrative transition.
struct NarrativeEvent {
    NarrativeEventKind kind = NarrativeEventKind::NodeEntered;
    content::ContentId dialogue;
    content::ContentId npc;
    content::ContentId quest;
    content::ContentId item;
    int amount = 0;
    std::string text;
};

/// \brief Counters for the smoke summary and M27.
struct NarrativeStats {
    std::size_t npcs = 0;
    std::size_t dialogues = 0;
    std::size_t quests = 0;
    std::size_t activeQuests = 0;
    std::size_t completedQuests = 0;
    int affinityTotal = 0;
    std::uint64_t dialoguesStarted = 0;
    std::uint64_t dialoguesEnded = 0;
    std::uint64_t nodesEntered = 0;
    std::uint64_t choices = 0;
    std::uint64_t gifts = 0;
    std::uint64_t questsStarted = 0;
    std::uint64_t questsCompleted = 0;
    std::size_t events = 0;
    bool dialogueActive = false;
};

}  // namespace fa::narrative
