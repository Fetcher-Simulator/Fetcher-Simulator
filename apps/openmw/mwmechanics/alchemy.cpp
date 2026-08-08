#include "alchemy.hpp"

#include <cassert>
#include <cmath>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

#include <components/alchemy/AlchemyMechanics.hpp>
#include <components/misc/rng.hpp>

#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "creaturestats.hpp"
#include "magiceffects.hpp"

#ifdef BUILD_MULTIPLAYER
#include <components/openmw-mp/Records/AlchemyProtocol.hpp>

#include "../mwmp/Main.hpp"
#include "../mwmp/sync/InventoryIdentity.hpp"
#endif

namespace
{
    constexpr size_t sNumEffects = 4;

    std::optional<Crafting::EffectKey> toKey(const ESM::Ingredient& ingredient, size_t i)
    {
        if (ingredient.mData.mEffectID[i].empty())
            return {};
        ESM::RefId arg = ingredient.mData.mSkills[i];
        if (arg.empty())
            arg = ingredient.mData.mAttributes[i];
        return Crafting::EffectKey{ ingredient.mData.mEffectID[i], std::move(arg) };
    }

    bool containsEffect(const ESM::Ingredient& ingredient, const Crafting::EffectKey& effect)
    {
        for (size_t j = 0; j < sNumEffects; ++j)
        {
            if (toKey(ingredient, j) == effect)
                return true;
        }
        return false;
    }
}

MWMechanics::Alchemy::Alchemy()
    : mValue(0)
{
}

std::vector<MWMechanics::EffectKey> MWMechanics::Alchemy::listEffects() const
{
    const std::vector<Crafting::EffectKey> effects = Crafting::AlchemyMechanics::listEffects(makeMechanicsInput());
    std::vector<MWMechanics::EffectKey> result;
    result.reserve(effects.size());
    for (const Crafting::EffectKey& effect : effects)
        result.emplace_back(effect.id, effect.arg);
    return result;
}

Crafting::AlchemyMechanicsInput MWMechanics::Alchemy::makeMechanicsInput() const
{
    Crafting::AlchemyMechanicsInput input;

    for (const MWWorld::Ptr& ingredientPtr : mIngredients)
    {
        if (ingredientPtr.isEmpty())
            continue;
        const ESM::Ingredient* ingredient = ingredientPtr.get<ESM::Ingredient>()->mBase;
        Crafting::AlchemyMechanicsInput::Ingredient resolved;
        resolved.weight = ingredient->mData.mWeight;
        resolved.count = ingredientPtr.getCellRef().getCount();
        for (std::size_t i = 0; i < 4; ++i)
        {
            resolved.effectIds[i] = ingredient->mData.mEffectID[i];
            resolved.skills[i] = ingredient->mData.mSkills[i];
            resolved.attributes[i] = ingredient->mData.mAttributes[i];
        }
        input.ingredients.push_back(std::move(resolved));
    }

    for (int type = 0; type < static_cast<int>(mTools.size()); ++type)
    {
        if (!mTools[type].isEmpty())
            input.apparatusQuality[type] = mTools[type].get<ESM::Apparatus>()->mBase->mData.mQuality;
    }

    const CreatureStats& creatureStats = mAlchemist.getClass().getCreatureStats(mAlchemist);
    input.alchemySkill = mAlchemist.getClass().getSkill(mAlchemist, ESM::Skill::Alchemy);
    input.intelligence = creatureStats.getAttribute(ESM::Attribute::Intelligence).getModified();
    input.luck = creatureStats.getAttribute(ESM::Attribute::Luck).getModified();

    const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
    input.magicEffect = [&store](const ESM::RefId& id) -> std::optional<Crafting::MagicEffectData> {
        const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(id);
        if (effect == nullptr)
            return std::nullopt;
        return Crafting::MagicEffectData{ effect->mData.mBaseCost, static_cast<std::uint32_t>(effect->mData.mFlags) };
    };
    input.gmst = [&store](std::string_view id) -> std::optional<float> {
        const ESM::GameSetting* setting = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(id));
        if (setting == nullptr)
            return std::nullopt;
        return setting->mValue.getFloat();
    };

    return input;
}

void MWMechanics::Alchemy::updateEffects()
{
    mEffects.clear();
    mValue = 0;

    if (countIngredients() < 2 || mAlchemist.isEmpty() || mTools[ESM::Apparatus::MortarPestle].isEmpty())
        return;

    const Crafting::AlchemyMechanics::EffectsResult result
        = Crafting::AlchemyMechanics::updateEffects(makeMechanicsInput());
    mEffects = std::move(result.effects);
    mValue = result.value;
}

const ESM::Potion* MWMechanics::Alchemy::getRecord(const ESM::Potion& toFind) const
{
    const MWWorld::Store<ESM::Potion>& potions = MWBase::Environment::get().getESMStore()->get<ESM::Potion>();

    MWWorld::Store<ESM::Potion>::iterator iter = potions.begin();
    for (; iter != potions.end(); ++iter)
    {
        if (iter->mEffects.mList.size() != mEffects.size())
            continue;

        if (iter->mName != toFind.mName || iter->mScript != toFind.mScript
            || iter->mData.mWeight != toFind.mData.mWeight || iter->mData.mValue != toFind.mData.mValue
            || iter->mData.mFlags != toFind.mData.mFlags)
            continue;

        // Don't choose an ID that came from the content files, would have unintended side effects
        // where alchemy can be used to produce quest-relevant items
        if (!potions.isDynamic(iter->mId))
            continue;

        bool mismatch = false;

        for (size_t i = 0; i < iter->mEffects.mList.size(); ++i)
        {
            const ESM::IndexedENAMstruct& first = iter->mEffects.mList[i];
            const ESM::ENAMstruct& second = mEffects[i];

            if (first.mData.mEffectID != second.mEffectID || first.mData.mArea != second.mArea
                || first.mData.mRange != second.mRange || first.mData.mSkill != second.mSkill
                || first.mData.mAttribute != second.mAttribute || first.mData.mMagnMin != second.mMagnMin
                || first.mData.mMagnMax != second.mMagnMax || first.mData.mDuration != second.mDuration)
            {
                mismatch = true;
                break;
            }
        }

        if (!mismatch)
            return &(*iter);
    }

    return nullptr;
}

void MWMechanics::Alchemy::removeIngredients()
{
    for (TIngredientsContainer::iterator iter(mIngredients.begin()); iter != mIngredients.end(); ++iter)
        if (!iter->isEmpty())
        {
            iter->getContainerStore()->remove(*iter, 1);

            if (iter->getCellRef().getCount() < 1)
                *iter = MWWorld::Ptr();
        }

    updateEffects();
}

void MWMechanics::Alchemy::addPotion(const Crafting::PotionDefinition& definition)
{
    ESM::Potion newRecord;

    newRecord.mData.mWeight = definition.weight;
    newRecord.mData.mValue = definition.value;
    newRecord.mData.mFlags = 0;
    newRecord.mRecordFlags = 0;

    newRecord.mName = definition.name;
    newRecord.mModel = definition.model;
    newRecord.mIcon = definition.icon;

    newRecord.mEffects.populate(definition.effects);

    const ESM::Potion* record = getRecord(newRecord);
    if (!record)
        record = MWBase::Environment::get().getESMStore()->insert(newRecord);

    mAlchemist.getClass().getContainerStore(mAlchemist).add(record->mId, 1);
}

void MWMechanics::Alchemy::increaseSkill()
{
    mAlchemist.getClass().skillUsageSucceeded(mAlchemist, ESM::Skill::Alchemy, ESM::Skill::Alchemy_CreatePotion);
}

float MWMechanics::Alchemy::getAlchemyFactor() const
{
    return Crafting::AlchemyMechanics::getAlchemyFactor(makeMechanicsInput());
}

int MWMechanics::Alchemy::countIngredients() const
{
    int ingredients = 0;

    for (TIngredientsIterator iter(beginIngredients()); iter != endIngredients(); ++iter)
        if (!iter->isEmpty())
            ++ingredients;

    return ingredients;
}

int MWMechanics::Alchemy::countPotionsToBrew() const
{
    return Crafting::AlchemyMechanics::countPotionsToBrew(makeMechanicsInput(), mPotionName);
}

void MWMechanics::Alchemy::setAlchemist(const MWWorld::Ptr& npc)
{
    mAlchemist = npc;

    mIngredients.resize(4);

    std::fill(mIngredients.begin(), mIngredients.end(), MWWorld::Ptr());

    mTools.resize(4);

    std::vector<MWWorld::Ptr> prevTools(mTools);

    std::fill(mTools.begin(), mTools.end(), MWWorld::Ptr());

    mEffects.clear();

    MWWorld::ContainerStore& store = npc.getClass().getContainerStore(npc);

    for (auto iter(store.begin(MWWorld::ContainerStore::Type_Apparatus)); iter != store.end(); ++iter)
    {
        MWWorld::LiveCellRef<ESM::Apparatus>* ref = iter->get<ESM::Apparatus>();

        int type = ref->mBase->mData.mType;

        if (type < 0 || type >= static_cast<int>(mTools.size()))
            throw std::runtime_error("invalid apparatus type");

        if (prevTools[type] == *iter)
            mTools[type] = *iter; // prefer the previous tool if still in the container

        if (!mTools[type].isEmpty() && !prevTools[type].isEmpty() && mTools[type] == prevTools[type])
            continue;

        if (!mTools[type].isEmpty())
            if (ref->mBase->mData.mQuality <= mTools[type].get<ESM::Apparatus>()->mBase->mData.mQuality)
                continue;

        mTools[type] = *iter;
    }
}

MWMechanics::Alchemy::TToolsIterator MWMechanics::Alchemy::beginTools() const
{
    return mTools.begin();
}

MWMechanics::Alchemy::TToolsIterator MWMechanics::Alchemy::endTools() const
{
    return mTools.end();
}

MWMechanics::Alchemy::TIngredientsIterator MWMechanics::Alchemy::beginIngredients() const
{
    return mIngredients.begin();
}

MWMechanics::Alchemy::TIngredientsIterator MWMechanics::Alchemy::endIngredients() const
{
    return mIngredients.end();
}

void MWMechanics::Alchemy::clear()
{
    mAlchemist = MWWorld::Ptr();
    mIngredients.clear();
    mEffects.clear();
    setPotionName("");
}

void MWMechanics::Alchemy::setPotionName(const std::string& name)
{
    mPotionName = name;
}

int MWMechanics::Alchemy::addIngredient(const MWWorld::Ptr& ingredient)
{
    // find a free slot
    int slot = -1;

    for (int i = 0; i < static_cast<int>(mIngredients.size()); ++i)
        if (mIngredients[i].isEmpty())
        {
            slot = i;
            break;
        }

    if (slot == -1)
        return -1;

    for (TIngredientsIterator iter(mIngredients.begin()); iter != mIngredients.end(); ++iter)
        if (!iter->isEmpty() && ingredient.getCellRef().getRefId() == iter->getCellRef().getRefId())
            return -1;

    mIngredients[slot] = ingredient;

    updateEffects();

    return slot;
}

void MWMechanics::Alchemy::removeIngredient(size_t index)
{
    if (index < mIngredients.size())
    {
        mIngredients[index] = MWWorld::Ptr();
        updateEffects();
    }
}

void MWMechanics::Alchemy::addApparatus(const MWWorld::Ptr& apparatus)
{
    int32_t slot = apparatus.get<ESM::Apparatus>()->mBase->mData.mType;

    mTools[slot] = apparatus;

    updateEffects();
}

void MWMechanics::Alchemy::removeApparatus(size_t index)
{
    if (index < mTools.size())
    {
        mTools[index] = MWWorld::Ptr();
        updateEffects();
    }
}

MWMechanics::Alchemy::TEffectsIterator MWMechanics::Alchemy::beginEffects() const
{
    return mEffects.begin();
}

MWMechanics::Alchemy::TEffectsIterator MWMechanics::Alchemy::endEffects() const
{
    return mEffects.end();
}

bool MWMechanics::Alchemy::knownEffect(size_t potionEffectIndex, const MWWorld::Ptr& npc)
{
    float alchemySkill = npc.getClass().getSkill(npc, ESM::Skill::Alchemy);
    static const float fWortChanceValue
        = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>().find("fWortChanceValue")->mValue.getFloat();
    return (potionEffectIndex <= 1 && alchemySkill >= fWortChanceValue)
        || (potionEffectIndex <= 3 && alchemySkill >= fWortChanceValue * 2)
        || (potionEffectIndex <= 5 && alchemySkill >= fWortChanceValue * 3)
        || (potionEffectIndex <= 7 && alchemySkill >= fWortChanceValue * 4);
}

MWMechanics::Alchemy::Result MWMechanics::Alchemy::getReadyStatus() const
{
    switch (Crafting::AlchemyMechanics::getReadyStatus(makeMechanicsInput(), mPotionName))
    {
        case Crafting::AlchemyMechanics::Result::NoMortarAndPestle:
            return Result_NoMortarAndPestle;
        case Crafting::AlchemyMechanics::Result::LessThanTwoIngredients:
            return Result_LessThanTwoIngredients;
        case Crafting::AlchemyMechanics::Result::NoName:
            return Result_NoName;
        case Crafting::AlchemyMechanics::Result::NoEffects:
            return Result_NoEffects;
        case Crafting::AlchemyMechanics::Result::Success:
            return Result_Success;
        case Crafting::AlchemyMechanics::Result::RandomFailure:
            break;
    }
    return Result_RandomFailure;
}

MWMechanics::Alchemy::Result MWMechanics::Alchemy::create(const std::string& name, int& count)
{
#ifdef BUILD_MULTIPLAYER
    if (mwmp::Main::isConnected())
    {
        count = 0;
        return Result_ServerAuthorityRequired;
    }
#endif
    setPotionName(name);
    Result readyStatus = getReadyStatus();

    if (readyStatus == Result_NoEffects)
        removeIngredients();

    if (readyStatus != Result_Success)
        return readyStatus;

    MWBase::Environment::get().getWorld()->breakInvisibility(mAlchemist);

    Result result = Result_RandomFailure;
    int brewedCount = 0;
    for (int i = 0; i < count; ++i)
    {
        if (createSingle() == Result_Success)
        {
            result = Result_Success;
            brewedCount++;
        }
    }

    count = brewedCount;
    return result;
}

MWMechanics::Alchemy::Result MWMechanics::Alchemy::createSingle()
{
    if (beginEffects() == endEffects())
    {
        // all effects were nullified due to insufficient skill
        removeIngredients();
        return Result_RandomFailure;
    }
    auto& prng = MWBase::Environment::get().getWorld()->getPrng();
    const Crafting::AlchemyMechanics::Attempt attempt
        = Crafting::AlchemyMechanics::createSingle(makeMechanicsInput(), prng, mPotionName);
    if (!attempt.success)
    {
        removeIngredients();
        return Result_RandomFailure;
    }

    addPotion(attempt.potion);

    removeIngredients();

    increaseSkill();

    return Result_Success;
}

std::string MWMechanics::Alchemy::suggestPotionName()
{
    std::vector<MWMechanics::EffectKey> effects = listEffects();
    if (effects.empty())
        return {};

    return effects.begin()->toString();
}

#ifdef BUILD_MULTIPLAYER
bool MWMechanics::Alchemy::captureMultiplayerRequest(mwmp::records::AlchemyRequest& request, std::string& error) const
{
    request.ingredientInstanceIds.clear();
    request.apparatusInstanceIds.clear();

    for (const MWWorld::Ptr& ingredient : mIngredients)
    {
        if (ingredient.isEmpty())
            continue;
        const uint32_t instanceId = mwmp::inventoryInstanceId(ingredient.getCellRef().getRefNum());
        if (instanceId == 0)
        {
            error = "An ingredient stack has no stable inventory identity yet; try again in a moment.";
            return false;
        }
        request.ingredientInstanceIds.push_back(instanceId);
    }

    for (const MWWorld::Ptr& apparatus : mTools)
    {
        if (apparatus.isEmpty())
            continue;
        const uint32_t instanceId = mwmp::inventoryInstanceId(apparatus.getCellRef().getRefNum());
        if (instanceId == 0)
        {
            error = "An apparatus stack has no stable inventory identity yet; try again in a moment.";
            return false;
        }
        request.apparatusInstanceIds.push_back(instanceId);
    }
    return true;
}
#endif

std::vector<std::string> MWMechanics::Alchemy::effectsDescription(
    const MWWorld::ConstPtr& ptr, const float alchemySkill)
{
    std::vector<std::string> effects;

    const auto& item = ptr.get<ESM::Ingredient>()->mBase;
    const auto& store = MWBase::Environment::get().getESMStore();
    const auto& mgef = store->get<ESM::MagicEffect>();
    const static auto fWortChanceValue = store->get<ESM::GameSetting>().find("fWortChanceValue")->mValue.getFloat();
    const auto& data = item->mData;

    for (size_t i = 0; i < sNumEffects; ++i)
    {
        const auto effectID = data.mEffectID[i];

        if (alchemySkill < fWortChanceValue * static_cast<int>(i + 1))
            break;

        if (!effectID.empty())
        {
            const ESM::Attribute* attribute = store->get<ESM::Attribute>().search(data.mAttributes[i]);
            const ESM::Skill* skill = store->get<ESM::Skill>().search(data.mSkills[i]);
            std::string effect = getMagicEffectString(*mgef.find(effectID), attribute, skill);

            effects.push_back(std::move(effect));
        }
    }
    return effects;
}
