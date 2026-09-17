#pragma once
#include <algorithm>
#include <cmath>
#include <array>

namespace mwmp
{
    enum PersuasionType { PT_Admire, PT_Intimidate, PT_Taunt, PT_Bribe10, PT_Bribe100, PT_Bribe1000 };
    struct PersuasionStats
    {
        float personality = 0, luck = 0, reputation = 0, fatigue = 1, level = 1, speechcraft = 0, mercantile = 0;
    };
    struct PersuasionInput
    {
        PersuasionStats player, npc;
        int disposition = 50, fight = 0, flee = 0;
    };
    struct PersuasionOutcome
    {
        bool success = false;
        int temporary = 0, permanent = 0, fight = 0, flee = 0;
    };
    template<class Gmst>
    std::array<float,3> persuasionRatings(const PersuasionStats& s, bool player, const Gmst& gmst)
    {
        const float pers = s.personality / gmst("fPersonalityMod"), luck = s.luck / gmst("fLuckMod");
        const float rep = s.reputation * gmst("fReputationMod"), level = s.level * gmst("fLevelMod");
        const float first = (rep + luck + pers + s.speechcraft) * s.fatigue;
        return {first, player ? first + level : (level + rep + luck + pers + s.speechcraft) * s.fatigue,
            (s.mercantile + luck + pers + (player ? 0 : rep)) * s.fatigue};
    }
    // Pure native mechanics. The caller owns randomness, payment and persistence.
    template<class Gmst>
    PersuasionOutcome resolvePersuasion(const PersuasionInput& input, PersuasionType type, int roll, const Gmst& gmst)
    {
        const auto [npcRating1,npcRating2,npcRating3] = persuasionRatings(input.npc,false,gmst);
        const auto [playerRating1,playerRating2,playerRating3] = persuasionRatings(input.player,true,gmst);
        const int currentDisposition = input.disposition;
        PersuasionOutcome result;
        result.fight = input.fight;
        result.flee = input.flee;
        auto& success = result.success;
        auto& tempChange = result.temporary;
        auto& permChange = result.permanent;
        float d = 1 - 0.02f * abs(currentDisposition - 50);
        float target1 = d * (playerRating1 - npcRating1 + 50);
        float target2 = d * (playerRating2 - npcRating2 + 50);

        float bribeMod;
        if (type == PT_Bribe10)
            bribeMod = gmst("fBribe10Mod");
        else if (type == PT_Bribe100)
            bribeMod = gmst("fBribe100Mod");
        else
            bribeMod = gmst("fBribe1000Mod");

        float target3 = d * (playerRating3 - npcRating3 + 50) + bribeMod;

        const float iPerMinChance = gmst("iPerMinChance");
        const float iPerMinChange = gmst("iPerMinChange");
        const float fPerDieRollMult = gmst("fPerDieRollMult");
        const float fPerTempMult = gmst("fPerTempMult");

        float x = 0;
        float y = 0;



        if (type == PT_Admire)
        {
            target1 = std::max(iPerMinChance, target1);
            success = (roll <= target1);
            float c = floor(fPerDieRollMult * (target1 - roll));
            x = success ? std::max(iPerMinChange, c) : c;
        }
        else if (type == PT_Intimidate)
        {
            target2 = std::max(iPerMinChance, target2);

            success = (roll <= target2);

            float r;
            if (roll != target2)
                r = floor(target2 - roll);
            else
                r = 1;

            if (roll <= target2)
            {
                float s = floor(r * fPerDieRollMult * fPerTempMult);

                const int flee = input.flee;
                const int fight = input.fight;
                result.flee = std::clamp(flee + int(std::max(iPerMinChange, s)), 0, 100);
                result.fight = std::clamp(fight + int(std::min(-iPerMinChange, -s)), 0, 100);
            }

            float c = -std::abs(floor(r * fPerDieRollMult));
            if (success)
            {
                if (std::abs(c) < iPerMinChange)
                {
                    // Deviating from Morrowind here: it doesn't increase disposition on marginal wins,
                    // which seems to be a bug (MCP fixes it too).
                    // Original logic: x = 0, y = -iPerMinChange
                    x = iPerMinChange;
                    y = x; // This goes unused.
                }
                else
                {
                    x = -floor(c * fPerTempMult);
                    y = c;
                }
            }
            else
            {
                x = floor(c * fPerTempMult);
                y = c;
            }
        }
        else if (type == PT_Taunt)
        {
            target1 = std::max(iPerMinChance, target1);
            success = (roll <= target1);

            float c = std::abs(floor(target1 - roll));

            if (success)
            {
                float s = c * fPerDieRollMult * fPerTempMult;
                const int flee = input.flee;
                const int fight = input.fight;
                result.flee = std::clamp(flee + std::min(-int(iPerMinChange), int(-s)), 0, 100);
                result.fight = std::clamp(fight + std::max(int(iPerMinChange), int(s)), 0, 100);
            }
            x = floor(-c * fPerDieRollMult);

            if (success && std::abs(x) < iPerMinChange)
                x = -iPerMinChange;
        }
        else // Bribe
        {
            target3 = std::max(iPerMinChance, target3);
            success = (roll <= target3);
            float c = floor((target3 - roll) * fPerDieRollMult);

            x = success ? std::max(iPerMinChange, c) : c;
        }

        if (type == PT_Intimidate)
        {
            tempChange = int(x);
            if (currentDisposition + tempChange > 100)
                tempChange = 100 - currentDisposition;
            else if (currentDisposition + tempChange < 0)
                tempChange = -currentDisposition;
            permChange = success ? -int(tempChange / fPerTempMult) : int(y);
        }
        else
        {
            tempChange = int(x * fPerTempMult);
            if (currentDisposition + tempChange > 100)
                tempChange = 100 - currentDisposition;
            else if (currentDisposition + tempChange < 0)
                tempChange = -currentDisposition;
            permChange = int(tempChange / fPerTempMult);
        }
        return result;
    }
}
