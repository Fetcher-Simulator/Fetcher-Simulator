#include "ResolvedContentFingerprint.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>
#include <components/openmw-mp/Sha256.hpp>

#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    struct Entry
    {
        std::uint8_t type = 0;
        std::string id;
        std::string definition;
    };

    template <class T>
    void appendRecords(const MWWorld::ESMStore& store, mwmp::records::RecordType type, std::vector<Entry>& out)
    {
        for (const T& record : store.get<T>())
        {
            Entry entry;
            entry.type = static_cast<std::uint8_t>(type);
            entry.id = record.mId.toString();
            entry.definition = mwmp::records::encodeDefinition(
                mwmp::records::canonicalize(mwmp::records::fromEsmRecord(record)));
            out.push_back(std::move(entry));
        }
    }

    void updateU32(mwmp::crypto::Sha256& hash, std::uint32_t value)
    {
        const std::array<std::uint8_t, 4> bytes = { static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 24) };
        hash.update(bytes.data(), bytes.size());
    }

    void updateBytes(mwmp::crypto::Sha256& hash, const void* data, std::size_t size)
    {
        hash.update(static_cast<const std::uint8_t*>(data), size);
    }
}

std::string MWMP::resolvedContentFingerprint(const MWWorld::ESMStore& store)
{
    std::vector<Entry> entries;
    appendRecords<ESM::Potion>(store, mwmp::records::RecordType::Potion, entries);
    appendRecords<ESM::Enchantment>(store, mwmp::records::RecordType::Enchantment, entries);
    appendRecords<ESM::Weapon>(store, mwmp::records::RecordType::Weapon, entries);
    appendRecords<ESM::Armor>(store, mwmp::records::RecordType::Armor, entries);
    appendRecords<ESM::Clothing>(store, mwmp::records::RecordType::Clothing, entries);
    appendRecords<ESM::Book>(store, mwmp::records::RecordType::Book, entries);

    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        return std::tie(left.type, left.id) < std::tie(right.type, right.id);
    });

    mwmp::crypto::Sha256 hash;
    static constexpr std::array<std::uint8_t, 8> header = { 'O', 'M', 'R', 'C', 1, 0, 0, 0 };
    hash.update(header.data(), header.size());
    updateU32(hash, static_cast<std::uint32_t>(entries.size()));
    for (const Entry& entry : entries)
    {
        hash.update(&entry.type, 1);
        updateU32(hash, static_cast<std::uint32_t>(entry.id.size()));
        updateBytes(hash, entry.id.data(), entry.id.size());
        updateU32(hash, static_cast<std::uint32_t>(entry.definition.size()));
        updateBytes(hash, entry.definition.data(), entry.definition.size());
    }
    return hash.finish();
}
