#include "../../apps/openmw-server/ServerLuaPackageSendQueue.hpp"
#include "../../apps/openmw/mwsound/proceduralstudio.hpp"
using NativeStudio=MWSound::ProceduralStudio;
std::string scoreNote(std::uint32_t frame,std::uint32_t gate,int lane,int pitch,int velocity=100)
{
    std::string bytes;
    for (auto value : {frame,gate}) for (int n=0;n<4;++n) bytes.push_back(static_cast<char>((value>>(8*n))&255));
    bytes.push_back(static_cast<char>(lane));bytes.push_back(static_cast<char>(pitch));bytes.push_back(static_cast<char>(velocity));return bytes;
}
void setupStudio(NativeStudio& studio)
{
    Synth::State state;for(std::size_t i=0;i<state.size();++i) state[i]=Synth::Parameters[i].initial;
    state[9]=0.002f;state[11]=1;state[12]=0.05f;
    check(studio.configure(1,state,1) && studio.configure(2,state,0.6f),"native track configuration");
}
std::vector<float> studioRender(NativeStudio& studio,unsigned frames,unsigned block=480)
{
    std::vector<float> samples(frames);
    for(unsigned at=0;at<frames;at+=block) studio.render(samples.data()+at,std::min(block,frames-at));
    for(float sample:samples) check(std::isfinite(sample) && std::abs(sample)<=1,"bounded native mix");
    return samples;
}
void studioTests()
{
    {
        mwmp::ServerLuaPackageSendQueue queue;
        for (unsigned i=0;i<20;++i) queue.push(std::vector<std::uint8_t>(48000,static_cast<std::uint8_t>(i)));
        unsigned expected=0, attempts=0;
        while (!queue.pump([&](const auto& packet) {
            if (++attempts%3==0) return false;
            check(packet.front()==expected++,"package retry preserves source/marker/character-list order");return true;
        })) {}
        check(expected==20,"package source larger than the socket buffer retries without dropping chunks");
    }
    {
        auto a=std::make_unique<NativeStudio>();auto b=std::make_unique<NativeStudio>();setupStudio(*a);setupStudio(*b);
        const auto score=scoreNote(0,24000,1,60)+scoreNote(0,65535,1,141,128)+scoreNote(0,0,1,185,128)
            +scoreNote(10000,1000,1,141,0)+scoreNote(10000,65535,1,185,0)+scoreNote(12000,65535,1,183,128)
            +scoreNote(23999,0,1,183,0);
        check(a->loadScore(score,24000,true) && b->loadScore(score,24000,true),"parameter/controller curves accepted");
        a->start(0,0);b->start(0,0);
        const auto expected=studioRender(*a,96000,480), irregular=studioRender(*b,96000,137);
        check(expected==irregular,"linear automation/controller PCM is block invariant across loops");
        check(energy(expected)>0 && a->strikes()==4,"controllers do not create attacks");
        a->stop();studioRender(*a,24000);a->noteOn(1,60,1);check(energy(studioRender(*a,24000))>0,"Stop removes automation and restores manual patch");
        check(!a->loadScore(scoreNote(0,65536,1,141,0),24000,true)
            && !a->loadScore(scoreNote(0,1,1,188,0),24000,true)
            && !a->loadScore(scoreNote(0,1,1,128,128),24000,true)
            && !a->loadScore(scoreNote(0,1,1,185,129),24000,true)
            && !a->loadScore(scoreNote(0,1,1,141,0)+scoreNote(0,2,1,141,128),24000,true),"malformed controls and duplicate curve times rejected");
        const auto pedalScore=scoreNote(0,65535,1,187,0)+scoreNote(100,1000,1,60)+scoreNote(6000,0,1,187,0);
        check(a->loadScore(pedalScore,12000,false) && a->start(0,0),"native sustain setup");
        studioRender(*a,4000);check(!a->activeNotes().empty(),"pedal defers scheduled note release");
        studioRender(*a,3000);check(a->activeNotes().empty(),"pedal-up releases deferred pitch");
        a->stop();studioRender(*a,24000);
        const auto mute=scoreNote(0,0,1,185,0)+scoreNote(12000,1000,1,60);
        check(a->loadScore(mute,24000,false) && a->start(0,.2),"controller chase seek setup");
        check(energy(studioRender(*a,20000))==0,"late listener chases expression before next onset");
        a->stop();studioRender(*a,24000);a->noteOn(1,60,1);
        check(energy(studioRender(*a,10000))>0,"finite end/Stop restores controller gain");
        const auto program=scoreNote(0,65535,1,151,129)+scoreNote(0,0,1,151,0)+scoreNote(100,1000,1,60);
        a->stop();studioRender(*a,24000);a->loadScore(program,24000,false);a->start(0,0);
        check(energy(studioRender(*a,12000))==0,"explicit automation overrides program snapshot");
        a->stop();studioRender(*a,24000);
        const auto bend=scoreNote(0,65535,1,183,0)+scoreNote(0,48000,1,69);
        a->loadScore(bend,48001,false);a->start(0,0);const auto bent=studioRender(*a,48000);
        unsigned crossings=0;for (unsigned i=24001;i<bent.size();++i) if (bent[i-1]<=0 && bent[i]>0) ++crossings;
        check(crossings>=246 && crossings<=247,"native pitch bend raises A4 by two semitones");
        a->stop();studioRender(*a,24000);
        const auto pedalEdge=scoreNote(0,1000,1,60)+scoreNote(1000,65535,1,187,0)+scoreNote(6000,0,1,187,0);
        a->loadScore(pedalEdge,12000,false);a->start(0,0);studioRender(*a,3000);
        check(!a->activeNotes().empty(),"same-frame pedal down precedes score release");
        a->stop();studioRender(*a,24000);
        std::cout<<"PASS automation ramps, controller chase/sustain, loop/reset, program precedence, bounded validation and block-invariant PCM\n";
    }
    {
        auto overdub=std::make_unique<NativeStudio>();setupStudio(*overdub);
        overdub->loadScore(scoreNote(0,4800,1,60),12000,false);overdub->start(0,0);studioRender(*overdub,1000);
        overdub->noteOn(1,60,1);studioRender(*overdub,100);overdub->noteOff(1,60);studioRender(*overdub,100);
        check(!overdub->activeNotes().empty() && overdub->trackVoices(1)>0,"live release cannot cut a same-pitch score gate");
        overdub->noteOn(1,60,1);studioRender(*overdub,6000);
        check(!overdub->activeNotes().empty() && overdub->trackVoices(1)>0,"score gate cannot cut a held recording key");
        overdub->noteOff(1,60);studioRender(*overdub,9600);
        check(overdub->activeNotes().empty() && overdub->voices()==0,"shared pitch releases after both sources finish");
    }
    auto silentStorage=std::make_unique<NativeStudio>();auto& silent=*silentStorage;
    check(silent.loadScore("",24000,true) && silent.start(0.01,0),"empty output score keeps native band clock");
    check(energy(studioRender(silent,96480,137))==0 && silent.playing() && silent.voices()==0 && silent.position()==2,
        "empty output loops in exact phase without allocating track voices");
    check(silent.units()==1 && silent.loadScore("",2400,false) && silent.replace(0),"silent finite replacement accepted");
    studioRender(silent,4800);check(!silent.playing(),"empty finite output completes");
    check(!silent.loadScore("",0,true),"empty output still requires a valid length");
    auto aStorage=std::make_unique<NativeStudio>();auto& a=*aStorage;auto bStorage=std::make_unique<NativeStudio>();auto& b=*bStorage;setupStudio(a);setupStudio(b);
    const auto score=scoreNote(37,12001,1,60)+scoreNote(37,6001,2,64)+scoreNote(6013,4000,2,67)+scoreNote(19000,20000,1,60);
    check(a.loadScore(score,24000,true) && b.loadScore(score,24000,true),"score loads with cross-loop gates");
    check(a.start(0.01,0) && b.start(0.01,0),"future native start");
    const auto expected=studioRender(a,96000,480), irregular=studioRender(b,96000,137);
    check(expected==irregular,"PCM and cross-track events are independent of render block boundaries");
    check(std::all_of(expected.begin(),expected.begin()+517,[](float sample){return sample==0;}),"start delay and exact onset retain silence before sample 517");
    check(a.position()>1.98 && a.playing() && a.strikes()>12,"transport advances and loops without Lua updates");
    a.stop();studioRender(a,9600);check(!a.playing() && a.voices()==0 && a.activeNotes().empty(),"Stop cancels future onsets and all gates");
    check(energy(studioRender(a,24000))==0,"cancelled score never restarts");
    auto lateStorage=std::make_unique<NativeStudio>();auto& late=*lateStorage;setupStudio(late);
    check(late.loadScore(scoreNote(0,20000,1,60)+scoreNote(12000,2000,2,67),24000,true) && late.start(0,0.125),"late seek accepted");
    studioRender(late,5000);check(late.strikes()==0 && late.activeNotes().empty(),"seek does not replay historical attacks");
    studioRender(late,2000);check(late.strikes()==1 && late.trackVoices(2)>0,"late listener enters at the next scheduled onset");
    check(late.loadScore(scoreNote(0,3000,1,72),24000,true) && late.replace(0.1),"live replacement queued");
    studioRender(late,4801);check(late.playing(),"replacement retains running transport");
    studioRender(late,24000);check(late.activeNotes().empty() || late.strikes()>1,"replacement reaches future loop without stale pointer");
    late.stop();late.noteOn(1,76,1);studioRender(late,1000);
    check(late.trackVoices(1)>0,"live note after Stop survives command ordering");
    late.stop();studioRender(late,9600);check(late.voices()==0,"live note releases on Stop");
    auto finiteStorage=std::make_unique<NativeStudio>();auto& finite=*finiteStorage;setupStudio(finite);
    check(finite.loadScore(scoreNote(0,1200,1,60),2400,false) && finite.start(0,0),"finite score accepted");
    studioRender(finite,24000);check(!finite.playing() && finite.voices()==0,"finite score finishes and releases");
    check(!finite.loadScore(scoreNote(0,1,17,60),24000,true) && !finite.loadScore(scoreNote(100,0,1,60),24000,true),"invalid lanes and zero gate rejected");
    check(!finite.loadScore(scoreNote(0,1,3,60),24000,true) && !finite.loadScore("bad",24000,true),"unconfigured track and malformed byte records rejected");
    check(!finite.loadScore(scoreNote(200,1,1,60)+scoreNote(100,1,1,60),24000,true),"unsorted score rejected");
    check(!finite.start(-1,0) && !finite.start(0,std::numeric_limits<double>::infinity()),"invalid transport times rejected");
    {
        std::string dense;
        for(unsigned n=0;n<640;++n) dense+=scoreNote(n*750,240,1,40+n%24);
        check(finite.loadScore(dense,480000,false) && finite.loadScore(dense,480000,true),"64-note/sec prerecorded band passes finite and looping native validation");
        check(finite.start(0,0),"dense band starts");studioRender(finite,96000);
        check(finite.strikes()>=128 && !finite.fault(),"dense band renders without note scheduler faults");
        finite.stop();studioRender(finite,24000);
        std::string burst;for(unsigned n=0;n<513;++n) burst+=scoreNote(0,1,1,60);
        check(!finite.loadScore(burst,48000,false),"513-note instantaneous flood rejected");
        std::string flood;for(unsigned n=0;n<4800;++n) flood+=scoreNote(n*120,1,1,60);
        check(!finite.loadScore(flood,576000,false),"sustained score density above 256/sec remains bounded");
        std::string edge;
        for(unsigned n=0;n<300;++n) edge+=scoreNote(0,1,1,60);
        for(unsigned n=0;n<300;++n) edge+=scoreNote(235200,1,1,60);
        check(finite.loadScore(edge,240000,false) && !finite.loadScore(edge,240000,true),"loop wrap shares the same burst budget instead of resetting it");
    }
    check(!finite.set(1,"polyphony",std::numeric_limits<float>::quiet_NaN()) && !finite.set(1,"polyphony",1.5f),"rack controls reject nonfinite and fractional polyphony");
    auto drain=std::make_unique<NativeStudio>();setupStudio(*drain);
    check(drain->loadScore(scoreNote(0,30000,1,60),12000,true) && drain->start(0,0),"cross-loop gate drain setup");
    studioRender(*drain,25000);
    check(drain->loadScore(scoreNote(0,1000,2,67),12000,false) && drain->replace(0),"finite replacement accepted after several loop cycles");
    studioRender(*drain,48000);
    check(!drain->playing() && drain->voices()==0 && drain->activeNotes().empty(),"old cross-loop gates release even after replacement transport finishes");
    auto mixedAStorage=std::make_unique<NativeStudio>();auto& mixedA=*mixedAStorage;auto mixedBStorage=std::make_unique<NativeStudio>();auto& mixedB=*mixedBStorage;setupStudio(mixedA);setupStudio(mixedB);
    auto strip=MWSound::ProceduralMixer::defaults();strip[0]=strip[6]=1;strip[2]=9;strip[3]=-8;strip[7]=-40;strip[8]=8;
    check(mixedA.mixer(1,strip) && mixedB.mixer(1,strip),"native per-lane mixer configured");
    check(!mixedA.mixer(17,strip),"out-of-range mixer lane rejected");
    strip[9]=0;check(!mixedA.mixer(1,strip),"invalid attack rejected without changing existing strip");
    mixedA.loadScore(score,24000,true);mixedB.loadScore(score,24000,true);mixedA.start(0,0);mixedB.start(0,0);
    check(studioRender(mixedA,96000,480)==studioRender(mixedB,96000,137),"active EQ/compression preserves block-independent scheduling and PCM");
    check(mixedA.trackPeak(1)>0 && mixedA.trackRms(1)>0 && mixedA.trackReduction(1)>1 && mixedA.masterRms()>0 && mixedA.mixPeak()>0,"post-fader lane meters, gain reduction and master meters are live");
    {
        auto full=std::make_unique<NativeStudio>();Synth::State patch;
        for (std::size_t i=0;i<patch.size();++i) patch[i]=Synth::Parameters[i].initial;
        std::string all;
        for (unsigned lane=1;lane<=NativeStudio::MaxTracks;++lane)
        {
            check(full->configure(lane,patch,.5f),"all 32 native tracks configure");
            all+=scoreNote(0,4800,lane,60);
        }
        check(NativeStudio::MaxTracks==32 && full->units()==33,"native track/output accounting includes the stream");
        check(!full->configure(33,patch,.5f),"native track 33 is rejected");
        check(full->loadScore(all,12000,false) && full->start(0,0),"32-part native score starts");
        studioRender(*full,1000);check(full->trackVoices(32)>0 && full->fault()==0,"track 32 sounds without a score fault");
        full->stop();studioRender(*full,24000);check(full->voices()==0,"32-track Stop releases every lane");
    }
    std::cout<<"PASS native Studio block-invariant PCM, shared onsets, loop gates, seek, Stop, replacement and validation\n";
}
