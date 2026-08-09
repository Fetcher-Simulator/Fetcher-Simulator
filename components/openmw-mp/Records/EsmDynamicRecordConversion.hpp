#ifndef OPENMW_MP_ESM_DYNAMIC_RECORD_CONVERSION_HPP
#define OPENMW_MP_ESM_DYNAMIC_RECORD_CONVERSION_HPP

#include <variant>

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadweap.hpp>

#include "DynamicRecordTypes.hpp"

namespace mwmp::records
{
    using EsmDynamicRecord
        = std::variant<ESM::Potion, ESM::Enchantment, ESM::Weapon, ESM::Armor, ESM::Clothing, ESM::Book,
            ESM::Dialogue, ESM::Script>;

    EsmDynamicRecord toEsmRecord(const DynamicRecordDefinition& definition);

    DynamicRecordDefinition fromEsmRecord(const ESM::Potion& record);
    DynamicRecordDefinition fromEsmRecord(const ESM::Enchantment& record);
    DynamicRecordDefinition fromEsmRecord(const ESM::Weapon& record);
    DynamicRecordDefinition fromEsmRecord(const ESM::Armor& record);
    DynamicRecordDefinition fromEsmRecord(const ESM::Clothing& record);
    DynamicRecordDefinition fromEsmRecord(const ESM::Book& record);
    DynamicRecordDefinition fromEsmRecord(const ESM::Dialogue& record);
    DynamicRecordDefinition fromEsmRecord(const ESM::Script& record);
}

#endif
