#include <apps/openmw-server/SpellmakingService.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <chrono>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/openmw-mp/Packets/Records/PacketSpellmakingRequest.hpp>
#include <components/openmw-mp/Packets/Records/PacketSpellmakingResult.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>
#include <components/openmw-mp/Sha256.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <components/openmw-mp/ServicePricing.hpp>
#include <components/openmw-mp/SpellmakingCost.hpp>
#include <sqlite3.h>

namespace
{
    using Error = mwmp::records::SpellmakingError;
    class Spellmaking : public testing::Test
    {
    protected:
        struct TemporaryDatabase
        {
            std::filesystem::path path = std::filesystem::temp_directory_path()
                / ("spellmaking-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + ".db");
            ~TemporaryDatabase()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
                std::filesystem::remove(path.string() + "-wal", error);
                std::filesystem::remove(path.string() + "-shm", error);
            }
        } temporary;
        const std::filesystem::path& path = temporary.path;
        mwmp::PlayerDatabase db{ path.string() };
        MWWorld::ESMStore store;
        mwmp::BasePlayer player;
        mwmp::SpellmakingService::Context context;
        mwmp::records::SpellmakingRequest request;
        std::uint64_t sequence = 1;
        Spellmaking()
        {
            context.accountId = db.createAccount("spellmaker-test");
            context.characterId = db.createCharacter(context.accountId, "Sith Soldier").characterId;
            context.player = &player;
            context.store = &store;
            context.creationSource = "spellmaking";
            context.findEquivalent
                = [](auto, auto) -> std::optional<mwmp::DynamicRecordService::CatalogRecord> { return std::nullopt; };
            context.allocateId = [&](auto) { return "$custom_spell_" + std::to_string(sequence++); };
            context.nextCommitSequence = [&]() { return sequence++; };
            context.lookupSpell = [&](const std::string& id) -> std::optional<ESM::Spell> {
                if (auto spell = store.get<ESM::Spell>().search(ESM::RefId::stringRefId(id)))
                    return *spell;
                return std::nullopt;
            };
            const auto gmst = [&](const char* id, float value) {
                ESM::GameSetting setting;
                setting.blank();
                setting.mId = ESM::RefId::stringRefId(id);
                setting.mValue.setType(ESM::VT_Float);
                setting.mValue.setFloat(value);
                store.insertStatic(setting);
            };
            gmst("fEffectCostMult", 1);
            gmst("fSpellMakingValueMult", 10);
            gmst("fFatigueBase", 1);
            gmst("fFatigueMult", 0);
            gmst("fDispRaceMod", 0);
            gmst("fDispPersonalityMult", 0);
            gmst("fDispPersonalityBase", 0);
            gmst("fDispCrimeMod", 0);
            ESM::NPC npc;
            npc.blank();
            npc.mId = ESM::RefId::stringRefId("spellmaker");
            npc.mFlags = 0;
            npc.mAiData.mServices = ESM::NPC::Spellmaking;
            npc.mNpdt.mDisposition = 50;
            npc.mNpdt.mAttributes.fill(0);
            npc.mNpdt.mSkills.fill(0);
            store.insertStatic(npc);
            context.resolveActor = [](auto id) -> std::optional<mwmp::SpellmakingService::ServiceActor> {
                if (id != 123)
                    return std::nullopt;
                mwmp::SpellmakingService::ServiceActor actor;
                actor.refId = "spellmaker";
                actor.available = true;
                actor.gold = mwmp::MerchantGoldMutation{ 123, "spellmaker", 100, 100, 0, 0 };
                return actor;
            };
            ESM::MagicEffect effect;
            effect.blank();
            effect.mId = ESM::MagicEffect::FireDamage;
            effect.mData.mBaseCost = 10;
            effect.mData.mFlags
                = ESM::MagicEffect::AllowSpellmaking | ESM::MagicEffect::CastSelf | ESM::MagicEffect::CastTarget;
            store.insertStatic(effect);
            ESM::Spell known;
            known.blank();
            known.mId = ESM::RefId::stringRefId("known_fire");
            known.mData.mType = ESM::Spell::ST_Spell;
            ESM::ENAMstruct enam{};
            enam.mEffectID = effect.mId;
            known.mEffects.populate({ enam });
            store.insertStatic(known);
            player.spellbookChanges.spellIds = { "known_fire" };
            mwmp::Item gold;
            gold.refId = "gold_001";
            gold.count = 10000;
            gold.instanceId = 42;
            player.inventoryChanges.items = { gold };
            db.saveCharacterInventory(context.characterId, player.inventoryChanges.items, false, 0);
            db.saveCharacterSpellbook(context.characterId, player.spellbookChanges.spellIds, false, 0);
            request.requestId = "test-spell-1";
            request.actorNetId = 123;
            request.name = "Custom Fire";
            request.maximumPrice = 10000;
            mwmp::records::MagicEffect choice;
            choice.effectId = effect.mId.serializeText();
            choice.magnitudeMin = 5;
            choice.magnitudeMax = 10;
            choice.duration = 2;
            request.effects = { choice };
        }
        mwmp::SpellmakingService::Outcome execute()
        {
            mwmp::PacketSpellmakingRequest packet;
            packet.request = request;
            const auto bytes = packet.encode();
            const auto hash
                = mwmp::crypto::sha256hex(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return mwmp::SpellmakingService(db).execute(request, hash, context);
        }
        void unchanged()
        {
            EXPECT_EQ(db.loadCharacterInventory(context.characterId)[0].count, 10000);
            EXPECT_EQ(db.loadCharacterSpellbook(context.characterId), std::vector<std::string>{ "known_fire" });
            EXPECT_EQ(db.loadInventoryRevision(context.characterId), 0u);
            EXPECT_TRUE(db.loadDynamicRecords().empty());
        }
    };
}

TEST_F(Spellmaking, PurchasePersistsPaymentDefinitionAndLearnedSpellTogether)
{
    const auto out = execute();
    ASSERT_TRUE(out.committed);
    EXPECT_TRUE(out.result.accepted);
    EXPECT_EQ(out.result.price, 230);
    ASSERT_EQ(db.loadDynamicRecordCatalog().size(), 1u);
    EXPECT_EQ(db.loadDynamicRecordCatalog()[0].linkCount, 1);
    EXPECT_EQ(db.loadCharacterInventory(context.characterId)[0].count, 10000 - out.result.price);
    EXPECT_EQ(db.loadMerchantGold(123)->gold, 100 + out.result.price);
    EXPECT_EQ(db.loadCharacterSpellbook(context.characterId), out.spellbook);
    EXPECT_EQ(db.loadSpellbookRevision(context.characterId), 1u);
    ASSERT_EQ(out.newRecords.size(), 1u);
    const auto definition = mwmp::records::decodeDefinition(out.newRecords[0].definition);
    const auto spell = std::get<ESM::Spell>(mwmp::records::toEsmRecord(definition));
    EXPECT_EQ(spell.mName, request.name);
    EXPECT_EQ(spell.mData.mType, ESM::Spell::ST_Spell);
    EXPECT_EQ(spell.mEffects.mList[0].mData.mMagnMax, 10);
    mwmp::PlayerDatabase reconnected(path.string());
    EXPECT_EQ(reconnected.loadCharacterSpellbook(context.characterId), out.spellbook);
    EXPECT_EQ(reconnected.loadDynamicRecords().size(), 1u);
    const auto replay = execute();
    EXPECT_TRUE(replay.replayed);
    EXPECT_FALSE(replay.committed);
    EXPECT_EQ(replay.encodedResult, out.encodedResult);
    EXPECT_EQ(db.loadCharacterInventory(context.characterId)[0].count, 10000 - out.result.price);
    EXPECT_EQ(db.loadMerchantGold(123)->gold, 100 + out.result.price);
}
TEST_F(Spellmaking, RetryAfterDatabaseReopenReplaysOriginalResult)
{
    const auto out = execute();
    ASSERT_TRUE(out.committed);
    mwmp::PlayerDatabase reopened(path.string());
    mwmp::PacketSpellmakingRequest packet;
    packet.request = request;
    const auto bytes = packet.encode();
    const auto hash
        = mwmp::crypto::sha256hex(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const auto replay = mwmp::SpellmakingService(reopened).execute(request, hash, context);
    EXPECT_TRUE(replay.replayed);
    EXPECT_EQ(replay.encodedResult, out.encodedResult);
}
TEST_F(Spellmaking, ReusedRequestWithDifferentRecipeCannotChargeAgain)
{
    const auto out = execute();
    ASSERT_TRUE(out.committed);
    request.effects[0].duration++;
    EXPECT_EQ(execute().result.error, Error::DuplicateRequestConflict);
    EXPECT_EQ(db.loadCharacterInventory(context.characterId)[0].count, 10000 - out.result.price);
}
TEST_F(Spellmaking, RejectsUnknownEffectWithoutPayment)
{
    player.spellbookChanges.spellIds.clear();
    EXPECT_EQ(execute().result.error, Error::UnknownEffect);
    unchanged();
}
TEST_F(Spellmaking, AbilitiesDoNotUnlockEffects)
{
    auto spell = *store.get<ESM::Spell>().find(ESM::RefId::stringRefId("known_fire"));
    spell.mData.mType = ESM::Spell::ST_Ability;
    store.overrideRecord(spell);
    EXPECT_EQ(execute().result.error, Error::UnknownEffect);
    unchanged();
}
TEST_F(Spellmaking, RejectsUnavailableServiceWithoutPayment)
{
    request.actorNetId = 999;
    EXPECT_EQ(execute().result.error, Error::InvalidService);
    unchanged();
}
TEST_F(Spellmaking, RejectsActorWithoutSpellmakingService)
{
    auto npc = *store.get<ESM::NPC>().find(ESM::RefId::stringRefId("spellmaker"));
    npc.mAiData.mServices = 0;
    store.overrideRecord(npc);
    EXPECT_EQ(execute().result.error, Error::InvalidService);
    unchanged();
}
TEST_F(Spellmaking, InsufficientGoldAndPriceChangeNeverCharge)
{
    request.maximumPrice = 0;
    const auto quote = execute();
    EXPECT_EQ(quote.result.error, Error::PriceChanged);
    EXPECT_GT(quote.result.price, 0);
    unchanged();
    request.requestId = "retry-with-confirmed-price";
    request.maximumPrice = quote.result.price;
    player.inventoryChanges.items[0].count = 0;
    EXPECT_EQ(execute().result.error, Error::InsufficientGold);
    unchanged();
}
TEST_F(Spellmaking, InvalidRangeMagnitudeDurationAndDuplicatesRejected)
{
    request.effects[0].range = 1;
    EXPECT_EQ(execute().result.error, Error::InvalidEffect);
    unchanged();
    request.requestId = "bad-magnitude";
    request.effects[0].range = 0;
    request.effects[0].magnitudeMax = 1001;
    EXPECT_EQ(execute().result.error, Error::InvalidEffect);
    unchanged();
    request.requestId = "bad-duration";
    request.effects[0].magnitudeMax = 10;
    request.effects[0].duration = -1;
    EXPECT_EQ(execute().result.error, Error::InvalidEffect);
    unchanged();
    request.requestId = "duplicate-effect";
    request.effects[0].duration = 2;
    request.effects.push_back(request.effects[0]);
    EXPECT_EQ(execute().result.error, Error::InvalidEffect);
    unchanged();
}
TEST_F(Spellmaking, StaleSpellbookDetectedInsideAtomicCommit)
{
    db.saveCharacterSpellbook(context.characterId, { "known_fire" }, false, 1);
    EXPECT_EQ(execute().result.error, Error::StaleSpellbookRevision);
    EXPECT_EQ(db.loadCharacterInventory(context.characterId)[0].count, 10000);
    EXPECT_TRUE(db.loadDynamicRecords().empty());
}
TEST_F(Spellmaking, StaleInventoryCannotCharge)
{
    request.inventoryRevision = 9;
    EXPECT_EQ(execute().result.error, Error::StaleInventoryRevision);
    unchanged();
}
TEST_F(Spellmaking, DatabaseFailureRollsBackPaymentSpellAndRecord)
{
    sqlite3* connection = nullptr;
    ASSERT_EQ(sqlite3_open(path.string().c_str(), &connection), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(connection,
                  "CREATE TRIGGER reject_spell BEFORE INSERT ON character_spellbook BEGIN SELECT RAISE(ABORT,'injected "
                  "failure'); END",
                  nullptr, nullptr, nullptr),
        SQLITE_OK);
    sqlite3_close(connection);
    EXPECT_THROW(execute(), std::exception);
    unchanged();
    EXPECT_FALSE(db.loadCraftRequest(context.accountId, context.characterId, request.requestId));
}
TEST_F(Spellmaking, PacketRoundTripsAndRejectsTruncationAndExcessiveEffectCount)
{
    mwmp::PacketSpellmakingRequest packet;
    packet.request = request;
    auto bytes = packet.encode();
    mwmp::PacketSpellmakingRequest decoded;
    ASSERT_TRUE(decoded.decode(bytes));
    EXPECT_EQ(decoded.request, request);
    bytes.pop_back();
    EXPECT_FALSE(decoded.decode(bytes));
    packet.request.effects.resize(9);
    EXPECT_FALSE(decoded.decode(packet.encode()));
    packet.request = request;
    packet.request.name.assign(65, 'x');
    EXPECT_FALSE(decoded.decode(packet.encode()));
    const auto result = execute().result;
    mwmp::PacketSpellmakingResult response;
    response.result = result;
    mwmp::PacketSpellmakingResult decodedResponse;
    ASSERT_TRUE(decodedResponse.decode(response.encode()));
    EXPECT_EQ(decodedResponse.result, result);
}

TEST_F(Spellmaking, DefinitionPreservesSelectedEffectOrderAndCanInstallOnTwoClients)
{
    ESM::MagicEffect other;
    other.blank();
    other.mId = ESM::MagicEffect::AbsorbHealth;
    other.mData.mBaseCost = 4;
    other.mData.mFlags = ESM::MagicEffect::AllowSpellmaking | ESM::MagicEffect::CastSelf;
    store.insertStatic(other);
    auto known = *store.get<ESM::Spell>().find(ESM::RefId::stringRefId("known_fire"));
    ESM::IndexedENAMstruct extra{};
    extra.mData.mEffectID = other.mId;
    known.mEffects.mList.push_back(extra);
    store.overrideRecord(known);
    auto choice = request.effects[0];
    choice.effectId = other.mId.serializeText();
    request.effects.push_back(choice);
    const auto out = execute();
    ASSERT_TRUE(out.committed);
    const auto spell = std::get<ESM::Spell>(
        mwmp::records::toEsmRecord(mwmp::records::decodeDefinition(out.newRecords[0].definition)));
    ASSERT_EQ(spell.mEffects.mList.size(), 2u);
    EXPECT_EQ(spell.mEffects.mList[0].mData.mEffectID, ESM::MagicEffect::FireDamage);
    EXPECT_EQ(spell.mEffects.mList[1].mData.mEffectID, ESM::MagicEffect::AbsorbHealth);
    // Independent receiving stores see the same named spell and effect payload.
    MWWorld::ESMStore clientA, clientB;
    auto installed = spell;
    installed.mId = ESM::RefId::stringRefId(out.result.recordId);
    clientA.overrideRecord(installed);
    clientB.overrideRecord(installed);
    EXPECT_EQ(clientA.get<ESM::Spell>().find(installed.mId)->mEffects.mList,
        clientB.get<ESM::Spell>().find(installed.mId)->mEffects.mList);
}
TEST_F(Spellmaking, DynamicKnownSpellsUnlockEffectsAndDeduplicationDoesNotChargeKnownSpell)
{
    const auto originalLookup = context.lookupSpell;
    player.spellbookChanges.spellIds = { "$custom_known" };
    context.lookupSpell
        = [&](const std::string& id) { return originalLookup(id == "$custom_known" ? "known_fire" : id); };
    const auto out = execute();
    ASSERT_TRUE(out.committed);
    player.spellbookChanges.spellIds.push_back(out.result.recordId);
    context.inventoryRevision = out.result.inventoryRevision;
    context.spellbookRevision = out.result.spellbookRevision;
    request.inventoryRevision = context.inventoryRevision;
    request.spellbookRevision = context.spellbookRevision;
    request.requestId = "same-spell-new-operation";
    context.findEquivalent = [&](auto, auto fingerprint) -> std::optional<mwmp::DynamicRecordService::CatalogRecord> {
        return mwmp::DynamicRecordService::CatalogRecord{ "spell", out.result.recordId, std::string(fingerprint),
            out.newRecords[0].definition };
    };
    EXPECT_EQ(execute().result.error, Error::AlreadyKnown);
    EXPECT_EQ(db.loadCharacterInventory(context.characterId)[0].count, 10000 - out.result.price);
}
TEST_F(Spellmaking, RateAndRecordQuotaRejectWithoutPayment)
{
    context.admissionError = mwmp::records::CreateError::RateLimited;
    EXPECT_EQ(execute().result.error, Error::RateLimited);
    unchanged();
    request.requestId = "record-quota";
    context.admissionError = mwmp::records::CreateError::None;
    context.maximumNewRecords = 0;
    EXPECT_EQ(execute().result.error, Error::QuotaExceeded);
    unchanged();
}

TEST_F(Spellmaking, MultiplayerPreviewAndChargeAgreeForAuthoritativePricingState)
{
    auto npc = *store.get<ESM::NPC>().find(ESM::RefId::stringRefId("spellmaker"));
    npc.mRace = ESM::RefId::stringRefId("breton"); player.race = "breton";
    const auto merc = ESM::Skill::refIdToIndex(ESM::Skill::Mercantile);
    const auto luck = ESM::Attribute::refIdToIndex(ESM::Attribute::Luck);
    const auto personality = ESM::Attribute::refIdToIndex(ESM::Attribute::Personality);
    npc.mNpdt.mSkills[merc] = 40; npc.mNpdt.mAttributes[luck] = 40; npc.mNpdt.mAttributes[personality] = 50;
    store.overrideRecord(npc);
    player.skills[merc].base = 35; player.skills[merc].damage = 10; player.skills[merc].mod = 5;
    player.attributes[personality].base = 65; player.attributes[personality].damage = 10; player.attributes[personality].mod = 5;
    player.attributes[luck].base = 50; player.bounty = 100;
    for (const auto& [id, value] : std::vector<std::pair<std::string, float>>{
        {"fDispRaceMod", 10}, {"fDispPersonalityMult", 0.2f}, {"fDispPersonalityBase", 50}, {"fDispCrimeMod", 0.01f}})
    {
        auto setting = *store.get<ESM::GameSetting>().find(id);
        setting.mValue.setFloat(value); store.overrideRecord(setting);
    }
    const auto actor = context.resolveActor(request.actorNetId);
    const auto previewInputs = mwmp::serviceBarterInput(&npc, player, &actor->dynamicStats,
        [&](std::string_view id) { return store.get<ESM::GameSetting>().find(id)->mValue.getFloat(); });
    // Native client-only Charm/faction/dialogue modifiers never enter this MP resolver.
    EXPECT_EQ(previewInputs.disposition, 61);
    const int displayedPrice = mwmp::spellmakingPrice(23.f, 10.f, previewInputs);
    EXPECT_EQ(displayedPrice, 227);
    request.maximumPrice = displayedPrice;
    const auto out = execute(); ASSERT_TRUE(out.committed);
    EXPECT_EQ(out.result.price, displayedPrice);
    EXPECT_EQ(db.loadCharacterInventory(context.characterId)[0].count, 10000 - displayedPrice);
}
TEST_F(Spellmaking, FractionalCostIsPreservedUntilPriceCalculation)
{
    auto magic = *store.get<ESM::MagicEffect>().find(ESM::MagicEffect::FireDamage);
    magic.mData.mBaseCost = 1.1f; store.overrideRecord(magic);
    ESM::ENAMstruct effect{}; effect.mEffectID = magic.mId;
    effect.mMagnMin = 5; effect.mMagnMax = 10; effect.mDuration = 2;
    const float raw = mwmp::spellmakingCost(std::vector<ESM::ENAMstruct>{effect},
        [&](const auto&) -> const ESM::MagicEffect& { return magic; }, 1.f);
    EXPECT_NEAR(raw, 2.53f, 0.00001f);
    request.maximumPrice = 25;
    const auto out = execute(); ASSERT_TRUE(out.committed); EXPECT_EQ(out.result.price, 25);
    const auto spell = std::get<ESM::Spell>(mwmp::records::toEsmRecord(
        mwmp::records::decodeDefinition(out.newRecords[0].definition)));
    EXPECT_EQ(spell.mData.mCost, 2); // Native stored magicka cost truncates; purchase price uses 2.53 * 10.
}
TEST_F(Spellmaking, RejectedTerminalRequestReplaysEvenAfterEligibilityChanges)
{
    request.actorNetId = 999;
    const auto first = execute(); EXPECT_EQ(first.result.error, Error::InvalidService);
    context.resolveActor = [](auto) -> std::optional<mwmp::SpellmakingService::ServiceActor> {
        mwmp::SpellmakingService::ServiceActor actor; actor.available = true; actor.refId = "spellmaker"; return actor;
    };
    const auto retry = execute(); EXPECT_TRUE(retry.replayed); EXPECT_EQ(retry.encodedResult, first.encodedResult); unchanged();
}
TEST_F(Spellmaking, OversizedPacketsAreRejectedBeforeRecordCreation)
{
    mwmp::PacketSpellmakingRequest packet; packet.request = request;
    packet.request.name.assign(5000, 'x');
    mwmp::PacketSpellmakingRequest decoded; EXPECT_FALSE(decoded.decode(packet.encode()));
    packet.request = request; packet.request.effects[0].attributeId.assign(129, 'x');
    EXPECT_FALSE(decoded.decode(packet.encode())); unchanged();
}
