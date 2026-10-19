#include "synthbindings.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwsound/proceduralsynth.hpp"
#include "../mwsound/proceduralstudio.hpp"
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
            api["version"] = 9;
            using Studio = MWSound::ProceduralStudio;
            lua.new_usertype<Studio>("ProceduralStudio", sol::no_constructor,
                "closed", sol::property(&Studio::closed), "close", &Studio::close,
                "keepAlive", &Studio::keepAlive, "stop", &Studio::stop,
                "playing", sol::property(&Studio::playing), "position", sol::property(&Studio::position),
                "audiblePosition", sol::property([](const Studio& studio) {
                    return std::max(0.0, studio.position() - MWBase::Environment::get().getSoundManager()->synthDelay(&studio));
                }),
                "fault", sol::property(&Studio::fault), "peak", sol::property(&Studio::peak),
                "voices", sol::property(&Studio::voices), "strikes", sol::property(&Studio::strikes),
                "noteOn", &Studio::noteOn, "noteOff", &Studio::noteOff,
                "releaseTrack", &Studio::releaseTrack, "trackPeak", &Studio::trackPeak, "trackVoices", &Studio::trackVoices,
                "delayPeak", sol::property(&Studio::delayPeak), "reverbPeak", sol::property(&Studio::reverbPeak),
                "effects", [](Studio& studio, const sol::table& values, float master) {
                    MWSound::ProceduralReturns::State state;
                    for (std::size_t i=0;i<state.size();++i)
                        state[i]=values[MWSound::ProceduralReturns::Parameters[i].name].get_or(MWSound::ProceduralReturns::Parameters[i].initial);
                    return studio.effects(state,master);
                },
                "trackRms", &Studio::trackRms, "trackReduction", &Studio::trackReduction,
                "masterRms", sol::property(&Studio::masterRms), "mixPeak", sol::property(&Studio::mixPeak),
                "mixer", [](Studio& studio, unsigned n, const sol::table& values) {
                    MWSound::ProceduralMixer::State state;
                    for (std::size_t i=0;i<state.size();++i)
                        state[i]=values[MWSound::ProceduralMixer::Parameters[i].name].get_or(MWSound::ProceduralMixer::Parameters[i].initial);
                    return studio.mixer(n,state);
                },
                "set", &Studio::set, "loadScore", &Studio::loadScore,
                "notes", [lua](const Studio& studio) {
                    sol::table notes(lua, sol::create);
                    for (unsigned key : studio.activeNotes()) notes[key] = true;
                    return notes;
                },
                "configure", [](Studio& studio, unsigned n, const sol::table& values, float level) {
                    auto sound = MWBase::Environment::get().getSoundManager();
                    if (!studio.hasTrack(n) && sound->synthUnits() >= 32) return false;
                    Synth::State state;
                    for (std::size_t i = 0; i < state.size(); ++i)
                        state[i] = values[Synth::Parameters[i].name].get_or(Synth::Parameters[i].initial);
                    return studio.configure(n, state, level);
                },
                "start", [](Studio& studio, double delay, double offset) {
                    const double queued = MWBase::Environment::get().getSoundManager()->synthDelay(&studio);
                    const double lead = delay - queued;
                    return studio.start(std::max(0.0, lead), offset + std::max(0.0, -lead));
                }, "replace", [](Studio& studio, double delay) {
                    const double queued = MWBase::Environment::get().getSoundManager()->synthDelay(&studio);
                    return studio.replace(std::max(0.0, delay - queued));
                });
            api["createStudio"] = [](const sol::object& object) {
                return MWBase::Environment::get().getSoundManager()->createStudio(ObjectVariant(object).ptr());
            };
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
            api["maxUnits"] = 32;
            api["maxScheduledNotes"] = Studio::MaxNotes;
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
            sol::table mixerParameters(lua, sol::create);
            for (const auto& p : MWSound::ProceduralMixer::Parameters)
            {
                sol::table spec(lua, sol::create);
                spec["min"] = p.min; spec["max"] = p.max; spec["default"] = p.initial;
                spec["discrete"] = std::string_view(p.name)=="eqEnabled" || std::string_view(p.name)=="compEnabled";
                mixerParameters[p.name] = LuaUtil::makeReadOnly(spec);
            }
            api["mixerParameters"] = LuaUtil::makeReadOnly(mixerParameters);
            return LuaUtil::makeReadOnly(api);
        });
    }
}
