#include "container.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwmechanics/aipackage.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/summoning.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/sync/WorldObjectSync.hpp"

#include "../mwscript/interpretercontext.hpp"

#include "containeritemmodel.hpp"
#include "countdialog.hpp"
#include "draganddrop.hpp"
#include "inventoryitemmodel.hpp"
#include "inventorywindow.hpp"
#include "itemtransfer.hpp"
#include "itemview.hpp"
#include "pickpocketitemmodel.hpp"
#include "sortfilteritemmodel.hpp"
#include "tooltips.hpp"

#include "../mwworld/cellstore.hpp"

namespace MWGui
{
    namespace
    {
        std::string makeCellId(const MWWorld::Ptr& ptr)
        {
            const MWWorld::CellStore* store = ptr.getCell();
            if (!store || !store->getCell())
                return {};

            const MWWorld::Cell* cell = store->getCell();
            if (cell->isExterior())
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "EXT:%d,%d", cell->getGridX(), cell->getGridY());
                return buf;
            }

            return std::string(cell->getNameId());
        }

        using InventoryStackCounts = std::unordered_map<ESM::RefNum, std::size_t>;

        InventoryStackCounts snapshotPlayerStacks(ItemModel& model, const std::string& refId, int charge)
        {
            InventoryStackCounts counts;
            model.update();
            for (std::size_t i = 0; i < model.getItemCount(); ++i)
            {
                const ItemStack stack = model.getItem(static_cast<ItemModel::ModelIndex>(i));
                if (stack.mBase.getCellRef().getRefId().serializeText() == refId
                    && static_cast<int>(stack.mBase.getCellRef().getCharge()) == charge)
                    counts[stack.mBase.getCellRef().getRefNum()] = stack.mCount;
            }
            return counts;
        }

        ItemModel::ModelIndex findReceivedPlayerStack(ItemModel& model, const std::string& refId, int charge,
            const InventoryStackCounts& before, std::size_t& receivedCount)
        {
            ItemModel::ModelIndex bestIndex = -1;
            receivedCount = 0;
            model.update();
            for (std::size_t i = 0; i < model.getItemCount(); ++i)
            {
                const ItemStack stack = model.getItem(static_cast<ItemModel::ModelIndex>(i));
                if (stack.mBase.getCellRef().getRefId().serializeText() != refId
                    || static_cast<int>(stack.mBase.getCellRef().getCharge()) != charge)
                    continue;

                const auto previous = before.find(stack.mBase.getCellRef().getRefNum());
                const std::size_t previousCount = previous == before.end() ? 0 : previous->second;
                const std::size_t increase = stack.mCount > previousCount ? stack.mCount - previousCount : 0;
                if (increase > receivedCount)
                {
                    bestIndex = static_cast<ItemModel::ModelIndex>(i);
                    receivedCount = increase;
                }
            }
            return bestIndex;
        }
    }

    ContainerWindow::ContainerWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer)
        : WindowBase("openmw_container_window.layout")
        , mDragAndDrop(&dragAndDrop)
        , mItemTransfer(&itemTransfer)
        , mSortModel(nullptr)
        , mModel(nullptr)
        , mSelectedItem(-1)
        , mUpdateNextFrame(false)
        , mTreatNextOpenAsLoot(false)
    {
        getWidget(mDisposeCorpseButton, "DisposeCorpseButton");
        getWidget(mTakeButton, "TakeButton");
        getWidget(mCloseButton, "CloseButton");

        getWidget(mItemView, "ItemView");
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &ContainerWindow::onBackgroundSelected);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &ContainerWindow::onItemSelected);

        mDisposeCorpseButton->eventMouseButtonClick
            += MyGUI::newDelegate(this, &ContainerWindow::onDisposeCorpseButtonClicked);
        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onCloseButtonClicked);
        mTakeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onTakeAllButtonClicked);

        setCoord(200, 0, 600, 300);

        mControllerButtons.mA = "#{Interface:Take}";
        mControllerButtons.mB = "#{Interface:Close}";
        mControllerButtons.mX = "#{Interface:TakeAll}";
        mControllerButtons.mR3 = "#{Interface:Info}";
        mControllerButtons.mL2 = "#{Interface:Inventory}";
    }

    void ContainerWindow::onItemSelected(int index)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            dropItem();
            return;
        }

        const ItemStack& item = mSortModel->getItem(index);

        // We can't take a conjured item from a container (some NPC we're pickpocketing, a box, etc)
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sContentsMessage1}");
            return;
        }

        MWWorld::Ptr object = item.mBase;
        size_t count = item.mCount;
        bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
        if (MyGUI::InputManager::getInstance().isControlPressed())
            count = 1;

        mSelectedItem = mSortModel->mapToSource(index);

        if (count > 1 && !shift)
        {
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            std::string name{ object.getClass().getName(object) };
            name += MWGui::ToolTips::getSoulString(object.getCellRef());
            dialog->openCountDialog(name, "#{sTake}", static_cast<int>(count));
            dialog->eventOkClicked.clear();
            if (Settings::gui().mControllerMenus || MyGUI::InputManager::getInstance().isAltPressed())
                dialog->eventOkClicked += MyGUI::newDelegate(this, &ContainerWindow::transferItem);
            else
                dialog->eventOkClicked += MyGUI::newDelegate(this, &ContainerWindow::dragItem);
        }
        else if (Settings::gui().mControllerMenus || MyGUI::InputManager::getInstance().isAltPressed())
            transferItem(nullptr, count);
        else
            dragItem(nullptr, count);
    }

    void ContainerWindow::dragItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        if (mModel == nullptr)
            return;

        const ItemStack item = mModel->getItem(mSelectedItem);

        if (usesAuthoritativeInventoryTransfer())
        {
            requestAuthoritativeTake(item, count, true);
            return;
        }

        if (!mModel->onTakeItem(item.mBase, static_cast<int>(count)))
            return;

        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mModel, mItemView, count);
    }

    void ContainerWindow::transferItem(MyGUI::Widget* /*sender*/, std::size_t count)
    {
        if (mModel == nullptr)
            return;

        const ItemStack item = mModel->getItem(mSelectedItem);

        if (usesAuthoritativeInventoryTransfer())
        {
            requestAuthoritativeTake(item, count, false);
            return;
        }

        if (!mModel->onTakeItem(item.mBase, static_cast<int>(count)))
            return;

        mItemTransfer->apply(item, count, *mItemView);
    }

    void ContainerWindow::dropItem()
    {
        if (mModel == nullptr)
            return;

        const ItemStack item = mDragAndDrop->mItem;
        const std::size_t count = mDragAndDrop->mDraggedCount;
        bool success = mModel->onDropItem(item.mBase, static_cast<int>(count));

        if (!success)
            return;

        if (auto* containerModel = dynamic_cast<ContainerItemModel*>(mModel);
            containerModel && containerModel->usesAuthoritativeInventoryTransfer())
        {
            requestAuthoritativePut(item, count);
            return;
        }

        mDragAndDrop->drop(mModel, mItemView);
    }

    bool ContainerWindow::usesAuthoritativeInventoryTransfer() const
    {
        if (!mwmp::Main::isInitialised() || mModel == nullptr || mPtr.isEmpty())
            return false;

        // A multiplayer corpse must never fall back to the legacy local-mutation
        // Take All / Dispose path. InventoryItemModel::onTakeItem() already routes
        // non-player actor transfers through InventoryTake, so allowing a corpse
        // window to report non-authoritative here creates a mixed mode where the
        // UI can delete the corpse while those server requests are still pending.
        if (mPtr.getClass().isActor() && mPtr != MWMechanics::getPlayer()
            && mPtr.getClass().getCreatureStats(mPtr).isDead())
            return true;

        if (const auto* containerModel = dynamic_cast<const ContainerItemModel*>(mModel))
            return containerModel->usesAuthoritativeInventoryTransfer();

        if (const auto* inventoryModel = dynamic_cast<const InventoryItemModel*>(mModel))
            return inventoryModel->usesAuthoritativeInventoryTransfer();

        return false;
    }

    void ContainerWindow::requestAuthoritativeTake(const ItemStack& item, std::size_t count, bool startDrag)
    {
        if (mAuthoritativeTransferPending || count == 0 || mPtr.isEmpty())
            return;

        InventoryWindow* inventoryWindow = MWBase::Environment::get().getWindowManager()->getInventoryWindow();
        ItemModel* playerModel = inventoryWindow->getModel();
        const std::string itemRefId = item.mBase.getCellRef().getRefId().serializeText();
        const int itemCharge = static_cast<int>(item.mBase.getCellRef().getCharge());
        const ESM::RefId sound = startDrag ? item.mBase.getClass().getUpSoundId(item.mBase)
                                           : item.mBase.getClass().getDownSoundId(item.mBase);
        const InventoryStackCounts before = snapshotPlayerStacks(*playerModel, itemRefId, itemCharge);
        const std::uint64_t serial = ++mAuthoritativeTransferSerial;
        mAuthoritativeTransferPending = true;

        const mwmp::InventoryTakeKind kind = mPtr.getClass().isActor()
                && mPtr.getClass().getCreatureStats(mPtr).isDead()
            ? mwmp::InventoryTakeKind::Corpse : mwmp::InventoryTakeKind::Container;
        const bool queued = mwmp::Main::get().getWorldObjectSync().requestInventoryTake(mPtr, item.mBase,
            static_cast<int>(count), kind,
            [this, before, itemRefId, itemCharge, sound, serial, startDrag](const mwmp::InventoryTakeResult& result) {
                InventoryWindow* inventoryWindow = MWBase::Environment::get().getWindowManager()->getInventoryWindow();
                ItemModel* playerModel = inventoryWindow->getModel();
                playerModel->update();
                inventoryWindow->updateItemView();

                if (serial != mAuthoritativeTransferSerial)
                    return;
                mAuthoritativeTransferPending = false;

                if (!result.accepted)
                {
                    const bool takeAll = mAuthoritativeTakeAllPending;
                    mAuthoritativeTakeAllPending = false;
                    mDisposeAfterAuthoritativeTakeAll = false;
                    Log(Debug::Warning) << "[MP] ContainerWindow: authoritative take rejected item=" << itemRefId
                                        << " error=" << mwmp::getInventoryTakeErrorCode(result.error)
                                        << " takeAll=" << takeAll;
                    return;
                }

                if (mModel)
                    mModel->update();
                if (mItemView)
                    mItemView->update();

                if (mAuthoritativeTakeAllPending)
                {
                    MWBase::Environment::get().getWindowManager()->playSound(sound);
                    continueAuthoritativeTakeAll();
                    return;
                }

                if (!startDrag)
                {
                    MWBase::Environment::get().getWindowManager()->playSound(sound);
                    return;
                }

                std::size_t receivedCount = 0;
                const ItemModel::ModelIndex receivedIndex
                    = findReceivedPlayerStack(*playerModel, itemRefId, itemCharge, before, receivedCount);
                const std::size_t dragCount
                    = std::min<std::size_t>(receivedCount, static_cast<std::size_t>(result.itemCount));
                if (receivedIndex < 0 || dragCount == 0 || mModel == nullptr || mDragAndDrop->mIsOnDragAndDrop)
                {
                    MWBase::Environment::get().getWindowManager()->playSound(sound);
                    Log(Debug::Warning) << "[MP] ContainerWindow: authoritative take "
                                           "accepted without native cursor"
                                        << " item=" << itemRefId << " received=" << receivedCount;
                    return;
                }

                mDragAndDrop->startDrag(receivedIndex, inventoryWindow->getSortFilterModel(), playerModel,
                    inventoryWindow->getItemView(), dragCount);
                if (mItemView)
                    mItemView->update();
            });

        if (!queued)
        {
            mAuthoritativeTransferPending = false;
            mAuthoritativeTakeAllPending = false;
            mDisposeAfterAuthoritativeTakeAll = false;
            Log(Debug::Warning) << "[MP] ContainerWindow: could not queue authoritative take item=" << itemRefId;
        }
    }

    void ContainerWindow::requestAuthoritativeTakeAll(bool disposeAfter)
    {
        if (mAuthoritativeTransferPending || mAuthoritativeTakeAllPending || mModel == nullptr || mPtr.isEmpty())
            return;

        // Preserve vanilla corpse presentation while the server moves each stack
        // atomically. Unequip once before the first authoritative removal so
        // InventoryStore bookkeeping cannot re-equip items between acknowledgements.
        if (mPtr.getClass().hasInventoryStore(mPtr))
        {
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            mModel->update();
            for (std::size_t i = 0; i < mModel->getItemCount(); ++i)
            {
                const ItemStack item = mModel->getItem(static_cast<ItemModel::ModelIndex>(i));
                if (invStore.isEquipped(item.mBase))
                    invStore.unequipItem(item.mBase);
            }
        }

        mAuthoritativeTakeAllPending = true;
        mDisposeAfterAuthoritativeTakeAll = disposeAfter;
        continueAuthoritativeTakeAll();
    }

    void ContainerWindow::continueAuthoritativeTakeAll()
    {
        if (!mAuthoritativeTakeAllPending || mAuthoritativeTransferPending)
            return;
        if (mModel == nullptr || mPtr.isEmpty())
        {
            mAuthoritativeTakeAllPending = false;
            mDisposeAfterAuthoritativeTakeAll = false;
            return;
        }

        mModel->update();
        if (mItemView)
            mItemView->update();

        if (mModel->getItemCount() != 0)
        {
            const ItemStack item = mModel->getItem(0);
            requestAuthoritativeTake(item, item.mCount, false);
            return;
        }

        const bool disposeAfter = mDisposeAfterAuthoritativeTakeAll;
        const MWWorld::Ptr ptr = mPtr;
        mAuthoritativeTakeAllPending = false;
        mDisposeAfterAuthoritativeTakeAll = false;
        if (disposeAfter)
            disposeCorpseNow(ptr);
        else
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::requestAuthoritativePut(const ItemStack& item, std::size_t count)
    {
        if (mAuthoritativeTransferPending || count == 0 || mPtr.isEmpty())
            return;

        const std::string itemRefId = item.mBase.getCellRef().getRefId().serializeText();
        const ESM::RefId sound = item.mBase.getClass().getDownSoundId(item.mBase);
        const std::uint64_t serial = ++mAuthoritativeTransferSerial;
        mAuthoritativeTransferPending = true;

        const bool queued = mwmp::Main::get().getWorldObjectSync().requestInventoryPut(mPtr, item.mBase,
            static_cast<int>(count), [this, itemRefId, sound, serial](const mwmp::InventoryPutResult& result) {
                InventoryWindow* inventoryWindow = MWBase::Environment::get().getWindowManager()->getInventoryWindow();
                inventoryWindow->getModel()->update();
                inventoryWindow->updateItemView();

                if (serial != mAuthoritativeTransferSerial)
                    return;
                mAuthoritativeTransferPending = false;

                if (!result.accepted)
                {
                    Log(Debug::Warning) << "[MP] ContainerWindow: authoritative put rejected item=" << itemRefId
                                        << " error=" << mwmp::getInventoryPutErrorCode(result.error);
                    return;
                }

                MWBase::Environment::get().getWindowManager()->playSound(sound);
                if (mDragAndDrop->mIsOnDragAndDrop)
                    mDragAndDrop->finish();
                if (mModel)
                    mModel->update();
                if (mItemView)
                    mItemView->update();
            });

        if (!queued)
        {
            mAuthoritativeTransferPending = false;
            Log(Debug::Warning) << "[MP] ContainerWindow: could not queue authoritative put item=" << itemRefId;
        }
    }

    void ContainerWindow::onBackgroundSelected()
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
            dropItem();
    }

    void ContainerWindow::setPtr(const MWWorld::Ptr& container)
    {
        if (container.isEmpty() || (container.getType() != ESM::REC_CONT && !container.getClass().isActor()))
            throw std::runtime_error("Invalid argument in ContainerWindow::setPtr");
        bool lootAnyway = mTreatNextOpenAsLoot;
        mTreatNextOpenAsLoot = false;
        mPtr = container;
        mPickpocketFinishSent = false;
        mPickpocketDetected = false;

        bool loot = mPtr.getClass().isActor() && mPtr.getClass().getCreatureStats(mPtr).isDead();

        std::unique_ptr<ItemModel> model;
        bool shouldSyncOpen = false;
        if (mPtr.getClass().hasInventoryStore(mPtr))
        {
            if (mPtr.getClass().isNpc() && !loot && !lootAnyway)
            {
                // we are stealing stuff
                model = std::make_unique<PickpocketItemModel>(mPtr, std::make_unique<InventoryItemModel>(container),
                    !mPtr.getClass().getCreatureStats(mPtr).getKnockedDown());
            }
            else
            {
                model = std::make_unique<InventoryItemModel>(container);
                shouldSyncOpen = loot;
            }
        }
        else
        {
            model = std::make_unique<ContainerItemModel>(container);
            shouldSyncOpen = true;
        }

        if (shouldSyncOpen)
            mwmp::Main::get().getWorldObjectSync().onLocalContainerOpened(container);

        mDisposeCorpseButton->setVisible(loot);
        mModel = model.get();
        auto sortModel = std::make_unique<SortFilterItemModel>(std::move(model));
        mSortModel = sortModel.get();

        mItemView->setModel(std::move(sortModel));
        mItemView->resetScrollBars();

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

        setTitle(container.getClass().getName(container));
    }

    void ContainerWindow::resetReference()
    {
        ++mAuthoritativeTransferSerial;
        mAuthoritativeTransferPending = false;
        mAuthoritativeTakeAllPending = false;
        mDisposeAfterAuthoritativeTakeAll = false;
        ReferenceInterface::resetReference();
        mItemView->setModel(nullptr);
        mModel = nullptr;
        mSortModel = nullptr;
    }

    void ContainerWindow::onOpen()
    {
        mItemTransfer->addTarget(*mItemView);
    }

    void ContainerWindow::finalizePickpocketSession()
    {
        if (!mwmp::Main::isInitialised() || mPickpocketFinishSent || mPickpocketDetected
            || mPtr.isEmpty() || dynamic_cast<PickpocketItemModel*>(mModel) == nullptr)
            return;
        if (mwmp::Main::get().getWorldObjectSync().isSuppressingPickpocketFinish())
        {
            mPickpocketDetected = true;
            return;
        }

        mPickpocketFinishSent
            = mwmp::Main::get().getWorldObjectSync().requestPickpocketFinish(mPtr);
        Log(mPickpocketFinishSent ? Debug::Info : Debug::Warning)
            << "[MP] ContainerWindow: pickpocket session finish request"
            << " actor=" << mPtr.getCellRef().getRefId().serializeText()
            << " sent=" << mPickpocketFinishSent;
    }

    void ContainerWindow::onClose()
    {
        // Make sure the window was actually closed and not temporarily hidden.
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Container))
            return;

        if (mModel)
            mModel->onClose();

        if (!mPtr.isEmpty())
            MWBase::Environment::get().getMechanicsManager()->onClose(mPtr);
        resetReference();

        mItemTransfer->removeTarget(*mItemView);
    }

    void ContainerWindow::onCloseButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onTakeAllButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (!mModel)
            return;
        if (mDragAndDrop != nullptr && mDragAndDrop->mIsOnDragAndDrop)
            return;

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

        // transfer everything into the player's inventory
        ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
        assert(mModel);
        mModel->update();

        if (usesAuthoritativeInventoryTransfer())
        {
            requestAuthoritativeTakeAll(false);
            return;
        }

        // unequip all items to avoid unequipping/reequipping
        if (mPtr.getClass().hasInventoryStore(mPtr))
        {
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            for (size_t i = 0; i < mModel->getItemCount(); ++i)
            {
                const ItemStack& item = mModel->getItem(static_cast<ItemModel::ModelIndex>(i));
                if (invStore.isEquipped(item.mBase) == false)
                    continue;

                invStore.unequipItem(item.mBase);
            }
        }

        mModel->update();

        if (auto* containerModel = dynamic_cast<ContainerItemModel*>(mModel))
            containerModel->beginSyncBatch(mwmp::ContainerAction::Remove);
        else if (auto* inventoryModel = dynamic_cast<InventoryItemModel*>(mModel))
            inventoryModel->beginSyncBatch(mwmp::ContainerAction::Remove);

        for (size_t i = 0; i < mModel->getItemCount(); ++i)
        {
            if (i == 0)
            {
                // play the sound of the first object
                MWWorld::Ptr item = mModel->getItem(static_cast<ItemModel::ModelIndex>(i)).mBase;
                const ESM::RefId& sound = item.getClass().getUpSoundId(item);
                MWBase::Environment::get().getWindowManager()->playSound(sound);
            }

            const ItemStack item = mModel->getItem(static_cast<ItemModel::ModelIndex>(i));

            if (!mModel->onTakeItem(item.mBase, static_cast<int>(item.mCount)))
                break;

            mModel->moveItem(item, item.mCount, playerModel);
        }

        if (auto* containerModel = dynamic_cast<ContainerItemModel*>(mModel))
            containerModel->endSyncBatch();
        else if (auto* inventoryModel = dynamic_cast<InventoryItemModel*>(mModel))
            inventoryModel->endSyncBatch();

        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::disposeCorpseNow(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty())
            return;

        if (ptr.getClass().isPersistent(ptr))
            MWBase::Environment::get().getWindowManager()->messageBox("#{sDisposeCorpseFail}");
        else
        {
            MWMechanics::CreatureStats& creatureStats = ptr.getClass().getCreatureStats(ptr);

            // If we dispose corpse before end of death animation, we should update death counter counter manually.
            // Also we should run actor's script - it may react on actor's death.
            if (creatureStats.isDead() && !creatureStats.isDeathAnimationFinished())
            {
                creatureStats.setDeathAnimationFinished(true);
                MWBase::Environment::get().getMechanicsManager()->notifyDied(ptr);

                const ESM::RefId& script = ptr.getClass().getScript(ptr);
                if (!script.empty() && MWBase::Environment::get().getWorld()->getScriptsEnabled())
                {
                    MWScript::InterpreterContext interpreterContext(&ptr.getRefData().getLocals(), ptr);
                    MWBase::Environment::get().getScriptManager()->run(script, interpreterContext);
                }

                // Clean up summoned creatures as well.
                auto& creatureMap = creatureStats.getSummonedCreatureMap();
                for (const auto& creature : creatureMap)
                    MWBase::Environment::get().getMechanicsManager()->cleanupSummonedCreature(creature.second);
                creatureMap.clear();

                // Check if we are a summon and inform our master we've bit the dust.
                for (const auto& package : creatureStats.getAiSequence())
                {
                    if (package->followTargetThroughDoors() && !package->getTarget().isEmpty())
                    {
                        const auto& summoner = package->getTarget();
                        auto& summons = summoner.getClass().getCreatureStats(summoner).getSummonedCreatureMap();
                        auto it = std::find_if(summons.begin(), summons.end(),
                            [&](const auto& entry) { return entry.second == ptr.getCellRef().getRefNum(); });
                        if (it != summons.end())
                        {
                            auto summon = *it;
                            summons.erase(it);
                            MWMechanics::purgeSummonEffect(summoner, summon);
                            break;
                        }
                    }
                }
            }

            MWBase::Environment::get().getWorld()->deleteObject(ptr);
        }

        mPtr = MWWorld::Ptr();
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onDisposeCorpseButtonClicked(MyGUI::Widget* /*sender*/)
    {
        if (mDragAndDrop == nullptr || mDragAndDrop->mIsOnDragAndDrop)
            return;

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);
        if (usesAuthoritativeInventoryTransfer())
        {
            requestAuthoritativeTakeAll(true);
            return;
        }

        // Copy mPtr because the legacy Take All path closes the window and resets the reference.
        const MWWorld::Ptr ptr = mPtr;
        onTakeAllButtonClicked(mTakeButton);
        disposeCorpseNow(ptr);
    }

    void ContainerWindow::onReferenceUnavailable()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onDeleteCustomData(const MWWorld::Ptr& ptr)
    {
        if (mModel && mModel->usesContainer(ptr))
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    ControllerButtons* ContainerWindow::getControllerButtons()
    {
        if (mDisposeCorpseButton->getVisible())
            mControllerButtons.mR1 = "#{Interface:DisposeOfCorpse}";
        else
            mControllerButtons.mR1.clear();
        return &mControllerButtons;
    }

    bool ContainerWindow::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
            int index = mItemView->getControllerFocus();
            if (index >= 0 && index < mItemView->getItemCount())
                onItemSelected(index);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_B)
        {
            onCloseButtonClicked(mCloseButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_X)
        {
            onTakeAllButtonClicked(mTakeButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        {
            if (mDisposeCorpseButton->getVisible())
                onDisposeCorpseButtonClicked(mDisposeCorpseButton);
        }
        else if (arg.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK || arg.button == SDL_CONTROLLER_BUTTON_DPAD_UP
            || arg.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN || arg.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT
            || arg.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
        {
            mItemView->onControllerButton(arg.button);
        }

        return true;
    }

    void ContainerWindow::setActiveControllerWindow(bool active)
    {
        mItemView->setActiveControllerWindow(active);
        WindowBase::setActiveControllerWindow(active);
    }

    void ContainerWindow::onFrame(float dt)
    {
        checkReferenceAvailable();

        if (mUpdateNextFrame)
        {
            mItemView->update();
            mUpdateNextFrame = false;
        }
    }

    void ContainerWindow::onInventoryUpdate(const MWWorld::Ptr& ptr)
    {
        if (ptr == mPtr)
            mUpdateNextFrame = true;
    }
}
