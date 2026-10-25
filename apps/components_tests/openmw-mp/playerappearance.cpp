#include "../../openmw-server/PlayerDatabase.hpp"
#include <chrono>
#include <components/openmw-mp/PlayerAppearance.hpp>
#include <filesystem>
#include <gtest/gtest.h>

namespace
{
    TEST(PlayerAppearance, FiltersBodyPartsByRecordMetadata)
    {
        const auto race = ESM::RefId::stringRefId("custom_mod_race_001");
        ESM::BodyPart part;
        part.blank();
        part.mRace = race;
        part.mData.mPart = ESM::BodyPart::MP_Head;
        part.mData.mType = ESM::BodyPart::MT_Skin;
        part.mModel = "custom/head.nif";
        EXPECT_TRUE(mwmp::appearancePartMatches(part, race, true, ESM::BodyPart::MP_Head));
        EXPECT_FALSE(mwmp::appearancePartMatches(part, race, false, ESM::BodyPart::MP_Head));
        EXPECT_FALSE(mwmp::appearancePartMatches(
            part, ESM::RefId::stringRefId("another_mod_race"), true, ESM::BodyPart::MP_Head));
        EXPECT_FALSE(mwmp::appearancePartMatches(part, race, true, ESM::BodyPart::MP_Hair));
        part.mData.mFlags = ESM::BodyPart::BPF_Female;
        EXPECT_TRUE(mwmp::appearancePartMatches(part, race, false, ESM::BodyPart::MP_Head));
        part.mData.mFlags |= ESM::BodyPart::BPF_NotPlayable;
        EXPECT_FALSE(mwmp::appearancePartMatches(part, race, false, ESM::BodyPart::MP_Head));
        part.mData.mFlags = ESM::BodyPart::BPF_Female;
        part.mData.mVampire = 1;
        EXPECT_FALSE(mwmp::appearancePartMatches(part, race, false, ESM::BodyPart::MP_Head));
        part.mData.mVampire = 0;
        part.mData.mType = ESM::BodyPart::MT_Armor;
        EXPECT_FALSE(mwmp::appearancePartMatches(part, race, false, ESM::BodyPart::MP_Head));
        part.mData.mType = ESM::BodyPart::MT_Skin;
        part.mModel.clear();
        EXPECT_FALSE(mwmp::appearancePartMatches(part, race, false, ESM::BodyPart::MP_Head));
    }

    TEST(PlayerAppearance, BoundsIdentifiersAndAllowsBald)
    {
        EXPECT_TRUE(mwmp::validAppearanceId("mod_race:head-001"));
        EXPECT_TRUE(mwmp::validAppearanceId("", true));
        EXPECT_FALSE(mwmp::validAppearanceId(""));
        EXPECT_FALSE(mwmp::validAppearanceId(std::string(257, 'x')));
        EXPECT_FALSE(mwmp::validAppearanceId(std::string("head\0suffix", 11)));
        EXPECT_FALSE(mwmp::validAppearanceId("head\nother"));
        EXPECT_FALSE(mwmp::validAppearanceId("head\x7f"));
    }

    struct TemporaryAppearanceDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-appearance-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + ".db");
        ~TemporaryAppearanceDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    TEST(PlayerAppearance, PersistsOnlyCosmeticFieldsAndSurvivesReconnect)
    {
        TemporaryAppearanceDatabase temporary;
        int64_t account, character, other;
        {
            mwmp::PlayerDatabase db(temporary.path.string());
            account = db.createAccount("appearance-test");
            character = db.createCharacter(account, "Keep Progress").characterId;
            other = db.createCharacter(account, "Other Slot").characterId;
            db.saveChargenData(character, "old_race", "old_head", "old_hair", true, "mage", "Custom Mage", "the_mage",
                "keep-class-data");
            db.markChargenComplete(character);
            db.savePosition(character, "Saved interior", 12, 34, 56, .1f, .2f, .3f);
            db.setNickname(character, "Keep Nickname");
            mwmp::Item item;
            item.refId = "iron_dagger";
            item.count = 2;
            item.instanceId = 42;
            db.saveCharacterInventory(character, { item });
            EXPECT_TRUE(db.saveCharacterAppearance(character, "new_mod_race", "new_mod_head", "", false));
            EXPECT_FALSE(db.saveCharacterAppearance(9999999, "new_mod_race", "new_mod_head", "", false));
        }
        mwmp::PlayerDatabase db(temporary.path.string());
        const auto record = db.lookupCharacter(account, "Keep Progress");
        ASSERT_TRUE(record);
        EXPECT_EQ(record->race, "new_mod_race");
        EXPECT_EQ(record->headMesh, "new_mod_head");
        EXPECT_TRUE(record->hairMesh.empty());
        EXPECT_FALSE(record->isMale);
        EXPECT_FALSE(record->isNew);
        EXPECT_EQ(record->classId, "mage");
        EXPECT_EQ(record->className, "Custom Mage");
        EXPECT_EQ(record->classData, "keep-class-data");
        EXPECT_EQ(record->birthSign, "the_mage");
        EXPECT_EQ(record->nickname, "Keep Nickname");
        EXPECT_EQ(record->cell, "Saved interior");
        EXPECT_FLOAT_EQ(record->posX, 12);
        EXPECT_FLOAT_EQ(record->posY, 34);
        EXPECT_FLOAT_EQ(record->posZ, 56);
        const auto inventory = db.loadCharacterInventory(character);
        ASSERT_EQ(inventory.size(), 1);
        EXPECT_EQ(inventory.front().instanceId, 42);
        EXPECT_EQ(inventory.front().count, 2);
        const auto untouched = db.lookupCharacter(account, "Other Slot");
        ASSERT_TRUE(untouched);
        EXPECT_EQ(untouched->characterId, other);
        EXPECT_TRUE(untouched->isNew);
        EXPECT_TRUE(untouched->race.empty());
    }
}
