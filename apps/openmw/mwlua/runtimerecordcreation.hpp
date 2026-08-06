#ifndef OPENMW_MWLUA_RUNTIMERECORDCREATION_H
#define OPENMW_MWLUA_RUNTIMERECORDCREATION_H

namespace ESM
{
    struct Activator;
    struct Armor;
    struct Book;
    struct Clothing;
    struct Container;
    struct Creature;
    struct Door;
    struct Enchantment;
    struct Ingredient;
    struct Light;
    struct Miscellaneous;
    struct NPC;
    struct Potion;
    struct Probe;
    struct Spell;
    struct Static;
    struct Weapon;
}

namespace MWLua
{
    struct Context;

#define MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Record) \
    const ESM::Record* createRuntimeRecord(const Context& context, const ESM::Record& record)

    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Activator);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Armor);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Book);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Clothing);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Container);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Creature);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Door);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Enchantment);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Ingredient);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Light);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Miscellaneous);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(NPC);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Potion);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Probe);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Spell);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Static);
    MWLUA_DECLARE_RUNTIME_RECORD_CREATION(Weapon);

#undef MWLUA_DECLARE_RUNTIME_RECORD_CREATION
}

#endif
