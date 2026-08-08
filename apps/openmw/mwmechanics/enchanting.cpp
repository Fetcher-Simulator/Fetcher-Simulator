#include "enchanting.hpp"

#include <algorithm>

#include <components/enchanting/EnchantingMechanics.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/values.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "actorutil.hpp"
#include "creaturestats.hpp"

#ifdef BUILD_MULTIPLAYER
#include "../mwmp/Main.hpp"
#include "../mwmp/sync/ActorSync.hpp"
#include "../mwmp/sync/InventoryIdentity.hpp"
#include <components/openmw-mp/Records/EnchantingProtocol.hpp>
#endif

namespace MWMechanics
{
    Enchanting::Enchanting()
        : mCastStyle(ESM::Enchantment::CastOnce)
        , mSelfEnchanting(false)
        , mObjectType(0)
        , mWeaponType(-1)
    {
    }

    void Enchanting::setOldItem(const MWWorld::Ptr& oldItem)
    {
        mOldItemPtr = oldItem;
        mWeaponType = -1;
        mObjectType = 0;
        if (!itemEmpty())
        {
            mObjectType = mOldItemPtr.getType();
            if (mObjectType == ESM::Weapon::sRecordId)
                mWeaponType = mOldItemPtr.get<ESM::Weapon>()->mBase->mData.mType;
        }
    }

    void Enchanting::setNewItemName(const std::string& s)
    {
        mNewItemName = s;
    }

    void Enchanting::setEffect(const ESM::EffectList& effectList)
    {
        mEffectList = effectList;
    }

    int Enchanting::getCastStyle() const
    {
        return mCastStyle;
    }

    void Enchanting::setSoulGem(const MWWorld::Ptr& soulGem)
    {
        mSoulGemPtr = soulGem;
    }

    bool Enchanting::create()
    {
        if (requiresServerAuthority())
            return false;

        const MWWorld::Ptr& player = getPlayer();
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);
        ESM::Enchantment enchantment;
        enchantment.mData.mFlags = 0;
        enchantment.mData.mType = mCastStyle;
        enchantment.mData.mCost = getBaseCastCost();
        enchantment.mRecordFlags = 0;

        store.remove(mSoulGemPtr, 1);

        // Exception for Azura Star, new one will be added after enchanting
        auto azurasStarId = ESM::RefId::stringRefId("Misc_SoulGem_Azura");
        if (mSoulGemPtr.get<ESM::Miscellaneous>()->mBase->mId == azurasStarId)
            store.add(azurasStarId, 1);

        if (mSelfEnchanting)
        {
            auto& prng = MWBase::Environment::get().getWorld()->getPrng();
            if (!Crafting::EnchantingMechanics::rollSuccess(makeMechanicsInput(), prng))
                return false;

            mEnchanter.getClass().skillUsageSucceeded(
                mEnchanter, ESM::Skill::Enchant, ESM::Skill::Enchant_CreateMagicItem);
        }

        enchantment.mEffects = mEffectList;

        int count = getEnchantItemsCount();
        enchantment.mData.mCharge = Crafting::EnchantingMechanics::enchantmentCharge(makeMechanicsInput(), count);

        // Try to find a dynamic enchantment with the same stats, create a new one if not found.
        const ESM::Enchantment* enchantmentPtr = getRecord(enchantment);
        if (enchantmentPtr == nullptr)
            enchantmentPtr = MWBase::Environment::get().getESMStore()->insert(enchantment);

        // Apply the enchantment
        const ESM::RefId& newItemId
            = mOldItemPtr.getClass().applyEnchantment(mOldItemPtr, enchantmentPtr->mId, getGemCharge(), mNewItemName);

        if (!mSelfEnchanting)
            payForEnchantment(count);

        // Add the new item to player inventory and remove the old one
        store.remove(mOldItemPtr, count);
        store.add(newItemId, count);

        return true;
    }

    bool Enchanting::requiresServerAuthority() const
    {
#ifdef BUILD_MULTIPLAYER
        return mwmp::Main::isConnected();
#else
        return false;
#endif
    }

    void Enchanting::nextCastStyle()
    {
        if (itemEmpty())
            return;

        mCastStyle = Crafting::EnchantingMechanics::nextCastStyle(makeMechanicsInput(), mCastStyle);
    }

    int Enchanting::getBaseCastCost() const
    {
        return Crafting::EnchantingMechanics::baseCastCost(makeMechanicsInput());
    }

    float Enchanting::getEnchantPoints(bool precise) const
    {
        return Crafting::EnchantingMechanics::enchantPoints(makeMechanicsInput(), precise);
    }

    int Enchanting::getEffectiveCastCost() const
    {
        return Crafting::EnchantingMechanics::effectiveCastCost(makeMechanicsInput());
    }

    const ESM::Enchantment* Enchanting::getRecord(const ESM::Enchantment& toFind) const
    {
        const MWWorld::Store<ESM::Enchantment>& enchantments
            = MWBase::Environment::get().getESMStore()->get<ESM::Enchantment>();
        MWWorld::Store<ESM::Enchantment>::iterator iter(enchantments.begin());
        iter += (enchantments.getSize() - enchantments.getDynamicSize());
        for (; iter != enchantments.end(); ++iter)
        {
            if (iter->mEffects.mList.size() != toFind.mEffects.mList.size())
                continue;

            if (iter->mData.mFlags != toFind.mData.mFlags || iter->mData.mType != toFind.mData.mType
                || iter->mData.mCost != toFind.mData.mCost || iter->mData.mCharge != toFind.mData.mCharge)
                continue;

            // Don't choose an ID that came from the content files, would have unintended side effects
            if (!enchantments.isDynamic(iter->mId))
                continue;

            bool mismatch = false;

            for (int i = 0; i < static_cast<int>(iter->mEffects.mList.size()); ++i)
            {
                if (iter->mEffects.mList[i] != toFind.mEffects.mList[i])
                {
                    mismatch = true;
                    break;
                }
            }

            if (!mismatch)
                return &(*iter);
        }

        return nullptr;
    }

    int Enchanting::getEnchantPrice(int count) const
    {
        return Crafting::EnchantingMechanics::enchantPrice(makeMechanicsInput(), count);
    }

    int Enchanting::getGemCharge() const
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        if (soulEmpty())
            return 0;
        if (mSoulGemPtr.getCellRef().getSoul().empty())
            return 0;
        const ESM::Creature* soul = store.get<ESM::Creature>().search(mSoulGemPtr.getCellRef().getSoul());
        if (soul)
            return soul->mData.mSoul;
        else
            return 0;
    }

    int Enchanting::getMaxEnchantValue() const
    {
        // The enchanting dialog asks for preview labels before the player has
        // selected a target item. Preserve the native empty-selection contract.
        if (itemEmpty())
            return 0;
        return Crafting::EnchantingMechanics::maxEnchantValue(makeMechanicsInput());
    }
    bool Enchanting::soulEmpty() const
    {
        return mSoulGemPtr.isEmpty();
    }

    bool Enchanting::itemEmpty() const
    {
        return mOldItemPtr.isEmpty();
    }

    void Enchanting::setSelfEnchanting(bool selfEnchanting)
    {
        mSelfEnchanting = selfEnchanting;
    }

    void Enchanting::setEnchanter(const MWWorld::Ptr& enchanter)
    {
        mEnchanter = enchanter;
        // Reset cast style
        mCastStyle = ESM::Enchantment::CastOnce;
    }

    int Enchanting::getEnchantChance() const
    {
        return Crafting::EnchantingMechanics::enchantChance(makeMechanicsInput());
    }

    int Enchanting::getEnchantItemsCount() const
    {
        // Native preview semantics use a count of one until a target item is selected.
        if (itemEmpty())
            return 1;
        const MWWorld::Ptr& player = getPlayer();
        const int availableCount = player.getClass().getContainerStore(player).count(mOldItemPtr.getCellRef().getRefId());
        return Crafting::EnchantingMechanics::enchantItemsCount(makeMechanicsInput(), availableCount);
    }

    Crafting::EnchantingMechanicsInput Enchanting::makeMechanicsInput() const
    {
        Crafting::EnchantingMechanicsInput input;
        input.itemType = static_cast<int>(mObjectType);
        input.weaponType = mWeaponType;
        input.gemCharge = getGemCharge();
        input.selfEnchanting = mSelfEnchanting;
        input.castStyle = mCastStyle;
        input.effects.reserve(mEffectList.mList.size());
        for (const ESM::IndexedENAMstruct& indexed : mEffectList.mList)
            input.effects.push_back(indexed.mData);
        input.projectilesEnchantMultiplier = Settings::game().mProjectilesEnchantMultiplier;

        const MWWorld::Ptr& player = getPlayer();
        if (!itemEmpty())
        {
            input.enchantCapacity = mOldItemPtr.getClass().getEnchantmentPoints(mOldItemPtr);
            if (mObjectType == ESM::Book::sRecordId)
                input.bookIsScroll = mOldItemPtr.get<ESM::Book>()->mBase->mData.mIsScroll != 0;
            input.availableCount
                = player.getClass().getContainerStore(player).count(mOldItemPtr.getCellRef().getRefId());
        }

        if (!mEnchanter.isEmpty())
        {
            const CreatureStats& creatureStats = mEnchanter.getClass().getCreatureStats(mEnchanter);
            input.enchantSkill = mEnchanter.getClass().getSkill(mEnchanter, ESM::Skill::Enchant);
            input.intelligence = creatureStats.getAttribute(ESM::Attribute::Intelligence).getModified();
            input.luck = creatureStats.getAttribute(ESM::Attribute::Luck).getModified();
            input.fatigueTerm = creatureStats.getFatigueTerm();
        }

        if (!mSelfEnchanting && !mEnchanter.isEmpty())
        {
            // Paid service pricing. NPCs use the barter formula with the full
            // client-side disposition and statistics; creature merchants keep
            // the native base-price special case.
            const CreatureStats& playerStats = player.getClass().getCreatureStats(player);
            const CreatureStats& paidEnchanterStats = mEnchanter.getClass().getCreatureStats(mEnchanter);
            Crafting::EnchantingBarterInput barter;
            barter.playerMercantile = player.getClass().getSkill(player, ESM::Skill::Mercantile);
            barter.playerLuck = playerStats.getAttribute(ESM::Attribute::Luck).getModified();
            barter.playerPersonality = playerStats.getAttribute(ESM::Attribute::Personality).getModified();
            barter.playerFatigueTerm = playerStats.getFatigueTerm();
            if (mEnchanter.getClass().isNpc())
            {
                barter.enchanterMercantile = mEnchanter.getClass().getSkill(mEnchanter, ESM::Skill::Mercantile);
                barter.enchanterLuck = paidEnchanterStats.getAttribute(ESM::Attribute::Luck).getModified();
                barter.enchanterPersonality = paidEnchanterStats.getAttribute(ESM::Attribute::Personality).getModified();
                barter.enchanterFatigueTerm = paidEnchanterStats.getFatigueTerm();
                barter.disposition
                    = MWBase::Environment::get().getMechanicsManager()->getDerivedDisposition(mEnchanter);
            }
            else
            {
                barter.creatureMerchant = true;
            }
            input.barter = std::move(barter);
        }

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        input.magicEffect = [&store](const ESM::RefId& id) -> std::optional<Crafting::MagicEffectData> {
            const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(id);
            if (effect == nullptr)
                return std::nullopt;
            return Crafting::MagicEffectData{
                effect->mData.mBaseCost, static_cast<std::uint32_t>(effect->mData.mFlags) };
        };
        input.gmst = [&store](std::string_view id) -> std::optional<float> {
            const ESM::GameSetting* setting = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(id));
            if (setting == nullptr)
                return std::nullopt;
            return setting->mValue.getFloat();
        };

        return input;
    }

    void Enchanting::payForEnchantment(int count) const
    {
        const MWWorld::Ptr& player = getPlayer();
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);

        int price = getEnchantPrice(count);
        store.remove(MWWorld::ContainerStore::sGoldId, price);

        // add gold to NPC trading gold pool
        CreatureStats& enchanterStats = mEnchanter.getClass().getCreatureStats(mEnchanter);
        enchanterStats.setGoldPool(enchanterStats.getGoldPool() + price);
    }

#ifdef BUILD_MULTIPLAYER
    bool Enchanting::captureMultiplayerRequest(
        mwmp::records::EnchantingRequest& request, std::string& error) const
    {
        request.targetInstanceId = 0;
        request.soulGemInstanceId = 0;
        request.effects.clear();

        if (itemEmpty() || soulEmpty())
        {
            error = "An item and a soul gem must be selected.";
            return false;
        }

        request.targetInstanceId = mwmp::inventoryInstanceId(mOldItemPtr.getCellRef().getRefNum());
        if (request.targetInstanceId == 0)
        {
            error = "The target item has no stable inventory identity yet; try again in a moment.";
            return false;
        }
        request.soulGemInstanceId = mwmp::inventoryInstanceId(mSoulGemPtr.getCellRef().getRefNum());
        if (request.soulGemInstanceId == 0)
        {
            error = "The soul gem has no stable inventory identity yet; try again in a moment.";
            return false;
        }

        request.castStyle = mCastStyle;
        request.itemName = mNewItemName;
        request.selfEnchanting = mSelfEnchanting;
        request.enchanterNetId = 0;
        if (!mSelfEnchanting)
        {
            if (mEnchanter.isEmpty())
            {
                error = "No enchanter is selected.";
                return false;
            }
            const std::string cellId(MWBase::Environment::get().getWorld()->getCellName());
            request.enchanterNetId = mwmp::Main::get().getActorSync().actorNetIdForPtr(cellId, mEnchanter);
            if (request.enchanterNetId == 0)
            {
                error = "The enchanter has no server identity yet; try again in a moment.";
                return false;
            }
        }

        for (const ESM::IndexedENAMstruct& indexed : mEffectList.mList)
        {
            const ESM::ENAMstruct& effect = indexed.mData;
            mwmp::records::EnchantingEffectChoice choice;
            choice.effectId = effect.mEffectID.serializeText();
            choice.range = effect.mRange;
            choice.magnitudeMin = effect.mMagnMin;
            choice.magnitudeMax = effect.mMagnMax;
            choice.duration = effect.mDuration;
            choice.area = effect.mArea;
            choice.skillId = effect.mSkill.serializeText();
            choice.attributeId = effect.mAttribute.serializeText();
            request.effects.push_back(std::move(choice));
        }
        return true;
    }
#endif
}
