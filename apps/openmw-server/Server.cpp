#include "Server.hpp"
#include "ActorRegistryInvariant.hpp"
#include "AlchemyService.hpp"
#include "CrimeService.hpp"
#include "CrimeSemanticService.hpp"
#include "FactionService.hpp"
#include "JailSentenceService.hpp"
#include "DynamicRecordService.hpp"
#include "EnchantingService.hpp"
#include "DoorStateAuthority.hpp"
#include "MasterServerClient.hpp"
#include "ServerCollisionWorld.hpp"
#include "ServerLuaRecordParser.hpp"
#include <extern/bcrypt/bcrypt.h>

// GNS C++ crypto API - CECSigningPublicKey::VerifySignature for challenge-response auth.
// Include paths: extern/GameNetworkingSockets/src/common + src/public
#include <crypto_25519.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "bindings/PlayerBindings.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <variant>

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/misc/constants.hpp>
#include <components/openmw-mp/MasterServerProtocol.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Packets/System/PacketGameSettings.hpp>
#include <components/openmw-mp/Packets/System/PacketHandshake.hpp>
#include <components/openmw-mp/Packets/System/PacketServerLuaPackage.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBaseInfo.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBounty.hpp>
#include <components/openmw-mp/Packets/Player/PacketGuardArrest.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerFaction.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerTopic.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCharGen.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerPosition.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCellChange.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerLoadedCells.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerEquipment.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAnimFlags.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAnimPlay.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAttack.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCast.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpeech.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerVehicleState.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerVehicleRequest.hpp>
#include <components/openmw-mp/Base/VehicleProfiles.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerInventory.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerJournal.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerStatsDynamic.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerDeath.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerResurrect.hpp>
#include <components/openmw-mp/Packets/Player/PacketChatMessage.hpp>
#include <components/openmw-mp/Packets/Lua/PacketLuaEvent.hpp>
#include <components/openmw-mp/Packets/Lua/PacketLuaStorage.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectPlace.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectDelete.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectCount.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectMove.hpp>
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>
#include <components/openmw-mp/Packets/Object/PacketWorldItemTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryPut.hpp>
#include <components/openmw-mp/Packets/Object/PacketBarter.hpp>
#include <components/openmw-mp/Packets/Object/PacketCrimeInteraction.hpp>
#include <components/openmw-mp/Packets/Object/PacketDoorState.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketRecordDynamic.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketRuntimeContentBootstrapComplete.hpp>
#include <components/openmw-mp/Packets/Records/PacketRecordCreateRequest.hpp>
#include <components/openmw-mp/Packets/Records/PacketRecordCreateResult.hpp>
#include <components/openmw-mp/Packets/Records/PacketAlchemyRequest.hpp>
#include <components/openmw-mp/Packets/Records/PacketAlchemyResult.hpp>
#include <components/openmw-mp/Packets/Records/PacketEnchantingRequest.hpp>
#include <components/openmw-mp/Packets/Records/PacketEnchantingResult.hpp>
#include <components/openmw-mp/Records/AlchemyProtocol.hpp>
#include <components/openmw-mp/Records/EnchantingProtocol.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordDependencies.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>
#include <components/openmw-mp/Sha256.hpp>
#include <components/version/version.hpp>
#include <components/openmw-mp/SpellbookSync.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpellbook.hpp>
#include <components/esm3/loadspel.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAI.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAnimFlags.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAnimPlay.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAttack.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAttackV2.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorSpeech.hpp>
#include <components/openmw-mp/Packets/Actor/PacketCrimeReaction.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAuthority.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCast.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCellChange.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorDeath.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorEquipment.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorIdentity.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorList.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPosition.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPositionV2.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPresentationV2.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorStatsDynamic.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCombatRequest.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCombatResult.hpp>
#include <components/openmw-mp/Packets/Actor/PacketCorpseDispose.hpp>
#include <components/openmw-mp/Packets/Actor/PacketMechanicsSnapshot.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketWorldTime.hpp>
// PacketWorldWeather is defined in PacketWorldTime.hpp

// Encode/decode ESM::Class::CLDTstruct as 15 comma-separated ints.
// Format: specialization, attr[0], attr[1], skills[0..4][0..1], isPlayable, services
namespace
{
    constexpr std::uint16_t ServerLuaValidationVersion = 3;

    std::string lowerAscii(std::string_view value)
    {
        std::string out(value);
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::string normalizeRuntimeAsset(std::string_view value)
    {
        std::string out = lowerAscii(value);
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }

    std::optional<mwmp::records::RecordType> parseRuntimeRecordType(std::string_view value)
    {
        const std::string normalized = lowerAscii(value);
        using Type = mwmp::records::RecordType;
        if (normalized == "potion") return Type::Potion;
        if (normalized == "enchantment") return Type::Enchantment;
        if (normalized == "weapon") return Type::Weapon;
        if (normalized == "armor") return Type::Armor;
        if (normalized == "clothing") return Type::Clothing;
        if (normalized == "book") return Type::Book;
        return std::nullopt;
    }

    bool isSha256(std::string_view value)
    {
        return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        });
    }

    std::vector<mwmp::PacketHandshakeResponse::PluginMismatch> validateContentFiles(
        const std::vector<mwmp::PacketHandshake::PluginEntry>& clientFiles,
        const std::vector<mwmp::ContentFileRule>& requiredFiles,
        bool strictOrder,
        bool requireExactList)
    {
        using Mismatch = mwmp::PacketHandshakeResponse::PluginMismatch;
        std::vector<Mismatch> mismatches;
        std::unordered_set<std::string> seenClientNames;
        for (const auto& clientFile : clientFiles)
        {
            const std::string normalizedName = lowerAscii(clientFile.filename);
            if (normalizedName.empty() || !seenClientNames.insert(normalizedName).second)
            {
                mismatches.push_back({ clientFile.filename, {}, clientFile.sha256,
                    normalizedName.empty() ? "empty filename" : "duplicate filename" });
            }
        }

        std::size_t previousIndex = 0;
        bool havePreviousIndex = false;
        std::unordered_set<std::string> requiredNames;
        for (const auto& required : requiredFiles)
        {
            const std::string normalizedName = lowerAscii(required.filename);
            if (normalizedName.empty() || !requiredNames.insert(normalizedName).second)
            {
                mismatches.push_back({ required.filename, required.sha256, {},
                    normalizedName.empty() ? "server manifest has an empty filename"
                                           : "server manifest has a duplicate filename" });
                continue;
            }
            if (!isSha256(required.sha256))
            {
                mismatches.push_back(
                    { required.filename, required.sha256, {}, "server manifest has an invalid SHA-256" });
                continue;
            }

            const auto clientIt = std::find_if(clientFiles.begin(), clientFiles.end(), [&](const auto& client) {
                return lowerAscii(client.filename) == normalizedName;
            });
            if (clientIt == clientFiles.end())
            {
                mismatches.push_back({ required.filename, lowerAscii(required.sha256), {}, "required file is missing" });
                continue;
            }

            const std::size_t clientIndex = static_cast<std::size_t>(std::distance(clientFiles.begin(), clientIt));
            if (strictOrder && havePreviousIndex && clientIndex <= previousIndex)
            {
                mismatches.push_back(
                    { required.filename, lowerAscii(required.sha256), clientIt->sha256, "required file is out of order" });
            }
            previousIndex = clientIndex;
            havePreviousIndex = true;

            if (!isSha256(clientIt->sha256))
            {
                mismatches.push_back({ required.filename, lowerAscii(required.sha256), clientIt->sha256,
                    "client could not provide a valid SHA-256" });
            }
            else if (lowerAscii(clientIt->sha256) != lowerAscii(required.sha256))
            {
                mismatches.push_back({ required.filename, lowerAscii(required.sha256), lowerAscii(clientIt->sha256),
                    "SHA-256 mismatch" });
            }
        }

        if (requireExactList)
        {
            for (const auto& clientFile : clientFiles)
            {
                if (!requiredNames.contains(lowerAscii(clientFile.filename)))
                    mismatches.push_back({ clientFile.filename, {}, clientFile.sha256, "unexpected file" });
            }
        }

        return mismatches;
    }

    std::vector<mwmp::PacketHandshakeResponse::PluginMismatch> validateExactOrderedManifestAllowingDuplicates(
        const std::vector<mwmp::PacketHandshake::PluginEntry>& clientFiles,
        const std::vector<mwmp::ContentFileRule>& requiredFiles)
    {
        using Mismatch = mwmp::PacketHandshakeResponse::PluginMismatch;
        std::vector<Mismatch> mismatches;
        const std::size_t shared = std::min(clientFiles.size(), requiredFiles.size());
        for (std::size_t i = 0; i < shared; ++i)
        {
            const auto& client = clientFiles[i];
            const auto& required = requiredFiles[i];
            const std::string requiredName = lowerAscii(required.filename);
            const std::string clientName = lowerAscii(client.filename);

            if (requiredName.empty())
            {
                mismatches.push_back({ required.filename, required.sha256, client.sha256,
                    "server manifest has an empty filename" });
                continue;
            }
            if (!isSha256(required.sha256))
            {
                mismatches.push_back({ required.filename, required.sha256, client.sha256,
                    "server manifest has an invalid SHA-256" });
                continue;
            }
            if (clientName.empty())
            {
                mismatches.push_back({ required.filename, lowerAscii(required.sha256), client.sha256,
                    "client manifest has an empty filename" });
                continue;
            }
            if (clientName != requiredName)
            {
                mismatches.push_back({ required.filename, lowerAscii(required.sha256), client.sha256,
                    "required file is missing or out of order (client has " + client.filename + ")" });
                continue;
            }
            if (!isSha256(client.sha256))
            {
                mismatches.push_back({ required.filename, lowerAscii(required.sha256), client.sha256,
                    "client could not provide a valid SHA-256" });
            }
            else if (lowerAscii(client.sha256) != lowerAscii(required.sha256))
            {
                mismatches.push_back({ required.filename, lowerAscii(required.sha256), lowerAscii(client.sha256),
                    "SHA-256 mismatch" });
            }
        }

        for (std::size_t i = shared; i < requiredFiles.size(); ++i)
            mismatches.push_back({ requiredFiles[i].filename, lowerAscii(requiredFiles[i].sha256), {},
                "required file is missing" });
        for (std::size_t i = shared; i < clientFiles.size(); ++i)
            mismatches.push_back({ clientFiles[i].filename, {}, clientFiles[i].sha256, "unexpected file" });
        return mismatches;
    }

    std::string contentFileRejectReason(
        const std::vector<mwmp::PacketHandshakeResponse::PluginMismatch>& mismatches,
        const std::string& helpUrl)
    {
        std::ostringstream out;
        out << "Content-file validation failed";
        if (!mismatches.empty())
        {
            const auto& mismatch = mismatches.front();
            out << ": " << (mismatch.filename.empty() ? "content list" : mismatch.filename)
                << " (" << mismatch.reason;
            if (!mismatch.expectedSha256.empty())
                out << "; expected SHA-256=" << mismatch.expectedSha256;
            if (!mismatch.actualSha256.empty())
                out << "; actual SHA-256=" << mismatch.actualSha256;
            out << ")";
        }
        out << ". Update your required mods and load order.";
        if (!helpUrl.empty())
            out << " Help: " << helpUrl;
        return out.str();
    }

    std::string jsonEscape(std::string_view text)
    {
        std::string out;
        out.reserve(text.size() + 8);
        for (char c : text)
        {
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char buffer[8];
                        std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                        out += buffer;
                    }
                    else
                    {
                        out.push_back(c);
                    }
                    break;
            }
        }
        return out;
    }

    std::string makeJsonErrorBody(std::string_view error)
    {
        return std::string("{\"ok\":false,\"error\":\"") + jsonEscape(error) + "\"}";
    }

    std::string makeDynamicRecordKey(std::string_view recordType, std::string_view recordId)
    {
        std::string key;
        key.reserve(recordType.size() + 1 + recordId.size());
        key.append(recordType);
        key.push_back('\x1f');
        key.append(recordId);
        return key;
    }

    std::optional<uint64_t> parseGeneratedRecordNumber(
        std::string_view prefix, std::string_view recordType, std::string_view recordId)
    {
        if (prefix.empty() || recordType.empty())
            return std::nullopt;

        std::string expectedPrefix;
        expectedPrefix.reserve(prefix.size() + recordType.size() + 2);
        expectedPrefix.append(prefix);
        expectedPrefix.push_back('_');
        expectedPrefix.append(recordType);
        expectedPrefix.push_back('_');

        if (!recordId.starts_with(expectedPrefix))
            return std::nullopt;

        std::string_view suffix = recordId.substr(expectedPrefix.size());
        if (suffix.empty())
            return std::nullopt;

        uint64_t value = 0;
        const auto [ptr, ec] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
        if (ec != std::errc() || ptr != suffix.data() + suffix.size())
            return std::nullopt;
        return value;
    }

    constexpr unsigned char sLuaFormatVersion = 0;
    constexpr unsigned char sLuaShortStringFlag = 0x20;

    enum class SerializedLuaType : unsigned char
    {
        Number = 0x0,
        LongString = 0x1,
        Boolean = 0x2,
        TableStart = 0x3,
        TableEnd = 0x4,
    };

    struct LuaWireValue;
    using LuaWireTable = std::vector<std::pair<std::string, LuaWireValue>>;

    struct LuaWireValue
    {
        using Variant = std::variant<std::monostate, double, bool, std::string, LuaWireTable>;
        Variant value;

        LuaWireValue() = default;
        LuaWireValue(double v)
            : value(v)
        {
        }
        LuaWireValue(bool v)
            : value(v)
        {
        }
        LuaWireValue(std::string v)
            : value(std::move(v))
        {
        }
        LuaWireValue(const char* v)
            : value(std::string(v))
        {
        }
        LuaWireValue(LuaWireTable v)
            : value(std::move(v))
        {
        }
    };

    template <typename T>
    T readLuaValue(std::string_view& data)
    {
        if (data.size() < sizeof(T))
            throw std::runtime_error("Unexpected end of Lua event payload");

        T value;
        std::memcpy(&value, data.data(), sizeof(T));
        data.remove_prefix(sizeof(T));
        return value;
    }

    std::string readLuaString(std::string_view& data, std::size_t size)
    {
        if (data.size() < size)
            throw std::runtime_error("Unexpected end of Lua string payload");

        std::string value(data.substr(0, size));
        data.remove_prefix(size);
        return value;
    }

    LuaWireValue parseLuaWireValue(std::string_view& data)
    {
        if (data.empty())
            throw std::runtime_error("Unexpected end of Lua payload");

        const unsigned char type = static_cast<unsigned char>(data.front());
        data.remove_prefix(1);

        if (type & sLuaShortStringFlag)
            return LuaWireValue(readLuaString(data, type & 0x1f));

        switch (static_cast<SerializedLuaType>(type))
        {
            case SerializedLuaType::Number:
                return LuaWireValue(readLuaValue<double>(data));
            case SerializedLuaType::LongString:
                return LuaWireValue(readLuaString(data, readLuaValue<std::uint32_t>(data)));
            case SerializedLuaType::Boolean:
                return LuaWireValue(readLuaValue<char>(data) != 0);
            case SerializedLuaType::TableStart:
            {
                LuaWireTable table;
                while (!data.empty() && static_cast<unsigned char>(data.front()) != static_cast<unsigned char>(SerializedLuaType::TableEnd))
                {
                    LuaWireValue key = parseLuaWireValue(data);
                    LuaWireValue value = parseLuaWireValue(data);
                    if (const auto* stringKey = std::get_if<std::string>(&key.value))
                        table.emplace_back(*stringKey, std::move(value));
                }

                if (data.empty())
                    throw std::runtime_error("Unexpected end of Lua table payload");

                data.remove_prefix(1);
                return LuaWireValue(std::move(table));
            }
            case SerializedLuaType::TableEnd:
                throw std::runtime_error("Unexpected end-of-table marker in Lua payload");
        }

        throw std::runtime_error("Unsupported Lua payload type");
    }

    LuaWireTable parseLuaWireTable(const std::string& data)
    {
        std::string_view view(data);
        if (view.empty())
            return {};
        if (static_cast<unsigned char>(view.front()) != sLuaFormatVersion)
            throw std::runtime_error("Unsupported Lua payload format version");

        view.remove_prefix(1);
        LuaWireValue root = parseLuaWireValue(view);
        if (!view.empty())
            throw std::runtime_error("Unexpected trailing bytes in Lua payload");

        if (const auto* table = std::get_if<LuaWireTable>(&root.value))
            return *table;

        throw std::runtime_error("Expected table payload");
    }

    const LuaWireValue* findLuaField(const LuaWireTable& table, std::string_view key)
    {
        for (const auto& [field, value] : table)
        {
            if (field == key)
                return &value;
        }
        return nullptr;
    }

    const LuaWireTable* getLuaTableField(const LuaWireTable& table, std::string_view key)
    {
        const LuaWireValue* value = findLuaField(table, key);
        return value ? std::get_if<LuaWireTable>(&value->value) : nullptr;
    }

    std::string getLuaStringField(const LuaWireTable& table, std::string_view key, std::string defaultValue = {})
    {
        const LuaWireValue* value = findLuaField(table, key);
        if (!value)
            return defaultValue;
        if (const auto* str = std::get_if<std::string>(&value->value))
            return *str;
        return defaultValue;
    }

    double getLuaNumberField(const LuaWireTable& table, std::string_view key, double defaultValue = 0.0)
    {
        const LuaWireValue* value = findLuaField(table, key);
        if (!value)
            return defaultValue;
        if (const auto* number = std::get_if<double>(&value->value))
            return *number;
        return defaultValue;
    }

    bool getLuaBoolField(const LuaWireTable& table, std::string_view key, bool defaultValue = false)
    {
        const LuaWireValue* value = findLuaField(table, key);
        if (!value)
            return defaultValue;
        if (const auto* boolean = std::get_if<bool>(&value->value))
            return *boolean;
        return defaultValue;
    }

    void appendLuaBytes(std::string& out, const void* bytes, std::size_t size)
    {
        out.append(static_cast<const char*>(bytes), size);
    }

    template <typename T>
    void appendLuaPod(std::string& out, T value)
    {
        appendLuaBytes(out, &value, sizeof(T));
    }

    void serializeLuaWireValue(std::string& out, const LuaWireValue& value);

    void appendLuaString(std::string& out, std::string_view value)
    {
        if (value.size() < 32)
        {
            out.push_back(static_cast<char>(sLuaShortStringFlag | static_cast<unsigned char>(value.size())));
        }
        else
        {
            out.push_back(static_cast<char>(SerializedLuaType::LongString));
            appendLuaPod<std::uint32_t>(out, static_cast<std::uint32_t>(value.size()));
        }

        out.append(value.data(), value.size());
    }

    void serializeLuaWireTable(std::string& out, const LuaWireTable& table)
    {
        out.push_back(static_cast<char>(SerializedLuaType::TableStart));
        for (const auto& [key, value] : table)
        {
            appendLuaString(out, key);
            serializeLuaWireValue(out, value);
        }
        out.push_back(static_cast<char>(SerializedLuaType::TableEnd));
    }

    void serializeLuaWireValue(std::string& out, const LuaWireValue& value)
    {
        if (std::holds_alternative<std::monostate>(value.value))
            throw std::runtime_error("Can not serialize nil into ActivateResult payload");

        if (const auto* number = std::get_if<double>(&value.value))
        {
            out.push_back(static_cast<char>(SerializedLuaType::Number));
            appendLuaPod<double>(out, *number);
            return;
        }

        if (const auto* boolean = std::get_if<bool>(&value.value))
        {
            out.push_back(static_cast<char>(SerializedLuaType::Boolean));
            out.push_back(*boolean ? 1 : 0);
            return;
        }

        if (const auto* stringValue = std::get_if<std::string>(&value.value))
        {
            appendLuaString(out, *stringValue);
            return;
        }

        if (const auto* table = std::get_if<LuaWireTable>(&value.value))
        {
            serializeLuaWireTable(out, *table);
            return;
        }

        throw std::runtime_error("Unsupported ActivateResult payload value");
    }

    std::string serializeLuaWireTable(const LuaWireTable& table)
    {
        std::string out;
        out.push_back(static_cast<char>(sLuaFormatVersion));
        serializeLuaWireTable(out, table);
        return out;
    }

    std::string encodeClassData(const ESM::Class::CLDTstruct& d)
    {
        std::ostringstream ss;
        ss << d.mSpecialization
           << ',' << d.mAttribute[0] << ',' << d.mAttribute[1];
        for (const auto& row : d.mSkills)
            for (auto v : row)
                ss << ',' << v;
        ss << ',' << d.mIsPlayable << ',' << d.mServices;
        return ss.str();
    }

    void decodeClassData(const std::string& s, ESM::Class::CLDTstruct& d)
    {
        if (s.empty()) return;
        std::istringstream ss(s);
        char comma;
        ss >> d.mSpecialization
           >> comma >> d.mAttribute[0] >> comma >> d.mAttribute[1];
        for (auto& row : d.mSkills)
            for (auto& v : row)
                ss >> comma >> v;
        ss >> comma >> d.mIsPlayable >> comma >> d.mServices;
    }

    std::string makeContainerKey(const std::string& cellId,
                                 const std::string& refId,
                                 uint32_t refNum,
                                 uint32_t mpNum = 0)
    {
        if (mpNum != 0)
            return cellId + "|mp|" + std::to_string(mpNum);
        return cellId + "|" + refId + "|" + std::to_string(refNum);
    }

    std::string makeWorldItemKey(const mwmp::PlacedObjectIdentity& identity)
    {
        return std::to_string(static_cast<unsigned>(identity.kind)) + "|" + identity.cellId + "|"
            + identity.refId + "|" + std::to_string(identity.refIndex) + "|"
            + std::to_string(identity.refContentFile) + "|" + std::to_string(identity.mpNum);
    }

    void appendOrMergeContainerItem(std::vector<mwmp::ContainerItem>& items, const mwmp::ContainerItem& item)
    {
        if (item.refId.empty() || item.count <= 0)
            return;

        auto it = std::find_if(items.begin(), items.end(),
            [&](const mwmp::ContainerItem& current)
            {
                if (current.instanceId != 0 || item.instanceId != 0)
                    return current.instanceId != 0 && current.instanceId == item.instanceId;
                return lowerAscii(current.refId) == lowerAscii(item.refId)
                    && current.charge == item.charge
                    && std::abs(current.enchantmentCharge - item.enchantmentCharge) < 0.001f
                    && current.soul == item.soul && current.restocking == item.restocking;
            });

        if (it == items.end())
            items.push_back(item);
        else
            it->count += item.count;
    }

    void normalizeContainerItems(std::vector<mwmp::ContainerItem>& items)
    {
        std::vector<mwmp::ContainerItem> normalized;
        normalized.reserve(items.size());

        for (const auto& item : items)
            appendOrMergeContainerItem(normalized, item);

        items = std::move(normalized);
    }

    bool sameItemIdentity(const mwmp::Item& left, const mwmp::Item& right)
    {
        return left.refId == right.refId
            && left.charge == right.charge
            && std::abs(left.enchantmentCharge - right.enchantmentCharge) < 0.001f
            && left.soul == right.soul;
    }

    bool inventoryContainsItemIdentity(const std::vector<mwmp::Item>& items, const mwmp::Item& target)
    {
        return std::any_of(items.begin(), items.end(), [&](const mwmp::Item& item) {
            if (target.instanceId != 0)
                return item.instanceId == target.instanceId && item.refId == target.refId;
            return sameItemIdentity(item, target);
        });
    }

    bool ensureInventoryContainsEquippedItems(mwmp::BasePlayer& player)
    {
        bool changed = false;
        player.inventoryChanges.action = mwmp::BasePlayer::InventoryChanges::Action::Set;

        for (const auto& entry : player.equipment)
        {
            const mwmp::Item& item = entry.item;
            if (item.refId.empty() || item.count <= 0)
                continue;
            if (inventoryContainsItemIdentity(player.inventoryChanges.items, item))
                continue;

            player.inventoryChanges.items.push_back(item);
            changed = true;
        }

        return changed;
    }

    bool sameItemStack(const mwmp::Item& left, const mwmp::Item& right)
    {
        return sameItemIdentity(left, right) && left.count == right.count;
    }

    bool sameCosmeticItemStack(const mwmp::Item& left, const mwmp::Item& right)
    {
        return left.refId == right.refId && left.count == right.count;
    }

    bool itemStackLess(const mwmp::Item& left, const mwmp::Item& right)
    {
        return std::tie(left.refId, left.charge, left.enchantmentCharge, left.soul, left.count)
            < std::tie(right.refId, right.charge, right.enchantmentCharge, right.soul, right.count);
    }

    bool cosmeticItemStackLess(const mwmp::Item& left, const mwmp::Item& right)
    {
        return std::tie(left.refId, left.count) < std::tie(right.refId, right.count);
    }

    bool sameInventorySnapshot(std::vector<mwmp::Item> left, std::vector<mwmp::Item> right)
    {
        left.erase(std::remove_if(left.begin(), left.end(), [](const mwmp::Item& item) {
            return item.refId.empty() || item.count <= 0;
        }), left.end());
        right.erase(std::remove_if(right.begin(), right.end(), [](const mwmp::Item& item) {
            return item.refId.empty() || item.count <= 0;
        }), right.end());

        std::sort(left.begin(), left.end(), itemStackLess);
        std::sort(right.begin(), right.end(), itemStackLess);
        if (left.size() != right.size())
            return false;

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (!sameItemStack(left[i], right[i]))
                return false;
        }
        return true;
    }

    bool sameCosmeticInventorySnapshot(std::vector<mwmp::Item> left, std::vector<mwmp::Item> right)
    {
        left.erase(std::remove_if(left.begin(), left.end(), [](const mwmp::Item& item) {
            return item.refId.empty() || item.count <= 0;
        }), left.end());
        right.erase(std::remove_if(right.begin(), right.end(), [](const mwmp::Item& item) {
            return item.refId.empty() || item.count <= 0;
        }), right.end());

        std::sort(left.begin(), left.end(), cosmeticItemStackLess);
        std::sort(right.begin(), right.end(), cosmeticItemStackLess);
        if (left.size() != right.size())
            return false;

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (!sameCosmeticItemStack(left[i], right[i]))
                return false;
        }
        return true;
    }

    bool looksLikeRestoredInventoryRegression(
        const mwmp::BasePlayer::InventoryChanges& incoming,
        const std::vector<mwmp::Item>& restored)
    {
        if (incoming.action != mwmp::BasePlayer::InventoryChanges::Action::Set || restored.empty())
            return false;
        if (sameInventorySnapshot(incoming.items, restored)
            || sameCosmeticInventorySnapshot(incoming.items, restored))
            return false;

        std::size_t restoredPositive = 0;
        std::size_t incomingPositive = 0;
        for (const auto& item : restored)
        {
            if (!item.refId.empty() && item.count > 0)
                ++restoredPositive;
        }
        for (const auto& item : incoming.items)
        {
            if (!item.refId.empty() && item.count > 0)
                ++incomingPositive;
        }

        if (incomingPositive < restoredPositive)
            return true;

        return std::any_of(restored.begin(), restored.end(), [&](const mwmp::Item& item) {
            return !item.refId.empty() && item.count > 0
                && !inventoryContainsItemIdentity(incoming.items, item);
        });
    }

    bool looksLikeRestoredSpellbookRegression(const mwmp::BasePlayer::SpellbookChanges& incoming,
        const std::vector<std::string>& restored)
    {
        // During login the client may send its pre-restore learned set before
        // applying the authoritative spellbook (or a mid-chargen partial set).
        // A full Set missing restored spells is a regression and must not
        // clobber the persisted state. Deltas are never treated as regressions:
        // Adds are harmless and Removes of a restored spell are only possible
        // from a client that already acknowledged it.
        if (incoming.action != mwmp::BasePlayer::SpellbookChanges::Action::Set || restored.empty())
            return false;

        std::vector<std::string> incomingSet = mwmp::canonicalizeSpellIds(incoming.spellIds);
        if (incomingSet == mwmp::canonicalizeSpellIds(restored))
            return false;

        return std::any_of(restored.begin(), restored.end(), [&](const std::string& spellId) {
            return std::find(incomingSet.begin(), incomingSet.end(), spellId) == incomingSet.end();
        });
    }

    std::size_t equippedItemCount(
        const std::array<mwmp::EquipmentItem, mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS>& equipment)
    {
        return static_cast<std::size_t>(std::count_if(equipment.begin(), equipment.end(),
            [](const mwmp::EquipmentItem& entry) { return !entry.item.refId.empty() && entry.item.count > 0; }));
    }

    bool sameEquipmentSnapshot(
        const std::array<mwmp::EquipmentItem, mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS>& left,
        const std::array<mwmp::EquipmentItem, mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS>& right)
    {
        for (int slot = 0; slot < mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS; ++slot)
        {
            if (left[slot].slot != right[slot].slot)
                return false;
            if (!sameItemStack(left[slot].item, right[slot].item))
                return false;
        }
        return true;
    }

    bool sameCosmeticEquipmentSnapshot(
        const std::array<mwmp::EquipmentItem, mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS>& left,
        const std::array<mwmp::EquipmentItem, mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS>& right)
    {
        for (int slot = 0; slot < mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS; ++slot)
        {
            if (left[slot].slot != right[slot].slot)
                return false;
            if (!sameCosmeticItemStack(left[slot].item, right[slot].item))
                return false;
        }
        return true;
    }

    bool looksLikeRestoredEquipmentRegression(
        const std::array<mwmp::EquipmentItem, mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS>& incoming,
        const std::array<mwmp::EquipmentItem, mwmp::BasePlayer::NUM_EQUIPMENT_SLOTS>& restored)
    {
        // Until the client echoes the restored snapshot, every slot is server
        // authoritative, including intentionally empty slots.  Treating a
        // superset as valid allowed OpenMW's startup auto-equip pass to fill those
        // empty slots and immediately overwrite the database snapshot.
        return !sameEquipmentSnapshot(incoming, restored)
            && !sameCosmeticEquipmentSnapshot(incoming, restored);
    }

    void applyContainerDelta(std::vector<mwmp::ContainerItem>& items,
                             const mwmp::ContainerItem& item,
                             mwmp::ContainerAction action)
    {
        if (item.refId.empty() || item.count <= 0)
            return;

        auto it = std::find_if(items.begin(), items.end(),
            [&](const mwmp::ContainerItem& current)
            {
                if (current.instanceId != 0 || item.instanceId != 0)
                    return current.instanceId != 0 && current.instanceId == item.instanceId;
                return lowerAscii(current.refId) == lowerAscii(item.refId)
                    && current.charge == item.charge
                    && std::abs(current.enchantmentCharge - item.enchantmentCharge) < 0.001f
                    && current.soul == item.soul && current.restocking == item.restocking;
            });

        if (action == mwmp::ContainerAction::Add)
        {
            if (it == items.end())
                items.push_back(item);
            else
                it->count += item.count;
            return;
        }

        if (action != mwmp::ContainerAction::Remove)
            return;

        int remaining = item.count;

        if (it != items.end())
        {
            const int removed = std::min(it->count, remaining);
            it->count -= removed;
            remaining -= removed;
            if (it->count <= 0)
                items.erase(it);
        }

        for (auto current = items.begin(); remaining > 0 && current != items.end();)
        {
            if (lowerAscii(current->refId) != lowerAscii(item.refId)
                || current->restocking != item.restocking)
            {
                ++current;
                continue;
            }

            const int removed = std::min(current->count, remaining);
            current->count -= removed;
            remaining -= removed;

            if (current->count <= 0)
                current = items.erase(current);
            else
                ++current;
        }
    }

    std::string makeCellKey(const mwmp::CellId& cell)
    {
        if (!cell.isExterior)
            return cell.cellName;

        char buf[32];
        std::snprintf(buf, sizeof(buf), "EXT:%d,%d", cell.gridX, cell.gridY);
        return buf;
    }

    std::optional<mwmp::CellId> parseCellKey(std::string_view cellId)
    {
        if (cellId.empty())
            return std::nullopt;

        mwmp::CellId parsed;
        if (cellId.rfind("EXT:", 0) == 0)
        {
            int gridX = 0;
            int gridY = 0;
            if (std::sscanf(cellId.data(), "EXT:%d,%d", &gridX, &gridY) != 2)
                return std::nullopt;

            parsed.isExterior = true;
            parsed.gridX = gridX;
            parsed.gridY = gridY;
            return parsed;
        }

        parsed.cellName = std::string(cellId);
        return parsed;
    }

    bool cellMatches(const mwmp::CellId& playerCell, const std::string& cellId)
    {
        if (cellId.rfind("EXT:", 0) == 0)
        {
            int gridX = 0;
            int gridY = 0;
            if (std::sscanf(cellId.c_str(), "EXT:%d,%d", &gridX, &gridY) != 2)
                return false;

            return playerCell.isExterior
                && playerCell.gridX == gridX
                && playerCell.gridY == gridY;
        }

        return !playerCell.isExterior && playerCell.cellName == cellId;
    }

    bool isExteriorCellKey(const std::string& cellId)
    {
        return cellId.rfind("EXT:", 0) == 0;
    }

    std::string exteriorCellIdForPosition(const mwmp::Position& position)
    {
        const float cellSize = static_cast<float>(Constants::CellSizeInUnits);
        const int gridX = static_cast<int>(std::floor(position.pos[0] / cellSize));
        const int gridY = static_cast<int>(std::floor(position.pos[1] / cellSize));
        return std::string("EXT:") + std::to_string(gridX) + "," + std::to_string(gridY);
    }

    float exteriorCellBorderDistance(const mwmp::Position& position)
    {
        const float cellSize = static_cast<float>(Constants::CellSizeInUnits);
        const float gridX = std::floor(position.pos[0] / cellSize);
        const float gridY = std::floor(position.pos[1] / cellSize);
        const float localX = position.pos[0] - gridX * cellSize;
        const float localY = position.pos[1] - gridY * cellSize;
        return std::min(std::min(localX, cellSize - localX), std::min(localY, cellSize - localY));
    }

    constexpr std::size_t MaxLoadedActorCells = 25;

    std::string makeActorKey(const mwmp::BaseActor& actor)
    {
        if (actor.mpNum != 0)
            return "mp|" + std::to_string(actor.mpNum);
        return actor.refId + "|" + std::to_string(actor.refNum) + "|" + std::to_string(actor.mpNum);
    }

    bool normalizeActorIdentity(mwmp::BaseActor& actor)
    {
        const bool ambiguous = mwmp::hasAmbiguousActorInstanceIdentity(actor);
        if (actor.mpNum != 0)
            actor.refNum = 0;
        return ambiguous;
    }

    bool isGeneratedSpawnerRefId(const std::string& refId)
    {
        return refId.rfind("spawner_", 0) == 0;
    }

    bool isUnmanagedSpawnerActor(const mwmp::BaseActor& actor)
    {
        return actor.mpNum == 0 && isGeneratedSpawnerRefId(actor.refId);
    }

    bool isNewerEventId(uint32_t incoming, uint32_t previous)
    {
        if (incoming == 0 || previous == 0)
            return true;

        return static_cast<int32_t>(incoming - previous) > 0;
    }

    bool sameDynamicStat(const mwmp::DynamicStat& a, const mwmp::DynamicStat& b)
    {
        return a.base == b.base && a.current == b.current && a.mod == b.mod;
    }

    bool sameDynamicStats(const mwmp::DynamicStats& a, const mwmp::DynamicStats& b)
    {
        return sameDynamicStat(a.health, b.health)
            && sameDynamicStat(a.magicka, b.magicka)
            && sameDynamicStat(a.fatigue, b.fatigue);
    }

    bool sameStatFloat(float a, float b)
    {
        return std::abs(a - b) <= 0.001f;
    }

    bool samePersistentAttribute(const mwmp::Attribute& a, const mwmp::Attribute& b)
    {
        return a.base == b.base
            && sameStatFloat(a.mod, b.mod)
            && sameStatFloat(a.damage, b.damage);
    }

    bool samePersistentSkill(const mwmp::Skill& a, const mwmp::Skill& b)
    {
        return sameStatFloat(a.base, b.base)
            && sameStatFloat(a.mod, b.mod)
            && sameStatFloat(a.damage, b.damage)
            && sameStatFloat(a.progress, b.progress)
            && a.increases == b.increases;
    }

    bool samePersistentPlayerStats(const mwmp::BasePlayer& a, const mwmp::BasePlayer& b)
    {
        if (!sameDynamicStats(a.dynamicStats, b.dynamicStats)
            || a.level != b.level
            || !sameStatFloat(a.levelProgress, b.levelProgress))
            return false;

        for (std::size_t i = 0; i < a.attributes.size(); ++i)
        {
            if (!samePersistentAttribute(a.attributes[i], b.attributes[i]))
                return false;
        }

        for (std::size_t i = 0; i < a.skills.size(); ++i)
        {
            if (!samePersistentSkill(a.skills[i], b.skills[i]))
                return false;
        }

        return true;
    }

    bool looksLikeRestoredStatsRegression(const mwmp::BasePlayer& incoming, const mwmp::BasePlayer& restored)
    {
        for (std::size_t i = 0; i < incoming.attributes.size(); ++i)
        {
            if (restored.attributes[i].base > 100
                && incoming.attributes[i].base <= 100
                && incoming.attributes[i].base < restored.attributes[i].base)
                return true;
        }

        for (std::size_t i = 0; i < incoming.skills.size(); ++i)
        {
            if (restored.skills[i].base > 100.f
                && incoming.skills[i].base <= 100.f
                && incoming.skills[i].base < restored.skills[i].base)
                return true;
        }

        return false;
    }

    void copyPersistentPlayerStats(mwmp::BasePlayer& dst, const mwmp::BasePlayer& src)
    {
        dst.dynamicStats = src.dynamicStats;
        dst.attributes = src.attributes;
        dst.skills = src.skills;
        dst.level = src.level;
        dst.levelProgress = src.levelProgress;
        dst.hasSavedStats = src.hasSavedStats;
    }

    bool isZeroActorPosition(const mwmp::Position& position)
    {
        return std::abs(position.pos[0]) <= 0.001f
            && std::abs(position.pos[1]) <= 0.001f
            && std::abs(position.pos[2]) <= 0.001f;
    }

    float positionDistanceSquared(const mwmp::Position& lhs, const mwmp::Position& rhs)
    {
        const float dx = lhs.pos[0] - rhs.pos[0];
        const float dy = lhs.pos[1] - rhs.pos[1];
        const float dz = lhs.pos[2] - rhs.pos[2];
        return dx * dx + dy * dy + dz * dz;
    }

    bool sameDeadVanillaActorState(const mwmp::BaseActor& a, const mwmp::BaseActor& b)
    {
        return a.refId == b.refId
            && a.refNum == b.refNum
            && a.mpNum == b.mpNum
            && a.cellId == b.cellId
            && a.position.pos[0] == b.position.pos[0]
            && a.position.pos[1] == b.position.pos[1]
            && a.position.pos[2] == b.position.pos[2]
            && a.position.rot[0] == b.position.rot[0]
            && a.position.rot[1] == b.position.rot[1]
            && a.position.rot[2] == b.position.rot[2]
            && sameDynamicStats(a.dynamicStats, b.dynamicStats)
            && a.deathState == b.deathState
            && a.isDead == b.isDead
            && a.isInstantDeath == b.isInstantDeath
            && a.deathAnimGroup == b.deathAnimGroup;
    }

    bool isLocomotionAnimGroup(const std::string& group)
    {
        return group.find("walk") != std::string::npos
            || group.find("run") != std::string::npos
            || group.find("swim") != std::string::npos
            || group.find("sneak") != std::string::npos
            || group.find("turn") != std::string::npos;
    }

    bool isIdleAnimGroup(const std::string& group)
    {
        return group == "idle" || group.rfind("idle", 0) == 0;
    }

    bool isBaseIdleAnimGroup(const std::string& group)
    {
        return group == "idle" || group == "idleswim" || group == "idlesneak";
    }

    bool isReliablePresentationAnimGroup(const std::string& group)
    {
        return !group.empty() && !isLocomotionAnimGroup(group) && !isBaseIdleAnimGroup(group);
    }

    mwmp::ActorPresentationSnapshot makePresentationSnapshot(const mwmp::BaseActor& actor, mwmp::ActorInstanceId actorNetId)
    {
        const bool axisLocomotion = std::abs(actor.animFlags.animFwd) > 0.1f
            || std::abs(actor.animFlags.animSide) > 0.1f;
        const float velX = actor.velocity.linear[0];
        const float velY = actor.velocity.linear[1];
        const float speedSq = velX * velX + velY * velY;
        const bool velocityLocomotion = speedSq > 20.f * 20.f;
        const bool hasLocomotionInput = !actor.isDead
            && actor.isMoving
            && (axisLocomotion || velocityLocomotion);

        float animFwd = hasLocomotionInput ? actor.animFlags.animFwd : 0.f;
        float animSide = hasLocomotionInput ? actor.animFlags.animSide : 0.f;
        if (hasLocomotionInput && !axisLocomotion && velocityLocomotion)
        {
            // NPC AI often moves the actor by transform velocity without leaving
            // useful movement axes behind.  Treat that as ordinary forward
            // locomotion instead of deriving signed strafe/backpedal axes from
            // world velocity, which proved too noisy near cell borders.
            animFwd = 1.f;
            animSide = 0.f;
        }

        mwmp::ActorPresentationSnapshot snapshot;
        snapshot.actorNetId = actorNetId;
        snapshot.isMoving = hasLocomotionInput;
        snapshot.isAttackingOrCasting = actor.isAttackingOrCasting;
        snapshot.hasWeaponDrawn = actor.hasWeaponDrawn;
        snapshot.hasSpellReadied = actor.hasSpellReadied;
        snapshot.isDead = actor.isDead;
        snapshot.movementFlags = static_cast<uint16_t>(actor.animFlags.movementFlags);
        snapshot.animFwd = hasLocomotionInput ? mwmp::quantizeActorAxis(animFwd) : 0;
        snapshot.animSide = hasLocomotionInput ? mwmp::quantizeActorAxis(animSide) : 0;
        mwmp::BaseActor presentationActor = actor;
        presentationActor.isMoving = hasLocomotionInput;
        snapshot.presentationFlags = mwmp::makeActorPresentationFlags(presentationActor);
        snapshot.currentAnimGroup = actor.animFlags.currentAnimGroup;
        snapshot.currentAnimCompletion = actor.animFlags.currentAnimCompletion;
        if ((actor.isDead || !hasLocomotionInput) && isLocomotionAnimGroup(snapshot.currentAnimGroup))
        {
            snapshot.currentAnimGroup.clear();
            snapshot.currentAnimCompletion = -1.f;
        }
        return snapshot;
    }

    uint64_t currentServerTimeMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
}

namespace mwmp
{

MPServer* MPServer::sInstance = nullptr;

// ---------------------------------------------------------------------------
double MPServer::getUptime() const
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now() - mStartTime).count();
}

// ---------------------------------------------------------------------------
void MPServer::broadcastServerMessage(const std::string& text)
{
    PacketChatMessage pkt;
    BasePlayer serverPlayer;
    serverPlayer.guid = 0;
    serverPlayer.name = "Server";
    pkt.setPlayer(&serverPlayer);
    pkt.message = text;
    pkt.channel = "";
    broadcastToAll(pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::broadcastNameColorMessage(const std::string& text)
{
    PacketChatMessage pkt;
    BasePlayer serverPlayer;
    serverPlayer.guid = 0;
    serverPlayer.name.clear();
    pkt.setPlayer(&serverPlayer);
    pkt.message = text;
    pkt.channel = "nameColor";
    broadcastToAll(pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::broadcastServerMessageToCell(const std::string& cellId, const std::string& text)
{
    PacketChatMessage pkt;
    BasePlayer serverPlayer;
    serverPlayer.guid = 0;
    serverPlayer.name = "Server";
    pkt.setPlayer(&serverPlayer);
    pkt.message = text;
    pkt.channel = "";
    broadcastToCell(cellId, pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::broadcastGameSettingsToCell(const std::string& cellId)
{
    if (cellId.empty())
        return;

    for (auto& [conn, client] : mClients)
    {
        if (!client.handshakeComplete || !cellMatches(client.player.cell, cellId))
            continue;
        sendGameSettingsToClient(conn, cellId);
    }
}

void MPServer::broadcastGameSettingsToAllPlayers()
{
    for (auto& [conn, client] : mClients)
    {
        if (!client.handshakeComplete)
            continue;

        sendGameSettingsToClient(conn, makeCellKey(client.player.cell));
    }
}

void MPServer::sendGameSettingsToPlayer(uint32_t guid)
{
    for (auto& [conn, client] : mClients)
    {
        if (!client.handshakeComplete || client.guid != guid)
            continue;

        sendGameSettingsToClient(conn, makeCellKey(client.player.cell));
        return;
    }
}

bool MPServer::teleportPlayer(uint32_t guid, const std::string& cellId, const Position& position)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->handshakeComplete || !client->charSelectComplete)
        return false;

    auto parsedCell = parseCellKey(cellId);
    if (!parsedCell)
        return false;

    const std::string oldCell = makeCellKey(client->player.cell);
    const std::unordered_set<std::string> oldActorInterestCells = actorInterestCellsForClient(*client);
    client->player.cell = *parsedCell;
    client->player.position = position;
    client->player.position.isTeleporting = true;
    client->player.velocity = {};
    // The timestamp belongs to the sending client's steady clock. Server-authored
    // discontinuities start a new interpolation timeline and must not reuse a
    // timestamp copied from another occupant (for example, a vehicle driver).
    client->player.positionSampleTimeUs = 0;
    client->pendingScriptedTeleportAck = true;
    client->scriptedTeleportTarget = client->player.position;
    client->scriptedTeleportGuardUntilMs = currentServerTimeMs() + 3000;
    client->lastScriptedTeleportRejectLogMs = 0;
    client->loadedActorCells.clear();
    client->loadedActorCellsSequence = 0;

    const std::string newCell = makeCellKey(client->player.cell);
    syncLuaPlayerSnapshot();

    for (const std::string& oldActorCellId : oldActorInterestCells)
    {
        if (oldActorCellId != newCell)
            refreshActorAuthorityForCell(oldActorCellId);
    }
    if (!newCell.empty())
        refreshActorAuthorityForCell(newCell, client->guid);

    if (oldCell != newCell)
    {
        Log(Debug::Info) << "[Server] Teleport " << client->name << " -> cell: " << newCell;
        mLua.onPlayerCellChange(client->guid, client->name, newCell, oldCell);

        PacketPlayerCellChange cellChange;
        cellChange.setPlayer(&client->player);
        const auto encoded = cellChange.encode();
        sendTo(client->conn, encoded);
        broadcastToAll(encoded, client->conn);
        {
            PacketPlayerPosition positionPacket;
            positionPacket.setPlayer(&client->player);
            const auto positionEncoded = positionPacket.encode();
            sendTo(client->conn, positionEncoded);
            broadcastToAll(positionEncoded, client->conn);
        }

        if (!newCell.empty())
            sendCellStateToClient(client->conn, newCell);
        sendPlayerStateBootstrapToClient(*client);
    }
    else
    {
        PacketPlayerPosition pkt;
        pkt.setPlayer(&client->player);
        const auto encoded = pkt.encode();
        sendTo(client->conn, encoded);
        broadcastToAll(encoded, client->conn);
    }

    client->player.position.isTeleporting = false;
    return true;
}

bool MPServer::upsertPlayerMark(uint32_t guid, const PlayerMark& mark)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !mPlayerDb || client->dbCharacterId == 0 || mark.name.empty() || mark.cell.empty())
        return false;

    mPlayerDb->upsertCharacterMark(client->dbCharacterId, mark);
    return true;
}

bool MPServer::deletePlayerMark(uint32_t guid, std::string_view name)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !mPlayerDb || client->dbCharacterId == 0 || name.empty())
        return false;

    mPlayerDb->deleteCharacterMark(client->dbCharacterId, name);
    return true;
}

bool MPServer::mutatePlayerBounty(uint32_t guid, CrimeMutationKind kind, std::int64_t value,
    const std::string& requestId, const std::string& source)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->charSelectComplete || !mPlayerDb
        || client->dbAccountId <= 0 || client->dbCharacterId <= 0)
        return false;

    CrimeMutationRequest request;
    request.requestId = requestId;
    request.kind = kind;
    request.value = value;
    request.source = source;

    try
    {
        const PlayerCrimeState previousState = client->player.crimeState;
        CrimeService service(*mPlayerDb);
        CrimeService::Context context;
        context.accountId = client->dbAccountId;
        context.characterId = client->dbCharacterId;
        const CrimeService::Outcome outcome = service.execute(request, context);

        // A durable duplicate result describes the original transition and may
        // predate later commits. Always mirror the current authoritative row.
        client->player.crimeState = mPlayerDb->loadPlayerCrimeState(client->dbCharacterId);
        client->player.bounty = client->player.crimeState.bounty;
        sendAuthoritativeCrimeState(*client);
        syncLuaPlayerSnapshot();

        Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
            << "[CrimeService] request=" << requestId
            << " player=" << client->slotName
            << " source=" << source
            << " accepted=" << outcome.result.accepted
            << " replayed=" << outcome.replayed
            << " error=" << getCrimeErrorCode(outcome.result.error)
            << " bounty=" << previousState.bounty << "->" << client->player.crimeState.bounty
            << " revision=" << previousState.revision << "->" << client->player.crimeState.revision;
        return outcome.result.accepted;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[CrimeService] persistence failure request=" << requestId
                          << " player=" << client->slotName
                          << " error=" << e.what();
        return false;
    }
}

bool MPServer::mutatePlayerFaction(uint32_t guid, FactionMutationKind kind, const std::string& factionId,
    std::int64_t value, const std::string& requestId, const std::string& source)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->charSelectComplete || !mPlayerDb
        || client->dbAccountId <= 0 || client->dbCharacterId <= 0)
        return false;

    FactionMutationRequest request;
    request.requestId = requestId;
    request.mutations.push_back({ kind, factionId, value });
    request.source = source;

    try
    {
        FactionService service(*mPlayerDb);
        FactionService::Context context;
        context.accountId = client->dbAccountId;
        context.characterId = client->dbCharacterId;
        context.findFaction = [this](std::string_view id)
            -> std::optional<FactionService::FactionDefinition> {
            const ESM::Faction* faction = mContentRegistry->store().get<ESM::Faction>().search(
                ESM::RefId::stringRefId(id));
            if (!faction)
                return std::nullopt;
            FactionService::FactionDefinition definition;
            definition.validRanks.resize(faction->mRanks.size());
            for (std::size_t index = 0; index < faction->mRanks.size(); ++index)
                definition.validRanks[index] = index == 0 || !faction->mRanks[index].empty();
            return definition;
        };
        const FactionService::Outcome outcome = service.execute(std::move(request), context);
        client->player.factionState = mPlayerDb->loadPlayerFactionState(client->dbCharacterId);
        sendAuthoritativeFactionState(*client, outcome.result.requestId,
            outcome.result.accepted, outcome.result.error);
        syncLuaPlayerSnapshot();
        Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
            << "[FactionService] trusted request=" << requestId
            << " player=" << client->slotName
            << " source=" << source
            << " accepted=" << outcome.result.accepted
            << " replayed=" << outcome.replayed
            << " error=" << getFactionErrorCode(outcome.result.error)
            << " revision=" << client->player.factionState.revision;
        return outcome.result.accepted;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[FactionService] trusted persistence failure request=" << requestId
                          << " player=" << client->slotName << " error=" << e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
void MPServer::sendServerMessage(uint32_t guid, const std::string& text)
{
    for (auto& [conn, client] : mClients)
    {
        if (client.guid == guid && client.handshakeComplete)
        {
            PacketChatMessage pkt;
            BasePlayer serverPlayer;
            serverPlayer.guid = 0;
            serverPlayer.name = "Server";
            pkt.setPlayer(&serverPlayer);
            pkt.message = text;
            pkt.channel = "";
            sendTo(conn, pkt.encode());
            return;
        }
    }
}

// ---------------------------------------------------------------------------
void MPServer::relayPlayerChat(uint32_t guid, const std::string& text)
{
    ConnectedClient* c = findClientByGuid(guid);
    if (!c || !c->handshakeComplete)
        return;

    PacketChatMessage pkt;
    c->player.name = c->name;
    pkt.setPlayer(&c->player);
    pkt.message = text;
    pkt.channel = "";
    broadcastToAll(pkt.encode());
}

// ---------------------------------------------------------------------------
bool MPServer::playSpeech(uint32_t guid, const std::string& soundPath)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->handshakeComplete || !client->charSelectComplete || soundPath.empty())
        return false;

    client->player.speechSound = soundPath;

    PacketPlayerSpeech pkt;
    pkt.setPlayer(&client->player);
    const auto encoded = pkt.encode();

    const std::string cellId = makeCellKey(client->player.cell);
    if (!cellId.empty())
        broadcastToCell(cellId, encoded);
    else
        sendTo(client->conn, encoded);

    client->player.speechSound.clear();
    return true;
}

// ---------------------------------------------------------------------------
bool MPServer::suspendPlacedVehicleObject(uint32_t mpNum, PlacedObject& object)
{
    if (mpNum == 0)
        return false;

    for (auto cellIt = mWorld.placedObjects.begin(); cellIt != mWorld.placedObjects.end(); ++cellIt)
    {
        auto& objects = cellIt->second;
        const auto objectIt = std::find_if(objects.begin(), objects.end(),
            [&](const PlacedObject& candidate) { return candidate.mpNum == mpNum; });
        if (objectIt == objects.end())
            continue;

        object = *objectIt;
        const std::string cellId = cellIt->first;
        mLua.removePlacedObject(mpNum);
        objects.erase(objectIt);
        if (objects.empty())
            mWorld.placedObjects.erase(cellIt);

        // Keep the database row while the vehicle is active. If the server
        // stops unexpectedly, the parked object reappears at its last durable
        // location instead of being lost permanently.
        PacketObjectDelete packet;
        packet.mpNum = mpNum;
        packet.cellId = cellId;
        broadcastToCell(cellId, packet.encode());
        return true;
    }

    return false;
}

bool MPServer::restoreActiveVehicleObject(ConnectedClient& client)
{
    const auto activeIt = mActiveVehiclesByDriver.find(client.guid);
    if (activeIt == mActiveVehiclesByDriver.end())
        return true;

    const std::string cellId = makeCellKey(client.player.cell);
    if (cellId.empty())
        return false;

    PlacedObject object = activeIt->second.parkedObject;
    object.cellId = cellId;
    object.position = client.player.position;
    object.position.isTeleporting = false;

    if (worldMpNumInUse(object.mpNum))
    {
        Log(Debug::Warning) << "[Server] Cannot restore active vehicle because mpNum is already in use"
                            << " player=" << client.name
                            << " mpNum=" << object.mpNum;
        return false;
    }

    mWorld.placedObjects[object.cellId].push_back(object);
    mLua.upsertPlacedObject(object);
    if (mPlayerDb)
        mPlayerDb->upsertWorldObject(object);

    PacketObjectPlace packet;
    packet.object = object;
    broadcastToCell(object.cellId, packet.encode());

    Log(Debug::Info) << "[Server] Restored parked vehicle"
                     << " player=" << client.name
                     << " profile='" << activeIt->second.profileId << "'"
                     << " mpNum=" << object.mpNum
                     << " cell=" << object.cellId
                     << " pos=(" << object.position.pos[0] << ","
                     << object.position.pos[1] << "," << object.position.pos[2] << ")";
    mActiveVehiclesByDriver.erase(activeIt);
    return true;
}

void MPServer::releaseVehiclePassengers(ConnectedClient& driver)
{
    std::vector<uint32_t> passengerGuids;
    for (const auto& [connection, client] : mClients)
    {
        if (client.player.vehicle.active
            && client.player.vehicle.occupantRole == VehicleOccupantRole::Passenger
            && client.player.vehicle.driverGuid == driver.guid)
        {
            passengerGuids.push_back(client.guid);
        }
    }

    const std::string cellId = makeCellKey(driver.player.cell);
    for (uint32_t passengerGuid : passengerGuids)
    {
        ConnectedClient* passenger = findClientByGuid(passengerGuid);
        if (!passenger)
            continue;

        Position exitPosition = driver.player.position;
        if (const VehicleProfile* profile = findVehicleProfile(passenger->player.vehicle.profileId))
        {
            const uint8_t seatIndex = passenger->player.vehicle.seatIndex;
            if (seatIndex < profile->seatCount && seatIndex < profile->seats.size())
            {
                const auto& offset = profile->seats[seatIndex].exitOffset;
                const float yaw = exitPosition.rot[2];
                exitPosition.pos[0] += std::cos(yaw) * offset[0] + std::sin(yaw) * offset[1];
                exitPosition.pos[1] += -std::sin(yaw) * offset[0] + std::cos(yaw) * offset[1];
                exitPosition.pos[2] += offset[2];
            }
        }
        exitPosition.rot[0] = 0.f;
        exitPosition.rot[1] = 0.f;

        setPlayerVehicleState(passengerGuid, false, std::string(), 0);
        if (!cellId.empty())
            teleportPlayer(passengerGuid, cellId, exitPosition);
    }
}

// ---------------------------------------------------------------------------
bool MPServer::setPlayerVehicleState(
    uint32_t guid, bool active, const std::string& profileId, uint32_t parkedObjectMpNum,
    VehicleOccupantRole occupantRole, uint32_t driverGuid, uint8_t seatIndex)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->handshakeComplete || !client->charSelectComplete)
        return false;

    if (active && !findVehicleProfile(profileId))
    {
        Log(Debug::Warning) << "[Server] Rejected unknown vehicle profile '" << profileId
                            << "' for player " << client->name;
        return false;
    }

    if (active && occupantRole == VehicleOccupantRole::None)
        return false;

    if (active && occupantRole == VehicleOccupantRole::Driver)
    {
        driverGuid = guid;
        seatIndex = 0;
    }
    else if (active && (driverGuid == 0 || driverGuid == guid))
        return false;

    BasePlayer::VehicleState& state = client->player.vehicle;
    const VehicleOccupantRole previousRole = state.occupantRole;
    if (!active && state.active && previousRole == VehicleOccupantRole::Driver)
        releaseVehiclePassengers(*client);

    const std::string nextProfileId = active ? profileId : std::string();
    const uint32_t nextParkedObjectMpNum = active ? parkedObjectMpNum : 0;
    const VehicleOccupantRole nextRole = active ? occupantRole : VehicleOccupantRole::None;
    const uint32_t nextDriverGuid = active ? driverGuid : 0;
    const uint8_t nextSeatIndex = active ? seatIndex : 0;
    if (state.active == active && state.profileId == nextProfileId
        && state.parkedObjectMpNum == nextParkedObjectMpNum
        && state.occupantRole == nextRole && state.driverGuid == nextDriverGuid
        && state.seatIndex == nextSeatIndex)
    {
        return active || restoreActiveVehicleObject(*client);
    }

    state.active = active;
    state.profileId = nextProfileId;
    state.parkedObjectMpNum = nextParkedObjectMpNum;
    state.occupantRole = nextRole;
    state.driverGuid = nextDriverGuid;
    state.seatIndex = nextSeatIndex;
    ++state.revision;
    if (state.revision == 0)
        ++state.revision;

    PacketPlayerVehicleState packet;
    packet.setPlayer(&client->player);
    broadcastToAll(packet.encode());

    Log(Debug::Info) << "[Server] Player vehicle state"
                     << " player=" << client->name
                     << " active=" << state.active
                     << " profile='" << state.profileId << "'"
                     << " parkedMpNum=" << state.parkedObjectMpNum
                     << " role=" << static_cast<int>(state.occupantRole)
                     << " driverGuid=" << state.driverGuid
                     << " seat=" << static_cast<int>(state.seatIndex)
                     << " revision=" << state.revision;
    const bool restored = active || previousRole != VehicleOccupantRole::Driver
        || restoreActiveVehicleObject(*client);
    syncLuaPlayerSnapshot();
    return restored;
}

// ---------------------------------------------------------------------------
bool MPServer::killPlayer(uint32_t guid, const std::string& deathMessage)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->handshakeComplete || !client->charSelectComplete || client->player.isDead)
        return false;

    client->player.isDead = true;
    client->player.deathAnimationGroup = "death1";
    client->player.dynamicStats.health.current = 0.f;

    // A dead player cannot continue owning an active native vehicle body. Clear
    // the authoritative state before broadcasting death so every client removes
    // the rigid body and seated presentation before respawn.
    setPlayerVehicleState(guid, false, std::string(), 0);

    PacketPlayerDeath pkt;
    pkt.setPlayer(&client->player);
    const auto encoded = pkt.encode();
    sendTo(client->conn, encoded);
    broadcastToAll(encoded, client->conn);

    Log(Debug::Info) << "[Server] Killed player by script: " << client->name;
    if (!deathMessage.empty() && mAnnouncePlayerDeaths)
        broadcastNameColorMessage(client->name + " " + deathMessage);
    else
        announcePlayerDeath(*client, pkt);
    syncLuaPlayerSnapshot();
    return true;
}

// ---------------------------------------------------------------------------
void MPServer::broadcastLuaEvent(uint32_t pid, const std::string& eventName, const std::string& eventData)
{
    PacketLuaEvent pkt;
    pkt.pid = pid;
    pkt.eventName = eventName;
    pkt.eventData = eventData;
    broadcastToAll(pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::broadcastLuaEventToCell(
    const std::string& cellId, uint32_t pid, const std::string& eventName, const std::string& eventData)
{
    PacketLuaEvent pkt;
    pkt.pid = pid;
    pkt.eventName = eventName;
    pkt.eventData = eventData;
    broadcastToCell(cellId, pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::sendLuaEvent(
    uint32_t guid, uint32_t pid, const std::string& eventName, const std::string& eventData)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->handshakeComplete)
        return;

    PacketLuaEvent pkt;
    pkt.pid = pid;
    pkt.eventName = eventName;
    pkt.eventData = eventData;
    sendTo(client->conn, pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::broadcastLuaStorage(
    LuaStorageAction action, const std::string& section, const std::vector<LuaStorageEntry>& entries)
{
    PacketLuaStorage pkt;
    pkt.action = action;
    pkt.section = section;
    pkt.entries = entries;
    broadcastToAll(pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::sendLuaStorage(uint32_t guid, LuaStorageAction action,
    const std::string& section, const std::vector<LuaStorageEntry>& entries)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || !client->charSelectComplete)
        return;

    PacketLuaStorage pkt;
    pkt.action = action;
    pkt.section = section;
    pkt.entries = entries;
    sendTo(client->conn, pkt.encode());
}

// ---------------------------------------------------------------------------
MPServer::MPServer(uint16_t port) : mPort(port)
{
    if (sInstance)
        throw std::runtime_error("MPServer: only one instance allowed");

    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg))
        throw std::runtime_error(std::string("GNS init failed: ") + errMsg);

    mInterface = SteamNetworkingSockets();
    if (!mInterface)
        throw std::runtime_error("MPServer: failed to get ISteamNetworkingSockets");

    sInstance  = this;
    mStartTime = std::chrono::steady_clock::now();
    Log(Debug::Info) << "[Server] Initialised";
}

// ---------------------------------------------------------------------------
MPServer::~MPServer()
{
    shutdown();
    GameNetworkingSockets_Kill();
    sInstance = nullptr;
}

// ---------------------------------------------------------------------------
void MPServer::run()
{
    if (mContentRegistryConfig.openmwConfig.empty())
        throw std::runtime_error(
            "Authoritative content is not configured; set [content] openmw_cfg in server.cfg");
    mContentRegistry = std::make_unique<ServerContentRegistry>(mContentRegistryConfig);
    if (const ESM::GameSetting* setting = mContentRegistry->store().get<ESM::GameSetting>().search(
            ESM::RefId::stringRefId("fAlarmRadius")))
    {
        const float configuredRadius = setting->mValue.getFloat();
        if (std::isfinite(configuredRadius) && configuredRadius > 0.f)
            mObservationAlarmRadius = configuredRadius;
    }
    if (const ESM::GameSetting* setting = mContentRegistry->store().get<ESM::GameSetting>().search(
            ESM::RefId::stringRefId("iMaxActivateDist")))
    {
        const int configuredDistance = setting->mValue.getInteger();
        if (configuredDistance > 0)
            mDoorInteractionRadius = std::max(512.f, static_cast<float>(configuredDistance));
    }
    mCollisionWorld = std::make_unique<ServerCollisionWorld>(*mContentRegistry);
    auto gameSettingFloat = [&](std::string_view id, float fallback) {
        const ESM::GameSetting* setting = mContentRegistry->store().get<ESM::GameSetting>().search(
            ESM::RefId::stringRefId(id));
        return setting ? setting->mValue.getFloat() : fallback;
    };
    AwarenessSettings awarenessSettings;
    awarenessSettings.sneakSkillMultiplier = gameSettingFloat("fSneakSkillMult", 1.f);
    awarenessSettings.sneakBootMultiplier = gameSettingFloat("fSneakBootMult", 1.f);
    awarenessSettings.sneakDistanceBase = gameSettingFloat("fSneakDistanceBase", 1.f);
    awarenessSettings.sneakDistanceMultiplier = gameSettingFloat("fSneakDistanceMultiplier", 0.f);
    awarenessSettings.sneakNoViewMultiplier = gameSettingFloat("fSneakNoViewMult", 1.f);
    awarenessSettings.sneakViewMultiplier = gameSettingFloat("fSneakViewMult", 1.f);
    awarenessSettings.fatigueBase = gameSettingFloat("fFatigueBase", 1.f);
    awarenessSettings.fatigueMultiplier = gameSettingFloat("fFatigueMult", 0.f);
    mObservationRollSource = std::make_unique<ServerAwarenessRollSource>();
    mObservationService = std::make_unique<ObservationService>(
        awarenessSettings, *mCollisionWorld, *mObservationRollSource);
    mServerLuaPackageRegistry
        = std::make_unique<ServerLuaPackageRegistry>(mServerLuaPackageRoot, Version::getLuaApiRevision());

    // Create listen socket
    SteamNetworkingIPAddr listenAddr;
    listenAddr.Clear();
    listenAddr.m_port = mPort;

    SteamNetworkingConfigValue_t opts[2];
    opts[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
               reinterpret_cast<void*>(&staticConnectionStatusChanged));
    // Allow connections without Steam certificate authentication (no Steam backend in dev)
    opts[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);

    mListenSocket = mInterface->CreateListenSocketIP(listenAddr, 2, opts);
    if (mListenSocket == k_HSteamListenSocket_Invalid)
        throw std::runtime_error("MPServer: CreateListenSocketIP failed");

    mPollGroup = mInterface->CreatePollGroup();
    if (mPollGroup == k_HSteamNetPollGroup_Invalid)
        throw std::runtime_error("MPServer: CreatePollGroup failed");

    Log(Debug::Info) << "[Server] Listening on port " << mPort;

    mGeneratedRecordIdPrefix = mLua.getString("Config", "GENERATED_RECORD_ID_PREFIX", "$custom");
    if (mGeneratedRecordIdPrefix.empty())
        mGeneratedRecordIdPrefix = "$custom";

    // Open player database.
    try
    {
        mPlayerDb.emplace(mDbPath);
        loadPersistentWorldState();
        syncLuaAuthorityState();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] PlayerDatabase failed to open: " << e.what();
        // Non-fatal - server runs without persistence if DB unavailable.
    }

    mLua.syncGeneratedRecordState(
        mGeneratedRecordIdPrefix, buildGeneratedDynamicRecordCounters(mGeneratedRecordIdPrefix));

    auto normalizeConfiguredCell = [](std::string raw) {
        // Normalise "x, y" coords: strip spaces that follow a comma so
        // findExteriorPosition / std::from_chars can parse the string.
        std::string norm;
        norm.reserve(raw.size());
        bool afterComma = false;
        for (char c : raw)
        {
            if (c == ',')
            {
                norm += c;
                afterComma = true;
            }
            else if (c == ' ' && afterComma)
            {
                // drop
            }
            else
            {
                norm += c;
                afterComma = false;
            }
        }
        return norm;
    };

    // If config.lua set Config.SPAWN_CELL, let it override the C++ default.
    // Config.DEFAULT_SPAWN can additionally provide a full position/rotation.
    {
        std::string raw = mLua.getString("Config", "SPAWN_CELL", "");
        if (!raw.empty())
        {
            mDefaultSpawnCell = normalizeConfiguredCell(std::move(raw));
            Log(Debug::Info) << "[Server] Spawn cell set from config.lua: " << mDefaultSpawnCell;
        }

        if (auto defaultSpawn = mLua.getConfigPlayerMark("DEFAULT_SPAWN"))
        {
            mDefaultSpawnCell = normalizeConfiguredCell(defaultSpawn->cell);
            mDefaultSpawnPosition = defaultSpawn->position;
            mHasDefaultSpawnPosition = true;
            Log(Debug::Info) << "[Server] Default spawn position set from config.lua: " << mDefaultSpawnCell
                             << " (" << mDefaultSpawnPosition.pos[0]
                             << ", " << mDefaultSpawnPosition.pos[1]
                             << ", " << mDefaultSpawnPosition.pos[2] << ")";
        }

        mDefaultPlayerMarks = mLua.getConfigPlayerMarks("DEFAULT_PLAYER_MARKS");
        for (auto& mark : mDefaultPlayerMarks)
            mark.cell = normalizeConfiguredCell(std::move(mark.cell));
        if (!mDefaultPlayerMarks.empty())
            Log(Debug::Info) << "[Server] Default new-character marks loaded: " << mDefaultPlayerMarks.size();
    }

    // Read Config.MAX_CHARS_PER_ACCOUNT from config.lua (0 = unlimited).
    mMaxCharsPerAccount = mLua.getInt("Config", "MAX_CHARS_PER_ACCOUNT", mMaxCharsPerAccount);
    mAnnouncePlayerDeaths = mLua.getBool("Config", "ANNOUNCE_PLAYER_DEATHS", true);
    mGuardArrestDialogueEnabled
        = lowerAscii(mLua.getString("Config", "GUARD_ARREST_MODE", "combat")) == "dialogue";
    Log(Debug::Info) << "[Server] Max chars per account: "
                     << (mMaxCharsPerAccount == 0 ? "unlimited" : std::to_string(mMaxCharsPerAccount));
    Log(Debug::Info) << "[Server] Guard arrest mode: "
                     << (mGuardArrestDialogueEnabled ? "dialogue" : "combat");

    mModChecksEnabled = mLua.getBool("Config", "MOD_CHECKS_ENABLED", false);
    mModChecksStrictOrder = mLua.getBool("Config", "MOD_CHECKS_STRICT_ORDER", false);
    mModChecksRequireExactList = mLua.getBool("Config", "MOD_CHECKS_REQUIRE_EXACT_LIST", false);
    mModChecksHelpUrl = mLua.getString("Config", "MOD_CHECKS_HELP_URL", "");
    mRequiredContentFiles = mLua.getConfigContentFileRules("REQUIRED_CONTENT_FILES");
    mRequiredLuaScripts = mLua.getConfigContentFileRules("REQUIRED_LUA_SCRIPTS");
    const std::string configuredResolvedFingerprint
        = lowerAscii(mLua.getString("Config", "RESOLVED_CONTENT_FINGERPRINT", ""));
    mResolvedContentFingerprint = lowerAscii(mContentRegistry->resolvedFingerprint());
    if (!configuredResolvedFingerprint.empty()
        && configuredResolvedFingerprint != mResolvedContentFingerprint)
    {
        throw std::runtime_error("Config.RESOLVED_CONTENT_FINGERPRINT does not match the authoritative "
            "headless content result (configured=" + configuredResolvedFingerprint
            + ", computed=" + mResolvedContentFingerprint + ")");
    }
    mRuntimeRecordContentIds.clear();
    for (const std::string& id : mLua.getConfigStringList("RUNTIME_RECORD_CONTENT_IDS"))
        mRuntimeRecordContentIds.insert(lowerAscii(id));
    mRuntimeRecordAssets.clear();
    for (const std::string& asset : mLua.getConfigStringList("RUNTIME_RECORD_ASSETS"))
        mRuntimeRecordAssets.insert(normalizeRuntimeAsset(asset));
    mRuntimeRecordRequestsPerMinute = static_cast<std::size_t>(
        std::max(0, mLua.getInt("Config", "RUNTIME_RECORD_REQUESTS_PER_MINUTE", 30)));
    mRuntimeRecordMaxPerCharacter = static_cast<std::size_t>(
        std::max(0, mLua.getInt("Config", "RUNTIME_RECORD_MAX_PER_CHARACTER", 2048)));
    mEnchantProjectilesMultiplier = static_cast<float>(
        std::max(0, mLua.getInt("Config", "ENCHANTING_PROJECTILES_MULTIPLIER", 0)));
    mRuntimeRecordCapabilities.clear();
    for (const RuntimeRecordCapability& capability
        : mLua.getConfigRuntimeRecordCapabilities("RUNTIME_RECORD_CAPABILITIES"))
    {
        auto& permitted = mRuntimeRecordCapabilities[lowerAscii(capability.packageId)];
        for (const std::string& typeName : capability.recordTypes)
        {
            if (const auto type = parseRuntimeRecordType(typeName))
                permitted.insert(*type);
            else
                Log(Debug::Warning) << "[Server] Ignored unsupported runtime-record capability type='"
                                    << typeName << "' package='" << capability.packageId << "'";
        }
        if (permitted.empty())
            mRuntimeRecordCapabilities.erase(lowerAscii(capability.packageId));
    }
    Log(Debug::Info) << "[Server] Runtime record client capabilities: packages="
                     << mRuntimeRecordCapabilities.size() << " (default deny)";
    if (!mRuntimeRecordContentIds.empty() || !mRuntimeRecordAssets.empty())
        Log(Debug::Warning) << "[Server] RUNTIME_RECORD_CONTENT_IDS and RUNTIME_RECORD_ASSETS are deprecated; "
                              "runtime references are validated against the authoritative content registry";

    const std::string journalSharing = lowerAscii(mLua.getString("Config", "JOURNAL_SHARING", "player"));
    if (journalSharing == "server")
        mJournalSharingMode = JournalSharingMode::Server;
    else if (journalSharing == "group")
        mJournalSharingMode = JournalSharingMode::Group;
    else
    {
        mJournalSharingMode = JournalSharingMode::Player;
        if (journalSharing != "player")
            Log(Debug::Warning) << "[Server] Invalid Config.JOURNAL_SHARING='" << journalSharing
                                << "'; using player";
    }
    mJournalSharingGroups = mLua.getConfigJournalGroups("JOURNAL_GROUPS");
    std::sort(mJournalSharingGroups.begin(), mJournalSharingGroups.end(),
        [](const JournalSharingGroup& left, const JournalSharingGroup& right) { return left.name < right.name; });
    Log(Debug::Info) << "[Server] Journal sharing mode=" << journalSharing
                     << " groups=" << mJournalSharingGroups.size();
    Log(Debug::Info) << "[Server] Content-file checks: "
                     << (mModChecksEnabled ? "enabled" : "disabled")
                     << " required=" << mRequiredContentFiles.size()
                     << " luaScripts=" << mRequiredLuaScripts.size()
                     << " strictOrder=" << (mModChecksStrictOrder ? "true" : "false")
                     << " exactList=" << (mModChecksRequireExactList ? "true" : "false");
    if (mModChecksEnabled && mRequiredContentFiles.empty())
        Log(Debug::Warning) << "[Server] Content-file checks are enabled but REQUIRED_CONTENT_FILES is empty";

    mAdminHttpEnabled = mLua.getBool("Config", "ADMIN_HTTP_ENABLED", true);
    mAdminHttpHost = mLua.getString("Config", "ADMIN_HTTP_HOST", "127.0.0.1");
    mAdminHttpPort = std::max(1, mLua.getInt("Config", "ADMIN_HTTP_PORT", 8081));
    mAdminHttpTimeoutMs = std::max(1, mLua.getInt("Config", "ADMIN_HTTP_TIMEOUT_MS", 250));
    mObservationDiagnosticsEnabled = mLua.getBool("Config", "OBSERVATION_DIAGNOSTICS_ENABLED", false);

    std::string normalizedAdminHttpHost = mAdminHttpHost;
    std::transform(normalizedAdminHttpHost.begin(), normalizedAdminHttpHost.end(), normalizedAdminHttpHost.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalizedAdminHttpHost != "127.0.0.1" && normalizedAdminHttpHost != "localhost")
    {
        Log(Debug::Warning) << "[Server] ADMIN_HTTP_HOST must stay loopback-only; forcing 127.0.0.1 instead of "
                            << mAdminHttpHost;
        mAdminHttpHost = "127.0.0.1";
    }

    rebuildLuaActorSnapshot();
    syncLuaPlayerSnapshot();
    mLua.start();
    mLua.onServerInit();
    startAdminHttpServer();

    // Register with the master server (async - does not block the tick loop).
    if (!mMasterUrl.empty())
    {
        MasterServerClient::Config cfg;
        cfg.masterUrl         = mMasterUrl;
        cfg.serverName        = mServerName;
        cfg.port              = mPort;
        cfg.maxPlayers        = mMaxPlayersConfig;
        cfg.buildVersion      = std::string(MultiplayerBuildVersion);
        cfg.protocolVersion   = MultiplayerProtocolVersion;
        cfg.gameMode          = mGameMode;
        cfg.lanAddress        = mLanAddress;
        mMasterClient.registerAsync(cfg);
    }

    mRunning = true;
    using Clock = std::chrono::steady_clock;
    auto last   = Clock::now();
    mLoopDiagnostics = {};
    mLoopDiagnostics.windowStart = last;

    while (mRunning)
    {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        mInterface->RunCallbacks();
        processIncomingMessages();
        {
            std::vector<std::pair<std::uint32_t, std::string>> pendingDiagnostics;
            {
                std::lock_guard lock(mPendingAdminDiagnosticMutex);
                pendingDiagnostics.swap(mPendingAdminDiagnosticCommands);
            }
            for (const auto& [guid, message] : pendingDiagnostics)
            {
                if (ConnectedClient* client = findClientByGuid(guid))
                {
                    if (!handleObservationDiagnosticCommand(*client, message))
                        handleCrimeWitnessDiagnosticCommand(*client, message);
                }
                else
                    Log(Debug::Warning) << "[Server] Dropped queued admin diagnostic for missing guid=" << guid;
            }
        }
        tick(dt);
        mLua.drainOutbound();
        flushLuaActorChanges();
        syncLuaPlayerSnapshot();

        const auto loopEnd = Clock::now();
        const double loopMs = std::chrono::duration<double, std::milli>(loopEnd - now).count();
        ++mLoopDiagnostics.loops;
        mLoopDiagnostics.loopTotalMs += loopMs;
        mLoopDiagnostics.loopMaxMs = std::max(mLoopDiagnostics.loopMaxMs, loopMs);

        const double diagnosticWindowSeconds
            = std::chrono::duration<double>(loopEnd - mLoopDiagnostics.windowStart).count();
        if (diagnosticWindowSeconds >= 1.0)
        {
            const double loopAverageMs = mLoopDiagnostics.loops != 0
                ? mLoopDiagnostics.loopTotalMs / static_cast<double>(mLoopDiagnostics.loops) : 0.0;
            const double messagesPerPoll = mLoopDiagnostics.loops != 0
                ? static_cast<double>(mLoopDiagnostics.messages) / static_cast<double>(mLoopDiagnostics.loops) : 0.0;
            const double relayAverageMs = mLoopDiagnostics.playerPositionRelays != 0
                ? mLoopDiagnostics.playerPositionRelayTotalMs
                    / static_cast<double>(mLoopDiagnostics.playerPositionRelays)
                : 0.0;
            Log(Debug::Info) << "[MPDIAG] Server loop"
                             << " loopsHz=" << (mLoopDiagnostics.loops / diagnosticWindowSeconds)
                             << " loopAvgMs=" << loopAverageMs
                             << " loopMaxMs=" << mLoopDiagnostics.loopMaxMs
                             << " messages=" << mLoopDiagnostics.messages
                             << " messagesPerPoll=" << messagesPerPoll
                             << " messagesPerPollMax=" << mLoopDiagnostics.maxMessagesPerPoll
                             << " pollsAt512=" << mLoopDiagnostics.pollsAtReceiveLimit
                             << " slowHandlers8ms=" << mLoopDiagnostics.slowHandlers
                             << " playerPositionRelays=" << mLoopDiagnostics.playerPositionRelays
                             << " playerRelayAvgMs=" << relayAverageMs
                             << " playerRelayMaxMs=" << mLoopDiagnostics.playerPositionRelayMaxMs;
            mLoopDiagnostics = {};
            mLoopDiagnostics.windowStart = loopEnd;
        }

        // Keep the network relay responsive independently of simulation/Lua
        // cadence.  At 50 ms, busy actor cells let actor traffic accumulate in
        // front of player movement and turn a single receive pass into 100-250 ms
        // bursts.  tick() is elapsed-time based and Lua owns its own 60 Hz thread,
        // so polling at 5 ms reduces relay latency without speeding game time.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    shutdown();
}

// ---------------------------------------------------------------------------
void MPServer::shutdown()
{
    stopAdminHttpServer();
    mLua.stop();

    // Tell the master server we are gone immediately (synchronous, best-effort).
    mMasterClient.unregister();

    if (mListenSocket != k_HSteamListenSocket_Invalid)
    {
        // Close all client connections gracefully
        for (auto& [conn, client] : mClients)
            mInterface->CloseConnection(conn, 0, "Server shutdown", true);
        mClients.clear();

        mCollisionOwnership = {};
        if (mCollisionWorld)
            mCollisionWorld->clear();

        mInterface->CloseListenSocket(mListenSocket);
        mListenSocket = k_HSteamListenSocket_Invalid;
    }
    if (mPollGroup != k_HSteamNetPollGroup_Invalid)
    {
        mInterface->DestroyPollGroup(mPollGroup);
        mPollGroup = k_HSteamNetPollGroup_Invalid;
    }
    Log(Debug::Info) << "[Server] Shutdown complete";
}

// ---------------------------------------------------------------------------
void MPServer::tick(float dt)
{
    // Advance world time - carry over into day/month/year when hour wraps.
    // 30-day months, 12-month year (Morrowind calendar approximation).
    mWorld.gameHour += (dt * mWorld.timeScale) / 3600.f;
    while (mWorld.gameHour >= 24.f)
    {
        mWorld.gameHour -= 24.f;
        if (++mWorld.day > 30)
        {
            mWorld.day = 1;
            if (++mWorld.month > 11)
            {
                mWorld.month = 0;
                ++mWorld.year;
            }
        }
    }

    // Periodic world-time broadcast so connected clients stay in sync.
    mWorld.timeSyncTimer += dt;
    if (mWorld.timeSyncTimer >= WorldState::TIME_SYNC_RATE)
    {
        mWorld.timeSyncTimer = 0.f;
        if (!mClients.empty())
            broadcastToAll(buildWorldTimePacket());
    }

    // Send a heartbeat to the master server at most once every 30 seconds.
    mMasterClient.tickHeartbeat(dt, getPlayerCount());

    flushScheduledGeneratedDynamicRecordGc();
}

// ---------------------------------------------------------------------------
void MPServer::processIncomingMessages()
{
    static constexpr int MAX_MSGS = 512;
    ISteamNetworkingMessage* msgs[MAX_MSGS];

    int n = mInterface->ReceiveMessagesOnPollGroup(mPollGroup, msgs, MAX_MSGS);
    mLoopDiagnostics.messages += static_cast<std::size_t>(std::max(n, 0));
    mLoopDiagnostics.maxMessagesPerPoll
        = std::max(mLoopDiagnostics.maxMessagesPerPoll, static_cast<std::size_t>(std::max(n, 0)));
    if (n == MAX_MSGS)
        ++mLoopDiagnostics.pollsAtReceiveLimit;

    // Actor-heavy cells can fill a receive batch with replaceable NPC state.
    // Preserve order within each class, but handle system/player/chat traffic
    // before actor and world traffic so gameplay packets never wait behind an
    // entire ActorSync batch.
    auto packetPriority = [](const ISteamNetworkingMessage* msg)
    {
        PacketHeader header;
        if (!BasePacket::peekHeader(static_cast<const uint8_t*>(msg->m_pData),
                static_cast<std::size_t>(msg->m_cbSize), header))
            return 4;

        const auto type = static_cast<PacketType>(header.type);
        if (header.type <= static_cast<uint16_t>(PacketType::CharacterSelectError)
            || type == PacketType::Challenge
            || type == PacketType::ChallengeResponse)
            return 0;
        if (header.type >= static_cast<uint16_t>(PacketType::PlayerBaseInfo)
            && header.type <= static_cast<uint16_t>(PacketType::ChatMessage))
            return 1;
        if (header.type >= static_cast<uint16_t>(PacketType::ActorList)
            && header.type <= static_cast<uint16_t>(PacketType::ActorAttackV2))
            return 3;
        return 2;
    };
    std::stable_sort(msgs, msgs + n, [&](const auto* lhs, const auto* rhs) {
        return packetPriority(lhs) < packetPriority(rhs);
    });

    for (int i = 0; i < n; ++i)
    {
        auto* msg = msgs[i];
        auto it = mClients.find(msg->m_conn);
        PacketHeader messageHeader;
        const bool hasHeader = BasePacket::peekHeader(static_cast<const uint8_t*>(msg->m_pData),
            static_cast<std::size_t>(msg->m_cbSize), messageHeader);
        const auto handlerStart = std::chrono::steady_clock::now();
        const bool handled = it != mClients.end();
        if (handled)
            onClientMessage(it->second,
                static_cast<const uint8_t*>(msg->m_pData),
                static_cast<std::size_t>(msg->m_cbSize));
        const double handlerMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - handlerStart).count();
        if (handlerMs >= 8.0)
        {
            ++mLoopDiagnostics.slowHandlers;
            Log(handlerMs >= 50.0 ? Debug::Warning : Debug::Info)
                << "[MPDIAG] Slow server packet handler"
                << " type=" << (hasHeader ? messageHeader.type : 0)
                << " bytes=" << msg->m_cbSize
                << " elapsedMs=" << handlerMs;
        }
        if (handled && hasHeader && static_cast<PacketType>(messageHeader.type) == PacketType::PlayerPosition)
        {
            ++mLoopDiagnostics.playerPositionRelays;
            mLoopDiagnostics.playerPositionRelayTotalMs += handlerMs;
            mLoopDiagnostics.playerPositionRelayMaxMs
                = std::max(mLoopDiagnostics.playerPositionRelayMaxMs, handlerMs);
        }
        msg->Release();
    }
}

// ---------------------------------------------------------------------------
void MPServer::onClientConnected(HSteamNetConnection conn)
{
    if ((int)mClients.size() >= mMaxPlayersConfig)
    {
        mInterface->CloseConnection(conn, 0, "Server full", false);
        return;
    }

    ConnectedClient client;
    client.conn = conn;
    client.guid = mNextGuid++;
    mClients.emplace(conn, client);

    mInterface->SetConnectionPollGroup(conn, mPollGroup);
    // Lane 0 carries player/chat/system traffic, lane 1 carries real-time actor
    // snapshots/events, and lane 2 carries reliable ActorSync bootstrap traffic.
    // Weighted peers prevent starvation without allowing bulk identity/list data
    // to head-of-line block wandering actors.
    const int lanePriorities[3] = { 0, 0, 0 };
    const uint16 laneWeights[3] = { 4, 4, 2 };
    const EResult laneResult = mInterface->ConfigureConnectionLanes(
        conn, 3, lanePriorities, laneWeights);
    if (laneResult != k_EResultOK)
        Log(Debug::Warning) << "[Server] Failed to configure network lanes conn=" << conn
                            << " result=" << static_cast<int>(laneResult);
    Log(Debug::Info) << "[Server] Client connected, conn=" << conn;
}

// Note: OnPlayerConnect fires after handshake completes (in handleHandshake),
// not here - the client has no name yet at this point.

// ---------------------------------------------------------------------------
void MPServer::applyCollisionOwnershipTransition(const CollisionCellOwnership::Transition& transition)
{
    if (!mCollisionWorld)
        return;

    auto makeSpec = [](const std::string& cellId) {
        const std::optional<CellId> parsed = parseCellKey(cellId);
        if (!parsed || makeCellKey(*parsed) != cellId)
            throw std::invalid_argument("non-canonical collision cell identity: " + cellId);
        ServerCollisionWorld::CellSpec spec;
        spec.exterior = parsed->isExterior;
        spec.x = parsed->gridX;
        spec.y = parsed->gridY;
        spec.interior = parsed->cellName;
        return spec;
    };

    // Acquire the complete new set before releasing the old set so a boundary
    // transition never creates a query window with missing collision geometry.
    for (const std::string& cellId : transition.acquire)
    {
        const ServerCollisionWorld::CellSpec spec = makeSpec(cellId);
        const bool newlyLoaded = mCollisionWorld->cellRefCount(cellId) == 0;
        std::uint64_t generation = mCollisionWorld->acquireCell(spec);
        if (newlyLoaded)
        {
            const auto doorsIt = mWorld.doorStates.find(cellId);
            if (doorsIt != mWorld.doorStates.end())
            {
                for (const DoorEntry& door : doorsIt->second)
                    mCollisionWorld->setDoorOpen(cellId, door.refId, door.refNum, door.isOpen);
                generation = mCollisionWorld->cellGeneration(cellId);
            }
        }
        Log(Debug::Verbose) << "[ServerCollision] Acquired live cell=" << cellId
                            << " refs=" << mCollisionWorld->cellRefCount(cellId)
                            << " generation=" << generation;
    }

    for (const std::string& cellId : transition.release)
    {
        const ServerCollisionWorld::CellSpec spec = makeSpec(cellId);
        mCollisionWorld->releaseCell(spec);
        Log(Debug::Verbose) << "[ServerCollision] Released live cell=" << cellId
                            << " refs=" << mCollisionWorld->cellRefCount(cellId)
                            << " generation=" << mCollisionWorld->cellGeneration(cellId);
    }
}

void MPServer::updateCollisionInterest(ConnectedClient& client)
{
    if (!client.charSelectComplete || !mCollisionWorld)
        return;

    const std::string ownerId = "player:" + std::to_string(client.guid);
    const std::vector<std::string> previous = mCollisionOwnership.cells(ownerId);
    CollisionCellOwnership::Transition transition = mCollisionOwnership.update(ownerId,
        collisionCellsForPlayer(client.player.cell, client.player.position, mObservationAlarmRadius));
    if (transition.acquire.empty() && transition.release.empty())
        return;

    try
    {
        applyCollisionOwnershipTransition(transition);
    }
    catch (const std::exception& e)
    {
        // Acquisitions precede releases. Restore the logical owner and undo
        // every possibly completed acquire; the failed acquire is a no-op to
        // release because ServerCollisionWorld rolls back its own lifecycle.
        mCollisionOwnership.update(ownerId, previous);
        CollisionCellOwnership::Transition rollback;
        rollback.release = transition.acquire;
        try
        {
            applyCollisionOwnershipTransition(rollback);
        }
        catch (const std::exception& rollbackError)
        {
            Log(Debug::Error) << "[ServerCollision] Interest rollback failed player=" << client.name
                              << " error=" << rollbackError.what();
        }
        Log(Debug::Warning) << "[ServerCollision] Interest update rejected player=" << client.name
                            << " cell=" << makeCellKey(client.player.cell)
                            << " error=" << e.what();
    }
}

void MPServer::releaseCollisionInterest(uint32_t playerGuid)
{
    if (!mCollisionWorld || playerGuid == 0)
        return;
    const CollisionCellOwnership::Transition transition
        = mCollisionOwnership.remove("player:" + std::to_string(playerGuid));
    try
    {
        applyCollisionOwnershipTransition(transition);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[ServerCollision] Failed to release player interest guid=" << playerGuid
                          << " error=" << e.what();
    }
}

// ---------------------------------------------------------------------------
float MPServer::playerBootWeight(const ConnectedClient& player) const
{
    constexpr std::size_t BootsSlot = 7;
    if (!mContentRegistry || BootsSlot >= player.player.equipment.size())
        return 0.f;
    const std::string& refId = player.player.equipment[BootsSlot].item.refId;
    if (refId.empty())
        return 0.f;

    const ESM::RefId id = ESM::RefId::stringRefId(refId);
    if (const ESM::Armor* armor = mContentRegistry->store().get<ESM::Armor>().search(id))
        return armor->mData.mWeight;
    if (const ESM::Clothing* clothing = mContentRegistry->store().get<ESM::Clothing>().search(id))
        return clothing->mData.mWeight;
    return 0.f;
}

CrimeWitnessBuildResult MPServer::buildLiveCrimeWitnesses(const CrimeWitnessBuildRequest& request)
{
    struct MaterializedSource final : LiveCrimeWitnessSource
    {
        std::vector<LiveCrimeWitnessActor> actorsInCell(std::string_view cellId) const override
        {
            const auto found = cells.find(std::string(cellId));
            return found == cells.end() ? std::vector<LiveCrimeWitnessActor>() : found->second;
        }

        std::optional<LiveCrimeWitnessActor> findActor(
            const ObservationActorIdentity& identity) const override
        {
            for (const auto& [cellId, actors] : cells)
            {
                (void)cellId;
                const auto found = std::find_if(actors.begin(), actors.end(), [&](const auto& actor) {
                    return actor.identity == identity;
                });
                if (found != actors.end())
                    return *found;
            }
            return std::nullopt;
        }

        std::unordered_map<std::string, std::vector<LiveCrimeWitnessActor>> cells;
    } source;

    if (!mContentRegistry)
        return {};

    Position eventPosition;
    eventPosition.pos[0] = request.offender.position.x;
    eventPosition.pos[1] = request.offender.position.y;
    eventPosition.pos[2] = request.offender.position.z;
    std::vector<std::string> cells
        = collisionCellsForPlayer(request.eventCell, eventPosition, request.alarmRadius);

    if (request.victim && request.victim->kind == ObservationActorKind::Npc)
    {
        const auto keyIt = mWorld.actorKeysByNetId.find(request.victim->actorInstanceId);
        if (keyIt != mWorld.actorKeysByNetId.end())
        {
            const auto locationIt = mWorld.actorLocations.find(keyIt->second);
            if (locationIt != mWorld.actorLocations.end())
                cells.push_back(locationIt->second);
        }
    }
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());

    const std::uint64_t nowMs = request.observedAtMs;
    for (const std::string& cellId : cells)
    {
        const auto cellIt = mWorld.actorCells.find(cellId);
        if (cellIt == mWorld.actorCells.end())
            continue;

        std::vector<LiveCrimeWitnessActor>& destination = source.cells[cellId];
        destination.reserve(cellIt->second.actors.size());
        for (const auto& [actorKey, record] : cellIt->second.actors)
        {
            (void)actorKey;
            if (!isValidActorInstanceId(record.actorNetId))
                continue;

            LiveCrimeWitnessActor actor;
            actor.identity.actorInstanceId = record.actorNetId;
            actor.refId = record.actor.refId;
            actor.cellId = cellId;
            actor.migrationGeneration = record.migrationGeneration;
            actor.authorityGeneration = isActorAuthorityLeaseValid(record, cellId, nowMs)
                ? record.actorAuthorityGeneration : cellIt->second.authorityGeneration;

            const ESM::RefId refId = ESM::RefId::stringRefId(record.actor.refId);
            if (const ESM::NPC* npc = mContentRegistry->store().get<ESM::NPC>().search(refId))
            {
                actor.identity.kind = ObservationActorKind::Npc;
                actor.alarm = static_cast<std::int32_t>(npc->mAiData.mAlarm);
                actor.alarmProvenance = CrimeAlarmProvenance::StaticContentBase;
            }
            else if (mContentRegistry->store().get<ESM::Creature>().search(refId))
                actor.identity.kind = ObservationActorKind::Creature;
            else
                continue;

            // The static adapter is only a fallback. A fresh accepted protocol-10
            // mechanics snapshot supersedes it with generation-bound effective
            // Alarm and relationship authority; absent delegated state still
            // fails closed.
            actor.relationship = CrimeWitnessRelationship::Unknown;
            actor.relationshipProvenance = CrimeRelationshipProvenance::Unavailable;
            destination.push_back(std::move(actor));
        }
    }

    return CrimeWitnessBuilder(mMechanicsSnapshots).build(request, source);
}

void MPServer::dispatchCrimeReactions(ConnectedClient& offender, const CrimeSemanticResult& result)
{
    if (!result.accepted || result.eventId.empty() || result.type == CrimeType::WerewolfExposure)
        return;

    std::unordered_map<std::string, CrimeReactionDirective> directives;
    const std::uint64_t nowMs = currentServerTimeMs();

    for (const CrimeWitnessResult& witness : result.witnesses)
    {
        if (!witness.identity.isValid() || witness.identity.kind != ObservationActorKind::Npc)
            continue;

        const auto keyIt = mWorld.actorKeysByNetId.find(witness.identity.actorInstanceId);
        if (keyIt == mWorld.actorKeysByNetId.end())
            continue;
        const auto locationIt = mWorld.actorLocations.find(keyIt->second);
        if (locationIt == mWorld.actorLocations.end())
            continue;
        auto cellIt = mWorld.actorCells.find(locationIt->second);
        if (cellIt == mWorld.actorCells.end())
            continue;
        auto actorIt = cellIt->second.actors.find(keyIt->second);
        if (actorIt == cellIt->second.actors.end())
            continue;

        ActorRegistryRecord& record = actorIt->second;
        if (record.actor.isDead || record.migrationGeneration == 0)
            continue;

        const ESM::NPC* npc = mContentRegistry->store().get<ESM::NPC>().search(
            ESM::RefId::stringRefId(record.actor.refId));
        if (!npc)
            continue;

        CrimeActorReaction reaction;
        reaction.actorNetId = record.actorNetId;
        reaction.migrationGeneration = record.migrationGeneration;

        // Native commitCrime makes every witness that actually perceived Theft
        // or Pickpocket complain, even if its Alarm is too low to report it.
        if ((result.type == CrimeType::Theft || result.type == CrimeType::Pickpocket)
            && witness.perceived)
            reaction.dialogue = CrimeReactionDialogue::Thief;
        // Native reportCrime emits "intruder" from Alarm-100 witnesses after a
        // reported Trespass. CrimeSemanticService::reported already represents
        // that exact post-perception reporting stage.
        else if (result.type == CrimeType::Trespass && witness.reported)
            reaction.dialogue = CrimeReactionDialogue::Intruder;

        const bool guard = npc->mClass == "guard";
        if (guard && witness.reported)
        {
            const bool continuingEnforcement = record.crimePursuitCharacterId == offender.dbCharacterId
                && record.crimeEnforcementState != CrimeEnforcementState::None;
            if (!continuingEnforcement)
                record.crimeEnforcementState = CrimeEnforcementState::Arrest;

            record.crimePursuitCharacterId = offender.dbCharacterId;
            record.crimePursuitLastGuid = offender.guid;
            record.crimePursuitReassertArmed = false;
            record.crimePursuitLastReassertMs = nowMs;

            if (record.crimeEnforcementState == CrimeEnforcementState::Combat)
            {
                reaction.flags = static_cast<std::uint8_t>(
                    CrimeReactionSetAlarmed | CrimeReactionStartCombat);
                record.actor.ai.type = BaseActor::AIAction::Type::Combat;
            }
            else if (record.crimeEnforcementState == CrimeEnforcementState::Arrest)
            {
                reaction.flags = static_cast<std::uint8_t>(
                    CrimeReactionSetAlarmed | CrimeReactionPursueOffender);
                record.actor.ai.type = BaseActor::AIAction::Type::Pursue;
            }
            else
                reaction.flags = CrimeReactionSetAlarmed;

            if (record.crimeEnforcementState != CrimeEnforcementState::ArrestPending)
            {
                record.actor.ai.targetId = std::string("mp_remote_") + std::to_string(offender.guid);
                record.actor.ai.targetMpNum = 0;
                record.actor.ai.duration = 0.f;
                record.actor.ai.reset = false;
                record.lastSnapshotTime = nowMs;
                markLuaActorDirty(record, locationIt->second);
            }
        }

        if (reaction.dialogue == CrimeReactionDialogue::None && reaction.flags == 0)
            continue;

        CrimeReactionDirective& directive = directives[locationIt->second];
        directive.eventId = result.eventId;
        directive.cellId = locationIt->second;
        directive.offenderGuid = offender.guid;
        directive.actors.push_back(reaction);
    }

    for (auto& [cellId, directive] : directives)
    {
        if (!validateCrimeReactionDirective(directive))
        {
            Log(Debug::Error) << "[CrimeReaction] refusing invalid server-authored directive"
                              << " event=" << result.eventId
                              << " cell=" << cellId
                              << " actors=" << directive.actors.size();
            continue;
        }

        PacketCrimeReaction packet;
        packet.directive = directive;
        broadcastActorToCell(cellId, packet.encode());

        std::size_t dialogueCount = 0;
        std::size_t pursueCount = 0;
        for (const CrimeActorReaction& reaction : directive.actors)
        {
            dialogueCount += reaction.dialogue != CrimeReactionDialogue::None ? 1 : 0;
            pursueCount += (reaction.flags & CrimeReactionPursueOffender) != 0 ? 1 : 0;
        }
        Log(Debug::Info) << "[CrimeReaction] dispatched"
                         << " event=" << result.eventId
                         << " type=" << static_cast<unsigned>(result.type)
                         << " cell=" << cellId
                         << " actors=" << directive.actors.size()
                         << " dialogue=" << dialogueCount
                         << " pursue=" << pursueCount
                         << " offenderGuid=" << offender.guid;
    }
}


void MPServer::dispatchOutstandingCrimePursuitsForCell(
    const std::string& cellId, std::int64_t onlyCharacterId)
{
    auto cellIt = mWorld.actorCells.find(cellId);
    if (cellIt == mWorld.actorCells.end() || cellIt->second.authorityGuid == 0)
        return;

    auto findOffender = [&](std::int64_t characterId) -> ConnectedClient* {
        if (characterId <= 0)
            return nullptr;
        for (auto& [connection, client] : mClients)
        {
            (void)connection;
            if (client.charSelectComplete && client.dbCharacterId == characterId)
                return &client;
        }
        return nullptr;
    };

    // A guard whose arrest was already resisted dominates enforcement in that
    // cell. Do not simultaneously rebuild other guards as fresh arrest offers
    // while one guard is already in the durable hostile/combat state.
    std::unordered_set<std::int64_t> hostileCharacters;
    for (const auto& [actorKey, record] : cellIt->second.actors)
    {
        (void)actorKey;
        if (record.crimePursuitCharacterId > 0 && !record.actor.isDead
            && record.migrationGeneration != 0
            && record.crimeEnforcementState == CrimeEnforcementState::Combat)
            hostileCharacters.insert(record.crimePursuitCharacterId);
    }

    const std::uint64_t nowMs = currentServerTimeMs();
    std::unordered_map<std::int64_t, CrimeReactionDirective> directives;

    for (auto& [actorKey, record] : cellIt->second.actors)
    {
        (void)actorKey;
        if (record.crimePursuitCharacterId <= 0
            || (onlyCharacterId > 0 && record.crimePursuitCharacterId != onlyCharacterId)
            || record.actor.isDead || record.migrationGeneration == 0)
            continue;

        if (record.crimeEnforcementState == CrimeEnforcementState::None)
            record.crimeEnforcementState = CrimeEnforcementState::Arrest;
        if (record.crimeEnforcementState == CrimeEnforcementState::ArrestPending)
            continue;
        if (record.crimeEnforcementState == CrimeEnforcementState::Arrest
            && hostileCharacters.contains(record.crimePursuitCharacterId))
            continue;

        ConnectedClient* offender = findOffender(record.crimePursuitCharacterId);
        if (!offender || offender->player.isDead || offender->player.crimeState.bounty <= 0
            || makeCellKey(offender->player.cell) != cellId)
            continue;

        ensureActorNetId(record, cellId);
        if (record.actorNetId == 0)
            continue;

        const std::string targetId = std::string("mp_remote_") + std::to_string(offender->guid);
        std::uint8_t flags = CrimeReactionSetAlarmed;
        if (record.crimeEnforcementState == CrimeEnforcementState::Combat)
        {
            record.actor.ai.type = BaseActor::AIAction::Type::Combat;
            flags = static_cast<std::uint8_t>(flags | CrimeReactionStartCombat);
        }
        else
        {
            record.actor.ai.type = BaseActor::AIAction::Type::Pursue;
            flags = static_cast<std::uint8_t>(flags | CrimeReactionPursueOffender);
        }
        record.actor.ai.targetId = targetId;
        record.actor.ai.targetMpNum = 0;
        record.actor.ai.duration = 0.f;
        record.actor.ai.reset = false;
        record.crimePursuitLastGuid = offender->guid;
        record.crimePursuitReassertArmed = false;
        record.crimePursuitLastReassertMs = nowMs;
        record.lastSnapshotTime = nowMs;
        markLuaActorDirty(record, cellId);

        CrimeReactionDirective& directive = directives[record.crimePursuitCharacterId];
        if (directive.eventId.empty())
        {
            const char* eventPrefix = record.crimeEnforcementState == CrimeEnforcementState::Combat
                ? "crime-combat:" : "crime-pursuit:";
            directive.eventId = std::string(eventPrefix) + std::to_string(record.crimePursuitCharacterId)
                + ':' + std::to_string(nowMs);
            directive.cellId = cellId;
            directive.offenderGuid = offender->guid;
        }
        directive.actors.push_back({ record.actorNetId, record.migrationGeneration,
            CrimeReactionDialogue::None, flags });
    }

    for (auto& [characterId, directive] : directives)
    {
        (void)characterId;
        if (!validateCrimeReactionDirective(directive))
        {
            Log(Debug::Error) << "[CrimeReaction] refusing invalid outstanding enforcement directive"
                              << " event=" << directive.eventId
                              << " cell=" << cellId
                              << " actors=" << directive.actors.size();
            continue;
        }

        PacketCrimeReaction packet;
        packet.directive = directive;
        broadcastActorToCell(cellId, packet.encode());

        std::size_t pursueCount = 0;
        std::size_t combatCount = 0;
        for (const CrimeActorReaction& reaction : directive.actors)
        {
            pursueCount += (reaction.flags & CrimeReactionPursueOffender) != 0 ? 1 : 0;
            combatCount += (reaction.flags & CrimeReactionStartCombat) != 0 ? 1 : 0;
        }
        if (combatCount != 0 && pursueCount == 0)
        {
            Log(Debug::Info) << "[CrimeReaction] reissued outstanding guard combat"
                             << " event=" << directive.eventId
                             << " cell=" << cellId
                             << " actors=" << directive.actors.size()
                             << " offenderGuid=" << directive.offenderGuid;
        }
        else
        {
            Log(Debug::Info) << "[CrimeReaction] reissued outstanding guard pursuit"
                             << " event=" << directive.eventId
                             << " cell=" << cellId
                             << " actors=" << directive.actors.size()
                             << " offenderGuid=" << directive.offenderGuid;
        }
    }
}

void MPServer::suspendOutstandingCrimePursuitsForCharacterInCell(
    ConnectedClient& offender, const std::string& cellId)
{
    if (offender.dbCharacterId <= 0 || cellId.empty())
        return;

    auto cellIt = mWorld.actorCells.find(cellId);
    if (cellIt == mWorld.actorCells.end())
        return;

    const std::uint64_t nowMs = currentServerTimeMs();
    CrimeReactionDirective directive;

    for (auto& [actorKey, record] : cellIt->second.actors)
    {
        (void)actorKey;
        if (record.crimePursuitCharacterId != offender.dbCharacterId)
            continue;

        if (record.crimeEnforcementState == CrimeEnforcementState::ArrestPending)
        {
            record.crimeEnforcementState = CrimeEnforcementState::Combat;
            Log(Debug::Info) << "[GuardArrest] pending arrest escalated to combat on cell exit"
                             << " player=" << offender.name
                             << " actorNetId=" << record.actorNetId
                             << " cell=" << cellId;
        }
        record.crimePursuitReassertArmed = false;

        const uint32_t targetGuid = record.crimePursuitLastGuid != 0
            ? record.crimePursuitLastGuid : offender.guid;
        const std::string targetId = targetGuid != 0
            ? std::string("mp_remote_") + std::to_string(targetGuid) : std::string();
        if (!targetId.empty() && record.actor.ai.targetId == targetId
            && (record.actor.ai.type == BaseActor::AIAction::Type::Pursue
                || record.actor.ai.type == BaseActor::AIAction::Type::Combat))
        {
            record.actor.ai.type = BaseActor::AIAction::Type::None;
            record.actor.ai.targetId.clear();
            record.actor.ai.targetMpNum = 0;
            record.actor.ai.duration = 0.f;
            record.actor.ai.reset = false;
            record.lastSnapshotTime = nowMs;
            markLuaActorDirty(record, cellId);
        }

        if (cellIt->second.authorityGuid == 0 || record.actor.isDead || record.migrationGeneration == 0)
            continue;

        ensureActorNetId(record, cellId);
        if (record.actorNetId == 0)
            continue;

        if (directive.eventId.empty())
        {
            directive.eventId = "crime-pursuit-suspend:" + std::to_string(offender.dbCharacterId)
                + ':' + std::to_string(nowMs);
            directive.cellId = cellId;
            directive.offenderGuid = offender.guid;
        }
        directive.actors.push_back({ record.actorNetId, record.migrationGeneration,
            CrimeReactionDialogue::None, CrimeReactionClearPursuit });
    }

    if (directive.actors.empty())
        return;
    if (!validateCrimeReactionDirective(directive))
    {
        Log(Debug::Error) << "[CrimeReaction] refusing invalid suspended-pursuit directive"
                          << " event=" << directive.eventId
                          << " cell=" << cellId
                          << " actors=" << directive.actors.size();
        return;
    }

    PacketCrimeReaction packet;
    packet.directive = directive;
    broadcastActorToCell(cellId, packet.encode());
    Log(Debug::Info) << "[CrimeReaction] suspended live guard pursuit"
                     << " event=" << directive.eventId
                     << " cell=" << cellId
                     << " actors=" << directive.actors.size()
                     << " offenderGuid=" << offender.guid
                     << " durableCharacterId=" << offender.dbCharacterId;
}

void MPServer::clearOutstandingCrimePursuitsForCharacter(ConnectedClient& offender)
{
    if (offender.dbCharacterId <= 0)
        return;

    const std::uint64_t nowMs = currentServerTimeMs();
    std::unordered_map<std::string, CrimeReactionDirective> directives;

    for (auto& [cellId, cellState] : mWorld.actorCells)
    {
        for (auto& [actorKey, record] : cellState.actors)
        {
            (void)actorKey;
            if (record.crimePursuitCharacterId != offender.dbCharacterId)
                continue;

            const uint32_t lastGuid = record.crimePursuitLastGuid;
            record.crimePursuitCharacterId = 0;
            record.crimePursuitLastGuid = 0;
            record.crimeEnforcementState = CrimeEnforcementState::None;
            record.crimePursuitReassertArmed = false;
            record.crimePursuitLastReassertMs = 0;

            const std::string lastTargetId = lastGuid != 0
                ? std::string("mp_remote_") + std::to_string(lastGuid) : std::string();
            if (!lastTargetId.empty() && record.actor.ai.targetId == lastTargetId
                && (record.actor.ai.type == BaseActor::AIAction::Type::Pursue
                    || record.actor.ai.type == BaseActor::AIAction::Type::Combat))
            {
                record.actor.ai.type = BaseActor::AIAction::Type::None;
                record.actor.ai.targetId.clear();
                record.actor.ai.targetMpNum = 0;
                record.actor.ai.duration = 0.f;
                record.actor.ai.reset = false;
                record.lastSnapshotTime = nowMs;
                markLuaActorDirty(record, cellId);
            }

            if (cellState.authorityGuid == 0 || record.actor.isDead || record.migrationGeneration == 0
                || makeCellKey(offender.player.cell) != cellId)
                continue;

            ensureActorNetId(record, cellId);
            if (record.actorNetId == 0)
                continue;

            CrimeReactionDirective& directive = directives[cellId];
            if (directive.eventId.empty())
            {
                directive.eventId = "crime-pursuit-clear:" + std::to_string(offender.dbCharacterId)
                    + ':' + std::to_string(nowMs);
                directive.cellId = cellId;
                directive.offenderGuid = offender.guid;
            }
            directive.actors.push_back({ record.actorNetId, record.migrationGeneration,
                CrimeReactionDialogue::None, CrimeReactionClearPursuit });
        }
    }

    for (auto& [cellId, directive] : directives)
    {
        if (!validateCrimeReactionDirective(directive))
        {
            Log(Debug::Error) << "[CrimeReaction] refusing invalid clear-pursuit directive"
                              << " event=" << directive.eventId
                              << " cell=" << cellId
                              << " actors=" << directive.actors.size();
            continue;
        }

        PacketCrimeReaction packet;
        packet.directive = directive;
        broadcastActorToCell(cellId, packet.encode());
        Log(Debug::Info) << "[CrimeReaction] cleared outstanding guard pursuit"
                         << " event=" << directive.eventId
                         << " cell=" << cellId
                         << " actors=" << directive.actors.size()
                         << " offenderGuid=" << offender.guid;
    }
}

std::vector<MPServer::LiveObservationDiagnostic> MPServer::observeNpcCandidates(
    std::uint32_t targetPlayerGuid, std::optional<ActorInstanceId> observerFilter, std::string_view eventId)
{
    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    constexpr float ObservationEndpointHeight = 128.f;
    constexpr std::size_t MaximumCandidates = 64;

    std::vector<LiveObservationDiagnostic> diagnostics;
    if (!mObservationService || eventId.empty())
        return diagnostics;
    ConnectedClient* targetClient = findClientByGuid(targetPlayerGuid);
    if (!targetClient || !targetClient->charSelectComplete)
        return diagnostics;

    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* targetAccepted = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, targetPlayerGuid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!targetAccepted || targetAccepted->snapshot.cellId != makeCellKey(targetClient->player.cell)
        || targetAccepted->snapshot.migrationGeneration != 1
        || targetAccepted->snapshot.authorityGeneration != targetPlayerGuid)
        return diagnostics;

    updateCollisionInterest(*targetClient);
    const std::string ownerId = "player:" + std::to_string(targetPlayerGuid);
    const std::vector<std::string> collisionCells = mCollisionOwnership.cells(ownerId);
    if (collisionCells.empty())
        return diagnostics;

    std::vector<CollisionCellGeneration> collisionGenerations;
    collisionGenerations.reserve(collisionCells.size());
    for (const std::string& cellId : collisionCells)
    {
        const std::uint64_t generation = mCollisionWorld->cellGeneration(cellId);
        if (generation == 0 || mCollisionWorld->cellRefCount(cellId) == 0)
            return {};
        collisionGenerations.push_back({ cellId, generation });
    }

    const MechanicsSnapshot& targetMechanics = targetAccepted->snapshot;
    const float alarmRadiusSquared = mObservationAlarmRadius * mObservationAlarmRadius;
    std::unordered_set<ActorInstanceId> seenActors;
    for (const std::string& cellId : collisionCells)
    {
        const auto cellIt = mWorld.actorCells.find(cellId);
        if (cellIt == mWorld.actorCells.end())
            continue;
        for (const auto& [actorKey, record] : cellIt->second.actors)
        {
            if (diagnostics.size() >= MaximumCandidates)
                return diagnostics;
            if (!isValidActorInstanceId(record.actorNetId)
                || (observerFilter && record.actorNetId != *observerFilter)
                || !seenActors.insert(record.actorNetId).second)
                continue;

            const ESM::RefId refId = ESM::RefId::stringRefId(record.actor.refId);
            if (mContentRegistry->store().get<ESM::NPC>().search(refId) == nullptr)
                continue;

            const AcceptedMechanicsSnapshot* observerAccepted = mMechanicsSnapshots.findFresh(
                { MechanicsSubjectKind::Npc, 0, record.actorNetId }, nowMs, MaximumSnapshotAgeMs);
            const bool leased = isActorAuthorityLeaseValid(record, cellId, nowMs);
            const std::uint32_t expectedAuthorityGeneration
                = leased ? record.actorAuthorityGeneration : cellIt->second.authorityGeneration;
            if (!observerAccepted || observerAccepted->snapshot.cellId != cellId
                || observerAccepted->snapshot.migrationGeneration != record.migrationGeneration
                || observerAccepted->snapshot.authorityGeneration != expectedAuthorityGeneration)
                continue;

            const MechanicsSnapshot& observerMechanics = observerAccepted->snapshot;
            const float dx = observerMechanics.position.pos[0] - targetMechanics.position.pos[0];
            const float dy = observerMechanics.position.pos[1] - targetMechanics.position.pos[1];
            const float dz = observerMechanics.position.pos[2] - targetMechanics.position.pos[2];
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            if (!std::isfinite(distanceSquared) || distanceSquared > alarmRadiusSquared)
                continue;

            ObservationQuery query;
            query.eventId = std::string(eventId) + ":" + std::to_string(record.actorNetId);
            query.cellId = targetMechanics.cellId;
            query.observer = makeLiveObservationSnapshot(*observerAccepted, 0.f);
            query.target = makeLiveObservationSnapshot(*targetAccepted, playerBootWeight(*targetClient));
            // Actor collision shapes are deliberately absent from the query-only
            // server world. Use the canonical humanoid observation height until
            // content-derived actor bounds become part of the server snapshot.
            query.observer.position.z += ObservationEndpointHeight;
            query.target.position.z += ObservationEndpointHeight;
            query.path = ObservationPath::LineOfSightAwareness;
            query.observerPolicy = ObservationObserverPolicy::VanillaCrimeWitness;
            query.eventAuthority = ObservationAuthority::ServerAuthoritative;
            query.observedAtMs = nowMs;
            query.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
            query.collisionGenerations = collisionGenerations;

            LiveObservationDiagnostic diagnostic;
            diagnostic.observerActorNetId = record.actorNetId;
            diagnostic.observerRefId = record.actor.refId;
            diagnostic.observerCellId = cellId;
            diagnostic.targetPlayerGuid = targetPlayerGuid;
            diagnostic.distance = std::sqrt(distanceSquared);
            diagnostic.observerPosition = query.observer.position;
            diagnostic.targetPosition = query.target.position;
            diagnostic.observerSnapshotAgeMs = nowMs - observerAccepted->receivedAtMs;
            diagnostic.targetSnapshotAgeMs = nowMs - targetAccepted->receivedAtMs;
            diagnostic.result = mObservationService->observe(query);
            if (diagnostic.result.reason == ObservationReason::BlockedLineOfSight && mCollisionWorld)
            {
                const ServerCollisionWorld::RaycastDiagnostic ray = mCollisionWorld->diagnoseLineOfSight(
                    { query.target.position.x, query.target.position.y, query.target.position.z },
                    { query.observer.position.x, query.observer.position.y, query.observer.position.z });
                diagnostic.rayHitFraction = ray.fraction;
                diagnostic.rayHitPoint = { ray.hitPoint.x(), ray.hitPoint.y(), ray.hitPoint.z() };
                diagnostic.blockerRefId = ray.refId;
                diagnostic.blockerRefNum = ray.refNum;
                diagnostic.blockerHeightfield = ray.heightfield;
            }
            diagnostics.push_back(std::move(diagnostic));
        }
    }
    return diagnostics;
}

bool MPServer::handleObservationDiagnosticCommand(ConnectedClient& requester, std::string_view message)
{
    if (message != "/observe" && !message.starts_with("/observe "))
        return false;
    if (!mObservationDiagnosticsEnabled)
    {
        sendServerMessage(requester.guid, "Observation diagnostics are disabled on this server.");
        return true;
    }

    std::istringstream input{ std::string(message) };
    std::string command;
    std::string observerText;
    std::string targetText;
    input >> command >> observerText >> targetText;
    ActorInstanceId observerActorNetId = 0;
    std::uint32_t targetGuid = requester.guid;
    auto parseInteger = [](std::string_view value, auto& output) {
        if (value.empty())
            return false;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), output);
        return error == std::errc() && end == value.data() + value.size();
    };
    if (!parseInteger(observerText, observerActorNetId)
        || (!targetText.empty() && !parseInteger(targetText, targetGuid)) || targetGuid == 0)
    {
        sendServerMessage(requester.guid, "Usage: /observe <actorNetId|0 for all> [targetPlayerGuid]");
        return true;
    }

    const std::optional<ActorInstanceId> filter
        = observerActorNetId == 0 ? std::nullopt : std::optional<ActorInstanceId>(observerActorNetId);
    const std::string eventId = "diagnostic:" + std::to_string(requester.guid)
        + ":" + std::to_string(currentServerTimeMs());
    const std::vector<LiveObservationDiagnostic> diagnostics
        = observeNpcCandidates(targetGuid, filter, eventId);
    if (diagnostics.empty())
    {
        sendServerMessage(requester.guid,
            "Observation: no eligible fresh NPC candidate (check identity, range, authority, and snapshots).");
        return true;
    }

    auto reasonName = [](ObservationReason reason) {
        switch (reason)
        {
            case ObservationReason::Observed: return "observed";
            case ObservationReason::InvalidQuery: return "invalid_query";
            case ObservationReason::ObserverKindRejected: return "observer_kind_rejected";
            case ObservationReason::ObserverIneligible: return "observer_ineligible";
            case ObservationReason::StaleActorSnapshot: return "stale_snapshot";
            case ObservationReason::CollisionUnavailable: return "collision_unavailable";
            case ObservationReason::CollisionGenerationMismatch: return "collision_generation_mismatch";
            case ObservationReason::BlockedLineOfSight: return "blocked_los";
            case ObservationReason::AwarenessFailed: return "awareness_failed";
        }
        return "unknown";
    };
    auto optionalBool = [](const std::optional<bool>& value) {
        return value ? (*value ? "true" : "false") : "n/a";
    };
    auto authorityName = [](ObservationAuthority authority) {
        switch (authority)
        {
            case ObservationAuthority::ServerAuthoritative: return "server";
            case ObservationAuthority::ActorAuthorityDelegated: return "actor-delegated";
            case ObservationAuthority::PlayerClientDelegated: return "player-delegated";
            case ObservationAuthority::MixedDelegated: return "mixed-delegated";
        }
        return "unknown";
    };

    constexpr std::size_t MaximumChatResults = 8;
    for (std::size_t i = 0; i < std::min(diagnostics.size(), MaximumChatResults); ++i)
    {
        const LiveObservationDiagnostic& diagnostic = diagnostics[i];
        const ObservationResult& result = diagnostic.result;
        std::ostringstream text;
        text << "Observation npc=" << diagnostic.observerActorNetId
             << " ref=" << diagnostic.observerRefId
             << " target=" << diagnostic.targetPlayerGuid
             << " cell=" << diagnostic.observerCellId
             << " distance=" << static_cast<int>(diagnostic.distance)
             << " snapshot=" << result.observerSnapshotGeneration
             << " authorityGen=" << result.observerAuthorityGeneration
             << " ageMs=" << diagnostic.observerSnapshotAgeMs << "/" << diagnostic.targetSnapshotAgeMs
             << " collision=";
        for (std::size_t generation = 0; generation < result.collisionGenerations.size(); ++generation)
        {
            if (generation != 0)
                text << ',';
            text << result.collisionGenerations[generation].cellId << '@'
                 << result.collisionGenerations[generation].generation;
        }
        text << " los=" << optionalBool(result.lineOfSight)
             << " awareness=" << optionalBool(result.awareness)
             << " observable=" << result.observable
             << " reason=" << reasonName(result.reason)
             << " provenance=" << authorityName(result.authority)
             << " observerPos=" << static_cast<int>(diagnostic.observerPosition.x) << ','
             << static_cast<int>(diagnostic.observerPosition.y) << ','
             << static_cast<int>(diagnostic.observerPosition.z)
             << " targetPos=" << static_cast<int>(diagnostic.targetPosition.x) << ','
             << static_cast<int>(diagnostic.targetPosition.y) << ','
             << static_cast<int>(diagnostic.targetPosition.z);
        if (result.reason == ObservationReason::BlockedLineOfSight)
        {
            text << " hit=" << static_cast<int>(diagnostic.rayHitPoint.x) << ','
                 << static_cast<int>(diagnostic.rayHitPoint.y) << ','
                 << static_cast<int>(diagnostic.rayHitPoint.z)
                 << " fraction=" << diagnostic.rayHitFraction
                 << " blocker=";
            if (diagnostic.blockerHeightfield)
                text << "heightfield";
            else if (!diagnostic.blockerRefId.empty())
                text << diagnostic.blockerRefId << '#' << diagnostic.blockerRefNum;
            else
                text << "unknown";
        }
        sendServerMessage(requester.guid, text.str());
        Log(Debug::Info) << "[ObservationDiagnostic] " << text.str();
    }
    if (diagnostics.size() > MaximumChatResults)
    {
        sendServerMessage(requester.guid, "Observation results truncated: "
            + std::to_string(diagnostics.size()) + " eligible candidates, first 8 shown.");
    }
    return true;
}

bool MPServer::handleCrimeWitnessDiagnosticCommand(ConnectedClient& requester, std::string_view message)
{
    if (message != "/crimewitness" && !message.starts_with("/crimewitness "))
        return false;
    if (!mObservationDiagnosticsEnabled)
    {
        sendServerMessage(requester.guid, "Crime witness diagnostics are disabled on this server.");
        return true;
    }

    std::istringstream input{ std::string(message) };
    std::string command;
    std::string victimText;
    input >> command >> victimText;
    std::optional<ObservationActorIdentity> victim;
    if (!victimText.empty())
    {
        ActorInstanceId actorNetId = 0;
        const auto [end, error]
            = std::from_chars(victimText.data(), victimText.data() + victimText.size(), actorNetId);
        if (error != std::errc() || end != victimText.data() + victimText.size()
            || !isValidActorInstanceId(actorNetId))
        {
            sendServerMessage(requester.guid, "Usage: /crimewitness [victimActorNetId]");
            return true;
        }
        victim = ObservationActorIdentity{ ObservationActorKind::Npc, 0, actorNetId };
    }

    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* offender = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, requester.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!offender || offender->snapshot.cellId != makeCellKey(requester.player.cell)
        || offender->snapshot.migrationGeneration != 1
        || offender->snapshot.authorityGeneration != requester.guid)
    {
        sendServerMessage(requester.guid, "Crime witness: requester mechanics snapshot is unavailable or stale.");
        return true;
    }

    CrimeWitnessBuildRequest request;
    request.eventCell = requester.player.cell;
    request.offender = makeLiveObservationSnapshot(*offender, playerBootWeight(requester));
    request.victim = victim;
    request.alarmRadius = mObservationAlarmRadius;
    request.observedAtMs = nowMs;
    request.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
    const CrimeWitnessBuildResult result = buildLiveCrimeWitnesses(request);

    auto kindName = [](ObservationActorKind kind) {
        switch (kind)
        {
            case ObservationActorKind::Player: return "player";
            case ObservationActorKind::Npc: return "npc";
            case ObservationActorKind::Creature: return "creature";
        }
        return "unknown";
    };
    auto alarmName = [](CrimeAlarmProvenance provenance) {
        switch (provenance)
        {
            case CrimeAlarmProvenance::Unavailable: return "unavailable";
            case CrimeAlarmProvenance::StaticContentBase: return "static-base";
            case CrimeAlarmProvenance::ValidatedActorAuthorityDelegated: return "actor-delegated-effective";
        }
        return "unknown";
    };
    auto relationshipName = [](CrimeWitnessRelationship relationship) {
        switch (relationship)
        {
            case CrimeWitnessRelationship::Eligible: return "safe";
            case CrimeWitnessRelationship::InCombatWithVictim: return "combat-victim";
            case CrimeWitnessRelationship::PlayerFollower: return "player-follower";
            case CrimeWitnessRelationship::Unknown: return "unknown";
        }
        return "unknown";
    };
    auto relationshipSourceName = [](CrimeRelationshipProvenance provenance) {
        switch (provenance)
        {
            case CrimeRelationshipProvenance::Unavailable: return "unavailable";
            case CrimeRelationshipProvenance::ServerAuthoritative: return "server";
            case CrimeRelationshipProvenance::ValidatedActorAuthorityDelegated: return "actor-delegated";
        }
        return "unknown";
    };
    auto reasonName = [](CrimeWitnessBuildReason reason) {
        switch (reason)
        {
            case CrimeWitnessBuildReason::Included: return "eligible";
            case CrimeWitnessBuildReason::DuplicateIdentity: return "duplicate";
            case CrimeWitnessBuildReason::CanonicalKindRejected: return "kind-rejected";
            case CrimeWitnessBuildReason::OutsideAlarmRadius: return "outside-radius";
            case CrimeWitnessBuildReason::MechanicsSnapshotMissing: return "snapshot-missing";
            case CrimeWitnessBuildReason::MechanicsSnapshotStale: return "snapshot-stale";
            case CrimeWitnessBuildReason::WrongCell: return "wrong-cell";
            case CrimeWitnessBuildReason::WrongMigrationGeneration: return "wrong-migration";
            case CrimeWitnessBuildReason::WrongAuthorityGeneration: return "wrong-authority";
            case CrimeWitnessBuildReason::ActorIneligible: return "actor-ineligible";
            case CrimeWitnessBuildReason::AlarmUnavailable: return "alarm-unavailable";
            case CrimeWitnessBuildReason::AlarmInvalid: return "alarm-invalid";
            case CrimeWitnessBuildReason::InCombatWithVictim: return "combat-victim";
            case CrimeWitnessBuildReason::PlayerFollower: return "player-follower";
            case CrimeWitnessBuildReason::RelationshipUnknown: return "relationship-unknown";
        }
        return "unknown";
    };

    if (result.decisions.empty())
    {
        sendServerMessage(requester.guid, "Crime witness: no canonical actor candidates in radius cells.");
        return true;
    }

    constexpr std::size_t MaximumChatResults = 8;
    for (std::size_t index = 0; index < std::min(result.decisions.size(), MaximumChatResults); ++index)
    {
        const CrimeWitnessBuildDecision& decision = result.decisions[index];
        std::ostringstream text;
        text << "CrimeWitness actor=" << decision.identity.actorInstanceId
             << " ref=" << decision.refId
             << " kind=" << kindName(decision.identity.kind)
             << " cell=" << decision.cellId
             << " distance=" << (decision.distance ? std::to_string(static_cast<int>(*decision.distance)) : "n/a")
             << " migration=" << decision.migrationGeneration
             << " authority=" << decision.authorityGeneration
             << " ageMs=" << (decision.snapshotAgeMs ? std::to_string(*decision.snapshotAgeMs) : "n/a")
             << " alarm=" << (decision.alarm ? std::to_string(*decision.alarm) : "n/a")
             << " alarmSource=" << alarmName(decision.alarmProvenance)
             << " relationship=" << relationshipName(decision.relationship)
             << " relationshipSource=" << relationshipSourceName(decision.relationshipProvenance)
             << " result=" << reasonName(decision.reason);
        sendServerMessage(requester.guid, text.str());
        Log(Debug::Info) << "[CrimeWitnessDiagnostic] " << text.str();
    }
    if (result.decisions.size() > MaximumChatResults)
    {
        sendServerMessage(requester.guid, "Crime witness results truncated: "
            + std::to_string(result.decisions.size()) + " candidates, first 8 shown.");
    }
    return true;
}

// ---------------------------------------------------------------------------
void MPServer::onClientDisconnected(HSteamNetConnection conn, const std::string& reason)
{
    auto it = mClients.find(conn);
    if (it == mClients.end()) return;

    auto& client = it->second;
    const std::string actorCell = makeCellKey(client.player.cell);
    const std::unordered_set<std::string> actorInterestCells = actorInterestCellsForClient(client);
    Log(Debug::Info) << "[Server] Client disconnected: "
                     << client.name << " (" << reason << ")";

    if (client.player.vehicle.active
        || mActiveVehiclesByDriver.find(client.guid) != mActiveVehiclesByDriver.end())
    {
        setPlayerVehicleState(client.guid, false, std::string(), 0);
    }

    // Persist last known position before removing the client.
    if (mPlayerDb && client.dbCharacterId != 0 && client.charSelectComplete)
    {
        const auto& pos = client.player.position;
        const std::string savedCell = makeCellKey(client.player.cell);
        try
        {
            Log(Debug::Info) << "[PlayerDB] savePosition: charId=" << client.dbCharacterId
                             << " cell='" << savedCell
                             << "' pos=(" << pos.pos[0] << "," << pos.pos[1] << "," << pos.pos[2] << ")";
            mPlayerDb->savePosition(client.dbCharacterId,
                                    savedCell,
                                    pos.pos[0], pos.pos[1], pos.pos[2],
                                    pos.rot[0], pos.rot[1], pos.rot[2]);
            // PlayerStatsDynamic is persisted on receipt. Avoid a blind disconnect
            // rewrite from a partially restored/template player during join/quit.
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] disconnect save error: " << e.what();
        }
    }

    if (client.charSelectComplete)
    {
        mLua.onPlayerDisconnect(client.guid, client.name, reason);

        // Notify all others
        PacketDisconnect pkt;
        pkt.guid   = client.guid;
        pkt.reason = reason;
        broadcastToAll(pkt.encode(), conn);
    }

    const uint32_t disconnectedGuid = client.guid;
    releaseCollisionInterest(disconnectedGuid);
    if (mObservationService)
        mObservationService->invalidateObserver({ ObservationActorKind::Player, disconnectedGuid, 0 });
    mMechanicsSnapshots.erasePlayer(disconnectedGuid);
    std::vector<std::pair<std::string, ActorRegistryRecord*>> revokedActorLeases;
    for (auto& [cellId, cellState] : mWorld.actorCells)
    {
        for (auto& [actorKey, record] : cellState.actors)
        {
            if (record.actorAuthorityGuid != disconnectedGuid)
                continue;
            record.actorAuthorityGuid = 0;
            record.actorAuthorityTargetGuid = 0;
            record.actorAuthorityLeaseUntilMs = 0;
            record.actorAuthorityReason.clear();
            ++record.actorAuthorityGeneration;
            revokedActorLeases.emplace_back(cellId, &record);
        }
    }

    mInterface->CloseConnection(conn, 0, nullptr, false);
    mLua.clearPlayerMarks(client.guid);
    mLua.clearPlayerData(client.guid);
    mClients.erase(it);
    for (const auto& [cellId, record] : revokedActorLeases)
    {
        broadcastActorAuthorityLease(cellId, *record);
        Log(Debug::Info) << "[Server] Actor authority lease revoked on disconnect"
                         << " actorNetId=" << record->actorNetId
                         << " refId=" << record->actor.refId
                         << " mpNum=" << record->actor.mpNum
                         << " cell=" << cellId
                         << " disconnectedGuid=" << disconnectedGuid
                         << " generation=" << record->actorAuthorityGeneration;
    }
    // A non-authority player leaving a cell does not change its simulation
    // owner.  Rebroadcasting the complete actor baseline in that case makes
    // already-applied corpses pass through the bootstrap hide/reveal path
    // again on every remaining client.  Per-actor leases owned by the player
    // were revoked and broadcast above, so only refresh cells whose actual
    // cell authority disconnected.
    auto refreshIfOwnedByDisconnectedPlayer = [&](const std::string& cellId)
    {
        const auto cellIt = mWorld.actorCells.find(cellId);
        if (cellIt != mWorld.actorCells.end()
            && cellIt->second.authorityGuid == disconnectedGuid)
        {
            refreshActorAuthorityForCell(cellId);
        }
    };
    if (!actorInterestCells.empty())
    {
        for (const std::string& cellId : actorInterestCells)
            refreshIfOwnedByDisconnectedPlayer(cellId);
    }
    else if (!actorCell.empty())
        refreshIfOwnedByDisconnectedPlayer(actorCell);
    syncLuaPlayerSnapshot();
}

// ---------------------------------------------------------------------------
bool MPServer::refreshActorAuthorityForCell(
    const std::string& cellId, uint32_t preferredGuid, bool reissueOutstandingPursuits)
{
    if (cellId.empty())
        return false;

    auto& cellState = mWorld.actorCells[cellId];
    uint32_t newAuthorityGuid = 0;
    const uint64_t now = currentServerTimeMs();
    const auto parsedActorCell = parseCellKey(cellId);

    // Expired, disconnected, or out-of-range per-actor owners must be cleared
    // before clients choose whether to simulate the fallback cell authority.
    for (auto& [actorKey, record] : cellState.actors)
    {
        if (record.actorAuthorityGuid == 0 || isActorAuthorityLeaseValid(record, cellId, now))
            continue;
        const uint32_t previousOwner = record.actorAuthorityGuid;
        record.actorAuthorityGuid = 0;
        record.actorAuthorityTargetGuid = 0;
        record.actorAuthorityLeaseUntilMs = 0;
        record.actorAuthorityReason.clear();
        ++record.actorAuthorityGeneration;
        broadcastActorAuthorityLease(cellId, record);
        Log(Debug::Info) << "[Server] Actor authority lease expired"
                         << " actorNetId=" << record.actorNetId
                         << " refId=" << record.actor.refId
                         << " mpNum=" << record.actor.mpNum
                         << " cell=" << cellId
                         << " previousOwner=" << previousOwner
                         << " generation=" << record.actorAuthorityGeneration;
    }

    auto isEligible = [&](const ConnectedClient& client)
    {
        if (!client.charSelectComplete)
            return false;
        if (parsedActorCell && parsedActorCell->isExterior)
        {
            if (!client.player.cell.isExterior)
                return false;
            return std::abs(client.player.cell.gridX - parsedActorCell->gridX)
                    <= mActorAuthorityExteriorRadius
                && std::abs(client.player.cell.gridY - parsedActorCell->gridY)
                    <= mActorAuthorityExteriorRadius;
        }
        return clientHasActorCellLoaded(client, cellId);
    };

    // Keep the current owner during its sticky window while it remains eligible.
    // This deliberately runs before exact-cell preference to prevent authority
    // churn when players straddle an exterior border.
    if (cellState.authorityGuid != 0 && now < cellState.authorityStickyUntilMs)
    {
        for (const auto& [conn, client] : mClients)
        {
            if (client.guid == cellState.authorityGuid && isEligible(client))
            {
                newAuthorityGuid = client.guid;
                break;
            }
        }
    }

    // After the sticky window expires (or its owner becomes ineligible), prefer
    // a client whose canonical active cell exactly matches the actor cell.
    if (newAuthorityGuid == 0 && mActorAuthorityPreferExactCell)
    {
        for (const auto& [conn, client] : mClients)
        {
            if (!isEligible(client) || !cellMatches(client.player.cell, cellId))
                continue;

            if (newAuthorityGuid == 0
                || (client.guid == cellState.authorityGuid
                    && newAuthorityGuid != cellState.authorityGuid)
                || (newAuthorityGuid != cellState.authorityGuid
                    && client.guid < newAuthorityGuid))
                newAuthorityGuid = client.guid;
        }
    }

    // For exterior cells visible to several players, prefer the closest active
    // exterior grid. Equal distance keeps the existing authority as hysteresis;
    // only a strictly closer player causes a handoff.
    if (newAuthorityGuid == 0)
    {
        if (parsedActorCell && parsedActorCell->isExterior)
        {
            int bestDistance = -1;
            for (const auto& [conn, client] : mClients)
            {
                if (!isEligible(client) || !client.player.cell.isExterior)
                    continue;

                const int distance = std::max(
                    std::abs(client.player.cell.gridX - parsedActorCell->gridX),
                    std::abs(client.player.cell.gridY - parsedActorCell->gridY));
                if (bestDistance == -1 || distance < bestDistance
                    || (distance == bestDistance
                        && client.guid == cellState.authorityGuid
                        && newAuthorityGuid != cellState.authorityGuid)
                    || (distance == bestDistance
                        && newAuthorityGuid != cellState.authorityGuid
                        && client.guid < newAuthorityGuid))
                {
                    bestDistance = distance;
                    newAuthorityGuid = client.guid;
                }
            }
        }
    }

    // Current authority is gone - try the preferred GUID after active/closest choices.
    if (newAuthorityGuid == 0 && preferredGuid != 0)
    {
        for (const auto& [conn, client] : mClients)
        {
            if (client.guid == preferredGuid && isEligible(client))
            {
                newAuthorityGuid = client.guid;
                break;
            }
        }
    }

    // No stronger preference - lowest GUID wins.
    if (newAuthorityGuid == 0)
    {
        for (const auto& [conn, client] : mClients)
        {
            if (!isEligible(client))
                continue;

            if (newAuthorityGuid == 0 || client.guid < newAuthorityGuid)
                newAuthorityGuid = client.guid;
        }
    }

    const bool authorityChanged = cellState.authorityGuid != newAuthorityGuid;
    if (authorityChanged)
    {
        cellState.authorityGuid = newAuthorityGuid;
        ++cellState.authorityGeneration;
        cellState.authorityStickyUntilMs = newAuthorityGuid != 0
            ? now + static_cast<uint64_t>(mActorAuthorityStickyMs) : 0;
        Log(Debug::Info) << "[Server] Actor authority for " << cellId
                         << " -> guid=" << newAuthorityGuid
                         << " generation=" << cellState.authorityGeneration
                         << " stickyUntilMs=" << cellState.authorityStickyUntilMs
                         << " exteriorRadius=" << mActorAuthorityExteriorRadius;
    }

    if (newAuthorityGuid == 0 && !cellState.actors.empty())
    {
        std::size_t removedRuntimeActors = 0;
        std::vector<ActorRegistryRecord> removedRecords;
        for (auto it = cellState.actors.begin(); it != cellState.actors.end();)
        {
            if (it->second.actor.mpNum == 0 || it->second.persistent)
            {
                ++it;
                continue;
            }

            ensureActorNetId(it->second, cellId);
            removedRecords.push_back(it->second);
            if (mPlayerDb)
                mPlayerDb->deleteSpawnedActorDynamicRecordLink(it->second.actor.mpNum, cellId);
            forgetActorLocation(it->second.actor, cellId);
            it = cellState.actors.erase(it);
            ++removedRuntimeActors;
        }

        if (removedRuntimeActors != 0)
        {
            broadcastActorIdentityRemovalForCell(cellId, cellState, removedRecords);
            for (const ActorRegistryRecord& record : removedRecords)
            {
                markLuaActorRemoved(record.actor.mpNum);
                forgetActorNetId(record.actorNetId, record.actor);
            }
            if (mPlayerDb)
                scheduleGeneratedDynamicRecordGc("actor_cell_empty");
            Log(Debug::Info) << "[Server] Cleared " << removedRuntimeActors
                             << " runtime spawned actor(s) for inactive cell " << cellId;
        }
    }

    sendActorStateToInterestedClients(cellId);
    if (authorityChanged && newAuthorityGuid != 0 && reissueOutstandingPursuits)
        dispatchOutstandingCrimePursuitsForCell(cellId);
    return authorityChanged;
}

// ---------------------------------------------------------------------------
void MPServer::sendActorAuthorityToClient(HSteamNetConnection conn, const std::string& cellId)
{
    auto it = mWorld.actorCells.find(cellId);
    CellActorState emptyState;
    const CellActorState& state = (it != mWorld.actorCells.end()) ? it->second : emptyState;

    ActorList actorList;
    actorList.cellId = cellId;
    actorList.isAuthority = state.authorityGuid != 0;
    actorList.authorityGuid = state.authorityGuid;
    actorList.authorityGeneration = state.authorityGeneration;
    actorList.snapshotSequence = state.nextSnapshotSequence;
    actorList.serverTimestamp = currentServerTimeMs();

    PacketActorAuthority pkt;
    pkt.setActorList(&actorList);
    sendTo(conn, pkt.encode());
}

// ---------------------------------------------------------------------------
void MPServer::sendActorStateToClient(HSteamNetConnection conn, const std::string& cellId)
{
    auto it = mWorld.actorCells.find(cellId);
    CellActorState emptyState;
    const CellActorState& state = (it != mWorld.actorCells.end()) ? it->second : emptyState;

    std::unordered_map<std::string, ActorRegistryRecord> actors = state.actors;
    mergeDeadVanillaActorsForCell(cellId, actors);
    if (actors.empty())
        return;

    ActorList actorList;
    actorList.cellId = cellId;
    actorList.isAuthority = false;
    actorList.authorityGuid = state.authorityGuid;
    actorList.authorityGeneration = state.authorityGeneration;
    actorList.snapshotSequence = state.nextSnapshotSequence;
    actorList.serverTimestamp = currentServerTimeMs();

    std::size_t deadVanillaCount = 0;
    bool includesHul = false;
    std::vector<BaseActor> deadBootstrapActors;
    actorList.actors.reserve(actors.size());
    for (const auto& [key, record] : actors)
    {
        if (record.actor.mpNum == 0 && record.actor.isDead)
        {
            ++deadVanillaCount;
            if (record.actor.refId == "hul")
                includesHul = true;
        }
        if (record.actor.isDead)
        {
            BaseActor deadActor = record.actor;
            deadActor.isInstantDeath = true;
            if (deadActor.dynamicStats.health.current > 0.f)
                deadActor.dynamicStats.health.current = 0.f;
            deadBootstrapActors.push_back(std::move(deadActor));
        }
        actorList.actors.push_back(record.actor);
    }

    if (deadVanillaCount != 0)
    {
        auto clientIt = mClients.find(conn);
        Log(Debug::Verbose) << "[Server] sendActorStateToClient dead vanilla snapshot"
                            << " to=" << (clientIt != mClients.end() ? clientIt->second.name : std::string("<unknown>"))
                            << " cell=" << cellId
                            << " actors=" << actorList.actors.size()
                            << " deadVanilla=" << deadVanillaCount
                            << " includesHul=" << includesHul;
    }

    if (it != mWorld.actorCells.end())
        sendActorIdentityToClient(conn, cellId, it->second);

    PacketActorList pkt;
    pkt.setActorList(&actorList);
    sendTo(conn, pkt.encode());

    // Per-actor leases are independent of ActorList state. Re-send them after
    // the bootstrap list so late joiners and newly interested clients make the
    // same ownership decision as existing observers.
    for (const auto& [key, record] : state.actors)
    {
        if (record.actorAuthorityGuid == 0 || !isActorAuthorityLeaseValid(record, cellId))
            continue;
        ActorList authority;
        authority.cellId = cellId;
        authority.isAuthority = true;
        authority.authorityGuid = record.actorAuthorityGuid;
        authority.authorityGeneration = record.actorAuthorityGeneration;
        authority.serverTimestamp = currentServerTimeMs();
        authority.actors.push_back(record.actor);
        PacketActorAuthority authorityPacket;
        authorityPacket.setActorList(&authority);
        sendTo(conn, authorityPacket.encode());
    }

    if (!deadBootstrapActors.empty())
    {
        ActorList statsList = actorList;
        statsList.actors = deadBootstrapActors;
        PacketActorStatsDynamic statsPkt;
        statsPkt.setActorList(&statsList);
        sendTo(conn, statsPkt.encode());

        ActorList deathList = actorList;
        deathList.actors = std::move(deadBootstrapActors);
        PacketActorDeath deathPkt;
        deathPkt.setActorList(&deathList);
        sendTo(conn, deathPkt.encode());
    }
}

void MPServer::sendActorIdentityToClient(HSteamNetConnection conn, const std::string& cellId, CellActorState& cellState)
{
    auto clientIt = mClients.find(conn);
    if (clientIt == mClients.end() || clientIt->second.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
        return;

    std::unordered_map<std::string, ActorRegistryRecord> actors = cellState.actors;
    mergeDeadVanillaActorsForCell(cellId, actors);
    if (actors.empty())
        return;

    ActorIdentityList identityList = buildActorIdentityList(cellId, cellState, actors);
    if (identityList.actors.empty())
        return;

    PacketActorIdentity pkt;
    pkt.setIdentityList(&identityList);
    sendTo(conn, pkt.encode());
    for (const ActorIdentityRecord& record : identityList.actors)
    {
        if (clientIt->second.actorV2IdentitySent.insert(record.actorNetId).second)
            ++clientIt->second.actorV2IdentitySentWindow;
    }

    Log(Debug::Verbose) << "[Server] ActorSync v2 identity sent"
                        << " to=" << clientIt->second.name
                        << " cell=" << cellId
                        << " actors=" << identityList.actors.size()
                        << " seq=" << identityList.sequence;
}

void MPServer::broadcastActorIdentityForCell(
    const std::string& cellId, CellActorState& cellState, HSteamNetConnection except)
{
    std::unordered_map<std::string, ActorRegistryRecord> actors = cellState.actors;
    mergeDeadVanillaActorsForCell(cellId, actors);
    if (actors.empty())
        return;

    ActorIdentityList identityList = buildActorIdentityList(cellId, cellState, actors);
    if (identityList.actors.empty())
        return;

    PacketActorIdentity pkt;
    pkt.setIdentityList(&identityList);
    const std::vector<uint8_t> encoded = pkt.encode();
    for (auto& [conn, client] : mClients)
    {
        if (conn == except
            || !clientHasActorCellLoaded(client, cellId)
            || client.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
            continue;
        sendTo(conn, encoded);
        for (const ActorIdentityRecord& record : identityList.actors)
        {
            if (client.actorV2IdentitySent.insert(record.actorNetId).second)
                ++client.actorV2IdentitySentWindow;
        }
    }
}

void MPServer::broadcastActorIdentityRemovalForCell(
    const std::string& cellId,
    CellActorState& cellState,
    const std::vector<ActorRegistryRecord>& records,
    ActorRemovalReason removalReason)
{
    if (records.empty())
        return;

    ActorIdentityList identityList;
    identityList.protocolVersion = ActorSyncProtocolVersionV2;
    identityList.cellId = cellId;
    identityList.authorityGuid = cellState.authorityGuid;
    identityList.authorityGeneration = cellState.authorityGeneration;
    identityList.sequence = cellState.nextSnapshotSequence++;
    identityList.serverTimestamp = currentServerTimeMs();
    identityList.actors.reserve(records.size());

    for (const ActorRegistryRecord& record : records)
    {
        ActorRegistryRecord canonicalRecord = record;
        const ActorInstanceId actorNetId = canonicalRecord.actorNetId != 0
            ? canonicalRecord.actorNetId : actorInstanceIdFromActor(canonicalRecord.actor);
        if (actorNetId == 0)
            continue;
        ensureCanonicalActorMigrationGeneration(
            canonicalRecord.migrationGeneration, canonicalRecord.actor);

        ActorIdentityRecord identity;
        identity.actorNetId = actorNetId;
        identity.persistent = canonicalRecord.persistent;
        identity.serverSpawned = canonicalRecord.actor.mpNum != 0;
        identity.removed = true;
        identity.removalReason = removalReason;
        identity.migrationGeneration = canonicalRecord.migrationGeneration;
        identity.actor = canonicalRecord.actor;
        identity.actor.cellId = cellId;
        identityList.actors.push_back(std::move(identity));
    }

    if (identityList.actors.empty())
        return;

    PacketActorIdentity pkt;
    pkt.setIdentityList(&identityList);
    const std::vector<uint8_t> encoded = pkt.encode();
    std::size_t sentClients = 0;
    for (auto& [conn, client] : mClients)
    {
        if (client.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
            continue;

        bool shouldSend = clientHasActorCellLoaded(client, cellId);
        if (!shouldSend)
        {
            for (const ActorIdentityRecord& record : identityList.actors)
            {
                if (client.actorV2IdentitySent.count(record.actorNetId) != 0
                    || client.actorV2IdentityAcked.count(record.actorNetId) != 0)
                {
                    shouldSend = true;
                    break;
                }
            }
        }
        if (!shouldSend)
            continue;

        sendTo(conn, encoded);
        ++sentClients;
        for (const ActorIdentityRecord& record : identityList.actors)
        {
            if (client.actorV2IdentitySent.insert(record.actorNetId).second)
                ++client.actorV2IdentitySentWindow;
        }
    }

    Log(Debug::Info) << "[Server] ActorSync v2 identity removed"
                     << " cell=" << cellId
                     << " actors=" << identityList.actors.size()
                     << " sentClients=" << sentClients
                     << " seq=" << identityList.sequence;
}

// ---------------------------------------------------------------------------
bool MPServer::clientHasActorCellLoaded(const ConnectedClient& client, const std::string& cellId) const
{
    if (cellId.empty() || !client.charSelectComplete)
        return false;

    if (cellMatches(client.player.cell, cellId))
        return true;

    if (client.loadedActorCells.find(cellId) != client.loadedActorCells.end())
        return true;

    // A same-cell scripted recall clears the reported loaded-cell set, and the
    // client may not emit another revision because its active cell did not
    // change. Use the configured exterior authority grid as the interest
    // fallback so authority selection and packet routing cannot disagree.
    const auto parsedCell = parseCellKey(cellId);
    return parsedCell && parsedCell->isExterior && client.player.cell.isExterior
        && std::abs(client.player.cell.gridX - parsedCell->gridX) <= mActorAuthorityExteriorRadius
        && std::abs(client.player.cell.gridY - parsedCell->gridY) <= mActorAuthorityExteriorRadius;
}

bool MPServer::clientEligibleForActorCell(const ConnectedClient& client, const std::string& cellId) const
{
    if (cellId.empty() || !client.charSelectComplete)
        return false;

    const auto parsedCell = parseCellKey(cellId);
    if (parsedCell && parsedCell->isExterior)
    {
        return client.player.cell.isExterior
            && std::abs(client.player.cell.gridX - parsedCell->gridX) <= mActorAuthorityExteriorRadius
            && std::abs(client.player.cell.gridY - parsedCell->gridY) <= mActorAuthorityExteriorRadius;
    }
    return clientHasActorCellLoaded(client, cellId);
}

std::unordered_set<std::string> MPServer::actorInterestCellsForClient(const ConnectedClient& client) const
{
    const std::string currentCell = makeCellKey(client.player.cell);
    std::unordered_set<std::string> cells = client.loadedActorCells;
    if (!currentCell.empty())
        cells.insert(currentCell);

    if (client.player.cell.isExterior)
    {
        for (int dx = -mActorAuthorityExteriorRadius; dx <= mActorAuthorityExteriorRadius; ++dx)
        {
            for (int dy = -mActorAuthorityExteriorRadius; dy <= mActorAuthorityExteriorRadius; ++dy)
            {
                cells.insert(std::string("EXT:") + std::to_string(client.player.cell.gridX + dx)
                    + "," + std::to_string(client.player.cell.gridY + dy));
            }
        }
    }
    return cells;
}

void MPServer::sendActorStateToInterestedClients(const std::string& cellId)
{
    for (const auto& [conn, client] : mClients)
    {
        if (!clientHasActorCellLoaded(client, cellId))
            continue;

        sendActorAuthorityToClient(conn, cellId);
        // Full actor baselines are sent during initial cell bootstrap via
        // sendCellStateToClient. Routine authority refreshes must not trigger
        // redundant complete actor-list reconstruction for all observers.
    }
}

// ---------------------------------------------------------------------------
bool MPServer::isActorAuthorityLeaseValid(
    const ActorRegistryRecord& record, const std::string& cellId, uint64_t now)
{
    if (record.actorAuthorityGuid == 0)
        return false;
    if (now == 0)
        now = currentServerTimeMs();
    if (record.actorAuthorityLeaseUntilMs != 0 && now > record.actorAuthorityLeaseUntilMs)
        return false;

    ConnectedClient* owner = findClientByGuid(record.actorAuthorityGuid);
    if (!owner || !owner->charSelectComplete)
        return false;

    // Script-owned leases intentionally use the same configured safety radius
    // as target-player leases. If a follower falls farther behind, simulation
    // returns to cell authority instead of asking a client to drive an unloaded actor.
    return clientEligibleForActorCell(*owner, cellId);
}

bool MPServer::isAllowedActorSender(
    const ConnectedClient& sender, const ActorRegistryRecord& record, const std::string& cellId)
{
    if (isActorAuthorityLeaseValid(record, cellId))
        return record.actorAuthorityGuid == sender.guid;
    const auto cellIt = mWorld.actorCells.find(cellId);
    return cellIt != mWorld.actorCells.end() && cellIt->second.authorityGuid == sender.guid;
}

void MPServer::handleMechanicsSnapshot(ConnectedClient& c, const uint8_t* data, size_t size)
{
    MechanicsSnapshotBatch batch;
    PacketMechanicsSnapshot packet;
    packet.setBatch(&batch);
    if (!packet.decode(data, size))
    {
        Log(Debug::Warning) << "[Server] Rejecting malformed MechanicsSnapshot from " << c.name;
        return;
    }

    const std::uint64_t receivedAtMs = currentServerTimeMs();
    for (const MechanicsSnapshot& snapshot : batch.snapshots)
    {
        std::optional<MechanicsSnapshotExpectation> expected;
        if (snapshot.kind == MechanicsSubjectKind::Player)
        {
            MechanicsSnapshotExpectation player;
            player.subject = { MechanicsSubjectKind::Player, c.guid, 0 };
            player.cellId = makeCellKey(c.player.cell);
            // Player mechanics authority is scoped to the authenticated
            // connection. The connection GUID is its server-issued epoch; cell
            // mismatch independently invalidates snapshots after migration.
            player.migrationGeneration = 1;
            player.authorityGeneration = c.guid;
            player.authenticatedPlayerGuid = c.guid;
            expected = std::move(player);
        }
        else
        {
            const auto keyIt = mWorld.actorKeysByNetId.find(snapshot.actorInstanceId);
            if (keyIt != mWorld.actorKeysByNetId.end())
            {
                const auto locationIt = mWorld.actorLocations.find(keyIt->second);
                if (locationIt != mWorld.actorLocations.end())
                {
                    const auto cellIt = mWorld.actorCells.find(locationIt->second);
                    if (cellIt != mWorld.actorCells.end())
                    {
                        const auto actorIt = cellIt->second.actors.find(keyIt->second);
                        if (actorIt != cellIt->second.actors.end())
                        {
                            const ActorRegistryRecord& record = actorIt->second;
                            const ESM::RefId refId = ESM::RefId::stringRefId(record.actor.refId);
                            std::optional<MechanicsSubjectKind> canonicalKind;
                            if (mContentRegistry->store().get<ESM::NPC>().search(refId) != nullptr)
                                canonicalKind = MechanicsSubjectKind::Npc;
                            else if (mContentRegistry->store().get<ESM::Creature>().search(refId) != nullptr)
                                canonicalKind = MechanicsSubjectKind::Creature;

                            if (canonicalKind)
                            {
                                MechanicsSnapshotExpectation actor;
                                actor.subject = { *canonicalKind, 0, record.actorNetId };
                                actor.cellId = locationIt->second;
                                actor.migrationGeneration = record.migrationGeneration;
                                const bool leased = isActorAuthorityLeaseValid(record, locationIt->second,
                                    receivedAtMs);
                                actor.authorityGeneration = leased
                                    ? record.actorAuthorityGeneration : cellIt->second.authorityGeneration;
                                actor.actorSenderEntitled = isAllowedActorSender(c, record, locationIt->second);
                                expected = std::move(actor);
                            }
                        }
                    }
                }
            }
        }

        bool coordinateCellMismatch = false;
        if (expected && isExteriorCellKey(expected->cellId))
            coordinateCellMismatch = exteriorCellIdForPosition(snapshot.position) != expected->cellId;

        const MechanicsSnapshotError error = coordinateCellMismatch
            ? MechanicsSnapshotError::WrongCell
            : mMechanicsSnapshots.accept(snapshot, expected, receivedAtMs);
        if (error != MechanicsSnapshotError::None)
        {
            Log(Debug::Verbose) << "[Server] Rejected MechanicsSnapshot"
                                << " sender=" << c.name
                                << " kind=" << static_cast<unsigned>(snapshot.kind)
                                << " playerGuid=" << snapshot.playerGuid
                                << " actorNetId=" << snapshot.actorInstanceId
                                << " cell=" << snapshot.cellId
                                << " migrationGeneration=" << snapshot.migrationGeneration
                                << " authorityGeneration=" << snapshot.authorityGeneration
                                << " sequence=" << snapshot.snapshotSequence
                                << " error=" << static_cast<unsigned>(error);
        }
        else if (snapshot.kind == MechanicsSubjectKind::Player && mPlayerDb)
        {
            try
            {
                const bool isWerewolf = (snapshot.stateFlags & MechanicsWerewolf) != 0;
                const AcceptedMechanicsSnapshot* accepted = mMechanicsSnapshots.find(
                    { MechanicsSubjectKind::Player, c.guid, 0 });
                if (accepted)
                    processWerewolfExposure(c, *accepted, isWerewolf, receivedAtMs);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "[WerewolfExposure] state persistence failed player=" << c.name
                                  << " error=" << e.what();
            }
        }
    }
}

void MPServer::processWerewolfExposure(ConnectedClient& c, const AcceptedMechanicsSnapshot& offender,
    bool isWerewolf, std::uint64_t observedAtMs)
{
    if (!mPlayerDb || !mContentRegistry || !mObservationService)
        return;

    const WerewolfStateTransition current = mPlayerDb->loadWerewolfState(c.dbCharacterId);
    const bool changed = current.isWerewolf != isWerewolf;
    const bool transformed = changed && isWerewolf;
    if (changed && current.transition >= MaximumPersistedRevision)
        throw std::overflow_error("Werewolf transition counter overflow");
    const std::uint64_t transition = current.transition + (changed ? 1 : 0);

    std::optional<CrimeSemanticService::Outcome> preparedCrime;
    if (transformed)
    {
        constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
        CrimeWitnessBuildRequest request;
        request.eventCell = c.player.cell;
        request.offender = makeLiveObservationSnapshot(offender, playerBootWeight(c));
        request.alarmRadius = mObservationAlarmRadius;
        request.observedAtMs = observedAtMs;
        request.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
        CrimeWitnessBuildResult witnesses = buildLiveCrimeWitnesses(request);

        CrimeIntent intent;
        intent.eventId = "werewolf:" + std::to_string(c.dbCharacterId) + ':' + std::to_string(transition);
        intent.source = "validated_werewolf_transformation";
        intent.type = CrimeType::WerewolfExposure;
        intent.cellId = offender.snapshot.cellId;
        intent.offender = request.offender;
        intent.observedAtMs = observedAtMs;
        intent.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
        for (const std::string& cellId : witnesses.candidateCellIds)
        {
            const std::uint64_t generation = mCollisionWorld ? mCollisionWorld->cellGeneration(cellId) : 0;
            if (generation != 0)
                intent.collisionGenerations.push_back({ cellId, generation });
        }

        const auto gmstInt = [&](std::string_view id) {
            return mContentRegistry->store().get<ESM::GameSetting>().find(id)->mValue.getInteger();
        };
        CrimePolicy policy;
        policy.alarmRadius = mObservationAlarmRadius;
        policy.theftBountyMultiplier = mContentRegistry->store().get<ESM::GameSetting>()
            .find("fCrimeStealing")->mValue.getFloat();
        policy.pickpocketBounty = gmstInt("iCrimePickPocket");
        policy.trespassBounty = gmstInt("iCrimeTresspass");
        policy.assaultBounty = gmstInt("iCrimeAttack");
        policy.murderBounty = gmstInt("iCrimeKilling");
        policy.werewolfBounty = gmstInt("iWereWolfBounty");

        CrimeService crime(*mPlayerDb);
        CrimeSemanticService semantics(*mPlayerDb, crime, *mObservationService, policy);
        CrimeSemanticService::Context context { c.dbAccountId, c.dbCharacterId, c.guid };
        context.deferCommit = true;
        preparedCrime = semantics.evaluate(intent, std::move(witnesses.witnesses), context);
        if (!preparedCrime->result.accepted || (!preparedCrime->replayed && !preparedCrime->pendingCommit))
            throw std::runtime_error("Werewolf exposure semantic preparation failed");
    }

    const std::optional<CrimeMutationCommit> crimeMutation
        = preparedCrime && preparedCrime->pendingCommit ? preparedCrime->pendingCommit : std::nullopt;
    const WerewolfStateTransition committed
        = mPlayerDb->updateWerewolfState(c.dbCharacterId, isWerewolf, crimeMutation);
    if (committed.transition != transition || committed.transformed != transformed)
        throw std::runtime_error("Werewolf transition changed during atomic commit");
    c.player.isWerewolf = isWerewolf;

    if (preparedCrime)
    {
        c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
        c.player.bounty = c.player.crimeState.bounty;
        sendAuthoritativeCrimeState(c);
        syncLuaPlayerSnapshot();
        Log(preparedCrime->result.accepted ? Debug::Info : Debug::Warning)
            << "[WerewolfExposure] eventId=werewolf:" << c.dbCharacterId << ':' << transition
            << " player=" << c.name << " seen=" << preparedCrime->result.crimeSeen
            << " reported=" << preparedCrime->result.bountyApplied
            << " bountyDelta=" << preparedCrime->result.bountyDelta
            << " replayed=" << preparedCrime->replayed;
    }
}

void MPServer::updateActorAuthorityLeaseFromAi(const std::string& cellId,
    ActorRegistryRecord& record, const BaseActor& actor, uint64_t timestamp, const char* source)
{
    if (record.actorAuthorityReason == "script-owner")
        return;

    uint32_t targetGuid = 0;
    // Target-player leases are appropriate for cooperative locality (followers/escorts),
    // but not for hostile AI. Leasing a Combat/Pursue actor to the player it is
    // fighting makes the attacker authoritative over its own victim and destroys
    // the independent validation boundary required by authoritative combat results.
    const bool playerTargeted = actor.ai.type == BaseActor::AIAction::Type::Follow
        || actor.ai.type == BaseActor::AIAction::Type::Escort;
    constexpr std::string_view remotePlayerPrefix = "mp_remote_";
    if (playerTargeted && actor.ai.targetId.starts_with(remotePlayerPrefix))
    {
        const std::string_view guidText(actor.ai.targetId.data() + remotePlayerPrefix.size(),
            actor.ai.targetId.size() - remotePlayerPrefix.size());
        if (!guidText.empty())
        {
            uint32_t parsedGuid = 0;
            const auto [end, error]
                = std::from_chars(guidText.data(), guidText.data() + guidText.size(), parsedGuid);
            if (error == std::errc() && end == guidText.data() + guidText.size())
                targetGuid = parsedGuid;
        }
    }

    const uint32_t previousOwner = record.actorAuthorityGuid;
    const uint32_t previousTarget = record.actorAuthorityTargetGuid;
    const std::string previousReason = record.actorAuthorityReason;
    if (targetGuid != 0)
    {
        record.actorAuthorityGuid = targetGuid;
        record.actorAuthorityTargetGuid = targetGuid;
        record.actorAuthorityReason = "target-player";
        record.actorAuthorityLeaseUntilMs = 0;
        if (!isActorAuthorityLeaseValid(record, cellId, timestamp))
        {
            record.actorAuthorityGuid = 0;
            record.actorAuthorityTargetGuid = 0;
            record.actorAuthorityLeaseUntilMs = 0;
            record.actorAuthorityReason.clear();
        }
    }
    else if (record.actorAuthorityReason == "target-player" && !record.actor.isDead)
    {
        record.actorAuthorityGuid = 0;
        record.actorAuthorityTargetGuid = 0;
        record.actorAuthorityLeaseUntilMs = 0;
        record.actorAuthorityReason.clear();
    }

    if (record.actorAuthorityGuid == previousOwner
        && record.actorAuthorityTargetGuid == previousTarget
        && record.actorAuthorityReason == previousReason)
        return;

    ++record.actorAuthorityGeneration;
    broadcastActorAuthorityLease(cellId, record);
    Log(Debug::Info) << "[Server] Actor authority lease updated from AI"
                     << " source=" << (source ? source : "unknown")
                     << " actorNetId=" << record.actorNetId
                     << " refId=" << record.actor.refId
                     << " mpNum=" << record.actor.mpNum
                     << " cell=" << cellId
                     << " previousOwner=" << previousOwner
                     << " owner=" << record.actorAuthorityGuid
                     << " targetGuid=" << record.actorAuthorityTargetGuid
                     << " reason=" << record.actorAuthorityReason
                     << " generation=" << record.actorAuthorityGeneration;
}

void MPServer::broadcastActorAuthorityLease(
    const std::string& cellId, const ActorRegistryRecord& record)
{
    ActorList authority;
    authority.cellId = cellId;
    authority.isAuthority = record.actorAuthorityGuid != 0;
    authority.authorityGuid = record.actorAuthorityGuid;
    authority.authorityGeneration = record.actorAuthorityGeneration;
    authority.serverTimestamp = currentServerTimeMs();
    authority.actors.push_back(record.actor);

    PacketActorAuthority packet;
    packet.setActorList(&authority);
    const std::vector<uint8_t> encoded = packet.encode();
    const ActorInstanceId actorNetId = record.actorNetId != 0
        ? record.actorNetId : actorInstanceIdFromActor(record.actor);
    const std::string leaseState = cellId + "|" + std::to_string(record.actorAuthorityGuid)
        + "|" + std::to_string(record.actorAuthorityGeneration);
    std::size_t recipients = 0;
    for (auto& [conn, client] : mClients)
    {
        const bool knewActor = record.actorNetId != 0
            && (client.actorV2IdentitySent.count(record.actorNetId) != 0
                || client.actorV2IdentityAcked.count(record.actorNetId) != 0);
        if (client.guid == record.actorAuthorityGuid
            || clientEligibleForActorCell(client, cellId)
            || knewActor)
        {
            if (actorNetId != 0)
            {
                const auto sentIt = client.actorAuthorityLeaseStateSent.find(actorNetId);
                if (sentIt != client.actorAuthorityLeaseStateSent.end() && sentIt->second == leaseState)
                    continue;
            }
            sendTo(conn, encoded);
            if (actorNetId != 0)
                client.actorAuthorityLeaseStateSent[actorNetId] = leaseState;
            ++recipients;
        }
    }
    if (recipients != 0)
    {
        Log(Debug::Info) << "[Server] Actor authority lease broadcast"
                         << " actorNetId=" << record.actorNetId
                         << " refId=" << record.actor.refId
                         << " mpNum=" << record.actor.mpNum
                         << " cell=" << cellId
                         << " owner=" << record.actorAuthorityGuid
                         << " reason=" << record.actorAuthorityReason
                         << " generation=" << record.actorAuthorityGeneration
                         << " recipients=" << recipients;
    }
}

void MPServer::broadcastActorAuthorityLeasesForCell(
    const std::string& cellId, CellActorState& cellState)
{
    const uint64_t now = currentServerTimeMs();
    for (const auto& [key, record] : cellState.actors)
    {
        if (record.actorAuthorityGuid == 0 || !isActorAuthorityLeaseValid(record, cellId, now))
            continue;
        broadcastActorAuthorityLease(cellId, record);
    }
}

// ---------------------------------------------------------------------------
bool MPServer::validateActorUpdate(const ConnectedClient& c, const ActorList& actorList, const char* packetName)
{
    if (actorList.cellId.empty())
    {
        Log(Debug::Warning) << "[Server] Rejecting " << packetName << " from " << c.name
                            << " because the actor cellId is empty";
        return false;
    }

    auto cellIt = mWorld.actorCells.find(actorList.cellId);
    if (cellIt == mWorld.actorCells.end() || cellIt->second.authorityGuid == 0)
    {
        refreshActorAuthorityForCell(actorList.cellId, c.guid);
        cellIt = mWorld.actorCells.find(actorList.cellId);
    }

    const uint32_t cellAuthorityGuid
        = cellIt != mWorld.actorCells.end() ? cellIt->second.authorityGuid : 0;
    bool allActorsLeasedToSender = !actorList.actors.empty() && cellIt != mWorld.actorCells.end();
    if (allActorsLeasedToSender)
    {
        for (const BaseActor& actor : actorList.actors)
        {
            const auto recordIt = cellIt->second.actors.find(makeActorKey(actor));
            if (recordIt == cellIt->second.actors.end()
                || !isActorAuthorityLeaseValid(recordIt->second, actorList.cellId)
                || recordIt->second.actorAuthorityGuid != c.guid)
            {
                allActorsLeasedToSender = false;
                break;
            }
        }
    }

    if (std::strcmp(packetName, "ActorPosition") != 0
        && !clientHasActorCellLoaded(c, actorList.cellId)
        && !allActorsLeasedToSender)
    {
        Log(Debug::Warning) << "[Server] Rejecting " << packetName << " from " << c.name
                            << " because actor cell is not eligible"
                            << " sender=" << c.guid
                            << " playerCell=" << makeCellKey(c.player.cell)
                            << " packetCell=" << actorList.cellId
                            << " cellAuthority=" << cellAuthorityGuid
                            << " allActorsLeasedToSender=" << allActorsLeasedToSender;
        return false;
    }

    bool senderAllowed = false;
    uint32_t rejectedLeaseOwner = 0;
    if (actorList.actors.empty())
        senderAllowed = cellAuthorityGuid == c.guid;
    else if (cellIt != mWorld.actorCells.end())
    {
        senderAllowed = true;
        for (const BaseActor& actor : actorList.actors)
        {
            const auto recordIt = cellIt->second.actors.find(makeActorKey(actor));
            if (recordIt == cellIt->second.actors.end())
            {
                if (cellAuthorityGuid != c.guid)
                {
                    senderAllowed = false;
                    break;
                }
                continue;
            }
            if (!isAllowedActorSender(c, recordIt->second, actorList.cellId))
            {
                if (isActorAuthorityLeaseValid(recordIt->second, actorList.cellId))
                    rejectedLeaseOwner = recordIt->second.actorAuthorityGuid;
                senderAllowed = false;
                break;
            }
        }
    }

    if (!senderAllowed)
    {
        Log(Debug::Warning) << "[Server] Rejecting " << packetName << " from " << c.name
                            << " because sender is not actor authority"
                            << " sender=" << c.guid
                            << " cell=" << actorList.cellId
                            << " leaseOwner=" << rejectedLeaseOwner
                            << " cellAuthority=" << cellAuthorityGuid;
        return false;
    }

    return true;
}

MPServer::ActorRegistryRecord* MPServer::findTrackedActor(CellActorState& cellState,
    const BaseActor& actor,
    ConnectedClient& sender,
    const char* packetName)
{
    const auto it = cellState.actors.find(makeActorKey(actor));
    if (it != cellState.actors.end())
        return &it->second;

    const bool noisyPacket = std::strcmp(packetName, "ActorPosition") == 0
        || std::strcmp(packetName, "ActorAttack") == 0;
    if (noisyPacket)
    {
        Log(Debug::Verbose) << "[Server] Ignoring late " << packetName
                            << " from " << sender.name
                            << " for unknown actor refId=" << actor.refId
                            << " refNum=" << actor.refNum
                            << " mpNum=" << actor.mpNum
                            << " cell=" << actor.cellId;
        return nullptr;
    }

    // Unknown reliable actor updates can repeat every frame when an old client
    // keeps a dead/removed vanilla actor in its authority scan.  Keep the first
    // diagnostic useful without turning that condition into synchronous log I/O
    // hundreds of times per second.
    const std::string logKey = std::string(packetName) + '\n' + actor.cellId + '\n' + makeActorKey(actor);
    const uint64_t nowMs = currentServerTimeMs();
    if (sender.lastUnknownActorLogMs.size() >= 1024
        && sender.lastUnknownActorLogMs.find(logKey) == sender.lastUnknownActorLogMs.end())
        sender.lastUnknownActorLogMs.clear();
    uint64_t& lastLogMs = sender.lastUnknownActorLogMs[logKey];
    if (lastLogMs == 0 || nowMs - lastLogMs >= 2000)
    {
        lastLogMs = nowMs;
        Log(Debug::Info) << "[Server] Ignoring late " << packetName
                         << " from " << sender.name
                         << " for unknown actor refId=" << actor.refId
                         << " refNum=" << actor.refNum
                         << " mpNum=" << actor.mpNum
                         << " cell=" << actor.cellId;
    }
    return nullptr;
}

void MPServer::rememberActorLocation(const BaseActor& actor, const std::string& cellId)
{
    if (cellId.empty() || (actor.mpNum == 0 && actor.refId.empty()))
        return;

    mWorld.actorLocations[makeActorKey(actor)] = cellId;
}

void MPServer::forgetActorLocation(const BaseActor& actor, const std::string& cellId)
{
    if (actor.mpNum == 0 && actor.refId.empty())
        return;

    const std::string actorKey = makeActorKey(actor);
    auto locationIt = mWorld.actorLocations.find(actorKey);
    if (locationIt == mWorld.actorLocations.end())
        return;

    if (!cellId.empty() && locationIt->second != cellId)
        return;

    mWorld.actorLocations.erase(locationIt);
}

void MPServer::rememberDeadVanillaActor(const ActorRegistryRecord& record)
{
    if (record.actor.mpNum != 0 || !record.actor.isDead || record.actor.refId.empty() || record.actor.cellId.empty())
        return;

    ActorRegistryRecord remembered = record;
    remembered.actor.cellId = record.actor.cellId;
    ensureActorNetId(remembered, remembered.actor.cellId);
    const std::string actorKey = makeActorKey(remembered.actor);
    const ActorInstanceId actorNetId = actorInstanceIdFromActor(remembered.actor);
    bool changed = false;

    // A stable vanilla/spawner refNum may only describe one creature species.
    // Remove stale rows produced by an earlier local leveled-list roll before
    // remembering the authoritative corpse.
    if (actorNetId != 0)
    {
        for (auto& [deadCellId, deadActors] : mWorld.deadVanillaActorCells)
        {
            for (auto deadIt = deadActors.begin(); deadIt != deadActors.end();)
            {
                if (deadIt->first == actorKey
                    || actorInstanceIdFromActor(deadIt->second.actor) != actorNetId)
                {
                    ++deadIt;
                    continue;
                }

                if (mPlayerDb)
                {
                    mPlayerDb->deleteDeadVanillaActor(
                        deadIt->second.actor.refId, deadIt->second.actor.refNum);
                }
                forgetActorLocation(deadIt->second.actor, deadCellId);
                deadIt = deadActors.erase(deadIt);
                changed = true;
            }
        }
    }

    for (auto cellIt = mWorld.deadVanillaActorCells.begin(); cellIt != mWorld.deadVanillaActorCells.end();)
    {
        if (cellIt->first == remembered.actor.cellId)
        {
            ++cellIt;
            continue;
        }

        if (cellIt->second.erase(actorKey) != 0)
            changed = true;
        if (cellIt->second.empty())
            cellIt = mWorld.deadVanillaActorCells.erase(cellIt);
        else
            ++cellIt;
    }

    auto& deadActors = mWorld.deadVanillaActorCells[remembered.actor.cellId];
    const auto previousIt = deadActors.find(actorKey);
    if (previousIt == deadActors.end()
        || !sameDeadVanillaActorState(previousIt->second.actor, remembered.actor))
        changed = true;

    deadActors[actorKey] = remembered;
    rememberActorLocation(remembered.actor, remembered.actor.cellId);

    for (auto& [cellId, cellState] : mWorld.actorCells)
    {
        for (auto actorIt = cellState.actors.begin(); actorIt != cellState.actors.end();)
        {
            const bool sameKeyInDestination
                = cellId == remembered.actor.cellId && actorIt->first == actorKey;
            const bool conflictingIdentity = actorNetId != 0
                && actorIt->second.actor.mpNum == 0
                && actorInstanceIdFromActor(actorIt->second.actor) == actorNetId
                && actorIt->first != actorKey;
            const bool staleSameActorInOtherCell = cellId != remembered.actor.cellId
                && actorIt->first == actorKey && actorIt->second.actor.mpNum == 0;
            if (sameKeyInDestination || (!conflictingIdentity && !staleSameActorInOtherCell))
            {
                ++actorIt;
                continue;
            }

            forgetActorLocation(actorIt->second.actor, cellId);
            cellState.staleLiveVanillaDeathResendMs.erase(actorIt->first);
            actorIt = cellState.actors.erase(actorIt);
            changed = true;

            Log(Debug::Verbose) << "[Server] Removed stale vanilla record for canonical corpse"
                                << " refId=" << remembered.actor.refId
                                << " refNum=" << remembered.actor.refNum
                                << " liveCell=" << cellId
                                << " deadCell=" << remembered.actor.cellId;
        }
    }

    if (changed && mPlayerDb)
        mPlayerDb->upsertDeadVanillaActor(remembered.actor);
}

void MPServer::forgetDeadVanillaActor(const BaseActor& actor, const std::string& cellId)
{
    if (actor.mpNum != 0 || actor.refId.empty())
        return;

    const std::string actorKey = makeActorKey(actor);
    if (mPlayerDb)
        mPlayerDb->deleteDeadVanillaActor(actor.refId, actor.refNum);

    auto eraseFromCell = [&](const std::string& targetCellId) -> bool
    {
        auto cellIt = mWorld.deadVanillaActorCells.find(targetCellId);
        if (cellIt == mWorld.deadVanillaActorCells.end())
            return false;

        const bool erased = cellIt->second.erase(actorKey) != 0;
        if (cellIt->second.empty())
            mWorld.deadVanillaActorCells.erase(cellIt);
        return erased;
    };

    if (!cellId.empty())
    {
        eraseFromCell(cellId);
        return;
    }

    if (!actor.cellId.empty() && eraseFromCell(actor.cellId))
        return;

    for (auto cellIt = mWorld.deadVanillaActorCells.begin(); cellIt != mWorld.deadVanillaActorCells.end();)
    {
        cellIt->second.erase(actorKey);
        if (cellIt->second.empty())
            cellIt = mWorld.deadVanillaActorCells.erase(cellIt);
        else
            ++cellIt;
    }
}

const MPServer::ActorRegistryRecord* MPServer::findDeadVanillaActor(
    const BaseActor& actor, std::string* cellId) const
{
    if (actor.mpNum != 0 || actor.refId.empty())
        return nullptr;

    const std::string actorKey = makeActorKey(actor);
    auto findInCell = [&](const std::string& targetCellId) -> const ActorRegistryRecord*
    {
        const auto cellIt = mWorld.deadVanillaActorCells.find(targetCellId);
        if (cellIt == mWorld.deadVanillaActorCells.end())
            return nullptr;

        const auto actorIt = cellIt->second.find(actorKey);
        if (actorIt == cellIt->second.end() || !actorIt->second.actor.isDead)
            return nullptr;

        if (cellId)
            *cellId = targetCellId;
        return &actorIt->second;
    };

    if (!actor.cellId.empty())
    {
        if (const ActorRegistryRecord* record = findInCell(actor.cellId))
            return record;
    }

    for (const auto& [targetCellId, actors] : mWorld.deadVanillaActorCells)
    {
        const auto actorIt = actors.find(actorKey);
        if (actorIt == actors.end() || !actorIt->second.actor.isDead)
            continue;

        if (cellId)
            *cellId = targetCellId;
        return &actorIt->second;
    }

    return nullptr;
}

void MPServer::rememberDisposedVanillaActor(const BaseActor& actor)
{
    if (actor.mpNum != 0 || actor.refId.empty() || actor.cellId.empty())
        return;

    BaseActor tombstone = actor;
    tombstone.mpNum = 0;
    tombstone.isDead = false;
    const std::string actorKey = makeActorKey(tombstone);
    mWorld.disposedVanillaActors[actorKey] = tombstone;
    if (mPlayerDb)
        mPlayerDb->upsertDisposedVanillaActor(tombstone);
}

const BaseActor* MPServer::findDisposedVanillaActor(const BaseActor& actor) const
{
    if (actor.mpNum != 0 || actor.refId.empty())
        return nullptr;

    const auto it = mWorld.disposedVanillaActors.find(makeActorKey(actor));
    return it != mWorld.disposedVanillaActors.end() ? &it->second : nullptr;
}

bool MPServer::rejectStaleAliveVanillaActor(
    const BaseActor& actor,
    const std::string& incomingCellId,
    const ConnectedClient& sender,
    const char* packetName) const
{
    if (actor.mpNum != 0 || actor.refId.empty())
        return false;

    if (const BaseActor* disposed = findDisposedVanillaActor(actor))
    {
        const bool importantPacket = std::strcmp(packetName, "ActorList") == 0
            || std::strcmp(packetName, "ActorAttack") == 0
            || std::strcmp(packetName, "ActorCellChange") == 0;
        Log(importantPacket ? Debug::Info : Debug::Verbose) << "[Server] " << packetName
                            << " ignored disposed vanilla actor from " << sender.name
                            << " refId=" << actor.refId
                            << " refNum=" << actor.refNum
                            << " incomingCell=" << incomingCellId
                            << " disposedCell=" << disposed->cellId;
        return true;
    }

    if (actor.isDead)
        return false;

    std::string deadCellId;
    const ActorRegistryRecord* deadRecord = findDeadVanillaActor(actor, &deadCellId);
    if (!deadRecord)
        return false;

    const bool importantPacket = std::strcmp(packetName, "ActorList") == 0
        || std::strcmp(packetName, "ActorAttack") == 0
        || std::strcmp(packetName, "ActorCellChange") == 0;
    Log(importantPacket ? Debug::Info : Debug::Verbose) << "[Server] " << packetName
                        << " ignored stale alive vanilla actor from " << sender.name
                        << " refId=" << actor.refId
                        << " refNum=" << actor.refNum
                        << " incomingCell=" << incomingCellId
                        << " deadCell=" << deadCellId
                        << " deadHp=" << deadRecord->actor.dynamicStats.health.current;
    return true;
}

bool MPServer::rejectResetStaleDeadVanillaActor(
    const BaseActor& actor,
    const std::string& incomingCellId,
    const ConnectedClient& sender,
    const char* packetName) const
{
    if (actor.mpNum != 0 || actor.refId.empty() || !actor.isDead)
        return false;

    const auto cellIt = mWorld.actorCells.find(incomingCellId);
    if (cellIt == mWorld.actorCells.end())
        return false;

    const std::string actorKey = makeActorKey(actor);
    if (cellIt->second.resetSuppressedVanillaDeaths.count(actorKey) == 0)
        return false;

    const bool importantPacket = std::strcmp(packetName, "ActorList") == 0
        || std::strcmp(packetName, "ActorDeath") == 0
        || std::strcmp(packetName, "ActorStatsDynamic") == 0;
    Log(importantPacket ? Debug::Info : Debug::Verbose) << "[Server] " << packetName
                        << " ignored stale dead vanilla actor after reset"
                        << " from=" << sender.name
                        << " refId=" << actor.refId
                        << " refNum=" << actor.refNum
                        << " incomingCell=" << incomingCellId;
    return true;
}

void MPServer::clearResetStaleDeathSuppressionForAliveVanillaActor(
    const BaseActor& actor,
    const std::string& incomingCellId)
{
    if (actor.mpNum != 0 || actor.refId.empty() || actor.isDead)
        return;

    auto cellIt = mWorld.actorCells.find(incomingCellId);
    if (cellIt == mWorld.actorCells.end() || cellIt->second.resetSuppressedVanillaDeaths.empty())
        return;

    const std::string actorKey = makeActorKey(actor);
    cellIt->second.resetSuppressedVanillaDeaths.erase(actorKey);
}

std::size_t MPServer::mergeDeadVanillaActorsForCell(
    const std::string& cellId,
    std::unordered_map<std::string, ActorRegistryRecord>& actors) const
{
    const auto cellIt = mWorld.deadVanillaActorCells.find(cellId);
    if (cellIt == mWorld.deadVanillaActorCells.end())
        return 0;

    std::size_t merged = 0;
    for (const auto& [actorKey, record] : cellIt->second)
    {
        const ActorInstanceId actorNetId = actorInstanceIdFromActor(record.actor);
        const bool identityAlreadyPresent = actorNetId != 0
            && std::any_of(actors.begin(), actors.end(), [&](const auto& entry) {
                return actorInstanceIdFromActor(entry.second.actor) == actorNetId;
            });
        if (identityAlreadyPresent && actors.find(actorKey) == actors.end())
            continue;

        auto actorIt = actors.find(actorKey);
        if (actorIt == actors.end() || !actorIt->second.actor.isDead)
            ++merged;

        ActorRegistryRecord remembered = record;
        remembered.actor.cellId = cellId;
        actors[actorKey] = std::move(remembered);
    }
    return merged;
}

ActorInstanceId MPServer::assignActorNetId(const BaseActor& actor)
{
    const ActorInstanceId actorNetId = actorInstanceIdFromActor(actor);
    if (actorNetId == 0)
        return 0;

    const std::string actorKey = makeActorKey(actor);
    mWorld.actorNetIdsByKey[actorKey] = actorNetId;
    mWorld.actorKeysByNetId[actorNetId] = actorKey;
    return actorNetId;
}

ActorInstanceId MPServer::ensureActorNetId(ActorRegistryRecord& record, const std::string& cellId)
{
    if (!cellId.empty())
        record.actor.cellId = cellId;

    const ActorInstanceId expectedActorNetId = actorInstanceIdFromActor(record.actor);
    if (expectedActorNetId == 0)
        return 0;

    ensureCanonicalActorMigrationGeneration(record.migrationGeneration, record.actor);

    if (record.actorNetId != 0 && record.actorNetId != expectedActorNetId)
    {
        mWorld.actorKeysByNetId.erase(record.actorNetId);
        mWorld.actorNetIdsByKey.erase(makeActorKey(record.actor));
        record.actorNetId = 0;
    }

    if (record.actorNetId == 0)
        record.actorNetId = assignActorNetId(record.actor);
    return record.actorNetId;
}

void MPServer::forgetActorNetId(ActorInstanceId actorNetId, const BaseActor& actor)
{
    if (actorNetId == 0)
        return;

    mMechanicsSnapshots.erase({ MechanicsSubjectKind::Npc, 0, actorNetId });
    mMechanicsSnapshots.erase({ MechanicsSubjectKind::Creature, 0, actorNetId });
    if (mObservationService)
    {
        mObservationService->invalidateObserver({ ObservationActorKind::Npc, 0, actorNetId });
        mObservationService->invalidateObserver({ ObservationActorKind::Creature, 0, actorNetId });
    }

    const auto keyIt = mWorld.actorKeysByNetId.find(actorNetId);
    if (keyIt != mWorld.actorKeysByNetId.end())
    {
        mWorld.actorNetIdsByKey.erase(keyIt->second);
        mWorld.actorKeysByNetId.erase(keyIt);
    }
    else
    {
        mWorld.actorNetIdsByKey.erase(makeActorKey(actor));
    }

    for (auto& clientEntry : mClients)
    {
        ConnectedClient& client = clientEntry.second;
        client.actorV2IdentitySent.erase(actorNetId);
        client.actorV2IdentityAcked.erase(actorNetId);
        client.actorV2LastSentMs.erase(actorNetId);
        client.actorV2MissingIdentityByNetIdWindow.erase(actorNetId);
    }
}

ActorIdentityList MPServer::buildActorIdentityList(
    const std::string& cellId,
    CellActorState& cellState,
    std::unordered_map<std::string, ActorRegistryRecord>& actors)
{
    std::size_t missingIdentity = 0;
    std::size_t ambiguousIdentityNormalized = 0;
    std::size_t unmanagedSpawnerPruned = 0;
    std::size_t duplicateIdentityDropped = 0;
    std::vector<std::string> unmanagedSpawnerKeys;
    for (auto& [actorKey, record] : cellState.actors)
    {
        if (normalizeActorIdentity(record.actor))
            ++ambiguousIdentityNormalized;
        if (isUnmanagedSpawnerActor(record.actor))
        {
            unmanagedSpawnerKeys.push_back(actorKey);
            continue;
        }
        ensureActorNetId(record, cellId);
    }
    for (const std::string& actorKey : unmanagedSpawnerKeys)
    {
        auto actorIt = cellState.actors.find(actorKey);
        if (actorIt != cellState.actors.end())
        {
            forgetActorLocation(actorIt->second.actor, cellId);
            cellState.actors.erase(actorIt);
            ++unmanagedSpawnerPruned;
        }
    }

    ActorIdentityList identityList;
    identityList.protocolVersion = ActorSyncProtocolVersionV2;
    identityList.cellId = cellId;
    identityList.authorityGuid = cellState.authorityGuid;
    identityList.authorityGeneration = cellState.authorityGeneration;
    identityList.sequence = cellState.nextSnapshotSequence;
    identityList.serverTimestamp = currentServerTimeMs();
    identityList.completeCellSnapshot = cellState.hasCompleteAuthoritySnapshot;
    identityList.actors.reserve(actors.size());
    std::unordered_set<ActorInstanceId> emittedActorNetIds;

    for (auto& [actorKey, record] : actors)
    {
        if (normalizeActorIdentity(record.actor))
            ++ambiguousIdentityNormalized;
        if (hasMissingActorInstanceIdentity(record.actor))
        {
            ++missingIdentity;
            continue;
        }
        if (isUnmanagedSpawnerActor(record.actor))
        {
            ++unmanagedSpawnerPruned;
            continue;
        }

        const ActorInstanceId actorNetId = ensureActorNetId(record, cellId);
        if (actorNetId == 0)
        {
            ++missingIdentity;
            continue;
        }
        if (!emittedActorNetIds.insert(actorNetId).second)
        {
            ++duplicateIdentityDropped;
            continue;
        }

        ActorIdentityRecord identity;
        identity.actorNetId = actorNetId;
        identity.persistent = record.persistent;
        identity.serverSpawned = record.actor.mpNum != 0;
        identity.migrationGeneration = record.migrationGeneration;
        identity.actor = record.actor;
        identity.actor.cellId = cellId;
        identityList.actors.push_back(std::move(identity));
    }

    if (missingIdentity != 0 || ambiguousIdentityNormalized != 0 || unmanagedSpawnerPruned != 0
        || duplicateIdentityDropped != 0)
    {
        const bool importantIdentityRepair = missingIdentity != 0 || ambiguousIdentityNormalized != 0;
        Log(importantIdentityRepair ? Debug::Info : Debug::Verbose)
            << "[Server] ActorIdentity normalized"
            << " cell=" << cellId
            << " sent=" << identityList.actors.size()
            << " missingIdentity=" << missingIdentity
            << " ambiguousIdentityNormalized=" << ambiguousIdentityNormalized
            << " unmanagedSpawnerPruned=" << unmanagedSpawnerPruned
            << " duplicateIdentityDropped=" << duplicateIdentityDropped;
    }

    return identityList;
}

std::optional<MPServer::ActorRegistryRecord> MPServer::removeActorFromOtherCells(
    const BaseActor& actor,
    const std::string& destinationCellId,
    std::unordered_set<std::string>& changedCellIds)
{
    if (actor.mpNum == 0 && actor.refId.empty())
        return std::nullopt;

    const std::string actorKey = makeActorKey(actor);
    std::optional<ActorRegistryRecord> migratedRecord;
    bool removedSpawnedActorLink = false;

    auto removeFromCell = [&](const std::string& cellId, CellActorState& cellState)
    {
        if (cellId == destinationCellId)
            return;

        auto actorIt = cellState.actors.find(actorKey);
        if (actorIt == cellState.actors.end())
            return;

        ActorRegistryRecord removedRecord = actorIt->second;
        if (!migratedRecord || removedRecord.lastSnapshotTime >= migratedRecord->lastSnapshotTime)
            migratedRecord = removedRecord;

        if (mPlayerDb && removedRecord.actor.mpNum != 0)
        {
            mPlayerDb->deleteSpawnedActorDynamicRecordLink(removedRecord.actor.mpNum, cellId);
            removedSpawnedActorLink = true;
        }

        cellState.actors.erase(actorIt);
        forgetActorLocation(removedRecord.actor, cellId);
        changedCellIds.insert(cellId);

        Log(removedRecord.actor.mpNum != 0 ? Debug::Info : Debug::Verbose)
            << "[Server] Actor migrated between cells"
            << " refId=" << actor.refId
            << " refNum=" << actor.refNum
            << " mpNum=" << actor.mpNum
            << " from=" << cellId
            << " to=" << destinationCellId;
    };

    const auto locationIt = mWorld.actorLocations.find(actorKey);
    std::string indexedCellId;
    if (locationIt != mWorld.actorLocations.end())
    {
        indexedCellId = locationIt->second;
        auto cellIt = mWorld.actorCells.find(indexedCellId);
        if (cellIt != mWorld.actorCells.end())
            removeFromCell(indexedCellId, cellIt->second);
        else if (indexedCellId != destinationCellId)
            mWorld.actorLocations.erase(locationIt);
    }

    // Fallback scan cleans up duplicate records from older builds or from any
    // path that inserted actor state before the location index existed.
    for (auto& [cellId, cellState] : mWorld.actorCells)
    {
        if (cellId == indexedCellId)
            continue;
        removeFromCell(cellId, cellState);
    }

    if (removedSpawnedActorLink)
        scheduleGeneratedDynamicRecordGc("actor_migration_unlink");

    return migratedRecord;
}

void MPServer::broadcastActorListForCell(const std::string& cellId, CellActorState& cellState)
{
    std::unordered_map<std::string, ActorRegistryRecord> actors = cellState.actors;
    mergeDeadVanillaActorsForCell(cellId, actors);

    ActorList actorList;
    actorList.cellId = cellId;
    actorList.isAuthority = false;
    actorList.authorityGuid = cellState.authorityGuid;
    actorList.authorityGeneration = cellState.authorityGeneration;
    actorList.snapshotSequence = cellState.nextSnapshotSequence++;
    actorList.serverTimestamp = currentServerTimeMs();
    actorList.actors.reserve(actors.size());
    for (const auto& [actorKey, record] : actors)
        actorList.actors.push_back(record.actor);

    PacketActorList pkt;
    pkt.setActorList(&actorList);
    broadcastActorIdentityForCell(cellId, cellState);
    broadcastActorToCell(cellId, pkt.encode());
    broadcastActorAuthorityLeasesForCell(cellId, cellState);
}

void MPServer::broadcastActorPositionV2ToCell(
    const std::string& cellId, CellActorState& cellState, const ActorList& actorList, HSteamNetConnection except)
{
    static constexpr std::size_t kSnapshotCostBytes = 42;
    static constexpr std::size_t kBudgetBytes = 900;

    const uint64_t now = currentServerTimeMs();
    auto tierForActor = [](const BaseActor& actor) -> std::size_t
    {
        if (actor.isDead)
            return 4;
        if (actor.isAttackingOrCasting
            || (actor.animFlags.actionFlags & (AnimFlags::AF_ATTACKING | AnimFlags::AF_CASTING)) != 0)
            return 0;
        const float speedSq = actor.velocity.linear[0] * actor.velocity.linear[0]
            + actor.velocity.linear[1] * actor.velocity.linear[1]
            + actor.velocity.linear[2] * actor.velocity.linear[2];
        if (actor.isMoving || speedSq > 25.f)
            return 1;
        return 2;
    };
    auto intervalForTier = [](std::size_t tier) -> uint64_t
    {
        switch (tier)
        {
            case 0: return 25;
            case 1: return 25;
            case 2: return 250;
            case 3: return 1000;
            default: return 0;
        }
    };

    for (auto& [conn, client] : mClients)
    {
        if (conn == except
            || !clientHasActorCellLoaded(client, cellId)
            || client.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
            continue;

        ActorPositionV2List positionList;
        positionList.protocolVersion = ActorSyncProtocolVersionV2;
        positionList.authorityGuid = actorList.authorityGuid;
        positionList.authorityGeneration = actorList.authorityGeneration;
        positionList.sequence = actorList.snapshotSequence;
        positionList.serverTimestamp = actorList.serverTimestamp;

        std::size_t budgetUsed = 0;
        for (const BaseActor& actor : actorList.actors)
        {
            auto actorIt = cellState.actors.find(makeActorKey(actor));
            if (actorIt == cellState.actors.end())
                continue;

            ActorRegistryRecord& record = actorIt->second;
            const ActorInstanceId actorNetId = ensureActorNetId(record, cellId);
            if (actorNetId == 0)
                continue;

            if (client.actorV2IdentitySent.count(actorNetId) == 0
                || client.actorV2IdentityAcked.count(actorNetId) == 0)
            {
                ++client.actorV2PositionSuppressedUntilIdentityKnownWindow;
                ++client.actorV2MissingIdentityByNetIdWindow[actorNetId];
                continue;
            }

            const std::size_t tier = tierForActor(actor);
            if (tier < (sizeof(client.actorV2TierCounts) / sizeof(client.actorV2TierCounts[0])))
                ++client.actorV2TierCounts[tier];

            const uint64_t intervalMs = intervalForTier(tier);
            const bool forceSend = actor.position.isTeleporting || tier == 0;
            const uint64_t lastSent = client.actorV2LastSentMs[actorNetId];
            if (!forceSend && intervalMs == 0)
            {
                ++client.actorV2DeferredWindow;
                continue;
            }
            if (!forceSend && lastSent != 0 && now - lastSent < intervalMs)
            {
                ++client.actorV2DeferredWindow;
                continue;
            }
            if (budgetUsed + kSnapshotCostBytes > kBudgetBytes)
            {
                ++client.actorV2DeferredWindow;
                continue;
            }

            positionList.snapshots.push_back(makeCompactActorSnapshot(actor, actorNetId));
            positionList.snapshots.back().migrationGeneration = record.migrationGeneration;
            client.actorV2LastSentMs[actorNetId] = now;
            budgetUsed += kSnapshotCostBytes;
        }

        if (!positionList.snapshots.empty())
        {
            PacketActorPositionV2 pkt;
            pkt.setPositionList(&positionList);
            const std::vector<uint8_t> encoded = pkt.encode();
            sendTo(conn, encoded, /*reliable=*/false);
            client.actorV2SnapshotsSentWindow += positionList.snapshots.size();
            client.actorV2BytesSentWindow += encoded.size();
        }

        if (client.actorV2DiagnosticsLastLogMs == 0)
            client.actorV2DiagnosticsLastLogMs = now;
        else if (now - client.actorV2DiagnosticsLastLogMs >= 1000)
        {
            ActorInstanceId noisiestMissingActorNetId = 0;
            std::size_t noisiestMissingCount = 0;
            for (const auto& [actorNetId, count] : client.actorV2MissingIdentityByNetIdWindow)
            {
                if (count > noisiestMissingCount)
                {
                    noisiestMissingActorNetId = actorNetId;
                    noisiestMissingCount = count;
                }
            }
            const bool logAtInfo = client.actorV2IdentitySentWindow != 0
                || client.actorV2IdentityAckedWindow != 0
                || client.actorV2PositionSuppressedUntilIdentityKnownWindow != 0
                || client.actorV2PresentationSuppressedUntilIdentityKnownWindow != 0
                || client.actorV2AttackSuppressedUntilIdentityKnownWindow != 0
                || noisiestMissingCount != 0
                || client.actorV2BytesSentWindow > 12000
                || client.actorV2DeferredWindow > 1200;

            if (logAtInfo)
            {
                Log(Debug::Info) << "[Server] ActorSync v2 budget"
                                 << " receiver=" << client.guid
                                 << " cell=" << cellId
                                 << " interested=" << actorList.actors.size()
                                 << " identitySent=" << client.actorV2IdentitySentWindow
                                 << " identityAcked=" << client.actorV2IdentityAckedWindow
                                 << " snapshots=" << client.actorV2SnapshotsSentWindow
                                 << " bytes=" << client.actorV2BytesSentWindow
                                 << " presentationSent=" << client.actorV2PresentationSentWindow
                                 << " presentationBytes=" << client.actorV2PresentationBytesSentWindow
                                 << " attackSent=" << client.actorV2AttackSentWindow
                                 << " attackSuppressedUntilIdentityKnown=" << client.actorV2AttackSuppressedUntilIdentityKnownWindow
                                 << " budget=" << kBudgetBytes
                                 << " deferred=" << client.actorV2DeferredWindow
                                 << " positionSuppressedUntilIdentityKnown=" << client.actorV2PositionSuppressedUntilIdentityKnownWindow
                                 << " presentationSuppressedUntilIdentityKnown=" << client.actorV2PresentationSuppressedUntilIdentityKnownWindow
                                 << " missingIdentityActorNetId=" << noisiestMissingActorNetId
                                 << " missingIdentityActorKey=" << describeActorInstanceId(noisiestMissingActorNetId)
                                 << " missingIdentityCount=" << noisiestMissingCount
                                 << " tier0=" << client.actorV2TierCounts[0]
                                 << " tier1=" << client.actorV2TierCounts[1]
                                 << " tier2=" << client.actorV2TierCounts[2]
                                 << " tier3=" << client.actorV2TierCounts[3]
                                 << " tier4=" << client.actorV2TierCounts[4];
            }
            client.actorV2DiagnosticsLastLogMs = now;
            client.actorV2IdentitySentWindow = 0;
            client.actorV2IdentityAckedWindow = 0;
            client.actorV2SnapshotsSentWindow = 0;
            client.actorV2BytesSentWindow = 0;
            client.actorV2PresentationSentWindow = 0;
            client.actorV2PresentationBytesSentWindow = 0;
            client.actorV2AttackSentWindow = 0;
            client.actorV2AttackSuppressedUntilIdentityKnownWindow = 0;
            client.actorV2DeferredWindow = 0;
            client.actorV2PositionSuppressedUntilIdentityKnownWindow = 0;
            client.actorV2PresentationSuppressedUntilIdentityKnownWindow = 0;
            client.actorV2MissingIdentityByNetIdWindow.clear();
            for (std::size_t& count : client.actorV2TierCounts)
                count = 0;
        }
    }
}

void MPServer::broadcastActorPresentationV2ToCell(
    const std::string& cellId, CellActorState& cellState, const ActorList& actorList, HSteamNetConnection except)
{
    for (auto& [conn, client] : mClients)
    {
        if (conn == except
            || !clientHasActorCellLoaded(client, cellId)
            || client.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
            continue;

        ActorPresentationV2List presentationList;
        presentationList.protocolVersion = ActorSyncProtocolVersionV2;
        presentationList.authorityGuid = actorList.authorityGuid;
        presentationList.authorityGeneration = actorList.authorityGeneration;
        presentationList.sequence = actorList.snapshotSequence;
        presentationList.serverTimestamp = actorList.serverTimestamp;
        presentationList.snapshots.reserve(actorList.actors.size());

        for (const BaseActor& actor : actorList.actors)
        {
            auto actorIt = cellState.actors.find(makeActorKey(actor));
            if (actorIt == cellState.actors.end())
                continue;

            ActorRegistryRecord& record = actorIt->second;
            const ActorInstanceId actorNetId = ensureActorNetId(record, cellId);
            if (actorNetId == 0)
                continue;

            if (client.actorV2IdentitySent.count(actorNetId) == 0
                || client.actorV2IdentityAcked.count(actorNetId) == 0)
            {
                ++client.actorV2PresentationSuppressedUntilIdentityKnownWindow;
                ++client.actorV2MissingIdentityByNetIdWindow[actorNetId];
                continue;
            }

            presentationList.snapshots.push_back(makePresentationSnapshot(actor, actorNetId));
        }

        if (presentationList.snapshots.empty())
            continue;

        PacketActorPresentationV2 pkt;
        pkt.setPresentationList(&presentationList);
        const std::vector<uint8_t> encoded = pkt.encode();
        // Presentation snapshots are superseded by newer sequence numbers. Do
        // not place them ahead of chat/player edges on the reliable stream.
        sendTo(conn, encoded, /*reliable=*/false);
        client.actorV2PresentationSentWindow += presentationList.snapshots.size();
        client.actorV2PresentationBytesSentWindow += encoded.size();
    }
}

void MPServer::upsertSpawnedActorDynamicRecordLinkIfNeeded(const BaseActor& actor)
{
    if (!mPlayerDb || actor.mpNum == 0 || actor.refId.empty() || actor.cellId.empty())
        return;

    for (const std::string_view recordType : { std::string_view("npc"), std::string_view("creature") })
    {
        if (mWorld.dynamicRecords.find(makeDynamicRecordKey(recordType, actor.refId)) == mWorld.dynamicRecords.end())
            continue;

        mPlayerDb->upsertSpawnedActorDynamicRecordLink(actor.refId, actor.cellId, actor.mpNum);
        return;
    }
}

void MPServer::persistSpawnedActorIfNeeded(ActorRegistryRecord& record, uint64_t now, bool force)
{
    if (!mPlayerDb || !record.persistent || record.actor.mpNum == 0)
        return;

    static constexpr uint64_t kHotPositionPersistIntervalMs = 2000;
    if (now == 0)
        now = currentServerTimeMs();

    if (!force
        && record.lastPersistTime != 0
        && now - record.lastPersistTime < kHotPositionPersistIntervalMs)
    {
        record.pendingPersist = true;
        return;
    }

    PersistedSpawnedActor persisted;
    persisted.actor = record.actor;
    persisted.persistent = true;
    mPlayerDb->upsertSpawnedActor(persisted);
    record.lastPersistTime = now;
    record.pendingPersist = false;
}

void MPServer::deletePersistedSpawnedActor(uint32_t mpNum)
{
    if (!mPlayerDb || mpNum == 0)
        return;

    mPlayerDb->deleteSpawnedActor(mpNum);
}

void MPServer::sendActorLifecycleEvent(const char* eventName, const BaseActor& actor, bool persistent)
{
    if (std::string_view(eventName) == "spawned")
        mLua.onActorSpawned(actor, persistent);
    else if (std::string_view(eventName) == "death")
        mLua.onActorDeath(actor, persistent);
}

// ---------------------------------------------------------------------------
void MPServer::onClientMessage(ConnectedClient& client,
                               const uint8_t* data, size_t size)
{
    PacketHeader hdr;
    if (!BasePacket::peekHeader(data, size, hdr))
        return;

    auto type = static_cast<PacketType>(hdr.type);

    if (type == PacketType::CorpseDispose)
    {
        Log(Debug::Info) << "[Server] CorpseDispose wire packet received"
                         << " client=" << client.name
                         << " bytes=" << size
                         << " headerType=" << hdr.type
                         << " payloadSize=" << hdr.payloadSize
                         << " sequence=" << hdr.sequence;
    }

    // Must complete handshake before any other packet is processed.
    if (!client.handshakeComplete
        && type != PacketType::Handshake
        && type != PacketType::ChallengeResponse)
    {
        Log(Debug::Warning) << "[Server] Pre-handshake packet from conn="
                            << client.conn << ", ignoring";
        return;
    }

    // Must select a character before any world/gameplay packets are processed.
    // CharacterSelect and PlayerCharGen are the only exceptions.
    if (client.handshakeComplete && !client.charSelectComplete
        && type != PacketType::CharacterSelect
        && type != PacketType::ChallengeResponse
        && type != PacketType::PlayerCharGen
        && type != PacketType::LinkKeyRequest    // allowed during charselect flow
        && type != PacketType::UnlinkKeyRequest  // allowed during charselect flow
        && type != PacketType::DeleteCharRequest  // allowed during charselect flow
        && type != PacketType::Handshake)
    {
        Log(Debug::Verbose) << "[Server] Pre-charselect packet type=" << (int)type
                            << " from " << client.name << ", ignoring";
        return;
    }

    switch (type)
    {
        case PacketType::Handshake:        handleHandshake(client, data, size);          break;
        case PacketType::CharacterSelect:  handleCharacterSelect(client, data, size);    break;
        case PacketType::ChallengeResponse:handleChallengeResponse(client, data, size); break;
        case PacketType::LinkKeyRequest:   handleLinkKeyRequest(client, data, size);    break;
        case PacketType::UnlinkKeyRequest: handleUnlinkKeyRequest(client, data, size);  break;
        case PacketType::DeleteCharRequest:handleDeleteCharRequest(client, data, size);  break;
        case PacketType::PlayerCharGen:    handlePlayerCharGen(client, data, size);      break;
        case PacketType::PlayerBaseInfo:   handlePlayerBaseInfo(client, data, size);     break;
        case PacketType::PlayerPosition:   handlePlayerPosition(client, data, size);     break;
        case PacketType::PlayerCellChange: handlePlayerCellChange(client, data, size);   break;
        case PacketType::PlayerLoadedCells: handlePlayerLoadedCells(client, data, size); break;
        case PacketType::PlayerEquipment:  handlePlayerEquipment(client, data, size);    break;
        case PacketType::PlayerAnimFlags:  handlePlayerAnimFlags(client, data, size);    break;
        case PacketType::PlayerAnimPlay:   handlePlayerAnimPlay(client, data, size);     break;
        case PacketType::PlayerAttack:     handlePlayerAttack(client, data, size);       break;
        case PacketType::PlayerCast:       handlePlayerCast(client, data, size);         break;
        case PacketType::PlayerInventory:  handlePlayerInventory(client, data, size);    break;
        case PacketType::PlayerSpellbook:  handlePlayerSpellbook(client, data, size);    break;
        case PacketType::PlayerFaction:    handlePlayerFaction(client, data, size);      break;
        case PacketType::PlayerBounty:     handlePlayerBounty(client, data, size);       break;
        case PacketType::PlayerTopic:      handlePlayerTopic(client, data, size);        break;
        case PacketType::RecordCreateRequest: handleRecordCreateRequest(client, data, size); break;
        case PacketType::AlchemyRequest:   handleAlchemyRequest(client, data, size);     break;
        case PacketType::EnchantingRequest: handleEnchantingRequest(client, data, size); break;
        case PacketType::PlayerJournal:    handlePlayerJournal(client, data, size);      break;
        case PacketType::PlayerStatsDynamic: handlePlayerStatsDynamic(client, data, size); break;
        case PacketType::PlayerDeath:      handlePlayerDeath(client, data, size);        break;
        case PacketType::PlayerResurrect:  handlePlayerResurrect(client, data, size);    break;
        case PacketType::PlayerVehicleRequest: handlePlayerVehicleRequest(client, data, size); break;
        case PacketType::ChatMessage:      handleChatMessage(client, data, size);        break;
        case PacketType::PacketLuaEvent:   handleLuaEvent(client, data, size);           break;
        case PacketType::ObjectPlace:      handleObjectPlace(client, data, size);        break;
        case PacketType::WorldItemTakeRequest: handleWorldItemTakeRequest(client, data, size); break;
        case PacketType::InventoryTakeRequest: handleInventoryTakeRequest(client, data, size); break;
        case PacketType::InventoryPutRequest: handleInventoryPutRequest(client, data, size); break;
        case PacketType::BarterRequest: handleBarterRequest(client, data, size); break;
        case PacketType::CrimeInteractionRequest: handleCrimeInteractionRequest(client, data, size); break;
        case PacketType::GuardArrest:     handleGuardArrest(client, data, size);       break;
        case PacketType::ObjectDelete:     handleObjectDelete(client, data, size);       break;
        case PacketType::ObjectMove:       handleObjectMove(client, data, size);         break;
        case PacketType::Container:        handleContainer(client, data, size);          break;
        case PacketType::DoorState:        handleDoorState(client, data, size);          break;
        case PacketType::WorldWeather:     handleWeather(client, data, size);            break;
        case PacketType::ActorList:        handleActorList(client, data, size);          break;
        case PacketType::ActorPosition:    handleActorPosition(client, data, size);      break;
        case PacketType::ActorPositionV2:  handleActorPositionV2(client, data, size);    break;
        case PacketType::MechanicsSnapshot: handleMechanicsSnapshot(client, data, size); break;
        case PacketType::ActorPresentationV2: handleActorPresentationV2(client, data, size); break;
        case PacketType::ActorIdentityAck: handleActorIdentityAck(client, data, size);   break;
        case PacketType::ActorAnimFlags:   handleActorAnimFlags(client, data, size);     break;
        case PacketType::ActorAnimPlay:    handleActorAnimPlay(client, data, size);      break;
        case PacketType::ActorAttack:      handleActorAttack(client, data, size);        break;
        case PacketType::ActorAttackV2:    handleActorAttackV2(client, data, size);      break;
        case PacketType::ActorSpeech:      handleActorSpeech(client, data, size);        break;
        case PacketType::ActorCast:        handleActorCast(client, data, size);          break;
        case PacketType::ActorCellChange:  handleActorCellChange(client, data, size);    break;
        case PacketType::ActorDeath:       handleActorDeath(client, data, size);         break;
        case PacketType::ActorEquipment:   handleActorEquipment(client, data, size);     break;
        case PacketType::ActorStatsDynamic: handleActorStatsDynamic(client, data, size); break;
        case PacketType::ActorAI:          handleActorAI(client, data, size);            break;
        case PacketType::ActorCombatRequest: handleActorCombatRequest(client, data, size); break;
        case PacketType::ActorCombatResult: handleActorCombatResult(client, data, size); break;
        case PacketType::CorpseDispose:     handleCorpseDispose(client, data, size);     break;
        default:
            Log(Debug::Verbose) << "[Server] Unhandled packet type " << hdr.type;
            break;
    }
}

// ---------------------------------------------------------------------------
std::vector<uint8_t> MPServer::buildWorldTimePacket() const
{
    PacketWorldTime pkt;
    pkt.time.hour      = mWorld.gameHour;
    pkt.time.day       = mWorld.day;
    pkt.time.month     = mWorld.month;
    pkt.time.year      = mWorld.year;
    pkt.time.gameHour  = mWorld.gameHour;
    pkt.timeScale      = mWorld.timeScale;
    return pkt.encode();
}

// ---------------------------------------------------------------------------
void MPServer::loadPersistentWorldState()
{
    if (!mPlayerDb) return;

    // Session-only spawned actor links from a previous process lifetime must not
    // keep generated actor records alive. Persistent spawned actors recreate
    // their links after dynamic records are loaded below.
    mPlayerDb->clearSpawnedActorDynamicRecordLinks();

    uint64_t maxMpNum = 0;
    std::size_t objectCount = 0;
    std::size_t spawnedActorCount = 0;
    std::size_t deadVanillaActorCount = 0;
    std::size_t disposedVanillaActorCount = 0;
    std::size_t dynamicRecordCount = 0;
    std::vector<DynamicRecordCatalogEntry> dynamicRecordCatalog;

    std::unordered_set<uint32_t> worldObjectMpNums;
    for (const auto& object : mPlayerDb->loadWorldObjects())
    {
        maxMpNum = std::max<uint64_t>(maxMpNum, object.mpNum);
        mWorld.placedObjects[object.cellId].push_back(object);
        if (object.mpNum != 0)
            worldObjectMpNums.insert(object.mpNum);
        ++objectCount;
    }

    std::size_t takenReferenceCount = 0;
    for (PlacedObjectIdentity identity : mPlayerDb->loadTakenWorldItemReferences())
    {
        mWorld.takenItemReferences[identity.cellId].push_back(std::move(identity));
        ++takenReferenceCount;
    }

    std::size_t partialWorldItemCount = 0;
    for (const WorldItemMutation& mutation : mPlayerDb->loadWorldItemCountOverrides())
    {
        mWorld.worldItemCountOverrides[makeWorldItemKey(mutation.object)] = mutation;
        ++partialWorldItemCount;
    }

    for (const auto& record : mPlayerDb->loadContainerRecords())
    {
        if (record.mpNum != 0 && worldObjectMpNums.count(record.mpNum) != 0)
        {
            Log(Debug::Warning) << "[Server] Pruning stale container record for world object mpNum="
                                << record.mpNum
                                << " refId=" << record.refId
                                << " cell=" << record.cellId;
            mPlayerDb->deleteContainerRecord(record.cellId, record.refId, record.refNum, record.mpNum);
            continue;
        }

        ContainerRecord normalized = record;
        normalizeContainerItems(normalized.items);
        mWorld.containers[makeContainerKey(
            normalized.cellId, normalized.refId, normalized.refNum, normalized.mpNum)] = std::move(normalized);
    }

    for (const auto& entry : mPlayerDb->loadDoorStates())
        mWorld.doorStates[entry.cellId].push_back(entry);

    dynamicRecordCatalog = mPlayerDb->loadDynamicRecordCatalog();

    for (auto record : mPlayerDb->loadDynamicRecords())
    {
        DynamicRecordCatalogEntry migratedCatalog;
        if (record.data.size() >= 4 && std::string_view(record.data).starts_with("OMDR"))
        {
            // Canonical typed payloads are transactionally rewritten when the
            // stored schema/fingerprint predates the current validator. An
            // unsupported future payload is fatal: serving it would let
            // clients interpret persistence under different semantics.
            records::DynamicRecordDefinition definition
                = records::upgradeDefinition(records::decodeDefinition(record.data));
            const auto errors = records::validate(definition);
            if (!errors.empty())
                throw std::runtime_error("Invalid persisted typed dynamic record " + record.recordId
                    + ": " + errors.front().code + " at " + errors.front().path);
            definition = records::canonicalize(std::move(definition));
            record.data = records::encodeDefinition(definition);
            record.schemaVersion = records::CurrentSchemaVersion;
            mPlayerDb->upsertDynamicRecord(record);

            migratedCatalog.definitionFingerprint = records::fingerprint(definition);
            migratedCatalog.schemaVersion = records::CurrentSchemaVersion;
            migratedCatalog.validationVersion = 1;

            const std::vector<std::string> dependencies = records::extractContentDependencies(definition);
            mPlayerDb->replaceDynamicRecordDependencies(record.recordType, record.recordId, dependencies);
        }
        else if (isCanonicalServerLuaRecordType(record.recordType))
        {
            // Preserve the original bytes before replacing the historical Lua
            // table with an OMDR definition. A failed conversion leaves the
            // live row untouched and records a durable diagnostic.
            try
            {
                mPlayerDb->backupLegacyDynamicRecord(record);
                records::DynamicRecordDefinition definition
                    = records::canonicalize(parseServerLuaRecord(record.recordType, record.data));
                const auto errors = records::validate(definition);
                if (!errors.empty())
                    throw std::runtime_error(errors.front().code + " at " + errors.front().path);
                const std::string canonicalData = records::encodeDefinition(definition);
                if (!upsertDynamicRecord(record.recordType, record.recordId, canonicalData,
                        record.recordScope, true))
                    throw std::runtime_error("canonical DynamicRecordService transaction was rejected");

                record.data = canonicalData;
                record.schemaVersion = records::CurrentSchemaVersion;
                migratedCatalog.definitionFingerprint = records::fingerprint(definition);
                migratedCatalog.schemaVersion = records::CurrentSchemaVersion;
                migratedCatalog.validationVersion = ServerLuaValidationVersion;
                mPlayerDb->clearLegacyDynamicRecordMigrationFailure(record.recordType, record.recordId);
                Log(Debug::Info) << "[Server] Migrated legacy server-Lua record type=" << record.recordType
                                 << " id=" << record.recordId << " to OMDR";
            }
            catch (const std::exception& e)
            {
                mPlayerDb->recordLegacyDynamicRecordMigrationFailure(
                    record.recordType, record.recordId, e.what());
                Log(Debug::Warning) << "[Server] Legacy dynamic record migration skipped type="
                                    << record.recordType << " id=" << record.recordId
                                    << " reason=" << e.what();
            }
        }
        else
        {
            const std::string reason = "record type is outside the canonical OMDR schema";
            mPlayerDb->recordLegacyDynamicRecordMigrationFailure(
                record.recordType, record.recordId, reason);
            Log(Debug::Warning) << "[Server] Retaining readable legacy dynamic record type="
                                << record.recordType << " id=" << record.recordId
                                << " reason=" << reason;
        }

        WorldState::StoredDynamicRecord stored;
        stored.recordType = record.recordType;
        stored.recordId = record.recordId;
        stored.recordScope = normalizeDynamicRecordScope(record.recordScope);
        if (stored.recordScope.empty())
            stored.recordScope = "permanent";
        stored.persistent = true;
        stored.data = record.data;
        if (record.data.starts_with("OMDR"))
        {
            const records::DynamicRecordDefinition definition
                = records::upgradeDefinition(records::decodeDefinition(record.data));
            stored.dependencyRecordIds = records::extractContentDependencies(definition);
            mContentRegistry->installRuntimeDefinition(record.recordId, definition);
        }
        stored.sequence = mWorld.nextDynamicRecordSequence++;
        mWorld.dynamicRecords[makeDynamicRecordKey(stored.recordType, stored.recordId)] = std::move(stored);
        ++dynamicRecordCount;

        DynamicRecordCatalogEntry catalogEntry;
        catalogEntry.recordType = record.recordType;
        catalogEntry.recordId = record.recordId;
        catalogEntry.recordScope = record.recordScope;
        catalogEntry.persistent = true;
        catalogEntry.createdAt = record.createdAt;
        catalogEntry.updatedAt = record.updatedAt;
        catalogEntry.definitionFingerprint = migratedCatalog.definitionFingerprint;
        catalogEntry.schemaVersion = migratedCatalog.schemaVersion;
        catalogEntry.validationVersion = migratedCatalog.validationVersion;
        mPlayerDb->upsertDynamicRecordCatalog(catalogEntry);
    }

    std::unordered_set<std::string> sessionRecordIds;
    for (const auto& entry : dynamicRecordCatalog)
    {
        if (!entry.persistent && !entry.recordId.empty())
            sessionRecordIds.insert(entry.recordId);
    }

    if (!sessionRecordIds.empty())
    {
        cleanupDynamicReferences(
            [&](std::string_view refId) -> bool { return sessionRecordIds.count(std::string(refId)) != 0; },
            /*broadcastLive=*/false,
            "startup_session_records");

        for (const auto& entry : dynamicRecordCatalog)
        {
            if (entry.persistent)
                continue;
            mWorld.dynamicRecords.erase(makeDynamicRecordKey(entry.recordType, entry.recordId));
            mPlayerDb->replaceDynamicRecordDependencies(entry.recordType, entry.recordId, {});
            mPlayerDb->deleteDynamicRecord(entry.recordType, entry.recordId);
            mPlayerDb->deleteDynamicRecordLinks(entry.recordId);
            mPlayerDb->deleteDynamicRecordCatalog(entry.recordType, entry.recordId);
        }
    }

    for (const auto& record : mPlayerDb->loadSpawnedActors())
    {
        BaseActor actor = record.actor;
        if (actor.mpNum == 0 || actor.refId.empty() || actor.cellId.empty())
            continue;

        normalizeActorIdentity(actor);
        maxMpNum = std::max<uint64_t>(maxMpNum, actor.mpNum);
        auto& cellState = mWorld.actorCells[actor.cellId];
        // serverSpawnTime = 0: loaded actors are "old"; no spawn-grace needed.
        ActorRegistryRecord& registryRecord = cellState.actors[makeActorKey(actor)]
            = { actor, currentServerTimeMs(), /*serverSpawnTime=*/0, /*persistent=*/true };
        ensureActorNetId(registryRecord, actor.cellId);
        rememberActorLocation(registryRecord.actor, actor.cellId);
        ++spawnedActorCount;

        for (const std::string_view recordType : { std::string_view("npc"), std::string_view("creature") })
        {
            const auto it = mWorld.dynamicRecords.find(makeDynamicRecordKey(recordType, actor.refId));
            if (it != mWorld.dynamicRecords.end())
            {
                mPlayerDb->upsertSpawnedActorDynamicRecordLink(actor.refId, actor.cellId, actor.mpNum);
                break;
            }
        }
    }

    const std::vector<BaseActor> persistedDeadVanillaActors = mPlayerDb->loadDeadVanillaActors();
    std::unordered_map<ActorInstanceId, std::size_t> persistedDeadIdentityCounts;
    for (const BaseActor& persistedActor : persistedDeadVanillaActors)
    {
        const ActorInstanceId actorNetId = actorInstanceIdFromActor(persistedActor);
        if (actorNetId != 0)
            ++persistedDeadIdentityCounts[actorNetId];
    }

    std::size_t conflictingDeadVanillaActorsPurged = 0;
    for (const auto& persistedActor : persistedDeadVanillaActors)
    {
        BaseActor actor = persistedActor;
        actor.mpNum = 0;
        if (actor.refId.empty() || actor.cellId.empty())
            continue;

        normalizeActorIdentity(actor);
        const ActorInstanceId actorNetId = actorInstanceIdFromActor(actor);
        const auto identityCountIt = persistedDeadIdentityCounts.find(actorNetId);
        if (actorNetId != 0 && identityCountIt != persistedDeadIdentityCounts.end()
            && identityCountIt->second > 1)
        {
            // Older leveled-list reconciliation builds could persist two
            // species under the same stable spawner refNum. Neither row is
            // trustworthy, so discard the entire collision and let the cell
            // authority publish one fresh canonical roll.
            mPlayerDb->deleteDeadVanillaActor(actor.refId, actor.refNum);
            ++conflictingDeadVanillaActorsPurged;
            continue;
        }
        actor.isDead = true;
        actor.isInstantDeath = true;
        if (actor.dynamicStats.health.current > 0.f)
            actor.dynamicStats.health.current = 0.f;

        ActorRegistryRecord record { actor, currentServerTimeMs(), /*serverSpawnTime=*/0, /*persistent=*/false };
        ensureActorNetId(record, actor.cellId);
        const std::string actorKey = makeActorKey(actor);
        auto& cellState = mWorld.actorCells[actor.cellId];
        cellState.actors[actorKey] = record;
        mWorld.deadVanillaActorCells[actor.cellId][actorKey] = record;
        rememberActorLocation(actor, actor.cellId);
        ++deadVanillaActorCount;
    }

    for (BaseActor actor : mPlayerDb->loadDisposedVanillaActors())
    {
        actor.mpNum = 0;
        actor.isDead = false;
        if (actor.refId.empty() || actor.cellId.empty())
            continue;
        normalizeActorIdentity(actor);
        mWorld.disposedVanillaActors[makeActorKey(actor)] = std::move(actor);
        ++disposedVanillaActorCount;
    }

    if (conflictingDeadVanillaActorsPurged != 0)
    {
        Log(Debug::Warning) << "[Server] Purged conflicting persisted vanilla corpses"
                            << " rows=" << conflictingDeadVanillaActorsPurged;
    }

    objectCount = 0;
    for (const auto& [cellId, objects] : mWorld.placedObjects)
        objectCount += objects.size();
    dynamicRecordCount = mWorld.dynamicRecords.size();

    const uint64_t minimumNextMpNum = std::max<uint64_t>(1, maxMpNum + 1);
    const uint64_t nextMpNum = mPlayerDb->loadNextMpNum(minimumNextMpNum);
    setNextWorldMpNum(nextMpNum);

    Log(Debug::Info) << "[Server] Loaded persistent world state: objects="
                     << objectCount
                     << " spawnedActors=" << spawnedActorCount
                     << " deadVanillaActors=" << deadVanillaActorCount
                     << " disposedVanillaActors=" << disposedVanillaActorCount
                     << " containers=" << mWorld.containers.size()
                     << " doorCells=" << mWorld.doorStates.size()
                     << " dynamicRecords=" << dynamicRecordCount
                     << " takenReferences=" << takenReferenceCount
                     << " partialWorldItems=" << partialWorldItemCount
                     << " nextMpNum=" << nextMpNum;
}

// ---------------------------------------------------------------------------
void MPServer::sendCellStateToClient(HSteamNetConnection conn, const std::string& cellId)
{
    sendGameSettingsToClient(conn, cellId);
    sendActorAuthorityToClient(conn, cellId);
    sendActorStateToClient(conn, cellId);
    sendCellObjectStateToClient(conn, cellId);
}

void MPServer::sendCellObjectStateToClient(HSteamNetConnection conn, const std::string& cellId)
{
    auto objectsIt = mWorld.placedObjects.find(cellId);
    if (objectsIt != mWorld.placedObjects.end())
    {
        for (const auto& object : objectsIt->second)
        {
            PacketObjectPlace pkt;
            pkt.object = object;
            sendTo(conn, pkt.encode());
        }
    }

    for (const auto& [key, record] : mWorld.containers)
    {
        if (record.cellId != cellId || !record.hasAuthority) continue;
        PacketContainer pkt;
        pkt.container = record;
        pkt.mAction = static_cast<uint8_t>(ContainerAction::Set);
        sendTo(conn, pkt.encode());
    }

    for (const auto& [key, mutation] : mWorld.worldItemCountOverrides)
    {
        (void)key;
        if (mutation.object.cellId != cellId || mutation.resultingWorldCount <= 0)
            continue;
        PacketObjectCount packet;
        packet.object = mutation.object;
        packet.count = mutation.resultingWorldCount;
        sendTo(conn, packet.encode());
    }

    const auto takenIt = mWorld.takenItemReferences.find(cellId);
    if (takenIt != mWorld.takenItemReferences.end())
    {
        for (const PlacedObjectIdentity& identity : takenIt->second)
        {
            PacketObjectDelete packet;
            packet.mpNum = identity.mpNum;
            packet.cellId = identity.cellId;
            packet.refId = identity.refId;
            packet.refNum = identity.refIndex;
            packet.refContentFile = identity.refContentFile;
            sendTo(conn, packet.encode());
        }
    }

    auto doorsIt = mWorld.doorStates.find(cellId);
    if (doorsIt != mWorld.doorStates.end() && !doorsIt->second.empty())
    {
        PacketDoorState pkt;
        pkt.authorGuid = 0;
        pkt.cellId = cellId;
        pkt.doors = doorsIt->second;
        sendTo(conn, pkt.encode());
    }
}

// ---------------------------------------------------------------------------
void MPServer::sendDynamicRecordsToClient(HSteamNetConnection conn)
{
    if (mWorld.dynamicRecords.empty())
        return;

    std::vector<const WorldState::StoredDynamicRecord*> ordered;
    ordered.reserve(mWorld.dynamicRecords.size());
    for (const auto& [key, record] : mWorld.dynamicRecords)
        ordered.push_back(&record);

    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return std::tie(left->sequence, left->recordType, left->recordId)
            < std::tie(right->sequence, right->recordType, right->recordId);
    });

    std::unordered_map<std::string, const WorldState::StoredDynamicRecord*> recordsById;
    for (const auto* record : ordered)
        recordsById.emplace(lowerAscii(record->recordId), record);
    enum class Visit : std::uint8_t { None, Active, Complete };
    std::unordered_map<const WorldState::StoredDynamicRecord*, Visit> visits;
    std::vector<const WorldState::StoredDynamicRecord*> dependencyOrdered;
    dependencyOrdered.reserve(ordered.size());
    std::function<void(const WorldState::StoredDynamicRecord*)> visit
        = [&](const WorldState::StoredDynamicRecord* record) {
        Visit& state = visits[record];
        if (state == Visit::Complete)
            return;
        if (state == Visit::Active)
            return; // persisted cycles cannot be satisfied; retain deterministic fallback order
        state = Visit::Active;
        for (const std::string& dependencyId : record->dependencyRecordIds)
        {
            const auto dependency = recordsById.find(lowerAscii(dependencyId));
            if (dependency != recordsById.end())
                visit(dependency->second);
        }
        state = Visit::Complete;
        dependencyOrdered.push_back(record);
    };
    for (const auto* record : ordered)
        visit(record);

    // Keep records isolated on initial bootstrap. Client-side conversion can
    // legitimately defer one record while its base dependency is unavailable;
    // batching same-type records would make that one bad entry block every
    // otherwise valid record in the packet.
    for (const auto* record : dependencyOrdered)
    {
        PacketRecordDynamic pkt;
        pkt.action = DynamicRecordAction::Upsert;
        pkt.recordType = record->recordType;
        pkt.entries.push_back({ record->recordId, record->data });
        sendTo(conn, pkt.encode());
    }
}

std::unordered_map<std::string, uint64_t> MPServer::buildGeneratedDynamicRecordCounters(const std::string& prefix) const
{
    std::unordered_map<std::string, uint64_t> nextGeneratedNumbers;

    for (const auto& [key, record] : mWorld.dynamicRecords)
    {
        if (record.recordScope != "generated")
            continue;

        const auto maybeGeneratedNum = parseGeneratedRecordNumber(prefix, record.recordType, record.recordId);
        if (!maybeGeneratedNum)
            continue;

        uint64_t& nextValue = nextGeneratedNumbers[record.recordType];
        nextValue = std::max(nextValue, *maybeGeneratedNum + 1);
    }

    return nextGeneratedNumbers;
}

bool MPServer::hasStaticNpcRecord(std::string_view recordId) const
{
    return mContentRegistry && mContentRegistry->hasStaticNpcRecord(recordId);
}

std::vector<DynamicRecordCatalogEntry> MPServer::listDynamicRecordCatalog()
{
    std::vector<DynamicRecordCatalogEntry> entries;
    if (mPlayerDb)
        entries = mPlayerDb->loadDynamicRecordCatalog();

    std::unordered_set<std::string> seenKeys;
    seenKeys.reserve(entries.size() + mWorld.dynamicRecords.size());

    for (auto& entry : entries)
    {
        const std::string key = makeDynamicRecordKey(entry.recordType, entry.recordId);
        entry.loaded = mWorld.dynamicRecords.find(key) != mWorld.dynamicRecords.end();
        seenKeys.insert(key);
    }

    for (const auto& [key, record] : mWorld.dynamicRecords)
    {
        if (seenKeys.count(key) != 0)
            continue;

        DynamicRecordCatalogEntry entry;
        entry.recordType = record.recordType;
        entry.recordId = record.recordId;
        entry.recordScope = record.recordScope;
        entry.persistent = record.persistent;
        entry.loaded = true;
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const DynamicRecordCatalogEntry& left, const DynamicRecordCatalogEntry& right) {
        if (left.recordType != right.recordType)
            return left.recordType < right.recordType;
        return left.recordId < right.recordId;
    });

    return entries;
}

std::optional<DynamicRecordCatalogEntry> MPServer::getDynamicRecordInfo(
    std::string_view recordType, std::string_view recordId)
{
    const std::string normalizedType = normalizeDynamicRecordType(recordType);
    if (normalizedType.empty() || recordId.empty())
        return std::nullopt;

    const std::string targetId(recordId);
    for (const auto& entry : listDynamicRecordCatalog())
    {
        if (entry.recordType == normalizedType && entry.recordId == targetId)
            return entry;
    }

    return std::nullopt;
}

std::vector<DatabaseTableInfo> MPServer::listBrowsableTables()
{
    return mPlayerDb ? mPlayerDb->listBrowsableTables() : std::vector<DatabaseTableInfo>{};
}

std::optional<DatabaseBrowsePage> MPServer::browseDatabaseTable(
    std::string_view tableName, int64_t offset, int64_t limit)
{
    if (!mPlayerDb)
        return std::nullopt;

    return mPlayerDb->browseTable(tableName, offset, limit);
}

std::optional<std::string> MPServer::loadCharacterLuaStorageValue(
    int64_t characterId, std::string_view storageNamespace, std::string_view key)
{
    if (!mPlayerDb || characterId <= 0 || storageNamespace.empty() || key.empty())
        return std::nullopt;

    return mPlayerDb->loadCharacterLuaStorageValue(characterId, storageNamespace, key);
}

bool MPServer::saveCharacterLuaStorageValue(
    int64_t characterId, std::string_view storageNamespace, std::string_view key, std::string_view value)
{
    if (!mPlayerDb || characterId <= 0 || storageNamespace.empty() || key.empty())
        return false;

    mPlayerDb->saveCharacterLuaStorageValue(characterId, storageNamespace, key, value);
    return true;
}

bool MPServer::deleteCharacterLuaStorageValue(
    int64_t characterId, std::string_view storageNamespace, std::string_view key)
{
    if (!mPlayerDb || characterId <= 0 || storageNamespace.empty() || key.empty())
        return false;

    return mPlayerDb->deleteCharacterLuaStorageValue(characterId, storageNamespace, key);
}

std::vector<DynamicRecordCatalogEntry> MPServer::collectGeneratedDynamicRecordGcCandidates(
    const std::optional<std::string>& recordType, const std::optional<bool>& persistent)
{
    std::vector<DynamicRecordCatalogEntry> candidates;

    const std::string normalizedType = recordType ? normalizeDynamicRecordType(*recordType) : std::string{};
    if (recordType && normalizedType.empty())
        return candidates;

    for (const auto& entry : listDynamicRecordCatalog())
    {
        if (entry.recordScope != "generated")
            continue;
        if (entry.linkCount > 0)
            continue;
        if (!normalizedType.empty() && entry.recordType != normalizedType)
            continue;
        if (persistent && entry.persistent != *persistent)
            continue;

        candidates.push_back(entry);
    }

    return candidates;
}

std::size_t MPServer::gcGeneratedDynamicRecordsAfterUnlink(std::string_view reason)
{
    const auto candidates = collectGeneratedDynamicRecordGcCandidates();
    if (candidates.empty())
        return 0;

    std::size_t removed = 0;
    std::size_t warnedPersistent = 0;

    for (const auto& entry : candidates)
    {
        if (entry.persistent)
        {
            ++warnedPersistent;
            Log(Debug::Warning) << "[Server] Generated persistent dynamic record became unlinked and is now a GC candidate:"
                                << " type=" << entry.recordType
                                << " id=" << entry.recordId
                                << " reason=" << reason;
        }

        if (removeDynamicRecord(entry.recordType, entry.recordId))
            ++removed;
    }

    if (removed > 0)
    {
        Log(Debug::Info) << "[Server] Auto-GC removed " << removed
                         << " generated dynamic record(s) after unlink reason=" << reason
                         << " warnedPersistent=" << warnedPersistent;
    }

    return removed;
}

void MPServer::scheduleGeneratedDynamicRecordGc(std::string_view reason, std::chrono::milliseconds delay)
{
    mGeneratedRecordGcScheduled = true;
    mGeneratedRecordGcReason.assign(reason.begin(), reason.end());
    mGeneratedRecordGcDueTime = std::chrono::steady_clock::now() + delay;
}

void MPServer::flushScheduledGeneratedDynamicRecordGc()
{
    if (!mGeneratedRecordGcScheduled)
        return;

    if (std::chrono::steady_clock::now() < mGeneratedRecordGcDueTime)
        return;

    const std::string reason = mGeneratedRecordGcReason.empty() ? "deferred_unlink" : mGeneratedRecordGcReason;
    mGeneratedRecordGcScheduled = false;
    mGeneratedRecordGcReason.clear();
    gcGeneratedDynamicRecordsAfterUnlink(reason);
}

MPServer::DynamicReferenceCleanupStats MPServer::cleanupDynamicReferences(
    const std::function<bool(std::string_view)>& shouldRemoveRefId,
    bool broadcastLive,
    std::string_view reason)
{
    DynamicReferenceCleanupStats stats;

    std::vector<PlacedObject> removedObjects;
    for (auto it = mWorld.placedObjects.begin(); it != mWorld.placedObjects.end();)
    {
        auto& objects = it->second;
        for (const auto& object : objects)
        {
            if (shouldRemoveRefId(object.refId))
                removedObjects.push_back(object);
        }

        objects.erase(std::remove_if(objects.begin(), objects.end(),
            [&](const PlacedObject& object) { return shouldRemoveRefId(object.refId); }),
            objects.end());

        if (objects.empty())
            it = mWorld.placedObjects.erase(it);
        else
            ++it;
    }

    for (const auto& object : removedObjects)
    {
        ++stats.placedObjects;
        mLua.removePlacedObject(object.mpNum);
        if (mPlayerDb)
            mPlayerDb->deleteWorldObject(object.mpNum);

        if (broadcastLive)
        {
            PacketObjectDelete pkt;
            pkt.mpNum = object.mpNum;
            pkt.cellId = object.cellId;
            broadcastToCell(object.cellId, pkt.encode());
        }
    }

    std::vector<ContainerRecord> updatedContainers;
    for (auto it = mWorld.containers.begin(); it != mWorld.containers.end();)
    {
        ContainerRecord& record = it->second;
        if (shouldRemoveRefId(record.refId))
        {
            ++stats.containers;
            if (mPlayerDb)
                mPlayerDb->deleteContainerRecord(record.cellId, record.refId, record.refNum, record.mpNum);
            it = mWorld.containers.erase(it);
            continue;
        }

        const std::size_t oldCount = record.items.size();
        record.items.erase(std::remove_if(record.items.begin(), record.items.end(),
            [&](const ContainerItem& item) { return shouldRemoveRefId(item.refId); }),
            record.items.end());

        if (record.items.size() != oldCount)
        {
            stats.containerItems += oldCount - record.items.size();
            if (mPlayerDb)
                mPlayerDb->upsertContainerRecord(record);
            if (broadcastLive)
                updatedContainers.push_back(record);
        }

        ++it;
    }

    if (broadcastLive)
    {
        for (const auto& record : updatedContainers)
        {
            PacketContainer pkt;
            pkt.container = record;
            pkt.mAction = static_cast<uint8_t>(ContainerAction::Set);
            broadcastToCell(record.cellId, pkt.encode());
        }
    }

    for (auto it = mWorld.doorStates.begin(); it != mWorld.doorStates.end();)
    {
        auto& entries = it->second;
        std::vector<DoorEntry> removedEntries;
        for (const auto& entry : entries)
        {
            if (shouldRemoveRefId(entry.refId))
                removedEntries.push_back(entry);
        }

        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [&](const DoorEntry& entry) { return shouldRemoveRefId(entry.refId); }),
            entries.end());

        for (const auto& entry : removedEntries)
        {
            ++stats.doorStates;
            if (mPlayerDb)
                mPlayerDb->deleteDoorState(entry.cellId, entry.refId, entry.refNum);
        }

        if (entries.empty())
            it = mWorld.doorStates.erase(it);
        else
            ++it;
    }

    std::unordered_set<int64_t> onlineCharacterIds;
    bool snapshotDirty = false;

    for (auto& [conn, client] : mClients)
    {
        if (client.dbCharacterId != 0)
            onlineCharacterIds.insert(client.dbCharacterId);

        bool inventoryChanged = false;
        auto& inventory = client.player.inventoryChanges.items;
        const std::size_t oldInventoryCount = inventory.size();
        inventory.erase(std::remove_if(inventory.begin(), inventory.end(),
            [&](const Item& item) { return shouldRemoveRefId(item.refId); }),
            inventory.end());
        if (inventory.size() != oldInventoryCount)
        {
            stats.inventoryItems += oldInventoryCount - inventory.size();
            inventoryChanged = true;
        }

        bool equipmentChanged = false;
        for (int slot = 0; slot < BasePlayer::NUM_EQUIPMENT_SLOTS; ++slot)
        {
            auto& entry = client.player.equipment[slot];
            if (entry.item.refId.empty())
                continue;

            if (shouldRemoveRefId(entry.item.refId) || !inventoryContainsItemIdentity(inventory, entry.item))
            {
                ++stats.equipmentItems;
                entry = EquipmentItem{};
                entry.slot = slot;
                equipmentChanged = true;
            }
        }

        if (!inventoryChanged && !equipmentChanged)
            continue;

        ++stats.characters;
        client.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
        ++client.inventoryRevision;
        client.player.inventoryChanges.revision = client.inventoryRevision;
        if (mPlayerDb && client.dbCharacterId != 0)
        {
            mPlayerDb->saveCharacterInventory(client.dbCharacterId, client.player.inventoryChanges.items, false,
                client.inventoryRevision);
            std::vector<EquipmentItem> equipment(client.player.equipment.begin(), client.player.equipment.end());
            mPlayerDb->saveCharacterEquipment(client.dbCharacterId, equipment, false);
        }

        if (broadcastLive)
        {
            sendAuthoritativeInventory(client);
            sendAuthoritativeEquipment(client);
        }
        snapshotDirty = true;
    }

    if (mPlayerDb)
    {
        for (const int64_t characterId : mPlayerDb->listCharactersWithSavedItems())
        {
            if (onlineCharacterIds.count(characterId) != 0)
                continue;

            std::vector<Item> inventory = mPlayerDb->loadCharacterInventory(characterId);
            const std::size_t oldInventoryCount = inventory.size();
            inventory.erase(std::remove_if(inventory.begin(), inventory.end(),
                [&](const Item& item) { return shouldRemoveRefId(item.refId); }),
                inventory.end());
            const std::size_t removedInventory = oldInventoryCount - inventory.size();

            std::vector<EquipmentItem> equipment = mPlayerDb->loadCharacterEquipment(characterId);
            const std::size_t oldEquipmentCount = equipment.size();
            equipment.erase(std::remove_if(equipment.begin(), equipment.end(),
                [&](const EquipmentItem& entry)
                {
                    return shouldRemoveRefId(entry.item.refId)
                        || !inventoryContainsItemIdentity(inventory, entry.item);
                }),
                equipment.end());
            const std::size_t removedEquipment = oldEquipmentCount - equipment.size();

            if (removedInventory == 0 && removedEquipment == 0)
                continue;

            stats.inventoryItems += removedInventory;
            stats.equipmentItems += removedEquipment;
            ++stats.characters;
            const uint64_t revision = mPlayerDb->loadInventoryRevision(characterId) + 1;
            mPlayerDb->saveCharacterInventory(characterId, inventory, false, revision);
            mPlayerDb->saveCharacterEquipment(characterId, equipment, false);
        }
    }

    if (snapshotDirty)
        syncLuaPlayerSnapshot();

    const std::size_t totalRemoved = stats.placedObjects + stats.containers + stats.containerItems
        + stats.doorStates + stats.inventoryItems + stats.equipmentItems;
    if (totalRemoved > 0)
    {
        Log(Debug::Info) << "[Server] Cleaned dangling dynamic references (" << reason << "):"
                         << " placed=" << stats.placedObjects
                         << " containers=" << stats.containers
                         << " containerItems=" << stats.containerItems
                         << " doors=" << stats.doorStates
                         << " inventoryItems=" << stats.inventoryItems
                         << " equipmentItems=" << stats.equipmentItems
                         << " characters=" << stats.characters;
    }

    return stats;
}

// ---------------------------------------------------------------------------
void MPServer::sendGameSettingsToClient(HSteamNetConnection conn, const std::string& cellId)
{
    if (cellId.empty())
        return;

    PacketGameSettings pkt;
    auto clientIt = mClients.find(conn);
    if (clientIt != mClients.end())
        pkt.settings = mLua.getEffectiveSurfPhysicsSettings(clientIt->second.guid, cellId);
    else
        pkt.settings = mLua.getCellSurfPhysicsSettings(cellId);
    // Config values are loaded before the dedicated Lua tick thread starts.
    // Never enter the Lua VM from this network-thread packet path.
    pkt.guardArrestMode = mGuardArrestDialogueEnabled
        ? GuardArrestMode::Dialogue
        : GuardArrestMode::Combat;
    sendTo(conn, pkt.encode());
}

void MPServer::populateRuntimeManifest(PacketHandshakeResponse& response) const
{
    response.serverLuaPackageManifestVersion = serverlua::ServerLuaPackageManifestVersion;
    response.multiplayerLuaApiVersion = serverlua::MultiplayerLuaApiVersion;
    response.openMWLuaApiVersion = Version::getLuaApiRevision();
    response.resolvedContentFingerprint = mResolvedContentFingerprint;
    std::unordered_set<uint8_t> seen;
    for (const auto& [packageId, types] : mRuntimeRecordCapabilities)
    {
        static_cast<void>(packageId);
        for (records::RecordType type : types)
        {
            const uint8_t value = static_cast<uint8_t>(type);
            if (seen.insert(value).second)
                response.supportedRuntimeRecordTypes.push_back(value);
        }
    }
    std::sort(response.supportedRuntimeRecordTypes.begin(), response.supportedRuntimeRecordTypes.end());
}

void MPServer::sendServerLuaPackages(HSteamNetConnection connection)
{
    if (!mServerLuaPackageRegistry)
        throw std::logic_error("Server Lua package registry is unavailable");
    const serverlua::PackageSet& packageSet = mServerLuaPackageRegistry->packageSet();
    PacketServerLuaPackageManifest manifest;
    manifest.packageSet = packageSet;
    sendTo(connection, manifest.encode());

    for (const serverlua::Package& package : packageSet.packages)
    {
        for (const serverlua::File& file : package.files)
        {
            for (std::size_t offset = 0; offset < file.source.size(); offset += serverlua::MaxChunkSize)
            {
                PacketServerLuaPackageChunk chunk;
                chunk.generation = packageSet.generation;
                chunk.packageId = package.packageId;
                chunk.packageHash = package.packageHash;
                chunk.filePath = file.path;
                chunk.offset = static_cast<std::uint32_t>(offset);
                chunk.bytes = file.source.substr(offset, serverlua::MaxChunkSize);
                sendTo(connection, chunk.encode());
            }
        }
    }

    PacketServerLuaPackageBootstrapComplete complete;
    complete.generation = packageSet.generation;
    complete.packageSetHash = packageSet.packageSetHash;
    sendTo(connection, complete.encode());
    Log(Debug::Verbose) << "[ServerLuaPackages] Sent package set generation=" << packageSet.generation
                        << " packages=" << packageSet.packages.size() << " to connection=" << connection;
}

// ---------------------------------------------------------------------------
void MPServer::handleHandshake(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketHandshake hs;
    if (!hs.decode(data, size))
    {
        mInterface->CloseConnection(c.conn, 0, "Bad handshake", false);
        return;
    }

    if (hs.protocolVersion != MultiplayerProtocolVersion)
    {
        PacketHandshakeResponse rsp;
        rsp.accepted      = false;
        rsp.serverVersion = MultiplayerBuildVersion;
        rsp.rejectReason  = "Multiplayer protocol mismatch: server="
                          + std::to_string(MultiplayerProtocolVersion)
                          + " client=" + std::to_string(hs.protocolVersion);
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Multiplayer protocol mismatch", true);
        return;
    }

    if (hs.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
    {
        PacketHandshakeResponse rsp;
        rsp.accepted      = false;
        rsp.serverVersion = MultiplayerBuildVersion;
        rsp.rejectReason  = "ActorSync protocol mismatch: server requires v2";
        rsp.actorSyncProtocolVersion = ActorSyncProtocolVersionV2;
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "ActorSync protocol mismatch", true);
        return;
    }
    c.actorSyncProtocolVersion = ActorSyncProtocolVersionV2;

    if (hs.contentManifestVersion != ContentManifestVersion)
    {
        PacketHandshakeResponse rsp;
        rsp.rejectReason = "Content manifest version mismatch: server="
            + std::to_string(ContentManifestVersion) + " client=" + std::to_string(hs.contentManifestVersion);
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Content manifest version mismatch", true);
        return;
    }
    if (hs.contentApiVersion != ContentApiVersion)
    {
        PacketHandshakeResponse rsp;
        rsp.rejectReason = "Content API version mismatch: server=" + std::to_string(ContentApiVersion)
            + " client=" + std::to_string(hs.contentApiVersion);
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Content API version mismatch", true);
        return;
    }
    if (hs.dynamicRecordWireVersion != records::CurrentWireVersion)
    {
        PacketHandshakeResponse rsp;
        rsp.rejectReason = "Runtime record schema mismatch: server="
            + std::to_string(records::CurrentWireVersion) + " client="
            + std::to_string(hs.dynamicRecordWireVersion);
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Runtime record schema mismatch", true);
        return;
    }
    if (hs.capabilityManifestVersion != RuntimeRecordCapabilityManifestVersion)
    {
        PacketHandshakeResponse rsp;
        rsp.rejectReason = "Runtime capability manifest version mismatch";
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Capability manifest version mismatch", true);
        return;
    }
    if (hs.serverLuaPackageManifestVersion != serverlua::ServerLuaPackageManifestVersion)
    {
        PacketHandshakeResponse rsp;
        populateRuntimeManifest(rsp);
        rsp.rejectReason = "Server Lua package manifest mismatch: server="
            + std::to_string(serverlua::ServerLuaPackageManifestVersion) + " client="
            + std::to_string(hs.serverLuaPackageManifestVersion);
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Server Lua package manifest mismatch", true);
        return;
    }
    if (hs.multiplayerLuaApiVersion != serverlua::MultiplayerLuaApiVersion)
    {
        PacketHandshakeResponse rsp;
        populateRuntimeManifest(rsp);
        rsp.rejectReason = "Multiplayer Lua API mismatch: server="
            + std::to_string(serverlua::MultiplayerLuaApiVersion) + " client="
            + std::to_string(hs.multiplayerLuaApiVersion);
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Multiplayer Lua API mismatch", true);
        return;
    }
    if (hs.openMWLuaApiVersion != static_cast<std::uint32_t>(Version::getLuaApiRevision()))
    {
        PacketHandshakeResponse rsp;
        populateRuntimeManifest(rsp);
        rsp.rejectReason = "OpenMW Lua API mismatch: server=" + std::to_string(Version::getLuaApiRevision())
            + " client=" + std::to_string(hs.openMWLuaApiVersion);
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "OpenMW Lua API mismatch", true);
        return;
    }

    // Validate content before any password/key lookup so incompatible clients
    // cannot create accounts or spend authentication work.
    {
        std::vector<ContentFileRule> authoritativeContent;
        authoritativeContent.reserve(mContentRegistry->contentFiles().size());
        for (const ServerContentRegistry::ManifestEntry& entry : mContentRegistry->contentFiles())
            authoritativeContent.push_back({ entry.filename, entry.sha256 });
        auto mismatches = validateContentFiles(hs.plugins, authoritativeContent, true, true);
        if (!mismatches.empty())
        {
            PacketHandshakeResponse rsp;
            rsp.pluginMismatches = std::move(mismatches);
            rsp.rejectReason = "Authoritative content mismatch: "
                + contentFileRejectReason(rsp.pluginMismatches, mModChecksHelpUrl);
            Log(Debug::Warning) << "[Handshake] Rejecting " << hs.playerName << ": " << rsp.rejectReason;
            sendTo(c.conn, rsp.encode());
            mInterface->CloseConnection(c.conn, 0, "Authoritative content mismatch", true);
            return;
        }

        std::vector<ContentFileRule> authoritativeLua;
        authoritativeLua.reserve(mContentRegistry->luaScripts().size());
        for (const ServerContentRegistry::ManifestEntry& entry : mContentRegistry->luaScripts())
            authoritativeLua.push_back({ entry.filename, entry.sha256 });
        auto luaMismatches = validateExactOrderedManifestAllowingDuplicates(hs.luaScripts, authoritativeLua);
        if (!luaMismatches.empty())
        {
            PacketHandshakeResponse rsp;
            rsp.pluginMismatches = std::move(luaMismatches);
            rsp.rejectReason = "Authoritative Lua content mismatch: "
                + contentFileRejectReason(rsp.pluginMismatches, mModChecksHelpUrl);
            Log(Debug::Warning) << "[Handshake] Rejecting " << hs.playerName << ": " << rsp.rejectReason;
            sendTo(c.conn, rsp.encode());
            mInterface->CloseConnection(c.conn, 0, "Authoritative Lua content mismatch", true);
            return;
        }
    }

    if (mModChecksEnabled)
    {
        auto mismatches = validateContentFiles(
            hs.plugins, mRequiredContentFiles, mModChecksStrictOrder, mModChecksRequireExactList);
        if (!mismatches.empty())
        {
            PacketHandshakeResponse rsp;
            rsp.accepted = false;
            rsp.serverVersion = MultiplayerBuildVersion;
            rsp.actorSyncProtocolVersion = ActorSyncProtocolVersionV2;
            rsp.pluginMismatches = std::move(mismatches);
            rsp.rejectReason = contentFileRejectReason(rsp.pluginMismatches, mModChecksHelpUrl);
            Log(Debug::Warning) << "[Handshake] Rejecting " << hs.playerName << ": " << rsp.rejectReason;
            sendTo(c.conn, rsp.encode());
            mInterface->CloseConnection(c.conn, 0, "Content-file mismatch", true);
            return;
        }

        if (!mRequiredLuaScripts.empty())
        {
            auto luaMismatches = validateContentFiles(hs.luaScripts, mRequiredLuaScripts, true, true);
            if (!luaMismatches.empty())
            {
                PacketHandshakeResponse rsp;
                rsp.pluginMismatches = std::move(luaMismatches);
                rsp.rejectReason = "Lua content mismatch: "
                    + contentFileRejectReason(rsp.pluginMismatches, mModChecksHelpUrl);
                Log(Debug::Warning) << "[Handshake] Rejecting " << hs.playerName << ": " << rsp.rejectReason;
                sendTo(c.conn, rsp.encode());
                mInterface->CloseConnection(c.conn, 0, "Lua content mismatch", true);
                return;
            }
        }
    }

    if (lowerAscii(hs.resolvedContentFingerprint) != mResolvedContentFingerprint)
    {
        PacketHandshakeResponse rsp;
        rsp.rejectReason = hs.resolvedContentFingerprint.empty()
            ? "Resolved content fingerprint is missing"
            : "Resolved content fingerprint mismatch";
        Log(Debug::Warning) << "[Handshake] Rejecting " << hs.playerName << ": " << rsp.rejectReason;
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Resolved content mismatch", true);
        return;
    }

    // -- Ed25519 keypair path --------------------------------------------------
    // If the client presents a public key and it maps to a known account,
    // issue a challenge instead of asking for a password.
    if (mPlayerDb && !hs.publicKey.empty())
    {
        try
        {
            const int64_t accountId = mPlayerDb->lookupAccountByKeypair(hs.publicKey);
            if (accountId >= 0)
            {
                // Recognised key - generate a random 32-byte challenge nonce.
                // Store the challenge in ConnectedClient so handleChallengeResponse
                // can verify the signature.
                std::memset(c.pendingChallenge, 0, 32);
#ifdef _WIN32
                typedef BOOLEAN (WINAPI *PfnRtlGenRandom)(void*, ULONG);
                static PfnRtlGenRandom rng = nullptr;
                if (!rng) rng = reinterpret_cast<PfnRtlGenRandom>(
                    GetProcAddress(LoadLibraryA("advapi32.dll"), "SystemFunction036"));
                if (rng) rng(c.pendingChallenge, 32);
#else
                {   std::ifstream r("/dev/urandom", std::ios::binary);
                    r.read(reinterpret_cast<char*>(c.pendingChallenge), 32); }
#endif
                c.pendingPublicKey = hs.publicKey;
                // Store login name so the accept path (in handleChallengeResponse)
                // can set c.loginName.
                c.loginName = hs.playerName.empty()
                    ? mPlayerDb->getUsernameForAccount(accountId)
                    : hs.playerName;
                c.dbAccountId = accountId;

                PacketChallenge pkt;
                std::memcpy(pkt.nonce, c.pendingChallenge, 32);
                sendTo(c.conn, pkt.encode());
                Log(Debug::Info) << "[Auth] Keypair challenge sent to " << c.loginName;
                return; // wait for PacketChallengeResponse
            }
            // Unknown key - reject immediately with a clear message.
            // The client sent a keypair auth request (empty passwordHash) so
            // falling through to password auth would always fail with
            // "Incorrect password" which is misleading.
            Log(Debug::Warning) << "[Auth] Keypair not recognised for conn=" << c.conn;
            PacketHandshakeResponse rsp;
            rsp.accepted     = false;
            rsp.rejectReason = "Key not recognised on this server. Please log in with your password to re-link.";
            sendTo(c.conn, rsp.encode());
            mInterface->CloseConnection(c.conn, 0, "Unknown key", true);
            return;
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[Auth] Keypair lookup error: " << e.what();
        }
    }

    // -- Authentication --------------------------------------------------------
    if (mPlayerDb)
    {
        try
        {
            if (hs.isRegistration)
            {
                // Registration: username must not already exist
                if (mPlayerDb->lookupAccount(hs.playerName) >= 0)
                {
                    PacketHandshakeResponse rsp;
                    rsp.accepted     = false;
                    rsp.rejectReason = "Username '" + hs.playerName + "' is already taken.";
                    sendTo(c.conn, rsp.encode());
                    mInterface->CloseConnection(c.conn, 0, "Username taken", true);
                    return;
                }
                // Hash the password and create the account
                const std::string hash = Bcrypt::hash(hs.passwordHash);
                const int64_t accountId = mPlayerDb->createAccount(hs.playerName);
                mPlayerDb->setPasswordHash(accountId, hash);
                Log(Debug::Info) << "[Auth] Registered new account: " << hs.playerName;
            }
            else
            {
                // Login: account must exist and password must match
                const int64_t accountId = mPlayerDb->lookupAccount(hs.playerName);
                if (accountId < 0)
                {
                    PacketHandshakeResponse rsp;
                    rsp.accepted     = false;
                    rsp.rejectReason = "Account not found. Did you mean to register?";
                    sendTo(c.conn, rsp.encode());
                    mInterface->CloseConnection(c.conn, 0, "Account not found", true);
                    return;
                }
                const std::string storedHash = mPlayerDb->getPasswordHash(accountId);
                if (storedHash.empty() || !Bcrypt::verify(hs.passwordHash, storedHash))
                {
                    PacketHandshakeResponse rsp;
                    rsp.accepted     = false;
                    rsp.rejectReason = "Incorrect password.";
                    sendTo(c.conn, rsp.encode());
                    mInterface->CloseConnection(c.conn, 0, "Bad password", true);
                    return;
                }
                Log(Debug::Info) << "[Auth] Login verified: " << hs.playerName;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[Auth] Auth error for " << hs.playerName << ": " << e.what();
            PacketHandshakeResponse rsp;
            rsp.accepted     = false;
            rsp.rejectReason = "Server authentication error. Please try again.";
            sendTo(c.conn, rsp.encode());
            mInterface->CloseConnection(c.conn, 0, "Auth error", true);
            return;
        }
    }

    // -- Accept - look up or create the player's character record -------------
    c.loginName         = hs.playerName;
    c.name              = hs.playerName;  // overwritten to charName after charselect
    c.player.guid       = c.guid;
    c.player.name       = hs.playerName;
    c.handshakeComplete = true;

    // Resolve account id - needed for CharacterList and later for CharacterSelect.
    if (mPlayerDb)
    {
        try { c.dbAccountId = mPlayerDb->lookupOrCreateAccount(hs.playerName); }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] account lookup error: " << e.what();
        }
    }

    // First player to complete handshake becomes the weather host.
    if (mWorld.hostGuid == 0)
        mWorld.hostGuid = c.guid;

    // Send the minimal handshake acceptance (no chargen data - that comes
    // via PacketCharacterData after the player picks a character).
    PacketHandshakeResponse rsp;
    rsp.accepted      = true;
    rsp.assignedGuid  = c.guid;
    rsp.serverVersion = MultiplayerBuildVersion;
    rsp.actorSyncProtocolVersion = c.actorSyncProtocolVersion;
    populateRuntimeManifest(rsp);
    sendTo(c.conn, rsp.encode());
    sendServerLuaPackages(c.conn);

    Log(Debug::Info) << "[Server] Handshake accepted: " << c.name
                     << " guid=" << c.guid
                     << " actorSyncProtocol=" << c.actorSyncProtocolVersion;

    // Build and send the character list so the client can show the
    // CharacterSelectDialog with one row per character.
    PacketCharacterList charListPkt;
    if (mPlayerDb && c.dbAccountId > 0)
    {
        try
        {
            for (const auto& cs : mPlayerDb->listCharacters(c.dbAccountId))
            {
                CharacterEntry entry;
                entry.name      = cs.name;
                entry.race      = cs.race;
                entry.className = cs.className;
                entry.lastSeen  = cs.lastSeen;
                entry.isNew     = cs.isNew;
                charListPkt.characters.push_back(std::move(entry));
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] listCharacters error: " << e.what();
        }
    }
    sendTo(c.conn, charListPkt.encode());
    Log(Debug::Info) << "[Server] Sent " << charListPkt.characters.size()
                     << " character(s) to " << c.name;
}

// ---------------------------------------------------------------------------
void MPServer::handleCharacterSelect(ConnectedClient& c, const uint8_t* data, size_t size)
{
    if (!c.handshakeComplete)
        return;

    PacketCharacterSelect sel;
    if (!sel.decode(data, size)) return;

    // Reject empty names - the old "" = new shorthand is gone.
    if (sel.charName.empty())
    {
        PacketCharacterSelectError err;
        err.reason = "Character name cannot be empty.";
        sendTo(c.conn, err.encode());
        return;
    }

    // Basic name validation: 2-24 printable ASCII characters.
    if (sel.charName.size() < 2 || sel.charName.size() > 24
        || sel.charName.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyz"
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "0123456789 '-") != std::string::npos)
    {
        PacketCharacterSelectError err;
        err.reason = "Invalid character name. Use 2-24 letters, numbers, spaces, hyphens, or apostrophes.";
        sendTo(c.conn, err.encode());
        return;
    }

    PacketCharacterData cdPkt;
    auto applyDefaultSpawn = [this](PacketCharacterData& pkt) {
        pkt.spawnCell = mDefaultSpawnCell;
        if (!mHasDefaultSpawnPosition)
            return;

        pkt.spawnX = mDefaultSpawnPosition.pos[0];
        pkt.spawnY = mDefaultSpawnPosition.pos[1];
        pkt.spawnZ = mDefaultSpawnPosition.pos[2];
        pkt.spawnRotX = mDefaultSpawnPosition.rot[0];
        pkt.spawnRotY = mDefaultSpawnPosition.rot[1];
        pkt.spawnRotZ = mDefaultSpawnPosition.rot[2];
    };
    applyDefaultSpawn(cdPkt);
    bool sendSavedInventory = false;
    bool sendSavedEquipment = false;
    bool sendSavedSpellbook = false;
    c.dbChargenCompletePending = false;
    c.hasRestoredStatsSnapshot = false;
    c.acceptedPlayerStatsThisSession = false;
    c.playerStatsRestoreGuardUntilMs = 0;
    c.playerDeathRestoreGuardUntilMs = 0;
    c.restoredInventorySnapshot.clear();
    c.restoredEquipmentSnapshot = {};
    c.hasRestoredInventorySnapshot = false;
    c.hasRestoredEquipmentSnapshot = false;
    c.acceptedPlayerInventoryThisSession = false;
    c.acceptedPlayerEquipmentThisSession = false;
    c.playerInventoryRestoreGuardUntilMs = 0;
    c.playerEquipmentRestoreGuardUntilMs = 0;
    c.lastPlayerInventoryRestoreCorrectionLogMs = 0;
    c.lastPlayerInventoryInstanceCorrectionLogMs = 0;
    c.lastPlayerEquipmentRestoreCorrectionLogMs = 0;
    c.lastPlayerEquipmentInstanceCorrectionLogMs = 0;
    c.pendingInventoryTransfers.clear();
    c.restoredSpellbookSnapshot.clear();
    c.hasRestoredSpellbookSnapshot = false;
    c.acceptedPlayerSpellbookThisSession = false;
    c.spellbookRevision = 0;
    c.playerSpellbookRestoreGuardUntilMs = 0;
    c.lastPlayerSpellbookRestoreCorrectionLogMs = 0;
    c.player.spellbookChanges.action = BasePlayer::SpellbookChanges::Action::Set;
    c.player.spellbookChanges.spellIds.clear();
    c.player.spellbookChanges.revision = 0;
    c.player.crimeState = {};
    c.player.factionState = {};
    c.player.bounty = 0;

    for (int slot = 0; slot < BasePlayer::NUM_EQUIPMENT_SLOTS; ++slot)
        c.player.equipment[slot].slot = slot;

    if (mPlayerDb && c.dbAccountId > 0)
    {
        try
        {
            if (sel.isNew)
            {
                // Enforce per-account character limit (0 = unlimited).
                if (mMaxCharsPerAccount > 0)
                {
                    const auto existing = mPlayerDb->listCharacters(c.dbAccountId);
                    if ((int)existing.size() >= mMaxCharsPerAccount)
                    {
                        PacketCharacterSelectError err;
                        err.reason = "Character limit reached ("
                                   + std::to_string(mMaxCharsPerAccount)
                                   + " per account). Delete a character to create a new one.";
                        sendTo(c.conn, err.encode());
                        return;
                    }
                }

                // New character slot - name must not already exist on this account.
                if (mPlayerDb->characterNameTaken(c.dbAccountId, sel.charName))
                {
                    PacketCharacterSelectError err;
                    err.reason = "You already have a character named '" + sel.charName + "'.";
                    sendTo(c.conn, err.encode());
                    return;
                }
                const PlayerRecord rec = mPlayerDb->createCharacter(c.dbAccountId, sel.charName);
                c.dbCharacterId      = rec.characterId;
                cdPkt.isNewCharacter = true;
                c.dbChargenCompletePending = true;
                applyDefaultSpawn(cdPkt);
                cdPkt.characterName  = sel.charName;
                for (const auto& mark : mDefaultPlayerMarks)
                    mPlayerDb->upsertCharacterMark(rec.characterId, mark);
                Log(Debug::Info) << "[Server] New character slot '" << sel.charName
                                 << "' created for " << c.name;
                if (!mDefaultPlayerMarks.empty())
                    Log(Debug::Info) << "[Server] Added " << mDefaultPlayerMarks.size()
                                     << " default mark(s) to '" << sel.charName << "'";
            }
            else
            {
                // Existing character - check it isn't already in use by a live session.
                for (const auto& [existingConn, existingClient] : mClients)
                {
                    if (existingConn != c.conn
                        && existingClient.charSelectComplete
                        && existingClient.loginName == c.loginName
                        && existingClient.slotName == sel.charName)
                    {
                        PacketCharacterSelectError err;
                        err.reason = "'" + sel.charName + "' is already in use by another session.";
                        sendTo(c.conn, err.encode());
                        return;
                    }
                }

                // Look up by (account_id, name).
                auto rec = mPlayerDb->lookupCharacter(c.dbAccountId, sel.charName);
                if (!rec)
                {
                    PacketCharacterSelectError err;
                    err.reason = "Character '" + sel.charName + "' not found on this account.";
                    sendTo(c.conn, err.encode());
                    return;
                }
                c.dbCharacterId      = rec->characterId;
                c.dbChargenCompletePending = rec->isNew;
                cdPkt.isNewCharacter = rec->isNew;
                cdPkt.hasSavedSpellbook = !rec->isNew && rec->hasSavedSpellbook;
                c.player.charGenComplete = !rec->isNew;
                if (rec->cell.empty())
                {
                    applyDefaultSpawn(cdPkt);
                }
                else
                {
                    cdPkt.spawnCell = rec->cell;
                    cdPkt.spawnX    = rec->posX;
                    cdPkt.spawnY    = rec->posY;
                    cdPkt.spawnZ    = rec->posZ;
                    cdPkt.spawnRotX = rec->rotX;
                    cdPkt.spawnRotY = rec->rotY;
                    cdPkt.spawnRotZ = rec->rotZ;
                }

                cdPkt.race      = rec->race;
                cdPkt.headMesh  = rec->headMesh;
                cdPkt.hairMesh  = rec->hairMesh;
                cdPkt.isMale    = rec->isMale;
                cdPkt.classId   = rec->classId;
                cdPkt.className = rec->className;
                cdPkt.birthSign = rec->birthSign;
                cdPkt.classData = rec->classData;

                if (!rec->isNew && rec->hasSavedInventory)
                {
                    c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
                    c.player.inventoryChanges.items = mPlayerDb->loadCharacterInventory(rec->characterId);
                    c.restoredInventorySnapshot = c.player.inventoryChanges.items;
                    c.hasRestoredInventorySnapshot = true;
                    c.playerInventoryRestoreGuardUntilMs = currentServerTimeMs() + 5000;
                    sendSavedInventory = true;
                }

                if (!rec->isNew && rec->hasSavedEquipment)
                {
                    for (auto& slotEntry : c.player.equipment)
                        slotEntry.item = {};

                    for (const auto& entry : mPlayerDb->loadCharacterEquipment(rec->characterId))
                    {
                        if (entry.slot < 0 || entry.slot >= BasePlayer::NUM_EQUIPMENT_SLOTS)
                            continue;
                        c.player.equipment[entry.slot] = entry;
                    }
                    c.restoredEquipmentSnapshot = c.player.equipment;
                    c.hasRestoredEquipmentSnapshot = true;
                    c.playerEquipmentRestoreGuardUntilMs = currentServerTimeMs() + 5000;
                    sendSavedEquipment = true;
                }

                if (!rec->isNew && rec->hasSavedSpellbook)
                {
                    c.player.spellbookChanges.action = BasePlayer::SpellbookChanges::Action::Set;
                    c.player.spellbookChanges.spellIds = mPlayerDb->loadCharacterSpellbook(rec->characterId);
                    c.restoredSpellbookSnapshot = c.player.spellbookChanges.spellIds;
                    c.hasRestoredSpellbookSnapshot = true;
                    c.playerSpellbookRestoreGuardUntilMs = currentServerTimeMs() + 5000;
                    sendSavedSpellbook = true;

                    const std::size_t dynamicSpells = std::count_if(
                        c.player.spellbookChanges.spellIds.begin(), c.player.spellbookChanges.spellIds.end(),
                        [&](const std::string& id) { return id.starts_with(mGeneratedRecordIdPrefix + "_"); });
                    Log(Debug::Info) << "[Spellbook] restored player=" << sel.charName
                                     << " learned=" << c.player.spellbookChanges.spellIds.size()
                                     << " dynamic=" << dynamicSpells;
                }

                if (sendSavedEquipment && equippedItemCount(c.player.equipment) > 0)
                {
                    if (!sendSavedInventory)
                    {
                        c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
                        c.player.inventoryChanges.items.clear();
                    }

                    if (ensureInventoryContainsEquippedItems(c.player))
                    {
                        c.restoredInventorySnapshot = c.player.inventoryChanges.items;
                        c.hasRestoredInventorySnapshot = true;
                        c.playerInventoryRestoreGuardUntilMs = currentServerTimeMs() + 5000;
                        sendSavedInventory = true;

                        if (mPlayerDb && c.dbCharacterId != 0)
                            mPlayerDb->saveCharacterInventory(c.dbCharacterId, c.player.inventoryChanges.items, false);

                        Log(Debug::Info) << "[PlayerDB] repaired restored inventory missing equipped item"
                                         << " charId=" << c.dbCharacterId
                                         << " name=" << sel.charName
                                         << " stacks=" << c.player.inventoryChanges.items.size()
                                         << " equipped=" << equippedItemCount(c.player.equipment);
                    }
                }

                if (sendSavedInventory)
                {
                    const bool assignedIds
                        = reconcileInventoryInstanceIds(c, c.player.inventoryChanges.items);
                    c.restoredInventorySnapshot = c.player.inventoryChanges.items;
                    if (assignedIds && mPlayerDb && c.dbCharacterId != 0)
                        mPlayerDb->saveCharacterInventory(c.dbCharacterId, c.player.inventoryChanges.items, false);
                }
                if (sendSavedEquipment)
                {
                    reconcileEquipmentInstanceIds(c);
                    c.restoredEquipmentSnapshot = c.player.equipment;
                    if (mPlayerDb && c.dbCharacterId != 0)
                    {
                        const std::vector<EquipmentItem> equipment(
                            c.player.equipment.begin(), c.player.equipment.end());
                        mPlayerDb->saveCharacterEquipment(c.dbCharacterId, equipment, false);
                    }
                }

                if (!rec->isNew && rec->hasSavedStats && mPlayerDb->loadCharacterStats(rec->characterId, c.player))
                {
                    cdPkt.hasSavedStats = true;
                    cdPkt.dynamicStats = c.player.dynamicStats;
                    cdPkt.attributes = c.player.attributes;
                    cdPkt.skills = c.player.skills;
                    cdPkt.level = c.player.level;
                    cdPkt.levelProgress = c.player.levelProgress;
                    c.restoredStatsSnapshot = c.player;
                    c.hasRestoredStatsSnapshot = true;
                    c.playerStatsRestoreGuardUntilMs = currentServerTimeMs() + 5000;
                    c.playerDeathRestoreGuardUntilMs = currentServerTimeMs() + 5000;
                    const Attribute& strength = c.player.attributes[0];
                    const Skill& blunt = c.player.skills[4];
                    if (strength.base > 100 || blunt.base > 100.f)
                    {
                        Log(Debug::Info) << "[PlayerDB] loaded persistent player stats"
                                         << " charId=" << rec->characterId
                                         << " name=" << sel.charName
                                         << " strength=" << strength.base
                                         << " blunt=" << blunt.base
                                         << " hp=" << c.player.dynamicStats.health.current
                                         << "/" << c.player.dynamicStats.health.base;
                    }
                }
                cdPkt.characterName  = sel.charName;
                Log(Debug::Info) << "[Server] Character '" << sel.charName
                                 << "' selected for " << c.name
                                 << " (dbNew=" << rec->isNew
                                 << ", responseNew=" << cdPkt.isNewCharacter << ")";
                if (rec->isNew)
                {
                    Log(Debug::Info) << "[Server] Resuming incomplete chargen for '" << sel.charName
                                     << "' race=" << rec->race
                                     << " class=" << rec->className
                                     << " birthSign=" << rec->birthSign;
                }
            }
            c.inventoryRevision = mPlayerDb->loadInventoryRevision(c.dbCharacterId);
            c.player.inventoryChanges.revision = c.inventoryRevision;
            c.spellbookRevision = mPlayerDb->loadSpellbookRevision(c.dbCharacterId);
            c.player.spellbookChanges.revision = c.spellbookRevision;
            c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
            c.player.factionState = mPlayerDb->loadPlayerFactionState(c.dbCharacterId);
            c.player.topicState = mPlayerDb->loadPlayerTopicState(c.dbCharacterId);
            c.player.bounty = c.player.crimeState.bounty;
            mPlayerDb->touch(c.dbCharacterId);
            mLua.setPlayerMarks(c.guid, mPlayerDb->loadCharacterMarks(c.dbCharacterId));
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] CharacterSelect error: " << e.what();
            PacketCharacterSelectError err;
            err.reason = "Server error processing character selection.";
            sendTo(c.conn, err.encode());
            return;
        }
    }
    else
    {
        // No DB - run as new character (dev/offline mode).
        cdPkt.isNewCharacter  = true;
        cdPkt.characterName   = sel.charName;
        applyDefaultSpawn(cdPkt);
        if (mDefaultPlayerMarks.empty())
            mLua.clearPlayerMarks(c.guid);
        else
            mLua.setPlayerMarks(c.guid, mDefaultPlayerMarks);
    }

    cdPkt.characterId = c.dbCharacterId;
    c.slotName = cdPkt.characterName;

    // Queue records and the persisted actor baseline before CharacterData.
    // CharacterData may overtake the default reliable lane because it shares
    // realtime actor lane 1 with ActorDeath. The ordered runtime-content marker
    // explicitly gates client world entry without weakening corpse bootstrap
    // ordering on the actor lane.
    // The normal sendCellStateToClient() below remains the post-selection catch-up.
    sendDynamicRecordsToClient(c.conn);
    PacketRuntimeContentBootstrapComplete runtimeContentComplete;
    sendTo(c.conn, runtimeContentComplete.encode());
    sendAuthoritativeJournal(c);
    if (!cdPkt.spawnCell.empty())
    {
        sendActorStateToClient(c.conn, cdPkt.spawnCell);
        Log(Debug::Verbose) << "[Server] Sent pre-world actor bootstrap"
                            << " to=" << sel.charName
                            << " cell=" << cdPkt.spawnCell;
    }

    // Semantic player state is distinct from runtime content, but both crime
    // faction state, and known topics are explicit world-entry prerequisites. They share the
    // ordered bootstrap lane with CharacterData so it cannot overtake them.
    sendAuthoritativeCrimeState(c);
    sendAuthoritativeFactionState(c);
    sendAuthoritativeTopicState(c);
    sendTo(c.conn, cdPkt.encode());
    c.charSelectComplete = true;

    // Update the display name now that the character slot is known.
    // slotName is the permanent DB key; name/player.name use nickname if set.
    if (!cdPkt.characterName.empty())
    {
        c.slotName = cdPkt.characterName;
        // Load nickname from the DB record (empty string if never set).
        if (mPlayerDb)
        {
            auto rec = mPlayerDb->lookupCharacter(c.dbAccountId, cdPkt.characterName);
            if (rec) c.nickname = rec->nickname;
        }
        const std::string displayName = c.nickname.empty() ? c.slotName : c.nickname;
        c.name        = displayName;
        c.player.name = displayName;
    }

    c.player.race = cdPkt.race;
    c.player.headMesh = cdPkt.headMesh;
    c.player.hairMesh = cdPkt.hairMesh;
    c.player.isMale = cdPkt.isMale;
    if (!cdPkt.classId.empty())
        c.player.charClass.mId = ESM::RefId::deserializeText(cdPkt.classId);
    c.player.charClass.mName = cdPkt.className;
    c.player.birthSign = cdPkt.birthSign;

    if (auto parsedCell = parseCellKey(cdPkt.spawnCell))
        c.player.cell = *parsedCell;
    c.player.position.pos[0] = cdPkt.spawnX;
    c.player.position.pos[1] = cdPkt.spawnY;
    c.player.position.pos[2] = cdPkt.spawnZ;
    c.player.position.rot[0] = cdPkt.spawnRotX;
    c.player.position.rot[1] = cdPkt.spawnRotY;
    c.player.position.rot[2] = cdPkt.spawnRotZ;
    c.player.position.isTeleporting = true;
    c.player.velocity = {};
    updateCollisionInterest(c);

    if (sendSavedInventory)
    {
        PacketPlayerInventory inventory;
        inventory.setPlayer(&c.player);
        sendTo(c.conn, inventory.encode());
        Log(Debug::Verbose) << "[PlayerDB] sent player inventory restore"
                            << " guid=" << c.guid
                            << " charId=" << c.dbCharacterId
                            << " name=" << c.slotName
                            << " stacks=" << c.player.inventoryChanges.items.size();
    }

    if (sendSavedEquipment)
    {
        PacketPlayerEquipment equipment;
        equipment.setPlayer(&c.player);
        sendTo(c.conn, equipment.encode());
        Log(Debug::Verbose) << "[PlayerDB] sent player equipment restore"
                            << " guid=" << c.guid
                            << " charId=" << c.dbCharacterId
                            << " name=" << c.slotName
                            << " equipped=" << equippedItemCount(c.player.equipment);
    }

    if (sendSavedSpellbook)
    {
        PacketPlayerSpellbook spellbook;
        spellbook.setPlayer(&c.player);
        sendTo(c.conn, spellbook.encode());
        Log(Debug::Info) << "[PlayerDB] sent player spellbook restore"
                         << " guid=" << c.player.guid
                         << " charId=" << c.dbCharacterId
                         << " name=" << c.slotName
                         << " revision=" << c.player.spellbookChanges.revision
                         << " spells=" << c.player.spellbookChanges.spellIds.size();
    }

    // Late-join catch-up: send state of all in-world players to the new joiner.
    for (auto& [existingConn, existingClient] : mClients)
    {
        if (existingConn == c.conn || !existingClient.charSelectComplete)
            continue;

        PacketPlayerBaseInfo baseInfo;
        baseInfo.setPlayer(&existingClient.player);
        sendTo(c.conn, baseInfo.encode());

        PacketPlayerCellChange cellChange;
        cellChange.setPlayer(&existingClient.player);
        sendTo(c.conn, cellChange.encode());

        PacketPlayerEquipment equipment;
        equipment.setPlayer(&existingClient.player);
        sendTo(c.conn, equipment.encode());

        PacketPlayerVehicleState vehicleState;
        vehicleState.setPlayer(&existingClient.player);
        sendTo(c.conn, vehicleState.encode());

        if (existingClient.player.position.pos[0] != 0.f
            || existingClient.player.position.pos[1] != 0.f
            || existingClient.player.position.pos[2] != 0.f)
        {
            PacketPlayerPosition positionPacket;
            positionPacket.setPlayer(&existingClient.player);
            sendTo(c.conn, positionPacket.encode());
        }
    }

    // Make already-connected clients aware of this player before the client's
    // first full sync arrives from the world.
    for (auto& [existingConn, existingClient] : mClients)
    {
        if (existingConn == c.conn || !existingClient.charSelectComplete)
            continue;

        PacketPlayerBaseInfo baseInfo;
        baseInfo.setPlayer(&c.player);
        sendTo(existingConn, baseInfo.encode());

        PacketPlayerCellChange cellChange;
        cellChange.setPlayer(&c.player);
        sendTo(existingConn, cellChange.encode());

        PacketPlayerEquipment equipment;
        equipment.setPlayer(&c.player);
        sendTo(existingConn, equipment.encode());

        PacketPlayerVehicleState vehicleState;
        vehicleState.setPlayer(&c.player);
        sendTo(existingConn, vehicleState.encode());

        PacketPlayerPosition positionPacket;
        positionPacket.setPlayer(&c.player);
        sendTo(existingConn, positionPacket.encode());
    }

    if (!cdPkt.spawnCell.empty())
        refreshActorAuthorityForCell(cdPkt.spawnCell, c.guid);

    sendTo(c.conn, buildWorldTimePacket());
    if (!cdPkt.spawnCell.empty())
        sendCellStateToClient(c.conn, cdPkt.spawnCell);

    if (mWorld.hasWeather)
        sendTo(c.conn, buildWorldWeatherPacket());

    syncLuaPlayerSnapshot();
    mLua.requestGlobalStorageSnapshot(c.guid);
    mLua.onPlayerConnect(c.guid, c.name);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void MPServer::handlePlayerCharGen(ConnectedClient& c, const uint8_t* data, size_t size)
{
    // Decode the packet - it now carries the full chargen result.
    PacketPlayerCharGen pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;

    if (mPlayerDb && c.dbCharacterId != 0)
    {
        try
        {
            // Persist race/class/birthsign so they can be restored on next login.
            mPlayerDb->saveChargenData(c.dbCharacterId,
                c.player.race,
                c.player.headMesh,
                c.player.hairMesh,
                c.player.isMale,
                c.player.charClass.mId.serializeText(),
                c.player.charClass.mName,
                c.player.birthSign,
                encodeClassData(c.player.charClass.mData));

            if (pkt.isComplete)
            {
                mPlayerDb->markChargenComplete(c.dbCharacterId);
                c.dbChargenCompletePending = false;
                c.player.charGenComplete = true;
                Log(Debug::Info) << "[Server] Chargen complete for " << c.name
                                 << " race=" << c.player.race
                                 << " class=" << c.player.charClass.mId.toString()
                                 << " birthSign=" << c.player.birthSign;
            }
            else
            {
                Log(Debug::Info) << "[Server] Chargen update for " << c.name
                                 << " race=" << c.player.race
                                 << " class=" << c.player.charClass.mId.toString()
                                 << " birthSign=" << c.player.birthSign;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] chargen save error: " << e.what();
        }
    }
}

void MPServer::handlePlayerBaseInfo(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerBaseInfo pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;

    // Stamp the server-authoritative display name (nickname if set, else character
    // name) before rebroadcasting.  The client sends its raw character name in its
    // own forceFullSync, but other clients must always see the canonical c.name.
    // This also keeps c.player.name in sync so the late-join catch-up loop
    // (which re-encodes from existingClient.player) sends the right name too.
    c.player.name = c.name;

    // Re-encode with the corrected name so all receivers get the nickname.
    broadcastToAll(pkt.encode(), c.conn);

    // Returning players do not include inventory/equipment in their first
    // client full sync; the server has the saved equipment snapshot.
    PacketPlayerEquipment equipment;
    equipment.setPlayer(&c.player);
    broadcastToAll(equipment.encode(), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerPosition(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer proposed = c.player;
    PacketPlayerPosition pkt;
    pkt.setPlayer(&proposed);
    if (!pkt.decode(data, size)) return;

    bool forceReliableTeleportRelay = false;

    if (c.pendingScriptedTeleportAck)
    {
        constexpr float kTeleportAckDistance = 512.f;
        const uint64_t nowMs = currentServerTimeMs();
        const float distanceSq = positionDistanceSquared(proposed.position, c.scriptedTeleportTarget);
        const bool reachedTeleportTarget = distanceSq <= kTeleportAckDistance * kTeleportAckDistance;

        if (reachedTeleportTarget)
        {
            c.pendingScriptedTeleportAck = false;
            c.scriptedTeleportGuardUntilMs = 0;
            proposed.position.isTeleporting = true;
            proposed.velocity = {};
            forceReliableTeleportRelay = true;
            Log(Debug::Verbose) << "[Server] Accepted post-teleport position ack from " << c.name
                                << " distance=" << std::sqrt(distanceSq);
        }
        else if (nowMs < c.scriptedTeleportGuardUntilMs)
        {
            if (c.lastScriptedTeleportRejectLogMs == 0 || nowMs - c.lastScriptedTeleportRejectLogMs >= 500)
            {
                c.lastScriptedTeleportRejectLogMs = nowMs;
                Log(Debug::Info) << "[Server] Ignoring stale pre-teleport position from " << c.name
                                 << " distance=" << std::sqrt(distanceSq);
            }

            PacketPlayerPosition correction;
            correction.setPlayer(&c.player);
            sendTo(c.conn, correction.encode());
            return;
        }
        else
        {
            c.pendingScriptedTeleportAck = false;
            c.scriptedTeleportGuardUntilMs = 0;
            Log(Debug::Warning) << "[Server] Teleport ack guard expired for " << c.name;
        }
    }

    // A passenger's world transform is derived from the driver's replicated
    // vehicle root. Ignore stale or malicious independent movement samples.
    if (c.player.vehicle.active
        && c.player.vehicle.occupantRole == VehicleOccupantRole::Passenger)
    {
        return;
    }

    if (!validateMovement(c, proposed))
    {
        // Send correction back
        PacketPlayerPosition correction;
        correction.setPlayer(&c.player);
        sendTo(c.conn, correction.encode());
        return;
    }

    c.player.position = proposed.position;
    c.player.velocity = proposed.velocity;
    c.player.positionSampleTimeUs = proposed.positionSampleTimeUs;
    c.player.vehicle.hasRigidBodyPose = proposed.vehicle.hasRigidBodyPose;
    c.player.vehicle.suspensionCompression = proposed.vehicle.suspensionCompression;
    c.player.vehicle.steeringAngle = proposed.vehicle.steeringAngle;
    updateCollisionInterest(c);

    if (c.player.vehicle.active
        && c.player.vehicle.occupantRole == VehicleOccupantRole::Driver)
    {
        for (auto& [connection, passenger] : mClients)
        {
            if (!passenger.player.vehicle.active
                || passenger.player.vehicle.occupantRole != VehicleOccupantRole::Passenger
                || passenger.player.vehicle.driverGuid != c.guid)
            {
                continue;
            }
            passenger.player.position = c.player.position;
            passenger.player.velocity = c.player.velocity;
            // This pose is derived from the driver, not sampled by the passenger.
            // Never put one process's steady-clock timestamp on another player.
            passenger.player.positionSampleTimeUs = 0;
            passenger.player.cell = c.player.cell;
            passenger.player.vehicle.hasRigidBodyPose = c.player.vehicle.hasRigidBodyPose;
            passenger.player.vehicle.suspensionCompression
                = c.player.vehicle.suspensionCompression;
            passenger.player.vehicle.steeringAngle = c.player.vehicle.steeringAngle;
            updateCollisionInterest(passenger);
        }
    }

    // Relay to all other clients (unreliable is fine - we use raw broadcast)
    if (forceReliableTeleportRelay)
    {
        PacketPlayerPosition relay;
        relay.setPlayer(&c.player);
        broadcastToAll(relay.encode(pkt.getSequence()), c.conn, /*reliable=*/true);
        c.player.position.isTeleporting = false;
    }
    else
        broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn, /*reliable=*/false);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerCellChange(ConnectedClient& c, const uint8_t* data, size_t size)
{
    const CellId oldCellState = c.player.cell;
    const Position oldPositionState = c.player.position;
    const Velocity oldVelocityState = c.player.velocity;
    std::string oldCell = makeCellKey(c.player.cell);
    PacketPlayerCellChange pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;
    const uint32_t cellChangeSequence = pkt.getSequence();

    if (c.player.vehicle.active
        && c.player.vehicle.occupantRole == VehicleOccupantRole::Passenger)
    {
        c.player.cell = oldCellState;
        c.player.position = oldPositionState;
        c.player.velocity = oldVelocityState;
        PacketPlayerCellChange correction;
        correction.setPlayer(&c.player);
        sendTo(c.conn, correction.encode(cellChangeSequence));
        return;
    }

    const std::string newCell = makeCellKey(c.player.cell);
    const int exteriorDx = oldCellState.isExterior && c.player.cell.isExterior
        ? std::abs(c.player.cell.gridX - oldCellState.gridX)
        : 0;
    const int exteriorDy = oldCellState.isExterior && c.player.cell.isExterior
        ? std::abs(c.player.cell.gridY - oldCellState.gridY)
        : 0;
    const bool senderMarkedTeleport = c.player.position.isTeleporting;
    const bool continuousExteriorCrossing = !senderMarkedTeleport
        && oldCellState.isExterior && c.player.cell.isExterior
        && exteriorDx <= 1 && exteriorDy <= 1;
    const bool preserveLocomotion = continuousExteriorCrossing || (!senderMarkedTeleport && oldCell == newCell);
    if (!preserveLocomotion)
    {
        c.player.velocity = {};
        c.player.position.isTeleporting = true;
    }

    Log(Debug::Info) << "[Server] " << c.name << " â†’ cell: " << newCell;
    Log(Debug::Info) << "[MPDIAG] PlayerCellChange continuity"
                     << " player=" << c.slotName
                     << " oldCell=" << oldCell
                     << " newCell=" << newCell
                     << " exteriorDx=" << exteriorDx
                     << " exteriorDy=" << exteriorDy
                     << " senderTeleport=" << senderMarkedTeleport
                     << " preserveLocomotion=" << preserveLocomotion
                     << " velocity=" << c.player.velocity.linear[0] << ","
                     << c.player.velocity.linear[1] << "," << c.player.velocity.linear[2]
                     << " sequence=" << cellChangeSequence;

    updateCollisionInterest(c);

    if (oldCell == newCell)
    {
        Log(Debug::Verbose) << "[Server] Ignoring same-cell PlayerCellChange side effects for "
                            << c.name << " cell=" << newCell
                            << " sequence=" << cellChangeSequence;

        PacketPlayerPosition positionPacket;
        positionPacket.setPlayer(&c.player);
        broadcastToAll(positionPacket.encode(cellChangeSequence), c.conn);
        c.player.position.isTeleporting = false;
        return;
    }

    syncLuaPlayerSnapshot();
    mLua.onPlayerCellChange(c.guid, c.name, newCell, oldCell);

    if (!oldCell.empty() && oldCell != newCell)
        refreshActorAuthorityForCell(oldCell);
    if (!newCell.empty())
        refreshActorAuthorityForCell(newCell, c.guid, false);

    // A guard's durable enforcement identity survives the offender leaving the
    // cell, but the live Pursue/Combat package must not cross hard world-space
    // transitions. Ordinary adjacent exterior crossings are continuous: both
    // cells remain active and native AiPursue can keep following the target
    // across the grid boundary without an artificial clear/reissue cycle.
    if (!oldCell.empty() && oldCell != newCell && !continuousExteriorCrossing)
        suspendOutstandingCrimePursuitsForCharacterInCell(c, oldCell);

    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);
    {
        PacketPlayerPosition positionPacket;
        positionPacket.setPlayer(&c.player);
        broadcastToAll(positionPacket.encode(cellChangeSequence), c.conn);
    }

    // The offender's persistent character identity survives cell exits and relogs.
    // Re-issue any guard enforcement only after peers have received the cell/position
    // change, so an actor-authority client can resolve the correct remote player Ptr.
    if (!newCell.empty())
    {
        dispatchOutstandingCrimePursuitsForCell(newCell, c.dbCharacterId);
        c.crimePursuitReissueHandledCell = newCell;
    }

    if (c.player.vehicle.active
        && c.player.vehicle.occupantRole == VehicleOccupantRole::Driver)
    {
        for (auto& [connection, passenger] : mClients)
        {
            if (!passenger.player.vehicle.active
                || passenger.player.vehicle.occupantRole != VehicleOccupantRole::Passenger
                || passenger.player.vehicle.driverGuid != c.guid)
            {
                continue;
            }

            passenger.player.cell = c.player.cell;
            passenger.player.position = c.player.position;
            passenger.player.velocity = c.player.velocity;
            updateCollisionInterest(passenger);

            PacketPlayerCellChange passengerCell;
            passengerCell.setPlayer(&passenger.player);
            broadcastToAll(passengerCell.encode(cellChangeSequence));
            if (!newCell.empty())
                sendCellStateToClient(passenger.conn, newCell);
        }
    }
    const std::string cellKey = makeCellKey(c.player.cell);
    if (!cellKey.empty() && c.loadedActorCells.find(cellKey) == c.loadedActorCells.end())
        sendCellStateToClient(c.conn, cellKey);
    sendPlayerStateBootstrapToClient(c);
}

void MPServer::handlePlayerLoadedCells(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerLoadedCells pkt;
    if (!pkt.decode(data, size))
        return;

    if (!c.charSelectComplete)
        return;

    if (pkt.sequence != 0 && c.loadedActorCellsSequence != 0
        && pkt.sequence <= c.loadedActorCellsSequence)
    {
        Log(Debug::Verbose) << "[Server] Ignoring stale PlayerLoadedCells from " << c.name
                            << " seq=" << pkt.sequence
                            << " current=" << c.loadedActorCellsSequence;
        return;
    }

    const std::string currentCell = makeCellKey(c.player.cell);
    if (currentCell.empty())
        return;

    if (!pkt.activeCellId.empty())
    {
        const auto parsedActive = parseCellKey(pkt.activeCellId);
        if (parsedActive && makeCellKey(*parsedActive) != currentCell)
        {
            Log(Debug::Verbose) << "[Server] PlayerLoadedCells active cell mismatch for " << c.name
                                << ": packet=" << pkt.activeCellId
                                << " server=" << currentCell;
        }
    }

    std::unordered_set<std::string> normalizedCells;
    normalizedCells.reserve(std::min<std::size_t>(pkt.loadedCellIds.size() + 1, MaxLoadedActorCells));

    const bool currentIsExterior = c.player.cell.isExterior;
    for (const std::string& rawCellId : pkt.loadedCellIds)
    {
        if (normalizedCells.size() >= MaxLoadedActorCells)
            break;

        const auto parsedCell = parseCellKey(rawCellId);
        if (!parsedCell)
            continue;

        const std::string normalizedCellId = makeCellKey(*parsedCell);
        if (normalizedCellId.empty())
            continue;

        if (!currentIsExterior)
        {
            if (normalizedCellId != currentCell)
                continue;
        }
        else if (!parsedCell->isExterior)
            continue;

        normalizedCells.insert(normalizedCellId);
    }

    normalizedCells.insert(currentCell);
    for (auto it = normalizedCells.begin(); normalizedCells.size() > MaxLoadedActorCells && it != normalizedCells.end();)
    {
        if (*it == currentCell)
        {
            ++it;
            continue;
        }
        it = normalizedCells.erase(it);
    }

    std::vector<std::string> addedCells;
    std::vector<std::string> removedCells;
    for (const std::string& cellId : normalizedCells)
    {
        if (c.loadedActorCells.find(cellId) == c.loadedActorCells.end())
            addedCells.push_back(cellId);
    }
    for (const std::string& cellId : c.loadedActorCells)
    {
        if (normalizedCells.find(cellId) == normalizedCells.end())
            removedCells.push_back(cellId);
    }

    c.loadedActorCells = std::move(normalizedCells);
    c.loadedActorCellsSequence = pkt.sequence;

    const bool currentCellReissueAlreadyHandled = c.crimePursuitReissueHandledCell == currentCell;
    bool currentCellAuthorityChanged = false;
    for (const std::string& cellId : addedCells)
    {
        const bool suppressPursuitReissue = cellId == currentCell && currentCellReissueAlreadyHandled;
        const bool authorityChanged = refreshActorAuthorityForCell(cellId, c.guid, !suppressPursuitReissue);
        if (cellId == currentCell)
            currentCellAuthorityChanged = authorityChanged;
        // Loaded-cell actor interest is handled by the authority refresh above.
        // Re-sending the full actor baseline here duplicates every adjacent-cell
        // actor snapshot and can stall the client's network frame during startup.
        // Adjacent cells still need their persisted objects, containers and doors
        // so exterior interactions remain consistent before the border is crossed.
        sendCellObjectStateToClient(c.conn, cellId);
    }
    for (const std::string& cellId : removedCells)
        refreshActorAuthorityForCell(cellId);

    const bool currentCellAdded
        = std::find(addedCells.begin(), addedCells.end(), currentCell) != addedCells.end();
    if (currentCellAdded && !currentCellAuthorityChanged && !currentCellReissueAlreadyHandled)
        dispatchOutstandingCrimePursuitsForCell(currentCell, c.dbCharacterId);
    c.crimePursuitReissueHandledCell.clear();

    Log(Debug::Verbose) << "[Server] PlayerLoadedCells from " << c.name
                        << " active=" << currentCell
                        << " loaded=" << c.loadedActorCells.size()
                        << " added=" << addedCells.size()
                        << " removed=" << removedCells.size()
                        << " seq=" << pkt.sequence;
    syncLuaPlayerSnapshot();
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerEquipment(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming = c.player;
    PacketPlayerEquipment pkt;
    pkt.setPlayer(&incoming);
    if (!pkt.decode(data, size)) return;

    for (const EquipmentItem& entry : incoming.equipment)
    {
        if (!entry.item.refId.empty() && !isAuthoritativeRecordReference(entry.item.refId))
        {
            Log(Debug::Warning) << "[Server] Rejected PlayerEquipment unknown generated record from=" << c.name
                                << " refId=" << entry.item.refId;
            sendAuthoritativeEquipment(c, true, true);
            return;
        }
    }

    const uint64_t nowMs = currentServerTimeMs();
    if (c.hasRestoredEquipmentSnapshot
        && !c.acceptedPlayerEquipmentThisSession
        && nowMs < c.playerEquipmentRestoreGuardUntilMs
        && looksLikeRestoredEquipmentRegression(incoming.equipment, c.restoredEquipmentSnapshot))
    {
        if (c.lastPlayerEquipmentRestoreCorrectionLogMs == 0
            || nowMs - c.lastPlayerEquipmentRestoreCorrectionLogMs >= 1000)
        {
            c.lastPlayerEquipmentRestoreCorrectionLogMs = nowMs;
            Log(Debug::Info) << "[PlayerDB] ignored startup player equipment overwrite"
                             << " charId=" << c.dbCharacterId
                             << " name=" << c.slotName
                             << " incomingEquipped=" << equippedItemCount(incoming.equipment)
                             << " restoredEquipped=" << equippedItemCount(c.restoredEquipmentSnapshot);
        }

        BasePlayer correction = c.player;
        correction.guid = c.guid;
        PacketPlayerEquipment correctionPkt;
        correctionPkt.setPlayer(&correction);
        sendTo(c.conn, correctionPkt.encode());
        return;
    }

    c.player.equipment = incoming.equipment;
    const bool correctedInstanceIds = reconcileEquipmentInstanceIds(c);
    if (correctedInstanceIds
        && (c.lastPlayerEquipmentInstanceCorrectionLogMs == 0
            || nowMs - c.lastPlayerEquipmentInstanceCorrectionLogMs >= 1000))
    {
        c.lastPlayerEquipmentInstanceCorrectionLogMs = nowMs;
        for (int slot = 0; slot < BasePlayer::NUM_EQUIPMENT_SLOTS; ++slot)
        {
            const Item& requested = incoming.equipment[slot].item;
            const Item& reconciled = c.player.equipment[slot].item;
            if (requested.instanceId == reconciled.instanceId)
                continue;
            Log(Debug::Info) << "[MPDIAG] PlayerEquipment instance correction"
                             << " player=" << c.slotName
                             << " slot=" << slot
                             << " refId=" << requested.refId
                             << " requested=" << requested.instanceId
                             << " reconciled=" << reconciled.instanceId;
            break;
        }
    }
    c.acceptedPlayerEquipmentThisSession = true;
    c.restoredEquipmentSnapshot = c.player.equipment;
    c.hasRestoredEquipmentSnapshot = true;
    c.playerEquipmentRestoreGuardUntilMs = 0;

    if (mPlayerDb && c.dbCharacterId != 0)
    {
        try
        {
            std::vector<EquipmentItem> equipment(c.player.equipment.begin(), c.player.equipment.end());
            mPlayerDb->saveCharacterEquipment(c.dbCharacterId, equipment);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] saveCharacterEquipment error: " << e.what();
        }
    }

    scheduleGeneratedDynamicRecordGc("player_equipment");
    // A normal equipment packet already describes the sender's live state.
    // Reconciliation below only normalizes hidden inventory identity metadata;
    // it must not visually re-equip the originating client. Doing so can never
    // make a transient local RefNum stick and creates an echo/restore/ack loop.
    // Persist and relay the reconciled identity to peers, while startup/login
    // corrections continue to use the explicit self-directed path above.
    sendAuthoritativeEquipment(c, true, false);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerAnimFlags(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerAnimFlags pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;
    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn, /*reliable=*/false);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerAnimPlay(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerAnimPlay pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;
    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerAttack(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerAttack pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;
    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerCast(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerCast pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;
    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);
}

// ---------------------------------------------------------------------------
bool MPServer::reconcileInventoryInstanceIds(ConnectedClient& c, std::vector<Item>& items)
{
    const uint64_t nowMs = currentServerTimeMs();
    c.pendingInventoryTransfers.erase(
        std::remove_if(c.pendingInventoryTransfers.begin(), c.pendingInventoryTransfers.end(),
            [&](const ConnectedClient::PendingInventoryTransfer& transfer) { return transfer.expiresAtMs < nowMs; }),
        c.pendingInventoryTransfers.end());

    const std::vector<Item>& previous = c.player.inventoryChanges.items;
    std::unordered_set<uint32_t> used;
    bool changed = false;
    std::size_t invalidSuppliedIds = 0;
    std::size_t recoveredPreviousIds = 0;
    std::size_t recoveredTransferIds = 0;
    std::size_t allocatedIds = 0;
    std::string firstCorrectedRefId;
    uint32_t firstRequestedId = 0;
    uint32_t firstAssignedId = 0;

    for (Item& item : items)
    {
        if (item.refId.empty() || item.count <= 0)
            continue;

        const uint32_t requestedId = item.instanceId;
        bool itemIdentityChanged = false;
        auto previousById = std::find_if(previous.begin(), previous.end(), [&](const Item& old) {
            return item.instanceId != 0 && old.instanceId == item.instanceId && old.refId == item.refId;
        });
        auto transferById = std::find_if(c.pendingInventoryTransfers.begin(), c.pendingInventoryTransfers.end(),
            [&](const ConnectedClient::PendingInventoryTransfer& transfer) {
                return item.instanceId != 0 && transfer.instanceId == item.instanceId
                    && transfer.refId == item.refId;
            });
        const bool suppliedIdIsValid = item.instanceId != 0 && used.count(item.instanceId) == 0
            && (previousById != previous.end() || transferById != c.pendingInventoryTransfers.end());
        if (!suppliedIdIsValid && item.instanceId != 0)
        {
            item.instanceId = 0;
            changed = true;
            itemIdentityChanged = true;
            ++invalidSuppliedIds;
        }

        if (item.instanceId == 0)
        {
            const auto previousMatch = std::find_if(previous.begin(), previous.end(), [&](const Item& old) {
                return old.instanceId != 0 && used.count(old.instanceId) == 0 && sameItemIdentity(old, item);
            });
            if (previousMatch != previous.end())
            {
                item.instanceId = previousMatch->instanceId;
                itemIdentityChanged = true;
                ++recoveredPreviousIds;
            }
        }

        if (item.instanceId == 0)
        {
            const auto transfer = std::find_if(c.pendingInventoryTransfers.begin(), c.pendingInventoryTransfers.end(),
                [&](const ConnectedClient::PendingInventoryTransfer& pending) {
                    return pending.instanceId != 0 && used.count(pending.instanceId) == 0
                        && pending.refId == item.refId;
                });
            if (transfer != c.pendingInventoryTransfers.end())
            {
                item.instanceId = transfer->instanceId;
                transferById = transfer;
                changed = true;
                itemIdentityChanged = true;
                ++recoveredTransferIds;
            }
        }

        if (item.instanceId == 0)
        {
            const std::optional<uint32_t> allocated = reserveWorldMpNum();
            if (!allocated)
            {
                Log(Debug::Warning) << "[Server] Inventory instance ID space exhausted for " << c.name;
                continue;
            }
            item.instanceId = *allocated;
            changed = true;
            itemIdentityChanged = true;
            ++allocatedIds;
        }

        if (itemIdentityChanged && firstCorrectedRefId.empty())
        {
            firstCorrectedRefId = item.refId;
            firstRequestedId = requestedId;
            firstAssignedId = item.instanceId;
        }

        used.insert(item.instanceId);
        if (transferById != c.pendingInventoryTransfers.end())
            c.pendingInventoryTransfers.erase(transferById);
    }

    const bool reconciledAnyIdentity
        = invalidSuppliedIds != 0 || recoveredPreviousIds != 0 || recoveredTransferIds != 0 || allocatedIds != 0;
    if (reconciledAnyIdentity
        && (c.lastPlayerInventoryInstanceCorrectionLogMs == 0
            || nowMs - c.lastPlayerInventoryInstanceCorrectionLogMs >= 1000))
    {
        c.lastPlayerInventoryInstanceCorrectionLogMs = nowMs;
        Log(Debug::Info) << "[MPDIAG] PlayerInventory instance reconciliation"
                         << " player=" << c.slotName
                         << " incomingStacks=" << items.size()
                         << " previousStacks=" << previous.size()
                         << " invalidSupplied=" << invalidSuppliedIds
                         << " recoveredPrevious=" << recoveredPreviousIds
                         << " recoveredTransfer=" << recoveredTransferIds
                         << " allocated=" << allocatedIds
                         << " echoRequired=" << changed
                         << " firstRef=" << firstCorrectedRefId
                         << " firstRequested=" << firstRequestedId
                         << " firstAssigned=" << firstAssignedId;
    }

    return changed;
}

bool MPServer::reconcileEquipmentInstanceIds(ConnectedClient& c)
{
    bool changed = false;
    for (EquipmentItem& equipped : c.player.equipment)
    {
        if (equipped.item.refId.empty() || equipped.item.count <= 0)
        {
            changed = changed || equipped.item.instanceId != 0;
            equipped.item.instanceId = 0;
            continue;
        }

        auto inventoryItem = std::find_if(c.player.inventoryChanges.items.begin(),
            c.player.inventoryChanges.items.end(), [&](const Item& item) {
                return equipped.item.instanceId != 0 && item.instanceId == equipped.item.instanceId
                    && item.refId == equipped.item.refId;
            });
        if (inventoryItem == c.player.inventoryChanges.items.end())
        {
            inventoryItem = std::find_if(c.player.inventoryChanges.items.begin(),
                c.player.inventoryChanges.items.end(),
                [&](const Item& item) { return sameItemIdentity(item, equipped.item); });
        }
        const uint32_t reconciledInstanceId
            = inventoryItem != c.player.inventoryChanges.items.end() ? inventoryItem->instanceId : 0;
        changed = changed || equipped.item.instanceId != reconciledInstanceId;
        equipped.item.instanceId = reconciledInstanceId;
    }
    return changed;
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerInventory(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming = c.player;
    PacketPlayerInventory pkt;
    pkt.setPlayer(&incoming);
    if (!pkt.decode(data, size)) return;

    if (incoming.inventoryChanges.revision != c.inventoryRevision)
    {
        Log(Debug::Info) << "[Server] Rejected stale PlayerInventory from=" << c.name
                         << " expectedRevision=" << c.inventoryRevision
                         << " receivedRevision=" << incoming.inventoryChanges.revision;
        sendAuthoritativeInventory(c);
        return;
    }

    for (const Item& item : incoming.inventoryChanges.items)
    {
        if (!item.refId.empty() && !isAuthoritativeRecordReference(item.refId))
        {
            Log(Debug::Warning) << "[Server] Rejected PlayerInventory unknown generated record from=" << c.name
                                << " refId=" << item.refId;
            sendAuthoritativeInventory(c);
            return;
        }
    }

    using InventoryAction = BasePlayer::InventoryChanges::Action;
    auto sameStack = [](const Item& left, const Item& right) {
        return left.refId == right.refId
            && left.charge == right.charge
            && std::abs(left.enchantmentCharge - right.enchantmentCharge) < 0.001f
            && left.soul == right.soul;
    };

    const uint64_t nowMs = currentServerTimeMs();
    if (c.hasRestoredInventorySnapshot
        && !c.acceptedPlayerInventoryThisSession
        && nowMs < c.playerInventoryRestoreGuardUntilMs
        && looksLikeRestoredInventoryRegression(incoming.inventoryChanges, c.restoredInventorySnapshot))
    {
        if (c.lastPlayerInventoryRestoreCorrectionLogMs == 0
            || nowMs - c.lastPlayerInventoryRestoreCorrectionLogMs >= 1000)
        {
            c.lastPlayerInventoryRestoreCorrectionLogMs = nowMs;
            Log(Debug::Info) << "[PlayerDB] ignored startup player inventory overwrite"
                             << " charId=" << c.dbCharacterId
                             << " name=" << c.slotName
                             << " incomingItems=" << incoming.inventoryChanges.items.size()
                             << " restoredItems=" << c.restoredInventorySnapshot.size();
        }

        BasePlayer correction = c.player;
        correction.guid = c.guid;
        correction.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
        PacketPlayerInventory correctionPkt;
        correctionPkt.setPlayer(&correction);
        sendTo(c.conn, correctionPkt.encode());
        return;
    }

    std::vector<Item> nextItems = c.player.inventoryChanges.items;

    if (incoming.inventoryChanges.action == InventoryAction::Set)
    {
        nextItems = incoming.inventoryChanges.items;
    }
    else if (incoming.inventoryChanges.action == InventoryAction::Add)
    {
        for (const auto& item : incoming.inventoryChanges.items)
        {
            auto it = std::find_if(
                nextItems.begin(),
                nextItems.end(),
                [&](const Item& existing) { return sameStack(existing, item); });
            if (it != nextItems.end())
                it->count += item.count;
            else
                nextItems.push_back(item);
        }
    }
    else if (incoming.inventoryChanges.action == InventoryAction::Remove)
    {
        for (const auto& item : incoming.inventoryChanges.items)
        {
            auto it = std::find_if(
                nextItems.begin(),
                nextItems.end(),
                [&](const Item& existing) { return sameStack(existing, item); });
            if (it == nextItems.end())
                continue;

            it->count -= item.count;
            if (it->count <= 0)
                nextItems.erase(it);
        }
    }

    reconcileInventoryInstanceIds(c, nextItems);
    c.player.inventoryChanges.action = InventoryAction::Set;
    c.player.inventoryChanges.items = std::move(nextItems);
    ++c.inventoryRevision;
    c.player.inventoryChanges.revision = c.inventoryRevision;

    c.acceptedPlayerInventoryThisSession = true;
    c.restoredInventorySnapshot = c.player.inventoryChanges.items;
    c.hasRestoredInventorySnapshot = true;
    c.playerInventoryRestoreGuardUntilMs = 0;

    if (mPlayerDb && c.dbCharacterId != 0)
    {
        try
        {
            mPlayerDb->saveCharacterInventory(
                c.dbCharacterId, c.player.inventoryChanges.items, true, c.inventoryRevision);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] saveCharacterInventory error: " << e.what();
        }
    }

    syncLuaPlayerSnapshot();
    scheduleGeneratedDynamicRecordGc("player_inventory");
    PacketPlayerInventory authoritative;
    authoritative.setPlayer(&c.player);
    const std::vector<uint8_t> encoded = authoritative.encode();
    // Ordinary client inventory changes already describe the sender's live
    // inventory. Echoing the full Set snapshot makes the client rebuild every
    // stack for routine mutations such as consuming one arrow. Only return a
    // snapshot when the server actually assigned or corrected hidden identity
    // metadata; peers still receive the accepted authoritative state.
    // The self reply acknowledges the new optimistic concurrency revision.
    // Client-side application already coalesces authoritative snapshots.
    sendTo(c.conn, encoded);
    broadcastToAll(encoded, c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerSpellbook(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming = c.player;
    PacketPlayerSpellbook pkt;
    pkt.setPlayer(&incoming);
    if (!pkt.decode(data, size))
    {
        Log(Debug::Warning) << "[Spellbook] rejected player=" << c.name
                            << " error=" << spellbookErrorName(SpellbookError::MalformedRequest);
        return;
    }

    using SpellbookAction = BasePlayer::SpellbookChanges::Action;

    if (incoming.spellbookChanges.revision != c.spellbookRevision)
    {
        Log(Debug::Info) << "[Spellbook] rejected player=" << c.name
                         << " error=" << spellbookErrorName(SpellbookError::StaleSpellbookRevision)
                         << " expectedRevision=" << c.spellbookRevision
                         << " receivedRevision=" << incoming.spellbookChanges.revision;
        sendAuthoritativeSpellbook(c);
        return;
    }

    const uint64_t nowMs = currentServerTimeMs();
    if (c.hasRestoredSpellbookSnapshot
        && !c.acceptedPlayerSpellbookThisSession
        && nowMs < c.playerSpellbookRestoreGuardUntilMs
        && looksLikeRestoredSpellbookRegression(incoming.spellbookChanges, c.restoredSpellbookSnapshot))
    {
        if (c.lastPlayerSpellbookRestoreCorrectionLogMs == 0
            || nowMs - c.lastPlayerSpellbookRestoreCorrectionLogMs >= 1000)
        {
            c.lastPlayerSpellbookRestoreCorrectionLogMs = nowMs;
            Log(Debug::Info) << "[PlayerDB] ignored startup player spellbook overwrite"
                             << " charId=" << c.dbCharacterId
                             << " name=" << c.slotName
                             << " incomingSpells=" << incoming.spellbookChanges.spellIds.size()
                             << " restoredSpells=" << c.restoredSpellbookSnapshot.size();
        }
        sendAuthoritativeSpellbook(c);
        return;
    }

    // Validate every proposed spell ID before mutating any state. Content IDs
    // must resolve to an ST_Spell record in the authoritative content store;
    // generated-looking IDs must be catalog-known persistent dynamic spell
    // records. The client can never introduce a spell definition.
    std::string firstRejectedSpellId;
    SpellbookError validationError = SpellbookError::None;
    for (const std::string& spellId : incoming.spellbookChanges.spellIds)
    {
        const SpellbookError error = validateSpellbookSpellId(spellId,
            mGeneratedRecordIdPrefix + "_",
            [&](const std::string& id) -> const ESM::Spell* {
                if (!mContentRegistry)
                    return nullptr;
                return mContentRegistry->store().get<ESM::Spell>().search(ESM::RefId::stringRefId(id));
            },
            [&](const std::string& id) -> std::optional<bool> {
                const auto it = mWorld.dynamicRecords.find(makeDynamicRecordKey("spell", id));
                if (it == mWorld.dynamicRecords.end())
                    return std::nullopt;
                return it->second.persistent;
            });
        if (error != SpellbookError::None)
        {
            validationError = error;
            firstRejectedSpellId = spellId;
            break;
        }
    }

    if (validationError != SpellbookError::None)
    {
        Log(Debug::Warning) << "[Spellbook] rejected player=" << c.name
                            << " error=" << spellbookErrorName(validationError)
                            << " spellId=" << firstRejectedSpellId;
        sendAuthoritativeSpellbook(c);
        return;
    }

    const std::vector<std::string> previous = c.player.spellbookChanges.spellIds;
    std::vector<std::string> nextSpellbook = applySpellbookAction(
        incoming.spellbookChanges.action, previous, incoming.spellbookChanges.spellIds);

    if (nextSpellbook.size() > MAX_SPELLBOOK_SIZE)
    {
        Log(Debug::Warning) << "[Spellbook] rejected player=" << c.name
                            << " error=" << spellbookErrorName(SpellbookError::TooManySpells)
                            << " count=" << nextSpellbook.size();
        sendAuthoritativeSpellbook(c);
        return;
    }

    // An idempotent mutation (already-known Add, absent Remove, identical Set)
    // changes nothing: acknowledge without advancing the revision so the
    // client's optimistic-concurrency token stays valid.
    const bool changed = nextSpellbook != previous;
    c.player.spellbookChanges.action = SpellbookAction::Set;
    c.player.spellbookChanges.spellIds = std::move(nextSpellbook);
    if (changed)
        ++c.spellbookRevision;
    c.player.spellbookChanges.revision = c.spellbookRevision;

    c.acceptedPlayerSpellbookThisSession = true;
    c.restoredSpellbookSnapshot = c.player.spellbookChanges.spellIds;
    c.hasRestoredSpellbookSnapshot = true;
    c.playerSpellbookRestoreGuardUntilMs = 0;

    if (changed && mPlayerDb && c.dbCharacterId != 0)
    {
        try
        {
            mPlayerDb->saveCharacterSpellbook(
                c.dbCharacterId, c.player.spellbookChanges.spellIds, true, c.spellbookRevision);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] saveCharacterSpellbook error: " << e.what();
        }
    }

    if (changed)
        scheduleGeneratedDynamicRecordGc("player_spellbook");

    // The authoritative reply doubles as the mutation acknowledgement; it
    // releases the client's in-flight gate and reconciles any drift. The
    // spellbook is private per-character state, so it is never broadcast.
    sendAuthoritativeSpellbook(c);

    if (!changed)
    {
        Log(Debug::Verbose) << "[Spellbook] accepted no-op player=" << c.name
                            << " action=" << static_cast<int>(incoming.spellbookChanges.action)
                            << " spells=" << c.player.spellbookChanges.spellIds.size()
                            << " revision=" << c.spellbookRevision;
        return;
    }

    if (incoming.spellbookChanges.action == SpellbookAction::Add)
    {
        for (const std::string& spellId : incoming.spellbookChanges.spellIds)
        {
            if (std::find(previous.begin(), previous.end(), spellId) == previous.end())
            {
                Log(Debug::Info) << "[Spellbook] accepted player=" << c.name
                                 << " action=add spellId=" << spellId
                                 << " revision=" << c.spellbookRevision;
            }
        }
    }
    else if (incoming.spellbookChanges.action == SpellbookAction::Remove)
    {
        for (const std::string& spellId : incoming.spellbookChanges.spellIds)
        {
            if (std::find(previous.begin(), previous.end(), spellId) != previous.end())
            {
                Log(Debug::Info) << "[Spellbook] removed player=" << c.name
                                 << " spellId=" << spellId
                                 << " revision=" << c.spellbookRevision;
            }
        }
    }
    else
    {
        Log(Debug::Info) << "[Spellbook] accepted player=" << c.name
                         << " action=set spells=" << c.player.spellbookChanges.spellIds.size()
                         << " revision=" << c.spellbookRevision;
    }
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerTopic(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming;
    PacketPlayerTopic packet;
    packet.setPlayer(&incoming);
    if (!packet.decode(data, size) || packet.action != PacketPlayerTopic::Action::Add
        || incoming.guid != c.guid || !mPlayerDb || c.dbCharacterId <= 0)
    {
        Log(Debug::Warning) << "[PlayerTopic] rejected malformed or unauthorized proposal player=" << c.name;
        sendAuthoritativeTopicState(c);
        return;
    }

    const auto& dialogues = mContentRegistry->store().get<ESM::Dialogue>();
    for (const std::string& id : incoming.topicState.knownTopicIds)
    {
        const ESM::Dialogue* dialogue = dialogues.search(ESM::RefId::stringRefId(id));
        if (!dialogue || dialogue->mType != ESM::Dialogue::Topic)
        {
            Log(Debug::Warning) << "[PlayerTopic] rejected invalid topic player=" << c.name
                                << " topic=" << id;
            sendAuthoritativeTopicState(c);
            return;
        }
    }

    try
    {
        const TopicMutationResult result = mPlayerDb->addKnownTopics(
            c.dbCharacterId, incoming.topicState.revision, incoming.topicState.knownTopicIds);
        c.player.topicState = result.state;
        sendAuthoritativeTopicState(c);
        Log(result.status == TopicMutationStatus::StaleRevision ? Debug::Warning : Debug::Info)
            << "[PlayerTopic] "
            << (result.status == TopicMutationStatus::Committed ? "accepted"
                : result.status == TopicMutationStatus::Idempotent ? "idempotent" : "stale")
            << " player=" << c.name << " revision=" << result.state.revision
            << " topics=" << result.state.knownTopicIds.size();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[PlayerTopic] persistence failure player=" << c.name << " error=" << e.what();
        c.player.topicState = mPlayerDb->loadPlayerTopicState(c.dbCharacterId);
        sendAuthoritativeTopicState(c);
    }
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerFaction(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming;
    PacketPlayerFaction packet;
    packet.setPlayer(&incoming);
    if (!packet.decode(data, size) || packet.mode != PacketPlayerFaction::Mode::Proposal
        || incoming.guid != c.guid || !packet.request.expectedRevision
        || packet.request.source != "client:faction-observation" || !c.charSelectComplete
        || !mPlayerDb || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        Log(Debug::Warning) << "[FactionService] rejected malformed or unauthorized proposal player=" << c.name;
        sendAuthoritativeFactionState(
            c, packet.request.requestId, false, FactionError::InvalidRequest);
        return;
    }

    try
    {
        FactionService service(*mPlayerDb);
        FactionService::Context context;
        context.accountId = c.dbAccountId;
        context.characterId = c.dbCharacterId;
        context.findFaction = [this](std::string_view id)
            -> std::optional<FactionService::FactionDefinition> {
            const ESM::Faction* faction = mContentRegistry->store().get<ESM::Faction>().search(
                ESM::RefId::stringRefId(id));
            if (!faction)
                return std::nullopt;
            FactionService::FactionDefinition definition;
            definition.validRanks.resize(faction->mRanks.size());
            for (std::size_t index = 0; index < faction->mRanks.size(); ++index)
                definition.validRanks[index] = index == 0 || !faction->mRanks[index].empty();
            return definition;
        };

        const FactionService::Outcome outcome = service.execute(std::move(packet.request), context);
        // Durable replays describe their original transition. Always send the
        // latest row so a delayed duplicate cannot roll the client backward.
        c.player.factionState = mPlayerDb->loadPlayerFactionState(c.dbCharacterId);
        sendAuthoritativeFactionState(c, outcome.result.requestId,
            outcome.result.accepted, outcome.result.error);
        Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
            << "[FactionService] request=" << outcome.result.requestId
            << " player=" << c.name
            << " accepted=" << outcome.result.accepted
            << " replayed=" << outcome.replayed
            << " error=" << getFactionErrorCode(outcome.result.error)
            << " revision=" << c.player.factionState.revision
            << " factions=" << c.player.factionState.factions.size();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[FactionService] persistence failure player=" << c.name
                          << " error=" << e.what();
        c.player.factionState = mPlayerDb->loadPlayerFactionState(c.dbCharacterId);
        sendAuthoritativeFactionState(
            c, packet.request.requestId, false, FactionError::PersistenceFailure);
    }
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerBounty(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming;
    PacketPlayerBounty packet;
    packet.setPlayer(&incoming);
    if (!packet.decode(data, size) || packet.mode != PacketPlayerBounty::Mode::Proposal
        || incoming.guid != c.guid || !packet.request.expectedRevision
        || !c.charSelectComplete || !mPlayerDb || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        Log(Debug::Warning) << "[CrimeService] rejected malformed or unauthorized proposal player=" << c.name;
        sendAuthoritativeCrimeState(c, packet.request.requestId, false, CrimeError::InvalidRequest);
        return;
    }

    const std::string_view source = packet.request.source;
    if (source != "mwscript:setpccrimelevel" && source != "mwscript:modpccrimelevel"
        && source != "openmw-lua:setcrimelevel")
    {
        Log(Debug::Warning) << "[CrimeService] rejected unsupported client source player=" << c.name
                            << " source=" << source;
        sendAuthoritativeCrimeState(c, packet.request.requestId, false, CrimeError::Unauthorized);
        return;
    }
    packet.request.source = "client:" + packet.request.source;

    try
    {
        CrimeService service(*mPlayerDb);
        CrimeService::Context context;
        context.accountId = c.dbAccountId;
        context.characterId = c.dbCharacterId;
        const CrimeService::Outcome outcome = service.execute(packet.request, context);
        c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
        c.player.bounty = c.player.crimeState.bounty;
        sendAuthoritativeCrimeState(c, outcome.result.requestId,
            outcome.result.accepted, outcome.result.error);
        syncLuaPlayerSnapshot();
        Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
            << "[CrimeService] client request=" << outcome.result.requestId
            << " player=" << c.name << " source=" << packet.request.source
            << " accepted=" << outcome.result.accepted << " replayed=" << outcome.replayed
            << " error=" << getCrimeErrorCode(outcome.result.error)
            << " bounty=" << c.player.crimeState.bounty
            << " revision=" << c.player.crimeState.revision;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[CrimeService] client mutation persistence failure player=" << c.name
                          << " error=" << e.what();
        c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
        sendAuthoritativeCrimeState(c, packet.request.requestId, false, CrimeError::PersistenceFailure);
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleRecordCreateRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketRecordCreateRequest packet;
    if (!packet.decode(data, size))
    {
        PacketRecordCreateResult errorPacket;
        errorPacket.result = DynamicRecordService::makeError(
            {}, records::CreateError::InvalidRequest, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }

    if (!mPlayerDb || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        PacketRecordCreateResult errorPacket;
        errorPacket.result = DynamicRecordService::makeError(packet.request.requestId,
            records::CreateError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }

    // Hash the decoded application payload with a neutral packet sequence. A
    // transport retry may legitimately use another packet sequence while still
    // representing the same idempotent request.
    const std::vector<uint8_t> canonicalRequest = packet.encode();
    const std::string requestHash = crypto::sha256hex(std::string_view(
        reinterpret_cast<const char*>(canonicalRequest.data()), canonicalRequest.size()));

    DynamicRecordService::Context context;
    context.accountId = c.dbAccountId;
    context.characterId = c.dbCharacterId;
    context.inventoryRevision = c.inventoryRevision;
    context.creationSource = "client_lua";
    bool isReplay = false;
    try
    {
        isReplay = mPlayerDb->loadCraftRequest(
            c.dbAccountId, c.dbCharacterId, packet.request.requestId).has_value();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] RecordCreate journal lookup failed: " << e.what();
        PacketRecordCreateResult errorPacket;
        errorPacket.result = DynamicRecordService::makeError(packet.request.requestId,
            records::CreateError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }
    if (!isReplay)
    {
        const uint64_t now = currentServerTimeMs();
        if (c.runtimeRecordRateWindowStartMs == 0
            || now - c.runtimeRecordRateWindowStartMs >= 60000)
        {
            c.runtimeRecordRateWindowStartMs = now;
            c.runtimeRecordRequestsInWindow = 0;
        }
        if (mRuntimeRecordRequestsPerMinute == 0
            || c.runtimeRecordRequestsInWindow >= mRuntimeRecordRequestsPerMinute)
            context.admissionError = records::CreateError::RateLimited;
        else
            ++c.runtimeRecordRequestsInWindow;
    }
    const std::string packageId = lowerAscii(packet.request.scriptPackageId);
    auto capability = mRuntimeRecordCapabilities.find(packageId);
    if (capability != mRuntimeRecordCapabilities.end())
    {
        context.allowCustomDefinitions = true;
        context.permittedTypes = capability->second;
        context.creationSource += ":" + packageId;
    }

    try
    {
        const std::vector<DynamicRecordCatalogEntry> catalog = mPlayerDb->loadDynamicRecordCatalog();
        const std::size_t owned = static_cast<std::size_t>(std::count_if(catalog.begin(), catalog.end(),
            [&](const DynamicRecordCatalogEntry& entry) {
                return entry.creatorCharacterId == c.dbCharacterId;
            }));
        context.maximumNewRecords = owned >= mRuntimeRecordMaxPerCharacter
            ? 0
            : mRuntimeRecordMaxPerCharacter - owned;
        context.isAssetAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasAsset(normalizeRuntimeAsset(asset));
        };
        context.isModelAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasModel(normalizeRuntimeAsset(asset));
        };
        context.isIconAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasIcon(normalizeRuntimeAsset(asset));
        };
        context.isContentIdAllowed = [&](std::string_view id) {
            const std::string normalized = lowerAscii(id);
            if (mContentRegistry->hasContentId(normalized))
                return true;
            if (!normalized.starts_with(lowerAscii(mGeneratedRecordIdPrefix) + "_"))
                return false;
            return std::any_of(catalog.begin(), catalog.end(), [&](const DynamicRecordCatalogEntry& entry) {
                return lowerAscii(entry.recordId) == normalized;
            });
        };

        DynamicRecordService service(*mPlayerDb);
        auto outcome = service.execute(packet.request, requestHash, context,
            [&](records::RecordType type, std::string_view fingerprint)
                -> std::optional<DynamicRecordService::CatalogRecord> {
                const std::string typeName(records::getRecordTypeName(type));
                for (const DynamicRecordCatalogEntry& catalogEntry : catalog)
                {
                    if (catalogEntry.recordType != typeName
                        || catalogEntry.definitionFingerprint != fingerprint)
                        continue;
                    auto stored = mWorld.dynamicRecords.find(
                        makeDynamicRecordKey(typeName, catalogEntry.recordId));
                    if (stored == mWorld.dynamicRecords.end())
                        continue;
                    return DynamicRecordService::CatalogRecord{ typeName, catalogEntry.recordId,
                        catalogEntry.definitionFingerprint, stored->second.data };
                }
                return std::nullopt;
            },
            [&](records::RecordType type) {
                return mLua.generateDynamicRecordId(std::string(records::getRecordTypeName(type)));
            },
            [&]() { return mWorld.nextDynamicRecordSequence++; });

        if (outcome.result.accepted && !outcome.replayed)
        {
            for (const DynamicRecordService::CommittedRecord& created : outcome.newRecords)
            {
                WorldState::StoredDynamicRecord stored;
                stored.recordType = created.recordType;
                stored.recordId = created.recordId;
                stored.data = created.definition;
                stored.recordScope = "generated";
                stored.persistent = true;
                stored.sequence = outcome.result.commitSequence;
                stored.dependencyRecordIds = created.dependencyRecordIds;
                mWorld.dynamicRecords[makeDynamicRecordKey(stored.recordType, stored.recordId)] = std::move(stored);

                PacketRecordDynamic definitionPacket;
                definitionPacket.action = DynamicRecordAction::Upsert;
                definitionPacket.recordType = created.recordType;
                definitionPacket.entries.push_back({ created.recordId, created.definition });
                broadcastToAll(definitionPacket.encode());
            }
        }

        // Re-send every mapping to the requester before publishing the result.
        // This makes replay and deduplication safe even if the local store missed
        // an earlier broadcast; the client still enforces a local visibility barrier.
        if (outcome.result.accepted)
        {
            for (const records::CreatedRecord& created : outcome.result.records)
            {
                const records::DynamicRecordDefinition definition = records::decodeDefinition(created.definition);
                PacketRecordDynamic definitionPacket;
                definitionPacket.action = DynamicRecordAction::Upsert;
                definitionPacket.recordType
                    = std::string(records::getRecordTypeName(records::getRecordType(definition)));
                definitionPacket.entries.push_back({ created.recordId, created.definition });
                sendTo(c.conn, definitionPacket.encode());
            }
        }

        sendTo(c.conn, outcome.encodedResult);
        Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
            << "[Server] RecordCreate " << (outcome.result.accepted ? "accepted" : "rejected")
            << " player=" << c.slotName
            << " requestId=" << outcome.result.requestId
            << " operation=" << static_cast<int>(packet.request.operation)
            << " records=" << outcome.result.records.size()
            << " replayed=" << (outcome.replayed ? "true" : "false")
            << " error=" << records::getCreateErrorCode(outcome.result.error);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] RecordCreate internal error player=" << c.slotName
                          << " requestId=" << packet.request.requestId << " error=" << e.what();
        PacketRecordCreateResult errorPacket;
        errorPacket.result = DynamicRecordService::makeError(packet.request.requestId,
            records::CreateError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
    }
}

// ---------------------------------------------------------------------------
namespace
{
    std::string joinInstanceIds(const std::vector<std::uint32_t>& ids)
    {
        std::string joined;
        for (const std::uint32_t id : ids)
        {
            if (!joined.empty())
                joined.push_back(',');
            joined += std::to_string(id);
        }
        return joined;
    }

    void logAlchemyResult(const mwmp::ConnectedClient& c, const mwmp::records::AlchemyRequest& request,
        const mwmp::AlchemyService::Outcome& outcome)
    {
        std::size_t successes = 0;
        std::string firstPotionId;
        bool firstReused = false;
        for (const mwmp::records::AlchemyAttemptResult& attempt : outcome.result.attempts)
        {
            if (!attempt.success)
                continue;
            ++successes;
            if (firstPotionId.empty())
            {
                firstPotionId = attempt.recordId;
                firstReused = attempt.reused;
            }
        }
        Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
            << "[Alchemy] " << (outcome.result.accepted ? "accepted" : "rejected")
            << " player=" << c.slotName
            << " requestId=" << outcome.result.requestId
            << " expectedRevision=" << request.inventoryRevision
            << " ingredients=[" << joinInstanceIds(request.ingredientInstanceIds) << "]"
            << " apparatus=[" << joinInstanceIds(request.apparatusInstanceIds) << "]"
            << " attempts=" << outcome.result.attempts.size()
            << " successes=" << successes
            << " potionId=" << (firstPotionId.empty() ? "-" : firstPotionId)
            << " reused=" << (firstReused ? "true" : "false")
            << " newRecords=" << outcome.newRecords.size()
            << " resultingRevision=" << outcome.result.inventoryRevision
            << " replayed=" << (outcome.replayed ? "true" : "false")
            << " error=" << mwmp::records::getAlchemyErrorCode(outcome.result.error);
    }

    void logEnchantingResult(const mwmp::ConnectedClient& c, const mwmp::records::EnchantingRequest& request,
        const mwmp::EnchantingService::Outcome& outcome)
    {
        Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
            << "[Enchanting] " << (outcome.result.accepted ? "accepted" : "rejected")
            << " player=" << c.slotName
            << " requestId=" << outcome.result.requestId
            << " expectedRevision=" << request.inventoryRevision
            << " targetInstance=" << request.targetInstanceId
            << " gemInstance=" << request.soulGemInstanceId
            << " mode=" << (request.selfEnchanting ? "self" : "paid")
            << " castStyle=" << request.castStyle
            << " effects=" << request.effects.size()
            << " success=" << (outcome.result.success ? "true" : "false")
            << " enchantmentId=" << (outcome.result.enchantmentRecordId.empty() ? "-" : outcome.result.enchantmentRecordId)
            << " itemId=" << (outcome.result.itemRecordId.empty() ? "-" : outcome.result.itemRecordId)
            << " enchantmentReused=" << (outcome.result.enchantmentReused ? "true" : "false")
            << " itemReused=" << (outcome.result.itemReused ? "true" : "false")
            << " newRecords=" << outcome.newRecords.size()
            << " resultingRevision=" << outcome.result.inventoryRevision
            << " replayed=" << (outcome.replayed ? "true" : "false")
            << " error=" << mwmp::records::getEnchantingErrorCode(outcome.result.error);
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleAlchemyRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketAlchemyRequest packet;
    if (!packet.decode(data, size))
    {
        PacketAlchemyResult errorPacket;
        errorPacket.result
            = AlchemyService::makeError({}, records::AlchemyError::InvalidRequest, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }

    if (!mPlayerDb || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        PacketAlchemyResult errorPacket;
        errorPacket.result = AlchemyService::makeError(
            packet.request.requestId, records::AlchemyError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }

    // Hash the decoded application payload with a neutral packet sequence. A
    // transport retry may legitimately use another packet sequence while still
    // representing the same idempotent request.
    const std::vector<uint8_t> canonicalRequest = packet.encode();
    const std::string requestHash = crypto::sha256hex(std::string_view(
        reinterpret_cast<const char*>(canonicalRequest.data()), canonicalRequest.size()));

    AlchemyService::Context context;
    context.accountId = c.dbAccountId;
    context.characterId = c.dbCharacterId;
    context.inventoryRevision = c.inventoryRevision;
    context.player = &c.player;
    context.inventory = &c.player.inventoryChanges.items;
    context.store = &mContentRegistry->store();
    context.creationSource = "alchemy";
    context.recordScope = "generated";
    context.persistent = true;
    context.validationVersion = 1;

    bool isReplay = false;
    try
    {
        isReplay = mPlayerDb->loadCraftRequest(
            c.dbAccountId, c.dbCharacterId, packet.request.requestId).has_value();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] Alchemy journal lookup failed: " << e.what();
        PacketAlchemyResult errorPacket;
        errorPacket.result = AlchemyService::makeError(
            packet.request.requestId, records::AlchemyError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }
    if (!isReplay)
    {
        const uint64_t now = currentServerTimeMs();
        if (c.runtimeRecordRateWindowStartMs == 0 || now - c.runtimeRecordRateWindowStartMs >= 60000)
        {
            c.runtimeRecordRateWindowStartMs = now;
            c.runtimeRecordRequestsInWindow = 0;
        }
        if (mRuntimeRecordRequestsPerMinute == 0
            || c.runtimeRecordRequestsInWindow >= mRuntimeRecordRequestsPerMinute)
            context.admissionError = records::CreateError::RateLimited;
        else
            ++c.runtimeRecordRequestsInWindow;
    }

    try
    {
        const std::vector<DynamicRecordCatalogEntry> catalog = mPlayerDb->loadDynamicRecordCatalog();
        const std::size_t owned = static_cast<std::size_t>(std::count_if(catalog.begin(), catalog.end(),
            [&](const DynamicRecordCatalogEntry& entry) {
                return entry.creatorCharacterId == c.dbCharacterId;
            }));
        context.maximumNewRecords = owned >= mRuntimeRecordMaxPerCharacter
            ? 0
            : mRuntimeRecordMaxPerCharacter - owned;
        context.isAssetAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasAsset(normalizeRuntimeAsset(asset));
        };
        context.isModelAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasModel(normalizeRuntimeAsset(asset));
        };
        context.isIconAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasIcon(normalizeRuntimeAsset(asset));
        };
        context.isContentIdAllowed = [&](std::string_view id) {
            const std::string normalized = lowerAscii(id);
            if (mContentRegistry->hasContentId(normalized))
                return true;
            if (!normalized.starts_with(lowerAscii(mGeneratedRecordIdPrefix) + "_"))
                return false;
            return std::any_of(catalog.begin(), catalog.end(), [&](const DynamicRecordCatalogEntry& entry) {
                return lowerAscii(entry.recordId) == normalized;
            });
        };
        context.findEquivalent = [&](records::RecordType type, std::string_view fingerprint)
            -> std::optional<DynamicRecordService::CatalogRecord> {
            const std::string typeName(records::getRecordTypeName(type));
            for (const DynamicRecordCatalogEntry& catalogEntry : catalog)
            {
                if (catalogEntry.recordType != typeName
                    || catalogEntry.definitionFingerprint != fingerprint)
                    continue;
                auto stored = mWorld.dynamicRecords.find(
                    makeDynamicRecordKey(typeName, catalogEntry.recordId));
                if (stored == mWorld.dynamicRecords.end())
                    continue;
                return DynamicRecordService::CatalogRecord{ typeName, catalogEntry.recordId,
                    catalogEntry.definitionFingerprint, stored->second.data };
            }
            return std::nullopt;
        };
        context.allocateId = [&](records::RecordType type) {
            return mLua.generateDynamicRecordId(std::string(records::getRecordTypeName(type)));
        };
        context.nextCommitSequence = [&]() { return mWorld.nextDynamicRecordSequence++; };
        context.listDynamicPotions = [&]() {
            std::vector<std::pair<std::string, std::string>> potions;
            for (const auto& [key, record] : mWorld.dynamicRecords)
            {
                if (record.recordType != "potion")
                    continue;
                potions.emplace_back(record.recordId, record.data);
            }
            return potions;
        };
        context.reconcileInventory = [&](std::vector<Item>& items) {
            reconcileInventoryInstanceIds(c, items);
        };

        AlchemyService service(*mPlayerDb);
        AlchemyService::Outcome outcome = service.execute(packet.request, requestHash, context);

        if (outcome.committed)
        {
            // Install the authoritative gameplay state on the client mirror.
            c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
            c.player.inventoryChanges.items = std::move(outcome.resultingInventory);
            c.player.inventoryChanges.revision = outcome.resultingInventoryRevision;
            c.inventoryRevision = outcome.resultingInventoryRevision;
            c.acceptedPlayerInventoryThisSession = true;
            c.restoredInventorySnapshot = c.player.inventoryChanges.items;
            c.hasRestoredInventorySnapshot = true;

            if (outcome.resultingStats)
            {
                c.player = *outcome.resultingStats;
                c.restoredStatsSnapshot = c.player;
                c.hasRestoredStatsSnapshot = true;
                c.acceptedPlayerStatsThisSession = true;
                const int alchemyIndex = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
                c.alchemySkillSyncGuard = ConnectedClient::AlchemySkillSyncGuard{
                    c.player.skills[alchemyIndex].base,
                    c.player.skills[alchemyIndex].progress,
                    c.player.level,
                    c.player.levelProgress };
            }

            for (const DynamicRecordService::CommittedRecord& created : outcome.newRecords)
            {
                WorldState::StoredDynamicRecord stored;
                stored.recordType = created.recordType;
                stored.recordId = created.recordId;
                stored.data = created.definition;
                stored.recordScope = "generated";
                stored.persistent = true;
                stored.sequence = outcome.result.commitSequence;
                stored.dependencyRecordIds = created.dependencyRecordIds;
                mWorld.dynamicRecords[makeDynamicRecordKey(stored.recordType, stored.recordId)] = std::move(stored);

                PacketRecordDynamic definitionPacket;
                definitionPacket.action = DynamicRecordAction::Upsert;
                definitionPacket.recordType = created.recordType;
                definitionPacket.entries.push_back({ created.recordId, created.definition });
                broadcastToAll(definitionPacket.encode());
            }

            sendAuthoritativeInventory(c);
            if (outcome.resultingStats)
            {
                PacketPlayerStatsDynamic statsPacket;
                statsPacket.setPlayer(&c.player);
                const std::vector<uint8_t> encoded = statsPacket.encode();
                sendTo(c.conn, encoded);
                broadcastToAll(encoded, c.conn);
            }
            syncLuaPlayerSnapshot();
        }

        // Re-send every referenced definition to the requester before
        // publishing the result so replay and deduplication are safe even
        // if the local store missed an earlier broadcast; the client still
        // enforces a local visibility barrier. This also covers replayed
        // results after a reconnect.
        for (const records::AlchemyAttemptResult& attempt : outcome.result.attempts)
        {
            if (!attempt.success || attempt.recordId.empty())
                continue;
            const auto stored = mWorld.dynamicRecords.find(makeDynamicRecordKey("potion", attempt.recordId));
            if (stored == mWorld.dynamicRecords.end())
                continue;
            PacketRecordDynamic definitionPacket;
            definitionPacket.action = DynamicRecordAction::Upsert;
            definitionPacket.recordType = "potion";
            definitionPacket.entries.push_back({ stored->second.recordId, stored->second.data });
            sendTo(c.conn, definitionPacket.encode());
        }

        sendTo(c.conn, outcome.encodedResult);
        logAlchemyResult(c, packet.request, outcome);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] Alchemy internal error player=" << c.slotName
                          << " requestId=" << packet.request.requestId << " error=" << e.what();
        PacketAlchemyResult errorPacket;
        errorPacket.result = AlchemyService::makeError(
            packet.request.requestId, records::AlchemyError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleEnchantingRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketEnchantingRequest packet;
    if (!packet.decode(data, size))
    {
        PacketEnchantingResult errorPacket;
        errorPacket.result
            = EnchantingService::makeError({}, records::EnchantingError::InvalidRequest, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }

    if (!mPlayerDb || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        PacketEnchantingResult errorPacket;
        errorPacket.result = EnchantingService::makeError(
            packet.request.requestId, records::EnchantingError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }

    // Hash the decoded application payload with a neutral packet sequence. A
    // transport retry may legitimately use another packet sequence while still
    // representing the same idempotent request.
    const std::vector<uint8_t> canonicalRequest = packet.encode();
    const std::string requestHash = crypto::sha256hex(std::string_view(
        reinterpret_cast<const char*>(canonicalRequest.data()), canonicalRequest.size()));

    EnchantingService::Context context;
    context.accountId = c.dbAccountId;
    context.characterId = c.dbCharacterId;
    context.inventoryRevision = c.inventoryRevision;
    context.player = &c.player;
    context.inventory = &c.player.inventoryChanges.items;
    context.store = &mContentRegistry->store();
    context.creationSource = "enchanting";
    context.recordScope = "generated";
    context.persistent = true;
    context.validationVersion = 1;
    context.projectilesEnchantMultiplier = mEnchantProjectilesMultiplier;

    bool isReplay = false;
    try
    {
        isReplay = mPlayerDb->loadCraftRequest(
            c.dbAccountId, c.dbCharacterId, packet.request.requestId).has_value();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] Enchanting journal lookup failed: " << e.what();
        PacketEnchantingResult errorPacket;
        errorPacket.result = EnchantingService::makeError(
            packet.request.requestId, records::EnchantingError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
        return;
    }
    if (!isReplay)
    {
        const uint64_t now = currentServerTimeMs();
        if (c.runtimeRecordRateWindowStartMs == 0 || now - c.runtimeRecordRateWindowStartMs >= 60000)
        {
            c.runtimeRecordRateWindowStartMs = now;
            c.runtimeRecordRequestsInWindow = 0;
        }
        if (mRuntimeRecordRequestsPerMinute == 0
            || c.runtimeRecordRequestsInWindow >= mRuntimeRecordRequestsPerMinute)
            context.admissionError = records::CreateError::RateLimited;
        else
            ++c.runtimeRecordRequestsInWindow;
    }

    try
    {
        const std::vector<DynamicRecordCatalogEntry> catalog = mPlayerDb->loadDynamicRecordCatalog();
        const std::size_t owned = static_cast<std::size_t>(std::count_if(catalog.begin(), catalog.end(),
            [&](const DynamicRecordCatalogEntry& entry) {
                return entry.creatorCharacterId == c.dbCharacterId;
            }));
        context.maximumNewRecords = owned >= mRuntimeRecordMaxPerCharacter
            ? 0
            : mRuntimeRecordMaxPerCharacter - owned;
        context.isAssetAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasAsset(normalizeRuntimeAsset(asset));
        };
        context.isModelAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasModel(normalizeRuntimeAsset(asset));
        };
        context.isIconAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasIcon(normalizeRuntimeAsset(asset));
        };
        context.isContentIdAllowed = [&](std::string_view id) {
            const std::string normalized = lowerAscii(id);
            if (mContentRegistry->hasContentId(normalized))
                return true;
            if (!normalized.starts_with(lowerAscii(mGeneratedRecordIdPrefix) + "_"))
                return false;
            return std::any_of(catalog.begin(), catalog.end(), [&](const DynamicRecordCatalogEntry& entry) {
                return lowerAscii(entry.recordId) == normalized;
            });
        };
        context.findEquivalent = [&](records::RecordType type, std::string_view fingerprint)
            -> std::optional<DynamicRecordService::CatalogRecord> {
            const std::string typeName(records::getRecordTypeName(type));
            for (const DynamicRecordCatalogEntry& catalogEntry : catalog)
            {
                if (catalogEntry.recordType != typeName
                    || catalogEntry.definitionFingerprint != fingerprint)
                    continue;
                auto stored = mWorld.dynamicRecords.find(
                    makeDynamicRecordKey(typeName, catalogEntry.recordId));
                if (stored == mWorld.dynamicRecords.end())
                    continue;
                return DynamicRecordService::CatalogRecord{ typeName, catalogEntry.recordId,
                    catalogEntry.definitionFingerprint, stored->second.data };
            }
            return std::nullopt;
        };
        context.allocateId = [&](records::RecordType type) {
            return mLua.generateDynamicRecordId(std::string(records::getRecordTypeName(type)));
        };
        context.nextCommitSequence = [&]() { return mWorld.nextDynamicRecordSequence++; };
        context.listDynamicEnchantments = [&]() {
            std::vector<std::pair<std::string, std::string>> enchantments;
            for (const auto& [key, record] : mWorld.dynamicRecords)
            {
                if (record.recordType != "enchantment")
                    continue;
                enchantments.emplace_back(record.recordId, record.data);
            }
            return enchantments;
        };
        context.resolveEnchanter = [&](std::uint64_t actorNetId)
            -> std::optional<EnchantingService::Context::EnchanterInfo> {
            for (const auto& [cellId, cellState] : mWorld.actorCells)
            {
                for (const auto& [key, record] : cellState.actors)
                {
                    if (record.actorNetId != actorNetId || record.actor.refId.empty())
                        continue;
                    EnchantingService::Context::EnchanterInfo info;
                    info.refId = record.actor.refId;
                    info.dynamicStats = record.actor.dynamicStats;
                    info.cellLoaded = clientHasActorCellLoaded(c, cellId);
                    return info;
                }
            }
            return std::nullopt;
        };
        context.reconcileInventory = [&](std::vector<Item>& items) {
            reconcileInventoryInstanceIds(c, items);
        };

        EnchantingService service(*mPlayerDb);
        EnchantingService::Outcome outcome = service.execute(packet.request, requestHash, context);

        if (outcome.committed)
        {
            // Install the authoritative gameplay state on the client mirror.
            c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
            c.player.inventoryChanges.items = std::move(outcome.resultingInventory);
            c.player.inventoryChanges.revision = outcome.resultingInventoryRevision;
            c.inventoryRevision = outcome.resultingInventoryRevision;
            c.acceptedPlayerInventoryThisSession = true;
            c.restoredInventorySnapshot = c.player.inventoryChanges.items;
            c.hasRestoredInventorySnapshot = true;

            if (outcome.resultingStats)
            {
                c.player = *outcome.resultingStats;
                c.restoredStatsSnapshot = c.player;
                c.hasRestoredStatsSnapshot = true;
                c.acceptedPlayerStatsThisSession = true;
                const int enchantIndex = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
                c.enchantSkillSyncGuard = ConnectedClient::EnchantSkillSyncGuard{
                    c.player.skills[enchantIndex].base,
                    c.player.skills[enchantIndex].progress,
                    c.player.level,
                    c.player.levelProgress };
            }

            for (const DynamicRecordService::CommittedRecord& created : outcome.newRecords)
            {
                WorldState::StoredDynamicRecord stored;
                stored.recordType = created.recordType;
                stored.recordId = created.recordId;
                stored.data = created.definition;
                stored.recordScope = "generated";
                stored.persistent = true;
                stored.sequence = outcome.result.commitSequence;
                stored.dependencyRecordIds = created.dependencyRecordIds;
                mWorld.dynamicRecords[makeDynamicRecordKey(stored.recordType, stored.recordId)] = std::move(stored);

                PacketRecordDynamic definitionPacket;
                definitionPacket.action = DynamicRecordAction::Upsert;
                definitionPacket.recordType = created.recordType;
                definitionPacket.entries.push_back({ created.recordId, created.definition });
                broadcastToAll(definitionPacket.encode());
            }

            sendAuthoritativeInventory(c);
            if (outcome.resultingStats)
            {
                PacketPlayerStatsDynamic statsPacket;
                statsPacket.setPlayer(&c.player);
                const std::vector<uint8_t> encoded = statsPacket.encode();
                sendTo(c.conn, encoded);
                broadcastToAll(encoded, c.conn);
            }
            syncLuaPlayerSnapshot();
        }

        // Re-send every referenced definition to the requester before
        // publishing the result so replay and deduplication are safe even
        // if the local store missed an earlier broadcast; the client still
        // enforces a local visibility barrier. This also covers replayed
        // results after a reconnect. The owning item is sent before the
        // enchantment so the client can resolve the dependency either way.
        for (const std::string recordId : { outcome.result.itemRecordId, outcome.result.enchantmentRecordId })
        {
            if (recordId.empty())
                continue;
            for (const auto& [key, record] : mWorld.dynamicRecords)
            {
                if (record.recordId != recordId)
                    continue;
                PacketRecordDynamic definitionPacket;
                definitionPacket.action = DynamicRecordAction::Upsert;
                definitionPacket.recordType = record.recordType;
                definitionPacket.entries.push_back({ record.recordId, record.data });
                sendTo(c.conn, definitionPacket.encode());
            }
        }

        sendTo(c.conn, outcome.encodedResult);
        logEnchantingResult(c, packet.request, outcome);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] Enchanting internal error player=" << c.slotName
                          << " requestId=" << packet.request.requestId << " error=" << e.what();
        PacketEnchantingResult errorPacket;
        errorPacket.result = EnchantingService::makeError(
            packet.request.requestId, records::EnchantingError::ServerError, c.inventoryRevision);
        sendTo(c.conn, errorPacket.encode());
    }
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerJournal(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming;
    PacketPlayerJournal packet;
    packet.setPlayer(&incoming);
    if (!packet.decode(data, size))
        return;
    if (incoming.guid != c.guid
        || incoming.journalChanges.action != BasePlayer::JournalChanges::Action::Add)
    {
        Log(Debug::Warning) << "[Server] Rejected non-delta PlayerJournal from " << c.name;
        return;
    }

    std::vector<BasePlayer::JournalItem> accepted;
    accepted.reserve(std::min<std::size_t>(incoming.journalChanges.items.size(), 256));
    for (BasePlayer::JournalItem item : incoming.journalChanges.items)
    {
        if (accepted.size() >= 256)
            break;
        if (item.quest.empty() || item.quest.size() > 256)
            continue;
        if (item.type == BasePlayer::JournalItem::Type::Entry
            && (item.infoId.empty() || item.infoId.size() > 256))
            continue;
        if (item.text.size() > 65535 || item.actorName.size() > 1024)
            continue;

        item.daysPassed = std::clamp(item.daysPassed, 0, 10000000);
        item.month = std::clamp(item.month, 0, 11);
        item.dayOfMonth = std::clamp(item.dayOfMonth, 0, 31);
        accepted.push_back(std::move(item));
    }
    if (accepted.empty())
        return;

    if (mPlayerDb && c.dbCharacterId > 0)
    {
        try
        {
            mPlayerDb->saveCharacterJournalChanges(c.dbCharacterId, accepted);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] saveCharacterJournalChanges error: " << e.what();
            return;
        }
    }

    for (auto& [_, target] : mClients)
    {
        if (!shouldShareJournal(c, target))
            continue;

        BasePlayer relay;
        relay.guid = target.guid;
        relay.journalChanges.action = BasePlayer::JournalChanges::Action::Add;
        relay.journalChanges.items = accepted;
        PacketPlayerJournal relayPacket;
        relayPacket.setPlayer(&relay);
        sendTo(target.conn, relayPacket.encode());
    }

    Log(Debug::Verbose) << "[Server] Persisted PlayerJournal"
                        << " player=" << c.name
                        << " mode=" << static_cast<int>(mJournalSharingMode)
                        << " items=" << accepted.size();
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerStatsDynamic(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming = c.player;
    PacketPlayerStatsDynamic pkt;
    pkt.setPlayer(&incoming);
    if (!pkt.decode(data, size)) return;

    const uint64_t nowMs = currentServerTimeMs();
    if (c.hasRestoredStatsSnapshot
        && !c.acceptedPlayerStatsThisSession
        && nowMs < c.playerStatsRestoreGuardUntilMs
        && !samePersistentPlayerStats(incoming, c.restoredStatsSnapshot)
        && looksLikeRestoredStatsRegression(incoming, c.restoredStatsSnapshot))
    {
        const Attribute& incomingStrength = incoming.attributes[0];
        const Skill& incomingBlunt = incoming.skills[4];
        const Attribute& restoredStrength = c.restoredStatsSnapshot.attributes[0];
        const Skill& restoredBlunt = c.restoredStatsSnapshot.skills[4];
        Log(Debug::Info) << "[PlayerDB] ignored startup player stats overwrite"
                         << " charId=" << c.dbCharacterId
                         << " name=" << c.slotName
                         << " incomingStrength=" << incomingStrength.base
                         << " incomingBlunt=" << incomingBlunt.base
                         << " restoredStrength=" << restoredStrength.base
                         << " restoredBlunt=" << restoredBlunt.base;

        BasePlayer correction = c.restoredStatsSnapshot;
        correction.guid = c.guid;
        PacketPlayerStatsDynamic correctionPkt;
        correctionPkt.setPlayer(&correction);
        sendTo(c.conn, correctionPkt.encode());
        return;
    }

    // Protect a server-authoritative alchemy skill award from being clobbered
    // by a stale client snapshot that was captured before the client applied
    // the pushed authoritative statistics.
    if (c.alchemySkillSyncGuard)
    {
        const int alchemyIndex = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
        const Skill& incomingAlchemy = incoming.skills[alchemyIndex];
        const auto& guard = *c.alchemySkillSyncGuard;
        const bool caughtUp = incomingAlchemy.base >= guard.skillBase - 0.01f
            && incoming.level >= guard.level
            && (incomingAlchemy.base > guard.skillBase + 0.01f || incoming.level > guard.level
                || incomingAlchemy.progress >= guard.skillProgress - 0.001f)
            && incoming.levelProgress >= guard.levelProgress - 0.001f;
        if (!caughtUp)
        {
            Log(Debug::Info) << "[Alchemy] rejected stale player stats overwrite"
                             << " charId=" << c.dbCharacterId
                             << " name=" << c.slotName
                             << " incomingAlchemyBase=" << incomingAlchemy.base
                             << " incomingAlchemyProgress=" << incomingAlchemy.progress
                             << " incomingLevel=" << incoming.level
                             << " guardAlchemyBase=" << guard.skillBase
                             << " guardAlchemyProgress=" << guard.skillProgress
                             << " guardLevel=" << guard.level;
            PacketPlayerStatsDynamic correctionPkt;
            correctionPkt.setPlayer(&c.player);
            sendTo(c.conn, correctionPkt.encode());
            return;
        }
        c.alchemySkillSyncGuard.reset();
    }

    if (c.enchantSkillSyncGuard)
    {
        const int enchantIndex = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
        const Skill& incomingEnchant = incoming.skills[enchantIndex];
        const auto& guard = *c.enchantSkillSyncGuard;
        const bool caughtUp = incomingEnchant.base >= guard.skillBase - 0.01f
            && incoming.level >= guard.level
            && (incomingEnchant.base > guard.skillBase + 0.01f || incoming.level > guard.level
                || incomingEnchant.progress >= guard.skillProgress - 0.001f)
            && incoming.levelProgress >= guard.levelProgress - 0.001f;
        if (!caughtUp)
        {
            Log(Debug::Info) << "[Enchanting] rejected stale player stats overwrite"
                             << " charId=" << c.dbCharacterId
                             << " name=" << c.slotName
                             << " incomingEnchantBase=" << incomingEnchant.base
                             << " incomingEnchantProgress=" << incomingEnchant.progress
                             << " incomingLevel=" << incoming.level
                             << " guardEnchantBase=" << guard.skillBase
                             << " guardEnchantProgress=" << guard.skillProgress
                             << " guardLevel=" << guard.level;
            PacketPlayerStatsDynamic correctionPkt;
            correctionPkt.setPlayer(&c.player);
            sendTo(c.conn, correctionPkt.encode());
            return;
        }
        c.enchantSkillSyncGuard.reset();
    }

    const bool hadPreviousStats = c.hasRestoredStatsSnapshot;
    const BasePlayer previousStats = c.restoredStatsSnapshot;
    copyPersistentPlayerStats(c.player, incoming);
    c.player.hasSavedStats = true;
    c.acceptedPlayerStatsThisSession = true;
    c.restoredStatsSnapshot = c.player;
    c.hasRestoredStatsSnapshot = true;
    c.playerStatsRestoreGuardUntilMs = 0;

    if (mPlayerDb && c.dbCharacterId != 0 && c.charSelectComplete)
    {
        try
        {
            mPlayerDb->saveCharacterStats(c.dbCharacterId, c.player);
            const Attribute& strength = c.player.attributes[0];
            const Skill& blunt = c.player.skills[4];
            const bool loggedTrackedStatsChanged = !hadPreviousStats
                || strength.base != previousStats.attributes[0].base
                || !sameStatFloat(blunt.base, previousStats.skills[4].base);
            if ((strength.base > 100 || blunt.base > 100.f) && loggedTrackedStatsChanged)
            {
                Log(Debug::Info) << "[PlayerDB] saved persistent player stats"
                                 << " charId=" << c.dbCharacterId
                                 << " name=" << c.slotName
                                 << " strength=" << strength.base
                                 << " blunt=" << blunt.base
                                 << " hp=" << c.player.dynamicStats.health.current
                                 << "/" << c.player.dynamicStats.health.base;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] saveCharacterStats error: " << e.what();
        }
    }
    syncLuaPlayerSnapshot();
    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerDeath(ConnectedClient& c, const uint8_t* data, size_t size)
{
    BasePlayer incoming = c.player;
    PacketPlayerDeath pkt;
    pkt.setPlayer(&incoming);
    if (!pkt.decode(data, size)) return;

    const uint64_t nowMs = currentServerTimeMs();
    if (nowMs < c.playerDeathRestoreGuardUntilMs
        && c.player.dynamicStats.health.current > 0.f)
    {
        Log(Debug::Info) << "[Server] Ignored stale startup PlayerDeath for " << c.name
                         << " health=" << c.player.dynamicStats.health.current
                         << "/" << c.player.dynamicStats.health.base
                         << " guardRemainingMs=" << (c.playerDeathRestoreGuardUntilMs - nowMs)
                         << " anim='" << incoming.deathAnimationGroup << "'";
        return;
    }

    c.playerDeathRestoreGuardUntilMs = 0;
    c.player.isDead = true;
    c.player.deathAnimationGroup = incoming.deathAnimationGroup;

    // Death ends the current live guard-enforcement encounter without paying
    // or otherwise mutating the offender's authoritative crime state. This
    // clears the durable-in-session pursuit identity so respawn does not
    // resurrect combat solely because the player still has an unpaid bounty.
    clearOutstandingCrimePursuitsForCharacter(c);

    // Clear authoritative vehicle ownership as part of death handling. This
    // ensures the driver, observers, and later respawn all agree that the old
    // rigid body no longer exists at the death location.
    setPlayerVehicleState(c.player.guid, false, std::string(), 0);

    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);

    Log(Debug::Info) << "[Server] Relayed PlayerDeath for " << c.name
                     << " anim='" << c.player.deathAnimationGroup << "'"
                     << " killerGuid=" << pkt.killerGuid
                     << " killerRefId='" << pkt.killerRefId << "'";
    announcePlayerDeath(c, pkt);
    syncLuaPlayerSnapshot();
}

// ---------------------------------------------------------------------------
void MPServer::announcePlayerDeath(const ConnectedClient& victim, const PacketPlayerDeath& pkt)
{
    if (!mAnnouncePlayerDeaths)
        return;

    std::string message;
    if (pkt.killerGuid != 0 && pkt.killerGuid != victim.guid)
    {
        if (ConnectedClient* killer = findClientByGuid(pkt.killerGuid))
            message = victim.name + " was killed by " + killer->name + ".";
    }

    if (message.empty() && !pkt.killerRefId.empty())
        message = victim.name + " was killed by " + pkt.killerRefId + ".";

    if (message.empty())
        message = victim.name + " died.";

    broadcastNameColorMessage(message);
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerResurrect(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerResurrect pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;

    c.player.isDead = false;
    c.player.deathAnimationGroup.clear();
    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);

    Log(Debug::Info) << "[Server] Relayed PlayerResurrect for " << c.name;
    syncLuaPlayerSnapshot();
}

// ---------------------------------------------------------------------------
void MPServer::handlePlayerVehicleRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketPlayerVehicleRequest request;
    if (!request.decode(data, size))
        return;

    auto reject = [&](std::string_view reason) {
        Log(Debug::Info) << "[Server] Vehicle request rejected"
                         << " player=" << c.name
                         << " action=" << static_cast<int>(request.action)
                         << " parkedMpNum=" << request.parkedObjectMpNum
                         << " reason=" << reason;
        sendServerMessage(c.guid, std::string("Vehicle request denied: ") + std::string(reason));
    };

    if (request.action == VehicleRequestAction::Exit)
    {
        if (!c.player.vehicle.active)
        {
            reject("not currently occupying a vehicle");
            return;
        }

        const VehicleProfile* profile = findVehicleProfile(c.player.vehicle.profileId);
        const std::string cellId = makeCellKey(c.player.cell);
        if (!profile || cellId.empty())
        {
            reject("invalid active vehicle state");
            return;
        }

        Position exitPosition = c.player.position;
        const float yaw = exitPosition.rot[2];
        const bool isPassenger
            = c.player.vehicle.occupantRole == VehicleOccupantRole::Passenger;
        const uint8_t seatIndex = c.player.vehicle.seatIndex;
        const auto& exitOffset = seatIndex < profile->seatCount && seatIndex < profile->seats.size()
            ? profile->seats[seatIndex].exitOffset : profile->driverExitOffset;
        const float localX = exitOffset[0];
        const float localY = exitOffset[1];
        exitPosition.pos[0] += std::cos(yaw) * localX + std::sin(yaw) * localY;
        exitPosition.pos[1] += -std::sin(yaw) * localX + std::cos(yaw) * localY;
        exitPosition.pos[2] += exitOffset[2];
        exitPosition.rot[0] = 0.f;
        exitPosition.rot[1] = 0.f;

        if (!setPlayerVehicleState(c.guid, false, std::string(), 0))
        {
            reject("could not restore parked vehicle");
            return;
        }
        if (!teleportPlayer(c.guid, cellId, exitPosition))
        {
            Log(Debug::Warning) << "[Server] Vehicle exit restored the parked object but failed to move driver"
                                << " player=" << c.name;
            return;
        }

        Log(Debug::Info) << "[Server] Vehicle exit accepted"
                         << " player=" << c.name
                         << " role=" << (isPassenger ? "passenger" : "driver")
                         << " cell=" << cellId
                         << " exit=(" << exitPosition.pos[0] << ","
                         << exitPosition.pos[1] << "," << exitPosition.pos[2] << ")";
        return;
    }

    if (request.action == VehicleRequestAction::EnterPassenger)
    {
        if (c.player.vehicle.active || c.player.isDead)
        {
            reject("player is not available for a passenger seat");
            return;
        }

        ConnectedClient* driver = findClientByGuid(request.driverGuid);
        if (!driver || !driver->player.vehicle.active
            || driver->player.vehicle.occupantRole != VehicleOccupantRole::Driver)
        {
            reject("active driver is unavailable");
            return;
        }

        const VehicleProfile* profile = findVehicleProfile(driver->player.vehicle.profileId);
        if (!profile || profile->seatCount <= 1 || profile->seatCount > profile->seats.size())
        {
            reject("vehicle has no passenger seats");
            return;
        }
        if (!cellMatches(c.player.cell, makeCellKey(driver->player.cell)))
        {
            reject("vehicle is in another cell");
            return;
        }

        const float dx = c.player.position.pos[0] - driver->player.position.pos[0];
        const float dy = c.player.position.pos[1] - driver->player.position.pos[1];
        const float dz = c.player.position.pos[2] - driver->player.position.pos[2];
        const float maximumDistance = std::max(profile->entryActivationDistance, 0.f);
        if (dx * dx + dy * dy + dz * dz > maximumDistance * maximumDistance)
        {
            reject("too far from vehicle");
            return;
        }

        auto seatIsAvailable = [&](uint8_t seatIndex) {
            if (seatIndex == 0 || seatIndex >= profile->seatCount)
                return false;
            for (const auto& [connection, other] : mClients)
            {
                if (other.player.vehicle.active
                    && other.player.vehicle.occupantRole == VehicleOccupantRole::Passenger
                    && other.player.vehicle.driverGuid == driver->guid
                    && other.player.vehicle.seatIndex == seatIndex)
                {
                    return false;
                }
            }
            return true;
        };

        uint8_t seatIndex = request.seatIndex;
        if (seatIndex == sAutomaticVehicleSeat)
        {
            for (uint8_t candidate = 1; candidate < profile->seatCount; ++candidate)
            {
                if (seatIsAvailable(candidate))
                {
                    seatIndex = candidate;
                    break;
                }
            }
        }
        if (!seatIsAvailable(seatIndex))
        {
            reject("no requested passenger seat is available");
            return;
        }

        const std::string driverCellId = makeCellKey(driver->player.cell);
        if (driverCellId.empty()
            || !teleportPlayer(c.guid, driverCellId, driver->player.position)
            || !setPlayerVehicleState(c.guid, true, driver->player.vehicle.profileId,
                driver->player.vehicle.parkedObjectMpNum, VehicleOccupantRole::Passenger,
                driver->guid, seatIndex))
        {
            reject("could not enter passenger seat");
            return;
        }

        Log(Debug::Info) << "[Server] Passenger entry accepted"
                         << " player=" << c.name
                         << " driver=" << driver->name
                         << " profile='" << profile->id << "'"
                         << " seat=" << static_cast<int>(seatIndex)
                         << " parkedMpNum=" << driver->player.vehicle.parkedObjectMpNum;
        return;
    }

    if (request.action != VehicleRequestAction::Enter)
    {
        reject("unknown request action");
        return;
    }
    if (c.player.vehicle.active)
    {
        reject("already driving");
        return;
    }
    if (c.player.isDead || request.parkedObjectMpNum == 0)
    {
        reject("invalid driver or parked object");
        return;
    }

    const PlacedObject* parkedObject = nullptr;
    for (const auto& [cellId, objects] : mWorld.placedObjects)
    {
        const auto objectIt = std::find_if(objects.begin(), objects.end(),
            [&](const PlacedObject& object) { return object.mpNum == request.parkedObjectMpNum; });
        if (objectIt != objects.end())
        {
            parkedObject = &*objectIt;
            break;
        }
    }
    if (!parkedObject)
    {
        reject("parked vehicle is unavailable");
        return;
    }

    const VehicleProfile* profile = findVehicleProfileByParkedRefId(parkedObject->refId);
    if (!profile)
    {
        reject("object is not a registered vehicle");
        return;
    }
    if (!cellMatches(c.player.cell, parkedObject->cellId))
    {
        reject("vehicle is in another cell");
        return;
    }

    const float dx = c.player.position.pos[0] - parkedObject->position.pos[0];
    const float dy = c.player.position.pos[1] - parkedObject->position.pos[1];
    const float dz = c.player.position.pos[2] - parkedObject->position.pos[2];
    const float maximumDistance = std::max(profile->entryActivationDistance, 0.f);
    if (dx * dx + dy * dy + dz * dz > maximumDistance * maximumDistance)
    {
        reject("too far from vehicle");
        return;
    }

    for (const auto& [connection, other] : mClients)
    {
        if (other.guid != c.guid && other.player.vehicle.active
            && other.player.vehicle.parkedObjectMpNum == request.parkedObjectMpNum)
        {
            reject("vehicle is already occupied");
            return;
        }
    }

    const Position parkedPosition = parkedObject->position;
    const std::string parkedCellId = parkedObject->cellId;
    PlacedObject suspendedObject;
    if (!suspendPlacedVehicleObject(request.parkedObjectMpNum, suspendedObject))
    {
        reject("could not reserve parked vehicle");
        return;
    }

    mActiveVehiclesByDriver[c.guid]
        = ActiveVehicleRecord{ suspendedObject, std::string(profile->id) };
    if (!teleportPlayer(c.guid, parkedCellId, parkedPosition)
        || !setPlayerVehicleState(c.guid, true, std::string(profile->id), suspendedObject.mpNum))
    {
        restoreActiveVehicleObject(c);
        reject("could not enter vehicle");
        return;
    }

    Log(Debug::Info) << "[Server] Vehicle entry accepted"
                     << " player=" << c.name
                     << " profile='" << profile->id << "'"
                     << " parkedMpNum=" << suspendedObject.mpNum
                     << " cell=" << parkedCellId;
}

// ---------------------------------------------------------------------------
void MPServer::handleActorList(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorList incoming;
    PacketActorList pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorList")) return;

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    std::unordered_set<uint32_t> previousSpawnedActorMpNums;
    std::unordered_map<uint32_t, bool> previousSpawnedActorPersistence;
    // Snapshot serverSpawnTime so we can protect recently server-spawned actors
    // from being evicted by an authority ActorList that arrived before the client
    // processed the spawn notification (timing race with the Lua tick thread).
    std::unordered_map<uint32_t, uint64_t> previousSpawnedActorSpawnTime;
    std::unordered_map<uint32_t, ActorRegistryRecord> previousActorRecords;
    std::unordered_map<std::string, ActorRegistryRecord> previousActorRecordsByKey;
    std::unordered_map<std::string, ActorRegistryRecord> previousDeadVanillaActorRecords;
    std::vector<ActorRegistryRecord> previousCellRecords;
    std::unordered_set<std::string> previousCellActorKeys;
    for (const auto& [key, record] : cellState.actors)
    {
        previousCellRecords.push_back(record);
        previousCellActorKeys.insert(key);
        previousActorRecordsByKey[key] = record;
        if (record.actor.mpNum != 0)
        {
            previousSpawnedActorMpNums.insert(record.actor.mpNum);
            previousSpawnedActorPersistence[record.actor.mpNum] = record.persistent;
            previousSpawnedActorSpawnTime[record.actor.mpNum] = record.serverSpawnTime;
            previousActorRecords[record.actor.mpNum] = record;
        }
        else if (record.actor.isDead)
            previousDeadVanillaActorRecords[key] = record;
    }

    std::unordered_set<uint32_t> currentSpawnedActorMpNums;
    std::unordered_set<std::string> migratedActorCells;
    std::unordered_map<std::string, std::vector<BaseActor>> canonicalDeadVanillaCorrections;
    std::unordered_map<std::string, std::vector<ActorRegistryRecord>> disposedVanillaCorrections;
    std::size_t staleDeadVanillaCorrections = 0;
    std::size_t disposedVanillaCorrectionsQueued = 0;
    std::size_t missingIdentityDropped = 0;
    std::size_t ambiguousIdentityNormalized = 0;
    std::size_t unmanagedSpawnerDropped = 0;
    std::size_t duplicateIdentityDropped = 0;
    std::size_t unknownSpawnedDropped = 0;
    std::size_t deadSpawnedSuppressed = 0;
    std::size_t boundaryActorListSuppressed = 0;
    std::string firstUnmanagedSpawnerRefId;
    uint32_t firstUnmanagedSpawnerRefNum = 0;
    std::string firstUnknownSpawnedRefId;
    uint32_t firstUnknownSpawnedMpNum = 0;
    std::string firstDeadSpawnedRefId;
    uint32_t firstDeadSpawnedMpNum = 0;
    std::string firstDeadSpawnedKnownCell;
    cellState.actors.clear();
    std::unordered_set<ActorInstanceId> incomingActorNetIds;

    auto cellContainsActorNetId = [&](ActorInstanceId actorNetId) {
        return actorNetId != 0
            && std::any_of(cellState.actors.begin(), cellState.actors.end(), [&](const auto& entry) {
                return actorInstanceIdFromActor(entry.second.actor) == actorNetId;
            });
    };

    auto findKnownDeadSpawnedRecord = [&](const BaseActor& actor, std::string* knownCellId = nullptr)
        -> const ActorRegistryRecord*
    {
        if (actor.mpNum == 0)
            return nullptr;

        const auto previousIt = previousActorRecords.find(actor.mpNum);
        if (previousIt != previousActorRecords.end() && previousIt->second.actor.isDead)
        {
            if (knownCellId)
                *knownCellId = incoming.cellId;
            return &previousIt->second;
        }

        const std::string actorKey = makeActorKey(actor);
        const auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt == mWorld.actorLocations.end())
            return nullptr;

        const auto cellIt = mWorld.actorCells.find(locationIt->second);
        if (cellIt == mWorld.actorCells.end())
            return nullptr;

        const auto actorIt = cellIt->second.actors.find(actorKey);
        if (actorIt == cellIt->second.actors.end() || !actorIt->second.actor.isDead)
            return nullptr;

        if (knownCellId)
            *knownCellId = locationIt->second;
        return &actorIt->second;
    };

    for (auto& actor : incoming.actors)
    {
        const bool hasUnknownGeneratedEquipment
            = std::any_of(actor.equipment.begin(), actor.equipment.end(), [&](const EquipmentItem& entry) {
                  return !entry.item.refId.empty() && !isAuthoritativeRecordReference(entry.item.refId);
              });
        if (hasUnknownGeneratedEquipment)
        {
            Log(Debug::Warning) << "[Server] Rejected ActorList unknown generated equipment from=" << c.name
                                << " actor=" << actor.refId;
            continue;
        }
        actor.cellId = incoming.cellId;
        if (normalizeActorIdentity(actor))
            ++ambiguousIdentityNormalized;
        if (hasMissingActorInstanceIdentity(actor))
        {
            ++missingIdentityDropped;
            continue;
        }
        if (isUnmanagedSpawnerActor(actor))
        {
            ++unmanagedSpawnerDropped;
            if (firstUnmanagedSpawnerRefId.empty())
            {
                firstUnmanagedSpawnerRefId = actor.refId;
                firstUnmanagedSpawnerRefNum = actor.refNum;
            }
            continue;
        }
        const ActorInstanceId incomingActorNetId = actorInstanceIdFromActor(actor);
        if (incomingActorNetId == 0 || !incomingActorNetIds.insert(incomingActorNetId).second)
        {
            ++duplicateIdentityDropped;
            continue;
        }
        if (const BaseActor* disposed = findDisposedVanillaActor(actor))
        {
            const std::string actorKey = makeActorKey(actor);
            const uint64_t nowMs = incoming.serverTimestamp != 0
                ? incoming.serverTimestamp : currentServerTimeMs();
            uint64_t& lastResendMs = cellState.staleLiveVanillaDeathResendMs[actorKey];
            if (lastResendMs == 0 || nowMs >= lastResendMs + 1000)
            {
                lastResendMs = nowMs;
                ActorRegistryRecord removal;
                removal.actor = *disposed;
                removal.actor.cellId = disposed->cellId;
                removal.actorNetId = actorInstanceIdFromActor(removal.actor);
                disposedVanillaCorrections[disposed->cellId].push_back(std::move(removal));
                ++disposedVanillaCorrectionsQueued;
            }
            continue;
        }
        if (rejectResetStaleDeadVanillaActor(actor, incoming.cellId, c, "ActorList"))
            continue;
        clearResetStaleDeathSuppressionForAliveVanillaActor(actor, incoming.cellId);

        if (actor.mpNum == 0 && !actor.isDead && !actor.refId.empty())
        {
            std::string deadCellId;
            if (const ActorRegistryRecord* deadRecord = findDeadVanillaActor(actor, &deadCellId))
            {
                const std::string actorKey = makeActorKey(actor);
                const uint64_t nowMs = incoming.serverTimestamp != 0
                    ? incoming.serverTimestamp : currentServerTimeMs();
                uint64_t& lastResendMs = cellState.staleLiveVanillaDeathResendMs[actorKey];
                const bool shouldResend = lastResendMs == 0 || nowMs >= lastResendMs + 1000;
                if (shouldResend)
                {
                    lastResendMs = nowMs;
                    ++staleDeadVanillaCorrections;
                    canonicalDeadVanillaCorrections[deadCellId].push_back(deadRecord->actor);
                    Log(Debug::Verbose) << "[Server] ActorList suppressed stale live vanilla actor for canonical corpse"
                                     << " from=" << c.name
                                     << " refId=" << deadRecord->actor.refId
                                     << " refNum=" << deadRecord->actor.refNum
                                     << " incomingCell=" << incoming.cellId
                                     << " deadCell=" << deadCellId
                                     << " pos=(" << deadRecord->actor.position.pos[0]
                                     << "," << deadRecord->actor.position.pos[1]
                                     << "," << deadRecord->actor.position.pos[2] << ")"
                                     << " deathAnim='" << deadRecord->actor.deathAnimGroup << "'";
                }
                continue;
            }
        }

        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorList"))
            continue;

        const std::string actorKey = makeActorKey(actor);
        std::string knownDeadSpawnedCellId;
        if (const ActorRegistryRecord* deadSpawnedRecord = findKnownDeadSpawnedRecord(actor, &knownDeadSpawnedCellId))
        {
            ++deadSpawnedSuppressed;
            if (firstDeadSpawnedRefId.empty())
            {
                firstDeadSpawnedRefId = deadSpawnedRecord->actor.refId;
                firstDeadSpawnedMpNum = deadSpawnedRecord->actor.mpNum;
                firstDeadSpawnedKnownCell = knownDeadSpawnedCellId;
            }
            continue;
        }

        if (actor.mpNum != 0 && isExteriorCellKey(incoming.cellId))
        {
            const std::string positionCellId = exteriorCellIdForPosition(actor.position);
            constexpr float kExteriorCellMismatchHysteresis = 64.f;
            if (!positionCellId.empty()
                && positionCellId != incoming.cellId
                && exteriorCellBorderDistance(actor.position) > kExteriorCellMismatchHysteresis
                && clientHasActorCellLoaded(c, positionCellId))
            {
                if (actor.refId == "fargoth" || actor.refId == "heddvild")
                {
                    Log(Debug::Info) << "[Server] ActorList skipped stale spawned exterior cell"
                                     << " from=" << c.name
                                     << " refId=" << actor.refId
                                     << " mpNum=" << actor.mpNum
                                     << " incomingCell=" << incoming.cellId
                                     << " positionCell=" << positionCellId
                                     << " pos=(" << actor.position.pos[0]
                                     << "," << actor.position.pos[1]
                                     << "," << actor.position.pos[2] << ")";
                }
                continue;
            }
        }

        auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt != mWorld.actorLocations.end() && locationIt->second != incoming.cellId)
        {
            if (actor.mpNum != 0
                && isExteriorCellKey(locationIt->second)
                && isExteriorCellKey(incoming.cellId))
            {
                // ActorList is a reliable keyset/identity snapshot, not a
                // migration transaction. For spawned actors, ActorCellChange is
                // the only canonical migration path while the v2 position stream
                // remains movement-only, so stale cell lists must not reverse a
                // committed handoff.
                ++boundaryActorListSuppressed;
                continue;
            }

            // If this actor previously belonged to this cell but has since migrated away,
            // this is a stale ActorList from the old cell. Ignore it.
            if (previousCellActorKeys.count(actorKey) != 0)
                continue;
        }

        std::optional<ActorRegistryRecord> migratedRecord
            = removeActorFromOtherCells(actor, incoming.cellId, migratedActorCells);
        const ActorRegistryRecord* previousRecord = nullptr;
        const auto previousRecordByKeyIt = previousActorRecordsByKey.find(actorKey);
        if (previousRecordByKeyIt != previousActorRecordsByKey.end())
            previousRecord = &previousRecordByKeyIt->second;
        else if (migratedRecord)
            previousRecord = &*migratedRecord;
        if (actor.mpNum != 0 && previousRecord == nullptr)
        {
            ++unknownSpawnedDropped;
            if (firstUnknownSpawnedRefId.empty())
            {
                firstUnknownSpawnedRefId = actor.refId;
                firstUnknownSpawnedMpNum = actor.mpNum;
            }
            continue;
        }
        if (actor.mpNum != 0)
            currentSpawnedActorMpNums.insert(actor.mpNum);
        const bool hadPreviousRecord = previousRecord != nullptr;
        const bool wasDead = hadPreviousRecord && previousRecord->actor.isDead;
        const bool persistent = actor.mpNum != 0 && previousRecord != nullptr && previousRecord->persistent;
        auto [it, inserted] = cellState.actors.emplace(actorKey,
            ActorRegistryRecord { actor, incoming.serverTimestamp, 0, persistent });
        if (!inserted)
            it->second = { actor, incoming.serverTimestamp, 0, persistent };
        if (previousRecord && previousRecord->actorNetId != 0)
            it->second.actorNetId = previousRecord->actorNetId;
        else if (migratedRecord && migratedRecord->actorNetId != 0)
            it->second.actorNetId = migratedRecord->actorNetId;
        if (previousRecord)
        {
            it->second.previousCellId = previousRecord->previousCellId;
            it->second.previousCellAuthorityGuid = previousRecord->previousCellAuthorityGuid;
            it->second.lastCellChangeTime = previousRecord->lastCellChangeTime;
            it->second.migrationGeneration = previousRecord->migrationGeneration;
            it->second.actorAuthorityGuid = previousRecord->actorAuthorityGuid;
            it->second.actorAuthorityGeneration = previousRecord->actorAuthorityGeneration;
            it->second.actorAuthorityReason = previousRecord->actorAuthorityReason;
            it->second.actorAuthorityTargetGuid = previousRecord->actorAuthorityTargetGuid;
            it->second.actorAuthorityLeaseUntilMs = previousRecord->actorAuthorityLeaseUntilMs;
            it->second.crimePursuitCharacterId = previousRecord->crimePursuitCharacterId;
            it->second.crimePursuitLastGuid = previousRecord->crimePursuitLastGuid;
            it->second.crimeEnforcementState = previousRecord->crimeEnforcementState;
            it->second.crimePursuitReassertArmed = previousRecord->crimePursuitReassertArmed;
            it->second.crimePursuitLastReassertMs = previousRecord->crimePursuitLastReassertMs;
        }
        // ActorList is a state refresh, not a migration transaction. The
        // server-owned migration timeline was copied above and is canonicalized
        // together with the actor identity at this boundary.
        ensureActorNetId(it->second, incoming.cellId);
        // Preserve the original serverSpawnTime so the grace-period logic
        // remains accurate even after later client updates.
        const auto spawnTimeIt = previousSpawnedActorSpawnTime.find(actor.mpNum);
        if (spawnTimeIt != previousSpawnedActorSpawnTime.end())
            it->second.serverSpawnTime = spawnTimeIt->second;
        else if (migratedRecord)
            it->second.serverSpawnTime = migratedRecord->serverSpawnTime;
        updateActorAuthorityLeaseFromAi(
            incoming.cellId, it->second, it->second.actor, incoming.serverTimestamp, "ActorList");
        persistSpawnedActorIfNeeded(it->second);
        rememberActorLocation(it->second.actor, incoming.cellId);
        if (migratedRecord && it->second.actor.mpNum != 0)
            upsertSpawnedActorDynamicRecordLinkIfNeeded(it->second.actor);

        if (actor.mpNum != 0 && hadPreviousRecord && actor.isDead && !wasDead)
        {
            Log(Debug::Info) << "[Server] ActorList observed death transition"
                             << " refId=" << it->second.actor.refId
                             << " mpNum=" << it->second.actor.mpNum
                             << " cell=" << incoming.cellId;
            sendActorLifecycleEvent("death", it->second.actor, it->second.persistent);
        }
    }

    if (missingIdentityDropped != 0 || ambiguousIdentityNormalized != 0 || unmanagedSpawnerDropped != 0
        || duplicateIdentityDropped != 0)
    {
        const bool importantActorListRepair = missingIdentityDropped != 0 || ambiguousIdentityNormalized != 0;
        Log(importantActorListRepair ? Debug::Info : Debug::Verbose)
            << "[Server] ActorList normalized identity"
            << " from=" << c.name
            << " cell=" << incoming.cellId
            << " actors=" << incoming.actors.size()
            << " missingIdentityDropped=" << missingIdentityDropped
            << " ambiguousIdentityNormalized=" << ambiguousIdentityNormalized
            << " unmanagedSpawnerDropped=" << unmanagedSpawnerDropped
            << " duplicateIdentityDropped=" << duplicateIdentityDropped
            << " firstUnmanagedSpawner=" << firstUnmanagedSpawnerRefId
            << " firstUnmanagedSpawnerRefNum=" << firstUnmanagedSpawnerRefNum;
    }

    if (unknownSpawnedDropped != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorList dropped unknown spawned actor(s)"
                            << " from=" << c.name
                            << " cell=" << incoming.cellId
                            << " actors=" << incoming.actors.size()
                            << " dropped=" << unknownSpawnedDropped
                            << " firstRefId=" << firstUnknownSpawnedRefId
                            << " firstMpNum=" << firstUnknownSpawnedMpNum;
    }

    if (deadSpawnedSuppressed != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorList suppressed update(s) for dead spawned actor"
                            << " from=" << c.name
                            << " incomingCell=" << incoming.cellId
                            << " suppressed=" << deadSpawnedSuppressed
                            << " firstRefId=" << firstDeadSpawnedRefId
                            << " firstMpNum=" << firstDeadSpawnedMpNum
                            << " firstKnownCell=" << firstDeadSpawnedKnownCell;
    }

    if (boundaryActorListSuppressed != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorList suppressed uncommitted exterior migration"
                            << " from=" << c.name
                            << " incomingCell=" << incoming.cellId
                            << " suppressed=" << boundaryActorListSuppressed;
    }

    // Grace period for freshly server-spawned actors: the authority's
    // first ActorList can arrive before the client has processed the spawn
    // notification sent by spawnActor() (Lua tick vs. incoming packet race).
    // If the actor was spawned within the last 10 seconds and the authority
    // didn't include it, re-inject it rather than deleting it.  The authority
    // will acknowledge it once it processes the pending spawn notification.
    // Note: actors removed via mp.removeActor() are already gone from
    // cellState.actors before handleActorList runs, so they won't appear in
    // previousSpawnedActorMpNums and are unaffected by this guard.
    constexpr uint64_t kSpawnGraceMs = 10000; // 10 s
    for (uint32_t previousMpNum : previousSpawnedActorMpNums)
    {
        if (currentSpawnedActorMpNums.count(previousMpNum) != 0)
            continue; // acknowledged by client

        const uint64_t spawnTime = previousSpawnedActorSpawnTime[previousMpNum];
        const auto& prev = previousActorRecords[previousMpNum];
        if (spawnTime == 0 || prev.lastSnapshotTime > spawnTime)
            continue; // already acknowledged once by the authority

        const uint64_t age = (incoming.serverTimestamp > spawnTime)
            ? (incoming.serverTimestamp - spawnTime) : 0;
        if (age > kSpawnGraceMs)
            continue; // old enough that the authority has definitely seen it

        // Re-inject: the actor is too new to have been intentionally removed.
        const std::string actorKey = makeActorKey(prev.actor);
        const auto locationIt2 = mWorld.actorLocations.find(actorKey);
        if (locationIt2 != mWorld.actorLocations.end() && locationIt2->second != incoming.cellId)
            continue;
        cellState.actors[actorKey] = prev;
        rememberActorLocation(prev.actor, incoming.cellId);
        currentSpawnedActorMpNums.insert(previousMpNum);
        Log(Debug::Info) << "[Server] handleActorList re-injected recent actor"
                         << " mpNum=" << previousMpNum
                         << " age=" << age << "ms"
                         << " cell=" << incoming.cellId;
    }

    std::size_t retainedOmittedSpawnedActors = 0;
    for (uint32_t previousMpNum : previousSpawnedActorMpNums)
    {
        if (currentSpawnedActorMpNums.count(previousMpNum) != 0)
            continue;

        const auto previousRecordIt = previousActorRecords.find(previousMpNum);
        if (previousRecordIt == previousActorRecords.end())
            continue;

        const ActorRegistryRecord& previousRecord = previousRecordIt->second;
        const std::string actorKey = makeActorKey(previousRecord.actor);
        const auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt != mWorld.actorLocations.end() && locationIt->second != incoming.cellId)
            continue;

        cellState.actors[actorKey] = previousRecord;
        rememberActorLocation(previousRecord.actor, incoming.cellId);
        currentSpawnedActorMpNums.insert(previousMpNum);
        ++retainedOmittedSpawnedActors;
    }

    if (retainedOmittedSpawnedActors != 0)
    {
        Log(Debug::Verbose) << "[Server] handleActorList retained omitted spawned actor(s)"
                            << " from=" << c.name
                            << " cell=" << incoming.cellId
                            << " count=" << retainedOmittedSpawnedActors;
    }

    std::size_t retainedDeadVanillaActors = 0;
    for (const auto& [actorKey, record] : previousDeadVanillaActorRecords)
    {
        if (cellState.actors.find(actorKey) != cellState.actors.end())
            continue;
        if (cellContainsActorNetId(actorInstanceIdFromActor(record.actor)))
            continue;

        cellState.actors[actorKey] = record;
        rememberActorLocation(record.actor, incoming.cellId);
        rememberDeadVanillaActor(record);
        ++retainedDeadVanillaActors;
    }
    if (retainedDeadVanillaActors != 0)
    {
        Log(Debug::Verbose) << "[Server] handleActorList retained dead vanilla actor(s)"
                            << " count=" << retainedDeadVanillaActors
                            << " cell=" << incoming.cellId;
    }

    std::size_t retainedCrimePursuitVanillaActors = 0;
    for (const ActorRegistryRecord& previousRecord : previousCellRecords)
    {
        if (previousRecord.actor.mpNum != 0 || previousRecord.actor.isDead
            || previousRecord.crimePursuitCharacterId <= 0)
            continue;

        const std::string actorKey = makeActorKey(previousRecord.actor);
        if (cellState.actors.find(actorKey) != cellState.actors.end())
            continue;
        if (cellContainsActorNetId(actorInstanceIdFromActor(previousRecord.actor)))
            continue;

        const auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt != mWorld.actorLocations.end() && locationIt->second != incoming.cellId)
            continue;

        cellState.actors[actorKey] = previousRecord;
        rememberActorLocation(previousRecord.actor, incoming.cellId);
        ++retainedCrimePursuitVanillaActors;
    }
    if (retainedCrimePursuitVanillaActors != 0)
    {
        Log(Debug::Verbose) << "[Server] handleActorList retained omitted crime-pursuit vanilla actor(s)"
                            << " count=" << retainedCrimePursuitVanillaActors
                            << " cell=" << incoming.cellId;
    }

    const std::size_t restoredDeadVanillaActors = mergeDeadVanillaActorsForCell(incoming.cellId, cellState.actors);
    if (restoredDeadVanillaActors != 0)
    {
        Log(Debug::Verbose) << "[Server] handleActorList restored dead vanilla overlay(s)"
                            << " count=" << restoredDeadVanillaActors
                            << " cell=" << incoming.cellId;
    }

    for (const ActorRegistryRecord& previousRecord : previousCellRecords)
    {
        if (cellState.actors.find(makeActorKey(previousRecord.actor)) != cellState.actors.end())
            continue;
        forgetActorLocation(previousRecord.actor, incoming.cellId);
    }

    if (mPlayerDb)
    {
        bool removedActorLink = false;
        for (uint32_t previousMpNum : previousSpawnedActorMpNums)
        {
            if (currentSpawnedActorMpNums.count(previousMpNum) != 0)
                continue;

            // NEW: retain persistent actors instead of deleting them on relog
            if (previousSpawnedActorPersistence[previousMpNum])
            {
                const auto& prev = previousActorRecords[previousMpNum];
                const std::string actorKey = makeActorKey(prev.actor);
                const auto locationIt = mWorld.actorLocations.find(actorKey);
                if (locationIt != mWorld.actorLocations.end() && locationIt->second != incoming.cellId)
                    continue; // already migrated, do not duplicate into old cell

                cellState.actors[actorKey] = prev;
                rememberActorLocation(prev.actor, incoming.cellId);
                currentSpawnedActorMpNums.insert(previousMpNum);
                Log(Debug::Verbose) << "[Server] retained persistent spawned actor"
                                    << " mpNum=" << previousMpNum
                                    << " cell=" << incoming.cellId;
                continue;
            }

            mPlayerDb->deleteSpawnedActorDynamicRecordLink(previousMpNum, incoming.cellId);
            removedActorLink = true;
        }

        if (removedActorLink)
            scheduleGeneratedDynamicRecordGc("actor_list_unlink");
    }

    for (auto& [actorKey, record] : cellState.actors)
    {
        ensureActorNetId(record, incoming.cellId);
        markLuaActorDirty(record, incoming.cellId);
    }

    for (const ActorRegistryRecord& previousRecord : previousCellRecords)
    {
        const uint32_t mpNum = previousRecord.actor.mpNum;
        if (mpNum == 0)
            continue;

        const std::string actorKey = makeActorKey(previousRecord.actor);
        if (cellState.actors.find(actorKey) != cellState.actors.end())
            continue;

        const auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt == mWorld.actorLocations.end())
        {
            markLuaActorRemoved(mpNum);
            continue;
        }

        const auto trackedCellIt = mWorld.actorCells.find(locationIt->second);
        if (trackedCellIt == mWorld.actorCells.end())
        {
            markLuaActorRemoved(mpNum);
            continue;
        }

        const auto trackedActorIt = trackedCellIt->second.actors.find(actorKey);
        if (trackedActorIt == trackedCellIt->second.actors.end())
            markLuaActorRemoved(mpNum);
        else
            markLuaActorDirty(trackedActorIt->second, trackedCellIt->first);
    }

    for (const std::string& migratedCellId : migratedActorCells)
    {
        auto migratedCellIt = mWorld.actorCells.find(migratedCellId);
        if (migratedCellIt != mWorld.actorCells.end())
            broadcastActorListForCell(migratedCellId, migratedCellIt->second);
    }

    ActorList deduped = incoming;
    deduped.actors.clear();
    deduped.actors.reserve(cellState.actors.size());
    std::unordered_set<ActorInstanceId> relayedActorNetIds;
    for (const auto& [actorKey, record] : cellState.actors)
    {
        const ActorInstanceId actorNetId = actorInstanceIdFromActor(record.actor);
        if (actorNetId != 0 && relayedActorNetIds.insert(actorNetId).second)
            deduped.actors.push_back(record.actor);
    }

    PacketActorList out;
    out.setActorList(&deduped);
    cellState.hasCompleteAuthoritySnapshot = true;
    broadcastActorIdentityForCell(incoming.cellId, cellState);
    broadcastActorToCell(incoming.cellId, out.encode(), c.conn);
    broadcastActorAuthorityLeasesForCell(incoming.cellId, cellState);
    for (auto& [disposedCellId, removals] : disposedVanillaCorrections)
    {
        auto& disposedCellState = mWorld.actorCells[disposedCellId];
        broadcastActorIdentityRemovalForCell(
            disposedCellId, disposedCellState, removals, ActorRemovalReason::CorpseDisposed);
    }
    if (disposedVanillaCorrectionsQueued != 0)
    {
        Log(Debug::Info) << "[Server] ActorList corrected disposed vanilla actor(s)"
                         << " from=" << c.name
                         << " sourceCell=" << incoming.cellId
                         << " actors=" << disposedVanillaCorrectionsQueued
                         << " cells=" << disposedVanillaCorrections.size();
    }
    if (staleDeadVanillaCorrections != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorList correcting stale live canonical corpse"
                            << " staleLiveReports=" << staleDeadVanillaCorrections
                            << " cells=" << canonicalDeadVanillaCorrections.size()
                            << " sourceCell=" << incoming.cellId
                            << " count=" << staleDeadVanillaCorrections;
    }
    for (auto& [deadCellId, deadActors] : canonicalDeadVanillaCorrections)
    {
        const auto deadCellIt = mWorld.actorCells.find(deadCellId);
        CellActorState* deadCellState
            = deadCellIt != mWorld.actorCells.end() ? &deadCellIt->second : nullptr;

        ActorList correction;
        correction.cellId = deadCellId;
        correction.isAuthority = false;
        correction.authorityGuid = deadCellState ? deadCellState->authorityGuid : 0;
        correction.authorityGeneration = deadCellState ? deadCellState->authorityGeneration : 0;
        correction.snapshotSequence = deadCellState ? deadCellState->nextSnapshotSequence++ : 0;
        correction.serverTimestamp = incoming.serverTimestamp;
        correction.actors.reserve(deadActors.size());
        for (BaseActor& actor : deadActors)
        {
            actor.isDead = true;
            actor.isInstantDeath = true;
            actor.cellId = deadCellId;
            actor.dynamicStats.health.current = std::min(0.f, actor.dynamicStats.health.current);
            correction.actors.push_back(std::move(actor));
        }

        PacketActorDeath deathPacket;
        deathPacket.setActorList(&correction);
        sendTo(c.conn, deathPacket.encode());
        Log(Debug::Verbose) << "[Server] ActorList sent targeted canonical corpse correction"
                            << " cell=" << deadCellId
                            << " sourceCell=" << incoming.cellId
                            << " actors=" << correction.actors.size();
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleActorPosition(ConnectedClient& c, const uint8_t* data, size_t size)
{
    if (c.actorSyncProtocolVersion >= ActorSyncProtocolVersionV2)
    {
        Log(Debug::Verbose) << "[Server] Ignoring retired legacy ActorPosition from v2 client"
                            << " from=" << c.name
                            << " protocol=" << c.actorSyncProtocolVersion;
        return;
    }

    ActorList incoming;
    PacketActorPosition pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorPosition")) return;

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectResetStaleDeadVanillaActor(actor, incoming.cellId, c, "ActorPosition"))
            continue;
        clearResetStaleDeathSuppressionForAliveVanillaActor(actor, incoming.cellId);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorPosition"))
            continue;

        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorPosition");
        if (!stored)
        {
            filtered.actors.push_back(actor);
            continue;
        }
        if (stored->actor.mpNum != 0 && stored->actor.isDead)
            continue;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.position = actor.position;
        stored->actor.velocity = actor.velocity;
        stored->actor.isMoving = actor.isMoving;
        stored->actor.hasWeaponDrawn = actor.hasWeaponDrawn;
        stored->actor.hasSpellReadied = actor.hasSpellReadied;
        stored->actor.isAttackingOrCasting = actor.isAttackingOrCasting;
        stored->actor.animFlags.movementFlags = actor.animFlags.movementFlags;
        stored->actor.animFlags.actionFlags = actor.animFlags.actionFlags;
        stored->actor.animFlags.animFwd = actor.animFlags.animFwd;
        stored->actor.animFlags.animSide = actor.animFlags.animSide;
        stored->actor.animFlags.currentAnimGroup = actor.animFlags.currentAnimGroup;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        ensureActorNetId(*stored, incoming.cellId);
        persistSpawnedActorIfNeeded(*stored, incoming.serverTimestamp, false);
        markLuaActorDirty(*stored, incoming.cellId);
        filtered.actors.push_back(actor);
    }

    if (filtered.actors.empty())
        return;

    broadcastActorPositionV2ToCell(filtered.cellId, cellState, filtered, c.conn);
    broadcastActorPresentationV2ToCell(filtered.cellId, cellState, filtered, c.conn);

    PacketActorPosition out;
    out.setActorList(&filtered);
    const std::vector<uint8_t> encodedLegacy = out.encode();
    for (auto& [conn, client] : mClients)
    {
        if (conn == c.conn
            || !clientHasActorCellLoaded(client, filtered.cellId)
            || client.actorSyncProtocolVersion >= ActorSyncProtocolVersionV2)
            continue;
        sendTo(conn, encodedLegacy, /*reliable=*/false);
    }
}

void MPServer::handleActorIdentityAck(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorIdentityAck ack;
    PacketActorIdentityAck pkt;
    pkt.setAck(&ack);
    if (!pkt.decode(data, size))
        return;
    if (ack.protocolVersion != ActorSyncProtocolVersionV2)
    {
        Log(Debug::Warning) << "[Server] Ignoring ActorIdentityAck from " << c.name
                            << " unsupported protocol=" << ack.protocolVersion;
        return;
    }

    std::size_t newlyAcked = 0;
    std::size_t invalidActorNetId = 0;
    ActorInstanceId firstInvalidActorNetId = 0;
    std::vector<ActorInstanceId> newlyAckedActorNetIds;
    newlyAckedActorNetIds.reserve(ack.actorNetIds.size());
    for (ActorInstanceId actorNetId : ack.actorNetIds)
    {
        if (!isValidActorInstanceId(actorNetId))
        {
            ++invalidActorNetId;
            if (firstInvalidActorNetId == 0)
                firstInvalidActorNetId = actorNetId;
            continue;
        }
        if (c.actorV2IdentitySent.count(actorNetId) == 0)
            continue;
        if (c.actorV2IdentityAcked.insert(actorNetId).second)
        {
            ++newlyAcked;
            ++c.actorV2IdentityAckedWindow;
            newlyAckedActorNetIds.push_back(actorNetId);
        }
    }

    if (newlyAcked != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorSync v2 identity ack"
                            << " receiver=" << c.guid
                            << " cell=" << ack.cellId
                            << " acked=" << newlyAcked
                            << " totalAcked=" << c.actorV2IdentityAcked.size()
                            << " seq=" << ack.sequence;
    }

    if (!newlyAckedActorNetIds.empty() && c.actorSyncProtocolVersion >= ActorSyncProtocolVersionV2)
    {
        ActorPresentationV2List bootstrap;
        bootstrap.protocolVersion = ActorSyncProtocolVersionV2;
        bootstrap.serverTimestamp = currentServerTimeMs();
        bootstrap.sequence = ack.sequence;
        bootstrap.snapshots.reserve(newlyAckedActorNetIds.size());

        std::size_t missingBootstrapIdentity = 0;
        std::size_t missingBootstrapActor = 0;
        std::size_t suppressedBootstrapCell = 0;
        std::unordered_map<uint32_t, ActorPresentationV2List> freshPhaseRequestsByAuthority;
        for (ActorInstanceId actorNetId : newlyAckedActorNetIds)
        {
            if (!isValidActorInstanceId(actorNetId)
                || c.actorV2IdentitySent.count(actorNetId) == 0
                || c.actorV2IdentityAcked.count(actorNetId) == 0)
                continue;

            auto keyIt = mWorld.actorKeysByNetId.find(actorNetId);
            if (keyIt == mWorld.actorKeysByNetId.end())
            {
                ++missingBootstrapIdentity;
                continue;
            }

            const std::string& actorKey = keyIt->second;
            auto locationIt = mWorld.actorLocations.find(actorKey);
            if (locationIt == mWorld.actorLocations.end())
            {
                ++missingBootstrapActor;
                continue;
            }

            if (!clientHasActorCellLoaded(c, locationIt->second))
            {
                ++suppressedBootstrapCell;
                continue;
            }

            auto cellIt = mWorld.actorCells.find(locationIt->second);
            if (cellIt == mWorld.actorCells.end())
            {
                ++missingBootstrapActor;
                continue;
            }

            bootstrap.authorityGuid = cellIt->second.authorityGuid;
            bootstrap.authorityGeneration = cellIt->second.authorityGeneration;

            auto actorIt = cellIt->second.actors.find(actorKey);
            if (actorIt == cellIt->second.actors.end())
            {
                ++missingBootstrapActor;
                continue;
            }

            ActorPresentationSnapshot snapshot = makePresentationSnapshot(actorIt->second.actor, actorNetId);
            if (snapshot.currentAnimGroup.empty()
                && !snapshot.isAttackingOrCasting
                && !snapshot.hasWeaponDrawn
                && !snapshot.hasSpellReadied
                && !snapshot.isDead
                && snapshot.movementFlags == 0)
                continue;

            const bool specialIdlePhase = isIdleAnimGroup(snapshot.currentAnimGroup)
                && isReliablePresentationAnimGroup(snapshot.currentAnimGroup)
                && !isBaseIdleAnimGroup(snapshot.currentAnimGroup)
                && snapshot.currentAnimCompletion >= 0.f;
            bool requestedFreshPhase = false;
            if (specialIdlePhase && cellIt->second.authorityGuid != 0 && cellIt->second.authorityGuid != c.guid)
            {
                ActorPresentationV2List& request = freshPhaseRequestsByAuthority[cellIt->second.authorityGuid];
                if (request.protocolVersion == 0)
                    request.protocolVersion = ActorSyncProtocolVersionV2;
                request.protocolVersion = ActorSyncProtocolVersionV2;
                request.authorityGuid = cellIt->second.authorityGuid;
                request.authorityGeneration = cellIt->second.authorityGeneration;
                request.sequence = ack.sequence;
                request.serverTimestamp = bootstrap.serverTimestamp;
                request.requestActorNetIds.push_back(actorNetId);
                requestedFreshPhase = true;
            }

            if (!requestedFreshPhase)
                bootstrap.snapshots.push_back(snapshot);
        }

        if (!bootstrap.snapshots.empty())
        {
            PacketActorPresentationV2 presentationPkt;
            presentationPkt.setPresentationList(&bootstrap);
            const std::vector<uint8_t> encoded = presentationPkt.encode();
            sendTo(c.conn, encoded, /*reliable=*/true);
            c.actorV2PresentationSentWindow += bootstrap.snapshots.size();
            c.actorV2PresentationBytesSentWindow += encoded.size();
            Log(Debug::Verbose) << "[Server] ActorSync v2 presentation bootstrap"
                                << " receiver=" << c.guid
                                << " cell=" << ack.cellId
                                << " snapshots=" << bootstrap.snapshots.size()
                                << " acked=" << newlyAcked
                                << " seq=" << ack.sequence;
        }
        else if (missingBootstrapIdentity != 0 || missingBootstrapActor != 0 || suppressedBootstrapCell != 0)
        {
            Log(Debug::Verbose) << "[Server] ActorSync v2 presentation bootstrap skipped"
                                << " receiver=" << c.guid
                                << " cell=" << ack.cellId
                                << " missingIdentity=" << missingBootstrapIdentity
                                << " missingActor=" << missingBootstrapActor
                                << " suppressedCell=" << suppressedBootstrapCell
                                << " acked=" << newlyAcked
                                << " seq=" << ack.sequence;
        }

        std::size_t freshPhaseRequestPackets = 0;
        std::size_t freshPhaseRequestActors = 0;
        for (auto& [authorityGuid, request] : freshPhaseRequestsByAuthority)
        {
            if (request.requestActorNetIds.empty())
                continue;

            auto authorityIt = std::find_if(mClients.begin(), mClients.end(),
                [authorityGuid](const auto& entry) { return entry.second.guid == authorityGuid; });
            if (authorityIt == mClients.end()
                || authorityIt->second.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
                continue;

            PacketActorPresentationV2 requestPkt;
            requestPkt.setPresentationList(&request);
            const std::vector<uint8_t> encoded = requestPkt.encode();
            sendTo(authorityIt->first, encoded, /*reliable=*/true);
            ++freshPhaseRequestPackets;
            freshPhaseRequestActors += request.requestActorNetIds.size();
        }

        if (freshPhaseRequestActors != 0)
        {
            Log(Debug::Verbose) << "[Server] ActorSync v2 requested fresh presentation phase"
                                << " receiver=" << c.guid
                                << " cell=" << ack.cellId
                                << " packets=" << freshPhaseRequestPackets
                                << " actors=" << freshPhaseRequestActors
                                << " seq=" << ack.sequence;
        }
    }

    if (invalidActorNetId != 0)
    {
        Log(Debug::Info) << "[Server] ActorSync v2 identity ack ignored invalid ids"
                         << " receiver=" << c.guid
                         << " cell=" << ack.cellId
                         << " invalidActorNetId=" << invalidActorNetId
                         << " firstInvalidActorKey=" << describeActorInstanceId(firstInvalidActorNetId)
                         << " seq=" << ack.sequence;
    }
}

void MPServer::handleActorPositionV2(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorPositionV2List incoming;
    PacketActorPositionV2 pkt;
    pkt.setPositionList(&incoming);
    if (!pkt.decode(data, size))
        return;
    if (incoming.protocolVersion != ActorSyncProtocolVersionV2)
    {
        Log(Debug::Warning) << "[Server] Rejecting ActorPositionV2 from " << c.name
                            << " unsupported protocol=" << incoming.protocolVersion;
        return;
    }

    const uint64_t timestamp = currentServerTimeMs();
    std::unordered_map<std::string, ActorList> updatesByCell;
    std::size_t accepted = 0;
    std::size_t invalidActorNetId = 0;
    std::size_t missingIdentity = 0;
    std::size_t wrongAuthority = 0;
    std::size_t deadVanillaSuppressed = 0;
    std::size_t deadSpawnedSuppressed = 0;
    std::size_t migratedByPositionCell = 0;
    std::size_t positionCellMismatchIgnored = 0;
    std::size_t staleReverseHandoffSuppressed = 0;
    std::size_t staleGenerationSuppressed = 0;
    std::size_t futureGenerationSuppressed = 0;
    ActorInstanceId firstInvalidActorNetId = 0;
    ActorInstanceId firstMissingActorNetId = 0;
    ActorInstanceId firstDeadVanillaActorNetId = 0;
    ActorInstanceId firstDeadSpawnedActorNetId = 0;
    std::unordered_set<std::string> migratedActorCells;
    std::unordered_set<std::string> canonicalDeadVanillaCellsToResend;

    auto applyPositionSnapshot = [](BaseActor& actor, const CompactActorSnapshot& snapshot,
                                   const std::string& cellId)
    {
        applyCompactActorSnapshotState(actor, snapshot, false);
        actor.cellId = cellId;
    };

    for (const CompactActorSnapshot& snapshot : incoming.snapshots)
    {
        if (!isValidActorInstanceId(snapshot.actorNetId))
        {
            ++invalidActorNetId;
            if (firstInvalidActorNetId == 0)
                firstInvalidActorNetId = snapshot.actorNetId;
            continue;
        }

        auto keyIt = mWorld.actorKeysByNetId.find(snapshot.actorNetId);
        if (keyIt == mWorld.actorKeysByNetId.end())
        {
            ++missingIdentity;
            if (firstMissingActorNetId == 0)
                firstMissingActorNetId = snapshot.actorNetId;
            continue;
        }

        const std::string& actorKey = keyIt->second;
        std::string cellId;
        CellActorState* cellState = nullptr;
        ActorRegistryRecord* record = nullptr;

        auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt != mWorld.actorLocations.end())
        {
            auto cellIt = mWorld.actorCells.find(locationIt->second);
            if (cellIt != mWorld.actorCells.end())
            {
                auto actorIt = cellIt->second.actors.find(actorKey);
                if (actorIt != cellIt->second.actors.end())
                {
                    cellId = cellIt->first;
                    cellState = &cellIt->second;
                    record = &actorIt->second;
                }
            }
        }

        if (!record)
        {
            for (auto& [candidateCellId, candidateCellState] : mWorld.actorCells)
            {
                auto actorIt = candidateCellState.actors.find(actorKey);
                if (actorIt == candidateCellState.actors.end())
                    continue;

                cellId = candidateCellId;
                cellState = &candidateCellState;
                record = &actorIt->second;
                rememberActorLocation(record->actor, cellId);
                break;
            }
        }

        if (!record || !cellState)
        {
            ++missingIdentity;
            if (firstMissingActorNetId == 0)
                firstMissingActorNetId = snapshot.actorNetId;
            continue;
        }

        // Reject snapshots whose migration generation does not match the
        // authoritative record. A stale source-generation snapshot must not
        // be accepted and relabeled with the current generation.
        if (generationIsNewer(record->migrationGeneration, snapshot.migrationGeneration))
        {
            ++staleGenerationSuppressed;
            continue;
        }
        if (generationIsNewer(snapshot.migrationGeneration, record->migrationGeneration))
        {
            ++futureGenerationSuppressed;
            continue;
        }

        std::string destinationCellId = cellId;
        if (record->actor.mpNum != 0 && isExteriorCellKey(cellId))
        {
            std::string positionCellId = exteriorCellIdForPosition(snapshot.position);
            if (!positionCellId.empty() && positionCellId != cellId)
                ++positionCellMismatchIgnored;

            // ActorCellChange is the only canonical migration path while handoff ordering is
            // being stabilized. Position snapshots remain movement-only and cannot rewrite
            // the server-owned actor cell.
            constexpr bool kAllowSpawnedActorCoordinateMigration = false;
            if (!kAllowSpawnedActorCoordinateMigration)
                positionCellId.clear();

            constexpr float kExteriorCellMismatchHysteresis = 64.f;
            constexpr uint64_t kCanonicalAuthorityFreshMs = 1000;
            const auto positionCellIt = mWorld.actorCells.find(positionCellId);
            const bool senderOwnsPositionCell = positionCellIt != mWorld.actorCells.end()
                && positionCellIt->second.authorityGuid == c.guid;
            const bool senderOwnsCanonicalCell = cellState->authorityGuid == c.guid;
            const bool destinationAuthorityHandoff = senderOwnsPositionCell && !senderOwnsCanonicalCell;
            const uint64_t canonicalSnapshotAge = record->lastCellChangeTime != 0
                && timestamp >= record->lastCellChangeTime
                ? timestamp - record->lastCellChangeTime
                : std::numeric_limits<uint64_t>::max();
            const bool staleReverseHandoff = destinationAuthorityHandoff
                && positionCellId == record->previousCellId
                && c.guid == record->previousCellAuthorityGuid
                && canonicalSnapshotAge <= kCanonicalAuthorityFreshMs;
            if (staleReverseHandoff)
            {
                ++staleReverseHandoffSuppressed;
                continue;
            }
            if (!positionCellId.empty()
                && positionCellId != cellId
                && (destinationAuthorityHandoff
                    || exteriorCellBorderDistance(snapshot.position) > kExteriorCellMismatchHysteresis)
                && clientHasActorCellLoaded(c, positionCellId))
            {
                destinationCellId = positionCellId;
            }
        }

        bool senderHasAuthority = isAllowedActorSender(c, *record, cellId);
        if (!senderHasAuthority && destinationCellId != cellId)
        {
            const auto destinationCellIt = mWorld.actorCells.find(destinationCellId);
            senderHasAuthority = destinationCellIt != mWorld.actorCells.end()
                && destinationCellIt->second.authorityGuid == c.guid;
        }
        if (!senderHasAuthority)
        {
            ++wrongAuthority;
            continue;
        }

        if (record->actor.mpNum == 0)
        {
            std::string deadCellId;
            if (const ActorRegistryRecord* deadRecord = findDeadVanillaActor(record->actor, &deadCellId))
            {
                if (deadCellId != cellId)
                {
                    forgetActorLocation(record->actor, cellId);
                    cellState->actors.erase(actorKey);
                    migratedActorCells.insert(cellId);
                    canonicalDeadVanillaCellsToResend.insert(deadCellId);
                }
                else
                {
                    BaseActor& actor = record->actor;
                    actor = deadRecord->actor;
                    actor.cellId = deadCellId;
                    actor.isDead = true;
                    actor.isInstantDeath = true;
                    actor.isMoving = false;
                    actor.isAttackingOrCasting = false;
                    actor.velocity = Velocity {};
                    actor.animFlags.animFwd = 0.f;
                    actor.animFlags.animSide = 0.f;
                    actor.animFlags.movementFlags = 0;
                    record->lastSnapshotTime = timestamp;
                    ensureActorNetId(*record, deadCellId);
                }
                ++deadVanillaSuppressed;
                if (firstDeadVanillaActorNetId == 0)
                    firstDeadVanillaActorNetId = snapshot.actorNetId;
                continue;
            }
        }

        if (record->actor.mpNum != 0 && record->actor.isDead)
        {
            ++deadSpawnedSuppressed;
            if (firstDeadSpawnedActorNetId == 0)
                firstDeadSpawnedActorNetId = snapshot.actorNetId;
            continue;
        }

        BaseActor& actor = record->actor;

        if (destinationCellId != cellId)
        {
            ActorRegistryRecord movedRecord = *record;
            applyPositionSnapshot(movedRecord.actor, snapshot, destinationCellId);
            movedRecord.lastSnapshotTime = timestamp;
            movedRecord.previousCellId = cellId;
            movedRecord.previousCellAuthorityGuid = cellState->authorityGuid;
            movedRecord.lastCellChangeTime = timestamp;

            forgetActorLocation(record->actor, cellId);
            cellState->actors.erase(actorKey);
            migratedActorCells.insert(cellId);
            if (mPlayerDb && movedRecord.actor.mpNum != 0)
                mPlayerDb->deleteSpawnedActorDynamicRecordLink(movedRecord.actor.mpNum, cellId);

            std::unordered_set<std::string> changedCellIds;
            removeActorFromOtherCells(movedRecord.actor, destinationCellId, changedCellIds);
            migratedActorCells.insert(changedCellIds.begin(), changedCellIds.end());

            auto& destinationCellState = mWorld.actorCells[destinationCellId];
            auto [destIt, inserted] = destinationCellState.actors.emplace(actorKey, movedRecord);
            if (!inserted)
                destIt->second = movedRecord;
            ensureActorNetId(destIt->second, destinationCellId);
            rememberActorLocation(destIt->second.actor, destinationCellId);
            persistSpawnedActorIfNeeded(destIt->second, timestamp, false);
            upsertSpawnedActorDynamicRecordLinkIfNeeded(destIt->second.actor);
            markLuaActorDirty(destIt->second, destinationCellId);
            if (destinationCellState.authorityGuid == 0)
                refreshActorAuthorityForCell(destinationCellId, c.guid);

            ActorList& outgoing = updatesByCell[destinationCellId];
            if (outgoing.cellId.empty())
            {
                outgoing.cellId = destinationCellId;
                outgoing.isAuthority = true;
                outgoing.authorityGuid = destinationCellState.authorityGuid;
                outgoing.authorityGeneration = destinationCellState.authorityGeneration;
                outgoing.snapshotSequence = destinationCellState.nextSnapshotSequence++;
                outgoing.serverTimestamp = timestamp;
            }
            outgoing.actors.push_back(destIt->second.actor);
            ++accepted;
            ++migratedByPositionCell;

            Log(Debug::Info) << "[Server] ActorPositionV2 migrated spawned actor by position cell"
                             << " refId=" << destIt->second.actor.refId
                             << " mpNum=" << destIt->second.actor.mpNum
                             << " from=" << cellId
                             << " to=" << destinationCellId
                             << " pos=(" << snapshot.position.pos[0]
                             << "," << snapshot.position.pos[1]
                             << "," << snapshot.position.pos[2] << ")";
            continue;
        }

        applyPositionSnapshot(actor, snapshot, cellId);
        record->lastSnapshotTime = timestamp;
        ensureActorNetId(*record, cellId);
        persistSpawnedActorIfNeeded(*record, timestamp, false);
        markLuaActorDirty(*record, cellId);

        ActorList& outgoing = updatesByCell[cellId];
        if (outgoing.cellId.empty())
        {
            outgoing.cellId = cellId;
            outgoing.isAuthority = true;
            outgoing.authorityGuid = cellState->authorityGuid;
            outgoing.authorityGeneration = cellState->authorityGeneration;
            outgoing.snapshotSequence = cellState->nextSnapshotSequence++;
            outgoing.serverTimestamp = timestamp;
        }
        outgoing.actors.push_back(actor);
        ++accepted;
    }

    for (auto& [cellId, actorList] : updatesByCell)
    {
        auto cellIt = mWorld.actorCells.find(cellId);
        if (cellIt == mWorld.actorCells.end())
            continue;
        broadcastActorPositionV2ToCell(cellId, cellIt->second, actorList, c.conn);
    }

    for (const std::string& migratedCellId : migratedActorCells)
    {
        auto migratedCellIt = mWorld.actorCells.find(migratedCellId);
        if (migratedCellIt != mWorld.actorCells.end())
            broadcastActorListForCell(migratedCellId, migratedCellIt->second);
    }

    for (const std::string& deadCellId : canonicalDeadVanillaCellsToResend)
        sendActorStateToInterestedClients(deadCellId);

    if (invalidActorNetId != 0 || missingIdentity != 0 || wrongAuthority != 0
        || staleReverseHandoffSuppressed != 0
        || deadVanillaSuppressed != 0 || deadSpawnedSuppressed != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorPositionV2 filtered"
                            << " from=" << c.name
                            << " snapshots=" << incoming.snapshots.size()
                            << " accepted=" << accepted
                            << " invalidActorNetId=" << invalidActorNetId
                            << " firstInvalidActorKey=" << describeActorInstanceId(firstInvalidActorNetId)
                            << " missingIdentity=" << missingIdentity
                            << " firstMissingActorNetId=" << firstMissingActorNetId
                            << " firstMissingActorKey=" << describeActorInstanceId(firstMissingActorNetId)
                            << " wrongAuthority=" << wrongAuthority
                            << " staleReverseHandoffSuppressed=" << staleReverseHandoffSuppressed
                            << " deadVanillaSuppressed=" << deadVanillaSuppressed
                            << " firstDeadVanillaActorNetId=" << firstDeadVanillaActorNetId
                            << " firstDeadVanillaActorKey=" << describeActorInstanceId(firstDeadVanillaActorNetId)
                            << " deadSpawnedSuppressed=" << deadSpawnedSuppressed
                            << " firstDeadSpawnedActorNetId=" << firstDeadSpawnedActorNetId
                            << " firstDeadSpawnedActorKey=" << describeActorInstanceId(firstDeadSpawnedActorNetId);
    }

    if (positionCellMismatchIgnored != 0)
    {
        static uint64_t nextPositionMismatchLogTime = 0;
        if (timestamp >= nextPositionMismatchLogTime)
        {
            Log(Debug::Verbose) << "[Server] ActorPositionV2 ignored coordinate cell mismatch"
                             << " from=" << c.name
                             << " ignored=" << positionCellMismatchIgnored
                             << " snapshots=" << incoming.snapshots.size();
            nextPositionMismatchLogTime = timestamp + 5000;
        }
    }

    if (migratedByPositionCell != 0 || staleGenerationSuppressed != 0 || futureGenerationSuppressed != 0)
    {
        Log(Debug::Info) << "[Server] ActorPositionV2 summary"
                         << " from=" << c.name
                         << " migrated=" << migratedByPositionCell
                         << " staleGenerationSuppressed=" << staleGenerationSuppressed
                         << " futureGenerationSuppressed=" << futureGenerationSuppressed
                         << " snapshots=" << incoming.snapshots.size();
    }
}

void MPServer::handleActorPresentationV2(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorPresentationV2List incoming;
    PacketActorPresentationV2 pkt;
    pkt.setPresentationList(&incoming);
    if (!pkt.decode(data, size))
        return;
    if (incoming.protocolVersion != ActorSyncProtocolVersionV2)
    {
        Log(Debug::Warning) << "[Server] Rejecting ActorPresentationV2 from " << c.name
                            << " unsupported protocol=" << incoming.protocolVersion;
        return;
    }

    const uint64_t timestamp = currentServerTimeMs();
    std::unordered_map<std::string, ActorList> updatesByCell;
    std::size_t accepted = 0;
    std::size_t invalidActorNetId = 0;
    std::size_t missingIdentity = 0;
    std::size_t wrongAuthority = 0;
    std::size_t deadVanillaSuppressed = 0;
    std::size_t deadSpawnedSuppressed = 0;
    ActorInstanceId firstInvalidActorNetId = 0;
    ActorInstanceId firstMissingActorNetId = 0;
    ActorInstanceId firstDeadVanillaActorNetId = 0;
    ActorInstanceId firstDeadSpawnedActorNetId = 0;
    std::unordered_set<std::string> staleLiveVanillaCellsChanged;
    std::unordered_set<std::string> canonicalDeadVanillaCellsToResend;

    for (const ActorPresentationSnapshot& snapshot : incoming.snapshots)
    {
        if (!isValidActorInstanceId(snapshot.actorNetId))
        {
            ++invalidActorNetId;
            if (firstInvalidActorNetId == 0)
                firstInvalidActorNetId = snapshot.actorNetId;
            continue;
        }

        auto keyIt = mWorld.actorKeysByNetId.find(snapshot.actorNetId);
        if (keyIt == mWorld.actorKeysByNetId.end())
        {
            ++missingIdentity;
            if (firstMissingActorNetId == 0)
                firstMissingActorNetId = snapshot.actorNetId;
            continue;
        }

        const std::string& actorKey = keyIt->second;
        std::string cellId;
        CellActorState* cellState = nullptr;
        ActorRegistryRecord* record = nullptr;

        auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt != mWorld.actorLocations.end())
        {
            auto cellIt = mWorld.actorCells.find(locationIt->second);
            if (cellIt != mWorld.actorCells.end())
            {
                auto actorIt = cellIt->second.actors.find(actorKey);
                if (actorIt != cellIt->second.actors.end())
                {
                    cellId = cellIt->first;
                    cellState = &cellIt->second;
                    record = &actorIt->second;
                }
            }
        }

        if (!record)
        {
            for (auto& [candidateCellId, candidateCellState] : mWorld.actorCells)
            {
                auto actorIt = candidateCellState.actors.find(actorKey);
                if (actorIt == candidateCellState.actors.end())
                    continue;

                cellId = candidateCellId;
                cellState = &candidateCellState;
                record = &actorIt->second;
                rememberActorLocation(record->actor, cellId);
                break;
            }
        }

        if (!record || !cellState)
        {
            ++missingIdentity;
            if (firstMissingActorNetId == 0)
                firstMissingActorNetId = snapshot.actorNetId;
            continue;
        }

        if (!isAllowedActorSender(c, *record, cellId))
        {
            ++wrongAuthority;
            continue;
        }

        const bool snapshotIsDead = snapshot.isDead || ((snapshot.presentationFlags & ActorPresentationDead) != 0);
        if (record->actor.mpNum == 0)
        {
            std::string deadCellId;
            if (const ActorRegistryRecord* deadRecord = findDeadVanillaActor(record->actor, &deadCellId))
            {
                if (deadCellId != cellId)
                {
                    forgetActorLocation(record->actor, cellId);
                    cellState->actors.erase(actorKey);
                    staleLiveVanillaCellsChanged.insert(cellId);
                    canonicalDeadVanillaCellsToResend.insert(deadCellId);
                }
                else
                {
                    BaseActor& actor = record->actor;
                    actor = deadRecord->actor;
                    actor.cellId = deadCellId;
                    actor.isDead = true;
                    actor.isInstantDeath = true;
                    actor.isMoving = false;
                    actor.isAttackingOrCasting = false;
                    actor.velocity = Velocity {};
                    actor.animFlags.animFwd = 0.f;
                    actor.animFlags.animSide = 0.f;
                    actor.animFlags.movementFlags = 0;
                    record->lastSnapshotTime = timestamp;
                    ensureActorNetId(*record, deadCellId);
                }
                ++deadVanillaSuppressed;
                if (firstDeadVanillaActorNetId == 0)
                    firstDeadVanillaActorNetId = snapshot.actorNetId;
                continue;
            }
        }

        if (record->actor.mpNum != 0 && record->actor.isDead && !snapshotIsDead)
        {
            ++deadSpawnedSuppressed;
            if (firstDeadSpawnedActorNetId == 0)
                firstDeadSpawnedActorNetId = snapshot.actorNetId;
            continue;
        }

        BaseActor& actor = record->actor;
        const bool wasDead = actor.isDead;
        actor.isAttackingOrCasting = snapshot.isAttackingOrCasting;
        actor.hasWeaponDrawn = snapshot.hasWeaponDrawn;
        actor.hasSpellReadied = snapshot.hasSpellReadied;
        actor.isDead = snapshotIsDead;
        actor.position.isTeleporting = (snapshot.presentationFlags & ActorPresentationTeleporting) != 0;
        static constexpr uint32_t kReliablePresentationMovementFlags =
            AnimFlags::MF_KNOCKED_DOWN | AnimFlags::MF_KNOCKED_OUT | AnimFlags::MF_RECOVERY;
        actor.animFlags.movementFlags =
            (actor.animFlags.movementFlags & ~kReliablePresentationMovementFlags)
            | (snapshot.movementFlags & kReliablePresentationMovementFlags);
        if (isReliablePresentationAnimGroup(snapshot.currentAnimGroup))
        {
            actor.animFlags.currentAnimGroup = snapshot.currentAnimGroup;
            actor.animFlags.currentAnimCompletion = snapshot.currentAnimCompletion;
        }
        else if (isReliablePresentationAnimGroup(actor.animFlags.currentAnimGroup))
        {
            actor.animFlags.currentAnimGroup.clear();
            actor.animFlags.currentAnimCompletion = -1.f;
        }
        if (snapshotIsDead)
        {
            actor.isMoving = false;
            actor.isAttackingOrCasting = false;
            actor.velocity = Velocity {};
            actor.animFlags.animFwd = 0.f;
            actor.animFlags.animSide = 0.f;
            actor.animFlags.movementFlags = 0;
        }
        actor.cellId = cellId;
        record->lastSnapshotTime = timestamp;
        ensureActorNetId(*record, cellId);
        markLuaActorDirty(*record, cellId);
        if (actor.mpNum != 0 && actor.isDead && !wasDead)
        {
            Log(Debug::Info) << "[Server] ActorPresentationV2 observed spawned death transition"
                             << " refId=" << actor.refId
                             << " mpNum=" << actor.mpNum
                             << " cell=" << cellId;
            sendActorLifecycleEvent("death", actor, record->persistent);
        }

        ActorList& outgoing = updatesByCell[cellId];
        if (outgoing.cellId.empty())
        {
            outgoing.cellId = cellId;
            outgoing.isAuthority = true;
            outgoing.authorityGuid = cellState->authorityGuid;
            outgoing.authorityGeneration = cellState->authorityGeneration;
            outgoing.snapshotSequence = cellState->nextSnapshotSequence++;
            outgoing.serverTimestamp = timestamp;
        }
        // Relay this event's own presentation state. The registry actor also
        // contains the latest continuous position state; rebuilding a presentation
        // packet from that cached movement can mark a stationary idle as moving.
        BaseActor presentationActor = actor;
        presentationActor.isMoving = snapshot.isMoving;
        presentationActor.velocity = Velocity {};
        presentationActor.animFlags.animFwd = dequantizeActorAxis(snapshot.animFwd);
        presentationActor.animFlags.animSide = dequantizeActorAxis(snapshot.animSide);
        presentationActor.animFlags.currentAnimGroup = snapshot.currentAnimGroup;
        presentationActor.animFlags.currentAnimCompletion = snapshot.currentAnimCompletion;
        outgoing.actors.push_back(std::move(presentationActor));
        ++accepted;
    }

    for (auto& [cellId, actorList] : updatesByCell)
    {
        auto cellIt = mWorld.actorCells.find(cellId);
        if (cellIt == mWorld.actorCells.end())
            continue;
        broadcastActorPresentationV2ToCell(cellId, cellIt->second, actorList, c.conn);
    }

    for (const std::string& changedCellId : staleLiveVanillaCellsChanged)
    {
        auto changedCellIt = mWorld.actorCells.find(changedCellId);
        if (changedCellIt != mWorld.actorCells.end())
            broadcastActorListForCell(changedCellId, changedCellIt->second);
    }

    for (const std::string& deadCellId : canonicalDeadVanillaCellsToResend)
        sendActorStateToInterestedClients(deadCellId);

    if (invalidActorNetId != 0 || missingIdentity != 0 || wrongAuthority != 0
        || deadVanillaSuppressed != 0 || deadSpawnedSuppressed != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorPresentationV2 filtered"
                            << " from=" << c.name
                            << " snapshots=" << incoming.snapshots.size()
                            << " accepted=" << accepted
                            << " invalidActorNetId=" << invalidActorNetId
                            << " firstInvalidActorKey=" << describeActorInstanceId(firstInvalidActorNetId)
                            << " missingIdentity=" << missingIdentity
                            << " firstMissingActorNetId=" << firstMissingActorNetId
                            << " firstMissingActorKey=" << describeActorInstanceId(firstMissingActorNetId)
                            << " wrongAuthority=" << wrongAuthority
                            << " deadVanillaSuppressed=" << deadVanillaSuppressed
                            << " firstDeadVanillaActorNetId=" << firstDeadVanillaActorNetId
                            << " firstDeadVanillaActorKey=" << describeActorInstanceId(firstDeadVanillaActorNetId)
                            << " deadSpawnedSuppressed=" << deadSpawnedSuppressed
                            << " firstDeadSpawnedActorNetId=" << firstDeadSpawnedActorNetId
                            << " firstDeadSpawnedActorKey=" << describeActorInstanceId(firstDeadSpawnedActorNetId);
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleActorAnimFlags(ConnectedClient& c, const uint8_t* data, size_t size)
{
    if (c.actorSyncProtocolVersion >= ActorSyncProtocolVersionV2)
    {
        Log(Debug::Verbose) << "[Server] Ignoring retired legacy ActorAnimFlags from v2 client"
                            << " from=" << c.name
                            << " protocol=" << c.actorSyncProtocolVersion;
        return;
    }

    ActorList incoming;
    PacketActorAnimFlags pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorAnimFlags")) return;

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorAnimFlags"))
            continue;

        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorAnimFlags");
        if (!stored)
            continue;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.animFlags = actor.animFlags;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        persistSpawnedActorIfNeeded(*stored);
        markLuaActorDirty(*stored, incoming.cellId);
        filtered.actors.push_back(actor);
    }

    if (filtered.actors.empty())
        return;

    PacketActorAnimFlags out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn, /*reliable=*/false);
}

// ---------------------------------------------------------------------------
void MPServer::handleActorAnimPlay(ConnectedClient& c, const uint8_t* data, size_t size)
{
    if (c.actorSyncProtocolVersion >= ActorSyncProtocolVersionV2)
    {
        Log(Debug::Verbose) << "[Server] Ignoring retired legacy ActorAnimPlay from v2 client"
                            << " from=" << c.name
                            << " protocol=" << c.actorSyncProtocolVersion;
        return;
    }

    ActorList incoming;
    PacketActorAnimPlay pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorAnimPlay")) return;

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorAnimPlay"))
            continue;

        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorAnimPlay");
        if (!stored)
            continue;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.animPlay = actor.animPlay;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        persistSpawnedActorIfNeeded(*stored);
        markLuaActorDirty(*stored, incoming.cellId);
        filtered.actors.push_back(actor);
    }

    if (filtered.actors.empty())
        return;

    PacketActorAnimPlay out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handleActorAttack(ConnectedClient& c, const uint8_t* data, size_t size)
{
    if (c.actorSyncProtocolVersion >= ActorSyncProtocolVersionV2)
    {
        Log(Debug::Verbose) << "[Server] Ignoring retired legacy ActorAttack from v2 client"
                            << " from=" << c.name
                            << " protocol=" << c.actorSyncProtocolVersion;
        return;
    }

    ActorList incoming;
    PacketActorAttack pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    Log(Debug::Info) << "[Server] Received ActorAttack from " << c.name << " cellId=" << incoming.cellId
                     << " actors=" << incoming.actors.size();
    if (!validateActorUpdate(c, incoming, "ActorAttack"))
    {
        Log(Debug::Info) << "[Server] ActorAttack rejected by validateActorUpdate from " << c.name
                         << " cellId=" << incoming.cellId;
        return;
    }
    Log(Debug::Info) << "[Server] ActorAttack accepted from " << c.name
                     << " cell=" << incoming.cellId
                     << " actors=" << incoming.actors.size();

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorAttack"))
            continue;

        Log(Debug::Verbose) << "[Server] ActorAttack candidate from " << c.name
                            << " refId=" << actor.refId
                            << " refNum=" << actor.refNum
                            << " mpNum=" << actor.mpNum
                            << " targetMpNum=" << actor.attack.targetMpNum
                            << " damage=" << actor.attack.damage
                            << " healthDamage=" << actor.attack.healthDamage;
        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorAttack");
        if (!stored)
        {
            Log(Debug::Warning) << "[Server] ActorAttack dropped for untracked actor"
                                << " refId=" << actor.refId
                                << " refNum=" << actor.refNum
                                << " mpNum=" << actor.mpNum
                                << " cell=" << incoming.cellId;
            continue;
        }
        Log(Debug::Verbose) << "[Server] ActorAttack matched tracked actor"
                            << " refId=" << stored->actor.refId
                            << " refNum=" << stored->actor.refNum
                            << " mpNum=" << stored->actor.mpNum
                            << " persistent=" << stored->persistent
                            << " wasDead=" << stored->actor.isDead;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.attack = actor.attack;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        persistSpawnedActorIfNeeded(*stored);
        markLuaActorDirty(*stored, incoming.cellId);
        filtered.actors.push_back(actor);
    }

    if (filtered.actors.empty())
    {
        Log(Debug::Info) << "[Server] ActorAttack dropped after filtering"
                         << " from=" << c.name
                         << " cell=" << incoming.cellId
                         << " actors=" << incoming.actors.size();
        return;
    }

    PacketActorAttack out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn);
    Log(Debug::Info) << "[Server] Broadcast ActorAttack to cell=" << filtered.cellId
                     << " actors=" << filtered.actors.size();
}

// ---------------------------------------------------------------------------
void MPServer::handleActorAttackV2(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorAttackV2List incoming;
    PacketActorAttackV2 pkt;
    pkt.setAttackList(&incoming);
    if (!pkt.decode(data, size))
        return;
    if (incoming.protocolVersion != ActorSyncProtocolVersionV2)
    {
        Log(Debug::Warning) << "[Server] Rejecting ActorAttackV2 from " << c.name
                            << " unsupported protocol=" << incoming.protocolVersion;
        return;
    }

    const uint64_t timestamp = currentServerTimeMs();
    std::unordered_map<std::string, ActorAttackV2List> updatesByCell;
    std::size_t accepted = 0;
    std::size_t invalidActorNetId = 0;
    std::size_t missingIdentity = 0;
    std::size_t wrongAuthority = 0;
    std::size_t unloadedCell = 0;
    std::size_t deadVanillaSuppressed = 0;
    ActorInstanceId firstInvalidActorNetId = 0;
    ActorInstanceId firstMissingActorNetId = 0;
    ActorInstanceId firstDeadVanillaActorNetId = 0;
    std::unordered_set<std::string> staleLiveVanillaCellsChanged;
    std::unordered_set<std::string> canonicalDeadVanillaCellsToResend;

    for (const ActorAttackV2Event& event : incoming.events)
    {
        if (!isValidActorInstanceId(event.actorNetId))
        {
            ++invalidActorNetId;
            if (firstInvalidActorNetId == 0)
                firstInvalidActorNetId = event.actorNetId;
            continue;
        }

        auto keyIt = mWorld.actorKeysByNetId.find(event.actorNetId);
        if (keyIt == mWorld.actorKeysByNetId.end())
        {
            ++missingIdentity;
            if (firstMissingActorNetId == 0)
                firstMissingActorNetId = event.actorNetId;
            continue;
        }

        const std::string& actorKey = keyIt->second;
        std::string cellId;
        CellActorState* cellState = nullptr;
        ActorRegistryRecord* record = nullptr;

        auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt != mWorld.actorLocations.end())
        {
            auto cellIt = mWorld.actorCells.find(locationIt->second);
            if (cellIt != mWorld.actorCells.end())
            {
                auto actorIt = cellIt->second.actors.find(actorKey);
                if (actorIt != cellIt->second.actors.end())
                {
                    cellId = cellIt->first;
                    cellState = &cellIt->second;
                    record = &actorIt->second;
                }
            }
        }

        if (!record)
        {
            for (auto& [candidateCellId, candidateCellState] : mWorld.actorCells)
            {
                auto actorIt = candidateCellState.actors.find(actorKey);
                if (actorIt == candidateCellState.actors.end())
                    continue;

                cellId = candidateCellId;
                cellState = &candidateCellState;
                record = &actorIt->second;
                rememberActorLocation(record->actor, cellId);
                break;
            }
        }

        if (!record || !cellState)
        {
            ++missingIdentity;
            if (firstMissingActorNetId == 0)
                firstMissingActorNetId = event.actorNetId;
            continue;
        }

        if (!clientEligibleForActorCell(c, cellId))
        {
            ++unloadedCell;
            continue;
        }

        if (!isAllowedActorSender(c, *record, cellId))
        {
            ++wrongAuthority;
            continue;
        }

        if (record->actor.mpNum == 0)
        {
            std::string deadCellId;
            if (const ActorRegistryRecord* deadRecord = findDeadVanillaActor(record->actor, &deadCellId))
            {
                if (deadCellId != cellId)
                {
                    forgetActorLocation(record->actor, cellId);
                    cellState->actors.erase(actorKey);
                    staleLiveVanillaCellsChanged.insert(cellId);
                    canonicalDeadVanillaCellsToResend.insert(deadCellId);
                }
                else
                {
                    BaseActor& actor = record->actor;
                    actor = deadRecord->actor;
                    actor.cellId = deadCellId;
                    actor.isDead = true;
                    actor.isInstantDeath = true;
                    actor.isMoving = false;
                    actor.isAttackingOrCasting = false;
                    actor.velocity = Velocity {};
                    actor.animFlags.animFwd = 0.f;
                    actor.animFlags.animSide = 0.f;
                    actor.animFlags.movementFlags = 0;
                    record->lastSnapshotTime = timestamp;
                    ensureActorNetId(*record, deadCellId);
                }
                ++deadVanillaSuppressed;
                if (firstDeadVanillaActorNetId == 0)
                    firstDeadVanillaActorNetId = event.actorNetId;
                continue;
            }
        }

        BaseActor& actor = record->actor;
        actor.attack = event.attack;
        actor.cellId = cellId;
        record->lastSnapshotTime = timestamp;
        const ActorInstanceId actorNetId = ensureActorNetId(*record, cellId);
        persistSpawnedActorIfNeeded(*record);
        markLuaActorDirty(*record, cellId);

        ActorAttackV2List& outgoing = updatesByCell[cellId];
        if (outgoing.events.empty())
        {
            outgoing.protocolVersion = ActorSyncProtocolVersionV2;
            outgoing.cellId = cellId;
            outgoing.authorityGuid = cellState->authorityGuid;
            outgoing.authorityGeneration = cellState->authorityGeneration;
            outgoing.sequence = cellState->nextSnapshotSequence++;
            outgoing.serverTimestamp = timestamp;
        }

        ActorAttackV2Event outgoingEvent = event;
        outgoingEvent.actorNetId = actorNetId;
        outgoing.events.push_back(outgoingEvent);
        ++accepted;
    }

    std::size_t sent = 0;
    std::size_t suppressedUntilIdentityKnown = 0;
    for (auto& [cellId, attackList] : updatesByCell)
    {
        for (auto& [conn, client] : mClients)
        {
            if (conn == c.conn
                || !clientHasActorCellLoaded(client, cellId)
                || client.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
                continue;

            ActorAttackV2List filtered = attackList;
            filtered.events.clear();
            filtered.events.reserve(attackList.events.size());
            for (const ActorAttackV2Event& event : attackList.events)
            {
                if (client.actorV2IdentitySent.count(event.actorNetId) == 0
                    || client.actorV2IdentityAcked.count(event.actorNetId) == 0)
                {
                    ++suppressedUntilIdentityKnown;
                    ++client.actorV2AttackSuppressedUntilIdentityKnownWindow;
                    ++client.actorV2MissingIdentityByNetIdWindow[event.actorNetId];
                    continue;
                }

                filtered.events.push_back(event);
            }

            if (filtered.events.empty())
                continue;

            PacketActorAttackV2 out;
            out.setAttackList(&filtered);
            sendTo(conn, out.encode(), /*reliable=*/true);
            client.actorV2AttackSentWindow += filtered.events.size();
            sent += filtered.events.size();
        }
    }

    for (const std::string& changedCellId : staleLiveVanillaCellsChanged)
    {
        auto changedCellIt = mWorld.actorCells.find(changedCellId);
        if (changedCellIt != mWorld.actorCells.end())
            broadcastActorListForCell(changedCellId, changedCellIt->second);
    }

    for (const std::string& deadCellId : canonicalDeadVanillaCellsToResend)
        sendActorStateToInterestedClients(deadCellId);

    Log((invalidActorNetId != 0 || missingIdentity != 0 || wrongAuthority != 0 || unloadedCell != 0
            || deadVanillaSuppressed != 0 || suppressedUntilIdentityKnown != 0) ? Debug::Info : Debug::Verbose)
        << "[Server] ActorAttackV2"
        << " from=" << c.name
        << " packetCell=" << incoming.cellId
        << " events=" << incoming.events.size()
        << " accepted=" << accepted
        << " sent=" << sent
        << " invalidActorNetId=" << invalidActorNetId
        << " firstInvalidActorKey=" << describeActorInstanceId(firstInvalidActorNetId)
        << " missingIdentity=" << missingIdentity
        << " firstMissingActorNetId=" << firstMissingActorNetId
        << " firstMissingActorKey=" << describeActorInstanceId(firstMissingActorNetId)
        << " wrongAuthority=" << wrongAuthority
        << " unloadedCell=" << unloadedCell
        << " deadVanillaSuppressed=" << deadVanillaSuppressed
        << " firstDeadVanillaActorNetId=" << firstDeadVanillaActorNetId
        << " firstDeadVanillaActorKey=" << describeActorInstanceId(firstDeadVanillaActorNetId)
        << " suppressedUntilIdentityKnown=" << suppressedUntilIdentityKnown;
}

// ---------------------------------------------------------------------------
void MPServer::handleActorSpeech(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorSpeechList incoming;
    PacketActorSpeech pkt;
    pkt.setSpeechList(&incoming);
    if (!pkt.decode(data, size))
        return;
    if (incoming.protocolVersion != ActorSyncProtocolVersionV2)
    {
        Log(Debug::Warning) << "[Server] Rejecting ActorSpeech from " << c.name
                            << " unsupported protocol=" << incoming.protocolVersion;
        return;
    }

    const uint64_t timestamp = currentServerTimeMs();
    std::unordered_map<std::string, ActorSpeechList> updatesByCell;
    std::size_t accepted = 0;
    std::size_t invalid = 0;
    std::size_t missingIdentity = 0;
    std::size_t wrongAuthority = 0;
    std::size_t unloadedCell = 0;
    std::size_t deadSuppressed = 0;

    for (const ActorSpeechEvent& event : incoming.events)
    {
        if (!isValidActorInstanceId(event.actorNetId)
            || event.eventId == 0
            || event.sound.empty()
            || event.sound.size() > 1024
            || event.sound.rfind("sound/", 0) != 0)
        {
            ++invalid;
            continue;
        }

        auto keyIt = mWorld.actorKeysByNetId.find(event.actorNetId);
        if (keyIt == mWorld.actorKeysByNetId.end())
        {
            ++missingIdentity;
            continue;
        }

        const std::string& actorKey = keyIt->second;
        std::string cellId;
        CellActorState* cellState = nullptr;
        ActorRegistryRecord* record = nullptr;

        auto locationIt = mWorld.actorLocations.find(actorKey);
        if (locationIt != mWorld.actorLocations.end())
        {
            auto cellIt = mWorld.actorCells.find(locationIt->second);
            if (cellIt != mWorld.actorCells.end())
            {
                auto actorIt = cellIt->second.actors.find(actorKey);
                if (actorIt != cellIt->second.actors.end())
                {
                    cellId = cellIt->first;
                    cellState = &cellIt->second;
                    record = &actorIt->second;
                }
            }
        }

        if (!record)
        {
            for (auto& [candidateCellId, candidateCellState] : mWorld.actorCells)
            {
                auto actorIt = candidateCellState.actors.find(actorKey);
                if (actorIt == candidateCellState.actors.end())
                    continue;

                cellId = candidateCellId;
                cellState = &candidateCellState;
                record = &actorIt->second;
                rememberActorLocation(record->actor, cellId);
                break;
            }
        }

        if (!record || !cellState)
        {
            ++missingIdentity;
            continue;
        }
        if (!clientEligibleForActorCell(c, cellId))
        {
            ++unloadedCell;
            continue;
        }
        if (!isAllowedActorSender(c, *record, cellId))
        {
            ++wrongAuthority;
            continue;
        }
        if (record->actor.isDead)
        {
            ++deadSuppressed;
            continue;
        }

        ActorSpeechList& outgoing = updatesByCell[cellId];
        if (outgoing.events.empty())
        {
            outgoing.protocolVersion = ActorSyncProtocolVersionV2;
            outgoing.cellId = cellId;
            outgoing.authorityGuid = cellState->authorityGuid;
            outgoing.authorityGeneration = cellState->authorityGeneration;
            outgoing.sequence = cellState->nextSnapshotSequence++;
            outgoing.serverTimestamp = timestamp;
        }
        outgoing.events.push_back(event);
        ++accepted;
    }

    std::size_t sent = 0;
    std::size_t suppressedUntilIdentityKnown = 0;
    for (auto& [cellId, speechList] : updatesByCell)
    {
        for (auto& [conn, client] : mClients)
        {
            if (conn == c.conn
                || !clientHasActorCellLoaded(client, cellId)
                || client.actorSyncProtocolVersion < ActorSyncProtocolVersionV2)
                continue;

            ActorSpeechList filtered = speechList;
            filtered.events.clear();
            filtered.events.reserve(speechList.events.size());
            for (const ActorSpeechEvent& event : speechList.events)
            {
                if (client.actorV2IdentitySent.count(event.actorNetId) == 0
                    || client.actorV2IdentityAcked.count(event.actorNetId) == 0)
                {
                    ++suppressedUntilIdentityKnown;
                    continue;
                }
                filtered.events.push_back(event);
            }

            if (filtered.events.empty())
                continue;

            PacketActorSpeech out;
            out.setSpeechList(&filtered);
            sendTo(conn, out.encode(), /*reliable=*/true);
            sent += filtered.events.size();
        }
    }

    Log((invalid != 0 || missingIdentity != 0 || wrongAuthority != 0 || unloadedCell != 0
            || deadSuppressed != 0 || suppressedUntilIdentityKnown != 0) ? Debug::Info : Debug::Verbose)
        << "[Server] ActorSpeech"
        << " from=" << c.name
        << " packetCell=" << incoming.cellId
        << " events=" << incoming.events.size()
        << " accepted=" << accepted
        << " sent=" << sent
        << " invalid=" << invalid
        << " missingIdentity=" << missingIdentity
        << " wrongAuthority=" << wrongAuthority
        << " unloadedCell=" << unloadedCell
        << " deadSuppressed=" << deadSuppressed
        << " suppressedUntilIdentityKnown=" << suppressedUntilIdentityKnown;
}

// ---------------------------------------------------------------------------
void MPServer::handleActorCast(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorList incoming;
    PacketActorCast pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    Log(Debug::Info) << "[Server] Received ActorCast from " << c.name << " cellId=" << incoming.cellId;
    if (!validateActorUpdate(c, incoming, "ActorCast")) return;
    Log(Debug::Info) << "[Server] ActorCast from " << c.name << " cell=" << incoming.cellId << " actors=" << incoming.actors.size();

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorCast"))
            continue;

        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorCast");
        if (!stored)
            continue;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.cast = actor.cast;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        persistSpawnedActorIfNeeded(*stored);
        markLuaActorDirty(*stored, incoming.cellId);
        filtered.actors.push_back(actor);
    }

    if (filtered.actors.empty())
        return;

    PacketActorCast out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn);
    Log(Debug::Info) << "[Server] Broadcast ActorCast to cell=" << filtered.cellId;
}

// ---------------------------------------------------------------------------
void MPServer::handleActorCellChange(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorList incoming;
    PacketActorCellChange pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorCellChange")) return;

    auto& sourceCellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = sourceCellState.authorityGuid;
    incoming.authorityGeneration = sourceCellState.authorityGeneration;
    incoming.snapshotSequence = sourceCellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList accepted = incoming;
    accepted.actors.clear();
    accepted.actors.reserve(incoming.actors.size());
    std::unordered_set<std::string> destinationCellIds;
    std::unordered_set<std::string> changedCellIds;
    std::vector<ActorRegistryRecord> storedAcceptedRecords;

    for (auto& actor : incoming.actors)
    {
        const std::string destinationCellId = actor.cellId;
        if (destinationCellId.empty() || destinationCellId == incoming.cellId)
        {
            Log(Debug::Warning) << "[Server] Rejecting ActorCellChange from " << c.name
                                << " for actor " << actor.refId
                                << " because destination cell is invalid: " << destinationCellId;
            continue;
        }

        if (!clientEligibleForActorCell(c, destinationCellId))
        {
            Log(Debug::Warning) << "[Server] Rejecting ActorCellChange from " << c.name
                                << " for actor " << actor.refId
                                << " because destination cell is not eligible: " << destinationCellId;
            continue;
        }

        normalizeActorIdentity(actor);
        actor.cellId = destinationCellId;
        if (isUnmanagedSpawnerActor(actor))
        {
            Log(Debug::Info) << "[Server] Rejecting unmanaged spawner ActorCellChange from " << c.name
                             << " refId=" << actor.refId
                             << " refNum=" << actor.refNum
                             << " from=" << incoming.cellId
                             << " to=" << destinationCellId;
            continue;
        }
        if (rejectStaleAliveVanillaActor(actor, destinationCellId, c, "ActorCellChange"))
            continue;

        const std::string actorKey = makeActorKey(actor);
        auto sourceActorIt = sourceCellState.actors.find(actorKey);
        if (sourceActorIt == sourceCellState.actors.end())
        {
            findTrackedActor(sourceCellState, actor, c, "ActorCellChange");
            continue;
        }

        constexpr uint64_t kExplicitReverseHandoffGuardMs = 2000;
        const ActorRegistryRecord& sourceRecord = sourceActorIt->second;
        const uint64_t canonicalSnapshotAge = sourceRecord.lastCellChangeTime != 0
            && incoming.serverTimestamp >= sourceRecord.lastCellChangeTime
            ? incoming.serverTimestamp - sourceRecord.lastCellChangeTime
            : std::numeric_limits<uint64_t>::max();
        if (destinationCellId == sourceRecord.previousCellId
            && canonicalSnapshotAge <= kExplicitReverseHandoffGuardMs)
        {
            Log(Debug::Info) << "[Server] Suppressed immediate reverse ActorCellChange"
                             << " from=" << c.name
                             << " refId=" << actor.refId
                             << " mpNum=" << actor.mpNum
                             << " canonical=" << incoming.cellId
                             << " previous=" << destinationCellId
                             << " ageMs=" << canonicalSnapshotAge;
            continue;
        }

        ActorRegistryRecord movedRecord = sourceActorIt->second;
        const bool wasDead = movedRecord.actor.isDead;
        ++movedRecord.migrationGeneration;
        if (movedRecord.migrationGeneration == 0)
            ++movedRecord.migrationGeneration; // Skip zero after overflow
        sourceCellState.actors.erase(sourceActorIt);
        forgetActorLocation(movedRecord.actor, incoming.cellId);
        if (mPlayerDb && movedRecord.actor.mpNum != 0)
            mPlayerDb->deleteSpawnedActorDynamicRecordLink(movedRecord.actor.mpNum, incoming.cellId);

        removeActorFromOtherCells(actor, destinationCellId, changedCellIds);

        movedRecord.actor = actor;
        movedRecord.actor.cellId = destinationCellId;
        movedRecord.actor.migrationGeneration = movedRecord.migrationGeneration;
        movedRecord.lastSnapshotTime = incoming.serverTimestamp;
        movedRecord.previousCellId = incoming.cellId;
        movedRecord.previousCellAuthorityGuid = sourceCellState.authorityGuid;
        movedRecord.lastCellChangeTime = incoming.serverTimestamp;

        auto& destinationCellState = mWorld.actorCells[destinationCellId];
        destinationCellState.actors[actorKey] = movedRecord;
        ActorRegistryRecord& storedMovedRecord = destinationCellState.actors[actorKey];
        updateActorAuthorityLeaseFromAi(destinationCellId, storedMovedRecord,
            storedMovedRecord.actor, incoming.serverTimestamp, "ActorCellChange");
        if (storedMovedRecord.actorAuthorityGuid != 0
            && isActorAuthorityLeaseValid(storedMovedRecord, destinationCellId, incoming.serverTimestamp))
        {
            broadcastActorAuthorityLease(destinationCellId, storedMovedRecord);
            Log(Debug::Info) << "[Server] Actor authority lease preserved across migration"
                             << " actorNetId=" << storedMovedRecord.actorNetId
                             << " refId=" << storedMovedRecord.actor.refId
                             << " mpNum=" << storedMovedRecord.actor.mpNum
                             << " from=" << incoming.cellId
                             << " to=" << destinationCellId
                             << " owner=" << storedMovedRecord.actorAuthorityGuid
                             << " reason=" << storedMovedRecord.actorAuthorityReason
                             << " generation=" << storedMovedRecord.actorAuthorityGeneration;
        }
        else if (storedMovedRecord.actorAuthorityGuid != 0)
        {
            const uint32_t previousOwner = storedMovedRecord.actorAuthorityGuid;
            storedMovedRecord.actorAuthorityGuid = 0;
            storedMovedRecord.actorAuthorityTargetGuid = 0;
            storedMovedRecord.actorAuthorityLeaseUntilMs = 0;
            storedMovedRecord.actorAuthorityReason.clear();
            ++storedMovedRecord.actorAuthorityGeneration;
            broadcastActorAuthorityLease(destinationCellId, storedMovedRecord);
            Log(Debug::Info) << "[Server] Actor authority lease expired"
                             << " source=ActorCellChange"
                             << " actorNetId=" << storedMovedRecord.actorNetId
                             << " refId=" << storedMovedRecord.actor.refId
                             << " mpNum=" << storedMovedRecord.actor.mpNum
                             << " cell=" << destinationCellId
                             << " previousOwner=" << previousOwner
                             << " generation=" << storedMovedRecord.actorAuthorityGeneration;
        }
        changedCellIds.insert(incoming.cellId);
        changedCellIds.insert(destinationCellId);
        rememberActorLocation(storedMovedRecord.actor, destinationCellId);
        rememberDeadVanillaActor(storedMovedRecord);
        persistSpawnedActorIfNeeded(storedMovedRecord);
        upsertSpawnedActorDynamicRecordLinkIfNeeded(storedMovedRecord.actor);
        markLuaActorDirty(storedMovedRecord, destinationCellId);

        if (storedMovedRecord.actor.mpNum != 0 && storedMovedRecord.actor.isDead && !wasDead)
            sendActorLifecycleEvent("death", storedMovedRecord.actor, storedMovedRecord.persistent);

        accepted.actors.push_back(storedMovedRecord.actor);
        destinationCellIds.insert(destinationCellId);
        storedAcceptedRecords.push_back(storedMovedRecord);

        Log(Debug::Info) << "[Server] ActorCellChange migrated actor"
                         << " refId=" << storedMovedRecord.actor.refId
                         << " mpNum=" << storedMovedRecord.actor.mpNum
                         << " from=" << incoming.cellId
                         << " to=" << destinationCellId;
    }

    if (accepted.actors.empty())
        return;

    PacketActorCellChange out;
    out.setActorList(&accepted);
    const std::vector<uint8_t> encoded = out.encode();

    // For each interested client that has not yet received this actor's
    // identity, send a targeted one-actor PacketActorIdentity before the
    // ActorCellChange. Without this, the client constructs a provisional
    // runtime but never receives position updates because broadcastActor-
    // PositionV2ToCell suppresses snapshots until identity is acknowledged.
    auto sendTargetedIdentityIfNeeded = [&](HSteamNetConnection conn, ConnectedClient& client,
        const ActorRegistryRecord& storedRecord)
    {
        ActorRegistryRecord canonicalRecord = storedRecord;
        ensureCanonicalActorMigrationGeneration(
            canonicalRecord.migrationGeneration, canonicalRecord.actor);
        const ActorInstanceId actorNetId = canonicalRecord.actorNetId;
        if (actorNetId == 0)
            return;
        if (client.actorV2IdentitySent.count(actorNetId) != 0
            || client.actorV2IdentityAcked.count(actorNetId) != 0)
            return;

        const std::string& actorCellId = canonicalRecord.actor.cellId;
        auto destCellIt = mWorld.actorCells.find(actorCellId);

        ActorIdentityList identityList;
        identityList.protocolVersion = ActorSyncProtocolVersionV2;
        identityList.cellId = actorCellId;
        identityList.authorityGuid = destCellIt != mWorld.actorCells.end()
            ? destCellIt->second.authorityGuid : 0;
        identityList.authorityGeneration = destCellIt != mWorld.actorCells.end()
            ? destCellIt->second.authorityGeneration : 0;
        identityList.sequence = destCellIt != mWorld.actorCells.end()
            ? destCellIt->second.nextSnapshotSequence++ : 1;
        identityList.serverTimestamp = currentServerTimeMs();
        identityList.completeCellSnapshot = false;

        ActorIdentityRecord identity;
        identity.actorNetId = actorNetId;
        identity.persistent = canonicalRecord.persistent;
        identity.serverSpawned = canonicalRecord.actor.mpNum != 0;
        identity.migrationGeneration = canonicalRecord.migrationGeneration;
        identity.actor = canonicalRecord.actor;
        identityList.actors.push_back(std::move(identity));

        PacketActorIdentity identityPkt;
        identityPkt.setIdentityList(&identityList);
        sendTo(conn, identityPkt.encode());
        client.actorV2IdentitySent.insert(actorNetId);
        ++client.actorV2IdentitySentWindow;

        Log(Debug::Verbose) << "[Server] Sent targeted identity before ActorCellChange"
                            << " actorNetId=" << actorNetId
                            << " cell=" << actorCellId
                            << " to=" << client.name;
    };

    for (auto& [conn, client] : mClients)
    {
        // Include the sender. The authoritative echo acknowledges that the
        // canonical migration was accepted and lets it stop driving the actor
        // when the destination cell belongs to another client.
        bool interested = clientHasActorCellLoaded(client, incoming.cellId);
        if (!interested)
        {
            for (const std::string& destinationCellId : destinationCellIds)
            {
                if (clientHasActorCellLoaded(client, destinationCellId))
                {
                    interested = true;
                    break;
                }
            }
        }

        if (!interested)
            continue;

        // Send targeted identity for each accepted actor that this client
        // has not yet acknowledged.
        for (const ActorRegistryRecord& storedRecord : storedAcceptedRecords)
            sendTargetedIdentityIfNeeded(conn, client, storedRecord);

        sendTo(conn, encoded);
    }

    for (const std::string& destinationCellId : destinationCellIds)
    {
        auto destinationCellIt = mWorld.actorCells.find(destinationCellId);
        if (destinationCellIt != mWorld.actorCells.end() && destinationCellIt->second.authorityGuid == 0)
            refreshActorAuthorityForCell(destinationCellId, c.guid);
    }

    // Do NOT broadcast full ActorList for changed cells after a routine
    // migration. The reliable ActorCellChange commit, targetted identity
    // sends, and compact position snapshots are sufficient for protocol-v8
    // clients. Full baselines are reserved for initial bootstrap, identity
    // sync, recovery, and explicit cell reset.
}

// ---------------------------------------------------------------------------
void MPServer::handleActorDeath(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorList incoming;
    PacketActorDeath pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    Log(Debug::Info) << "[Server] Received ActorDeath from " << c.name << " cellId=" << incoming.cellId;
    if (!validateActorUpdate(c, incoming, "ActorDeath")) return;
    Log(Debug::Info) << "[Server] ActorDeath from " << c.name << " cell=" << incoming.cellId << " actors=" << incoming.actors.size();

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());
    std::unordered_set<std::string> changedCellIds;

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectResetStaleDeadVanillaActor(actor, incoming.cellId, c, "ActorDeath"))
            continue;
        clearResetStaleDeathSuppressionForAliveVanillaActor(actor, incoming.cellId);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorDeath"))
            continue;

        Log(Debug::Info) << "[Server] ActorDeath candidate from " << c.name
                         << " refId=" << actor.refId
                         << " refNum=" << actor.refNum
                         << " mpNum=" << actor.mpNum
                         << " eventId=" << actor.deathEventId
                         << " isDead=" << actor.isDead
                         << " hp=" << actor.dynamicStats.health.current
                         << " deathAnim='" << actor.deathAnimGroup << "'";
        const std::string actorKeyForDeath = makeActorKey(actor);
        auto storedIt = cellState.actors.find(actorKeyForDeath);
        ActorRegistryRecord* stored = storedIt != cellState.actors.end() ? &storedIt->second : nullptr;
        if (!stored)
        {
            const std::string& actorKey = actorKeyForDeath;
            std::string sourceCellId;
            CellActorState* sourceCellState = nullptr;
            ActorRegistryRecord* sourceRecord = nullptr;

            const auto locationIt = mWorld.actorLocations.find(actorKey);
            if (locationIt != mWorld.actorLocations.end() && locationIt->second != incoming.cellId)
            {
                auto sourceCellIt = mWorld.actorCells.find(locationIt->second);
                if (sourceCellIt != mWorld.actorCells.end())
                {
                    auto sourceActorIt = sourceCellIt->second.actors.find(actorKey);
                    if (sourceActorIt != sourceCellIt->second.actors.end())
                    {
                        sourceCellId = sourceCellIt->first;
                        sourceCellState = &sourceCellIt->second;
                        sourceRecord = &sourceActorIt->second;
                    }
                }
            }

            if (!sourceRecord)
            {
                for (auto& [candidateCellId, candidateCellState] : mWorld.actorCells)
                {
                    if (candidateCellId == incoming.cellId)
                        continue;

                    auto sourceActorIt = candidateCellState.actors.find(actorKey);
                    if (sourceActorIt == candidateCellState.actors.end())
                        continue;

                    sourceCellId = candidateCellId;
                    sourceCellState = &candidateCellState;
                    sourceRecord = &sourceActorIt->second;
                    break;
                }
            }

            if (sourceRecord && sourceRecord->actor.isDead)
            {
                Log(Debug::Verbose) << "[Server] ActorDeath ignored cross-cell duplicate for already-dead actor"
                                    << " refId=" << actor.refId
                                    << " refNum=" << actor.refNum
                                    << " mpNum=" << actor.mpNum
                                    << " sourceCell=" << sourceCellId
                                    << " incomingCell=" << incoming.cellId;
                continue;
            }

            if (sourceRecord && sourceCellState)
            {
                ActorRegistryRecord movedRecord = *sourceRecord;
                sourceCellState->actors.erase(actorKey);
                forgetActorLocation(movedRecord.actor, sourceCellId);
                if (mPlayerDb && movedRecord.actor.mpNum != 0)
                {
                    mPlayerDb->deleteSpawnedActorDynamicRecordLink(movedRecord.actor.mpNum, sourceCellId);
                    scheduleGeneratedDynamicRecordGc("actor_death_migration_unlink");
                }
                changedCellIds.insert(sourceCellId);

                auto [movedIt, inserted] = cellState.actors.emplace(actorKey, movedRecord);
                if (!inserted)
                    movedIt->second = movedRecord;
                stored = &movedIt->second;

                Log(Debug::Info) << "[Server] ActorDeath moved tracked actor to death cell"
                                 << " refId=" << actor.refId
                                 << " refNum=" << actor.refNum
                                 << " mpNum=" << actor.mpNum
                                 << " from=" << sourceCellId
                                 << " to=" << incoming.cellId;
            }
        }
        if (!stored)
        {
            Log(Debug::Warning) << "[Server] ActorDeath dropped for untracked actor"
                                << " refId=" << actor.refId
                                << " refNum=" << actor.refNum
                                << " mpNum=" << actor.mpNum
                                << " cell=" << incoming.cellId;
            continue;
        }
        const bool wasDead = stored->actor.isDead;
        Log(Debug::Info) << "[Server] ActorDeath matched tracked actor"
                         << " refId=" << stored->actor.refId
                         << " refNum=" << stored->actor.refNum
                         << " mpNum=" << stored->actor.mpNum
                         << " persistent=" << stored->persistent
                         << " wasDead=" << wasDead
                         << " lastDeathEventId=" << stored->lastDeathEventId
                         << " prevHp=" << stored->actor.dynamicStats.health.current;
        if (actor.deathEventId != 0
            && stored->lastDeathEventId != 0
            && !isNewerEventId(actor.deathEventId, stored->lastDeathEventId))
        {
            Log(Debug::Verbose) << "[Server] ActorDeath ignored duplicate event"
                                << " refId=" << stored->actor.refId
                                << " refNum=" << stored->actor.refNum
                                << " mpNum=" << stored->actor.mpNum
                                << " eventId=" << actor.deathEventId
                                << " lastDeathEventId=" << stored->lastDeathEventId
                                << " cell=" << incoming.cellId;
            continue;
        }

        const bool alreadyRememberedDeadVanilla = actor.mpNum == 0 && findDeadVanillaActor(actor) != nullptr;
        const bool duplicateAlreadyDead = wasDead
            && actor.isDead
            && (alreadyRememberedDeadVanilla
                || (actor.mpNum != 0 && !stored->actor.deathAnimGroup.empty()));
        if (duplicateAlreadyDead)
        {
            Log(Debug::Verbose) << "[Server] ActorDeath ignored duplicate already-dead actor"
                                << " refId=" << stored->actor.refId
                                << " refNum=" << stored->actor.refNum
                                << " mpNum=" << stored->actor.mpNum
                                << " cell=" << incoming.cellId;
            continue;
        }
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.position = actor.position;
        stored->actor.velocity = Velocity {};
        stored->actor.deathEventId = actor.deathEventId;
        stored->actor.deathCauseCombatEventId = actor.deathCauseCombatEventId;
        stored->actor.deathState = actor.deathState;
        stored->actor.isDead = actor.isDead;
        stored->actor.isInstantDeath = actor.isInstantDeath;
        stored->actor.deathAnimGroup = actor.deathAnimGroup;
        stored->actor.dynamicStats.health.current = actor.dynamicStats.health.current;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        if (actor.deathEventId != 0)
            stored->lastDeathEventId = actor.deathEventId;

        if (actor.isDead && !wasDead && actor.deathCauseCombatEventId != 0 && mPlayerDb)
        {
            const std::optional<CombatEventRecord> cause
                = mPlayerDb->loadCombatEvent(actor.deathCauseCombatEventId);
            const bool attributable = cause && cause->accepted
                && (cause->resultFlags & CombatResultApplied) != 0
                && (cause->resultFlags & CombatVictimDied) != 0
                && cause->victimActorInstanceId == stored->actorNetId
                && cause->migrationGeneration == stored->migrationGeneration
                && cause->victimRefId == stored->actor.refId
                && (cause->qualifyingCrime || mPlayerDb->hasReportedCriminalAssault(
                    cause->characterId, stored->actorNetId, stored->migrationGeneration));
            if (attributable)
            {
                const std::string murderEventId = "combat:" + std::to_string(cause->eventId) + ":murder";
                const auto murder = mPlayerDb->loadSemanticRequest(
                    "crime-event", cause->accountId, cause->characterId, murderEventId);
                if (!murder)
                {
                    Log(Debug::Error) << "[ActorDeath] missing atomic Murder result eventId="
                                      << murderEventId << " victim=" << stored->actorNetId;
                }
                else
                {
                    Log(Debug::Info) << "[ActorDeath] confirmed atomic Murder eventId="
                                     << murderEventId << " victim=" << stored->actorNetId;
                }
            }
            else
            {
                Log(Debug::Warning) << "[ActorDeath] rejected combat attribution deathEventId="
                                    << actor.deathEventId << " cause=" << actor.deathCauseCombatEventId
                                    << " victim=" << stored->actorNetId;
            }
        }
        rememberActorLocation(stored->actor, incoming.cellId);
        persistSpawnedActorIfNeeded(*stored);
        upsertSpawnedActorDynamicRecordLinkIfNeeded(stored->actor);
        markLuaActorDirty(*stored, incoming.cellId);
        if (stored->actor.mpNum == 0 && stored->actor.isDead)
        {
            Log(Debug::Info) << "[Server] ActorDeath stored vanilla corpse transform"
                             << " refId=" << stored->actor.refId
                             << " refNum=" << stored->actor.refNum
                             << " eventId=" << stored->actor.deathEventId
                             << " cell=" << incoming.cellId
                             << " pos=(" << stored->actor.position.pos[0]
                             << "," << stored->actor.position.pos[1]
                             << "," << stored->actor.position.pos[2] << ")"
                             << " deathAnim='" << stored->actor.deathAnimGroup << "'";
            rememberDeadVanillaActor(*stored);
        }
        else if (wasDead)
            forgetDeadVanillaActor(stored->actor, incoming.cellId);
        if (actor.isDead && !wasDead)
            sendActorLifecycleEvent("death", stored->actor, stored->persistent);
        filtered.actors.push_back(actor);
    }

    auto broadcastChangedDeathCells = [&]()
    {
        for (const std::string& changedCellId : changedCellIds)
        {
            auto changedCellIt = mWorld.actorCells.find(changedCellId);
            if (changedCellIt != mWorld.actorCells.end())
                broadcastActorListForCell(changedCellId, changedCellIt->second);
        }
    };

    if (filtered.actors.empty())
    {
        broadcastChangedDeathCells();
        return;
    }

    PacketActorDeath out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn);
    Log(Debug::Info) << "[Server] Broadcast ActorDeath to cell=" << filtered.cellId;
    broadcastChangedDeathCells();
}

// ---------------------------------------------------------------------------
void MPServer::handleActorEquipment(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorList incoming;
    PacketActorEquipment pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorEquipment")) return;

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());

    for (auto& actor : incoming.actors)
    {
        const bool hasUnknownGeneratedEquipment
            = std::any_of(actor.equipment.begin(), actor.equipment.end(), [&](const EquipmentItem& entry) {
                  return !entry.item.refId.empty() && !isAuthoritativeRecordReference(entry.item.refId);
              });
        if (hasUnknownGeneratedEquipment)
        {
            Log(Debug::Warning) << "[Server] Rejected ActorEquipment unknown generated record from=" << c.name
                                << " actor=" << actor.refId;
            continue;
        }
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorEquipment"))
            continue;

        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorEquipment");
        if (!stored)
            continue;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.equipment = actor.equipment;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        persistSpawnedActorIfNeeded(*stored);
        markLuaActorDirty(*stored, incoming.cellId);
        filtered.actors.push_back(actor);
    }

    if (filtered.actors.empty())
        return;

    PacketActorEquipment out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handleActorStatsDynamic(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorList incoming;
    PacketActorStatsDynamic pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorStatsDynamic")) return;

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());

    std::size_t deadSpawnedSuppressed = 0;
    std::string firstDeadSpawnedRefId;
    uint32_t firstDeadSpawnedMpNum = 0;

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectResetStaleDeadVanillaActor(actor, incoming.cellId, c, "ActorStatsDynamic"))
            continue;
        clearResetStaleDeathSuppressionForAliveVanillaActor(actor, incoming.cellId);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorStatsDynamic"))
            continue;

        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorStatsDynamic");
        if (!stored)
            continue;
        if (stored->actor.mpNum != 0 && stored->actor.isDead && !actor.isDead)
        {
            ++deadSpawnedSuppressed;
            if (firstDeadSpawnedRefId.empty())
            {
                firstDeadSpawnedRefId = stored->actor.refId;
                firstDeadSpawnedMpNum = stored->actor.mpNum;
            }
            continue;
        }
        const bool wasDead = stored->actor.isDead;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;
        stored->actor.dynamicStats = actor.dynamicStats;
        stored->actor.isDead = actor.isDead;
        stored->lastSnapshotTime = incoming.serverTimestamp;
        persistSpawnedActorIfNeeded(*stored);
        markLuaActorDirty(*stored, incoming.cellId);
        if (wasDead && !stored->actor.isDead)
            forgetDeadVanillaActor(stored->actor, incoming.cellId);
        if (actor.isDead && !wasDead)
            sendActorLifecycleEvent("death", stored->actor, stored->persistent);
        filtered.actors.push_back(actor);
    }

    if (deadSpawnedSuppressed != 0)
    {
        Log(Debug::Verbose) << "[Server] ActorStatsDynamic suppressed live update(s) for dead spawned actor"
                            << " from=" << c.name
                            << " cell=" << incoming.cellId
                            << " suppressed=" << deadSpawnedSuppressed
                            << " firstRefId=" << firstDeadSpawnedRefId
                            << " firstMpNum=" << firstDeadSpawnedMpNum;
    }

    if (filtered.actors.empty())
        return;

    PacketActorStatsDynamic out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handleActorAI(ConnectedClient& c, const uint8_t* data, size_t size)
{
    ActorList incoming;
    PacketActorAI pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;
    if (!validateActorUpdate(c, incoming, "ActorAI")) return;

    auto& cellState = mWorld.actorCells[incoming.cellId];
    incoming.isAuthority = true;
    incoming.authorityGuid = cellState.authorityGuid;
    incoming.authorityGeneration = cellState.authorityGeneration;
    incoming.snapshotSequence = cellState.nextSnapshotSequence++;
    incoming.serverTimestamp = currentServerTimeMs();

    ActorList filtered = incoming;
    filtered.actors.clear();
    filtered.actors.reserve(incoming.actors.size());
    std::unordered_set<std::int64_t> crimePursuitReissues;

    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
        if (rejectStaleAliveVanillaActor(actor, incoming.cellId, c, "ActorAI"))
            continue;

        ActorRegistryRecord* stored = findTrackedActor(cellState, actor, c, "ActorAI");
        if (!stored)
            continue;
        stored->actor.refId = actor.refId;
        stored->actor.refNum = actor.refNum;
        stored->actor.mpNum = actor.mpNum;
        stored->actor.cellId = incoming.cellId;

        if (stored->crimePursuitCharacterId > 0)
        {
            constexpr std::uint64_t CrimePursuitReassertCooldownMs = 2000;
            const std::string expectedTargetId = stored->crimePursuitLastGuid != 0
                ? std::string("mp_remote_") + std::to_string(stored->crimePursuitLastGuid) : std::string();
            const bool incomingCombatForOffender = actor.ai.type == BaseActor::AIAction::Type::Combat
                && !expectedTargetId.empty() && actor.ai.targetId == expectedTargetId;
            const bool incomingTerminalNeutral = actor.ai.type != BaseActor::AIAction::Type::Pursue
                && actor.ai.type != BaseActor::AIAction::Type::Combat
                && actor.ai.type != BaseActor::AIAction::Type::Travel;

            if (incomingCombatForOffender)
                stored->crimePursuitReassertArmed = true;
            else if (incomingTerminalNeutral && stored->crimePursuitReassertArmed)
            {
                stored->crimePursuitReassertArmed = false;
                const bool cooldownElapsed = incoming.serverTimestamp >= stored->crimePursuitLastReassertMs
                    && incoming.serverTimestamp - stored->crimePursuitLastReassertMs >= CrimePursuitReassertCooldownMs;
                if (cooldownElapsed)
                {
                    crimePursuitReissues.insert(stored->crimePursuitCharacterId);
                    Log(Debug::Info) << "[CrimeReaction] guard abandoned confirmed combat; scheduling pursuit reissue"
                                     << " cell=" << incoming.cellId
                                     << " actorNetId=" << stored->actorNetId
                                     << " refId=" << stored->actor.refId
                                     << " incomingType=" << static_cast<unsigned>(actor.ai.type)
                                     << " offenderCharacterId=" << stored->crimePursuitCharacterId;
                }
            }
        }

        stored->actor.ai = actor.ai;
        stored->lastSnapshotTime = incoming.serverTimestamp;

        updateActorAuthorityLeaseFromAi(
            incoming.cellId, *stored, stored->actor, incoming.serverTimestamp, "ActorAI");
        persistSpawnedActorIfNeeded(*stored);
        markLuaActorDirty(*stored, incoming.cellId);
        filtered.actors.push_back(actor);
    }

    if (filtered.actors.empty())
        return;

    PacketActorAI out;
    out.setActorList(&filtered);
    broadcastActorToCell(filtered.cellId, out.encode(), c.conn);

    for (const std::int64_t characterId : crimePursuitReissues)
        dispatchOutstandingCrimePursuitsForCell(filtered.cellId, characterId);
}

// ---------------------------------------------------------------------------
void MPServer::handleActorCombatRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    // Decode the incoming request.
    ActorList incoming;
    PacketActorCombatRequest pkt;
    pkt.setActorList(&incoming);
    if (!pkt.decode(data, size)) return;

    // Tag with the sending client's guid before any routing decisions.
    incoming.authorityGuid = c.guid;
    for (auto& actor : incoming.actors)
    {
        actor.cellId = incoming.cellId;
        normalizeActorIdentity(actor);
    }

    PacketActorCombatRequest out;
    out.setActorList(&incoming);

    if (incoming.cellId.empty() || !clientHasActorCellLoaded(c, incoming.cellId))
    {
        Log(Debug::Verbose) << "[Server] Rejecting ActorCombatRequest from " << c.name
                            << " because the actor cell is not loaded: player=" << makeCellKey(c.player.cell)
                            << " packet=" << incoming.cellId;
        return;
    }

    auto cellIt = mWorld.actorCells.find(incoming.cellId);
    if (cellIt == mWorld.actorCells.end()) return;

    const uint32_t cellAuthorityGuid = cellIt->second.authorityGuid;

    // NPC->player damage: routed by the cell authority to the victim player.
    // This must be checked BEFORE the authority guard because the sender IS
    // the authority in this case (authority forwards NPC hits to the victim).
    if (incoming.victimPlayerGuid != 0)
    {
        bool senderOwnsEveryAttacker = !incoming.actors.empty();
        for (const BaseActor& actor : incoming.actors)
        {
            const auto recordIt = cellIt->second.actors.find(makeActorKey(actor));
            if (recordIt == cellIt->second.actors.end()
                || !isAllowedActorSender(c, recordIt->second, incoming.cellId))
            {
                senderOwnsEveryAttacker = false;
                break;
            }
        }
        if (!senderOwnsEveryAttacker)
        {
            Log(Debug::Warning) << "[Server] Rejected NpcPlayerDamage from non-owner"
                                << " sender=" << c.guid
                                << " cell=" << incoming.cellId
                                << " cellAuthority=" << cellAuthorityGuid;
            return;
        }

        for (auto& [conn, client] : mClients)
        {
            if (client.guid == incoming.victimPlayerGuid)
            {
                sendTo(conn, out.encode(), true);
                Log(Debug::Info) << "[Server] Routed NpcPlayerDamage from guid=" << c.guid
                                 << " to victim guid=" << incoming.victimPlayerGuid;
                break;
            }
        }
        return;
    }

    // Player->NPC combat is a server-issued single-victim transaction. The
    // proposal is not a crime result: only the independently entitled actor
    // authority can later confirm that it applied this exact pending event.
    if (incoming.actors.size() != 1 || !mPlayerDb || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
        return;
    BaseActor& actor = incoming.actors.front();
    const auto recordIt = cellIt->second.actors.find(makeActorKey(actor));
    if (recordIt == cellIt->second.actors.end())
        return;
    ActorRegistryRecord& record = recordIt->second;
    const ActorInstanceId actorNetId = ensureActorNetId(record, incoming.cellId);
    std::uint32_t actorAuthorityGuid = cellAuthorityGuid;
    std::uint32_t authorityGeneration = cellIt->second.authorityGeneration;
    if (isActorAuthorityLeaseValid(record, incoming.cellId))
    {
        actorAuthorityGuid = record.actorAuthorityGuid;
        authorityGeneration = record.actorAuthorityGeneration;
    }
    if (actorAuthorityGuid == 0 || actorAuthorityGuid == c.guid)
    {
        Log(Debug::Warning) << "[CombatProposal] rejected authority-conflict attacker=" << c.guid
                            << " victim=" << actorNetId << " authority=" << actorAuthorityGuid;
        return;
    }
    if (!std::isfinite(actor.attack.damage) || actor.attack.damage <= 0.f
        || actor.attack.damage > MaximumMechanicsValueMagnitude || actor.attack.targetKind != Attack::TargetActor)
        return;

    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    for (auto it = mPendingCombatPresentations.begin(); it != mPendingCombatPresentations.end();)
    {
        if (nowMs >= it->second.createdAtMs
            && nowMs - it->second.createdAtMs > MaximumCombatProposalAgeMs * 2)
            it = mPendingCombatPresentations.erase(it);
        else
            ++it;
    }
    const AcceptedMechanicsSnapshot* playerMechanics = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!playerMechanics || playerMechanics->snapshot.cellId != incoming.cellId)
        return;
    const float dx = playerMechanics->snapshot.position.pos[0] - record.actor.position.pos[0];
    const float dy = playerMechanics->snapshot.position.pos[1] - record.actor.position.pos[1];
    const float dz = playerMechanics->snapshot.position.pos[2] - record.actor.position.pos[2];
    const float rangeSquared = dx * dx + dy * dy + dz * dz;
    constexpr float MaximumCombatProposalRange = 1024.f;
    if (!std::isfinite(rangeSquared)
        || rangeSquared > MaximumCombatProposalRange * MaximumCombatProposalRange)
        return;

    CombatEventRecord event;
    event.accountId = c.dbAccountId;
    event.characterId = c.dbCharacterId;
    event.attackerGuid = c.guid;
    event.victimActorInstanceId = actorNetId;
    event.victimRefId = record.actor.refId;
    event.cellId = incoming.cellId;
    event.migrationGeneration = record.migrationGeneration;
    event.authorityGeneration = authorityGeneration;
    event.actorAuthorityGuid = actorAuthorityGuid;
    event.proposedDamage = actor.attack.damage;
    event.proposedHealthDamage = actor.attack.healthDamage;
    event.proposalHash = crypto::sha256hex(std::string(
        reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + size));
    event.createdAtMs = nowMs;
    try
    {
        incoming.combatEventId = mPlayerDb->createCombatEvent(event);
        mPendingCombatPresentations[incoming.combatEventId] = { actor.attack, nowMs };
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[CombatProposal] persistence failure: " << e.what();
        return;
    }
    incoming.combatVictimActorInstanceId = actorNetId;
    incoming.combatVictimMigrationGeneration = record.migrationGeneration;
    incoming.combatVictimAuthorityGeneration = authorityGeneration;

    PacketActorCombatRequest routedPacket;
    routedPacket.setActorList(&incoming);
    for (auto& [conn, client] : mClients)
    {
        if (client.guid != actorAuthorityGuid)
            continue;
        sendTo(conn, routedPacket.encode(), true);
        Log(Debug::Info) << "[CombatProposal] eventId=" << incoming.combatEventId
                         << " attacker=" << c.guid << " victim=" << actorNetId
                         << " authority=" << actorAuthorityGuid
                         << " migration=" << record.migrationGeneration
                         << " authorityGeneration=" << authorityGeneration;
        break;
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleActorCombatResult(ConnectedClient& c, const uint8_t* data, size_t size)
{
    if (!mPlayerDb || !mContentRegistry || !mObservationService)
        return;
    ActorList result;
    PacketActorCombatResult packet;
    packet.setActorList(&result);
    if (!packet.decode(data, size) || result.actors.size() != 1)
        return;

    const std::optional<CombatEventRecord> event = mPlayerDb->loadCombatEvent(result.combatEventId);
    if (!event)
    {
        Log(Debug::Warning) << "[CombatResult] rejected unknown eventId=" << result.combatEventId;
        return;
    }
    const BaseActor& victim = result.actors.front();
    const std::uint64_t nowMs = currentServerTimeMs();
    if (event->attackerGuid == c.guid || event->actorAuthorityGuid != c.guid
        || event->victimActorInstanceId != result.combatVictimActorInstanceId
        || event->cellId != result.cellId || event->migrationGeneration != result.combatVictimMigrationGeneration
        || event->authorityGeneration != result.combatVictimAuthorityGeneration
        || event->victimRefId != victim.refId)
    {
        Log(Debug::Warning) << "[CombatResult] rejected binding mismatch eventId=" << result.combatEventId
                            << " sender=" << c.guid;
        return;
    }
    const bool applied = (result.combatResultFlags & CombatResultApplied) != 0;
    if (applied && result.combatAppliedDamage != event->proposedDamage)
        return;
    if (!applied && result.combatAppliedDamage != 0.f)
        return;
    if (event->accepted)
    {
        const CombatEventCommitStatus replay = mPlayerDb->acceptCombatEvent(result.combatEventId,
            result.combatResultSequence, result.combatResultFlags, result.combatAppliedDamage,
            event->qualifyingCrime, {}, event->assaultReported);
        if (replay == CombatEventCommitStatus::ConflictingReplay)
            Log(Debug::Warning) << "[CombatResult] rejected conflicting replay eventId="
                                << result.combatEventId;
        return;
    }
    if (nowMs < event->createdAtMs || nowMs - event->createdAtMs > MaximumCombatProposalAgeMs)
    {
        mPendingCombatPresentations.erase(result.combatEventId);
        return;
    }

    const auto keyIt = mWorld.actorKeysByNetId.find(event->victimActorInstanceId);
    const auto locationIt = keyIt == mWorld.actorKeysByNetId.end()
        ? mWorld.actorLocations.end() : mWorld.actorLocations.find(keyIt->second);
    if (locationIt == mWorld.actorLocations.end() || locationIt->second != event->cellId)
        return;
    auto cellIt = mWorld.actorCells.find(locationIt->second);
    if (cellIt == mWorld.actorCells.end())
        return;
    auto actorIt = cellIt->second.actors.find(keyIt->second);
    if (actorIt == cellIt->second.actors.end())
        return;
    ActorRegistryRecord& actorRecord = actorIt->second;
    const bool leased = isActorAuthorityLeaseValid(actorRecord, event->cellId, nowMs);
    const std::uint32_t currentAuthority = leased ? actorRecord.actorAuthorityGuid : cellIt->second.authorityGuid;
    const std::uint32_t currentGeneration
        = leased ? actorRecord.actorAuthorityGeneration : cellIt->second.authorityGeneration;
    if (currentAuthority != c.guid || currentGeneration != event->authorityGeneration
        || actorRecord.migrationGeneration != event->migrationGeneration
        || !isAllowedActorSender(c, actorRecord, event->cellId))
        return;

    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const AcceptedMechanicsSnapshot* victimSnapshot = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Npc, 0, event->victimActorInstanceId }, nowMs, MaximumSnapshotAgeMs);
    if (!victimSnapshot || victimSnapshot->snapshot.cellId != event->cellId
        || victimSnapshot->snapshot.migrationGeneration != event->migrationGeneration
        || victimSnapshot->snapshot.authorityGeneration != event->authorityGeneration)
    {
        Log(Debug::Warning) << "[CombatResult] rejected without fresh victim mechanics eventId="
                            << event->eventId;
        return;
    }

    const bool victimIsNpc = mContentRegistry->store().get<ESM::NPC>().search(
        ESM::RefId::stringRefId(actorRecord.actor.refId)) != nullptr;
    const bool qualifyingCrime = victimIsNpc && isQualifyingCriminalAttack(result.combatResultFlags);
    const bool victimDied = (result.combatResultFlags & CombatVictimDied) != 0;
    const bool priorReportedAssault = victimIsNpc && victimDied
        && mPlayerDb->hasReportedCriminalAssault(
            event->characterId, event->victimActorInstanceId, event->migrationGeneration);
    const bool murderAttributable = victimIsNpc && victimDied && (qualifyingCrime || priorReportedAssault);

    ConnectedClient* attacker = nullptr;
    const AcceptedMechanicsSnapshot* offender = nullptr;
    if (qualifyingCrime || murderAttributable)
    {
        for (auto& [connection, candidate] : mClients)
        {
            if (candidate.guid == event->attackerGuid && candidate.dbAccountId == event->accountId
                && candidate.dbCharacterId == event->characterId)
            {
                attacker = &candidate;
                break;
            }
        }
        offender = attacker ? mMechanicsSnapshots.findFresh(
            { MechanicsSubjectKind::Player, attacker->guid, 0 }, nowMs, MaximumSnapshotAgeMs) : nullptr;
        if (!attacker || !offender || offender->snapshot.cellId != event->cellId)
            return;
    }

    const auto lastSequenceIt = mCombatResultSequencesByAuthority.find(c.guid);
    if (lastSequenceIt != mCombatResultSequencesByAuthority.end() && lastSequenceIt->second != 0
        && result.combatResultSequence <= lastSequenceIt->second)
        return;

    std::vector<CrimeMutationCommit> crimeMutations;
    std::optional<CrimeSemanticService::Outcome> assaultOutcome;
    std::optional<CrimeSemanticService::Outcome> murderOutcome;
    bool assaultReported = false;
    if (qualifyingCrime || murderAttributable)
    {
        ObservationActorIdentity victimIdentity;
        victimIdentity.kind = ObservationActorKind::Npc;
        victimIdentity.actorInstanceId = event->victimActorInstanceId;
        CrimeWitnessBuildRequest witnessRequest;
        witnessRequest.eventCell = attacker->player.cell;
        witnessRequest.offender = makeLiveObservationSnapshot(*offender, playerBootWeight(*attacker));
        witnessRequest.victim = victimIdentity;
        witnessRequest.alarmRadius = mObservationAlarmRadius;
        witnessRequest.observedAtMs = nowMs;
        witnessRequest.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
        CrimeWitnessBuildResult witnesses = buildLiveCrimeWitnesses(witnessRequest);

        const auto gmstInt = [&](std::string_view id) {
            return mContentRegistry->store().get<ESM::GameSetting>().find(id)->mValue.getInteger();
        };
        CrimePolicy policy;
        policy.alarmRadius = mObservationAlarmRadius;
        policy.theftBountyMultiplier = mContentRegistry->store().get<ESM::GameSetting>()
            .find("fCrimeStealing")->mValue.getFloat();
        policy.pickpocketBounty = gmstInt("iCrimePickPocket");
        policy.trespassBounty = gmstInt("iCrimeTresspass");
        policy.assaultBounty = gmstInt("iCrimeAttack");
        policy.murderBounty = gmstInt("iCrimeKilling");
        CrimeService crime(*mPlayerDb);
        CrimeSemanticService semantics(*mPlayerDb, crime, *mObservationService, policy);

        std::optional<PlayerCrimeState> nextCrimeState;
        if (qualifyingCrime)
        {
            CrimeIntent intent;
            intent.eventId = "combat:" + std::to_string(event->eventId) + ":assault";
            intent.source = "validated_combat_result";
            intent.type = CrimeType::Assault;
            intent.cellId = event->cellId;
            intent.offender = witnessRequest.offender;
            intent.victim = victimIdentity;
            intent.victimAware = true;
            intent.observedAtMs = nowMs;
            intent.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
            for (const std::string& cellId : witnesses.candidateCellIds)
            {
                const std::uint64_t generation = mCollisionWorld ? mCollisionWorld->cellGeneration(cellId) : 0;
                if (generation != 0)
                    intent.collisionGenerations.push_back({ cellId, generation });
            }
            CrimeSemanticService::Context context { event->accountId, event->characterId, event->attackerGuid };
            context.deferCommit = true;
            assaultOutcome = semantics.evaluate(intent, witnesses.witnesses, context);
            if (!assaultOutcome->result.accepted
                || (!assaultOutcome->replayed && !assaultOutcome->pendingCommit))
                return;
            assaultReported = assaultOutcome->result.reportingStageRun;
            if (assaultOutcome->pendingCommit)
            {
                crimeMutations.push_back(*assaultOutcome->pendingCommit);
                nextCrimeState = assaultOutcome->result.state;
            }
        }

        if (murderAttributable)
        {
            CrimeIntent intent;
            intent.eventId = "combat:" + std::to_string(event->eventId) + ":murder";
            intent.source = "validated_combat_death";
            intent.type = CrimeType::Murder;
            intent.cellId = event->cellId;
            intent.offender = witnessRequest.offender;
            intent.victim = victimIdentity;
            intent.observedAtMs = nowMs;
            intent.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
            for (const std::string& cellId : witnesses.candidateCellIds)
            {
                const std::uint64_t generation = mCollisionWorld ? mCollisionWorld->cellGeneration(cellId) : 0;
                if (generation != 0)
                    intent.collisionGenerations.push_back({ cellId, generation });
            }
            CrimeSemanticService::Context context { event->accountId, event->characterId, event->attackerGuid };
            context.deferCommit = true;
            if (nextCrimeState)
                context.startingState = *nextCrimeState;
            murderOutcome = semantics.evaluate(intent, witnesses.witnesses, context);
            if (!murderOutcome->result.accepted
                || (!murderOutcome->replayed && !murderOutcome->pendingCommit))
                return;
            if (murderOutcome->pendingCommit)
                crimeMutations.push_back(*murderOutcome->pendingCommit);
        }
    }

    const CombatEventCommitStatus committed = mPlayerDb->acceptCombatEvent(result.combatEventId,
        result.combatResultSequence, result.combatResultFlags, result.combatAppliedDamage,
        qualifyingCrime, crimeMutations, assaultReported);
    if (committed == CombatEventCommitStatus::UnknownEvent
        || committed == CombatEventCommitStatus::ConflictingReplay
        || committed == CombatEventCommitStatus::CrimeDuplicateConflict)
        return;
    if (committed == CombatEventCommitStatus::StaleCrimeRevision)
    {
        Log(Debug::Warning) << "[CombatResult] crime revision changed before atomic commit eventId="
                            << event->eventId;
        return;
    }
    if (committed == CombatEventCommitStatus::IdenticalReplay)
        return;

    mCombatResultSequencesByAuthority[c.guid] = result.combatResultSequence;
    if (attacker && (assaultOutcome || murderOutcome))
    {
        attacker->player.crimeState = mPlayerDb->loadPlayerCrimeState(attacker->dbCharacterId);
        attacker->player.bounty = attacker->player.crimeState.bounty;
        sendAuthoritativeCrimeState(*attacker);
        if (assaultOutcome)
            dispatchCrimeReactions(*attacker, assaultOutcome->result);
        if (murderOutcome)
            dispatchCrimeReactions(*attacker, murderOutcome->result);
    }

    if (const auto presentationIt = mPendingCombatPresentations.find(result.combatEventId);
        presentationIt != mPendingCombatPresentations.end())
    {
        result.isAuthority = true;
        result.actors.front().attack = presentationIt->second.attack;
        PacketActorCombatResult presentationPacket;
        presentationPacket.setActorList(&result);
        const std::vector<std::uint8_t> encodedPresentation = presentationPacket.encode();
        std::size_t presentationRecipients = 0;
        for (auto& [conn, client] : mClients)
        {
            // The attacking client already rendered its own local hit. Everyone
            // else, including the independently authoritative victim client,
            // receives the server-accepted presentation exactly once.
            if (client.guid == event->attackerGuid || !clientHasActorCellLoaded(client, event->cellId))
                continue;
            sendTo(conn, encodedPresentation, true);
            ++presentationRecipients;
        }
        Log(Debug::Info) << "[CombatPresentation] eventId=" << event->eventId
                         << " victim=" << event->victimActorInstanceId
                         << " recipients=" << presentationRecipients
                         << " hitPos=(" << result.actors.front().attack.hitPos[0] << ","
                         << result.actors.front().attack.hitPos[1] << ","
                         << result.actors.front().attack.hitPos[2] << ")";
        mPendingCombatPresentations.erase(presentationIt);
    }

    Log(Debug::Info) << "[CombatResultAccepted] eventId=" << event->eventId
                     << " attacker=" << event->attackerGuid
                     << " victim=" << event->victimActorInstanceId
                     << " authority=" << c.guid
                     << " assaultReported=" << assaultReported
                     << " murder=" << murderAttributable;
}

// ---------------------------------------------------------------------------
bool MPServer::disposeCorpseAuthoritative(
    const ActorRegistryRecord& record,
    const std::string& canonicalCellId,
    uint32_t requestingGuid)
{
    // Copy the entire record before any map mutations so references into
    // erased map elements are never accessed.
    const ActorRegistryRecord removedRecord = record;
    const BaseActor actor = removedRecord.actor;
    const bool wasPersistent = removedRecord.persistent;

    if (removedRecord.actorNetId == 0)
    {
        Log(Debug::Verbose) << "[Server] disposeCorpseAuthoritative: actor missing actorNetId"
                            << " refId=" << actor.refId
                            << " mpNum=" << actor.mpNum
                            << " cell=" << canonicalCellId;
        return false;
    }

    std::vector<ActorRegistryRecord> removedRecords = { removedRecord };

    // Remove the actor from its canonical actor cell.
    auto cellIt = mWorld.actorCells.find(canonicalCellId);
    if (cellIt != mWorld.actorCells.end())
    {
        const std::string actorKey = makeActorKey(actor);
        cellIt->second.actors.erase(actorKey);
        cellIt->second.resetSuppressedVanillaDeaths.erase(actorKey);
        cellIt->second.staleLiveVanillaDeathResendMs.erase(actorKey);
    }

    forgetActorLocation(actor, canonicalCellId);

    if (actor.mpNum == 0)
    {
        forgetDeadVanillaActor(actor, canonicalCellId);
        rememberDisposedVanillaActor(actor);
    }
    else
    {
        if (mPlayerDb)
            mPlayerDb->deleteSpawnedActorDynamicRecordLink(actor.mpNum, canonicalCellId);
        if (actor.mpNum != 0 && wasPersistent && mPlayerDb)
            deletePersistedSpawnedActor(actor.mpNum);
    }

    // Remove the authoritative container record.
    const std::string containerKey = makeContainerKey(canonicalCellId, actor.refId, actor.refNum, actor.mpNum);
    mWorld.containers.erase(containerKey);
    if (mPlayerDb)
        mPlayerDb->deleteContainerRecord(canonicalCellId, actor.refId, actor.refNum, actor.mpNum);

    markLuaActorRemoved(actor.mpNum);

    // Broadcast removal with CorpseDisposed reason.
    broadcastActorIdentityRemovalForCell(canonicalCellId, cellIt != mWorld.actorCells.end()
        ? cellIt->second : mWorld.actorCells[canonicalCellId], removedRecords, ActorRemovalReason::CorpseDisposed);

    // Forget identity mappings after constructing and broadcasting the removal.
    for (const ActorRegistryRecord& rec : removedRecords)
        forgetActorNetId(rec.actorNetId, rec.actor);

    Log(Debug::Info) << "[Server] Corpse disposed authoritatively"
                     << " actorNetId=" << removedRecord.actorNetId
                     << " refId=" << actor.refId
                     << " mpNum=" << actor.mpNum
                     << " cell=" << canonicalCellId
                     << " requester=" << requestingGuid;
    return true;
}

void MPServer::handleCorpseDispose(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketCorpseDispose pkt;
    if (!pkt.decode(data, size))
    {
        PacketHeader header;
        const bool hasHeader = BasePacket::peekHeader(data, size, header);
        Log(Debug::Warning) << "[Server] CorpseDispose decode failed"
                            << " client=" << c.name
                            << " bytes=" << size
                            << " headerValid=" << hasHeader
                            << " headerType=" << (hasHeader ? header.type : 0)
                            << " payloadSize=" << (hasHeader ? header.payloadSize : 0)
                            << " sequence=" << (hasHeader ? header.sequence : 0);
        return;
    }

    Log(Debug::Info) << "[Server] CorpseDispose request from " << c.name
                        << " actorNetId=" << pkt.actorNetId
                        << " mpNum=" << pkt.mpNum
                        << " refId=" << pkt.refId
                        << " refNum=" << pkt.refNum
                        << " cell=" << pkt.cellId;

    // 1. Resolve the authoritative actor record by actorNetId first, then mpNum, then refId/refNum.
    const ActorRegistryRecord* resolvedRecord = nullptr;
    std::optional<ActorRegistryRecord> ownedResolvedRecord;
    std::string canonicalCellId;

    if (pkt.actorNetId != 0)
    {
        for (auto& [cellId, cellState] : mWorld.actorCells)
        {
            for (auto& [actorKey, record] : cellState.actors)
            {
                if (record.actorNetId == pkt.actorNetId)
                {
                    resolvedRecord = &record;
                    canonicalCellId = cellId;
                    break;
                }
            }
            if (resolvedRecord) break;
        }
    }

    if (!resolvedRecord && pkt.mpNum != 0)
    {
        for (auto& [cellId, cellState] : mWorld.actorCells)
        {
            for (auto& [actorKey, record] : cellState.actors)
            {
                if (record.actor.mpNum == pkt.mpNum)
                {
                    resolvedRecord = &record;
                    canonicalCellId = cellId;
                    break;
                }
            }
            if (resolvedRecord) break;
        }
    }

    if (!resolvedRecord && !pkt.refId.empty() && pkt.refNum != 0)
    {
        for (auto& [cellId, cellState] : mWorld.actorCells)
        {
            for (auto& [actorKey, record] : cellState.actors)
            {
                if (record.actor.mpNum == 0 && record.actor.refId == pkt.refId
                    && record.actor.refNum == pkt.refNum)
                {
                    // If actorNetId was also provided, validate it matches.
                    if (pkt.actorNetId != 0 && record.actorNetId != pkt.actorNetId)
                        continue;
                    resolvedRecord = &record;
                    canonicalCellId = cellId;
                    break;
                }
            }
            if (resolvedRecord) break;
        }
    }

    if (!resolvedRecord && !pkt.refId.empty() && pkt.refNum != 0)
    {
        // Persisted vanilla corpses may exist in deadVanillaActorCells without a
        // matching actorCells entry. Resolve an owned copy, then continue through
        // the same identity, loaded-cell, container, and death validation below.
        BaseActor requested;
        requested.refId = pkt.refId;
        requested.refNum = pkt.refNum;
        requested.cellId = pkt.cellId;

        std::string deadCellId;
        if (const ActorRegistryRecord* deadRecord = findDeadVanillaActor(requested, &deadCellId))
        {
            ownedResolvedRecord = *deadRecord;
            canonicalCellId = deadCellId;
            ensureActorNetId(*ownedResolvedRecord, canonicalCellId);
            resolvedRecord = &*ownedResolvedRecord;
        }
    }

    if (!resolvedRecord)
    {
        Log(Debug::Verbose) << "[Server] CorpseDispose rejected: actor not found"
                            << " requester=" << c.name
                            << " actorNetId=" << pkt.actorNetId
                            << " mpNum=" << pkt.mpNum
                            << " refId=" << pkt.refId;
        return;
    }

    if (pkt.actorNetId != 0 && resolvedRecord->actorNetId != pkt.actorNetId)
    {
        Log(Debug::Verbose) << "[Server] CorpseDispose rejected: actorNetId mismatch"
                            << " requester=" << c.name
                            << " requested=" << pkt.actorNetId
                            << " resolved=" << resolvedRecord->actorNetId
                            << " refId=" << resolvedRecord->actor.refId;
        return;
    }

    // 2. Verify it is dead.
    if (!resolvedRecord->actor.isDead)
    {
        Log(Debug::Verbose) << "[Server] CorpseDispose rejected: actor is not dead"
                            << " requester=" << c.name
                            << " actorNetId=" << resolvedRecord->actorNetId
                            << " refId=" << resolvedRecord->actor.refId;
        return;
    }

    // 3. Verify the requester can access the corpse cell.
    if (!clientHasActorCellLoaded(c, canonicalCellId))
    {
        Log(Debug::Verbose) << "[Server] CorpseDispose rejected: requester " << c.name
                            << " does not have corpse cell loaded " << canonicalCellId;
        return;
    }

    // 4. Verify the authoritative corpse container is empty.
    const std::string containerKey = makeContainerKey(
        canonicalCellId, resolvedRecord->actor.refId, resolvedRecord->actor.refNum, resolvedRecord->actor.mpNum);
    auto containerIt = mWorld.containers.find(containerKey);
    if (containerIt != mWorld.containers.end() && !containerIt->second.items.empty())
    {
        Log(Debug::Verbose) << "[Server] CorpseDispose rejected: corpse container is not empty"
                            << " requester=" << c.name
                            << " actorNetId=" << resolvedRecord->actorNetId
                            << " refId=" << resolvedRecord->actor.refId;
        return;
    }

    // 5. Perform authoritative disposal.
    if (!disposeCorpseAuthoritative(*resolvedRecord, canonicalCellId, c.guid))
    {
        Log(Debug::Warning) << "[Server] CorpseDispose failed: disposal returned false"
                            << " requester=" << c.name
                            << " actorNetId=" << resolvedRecord->actorNetId;
    }
}

// ---------------------------------------------------------------------------
std::vector<uint8_t> MPServer::buildWorldWeatherPacket() const
{
    PacketWorldWeather pkt;
    pkt.currentWeather   = mWorld.weatherCurrent;
    pkt.nextWeather      = mWorld.weatherNext;
    pkt.transitionFactor = mWorld.weatherTransition;
    pkt.regionName       = mWorld.weatherRegion;
    return pkt.encode();
}

// ---------------------------------------------------------------------------
void MPServer::handleWeather(ConnectedClient& c, const uint8_t* data, size_t size)
{
    // Only the host is trusted to report weather.
    // Ignore packets from any other client - they should not be sending these.
    if (c.guid != mWorld.hostGuid)
    {
        Log(Debug::Verbose) << "[Server] Ignoring weather from non-host " << c.name;
        return;
    }

    PacketWorldWeather pkt;
    if (!pkt.decode(data, size)) return;

    mWorld.weatherCurrent    = pkt.currentWeather;
    mWorld.weatherNext       = pkt.nextWeather;
    mWorld.weatherTransition = pkt.transitionFactor;
    mWorld.weatherRegion     = pkt.regionName;
    mWorld.hasWeather        = true;

    Log(Debug::Verbose) << "[Server] Weather from host " << c.name
                        << ": current=" << pkt.currentWeather
                        << " region=" << pkt.regionName;

    // Relay to all non-host clients.
    broadcastToAll(std::vector<uint8_t>(data, data + size), c.conn);

    mLua.onWorldWeather(
        mWorld.weatherRegion, mWorld.weatherCurrent, mWorld.weatherNext, mWorld.weatherTransition);
}

// ---------------------------------------------------------------------------
void MPServer::handleObjectPlace(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketObjectPlace pkt;
    if (!pkt.decode(data, size)) return;

    if (!acceptPlacedObject(pkt.object, &c))
        return;

    Log(Debug::Info) << "[Server] ObjectPlace accepted: player=" << c.name
                     << " refId=" << pkt.object.refId
                     << " mpNum=" << pkt.object.mpNum
                     << " cell=" << pkt.object.cellId
                     << " count=" << pkt.object.count;

    sendTo(c.conn, pkt.encode());
    broadcastToCell(pkt.object.cellId, pkt.encode(), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handleWorldItemTakeRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketWorldItemTakeRequest packet;
    WorldItemTakeResult result;
    auto sendResult = [&] {
        PacketWorldItemTakeResult response;
        response.result = result;
        sendTo(c.conn, response.encode());
    };
    if (!packet.decode(data, size))
        return;
    const WorldItemTakeRequest& request = packet.request;
    result.requestId = request.requestId;
    result.object = request.object;

    const WorldItemTakeError requestError = validateWorldItemTakeRequest(request);
    if (requestError != WorldItemTakeError::None || !mPlayerDb || !mContentRegistry
        || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        result.error = requestError != WorldItemTakeError::None
            ? requestError : WorldItemTakeError::PersistenceFailure;
        sendResult();
        return;
    }

    const std::string canonicalPlayerCell = makeCellKey(c.player.cell);
    if (request.object.cellId != canonicalPlayerCell)
    {
        result.error = WorldItemTakeError::WrongCell;
        sendResult();
        return;
    }

    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* acceptedPlayer = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!acceptedPlayer || acceptedPlayer->snapshot.cellId != canonicalPlayerCell
        || acceptedPlayer->snapshot.migrationGeneration != 1
        || acceptedPlayer->snapshot.authorityGeneration != c.guid)
    {
        result.error = WorldItemTakeError::PlayerSnapshotUnavailable;
        sendResult();
        return;
    }

    ServerContentRegistry::PlacedItemReference item;
    if (request.object.kind == PlacedObjectKind::ContentReference)
    {
        const auto found = mContentRegistry->findPlacedItemReference(request.object);
        if (!found)
        {
            result.error = WorldItemTakeError::UnknownObject;
            sendResult();
            return;
        }
        item = *found;
        const auto countOverride = mWorld.worldItemCountOverrides.find(makeWorldItemKey(request.object));
        if (countOverride != mWorld.worldItemCountOverrides.end())
        {
            item.worldCount = countOverride->second.resultingWorldCount;
            item.inventoryCount = item.gold ? item.worldCount * item.itemValue : item.worldCount;
            item.enabled = item.worldCount > 0;
        }
    }
    else
    {
        const auto objectsIt = mWorld.placedObjects.find(request.object.cellId);
        if (objectsIt == mWorld.placedObjects.end())
        {
            result.error = WorldItemTakeError::UnknownObject;
            sendResult();
            return;
        }
        const auto found = std::find_if(objectsIt->second.begin(), objectsIt->second.end(),
            [&](const PlacedObject& object) {
                return object.mpNum == request.object.mpNum && object.refId == request.object.refId;
            });
        if (found == objectsIt->second.end())
        {
            result.error = WorldItemTakeError::UnknownObject;
            sendResult();
            return;
        }
        item.identity = request.object;
        item.position = found->position;
        item.worldCount = found->count;
        item.inventoryCount = found->count;
        item.enabled = found->count > 0;
        try
        {
            ESM::RefId contentId = ESM::RefId::deserializeText(found->refId);
            if (contentId.empty())
                contentId = ESM::RefId::stringRefId(found->refId);
            MWWorld::ManualRef contentRef(mContentRegistry->store(), contentId, found->count);
            const MWWorld::Ptr ptr = contentRef.getPtr();
            item.gold = ptr.getClass().isGold(ptr);
            item.itemValue = ptr.getClass().getValue(ptr);
            if (item.gold)
                item.inventoryCount = item.worldCount * item.itemValue;
            item.charge = static_cast<std::int32_t>(ptr.getCellRef().getCharge());
            item.enchantmentCharge = ptr.getCellRef().getEnchantmentCharge();
            item.soul = ptr.getCellRef().getSoul().serializeText();
        }
        catch (const std::exception&)
        {
            result.error = WorldItemTakeError::UnknownObject;
            sendResult();
            return;
        }
    }

    if (!item.enabled)
    {
        result.error = WorldItemTakeError::ObjectUnavailable;
        sendResult();
        return;
    }
    if (request.requestedCount != item.worldCount)
    {
        result.error = WorldItemTakeError::InvalidCount;
        sendResult();
        return;
    }

    const MechanicsSnapshot& playerMechanics = acceptedPlayer->snapshot;
    const float dx = playerMechanics.position.pos[0] - item.position.pos[0];
    const float dy = playerMechanics.position.pos[1] - item.position.pos[1];
    const float dz = playerMechanics.position.pos[2] - item.position.pos[2];
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distanceSquared) || distanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
    {
        result.error = WorldItemTakeError::OutOfRange;
        sendResult();
        return;
    }

    bool factionAllowsUse = item.factionId.empty();
    if (!item.factionId.empty())
    {
        const std::string wanted = lowerAscii(item.factionId);
        const auto membership = std::find_if(c.player.factionState.factions.begin(),
            c.player.factionState.factions.end(), [&](const PlayerFactionEntry& entry) {
                return lowerAscii(entry.factionId) == wanted;
            });
        factionAllowsUse = membership != c.player.factionState.factions.end()
            && !membership->expelled && membership->rank >= item.factionRank;
    }
    const bool ownerAllowsUse = item.ownerId.empty() || lowerAscii(item.ownerId) == "player";
    const bool theft = !item.ownershipGlobalAllowsUse && (!ownerAllowsUse || !factionAllowsUse);
    const std::int64_t crimeValue = theft
        ? (item.gold ? item.inventoryCount
                     : static_cast<std::int64_t>(item.worldCount) * item.itemValue)
        : 0;

    std::vector<Item> inventory = c.player.inventoryChanges.items;
    Item added;
    added.instanceId = request.object.kind == PlacedObjectKind::ServerPlaced
        ? request.object.mpNum : reserveWorldMpNum().value_or(0);
    if (added.instanceId == 0)
    {
        result.error = WorldItemTakeError::PersistenceFailure;
        sendResult();
        return;
    }
    added.refId = item.identity.refId;
    added.count = item.inventoryCount;
    added.charge = item.charge;
    added.enchantmentCharge = item.enchantmentCharge;
    added.soul = item.soul;
    inventory.push_back(added);

    result.accepted = true;
    result.itemRefId = added.refId;
    result.itemCount = added.count;
    result.crimeValue = crimeValue;
    result.theft = theft;
    result.inventoryRevision = c.inventoryRevision + 1;

    std::optional<CrimeSemanticService::Outcome> preparedCrime;
    std::string crimeEventId;
    if (result.theft && mObservationService)
    {
        CrimeWitnessBuildRequest witnessRequest;
        witnessRequest.eventCell = c.player.cell;
        witnessRequest.offender = makeLiveObservationSnapshot(*acceptedPlayer, playerBootWeight(c));
        witnessRequest.alarmRadius = mObservationAlarmRadius;
        witnessRequest.observedAtMs = nowMs;
        witnessRequest.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
        CrimeWitnessBuildResult witnesses = buildLiveCrimeWitnesses(witnessRequest);

        CrimeIntent intent;
        intent.eventId = "world-item-take:" + std::to_string(c.dbAccountId) + ":"
            + std::to_string(c.dbCharacterId) + ":" + request.requestId;
        crimeEventId = intent.eventId;
        intent.source = "world_item_take";
        intent.type = CrimeType::Theft;
        intent.cellId = canonicalPlayerCell;
        intent.offender = witnessRequest.offender;
        intent.value = result.crimeValue;
        intent.observedAtMs = nowMs;
        intent.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
        for (const std::string& cellId : witnesses.candidateCellIds)
        {
            const std::uint64_t generation = mCollisionWorld ? mCollisionWorld->cellGeneration(cellId) : 0;
            if (generation != 0)
                intent.collisionGenerations.push_back({ cellId, generation });
        }

        const auto gmstInt = [&](std::string_view id) {
            return mContentRegistry->store().get<ESM::GameSetting>()
                .find(id)->mValue.getInteger();
        };
        CrimePolicy policy;
        policy.alarmRadius = mObservationAlarmRadius;
        policy.theftBountyMultiplier = mContentRegistry->store().get<ESM::GameSetting>()
            .find("fCrimeStealing")->mValue.getFloat();
        policy.pickpocketBounty = gmstInt("iCrimePickPocket");
        policy.trespassBounty = gmstInt("iCrimeTresspass");
        policy.assaultBounty = gmstInt("iCrimeAttack");
        policy.murderBounty = gmstInt("iCrimeKilling");

        CrimeService crime(*mPlayerDb);
        CrimeSemanticService semantics(*mPlayerDb, crime, *mObservationService, policy);
        CrimeSemanticService::Context context;
        context.accountId = c.dbAccountId;
        context.characterId = c.dbCharacterId;
        context.playerGuid = c.guid;
        context.deferCommit = true;
        preparedCrime = semantics.evaluate(intent, std::move(witnesses.witnesses), context);
        if (!preparedCrime->result.accepted || (!preparedCrime->replayed && !preparedCrime->pendingCommit))
        {
            Log(Debug::Warning) << "[WorldItemTake] semantic preparation rejected request=" << request.requestId
                                << " error=" << static_cast<unsigned>(preparedCrime->result.error);
            result.accepted = false;
            result.error = WorldItemTakeError::PersistenceFailure;
            sendResult();
            return;
        }
    }

    WorldItemTakeCommit commit;
    commit.accountId = c.dbAccountId;
    commit.characterId = c.dbCharacterId;
    commit.requestId = request.requestId;
    commit.requestHash = crypto::sha256hex(canonicalWorldItemTakeRequest(request));
    commit.object = request.object;
    commit.result = result;
    commit.expectedWorldCount = item.worldCount;
    commit.expectedInventoryRevision = request.expectedInventoryRevision;
    commit.resultingInventoryRevision = result.inventoryRevision;
    commit.inventory = inventory;
    if (result.theft && !item.gold)
    {
        if (!item.ownerId.empty())
            commit.stolenItemMutations.push_back(
                { added.refId, item.ownerId, false, added.count });
        else if (!item.factionId.empty())
            commit.stolenItemMutations.push_back(
                { added.refId, item.factionId, true, added.count });
    }

    if (preparedCrime && preparedCrime->pendingCommit)
        commit.crimeMutation = *preparedCrime->pendingCommit;

    WorldItemTakeCommitResult committed;
    try
    {
        committed = mPlayerDb->commitWorldItemTake(commit);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[WorldItemTake] persistence failure request=" << request.requestId
                          << " player=" << c.name << " error=" << e.what();
        result.accepted = false;
        result.error = WorldItemTakeError::PersistenceFailure;
        sendResult();
        return;
    }

    if (committed.status == WorldItemTakeCommitStatus::DuplicateRequestConflict)
    {
        result.accepted = false;
        result.error = WorldItemTakeError::DuplicateConflict;
        sendResult();
        return;
    }
    if (committed.status == WorldItemTakeCommitStatus::ObjectAlreadyTaken
        || committed.status == WorldItemTakeCommitStatus::StaleSource)
    {
        result.accepted = false;
        result.error = WorldItemTakeError::ObjectUnavailable;
        sendResult();
        return;
    }
    if (committed.status == WorldItemTakeCommitStatus::StaleInventoryRevision)
    {
        result.accepted = false;
        result.error = WorldItemTakeError::StaleInventoryRevision;
        sendAuthoritativeInventory(c);
        sendResult();
        return;
    }
    if (committed.status == WorldItemTakeCommitStatus::CrimeDuplicateConflict)
    {
        result.accepted = false;
        result.error = WorldItemTakeError::DuplicateConflict;
        sendResult();
        return;
    }
    if (committed.status == WorldItemTakeCommitStatus::StaleCrimeRevision)
    {
        Log(Debug::Warning) << "[WorldItemTake] crime revision changed before atomic commit request="
                            << request.requestId;
        result.accepted = false;
        result.error = WorldItemTakeError::PersistenceFailure;
        sendResult();
        return;
    }

    result = committed.result;
    result.requestId = request.requestId;
    result.replayed = committed.status == WorldItemTakeCommitStatus::DuplicateRequest;
    if (!result.replayed)
    {
        c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
        c.player.inventoryChanges.items = std::move(inventory);
        c.inventoryRevision = result.inventoryRevision;
        c.player.inventoryChanges.revision = c.inventoryRevision;
        c.restoredInventorySnapshot = c.player.inventoryChanges.items;
        c.hasRestoredInventorySnapshot = true;
        mWorld.worldItemCountOverrides.erase(makeWorldItemKey(result.object));
        mWorld.takenItemReferences[result.object.cellId].push_back(result.object);

        if (result.object.kind == PlacedObjectKind::ServerPlaced)
            removePlacedObjectAuthoritative(result.object.mpNum, result.object.cellId);
        else
        {
            PacketObjectDelete deletion;
            deletion.cellId = result.object.cellId;
            deletion.refId = result.object.refId;
            deletion.refNum = result.object.refIndex;
            deletion.refContentFile = result.object.refContentFile;
            broadcastToCell(result.object.cellId, deletion.encode());
        }
        syncLuaPlayerSnapshot();
        scheduleGeneratedDynamicRecordGc("world_item_take");
    }
    sendAuthoritativeInventory(c);

    if (preparedCrime)
    {
        c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
        c.player.bounty = c.player.crimeState.bounty;
        sendAuthoritativeCrimeState(c);
        if (!result.replayed)
            dispatchCrimeReactions(c, preparedCrime->result);
        Log(preparedCrime->result.accepted ? Debug::Info : Debug::Warning)
            << "[CrimeSemanticResult] type=Theft eventId=" << crimeEventId
            << " crimeSeen=" << preparedCrime->result.crimeSeen
            << " reportingRan=" << preparedCrime->result.reportingStageRun
            << " bountyDelta=" << preparedCrime->result.bountyDelta
            << " crimeIdAdvanced=" << preparedCrime->result.currentCrimeIdAdvanced
            << " finalBounty=" << c.player.crimeState.bounty
            << " finalCurrentCrimeId=" << c.player.crimeState.currentCrimeId
            << " revision=" << c.player.crimeState.revision
            << " replayed=" << preparedCrime->replayed;
    }

    Log(Debug::Info) << "[CrimeCauseAccepted] type=" << (result.theft ? "Theft" : "None")
                     << " eventId=" << request.requestId
                     << " offenderGuid=" << c.guid
                     << " object=" << result.object.refId
                     << " value=" << result.crimeValue
                     << " replayed=" << result.replayed;
    sendResult();
}

// ---------------------------------------------------------------------------
void MPServer::handleInventoryTakeRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketInventoryTakeRequest packet;
    if (!packet.decode(data, size))
        return;

    const InventoryTakeRequest& request = packet.request;
    InventoryTakeResult result;
    result.requestId = request.requestId;
    result.kind = request.kind;
    result.source = request.source;
    result.itemRefId = request.itemRefId;
    result.itemCharge = request.itemCharge;
    auto sendResult = [&] {
        PacketInventoryTakeResult response;
        response.result = result;
        sendTo(c.conn, response.encode());
    };
    auto reject = [&](InventoryTakeError error) {
        result.accepted = false;
        result.error = error;
        result.inventoryRevision = c.inventoryRevision;
        sendResult();
    };

    const InventoryTakeError validation = validateInventoryTakeRequest(request);
    if (validation != InventoryTakeError::None || !mPlayerDb || !mContentRegistry
        || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        reject(validation != InventoryTakeError::None ? validation : InventoryTakeError::PersistenceFailure);
        return;
    }
    if (request.source.refId.starts_with("mp_remote_"))
    {
        Log(Debug::Warning) << "[InventoryTake] rejected remote-player inventory source request="
                            << request.requestId << " player=" << c.name
                            << " source=" << request.source.refId;
        reject(InventoryTakeError::InvalidRequest);
        return;
    }
    const std::string requestHash = crypto::sha256hex(canonicalInventoryTakeRequest(request));
    if (const auto stored = mPlayerDb->loadInventoryTake(
            c.dbAccountId, c.dbCharacterId, request.requestId))
    {
        if (stored->requestHash != requestHash)
            reject(InventoryTakeError::DuplicateConflict);
        else
        {
            result = stored->result;
            sendAuthoritativeInventory(c);
            sendResult();
        }
        return;
    }

    const std::string canonicalPlayerCell = makeCellKey(c.player.cell);
    if (request.source.cellId != canonicalPlayerCell)
    {
        reject(InventoryTakeError::WrongCell);
        return;
    }
    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* acceptedPlayer = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!acceptedPlayer || acceptedPlayer->snapshot.cellId != canonicalPlayerCell
        || acceptedPlayer->snapshot.migrationGeneration != 1
        || acceptedPlayer->snapshot.authorityGeneration != c.guid)
    {
        reject(InventoryTakeError::PlayerSnapshotUnavailable);
        return;
    }
    if (request.expectedInventoryRevision != c.inventoryRevision)
    {
        sendAuthoritativeInventory(c);
        reject(InventoryTakeError::StaleInventoryRevision);
        return;
    }

    const bool actorSource = request.source.actorInstanceId != 0;
    ActorRegistryRecord* actorRecord = nullptr;
    std::string sourceKey;
    Position sourcePosition;
    std::string ownerId;
    std::string factionId;
    int factionRank = -1;
    bool ownershipGlobalAllowsUse = false;
    int merchantServices = 0;
    std::uint32_t bootstrapAuthority = 0;
    if (actorSource)
    {
        const auto keyIt = mWorld.actorKeysByNetId.find(request.source.actorInstanceId);
        const auto locationIt = keyIt == mWorld.actorKeysByNetId.end()
            ? mWorld.actorLocations.end() : mWorld.actorLocations.find(keyIt->second);
        auto cellIt = locationIt == mWorld.actorLocations.end()
            ? mWorld.actorCells.end() : mWorld.actorCells.find(locationIt->second);
        auto actorIt = cellIt == mWorld.actorCells.end() || keyIt == mWorld.actorKeysByNetId.end()
            ? std::unordered_map<std::string, ActorRegistryRecord>::iterator{}
            : cellIt->second.actors.find(keyIt->second);
        if (cellIt == mWorld.actorCells.end() || keyIt == mWorld.actorKeysByNetId.end()
            || actorIt == cellIt->second.actors.end())
        {
            reject(InventoryTakeError::StaleSource);
            return;
        }
        actorRecord = &actorIt->second;
        const bool deadKind = request.kind == InventoryTakeKind::Corpse;
        if (locationIt->second != canonicalPlayerCell
            || actorRecord->actorNetId != request.source.actorInstanceId
            || actorRecord->migrationGeneration != request.source.migrationGeneration
            || actorRecord->actor.refId != request.source.refId
            || actorRecord->actor.refNum != request.source.refNum
            || actorRecord->actor.mpNum != request.source.mpNum
            || actorRecord->actor.isDead != deadKind
            || (request.kind == InventoryTakeKind::Container))
        {
            reject(InventoryTakeError::StaleSource);
            return;
        }
        // Vanilla actors require a placed refNum; server-spawned actors are lifetime-unique
        // by mpNum and are persisted under that identity.
        if (actorRecord->actor.mpNum == 0 && actorRecord->actor.refNum == 0)
        {
            reject(InventoryTakeError::StaleSource);
            return;
        }
        sourcePosition = actorRecord->actor.position;
        bootstrapAuthority = isActorAuthorityLeaseValid(*actorRecord, canonicalPlayerCell, nowMs)
            ? actorRecord->actorAuthorityGuid : cellIt->second.authorityGuid;
        sourceKey = makeContainerKey(canonicalPlayerCell, actorRecord->actor.refId,
            actorRecord->actor.refNum, actorRecord->actor.mpNum);
    }
    else
    {
        if (request.kind != InventoryTakeKind::Container && request.kind != InventoryTakeKind::Barter)
        {
            reject(InventoryTakeError::InvalidRequest);
            return;
        }
        bool sourceResolved = false;
        if (request.source.mpNum != 0)
        {
            // Player-placed containers are unowned and therefore are not a Theft
            // producer. Their legacy persistence key is not lifetime-unique, so
            // they remain on the existing synchronization path for now.
            reject(InventoryTakeError::StaleSource);
            return;
        }
        else
        {
            const auto found = mContentRegistry->findContainerReference(
                request.source.cellId, request.source.refId, request.source.refNum);
            if (found && found->enabled)
            {
                sourcePosition = found->position;
                ownerId = found->ownerId;
                factionId = found->factionId;
                factionRank = found->factionRank;
                ownershipGlobalAllowsUse = found->ownershipGlobalAllowsUse;
                sourceResolved = true;
            }
        }
        if (!sourceResolved)
        {
            reject(InventoryTakeError::StaleSource);
            return;
        }
        const auto cellIt = mWorld.actorCells.find(canonicalPlayerCell);
        bootstrapAuthority = cellIt == mWorld.actorCells.end() ? 0 : cellIt->second.authorityGuid;
        sourceKey = makeContainerKey(request.source.cellId, request.source.refId,
            request.source.refNum, request.source.mpNum);
    }

    Position interactionPosition = sourcePosition;
    if (request.kind == InventoryTakeKind::Barter)
    {
        if (request.merchant.cellId != canonicalPlayerCell)
        {
            reject(InventoryTakeError::WrongCell);
            return;
        }

        const auto merchantKeyIt = mWorld.actorKeysByNetId.find(request.merchant.actorInstanceId);
        const auto merchantLocationIt = merchantKeyIt == mWorld.actorKeysByNetId.end()
            ? mWorld.actorLocations.end() : mWorld.actorLocations.find(merchantKeyIt->second);
        auto merchantCellIt = merchantLocationIt == mWorld.actorLocations.end()
            ? mWorld.actorCells.end() : mWorld.actorCells.find(merchantLocationIt->second);
        auto merchantIt = merchantCellIt == mWorld.actorCells.end() || merchantKeyIt == mWorld.actorKeysByNetId.end()
            ? std::unordered_map<std::string, ActorRegistryRecord>::iterator{}
            : merchantCellIt->second.actors.find(merchantKeyIt->second);
        if (merchantCellIt == mWorld.actorCells.end() || merchantKeyIt == mWorld.actorKeysByNetId.end()
            || merchantIt == merchantCellIt->second.actors.end())
        {
            reject(InventoryTakeError::StaleSource);
            return;
        }

        const ActorRegistryRecord& merchantRecord = merchantIt->second;
        if (merchantLocationIt->second != canonicalPlayerCell
            || merchantRecord.actorNetId != request.merchant.actorInstanceId
            || merchantRecord.migrationGeneration != request.merchant.migrationGeneration
            || merchantRecord.actor.refId != request.merchant.refId
            || merchantRecord.actor.refNum != request.merchant.refNum
            || merchantRecord.actor.mpNum != request.merchant.mpNum
            || merchantRecord.actor.isDead)
        {
            reject(InventoryTakeError::StaleSource);
            return;
        }
        if (merchantRecord.actor.mpNum == 0 && merchantRecord.actor.refNum == 0)
        {
            reject(InventoryTakeError::StaleSource);
            return;
        }

        ESM::RefId merchantContentId = ESM::RefId::deserializeText(merchantRecord.actor.refId);
        if (merchantContentId.empty())
            merchantContentId = ESM::RefId::stringRefId(merchantRecord.actor.refId);
        const auto& contentStore = mContentRegistry->store();
        const ESM::NPC* merchantNpc = contentStore.get<ESM::NPC>().search(merchantContentId);
        const ESM::Creature* merchantCreature = merchantNpc == nullptr
            ? contentStore.get<ESM::Creature>().search(merchantContentId) : nullptr;
        if (merchantNpc != nullptr)
        {
            merchantServices = (merchantNpc->mFlags & ESM::NPC::Autocalc) && !merchantNpc->mClass.empty()
                ? contentStore.get<ESM::Class>().search(merchantNpc->mClass) != nullptr
                    ? contentStore.get<ESM::Class>().find(merchantNpc->mClass)->mData.mServices
                    : 0
                : merchantNpc->mAiData.mServices;
        }
        else if (merchantCreature != nullptr)
            merchantServices = merchantCreature->mAiData.mServices;
        if ((merchantServices & ESM::NPC::AllItems) == 0)
        {
            Log(Debug::Warning) << "[InventoryTake] rejected barter merchant without barter services request="
                                << request.requestId << " merchant=" << merchantRecord.actor.refId;
            reject(InventoryTakeError::StaleSource);
            return;
        }

        if (actorSource)
        {
            if (request.source.actorInstanceId != request.merchant.actorInstanceId)
            {
                reject(InventoryTakeError::StaleSource);
                return;
            }
        }
        else if (ownerId.empty() || lowerAscii(ownerId) != lowerAscii(merchantRecord.actor.refId))
        {
            Log(Debug::Warning) << "[InventoryTake] rejected barter source ownership request="
                                << request.requestId << " merchant=" << merchantRecord.actor.refId
                                << " source=" << request.source.refId << " owner=" << ownerId;
            reject(InventoryTakeError::StaleSource);
            return;
        }
        interactionPosition = merchantRecord.actor.position;
    }

    auto sourceIt = mWorld.containers.find(sourceKey);
    if (sourceIt == mWorld.containers.end() || !sourceIt->second.hasAuthority)
    {
        if (bootstrapAuthority != 0)
        {
            const auto authority = std::find_if(mClients.begin(), mClients.end(),
                [&](const auto& entry) { return entry.second.guid == bootstrapAuthority; });
            if (authority != mClients.end())
            {
                PacketContainer bootstrap;
                bootstrap.container.cellId = request.source.cellId;
                bootstrap.container.refId = request.source.refId;
                bootstrap.container.refNum = request.source.refNum;
                bootstrap.container.mpNum = request.source.mpNum;
                bootstrap.mAction = static_cast<std::uint8_t>(ContainerAction::BootstrapRequest);
                sendTo(authority->second.conn, bootstrap.encode());
            }
        }
        reject(InventoryTakeError::SourceUnavailable);
        return;
    }

    const bool finish = request.kind == InventoryTakeKind::PickpocketFinish;
    if (!finish)
    {
        const float dx = acceptedPlayer->snapshot.position.pos[0] - interactionPosition.pos[0];
        const float dy = acceptedPlayer->snapshot.position.pos[1] - interactionPosition.pos[1];
        const float dz = acceptedPlayer->snapshot.position.pos[2] - interactionPosition.pos[2];
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distanceSquared)
            || distanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
        {
            reject(InventoryTakeError::OutOfRange);
            return;
        }
    }

    ContainerRecord expectedSource = sourceIt->second;
    normalizeContainerItems(expectedSource.items);
    ContainerRecord resultingSource = expectedSource;
    auto sourceItem = finish ? resultingSource.items.end()
        : std::find_if(resultingSource.items.begin(), resultingSource.items.end(),
            [&](const ContainerItem& item) {
                return lowerAscii(item.refId) == lowerAscii(request.itemRefId)
                    && item.charge == request.itemCharge
                    && std::abs(item.enchantmentCharge - request.itemEnchantmentCharge) < 0.001f
                    && item.soul == request.itemSoul && item.count >= request.requestedCount;
            });
    if (!finish && sourceItem == resultingSource.items.end())
    {
        std::ostringstream available;
        bool first = true;
        for (const ContainerItem& item : resultingSource.items)
        {
            if (!first)
                available << ',';
            first = false;
            available << item.refId << "[count=" << item.count << ",charge=" << item.charge << ']';
        }
        Log(Debug::Warning) << "[InventoryTake] item unavailable request=" << request.requestId
                            << " source=" << request.source.refId
                            << " requested=" << request.itemRefId
                            << " count=" << request.requestedCount
                            << " charge=" << request.itemCharge
                            << " available=" << available.str();
        reject(InventoryTakeError::ItemUnavailable);
        return;
    }

    ContainerItem removedSourceItem;
    if (!finish)
    {
        removedSourceItem = *sourceItem;
        removedSourceItem.count = request.requestedCount;
    }

    int itemValue = 0;
    int minimumBarterPrice = 0;
    bool gold = false;
    Item added;
    if (!finish)
    {
        try
        {
            ESM::RefId contentId = ESM::RefId::deserializeText(request.itemRefId);
            if (contentId.empty())
                contentId = ESM::RefId::stringRefId(request.itemRefId);
            MWWorld::ManualRef contentRef(mContentRegistry->store(), contentId, request.requestedCount);
            MWWorld::Ptr ptr = contentRef.getPtr();
            itemValue = ptr.getClass().getValue(ptr);
            gold = ptr.getClass().isGold(ptr);
            if (request.kind == InventoryTakeKind::Barter)
            {
                if (gold || !ptr.getClass().canSell(ptr, merchantServices))
                {
                    Log(Debug::Warning) << "[InventoryTake] rejected barter item outside merchant services request="
                                        << request.requestId << " merchant=" << request.merchant.refId
                                        << " item=" << request.itemRefId << " services=" << merchantServices;
                    reject(InventoryTakeError::ItemUnavailable);
                    return;
                }

                float effectiveUnitValue = static_cast<float>(itemValue);
                if (ptr.getClass().hasItemHealth(ptr))
                {
                    if (request.itemCharge >= 0)
                        ptr.getCellRef().setCharge(request.itemCharge);
                    effectiveUnitValue *= ptr.getClass().getItemNormalizedHealth(ptr);
                }
                const int effectiveValue = static_cast<int>(effectiveUnitValue * request.requestedCount);
                minimumBarterPrice = static_cast<int>(std::max(1.f, 0.75f * effectiveValue));
                if (request.barterPrice < minimumBarterPrice)
                {
                    Log(Debug::Warning) << "[InventoryTake] rejected barter price below native floor request="
                                        << request.requestId << " merchant=" << request.merchant.refId
                                        << " item=" << request.itemRefId << " price=" << request.barterPrice
                                        << " minimum=" << minimumBarterPrice;
                    reject(InventoryTakeError::InvalidPrice);
                    return;
                }
            }
            added.refId = request.itemRefId;
            added.count = gold ? request.requestedCount * itemValue : request.requestedCount;
            added.charge = sourceItem->charge;
            added.enchantmentCharge = sourceItem->enchantmentCharge;
            added.soul = sourceItem->soul;
        }
        catch (const std::exception&)
        {
            reject(InventoryTakeError::ItemUnavailable);
            return;
        }
    }

    const AcceptedMechanicsSnapshot* acceptedVictim = nullptr;
    if (actorSource && request.kind != InventoryTakeKind::Corpse
        && request.kind != InventoryTakeKind::Barter)
    {
        acceptedVictim = mMechanicsSnapshots.findFresh(
            { MechanicsSubjectKind::Npc, 0, request.source.actorInstanceId }, nowMs, MaximumSnapshotAgeMs);
        if (!acceptedVictim
            || acceptedVictim->snapshot.migrationGeneration != request.source.migrationGeneration
            || acceptedVictim->snapshot.cellId != canonicalPlayerCell)
        {
            reject(InventoryTakeError::PlayerSnapshotUnavailable);
            return;
        }
    }

    bool detected = false;
    int detectionRoll = -1;
    const bool pickpocket = request.kind == InventoryTakeKind::Pickpocket || finish;
    if (pickpocket)
    {
        if (!mObservationRollSource)
        {
            reject(InventoryTakeError::PersistenceFailure);
            return;
        }
        const float fatigueBase = mContentRegistry->store().get<ESM::GameSetting>()
            .find("fFatigueBase")->mValue.getFloat();
        const float fatigueMultiplier = mContentRegistry->store().get<ESM::GameSetting>()
            .find("fFatigueMult")->mValue.getFloat();
        const auto fatigueTerm = [&](const MechanicsSnapshot& snapshot) {
            const float normalized = std::floor(snapshot.fatigueMaximumModified) == 0.f
                ? 1.f : std::max(0.f, snapshot.fatigueCurrent / snapshot.fatigueMaximumModified);
            return fatigueBase - fatigueMultiplier * (1.f - normalized);
        };
        const MechanicsSnapshot& thief = acceptedPlayer->snapshot;
        const MechanicsSnapshot& victim = acceptedVictim->snapshot;
        const float valueTerm = finish ? 0.f
            : 10.f * mContentRegistry->store().get<ESM::GameSetting>()
                .find("fPickPocketMod")->mValue.getFloat()
                * static_cast<float>(itemValue * request.requestedCount);
        const int minimumDivisor = mContentRegistry->store().get<ESM::GameSetting>()
            .find("iPickMinChance")->mValue.getInteger();
        const int maximumChance = mContentRegistry->store().get<ESM::GameSetting>()
            .find("iPickMaxChance")->mValue.getInteger();
        detectionRoll = std::clamp(mObservationRollSource->nextRoll0To99(), 0, 99);
        PickpocketDetectionInput detection;
        detection.thiefSneak = thief.sneakSkill;
        detection.thiefAgility = thief.agility;
        detection.thiefLuck = thief.luck;
        detection.thiefFatigueTerm = fatigueTerm(thief);
        detection.victimSneak = victim.sneakSkill;
        detection.victimAgility = victim.agility;
        detection.victimLuck = victim.luck;
        detection.victimFatigueTerm = fatigueTerm(victim);
        detection.valueTerm = valueTerm;
        detection.minimumChanceDivisor = minimumDivisor;
        detection.maximumChance = maximumChance;
        detection.roll0To99 = detectionRoll;
        const PickpocketDetectionResult evaluated = evaluatePickpocketDetection(detection);
        if (!evaluated.valid)
        {
            reject(InventoryTakeError::PersistenceFailure);
            return;
        }
        detected = evaluated.detected;
    }

    bool factionAllowsUse = factionId.empty();
    if (!factionId.empty())
    {
        const std::string wanted = lowerAscii(factionId);
        const auto membership = std::find_if(c.player.factionState.factions.begin(),
            c.player.factionState.factions.end(), [&](const PlayerFactionEntry& entry) {
                return lowerAscii(entry.factionId) == wanted;
            });
        factionAllowsUse = membership != c.player.factionState.factions.end()
            && !membership->expelled && membership->rank >= factionRank;
    }
    const bool ownerAllowsUse = ownerId.empty() || lowerAscii(ownerId) == "player";
    bool actorAllowsUse = false;
    if (acceptedVictim)
    {
        const std::uint8_t flags = acceptedVictim->snapshot.witnessStateFlags;
        if ((flags & MechanicsWitnessRelationshipKnown) == 0)
        {
            reject(InventoryTakeError::PlayerSnapshotUnavailable);
            return;
        }
        actorAllowsUse = (flags & MechanicsWitnessPlayerFollower) != 0;
    }
    const bool theft = !pickpocket && request.kind != InventoryTakeKind::Corpse
        && request.kind != InventoryTakeKind::Barter
        && ((actorSource && !actorAllowsUse)
            || (!actorSource && !ownershipGlobalAllowsUse && (!ownerAllowsUse || !factionAllowsUse)));
    const std::int64_t crimeValue = theft
        ? (gold ? added.count : static_cast<std::int64_t>(request.requestedCount) * itemValue) : 0;

    std::vector<Item> inventory = c.player.inventoryChanges.items;
    if (request.kind == InventoryTakeKind::Barter)
    {
        std::int64_t availableGold = 0;
        for (const Item& item : inventory)
        {
            if (lowerAscii(item.refId) == "gold_001" && item.count > 0)
                availableGold += item.count;
        }
        if (availableGold < request.barterPrice)
        {
            Log(Debug::Warning) << "[InventoryTake] rejected barter for insufficient gold request="
                                << request.requestId << " player=" << c.name
                                << " price=" << request.barterPrice << " available=" << availableGold;
            reject(InventoryTakeError::InsufficientGold);
            return;
        }

        std::int64_t remainingGold = request.barterPrice;
        for (auto it = inventory.begin(); it != inventory.end() && remainingGold > 0;)
        {
            if (lowerAscii(it->refId) != "gold_001" || it->count <= 0)
            {
                ++it;
                continue;
            }
            const int removed = static_cast<int>(std::min<std::int64_t>(remainingGold, it->count));
            it->count -= removed;
            remainingGold -= removed;
            if (it->count <= 0)
                it = inventory.erase(it);
            else
                ++it;
        }
    }

    std::uint64_t resultingRevision = request.expectedInventoryRevision;
    if (!detected && !finish)
    {
        sourceItem->count -= request.requestedCount;
        if (sourceItem->count == 0)
            resultingSource.items.erase(sourceItem);
        normalizeContainerItems(resultingSource.items);
        auto destinationStack = std::find_if(inventory.begin(), inventory.end(),
            [&](const Item& item) { return sameItemIdentity(item, added); });
        if (destinationStack != inventory.end())
        {
            destinationStack->count += added.count;
            added.instanceId = destinationStack->instanceId;
        }
        else
        {
            const auto instance = reserveWorldMpNum();
            if (!instance)
            {
                reject(InventoryTakeError::PersistenceFailure);
                return;
            }
            added.instanceId = *instance;
            inventory.push_back(added);
        }
        resultingRevision = request.expectedInventoryRevision + 1;
    }

    result.accepted = true;
    result.error = InventoryTakeError::None;
    result.itemCount = finish ? 0 : request.requestedCount;
    result.inventoryRevision = resultingRevision;
    result.detected = detected;
    result.detectionRoll = detectionRoll;
    result.theft = theft;
    result.crimeValue = crimeValue;

    const bool crime = result.theft || (pickpocket && result.detected);
    std::optional<CrimeSemanticService::Outcome> preparedCrime;
    if (crime && mObservationService)
    {
        CrimeWitnessBuildRequest witnessRequest;
        witnessRequest.eventCell = c.player.cell;
        witnessRequest.offender = makeLiveObservationSnapshot(*acceptedPlayer, playerBootWeight(c));
        witnessRequest.victim = actorSource
            ? std::optional<ObservationActorIdentity>({ ObservationActorKind::Npc, 0,
                  request.source.actorInstanceId }) : std::nullopt;
        witnessRequest.alarmRadius = mObservationAlarmRadius;
        witnessRequest.observedAtMs = nowMs;
        witnessRequest.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
        CrimeWitnessBuildResult witnesses = buildLiveCrimeWitnesses(witnessRequest);

        CrimeIntent intent;
        intent.eventId = "inventory-take:" + std::to_string(c.dbAccountId) + ":"
            + std::to_string(c.dbCharacterId) + ":" + request.requestId;
        intent.source = pickpocket ? "authoritative_pickpocket" : "authoritative_container_take";
        intent.type = pickpocket ? CrimeType::Pickpocket : CrimeType::Theft;
        intent.cellId = canonicalPlayerCell;
        intent.offender = witnessRequest.offender;
        intent.victim = witnessRequest.victim;
        intent.victimAware = pickpocket;
        intent.value = result.crimeValue;
        intent.observedAtMs = nowMs;
        intent.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
        for (const std::string& cellId : witnesses.candidateCellIds)
        {
            const std::uint64_t generation = mCollisionWorld ? mCollisionWorld->cellGeneration(cellId) : 0;
            if (generation != 0)
                intent.collisionGenerations.push_back({ cellId, generation });
        }
        const auto gmstInt = [&](std::string_view id) {
            return mContentRegistry->store().get<ESM::GameSetting>().find(id)->mValue.getInteger();
        };
        CrimePolicy policy;
        policy.alarmRadius = mObservationAlarmRadius;
        policy.theftBountyMultiplier = mContentRegistry->store().get<ESM::GameSetting>()
            .find("fCrimeStealing")->mValue.getFloat();
        policy.pickpocketBounty = gmstInt("iCrimePickPocket");
        policy.trespassBounty = gmstInt("iCrimeTresspass");
        policy.assaultBounty = gmstInt("iCrimeAttack");
        policy.murderBounty = gmstInt("iCrimeKilling");
        CrimeService crimeService(*mPlayerDb);
        CrimeSemanticService semantics(*mPlayerDb, crimeService, *mObservationService, policy);
        CrimeSemanticService::Context context { c.dbAccountId, c.dbCharacterId, c.guid };
        context.deferCommit = true;
        preparedCrime = semantics.evaluate(intent, std::move(witnesses.witnesses), context);
        if (!preparedCrime->result.accepted || (!preparedCrime->replayed && !preparedCrime->pendingCommit))
        {
            Log(Debug::Warning) << "[InventoryTake] semantic preparation rejected request=" << request.requestId
                                << " error=" << static_cast<unsigned>(preparedCrime->result.error);
            reject(InventoryTakeError::PersistenceFailure);
            return;
        }
    }

    InventoryTakeCommit commit;
    commit.accountId = c.dbAccountId;
    commit.characterId = c.dbCharacterId;
    commit.requestId = request.requestId;
    commit.requestHash = requestHash;
    commit.result = result;
    commit.expectedInventoryRevision = request.expectedInventoryRevision;
    commit.resultingInventoryRevision = resultingRevision;
    commit.inventory = inventory;
    commit.expectedSource = expectedSource;
    if (!detected && !finish)
        commit.resultingSource = resultingSource;

    if (!detected && !finish && !gold && (pickpocket || theft))
    {
        if (actorSource)
            commit.stolenItemMutations.push_back(
                { added.refId, actorRecord->actor.refId, false, request.requestedCount });
        else if (!ownerId.empty())
            commit.stolenItemMutations.push_back(
                { added.refId, ownerId, false, request.requestedCount });
        else if (!factionId.empty())
            commit.stolenItemMutations.push_back(
                { added.refId, factionId, true, request.requestedCount });
    }

    if (preparedCrime && preparedCrime->pendingCommit)
        commit.crimeMutation = *preparedCrime->pendingCommit;

    InventoryTakeCommitResult committed;
    try
    {
        committed = mPlayerDb->commitInventoryTake(commit);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[InventoryTake] persistence failure request=" << request.requestId
                          << " player=" << c.name << " error=" << e.what();
        reject(InventoryTakeError::PersistenceFailure);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::DuplicateRequestConflict)
    {
        reject(InventoryTakeError::DuplicateConflict);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::StaleInventoryRevision)
    {
        sendAuthoritativeInventory(c);
        reject(InventoryTakeError::StaleInventoryRevision);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::StaleSource)
    {
        reject(InventoryTakeError::StaleSource);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::CrimeDuplicateConflict)
    {
        reject(InventoryTakeError::DuplicateConflict);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::StaleCrimeRevision)
    {
        Log(Debug::Warning) << "[InventoryTake] crime revision changed before atomic commit request="
                            << request.requestId;
        reject(InventoryTakeError::PersistenceFailure);
        return;
    }

    result = committed.result;
    result.replayed = committed.status == InventoryTakeCommitStatus::DuplicateRequest;
    if (!result.replayed)
    {
        if (!detected && !finish)
        {
            sourceIt->second = resultingSource;
            c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
            c.player.inventoryChanges.items = std::move(inventory);
            c.inventoryRevision = resultingRevision;
            c.player.inventoryChanges.revision = resultingRevision;
            c.restoredInventorySnapshot = c.player.inventoryChanges.items;
            c.hasRestoredInventorySnapshot = true;
            PacketContainer sourcePacket;
            sourcePacket.container.cellId = sourceIt->second.cellId;
            sourcePacket.container.refId = sourceIt->second.refId;
            sourcePacket.container.refNum = sourceIt->second.refNum;
            sourcePacket.container.mpNum = sourceIt->second.mpNum;
            sourcePacket.container.hasAuthority = true;
            sourcePacket.container.items.push_back(removedSourceItem);
            sourcePacket.mAction = static_cast<std::uint8_t>(ContainerAction::Remove);
            broadcastToCell(canonicalPlayerCell, sourcePacket.encode());
            scheduleGeneratedDynamicRecordGc("inventory_take");
        }
        syncLuaPlayerSnapshot();
    }
    sendAuthoritativeInventory(c);

    if (preparedCrime)
    {
        c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
        c.player.bounty = c.player.crimeState.bounty;
        sendAuthoritativeCrimeState(c);
        if (!result.replayed)
            dispatchCrimeReactions(c, preparedCrime->result);
        Log(preparedCrime->result.accepted ? Debug::Info : Debug::Warning)
            << "[CrimeSemanticResult] type=" << (pickpocket ? "Pickpocket" : "Theft")
            << " eventId=inventory-take:" << c.dbAccountId << ':' << c.dbCharacterId << ':' << request.requestId
            << " crimeSeen=" << preparedCrime->result.crimeSeen
            << " reportingRan=" << preparedCrime->result.reportingStageRun
            << " bountyDelta=" << preparedCrime->result.bountyDelta
            << " crimeIdAdvanced=" << preparedCrime->result.currentCrimeIdAdvanced
            << " finalBounty=" << c.player.crimeState.bounty
            << " finalCurrentCrimeId=" << c.player.crimeState.currentCrimeId
            << " revision=" << c.player.crimeState.revision
            << " replayed=" << preparedCrime->replayed;
    }

    Log(Debug::Info) << "[InventoryTake] accepted request=" << request.requestId
                     << " kind=" << static_cast<int>(request.kind)
                     << " source=" << request.source.refId
                     << (request.kind == InventoryTakeKind::Barter ? " merchant=" : "")
                     << (request.kind == InventoryTakeKind::Barter ? request.merchant.refId : "")
                     << " item=" << request.itemRefId
                     << (request.kind == InventoryTakeKind::Barter ? " price=" : "")
                     << (request.kind == InventoryTakeKind::Barter ? std::to_string(request.barterPrice) : "")
                     << " count=" << result.itemCount << " detected=" << result.detected
                     << " roll=" << result.detectionRoll << " theft=" << result.theft
                     << " replayed=" << result.replayed;
    sendResult();
}

// ---------------------------------------------------------------------------
void MPServer::handleInventoryPutRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketInventoryPutRequest packet;
    if (!packet.decode(data, size))
        return;

    const InventoryPutRequest& request = packet.request;
    InventoryPutResult result;
    result.requestId = request.requestId;
    result.destination = request.destination;
    result.itemRefId = request.itemRefId;
    result.itemInstanceId = request.itemInstanceId;
    result.itemCharge = request.itemCharge;
    auto sendResult = [&] {
        PacketInventoryPutResult response;
        response.result = result;
        sendTo(c.conn, response.encode());
    };
    auto reject = [&](InventoryPutError error) {
        result.accepted = false;
        result.error = error;
        result.inventoryRevision = c.inventoryRevision;
        sendResult();
    };

    const InventoryPutError validation = validateInventoryPutRequest(request);
    if (validation != InventoryPutError::None || !mPlayerDb || !mContentRegistry
        || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        reject(validation != InventoryPutError::None ? validation : InventoryPutError::PersistenceFailure);
        return;
    }

    const std::string requestHash = crypto::sha256hex(canonicalInventoryPutRequest(request));
    if (const auto stored = mPlayerDb->loadInventoryTake(
            c.dbAccountId, c.dbCharacterId, request.requestId))
    {
        if (stored->requestHash != requestHash)
        {
            reject(InventoryPutError::DuplicateConflict);
            return;
        }
        result.accepted = true;
        result.replayed = true;
        result.error = InventoryPutError::None;
        result.destination = stored->result.source;
        result.itemRefId = stored->result.itemRefId;
        result.itemCharge = stored->result.itemCharge;
        result.itemCount = stored->result.itemCount;
        result.inventoryRevision = stored->result.inventoryRevision;
        const std::string destinationKey = makeContainerKey(result.destination.cellId,
            result.destination.refId, result.destination.refNum, result.destination.mpNum);
        const auto destinationIt = mWorld.containers.find(destinationKey);
        if (destinationIt != mWorld.containers.end() && destinationIt->second.hasAuthority)
        {
            PacketContainer correction;
            correction.container = destinationIt->second;
            correction.mAction = static_cast<std::uint8_t>(ContainerAction::Set);
            sendTo(c.conn, correction.encode());
        }
        sendAuthoritativeInventory(c);
        sendResult();
        return;
    }

    const std::string canonicalPlayerCell = makeCellKey(c.player.cell);
    if (request.destination.cellId != canonicalPlayerCell)
    {
        reject(InventoryPutError::WrongCell);
        return;
    }
    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* acceptedPlayer = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!acceptedPlayer || acceptedPlayer->snapshot.cellId != canonicalPlayerCell
        || acceptedPlayer->snapshot.migrationGeneration != 1
        || acceptedPlayer->snapshot.authorityGeneration != c.guid)
    {
        reject(InventoryPutError::PlayerSnapshotUnavailable);
        return;
    }
    if (request.expectedInventoryRevision != c.inventoryRevision)
    {
        sendAuthoritativeInventory(c);
        reject(InventoryPutError::StaleInventoryRevision);
        return;
    }

    const bool actorDestination = request.destination.actorInstanceId != 0;
    Position destinationPosition;
    std::uint32_t bootstrapAuthority = 0;
    std::string destinationKey;
    if (actorDestination)
    {
        const auto keyIt = mWorld.actorKeysByNetId.find(request.destination.actorInstanceId);
        const auto locationIt = keyIt == mWorld.actorKeysByNetId.end()
            ? mWorld.actorLocations.end() : mWorld.actorLocations.find(keyIt->second);
        auto cellIt = locationIt == mWorld.actorLocations.end()
            ? mWorld.actorCells.end() : mWorld.actorCells.find(locationIt->second);
        auto actorIt = cellIt == mWorld.actorCells.end() || keyIt == mWorld.actorKeysByNetId.end()
            ? std::unordered_map<std::string, ActorRegistryRecord>::iterator{}
            : cellIt->second.actors.find(keyIt->second);
        if (cellIt == mWorld.actorCells.end() || keyIt == mWorld.actorKeysByNetId.end()
            || actorIt == cellIt->second.actors.end())
        {
            reject(InventoryPutError::StaleDestination);
            return;
        }

        ActorRegistryRecord& actorRecord = actorIt->second;
        if (locationIt->second != canonicalPlayerCell
            || actorRecord.actorNetId != request.destination.actorInstanceId
            || actorRecord.migrationGeneration != request.destination.migrationGeneration
            || actorRecord.actor.refId != request.destination.refId
            || actorRecord.actor.refNum != request.destination.refNum
            || actorRecord.actor.mpNum != request.destination.mpNum
            || !actorRecord.actor.isDead
            || (actorRecord.actor.mpNum == 0 && actorRecord.actor.refNum == 0))
        {
            reject(InventoryPutError::StaleDestination);
            return;
        }

        destinationPosition = actorRecord.actor.position;
        bootstrapAuthority = isActorAuthorityLeaseValid(actorRecord, canonicalPlayerCell, nowMs)
            ? actorRecord.actorAuthorityGuid : cellIt->second.authorityGuid;
        destinationKey = makeContainerKey(canonicalPlayerCell, actorRecord.actor.refId,
            actorRecord.actor.refNum, actorRecord.actor.mpNum);
    }
    else
    {
        if (request.destination.mpNum != 0)
        {
            // Dynamic non-actor containers are still on the legacy synchronization
            // path. Spawned corpses are lifetime-unique by mpNum and are handled above.
            reject(InventoryPutError::StaleDestination);
            return;
        }
        const auto destinationReference = mContentRegistry->findContainerReference(
            request.destination.cellId, request.destination.refId, request.destination.refNum);
        if (!destinationReference || !destinationReference->enabled)
        {
            reject(InventoryPutError::StaleDestination);
            return;
        }

        destinationPosition = destinationReference->position;
        const auto cellIt = mWorld.actorCells.find(canonicalPlayerCell);
        bootstrapAuthority = cellIt == mWorld.actorCells.end() ? 0 : cellIt->second.authorityGuid;
        destinationKey = makeContainerKey(request.destination.cellId,
            request.destination.refId, request.destination.refNum, request.destination.mpNum);
    }

    auto destinationIt = mWorld.containers.find(destinationKey);
    if (destinationIt == mWorld.containers.end() || !destinationIt->second.hasAuthority)
    {
        if (bootstrapAuthority != 0)
        {
            const auto authority = std::find_if(mClients.begin(), mClients.end(),
                [&](const auto& entry) { return entry.second.guid == bootstrapAuthority; });
            if (authority != mClients.end())
            {
                PacketContainer bootstrap;
                bootstrap.container.cellId = request.destination.cellId;
                bootstrap.container.refId = request.destination.refId;
                bootstrap.container.refNum = request.destination.refNum;
                bootstrap.container.mpNum = request.destination.mpNum;
                bootstrap.mAction = static_cast<std::uint8_t>(ContainerAction::BootstrapRequest);
                sendTo(authority->second.conn, bootstrap.encode());
            }
        }
        reject(InventoryPutError::DestinationUnavailable);
        return;
    }

    const float dx = acceptedPlayer->snapshot.position.pos[0] - destinationPosition.pos[0];
    const float dy = acceptedPlayer->snapshot.position.pos[1] - destinationPosition.pos[1];
    const float dz = acceptedPlayer->snapshot.position.pos[2] - destinationPosition.pos[2];
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distanceSquared)
        || distanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
    {
        reject(InventoryPutError::OutOfRange);
        return;
    }

    std::vector<Item> inventory = c.player.inventoryChanges.items;
    auto sourceItem = std::find_if(inventory.begin(), inventory.end(), [&](const Item& item) {
        return item.instanceId == request.itemInstanceId
            && lowerAscii(item.refId) == lowerAscii(request.itemRefId)
            && item.charge == request.itemCharge && item.count >= request.requestedCount;
    });
    if (sourceItem == inventory.end() || !isAuthoritativeRecordReference(sourceItem->refId))
    {
        reject(InventoryPutError::ItemUnavailable);
        return;
    }

    ContainerRecord expectedDestination = destinationIt->second;
    normalizeContainerItems(expectedDestination.items);
    ContainerRecord resultingDestination = expectedDestination;
    ContainerItem destinationItem;
    destinationItem.refId = sourceItem->refId;
    destinationItem.count = request.requestedCount;
    destinationItem.charge = sourceItem->charge;
    destinationItem.enchantmentCharge = sourceItem->enchantmentCharge;
    destinationItem.soul = sourceItem->soul;
    if (request.requestedCount == sourceItem->count)
        destinationItem.instanceId = sourceItem->instanceId;
    else
    {
        const auto instance = reserveWorldMpNum();
        if (!instance)
        {
            reject(InventoryPutError::PersistenceFailure);
            return;
        }
        destinationItem.instanceId = *instance;
    }
    appendOrMergeContainerItem(resultingDestination.items, destinationItem);
    normalizeContainerItems(resultingDestination.items);

    sourceItem->count -= request.requestedCount;
    if (sourceItem->count == 0)
        inventory.erase(sourceItem);
    const std::uint64_t resultingRevision = request.expectedInventoryRevision + 1;

    result.accepted = true;
    result.error = InventoryPutError::None;
    result.itemCount = request.requestedCount;
    result.inventoryRevision = resultingRevision;

    // InventoryTakeCommit is direction-neutral at the storage layer: it
    // compare-and-swaps one persistent container plus one character inventory
    // and journals a canonical request in the same SQLite transaction. Reuse
    // that proven primitive while keeping a distinct InventoryPut wire/API.
    InventoryTakeCommit commit;
    commit.accountId = c.dbAccountId;
    commit.characterId = c.dbCharacterId;
    commit.requestId = request.requestId;
    commit.requestHash = requestHash;
    commit.result.requestId = request.requestId;
    commit.result.accepted = true;
    commit.result.kind = actorDestination ? InventoryTakeKind::Corpse : InventoryTakeKind::Container;
    commit.result.source = request.destination;
    commit.result.itemRefId = request.itemRefId;
    commit.result.itemCharge = request.itemCharge;
    commit.result.itemCount = request.requestedCount;
    commit.result.inventoryRevision = resultingRevision;
    commit.expectedInventoryRevision = request.expectedInventoryRevision;
    commit.resultingInventoryRevision = resultingRevision;
    commit.inventory = inventory;
    commit.expectedSource = expectedDestination;
    commit.resultingSource = resultingDestination;

    InventoryTakeCommitResult committed;
    try
    {
        committed = mPlayerDb->commitInventoryTake(commit);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[InventoryPut] persistence failure request=" << request.requestId
                          << " player=" << c.name << " error=" << e.what();
        reject(InventoryPutError::PersistenceFailure);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::DuplicateRequestConflict)
    {
        reject(InventoryPutError::DuplicateConflict);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::StaleInventoryRevision)
    {
        sendAuthoritativeInventory(c);
        reject(InventoryPutError::StaleInventoryRevision);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::StaleSource)
    {
        reject(InventoryPutError::StaleDestination);
        return;
    }
    if (committed.status != InventoryTakeCommitStatus::Committed
        && committed.status != InventoryTakeCommitStatus::DuplicateRequest)
    {
        reject(InventoryPutError::PersistenceFailure);
        return;
    }

    result.replayed = committed.status == InventoryTakeCommitStatus::DuplicateRequest;
    result.itemCount = committed.result.itemCount;
    result.inventoryRevision = committed.result.inventoryRevision;
    if (!result.replayed)
    {
        destinationIt->second = resultingDestination;
        c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
        c.player.inventoryChanges.items = std::move(inventory);
        c.inventoryRevision = resultingRevision;
        c.player.inventoryChanges.revision = resultingRevision;
        c.restoredInventorySnapshot = c.player.inventoryChanges.items;
        c.hasRestoredInventorySnapshot = true;

        PacketContainer destinationDelta;
        destinationDelta.container.cellId = resultingDestination.cellId;
        destinationDelta.container.refId = resultingDestination.refId;
        destinationDelta.container.refNum = resultingDestination.refNum;
        destinationDelta.container.mpNum = resultingDestination.mpNum;
        destinationDelta.container.hasAuthority = true;
        destinationDelta.container.items.push_back(destinationItem);
        destinationDelta.mAction = static_cast<std::uint8_t>(ContainerAction::Add);
        broadcastToCell(canonicalPlayerCell, destinationDelta.encode());
        scheduleGeneratedDynamicRecordGc("inventory_put");
        syncLuaPlayerSnapshot();
    }
    sendAuthoritativeInventory(c);

    Log(Debug::Info) << "[InventoryPut] accepted request=" << request.requestId
                     << " destination=" << request.destination.refId
                     << " refNum=" << request.destination.refNum
                     << " mpNum=" << request.destination.mpNum
                     << " actorNetId=" << request.destination.actorInstanceId
                     << " item=" << request.itemRefId << " instanceId=" << request.itemInstanceId
                     << " count=" << result.itemCount << " replayed=" << result.replayed;
    sendResult();
}

// ---------------------------------------------------------------------------
void MPServer::handleBarterRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketBarterRequest packet;
    if (!packet.decode(data, size))
        return;

    const BarterRequest& request = packet.request;
    BarterResult result;
    result.requestId = request.requestId;
    result.balance = request.balance;
    for (const BarterLine& line : request.lines)
    {
        if (line.kind == BarterLineKind::Sell)
            ++result.sellLines;
        else
            ++result.buyLines;
    }
    auto sendResult = [&] {
        PacketBarterResult response;
        response.result = result;
        sendTo(c.conn, response.encode());
    };
    auto reject = [&](BarterError error) {
        result.accepted = false;
        result.error = error;
        result.inventoryRevision = c.inventoryRevision;
        sendResult();
    };

    const BarterError validation = validateBarterRequest(request);
    if (validation != BarterError::None || !mPlayerDb || !mContentRegistry
        || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        reject(validation != BarterError::None ? validation : BarterError::PersistenceFailure);
        return;
    }

    const std::string requestHash = crypto::sha256hex(canonicalBarterRequest(request));
    if (const auto stored = mPlayerDb->loadInventoryTake(c.dbAccountId, c.dbCharacterId, request.requestId))
    {
        if (stored->requestHash != requestHash)
            reject(BarterError::DuplicateConflict);
        else
        {
            result.accepted = true;
            result.replayed = true;
            result.error = BarterError::None;
            result.inventoryRevision = stored->result.inventoryRevision;
            result.merchantGold = std::max(0, stored->result.itemCharge);
            sendAuthoritativeInventory(c);
            sendResult();
        }
        return;
    }

    const std::string canonicalPlayerCell = makeCellKey(c.player.cell);
    if (request.merchant.cellId != canonicalPlayerCell)
    {
        reject(BarterError::WrongCell);
        return;
    }
    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* acceptedPlayer = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!acceptedPlayer || acceptedPlayer->snapshot.cellId != canonicalPlayerCell
        || acceptedPlayer->snapshot.migrationGeneration != 1
        || acceptedPlayer->snapshot.authorityGeneration != c.guid)
    {
        reject(BarterError::PlayerSnapshotUnavailable);
        return;
    }
    if (request.expectedInventoryRevision != c.inventoryRevision)
    {
        sendAuthoritativeInventory(c);
        reject(BarterError::StaleInventoryRevision);
        return;
    }

    const auto merchantKeyIt = mWorld.actorKeysByNetId.find(request.merchant.actorInstanceId);
    const auto merchantLocationIt = merchantKeyIt == mWorld.actorKeysByNetId.end()
        ? mWorld.actorLocations.end() : mWorld.actorLocations.find(merchantKeyIt->second);
    auto merchantCellIt = merchantLocationIt == mWorld.actorLocations.end()
        ? mWorld.actorCells.end() : mWorld.actorCells.find(merchantLocationIt->second);
    auto merchantIt = merchantCellIt == mWorld.actorCells.end() || merchantKeyIt == mWorld.actorKeysByNetId.end()
        ? std::unordered_map<std::string, ActorRegistryRecord>::iterator{}
        : merchantCellIt->second.actors.find(merchantKeyIt->second);
    if (merchantCellIt == mWorld.actorCells.end() || merchantKeyIt == mWorld.actorKeysByNetId.end()
        || merchantIt == merchantCellIt->second.actors.end())
    {
        reject(BarterError::StaleSource);
        return;
    }

    ActorRegistryRecord& merchantRecord = merchantIt->second;
    if (merchantLocationIt->second != canonicalPlayerCell
        || merchantRecord.actorNetId != request.merchant.actorInstanceId
        || merchantRecord.migrationGeneration != request.merchant.migrationGeneration
        || merchantRecord.actor.refId != request.merchant.refId
        || merchantRecord.actor.refNum != request.merchant.refNum
        || merchantRecord.actor.mpNum != request.merchant.mpNum
        || merchantRecord.actor.isDead
        || (merchantRecord.actor.mpNum == 0 && merchantRecord.actor.refNum == 0))
    {
        reject(BarterError::StaleSource);
        return;
    }

    const float mdx = acceptedPlayer->snapshot.position.pos[0] - merchantRecord.actor.position.pos[0];
    const float mdy = acceptedPlayer->snapshot.position.pos[1] - merchantRecord.actor.position.pos[1];
    const float mdz = acceptedPlayer->snapshot.position.pos[2] - merchantRecord.actor.position.pos[2];
    const float merchantDistanceSquared = mdx * mdx + mdy * mdy + mdz * mdz;
    if (!std::isfinite(merchantDistanceSquared)
        || merchantDistanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
    {
        Log(Debug::Warning) << "[Barter] out of range request=" << request.requestId
                            << " merchant=" << request.merchant.refId
                            << " distance=" << std::sqrt(std::max(0.f, merchantDistanceSquared))
                            << " limit=" << mDoorInteractionRadius;
        reject(BarterError::OutOfRange);
        return;
    }

    ESM::RefId merchantContentId = ESM::RefId::deserializeText(merchantRecord.actor.refId);
    if (merchantContentId.empty())
        merchantContentId = ESM::RefId::stringRefId(merchantRecord.actor.refId);
    const auto& contentStore = mContentRegistry->store();
    const ESM::NPC* merchantNpc = contentStore.get<ESM::NPC>().search(merchantContentId);
    const ESM::Creature* merchantCreature = merchantNpc == nullptr
        ? contentStore.get<ESM::Creature>().search(merchantContentId) : nullptr;
    int merchantServices = 0;
    if (merchantNpc != nullptr)
    {
        merchantServices = (merchantNpc->mFlags & ESM::NPC::Autocalc) && !merchantNpc->mClass.empty()
            ? contentStore.get<ESM::Class>().search(merchantNpc->mClass) != nullptr
                ? contentStore.get<ESM::Class>().find(merchantNpc->mClass)->mData.mServices
                : 0
            : merchantNpc->mAiData.mServices;
    }
    else if (merchantCreature != nullptr)
        merchantServices = merchantCreature->mAiData.mServices;
    if ((merchantServices & ESM::NPC::AllItems) == 0)
    {
        reject(BarterError::StaleSource);
        return;
    }

    const std::int32_t baseMerchantGold = std::max(0,
        merchantNpc != nullptr ? merchantNpc->mNpdt.mGold : merchantCreature->mData.mGold);
    const double currentGameHours = (static_cast<double>(mWorld.year) * 12.0 * 30.0
        + static_cast<double>(mWorld.month) * 30.0 + static_cast<double>(mWorld.day - 1)) * 24.0
        + static_cast<double>(mWorld.gameHour);
    const double barterGoldResetDelay = std::max(0.0,
        static_cast<double>(contentStore.get<ESM::GameSetting>()
            .find("fBarterGoldResetDelay")->mValue.getFloat()));
    const auto storedMerchantGold = mPlayerDb->loadMerchantGold(request.merchant.actorInstanceId);
    std::optional<BarterMerchantGoldState> storedGoldState;
    if (storedMerchantGold)
        storedGoldState = BarterMerchantGoldState{ storedMerchantGold->gold, storedMerchantGold->lastRestockTime };
    const BarterMerchantGoldResolution merchantGoldState = resolveBarterMerchantGold(
        baseMerchantGold, storedGoldState, currentGameHours, barterGoldResetDelay);
    const std::int32_t authoritativeMerchantGold = merchantGoldState.authoritativeGold;
    result.merchantGold = authoritativeMerchantGold;

    const std::uint32_t merchantBootstrapAuthority
        = isActorAuthorityLeaseValid(merchantRecord, canonicalPlayerCell, nowMs)
        ? merchantRecord.actorAuthorityGuid : merchantCellIt->second.authorityGuid;
    const std::string merchantContainerKey = makeContainerKey(canonicalPlayerCell,
        merchantRecord.actor.refId, merchantRecord.actor.refNum, merchantRecord.actor.mpNum);

    auto requestBootstrap = [&](const InventorySourceIdentity& identity, std::uint32_t authorityGuid) {
        if (authorityGuid == 0)
            return;
        const auto authority = std::find_if(mClients.begin(), mClients.end(),
            [&](const auto& entry) { return entry.second.guid == authorityGuid; });
        if (authority == mClients.end())
            return;
        PacketContainer bootstrap;
        bootstrap.container.cellId = identity.cellId;
        bootstrap.container.refId = identity.refId;
        bootstrap.container.refNum = identity.refNum;
        bootstrap.container.mpNum = identity.mpNum;
        bootstrap.mAction = static_cast<std::uint8_t>(ContainerAction::BootstrapRequest);
        sendTo(authority->second.conn, bootstrap.encode());
    };

    std::unordered_set<std::string> requestedBootstrapKeys;
    auto markMissingSource = [&](const std::string& key, const InventorySourceIdentity& identity,
                                 std::uint32_t authorityGuid) {
        if (requestedBootstrapKeys.insert(key).second)
        {
            result.missingSources.push_back(identity);
            requestBootstrap(identity, authorityGuid);
        }
    };

    struct WorkingContainer
    {
        ContainerRecord expected;
        ContainerRecord resulting;
    };
    std::map<std::string, WorkingContainer> working;
    auto ensureWorking = [&](const std::string& key, const InventorySourceIdentity& identity,
                             std::uint32_t bootstrapAuthority) -> WorkingContainer* {
        auto found = working.find(key);
        if (found != working.end())
            return &found->second;
        auto authoritative = mWorld.containers.find(key);
        if (authoritative == mWorld.containers.end() || !authoritative->second.hasAuthority)
        {
            markMissingSource(key, identity, bootstrapAuthority);
            return nullptr;
        }
        ContainerRecord expected = authoritative->second;
        normalizeContainerItems(expected.items);
        auto [inserted, ok] = working.emplace(key, WorkingContainer{ expected, expected });
        (void)ok;
        return &inserted->second;
    };

    const bool hasSales = std::any_of(request.lines.begin(), request.lines.end(),
        [](const BarterLine& line) { return line.kind == BarterLineKind::Sell; });
    if (hasSales)
        ensureWorking(merchantContainerKey, request.merchant, merchantBootstrapAuthority);

    // Resolve and request every persistent buy source before any semantic
    // inventory construction. One response tells the client the complete set
    // it must await, avoiding partial/repeated one-container retries.
    for (const BarterLine& line : request.lines)
    {
        if (line.kind == BarterLineKind::Sell || line.kind == BarterLineKind::BuyWorldItem)
            continue;
        std::string sourceKey;
        std::uint32_t bootstrapAuthority = 0;
        if (line.source.actorInstanceId != 0)
        {
            if (line.source.actorInstanceId != request.merchant.actorInstanceId
                || line.source.cellId != request.merchant.cellId
                || line.source.refId != request.merchant.refId
                || line.source.refNum != request.merchant.refNum
                || line.source.mpNum != request.merchant.mpNum
                || line.source.migrationGeneration != request.merchant.migrationGeneration)
            {
                reject(BarterError::StaleSource);
                return;
            }
            sourceKey = merchantContainerKey;
            bootstrapAuthority = merchantBootstrapAuthority;
        }
        else
        {
            if (line.source.cellId != canonicalPlayerCell || line.source.mpNum != 0)
            {
                reject(line.source.cellId != canonicalPlayerCell
                    ? BarterError::WrongCell : BarterError::StaleSource);
                return;
            }
            const auto placed = mContentRegistry->findContainerReference(
                line.source.cellId, line.source.refId, line.source.refNum);
            if (!placed || !placed->enabled || placed->ownerId.empty()
                || lowerAscii(placed->ownerId) != lowerAscii(merchantRecord.actor.refId))
            {
                reject(BarterError::StaleSource);
                return;
            }
            sourceKey = makeContainerKey(line.source.cellId, line.source.refId,
                line.source.refNum, line.source.mpNum);
            bootstrapAuthority = merchantCellIt->second.authorityGuid;
        }
        ensureWorking(sourceKey, line.source, bootstrapAuthority);
    }
    if (!result.missingSources.empty())
    {
        reject(BarterError::SourceUnavailable);
        return;
    }

    auto isRestockingTemplate = [&](const InventorySourceIdentity& source,
                                    std::string_view itemRefId, int count) {
        const auto matches = [&](const ESM::InventoryList& inventory) {
            return std::any_of(inventory.mList.begin(), inventory.mList.end(), [&](const ESM::ContItem& item) {
                if (item.mCount >= 0 || std::abs(static_cast<std::int64_t>(item.mCount)) < count)
                    return false;
                if (lowerAscii(item.mItem.serializeText()) == lowerAscii(std::string(itemRefId)))
                    return true;
                const ESM::ItemLevList* list = contentStore.get<ESM::ItemLevList>().search(item.mItem);
                return list != nullptr && isEligibleBarterRestockDescendant(*list, itemRefId,
                    std::max(1, c.player.level), [&](std::string_view listId) {
                        ESM::RefId id = ESM::RefId::deserializeText(listId);
                        if (id.empty())
                            id = ESM::RefId::stringRefId(listId);
                        return contentStore.get<ESM::ItemLevList>().search(id);
                    });
            });
        };
        if (source.actorInstanceId != 0)
        {
            if (source.actorInstanceId != request.merchant.actorInstanceId)
                return false;
            if (merchantNpc)
                return matches(merchantNpc->mInventory);
            return merchantCreature && matches(merchantCreature->mInventory);
        }
        ESM::RefId sourceId = ESM::RefId::deserializeText(source.refId);
        if (sourceId.empty())
            sourceId = ESM::RefId::stringRefId(source.refId);
        const ESM::Container* container = contentStore.get<ESM::Container>().search(sourceId);
        return container != nullptr && matches(container->mInventory);
    };

    const std::vector<Item> initialInventory = c.player.inventoryChanges.items;
    std::vector<Item> inventory = initialInventory;
    std::unordered_map<std::uint32_t, std::int64_t> remainingSaleCounts;
    for (const Item& item : initialInventory)
    {
        if (item.instanceId != 0 && item.count > 0)
            remainingSaleCounts[item.instanceId] += item.count;
    }
    std::vector<WorldItemMutation> worldItemMutations;
    std::int64_t minimumBuyCost = 0;
    std::int64_t maximumSellRevenue = 0;

    for (const BarterLine& line : request.lines)
    {
        ESM::RefId contentId = ESM::RefId::deserializeText(line.itemRefId);
        if (contentId.empty())
            contentId = ESM::RefId::stringRefId(line.itemRefId);
        MWWorld::ManualRef contentRef(contentStore, contentId, line.count);
        MWWorld::Ptr contentPtr = contentRef.getPtr();
        if (contentPtr.isEmpty() || contentPtr.getClass().isGold(contentPtr)
            || !contentPtr.getClass().canSell(contentPtr, merchantServices))
        {
            reject(BarterError::ItemUnavailable);
            return;
        }

        float effectiveUnitValue = static_cast<float>(contentPtr.getClass().getValue(contentPtr));
        if (contentPtr.getClass().hasItemHealth(contentPtr))
        {
            if (line.itemCharge >= 0)
                contentPtr.getCellRef().setCharge(line.itemCharge);
            effectiveUnitValue *= contentPtr.getClass().getItemNormalizedHealth(contentPtr);
        }
        const double effectiveValue = static_cast<double>(effectiveUnitValue) * line.count;
        const double boundedPrice = std::max(1.0, 0.75 * effectiveValue);
        if (!std::isfinite(effectiveValue) || effectiveValue < 0.0
            || boundedPrice > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        {
            reject(BarterError::InvalidBalance);
            return;
        }
        const std::int64_t cappedValue = static_cast<std::int64_t>(boundedPrice);

        if (line.kind == BarterLineKind::BuyWorldItem)
        {
            ServerContentRegistry::PlacedItemReference placedItem;
            if (line.worldObject.cellId != canonicalPlayerCell)
            {
                reject(BarterError::WrongCell);
                return;
            }
            if (line.worldObject.kind == PlacedObjectKind::ContentReference)
            {
                const auto found = mContentRegistry->findPlacedItemReference(line.worldObject);
                if (!found)
                {
                    reject(BarterError::WorldItemUnavailable);
                    return;
                }
                placedItem = *found;
            }
            else
            {
                // Server-placed objects currently carry no authoritative owner
                // metadata. They cannot be proved as merchant stock, so fail
                // closed until that lifecycle gains an ownership field.
                reject(BarterError::StaleSource);
                return;
            }

            const auto taken = mWorld.takenItemReferences.find(line.worldObject.cellId);
            const bool alreadyTaken = taken != mWorld.takenItemReferences.end()
                && std::find(taken->second.begin(), taken->second.end(), line.worldObject) != taken->second.end();
            std::int32_t currentWorldCount = placedItem.worldCount;
            const auto countOverride = mWorld.worldItemCountOverrides.find(makeWorldItemKey(line.worldObject));
            if (countOverride != mWorld.worldItemCountOverrides.end())
                currentWorldCount = countOverride->second.resultingWorldCount;
            const float wdx = merchantRecord.actor.position.pos[0] - placedItem.position.pos[0];
            const float wdy = merchantRecord.actor.position.pos[1] - placedItem.position.pos[1];
            const float wdz = merchantRecord.actor.position.pos[2] - placedItem.position.pos[2];
            const float worldDistanceSquared = wdx * wdx + wdy * wdy + wdz * wdz;
            if (alreadyTaken || !placedItem.enabled || placedItem.gold
                || currentWorldCount <= 0 || line.count <= 0 || line.count > currentWorldCount
                || placedItem.charge != line.itemCharge
                || std::abs(placedItem.enchantmentCharge - line.itemEnchantmentCharge) >= 0.001f
                || placedItem.soul != line.itemSoul || placedItem.ownerId.empty()
                || lowerAscii(placedItem.ownerId) != lowerAscii(merchantRecord.actor.refId)
                || !std::isfinite(worldDistanceSquared)
                || worldDistanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
            {
                reject(BarterError::WorldItemUnavailable);
                return;
            }

            Item added;
            added.refId = placedItem.identity.refId;
            added.count = line.count;
            added.charge = placedItem.charge;
            added.enchantmentCharge = placedItem.enchantmentCharge;
            added.soul = placedItem.soul;
            auto destinationStack = std::find_if(inventory.begin(), inventory.end(),
                [&](const Item& item) { return sameItemIdentity(item, added); });
            if (destinationStack != inventory.end())
            {
                if (destinationStack->count > std::numeric_limits<std::int32_t>::max() - added.count)
                {
                    reject(BarterError::InvalidCount);
                    return;
                }
                destinationStack->count += added.count;
            }
            else
            {
                const auto instance = reserveWorldMpNum();
                if (!instance)
                {
                    reject(BarterError::PersistenceFailure);
                    return;
                }
                added.instanceId = *instance;
                inventory.push_back(std::move(added));
            }
            worldItemMutations.push_back({ line.worldObject, currentWorldCount, currentWorldCount - line.count });
            minimumBuyCost += cappedValue;
            continue;
        }

        if (line.kind == BarterLineKind::Sell)
        {
            const auto originalSource = std::find_if(initialInventory.begin(), initialInventory.end(),
                [&](const Item& item) {
                return item.instanceId == line.itemInstanceId
                    && lowerAscii(item.refId) == lowerAscii(line.itemRefId)
                    && item.charge == line.itemCharge
                    && std::abs(item.enchantmentCharge - line.itemEnchantmentCharge) < 0.001f
                    && item.soul == line.itemSoul && item.count >= line.count;
            });
            auto remaining = remainingSaleCounts.find(line.itemInstanceId);
            if (originalSource == initialInventory.end() || remaining == remainingSaleCounts.end()
                || remaining->second < line.count || !isAuthoritativeRecordReference(originalSource->refId))
            {
                reject(BarterError::ItemUnavailable);
                return;
            }
            remaining->second -= line.count;

            // Apply the sale to the working inventory only after proving it
            // against the immutable pre-offer snapshot. This makes offer line
            // order irrelevant and prevents a client from buying an item and
            // selling those newly acquired units in the same transaction.
            auto sourceItem = std::find_if(inventory.begin(), inventory.end(), [&](const Item& item) {
                return item.instanceId == line.itemInstanceId
                    && lowerAscii(item.refId) == lowerAscii(line.itemRefId)
                    && item.charge == line.itemCharge
                    && std::abs(item.enchantmentCharge - line.itemEnchantmentCharge) < 0.001f
                    && item.soul == line.itemSoul && item.count >= line.count;
            });
            if (sourceItem == inventory.end())
            {
                reject(BarterError::ItemUnavailable);
                return;
            }

            WorkingContainer* merchantInventory = ensureWorking(
                merchantContainerKey, request.merchant, merchantBootstrapAuthority);
            if (!merchantInventory)
            {
                reject(BarterError::SourceUnavailable);
                return;
            }

            ContainerItem sold;
            sold.refId = sourceItem->refId;
            sold.count = line.count;
            sold.charge = sourceItem->charge;
            sold.enchantmentCharge = sourceItem->enchantmentCharge;
            sold.soul = sourceItem->soul;
            if (line.count == sourceItem->count)
                sold.instanceId = sourceItem->instanceId;
            else
            {
                const auto instance = reserveWorldMpNum();
                if (!instance)
                {
                    reject(BarterError::PersistenceFailure);
                    return;
                }
                sold.instanceId = *instance;
            }
            appendOrMergeContainerItem(merchantInventory->resulting.items, sold);
            normalizeContainerItems(merchantInventory->resulting.items);

            sourceItem->count -= line.count;
            if (sourceItem->count == 0)
                inventory.erase(sourceItem);
            maximumSellRevenue += cappedValue;
            continue;
        }

        std::string sourceKey;
        std::uint32_t bootstrapAuthority = 0;
        if (line.source.actorInstanceId != 0)
        {
            if (line.source.actorInstanceId != request.merchant.actorInstanceId
                || line.source.cellId != request.merchant.cellId
                || line.source.refId != request.merchant.refId
                || line.source.refNum != request.merchant.refNum
                || line.source.mpNum != request.merchant.mpNum
                || line.source.migrationGeneration != request.merchant.migrationGeneration)
            {
                reject(BarterError::StaleSource);
                return;
            }
            sourceKey = merchantContainerKey;
            bootstrapAuthority = merchantBootstrapAuthority;
        }
        else
        {
            if (line.source.cellId != canonicalPlayerCell || line.source.mpNum != 0)
            {
                reject(line.source.cellId != canonicalPlayerCell ? BarterError::WrongCell : BarterError::StaleSource);
                return;
            }
            const auto placed = mContentRegistry->findContainerReference(
                line.source.cellId, line.source.refId, line.source.refNum);
            if (!placed || !placed->enabled
                || placed->ownerId.empty()
                || lowerAscii(placed->ownerId) != lowerAscii(merchantRecord.actor.refId))
            {
                reject(BarterError::StaleSource);
                return;
            }
            sourceKey = makeContainerKey(line.source.cellId, line.source.refId,
                line.source.refNum, line.source.mpNum);
            bootstrapAuthority = merchantCellIt->second.authorityGuid;
        }

        WorkingContainer* source = ensureWorking(sourceKey, line.source, bootstrapAuthority);
        if (!source)
        {
            reject(BarterError::SourceUnavailable);
            return;
        }
        auto sourceItem = std::find_if(source->resulting.items.begin(), source->resulting.items.end(),
            [&](const ContainerItem& item) {
                return lowerAscii(item.refId) == lowerAscii(line.itemRefId)
                    && item.instanceId == line.itemInstanceId
                    && item.charge == line.itemCharge
                    && std::abs(item.enchantmentCharge - line.itemEnchantmentCharge) < 0.001f
                    && item.soul == line.itemSoul && item.count >= line.count;
            });
        if (sourceItem == source->resulting.items.end())
        {
            reject(BarterError::ItemUnavailable);
            return;
        }
        if (line.kind == BarterLineKind::BuyRestocking
            && (!sourceItem->restocking
                || !isRestockingTemplate(line.source, line.itemRefId, line.count)))
        {
            Log(Debug::Warning) << "[Barter] rejected unproven restocking line request=" << request.requestId
                                << " source=" << line.source.refId << " item=" << line.itemRefId;
            reject(BarterError::StaleSource);
            return;
        }
        if (line.kind == BarterLineKind::BuyFinite && sourceItem->restocking)
        {
            reject(BarterError::StaleSource);
            return;
        }

        Item added;
        added.refId = sourceItem->refId;
        added.count = line.count;
        added.charge = sourceItem->charge;
        added.enchantmentCharge = sourceItem->enchantmentCharge;
        added.soul = sourceItem->soul;
        auto destinationStack = std::find_if(inventory.begin(), inventory.end(),
            [&](const Item& item) { return sameItemIdentity(item, added); });
        if (destinationStack != inventory.end())
        {
            if (destinationStack->count > std::numeric_limits<std::int32_t>::max() - added.count)
            {
                reject(BarterError::InvalidCount);
                return;
            }
            destinationStack->count += added.count;
            added.instanceId = destinationStack->instanceId;
        }
        else
        {
            const auto instance = reserveWorldMpNum();
            if (!instance)
            {
                reject(BarterError::PersistenceFailure);
                return;
            }
            added.instanceId = *instance;
            inventory.push_back(added);
        }

        if (line.kind == BarterLineKind::BuyFinite)
        {
            sourceItem->count -= line.count;
            if (sourceItem->count == 0)
                source->resulting.items.erase(sourceItem);
            normalizeContainerItems(source->resulting.items);
        }
        minimumBuyCost += cappedValue;
    }

    const std::int64_t maximumSafeBalance = maximumSellRevenue - minimumBuyCost;
    if (static_cast<std::int64_t>(request.balance) > maximumSafeBalance)
    {
        Log(Debug::Warning) << "[Barter] rejected unsafe balance request=" << request.requestId
                            << " balance=" << request.balance
                            << " maximum=" << maximumSafeBalance
                            << " buyFloor=" << minimumBuyCost
                            << " sellCap=" << maximumSellRevenue;
        reject(BarterError::InvalidBalance);
        return;
    }
    if (request.balance > 0 && request.balance > authoritativeMerchantGold)
    {
        reject(BarterError::MerchantGoldInsufficient);
        return;
    }

    const std::int64_t resultingMerchantGold64
        = static_cast<std::int64_t>(authoritativeMerchantGold) - request.balance;
    if (resultingMerchantGold64 < 0
        || resultingMerchantGold64 > std::numeric_limits<std::int32_t>::max())
    {
        reject(BarterError::InvalidBalance);
        return;
    }
    const std::int32_t resultingMerchantGold = static_cast<std::int32_t>(resultingMerchantGold64);
    result.merchantGold = resultingMerchantGold;

    if (request.balance < 0)
    {
        std::int64_t remainingGold = -static_cast<std::int64_t>(request.balance);
        std::int64_t availableGold = 0;
        for (const Item& item : inventory)
        {
            if (lowerAscii(item.refId) == "gold_001")
                availableGold += item.count;
        }
        if (availableGold < remainingGold)
        {
            reject(BarterError::InsufficientGold);
            return;
        }
        for (auto it = inventory.begin(); it != inventory.end() && remainingGold > 0;)
        {
            if (lowerAscii(it->refId) != "gold_001")
            {
                ++it;
                continue;
            }
            const int removed = static_cast<int>(std::min<std::int64_t>(it->count, remainingGold));
            it->count -= removed;
            remainingGold -= removed;
            if (it->count <= 0)
                it = inventory.erase(it);
            else
                ++it;
        }
    }
    else if (request.balance > 0)
    {
        Item gold;
        gold.refId = "gold_001";
        gold.count = request.balance;
        gold.charge = -1;
        auto existingGold = std::find_if(inventory.begin(), inventory.end(),
            [&](const Item& item) { return lowerAscii(item.refId) == "gold_001"; });
        if (existingGold != inventory.end())
        {
            if (existingGold->count > std::numeric_limits<std::int32_t>::max() - request.balance)
            {
                reject(BarterError::InvalidCount);
                return;
            }
            existingGold->count += request.balance;
        }
        else
        {
            const auto instance = reserveWorldMpNum();
            if (!instance)
            {
                reject(BarterError::PersistenceFailure);
                return;
            }
            gold.instanceId = *instance;
            inventory.push_back(std::move(gold));
        }
    }

    if (request.expectedInventoryRevision == std::numeric_limits<std::uint64_t>::max())
    {
        reject(BarterError::StaleInventoryRevision);
        return;
    }
    const std::uint64_t resultingRevision = request.expectedInventoryRevision + 1;
    InventoryTakeCommit commit;
    commit.accountId = c.dbAccountId;
    commit.characterId = c.dbCharacterId;
    commit.requestId = request.requestId;
    commit.requestHash = requestHash;
    commit.expectedInventoryRevision = request.expectedInventoryRevision;
    commit.resultingInventoryRevision = resultingRevision;
    commit.inventory = inventory;
    commit.result.requestId = request.requestId;
    commit.result.accepted = true;
    commit.result.kind = InventoryTakeKind::Barter;
    commit.result.source = request.merchant;
    commit.result.itemRefId = "__barter_batch__";
    // The shared durable take journal stores the canonical resulting merchant
    // gold in itemCharge for exact barter replay.
    commit.result.itemCharge = resultingMerchantGold;
    commit.result.itemCount = result.buyLines;
    commit.result.inventoryRevision = resultingRevision;
    commit.worldItemMutations = worldItemMutations;
    commit.merchantGoldMutation = MerchantGoldMutation{ request.merchant.actorInstanceId,
        merchantRecord.actor.refId, merchantGoldState.expectedGold, resultingMerchantGold,
        merchantGoldState.expectedRestockTime, merchantGoldState.resultingRestockTime };
    for (const auto& [key, source] : working)
    {
        (void)key;
        // Even unchanged restocking sources participate in the transaction's
        // compare-and-swap set. Their concrete authoritative snapshot proves
        // the materialized item while the negative ESM template remains intact.
        commit.containerMutations.push_back({ source.expected, source.resulting });
    }

    InventoryTakeCommitResult committed;
    try
    {
        committed = mPlayerDb->commitInventoryTake(commit);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Barter] persistence failure request=" << request.requestId
                          << " player=" << c.name << " error=" << e.what();
        reject(BarterError::PersistenceFailure);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::DuplicateRequestConflict)
    {
        reject(BarterError::DuplicateConflict);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::StaleInventoryRevision)
    {
        sendAuthoritativeInventory(c);
        reject(BarterError::StaleInventoryRevision);
        return;
    }
    if (committed.status == InventoryTakeCommitStatus::StaleSource)
    {
        reject(BarterError::StaleSource);
        return;
    }
    if (committed.status != InventoryTakeCommitStatus::Committed
        && committed.status != InventoryTakeCommitStatus::DuplicateRequest)
    {
        reject(BarterError::PersistenceFailure);
        return;
    }

    result.accepted = true;
    result.error = BarterError::None;
    result.replayed = committed.status == InventoryTakeCommitStatus::DuplicateRequest;
    result.inventoryRevision = committed.result.inventoryRevision;
    result.merchantGold = std::max(0, committed.result.itemCharge);
    if (!result.replayed)
    {
        c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
        c.player.inventoryChanges.items = std::move(inventory);
        c.inventoryRevision = resultingRevision;
        c.player.inventoryChanges.revision = resultingRevision;
        c.restoredInventorySnapshot = c.player.inventoryChanges.items;
        c.hasRestoredInventorySnapshot = true;
        for (const ContainerMutation& mutation : commit.containerMutations)
        {
            if (mutation.expected.items == mutation.resulting.items)
                continue;
            const std::string key = makeContainerKey(mutation.resulting.cellId, mutation.resulting.refId,
                mutation.resulting.refNum, mutation.resulting.mpNum);
            mWorld.containers[key] = mutation.resulting;
            PacketContainer update;
            update.container = mutation.resulting;
            update.container.hasAuthority = true;
            update.mAction = static_cast<std::uint8_t>(ContainerAction::Set);
            broadcastToCell(canonicalPlayerCell, update.encode());
        }
        for (const WorldItemMutation& mutation : commit.worldItemMutations)
        {
            const std::string worldKey = makeWorldItemKey(mutation.object);
            if (mutation.resultingWorldCount > 0)
            {
                if (mutation.object.kind == PlacedObjectKind::ServerPlaced)
                {
                    auto placed = mWorld.placedObjects.find(mutation.object.cellId);
                    if (placed != mWorld.placedObjects.end())
                    {
                        const auto object = std::find_if(placed->second.begin(), placed->second.end(),
                            [&](const PlacedObject& value) { return value.mpNum == mutation.object.mpNum; });
                        if (object != placed->second.end())
                            object->count = mutation.resultingWorldCount;
                    }
                }
                else
                    mWorld.worldItemCountOverrides[worldKey] = mutation;

                PacketObjectCount update;
                update.object = mutation.object;
                update.count = mutation.resultingWorldCount;
                broadcastToCell(mutation.object.cellId, update.encode());
                continue;
            }

            mWorld.worldItemCountOverrides.erase(worldKey);
            mWorld.takenItemReferences[mutation.object.cellId].push_back(mutation.object);
            if (mutation.object.kind == PlacedObjectKind::ServerPlaced)
                removePlacedObjectAuthoritative(mutation.object.mpNum, mutation.object.cellId);
            else
            {
                PacketObjectDelete deletion;
                deletion.cellId = mutation.object.cellId;
                deletion.refId = mutation.object.refId;
                deletion.refNum = mutation.object.refIndex;
                deletion.refContentFile = mutation.object.refContentFile;
                broadcastToCell(mutation.object.cellId, deletion.encode());
            }
        }
        scheduleGeneratedDynamicRecordGc("barter_transaction");
        syncLuaPlayerSnapshot();
    }
    sendAuthoritativeInventory(c);
    Log(Debug::Info) << "[Barter] accepted request=" << request.requestId
                     << " merchant=" << request.merchant.refId
                     << " lines=" << request.lines.size()
                     << " buys=" << result.buyLines << " sells=" << result.sellLines
                     << " balance=" << request.balance
                     << " merchantGold=" << result.merchantGold
                     << " containers=" << commit.containerMutations.size()
                     << " worldItems=" << commit.worldItemMutations.size()
                     << " revision=" << result.inventoryRevision
                     << " replayed=" << result.replayed;
    sendResult();
}

// ---------------------------------------------------------------------------
void MPServer::handleCrimeInteractionRequest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketCrimeInteraction packet;
    if (!packet.decode(data, size) || !mPlayerDb || !mContentRegistry || !mObservationService
        || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
        return;
    const CrimeInteractionRequest& request = packet.request;
    const std::string canonicalPlayerCell = makeCellKey(c.player.cell);
    if (request.cellId != canonicalPlayerCell)
        return;
    const std::string eventId = "crime-interaction:" + std::to_string(c.dbAccountId) + ":"
        + std::to_string(c.dbCharacterId) + ":" + request.requestId;
    const std::string requestHash = crypto::sha256hex(canonicalCrimeInteractionRequest(request));
    const std::string semanticSource = "authoritative_unlock:" + requestHash;
    if (const auto existing
        = mPlayerDb->loadSemanticRequest("crime-event", c.dbAccountId, c.dbCharacterId, eventId))
    {
        if (existing->source != semanticSource)
        {
            Log(Debug::Warning) << "[CrimeInteraction] conflicting duplicate request="
                                << request.requestId << " player=" << c.name;
            return;
        }
        c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
        c.player.bounty = c.player.crimeState.bounty;
        sendAuthoritativeCrimeState(c);
        Log(Debug::Info) << "[CrimeInteraction] replay request=" << request.requestId
                         << " player=" << c.name;
        return;
    }

    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* acceptedPlayer = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    if (!acceptedPlayer || acceptedPlayer->snapshot.cellId != canonicalPlayerCell
        || acceptedPlayer->snapshot.migrationGeneration != 1
        || acceptedPlayer->snapshot.authorityGeneration != c.guid)
        return;

    const auto target = mContentRegistry->findCrimeInteractionReference(
        request.cellId, request.refId, request.refNum, request.refContentFile);
    if (!target || !target->enabled
        || request.kind != CrimeInteractionKind::UnlockAttempt
        || (target->lockLevel == 0 && !target->locked && !target->trapped))
        return;

    const float dx = acceptedPlayer->snapshot.position.pos[0] - target->position.pos[0];
    const float dy = acceptedPlayer->snapshot.position.pos[1] - target->position.pos[1];
    const float dz = acceptedPlayer->snapshot.position.pos[2] - target->position.pos[2];
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distanceSquared)
        || distanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
        return;

    bool factionAllowsUse = target->factionId.empty();
    if (!target->factionId.empty())
    {
        const std::string wanted = lowerAscii(target->factionId);
        const auto membership = std::find_if(c.player.factionState.factions.begin(),
            c.player.factionState.factions.end(), [&](const PlayerFactionEntry& entry) {
                return lowerAscii(entry.factionId) == wanted;
            });
        factionAllowsUse = membership != c.player.factionState.factions.end()
            && !membership->expelled && membership->rank >= target->factionRank;
    }
    const bool ownerAllowsUse = target->ownerId.empty() || lowerAscii(target->ownerId) == "player";
    const bool trespass = !target->ownershipGlobalAllowsUse && (!ownerAllowsUse || !factionAllowsUse);
    if (!trespass)
    {
        sendAuthoritativeCrimeState(c);
        return;
    }

    CrimeWitnessBuildRequest witnessRequest;
    witnessRequest.eventCell = c.player.cell;
    witnessRequest.offender = makeLiveObservationSnapshot(*acceptedPlayer, playerBootWeight(c));
    witnessRequest.alarmRadius = mObservationAlarmRadius;
    witnessRequest.observedAtMs = nowMs;
    witnessRequest.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
    CrimeWitnessBuildResult witnesses = buildLiveCrimeWitnesses(witnessRequest);

    CrimeIntent intent;
    intent.eventId = eventId;
    intent.source = semanticSource;
    intent.type = CrimeType::Trespass;
    intent.cellId = canonicalPlayerCell;
    intent.offender = witnessRequest.offender;
    intent.observedAtMs = nowMs;
    intent.maximumSnapshotAgeMs = MaximumSnapshotAgeMs;
    for (const std::string& cellId : witnesses.candidateCellIds)
    {
        const std::uint64_t generation = mCollisionWorld ? mCollisionWorld->cellGeneration(cellId) : 0;
        if (generation != 0)
            intent.collisionGenerations.push_back({ cellId, generation });
    }

    const auto gmstInt = [&](std::string_view id) {
        return mContentRegistry->store().get<ESM::GameSetting>().find(id)->mValue.getInteger();
    };
    CrimePolicy policy;
    policy.alarmRadius = mObservationAlarmRadius;
    policy.theftBountyMultiplier = mContentRegistry->store().get<ESM::GameSetting>()
        .find("fCrimeStealing")->mValue.getFloat();
    policy.pickpocketBounty = gmstInt("iCrimePickPocket");
    policy.trespassBounty = gmstInt("iCrimeTresspass");
    policy.assaultBounty = gmstInt("iCrimeAttack");
    policy.murderBounty = gmstInt("iCrimeKilling");
    CrimeService crime(*mPlayerDb);
    CrimeSemanticService semantics(*mPlayerDb, crime, *mObservationService, policy);
    const CrimeSemanticService::Context context { c.dbAccountId, c.dbCharacterId, c.guid };
    const CrimeSemanticService::Outcome outcome
        = semantics.evaluate(intent, std::move(witnesses.witnesses), context);
    c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
    c.player.bounty = c.player.crimeState.bounty;
    sendAuthoritativeCrimeState(c);
    if (!outcome.replayed)
        dispatchCrimeReactions(c, outcome.result);
    syncLuaPlayerSnapshot();
    Log(outcome.result.accepted ? Debug::Info : Debug::Warning)
        << "[CrimeInteraction] eventId=" << intent.eventId << " player=" << c.name
        << " target=" << request.refId << " seen=" << outcome.result.crimeSeen
        << " bountyDelta=" << outcome.result.bountyDelta << " replayed=" << outcome.replayed;
}

// ---------------------------------------------------------------------------
void MPServer::handleGuardArrest(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketGuardArrest packet;
    if (!packet.decode(data, size))
        return;

    if (packet.mode == PacketGuardArrest::Mode::Reach)
    {
        const GuardArrestReach& reach = packet.reach;
        if (!mGuardArrestDialogueEnabled || !mPlayerDb || !mContentRegistry || !c.charSelectComplete)
            return;

        ConnectedClient* offender = findClientByGuid(reach.offenderGuid);
        if (!offender || !offender->charSelectComplete || offender->dbCharacterId <= 0
            || offender->player.isDead || makeCellKey(offender->player.cell) != reach.cellId
            || offender->player.crimeState.bounty <= 0)
            return;

        const auto keyIt = mWorld.actorKeysByNetId.find(reach.actorNetId);
        if (keyIt == mWorld.actorKeysByNetId.end())
            return;
        const auto locationIt = mWorld.actorLocations.find(keyIt->second);
        if (locationIt == mWorld.actorLocations.end() || locationIt->second != reach.cellId)
            return;
        auto cellIt = mWorld.actorCells.find(reach.cellId);
        if (cellIt == mWorld.actorCells.end())
            return;
        auto actorIt = cellIt->second.actors.find(keyIt->second);
        if (actorIt == cellIt->second.actors.end())
            return;

        ActorRegistryRecord& guardRecord = actorIt->second;
        if (guardRecord.actorNetId != reach.actorNetId
            || guardRecord.migrationGeneration != reach.migrationGeneration
            || guardRecord.actor.isDead || !isAllowedActorSender(c, guardRecord, reach.cellId))
            return;

        const ESM::NPC* guardNpc = mContentRegistry->store().get<ESM::NPC>().search(
            ESM::RefId::stringRefId(guardRecord.actor.refId));
        if (!guardNpc || guardNpc->mClass != "guard")
            return;

        if (guardRecord.crimePursuitCharacterId != offender->dbCharacterId
            || guardRecord.crimePursuitLastGuid != offender->guid
            || guardRecord.crimeEnforcementState != CrimeEnforcementState::Arrest)
            return;

        // Only one arrest offer may be active for an offender in a cell, and
        // an already-hostile guard suppresses fresh arrest offers there.
        for (const auto& [otherActorKey, otherRecord] : cellIt->second.actors)
        {
            (void)otherActorKey;
            if (otherRecord.crimePursuitCharacterId != offender->dbCharacterId
                || otherRecord.actor.isDead || otherRecord.migrationGeneration == 0)
                continue;
            if (otherRecord.crimeEnforcementState == CrimeEnforcementState::ArrestPending
                || otherRecord.crimeEnforcementState == CrimeEnforcementState::Combat)
                return;
        }

        constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
        const std::uint64_t nowMs = currentServerTimeMs();
        const AcceptedMechanicsSnapshot* offenderSnapshot = mMechanicsSnapshots.findFresh(
            { MechanicsSubjectKind::Player, offender->guid, 0 }, nowMs, MaximumSnapshotAgeMs);
        const AcceptedMechanicsSnapshot* guardSnapshot = mMechanicsSnapshots.findFresh(
            { MechanicsSubjectKind::Npc, 0, reach.actorNetId }, nowMs, MaximumSnapshotAgeMs);
        if (!offenderSnapshot || !guardSnapshot
            || offenderSnapshot->snapshot.cellId != reach.cellId
            || guardSnapshot->snapshot.cellId != reach.cellId
            || offenderSnapshot->snapshot.migrationGeneration != 1
            || offenderSnapshot->snapshot.authorityGeneration != offender->guid
            || guardSnapshot->snapshot.migrationGeneration != reach.migrationGeneration)
            return;

        const float dx = offenderSnapshot->snapshot.position.pos[0] - guardSnapshot->snapshot.position.pos[0];
        const float dy = offenderSnapshot->snapshot.position.pos[1] - guardSnapshot->snapshot.position.pos[1];
        const float dz = offenderSnapshot->snapshot.position.pos[2] - guardSnapshot->snapshot.position.pos[2];
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distanceSquared)
            || distanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
            return;

        guardRecord.crimeEnforcementState = CrimeEnforcementState::ArrestPending;
        guardRecord.crimePursuitReassertArmed = false;
        guardRecord.lastSnapshotTime = nowMs;
        markLuaActorDirty(guardRecord, reach.cellId);

        PacketGuardArrest prompt;
        prompt.mode = PacketGuardArrest::Mode::Prompt;
        prompt.reach = reach;
        sendTo(offender->conn, prompt.encode());
        Log(Debug::Info) << "[GuardArrest] routed prompt"
                         << " authority=" << c.name
                         << " offender=" << offender->name
                         << " offenderGuid=" << offender->guid
                         << " actorNetId=" << reach.actorNetId
                         << " cell=" << reach.cellId;
        return;
    }

    if (packet.mode != PacketGuardArrest::Mode::Request)
        return;

    const GuardArrestRequest& request = packet.request;
    const bool confiscatesStolenItems = request.action != GuardArrestAction::Resist;
    GuardArrestResult result;
    result.requestId = request.requestId;
    result.action = request.action;

    auto refreshClientState = [&] {
        if (mPlayerDb && c.dbCharacterId > 0)
        {
            c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
            c.player.bounty = c.player.crimeState.bounty;
            c.inventoryRevision = mPlayerDb->loadInventoryRevision(c.dbCharacterId);
            c.player.inventoryChanges.revision = c.inventoryRevision;
        }
    };
    auto refreshResultState = [&] {
        refreshClientState();
        result.crimeState = c.player.crimeState;
        result.inventoryRevision = c.inventoryRevision;
    };
    auto sendResult = [&] {
        PacketGuardArrest response;
        response.mode = PacketGuardArrest::Mode::Result;
        response.result = result;
        sendTo(c.conn, response.encode());
    };
    auto reject = [&](GuardArrestError error, bool restoreInventory = false) {
        refreshResultState();
        result.accepted = false;
        result.error = error;
        result.goldPaid = 0;
        result.sentenceDays = 0;
        if (restoreInventory && mPlayerDb && c.dbCharacterId > 0)
        {
            c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
            c.player.inventoryChanges.items = mPlayerDb->loadCharacterInventory(c.dbCharacterId);
            sendAuthoritativeInventory(c);
        }
        sendResult();
    };

    if (!mPlayerDb || !mContentRegistry || !c.charSelectComplete
        || c.dbAccountId <= 0 || c.dbCharacterId <= 0)
    {
        reject(GuardArrestError::Unauthorized);
        return;
    }

    const std::string requestHash = crypto::sha256hex(canonicalGuardArrestRequest(request));
    if (const auto existing
        = mPlayerDb->loadSemanticRequest("guard-arrest", c.dbAccountId, c.dbCharacterId, request.requestId))
    {
        if (existing->requestHash != requestHash)
        {
            reject(GuardArrestError::DuplicateConflict, confiscatesStolenItems);
            return;
        }
        try
        {
            result = decodeGuardArrestResult(existing->resultPayload);
            refreshClientState();
            if (confiscatesStolenItems)
            {
                c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
                c.player.inventoryChanges.items = mPlayerDb->loadCharacterInventory(c.dbCharacterId);
                sendAuthoritativeInventory(c);
                for (int slot = 0; slot < BasePlayer::NUM_EQUIPMENT_SLOTS; ++slot)
                    c.player.equipment[slot] = { slot, {} };
                for (const EquipmentItem& entry : mPlayerDb->loadCharacterEquipment(c.dbCharacterId))
                {
                    if (entry.slot >= 0 && entry.slot < BasePlayer::NUM_EQUIPMENT_SLOTS)
                        c.player.equipment[entry.slot] = entry;
                }
                sendAuthoritativeEquipment(c, true, true);
            }
            sendAuthoritativeCrimeState(c);
            sendResult();
            Log(Debug::Info) << "[GuardArrest] replay request=" << request.requestId
                             << " player=" << c.name
                             << " action=" << static_cast<unsigned>(request.action);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "[GuardArrest] corrupt stored result request=" << request.requestId
                              << " player=" << c.name << " error=" << e.what();
            reject(GuardArrestError::PersistenceFailure, confiscatesStolenItems);
        }
        return;
    }

    const std::string canonicalPlayerCell = makeCellKey(c.player.cell);
    if (request.cellId != canonicalPlayerCell)
    {
        reject(GuardArrestError::WrongCell, request.action == GuardArrestAction::PayFine);
        return;
    }
    if (c.player.isDead)
    {
        reject(GuardArrestError::PlayerDead, request.action == GuardArrestAction::PayFine);
        return;
    }

    refreshClientState();
    if (request.expectedCrimeRevision != c.player.crimeState.revision)
    {
        reject(GuardArrestError::StaleCrimeRevision, request.action == GuardArrestAction::PayFine);
        return;
    }
    if (c.player.crimeState.bounty <= 0)
    {
        reject(GuardArrestError::NoBounty, request.action == GuardArrestAction::PayFine);
        return;
    }
    if (confiscatesStolenItems
        && request.expectedInventoryRevision != c.inventoryRevision)
    {
        reject(GuardArrestError::StaleInventoryRevision, true);
        return;
    }

    const auto keyIt = mWorld.actorKeysByNetId.find(request.actorNetId);
    if (keyIt == mWorld.actorKeysByNetId.end())
    {
        reject(GuardArrestError::UnknownGuard, request.action == GuardArrestAction::PayFine);
        return;
    }
    const auto locationIt = mWorld.actorLocations.find(keyIt->second);
    if (locationIt == mWorld.actorLocations.end() || locationIt->second != request.cellId)
    {
        reject(GuardArrestError::WrongCell, request.action == GuardArrestAction::PayFine);
        return;
    }
    auto cellIt = mWorld.actorCells.find(request.cellId);
    if (cellIt == mWorld.actorCells.end())
    {
        reject(GuardArrestError::UnknownGuard, request.action == GuardArrestAction::PayFine);
        return;
    }
    auto actorIt = cellIt->second.actors.find(keyIt->second);
    if (actorIt == cellIt->second.actors.end())
    {
        reject(GuardArrestError::UnknownGuard, request.action == GuardArrestAction::PayFine);
        return;
    }
    ActorRegistryRecord& guardRecord = actorIt->second;
    if (guardRecord.actorNetId != request.actorNetId
        || guardRecord.migrationGeneration != request.migrationGeneration
        || guardRecord.actor.isDead)
    {
        reject(GuardArrestError::InvalidGuard, request.action == GuardArrestAction::PayFine);
        return;
    }

    const ESM::NPC* guardNpc = mContentRegistry->store().get<ESM::NPC>().search(
        ESM::RefId::stringRefId(guardRecord.actor.refId));
    if (!guardNpc || guardNpc->mClass != "guard")
    {
        reject(GuardArrestError::InvalidGuard, request.action == GuardArrestAction::PayFine);
        return;
    }
    if ((guardRecord.crimePursuitCharacterId > 0
            && guardRecord.crimePursuitCharacterId != c.dbCharacterId)
        || (guardRecord.crimePursuitCharacterId == c.dbCharacterId
            && guardRecord.crimeEnforcementState == CrimeEnforcementState::Combat))
    {
        reject(GuardArrestError::InvalidGuard, request.action == GuardArrestAction::PayFine);
        return;
    }

    constexpr std::uint64_t MaximumSnapshotAgeMs = 1000;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* playerSnapshot = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumSnapshotAgeMs);
    const AcceptedMechanicsSnapshot* guardSnapshot = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Npc, 0, request.actorNetId }, nowMs, MaximumSnapshotAgeMs);
    if (!playerSnapshot || !guardSnapshot
        || playerSnapshot->snapshot.cellId != request.cellId
        || guardSnapshot->snapshot.cellId != request.cellId
        || playerSnapshot->snapshot.migrationGeneration != 1
        || playerSnapshot->snapshot.authorityGeneration != c.guid
        || guardSnapshot->snapshot.migrationGeneration != request.migrationGeneration)
    {
        reject(GuardArrestError::SnapshotUnavailable, request.action == GuardArrestAction::PayFine);
        return;
    }

    const float dx = playerSnapshot->snapshot.position.pos[0] - guardSnapshot->snapshot.position.pos[0];
    const float dy = playerSnapshot->snapshot.position.pos[1] - guardSnapshot->snapshot.position.pos[1];
    const float dz = playerSnapshot->snapshot.position.pos[2] - guardSnapshot->snapshot.position.pos[2];
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distanceSquared)
        || distanceSquared > mDoorInteractionRadius * mDoorInteractionRadius)
    {
        reject(GuardArrestError::OutOfRange, request.action == GuardArrestAction::PayFine);
        return;
    }
    ContainerRecord expectedEvidence;
    bool evidenceWasPersisted = false;
    if (confiscatesStolenItems)
    {
        const auto contentEvidence = mContentRegistry->resolveJailEvidenceContainer(
            c.player.cell, playerSnapshot->snapshot.position);
        if (!contentEvidence)
        {
            Log(Debug::Warning) << "[GuardArrest] no unambiguous server evidence destination request="
                                << request.requestId << " player=" << c.name;
            reject(GuardArrestError::EvidenceUnavailable, true);
            return;
        }
        expectedEvidence = *contentEvidence;
        const std::string evidenceKey = makeContainerKey(expectedEvidence.cellId,
            expectedEvidence.refId, expectedEvidence.refNum, expectedEvidence.mpNum);
        const auto persistedEvidence = mWorld.containers.find(evidenceKey);
        if (persistedEvidence != mWorld.containers.end() && persistedEvidence->second.hasAuthority)
        {
            expectedEvidence = persistedEvidence->second;
            evidenceWasPersisted = true;
        }
    }

    const PlayerCrimeState previousCrime = c.player.crimeState;
    PlayerCrimeState nextCrime = previousCrime;
    std::vector<Item> nextInventory = c.player.inventoryChanges.items;
    std::int64_t goldPaid = 0;
    std::uint32_t sentenceDays = 0;
    std::uint64_t resultingInventoryRevision = c.inventoryRevision;
    std::optional<JailSentencePlan> jailPlan;

    if (request.action == GuardArrestAction::PayFine)
    {
        const std::string offenderTarget = "mp_remote_" + std::to_string(c.guid);
        const bool activeArrest = guardRecord.crimePursuitCharacterId == c.dbCharacterId
            || ((guardRecord.actor.ai.type == BaseActor::AIAction::Type::Pursue
                    || guardRecord.actor.ai.type == BaseActor::AIAction::Type::Combat)
                && guardRecord.actor.ai.targetId == offenderTarget);

        goldPaid = previousCrime.bounty;
        if (!activeArrest)
        {
            const float multiplier = mContentRegistry->store().get<ESM::GameSetting>()
                .find("fCrimeGoldTurnInMult")->mValue.getFloat();
            goldPaid = std::max<std::int64_t>(1,
                static_cast<std::int64_t>(static_cast<float>(previousCrime.bounty) * multiplier));
        }

        std::int64_t availableGold = 0;
        for (const Item& item : nextInventory)
        {
            if (lowerAscii(item.refId) == "gold_001" && item.count > 0)
                availableGold += item.count;
        }
        if (availableGold < goldPaid)
        {
            reject(GuardArrestError::InsufficientGold, true);
            return;
        }

        std::int64_t remaining = goldPaid;
        for (auto it = nextInventory.begin(); it != nextInventory.end() && remaining > 0;)
        {
            if (lowerAscii(it->refId) != "gold_001" || it->count <= 0)
            {
                ++it;
                continue;
            }
            const int removed = static_cast<int>(std::min<std::int64_t>(remaining, it->count));
            it->count -= removed;
            remaining -= removed;
            if (it->count <= 0)
                it = nextInventory.erase(it);
            else
                ++it;
        }
        resultingInventoryRevision = c.inventoryRevision + 1;
    }
    else if (request.action == GuardArrestAction::Surrender)
    {
        const int daysMod = std::max(1, mContentRegistry->store().get<ESM::GameSetting>()
            .find("iDaysinPrisonMod")->mValue.getInteger());
        sentenceDays = static_cast<std::uint32_t>(std::max(1, previousCrime.bounty / daysMod));
    }

    if (confiscatesStolenItems)
    {
        std::vector<EquipmentItem> equipment;
        for (const EquipmentItem& entry : c.player.equipment)
        {
            if (!entry.item.refId.empty() && entry.item.count > 0)
                equipment.push_back(entry);
        }
        jailPlan = JailSentenceService::planConfiscation(nextInventory, equipment,
            mPlayerDb->loadCharacterStolenItems(c.dbCharacterId), expectedEvidence, [&]() {
                return reserveWorldMpNum().value_or(0);
            });
        if (jailPlan->error != JailSentencePlanError::None)
        {
            Log(Debug::Error) << "[GuardArrest] confiscation planning failed request="
                              << request.requestId << " player=" << c.name
                              << " error=" << static_cast<unsigned>(jailPlan->error);
            reject(jailPlan->error == JailSentencePlanError::EvidenceUnavailable
                    ? GuardArrestError::EvidenceUnavailable : GuardArrestError::PersistenceFailure,
                true);
            return;
        }
        nextInventory = jailPlan->inventory;
        if (request.action == GuardArrestAction::Surrender && jailPlan->inventoryChanged())
            resultingInventoryRevision = c.inventoryRevision + 1;
    }

    if (request.action != GuardArrestAction::Resist)
    {
        if (previousCrime.revision >= MaximumPersistedRevision)
        {
            reject(GuardArrestError::PersistenceFailure, request.action == GuardArrestAction::PayFine);
            return;
        }
        nextCrime.bounty = 0;
        nextCrime.paidCrimeId = nextCrime.currentCrimeId;
        ++nextCrime.revision;
    }

    result.accepted = true;
    result.error = GuardArrestError::None;
    result.crimeState = nextCrime;
    result.inventoryRevision = resultingInventoryRevision;
    result.goldPaid = goldPaid;
    result.sentenceDays = sentenceDays;

    CrimeMutationCommit crimeCommit;
    crimeCommit.service = "guard-arrest";
    crimeCommit.accountId = c.dbAccountId;
    crimeCommit.characterId = c.dbCharacterId;
    crimeCommit.requestId = request.requestId;
    crimeCommit.requestHash = requestHash;
    crimeCommit.resultPayload = encodeGuardArrestResult(result);
    crimeCommit.source = request.action == GuardArrestAction::PayFine ? "guard_arrest_pay"
        : request.action == GuardArrestAction::Surrender ? "guard_arrest_surrender"
                                                         : "guard_arrest_resist";
    crimeCommit.expectedRevision = previousCrime.revision;
    crimeCommit.resultingState = nextCrime;

    GuardArrestCommit commit;
    commit.crimeMutation = std::move(crimeCommit);
    commit.inventoryChanged = request.action == GuardArrestAction::PayFine
        || (jailPlan && jailPlan->inventoryChanged());
    commit.expectedInventoryRevision = c.inventoryRevision;
    commit.resultingInventoryRevision = resultingInventoryRevision;
    commit.inventory = nextInventory;
    if (jailPlan)
    {
        commit.equipment = jailPlan->equipment;
        commit.equipmentChanged = jailPlan->equipmentChanged;
        commit.evidenceChanged = jailPlan->inventoryChanged();
        commit.evidenceWasPersisted = evidenceWasPersisted;
        commit.expectedEvidence = expectedEvidence;
        commit.resultingEvidence = jailPlan->evidence;
        commit.stolenItemMutations = jailPlan->stolenItemMutations;
    }

    GuardArrestCommitResult committed;
    try
    {
        committed = mPlayerDb->commitGuardArrest(commit);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[GuardArrest] persistence failure request=" << request.requestId
                          << " player=" << c.name << " error=" << e.what();
        reject(GuardArrestError::PersistenceFailure, confiscatesStolenItems);
        return;
    }

    if (committed.status == GuardArrestCommitStatus::DuplicateRequest)
    {
        try
        {
            result = decodeGuardArrestResult(committed.storedResultPayload);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "[GuardArrest] corrupt duplicate result request=" << request.requestId
                              << " player=" << c.name << " error=" << e.what();
            reject(GuardArrestError::PersistenceFailure, confiscatesStolenItems);
            return;
        }
    }
    else if (committed.status == GuardArrestCommitStatus::DuplicateRequestConflict)
    {
        reject(GuardArrestError::DuplicateConflict, confiscatesStolenItems);
        return;
    }
    else if (committed.status == GuardArrestCommitStatus::StaleCrimeRevision)
    {
        reject(GuardArrestError::StaleCrimeRevision, confiscatesStolenItems);
        return;
    }
    else if (committed.status == GuardArrestCommitStatus::StaleInventoryRevision)
    {
        reject(GuardArrestError::StaleInventoryRevision, true);
        return;
    }
    else if (committed.status == GuardArrestCommitStatus::StaleEvidence)
    {
        reject(GuardArrestError::StaleEvidence, true);
        return;
    }

    c.player.crimeState = mPlayerDb->loadPlayerCrimeState(c.dbCharacterId);
    c.player.bounty = c.player.crimeState.bounty;
    if (confiscatesStolenItems)
    {
        c.inventoryRevision = mPlayerDb->loadInventoryRevision(c.dbCharacterId);
        c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
        c.player.inventoryChanges.revision = c.inventoryRevision;
        c.player.inventoryChanges.items = mPlayerDb->loadCharacterInventory(c.dbCharacterId);
        c.acceptedPlayerInventoryThisSession = true;
        c.restoredInventorySnapshot = c.player.inventoryChanges.items;
        c.hasRestoredInventorySnapshot = true;
        c.playerInventoryRestoreGuardUntilMs = 0;
        sendAuthoritativeInventory(c);

        for (int slot = 0; slot < BasePlayer::NUM_EQUIPMENT_SLOTS; ++slot)
            c.player.equipment[slot] = { slot, {} };
        for (const EquipmentItem& entry : mPlayerDb->loadCharacterEquipment(c.dbCharacterId))
        {
            if (entry.slot >= 0 && entry.slot < BasePlayer::NUM_EQUIPMENT_SLOTS)
                c.player.equipment[entry.slot] = entry;
        }
        sendAuthoritativeEquipment(c, true, true);
    }

    if (committed.status == GuardArrestCommitStatus::Committed && commit.evidenceChanged)
    {
        const ContainerRecord& evidence = commit.resultingEvidence;
        mWorld.containers[makeContainerKey(
            evidence.cellId, evidence.refId, evidence.refNum, evidence.mpNum)] = evidence;
        PacketContainer evidencePacket;
        evidencePacket.container = evidence;
        evidencePacket.mAction = static_cast<std::uint8_t>(ContainerAction::Set);
        broadcastToCell(evidence.cellId, evidencePacket.encode());
    }

    sendAuthoritativeCrimeState(c);
    syncLuaPlayerSnapshot();

    if (request.action == GuardArrestAction::Resist)
    {
        guardRecord.crimePursuitCharacterId = c.dbCharacterId;
        guardRecord.crimePursuitLastGuid = c.guid;
        guardRecord.crimeEnforcementState = CrimeEnforcementState::Combat;
        guardRecord.crimePursuitReassertArmed = false;
        guardRecord.crimePursuitLastReassertMs = nowMs;
        guardRecord.actor.ai.type = BaseActor::AIAction::Type::Combat;
        guardRecord.actor.ai.targetId = std::string("mp_remote_") + std::to_string(c.guid);
        guardRecord.actor.ai.targetMpNum = 0;
        guardRecord.actor.ai.duration = 0.f;
        guardRecord.actor.ai.reset = false;
        guardRecord.lastSnapshotTime = nowMs;

        CrimeReactionDirective directive;
        directive.eventId = "guard-arrest-resist:" + std::to_string(c.dbCharacterId)
            + ':' + std::to_string(nowMs);
        directive.cellId = request.cellId;
        directive.offenderGuid = c.guid;
        directive.actors.push_back({ guardRecord.actorNetId, guardRecord.migrationGeneration,
            CrimeReactionDialogue::None,
            static_cast<std::uint8_t>(CrimeReactionSetAlarmed | CrimeReactionStartCombat) });
        if (validateCrimeReactionDirective(directive))
        {
            PacketCrimeReaction reactionPacket;
            reactionPacket.directive = directive;
            broadcastActorToCell(request.cellId, reactionPacket.encode());
            Log(Debug::Info) << "[CrimeReaction] dispatched resist combat"
                             << " event=" << directive.eventId
                             << " cell=" << request.cellId
                             << " actorNetId=" << guardRecord.actorNetId
                             << " offenderGuid=" << c.guid;
        }
        else
        {
            Log(Debug::Error) << "[CrimeReaction] refusing invalid resist-combat directive"
                              << " request=" << request.requestId
                              << " actorNetId=" << guardRecord.actorNetId;
        }
    }

    sendResult();
    Log(Debug::Info) << "[GuardArrest] accepted request=" << request.requestId
                     << " player=" << c.name
                     << " action=" << static_cast<unsigned>(request.action)
                     << " bountyBefore=" << previousCrime.bounty
                     << " bountyAfter=" << c.player.crimeState.bounty
                     << " goldPaid=" << result.goldPaid
                     << " sentenceDays=" << result.sentenceDays;
}

// ---------------------------------------------------------------------------
void MPServer::handleObjectDelete(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketObjectDelete pkt;
    if (!pkt.decode(data, size)) return;

    if (pkt.mpNum == 0 && !pkt.refId.empty() && pkt.refNum != 0)
    {
        BaseActor requestedActor;
        requestedActor.refId = pkt.refId;
        requestedActor.refNum = pkt.refNum;
        requestedActor.cellId = pkt.cellId;

        std::string deadCellId;
        const ActorRegistryRecord* deadRecord = findDeadVanillaActor(requestedActor, &deadCellId);
        if (!deadRecord)
        {
            Log(Debug::Info) << "[Server] Ignoring vanilla corpse dispose from " << c.name
                             << " refId=" << pkt.refId
                             << " refNum=" << pkt.refNum
                             << " packetCell=" << pkt.cellId
                             << " reason=unknown-corpse";
            return;
        }
        if (!cellMatches(c.player.cell, deadCellId))
        {
            Log(Debug::Info) << "[Server] Ignoring vanilla corpse dispose from " << c.name
                             << " refId=" << pkt.refId
                             << " refNum=" << pkt.refNum
                             << " packetCell=" << pkt.cellId
                             << " canonicalCell=" << deadCellId
                             << " reason=player-cell-mismatch";
            return;
        }

        const std::string containerKey
            = makeContainerKey(deadCellId, deadRecord->actor.refId, deadRecord->actor.refNum, 0);
        auto containerIt = mWorld.containers.find(containerKey);
        if (containerIt != mWorld.containers.end() && !containerIt->second.items.empty())
        {
            Log(Debug::Info) << "[Server] Ignoring vanilla corpse dispose from " << c.name
                             << " refId=" << pkt.refId
                             << " refNum=" << pkt.refNum
                             << " canonicalCell=" << deadCellId
                             << " remainingItems=" << containerIt->second.items.size()
                             << " reason=container-not-empty";
            return;
        }

        ActorRegistryRecord removedRecord = *deadRecord;
        ensureActorNetId(removedRecord, deadCellId);
        auto& actorCell = mWorld.actorCells[deadCellId];
        const std::string actorKey = makeActorKey(removedRecord.actor);
        actorCell.actors.erase(actorKey);
        forgetDeadVanillaActor(removedRecord.actor, deadCellId);

        if (containerIt != mWorld.containers.end())
        {
            if (mPlayerDb)
                mPlayerDb->deleteContainerRecord(containerIt->second.cellId, containerIt->second.refId,
                    containerIt->second.refNum, containerIt->second.mpNum);
            mWorld.containers.erase(containerIt);
        }

        broadcastActorIdentityRemovalForCell(deadCellId, actorCell, { removedRecord });
        forgetActorNetId(removedRecord.actorNetId, removedRecord.actor);

        Log(Debug::Info) << "[Server] Vanilla corpse dispose accepted: player=" << c.name
                         << " refId=" << removedRecord.actor.refId
                         << " refNum=" << removedRecord.actor.refNum
                         << " actorNetId=" << removedRecord.actorNetId
                         << " cell=" << deadCellId;
        return;
    }

    for (const auto& [actorCellId, cellState] : mWorld.actorCells)
    {
        for (const auto& [actorKey, actorRecord] : cellState.actors)
        {
            if (actorRecord.actor.mpNum == pkt.mpNum)
            {
                const BaseActor& actor = actorRecord.actor;
                if (!cellMatches(c.player.cell, actorCellId))
                {
                    Log(Debug::Verbose) << "[Server] Ignoring ObjectDelete from " << c.name
                                        << " mpNum=" << pkt.mpNum
                                        << " cell=" << pkt.cellId
                                        << " because spawned corpse is not in the player's current cell";
                    return;
                }

                if (!actor.isDead)
                {
                    Log(Debug::Verbose) << "[Server] Ignoring ObjectDelete from " << c.name
                                        << " mpNum=" << pkt.mpNum
                                        << " cell=" << pkt.cellId
                                        << " because spawned actor is still alive";
                    return;
                }

                const std::string containerKey = makeContainerKey(actorCellId, actor.refId, actor.refNum, actor.mpNum);
                auto containerIt = mWorld.containers.find(containerKey);
                if (containerIt != mWorld.containers.end() && !containerIt->second.items.empty())
                {
                    Log(Debug::Verbose) << "[Server] Ignoring ObjectDelete from " << c.name
                                        << " mpNum=" << pkt.mpNum
                                        << " cell=" << pkt.cellId
                                        << " because spawned corpse container is not empty";
                    return;
                }

                if (!removeActor(actor.mpNum, actorCellId))
                {
                    Log(Debug::Verbose) << "[Server] Ignoring ObjectDelete from " << c.name
                                        << " mpNum=" << pkt.mpNum
                                        << " cell=" << pkt.cellId
                                        << " because corpse removal failed";
                    return;
                }

                Log(Debug::Info) << "[Server] Corpse dispose accepted: player=" << c.name
                                 << " mpNum=" << pkt.mpNum
                                 << " cell=" << actorCellId;
                return;
            }
        }
    }

    std::optional<PlacedObject> takenObject;
    if (pkt.takenIntoInventory)
    {
        for (const auto& [cellId, objects] : mWorld.placedObjects)
        {
            const auto it = std::find_if(objects.begin(), objects.end(),
                [&](const PlacedObject& object) { return object.mpNum == pkt.mpNum; });
            if (it != objects.end())
            {
                takenObject = *it;
                break;
            }
        }
    }

    if (!removePlacedObjectAuthoritative(pkt.mpNum, pkt.cellId))
    {
        Log(Debug::Verbose) << "[Server] Ignoring ObjectDelete from " << c.name
                            << " mpNum=" << pkt.mpNum
                            << " cell=" << pkt.cellId
                            << " because no matching placed object exists";
        return;
    }

    if (takenObject)
    {
        c.pendingInventoryTransfers.push_back({ takenObject->mpNum, takenObject->refId, takenObject->count,
            currentServerTimeMs() + 5000 });
        Log(Debug::Verbose) << "[Server] Preserving world instance identity for inventory transfer"
                            << " player=" << c.name
                            << " refId=" << takenObject->refId
                            << " instanceId=" << takenObject->mpNum;
    }

    Log(Debug::Info) << "[Server] ObjectDelete accepted: player=" << c.name
                     << " mpNum=" << pkt.mpNum
                     << " cell=" << pkt.cellId;
}

// ---------------------------------------------------------------------------
void MPServer::handleObjectMove(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketObjectMove pkt;
    if (!pkt.decode(data, size)) return;

    auto objectsIt = mWorld.placedObjects.find(pkt.cellId);
    if (objectsIt != mWorld.placedObjects.end())
    {
        for (auto& object : objectsIt->second)
        {
            if (object.mpNum != pkt.mpNum) continue;
            object.position = pkt.position;
            mLua.upsertPlacedObject(object);
            if (mPlayerDb)
                mPlayerDb->upsertWorldObject(object);
            break;
        }
    }

    broadcastToCell(pkt.cellId, std::vector<uint8_t>(data, data + size), c.conn, /*reliable=*/false);
}

// ---------------------------------------------------------------------------
void MPServer::handleContainer(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketContainer pkt;
    if (!pkt.decode(data, size)) return;

    for (const ContainerItem& item : pkt.container.items)
    {
        if (!item.refId.empty() && !isAuthoritativeRecordReference(item.refId))
        {
            Log(Debug::Warning) << "[Server] Rejected Container unknown generated record from=" << c.name
                                << " refId=" << item.refId;
            return;
        }
    }

    const auto action = static_cast<ContainerAction>(pkt.mAction);
    if (action != ContainerAction::Set
        && action != ContainerAction::Add
        && action != ContainerAction::Remove
        && action != ContainerAction::BootstrapRequest)
        return;

    if (pkt.container.cellId.empty() || pkt.container.refId.empty())
        return;

    if (!cellMatches(c.player.cell, pkt.container.cellId))
    {
        Log(Debug::Warning) << "[Server] Rejecting Container from " << c.name
                            << " due to cell mismatch: player="
                            << makeCellKey(c.player.cell)
                            << " packet=" << pkt.container.cellId;
        return;
    }

    bool senderAuthoritative = false;
    auto actorCellIt = mWorld.actorCells.find(pkt.container.cellId);

    if (action == ContainerAction::BootstrapRequest)
    {
        const std::string key = makeContainerKey(
            pkt.container.cellId, pkt.container.refId, pkt.container.refNum, pkt.container.mpNum);
        const auto currentIt = mWorld.containers.find(key);
        if (currentIt != mWorld.containers.end() && currentIt->second.hasAuthority)
        {
            PacketContainer current;
            current.container = currentIt->second;
            current.mAction = static_cast<std::uint8_t>(ContainerAction::Set);
            sendTo(c.conn, current.encode());
            Log(Debug::Info) << "[Server] Container bootstrap satisfied from authoritative state requester=" << c.name
                             << " refId=" << pkt.container.refId << " refNum=" << pkt.container.refNum
                             << " mpNum=" << pkt.container.mpNum;
            return;
        }

        if (actorCellIt == mWorld.actorCells.end() || actorCellIt->second.authorityGuid == 0)
        {
            Log(Debug::Warning) << "[Server] Container bootstrap unavailable requester=" << c.name
                                << " refId=" << pkt.container.refId << " refNum=" << pkt.container.refNum
                                << " because no cell authority is available";
            return;
        }

        ConnectedClient* authority = nullptr;
        for (auto& [conn, candidate] : mClients)
        {
            (void)conn;
            if (candidate.guid == actorCellIt->second.authorityGuid && candidate.handshakeComplete)
            {
                authority = &candidate;
                break;
            }
        }
        if (!authority)
        {
            Log(Debug::Warning) << "[Server] Container bootstrap unavailable requester=" << c.name
                                << " refId=" << pkt.container.refId << " authorityGuid="
                                << actorCellIt->second.authorityGuid << " is not connected";
            return;
        }

        PacketContainer bootstrap;
        bootstrap.container.cellId = pkt.container.cellId;
        bootstrap.container.refId = pkt.container.refId;
        bootstrap.container.refNum = pkt.container.refNum;
        bootstrap.container.mpNum = pkt.container.mpNum;
        bootstrap.mAction = static_cast<std::uint8_t>(ContainerAction::BootstrapRequest);
        sendTo(authority->conn, bootstrap.encode());
        Log(Debug::Info) << "[Server] Container bootstrap relayed requester=" << c.name
                         << " authorityGuid=" << actorCellIt->second.authorityGuid
                         << " refId=" << pkt.container.refId << " refNum=" << pkt.container.refNum
                         << " mpNum=" << pkt.container.mpNum;
        return;
    }

    ActorRegistryRecord* sourceActor = nullptr;
    if (actorCellIt != mWorld.actorCells.end())
    {
        const auto actorIt = std::find_if(actorCellIt->second.actors.begin(), actorCellIt->second.actors.end(),
            [&](auto& entry) {
                const BaseActor& actor = entry.second.actor;
                return actor.refId == pkt.container.refId
                    && ((pkt.container.mpNum != 0 && actor.mpNum == pkt.container.mpNum)
                        || (pkt.container.mpNum == 0 && actor.refNum == pkt.container.refNum));
            });
        if (actorIt != actorCellIt->second.actors.end())
            sourceActor = &actorIt->second;
    }
    if (sourceActor)
        senderAuthoritative = isAllowedActorSender(c, *sourceActor, pkt.container.cellId);
    else if (actorCellIt != mWorld.actorCells.end())
        senderAuthoritative = actorCellIt->second.authorityGuid == c.guid;
    if (!senderAuthoritative)
    {
        if (action == ContainerAction::Set)
        {
            const std::string currentKey = makeContainerKey(
                pkt.container.cellId, pkt.container.refId, pkt.container.refNum, pkt.container.mpNum);
            const auto currentIt = mWorld.containers.find(currentKey);
            if (currentIt != mWorld.containers.end() && currentIt->second.hasAuthority)
            {
                PacketContainer current;
                current.container = currentIt->second;
                current.mAction = static_cast<std::uint8_t>(ContainerAction::Set);
                sendTo(c.conn, current.encode());
            }
        }
        Log(Debug::Warning) << "[Server] Rejected Container from non-authority player=" << c.name
                            << " refId=" << pkt.container.refId << " refNum=" << pkt.container.refNum
                            << " mpNum=" << pkt.container.mpNum;
        return;
    }

    if (action == ContainerAction::Remove && pkt.container.mpNum == 0)
    {
        Log(Debug::Warning) << "[Server] Rejected legacy Container(Remove); authoritative take required"
                            << " player=" << c.name << " refId=" << pkt.container.refId;
        return;
    }

    const std::string key = makeContainerKey(
        pkt.container.cellId, pkt.container.refId, pkt.container.refNum, pkt.container.mpNum);
    auto& authoritative = mWorld.containers[key];

    if (action == ContainerAction::Set)
    {
        normalizeContainerItems(pkt.container.items);

        if (authoritative.hasAuthority)
        {
            PacketContainer current;
            current.container = authoritative;
            current.mAction = static_cast<uint8_t>(ContainerAction::Set);
            sendTo(c.conn, current.encode());
            Log(Debug::Info) << "[Server] Container(Set replay): player=" << c.name
                             << " refId=" << authoritative.refId
                             << " refNum=" << authoritative.refNum
                             << " mpNum=" << authoritative.mpNum
                             << " items=" << authoritative.items.size();
            return;
        }

        authoritative = pkt.container;
        authoritative.hasAuthority = true;
        if (mPlayerDb)
            mPlayerDb->upsertContainerRecord(authoritative);

        scheduleGeneratedDynamicRecordGc("container_set");

        PacketContainer accepted;
        accepted.container = authoritative;
        accepted.mAction = static_cast<uint8_t>(ContainerAction::Set);
        sendTo(c.conn, accepted.encode());
        broadcastToCell(authoritative.cellId, accepted.encode(), c.conn);
        Log(Debug::Info) << "[Server] Container(Set accepted): player=" << c.name
                         << " refId=" << authoritative.refId
                         << " refNum=" << authoritative.refNum
                         << " mpNum=" << authoritative.mpNum
                         << " items=" << authoritative.items.size();
        return;
    }

    if (!authoritative.hasAuthority)
    {
        Log(Debug::Verbose) << "[Server] Ignoring Container delta before Set from " << c.name
                            << " refId=" << pkt.container.refId
                            << " refNum=" << pkt.container.refNum;
        return;
    }

    authoritative.hasAuthority = true;
    if (authoritative.cellId.empty())
    {
        authoritative.cellId = pkt.container.cellId;
        authoritative.refId = pkt.container.refId;
        authoritative.refNum = pkt.container.refNum;
        authoritative.mpNum = pkt.container.mpNum;
    }
    else if (authoritative.mpNum == 0 && pkt.container.mpNum != 0)
    {
        authoritative.mpNum = pkt.container.mpNum;
    }

    normalizeContainerItems(pkt.container.items);
    for (const auto& item : pkt.container.items)
        applyContainerDelta(authoritative.items, item, action);

    normalizeContainerItems(authoritative.items);

    if (mPlayerDb)
        mPlayerDb->upsertContainerRecord(authoritative);

    scheduleGeneratedDynamicRecordGc("container_delta");

    const ContainerItem* firstDelta = pkt.container.items.empty() ? nullptr : &pkt.container.items.front();
    Log(Debug::Info) << "[Server] Container(" << static_cast<int>(action) << "): player=" << c.name
                     << " refId=" << authoritative.refId
                     << " refNum=" << authoritative.refNum
                     << " mpNum=" << authoritative.mpNum
                     << " deltaItems=" << pkt.container.items.size()
                     << " totalItems=" << authoritative.items.size()
                     << " firstDeltaRefId=" << (firstDelta ? firstDelta->refId : "")
                     << " firstDeltaCount=" << (firstDelta ? firstDelta->count : 0)
                     << " firstDeltaCharge=" << (firstDelta ? firstDelta->charge : -999);
    broadcastToCell(authoritative.cellId, std::vector<uint8_t>(data, data + size), c.conn);
}

// ---------------------------------------------------------------------------
void MPServer::handleDoorState(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketDoorState pkt;
    if (!pkt.decode(data, size)) return;

    Log(Debug::Verbose) << "[Server] DoorState from " << c.name
                        << " cell=" << pkt.cellId
                        << " doors=" << pkt.doors.size();

    auto findCurrent = [&](const DoorEntry& requested) -> DoorEntry* {
        const auto cellIt = mWorld.doorStates.find(requested.cellId);
        if (cellIt == mWorld.doorStates.end())
            return nullptr;
        const ESM::RefId requestedId = ESM::RefId::stringRefId(requested.refId);
        for (DoorEntry& current : cellIt->second)
        {
            if (current.refNum == requested.refNum
                && ESM::RefId::stringRefId(current.refId) == requestedId)
                return &current;
        }
        return nullptr;
    };
    auto sendCorrection = [&]() {
        PacketDoorState correction;
        correction.authorGuid = c.guid;
        correction.cellId = pkt.cellId;
        for (const DoorEntry& requested : pkt.doors)
        {
            if (DoorEntry* current = findCurrent(requested))
                correction.doors.push_back(*current);
            else
            {
                DoorEntry initial = requested;
                initial.isOpen = false;
                initial.isLocked = false;
                initial.lockLevel = 0;
                initial.revision = 0;
                correction.doors.push_back(std::move(initial));
            }
        }
        if (!correction.doors.empty())
            sendTo(c.conn, correction.encode());
    };

    const std::optional<CellId> parsedCell = parseCellKey(pkt.cellId);
    if (pkt.doors.size() != 1 || !parsedCell || makeCellKey(*parsedCell) != pkt.cellId)
    {
        Log(Debug::Warning) << "[Server] DoorState rejected player=" << c.name
                            << " reason=invalid_or_noncanonical_batch";
        return;
    }

    constexpr std::uint64_t MaximumDoorSnapshotAgeMs = 1500;
    const std::uint64_t nowMs = currentServerTimeMs();
    const AcceptedMechanicsSnapshot* playerSnapshot = mMechanicsSnapshots.findFresh(
        { MechanicsSubjectKind::Player, c.guid, 0 }, nowMs, MaximumDoorSnapshotAgeMs);
    if (playerSnapshot == nullptr || playerSnapshot->snapshot.cellId != makeCellKey(c.player.cell))
    {
        Log(Debug::Warning) << "[Server] DoorState rejected player=" << c.name
                            << " reason=missing_or_stale_mechanics_snapshot";
        sendCorrection();
        return;
    }

    DoorEntry accepted = pkt.doors.front();
    const std::optional<ServerCollisionWorld::DoorReference> door
        = mCollisionWorld ? mCollisionWorld->findDoor(accepted.cellId, accepted.refId, accepted.refNum) : std::nullopt;
    std::vector<std::string> relevantCells
        = mCollisionOwnership.cells("player:" + std::to_string(c.guid));

    DoorStateProposalContext context;
    context.packetCellId = pkt.cellId;
    context.relevantCellIds = std::move(relevantCells);
    context.playerPosition = { playerSnapshot->snapshot.position.pos[0],
        playerSnapshot->snapshot.position.pos[1], playerSnapshot->snapshot.position.pos[2] };
    context.maximumDistance = mDoorInteractionRadius;
    if (DoorEntry* current = findCurrent(accepted))
        context.current = *current;
    if (door)
    {
        context.reference = DoorStateReference { accepted.cellId, door->refId, door->refNum,
            { door->position.x(), door->position.y(), door->position.z() } };
        accepted.refId = door->refId;
        accepted.refNum = door->refNum;
    }

    const DoorStateProposalError validation = validateDoorStateProposal(accepted, context);
    if (validation != DoorStateProposalError::None)
    {
        Log(Debug::Warning) << "[Server] DoorState rejected player=" << c.name
                            << " cell=" << pkt.cellId
                            << " refId=" << accepted.refId
                            << " refNum=" << accepted.refNum
                            << " revision=" << accepted.revision
                            << " reason=" << doorStateProposalErrorName(validation);
        sendCorrection();
        return;
    }

    accepted.mpNum = 0;
    if (context.current)
    {
        accepted.isLocked = context.current->isLocked;
        accepted.lockLevel = context.current->lockLevel;
    }
    else
    {
        accepted.isLocked = false;
        accepted.lockLevel = 0;
    }

    try
    {
        if (mPlayerDb)
            mPlayerDb->upsertDoorState(accepted);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] DoorState persistence rejected player=" << c.name
                          << " error=" << e.what();
        sendCorrection();
        return;
    }

    auto& cellDoors = mWorld.doorStates[accepted.cellId];
    if (DoorEntry* current = findCurrent(accepted))
        *current = accepted;
    else
        cellDoors.push_back(accepted);

    std::size_t collisionChanges = 0;
    std::uint64_t collisionGeneration = 0;
    if (mCollisionWorld)
    {
        collisionChanges = mCollisionWorld->setDoorOpen(
            accepted.cellId, accepted.refId, accepted.refNum, accepted.isOpen);
        collisionGeneration = mCollisionWorld->cellGeneration(accepted.cellId);
    }

    PacketDoorState authoritative;
    authoritative.authorGuid = c.guid;
    authoritative.cellId = accepted.cellId;
    authoritative.doors.push_back(accepted);
    const std::vector<std::uint8_t> encoded = authoritative.encode();
    sendTo(c.conn, encoded);
    broadcastToCell(accepted.cellId, encoded, c.conn);
    mLua.onDoorState(accepted.cellId, accepted.refId, accepted.isOpen);

    Log(Debug::Info) << "[Server] DoorState accepted player=" << c.name
                     << " cell=" << accepted.cellId
                     << " refId=" << accepted.refId
                     << " refNum=" << accepted.refNum
                     << " open=" << accepted.isOpen
                     << " revision=" << accepted.revision
                     << " collisionChanges=" << collisionChanges
                     << " collisionGeneration=" << collisionGeneration;
}

// ---------------------------------------------------------------------------
void MPServer::handleChatMessage(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketChatMessage pkt;
    pkt.setPlayer(&c.player);
    if (!pkt.decode(data, size)) return;

    // The client encodes its local player name into the packet, which may be
    // the slot name (before a nickname is set) rather than the current display
    // name. Re-assert the server-authoritative name so the relay uses the
    // nickname if one has been set, and to prevent the decode from corrupting
    // c.player.name for subsequent operations.
    c.player.name = c.name;

    if (handleObservationDiagnosticCommand(c, pkt.message)
        || handleCrimeWitnessDiagnosticCommand(c, pkt.message))
        return;

    Log(Debug::Info) << "[Server] Chat [" << c.name << "] "
                     << "(server time " << mWorld.gameHour << "h "
                     << "day=" << mWorld.day << " mo=" << mWorld.month
                     << " yr=" << mWorld.year << "): "
                     << pkt.message;

    if (mLua.isLoaded())
        mLua.onPlayerSendMessage(c.guid, c.name, pkt.message);
    else
        broadcastToAll(pkt.encode());  // re-encoded with authoritative name
}

// ---------------------------------------------------------------------------
void MPServer::handleLuaEvent(ConnectedClient& c, const uint8_t* data, size_t size)
{
    PacketLuaEvent pkt;
    if (!pkt.decode(data, size)) return;

    Log(Debug::Verbose) << "[Server] LuaEvent from " << c.name
                        << " pid=" << c.guid
                        << " name=" << pkt.eventName
                        << " bytes=" << pkt.eventData.size();

    if (pkt.eventName == "Activate")
    {
        std::string error;
        const std::optional<LuaUtil::BinaryData> resultData = mLua.evaluateImmediateIntent(c.guid, pkt.eventName, pkt.eventData, &error);
        if (resultData)
        {
            mLua.drainOutbound();

            const std::string playerCell = makeCellKey(c.player.cell);
            if (!playerCell.empty())
                broadcastLuaEventToCell(playerCell, 0, "ActivateResult", *resultData);
            else
                broadcastLuaEvent(0, "ActivateResult", *resultData);

            try
            {
                const LuaWireTable result = parseLuaWireTable(*resultData);
                Log(Debug::Info) << "[Server] Immediate Activate seq=" << getLuaNumberField(result, "seq", 0.0)
                                 << " by " << c.name
                                 << " action=" << getLuaStringField(result, "action")
                                 << " object=" << getLuaStringField(result, "objectId")
                                 << " recordId=" << getLuaStringField(result, "objectRecordId")
                                 << " accepted=" << (getLuaBoolField(result, "accepted") ? "true" : "false")
                                 << " verified=" << (getLuaBoolField(result, "serverVerified") ? "true" : "false")
                                 << " reason=" << getLuaStringField(result, "reason")
                                 << " mutation=" << getLuaStringField(result, "mutation");
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "[Server] Immediate Activate log parse failed: " << e.what();
            }
            return;
        }

        Log(Debug::Warning) << "[Server] Immediate Activate failed for " << c.name
                            << ": " << (error.empty() ? "unknown" : error)
                            << "; falling back to queued Lua event";
    }

    mLua.onLuaEvent(c.guid, c.dbCharacterId, pkt.eventName, pkt.eventData);
}

// ---------------------------------------------------------------------------
bool MPServer::validateMovement(const ConnectedClient& /*c*/,
                                const BasePlayer& /*proposed*/) const
{
    // TODO: re-enable anti-cheat once position sync is stable.
    // The per-tick distance check was causing false positives during
    // load transitions and initial position sync.
    return true;
}

// ---------------------------------------------------------------------------
void MPServer::kickClient(uint32_t guid, const std::string& reason)
{
    for (auto& [conn, client] : mClients)
    {
        if (client.guid == guid)
        {
            mInterface->CloseConnection(conn, 0, reason.c_str(), true);
            return;
        }
    }
}

ConnectedClient* MPServer::findClientByGuid(uint32_t guid)
{
    for (auto& [conn, client] : mClients)
        if (client.guid == guid)
            return &client;
    return nullptr;
}

bool MPServer::grantPlayerInventoryItem(uint32_t guid, const std::string& refId, int count)
{
    ConnectedClient* client = findClientByGuid(guid);
    return client ? grantInventoryItem(*client, refId, count) : false;
}

bool MPServer::isAuthoritativeRecordReference(std::string_view refId) const
{
    const std::string generatedPrefix = mGeneratedRecordIdPrefix + "_";
    if (!refId.starts_with(generatedPrefix))
        return true;

    return std::any_of(mWorld.dynamicRecords.begin(), mWorld.dynamicRecords.end(), [&](const auto& entry) {
        return entry.second.recordId == refId;
    });
}

bool MPServer::ensurePlayerInventoryItem(uint32_t guid, const std::string& refId)
{
    ConnectedClient* client = findClientByGuid(guid);
    if (!client || refId.empty())
        return false;

    const auto& items = client->player.inventoryChanges.items;
    const auto existing = std::find_if(items.begin(), items.end(), [&](const Item& item) {
        return item.refId == refId && item.count > 0 && item.charge == -1 && item.soul.empty();
    });
    if (existing == items.end())
        return grantInventoryItem(*client, refId, 1);

    // Dynamic records arrive before client Lua bootstrap. The earlier login
    // inventory restore can therefore skip this item while its record is still
    // unavailable. Resend the inventory here without adding another copy.
    sendAuthoritativeInventory(*client);
    return true;
}

bool MPServer::placeObject(const std::string& refId, int count, const std::string& cellId, const Position& position)
{
    if (!isAuthoritativeRecordReference(refId))
        return false;
    PlacedObject object;
    object.refId = refId;
    object.count = count;
    object.cellId = cellId;
    object.position = position;

    if (!acceptPlacedObject(object))
        return false;

    Log(Debug::Info) << "[Server] Script ObjectPlace accepted: refId=" << object.refId
                     << " mpNum=" << object.mpNum
                     << " cell=" << object.cellId
                     << " count=" << object.count;

    PacketObjectPlace pkt;
    pkt.object = object;
    broadcastToCell(object.cellId, pkt.encode());
    return true;
}

bool MPServer::removePlacedObjectByMpNum(uint32_t mpNum, const std::string& cellId)
{
    return removePlacedObjectAuthoritativeAnyCell(mpNum, cellId);
}

bool MPServer::worldMpNumInUse(uint32_t mpNum) const
{
    if (mpNum == 0)
        return false;

    for (const auto& [cellId, objects] : mWorld.placedObjects)
    {
        for (const auto& object : objects)
        {
            if (object.mpNum == mpNum)
                return true;
        }
    }

    for (const auto& [cellId, cellState] : mWorld.actorCells)
    {
        for (const auto& [actorKey, record] : cellState.actors)
        {
            if (record.actor.mpNum == mpNum)
                return true;
        }
    }

    for (const auto& [containerKey, record] : mWorld.containers)
    {
        if (record.mpNum == mpNum)
            return true;
    }

    for (const auto& [cellId, doors] : mWorld.doorStates)
    {
        for (const auto& door : doors)
        {
            if (door.mpNum == mpNum)
                return true;
        }
    }

    return false;
}

void MPServer::setNextWorldMpNum(uint64_t nextMpNum)
{
    nextMpNum = std::max<uint64_t>(nextMpNum, 1);
    mWorld.nextObjectMpNum = nextMpNum;
    mWorld.nextActorMpNum = nextMpNum;
    if (mPlayerDb)
        mPlayerDb->saveNextMpNum(nextMpNum);
}

std::optional<uint32_t> MPServer::reserveWorldMpNum()
{
    uint64_t candidate = std::max<uint64_t>(mWorld.nextObjectMpNum, mWorld.nextActorMpNum);
    candidate = std::max<uint64_t>(candidate, 1);

    constexpr uint64_t maxMpNum = std::numeric_limits<uint32_t>::max();
    if (candidate > maxMpNum)
        return std::nullopt;

    setNextWorldMpNum(candidate + 1);
    return static_cast<uint32_t>(candidate);
}

void MPServer::advanceWorldMpNumPast(uint32_t mpNum)
{
    if (mpNum == 0)
        return;

    const uint64_t minimumNext = static_cast<uint64_t>(mpNum) + 1;
    const uint64_t nextMpNum = std::max({ mWorld.nextObjectMpNum, mWorld.nextActorMpNum, minimumNext });
    if (nextMpNum != mWorld.nextObjectMpNum || nextMpNum != mWorld.nextActorMpNum)
        setNextWorldMpNum(nextMpNum);
}

bool MPServer::removeGameObject(uint32_t mpNum, const std::string& cellId)
{
    if (mpNum == 0)
        return false;

    for (const auto& [actorCellId, cellState] : mWorld.actorCells)
    {
        if (!cellId.empty() && actorCellId != cellId)
            continue;

        for (const auto& [actorKey, record] : cellState.actors)
        {
            if (record.actor.mpNum == mpNum)
                return removeActor(mpNum, actorCellId);
        }
    }

    return removePlacedObjectAuthoritativeAnyCell(mpNum, cellId);
}

bool MPServer::spawnActor(
    const std::string& refId, uint32_t refNum, uint32_t mpNum, const std::string& cellId, const Position& position,
    bool persistent, uint32_t authorityGuid)
{
    if (refId.empty() || cellId.empty() || !isAuthoritativeRecordReference(refId))
        return false;

    const WorldState::StoredDynamicRecord* dynamicActorRecord = nullptr;
    for (const std::string_view recordType : { std::string_view("npc"), std::string_view("creature") })
    {
        auto it = mWorld.dynamicRecords.find(makeDynamicRecordKey(recordType, refId));
        if (it != mWorld.dynamicRecords.end())
        {
            dynamicActorRecord = &it->second;
            break;
        }
    }

    if (dynamicActorRecord == nullptr)
    {
        for (const auto& [key, record] : mWorld.dynamicRecords)
        {
            if (record.recordId != refId)
                continue;

            Log(Debug::Warning) << "[Server] Rejecting Script ActorSpawn for non-actor dynamic record type="
                                << record.recordType << " id=" << refId;
            return false;
        }
    }

    uint32_t assignedMpNum = mpNum;
    if (assignedMpNum == 0)
    {
        do
        {
            const std::optional<uint32_t> reservedMpNum = reserveWorldMpNum();
            if (!reservedMpNum)
            {
                Log(Debug::Warning) << "[Server] Rejecting Script ActorSpawn because mpNum space is exhausted";
                return false;
            }

            assignedMpNum = *reservedMpNum;
        }
        while (worldMpNumInUse(assignedMpNum));
    }
    else
    {
        if (worldMpNumInUse(assignedMpNum))
        {
            Log(Debug::Warning) << "[Server] Rejecting Script ActorSpawn for duplicate actor mpNum=" << assignedMpNum;
            return false;
        }

        advanceWorldMpNumPast(assignedMpNum);
    }

    BaseActor actor;
    actor.refId = refId;
    actor.mpNum = assignedMpNum;
    actor.refNum = assignedMpNum != 0 ? 0 : refNum;
    actor.cellId = cellId;
    actor.position = position;
    actor.equipment.resize(BaseActor::NUM_EQUIPMENT_SLOTS);

    std::size_t staleContainerCount = 0;
    for (auto it = mWorld.containers.begin(); it != mWorld.containers.end();)
    {
        const ContainerRecord& record = it->second;
        if (record.mpNum != assignedMpNum)
        {
            ++it;
            continue;
        }

        if (mPlayerDb)
            mPlayerDb->deleteContainerRecord(record.cellId, record.refId, record.refNum, record.mpNum);
        it = mWorld.containers.erase(it);
        ++staleContainerCount;
    }

    if (staleContainerCount != 0)
        Log(Debug::Info) << "[Server] Script ActorSpawn cleared stale container authority"
                         << " mpNum=" << assignedMpNum
                         << " count=" << staleContainerCount;

    auto& cellState = mWorld.actorCells[cellId];
    if (cellState.authorityGuid == 0)
        refreshActorAuthorityForCell(cellId);

    const uint64_t timestamp = currentServerTimeMs();
    ActorRegistryRecord registryRecord;
    registryRecord.actor           = actor;
    registryRecord.lastSnapshotTime = timestamp;
    registryRecord.serverSpawnTime  = timestamp;   // never updated by client
    registryRecord.persistent       = persistent;
    if (authorityGuid != 0)
    {
        registryRecord.actorAuthorityGuid = authorityGuid;
        registryRecord.actorAuthorityGeneration = 1;
        registryRecord.actorAuthorityReason = "script-owner";
        registryRecord.actorAuthorityTargetGuid = authorityGuid;
        registryRecord.actorAuthorityLeaseUntilMs = 0;
        if (!isActorAuthorityLeaseValid(registryRecord, cellId, timestamp))
        {
            Log(Debug::Warning) << "[Server] Script ActorSpawn ignored invalid actor authority lease"
                                << " refId=" << refId
                                << " mpNum=" << assignedMpNum
                                << " authorityGuid=" << authorityGuid
                                << " cell=" << cellId;
            registryRecord.actorAuthorityGuid = 0;
            registryRecord.actorAuthorityReason.clear();
            registryRecord.actorAuthorityTargetGuid = 0;
        }
    }
    ensureActorNetId(registryRecord, cellId);
    cellState.actors[makeActorKey(actor)] = registryRecord;
    rememberActorLocation(actor, cellId);

    if (dynamicActorRecord != nullptr)
    {
        PacketRecordDynamic recordPkt;
        recordPkt.action = DynamicRecordAction::Upsert;
        recordPkt.recordType = dynamicActorRecord->recordType;
        recordPkt.entries.push_back({ dynamicActorRecord->recordId, dynamicActorRecord->data });
        broadcastActorToCell(cellId, recordPkt.encode());

        if (mPlayerDb)
            mPlayerDb->upsertSpawnedActorDynamicRecordLink(actor.refId, cellId, actor.mpNum);
    }

    persistSpawnedActorIfNeeded(registryRecord);
    markLuaActorDirty(registryRecord, cellId);

    ActorList actorList;
    actorList.cellId = cellId;
    actorList.isAuthority = false;
    actorList.authorityGuid = cellState.authorityGuid;
    actorList.authorityGeneration = cellState.authorityGeneration;
    actorList.snapshotSequence = cellState.nextSnapshotSequence++;
    actorList.serverTimestamp = timestamp;
    actorList.actors.reserve(cellState.actors.size());
    for (const auto& [actorKey, record] : cellState.actors)
        actorList.actors.push_back(record.actor);

    PacketActorList pkt;
    pkt.setActorList(&actorList);
    broadcastActorIdentityForCell(cellId, cellState);
    broadcastActorToCell(cellId, pkt.encode());
    if (registryRecord.actorAuthorityGuid != 0)
        broadcastActorAuthorityLease(cellId, registryRecord);

    Log(Debug::Info) << "[Server] Script ActorSpawn accepted: refId=" << actor.refId
                     << " refNum=" << actor.refNum
                     << " mpNum=" << actor.mpNum
                     << " actorNetId=" << registryRecord.actorNetId
                     << " cell=" << actor.cellId
                     << " persistent=" << persistent
                     << " actorAuthorityGuid=" << registryRecord.actorAuthorityGuid
                     << " actorAuthorityReason=" << registryRecord.actorAuthorityReason;
    sendActorLifecycleEvent("spawned", actor, persistent);
    return true;
}

bool MPServer::removeActor(uint32_t mpNum, const std::string& cellId)
{
    if (mpNum == 0)
        return false;

    auto cellIt = cellId.empty() ? mWorld.actorCells.end() : mWorld.actorCells.find(cellId);
    std::string resolvedCellId = cellId;

    auto actorPresentInCell = [&](const CellActorState& state)
    {
        for (const auto& [actorKey, record] : state.actors)
        {
            if (record.actor.mpNum == mpNum)
                return true;
        }
        return false;
    };

    if (cellIt == mWorld.actorCells.end() || !actorPresentInCell(cellIt->second))
    {
        cellIt = mWorld.actorCells.end();
        resolvedCellId.clear();
        for (auto it = mWorld.actorCells.begin(); it != mWorld.actorCells.end(); ++it)
        {
            if (!actorPresentInCell(it->second))
                continue;

            cellIt = it;
            resolvedCellId = it->first;
            break;
        }
    }

    if (cellIt == mWorld.actorCells.end() || resolvedCellId.empty())
        return false;

    auto& actors = cellIt->second.actors;
    bool removed = false;
    std::vector<ActorRegistryRecord> removedRecords;
    for (auto it = actors.begin(); it != actors.end();)
    {
        if (it->second.actor.mpNum == mpNum)
        {
            ensureActorNetId(it->second, resolvedCellId);
            removedRecords.push_back(it->second);
            forgetActorLocation(it->second.actor, resolvedCellId);
            it = actors.erase(it);
            removed = true;
        }
        else
        {
            ++it;
        }
    }

    if (!removed)
        return false;

    markLuaActorRemoved(mpNum);
    broadcastActorIdentityRemovalForCell(resolvedCellId, cellIt->second, removedRecords);

    for (const ActorRegistryRecord& record : removedRecords)
        forgetActorNetId(record.actorNetId, record.actor);

    for (auto it = mWorld.containers.begin(); it != mWorld.containers.end();)
    {
        const ContainerRecord& record = it->second;
        if (record.mpNum == mpNum)
        {
            if (mPlayerDb)
                mPlayerDb->deleteContainerRecord(record.cellId, record.refId, record.refNum, record.mpNum);
            it = mWorld.containers.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (mPlayerDb)
    {
        mPlayerDb->deleteSpawnedActorDynamicRecordLink(mpNum, resolvedCellId);
        mPlayerDb->deleteSpawnedActor(mpNum);
        scheduleGeneratedDynamicRecordGc("remove_actor");
    }

    ActorList actorList;
    actorList.cellId = resolvedCellId;
    actorList.isAuthority = false;
    actorList.authorityGuid = cellIt->second.authorityGuid;
    actorList.authorityGeneration = cellIt->second.authorityGeneration;
    actorList.snapshotSequence = cellIt->second.nextSnapshotSequence++;
    actorList.serverTimestamp = currentServerTimeMs();
    actorList.actors.reserve(cellIt->second.actors.size());
    for (const auto& [actorKey, record] : cellIt->second.actors)
        actorList.actors.push_back(record.actor);

    PacketActorList pkt;
    pkt.setActorList(&actorList);
    const std::vector<uint8_t> encodedActorList = pkt.encode();
    for (auto& [conn, client] : mClients)
    {
        // ActorSync v2 lifetime is driven by the reliable removed identities
        // sent above. Broadcasting a shrinking full ActorList after every
        // removal amplifies an N-actor clear to O(N^2) reliable traffic and can
        // fill the connection queue before the final removals are delivered.
        if (client.actorSyncProtocolVersion >= ActorSyncProtocolVersionV2
            || !clientHasActorCellLoaded(client, resolvedCellId))
            continue;
        sendTo(conn, encodedActorList);
    }

    Log(Debug::Info) << "[Server] Script ActorRemove accepted: mpNum=" << mpNum
                     << " cell=" << resolvedCellId;
    return true;
}

bool MPServer::resetCellStateForTesting(const std::string& cellId)
{
    if (cellId.empty())
        return false;

    std::vector<ActorRegistryRecord> removedRecords;
    std::unordered_set<std::string> resetSuppressedVanillaDeaths;
    std::size_t runtimeSpawnedActors = 0;
    std::size_t runtimeVanillaActors = 0;
    std::size_t deadVanillaActors = 0;
    std::size_t disposedVanillaActors = 0;

    auto cellIt = mWorld.actorCells.find(cellId);
    if (cellIt != mWorld.actorCells.end())
    {
        removedRecords.reserve(cellIt->second.actors.size());
        for (auto& [actorKey, record] : cellIt->second.actors)
        {
            ensureActorNetId(record, cellId);
            removedRecords.push_back(record);
            if (record.actor.mpNum != 0)
            {
                ++runtimeSpawnedActors;
                markLuaActorRemoved(record.actor.mpNum);
            }
            else
            {
                ++runtimeVanillaActors;
                resetSuppressedVanillaDeaths.insert(actorKey);
            }
            forgetActorLocation(record.actor, cellId);
        }
        cellIt->second.actors.clear();
        cellIt->second.staleLiveVanillaDeathResendMs.clear();
    }

    auto deadCellIt = mWorld.deadVanillaActorCells.find(cellId);
    if (deadCellIt != mWorld.deadVanillaActorCells.end())
    {
        for (auto& [actorKey, record] : deadCellIt->second)
        {
            ActorRegistryRecord removedRecord = record;
            ensureActorNetId(removedRecord, cellId);
            removedRecords.push_back(removedRecord);
            forgetActorLocation(removedRecord.actor, cellId);
            resetSuppressedVanillaDeaths.insert(actorKey);
            ++deadVanillaActors;
        }
        mWorld.deadVanillaActorCells.erase(deadCellIt);
    }

    for (auto it = mWorld.disposedVanillaActors.begin(); it != mWorld.disposedVanillaActors.end();)
    {
        if (it->second.cellId != cellId)
        {
            ++it;
            continue;
        }
        it = mWorld.disposedVanillaActors.erase(it);
        ++disposedVanillaActors;
    }

    if (cellIt == mWorld.actorCells.end())
        cellIt = mWorld.actorCells.emplace(cellId, CellActorState {}).first;
    cellIt->second.staleLiveVanillaDeathResendMs.clear();
    cellIt->second.resetSuppressedVanillaDeaths.insert(
        resetSuppressedVanillaDeaths.begin(), resetSuppressedVanillaDeaths.end());

    if (!removedRecords.empty())
    {
        broadcastActorIdentityRemovalForCell(cellId, cellIt->second, removedRecords);
        for (const ActorRegistryRecord& record : removedRecords)
            forgetActorNetId(record.actorNetId, record.actor);

        ActorList actorList;
        actorList.cellId = cellId;
        actorList.isAuthority = false;
        actorList.authorityGuid = cellIt->second.authorityGuid;
        actorList.authorityGeneration = cellIt->second.authorityGeneration;
        actorList.snapshotSequence = cellIt->second.nextSnapshotSequence++;
        actorList.serverTimestamp = currentServerTimeMs();

        PacketActorList pkt;
        pkt.setActorList(&actorList);
        broadcastActorToCell(cellId, pkt.encode());
    }

    std::size_t runtimePlacedObjects = 0;
    auto objectsIt = mWorld.placedObjects.find(cellId);
    if (objectsIt != mWorld.placedObjects.end())
    {
        runtimePlacedObjects = objectsIt->second.size();
        for (const PlacedObject& object : objectsIt->second)
            mLua.removePlacedObject(object.mpNum);
        mWorld.placedObjects.erase(objectsIt);
    }

    std::size_t runtimeContainers = 0;
    for (auto it = mWorld.containers.begin(); it != mWorld.containers.end();)
    {
        if (it->second.cellId != cellId)
        {
            ++it;
            continue;
        }

        it = mWorld.containers.erase(it);
        ++runtimeContainers;
    }

    const std::size_t runtimeDoorStates = mWorld.doorStates.erase(cellId);

    std::size_t dbPlacedObjects = 0;
    std::size_t dbSpawnedActors = 0;
    std::size_t dbDeadVanillaActors = 0;
    std::size_t dbContainers = 0;
    std::size_t dbDoorStates = 0;
    std::size_t dbSpawnedActorLinks = 0;
    if (mPlayerDb)
    {
        dbPlacedObjects = mPlayerDb->deleteWorldObjectsForCell(cellId);
        dbSpawnedActors = mPlayerDb->deleteSpawnedActorsForCell(cellId);
        dbDeadVanillaActors = mPlayerDb->deleteDeadVanillaActorsForCell(cellId);
        dbContainers = mPlayerDb->deleteContainerRecordsForCell(cellId);
        dbDoorStates = mPlayerDb->deleteDoorStatesForCell(cellId);
        dbSpawnedActorLinks = mPlayerDb->deleteSpawnedActorDynamicRecordLinksForCell(cellId);
        scheduleGeneratedDynamicRecordGc("reset_cell");
    }

    refreshActorAuthorityForCell(cellId);
    sendActorStateToInterestedClients(cellId);
    syncLuaPlayerSnapshot();

    Log(Debug::Info) << "[Server] Reset cell state"
                     << " cell=" << cellId
                     << " runtimeSpawnedActors=" << runtimeSpawnedActors
                     << " runtimeVanillaActors=" << runtimeVanillaActors
                     << " deadVanillaActors=" << deadVanillaActors
                     << " disposedVanillaActors=" << disposedVanillaActors
                     << " resetSuppressedVanillaDeaths=" << resetSuppressedVanillaDeaths.size()
                     << " runtimePlacedObjects=" << runtimePlacedObjects
                     << " runtimeContainers=" << runtimeContainers
                     << " runtimeDoorStates=" << runtimeDoorStates
                     << " dbPlacedObjects=" << dbPlacedObjects
                     << " dbSpawnedActors=" << dbSpawnedActors
                     << " dbDeadVanillaActors=" << dbDeadVanillaActors
                     << " dbContainers=" << dbContainers
                     << " dbDoorStates=" << dbDoorStates
                     << " dbSpawnedActorLinks=" << dbSpawnedActorLinks;

    return true;
}

std::size_t MPServer::resetAllCellStatesForTesting()
{
    std::unordered_set<std::string> uniqueCellIds;
    auto rememberCell = [&](const std::string& cellId) {
        if (!cellId.empty())
            uniqueCellIds.insert(cellId);
    };

    for (const auto& [cellId, cellState] : mWorld.actorCells)
    {
        (void)cellState;
        rememberCell(cellId);
    }
    for (const auto& [cellId, actors] : mWorld.deadVanillaActorCells)
    {
        (void)actors;
        rememberCell(cellId);
    }
    for (const auto& [actorKey, actor] : mWorld.disposedVanillaActors)
    {
        (void)actorKey;
        rememberCell(actor.cellId);
    }
    for (const auto& [cellId, objects] : mWorld.placedObjects)
    {
        (void)objects;
        rememberCell(cellId);
    }
    for (const auto& [containerKey, container] : mWorld.containers)
    {
        (void)containerKey;
        rememberCell(container.cellId);
    }
    for (const auto& [cellId, doors] : mWorld.doorStates)
    {
        (void)doors;
        rememberCell(cellId);
    }

    if (mPlayerDb)
    {
        for (const PlacedObject& object : mPlayerDb->loadWorldObjects())
            rememberCell(object.cellId);
        for (const PersistedSpawnedActor& record : mPlayerDb->loadSpawnedActors())
            rememberCell(record.actor.cellId);
        for (const BaseActor& actor : mPlayerDb->loadDeadVanillaActors())
            rememberCell(actor.cellId);
        for (const BaseActor& actor : mPlayerDb->loadDisposedVanillaActors())
            rememberCell(actor.cellId);
        for (const ContainerRecord& container : mPlayerDb->loadContainerRecords())
            rememberCell(container.cellId);
        for (const DoorEntry& door : mPlayerDb->loadDoorStates())
            rememberCell(door.cellId);
    }

    std::vector<std::string> cellIds(uniqueCellIds.begin(), uniqueCellIds.end());
    std::sort(cellIds.begin(), cellIds.end());

    std::size_t resetCells = 0;
    for (const std::string& cellId : cellIds)
    {
        if (resetCellStateForTesting(cellId))
            ++resetCells;
    }

    Log(Debug::Info) << "[Server] Reset all cell state cells=" << resetCells;
    return resetCells;
}

bool MPServer::upsertDynamicRecord(const std::string& recordType, const std::string& recordId, const std::string& data,
    const std::string& recordScope, bool persistent, const std::string& authoringMode)
{
    const std::string normalizedType = normalizeDynamicRecordType(recordType);
    const std::string normalizedScope = normalizeDynamicRecordScope(recordScope);
    const std::string normalizedAuthoringMode = normalizeDynamicRecordAuthoringMode(authoringMode);
    if (normalizedType.empty() || recordId.empty() || !mPlayerDb)
        return false;
    if (normalizedScope.empty() || normalizedAuthoringMode.empty())
        return false;
    if (!isCanonicalServerLuaRecordType(normalizedType))
    {
        // Trusted server Lua historically supports a wider RecordDynamic surface
        // (notably Bardcraft NPCs and administrative spell/NPC test records).
        // Keep that server-only compatibility path until those record kinds have
        // typed OMDR DTO coverage. Client proposals can never enter this API.
        auto& record = mWorld.dynamicRecords[makeDynamicRecordKey(normalizedType, recordId)];
        record.recordType = normalizedType;
        record.recordId = recordId;
        record.data = data;
        record.recordScope = normalizedScope;
        record.persistent = persistent;
        record.sequence = mWorld.nextDynamicRecordSequence++;

        if (normalizedScope == "generated")
            mLua.observeGeneratedRecordId(normalizedType, recordId);

        DynamicRecordCatalogEntry catalogRecord;
        catalogRecord.recordType = normalizedType;
        catalogRecord.recordId = recordId;
        catalogRecord.recordScope = normalizedScope;
        catalogRecord.persistent = persistent;
        catalogRecord.creationSource = "server_lua_legacy";
        catalogRecord.schemaVersion = 0;
        catalogRecord.validationVersion = 0;
        mPlayerDb->upsertDynamicRecordCatalog(catalogRecord);

        if (persistent)
        {
            PersistedDynamicRecord persisted;
            persisted.recordType = normalizedType;
            persisted.recordId = recordId;
            persisted.recordScope = normalizedScope;
            persisted.data = data;
            persisted.schemaVersion = 0;
            mPlayerDb->upsertDynamicRecord(persisted);
        }
        else
            mPlayerDb->deleteDynamicRecord(normalizedType, recordId);

        PacketRecordDynamic packet;
        packet.action = DynamicRecordAction::Upsert;
        packet.recordType = normalizedType;
        packet.entries.push_back({ recordId, data });
        broadcastToAll(packet.encode());

        Log(Debug::Info) << "[Server] Trusted legacy server-Lua record type=" << normalizedType
                         << " id=" << recordId
                         << " scope=" << normalizedScope
                         << " persistent=" << (persistent ? "true" : "false")
                         << " (awaiting typed OMDR coverage)";
        return true;
    }

    try
    {
        const records::AuthoringMode mode = normalizedAuthoringMode == "new" ? records::AuthoringMode::New
            : normalizedAuthoringMode == "override" ? records::AuthoringMode::Override
                                                    : records::AuthoringMode::Generated;
        records::DynamicRecordDefinition definition = data.starts_with("OMDR")
            ? records::decodeDefinition(data)
            : parseServerLuaRecord(normalizedType, data, mode);
        if (records::getRecordTypeName(records::getRecordType(definition)) != normalizedType)
            throw std::runtime_error("record type does not match the typed definition");
        if (data.starts_with("OMDR") && definition.authoringMode != mode)
            throw std::runtime_error("authoring mode does not match the typed definition");
        const bool durableServerContent
            = normalizedType == "dialogue" || normalizedType == "script";
        const bool durableStaticOverride
            = mode == records::AuthoringMode::Override && normalizedType == "clothing";
        if (durableServerContent && mode == records::AuthoringMode::Generated)
            throw std::runtime_error("Dialogue and Script require explicit mode=new or mode=override");
        if ((durableServerContent || durableStaticOverride)
            && (normalizedScope != "permanent" || !persistent))
            throw std::runtime_error("Explicit server content definitions must be permanent and persistent");

        records::RecordCreateRequest request;
        request.operation = records::CreateOperation::ServerScript;
        request.scriptPackageId = "server_lua";
        request.bundle.records.push_back({ "record", std::move(definition) });

        const std::string identityMaterial = "server-lua-validation-v"
            + std::to_string(ServerLuaValidationVersion) + '\0' + normalizedType + '\0' + recordId + '\0'
            + normalizedScope + '\0' + (persistent ? "1" : "0") + '\0' + normalizedAuthoringMode + '\0' + data;
        const std::string identityHash = crypto::sha256hex(identityMaterial);
        request.requestId = "server-lua:" + identityHash;
        const std::string requestHash = identityHash;

        const std::vector<DynamicRecordCatalogEntry> catalog = mPlayerDb->loadDynamicRecordCatalog();
        DynamicRecordService::Context context;
        context.trustedServerRequest = true;
        context.creationSource = "server_lua";
        context.validationVersion = ServerLuaValidationVersion;
        context.serverRequestSource = "server_lua";
        context.recordScope = normalizedScope;
        context.persistent = persistent;
        context.fixedRecordIds.emplace("record", recordId);
        context.allowStaticOverrides = mClients.empty();
        context.isAssetAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasAsset(normalizeRuntimeAsset(asset));
        };
        context.isModelAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasModel(normalizeRuntimeAsset(asset));
        };
        context.isIconAllowed = [&](std::string_view asset) {
            return mContentRegistry->hasIcon(normalizeRuntimeAsset(asset));
        };
        context.isContentIdAllowed = [&](std::string_view id) {
            if (mContentRegistry->hasContentId(lowerAscii(id)))
                return true;
            return std::any_of(catalog.begin(), catalog.end(), [&](const DynamicRecordCatalogEntry& entry) {
                return lowerAscii(entry.recordId) == lowerAscii(id);
            });
        };
        context.hasStaticRecord = [&](records::RecordType type, std::string_view id) {
            return mContentRegistry->hasStaticRecord(static_cast<std::uint8_t>(type), id);
        };
        context.loadDurableJournalInfoIds = [&](std::string_view dialogueId) {
            return mPlayerDb->loadReferencedJournalInfoIds(dialogueId);
        };
        context.validateScriptSource = [&](std::string_view scriptId, std::string_view source) {
            return mContentRegistry->validateScriptSource(scriptId, source);
        };
        context.findRecordById = [&](records::RecordType type, std::string_view id)
            -> std::optional<DynamicRecordService::CatalogRecord> {
            const std::string typeName(records::getRecordTypeName(type));
            for (const DynamicRecordCatalogEntry& entry : catalog)
            {
                if (entry.recordType != typeName || lowerAscii(entry.recordId) != lowerAscii(id))
                    continue;
                const auto stored = mWorld.dynamicRecords.find(makeDynamicRecordKey(typeName, entry.recordId));
                if (stored != mWorld.dynamicRecords.end())
                    return DynamicRecordService::CatalogRecord{
                        typeName, entry.recordId, entry.definitionFingerprint, stored->second.data };
            }
            return std::nullopt;
        };

        DynamicRecordService service(*mPlayerDb);
        auto outcome = service.execute(request, requestHash, context,
            [&](records::RecordType type, std::string_view fingerprint)
                -> std::optional<DynamicRecordService::CatalogRecord> {
                const std::string typeName(records::getRecordTypeName(type));
                for (const DynamicRecordCatalogEntry& entry : catalog)
                {
                    if (entry.recordType != typeName || entry.recordId != recordId
                        || entry.definitionFingerprint != fingerprint)
                        continue;
                    auto stored = mWorld.dynamicRecords.find(makeDynamicRecordKey(typeName, entry.recordId));
                    if (stored != mWorld.dynamicRecords.end())
                        return DynamicRecordService::CatalogRecord{
                            typeName, entry.recordId, entry.definitionFingerprint, stored->second.data };
                }
                return std::nullopt;
            },
            [&](records::RecordType) { return recordId; },
            [&]() { return mWorld.nextDynamicRecordSequence++; });

        if (!outcome.result.accepted)
        {
            Log(Debug::Warning) << "[Server] Rejected server-Lua dynamic record type=" << normalizedType
                                << " id=" << recordId
                                << " error=" << records::getCreateErrorCode(outcome.result.error);
            return false;
        }

        for (const records::CreatedRecord& created : outcome.result.records)
        {
            const records::DynamicRecordDefinition installedDefinition
                = records::decodeDefinition(created.definition);
            mContentRegistry->installRuntimeDefinition(created.recordId, installedDefinition);
            WorldState::StoredDynamicRecord stored;
            stored.recordType = normalizedType;
            stored.recordId = created.recordId;
            stored.data = created.definition;
            stored.recordScope = normalizedScope;
            stored.persistent = persistent;
            stored.sequence = outcome.result.commitSequence;
            stored.dependencyRecordIds = records::extractContentDependencies(installedDefinition);
            mWorld.dynamicRecords[makeDynamicRecordKey(normalizedType, created.recordId)] = std::move(stored);

            PacketRecordDynamic packet;
            packet.action = DynamicRecordAction::Upsert;
            packet.recordType = normalizedType;
            packet.entries.push_back({ created.recordId, created.definition });
            broadcastToAll(packet.encode());
        }
        if (normalizedScope == "generated")
            mLua.observeGeneratedRecordId(normalizedType, recordId);

        Log(Debug::Info) << "[Server] Canonical server-Lua record type=" << normalizedType
                         << " id=" << recordId
                         << " scope=" << normalizedScope
                         << " persistent=" << (persistent ? "true" : "false")
                         << " replayed=" << (outcome.replayed ? "true" : "false");
        return true;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[Server] Server-Lua dynamic record failed type=" << normalizedType
                          << " id=" << recordId << ": " << e.what();
        return false;
    }
}

bool MPServer::removeDynamicRecord(const std::string& recordType, const std::string& recordId)
{
    const std::string normalizedType = normalizeDynamicRecordType(recordType);
    if (normalizedType.empty() || recordId.empty())
        return false;
    if (normalizedType == "dialogue" || normalizedType == "script")
    {
        Log(Debug::Warning) << "[Server] Refusing removal of durable server content type="
                            << normalizedType << " id=" << recordId;
        return false;
    }

    auto it = mWorld.dynamicRecords.find(makeDynamicRecordKey(normalizedType, recordId));
    if (it == mWorld.dynamicRecords.end())
        return false;

    cleanupDynamicReferences(
        [&](std::string_view refId) -> bool { return refId == recordId; },
        /*broadcastLive=*/true,
        recordId);

    mWorld.dynamicRecords.erase(it);

    if (mPlayerDb)
    {
        mPlayerDb->replaceDynamicRecordDependencies(normalizedType, recordId, {});
        mPlayerDb->deleteDynamicRecord(normalizedType, recordId);
        mPlayerDb->deleteDynamicRecordCatalog(normalizedType, recordId);
        mPlayerDb->deleteDynamicRecordLinks(recordId);
    }

    PacketRecordDynamic pkt;
    pkt.action = DynamicRecordAction::Remove;
    pkt.recordType = normalizedType;
    pkt.entries.push_back({ recordId, {} });
    broadcastToAll(pkt.encode());

    Log(Debug::Info) << "[Server] Removed dynamic record type=" << normalizedType
                     << " id=" << recordId;
    return true;
}

bool MPServer::setDynamicRecordDependencies(
    const std::string& recordType, const std::string& recordId, const std::vector<std::string>& dependencyRecordIds)
{
    const std::string normalizedType = normalizeDynamicRecordType(recordType);
    if (normalizedType.empty() || recordId.empty())
        return false;

    auto stored = mWorld.dynamicRecords.find(makeDynamicRecordKey(normalizedType, recordId));
    if (stored == mWorld.dynamicRecords.end())
        return false;

    std::vector<std::string> uniqueDependencies;
    std::unordered_set<std::string> seen;

    for (const auto& dependencyRecordId : dependencyRecordIds)
    {
        if (dependencyRecordId.empty() || dependencyRecordId == recordId)
            continue;
        if (!seen.insert(dependencyRecordId).second)
            continue;

        bool found = false;
        for (const auto& [key, record] : mWorld.dynamicRecords)
        {
            if (record.recordId == dependencyRecordId)
            {
                found = true;
                break;
            }
        }

        if (!found)
            return false;

        uniqueDependencies.push_back(dependencyRecordId);
    }

    if (mPlayerDb)
        mPlayerDb->replaceDynamicRecordDependencies(normalizedType, recordId, uniqueDependencies);
    stored->second.dependencyRecordIds = uniqueDependencies;

    Log(Debug::Info) << "[Server] Set dynamic record dependencies type=" << normalizedType
                     << " id=" << recordId
                     << " count=" << uniqueDependencies.size();
    return true;
}

// ---------------------------------------------------------------------------
void MPServer::setPlayerNickname(uint32_t guid, const std::string& nickname)
{
    ConnectedClient* c = findClientByGuid(guid);
    if (!c || !c->charSelectComplete) return;

    // Clamp to 32 chars to prevent abuse
    const std::string nick = nickname.substr(0, 32);

    c->nickname = nick;
    const std::string displayName = nick.empty() ? c->slotName : nick;
    c->name        = displayName;
    c->player.name = displayName;

    // Persist to DB
    if (mPlayerDb && c->dbCharacterId != 0)
        mPlayerDb->setNickname(c->dbCharacterId, nick);

    // Broadcast updated base info so all clients update their nameplate
    PacketPlayerBaseInfo pkt;
    pkt.setPlayer(&c->player);
    broadcastToAll(pkt.encode());
    syncLuaPlayerSnapshot();

    Log(Debug::Info) << "[Server] " << c->slotName
                     << " nickname set to '" << displayName << "'";
}

int MPServer::getPlayerCount() const
{
    int count = 0;
    for (const auto& [conn, client] : mClients)
        if (client.charSelectComplete)
            ++count;
    return count;
}

bool MPServer::acceptPlacedObject(PlacedObject& object, ConnectedClient* source)
{
    if (!isAuthoritativeRecordReference(object.refId))
    {
        Log(Debug::Warning) << "[Server] Rejecting ObjectPlace unknown generated record id=" << object.refId;
        return false;
    }
    if (object.refId.empty() || object.cellId.empty() || object.count <= 0)
        return false;

    const uint32_t requestedInstanceId = object.mpNum;
    object.mpNum = 0;
    if (source && requestedInstanceId != 0)
    {
        const auto sourceStack = std::find_if(source->player.inventoryChanges.items.begin(),
            source->player.inventoryChanges.items.end(), [&](const Item& item) {
                return item.instanceId == requestedInstanceId && item.refId == object.refId && item.count > 0;
            });
        // Only a whole-stack drop transfers its identity. A split remains in
        // inventory under the old ID and the dropped portion gets a new one.
        if (sourceStack != source->player.inventoryChanges.items.end() && object.count == sourceStack->count
            && !worldMpNumInUse(requestedInstanceId))
            object.mpNum = requestedInstanceId;
    }

    do
    {
        if (object.mpNum != 0)
            break;
        const std::optional<uint32_t> reservedMpNum = reserveWorldMpNum();
        if (!reservedMpNum)
        {
            Log(Debug::Warning) << "[Server] Rejecting ObjectPlace because mpNum space is exhausted";
            return false;
        }

        object.mpNum = *reservedMpNum;
    }
    while (worldMpNumInUse(object.mpNum));

    auto& objects = mWorld.placedObjects[object.cellId];
    objects.push_back(object);
    mLua.upsertPlacedObject(object);

    if (mPlayerDb)
        mPlayerDb->upsertWorldObject(object);

    return true;
}

// ---------------------------------------------------------------------------
void MPServer::syncLuaPlayerSnapshot()
{
    if (!mLua.isLoaded())
        return;

    std::vector<LuaPlayerSnapshot> players;
    players.reserve(mClients.size());

    for (const auto& [conn, client] : mClients)
    {
        if (!client.charSelectComplete)
            continue;

        LuaPlayerSnapshot snapshot;
        snapshot.guid = client.guid;
        snapshot.dbCharacterId = client.dbCharacterId;
        snapshot.name = client.name;
        snapshot.cell = makeCellKey(client.player.cell);
        const std::unordered_set<std::string> loadedActorCells = actorInterestCellsForClient(client);
        snapshot.loadedActorCells.assign(loadedActorCells.begin(), loadedActorCells.end());
        std::sort(snapshot.loadedActorCells.begin(), snapshot.loadedActorCells.end());
        snapshot.nickname = client.nickname;
        snapshot.race = client.player.race;
        snapshot.isMale = client.player.isMale;
        snapshot.x = client.player.position.pos[0];
        snapshot.y = client.player.position.pos[1];
        snapshot.z = client.player.position.pos[2];
        snapshot.rx = client.player.position.rot[0];
        snapshot.ry = client.player.position.rot[1];
        snapshot.rz = client.player.position.rot[2];
        snapshot.dynamicStats = client.player.dynamicStats;
        snapshot.skills = client.player.skills;
        snapshot.inventory = client.player.inventoryChanges.items;
        snapshot.crimeState = client.player.crimeState;
        snapshot.factionState = client.player.factionState;
        players.push_back(std::move(snapshot));
    }

    mLua.syncSnapshot(getUptime(), mWorld.gameHour, players);
}

// ---------------------------------------------------------------------------
void MPServer::rebuildLuaActorSnapshot()
{
    if (!mLua.isLoaded())
    {
        mLuaDirtyActors.clear();
        mLuaRemovedActors.clear();
        return;
    }

    std::vector<LuaActorSnapshot> actors;
    for (const auto& [cellId, cellState] : mWorld.actorCells)
    {
        for (const auto& [actorKey, record] : cellState.actors)
        {
            if (record.actor.mpNum == 0)
                continue;

            LuaActorSnapshot snapshot;
            snapshot.actor = record.actor;
            if (snapshot.actor.cellId.empty())
                snapshot.actor.cellId = cellId;
            snapshot.persistent = record.persistent;
            actors.push_back(std::move(snapshot));
        }
    }
    mLua.syncActors(std::move(actors));
    mLuaDirtyActors.clear();
    mLuaRemovedActors.clear();
}

// ---------------------------------------------------------------------------
void MPServer::markLuaActorDirty(const ActorRegistryRecord& record, const std::string& cellId)
{
    const uint32_t mpNum = record.actor.mpNum;
    if (mpNum == 0)
        return;

    mLuaRemovedActors.erase(mpNum);
    mLuaDirtyActors.insert_or_assign(mpNum, LuaActorLocation { cellId, makeActorKey(record.actor) });
}

// ---------------------------------------------------------------------------
void MPServer::markLuaActorRemoved(uint32_t mpNum)
{
    if (mpNum == 0)
        return;

    mLuaDirtyActors.erase(mpNum);
    mLuaRemovedActors.insert(mpNum);
}

// ---------------------------------------------------------------------------
void MPServer::flushLuaActorChanges()
{
    if (!mLua.isLoaded())
    {
        mLuaDirtyActors.clear();
        mLuaRemovedActors.clear();
        return;
    }

    for (uint32_t mpNum : mLuaRemovedActors)
        mLua.removeActor(mpNum);

    for (const auto& [mpNum, location] : mLuaDirtyActors)
    {
        const auto cellIt = mWorld.actorCells.find(location.cellId);
        if (cellIt == mWorld.actorCells.end())
        {
            mLua.removeActor(mpNum);
            continue;
        }

        const auto actorIt = cellIt->second.actors.find(location.actorKey);
        if (actorIt == cellIt->second.actors.end() || actorIt->second.actor.mpNum != mpNum)
        {
            mLua.removeActor(mpNum);
            continue;
        }

        LuaActorSnapshot snapshot;
        snapshot.actor = actorIt->second.actor;
        if (snapshot.actor.cellId.empty())
            snapshot.actor.cellId = location.cellId;
        snapshot.persistent = actorIt->second.persistent;
        mLua.upsertActor(std::move(snapshot));
    }

    mLuaRemovedActors.clear();
    mLuaDirtyActors.clear();
}

// ---------------------------------------------------------------------------
void MPServer::sendAuthoritativeInventory(ConnectedClient& c)
{
    c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
    PacketPlayerInventory inventory;
    inventory.setPlayer(&c.player);
    sendTo(c.conn, inventory.encode());
}

void MPServer::sendAuthoritativeSpellbook(ConnectedClient& c)
{
    c.player.spellbookChanges.action = BasePlayer::SpellbookChanges::Action::Set;
    c.player.spellbookChanges.revision = c.spellbookRevision;
    PacketPlayerSpellbook spellbook;
    spellbook.setPlayer(&c.player);
    sendTo(c.conn, spellbook.encode());
}

void MPServer::sendAuthoritativeCrimeState(
    ConnectedClient& c, std::string requestId, bool accepted, CrimeError error)
{
    c.player.bounty = c.player.crimeState.bounty;
    PacketPlayerBounty packet;
    packet.mode = PacketPlayerBounty::Mode::Result;
    packet.resultRequestId = std::move(requestId);
    packet.accepted = accepted;
    packet.error = error;
    packet.setPlayer(&c.player);
    sendTo(c.conn, packet.encode());
    if (c.player.crimeState.bounty <= 0)
        clearOutstandingCrimePursuitsForCharacter(c);
}

void MPServer::sendAuthoritativeTopicState(ConnectedClient& c)
{
    PacketPlayerTopic packet;
    packet.action = PacketPlayerTopic::Action::Set;
    packet.setPlayer(&c.player);
    sendTo(c.conn, packet.encode());
}

void MPServer::sendAuthoritativeFactionState(
    ConnectedClient& c, std::string requestId, bool accepted, FactionError error)
{
    PacketPlayerFaction packet;
    packet.mode = PacketPlayerFaction::Mode::Result;
    packet.resultRequestId = std::move(requestId);
    packet.accepted = accepted;
    packet.error = error;
    packet.setPlayer(&c.player);
    sendTo(c.conn, packet.encode());
}

// ---------------------------------------------------------------------------
void MPServer::sendAuthoritativeEquipment(ConnectedClient& c, bool includeOthers, bool includeSelf)
{
    PacketPlayerEquipment equipment;
    equipment.setPlayer(&c.player);
    const std::vector<uint8_t> encoded = equipment.encode();
    if (includeSelf)
        sendTo(c.conn, encoded);
    if (includeOthers)
        broadcastToAll(encoded, c.conn);
}

std::string MPServer::journalGroupFor(const ConnectedClient& c) const
{
    const std::string account = lowerAscii(c.loginName);
    const std::string character = lowerAscii(c.slotName);
    for (const JournalSharingGroup& group : mJournalSharingGroups)
    {
        for (const JournalGroupMember& member : group.members)
        {
            if (lowerAscii(member.account) != account)
                continue;
            if (member.character.empty() || lowerAscii(member.character) == character)
                return group.name;
        }
    }
    return {};
}

bool MPServer::shouldShareJournal(const ConnectedClient& source, const ConnectedClient& target) const
{
    if (source.conn == target.conn || !target.charSelectComplete)
        return false;
    if (mJournalSharingMode == JournalSharingMode::Server)
        return true;
    if (mJournalSharingMode != JournalSharingMode::Group)
        return false;

    const std::string sourceGroup = journalGroupFor(source);
    return !sourceGroup.empty() && sourceGroup == journalGroupFor(target);
}

std::vector<int64_t> MPServer::journalSourceCharacterIds(const ConnectedClient& c)
{
    if (!mPlayerDb || c.dbCharacterId <= 0)
        return {};
    if (mJournalSharingMode == JournalSharingMode::Player)
        return { c.dbCharacterId };

    const std::vector<JournalCharacterIdentity> identities = mPlayerDb->listJournalCharacterIdentities();
    if (mJournalSharingMode == JournalSharingMode::Server)
    {
        std::vector<int64_t> ids;
        ids.reserve(identities.size());
        for (const JournalCharacterIdentity& identity : identities)
            ids.push_back(identity.characterId);
        return ids;
    }

    const std::string groupName = journalGroupFor(c);
    if (groupName.empty())
        return { c.dbCharacterId };

    const auto group = std::find_if(mJournalSharingGroups.begin(), mJournalSharingGroups.end(),
        [&](const JournalSharingGroup& value) { return value.name == groupName; });
    if (group == mJournalSharingGroups.end())
        return { c.dbCharacterId };

    std::vector<int64_t> ids;
    for (const JournalCharacterIdentity& identity : identities)
    {
        const std::string account = lowerAscii(identity.accountName);
        const std::string character = lowerAscii(identity.characterName);
        const bool matches = std::any_of(group->members.begin(), group->members.end(),
            [&](const JournalGroupMember& member) {
                return lowerAscii(member.account) == account
                    && (member.character.empty() || lowerAscii(member.character) == character);
            });
        if (matches)
            ids.push_back(identity.characterId);
    }

    if (std::find(ids.begin(), ids.end(), c.dbCharacterId) == ids.end())
        ids.push_back(c.dbCharacterId);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

void MPServer::sendAuthoritativeJournal(ConnectedClient& c)
{
    std::vector<BasePlayer::JournalItem> items;
    std::vector<int64_t> sourceIds;
    if (mPlayerDb && c.dbCharacterId > 0)
    {
        sourceIds = journalSourceCharacterIds(c);
        items = mPlayerDb->loadCharacterJournals(sourceIds);
    }

    BasePlayer payload;
    payload.guid = c.guid;
    std::size_t offset = 0;
    bool first = true;
    do
    {
        const std::size_t count = std::min<std::size_t>(
            PacketPlayerJournal::MaxItems, items.size() - offset);
        payload.journalChanges.action = first ? BasePlayer::JournalChanges::Action::Set
                                              : BasePlayer::JournalChanges::Action::Append;
        payload.journalChanges.snapshotComplete = offset + count == items.size();
        payload.journalChanges.items.assign(items.begin() + offset, items.begin() + offset + count);

        PacketPlayerJournal journal;
        journal.setPlayer(&payload);
        sendTo(c.conn, journal.encode());
        first = false;
        offset += count;
    } while (offset < items.size());

    Log(Debug::Verbose) << "[PlayerDB] sent authoritative journal"
                        << " guid=" << c.guid
                        << " charId=" << c.dbCharacterId
                        << " sourceCharacters=" << sourceIds.size()
                        << " items=" << items.size();
}

// ---------------------------------------------------------------------------
void MPServer::sendPlayerStateBootstrapToClient(ConnectedClient& receiver)
{
    if (!receiver.charSelectComplete)
        return;

    std::size_t sent = 0;
    for (auto& [conn, client] : mClients)
    {
        if (conn == receiver.conn || !client.charSelectComplete)
            continue;

        PacketPlayerBaseInfo baseInfo;
        baseInfo.setPlayer(&client.player);
        sendTo(receiver.conn, baseInfo.encode());

        PacketPlayerCellChange cellChange;
        cellChange.setPlayer(&client.player);
        sendTo(receiver.conn, cellChange.encode());

        PacketPlayerEquipment equipment;
        equipment.setPlayer(&client.player);
        sendTo(receiver.conn, equipment.encode());

        PacketPlayerVehicleState vehicleState;
        vehicleState.setPlayer(&client.player);
        sendTo(receiver.conn, vehicleState.encode());

        if (client.player.position.pos[0] != 0.f
            || client.player.position.pos[1] != 0.f
            || client.player.position.pos[2] != 0.f)
        {
            PacketPlayerPosition positionPacket;
            positionPacket.setPlayer(&client.player);
            sendTo(receiver.conn, positionPacket.encode());
        }

        ++sent;
    }

    if (sent != 0)
    {
        Log(Debug::Verbose) << "[Server] Sent player bootstrap to " << receiver.name
                            << " players=" << sent
                            << " cell=" << makeCellKey(receiver.player.cell);
    }
}

// ---------------------------------------------------------------------------
bool MPServer::grantInventoryItem(ConnectedClient& c, const std::string& refId, int count)
{
    if (refId.empty() || count <= 0 || !isAuthoritativeRecordReference(refId))
        return false;

    auto& items = c.player.inventoryChanges.items;
    auto it = std::find_if(items.begin(), items.end(), [&](const Item& item) {
        return item.refId == refId && item.charge == -1 && item.soul.empty();
    });

    if (it != items.end())
    {
        it->count += count;
    }
    else
    {
        Item item;
        item.refId = refId;
        item.count = count;
        item.charge = -1;
        item.enchantmentCharge = -1.f;
        items.push_back(std::move(item));
    }

    c.player.inventoryChanges.action = BasePlayer::InventoryChanges::Action::Set;
    reconcileInventoryInstanceIds(c, c.player.inventoryChanges.items);
    ++c.inventoryRevision;
    c.player.inventoryChanges.revision = c.inventoryRevision;

    if (mPlayerDb && c.dbCharacterId != 0)
        mPlayerDb->saveCharacterInventory(c.dbCharacterId, c.player.inventoryChanges.items, true,
            c.inventoryRevision);

    sendAuthoritativeInventory(c);
    syncLuaPlayerSnapshot();
    scheduleGeneratedDynamicRecordGc("grant_inventory_item");
    return true;
}

// ---------------------------------------------------------------------------
bool MPServer::removePlacedObjectAuthoritative(uint32_t mpNum, const std::string& cellId)
{
    if (mpNum == 0 || cellId.empty())
        return false;

    auto objectsIt = mWorld.placedObjects.find(cellId);
    if (objectsIt == mWorld.placedObjects.end())
        return false;

    auto& objects = objectsIt->second;
    const auto matchIt = std::find_if(objects.begin(), objects.end(),
        [&](const PlacedObject& object) { return object.mpNum == mpNum; });
    if (matchIt == objects.end())
        return false;

    mLua.removePlacedObject(mpNum);

    objects.erase(std::remove_if(objects.begin(), objects.end(),
        [&](const PlacedObject& object) { return object.mpNum == mpNum; }),
        objects.end());
    if (objects.empty())
    {
        mWorld.placedObjects.erase(objectsIt);
    }

    if (mPlayerDb)
        mPlayerDb->deleteWorldObject(mpNum);

    scheduleGeneratedDynamicRecordGc("remove_placed_object_authoritative");

    PacketObjectDelete pkt;
    pkt.mpNum = mpNum;
    pkt.cellId = cellId;
    broadcastToCell(cellId, pkt.encode());
    return true;
}

bool MPServer::removePlacedObjectAuthoritativeAnyCell(uint32_t mpNum, const std::string& preferredCellId)
{
    if (mpNum == 0)
        return false;

    if (!preferredCellId.empty() && removePlacedObjectAuthoritative(mpNum, preferredCellId))
        return true;

    for (const auto& [cellId, objects] : mWorld.placedObjects)
    {
        if (cellId == preferredCellId)
            continue;

        const auto it = std::find_if(objects.begin(), objects.end(),
            [&](const PlacedObject& object) { return object.mpNum == mpNum; });
        if (it != objects.end())
            return removePlacedObjectAuthoritative(mpNum, cellId);
    }

    return false;
}

// ---------------------------------------------------------------------------
void MPServer::startAdminHttpServer()
{
    if (!mAdminHttpEnabled)
        return;

    if (!mAdminHttpServer)
    {
        mAdminHttpServer = std::make_unique<AdminHttpServer>(
            [this](std::string_view action, const std::map<std::string, std::string>& query) {
                return handleAdminHttpRequest(action, query);
            });
    }

    std::string error;
    if (!mAdminHttpServer->start(mAdminHttpHost, mAdminHttpPort, &error))
    {
        Log(Debug::Warning) << "[Server] Failed to start admin HTTP listener on "
                            << mAdminHttpHost << ":" << mAdminHttpPort
                            << " error=" << error;
        mAdminHttpServer.reset();
        return;
    }

    Log(Debug::Info) << "[Server] Admin HTTP browser listening at " << mAdminHttpServer->url();
}

void MPServer::stopAdminHttpServer()
{
    if (!mAdminHttpServer)
        return;

    mAdminHttpServer->stop();
    mAdminHttpServer.reset();
}

AdminHttpServer::Response MPServer::handleAdminHttpRequest(
    std::string_view action, const std::map<std::string, std::string>& query)
{
    AdminHttpServer::Response response;
    response.contentType = "application/json; charset=utf-8";

    if (action == "shutdown")
    {
        Log(Debug::Info) << "[Server] Admin HTTP shutdown requested";
        requestStop();
        response.body = "{\"ok\":true,\"status\":\"shutting_down\"}";
        return response;
    }

    if (action == "reset_cell")
    {
        const auto cellIt = query.find("cell");
        if (cellIt == query.end() || cellIt->second.empty())
        {
            response.status = 400;
            response.body = makeJsonErrorBody("cell_required");
            return response;
        }

        const auto parsedCell = parseCellKey(cellIt->second);
        if (!parsedCell)
        {
            response.status = 400;
            response.body = makeJsonErrorBody("invalid_cell");
            return response;
        }

        const std::string cellId = makeCellKey(*parsedCell);
        if (!resetCellStateForTesting(cellId))
        {
            response.status = 500;
            response.body = makeJsonErrorBody("reset_cell_failed");
            return response;
        }

        Log(Debug::Info) << "[Server] Admin HTTP reset cell requested cell=" << cellId;
        response.body = "{\"ok\":true,\"status\":\"reset\",\"cell\":\"" + cellId + "\"}";
        return response;
    }

    if (action == "reset_all_cells")
    {
        const std::size_t resetCells = resetAllCellStatesForTesting();
        Log(Debug::Info) << "[Server] Admin HTTP reset all cells requested cells=" << resetCells;
        response.body = "{\"ok\":true,\"status\":\"reset\",\"cells\":" + std::to_string(resetCells) + "}";
        return response;
    }

    if (!mLua.isLoaded() || !mLua.isRunning())
    {
        response.status = 503;
        response.body = makeJsonErrorBody("lua_service_unavailable");
        return response;
    }

    if (action == "bardcraft_command" || action == "chat_command")
    {
        const auto guidIt = query.find("guid");
        const auto messageIt = query.find("message");
        if (guidIt == query.end() || messageIt == query.end() || messageIt->second.empty())
        {
            response.status = 400;
            response.body = makeJsonErrorBody("guid_and_message_required");
            return response;
        }

        uint32_t guid = 0;
        const std::string& guidText = guidIt->second;
        const auto [end, error] = std::from_chars(guidText.data(), guidText.data() + guidText.size(), guid);
        if (error != std::errc() || end != guidText.data() + guidText.size() || guid == 0)
        {
            response.status = 400;
            response.body = makeJsonErrorBody("invalid_guid");
            return response;
        }

        if (action == "chat_command"
            && (messageIt->second == "/observe" || messageIt->second.starts_with("/observe ")
                || messageIt->second == "/crimewitness" || messageIt->second.starts_with("/crimewitness ")))
        {
            {
                std::lock_guard lock(mPendingAdminDiagnosticMutex);
                mPendingAdminDiagnosticCommands.emplace_back(guid, messageIt->second);
            }
            response.body = "{\"ok\":true,\"status\":\"queued\"}";
            return response;
        }

        const auto player = mLua.getPlayer(guid);
        if (!player)
        {
            response.status = 404;
            response.body = makeJsonErrorBody("player_not_found");
            return response;
        }

        LuaWireTable payload;
        payload.emplace_back("guid", static_cast<double>(guid));
        payload.emplace_back("message", messageIt->second);
        std::string errorMessage;
        const bool isBardcraftCommand = action == "bardcraft_command";
        const auto resultData = mLua.callSynchronousInterface(
            isBardcraftCommand ? "BardcraftAdmin" : "IntentPolicy",
            isBardcraftCommand ? "handleCommand" : "handleChatCommand",
            serializeLuaWireTable(payload), mAdminHttpTimeoutMs, &errorMessage);
        if (!resultData)
        {
            response.status = errorMessage == "timeout" ? 504 : 500;
            response.body = makeJsonErrorBody(
                errorMessage.empty() ? "bardcraft_command_failed" : errorMessage);
            return response;
        }

        const LuaWireTable result = parseLuaWireTable(*resultData);
        if (!getLuaBoolField(result, "ok"))
        {
            response.status = 400;
            response.body = makeJsonErrorBody(getLuaStringField(result, "error", "command_not_handled"));
            return response;
        }

        Log(Debug::Info) << "[Server] Admin HTTP processed "
                         << (isBardcraftCommand ? "Bardcraft" : "chat") << " command guid=" << guid
                         << " name=" << player->name;
        response.body = "{\"ok\":true,\"status\":\"processed\"}";
        return response;
    }

    LuaWireTable payload;
    payload.emplace_back("action", std::string(action));
    for (const auto& [key, value] : query)
        payload.emplace_back(key, value);

    std::string error;
    const auto resultData = mLua.callSynchronousInterface(
        "IntentPolicy", "handleAdminUiHttp", serializeLuaWireTable(payload), mAdminHttpTimeoutMs, &error);
    if (!resultData)
    {
        response.status = error == "timeout" ? 504 : 500;
        response.body = makeJsonErrorBody(error.empty() ? "admin_http_request_failed" : error);
        return response;
    }

    try
    {
        const LuaWireTable result = parseLuaWireTable(*resultData);
        response.status = std::max(100, std::min(599, static_cast<int>(getLuaNumberField(result, "status", 200.0))));
        response.contentType = getLuaStringField(result, "contentType", response.contentType);
        response.body = getLuaStringField(result, "body", "{}");
        if (response.body.empty())
            response.body = "{}";
    }
    catch (const std::exception& e)
    {
        response.status = 500;
        response.body = makeJsonErrorBody(std::string("invalid_admin_http_response:") + e.what());
    }

    return response;
}

// ---------------------------------------------------------------------------
void MPServer::syncLuaAuthorityState()
{
    std::vector<PlacedObject> placedObjects;
    for (const auto& [cellId, objects] : mWorld.placedObjects)
    {
        placedObjects.insert(placedObjects.end(), objects.begin(), objects.end());
    }

    mLua.syncPlacedObjects(std::move(placedObjects));
}

// ---------------------------------------------------------------------------
EResult MPServer::sendPacketOnConfiguredLane(HSteamNetConnection conn,
                                              const std::vector<uint8_t>& data,
                                              int flags)
{
    if (data.empty())
        return k_EResultInvalidParam;

    PacketHeader header;
    const bool hasHeader = BasePacket::peekHeader(data.data(), data.size(), header);
    const PacketType type = hasHeader ? static_cast<PacketType>(header.type) : PacketType::Handshake;
    const bool actorPacket = hasHeader
        && header.type >= static_cast<uint16_t>(PacketType::ActorList)
        && header.type <= static_cast<uint16_t>(PacketType::ActorAttackV2);
    // CharacterData is the client's enter-world gate. Route it and its
    // authoritative crime-state prerequisite on the same
    // reliable lane as ActorDeath so a pre-world corpse bootstrap cannot be
    // overtaken by the load trigger merely because actor and system packets use
    // separate SteamNetworkingSockets lanes.
    const bool characterBootstrapPacket = hasHeader
        && (type == PacketType::CharacterData || type == PacketType::PlayerBounty);

    if (!actorPacket && !characterBootstrapPacket)
    {
        return mInterface->SendMessageToConnection(
            conn, data.data(), static_cast<uint32_t>(data.size()), flags, nullptr);
    }

    SteamNetworkingMessage_t* message = SteamNetworkingUtils()->AllocateMessage(
        static_cast<int>(data.size()));
    if (!message)
        return k_EResultFail;
    std::memcpy(message->m_pData, data.data(), data.size());
    message->m_conn = conn;
    message->m_nFlags = flags;
    const bool realtimeActorPacket = characterBootstrapPacket
        || type == PacketType::ActorPosition
        || type == PacketType::ActorAnimFlags
        || type == PacketType::ActorAnimPlay
        || type == PacketType::ActorAttack
        || type == PacketType::ActorCast
        || type == PacketType::ActorDeath
        || type == PacketType::ActorSpeech
        || type == PacketType::ActorCombatRequest
        || type == PacketType::ActorPositionV2
        || type == PacketType::ActorPresentationV2
        || type == PacketType::ActorAttackV2;
    message->m_idxLane = realtimeActorPacket ? 1 : 2;

    int64 result = 0;
    mInterface->SendMessages(1, &message, &result);
    return result < 0 ? static_cast<EResult>(-result) : k_EResultOK;
}

// ---------------------------------------------------------------------------
void MPServer::broadcastToAll(const std::vector<uint8_t>& data,
                              HSteamNetConnection except, bool reliable)
{
    int flags = reliable ? k_nSteamNetworkingSend_Reliable
                         : k_nSteamNetworkingSend_UnreliableNoDelay;
    for (auto& [conn, client] : mClients)
    {
        if (conn == except || !client.charSelectComplete) continue;
        sendPacketOnConfiguredLane(conn, data, flags);
    }
}

void MPServer::sendTo(HSteamNetConnection conn,
                      const std::vector<uint8_t>& data, bool reliable)
{
    int flags = reliable ? k_nSteamNetworkingSend_Reliable
                         : k_nSteamNetworkingSend_UnreliableNoDelay;
    sendPacketOnConfiguredLane(conn, data, flags);
}

void MPServer::broadcastToCell(const std::string& cellId,
                               const std::vector<uint8_t>& data,
                               HSteamNetConnection except,
                               bool reliable)
{
    int flags = reliable ? k_nSteamNetworkingSend_Reliable
                         : k_nSteamNetworkingSend_UnreliableNoDelay;
    for (auto& [conn, client] : mClients)
    {
        if (conn == except || !client.charSelectComplete) continue;
        if (!clientHasActorCellLoaded(client, cellId)) continue;
        sendPacketOnConfiguredLane(conn, data, flags);
    }
}

void MPServer::broadcastActorToCell(const std::string& cellId,
                                    const std::vector<uint8_t>& data,
                                    HSteamNetConnection except,
                                    bool reliable)
{
    int flags = reliable ? k_nSteamNetworkingSend_Reliable
                         : k_nSteamNetworkingSend_UnreliableNoDelay;
    for (auto& [conn, client] : mClients)
    {
        if (conn == except || !clientHasActorCellLoaded(client, cellId)) continue;
        sendPacketOnConfiguredLane(conn, data, flags);
    }
}

// ---------------------------------------------------------------------------
void MPServer::staticConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
    if (sInstance) sInstance->onConnectionStatusChanged(info);
}

void MPServer::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
    switch (info->m_info.m_eState)
    {
        case k_ESteamNetworkingConnectionState_Connecting:
            // Accept all incoming connections
            if (mInterface->AcceptConnection(info->m_hConn) != k_EResultOK)
            {
                mInterface->CloseConnection(info->m_hConn, 0, "Accept failed", false);
                return;
            }
            onClientConnected(info->m_hConn);
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            onClientDisconnected(info->m_hConn, info->m_info.m_szEndDebug);
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleChallengeResponse(ConnectedClient& c,
                                        const uint8_t* data, size_t size)
{
    // Must have an outstanding challenge - ignore if there isn't one.
    if (c.pendingPublicKey.empty()) return;

    PacketChallengeResponse pkt;
    if (!pkt.decode(data, size)) return;

    // Load the stored public key from its base64 representation directly into
    // a GNS key object - no manual base64 decode needed.
    CECSigningPublicKey pubKey;
    if (!pubKey.SetFromBase64EncodedString(c.pendingPublicKey.c_str()) || !pubKey.IsValid())
    {
        mInterface->CloseConnection(c.conn, 0, "Bad keypair", true);
        return;
    }

    // Verify the Ed25519 signature using the GNS C++ API.
    CryptoSignature_t sig;
    std::memcpy(sig, pkt.signature, 64);
    if (!pubKey.VerifySignature(c.pendingChallenge, 32, sig))
    {
        PacketHandshakeResponse rsp;
        rsp.accepted     = false;
        rsp.rejectReason = "Keypair verification failed.";
        sendTo(c.conn, rsp.encode());
        mInterface->CloseConnection(c.conn, 0, "Bad signature", true);
        Log(Debug::Warning) << "[Auth] Bad signature from " << c.loginName;
        return;
    }

    Log(Debug::Info) << "[Auth] Keypair auth verified for " << c.loginName;
    c.pendingPublicKey.clear();

    // Auth succeeded via keypair - proceed exactly as a normal accepted handshake.
    c.player.guid       = c.guid;
    c.player.name       = c.loginName;
    c.handshakeComplete = true;

    if (mWorld.hostGuid == 0)
        mWorld.hostGuid = c.guid;

    PacketHandshakeResponse rsp;
    rsp.accepted      = true;
    rsp.assignedGuid  = c.guid;
    rsp.serverVersion = MultiplayerBuildVersion;
    rsp.actorSyncProtocolVersion = c.actorSyncProtocolVersion;
    populateRuntimeManifest(rsp);
    sendTo(c.conn, rsp.encode());
    sendServerLuaPackages(c.conn);

    Log(Debug::Info) << "[Server] Keypair handshake accepted: " << c.name
                     << " guid=" << c.guid;

    PacketCharacterList charListPkt;
    if (mPlayerDb && c.dbAccountId > 0)
    {
        try
        {
            for (const auto& cs : mPlayerDb->listCharacters(c.dbAccountId))
            {
                CharacterEntry entry;
                entry.name      = cs.name;
                entry.race      = cs.race;
                entry.className = cs.className;
                entry.lastSeen  = cs.lastSeen;
                entry.isNew     = cs.isNew;
                charListPkt.characters.push_back(std::move(entry));
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[PlayerDB] listCharacters error: " << e.what();
        }
    }
    sendTo(c.conn, charListPkt.encode());
    Log(Debug::Info) << "[Server] Sent " << charListPkt.characters.size()
                     << " character(s) to " << c.name;
}

// ---------------------------------------------------------------------------
void MPServer::handleLinkKeyRequest(ConnectedClient& c,
                                     const uint8_t* data, size_t size)
{
    if (!c.handshakeComplete || !mPlayerDb || c.dbAccountId <= 0) return;

    PacketLinkKeyRequest pkt;
    if (!pkt.decode(data, size)) return;

    if (pkt.publicKey.empty()) return;

    // Check the key isn't already registered globally.
    if (mPlayerDb->lookupAccountByKeypair(pkt.publicKey) >= 0)
    {
        Log(Debug::Warning) << "[Auth] LinkKey: key already registered for "
                            << c.loginName;
        return; // silently ignore - client considers itself linked already
    }

    try
    {
        mPlayerDb->addKeypair(c.dbAccountId, pkt.publicKey,
                              pkt.label.empty() ? "linked machine" : pkt.label);
        Log(Debug::Info) << "[Auth] Keypair linked for " << c.loginName
                         << " label='" << pkt.label << "'";
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "[Auth] addKeypair error: " << e.what();
    }
}

// ---------------------------------------------------------------------------
void MPServer::handleUnlinkKeyRequest(ConnectedClient& c,
                                       const uint8_t* data, size_t size)
{
    if (!c.handshakeComplete || !mPlayerDb || c.dbAccountId <= 0) return;

    PacketUnlinkKeyRequest pkt;
    if (!pkt.decode(data, size)) return;

    if (pkt.publicKey.empty()) return;

    // Only allow removing a key that belongs to this account.
    const int64_t owner = mPlayerDb->lookupAccountByKeypair(pkt.publicKey);
    if (owner != c.dbAccountId)
    {
        Log(Debug::Warning) << "[Auth] UnlinkKey: key not owned by " << c.loginName;
        return;
    }

    try
    {
        // Simple DELETE - use a prepared statement via exec since we don't have
        // a dedicated removeKeypair method; add one to PlayerDatabase.
        // For now find and delete by public_key.
        mPlayerDb->removeKeypair(pkt.publicKey);
        Log(Debug::Info) << "[Auth] Keypair unlinked for " << c.loginName;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "[Auth] removeKeypair error: " << e.what();
    }
}


// ---------------------------------------------------------------------------
void MPServer::handleDeleteCharRequest(ConnectedClient& c,
                                        const uint8_t* data, size_t size)
{
    if (!c.handshakeComplete || c.charSelectComplete || !mPlayerDb || c.dbAccountId <= 0)
        return;

    PacketDeleteCharRequest pkt;
    if (!pkt.decode(data, size) || pkt.charName.empty()) return;

    // Refuse to delete a character that is live in another session.
    for (const auto& [conn, other] : mClients)
    {
        if (&other == &c) continue;
        if (other.handshakeComplete && other.charSelectComplete
            && other.dbAccountId == c.dbAccountId
            && other.slotName == pkt.charName)
        {
            PacketDeleteCharResponse rsp;
            rsp.success  = false;
            rsp.charName = pkt.charName;
            rsp.error    = "'" + pkt.charName + "' is currently in-world in another session.";
            sendTo(c.conn, rsp.encode());
            return;
        }
    }

    PacketDeleteCharResponse rsp;
    rsp.charName = pkt.charName;
    try
    {
        rsp.success = mPlayerDb->deleteCharacter(c.dbAccountId, pkt.charName);
        if (!rsp.success)
            rsp.error = "Character '" + pkt.charName + "' not found on this account.";
        else
            Log(Debug::Info) << "[Server] Deleted character '" << pkt.charName
                             << "' for " << c.loginName;
    }
    catch (const std::exception& e)
    {
        rsp.success = false;
        rsp.error   = "Server error during deletion.";
        Log(Debug::Warning) << "[Server] deleteCharacter error: " << e.what();
    }
    sendTo(c.conn, rsp.encode());
}

} // namespace mwmp
