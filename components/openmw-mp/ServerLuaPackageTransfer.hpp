#ifndef OPENMW_MP_SERVERLUAPACKAGETRANSFER_HPP
#define OPENMW_MP_SERVERLUAPACKAGETRANSFER_HPP

#include "ServerLuaPackage.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mwmp::serverlua
{
    class PackageTransfer
    {
    public:
        enum class State
        {
            Empty,
            Receiving,
            Ready,
            Failed,
        };

        void reset();
        bool begin(PackageSet manifest, std::uint32_t supportedOpenMWLuaApi,
            std::uint16_t supportedMultiplayerLuaApi);
        bool receive(std::uint64_t generation, std::string_view packageId, std::string_view packageHash,
            std::string_view filePath, std::uint32_t offset, std::string_view bytes);
        bool finish(std::uint64_t generation, std::string_view packageSetHash,
            std::uint32_t supportedOpenMWLuaApi, std::uint16_t supportedMultiplayerLuaApi);

        State state() const { return mState; }
        const std::string& error() const { return mError; }
        const PackageSet* readyPackageSet() const { return mState == State::Ready ? &mPackageSet : nullptr; }
        std::optional<PackageSet> takeReadyPackageSet();

    private:
        struct FileReceipt
        {
            std::string bytes;
            std::vector<std::uint8_t> received;
            std::size_t receivedCount = 0;
        };

        bool fail(std::string message);

        State mState = State::Empty;
        std::string mError;
        PackageSet mPackageSet;
        std::vector<std::vector<FileReceipt>> mReceipts;
    };
}

#endif
