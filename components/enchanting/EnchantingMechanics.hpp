#ifndef OPENMW_COMPONENTS_ENCHANTING_ENCHANTINGMECHANICS_HPP
#define OPENMW_COMPONENTS_ENCHANTING_ENCHANTINGMECHANICS_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include <components/alchemy/AlchemyMechanics.hpp> // Crafting::MagicEffectData
#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadench.hpp>
#include <components/misc/rng.hpp>

namespace Crafting
{
    /// Barter-offer inputs for paid NPC enchanting. Mirrors the inputs of the
    /// native MechanicsManager::getBarterOffer; callers resolve the values
    /// from authoritative state (the single-player client uses full actor
    /// state, the multiplayer server uses content records and synced stats).
    struct EnchantingBarterInput
    {
        float playerMercantile = 0.f;
        float playerLuck = 0.f;
        float playerPersonality = 0.f;
        float playerFatigueTerm = 1.f;
        float enchanterMercantile = 0.f;
        float enchanterLuck = 0.f;
        float enchanterPersonality = 0.f;
        float enchanterFatigueTerm = 1.f;
        int disposition = 50; // derived disposition, clamped like native
        bool creatureMerchant = false; // native special case: no barter adjustment
    };

    /// Fully resolved input for the shared enchanting calculation. Callers
    /// (single-player client and authoritative server) resolve content
    /// records, character statistics, and GMSTs into this structure; the
    /// calculation itself is content- and runtime-independent.
    struct EnchantingMechanicsInput
    {
        // Target item identity. itemType is the ESM record id (Weapon/Armor/
        // Clothing/Book); weaponType/weaponClass are meaningful for weapons.
        int itemType = 0;
        int weaponType = -1;
        int enchantCapacity = 0; // enchantment points of the base item
        bool bookIsScroll = false; // scroll-type books are the only enchantable books

        // Soul gem charge (the soul value of the contained creature).
        int gemCharge = 0;

        // The player's total inventory count of the target refId, used by the
        // projectile/ammo count calculation and the success chance.
        int availableCount = 0;

        // Player choices.
        int castStyle = ESM::Enchantment::CastOnce;
        bool selfEnchanting = true; // false = paid NPC service
        std::vector<ESM::ENAMstruct> effects; // UI order, user-selected values

        // Authoritative enchanter statistics (the player for self-enchanting,
        // the paid enchanter otherwise).
        float enchantSkill = 0.f;
        float intelligence = 0.f;
        float luck = 0.f;
        float fatigueTerm = 1.f;

        // Client setting "projectiles enchant multiplier"; the server reads
        // its own authoritative equivalent. 0 = one projectile at a time.
        float projectilesEnchantMultiplier = 0.f;

        // Paid-service pricing inputs; absent for self-enchanting.
        std::optional<EnchantingBarterInput> barter;

        // Lazy content lookups, invoked exactly where the native mechanics
        // would consult the ESMStore. Returning nullopt means the record is
        // missing, which the mechanics reports as an invalid-data error.
        std::function<std::optional<MagicEffectData>(const ESM::RefId&)> magicEffect;
        std::function<std::optional<float>(std::string_view gmstId)> gmst;
    };

    /// Pure, reusable implementation of the native OpenMW enchanting
    /// mechanics (MWMechanics::Enchanting). Both the single-player enchanting
    /// window and the multiplayer authoritative server derive their
    /// calculations from this class so the formulas can never drift apart.
    class EnchantingMechanics
    {
    public:
        /// The weapon-class column of the hardcoded weapon-type table used by
        /// the enchanting formulas (native MWMechanics::getWeaponType).
        /// Returns -1 for non-weapon types.
        static int weaponClassOf(int weaponType);

        /// Whether the item can be enchanted at all: Weapon, Armor, Clothing,
        /// or a scroll-type Book (matches the native UI enchantable filter).
        static bool isEnchantable(const EnchantingMechanicsInput& input);

        /// Per-effect costs, exactly like native Enchanting::getEffectCosts.
        /// Throws std::runtime_error on invalid content (missing magic effect
        /// record or GMST).
        static std::vector<float> effectCosts(const EnchantingMechanicsInput& input);

        /// Sum of the effect costs. `precise` selects raw values (true) or
        /// floored per-effect costs (false) like native getEnchantPoints.
        static float enchantPoints(const EnchantingMechanicsInput& input, bool precise = true);

        /// The cost stored in the Enchantment record (0 for constant effect).
        static int baseCastCost(const EnchantingMechanicsInput& input);

        /// UI preview cost: base cost adjusted by the enchanter's Enchant
        /// skill (native getEffectiveEnchantmentCastCost).
        static int effectiveCastCost(const EnchantingMechanicsInput& input);

        /// Capacity check value: item enchantment points * fEnchantmentMult.
        static int maxEnchantValue(const EnchantingMechanicsInput& input);

        /// Success chance used for the self-enchant roll and the UI preview.
        static int enchantChance(const EnchantingMechanicsInput& input);

        /// Number of items enchanted in one operation. For thrown weapons and
        /// ammo this scales with the soul charge; for every other item it is
        /// exactly one.
        static int enchantItemsCount(const EnchantingMechanicsInput& input, int availableCount);

        /// Price multiplier applied to thrown-weapon/ammo enchants.
        static float typeMultiplier(const EnchantingMechanicsInput& input);

        /// The charge stored on the new Enchantment record (0 for constant
        /// effect, otherwise the gem charge divided by the enchanted count).
        static int enchantmentCharge(const EnchantingMechanicsInput& input, int count);

        /// Paid-service price for enchanting `count` items. Returns 0 when no
        /// barter inputs are present (self-enchanting has no price).
        static int enchantPrice(const EnchantingMechanicsInput& input, int count);

        /// The native cast-style cycle (Enchanting::nextCastStyle).
        static int nextCastStyle(const EnchantingMechanicsInput& input, int current);

        /// Every cast style reachable by the native cycle for this item and
        /// soul charge, in deterministic order.
        static std::vector<int> validCastStyles(const EnchantingMechanicsInput& input);

        /// The authoritative success roll. Self-enchanting rolls the native
        /// chance against the provided generator; paid services always
        /// succeed exactly like native (mSelfEnchanting == false skips the
        /// roll).
        static bool rollSuccess(const EnchantingMechanicsInput& input, Misc::Rng::Generator& prng);
    };
}

#endif
