#ifndef OPENMW_APPS_OPENMW_MWMP_GUI_SERVER_BROWSER_DIALOG_HPP
#define OPENMW_APPS_OPENMW_MWMP_GUI_SERVER_BROWSER_DIALOG_HPP

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_KeyCode.h>
#include <MyGUI_MultiListBox.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>
#include <MyGUI_Window.h>

#include <components/openmw-mp/MasterServerProtocol.hpp>

#include "../../mwgui/windowbase.hpp"

namespace mwmp
{
    class ServerBrowserDialog : public MWGui::WindowModal
    {
    public:
        using ConnectCallback = std::function<void(const std::string& address, std::uint16_t port)>;

        ServerBrowserDialog();
        ~ServerBrowserDialog() override;

        void setConnectCallback(ConnectCallback callback) { mConnectCallback = std::move(callback); }
        void refresh();

        void onFrame(float dt) override;
        void onOpen() override;

    private:
        enum class State
        {
            Idle,
            Loading,
            Loaded,
            Error,
        };

        enum class FetchError
        {
            None,
            UnsupportedUrl,
            Connection,
            Timeout,
            Tls,
            HttpStatus,
        };

        struct FetchResult
        {
            std::uint64_t generation = 0;
            FetchError error = FetchError::None;
            int httpStatus = 0;
            std::string body;
        };

        void setState(State state);
        void requestLoop(std::stop_token stopToken);
        static FetchResult fetchServerList(const std::string& masterUrl, std::uint64_t generation);
        void consumeFetchResult(FetchResult result);
        void applyFilter();
        void updateColumnLayout();
        void updateConnectAvailability();
        void doConnect();

        void onRefreshClicked(MyGUI::Widget* sender);
        void onConnectClicked(MyGUI::Widget* sender);
        void onCancelClicked(MyGUI::Widget* sender);
        void onListAccept(MyGUI::MultiListBox* sender, std::size_t index);
        void onListSelectChange(MyGUI::MultiListBox* sender, std::size_t index);
        void onSearchChanged(MyGUI::EditBox* sender);
        void onKeyPress(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);
        void onColumnClicked(MyGUI::Widget* sender);
        void onWindowResize(MyGUI::Window* sender);

        MyGUI::EditBox* mSearch = nullptr;
        MyGUI::MultiListBox* mList = nullptr;
        MyGUI::TextBox* mStatus = nullptr;
        MyGUI::Button* mRefreshButton = nullptr;
        MyGUI::Button* mConnectButton = nullptr;
        MyGUI::Button* mCancelButton = nullptr;

        MyGUI::TextBox* mColumnCompatibility = nullptr;
        MyGUI::TextBox* mColumnName = nullptr;
        MyGUI::TextBox* mColumnPlayers = nullptr;
        MyGUI::TextBox* mColumnBuildVersion = nullptr;
        MyGUI::TextBox* mColumnMode = nullptr;
        MyGUI::TextBox* mColumnCountry = nullptr;

        State mState = State::Idle;
        std::vector<PublicServerEntry> mServers;
        std::vector<std::size_t> mFiltered;
        std::string mMasterUrl;
        ServerSortColumn mSortColumn = ServerSortColumn::Players;
        bool mSortAscending = false;
        std::size_t mSelected = MyGUI::ITEM_NONE;
        ConnectCallback mConnectCallback;

        std::mutex mRequestMutex;
        std::condition_variable_any mRequestCondition;
        std::jthread mRequestWorker;
        std::uint64_t mRequestedGeneration = 0;
        bool mRequestPending = false;
        std::string mRequestedUrl;
        std::optional<FetchResult> mCompletedRequest;
    };
}

#endif
