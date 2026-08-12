#ifndef OPENMW_MP_PACKETSERVERLUAPACKAGE_HPP
#define OPENMW_MP_PACKETSERVERLUAPACKAGE_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/ServerLuaPackage.hpp>

#include <stdexcept>

namespace mwmp
{
    class PacketServerLuaPackageManifest : public BasePacket
    {
    public:
        serverlua::PackageSet packageSet;

        PacketServerLuaPackageManifest()
            : BasePacket(PacketType::ServerLuaPackageManifest)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(packageSet.manifestVersion);
            stream.write(packageSet.generation);
            stream.writeString(packageSet.packageSetHash);
            stream.write(static_cast<std::uint16_t>(packageSet.packages.size()));
            for (const serverlua::Package& package : packageSet.packages)
            {
                stream.write(package.manifestVersion);
                stream.writeString(package.packageId);
                stream.write(package.packageVersion);
                stream.write(package.requiredOpenMWLuaApi);
                stream.write(package.requiredMultiplayerLuaApi);
                stream.writeString(package.packageHash);
                stream.write(static_cast<std::uint16_t>(package.dependencies.size()));
                for (const std::string& dependency : package.dependencies)
                    stream.writeString(dependency);
                stream.write(static_cast<std::uint16_t>(package.files.size()));
                for (const serverlua::File& file : package.files)
                {
                    stream.writeString(file.path);
                    stream.write(file.sourceSize);
                    stream.writeString(file.sourceHash);
                }
                stream.write(static_cast<std::uint16_t>(package.registrations.size()));
                for (const serverlua::ScriptRegistration& registration : package.registrations)
                {
                    stream.writeString(registration.path);
                    stream.write(registration.flags);
                }
                stream.write(static_cast<std::uint16_t>(package.overrides.size()));
                for (const serverlua::CompatibilityOverride& override : package.overrides)
                {
                    stream.writeString(override.target);
                    stream.writeString(override.source);
                    stream.write(static_cast<std::uint8_t>(override.basePolicy));
                    stream.write(static_cast<std::uint16_t>(override.acceptedBaseHashes.size()));
                    for (const std::string& hash : override.acceptedBaseHashes)
                        stream.writeString(hash);
                }
            }
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(packageSet.manifestVersion);
            stream.read(packageSet.generation);
            packageSet.packageSetHash = stream.readString();
            std::uint16_t packageCount = 0;
            stream.read(packageCount);
            if (packageCount > serverlua::MaxPackages)
                throw std::runtime_error("PacketServerLuaPackageManifest: too many packages");
            packageSet.packages.resize(packageCount);
            std::size_t totalSize = 0;
            for (serverlua::Package& package : packageSet.packages)
            {
                stream.read(package.manifestVersion);
                package.packageId = stream.readString();
                stream.read(package.packageVersion);
                stream.read(package.requiredOpenMWLuaApi);
                stream.read(package.requiredMultiplayerLuaApi);
                package.packageHash = stream.readString();
                std::uint16_t count = 0;
                stream.read(count);
                if (count > serverlua::MaxDependenciesPerPackage)
                    throw std::runtime_error("PacketServerLuaPackageManifest: too many dependencies");
                package.dependencies.resize(count);
                for (std::string& dependency : package.dependencies)
                    dependency = stream.readString();
                stream.read(count);
                if (count == 0 || count > serverlua::MaxFilesPerPackage)
                    throw std::runtime_error("PacketServerLuaPackageManifest: invalid file count");
                package.files.resize(count);
                std::size_t packageSize = 0;
                for (serverlua::File& file : package.files)
                {
                    file.path = stream.readString();
                    stream.read(file.sourceSize);
                    file.sourceHash = stream.readString();
                    if (file.sourceSize > serverlua::MaxFileSize)
                        throw std::runtime_error("PacketServerLuaPackageManifest: file too large");
                    packageSize += file.sourceSize;
                }
                if (packageSize > serverlua::MaxPackageSize)
                    throw std::runtime_error("PacketServerLuaPackageManifest: package too large");
                totalSize += packageSize;
                stream.read(count);
                if (count > serverlua::MaxRegistrationsPerPackage)
                    throw std::runtime_error("PacketServerLuaPackageManifest: invalid registration count");
                package.registrations.resize(count);
                for (serverlua::ScriptRegistration& registration : package.registrations)
                {
                    registration.path = stream.readString();
                    stream.read(registration.flags);
                }
                stream.read(count);
                if (count > serverlua::MaxOverridesPerPackage)
                    throw std::runtime_error("PacketServerLuaPackageManifest: too many compatibility overrides");
                package.overrides.resize(count);
                for (serverlua::CompatibilityOverride& override : package.overrides)
                {
                    override.target = stream.readString();
                    override.source = stream.readString();
                    std::uint8_t basePolicy = 0;
                    stream.read(basePolicy);
                    override.basePolicy = static_cast<serverlua::OverrideBasePolicy>(basePolicy);
                    std::uint16_t hashCount = 0;
                    stream.read(hashCount);
                    if (hashCount > serverlua::MaxAcceptedBaseHashesPerOverride)
                        throw std::runtime_error("PacketServerLuaPackageManifest: too many accepted base hashes");
                    override.acceptedBaseHashes.resize(hashCount);
                    for (std::string& hash : override.acceptedBaseHashes)
                        hash = stream.readString();
                }
                if (package.registrations.empty() && package.overrides.empty())
                    throw std::runtime_error("PacketServerLuaPackageManifest: package has no executable policy");
            }
            if (totalSize > serverlua::MaxPackageSetSize || !stream.eof())
                throw std::runtime_error("PacketServerLuaPackageManifest: invalid package-set size or trailing data");
        }
    };

    class PacketServerLuaPackageChunk : public BasePacket
    {
    public:
        std::uint64_t generation = 0;
        std::string packageId;
        std::string packageHash;
        std::string filePath;
        std::uint32_t offset = 0;
        std::string bytes;

        PacketServerLuaPackageChunk()
            : BasePacket(PacketType::ServerLuaPackageChunk)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            if (bytes.size() > serverlua::MaxChunkSize)
                throw std::runtime_error("PacketServerLuaPackageChunk: chunk too large");
            stream.write(generation);
            stream.writeString(packageId);
            stream.writeString(packageHash);
            stream.writeString(filePath);
            stream.write(offset);
            stream.writeBytes(bytes);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(generation);
            packageId = stream.readString();
            packageHash = stream.readString();
            filePath = stream.readString();
            stream.read(offset);
            bytes = stream.readBytes(serverlua::MaxChunkSize);
            if (bytes.empty() || !stream.eof())
                throw std::runtime_error("PacketServerLuaPackageChunk: empty chunk or trailing data");
        }
    };

    class PacketServerLuaPackageBootstrapComplete : public BasePacket
    {
    public:
        std::uint64_t generation = 0;
        std::string packageSetHash;

        PacketServerLuaPackageBootstrapComplete()
            : BasePacket(PacketType::ServerLuaPackageBootstrapComplete)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(generation);
            stream.writeString(packageSetHash);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(generation);
            packageSetHash = stream.readString();
            if (!stream.eof())
                throw std::runtime_error("PacketServerLuaPackageBootstrapComplete: trailing data");
        }
    };
}

#endif
