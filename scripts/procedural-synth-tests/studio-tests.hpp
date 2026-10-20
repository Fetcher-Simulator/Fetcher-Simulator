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
    std::cout<<"PASS native Studio block-invariant PCM, shared onsets, loop gates, seek, Stop, replacement and validation\n";
}
