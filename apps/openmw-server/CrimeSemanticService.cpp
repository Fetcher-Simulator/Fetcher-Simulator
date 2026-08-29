#include "CrimeSemanticService.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

#include <components/openmw-mp/Sha256.hpp>

namespace
{
    constexpr std::string_view IntentMagic = "OMCI";
    constexpr std::string_view ResultMagic = "OMCS";
    constexpr std::string_view SemanticService = "crime-event";
    constexpr float CanonicalHumanoidObservationHeight = 128.f;

    bool validType(mwmp::CrimeType type);

    class Writer
    {
    public:
        void u8(std::uint8_t value) { mBytes.push_back(static_cast<char>(value)); }
        void u16(std::uint16_t value)
        {
            u8(static_cast<std::uint8_t>(value));
            u8(static_cast<std::uint8_t>(value >> 8));
        }
        void u32(std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        void u64(std::uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
        void i64(std::int64_t value) { u64(std::bit_cast<std::uint64_t>(value)); }
        void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
        void string(std::string_view value)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Crime semantic string is too long");
            u32(static_cast<std::uint32_t>(value.size()));
            mBytes.append(value);
        }
        void fixed(std::string_view value) { mBytes.append(value); }
        std::string take() { return std::move(mBytes); }

    private:
        std::string mBytes;
    };

    class Reader
    {
    public:
        explicit Reader(std::string_view bytes)
            : mBytes(bytes)
        {
        }
        std::uint8_t u8()
        {
            need(1);
            return static_cast<std::uint8_t>(mBytes[mPosition++]);
        }
        bool boolean()
        {
            const std::uint8_t value = u8();
            if (value > 1)
                throw std::runtime_error("Invalid stored crime semantic boolean");
            return value != 0;
        }
        std::uint16_t u16()
        {
            const std::uint16_t low = u8();
            return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(u8()) << 8));
        }
        std::uint32_t u32()
        {
            std::uint32_t value = 0;
            for (int shift = 0; shift < 32; shift += 8)
                value |= static_cast<std::uint32_t>(u8()) << shift;
            return value;
        }
        std::uint64_t u64()
        {
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 8)
                value |= static_cast<std::uint64_t>(u8()) << shift;
            return value;
        }
        std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
        float f32() { return std::bit_cast<float>(u32()); }
        std::string string(std::size_t maximum)
        {
            const std::uint32_t size = u32();
            if (size > maximum)
                throw std::runtime_error("Crime semantic string exceeds limit");
            need(size);
            std::string result(mBytes.substr(mPosition, size));
            mPosition += size;
            return result;
        }
        void expect(std::string_view expected)
        {
            need(expected.size());
            if (mBytes.substr(mPosition, expected.size()) != expected)
                throw std::runtime_error("Invalid crime semantic payload magic");
            mPosition += expected.size();
        }
        void finish() const
        {
            if (mPosition != mBytes.size())
                throw std::runtime_error("Trailing crime semantic payload bytes");
        }

    private:
        void need(std::size_t count) const
        {
            if (count > mBytes.size() - mPosition)
                throw std::runtime_error("Truncated crime semantic payload");
        }
        std::string_view mBytes;
        std::size_t mPosition = 0;
    };

    bool finite(const mwmp::ObservationVector& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    float distanceSquared(const mwmp::ObservationVector& left, const mwmp::ObservationVector& right)
    {
        const float x = left.x - right.x;
        const float y = left.y - right.y;
        const float z = left.z - right.z;
        return x * x + y * y + z * z;
    }

    auto identityKey(const mwmp::ObservationActorIdentity& identity)
    {
        return std::tuple(static_cast<std::uint8_t>(identity.kind), identity.playerGuid, identity.actorInstanceId);
    }

    void writeIdentity(Writer& writer, const mwmp::ObservationActorIdentity& identity)
    {
        writer.u8(static_cast<std::uint8_t>(identity.kind));
        writer.u32(identity.playerGuid);
        writer.u64(identity.actorInstanceId);
    }

    mwmp::ObservationActorIdentity readIdentity(Reader& reader)
    {
        mwmp::ObservationActorIdentity identity;
        identity.kind = static_cast<mwmp::ObservationActorKind>(reader.u8());
        identity.playerGuid = reader.u32();
        identity.actorInstanceId = reader.u64();
        if (static_cast<std::uint8_t>(identity.kind) > static_cast<std::uint8_t>(mwmp::ObservationActorKind::Creature)
            || !identity.isValid())
            throw std::runtime_error("Invalid stored crime actor identity");
        return identity;
    }

    void writeVector(Writer& writer, const mwmp::ObservationVector& value)
    {
        writer.f32(value.x);
        writer.f32(value.y);
        writer.f32(value.z);
    }

    void writeSnapshot(Writer& writer, const mwmp::ObservationActorSnapshot& value)
    {
        writeIdentity(writer, value.identity);
        writeVector(writer, value.position);
        writeVector(writer, value.forward);
        writer.u8(value.hasFacing);
        writer.u8(value.eligibilityKnown);
        writer.u8(value.awarenessInputsKnown);
        writer.u8(value.enabled);
        writer.u8(value.alive);
        writer.u8(value.conscious);
        writer.u8(value.sneaking);
        writer.u8(value.onGround);
        writer.f32(value.sneakSkill);
        writer.f32(value.agility);
        writer.f32(value.luck);
        writer.f32(value.fatigueCurrent);
        writer.f32(value.fatigueMaximumModified);
        writer.f32(value.bootWeight);
        writer.f32(value.chameleon);
        writer.f32(value.invisibility);
        writer.f32(value.blind);
        writer.u32(value.migrationGeneration);
        writer.u32(value.authorityGeneration);
        writer.u64(value.snapshotGeneration);
        writer.u64(value.sampledAtMs);
        writer.u8(static_cast<std::uint8_t>(value.authority));
    }

    void writeState(Writer& writer, const mwmp::PlayerCrimeState& state)
    {
        writer.u16(state.schemaVersion);
        writer.i32(state.bounty);
        writer.i32(state.currentCrimeId);
        writer.i32(state.paidCrimeId);
        writer.u64(state.revision);
    }

    mwmp::PlayerCrimeState readState(Reader& reader)
    {
        mwmp::PlayerCrimeState state;
        state.schemaVersion = reader.u16();
        state.bounty = reader.i32();
        state.currentCrimeId = reader.i32();
        state.paidCrimeId = reader.i32();
        state.revision = reader.u64();
        if (mwmp::validatePlayerCrimeState(state) != mwmp::CrimeError::None)
            throw std::runtime_error("Invalid stored crime semantic state");
        return state;
    }

    void writeAggression(Writer& writer, const mwmp::CrimeAggressionResult& value)
    {
        writer.u8(value.evaluated);
        writer.u8(value.combat);
        writer.i32(value.baseFight);
        writer.i32(value.finalFight);
        writer.i32(value.crimeFight);
        writer.f32(value.dispositionTerm);
        writer.f32(value.dispositionBias);
        writer.f32(value.distance);
        writer.f32(value.distanceBias);
        writer.f32(value.alarmTerm);
        writer.f32(value.unclampedFightTerm);
        writer.f32(value.fightTerm);
    }

    mwmp::CrimeAggressionResult readAggression(Reader& reader)
    {
        mwmp::CrimeAggressionResult value;
        value.evaluated = reader.boolean();
        value.combat = reader.boolean();
        value.baseFight = reader.i32();
        value.finalFight = reader.i32();
        value.crimeFight = reader.i32();
        value.dispositionTerm = reader.f32();
        value.dispositionBias = reader.f32();
        value.distance = reader.f32();
        value.distanceBias = reader.f32();
        value.alarmTerm = reader.f32();
        value.unclampedFightTerm = reader.f32();
        value.fightTerm = reader.f32();
        if (value.baseFight < 0 || value.baseFight > 100 || value.finalFight < 0
            || value.finalFight > 100 || (value.combat && !value.evaluated)
            || !std::isfinite(value.dispositionTerm) || !std::isfinite(value.dispositionBias)
            || !std::isfinite(value.distance) || value.distance < 0.f
            || !std::isfinite(value.distanceBias) || !std::isfinite(value.alarmTerm)
            || !std::isfinite(value.unclampedFightTerm) || !std::isfinite(value.fightTerm))
            throw std::runtime_error("Invalid stored crime aggression result");
        return value;
    }

    std::string encodeIntent(const mwmp::CrimeIntent& intent)
    {
        Writer writer;
        writer.fixed(IntentMagic);
        writer.u16(intent.version);
        writer.string(intent.eventId);
        writer.string(intent.source);
        writer.u8(static_cast<std::uint8_t>(intent.type));
        writer.string(intent.cellId);
        writeSnapshot(writer, intent.offender);
        writer.u8(intent.victim.has_value());
        if (intent.victim)
            writeIdentity(writer, *intent.victim);
        writer.u8(intent.victimAware);
        writer.i64(intent.value);
        writer.u64(intent.observedAtMs);
        writer.u64(intent.maximumSnapshotAgeMs);
        writer.u32(static_cast<std::uint32_t>(intent.collisionGenerations.size()));
        for (const auto& generation : intent.collisionGenerations)
        {
            writer.string(generation.cellId);
            writer.u64(generation.generation);
        }
        return writer.take();
    }

    void writeOptionalBool(Writer& writer, const std::optional<bool>& value)
    {
        writer.u8(value ? (*value ? 2 : 1) : 0);
    }

    std::optional<bool> readOptionalBool(Reader& reader)
    {
        const std::uint8_t value = reader.u8();
        if (value > 2)
            throw std::runtime_error("Invalid stored optional boolean");
        if (value == 0)
            return std::nullopt;
        return value == 2;
    }

    void writeObservation(Writer& writer, const mwmp::ObservationResult& value)
    {
        writer.u8(value.observable);
        writeOptionalBool(writer, value.lineOfSight);
        writeOptionalBool(writer, value.awareness);
        writer.u8(value.awarenessRoll.has_value());
        if (value.awarenessRoll)
            writer.i32(*value.awarenessRoll);
        writer.u8(value.awarenessThreshold.has_value());
        if (value.awarenessThreshold)
            writer.f32(*value.awarenessThreshold);
        writer.u8(static_cast<std::uint8_t>(value.path));
        writer.u8(static_cast<std::uint8_t>(value.reason));
        writer.u8(static_cast<std::uint8_t>(value.authority));
        writer.u32(value.observerMigrationGeneration);
        writer.u32(value.observerAuthorityGeneration);
        writer.u64(value.observerSnapshotGeneration);
        writer.u64(value.targetSnapshotGeneration);
        writer.u32(static_cast<std::uint32_t>(value.collisionGenerations.size()));
        for (const auto& generation : value.collisionGenerations)
        {
            writer.string(generation.cellId);
            writer.u64(generation.generation);
        }
    }

    mwmp::ObservationResult readObservation(Reader& reader)
    {
        mwmp::ObservationResult value;
        value.observable = reader.boolean();
        value.lineOfSight = readOptionalBool(reader);
        value.awareness = readOptionalBool(reader);
        if (reader.boolean())
            value.awarenessRoll = reader.i32();
        if (reader.boolean())
            value.awarenessThreshold = reader.f32();
        value.path = static_cast<mwmp::ObservationPath>(reader.u8());
        value.reason = static_cast<mwmp::ObservationReason>(reader.u8());
        value.authority = static_cast<mwmp::ObservationAuthority>(reader.u8());
        if (static_cast<std::uint8_t>(value.path) > static_cast<std::uint8_t>(mwmp::ObservationPath::MurderHearing)
            || static_cast<std::uint8_t>(value.reason)
                > static_cast<std::uint8_t>(mwmp::ObservationReason::AwarenessFailed)
            || static_cast<std::uint8_t>(value.authority)
                > static_cast<std::uint8_t>(mwmp::ObservationAuthority::MixedDelegated)
            || (value.awarenessRoll && (*value.awarenessRoll < 0 || *value.awarenessRoll > 99))
            || (value.awarenessThreshold && !std::isfinite(*value.awarenessThreshold)))
            throw std::runtime_error("Invalid stored crime observation");
        value.observerMigrationGeneration = reader.u32();
        value.observerAuthorityGeneration = reader.u32();
        value.observerSnapshotGeneration = reader.u64();
        value.targetSnapshotGeneration = reader.u64();
        const std::uint32_t count = reader.u32();
        if (count > mwmp::MaximumCrimeWitnesses)
            throw std::runtime_error("Too many stored collision generations");
        value.collisionGenerations.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
            value.collisionGenerations.push_back({ reader.string(1024), reader.u64() });
        return value;
    }

    std::string encodeResult(const mwmp::CrimeSemanticResult& result)
    {
        Writer writer;
        writer.fixed(ResultMagic);
        writer.u16(result.version);
        writer.string(result.eventId);
        writer.u8(static_cast<std::uint8_t>(result.type));
        writer.u8(result.accepted);
        writer.u16(static_cast<std::uint16_t>(result.error));
        writer.u32(static_cast<std::uint32_t>(result.witnesses.size()));
        for (const mwmp::CrimeWitnessResult& witness : result.witnesses)
        {
            writeIdentity(writer, witness.identity);
            writer.u8(witness.candidate);
            writer.u8(witness.victim);
            writer.u8(static_cast<std::uint8_t>(witness.eligibility));
            writer.i32(witness.alarm);
            writer.i32(witness.fight);
            writer.u8(witness.guard);
            writer.u8(static_cast<std::uint8_t>(witness.relationshipAuthority));
            writer.u8(witness.observation.has_value());
            if (witness.observation)
                writeObservation(writer, *witness.observation);
            writer.u8(witness.perceived);
            writer.u8(witness.reportCapable);
            writer.u8(witness.reported);
            writeAggression(writer, witness.aggression);
        }
        writer.u8(result.crimeSeen);
        writer.u8(result.reportingStageRun);
        writer.u8(result.bountyApplied);
        writer.i32(result.bountyDelta);
        writer.u8(result.currentCrimeIdAdvanced);
        writeState(writer, result.state);
        return writer.take();
    }

    mwmp::CrimeSemanticResult decodeResult(std::string_view bytes)
    {
        Reader reader(bytes);
        reader.expect(ResultMagic);
        mwmp::CrimeSemanticResult result;
        result.version = reader.u16();
        if (result.version != mwmp::CrimeSemanticVersion)
            throw std::runtime_error("Unsupported crime semantic result version");
        result.eventId = reader.string(mwmp::MaximumSemanticRequestIdLength);
        result.type = static_cast<mwmp::CrimeType>(reader.u8());
        result.accepted = reader.boolean();
        result.error = static_cast<mwmp::CrimeSemanticError>(reader.u16());
        if (!validType(result.type)
            || static_cast<std::uint16_t>(result.error)
                > static_cast<std::uint16_t>(mwmp::CrimeSemanticError::CorruptStoredResult))
            throw std::runtime_error("Invalid stored crime semantic result header");
        const std::uint32_t count = reader.u32();
        if (count > mwmp::MaximumCrimeWitnesses)
            throw std::runtime_error("Too many stored crime witnesses");
        result.witnesses.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            mwmp::CrimeWitnessResult witness;
            witness.identity = readIdentity(reader);
            witness.candidate = reader.boolean();
            witness.victim = reader.boolean();
            witness.eligibility = static_cast<mwmp::CrimeWitnessEligibility>(reader.u8());
            witness.alarm = reader.i32();
            witness.fight = reader.i32();
            witness.guard = reader.boolean();
            witness.relationshipAuthority = static_cast<mwmp::ObservationAuthority>(reader.u8());
            if (static_cast<std::uint8_t>(witness.eligibility)
                    > static_cast<std::uint8_t>(mwmp::CrimeWitnessEligibility::RelationshipUnknown)
                || static_cast<std::uint8_t>(witness.relationshipAuthority)
                    > static_cast<std::uint8_t>(mwmp::ObservationAuthority::MixedDelegated))
                throw std::runtime_error("Invalid stored crime witness provenance");
            if (reader.boolean())
                witness.observation = readObservation(reader);
            witness.perceived = reader.boolean();
            witness.reportCapable = reader.boolean();
            witness.reported = reader.boolean();
            witness.aggression = readAggression(reader);
            result.witnesses.push_back(std::move(witness));
        }
        result.crimeSeen = reader.boolean();
        result.reportingStageRun = reader.boolean();
        result.bountyApplied = reader.boolean();
        result.bountyDelta = reader.i32();
        result.currentCrimeIdAdvanced = reader.boolean();
        result.state = readState(reader);
        reader.finish();
        if (result.eventId.empty() || result.bountyDelta < 0
            || (result.accepted && result.error != mwmp::CrimeSemanticError::None)
            || (!result.accepted && result.error == mwmp::CrimeSemanticError::None)
            || result.reportingStageRun != result.crimeSeen
            || result.currentCrimeIdAdvanced
                != (result.reportingStageRun && result.type != mwmp::CrimeType::WerewolfExposure)
            || (result.bountyDelta != 0 && !result.bountyApplied))
            throw std::runtime_error("Inconsistent stored crime semantic result");
        return result;
    }

    bool validType(mwmp::CrimeType type)
    {
        return type >= mwmp::CrimeType::Theft && type <= mwmp::CrimeType::WerewolfExposure;
    }

    bool canonicalGenerations(const std::vector<mwmp::CollisionCellGeneration>& generations)
    {
        for (std::size_t i = 0; i < generations.size(); ++i)
        {
            if (generations[i].cellId.empty() || generations[i].generation == 0
                || (i != 0 && generations[i - 1].cellId >= generations[i].cellId))
                return false;
        }
        return true;
    }

    bool validIntent(const mwmp::CrimeIntent& intent, const mwmp::CrimeSemanticService::Context& context)
    {
        if (intent.version != mwmp::CrimeSemanticVersion || intent.eventId.empty()
            || intent.eventId.size() > mwmp::MaximumSemanticRequestIdLength
            || intent.eventId.find('\0') != std::string::npos || intent.source.empty()
            || intent.source.size() > mwmp::MaximumSemanticSourceLength || intent.source.find('\0') != std::string::npos
            || intent.cellId.empty() || intent.cellId.size() > 1024 || intent.cellId.find('\0') != std::string::npos
            || !validType(intent.type) || !intent.offender.identity.isValid()
            || intent.offender.identity.kind != mwmp::ObservationActorKind::Player
            || intent.offender.identity.playerGuid != context.playerGuid || !finite(intent.offender.position)
            || intent.observedAtMs == 0 || intent.maximumSnapshotAgeMs == 0
            || intent.collisionGenerations.size() > mwmp::MaximumCrimeWitnesses
            || !canonicalGenerations(intent.collisionGenerations))
            return false;
        if (intent.victim && (!intent.victim->isValid() || *intent.victim == intent.offender.identity))
            return false;
        if (intent.victimAware && !intent.victim)
            return false;
        if ((intent.type == mwmp::CrimeType::Pickpocket || intent.type == mwmp::CrimeType::Assault
                || intent.type == mwmp::CrimeType::Murder)
            && !intent.victim)
            return false;
        if (intent.type == mwmp::CrimeType::Theft)
            return intent.value >= 0 && intent.value <= std::numeric_limits<std::int32_t>::max();
        return intent.value == 0;
    }

    std::optional<std::int32_t> bountyFor(const mwmp::CrimeIntent& intent, const mwmp::CrimePolicy& policy)
    {
        if (intent.type == mwmp::CrimeType::Theft)
        {
            const double bounty = static_cast<double>(intent.value) * policy.theftBountyMultiplier;
            if (!std::isfinite(bounty) || bounty > std::numeric_limits<std::int32_t>::max())
                return std::nullopt;
            return std::max<std::int32_t>(1, static_cast<std::int32_t>(bounty));
        }
        switch (intent.type)
        {
            case mwmp::CrimeType::Pickpocket:
                return policy.pickpocketBounty;
            case mwmp::CrimeType::Trespass:
                return policy.trespassBounty;
            case mwmp::CrimeType::Assault:
                return policy.assaultBounty;
            case mwmp::CrimeType::Murder:
                return policy.murderBounty;
            case mwmp::CrimeType::WerewolfExposure:
                return policy.werewolfBounty;
            case mwmp::CrimeType::Theft:
                break;
        }
        return std::nullopt;
    }
}

namespace mwmp
{
    CrimeSemanticService::CrimeSemanticService(PlayerDatabase& database, CrimeService& crimeService,
        ObservationService& observationService, CrimePolicy policy)
        : mDatabase(database)
        , mCrimeService(crimeService)
        , mObservationService(observationService)
        , mPolicy(policy)
    {
        if (!std::isfinite(policy.alarmRadius) || policy.alarmRadius < 0.f
            || !std::isfinite(policy.alarmRadius * policy.alarmRadius) || !std::isfinite(policy.theftBountyMultiplier)
            || policy.theftBountyMultiplier < 0.f || policy.pickpocketBounty < 0 || policy.trespassBounty < 0
            || policy.assaultBounty < 0 || policy.murderBounty < 0 || policy.werewolfBounty < 0
            || policy.reportingAlarmThreshold != 100
            || !std::isfinite(policy.aggression.dispositionTrespass)
            || !std::isfinite(policy.aggression.dispositionPickpocket)
            || !std::isfinite(policy.aggression.dispositionAttack)
            || !std::isfinite(policy.aggression.dispositionAttacking)
            || !std::isfinite(policy.aggression.dispositionKilling)
            || !std::isfinite(policy.aggression.dispositionStealing)
            || !std::isfinite(policy.aggression.fightDispositionMultiplier)
            || !std::isfinite(policy.aggression.fightDistanceMultiplier))
            throw std::invalid_argument("Invalid authoritative crime policy");
    }

    CrimeSemanticService::Outcome CrimeSemanticService::evaluate(
        const CrimeIntent& intent, std::vector<CrimeWitnessCandidate> witnesses, const Context& context)
    {
        Outcome outcome;
        outcome.result.eventId = intent.eventId;
        outcome.result.type = intent.type;
        if (context.accountId <= 0 || context.characterId <= 0 || context.playerGuid == 0)
        {
            outcome.result.error = CrimeSemanticError::Unauthorized;
            return outcome;
        }

        const std::string requestHash = crypto::sha256hex(encodeIntent(intent));
        if (const auto existing
            = mDatabase.loadSemanticRequest(SemanticService, context.accountId, context.characterId, intent.eventId))
        {
            if (existing->requestHash != requestHash)
            {
                outcome.result.error = CrimeSemanticError::DuplicateConflict;
                outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
                return outcome;
            }
            try
            {
                outcome.result = decodeResult(existing->resultPayload);
                outcome.replayed = true;
            }
            catch (const std::exception&)
            {
                outcome.result.error = CrimeSemanticError::CorruptStoredResult;
                outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
            }
            return outcome;
        }

        outcome.result.state = context.deferCommit && context.startingState
            ? *context.startingState : mDatabase.loadPlayerCrimeState(context.characterId);
        auto reject = [&](CrimeSemanticError error) {
            outcome.result.accepted = false;
            outcome.result.error = error;
            if (intent.eventId.empty() || intent.eventId.size() > MaximumSemanticRequestIdLength
                || intent.eventId.find('\0') != std::string::npos || intent.source.empty()
                || intent.source.size() > MaximumSemanticSourceLength || intent.source.find('\0') != std::string::npos)
                return;
            SemanticRequestRecord record;
            record.service = std::string(SemanticService);
            record.accountId = context.accountId;
            record.characterId = context.characterId;
            record.requestId = intent.eventId;
            record.requestHash = requestHash;
            record.status = "rejected";
            record.errorCode = static_cast<std::uint16_t>(error);
            record.resultPayload = encodeResult(outcome.result);
            record.source = intent.source;
            mDatabase.insertRejectedSemanticRequest(record);
        };

        if (!validIntent(intent, context) || witnesses.size() > MaximumCrimeWitnesses)
        {
            reject(CrimeSemanticError::InvalidIntent);
            return outcome;
        }

        if (std::any_of(witnesses.begin(), witnesses.end(), [](const CrimeWitnessCandidate& witness) {
                return !witness.actor.identity.isValid()
                    || witness.alarm < 0 || witness.alarm > 100
                    || witness.fight < 0 || witness.fight > 100
                    || static_cast<std::uint8_t>(witness.relationship)
                    > static_cast<std::uint8_t>(CrimeWitnessRelationship::Unknown)
                    || static_cast<std::uint8_t>(witness.relationshipAuthority)
                    > static_cast<std::uint8_t>(ObservationAuthority::MixedDelegated);
            }))
        {
            reject(CrimeSemanticError::InvalidIntent);
            return outcome;
        }

        std::sort(witnesses.begin(), witnesses.end(), [](const auto& left, const auto& right) {
            return identityKey(left.actor.identity) < identityKey(right.actor.identity);
        });
        if (std::adjacent_find(witnesses.begin(), witnesses.end(),
                [](const auto& left, const auto& right) { return left.actor.identity == right.actor.identity; })
            != witnesses.end())
        {
            reject(CrimeSemanticError::InvalidIntent);
            return outcome;
        }

        const float radiusSquared = mPolicy.alarmRadius * mPolicy.alarmRadius;
        outcome.result.witnesses.reserve(witnesses.size());
        for (const CrimeWitnessCandidate& input : witnesses)
        {
            CrimeWitnessResult witness;
            witness.identity = input.actor.identity;
            witness.alarm = input.alarm;
            witness.fight = input.fight;
            witness.guard = input.guard;
            witness.relationshipAuthority = input.relationshipAuthority;
            witness.victim = intent.victim && *intent.victim == input.actor.identity;
            witness.candidate = finite(input.actor.position)
                && (witness.victim || distanceSquared(intent.offender.position, input.actor.position) <= radiusSquared);

            if (!witness.candidate)
                witness.eligibility = CrimeWitnessEligibility::OutsideAlarmRadius;
            else if (!input.actor.identity.isValid() || input.actor.identity.kind != ObservationActorKind::Npc)
                witness.eligibility = CrimeWitnessEligibility::CanonicalKindRejected;
            else if (!input.actor.eligibilityKnown || !input.actor.enabled || !input.actor.alive
                || !input.actor.conscious)
                witness.eligibility = CrimeWitnessEligibility::ActorIneligible;
            else if (input.relationship == CrimeWitnessRelationship::InCombatWithVictim)
                witness.eligibility = CrimeWitnessEligibility::InCombatWithVictim;
            else if (input.relationship == CrimeWitnessRelationship::PlayerFollower)
                witness.eligibility = CrimeWitnessEligibility::PlayerFollower;
            else if (input.relationship != CrimeWitnessRelationship::Eligible)
                witness.eligibility = CrimeWitnessEligibility::RelationshipUnknown;
            else
                witness.eligibility = CrimeWitnessEligibility::Eligible;

            if (witness.eligibility == CrimeWitnessEligibility::Eligible)
            {
                ObservationQuery query;
                query.eventId = intent.eventId;
                query.cellId = intent.cellId;
                query.observer = input.actor;
                query.target = intent.offender;
                query.victim = intent.victim;
                query.observerPolicy = ObservationObserverPolicy::VanillaCrimeWitness;
                query.eventAuthority = input.relationshipAuthority;
                query.observedAtMs = intent.observedAtMs;
                query.maximumSnapshotAgeMs = intent.maximumSnapshotAgeMs;
                query.collisionGenerations = intent.collisionGenerations;
                if (witness.victim && (intent.victimAware || intent.type == CrimeType::Assault))
                    query.path = ObservationPath::VictimAware;
                else if (intent.type == CrimeType::Murder && !witness.victim)
                    query.path = ObservationPath::MurderHearing;

                if (query.path == ObservationPath::LineOfSightAwareness)
                {
                    query.observer.position.z += CanonicalHumanoidObservationHeight;
                    query.target.position.z += CanonicalHumanoidObservationHeight;
                }

                witness.observation = mObservationService.observe(query);
                witness.perceived = witness.observation->observable;
            }
            outcome.result.crimeSeen = outcome.result.crimeSeen || witness.perceived;
            outcome.result.witnesses.push_back(std::move(witness));
        }

        outcome.result.reportingStageRun = outcome.result.crimeSeen;
        for (CrimeWitnessResult& witness : outcome.result.witnesses)
        {
            witness.reportCapable = witness.eligibility == CrimeWitnessEligibility::Eligible
                && witness.alarm >= mPolicy.reportingAlarmThreshold;
            witness.reported = outcome.result.reportingStageRun && witness.reportCapable;
            outcome.result.bountyApplied = outcome.result.bountyApplied || witness.reported;
            if (outcome.result.reportingStageRun
                && witness.eligibility == CrimeWitnessEligibility::Eligible
                && !(witness.guard && witness.reportCapable))
            {
                const auto inputIt = std::find_if(witnesses.begin(), witnesses.end(), [&](const auto& input) {
                    return input.actor.identity == witness.identity;
                });
                if (inputIt != witnesses.end())
                {
                    CrimeAggressionInput aggressionInput;
                    aggressionInput.type = intent.type;
                    aggressionInput.value = intent.value;
                    aggressionInput.victim = witness.victim;
                    aggressionInput.baseFight = witness.fight;
                    aggressionInput.alarm = witness.alarm;
                    aggressionInput.witnessPosition = inputIt->actor.position;
                    aggressionInput.offenderPosition = intent.offender.position;
                    witness.aggression = calculateCrimeAggression(aggressionInput, mPolicy.aggression);
                }
            }
        }

        const std::optional<std::int32_t> bounty = bountyFor(intent, mPolicy);
        if (!bounty)
        {
            reject(CrimeSemanticError::StateOverflow);
            return outcome;
        }
        outcome.result.bountyDelta = outcome.result.bountyApplied ? *bounty : 0;
        // Native werewolf exposure changes bounty directly and does not record
        // a normal crime-id threshold. Preserve that distinction while still
        // journaling perception/reporting idempotently.
        outcome.result.currentCrimeIdAdvanced
            = outcome.result.reportingStageRun && intent.type != CrimeType::WerewolfExposure;
        outcome.result.accepted = true;
        outcome.result.error = CrimeSemanticError::None;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const PlayerCrimeState current = context.deferCommit && context.startingState
                ? *context.startingState : mDatabase.loadPlayerCrimeState(context.characterId);
            if ((outcome.result.currentCrimeIdAdvanced
                    && current.currentCrimeId == std::numeric_limits<std::int32_t>::max())
                || outcome.result.bountyDelta > std::numeric_limits<std::int32_t>::max() - current.bounty
                || ((outcome.result.currentCrimeIdAdvanced || outcome.result.bountyDelta != 0)
                    && current.revision >= MaximumPersistedRevision))
            {
                outcome.result.state = current;
                reject(CrimeSemanticError::StateOverflow);
                return outcome;
            }

            PlayerCrimeState next = current;
            next.bounty += outcome.result.bountyDelta;
            if (outcome.result.currentCrimeIdAdvanced)
                ++next.currentCrimeId;
            if (outcome.result.currentCrimeIdAdvanced || outcome.result.bountyDelta != 0)
                ++next.revision;
            outcome.result.state = next;

            if (context.deferCommit)
            {
                CrimeMutationCommit deferred;
                deferred.service = std::string(SemanticService);
                deferred.accountId = context.accountId;
                deferred.characterId = context.characterId;
                deferred.requestId = intent.eventId;
                deferred.requestHash = requestHash;
                deferred.resultPayload = encodeResult(outcome.result);
                deferred.source = intent.source;
                deferred.expectedRevision = current.revision;
                deferred.resultingState = next;
                deferred.failurePoint = context.failurePoint;
                outcome.pendingCommit = std::move(deferred);
                return outcome;
            }

            CrimeService::AuthoritativeTransition transition;
            transition.requestId = intent.eventId;
            transition.requestHash = requestHash;
            transition.source = intent.source;
            transition.bountyDelta = outcome.result.bountyDelta;
            transition.advanceCurrentCrimeId = outcome.result.currentCrimeIdAdvanced;
            transition.expectedRevision = current.revision;
            transition.terminalResultPayload = encodeResult(outcome.result);
            CrimeService::Context crimeContext;
            crimeContext.accountId = context.accountId;
            crimeContext.characterId = context.characterId;
            crimeContext.failurePoint = context.failurePoint;

            const CrimeService::TransitionOutcome committed
                = mCrimeService.commitAuthoritativeTransition(transition, crimeContext);
            switch (committed.status)
            {
                case CrimeCommitStatus::Committed:
                    outcome.committed = true;
                    return outcome;
                case CrimeCommitStatus::DuplicateRequest:
                    try
                    {
                        outcome.result = decodeResult(committed.storedResultPayload);
                        outcome.replayed = true;
                    }
                    catch (const std::exception&)
                    {
                        outcome.result.accepted = false;
                        outcome.result.error = CrimeSemanticError::CorruptStoredResult;
                        outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
                    }
                    return outcome;
                case CrimeCommitStatus::DuplicateRequestConflict:
                    outcome.result.accepted = false;
                    outcome.result.error = CrimeSemanticError::DuplicateConflict;
                    outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
                    return outcome;
                case CrimeCommitStatus::StaleRevision:
                    break;
            }
        }

        outcome.result.accepted = false;
        outcome.result.error = CrimeSemanticError::StaleRevision;
        outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
        return outcome;
    }
}
