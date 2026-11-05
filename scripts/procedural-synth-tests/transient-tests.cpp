#include "../../apps/openmw/mwsound/proceduralsample.hpp"
#include <cstdlib>
#include <iostream>
using Sample=MWSound::ProceduralSample;
void check(bool value,const char* message){if(!value){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
void burst(Sample& s,double at,double level,double length=.04)
{
    for(std::size_t n=std::size_t(at*48000);n<std::min(s.pcm.size(),std::size_t((at+length)*48000));++n)
    {
        const double t=n/48000.0-at;
        s.pcm[n]+=float(level*std::sin(2*3.141592653589793*1700*t)*std::exp(-t/.012));
    }
}
int main()
{
    Sample s;s.pcm.resize(96000);burst(s,.2,1);burst(s,.65,.7);burst(s,1.25,.9);
    auto found=s.transients(.5,.08,false,false);check(found && found->points.size()==3,"three separate drum-like attacks");
    const double expected[]={.2,.65,1.25};
    for(unsigned n=0;n<3;++n)check(std::abs(found->points[n]-expected[n])<.013,"suggestion leaves the attack intact within 13ms");
    const auto points=found->points;for(auto& value:s.pcm)value*=.07f;
    auto quiet=s.transients(.5,.08,false,false);check(quiet && quiet->points==points,"detection is relative to clip level");
    Sample weak;weak.pcm.resize(96000);burst(weak,.2,1);burst(weak,.9,.045);
    auto high=weak.transients(1,.08,false,false),low=weak.transients(0,.08,false,false);
    check(high && low && high->points.size()>low->points.size(),"sensitivity reveals a quieter attack");
    Sample dense;dense.pcm.resize(48000*5);
    for(unsigned n=0;n<24;++n)burst(dense,.1+n*.2,1,.025);
    auto capped=dense.transients(.5,.08,false,true);check(capped && capped->found==24 && capped->points.size()==15,"marker cap reports omitted attacks");
    auto spaced=dense.transients(.5,.5,false,true);check(spaced && spaced->found<capped->found,"minimum gap groups nearby attacks");
    for(std::size_t n=1;n<spaced->points.size();++n)check(spaced->points[n]-spaced->points[n-1]>=.5-1e-9,"gap remains valid after zero snap and millisecond rounding");
    auto reversed=s.transients(1,.08,true,true);
    Sample reversedPcm;reversedPcm.pcm.assign(s.pcm.rbegin(),s.pcm.rend());
    auto explicitReverse=reversedPcm.transients(1,.08,false,true);
    check(reversed && explicitReverse && reversed->points==explicitReverse->points && reversed->points.size()==3,"reverse analyses displayed PCM, including slower reversed attacks at high sensitivity");
    check(s.pcm[0]==0 && s.pcm.size()==96000,"analysis does not modify PCM");
    Sample silence;silence.pcm.resize(48000);check(silence.transients(.5,.08,false,true)->points.empty(),"silence has no attacks");
    silence.pcm.assign(48000,.5f);check(silence.transients(.5,.08,false,false)->points.empty(),"steady signal does not produce repeated cuts");
    for(std::size_t n=0;n<silence.pcm.size();++n)silence.pcm[n]=float(.5*std::sin(2*3.141592653589793*220*n/48000));
    check(silence.transients(.5,.08,false,false)->points.empty(),"steady musical tone is not split by waveform cycles");
    check(!s.transients(-.1,.08,false,false) && !s.transients(.5,0,false,false)
        && !s.transients(.5,std::nan(""),false,false),"invalid controls are rejected");
    s.zones.push_back({});check(!s.transients(.5,.08,false,false),"banks retain their zone behaviour");
    Sample maximum;maximum.pcm.resize(1440000);burst(maximum,29,1);
    check(maximum.transients(.5,.08,false,false)->points.size()==1,"thirty-second excerpt remains bounded");
    maximum.pcm.push_back(0);check(!maximum.transients(.5,.08,false,false),"oversized analysis is rejected");
    std::cout<<"PASS native transient detection: attack timing, level invariance, sensitivity, spacing, reverse, zero snap, cap and bounds\n";
}
