#include "synthbindings.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwsound/instrumentacoustics.hpp"
#include "../mwsound/proceduralsamplebank.hpp"
#include "../mwsound/proceduralstudio.hpp"
#include "../mwsound/proceduralsynth.hpp"
#include "luamanagerimp.hpp"
#include "objectvariant.hpp"
#include "studiomidi.hpp"
#include <cmath>
#include <components/files/conversion.hpp>
#include <components/misc/constants.hpp>
#include <components/settings/values.hpp>

namespace MWLua
{
    namespace
    {
        struct SampleSettings
        {
            std::shared_ptr<MWSound::ProceduralSample> data;
            int root = 60;
            bool oneShot = false;
            std::string hash;
        };
        SampleSettings readSample(const sol::object& value)
        {
            SampleSettings result;
            if (!value.valid() || value == sol::nil) return result;
            const auto table = value.as<sol::table>();
            auto path = table["path"].get<std::string>();
            const double offset = table["offset"].get<double>(), duration = table["duration"].get<double>();
            const double root = table["root"].get<double>();
            result.oneShot = table["oneShot"].get<bool>();
            result.hash = table["hash"].get<std::string>();
            if (!MWSound::ProceduralSampleBank::valid(path, offset, duration) || !std::isfinite(root)
                || root < 0 || root > 127 || std::floor(root) != root || result.hash.size() != 32
                || result.hash.find_first_not_of("0123456789abcdef") != result.hash.npos)
                throw std::invalid_argument("Invalid sample settings");
            if(path.ends_with(".fsb") && (root!=60 || result.oneShot))
                throw std::invalid_argument("Instrument banks use fixed zone roots and gated playback");
            result.root = static_cast<int>(root);
            result.data = MWBase::Environment::get().getSoundManager()->requestSample(std::move(path), offset, duration);
            return result;
        }
    }
    sol::table initSynthPackage(const Context& context)
    {
        return context.cachePackage("openmw_synth", [&context]() {
            sol::state_view lua = context.sol();
            using Synth = MWSound::ProceduralSynth;
            lua.new_usertype<Synth>(
                "ProceduralSynth", sol::no_constructor, "set", &Synth::set, "setMonitorEligible",
                [](Synth& s, bool value) { s.monitorEligible(value); }, "setObstruction",
                [](Synth& s, float value) { return s.obstruction(value); }, "noteOn", &Synth::noteOn, "noteOff",
                &Synth::noteOff, "allNotesOff", &Synth::allNotesOff, "close", &Synth::close, "keepAlive",
                &Synth::keepAlive, "closed", sol::property(&Synth::closed), "peak", sol::property(&Synth::peak),
                "voices", sol::property(&Synth::voices), "sample",
                [](Synth& synth, const sol::object& value) {
                    auto s = readSample(value);
                    return synth.sample(std::move(s.data), s.root, s.oneShot, s.hash);
                });
            sol::table api(lua, sol::create);
            api["version"] = 22;
            api["exportMidi"] = [manager = context.mLuaManager, lua](std::string name, std::string bytes) {
                const auto path = manager->exportStudioMidi(name, bytes);
                sol::table result(lua, sol::create);
                result["name"] = Files::pathToUnicodeString(path.filename());
                result["path"] = Files::pathToUnicodeString(path);
                return LuaUtil::makeReadOnly(result);
            };
            api["listMidiExports"] = [manager = context.mLuaManager] { return sol::as_table(manager->listStudioMidi()); };
            api["readMidiExport"] = [manager = context.mLuaManager](std::string name) { return manager->readStudioMidi(name); };
            api["sampleInfo"] = [lua](std::string path, double offset, double duration) {
                const auto data = MWBase::Environment::get().getSoundManager()->requestSample(std::move(path), offset, duration);
                sol::table info(lua, sol::create);
                const auto status = data->status.load(std::memory_order_acquire);
                info["status"] = status == MWSound::ProceduralSample::Ready ? "Ready"
                    : status == MWSound::ProceduralSample::Loading ? "Loading" : "Unavailable";
                if (status != MWSound::ProceduralSample::Loading)
                {
                    info["hash"] = data->hash; info["error"] = data->error;
                    info["duration"] = data->pcm.size() / 48000.0;
                    if(!data->zones.empty())
                    {
                        info["zones"]=data->zones.size();
                        std::uint32_t longest=0;for(const auto& zone:data->zones)longest=std::max(longest,zone.length);
                        info["duration"]=longest/48000.0;
                    }
                }
                return LuaUtil::makeReadOnly(info);
            };
            using Studio = MWSound::ProceduralStudio;
            lua.new_usertype<Studio>(
                "ProceduralStudio", sol::no_constructor, "setMonitorEligible",
                [](Studio& s, bool value) { s.monitorEligible(value); }, "monitorEligible",
                sol::property([](const Studio& s) { return s.monitorEligible(); }), "setObstruction",
                [](Studio& s, float value) { return s.obstruction(value); }, "closed", sol::property(&Studio::closed),
                "close", &Studio::close, "keepAlive", &Studio::keepAlive, "stop", &Studio::stop, "playing",
                sol::property(&Studio::playing), "position", sol::property(&Studio::position), "audiblePosition",
                sol::property([](const Studio& studio) {
                    return std::max(
                        0.0, studio.position() - MWBase::Environment::get().getSoundManager()->synthDelay(&studio));
                }),
                "outputDelay", sol::property([](const Studio& studio) {
                    return MWBase::Environment::get().getSoundManager()->synthDelay(&studio);
                }),
                "fault", sol::property(&Studio::fault), "peak", sol::property(&Studio::peak), "voices",
                sol::property(&Studio::voices), "strikes", sol::property(&Studio::strikes), "noteOn", &Studio::noteOn,
                "noteOff", &Studio::noteOff, "releaseTrack", &Studio::releaseTrack, "trackPeak", &Studio::trackPeak,
                "trackVoices", &Studio::trackVoices, "delayPeak", sol::property(&Studio::delayPeak), "reverbPeak",
                sol::property(&Studio::reverbPeak), "effects",
                [](Studio& studio, const sol::table& values, float master) {
                    MWSound::ProceduralReturns::State state;
                    for (std::size_t i = 0; i < state.size(); ++i)
                        state[i]=values[MWSound::ProceduralReturns::Parameters[i].name].get_or(MWSound::ProceduralReturns::Parameters[i].initial);
                    return studio.effects(state, master);
                },
                "trackRms", &Studio::trackRms, "trackReduction", &Studio::trackReduction, "masterRms",
                sol::property(&Studio::masterRms), "mixPeak", sol::property(&Studio::mixPeak), "mixer",
                [](Studio& studio, unsigned n, const sol::table& values) {
                    MWSound::ProceduralMixer::State state;
                    for (std::size_t i = 0; i < state.size(); ++i)
                        state[i]=values[MWSound::ProceduralMixer::Parameters[i].name].get_or(MWSound::ProceduralMixer::Parameters[i].initial);
                    return studio.mixer(n, state);
                },
                "set", &Studio::set, "monitor", &Studio::monitor, "loadScore", &Studio::loadScore, "notes",
                [lua](const Studio& studio) {
                    sol::table notes(lua, sol::create);
                    for (unsigned key : studio.activeNotes())
                        notes[key] = true;
                    return notes;
                },
                "configure",
                [](Studio& studio, unsigned n, const sol::table& values, float level) {
                    auto sound = MWBase::Environment::get().getSoundManager();
                    if (!studio.hasTrack(n) && sound->synthUnits() >= MWSound::ProceduralAudio::MaxUnits)
                        return false;
                    Synth::State state;
                    for (std::size_t i = 0; i < state.size(); ++i)
                        state[i] = values[Synth::Parameters[i].name].get_or(Synth::Parameters[i].initial);
                    auto sample = readSample(values["sample"]);
                    return studio.configure(n, state, level)
                        && studio.sample(n, std::move(sample.data), sample.root, sample.oneShot, sample.hash);
                },
                "start",
                [](Studio& studio, double delay, double offset) {
                    const double queued = MWBase::Environment::get().getSoundManager()->synthDelay(&studio);
                    const double lead = delay - queued;
                    return studio.start(std::max(0.0, lead), offset + std::max(0.0, -lead));
                },
                "replace",
                [](Studio& studio, double delay) {
                    const double queued = MWBase::Environment::get().getSoundManager()->synthDelay(&studio);
                    return studio.replace(std::max(0.0, delay - queued));
                });
            api["createStudio"] = [](const sol::object& object) {
                return MWBase::Environment::get().getSoundManager()->createStudio(ObjectVariant(object).ptr());
            };
            api["createMonitor"] = [](const sol::object& object) {
                return MWBase::Environment::get().getSoundManager()->createStudio(ObjectVariant(object).ptr(), true);
            };
            lua.new_usertype<StudioMidiInput>("StudioMidiInput", sol::no_constructor,
                "close", &StudioMidiInput::close, "connected", sol::property(&StudioMidiInput::connected),
                "poll", [lua](StudioMidiInput& input) {
                    sol::table result(lua, sol::create), events(lua, sol::create);
                    unsigned lost=0;
                    const bool connected=input.connected();
                    if (input.queue) {
                        const auto batch=input.queue->drain(lost);const double elapsed=StudioMidiInput::now()-input.started;
                        int index=1;
                        for (const auto& e : batch) {
                            sol::table event(lua, sol::create);event["status"]=e.status & 0xf0;event["channel"]=(e.status & 15)+1;
                            event["a"]=e.a;event["b"]=e.b;event["age"]=std::max(0.0,elapsed-e.time);events[index++]=event;
                        }
                    }
                    result["events"]=events;result["dropped"]=lost;result["connected"]=connected;return result;
                });
            api["midiAvailable"] = StudioMidiInput::available();
            api["midiDevices"] = [lua] {
                sol::table result(lua, sol::create);int index=1;
                for (const auto& device : StudioMidiInput::devices()) {
                    sol::table item(lua, sol::create);item["id"]=device.id;item["name"]=device.name;result[index++]=item;
                }
                return result;
            };
            api["openMidiInput"] = &StudioMidiInput::open;
            api["getMonitorVolume"] = []() -> float { return Settings::sound().mInstrumentVolume; };
            api["unitsPerMeter"] = Constants::UnitsPerMeter;
            api["getListening"] = [lua] {
                auto manager = MWBase::Environment::get().getSoundManager();
                const auto p = manager->instrumentListening();
                const auto r = MWSound::InstrumentRoom::estimate(p);
                sol::table t(lua, sol::create);
                t["balanced"] = p.balanced;
                t["room"] = p.room;
                t["reference"] = p.reference;
                t["rolloff"] = p.rolloff;
                t["wet"] = p.wet;
                t["width"] = p.width;
                t["depth"] = p.depth;
                t["height"] = p.height;
                t["enclosure"] = p.enclosure;
                t["absorption"] = p.absorption;
                t["scattering"] = p.scattering;
                t["decay"] = r.decay;
                t["reflectionDelay"] = r.delay;
                t["reflectionsAvailable"] = manager->instrumentReflectionsAvailable();
                const auto counts = manager->instrumentOutputCounts();
                t["worldOutputs"] = counts.first;
                t["balancedOutputs"] = counts.second;
                t["units"] = manager->synthUnits();
                t["listenerPosition"] = manager->instrumentListenerPosition();
                return t;
            };
            api["setListening"] = [manager = context.mLuaManager](const sol::table& t) {
                MWSound::InstrumentListening p;
                p.balanced = t["balanced"].get_or(false);
                p.room = t["room"].get_or(true);
                p.reference = t["reference"].get_or(250.f);
                p.rolloff = t["rolloff"].get_or(.45f);
                p.wet = t["wet"].get_or(.3f);
                p.width = t["width"].get_or(12.f);
                p.depth = t["depth"].get_or(12.f);
                p.height = t["height"].get_or(4.f);
                p.enclosure = t["enclosure"].get_or(0.f);
                p.absorption = t["absorption"].get_or(.35f);
                p.scattering = t["scattering"].get_or(.3f);
                if (!p.valid())
                    throw std::invalid_argument("Invalid instrument listening/acoustics settings");
                manager->addAction([p] { MWBase::Environment::get().getSoundManager()->instrumentListening(p); },
                    "Set instrument listening");
            };
            api["setMonitorVolume"] = [manager = context.mLuaManager](float value) {
                if (!std::isfinite(value) || value < 0 || value > 1)
                    throw std::invalid_argument("Monitor volume must be between 0 and 1");
                manager->addAction([value] {
                    Settings::sound().mInstrumentVolume.set(value);
                    MWBase::Environment::get().getSoundManager()->processChangedSettings(
                        { { "Sound", "instrument volume" } });
                }, "Set instrument monitor volume");
            };
            api["maxInstances"] = MWSound::ProceduralAudio::MaxUnits;
            api["maxUnits"] = MWSound::ProceduralAudio::MaxUnits;
            api["maxScheduledNotes"] = Studio::MaxNotes;
            api["scheduledNoteRate"] = Studio::NoteRate;
            api["scheduledNoteBurst"] = Studio::NoteBurst;
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
