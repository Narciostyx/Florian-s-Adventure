/// \file
/// \brief Tests for M23: narrative parsing, conditions, dialogue choices/actions,
/// affinity/gifts, quest progress/completion and rewards.

#include <catch2/catch_test_macros.hpp>

#include "fa/content/ContentId.hpp"
#include "fa/inventory/Inventory.hpp"
#include "fa/narrative/NarrativeDatabase.hpp"
#include "fa/narrative/NarrativeService.hpp"

namespace {

using fa::content::ContentId;
using fa::content::Season;
using fa::inventory::Inventory;
using fa::inventory::ItemStack;
using fa::narrative::DialogueCondition;
using fa::narrative::NarrativeDatabase;
using fa::narrative::NarrativeEventKind;
using fa::narrative::NarrativeService;
using fa::narrative::QuestEvent;
using fa::narrative::QuestObjectiveKind;

constexpr std::string_view kDocument = R"({
  "schema_version": 1,
  "npcs": [
    {"id": "npc.elder", "name": "Elder", "liked_items": ["item.turnip"]}
  ],
  "dialogues": [
    {"id": "dialogue.elder", "npc": "npc.elder", "start": "greet", "nodes": [
      {"id": "greet", "speaker": "Elder", "text": "Hello",
       "choices": [
         {"text": "Help", "next": "quest",
          "conditions": [{"kind": "affinity_at_least", "id": "npc.elder", "value": 1}],
          "actions": [{"kind": "add_affinity", "id": "npc.elder", "amount": 1}]},
         {"text": "Bye", "next": "bye"}
       ]},
      {"id": "quest", "speaker": "Elder", "text": "Bring a turnip",
       "actions": [{"kind": "start_quest", "id": "quest.turnip"}],
       "next": "bye"},
      {"id": "bye", "speaker": "Elder", "text": "Bye", "next": ""}
    ]}
  ],
  "quests": [
    {"id": "quest.turnip", "title": "Turnip", "description": "Bring one turnip",
     "objectives": [{"kind": "collect", "target": "item.turnip", "required": 1}],
     "rewards": [{"item": "item.turnip_seed", "amount": 2}], "gold": 10}
  ]
})";

NarrativeDatabase makeDatabase() {
    NarrativeDatabase database;
    REQUIRE(database.loadDocument(kDocument).isOk());
    return database;
}

Inventory makeInventory() {
    return Inventory(8, 99);
}

}  // namespace

TEST_CASE("Narrative database loads and validates references", "[narrative]") {
    NarrativeDatabase database;
    REQUIRE(database.loadDocument(kDocument).isOk());
    CHECK(database.npcs().size() == 1);
    CHECK(database.dialogues().size() == 1);
    CHECK(database.quests().size() == 1);
    CHECK(database.npc(ContentId::fromName("npc.elder")) != nullptr);
    CHECK(database.dialogue(ContentId::fromName("dialogue.elder")) != nullptr);
    CHECK(database.quest(ContentId::fromName("quest.turnip")) != nullptr);
    CHECK(database.stats().npcs == 1);
}

TEST_CASE("Narrative database rejects unknown references and duplicates", "[narrative]") {
    NarrativeDatabase database;
    const std::string_view missingNpc = R"({
      "schema_version": 1,
      "npcs": [],
      "dialogues": [{"id": "dialogue.x", "npc": "npc.none", "start": "a",
                     "nodes": [{"id": "a", "text": "x", "next": ""}]}]
    })";
    CHECK(database.loadDocument(missingNpc).error().code == fa::narrative::narrative_error::kUnknownNpc);

    NarrativeDatabase duplicate;
    const std::string_view twice = R"({
      "schema_version": 1,
      "npcs": [{"id": "npc.a", "name": "A"}, {"id": "npc.a", "name": "B"}]
    })";
    CHECK(duplicate.loadDocument(twice).error().code == fa::narrative::narrative_error::kDuplicateId);

    NarrativeDatabase badStart;
    const std::string_view start = R"({
      "schema_version": 1,
      "npcs": [{"id": "npc.a", "name": "A"}],
      "dialogues": [{"id": "dialogue.a", "npc": "npc.a", "start": "missing",
                     "nodes": [{"id": "a", "text": "x", "next": ""}]}]
    })";
    CHECK(badStart.loadDocument(start).isError());
}

TEST_CASE("Dialogue conditions filter choices", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);

    REQUIRE(service.startDialogue(ContentId::fromName("dialogue.elder")).isOk());
    CHECK(service.dialogueActive());
    CHECK(service.currentNode() != nullptr);
    CHECK(service.currentNode()->id == "greet");
    CHECK(service.availableChoices().size() == 2);

    // The first dialogue talk raises affinity to 1, so the affinity choice is visible.
    CHECK(service.affinity(ContentId::fromName("npc.elder")) == 1);
    CHECK(service.stats().dialogueActive);
}

TEST_CASE("Dialogue choices apply actions and advance nodes", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);

    REQUIRE(service.startDialogue(ContentId::fromName("dialogue.elder")).isOk());
    REQUIRE(service.choose(0).isOk());
    CHECK(service.currentNode() != nullptr);
    CHECK(service.currentNode()->id == "quest");
    CHECK(service.affinity(ContentId::fromName("npc.elder")) == 2);
    CHECK(service.isQuestActive(ContentId::fromName("quest.turnip")));

    REQUIRE(service.advance().isOk());
    CHECK(service.currentNode()->id == "bye");
    REQUIRE(service.advance().isOk());
    CHECK_FALSE(service.dialogueActive());
    CHECK(service.stats().dialoguesStarted == 1);
    CHECK(service.stats().dialoguesEnded == 1);
    CHECK(service.stats().choices == 1);
    CHECK(service.stats().nodesEntered >= 3);
}

TEST_CASE("Quest progress completes and grants rewards", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);

    REQUIRE(service.startQuest(ContentId::fromName("quest.turnip")).isOk());
    CHECK(service.isQuestActive(ContentId::fromName("quest.turnip")));
    CHECK(service.activeQuests().size() == 1);

    QuestEvent event;
    event.kind = QuestObjectiveKind::Collect;
    event.target = ContentId::fromName("item.turnip");
    event.amount = 1;
    service.reportEvent(event);
    CHECK(service.isQuestCompleted(ContentId::fromName("quest.turnip")));
    CHECK_FALSE(service.isQuestActive(ContentId::fromName("quest.turnip")));
    CHECK(inventory.countOf(ContentId::fromName("item.turnip_seed")) == 2);
    CHECK(service.stats().questsStarted == 1);
    CHECK(service.stats().questsCompleted == 1);

    CHECK(service.startQuest(ContentId::fromName("quest.turnip")).error().code ==
          fa::narrative::narrative_error::kAlreadyCompleted);
    CHECK(service.completeQuest(ContentId::fromName("quest.turnip")).error().code ==
          fa::narrative::narrative_error::kAlreadyCompleted);
}

TEST_CASE("Quest states expose active and completed quests for the log", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);

    const ContentId quest = ContentId::fromName("quest.turnip");
    REQUIRE(service.startQuest(quest).isOk());
    std::vector<fa::narrative::QuestState> states = service.questStates();
    REQUIRE(states.size() == 1);
    CHECK(states[0].id == quest);
    CHECK(states[0].active);
    CHECK_FALSE(states[0].completed);

    QuestEvent event;
    event.kind = QuestObjectiveKind::Collect;
    event.target = ContentId::fromName("item.turnip");
    event.amount = 1;
    service.reportEvent(event);

    states = service.questStates();
    REQUIRE(states.size() == 1);
    CHECK_FALSE(states[0].active);
    CHECK(states[0].completed);
    REQUIRE_FALSE(states[0].progress.empty());
    CHECK(states[0].progress[0] == 1);
}

TEST_CASE("Quest reports only matching objectives", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);
    REQUIRE(service.startQuest(ContentId::fromName("quest.turnip")).isOk());

    QuestEvent wrong;
    wrong.kind = QuestObjectiveKind::Collect;
    wrong.target = ContentId::fromName("item.berry");
    wrong.amount = 5;
    service.reportEvent(wrong);
    CHECK(service.isQuestActive(ContentId::fromName("quest.turnip")));

    QuestEvent partial;
    partial.kind = QuestObjectiveKind::Collect;
    partial.target = ContentId::fromName("item.turnip");
    partial.amount = 1;
    service.reportEvent(partial);
    CHECK(service.isQuestCompleted(ContentId::fromName("quest.turnip")));
}

TEST_CASE("Gifts consume items and respect the daily cap", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);
    REQUIRE(inventory.add(ItemStack{ContentId::fromName("item.turnip"), 2, -1}).isOk());

    const ContentId elder = ContentId::fromName("npc.elder");
    REQUIRE(service.giveGift(elder, ContentId::fromName("item.turnip"), inventory).isOk());
    CHECK(service.affinity(elder) == 3);
    CHECK(inventory.countOf(ContentId::fromName("item.turnip")) == 1);
    CHECK(service.giveGift(elder, ContentId::fromName("item.turnip"), inventory).error().code ==
          fa::narrative::narrative_error::kAlreadyGifted);

    service.onDayChanged();
    REQUIRE(service.giveGift(elder, ContentId::fromName("item.turnip"), inventory).isOk());
    CHECK(service.affinity(elder) == 6);
    CHECK(inventory.countOf(ContentId::fromName("item.turnip")) == 0);
    CHECK(service.stats().gifts == 2);
}

TEST_CASE("Gift rejects an item that is not in the inventory", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);

    CHECK(service.giveGift(ContentId::fromName("npc.elder"), ContentId::fromName("item.turnip"), inventory)
              .error()
              .code == fa::narrative::narrative_error::kMissingItem);
}

TEST_CASE("Season and flag conditions are evaluated", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);
    service.setSeason(Season::Spring);

    REQUIRE(service.startDialogue(ContentId::fromName("dialogue.elder")).isOk());
    REQUIRE(service.choose(0).isOk());
    REQUIRE(service.advance().isOk());
    REQUIRE(service.advance().isOk());

    CHECK(service.stats().events > 0);
    bool sawQuestStarted = false;
    for (const auto& event : service.events()) {
        if (event.kind == NarrativeEventKind::QuestStarted) {
            sawQuestStarted = true;
        }
    }
    CHECK(sawQuestStarted);
}

TEST_CASE("Narrative snapshot restores affinity, flags and quest progress", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);

    const ContentId elder = ContentId::fromName("npc.elder");
    const ContentId quest = ContentId::fromName("quest.turnip");
    const ContentId flag = ContentId::fromName("flag.met_elder");

    REQUIRE(service.startDialogue(ContentId::fromName("dialogue.elder")).isOk());
    REQUIRE(service.choose(0).isOk());
    REQUIRE(service.advance().isOk());
    REQUIRE(service.advance().isOk());
    CHECK_FALSE(service.dialogueActive());
    REQUIRE(service.affinity(elder) == 2);
    REQUIRE(service.isQuestActive(quest));

    fa::narrative::NarrativeSnapshot snapshot = service.snapshot();
    snapshot.flags[flag] = true;
    snapshot.talkedToday[elder] = true;
    snapshot.giftedToday[elder] = true;
    REQUIRE(snapshot.quests.count(quest) == 1);
    snapshot.quests[quest].progress = {0};

    service.clear();
    CHECK(service.affinity(elder) == 0);
    CHECK_FALSE(service.isQuestActive(quest));

    service.restore(snapshot);
    CHECK(service.affinity(elder) == 2);
    CHECK(service.isQuestActive(quest));
    CHECK_FALSE(service.dialogueActive());

    const fa::narrative::NarrativeSnapshot roundTrip = service.snapshot();
    CHECK(roundTrip.affinity.at(elder) == 2);
    CHECK(roundTrip.flags.count(flag) == 1);
    CHECK(roundTrip.talkedToday.count(elder) == 1);
    CHECK(roundTrip.giftedToday.count(elder) == 1);
    CHECK(roundTrip.quests.at(quest).active);

    // A snapshot taken mid-dialogue restores the facts but not the conversation.
    REQUIRE(service.startDialogue(ContentId::fromName("dialogue.elder")).isOk());
    service.restore(roundTrip);
    CHECK_FALSE(service.dialogueActive());
    CHECK(service.currentNode() == nullptr);
}

TEST_CASE("Narrative service clear resets runtime state", "[narrative]") {
    NarrativeDatabase database = makeDatabase();
    Inventory inventory = makeInventory();
    NarrativeService service(&database);
    service.setInventory(&inventory);
    REQUIRE(service.startQuest(ContentId::fromName("quest.turnip")).isOk());
    CHECK(service.stats().questsStarted == 1);
    service.clear();
    CHECK(service.stats().questsStarted == 0);
    CHECK_FALSE(service.dialogueActive());
    CHECK(service.events().empty());
}
