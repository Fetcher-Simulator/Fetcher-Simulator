#include "DynamicRecordService.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include <components/openmw-mp/Packets/Records/PacketRecordCreateResult.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>

namespace mwmp
{
    namespace
    {
        std::vector<uint8_t> encodeResult(const records::RecordCreateResult& result)
        {
            PacketRecordCreateResult packet;
            packet.result = result;
            return packet.encode();
        }

        std::string asString(const std::vector<uint8_t>& bytes)
        {
            return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        }

        void resolveTemporaryReference(records::DynamicRecordDefinition& definition,
            const std::unordered_map<std::string, std::string>& ids)
        {
            std::visit(
                [&](auto& record) {
                    using Record = std::decay_t<decltype(record)>;
                    if constexpr (std::is_same_v<Record, records::Weapon> || std::is_same_v<Record, records::Armor>
                        || std::is_same_v<Record, records::Clothing> || std::is_same_v<Record, records::Book>)
                    {
                        if (record.enchantment.kind != records::ReferenceKind::TemporaryKey)
                            return;
                        auto it = ids.find(record.enchantment.value);
                        if (it == ids.end())
                            throw std::runtime_error("unresolved temporary record reference");
                        record.enchantment.kind = records::ReferenceKind::ContentId;
                        record.enchantment.value = it->second;
                    }
                },
                definition.data);
        }

        records::CreateError mapValidationError(const records::ValidationError& error)
        {
            if (error.code == "unsupported_schema")
                return records::CreateError::UnsupportedSchema;
            if (error.code.find("dependency") != std::string::npos)
                return records::CreateError::InvalidDependency;
            if (error.code == "invalid_asset_path")
                return records::CreateError::InvalidAsset;
            if (error.code.find("effect") != std::string::npos)
                return records::CreateError::InvalidEffect;
            return records::CreateError::InvalidDefinition;
        }

        records::CreateError validateAuthoritativeReferences(
            const records::DynamicRecordBundle& bundle, const DynamicRecordService::Context& context)
        {
            const auto contentAllowed = [&](std::string_view id) {
                return id.empty() || (context.isContentIdAllowed && context.isContentIdAllowed(id));
            };
            const auto assetAllowed = [&](std::string_view path) {
                return path.empty() || (context.isAssetAllowed && context.isAssetAllowed(path));
            };
            const auto modelAllowed = [&](std::string_view path) {
                return path.empty() || (context.isModelAllowed ? context.isModelAllowed(path) : assetAllowed(path));
            };
            const auto iconAllowed = [&](std::string_view path) {
                return path.empty() || (context.isIconAllowed ? context.isIconAllowed(path) : assetAllowed(path));
            };

            records::CreateError error = records::CreateError::None;
            for (const records::RecordDraft& draft : bundle.records)
            {
                std::visit(
                    [&](const auto& record) {
                        using Record = std::decay_t<decltype(record)>;
                        if constexpr (!std::is_same_v<Record, records::Enchantment>)
                        {
                            if (!modelAllowed(record.item.model) || !iconAllowed(record.item.icon))
                                error = records::CreateError::InvalidAsset;
                            else if (!contentAllowed(record.item.scriptId))
                                error = records::CreateError::ContentMismatch;
                        }
                        if constexpr (std::is_same_v<Record, records::Potion>
                            || std::is_same_v<Record, records::Enchantment>)
                        {
                            for (const records::MagicEffect& effect : record.effects)
                            {
                                if (!contentAllowed(effect.effectId) || !contentAllowed(effect.skillId)
                                    || !contentAllowed(effect.attributeId))
                                    error = records::CreateError::ContentMismatch;
                            }
                        }
                        if constexpr (std::is_same_v<Record, records::Weapon>
                            || std::is_same_v<Record, records::Armor>
                            || std::is_same_v<Record, records::Clothing>
                            || std::is_same_v<Record, records::Book>)
                        {
                            if (record.enchantment.kind == records::ReferenceKind::ContentId
                                && !contentAllowed(record.enchantment.value))
                                error = records::CreateError::ContentMismatch;
                        }
                        if constexpr (std::is_same_v<Record, records::Armor>
                            || std::is_same_v<Record, records::Clothing>)
                        {
                            for (const records::BodyPartReference& part : record.parts)
                            {
                                if (!contentAllowed(part.maleId) || !contentAllowed(part.femaleId))
                                    error = records::CreateError::ContentMismatch;
                            }
                        }
                    },
                    draft.definition.data);
                if (error != records::CreateError::None)
                    break;
            }
            return error;
        }

        std::vector<std::string> dependencyOrder(const records::DynamicRecordBundle& bundle)
        {
            std::unordered_map<std::string, const records::RecordDraft*> drafts;
            std::unordered_map<std::string, std::vector<std::string>> dependencies;
            for (const auto& draft : bundle.records)
                drafts.emplace(draft.temporaryKey, &draft);
            for (const auto& edge : bundle.dependencies)
                dependencies[edge.ownerKey].push_back(edge.dependencyKey);

            // A typed temporary reference is a dependency even when the caller
            // omitted the redundant explicit graph edge.
            for (const auto& draft : bundle.records)
            {
                std::visit(
                    [&](const auto& record) {
                        using Record = std::decay_t<decltype(record)>;
                        if constexpr (std::is_same_v<Record, records::Weapon>
                            || std::is_same_v<Record, records::Armor>
                            || std::is_same_v<Record, records::Clothing>
                            || std::is_same_v<Record, records::Book>)
                        {
                            if (record.enchantment.kind == records::ReferenceKind::TemporaryKey)
                                dependencies[draft.temporaryKey].push_back(record.enchantment.value);
                        }
                    },
                    draft.definition.data);
            }

            enum class Visit : uint8_t { None, Active, Complete };
            std::unordered_map<std::string, Visit> visits;
            std::vector<std::string> order;
            std::function<void(const std::string&)> visit = [&](const std::string& key) {
                Visit& state = visits[key];
                if (state == Visit::Complete)
                    return;
                if (state == Visit::Active)
                    throw std::runtime_error("dependency cycle");
                if (!drafts.contains(key))
                    throw std::runtime_error("missing dependency endpoint");
                state = Visit::Active;
                auto& values = dependencies[key];
                std::sort(values.begin(), values.end());
                values.erase(std::unique(values.begin(), values.end()), values.end());
                for (const std::string& dependency : values)
                    visit(dependency);
                state = Visit::Complete;
                order.push_back(key);
            };
            for (const auto& draft : bundle.records)
                visit(draft.temporaryKey);
            return order;
        }

        std::vector<std::string> resolvedDependencies(const records::DynamicRecordBundle& bundle,
            std::string_view ownerKey, const std::unordered_map<std::string, std::string>& ids,
            const records::DynamicRecordDefinition& definition)
        {
            std::vector<std::string> result;
            for (const auto& edge : bundle.dependencies)
            {
                if (edge.ownerKey != ownerKey)
                    continue;
                auto it = ids.find(edge.dependencyKey);
                if (it == ids.end())
                    throw std::runtime_error("unresolved dependency ID");
                result.push_back(it->second);
            }
            std::visit(
                [&](const auto& record) {
                    using Record = std::decay_t<decltype(record)>;
                    if constexpr (std::is_same_v<Record, records::Weapon> || std::is_same_v<Record, records::Armor>
                        || std::is_same_v<Record, records::Clothing> || std::is_same_v<Record, records::Book>)
                    {
                        if (record.enchantment.kind == records::ReferenceKind::ContentId
                            && !record.enchantment.value.empty())
                            result.push_back(record.enchantment.value);
                    }
                },
                definition.data);
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }
    }

    records::RecordCreateResult DynamicRecordService::makeError(
        std::string requestId, records::CreateError error, uint64_t inventoryRevision)
    {
        records::RecordCreateResult result;
        result.requestId = std::move(requestId);
        result.accepted = false;
        result.error = error;
        result.inventoryRevision = inventoryRevision;
        return result;
    }

    DynamicRecordService::PreparedRecord DynamicRecordService::prepareSingleRecord(
        const records::RecordDraft& draft, const Context& context, const FindEquivalent& findEquivalent,
        const AllocateId& allocateId) const
    {
        PreparedRecord prepared;
        records::DynamicRecordDefinition definition = records::canonicalize(draft.definition);
        const records::RecordType type = records::getRecordType(definition);
        const std::string fingerprint = records::fingerprint(definition);
        const std::string encodedDefinition = records::encodeDefinition(definition);

        prepared.created.temporaryKey = draft.temporaryKey;
        prepared.created.definition = encodedDefinition;
        const auto fixed = context.fixedRecordIds.find(draft.temporaryKey);
        if (fixed == context.fixedRecordIds.end())
        {
            if (auto equivalent = findEquivalent(type, fingerprint))
            {
                prepared.created.recordId = equivalent->recordId;
                prepared.created.reused = true;
            }
        }
        if (prepared.created.recordId.empty())
        {
            prepared.created.recordId = fixed == context.fixedRecordIds.end() ? allocateId(type) : fixed->second;
            if (prepared.created.recordId.empty())
                throw std::runtime_error("Authoritative record ID allocation failed");

            DynamicRecordCommitEntry entry;
            entry.record.recordType = std::string(records::getRecordTypeName(type));
            entry.record.recordId = prepared.created.recordId;
            entry.record.data = encodedDefinition;
            entry.record.recordScope = context.recordScope;
            entry.record.schemaVersion = records::CurrentSchemaVersion;
            entry.catalog.recordType = entry.record.recordType;
            entry.catalog.recordId = prepared.created.recordId;
            entry.catalog.recordScope = context.recordScope;
            entry.catalog.persistent = context.persistent;
            entry.catalog.definitionFingerprint = fingerprint;
            entry.catalog.creatorAccountId = context.accountId;
            entry.catalog.creatorCharacterId = context.characterId;
            entry.catalog.creationSource = context.creationSource;
            entry.catalog.schemaVersion = records::CurrentSchemaVersion;
            entry.catalog.validationVersion = context.validationVersion;
            prepared.entry = std::move(entry);
        }
        return prepared;
    }

    DynamicRecordService::Outcome DynamicRecordService::execute(const records::RecordCreateRequest& request,
        std::string_view requestHash, const Context& context, const FindEquivalent& findEquivalent,
        const AllocateId& allocateId, const NextCommitSequence& nextCommitSequence)
    {
        Outcome outcome;
        outcome.result.requestId = request.requestId;
        outcome.result.inventoryRevision = context.inventoryRevision;

        const auto existingRequest = context.serverRequestSource.empty()
            ? mDatabase.loadCraftRequest(context.accountId, context.characterId, request.requestId)
            : mDatabase.loadServerRecordRequest(context.serverRequestSource, request.requestId);
        if (const auto& existing = existingRequest)
        {
            if (existing->requestHash != requestHash)
            {
                outcome.result = makeError(
                    request.requestId, records::CreateError::DuplicateRequestConflict, context.inventoryRevision);
                outcome.encodedResult = encodeResult(outcome.result);
                return outcome;
            }
            if (existing->status == "accepted" || existing->status == "rejected")
            {
                outcome.encodedResult.assign(existing->resultPayload.begin(), existing->resultPayload.end());
                PacketRecordCreateResult packet;
                if (!packet.decode(outcome.encodedResult))
                    throw std::runtime_error("Persisted record-create result is corrupt");
                outcome.result = std::move(packet.result);
                outcome.replayed = true;
                return outcome;
            }
            outcome.result = makeError(request.requestId, records::CreateError::RequestPending,
                context.inventoryRevision);
            outcome.encodedResult = encodeResult(outcome.result);
            return outcome;
        }

        records::CreateError earlyError = records::CreateError::None;
        if (request.protocolVersion != records::CurrentCreateProtocolVersion)
            earlyError = records::CreateError::UnsupportedProtocol;
        else if (request.requestId.empty() || request.requestId.size() > 128 || requestHash.empty())
            earlyError = records::CreateError::InvalidRequest;
        else if (context.admissionError != records::CreateError::None)
            earlyError = context.admissionError;
        else if (request.inventoryRevision != context.inventoryRevision)
            earlyError = records::CreateError::StaleInventoryRevision;
        else if (request.operation == records::CreateOperation::Alchemy
            || request.operation == records::CreateOperation::Enchanting)
            // These operations must go through content-aware semantic validators;
            // accepting an arbitrary definition bundle here would be a security bug.
            earlyError = records::CreateError::CraftFailed;
        else if (request.operation == records::CreateOperation::ServerScript && !context.trustedServerRequest)
            earlyError = records::CreateError::Unauthorized;
        else if (request.operation == records::CreateOperation::CustomRecord && !context.allowCustomDefinitions)
            earlyError = records::CreateError::Unauthorized;

        const auto validationErrors = records::validate(request.bundle);
        if (earlyError == records::CreateError::None && !validationErrors.empty())
            earlyError = mapValidationError(validationErrors.front());

        if (earlyError == records::CreateError::None)
            earlyError = validateAuthoritativeReferences(request.bundle, context);

        if (earlyError == records::CreateError::None)
        {
            for (const auto& draft : request.bundle.records)
            {
                if (!context.trustedServerRequest
                    && !context.permittedTypes.contains(records::getRecordType(draft.definition)))
                {
                    earlyError = records::CreateError::RecordTypeNotPermitted;
                    break;
                }
            }
        }

        if (earlyError != records::CreateError::None)
        {
            outcome.result = makeError(request.requestId, earlyError, context.inventoryRevision);
            outcome.encodedResult = encodeResult(outcome.result);
            CraftRequestRecord journal;
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = std::string(requestHash);
            if (!request.requestId.empty() && request.requestId.size() <= 128 && !requestHash.empty())
            {
                if (context.serverRequestSource.empty())
                    mDatabase.insertRejectedCraftRequest(journal, asString(outcome.encodedResult));
                else
                    mDatabase.insertRejectedServerRecordRequest(
                        context.serverRequestSource, journal, asString(outcome.encodedResult));
            }
            return outcome;
        }

        records::DynamicRecordBundle bundle = records::canonicalize(request.bundle);
        const std::vector<std::string> order = dependencyOrder(bundle);
        std::unordered_map<std::string, const records::RecordDraft*> drafts;
        for (const auto& draft : bundle.records)
            drafts.emplace(draft.temporaryKey, &draft);

        std::unordered_map<std::string, std::string> ids;
        std::vector<DynamicRecordCommitEntry> commitEntries;
        for (const std::string& key : order)
        {
            records::DynamicRecordDefinition definition = drafts.at(key)->definition;
            resolveTemporaryReference(definition, ids);
            definition = records::canonicalize(std::move(definition));
            const records::RecordType type = records::getRecordType(definition);
            const std::string fingerprint = records::fingerprint(definition);
            const std::string encodedDefinition = records::encodeDefinition(definition);

            records::CreatedRecord created;
            created.temporaryKey = key;
            created.definition = encodedDefinition;
            const auto fixed = context.fixedRecordIds.find(key);
            if (fixed == context.fixedRecordIds.end())
            {
                if (auto equivalent = findEquivalent(type, fingerprint))
                {
                    created.recordId = equivalent->recordId;
                    created.reused = true;
                }
            }
            if (created.recordId.empty())
            {
                created.recordId = fixed == context.fixedRecordIds.end() ? allocateId(type) : fixed->second;
                if (created.recordId.empty())
                    throw std::runtime_error("Authoritative record ID allocation failed");

                CommittedRecord runtime;
                runtime.recordType = std::string(records::getRecordTypeName(type));
                runtime.recordId = created.recordId;
                runtime.definition = encodedDefinition;
                runtime.dependencyRecordIds = resolvedDependencies(bundle, key, ids, definition);
                outcome.newRecords.push_back(runtime);

                DynamicRecordCommitEntry entry;
                entry.record.recordType = runtime.recordType;
                entry.record.recordId = runtime.recordId;
                entry.record.data = runtime.definition;
                entry.record.recordScope = context.recordScope;
                entry.record.schemaVersion = records::CurrentSchemaVersion;
                entry.catalog.recordType = runtime.recordType;
                entry.catalog.recordId = runtime.recordId;
                entry.catalog.recordScope = context.recordScope;
                entry.catalog.persistent = context.persistent;
                entry.catalog.definitionFingerprint = fingerprint;
                entry.catalog.creatorAccountId = context.accountId;
                entry.catalog.creatorCharacterId = context.characterId;
                entry.catalog.creationSource = context.creationSource;
                entry.catalog.schemaVersion = records::CurrentSchemaVersion;
                entry.catalog.validationVersion = context.validationVersion;
                entry.dependencyRecordIds = runtime.dependencyRecordIds;
                commitEntries.push_back(std::move(entry));
            }
            ids.emplace(key, created.recordId);
            outcome.result.records.push_back(std::move(created));
        }

        if (commitEntries.size() > context.maximumNewRecords)
        {
            outcome.newRecords.clear();
            outcome.result = makeError(request.requestId, records::CreateError::QuotaExceeded,
                context.inventoryRevision);
            outcome.encodedResult = encodeResult(outcome.result);
            CraftRequestRecord journal;
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = std::string(requestHash);
            if (context.serverRequestSource.empty())
                mDatabase.insertRejectedCraftRequest(journal, asString(outcome.encodedResult));
            else
                mDatabase.insertRejectedServerRecordRequest(
                    context.serverRequestSource, journal, asString(outcome.encodedResult));
            return outcome;
        }

        // Publish mappings in canonical key order even though dependency
        // processing may have produced a different traversal order.
        std::sort(outcome.result.records.begin(), outcome.result.records.end(),
            [](const auto& left, const auto& right) { return left.temporaryKey < right.temporaryKey; });
        outcome.result.accepted = true;
        outcome.result.error = records::CreateError::None;
        outcome.result.commitSequence = nextCommitSequence();
        outcome.encodedResult = encodeResult(outcome.result);

        DynamicRecordCommit commit;
        commit.accountId = context.accountId;
        commit.characterId = context.characterId;
        commit.requestId = request.requestId;
        commit.requestHash = std::string(requestHash);
        commit.resultPayload = asString(outcome.encodedResult);
        commit.expectedInventoryRevision = context.inventoryRevision;
        commit.resultingInventoryRevision = context.inventoryRevision;
        commit.records = std::move(commitEntries);
        commit.serverSource = context.serverRequestSource;
        const DynamicRecordCommitStatus status = mDatabase.commitDynamicRecordRequest(commit);
        if (status == DynamicRecordCommitStatus::DuplicateRequest
            || status == DynamicRecordCommitStatus::DuplicateRequestConflict)
        {
            // A concurrent/re-entrant caller won the idempotency key. Re-enter
            // once through the replay path; no runtime state has been published.
            return execute(request, requestHash, context, findEquivalent, allocateId, nextCommitSequence);
        }
        if (status == DynamicRecordCommitStatus::StaleInventoryRevision)
        {
            outcome.newRecords.clear();
            outcome.result = makeError(request.requestId, records::CreateError::StaleInventoryRevision,
                mDatabase.loadInventoryRevision(context.characterId));
            outcome.encodedResult = encodeResult(outcome.result);
            CraftRequestRecord journal;
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = std::string(requestHash);
            if (context.serverRequestSource.empty())
                mDatabase.insertRejectedCraftRequest(journal, asString(outcome.encodedResult));
            else
                mDatabase.insertRejectedServerRecordRequest(
                    context.serverRequestSource, journal, asString(outcome.encodedResult));
        }
        return outcome;
    }
}
