#include "fa/narrative/NarrativeDatabase.hpp"

#include <map>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace fa::narrative {
namespace {

using Json = nlohmann::json;

[[nodiscard]] bool hasPrefix(const content::ContentId& id, std::string_view prefix) {
    return id.name().rfind(prefix, 0) == 0;
}

[[nodiscard]] bool readStringField(const Json& object, const char* key, std::string& out) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return false;
    }
    if (!found->is_string()) {
        return false;
    }
    out = found->get<std::string>();
    return true;
}

[[nodiscard]] bool readIntField(const Json& object, const char* key, int& out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) {
        return false;
    }
    out = found->get<int>();
    return true;
}

[[nodiscard]] bool
readIdField(const Json& object, const char* key, std::string_view prefix, content::ContentId& out, std::string& error) {
    std::string name;
    if (!readStringField(object, key, name) || name.empty()) {
        error = std::string(key) + " must be a non-empty string";
        return false;
    }
    out = content::ContentId::fromName(name);
    if (!out.isValid() || (!prefix.empty() && !hasPrefix(out, prefix))) {
        error = std::string(key) + " is not a valid " + std::string(prefix) + " id";
        return false;
    }
    return true;
}

[[nodiscard]] bool parseCondition(const Json& value, DialogueCondition& out, std::string& error) {
    std::string kind;
    if (!readStringField(value, "kind", kind)) {
        error = "condition needs a string \"kind\"";
        return false;
    }
    if (kind == "always") {
        out.kind = DialogueCondition::Kind::Always;
        return true;
    }
    if (kind == "affinity_at_least") {
        out.kind = DialogueCondition::Kind::AffinityAtLeast;
        return readIdField(value, "id", "npc.", out.id, error) && readIntField(value, "value", out.value);
    }
    if (kind == "quest_active") {
        out.kind = DialogueCondition::Kind::QuestActive;
        return readIdField(value, "id", "quest.", out.id, error);
    }
    if (kind == "quest_completed") {
        out.kind = DialogueCondition::Kind::QuestCompleted;
        return readIdField(value, "id", "quest.", out.id, error);
    }
    if (kind == "has_item") {
        out.kind = DialogueCondition::Kind::HasItem;
        return readIdField(value, "id", "item.", out.id, error) && readIntField(value, "value", out.value);
    }
    if (kind == "flag") {
        out.kind = DialogueCondition::Kind::Flag;
        return readIdField(value, "id", "flag.", out.id, error);
    }
    if (kind == "season") {
        out.kind = DialogueCondition::Kind::Season;
        std::string season;
        if (!readStringField(value, "season", season)) {
            error = "season condition needs a \"season\" string";
            return false;
        }
        const std::uint8_t mask = content::parseSeasonMask(season);
        if (mask == 0 || (mask & (mask - 1)) != 0) {
            error = "season condition must name exactly one season";
            return false;
        }
        out.season = static_cast<content::Season>(mask);
        return true;
    }
    error = "unknown condition kind \"" + kind + "\"";
    return false;
}

[[nodiscard]] bool parseAction(const Json& value, DialogueAction& out, std::string& error) {
    std::string kind;
    if (!readStringField(value, "kind", kind)) {
        error = "action needs a string \"kind\"";
        return false;
    }
    if (kind == "give_item") {
        out.kind = DialogueAction::Kind::GiveItem;
        return readIdField(value, "id", "item.", out.id, error) && readIntField(value, "amount", out.amount);
    }
    if (kind == "add_affinity") {
        out.kind = DialogueAction::Kind::AddAffinity;
        return readIdField(value, "id", "npc.", out.id, error) && readIntField(value, "amount", out.amount);
    }
    if (kind == "start_quest") {
        out.kind = DialogueAction::Kind::StartQuest;
        return readIdField(value, "id", "quest.", out.id, error);
    }
    if (kind == "complete_quest") {
        out.kind = DialogueAction::Kind::CompleteQuest;
        return readIdField(value, "id", "quest.", out.id, error);
    }
    if (kind == "set_flag") {
        out.kind = DialogueAction::Kind::SetFlag;
        return readIdField(value, "id", "flag.", out.id, error);
    }
    if (kind == "open_shop") {
        out.kind = DialogueAction::Kind::OpenShop;
        return readStringField(value, "text", out.text);
    }
    error = "unknown action kind \"" + kind + "\"";
    return false;
}

[[nodiscard]] bool parseConditions(const Json& object, std::vector<DialogueCondition>& out, std::string& error) {
    const auto found = object.find("conditions");
    if (found == object.end()) {
        return true;
    }
    if (!found->is_array()) {
        error = "\"conditions\" must be an array";
        return false;
    }
    for (const Json& entry : *found) {
        DialogueCondition condition;
        if (!parseCondition(entry, condition, error)) {
            return false;
        }
        out.push_back(std::move(condition));
    }
    return true;
}

[[nodiscard]] bool parseActions(const Json& object, std::vector<DialogueAction>& out, std::string& error) {
    const auto found = object.find("actions");
    if (found == object.end()) {
        return true;
    }
    if (!found->is_array()) {
        error = "\"actions\" must be an array";
        return false;
    }
    for (const Json& entry : *found) {
        DialogueAction action;
        if (!parseAction(entry, action, error)) {
            return false;
        }
        out.push_back(std::move(action));
    }
    return true;
}

[[nodiscard]] bool parseNpc(const Json& entry, NpcDef& npc, std::string& error) {
    if (!entry.is_object() || !readIdField(entry, "id", "npc.", npc.id, error)) {
        return false;
    }
    if (entry.contains("name") && !readStringField(entry, "name", npc.name)) {
        error = "npc \"name\" must be a string";
        return false;
    }
    if (entry.contains("portrait") && !readStringField(entry, "portrait", npc.portrait)) {
        error = "npc \"portrait\" must be a string";
        return false;
    }
    if (entry.contains("shop") && !readStringField(entry, "shop", npc.shop)) {
        error = "npc \"shop\" must be a string";
        return false;
    }
    if (const auto likes = entry.find("liked_items"); likes != entry.end()) {
        if (!likes->is_array()) {
            error = "npc \"liked_items\" must be an array";
            return false;
        }
        for (const Json& value : *likes) {
            if (!value.is_string()) {
                error = "npc liked item must be a string";
                return false;
            }
            const content::ContentId id = content::ContentId::fromName(value.get<std::string>());
            if (!hasPrefix(id, "item.")) {
                error = "npc liked item must be an item id";
                return false;
            }
            npc.likedItems.push_back(id);
        }
    }
    return true;
}

[[nodiscard]] bool parseDialogue(const Json& entry, DialogueDef& dialogue, std::string& error) {
    if (!entry.is_object() || !readIdField(entry, "id", "dialogue.", dialogue.id, error) ||
        !readIdField(entry, "npc", "npc.", dialogue.npc, error)) {
        return false;
    }
    if (!readStringField(entry, "start", dialogue.startNode) || dialogue.startNode.empty()) {
        error = "dialogue needs a non-empty \"start\" node";
        return false;
    }
    const auto nodes = entry.find("nodes");
    if (nodes == entry.end() || !nodes->is_array() || nodes->empty()) {
        error = "dialogue needs a non-empty \"nodes\" array";
        return false;
    }
    std::map<std::string, bool> nodeIds;
    for (const Json& value : *nodes) {
        DialogueNode node;
        if (!value.is_object() || !readStringField(value, "id", node.id) || node.id.empty()) {
            error = "dialogue node needs a non-empty \"id\"";
            return false;
        }
        if (nodeIds.find(node.id) != nodeIds.end()) {
            error = "duplicate dialogue node \"" + node.id + "\"";
            return false;
        }
        nodeIds[node.id] = true;
        if (value.contains("speaker") && !readStringField(value, "speaker", node.speaker)) {
            error = "node \"speaker\" must be a string";
            return false;
        }
        if (value.contains("text") && !readStringField(value, "text", node.text)) {
            error = "node \"text\" must be a string";
            return false;
        }
        if (value.contains("next") && !readStringField(value, "next", node.next)) {
            error = "node \"next\" must be a string";
            return false;
        }
        if (!parseConditions(value, node.conditions, error) || !parseActions(value, node.actions, error)) {
            return false;
        }
        if (const auto choices = value.find("choices"); choices != value.end()) {
            if (!choices->is_array()) {
                error = "node \"choices\" must be an array";
                return false;
            }
            for (const Json& choiceValue : *choices) {
                DialogueChoice choice;
                if (!choiceValue.is_object() || !readStringField(choiceValue, "text", choice.text)) {
                    error = "choice needs a string \"text\"";
                    return false;
                }
                if (choiceValue.contains("next") && !readStringField(choiceValue, "next", choice.next)) {
                    error = "choice \"next\" must be a string";
                    return false;
                }
                if (!parseConditions(choiceValue, choice.conditions, error) ||
                    !parseActions(choiceValue, choice.actions, error)) {
                    return false;
                }
                node.choices.push_back(std::move(choice));
            }
        }
        dialogue.nodes.push_back(std::move(node));
    }
    if (nodeIds.find(dialogue.startNode) == nodeIds.end()) {
        error = "dialogue start node \"" + dialogue.startNode + "\" does not exist";
        return false;
    }
    for (const DialogueNode& node : dialogue.nodes) {
        if (!node.next.empty() && nodeIds.find(node.next) == nodeIds.end()) {
            error = "dialogue node \"" + node.id + "\" points to missing node \"" + node.next + "\"";
            return false;
        }
        for (const DialogueChoice& choice : node.choices) {
            if (!choice.next.empty() && nodeIds.find(choice.next) == nodeIds.end()) {
                error = "dialogue choice in \"" + node.id + "\" points to missing node \"" + choice.next + "\"";
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool parseQuest(const Json& entry, QuestDef& quest, std::string& error) {
    if (!entry.is_object() || !readIdField(entry, "id", "quest.", quest.id, error)) {
        return false;
    }
    if (entry.contains("title") && !readStringField(entry, "title", quest.title)) {
        error = "quest \"title\" must be a string";
        return false;
    }
    if (entry.contains("description") && !readStringField(entry, "description", quest.description)) {
        error = "quest \"description\" must be a string";
        return false;
    }
    if (entry.contains("gold") && !readIntField(entry, "gold", quest.goldReward)) {
        error = "quest \"gold\" must be an integer";
        return false;
    }
    if (!parseConditions(entry, quest.conditions, error)) {
        return false;
    }
    const auto objectives = entry.find("objectives");
    if (objectives == entry.end() || !objectives->is_array() || objectives->empty()) {
        error = "quest needs a non-empty \"objectives\" array";
        return false;
    }
    for (const Json& value : *objectives) {
        QuestObjective objective;
        std::string kind;
        if (!value.is_object() || !readStringField(value, "kind", kind)) {
            error = "quest objective needs a string \"kind\"";
            return false;
        }
        if (kind == "collect") {
            objective.kind = QuestObjectiveKind::Collect;
            if (!readIdField(value, "target", "item.", objective.target, error)) {
                return false;
            }
        } else if (kind == "deliver") {
            objective.kind = QuestObjectiveKind::Deliver;
            if (!readIdField(value, "target", "item.", objective.target, error)) {
                return false;
            }
        } else if (kind == "kill") {
            objective.kind = QuestObjectiveKind::Kill;
            if (!readIdField(value, "target", "enemy.", objective.target, error)) {
                return false;
            }
        } else if (kind == "plant") {
            objective.kind = QuestObjectiveKind::Plant;
            if (!readIdField(value, "target", "crop.", objective.target, error)) {
                return false;
            }
        } else if (kind == "talk") {
            objective.kind = QuestObjectiveKind::Talk;
            if (!readIdField(value, "target", "npc.", objective.target, error)) {
                return false;
            }
        } else {
            error = "unknown quest objective kind \"" + kind + "\"";
            return false;
        }
        if (!readIntField(value, "required", objective.required) || objective.required <= 0) {
            error = "quest objective needs a positive \"required\"";
            return false;
        }
        quest.objectives.push_back(std::move(objective));
    }
    if (const auto rewards = entry.find("rewards"); rewards != entry.end()) {
        if (!rewards->is_array()) {
            error = "quest \"rewards\" must be an array";
            return false;
        }
        for (const Json& value : *rewards) {
            inventory::ItemStack stack;
            if (!value.is_object() || !readIdField(value, "item", "item.", stack.id, error) ||
                !readIntField(value, "amount", stack.count) || stack.count <= 0) {
                error = "quest reward needs an item id and positive amount";
                return false;
            }
            quest.rewards.push_back(std::move(stack));
        }
    }
    return true;
}

}  // namespace

std::string_view toString(NarrativeEventKind kind) noexcept {
    switch (kind) {
        case NarrativeEventKind::DialogueStarted:
            return "dialogue_started";
        case NarrativeEventKind::NodeEntered:
            return "node_entered";
        case NarrativeEventKind::ChoiceSelected:
            return "choice_selected";
        case NarrativeEventKind::DialogueEnded:
            return "dialogue_ended";
        case NarrativeEventKind::GiftGiven:
            return "gift_given";
        case NarrativeEventKind::QuestStarted:
            return "quest_started";
        case NarrativeEventKind::QuestProgress:
            return "quest_progress";
        case NarrativeEventKind::QuestCompleted:
            return "quest_completed";
        case NarrativeEventKind::ActionApplied:
            return "action_applied";
        default:
            return "?";
    }
}

std::string_view toString(QuestObjectiveKind kind) noexcept {
    switch (kind) {
        case QuestObjectiveKind::Collect:
            return "collect";
        case QuestObjectiveKind::Deliver:
            return "deliver";
        case QuestObjectiveKind::Kill:
            return "kill";
        case QuestObjectiveKind::Plant:
            return "plant";
        case QuestObjectiveKind::Talk:
            return "talk";
        default:
            return "?";
    }
}

std::string_view toString(DialogueCondition::Kind kind) noexcept {
    switch (kind) {
        case DialogueCondition::Kind::Always:
            return "always";
        case DialogueCondition::Kind::AffinityAtLeast:
            return "affinity_at_least";
        case DialogueCondition::Kind::QuestActive:
            return "quest_active";
        case DialogueCondition::Kind::QuestCompleted:
            return "quest_completed";
        case DialogueCondition::Kind::HasItem:
            return "has_item";
        case DialogueCondition::Kind::Flag:
            return "flag";
        case DialogueCondition::Kind::Season:
            return "season";
        default:
            return "?";
    }
}

std::string_view toString(DialogueAction::Kind kind) noexcept {
    switch (kind) {
        case DialogueAction::Kind::GiveItem:
            return "give_item";
        case DialogueAction::Kind::AddAffinity:
            return "add_affinity";
        case DialogueAction::Kind::StartQuest:
            return "start_quest";
        case DialogueAction::Kind::CompleteQuest:
            return "complete_quest";
        case DialogueAction::Kind::SetFlag:
            return "set_flag";
        case DialogueAction::Kind::OpenShop:
            return "open_shop";
        default:
            return "?";
    }
}

core::Status NarrativeDatabase::loadDocument(std::string_view json) {
    const Json document = Json::parse(json.begin(), json.end(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return core::errorStatus(narrative_error::kInvalidDocument, "narrative document is not an object");
    }
    int schema = 0;
    if (!readIntField(document, "schema_version", schema)) {
        return core::errorStatus(narrative_error::kInvalidDocument,
                                 "narrative document needs an integer \"schema_version\"");
    }
    if (schema != 1) {
        return core::errorStatus(narrative_error::kUnsupportedSchema, "unsupported narrative schema version");
    }

    std::vector<NpcDef> npcs;
    std::vector<DialogueDef> dialogues;
    std::vector<QuestDef> quests;
    std::string error;

    if (const auto values = document.find("npcs"); values != document.end()) {
        if (!values->is_array()) {
            return core::errorStatus(narrative_error::kInvalidDocument, "\"npcs\" must be an array");
        }
        for (const Json& value : *values) {
            NpcDef npc;
            if (!parseNpc(value, npc, error)) {
                return core::errorStatus(narrative_error::kInvalidDocument, error);
            }
            npcs.push_back(std::move(npc));
        }
    }
    if (const auto values = document.find("dialogues"); values != document.end()) {
        if (!values->is_array()) {
            return core::errorStatus(narrative_error::kInvalidDocument, "\"dialogues\" must be an array");
        }
        for (const Json& value : *values) {
            DialogueDef dialogue;
            if (!parseDialogue(value, dialogue, error)) {
                return core::errorStatus(narrative_error::kInvalidDocument, error);
            }
            dialogues.push_back(std::move(dialogue));
        }
    }
    if (const auto values = document.find("quests"); values != document.end()) {
        if (!values->is_array()) {
            return core::errorStatus(narrative_error::kInvalidDocument, "\"quests\" must be an array");
        }
        for (const Json& value : *values) {
            QuestDef quest;
            if (!parseQuest(value, quest, error)) {
                return core::errorStatus(narrative_error::kInvalidDocument, error);
            }
            quests.push_back(std::move(quest));
        }
    }

    std::map<content::ContentId, bool> npcIds;
    for (const NpcDef& npc : npcs) {
        if (m_npcIndex.find(npc.id) != m_npcIndex.end() || npcIds.find(npc.id) != npcIds.end()) {
            return core::errorStatus(narrative_error::kDuplicateId, "duplicate npc id " + npc.id.name());
        }
        npcIds[npc.id] = true;
    }
    std::map<content::ContentId, bool> dialogueIds;
    for (const DialogueDef& dialogue : dialogues) {
        if (m_dialogueIndex.find(dialogue.id) != m_dialogueIndex.end() ||
            dialogueIds.find(dialogue.id) != dialogueIds.end()) {
            return core::errorStatus(narrative_error::kDuplicateId, "duplicate dialogue id " + dialogue.id.name());
        }
        dialogueIds[dialogue.id] = true;
        if (m_npcIndex.find(dialogue.npc) == m_npcIndex.end() && npcIds.find(dialogue.npc) == npcIds.end()) {
            return core::errorStatus(narrative_error::kUnknownNpc,
                                     "dialogue \"" + dialogue.id.name() + "\" references unknown npc");
        }
    }
    std::map<content::ContentId, bool> questIds;
    for (const QuestDef& quest : quests) {
        if (m_questIndex.find(quest.id) != m_questIndex.end() || questIds.find(quest.id) != questIds.end()) {
            return core::errorStatus(narrative_error::kDuplicateId, "duplicate quest id " + quest.id.name());
        }
        questIds[quest.id] = true;
    }

    for (NpcDef& npc : npcs) {
        m_npcIndex.emplace(npc.id, m_npcs.size());
        m_npcs.push_back(std::move(npc));
    }
    for (DialogueDef& dialogue : dialogues) {
        m_dialogueIndex.emplace(dialogue.id, m_dialogues.size());
        m_dialogues.push_back(std::move(dialogue));
    }
    for (QuestDef& quest : quests) {
        m_questIndex.emplace(quest.id, m_quests.size());
        m_quests.push_back(std::move(quest));
    }
    m_schemaVersion = schema;
    return core::okStatus();
}

void NarrativeDatabase::clear() noexcept {
    m_npcs.clear();
    m_dialogues.clear();
    m_quests.clear();
    m_npcIndex.clear();
    m_dialogueIndex.clear();
    m_questIndex.clear();
    m_schemaVersion = 0;
}

const NpcDef* NarrativeDatabase::npc(const content::ContentId& id) const noexcept {
    const auto found = m_npcIndex.find(id);
    return found != m_npcIndex.end() ? &m_npcs[found->second] : nullptr;
}

const DialogueDef* NarrativeDatabase::dialogue(const content::ContentId& id) const noexcept {
    const auto found = m_dialogueIndex.find(id);
    return found != m_dialogueIndex.end() ? &m_dialogues[found->second] : nullptr;
}

const QuestDef* NarrativeDatabase::quest(const content::ContentId& id) const noexcept {
    const auto found = m_questIndex.find(id);
    return found != m_questIndex.end() ? &m_quests[found->second] : nullptr;
}

NarrativeStats NarrativeDatabase::stats() const noexcept {
    NarrativeStats result;
    result.npcs = m_npcs.size();
    result.dialogues = m_dialogues.size();
    result.quests = m_quests.size();
    return result;
}

}  // namespace fa::narrative
