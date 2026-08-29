#ifndef OPENMW_SERVER_CRIMEAGGRESSION_HPP
#define OPENMW_SERVER_CRIMEAGGRESSION_HPP

#include <cstdint>

#include "ObservationService.hpp"

namespace mwmp
{
    enum class CrimeType : std::uint8_t
    {
        Theft = 1,
        Pickpocket = 2,
        Trespass = 3,
        Assault = 4,
        Murder = 5,
        WerewolfExposure = 6,
    };

    struct CrimeAggressionPolicy
    {
        std::int32_t fightTrespass = 0;
        std::int32_t fightPickpocket = 0;
        std::int32_t fightAttack = 0;
        std::int32_t fightAttacking = 0;
        std::int32_t fightKilling = 0;
        std::int32_t fightStealing = 0;
        float dispositionTrespass = 0.f;
        float dispositionPickpocket = 0.f;
        float dispositionAttack = 0.f;
        float dispositionAttacking = 0.f;
        float dispositionKilling = 0.f;
        float dispositionStealing = 0.f;
        float fightDispositionMultiplier = 0.f;
        std::int32_t fightDistanceBase = 0;
        float fightDistanceMultiplier = 0.f;
    };

    struct CrimeAggressionInput
    {
        CrimeType type = CrimeType::Theft;
        std::int64_t value = 0;
        bool victim = false;
        std::int32_t baseFight = 0;
        std::int32_t alarm = 0;
        ObservationVector witnessPosition;
        ObservationVector offenderPosition;
    };

    /// Exact pure form of the non-guard Fight branch in
    /// MechanicsManager::reportCrime(). The caller remains responsible for
    /// reportCrime ordering/eligibility and the Alarm-100 guard branch.
    struct CrimeAggressionResult
    {
        bool evaluated = false;
        bool combat = false;
        std::int32_t baseFight = 0;
        std::int32_t finalFight = 0;
        std::int32_t crimeFight = 0;
        float dispositionTerm = 0.f;
        float dispositionBias = 0.f;
        float distance = 0.f;
        float distanceBias = 0.f;
        float alarmTerm = 0.f;
        float unclampedFightTerm = 0.f;
        float fightTerm = 0.f;

        bool operator==(const CrimeAggressionResult&) const = default;
    };

    CrimeAggressionResult calculateCrimeAggression(
        const CrimeAggressionInput& input, const CrimeAggressionPolicy& policy);
}

#endif
