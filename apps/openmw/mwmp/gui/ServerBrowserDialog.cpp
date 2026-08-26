#include "ServerBrowserDialog.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <string_view>

#include <MyGUI_InputManager.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/settings/values.hpp>

#include <httplib.h>

namespace mwmp
{
    namespace
    {
        bool supportedUrl(std::string_view url)
        {
            return url.starts_with("https://") || url.starts_with("http://");
        }

        bool isTlsError(httplib::Error error)
        {
            return error == httplib::Error::SSLConnection || error == httplib::Error::SSLLoadingCerts
                || error == httplib::Error::SSLServerVerification
                || error == httplib::Error::SSLServerHostnameVerification;
        }

        bool isTimeoutError(httplib::Error error)
        {
            return error == httplib::Error::ConnectionTimeout || error == httplib::Error::Timeout;
        }
    }

    ServerBrowserDialog::ServerBrowserDialog()
        : WindowModal("openmw_mp_server_browser.layout")
    {
        getWidget(mSearch, "SearchBox");
        getWidget(mList, "ServerList");
        getWidget(mStatus, "StatusLabel");
        getWidget(mRefreshButton, "RefreshButton");
        getWidget(mConnectButton, "ConnectButton");
        getWidget(mCancelButton, "CancelButton");

        getWidget(mColumnCompatibility, "ColCompatibility");
        getWidget(mColumnName, "ColName");
        getWidget(mColumnPlayers, "ColPlayers");
        getWidget(mColumnBuildVersion, "ColVersion");
        getWidget(mColumnMode, "ColMode");
        getWidget(mColumnCountry, "ColCountry");

        mList->addColumn("", 36);
        mList->addColumn("", 400);
        mList->addColumn("", 90);
        mList->addColumn("", 70);
        mList->addColumn("", 200);
        mList->addColumn("", 60);

        for (MyGUI::TextBox* header : { mColumnName, mColumnPlayers, mColumnBuildVersion, mColumnMode, mColumnCountry })
        {
            header->setNeedMouseFocus(true);
            header->eventMouseButtonClick += MyGUI::newDelegate(this, &ServerBrowserDialog::onColumnClicked);
        }

        mList->eventListChangePosition += MyGUI::newDelegate(this, &ServerBrowserDialog::onListSelectChange);
        mList->eventListSelectAccept += MyGUI::newDelegate(this, &ServerBrowserDialog::onListAccept);
        mSearch->eventEditTextChange += MyGUI::newDelegate(this, &ServerBrowserDialog::onSearchChanged);
        mSearch->eventKeyButtonPressed += MyGUI::newDelegate(this, &ServerBrowserDialog::onKeyPress);
        mList->eventKeyButtonPressed += MyGUI::newDelegate(this, &ServerBrowserDialog::onKeyPress);
        mRefreshButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ServerBrowserDialog::onRefreshClicked);
        mConnectButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ServerBrowserDialog::onConnectClicked);
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ServerBrowserDialog::onCancelClicked);

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord
            += MyGUI::newDelegate(this, &ServerBrowserDialog::onWindowResize);
        updateColumnLayout();

        mConnectButton->setEnabled(false);
        mRequestWorker = std::jthread([this](std::stop_token stopToken) { requestLoop(stopToken); });
    }

    ServerBrowserDialog::~ServerBrowserDialog()
    {
        mRequestWorker.request_stop();
        mRequestCondition.notify_all();
        if (mRequestWorker.joinable())
            mRequestWorker.join();
    }

    void ServerBrowserDialog::onOpen()
    {
        WindowModal::onOpen();
        mMasterUrl = Settings::multiplayer().mMasterServerUrl.get();
        mSelected = MyGUI::ITEM_NONE;
        mConnectButton->setEnabled(false);
        MyGUI::InputManager::getInstance().setKeyFocusWidget(mList);
        refresh();
    }

    void ServerBrowserDialog::onFrame(float)
    {
        std::optional<FetchResult> completed;
        {
            std::lock_guard lock(mRequestMutex);
            if (mCompletedRequest)
            {
                completed = std::move(mCompletedRequest);
                mCompletedRequest.reset();
            }
        }
        if (completed)
            consumeFetchResult(std::move(*completed));
    }

    void ServerBrowserDialog::refresh()
    {
        if (mMasterUrl.empty())
        {
            setState(State::Error);
            mStatus->setCaption("No master server URL configured.");
            return;
        }
        if (!supportedUrl(mMasterUrl))
        {
            setState(State::Error);
            mStatus->setCaption("Unsupported master server URL.");
            return;
        }

        setState(State::Loading);
        mSelected = MyGUI::ITEM_NONE;
        mConnectButton->setEnabled(false);
        {
            std::lock_guard lock(mRequestMutex);
            ++mRequestedGeneration;
            mRequestedUrl = mMasterUrl;
            mRequestPending = true;
            mCompletedRequest.reset();
        }
        mRequestCondition.notify_all();
    }

    void ServerBrowserDialog::setState(State state)
    {
        mState = state;
        mRefreshButton->setEnabled(true);
        if (state == State::Loading)
        {
            mList->removeAllItems();
            mStatus->setCaption("Fetching server list...");
        }
    }

    void ServerBrowserDialog::requestLoop(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested())
        {
            std::uint64_t generation = 0;
            std::string url;
            {
                std::unique_lock lock(mRequestMutex);
                mRequestCondition.wait(lock, stopToken, [this] { return mRequestPending; });
                if (stopToken.stop_requested())
                    break;
                generation = mRequestedGeneration;
                url = mRequestedUrl;
                mRequestPending = false;
            }

            FetchResult result = fetchServerList(url, generation);
            std::lock_guard lock(mRequestMutex);
            if (generation == mRequestedGeneration)
                mCompletedRequest = std::move(result);
        }
    }

    ServerBrowserDialog::FetchResult ServerBrowserDialog::fetchServerList(
        const std::string& masterUrl, std::uint64_t generation)
    {
        FetchResult result;
        result.generation = generation;
        if (!supportedUrl(masterUrl))
        {
            result.error = FetchError::UnsupportedUrl;
            return result;
        }

        try
        {
            httplib::Client client(masterUrl);
            client.set_connection_timeout(3);
            client.set_read_timeout(5);
            client.set_write_timeout(5);
            client.enable_server_certificate_verification(true);
            const auto response = client.Get("/v1/servers");
            if (!response)
            {
                if (isTlsError(response.error()))
                    result.error = FetchError::Tls;
                else if (isTimeoutError(response.error()))
                    result.error = FetchError::Timeout;
                else
                    result.error = FetchError::Connection;
                Log(Debug::Warning) << "[ServerBrowser] request failed: " << httplib::to_string(response.error());
                return result;
            }
            if (response->status != 200)
            {
                result.error = FetchError::HttpStatus;
                result.httpStatus = response->status;
                return result;
            }
            result.body = response->body;
            return result;
        }
        catch (const std::exception& error)
        {
            Log(Debug::Warning) << "[ServerBrowser] request error: " << error.what();
            result.error = FetchError::Connection;
            return result;
        }
    }

    void ServerBrowserDialog::consumeFetchResult(FetchResult result)
    {
        if (result.error != FetchError::None)
        {
            setState(State::Error);
            switch (result.error)
            {
                case FetchError::UnsupportedUrl:
                    mStatus->setCaption("Unsupported master server URL.");
                    break;
                case FetchError::Tls:
                    mStatus->setCaption("TLS certificate or HTTPS connection error.");
                    break;
                case FetchError::Timeout:
                    mStatus->setCaption("Master server request timed out.");
                    break;
                case FetchError::HttpStatus:
                    mStatus->setCaption("Master server returned HTTP " + std::to_string(result.httpStatus) + ".");
                    break;
                case FetchError::Connection:
                    mStatus->setCaption("Could not resolve or connect to the master server.");
                    break;
                case FetchError::None:
                    break;
            }
            return;
        }

        ServerListParseResult parsed = parsePublicServerList(result.body);
        if (!parsed.error.empty())
        {
            Log(Debug::Warning) << "[ServerBrowser] malformed response: " << parsed.error;
            setState(State::Error);
            mStatus->setCaption("Master server returned a malformed response.");
            return;
        }
        if (parsed.skippedEntries != 0)
            Log(Debug::Warning) << "[ServerBrowser] skipped " << parsed.skippedEntries
                                << " malformed server list entries";

        mServers = std::move(parsed.entries);
        setState(State::Loaded);
        applyFilter();
        if (mServers.empty())
            mStatus->setCaption("No servers are currently listed.");
        else if (parsed.skippedEntries != 0)
            mStatus->setCaption(std::to_string(mFiltered.size()) + " servers; skipped "
                + std::to_string(parsed.skippedEntries) + " malformed entries.");
    }

    void ServerBrowserDialog::applyFilter()
    {
        const std::string search = Misc::StringUtils::lowerCase(std::string(mSearch->getCaption()));
        std::vector<PublicServerEntry> sortable;
        std::vector<std::size_t> sourceIndices;
        sortable.reserve(mServers.size());
        sourceIndices.reserve(mServers.size());
        for (std::size_t index = 0; index < mServers.size(); ++index)
        {
            if (!search.empty() && Misc::StringUtils::lowerCase(mServers[index].name).find(search) == std::string::npos)
            {
                continue;
            }
            sortable.push_back(mServers[index]);
            sourceIndices.push_back(index);
        }

        const std::vector<std::size_t> order = sortedServerIndices(sortable, mSortColumn, mSortAscending);
        mFiltered.clear();
        mFiltered.reserve(order.size());
        for (const std::size_t index : order)
            mFiltered.push_back(sourceIndices[index]);

        mList->removeAllItems();
        for (const std::size_t index : mFiltered)
        {
            const PublicServerEntry& entry = mServers[index];
            const std::size_t row = mList->getItemCount();
            mList->addItem(isProtocolCompatible(entry) ? "" : "[!]");
            mList->setSubItemNameAt(1, row, entry.name);
            mList->setSubItemNameAt(
                2, row, std::to_string(entry.currentPlayers) + "/" + std::to_string(entry.maxPlayers));
            mList->setSubItemNameAt(3, row, entry.buildVersion);
            mList->setSubItemNameAt(4, row, entry.gameMode);
            mList->setSubItemNameAt(5, row, entry.country);
        }

        if (mState == State::Loaded)
            mStatus->setCaption(
                std::to_string(mFiltered.size()) + " / " + std::to_string(mServers.size()) + " servers");
        mSelected = MyGUI::ITEM_NONE;
        mConnectButton->setEnabled(false);
    }

    void ServerBrowserDialog::updateColumnLayout()
    {
        constexpr int compatibilityWidth = 36;
        constexpr int playersWidth = 90;
        constexpr int buildWidth = 70;
        constexpr int modeWidth = 200;
        constexpr int countryWidth = 60;
        constexpr int frameInset = 3;

        const int availableWidth = std::max(0, mList->getWidth() - frameInset * 2);
        const int fixedWidth = compatibilityWidth + playersWidth + buildWidth + modeWidth + countryWidth;
        const int nameWidth = std::max(220, availableWidth - fixedWidth);
        const int widths[] = {
            compatibilityWidth,
            nameWidth,
            playersWidth,
            buildWidth,
            modeWidth,
            countryWidth,
        };

        for (std::size_t column = 0; column < std::size(widths); ++column)
            mList->setColumnWidthAt(column, widths[column]);

        MyGUI::TextBox* headers[] = {
            mColumnCompatibility,
            mColumnName,
            mColumnPlayers,
            mColumnBuildVersion,
            mColumnMode,
            mColumnCountry,
        };
        int left = mList->getLeft() + frameInset;
        for (std::size_t column = 0; column < std::size(headers); ++column)
        {
            headers[column]->setCoord(left, headers[column]->getTop(), widths[column], headers[column]->getHeight());
            left += widths[column];
        }
    }

    void ServerBrowserDialog::updateConnectAvailability()
    {
        if (mSelected == MyGUI::ITEM_NONE || mSelected >= mFiltered.size())
        {
            mConnectButton->setEnabled(false);
            return;
        }

        const PublicServerEntry& entry = mServers[mFiltered[mSelected]];
        const bool compatible = isProtocolCompatible(entry);
        mConnectButton->setEnabled(compatible);
        if (compatible)
        {
            mStatus->setCaption(
                std::to_string(mFiltered.size()) + " / " + std::to_string(mServers.size()) + " servers");
        }
        else
        {
            mStatus->setCaption("Incompatible multiplayer protocol: server " + std::to_string(entry.protocolVersion)
                + ", client " + std::to_string(MultiplayerProtocolVersion) + ".");
        }
    }

    void ServerBrowserDialog::onRefreshClicked(MyGUI::Widget*)
    {
        refresh();
    }

    void ServerBrowserDialog::onConnectClicked(MyGUI::Widget*)
    {
        doConnect();
    }

    void ServerBrowserDialog::onCancelClicked(MyGUI::Widget*)
    {
        setVisible(false);
    }

    void ServerBrowserDialog::onListSelectChange(MyGUI::MultiListBox*, std::size_t index)
    {
        mSelected = index;
        updateConnectAvailability();
    }

    void ServerBrowserDialog::onListAccept(MyGUI::MultiListBox*, std::size_t index)
    {
        mSelected = index;
        doConnect();
    }

    void ServerBrowserDialog::onSearchChanged(MyGUI::EditBox*)
    {
        applyFilter();
    }

    void ServerBrowserDialog::onKeyPress(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char)
    {
        if (key == MyGUI::KeyCode::Return || key == MyGUI::KeyCode::NumpadEnter)
            doConnect();
        else if (key == MyGUI::KeyCode::Escape)
            setVisible(false);
    }

    void ServerBrowserDialog::onColumnClicked(MyGUI::Widget* sender)
    {
        ServerSortColumn column;
        bool textColumn = true;
        if (sender == mColumnName)
            column = ServerSortColumn::Name;
        else if (sender == mColumnPlayers)
        {
            column = ServerSortColumn::Players;
            textColumn = false;
        }
        else if (sender == mColumnBuildVersion)
            column = ServerSortColumn::BuildVersion;
        else if (sender == mColumnMode)
            column = ServerSortColumn::GameMode;
        else if (sender == mColumnCountry)
            column = ServerSortColumn::Country;
        else
            return;

        if (mSortColumn == column)
            mSortAscending = !mSortAscending;
        else
        {
            mSortColumn = column;
            mSortAscending = textColumn;
        }

        const std::string indicator = mSortAscending ? " ^" : " v";
        const std::pair<MyGUI::TextBox*, std::pair<ServerSortColumn, const char*>> headers[] = {
            { mColumnName, { ServerSortColumn::Name, "Server Name" } },
            { mColumnPlayers, { ServerSortColumn::Players, "Players" } },
            { mColumnBuildVersion, { ServerSortColumn::BuildVersion, "Build" } },
            { mColumnMode, { ServerSortColumn::GameMode, "Mode" } },
            { mColumnCountry, { ServerSortColumn::Country, "CC" } },
        };
        for (const auto& [widget, description] : headers)
            widget->setCaption(description.second + (description.first == mSortColumn ? indicator : ""));
        applyFilter();
    }

    void ServerBrowserDialog::onWindowResize(MyGUI::Window* sender)
    {
        MWGui::WindowBase::clampWindowCoordinates(sender);
        updateColumnLayout();
    }

    void ServerBrowserDialog::doConnect()
    {
        if (mSelected == MyGUI::ITEM_NONE || mSelected >= mFiltered.size())
            return;

        const PublicServerEntry& entry = mServers[mFiltered[mSelected]];
        if (entry.host.empty() || !isProtocolCompatible(entry))
        {
            updateConnectAvailability();
            return;
        }

        setVisible(false);
        if (mConnectCallback)
            mConnectCallback(entry.host, entry.port);
    }
}
