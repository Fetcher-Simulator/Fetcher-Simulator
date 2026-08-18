#include "pickpocketitemmodel.hpp"

#include <components/esm3/loadskil.hpp>
#include <components/misc/rng.hpp>
#include <components/debug/debuglog.hpp>

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/pickpocket.hpp"

#include "../mwmp/Main.hpp"
#include "../mwmp/sync/WorldObjectSync.hpp"

#include "../mwworld/class.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

namespace MWGui
{

    PickpocketItemModel::PickpocketItemModel(
        const MWWorld::Ptr& actor, std::unique_ptr<ItemModel> sourceModel, bool hideItems)
        : mActor(actor)
        , mPickpocketDetected(false)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        mSourceModel = std::move(sourceModel);
        float chance = player.getClass().getSkill(player, ESM::Skill::Sneak);

        mSourceModel->update();
        // build list of items that player is unable to find when attempts to pickpocket.
        if (hideItems)
        {
            auto& prng = MWBase::Environment::get().getWorld()->getPrng();
            for (size_t i = 0; i < mSourceModel->getItemCount(); ++i)
            {
                if (Misc::Rng::roll0to99(prng) > chance)
                {
                    const ItemStack hidden = mSourceModel->getItem(static_cast<ModelIndex>(i));
                    mHiddenItems.emplace_back(hidden.mBase.getCellRef().getRefId().serializeText(),
                        static_cast<int>(hidden.mBase.getCellRef().getCharge()));
                }
            }
        }
    }

    bool PickpocketItemModel::allowedToUseItems() const
    {
        return false;
    }

    ItemStack PickpocketItemModel::getItem(ModelIndex index)
    {
        if (index < 0)
            throw std::runtime_error("Invalid index supplied");
        if (mItems.size() <= static_cast<size_t>(index))
            throw std::runtime_error("Item index out of range");
        return mItems[index];
    }

    size_t PickpocketItemModel::getItemCount()
    {
        return mItems.size();
    }

    void PickpocketItemModel::update()
    {
        mSourceModel->update();
        mItems.clear();
        for (size_t i = 0; i < mSourceModel->getItemCount(); ++i)
        {
            const ItemStack& item = mSourceModel->getItem(static_cast<ModelIndex>(i));

            // Bound items may not be stolen
            if (item.mFlags & ItemStack::Flag_Bound)
                continue;

            const HiddenItemIdentity identity {
                item.mBase.getCellRef().getRefId().serializeText(),
                static_cast<int>(item.mBase.getCellRef().getCharge()) };
            if (std::find(mHiddenItems.begin(), mHiddenItems.end(), identity) == mHiddenItems.end()
                && item.mType != ItemStack::Type_Equipped)
                mItems.push_back(item);
        }
    }

    bool PickpocketItemModel::onDropItem(const MWWorld::Ptr& item, int count)
    {
        // don't allow "reverse pickpocket" (it will be handled by scripts after 1.0)
        return false;
    }

    void PickpocketItemModel::onClose()
    {
        // Connected multiplayer finalizes pickpocket sessions from the actual
        // GM_Container removal boundary in WindowManager. The visibility callback
        // is not guaranteed for every user-facing close path and can also run for
        // temporary hiding, so it must not own the authoritative finish request.
        if (mwmp::Main::isInitialised())
            return;

        // Make sure we were actually closed, rather than just temporarily hidden (e.g. console or main menu opened)
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Container)
            // If it was already detected while taking an item, no need to check now
            || mPickpocketDetected)
            return;

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWMechanics::Pickpocket pickpocket(player, mActor);
        if (pickpocket.finish())
        {
            MWBase::Environment::get().getMechanicsManager()->commitCrime(
                player, mActor, MWBase::MechanicsManager::OT_Pickpocket, ESM::RefId(), 0, true);
            mPickpocketDetected = true;
            MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Container);
        }
    }

    bool PickpocketItemModel::onTakeItem(const MWWorld::Ptr& item, int count)
    {
        if (mActor.getClass().getCreatureStats(mActor).getKnockedDown())
            return mSourceModel->onTakeItem(item, count);

        if (mwmp::Main::isInitialised())
        {
            mwmp::Main::get().getWorldObjectSync().requestInventoryTake(
                mActor, item, count, mwmp::InventoryTakeKind::Pickpocket);
            return false;
        }

        bool success = stealItem(item, count);
        if (success)
        {
            MWWorld::Ptr player = MWMechanics::getPlayer();
            MWBase::Environment::get().getMechanicsManager()->itemTaken(player, item, mActor, count, false);
        }

        return success;
    }

    bool PickpocketItemModel::stealItem(const MWWorld::Ptr& item, int count)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWMechanics::Pickpocket pickpocket(player, mActor);
        if (pickpocket.pick(item, count))
        {
            MWBase::Environment::get().getMechanicsManager()->commitCrime(
                player, mActor, MWBase::MechanicsManager::OT_Pickpocket, ESM::RefId(), 0, true);
            mPickpocketDetected = true;
            MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Container);
            return false;
        }
        else
            player.getClass().skillUsageSucceeded(player, ESM::Skill::Sneak, ESM::Skill::Sneak_PickPocket);

        return true;
    }
}
