#ifndef MWGUI_ENCHANTINGDIALOG_H
#define MWGUI_ENCHANTINGDIALOG_H

#include <memory>

#ifdef BUILD_MULTIPLAYER
#include <components/openmw-mp/Records/EnchantingProtocol.hpp>
#endif

#include "itemselection.hpp"
#include "spellcreationdialog.hpp"

#include "../mwmechanics/enchanting.hpp"

namespace MWGui
{

    class ItemWidget;

    class EnchantingDialog : public WindowBase, public ReferenceInterface, public EffectEditorBase
    {
    public:
        EnchantingDialog();
        virtual ~EnchantingDialog() = default;

        void onOpen() override;

        void onFrame(float dt) override { checkReferenceAvailable(); }
        void clear() override { resetReference(); }

        void setSoulGem(const MWWorld::Ptr& gem);
        void setItem(const MWWorld::Ptr& item);

        /// Actor Ptr: buy enchantment from this actor
        /// Soulgem Ptr: player self-enchant
        void setPtr(const MWWorld::Ptr& ptr) override;

        void resetReference() override;

        std::string_view getWindowIdForLua() const override { return "EnchantingDialog"; }

    protected:
        void onReferenceUnavailable() override;
        void notifyEffectsChanged() override;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onSelectItem(MyGUI::Widget* sender);
        void onSelectSoul(MyGUI::Widget* sender);

        void onItemSelected(MWWorld::Ptr item);
        void onItemCancel();
        void onSoulSelected(MWWorld::Ptr item);
        void onSoulCancel();
        void onBuyButtonClicked(MyGUI::Widget* sender);
        void updateLabels();
        void onTypeButtonClicked(MyGUI::Widget* sender);
        void onAccept(MyGUI::EditBox* sender);

#ifdef BUILD_MULTIPLAYER
        /// Sends the current selections as a semantic server-authoritative
        /// enchanting request; the completion is held until the returned
        /// records and inventory revision are visible locally.
        void startMultiplayerEnchant();
        void onMultiplayerEnchantResult(const mwmp::records::EnchantingResult& result);
#endif

        std::unique_ptr<ItemSelectionDialog> mItemSelectionDialog;

        MyGUI::Widget* mChanceLayout;

        MyGUI::Button* mCancelButton;
        ItemWidget* mItemBox;
        ItemWidget* mSoulBox;

        MyGUI::Button* mTypeButton;
        MyGUI::Button* mBuyButton;

        MyGUI::EditBox* mName;
        MyGUI::TextBox* mEnchantmentPoints;
        MyGUI::TextBox* mCastCost;
        MyGUI::TextBox* mCharge;
        MyGUI::TextBox* mSuccessChance;
        MyGUI::TextBox* mPrice;
        MyGUI::TextBox* mPriceText;

        MWMechanics::Enchanting mEnchanting;
        ESM::EffectList mEffectList;

#ifdef BUILD_MULTIPLAYER
        /// Bumped whenever the dialog opens for a new target; stale completion
        /// callbacks from an earlier session are ignored.
        std::uint64_t mSessionToken = 0;
#endif

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
    };

}

#endif
