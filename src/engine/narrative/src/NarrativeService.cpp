#include "fa/narrative/NarrativeService.hpp"

#include <algorithm>
#include <utility>

#include "fa/inventory/Inventory.hpp"

namespace fa::narrative {

NarrativeService::NarrativeService(const NarrativeDatabase* database) : m_database(database) {}

int NarrativeService::affinity(const content::ContentId& npc) const noexcept {
    const auto found = m_affinity.find(npc);
    return found != m_affinity.end() ? found->second : 0;
}

core::Status NarrativeService::giveGift(const content::ContentId& npc,
                                        const content::ContentId& item,
                                        inventory::Inventory& inventory) {
    if (m_database == nullptr) {
        return core::errorStatus(narrative_error::kNotLoaded, "narrative database is not loaded");
    }
    const NpcDef* definition = m_database->npc(npc);
    if (definition == nullptr) {
        return core::errorStatus(narrative_error::kUnknownNpc, "unknown npc");
    }
    if (m_giftedToday.find(npc) != m_giftedToday.end()) {
        return core::errorStatus(narrative_error::kAlreadyGifted, "npc already received a gift today");
    }
    if (inventory.countOf(item) <= 0) {
        return core::errorStatus(narrative_error::kMissingItem, "gift item is not in the inventory");
    }
    const core::Result<inventory::ItemStack> removed = inventory.remove(item, 1);
    if (removed.isError()) {
        return core::errorStatus(removed.error().code, removed.error().message);
    }

    const bool liked =
        std::find(definition->likedItems.begin(), definition->likedItems.end(), item) != definition->likedItems.end();
    const int gain = liked ? 3 : 1;
    m_affinity[npc] += gain;
    m_giftedToday[npc] = true;
    ++m_totals.gifts;
    record(NarrativeEventKind::GiftGiven, item, gain, definition->name);
    m_events.back().npc = npc;
    return core::okStatus();
}

void NarrativeService::onDayChanged() noexcept {
    m_talkedToday.clear();
    m_giftedToday.clear();
}

core::Status NarrativeService::startDialogue(const content::ContentId& dialogue, scene::EntityId speaker) {
    if (m_database == nullptr) {
        return core::errorStatus(narrative_error::kNotLoaded, "narrative database is not loaded");
    }
    const DialogueDef* definition = m_database->dialogue(dialogue);
    if (definition == nullptr) {
        return core::errorStatus(narrative_error::kUnknownDialogue, "unknown dialogue");
    }
    if (m_active) {
        static_cast<void>(endDialogue());
    }

    m_active = true;
    m_dialogue = dialogue;
    m_npc = definition->npc;
    m_speaker = speaker;
    m_node = definition->startNode;
    ++m_totals.dialoguesStarted;
    record(NarrativeEventKind::DialogueStarted, {}, 0, definition->startNode);

    if (m_talkedToday.find(m_npc) == m_talkedToday.end()) {
        m_talkedToday[m_npc] = true;
        ++m_affinity[m_npc];
        QuestEvent talk;
        talk.kind = QuestObjectiveKind::Talk;
        talk.target = m_npc;
        talk.amount = 1;
        reportEvent(talk);
    }

    enterNode(m_node);
    return core::okStatus();
}

const DialogueDef* NarrativeService::activeDialogue() const noexcept {
    if (!m_active || m_database == nullptr) {
        return nullptr;
    }
    return m_database->dialogue(m_dialogue);
}

const DialogueNode* NarrativeService::currentNode() const noexcept {
    const DialogueDef* definition = activeDialogue();
    if (definition == nullptr) {
        return nullptr;
    }
    for (const DialogueNode& node : definition->nodes) {
        if (node.id == m_node) {
            return &node;
        }
    }
    return nullptr;
}

std::vector<std::size_t> NarrativeService::availableChoices() const {
    std::vector<std::size_t> result;
    const DialogueNode* node = currentNode();
    if (node == nullptr) {
        return result;
    }
    for (std::size_t index = 0; index < node->choices.size(); ++index) {
        const DialogueChoice& choice = node->choices[index];
        const bool visible = std::all_of(choice.conditions.begin(),
                                         choice.conditions.end(),
                                         [this](const DialogueCondition& condition) { return evaluate(condition); });
        if (visible) {
            result.push_back(index);
        }
    }
    return result;
}

core::Status NarrativeService::choose(std::size_t displayedIndex) {
    if (!m_active) {
        return core::errorStatus(narrative_error::kDialogueInactive, "no dialogue is active");
    }
    const DialogueNode* node = currentNode();
    if (node == nullptr) {
        static_cast<void>(endDialogue());
        return core::errorStatus(narrative_error::kUnknownNode, "current dialogue node is missing");
    }
    const std::vector<std::size_t> available = availableChoices();
    if (displayedIndex >= available.size()) {
        return core::errorStatus(narrative_error::kInvalidChoice, "choice index is out of range");
    }
    const DialogueChoice& choice = node->choices[available[displayedIndex]];
    for (const DialogueAction& action : choice.actions) {
        apply(action);
    }
    ++m_totals.choices;
    record(NarrativeEventKind::ChoiceSelected, {}, 0, choice.text);
    if (!choice.next.empty()) {
        enterNode(choice.next);
    } else {
        static_cast<void>(endDialogue());
    }
    return core::okStatus();
}

core::Status NarrativeService::advance() {
    if (!m_active) {
        return core::errorStatus(narrative_error::kDialogueInactive, "no dialogue is active");
    }
    const DialogueNode* node = currentNode();
    if (node == nullptr) {
        static_cast<void>(endDialogue());
        return core::errorStatus(narrative_error::kUnknownNode, "current dialogue node is missing");
    }
    if (!availableChoices().empty()) {
        return core::okStatus();
    }
    if (!node->next.empty()) {
        enterNode(node->next);
    } else {
        static_cast<void>(endDialogue());
    }
    return core::okStatus();
}

core::Status NarrativeService::endDialogue() {
    if (!m_active) {
        return core::okStatus();
    }
    record(NarrativeEventKind::DialogueEnded, {}, 0, m_node);
    ++m_totals.dialoguesEnded;
    m_active = false;
    m_dialogue = content::ContentId{};
    m_npc = content::ContentId{};
    m_speaker = scene::EntityId{};
    m_node.clear();
    return core::okStatus();
}

core::Status NarrativeService::startQuest(const content::ContentId& quest) {
    if (m_database == nullptr) {
        return core::errorStatus(narrative_error::kNotLoaded, "narrative database is not loaded");
    }
    const QuestDef* definition = m_database->quest(quest);
    if (definition == nullptr) {
        return core::errorStatus(narrative_error::kUnknownQuest, "unknown quest");
    }
    const auto found = m_quests.find(quest);
    if (found != m_quests.end()) {
        if (found->second.completed) {
            return core::errorStatus(narrative_error::kAlreadyCompleted, "quest is already completed");
        }
        if (found->second.active) {
            return core::errorStatus(narrative_error::kAlreadyActive, "quest is already active");
        }
    }
    const bool allowed = std::all_of(definition->conditions.begin(),
                                     definition->conditions.end(),
                                     [this](const DialogueCondition& condition) { return evaluate(condition); });
    if (!allowed) {
        return core::errorStatus(narrative_error::kConditionFailed, "quest conditions are not met");
    }

    QuestState state;
    state.id = quest;
    state.active = true;
    state.progress.assign(definition->objectives.size(), 0);
    m_quests[quest] = std::move(state);
    ++m_totals.questsStarted;
    record(NarrativeEventKind::QuestStarted, {}, 0, definition->title);
    m_events.back().quest = quest;
    return core::okStatus();
}

core::Status NarrativeService::completeQuest(const content::ContentId& quest) {
    if (m_database == nullptr) {
        return core::errorStatus(narrative_error::kNotLoaded, "narrative database is not loaded");
    }
    const QuestDef* definition = m_database->quest(quest);
    if (definition == nullptr) {
        return core::errorStatus(narrative_error::kUnknownQuest, "unknown quest");
    }
    const auto found = m_quests.find(quest);
    if (found != m_quests.end() && found->second.completed) {
        return core::errorStatus(narrative_error::kAlreadyCompleted, "quest is already completed");
    }
    if (found == m_quests.end() || !found->second.active) {
        return core::errorStatus(narrative_error::kNotActive, "quest is not active");
    }

    if (m_inventory != nullptr) {
        for (const inventory::ItemStack& reward : definition->rewards) {
            static_cast<void>(m_inventory->add(reward));
        }
    }
    found->second.active = false;
    found->second.completed = true;
    ++m_totals.questsCompleted;
    record(NarrativeEventKind::QuestCompleted, {}, definition->goldReward, definition->title);
    m_events.back().quest = quest;
    return core::okStatus();
}

void NarrativeService::reportEvent(const QuestEvent& event) {
    if (m_database == nullptr) {
        return;
    }
    std::vector<content::ContentId> completed;
    for (auto& [questId, state] : m_quests) {
        if (!state.active) {
            continue;
        }
        const QuestDef* definition = m_database->quest(questId);
        if (definition == nullptr) {
            continue;
        }
        bool changed = false;
        for (std::size_t index = 0; index < definition->objectives.size(); ++index) {
            const QuestObjective& objective = definition->objectives[index];
            if (objective.kind != event.kind || objective.target != event.target) {
                continue;
            }
            const int next = std::min(objective.required, state.progress[index] + event.amount);
            if (next != state.progress[index]) {
                state.progress[index] = next;
                changed = true;
            }
        }
        if (changed) {
            record(NarrativeEventKind::QuestProgress, event.target, event.amount);
            m_events.back().quest = questId;
            bool done = true;
            for (std::size_t index = 0; index < definition->objectives.size(); ++index) {
                if (state.progress[index] < definition->objectives[index].required) {
                    done = false;
                    break;
                }
            }
            if (done) {
                completed.push_back(questId);
            }
        }
    }
    for (const content::ContentId& questId : completed) {
        static_cast<void>(completeQuest(questId));
    }
}

const QuestState* NarrativeService::questState(const content::ContentId& quest) const noexcept {
    const auto found = m_quests.find(quest);
    return found != m_quests.end() ? &found->second : nullptr;
}

bool NarrativeService::isQuestActive(const content::ContentId& quest) const noexcept {
    const QuestState* state = questState(quest);
    return state != nullptr && state->active;
}

bool NarrativeService::isQuestCompleted(const content::ContentId& quest) const noexcept {
    const QuestState* state = questState(quest);
    return state != nullptr && state->completed;
}

std::vector<QuestState> NarrativeService::activeQuests() const {
    std::vector<QuestState> result;
    for (const auto& [id, state] : m_quests) {
        if (state.active) {
            result.push_back(state);
        }
    }
    return result;
}

std::vector<QuestState> NarrativeService::questStates() const {
    std::vector<QuestState> result;
    result.reserve(m_quests.size());
    for (const auto& [id, state] : m_quests) {
        result.push_back(state);
    }
    return result;
}

NarrativeStats NarrativeService::stats() const noexcept {
    NarrativeStats result = m_totals;
    if (m_database != nullptr) {
        const NarrativeStats definitions = m_database->stats();
        result.npcs = definitions.npcs;
        result.dialogues = definitions.dialogues;
        result.quests = definitions.quests;
    }
    result.activeQuests = 0;
    result.completedQuests = 0;
    result.affinityTotal = 0;
    for (const auto& [id, state] : m_quests) {
        static_cast<void>(id);
        if (state.active) {
            ++result.activeQuests;
        }
        if (state.completed) {
            ++result.completedQuests;
        }
    }
    for (const auto& [id, value] : m_affinity) {
        static_cast<void>(id);
        result.affinityTotal += value;
    }
    result.events = m_events.size();
    result.dialogueActive = m_active;
    return result;
}

NarrativeSnapshot NarrativeService::snapshot() const {
    NarrativeSnapshot result;
    result.affinity = m_affinity;
    result.flags = m_flags;
    result.talkedToday = m_talkedToday;
    result.giftedToday = m_giftedToday;
    result.quests = m_quests;
    return result;
}

void NarrativeService::restore(const NarrativeSnapshot& snapshot) {
    m_affinity = snapshot.affinity;
    m_flags = snapshot.flags;
    m_talkedToday = snapshot.talkedToday;
    m_giftedToday = snapshot.giftedToday;
    m_quests = snapshot.quests;
    m_active = false;
    m_dialogue = content::ContentId{};
    m_npc = content::ContentId{};
    m_speaker = scene::EntityId{};
    m_node.clear();
    m_events.clear();
}

void NarrativeService::clear() noexcept {
    m_affinity.clear();
    m_flags.clear();
    m_talkedToday.clear();
    m_giftedToday.clear();
    m_quests.clear();
    m_active = false;
    m_dialogue = content::ContentId{};
    m_npc = content::ContentId{};
    m_speaker = scene::EntityId{};
    m_node.clear();
    m_events.clear();
    m_totals = NarrativeStats{};
}

bool NarrativeService::evaluate(const DialogueCondition& condition) const noexcept {
    switch (condition.kind) {
        case DialogueCondition::Kind::Always:
            return true;
        case DialogueCondition::Kind::AffinityAtLeast:
            return affinity(condition.id) >= condition.value;
        case DialogueCondition::Kind::QuestActive:
            return isQuestActive(condition.id);
        case DialogueCondition::Kind::QuestCompleted:
            return isQuestCompleted(condition.id);
        case DialogueCondition::Kind::HasItem:
            return m_inventory != nullptr && m_inventory->countOf(condition.id) >= condition.value;
        case DialogueCondition::Kind::Flag: {
            const auto found = m_flags.find(condition.id);
            return found != m_flags.end() && found->second;
        }
        case DialogueCondition::Kind::Season:
            return m_season == condition.season;
        default:
            return false;
    }
}

void NarrativeService::apply(const DialogueAction& action) {
    switch (action.kind) {
        case DialogueAction::Kind::GiveItem:
            if (m_inventory != nullptr) {
                static_cast<void>(m_inventory->add(inventory::ItemStack{action.id, action.amount, -1}));
            }
            record(NarrativeEventKind::ActionApplied, action.id, action.amount);
            break;
        case DialogueAction::Kind::AddAffinity:
            m_affinity[action.id] += action.amount;
            record(NarrativeEventKind::ActionApplied, action.id, action.amount);
            break;
        case DialogueAction::Kind::StartQuest:
            static_cast<void>(startQuest(action.id));
            break;
        case DialogueAction::Kind::CompleteQuest:
            static_cast<void>(completeQuest(action.id));
            break;
        case DialogueAction::Kind::SetFlag:
            m_flags[action.id] = true;
            record(NarrativeEventKind::ActionApplied, action.id, 1);
            break;
        case DialogueAction::Kind::OpenShop:
            record(NarrativeEventKind::ActionApplied, {}, 0, action.text);
            break;
        default:
            break;
    }
}

void NarrativeService::enterNode(std::string_view nodeId) {
    const DialogueDef* definition = activeDialogue();
    if (definition == nullptr) {
        return;
    }
    const DialogueNode* node = nullptr;
    for (const DialogueNode& candidate : definition->nodes) {
        if (candidate.id == nodeId) {
            node = &candidate;
            break;
        }
    }
    if (node == nullptr) {
        static_cast<void>(endDialogue());
        return;
    }
    const bool allowed = std::all_of(node->conditions.begin(),
                                     node->conditions.end(),
                                     [this](const DialogueCondition& condition) { return evaluate(condition); });
    if (!allowed) {
        if (!node->next.empty()) {
            enterNode(node->next);
        } else {
            static_cast<void>(endDialogue());
        }
        return;
    }
    m_node = node->id;
    ++m_totals.nodesEntered;
    record(NarrativeEventKind::NodeEntered, {}, 0, node->id);
    for (const DialogueAction& action : node->actions) {
        apply(action);
    }
}

void NarrativeService::record(NarrativeEventKind kind, const content::ContentId& item, int amount, std::string text) {
    NarrativeEvent event;
    event.kind = kind;
    event.dialogue = m_dialogue;
    event.npc = m_npc;
    event.item = item;
    event.amount = amount;
    event.text = std::move(text);
    m_events.push_back(std::move(event));
}

}  // namespace fa::narrative
