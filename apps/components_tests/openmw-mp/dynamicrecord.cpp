#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>
#include <components/openmw-mp/Sha256.hpp>
#include <components/openmw-mp/Packets/Records/PacketRecordCreateRequest.hpp>
#include <components/openmw-mp/Packets/Records/PacketRecordCreateResult.hpp>
#include <components/openmw-mp/Packets/System/PacketHandshake.hpp>

namespace
{
    mwmp::records::MagicEffect effect(std::string id, int magnitude)
    {
        mwmp::records::MagicEffect result;
        result.effectId = std::move(id);
        result.range = 1;
        result.duration = 5;
        result.magnitudeMin = magnitude;
        result.magnitudeMax = magnitude;
        return result;
    }

    mwmp::records::DynamicRecordDefinition potionDefinition()
    {
        mwmp::records::Potion potion;
        potion.item.name = "Potion";
        potion.item.model = "Meshes\\Potion.NIF";
        potion.item.icon = "Icons/Potion.DDS";
        potion.item.weight = 0.5f;
        potion.item.value = 20;
        potion.effects = { effect("RestoreHealth", 10), effect("fortifyAttribute", 5) };
        return { mwmp::records::CurrentSchemaVersion, std::move(potion) };
    }
}

TEST(DynamicRecord, Sha256UsesStandardDigest)
{
    EXPECT_EQ(mwmp::crypto::sha256hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(DynamicRecord, DefinitionCodecRoundTripsEverySupportedType)
{
    using namespace mwmp::records;
    std::vector<DynamicRecordDefinition> definitions;
    definitions.push_back(potionDefinition());

    Enchantment enchantment;
    enchantment.type = 2;
    enchantment.cost = 10;
    enchantment.charge = 100;
    enchantment.effects = { effect("fireDamage", 4) };
    definitions.push_back({ CurrentSchemaVersion, enchantment });

    Weapon weapon;
    weapon.item.name = "Blade";
    weapon.item.weight = 8.f;
    weapon.item.value = 100;
    weapon.enchantment = { ReferenceKind::ContentId, "$custom_enchantment_1" };
    weapon.type = 1;
    weapon.health = 500;
    weapon.speed = 1.2f;
    weapon.reach = 1.f;
    weapon.chop = { 4, 12 };
    weapon.slash = { 3, 10 };
    weapon.thrust = { 2, 8 };
    definitions.push_back({ CurrentSchemaVersion, weapon });

    Armor armor;
    armor.item.name = "Cuirass";
    armor.item.weight = 20.f;
    armor.item.value = 200;
    armor.type = 1;
    armor.health = 800;
    armor.armorRating = 30;
    armor.parts.push_back({ 3, "male_part", "female_part" });
    definitions.push_back({ CurrentSchemaVersion, armor });

    Clothing clothing;
    clothing.item.name = "Robe";
    clothing.item.weight = 3.f;
    clothing.item.value = 50;
    clothing.type = 4;
    clothing.parts.push_back({ 3, "robe_m", "robe_f" });
    definitions.push_back({ CurrentSchemaVersion, clothing });

    Book book;
    book.item.name = "Scroll";
    book.item.weight = 0.2f;
    book.item.value = 25;
    book.text = "Words of power";
    book.isScroll = true;
    definitions.push_back({ CurrentSchemaVersion, book });

    for (const DynamicRecordDefinition& definition : definitions)
    {
        SCOPED_TRACE(getRecordTypeName(getRecordType(definition)));
        EXPECT_EQ(decodeDefinition(encodeDefinition(definition)), definition);
        EXPECT_TRUE(validate(definition).empty());

        const DynamicRecordDefinition normalized = canonicalize(definition);
        const EsmDynamicRecord esm = toEsmRecord(normalized);
        const DynamicRecordDefinition converted = std::visit(
            [](const auto& record) { return fromEsmRecord(record); }, esm);
        EXPECT_EQ(encodeDefinition(converted), encodeDefinition(normalized));
    }
}

TEST(DynamicRecord, BundleCodecRoundTripsRelatedRecords)
{
    using namespace mwmp::records;
    Enchantment enchantment;
    enchantment.type = 2;
    Weapon weapon;
    weapon.item.weight = 1.f;
    weapon.enchantment = { ReferenceKind::TemporaryKey, "enchantment" };

    DynamicRecordBundle bundle;
    bundle.records = { { "enchantment", { CurrentSchemaVersion, enchantment } },
        { "item", { CurrentSchemaVersion, weapon } } };
    bundle.dependencies = { { "item", "enchantment" } };

    EXPECT_TRUE(validate(bundle).empty());
    EXPECT_EQ(decodeBundle(encodeBundle(bundle)), bundle);
}

TEST(DynamicRecord, CanonicalFingerprintIgnoresCaseAndEffectOrderWhereSemanticsAreEquivalent)
{
    using namespace mwmp::records;
    DynamicRecordDefinition first = potionDefinition();
    DynamicRecordDefinition second = first;
    auto& potion = std::get<Potion>(second.data);
    potion.item.model = "meshes/potion.nif";
    potion.item.icon = "ICONS\\POTION.DDS";
    std::reverse(potion.effects.begin(), potion.effects.end());
    for (MagicEffect& item : potion.effects)
        item.effectId[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(item.effectId[0])));

    EXPECT_EQ(fingerprint(first), fingerprint(second));
}

TEST(DynamicRecord, ValidationRejectsMalformedAndCyclicBundles)
{
    using namespace mwmp::records;
    Weapon weapon;
    weapon.item.weight = -1.f;
    weapon.enchantment = { ReferenceKind::TemporaryKey, "missing" };
    weapon.chop = { 10, 2 };

    DynamicRecordBundle bundle;
    bundle.records = { { "a", { CurrentSchemaVersion, weapon } }, { "b", potionDefinition() } };
    bundle.dependencies = { { "a", "b" }, { "b", "a" }, { "a", "missing" } };

    const std::vector<ValidationError> errors = validate(bundle);
    EXPECT_FALSE(errors.empty());
    EXPECT_NE(std::find_if(errors.begin(), errors.end(), [](const ValidationError& error) {
        return error.code == "dependency_cycle";
    }), errors.end());
    EXPECT_NE(std::find_if(errors.begin(), errors.end(), [](const ValidationError& error) {
        return error.code == "missing_dependency_record";
    }), errors.end());
    EXPECT_NE(std::find_if(errors.begin(), errors.end(), [](const ValidationError& error) {
        return error.code == "invalid_weapon_damage";
    }), errors.end());
}

TEST(DynamicRecord, DecoderRejectsTruncationAndTrailingBytes)
{
    const std::string encoded = mwmp::records::encodeDefinition(potionDefinition());
    EXPECT_THROW(mwmp::records::decodeDefinition(std::string_view(encoded).substr(0, encoded.size() - 1)),
        std::runtime_error);
    EXPECT_THROW(mwmp::records::decodeDefinition(encoded + "x"), std::runtime_error);
}

TEST(DynamicRecord, CreateRequestAndResultPacketsRoundTripStableProtocolValues)
{
    using namespace mwmp::records;
    mwmp::PacketRecordCreateRequest outgoingRequest;
    outgoingRequest.request.requestId = "request-1";
    outgoingRequest.request.operation = CreateOperation::Alchemy;
    outgoingRequest.request.inventoryRevision = 17;
    outgoingRequest.request.bundle.records = { { "potion", potionDefinition() } };
    outgoingRequest.request.evidence = "recipe-evidence";

    mwmp::PacketRecordCreateRequest incomingRequest;
    ASSERT_TRUE(incomingRequest.decode(outgoingRequest.encode()));
    EXPECT_EQ(incomingRequest.request, outgoingRequest.request);

    mwmp::PacketRecordCreateResult outgoingResult;
    outgoingResult.result.requestId = "request-1";
    outgoingResult.result.accepted = true;
    outgoingResult.result.inventoryRevision = 18;
    outgoingResult.result.commitSequence = 4;
    outgoingResult.result.records.push_back(
        { "potion", "$custom_potion_4", false, encodeDefinition(potionDefinition()) });

    mwmp::PacketRecordCreateResult incomingResult;
    ASSERT_TRUE(incomingResult.decode(outgoingResult.encode()));
    EXPECT_EQ(incomingResult.result, outgoingResult.result);
    EXPECT_EQ(getCreateErrorCode(CreateError::DuplicateRequestConflict), "duplicate_request_conflict");
}

TEST(DynamicRecord, HandshakeRoundTripsResolvedContentAndRuntimeManifest)
{
    mwmp::PacketHandshake outgoing;
    outgoing.clientVersion = "test";
    outgoing.playerName = "player";
    outgoing.resolvedContentFingerprint = std::string(64, 'a');
    outgoing.plugins.push_back({ "base.esm", std::string(64, 'b') });
    outgoing.luaScripts.push_back({ "scripts/load.lua", std::string(64, 'c') });

    mwmp::PacketHandshake incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.contentManifestVersion, mwmp::ContentManifestVersion);
    EXPECT_EQ(incoming.dynamicRecordWireVersion, mwmp::records::CurrentWireVersion);
    EXPECT_EQ(incoming.resolvedContentFingerprint, outgoing.resolvedContentFingerprint);
    ASSERT_EQ(incoming.luaScripts.size(), 1u);
    EXPECT_EQ(incoming.luaScripts.front().filename, "scripts/load.lua");

    mwmp::PacketHandshakeResponse response;
    response.accepted = true;
    response.resolvedContentFingerprint = outgoing.resolvedContentFingerprint;
    response.supportedRuntimeRecordTypes = { 1, 2, 6 };
    mwmp::PacketHandshakeResponse decoded;
    ASSERT_TRUE(decoded.decode(response.encode()));
    EXPECT_EQ(decoded.supportedRuntimeRecordTypes, response.supportedRuntimeRecordTypes);
}
