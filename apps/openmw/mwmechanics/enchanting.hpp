#ifndef GAME_MWMECHANICS_ENCHANTING_H
#define GAME_MWMECHANICS_ENCHANTING_H

#include <string>
#include <vector>

#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadench.hpp>

#include "../mwworld/ptr.hpp"

namespace Crafting
{
    struct EnchantingMechanicsInput;
}

#ifdef BUILD_MULTIPLAYER
namespace mwmp::records
{
    struct EnchantingRequest;
}
#endif

namespace MWMechanics
{
    class Enchanting
    {
        MWWorld::Ptr mOldItemPtr;
        MWWorld::Ptr mSoulGemPtr;
        MWWorld::Ptr mEnchanter;

        int mCastStyle;

        bool mSelfEnchanting;

        ESM::EffectList mEffectList;

        std::string mNewItemName;
        unsigned int mObjectType;
        int mWeaponType;

        const ESM::Enchantment* getRecord(const ESM::Enchantment& newEnchantment) const;
        int getBaseCastCost() const; // To be saved in the enchantment's record
        int getEnchantItemsCount() const;
        void payForEnchantment(int count) const;
        int getEnchantPrice(int count) const;

        /// Resolves the current selections into the shared enchanting
        /// calculation inputs (statistics, content lookups, GMSTs).
        Crafting::EnchantingMechanicsInput makeMechanicsInput() const;

    public:
        Enchanting();
        void setEnchanter(const MWWorld::Ptr& enchanter);
        void setSelfEnchanting(bool selfEnchanting);
        void setOldItem(const MWWorld::Ptr& oldItem);
        MWWorld::Ptr getOldItem() { return mOldItemPtr; }
        MWWorld::Ptr getGem() { return mSoulGemPtr; }
        void setNewItemName(const std::string& s);
        void setEffect(const ESM::EffectList& effectList);
        void setSoulGem(const MWWorld::Ptr& soulGem);
        bool create(); // Return true if created, false if failed.
        bool requiresServerAuthority() const;
        void nextCastStyle(); // Set enchant type to next possible type (for mOldItemPtr object)
        int getCastStyle() const;
        float getEnchantPoints(bool precise = true) const;
        int getEffectiveCastCost()
            const; // Effective cost taking player Enchant skill into account, used for preview purposes in the UI
        int getEnchantPrice() const { return getEnchantPrice(getEnchantItemsCount()); }
        int getMaxEnchantValue() const;
        int getGemCharge() const;
        int getEnchantChance() const;
        bool soulEmpty() const; // Return true if empty
        bool itemEmpty() const; // Return true if empty

#ifdef BUILD_MULTIPLAYER
        /// Captures the current selections as a semantic multiplayer
        /// enchanting request. Only player choices are captured: the selected
        /// inventory instances, cast style, item name, and effect list. No
        /// calculation, record, or mutation is performed locally.
        /// \return false and sets \a error when a selection cannot be
        /// referenced by a stable inventory instance yet.
        bool captureMultiplayerRequest(mwmp::records::EnchantingRequest& request, std::string& error) const;
#endif
    };
}
#endif
