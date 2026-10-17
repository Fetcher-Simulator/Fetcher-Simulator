#include "synthbindings.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwsound/proceduralsynth.hpp"
#include "objectvariant.hpp"
#include "luamanagerimp.hpp"
#include <components/settings/values.hpp>
#include <cmath>

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
            api["version"] = 3;
            api["getMonitorVolume"] = []() -> float { return Settings::sound().mInstrumentVolume; };
            api["setMonitorVolume"] = [manager = context.mLuaManager](float value) {
                if (!std::isfinite(value) || value < 0 || value > 1)
                    throw std::invalid_argument("Monitor volume must be between 0 and 1");
                manager->addAction([value] {
                    Settings::sound().mInstrumentVolume.set(value);
                    MWBase::Environment::get().getSoundManager()->processChangedSettings(
                        { { "Sound", "instrument volume" } });
                }, "Set instrument monitor volume");
            };
            api["maxInstances"] = 32;
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
