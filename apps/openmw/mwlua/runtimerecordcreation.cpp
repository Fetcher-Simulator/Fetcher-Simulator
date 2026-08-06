#include "runtimerecordcreation.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/lua/luastate.hpp>

#include "context.hpp"
#include "mutationaudit.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwworld/esmstore.hpp"

#ifdef BUILD_MULTIPLAYER
#include "../mwmp/Main.hpp"
#endif

namespace MWLua
{
    namespace
    {
        void checkRuntimeRecordCreationAllowed(const Context& context, std::string_view recordType)
        {
            if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame)
            {
                throw std::runtime_error(
                    "This function cannot be used until the game is fully initialized.\n" + context.mLua->debugTraceback());
            }

#ifdef BUILD_MULTIPLAYER
            if (mwmp::Main::isConnected())
            {
                std::string_view script = context.mLua->getActiveScriptPath();
                if (script.empty())
                    script = "<unknown-script>";

                throw std::runtime_error(std::string("world.createRecord is not available as a synchronous operation "
                    "while connected to multiplayer (script=")
                    + std::string(script) + ", recordType=" + std::string(recordType)
                    + ", connectionState=connected). Create a local draft for previews, then use the asynchronous "
                      "multiplayer record API or request creation through a server-side script.");
            }
#endif
        }

        template <class Record>
        const Record* createRuntimeRecordImpl(
            const Context& context, const Record& record, std::string_view recordType)
        {
            checkRuntimeRecordCreationAllowed(context, recordType);
            auditNativeMutation(context, "world.createRecord", "record-definition", recordType);

            if constexpr (std::is_same_v<Record, ESM::NPC>)
            {
                if (!record.mId.empty())
                {
                    ESM::NPC copy = record;
                    copy.mId = {};
                    return MWBase::Environment::get().getESMStore()->insert(copy);
                }
            }

            return MWBase::Environment::get().getESMStore()->insert(record);
        }
    }

#define MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Record)                                                        \
    const ESM::Record* createRuntimeRecord(const Context& context, const ESM::Record& record)               \
    {                                                                                                       \
        return createRuntimeRecordImpl(context, record, #Record);                                           \
    }

    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Activator)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Armor)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Book)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Clothing)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Container)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Creature)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Door)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Enchantment)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Ingredient)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Light)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Miscellaneous)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(NPC)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Potion)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Probe)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Spell)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Static)
    MWLUA_DEFINE_RUNTIME_RECORD_CREATION(Weapon)

#undef MWLUA_DEFINE_RUNTIME_RECORD_CREATION
}
