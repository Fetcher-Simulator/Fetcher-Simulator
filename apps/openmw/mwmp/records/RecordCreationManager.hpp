#ifndef OPENMW_MWMP_RECORDCREATIONMANAGER_HPP
#define OPENMW_MWMP_RECORDCREATIONMANAGER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <components/openmw-mp/Records/RecordCreateProtocol.hpp>

namespace mwmp
{
    class NetworkClient;

    class RecordCreationManager
    {
    public:
        using Completion = std::function<void(const records::RecordCreateResult&)>;

        explicit RecordCreationManager(NetworkClient& client);

        std::string nextRequestId();
        void request(records::RecordCreateRequest request, Completion completion);
        void onResult(records::RecordCreateResult result);
        void setInventoryRevision(uint64_t revision);
        uint64_t inventoryRevision() const { return mInventoryRevision; }
        void notifyRecordStoreChanged() { mStoreChanged = true; }
        void update();
        void cancelAll(records::CreateError error = records::CreateError::ServerError);

    private:
        struct Pending
        {
            Completion completion;
            std::optional<records::RecordCreateResult> result;
        };

        bool isReady(const records::RecordCreateResult& result) const;

        NetworkClient& mClient;
        std::unordered_map<std::string, Pending> mPending;
        uint64_t mNextRequest = 1;
        std::string mRequestPrefix;
        uint64_t mInventoryRevision = 0;
        bool mStoreChanged = false;
    };
}

#endif
