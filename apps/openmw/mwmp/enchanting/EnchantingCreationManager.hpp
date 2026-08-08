#ifndef OPENMW_MWMP_ENCHANTINGCREATIONMANAGER_HPP
#define OPENMW_MWMP_ENCHANTINGCREATIONMANAGER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <components/openmw-mp/Records/EnchantingProtocol.hpp>

namespace mwmp
{
    class NetworkClient;
    class RecordCreationManager;

    /// Client coordinator for server-authoritative native enchanting. Sends
    /// semantic requests, tracks pending operations, and holds completion
    /// until the returned Enchantment and owning-item records resolve locally
    /// (including the item's enchantment dependency) and the authoritative
    /// inventory revision is visible.
    class EnchantingCreationManager
    {
    public:
        using Completion = std::function<void(const records::EnchantingResult&)>;

        EnchantingCreationManager(NetworkClient& client, RecordCreationManager& recordCreationManager);

        std::string nextRequestId();

        /// Submits a semantic enchanting request. Returns false (and sets
        /// \a error) when another request is already pending; duplicate
        /// submissions are not permitted.
        bool request(records::EnchantingRequest request, Completion completion, std::string& error);

        void onResult(records::EnchantingResult result);

        /// Called from the frame loop; completes requests whose definitions
        /// and inventory revision are visible locally.
        void update();

        /// Completes every pending request with an error (used on disconnect).
        void cancelAll(records::EnchantingError error = records::EnchantingError::ServerError);

        bool hasPending() const { return !mPending.empty(); }

    private:
        struct Pending
        {
            Completion completion;
            std::optional<records::EnchantingResult> result;
        };

        bool isReady(const records::EnchantingResult& result) const;

        NetworkClient& mClient;
        RecordCreationManager& mRecordCreationManager;
        std::unordered_map<std::string, Pending> mPending;
        uint64_t mNextRequest = 1;
        std::string mRequestPrefix;
    };
}

#endif
