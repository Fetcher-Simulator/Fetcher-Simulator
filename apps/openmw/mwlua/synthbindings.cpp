#include "synthbindings.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwsound/proceduralsynth.hpp"
#include "objectvariant.hpp"

namespace MWLua
{
    sol::table initSynthPackage(const Context& context)
    {
        return context.cachePackage("openmw_synth", [&context]() {
            sol::state_view lua = context.sol();
            using Synth = MWSound::ProceduralSynth;
            lua.new_usertype<Synth>("ProceduralSynth", sol::no_constructor, "set", &Synth::set, "noteOn",
                &Synth::noteOn, "noteOff", &Synth::noteOff, "allNotesOff", &Synth::allNotesOff, "close", &Synth::close,
                "keepAlive", &Synth::keepAlive, "closed", sol::property(&Synth::closed), "peak",
                sol::property(&Synth::peak), "voices", sol::property(&Synth::voices));
            sol::table api(lua, sol::create);
            api["version"] = 1;
            api["sampleRate"] = Synth::SampleRate;
            api["create"] = [](const sol::object& object) {
                auto ptr = ObjectVariant(object).ptr();
                return MWBase::Environment::get().getSoundManager()->createSynth(ptr);
            };
            sol::table parameters(lua, sol::create);
            for (const auto& p : Synth::Parameters)
            {
                sol::table spec(lua, sol::create);
                spec["min"] = p.min;
                spec["max"] = p.max;
                spec["default"] = p.initial;
                spec["discrete"] = p.discrete;
                parameters[p.name] = LuaUtil::makeReadOnly(spec);
            }
            api["parameters"] = LuaUtil::makeReadOnly(parameters);
            return LuaUtil::makeReadOnly(api);
        });
    }
}
