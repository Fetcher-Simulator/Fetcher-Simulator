#ifndef OPENMW_MWMP_ALCHEMYCREATIONMANAGER_HPP
#define OPENMW_MWMP_ALCHEMYCREATIONMANAGER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <components/openmw-mp/Records/AlchemyProtocol.hpp>

namespace mwmp
{
    class NetworkClient;
    class RecordCreationManager;

    /// Client coordinator for server-authoritative native alchemy. Sends
    /// semantic requests, tracks pending operations, and holds completion
    /// until the required canonical records resolve locally and the
    /// authoritative inventory revision is visible.
    class AlchemyCreationManager
    {
    public:
        using Completion = std::function<void(const records::AlchemyResult&)>;

        AlchemyCreationManager(NetworkClient& client, RecordCreationManager& recordCreationManager);

        std::string nextRequestId();

        /// Submits a semantic alchemy request. Returns false (and sets
        /// \a error) when another request is already pending; duplicate
        /// submissions are not permitted.
        bool request(records::AlchemyRequest request, Completion completion, std::string& error);

        void onResult(records::AlchemyResult result);

        /// Called from the frame loop; completes requests whose definitions
        /// and inventory revision are visible locally.
        void update();

        /// Completes every pending request with an error (used on disconnect).
        void cancelAll(records::AlchemyError error = records::AlchemyError::ServerError);

        bool hasPending() const { return !mPending.empty(); }

    private:
        struct Pending
        {
            Completion completion;
            std::optional<records::AlchemyResult> result;
        };

        bool isReady(const records::AlchemyResult& result) const;

        NetworkClient& mClient;
        RecordCreationManager& mRecordCreationManager;
        std::unordered_map<std::string, Pending> mPending;
        uint64_t mNextRequest = 1;
        std::string mRequestPrefix;
    };
}

#endif
