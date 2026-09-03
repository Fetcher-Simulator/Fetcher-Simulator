#ifndef MGUI_CONTAINER_H
#define MGUI_CONTAINER_H

#include "itemmodel.hpp"
#include "referenceinterface.hpp"
#include "windowbase.hpp"

#include <components/misc/notnullptr.hpp>

#include <cstdint>
#include <functional>

namespace MyGUI
{
    class Gui;
    class Widget;
}

namespace MWGui
{
    class ContainerWindow;
    class ItemView;
    class SortFilterItemModel;
    class ItemTransfer;

    class ContainerWindow : public WindowBase, public ReferenceInterface
    {
    public:
        explicit ContainerWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer);

        void setPtr(const MWWorld::Ptr& container) override;

        void onOpen() override;

        void onClose() override;

        // Multiplayer pickpocket sessions must be finalized at the actual GUI-mode
        // removal boundary rather than relying on the visibility callback. This
        // keeps console/main-menu hiding harmless while guaranteeing Close/Escape
        // perform the native finish detection exactly once.
        void finalizePickpocketSession();

        void clear() override { resetReference(); }

        void onFrame(float dt) override;

        void resetReference() override;

        void onDeleteCustomData(const MWWorld::Ptr& ptr) override;

        void treatNextOpenAsLoot() { mTreatNextOpenAsLoot = true; }

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        std::string_view getWindowIdForLua() const override { return "Container"; }

        ControllerButtons* getControllerButtons() override;
        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

        MWGui::ItemView* getItemView() { return mItemView; }
        ItemModel* getModel() { return mModel; }

    private:
        Misc::NotNullPtr<DragAndDrop> mDragAndDrop;
        Misc::NotNullPtr<ItemTransfer> mItemTransfer;

        MWGui::ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        ItemModel* mModel;
        int mSelectedItem;
        bool mUpdateNextFrame;
        bool mTreatNextOpenAsLoot;
        bool mPickpocketFinishSent = false;
        bool mPickpocketDetected = false;
        bool mAuthoritativeTransferPending = false;
        bool mAuthoritativeTakeAllPending = false;
        bool mDisposeAfterAuthoritativeTakeAll = false;
        std::uint64_t mAuthoritativeTransferSerial = 0;
        std::function<bool()> mAuthoritativeCursorResolver;
        MyGUI::Button* mDisposeCorpseButton;
        MyGUI::Button* mTakeButton;
        MyGUI::Button* mCloseButton;

        void onItemSelected(int index);
        void onBackgroundSelected();
        void dragItem(MyGUI::Widget* sender, std::size_t count);
        void transferItem(MyGUI::Widget* sender, std::size_t count);
        void dropItem();
        bool usesAuthoritativeInventoryTransfer() const;
        void requestAuthoritativeTake(const ItemStack& item, std::size_t count, bool startDrag);
        void requestAuthoritativeTakeAll(bool disposeAfter);
        void continueAuthoritativeTakeAll();
        void requestAuthoritativePut(const ItemStack& item, std::size_t count);
        void disposeCorpseNow(const MWWorld::Ptr& ptr);
        void onCloseButtonClicked(MyGUI::Widget* sender);
        void onTakeAllButtonClicked(MyGUI::Widget* sender);
        void onDisposeCorpseButtonClicked(MyGUI::Widget* sender);

        void onReferenceUnavailable() override;
    };
}
#endif // CONTAINER_H
