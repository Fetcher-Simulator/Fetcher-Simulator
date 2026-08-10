#include "ServerLuaPackageTransfer.hpp"

#include <algorithm>
#include <limits>

namespace mwmp::serverlua
{
    namespace
    {
        std::string describe(const std::vector<ValidationError>& errors)
        {
            if (errors.empty())
                return {};
            return errors.front().path + " [" + errors.front().code + "]: " + errors.front().message;
        }
    }

    void PackageTransfer::reset()
    {
        mState = State::Empty;
        mError.clear();
        mPackageSet = {};
        mReceipts.clear();
    }

    bool PackageTransfer::fail(std::string message)
    {
        if (mState != State::Failed)
            mError = std::move(message);
        mState = State::Failed;
        return false;
    }

    bool PackageTransfer::begin(PackageSet manifest, std::uint32_t supportedOpenMWLuaApi,
        std::uint16_t supportedMultiplayerLuaApi)
    {
        reset();
        const auto errors
            = validatePackageSet(manifest, supportedOpenMWLuaApi, supportedMultiplayerLuaApi, false);
        if (!errors.empty())
            return fail("Invalid package manifest: " + describe(errors));
        if (manifest.generation == 0 || manifest.packageSetHash.empty())
            return fail("Package manifest has no generation identity");

        mPackageSet = std::move(manifest);
        mReceipts.resize(mPackageSet.packages.size());
        for (std::size_t packageIndex = 0; packageIndex < mPackageSet.packages.size(); ++packageIndex)
        {
            auto& package = mPackageSet.packages[packageIndex];
            auto& receipts = mReceipts[packageIndex];
            receipts.resize(package.files.size());
            for (std::size_t fileIndex = 0; fileIndex < package.files.size(); ++fileIndex)
            {
                package.files[fileIndex].source.clear();
                receipts[fileIndex].bytes.resize(package.files[fileIndex].sourceSize);
                receipts[fileIndex].received.resize(package.files[fileIndex].sourceSize);
            }
        }
        mState = State::Receiving;
        return true;
    }

    bool PackageTransfer::receive(std::uint64_t generation, std::string_view packageId,
        std::string_view packageHash, std::string_view filePath, std::uint32_t offset, std::string_view bytes)
    {
        if (mState != State::Receiving)
            return fail("Package chunk arrived outside an active transfer");
        if (generation != mPackageSet.generation)
            return fail("Stale or foreign package generation");
        if (bytes.empty() || bytes.size() > MaxChunkSize)
            return fail("Package chunk size is invalid");

        const std::string normalizedId = normalizePackageId(packageId);
        const std::string normalizedPath = normalizeRelativePath(filePath);
        auto packageIt = std::find_if(mPackageSet.packages.begin(), mPackageSet.packages.end(),
            [&](const Package& package) { return package.packageId == normalizedId; });
        if (packageIt == mPackageSet.packages.end() || packageIt->packageHash != packageHash)
            return fail("Package chunk identity does not match the manifest");
        auto fileIt = std::find_if(packageIt->files.begin(), packageIt->files.end(),
            [&](const File& file) { return file.path == normalizedPath; });
        if (fileIt == packageIt->files.end())
            return fail("Package chunk file is not declared in the manifest");
        if (offset > fileIt->sourceSize || bytes.size() > fileIt->sourceSize - offset)
            return fail("Package chunk range is outside the declared file");

        const std::size_t packageIndex = static_cast<std::size_t>(packageIt - mPackageSet.packages.begin());
        const std::size_t fileIndex = static_cast<std::size_t>(fileIt - packageIt->files.begin());
        FileReceipt& receipt = mReceipts[packageIndex][fileIndex];
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            const std::size_t position = static_cast<std::size_t>(offset) + i;
            if (receipt.received[position])
            {
                if (receipt.bytes[position] != bytes[i])
                    return fail("Conflicting overlapping package chunk");
                continue;
            }
            receipt.bytes[position] = bytes[i];
            receipt.received[position] = 1;
            ++receipt.receivedCount;
        }
        return true;
    }

    bool PackageTransfer::finish(std::uint64_t generation, std::string_view packageSetHash,
        std::uint32_t supportedOpenMWLuaApi, std::uint16_t supportedMultiplayerLuaApi)
    {
        if (mState != State::Receiving)
            return fail("Package completion arrived outside an active transfer");
        if (generation != mPackageSet.generation || packageSetHash != mPackageSet.packageSetHash)
            return fail("Package completion identity does not match the manifest");
        for (std::size_t packageIndex = 0; packageIndex < mPackageSet.packages.size(); ++packageIndex)
        {
            for (std::size_t fileIndex = 0; fileIndex < mPackageSet.packages[packageIndex].files.size(); ++fileIndex)
            {
                File& file = mPackageSet.packages[packageIndex].files[fileIndex];
                FileReceipt& receipt = mReceipts[packageIndex][fileIndex];
                if (receipt.receivedCount != file.sourceSize)
                    return fail("Package completion arrived with an incomplete file: " + file.path);
                file.source = std::move(receipt.bytes);
            }
        }
        const auto errors = validatePackageSet(
            mPackageSet, supportedOpenMWLuaApi, supportedMultiplayerLuaApi, true);
        if (!errors.empty())
            return fail("Completed package set is invalid: " + describe(errors));
        mReceipts.clear();
        mState = State::Ready;
        return true;
    }

    std::optional<PackageSet> PackageTransfer::takeReadyPackageSet()
    {
        if (mState != State::Ready)
            return std::nullopt;
        PackageSet result = std::move(mPackageSet);
        reset();
        return result;
    }
}
