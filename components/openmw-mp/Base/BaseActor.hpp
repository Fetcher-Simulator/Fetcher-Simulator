#ifndef OPENMW_MP_BASEACTOR_HPP
#define OPENMW_MP_BASEACTOR_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "BaseStructs.hpp"

namespace mwmp
{
    struct BaseActor
    {
        // ------------------------------------------------------------------
        // Identity
        // ------------------------------------------------------------------
        std::string refId;          // ESM record ID (e.g. "scamp")
        uint32_t    refNum  = 0;    // ESP/ESM ref number
        uint32_t    mpNum   = 0;    // server-assigned multiplayer number
        std::string cellId;         // stringified cell name for easy lookup

        // ------------------------------------------------------------------
        // World state
        // ------------------------------------------------------------------
        Position    position;
        Velocity    velocity;
        bool        isMoving = false;
        bool        hasWeaponDrawn = false;
        bool        hasSpellReadied = false;
        bool        isAttackingOrCasting = false;

        // ------------------------------------------------------------------
        // Combat
        // ------------------------------------------------------------------
        DynamicStats dynamicStats;
        AnimFlags    animFlags;
        AnimPlay     animPlay;
        Attack       attack;
        CastSpell    cast;
        uint8_t      deathState = 0;
        uint32_t     deathEventId = 0;
        // Stable server-issued combat event that causally produced this death.
        // Zero means that no validated multiplayer combat cause is available.
        uint64_t     deathCauseCombatEventId = 0;
        bool         isDead = false;
        bool         isInstantDeath = false;
        std::string  deathAnimGroup;         // e.g. "death1"/"death2"/"death_knock_down" synced from authority

        // ------------------------------------------------------------------
        // AI
        // ------------------------------------------------------------------
        struct AIAction
        {
            enum class Type { None=0, Wander, Travel, Follow, Escort, Combat, Pursue };
            Type        type       = Type::None;
            std::string targetId;
            uint32_t    targetMpNum = 0;
            float       duration    = 0.f;
            bool        reset       = false;
        };
        AIAction ai;

        // ------------------------------------------------------------------
        // Equipment (actors can wear items too)
        // ------------------------------------------------------------------
        static constexpr int NUM_EQUIPMENT_SLOTS = 19;
        std::vector<EquipmentItem> equipment;

        bool isFollowerCellChange = false;
        uint32_t migrationGeneration = 0;
    };

    // A batch of actors belonging to a single cell
    struct ActorList
    {
        std::string         cellId;
        std::vector<BaseActor> actors;
        bool                isAuthority = false;  // true = sender is authority for this cell
        uint32_t            authorityGuid = 0;
        uint32_t            victimPlayerGuid = 0;   // non-zero = this request targets a player (NPC->player damage)
        // Protocol-10 combat transaction metadata. Attackers send eventId=0;
        // the server allocates and binds the remaining fields before routing
        // the proposal to the current victim authority.
        uint64_t            combatEventId = 0;
        uint64_t            combatVictimActorInstanceId = 0;
        uint32_t            combatVictimMigrationGeneration = 0;
        uint32_t            combatVictimAuthorityGeneration = 0;
        uint32_t            combatResultSequence = 0;
        uint8_t             combatResultFlags = 0;
        float               combatAppliedDamage = 0.f;
        uint32_t            authorityGeneration = 0;
        uint32_t            snapshotSequence = 0;
        uint64_t            serverTimestamp = 0;
    };

} // namespace mwmp

#endif // OPENMW_MP_BASEACTOR_HPP
