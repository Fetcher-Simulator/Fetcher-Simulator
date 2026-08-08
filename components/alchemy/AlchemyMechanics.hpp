#ifndef OPENMW_COMPONENTS_ALCHEMY_ALCHEMYMECHANICS_HPP
#define OPENMW_COMPONENTS_ALCHEMY_ALCHEMYMECHANICS_HPP

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/esm3/effectlist.hpp>
#include <components/misc/rng.hpp>

namespace Crafting
{
    /// Effect identity as used by the alchemy ingredient matcher. Mirrors
    /// MWMechanics::EffectKey without depending on the client runtime.
    struct EffectKey
    {
        ESM::RefId id;
        ESM::RefId arg; // skill or attribute targeted by the effect

        bool operator==(const EffectKey&) const = default;
    };

    /// The subset of a MagicEffect record consumed by alchemy calculations.
    struct MagicEffectData
    {
        float baseCost = 0.f;
        std::uint32_t flags = 0;
    };

    /// Fully resolved input for the shared alchemy calculation. Callers
    /// (single-player client and authoritative server) resolve content
    /// records, character statistics, and GMSTs into this structure; the
    /// calculation itself is content- and runtime-independent.
    struct AlchemyMechanicsInput
    {
        /// Resolved ingredient data in slot order (empty slots omitted).
        struct Ingredient
        {
            float weight = 0.f;
            int count = 1; // stack size of the selected inventory instance
            std::array<ESM::RefId, 4> effectIds;  // mData.mEffectID
            std::array<ESM::RefId, 4> skills;     // mData.mSkills
            std::array<ESM::RefId, 4> attributes; // mData.mAttributes
        };

        std::vector<Ingredient> ingredients;

        /// Apparatus quality by ESM::Apparatus type (MortarPestle=0,
        /// Alembic=1, Calcinator=2, Retort=3). Empty means absent.
        std::array<std::optional<float>, 4> apparatusQuality;

        // Authoritative character statistics (modified values).
        float alchemySkill = 0.f;
        float intelligence = 0.f;
        float luck = 0.f;

        // Lazy content lookups, invoked exactly where the native mechanics
        // would consult the ESMStore. Returning nullopt means the record is
        // missing, which the mechanics reports as an invalid-data error.
        std::function<std::optional<MagicEffectData>(const ESM::RefId&)> magicEffect;
        std::function<std::optional<float>(std::string_view gmstId)> gmst;
    };

    /// A complete potion definition as produced by a successful attempt.
    struct PotionDefinition
    {
        std::string name;
        float weight = 0.f;
        int value = 0;
        std::vector<ESM::ENAMstruct> effects;
        std::string model;
        std::string icon;
    };

    /// Pure, reusable implementation of the native OpenMW alchemy mechanics.
    /// Both the single-player alchemy window and the multiplayer authoritative
    /// server derive their calculations from this class so the formulas can
    /// never drift apart.
    class AlchemyMechanics
    {
    public:
        enum class Result
        {
            Success,
            NoMortarAndPestle,
            LessThanTwoIngredients,
            NoName,
            NoEffects,
            RandomFailure,
        };

        /// Effects shared by at least two ingredients, in slot order.
        static std::vector<EffectKey> listEffects(const AlchemyMechanicsInput& input);

        static int countIngredients(const AlchemyMechanicsInput& input);

        static float getAlchemyFactor(const AlchemyMechanicsInput& input);

        struct EffectsResult
        {
            std::vector<ESM::ENAMstruct> effects;
            int value = 0;
        };

        /// Builds the quantified effect list and potion value exactly like the
        /// native updateEffects(). Throws std::runtime_error on invalid
        /// content (missing GMST, non-positive magic effect base cost, ...).
        static EffectsResult updateEffects(const AlchemyMechanicsInput& input);

        static Result getReadyStatus(const AlchemyMechanicsInput& input, const std::string& potionName);

        /// Maximum number of potions that can be brewed from the selected
        /// ingredient stacks (the smallest stack size), or 0 when the setup
        /// is not ready.
        static int countPotionsToBrew(const AlchemyMechanicsInput& input, const std::string& potionName);

        struct Attempt
        {
            bool success = false;
            PotionDefinition potion; // valid when success
        };

        /// One brewing attempt: rolls success against the alchemy factor and
        /// produces the potion definition on success. Does not mutate any
        /// inventory or skill state; callers own consumption semantics.
        static Attempt createSingle(
            const AlchemyMechanicsInput& input, Misc::Rng::Generator& prng, const std::string& potionName);

        /// The six randomized potion mesh/icon names used by native alchemy.
        static const std::array<std::string_view, 6>& meshes();
    };
}

#endif
