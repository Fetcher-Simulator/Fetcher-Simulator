#ifndef OPENMW_MWLUA_SYNTHBINDINGS_H
#define OPENMW_MWLUA_SYNTHBINDINGS_H
#include "context.hpp"
namespace MWLua
{
    sol::table initSynthPackage(const Context& context);
}
#endif
