#include "CrimeAggression.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    bool finite(const mwmp::ObservationVector& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
}

mwmp::CrimeAggressionResult mwmp::calculateCrimeAggression(
    const CrimeAggressionInput& input, const CrimeAggressionPolicy& policy)
{
    CrimeAggressionResult result;
    result.baseFight = input.baseFight;
    result.finalFight = input.baseFight;
    if (input.baseFight < 0 || input.baseFight > 100 || input.alarm < 0 || input.alarm > 100
        || input.value < 0 || input.value > std::numeric_limits<std::int32_t>::max()
        || !finite(input.witnessPosition) || !finite(input.offenderPosition))
        return result;

    std::int64_t crimeFight = 0;
    switch (input.type)
    {
        case CrimeType::Trespass:
            crimeFight = policy.fightTrespass;
            result.dispositionTerm = policy.dispositionTrespass;
            break;
        case CrimeType::Pickpocket:
            crimeFight = input.victim
                ? static_cast<std::int64_t>(policy.fightPickpocket) * 4
                : policy.fightPickpocket;
            result.dispositionTerm = policy.dispositionPickpocket;
            break;
        case CrimeType::Assault:
            crimeFight = input.victim ? policy.fightAttack : policy.fightAttacking;
            result.dispositionTerm
                = input.victim ? policy.dispositionAttacking : policy.dispositionAttack;
            break;
        case CrimeType::Murder:
            crimeFight = policy.fightKilling;
            result.dispositionTerm = policy.dispositionKilling;
            break;
        case CrimeType::Theft:
            crimeFight = static_cast<std::int64_t>(policy.fightStealing) * input.value;
            result.dispositionTerm = policy.dispositionStealing * static_cast<float>(input.value);
            break;
        case CrimeType::WerewolfExposure:
            return result;
    }
    if (crimeFight < std::numeric_limits<std::int32_t>::min()
        || crimeFight > std::numeric_limits<std::int32_t>::max()
        || !std::isfinite(result.dispositionTerm))
        return result;

    result.evaluated = true;
    result.crimeFight = static_cast<std::int32_t>(crimeFight);
    result.alarmTerm = 0.01f * static_cast<float>(input.alarm);
    if (input.type == CrimeType::Pickpocket)
    {
        if (input.victim && result.alarmTerm == 0.f)
            result.alarmTerm = 1.f;
        else if (!input.victim)
            result.alarmTerm = 0.f;
    }

    result.dispositionBias
        = (50.f - result.dispositionTerm) * policy.fightDispositionMultiplier;
    // reportCrime only admits NPC witnesses. NPCs cannot move on the Z axis,
    // so native getAggroDistance() takes the full three-dimensional distance
    // (the XY-only branch is for swimming/flying creatures).
    const float dx = input.witnessPosition.x - input.offenderPosition.x;
    const float dy = input.witnessPosition.y - input.offenderPosition.y;
    const float dz = input.witnessPosition.z - input.offenderPosition.z;
    result.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    result.distanceBias = static_cast<float>(policy.fightDistanceBase)
        - policy.fightDistanceMultiplier * result.distance;

    float fightTerm = static_cast<float>(result.crimeFight);
    fightTerm += result.dispositionBias;
    fightTerm += result.distanceBias;
    fightTerm *= result.alarmTerm;
    result.unclampedFightTerm = fightTerm;

    if (static_cast<float>(input.baseFight) + fightTerm > 100.f)
        fightTerm = static_cast<float>(100 - input.baseFight);
    fightTerm = std::max(0.f, fightTerm);
    result.fightTerm = fightTerm;
    result.combat = static_cast<float>(input.baseFight) + fightTerm >= 100.f;
    if (result.combat)
        result.finalFight = input.baseFight + static_cast<std::int32_t>(fightTerm);
    return result;
}
