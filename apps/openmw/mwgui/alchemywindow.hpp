#ifndef MWGUI_ALCHEMY_H
#define MWGUI_ALCHEMY_H

#include <cstdint>
#include <memory>
#include <vector>

#include <MyGUI_ComboBox.h>
#include <MyGUI_ControllerItem.h>

#include <components/widgets/box.hpp>
#include <components/widgets/numericeditbox.hpp>

#include "itemselection.hpp"
#include "windowbase.hpp"

#include "../mwmechanics/alchemy.hpp"

#ifdef BUILD_MULTIPLAYER
#include <components/openmw-mp/Records/AlchemyProtocol.hpp>
#endif

namespace MWGui
{
    class ItemView;
    class ItemWidget;
    class InventoryItemModel;
    class SortFilterItemModel;

    class AlchemyWindow : public WindowBase
    {
    public:
        AlchemyWindow();

        void onOpen() override;

        void onResChange(int, int) override { center(); }

        std::string_view getWindowIdForLua() const override { return "Alchemy"; }

    private:
        static const float sCountChangeInitialPause; // in seconds
        static const float sCountChangeInterval; // in seconds

        std::string mSuggestedPotionName;
        enum class FilterType
        {
            ByName,
            ByEffect
        };
        FilterType mCurrentFilter;

        std::unique_ptr<ItemSelectionDialog> mItemSelectionDialog;

        ItemView* mItemView;
        InventoryItemModel* mModel;
        SortFilterItemModel* mSortModel;

        MyGUI::Button* mCreateButton;
        MyGUI::Button* mCancelButton;

        MyGUI::Widget* mEffectsBox;

        MyGUI::Button* mIncreaseButton;
        MyGUI::Button* mDecreaseButton;
        Gui::AutoSizedButton* mFilterType;
        MyGUI::ComboBox* mFilterValue;
        MyGUI::EditBox* mNameEdit;
        Gui::NumericEditBox* mBrewCountEdit;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onCreateButtonClicked(MyGUI::Widget* sender);
        void onIngredientSelected(MyGUI::Widget* sender);
        void onApparatusSelected(MyGUI::Widget* sender);
        void onAccept(MyGUI::EditBox*);
        void onIncreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onDecreaseButtonPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onCountButtonReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onCountValueChanged(int value);
        void onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller);

        void applyFilter(const std::string& filter);
        void initFilter();
        void onFilterChanged(MyGUI::ComboBox* sender, size_t index);
        void onFilterEdited(MyGUI::EditBox* sender);
        void switchFilterType(MyGUI::Widget* sender);
        void updateFilters();

        void addRepeatController(MyGUI::Widget* widget);

        void onIncreaseButtonTriggered();
        void onDecreaseButtonTriggered();

        void onSelectedItem(int index);

        void onItemSelected(MWWorld::Ptr item);
        void onItemCancel();

        void createPotions(int count);
#ifdef BUILD_MULTIPLAYER
        void startMultiplayerBrew(int count);
        void onMultiplayerBrewResult(const mwmp::records::AlchemyResult& result);
#endif
        void refreshAfterBrew();

        void update();

        std::unique_ptr<MWMechanics::Alchemy> mAlchemy;

        std::vector<ItemWidget*> mApparatus;
        std::vector<ItemWidget*> mIngredients;

        // Incremented on every window open; stale async completions from a
        // previous session are ignored.
        std::uint64_t mSessionToken = 0;

        bool onControllerButtonEvent(const SDL_ControllerButtonEvent& arg) override;
        void filterListButtonHandler(const SDL_ControllerButtonEvent& arg);
    };
}

#endif
