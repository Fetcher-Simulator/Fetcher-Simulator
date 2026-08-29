#ifndef OPENMW_MP_MECHANICSSNAPSHOT_HPP
#define OPENMW_MP_MECHANICSSNAPSHOT_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>

namespace mwmp
{
    inline constexpr std::uint16_t MechanicsSnapshotWireVersion = 4;
    inline constexpr std::size_t MaximumMechanicsSnapshotsPerPacket = 128;
    inline constexpr std::size_t MaximumMechanicsCellIdSize = 255;
    inline constexpr float MaximumMechanicsPositionMagnitude = 100000000.f;
    inline constexpr float MaximumMechanicsValueMagnitude = 1000000.f;

    enum class MechanicsSubjectKind : std::uint8_t
    {
        Player = 1,
        Npc = 2,
        Creature = 3,
    };

    enum MechanicsStateFlags : std::uint8_t
    {
        MechanicsEnabled = 1u << 0,
        MechanicsAlive = 1u << 1,
        MechanicsConscious = 1u << 2,
        MechanicsSneaking = 1u << 3,
        MechanicsOnGround = 1u << 4,
        MechanicsWerewolf = 1u << 5,
    };

    enum MechanicsWitnessStateFlags : std::uint8_t
    {
        MechanicsWitnessRelationshipKnown = 1u << 0,
        MechanicsWitnessPlayerFollower = 1u << 1,
        MechanicsWitnessHasCombatTarget = 1u << 2,
        MechanicsWitnessEffectiveAlarmKnown = 1u << 3,
        MechanicsWitnessEffectiveFightKnown = 1u << 4,
    };

    struct MechanicsSnapshot
    {
        MechanicsSubjectKind kind = MechanicsSubjectKind::Player;
        std::uint32_t playerGuid = 0;
        ActorInstanceId actorInstanceId = 0;
        std::string cellId;
        Position position;
        std::uint8_t stateFlags = 0;
        float sneakSkill = 0.f;
        float agility = 0.f;
        float luck = 0.f;
        float fatigueCurrent = 0.f;
        float fatigueMaximumModified = 0.f;
        float chameleon = 0.f;
        float invisibility = 0.f;
        float blind = 0.f;
        std::uint8_t witnessStateFlags = 0;
        std::int32_t effectiveAlarm = 0;
        std::int32_t effectiveFight = 0;
        MechanicsSubjectKind combatTargetKind = MechanicsSubjectKind::Player;
        std::uint32_t combatTargetPlayerGuid = 0;
        ActorInstanceId combatTargetActorInstanceId = 0;
        std::uint32_t migrationGeneration = 0;
        std::uint32_t authorityGeneration = 0;
        std::uint32_t snapshotSequence = 0;

        bool operator==(const MechanicsSnapshot& other) const
        {
            return kind == other.kind && playerGuid == other.playerGuid
                && actorInstanceId == other.actorInstanceId && cellId == other.cellId
                && position.pos[0] == other.position.pos[0] && position.pos[1] == other.position.pos[1]
                && position.pos[2] == other.position.pos[2] && position.rot[0] == other.position.rot[0]
                && position.rot[1] == other.position.rot[1] && position.rot[2] == other.position.rot[2]
                && stateFlags == other.stateFlags && sneakSkill == other.sneakSkill
                && agility == other.agility && luck == other.luck
                && fatigueCurrent == other.fatigueCurrent
                && fatigueMaximumModified == other.fatigueMaximumModified
                && chameleon == other.chameleon && invisibility == other.invisibility
                && blind == other.blind && witnessStateFlags == other.witnessStateFlags
                && effectiveAlarm == other.effectiveAlarm && effectiveFight == other.effectiveFight
                && combatTargetKind == other.combatTargetKind
                && combatTargetPlayerGuid == other.combatTargetPlayerGuid
                && combatTargetActorInstanceId == other.combatTargetActorInstanceId
                && migrationGeneration == other.migrationGeneration
                && authorityGeneration == other.authorityGeneration
                && snapshotSequence == other.snapshotSequence;
        }
    };

    struct MechanicsSnapshotBatch
    {
        std::uint16_t wireVersion = MechanicsSnapshotWireVersion;
        std::vector<MechanicsSnapshot> snapshots;

        bool operator==(const MechanicsSnapshotBatch&) const = default;
    };

    inline bool isKnownMechanicsSubjectKind(MechanicsSubjectKind kind)
    {
        return kind == MechanicsSubjectKind::Player || kind == MechanicsSubjectKind::Npc
            || kind == MechanicsSubjectKind::Creature;
    }

    inline bool isCanonicalMechanicsCellId(std::string_view cellId)
    {
        if (cellId.empty() || cellId.size() > MaximumMechanicsCellIdSize)
            return false;
        for (const unsigned char value : cellId)
        {
            if (value == 0 || value < 0x20 || value == 0x7f)
                return false;
        }

        if (!cellId.starts_with("EXT:"))
            return true;

        int gridX = 0;
        int gridY = 0;
        int consumed = 0;
        if (std::sscanf(std::string(cellId).c_str(), "EXT:%d,%d%n", &gridX, &gridY, &consumed) != 2
            || consumed != static_cast<int>(cellId.size()))
            return false;
        return cellId == "EXT:" + std::to_string(gridX) + "," + std::to_string(gridY);
    }

    inline bool isFiniteMechanicsValue(float value, float maximumMagnitude = MaximumMechanicsValueMagnitude)
    {
        return std::isfinite(value) && std::abs(value) <= maximumMagnitude;
    }

    inline bool validateMechanicsSnapshot(const MechanicsSnapshot& snapshot)
    {
        constexpr std::uint8_t KnownStateFlags = MechanicsEnabled | MechanicsAlive
            | MechanicsConscious | MechanicsSneaking | MechanicsOnGround | MechanicsWerewolf;
        constexpr std::uint8_t KnownWitnessStateFlags = MechanicsWitnessRelationshipKnown
            | MechanicsWitnessPlayerFollower | MechanicsWitnessHasCombatTarget
            | MechanicsWitnessEffectiveAlarmKnown | MechanicsWitnessEffectiveFightKnown;
        if (!isKnownMechanicsSubjectKind(snapshot.kind) || !isCanonicalMechanicsCellId(snapshot.cellId)
            || snapshot.migrationGeneration == 0 || snapshot.authorityGeneration == 0
            || snapshot.snapshotSequence == 0 || (snapshot.stateFlags & ~KnownStateFlags) != 0
            || (snapshot.witnessStateFlags & ~KnownWitnessStateFlags) != 0)
            return false;

        if (snapshot.kind == MechanicsSubjectKind::Player)
        {
            if (snapshot.playerGuid == 0 || snapshot.actorInstanceId != 0
                || snapshot.witnessStateFlags != 0 || snapshot.effectiveAlarm != 0
                || snapshot.effectiveFight != 0
                || snapshot.combatTargetPlayerGuid != 0 || snapshot.combatTargetActorInstanceId != 0)
                return false;
        }
        else if (snapshot.playerGuid != 0 || !isValidActorInstanceId(snapshot.actorInstanceId))
            return false;

        const bool relationshipKnown
            = (snapshot.witnessStateFlags & MechanicsWitnessRelationshipKnown) != 0;
        const bool playerFollower
            = (snapshot.witnessStateFlags & MechanicsWitnessPlayerFollower) != 0;
        const bool hasCombatTarget
            = (snapshot.witnessStateFlags & MechanicsWitnessHasCombatTarget) != 0;
        const bool effectiveAlarmKnown
            = (snapshot.witnessStateFlags & MechanicsWitnessEffectiveAlarmKnown) != 0;
        const bool effectiveFightKnown
            = (snapshot.witnessStateFlags & MechanicsWitnessEffectiveFightKnown) != 0;
        if ((!relationshipKnown && (playerFollower || hasCombatTarget))
            || (!effectiveAlarmKnown && snapshot.effectiveAlarm != 0)
            || (effectiveAlarmKnown && (snapshot.effectiveAlarm < 0 || snapshot.effectiveAlarm > 100))
            || (!effectiveFightKnown && snapshot.effectiveFight != 0)
            || (effectiveFightKnown && (snapshot.effectiveFight < 0 || snapshot.effectiveFight > 100)))
            return false;

        if (hasCombatTarget)
        {
            if (!isKnownMechanicsSubjectKind(snapshot.combatTargetKind))
                return false;
            if (snapshot.combatTargetKind == MechanicsSubjectKind::Player)
            {
                if (snapshot.combatTargetPlayerGuid == 0 || snapshot.combatTargetActorInstanceId != 0)
                    return false;
            }
            else if (snapshot.combatTargetPlayerGuid != 0
                || !isValidActorInstanceId(snapshot.combatTargetActorInstanceId))
                return false;
        }
        else if (snapshot.combatTargetPlayerGuid != 0 || snapshot.combatTargetActorInstanceId != 0)
            return false;

        for (float axis : snapshot.position.pos)
        {
            if (!isFiniteMechanicsValue(axis, MaximumMechanicsPositionMagnitude))
                return false;
        }
        for (float axis : snapshot.position.rot)
        {
            if (!isFiniteMechanicsValue(axis))
                return false;
        }

        return isFiniteMechanicsValue(snapshot.sneakSkill)
            && isFiniteMechanicsValue(snapshot.agility)
            && isFiniteMechanicsValue(snapshot.luck)
            && isFiniteMechanicsValue(snapshot.fatigueCurrent)
            && isFiniteMechanicsValue(snapshot.fatigueMaximumModified)
            && snapshot.fatigueMaximumModified >= 0.f
            && isFiniteMechanicsValue(snapshot.chameleon)
            && isFiniteMechanicsValue(snapshot.invisibility)
            && isFiniteMechanicsValue(snapshot.blind);
    }
}

#endif
