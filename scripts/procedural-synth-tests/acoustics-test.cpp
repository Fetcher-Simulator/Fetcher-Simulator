#include "../../apps/openmw/mwsound/instrumentacoustics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
void check(bool v, const char* why)
{
    if (!v)
        throw std::runtime_error(why);
}
int main()
{
    MWSound::InstrumentListening p;
    check(p.valid(), "default preferences");
    auto small = MWSound::InstrumentRoom::estimate(p);
    check(small.gain == 0, "open space is dry");
    p.enclosure = 1;
    p.width = 6;
    p.depth = 5;
    p.height = 3;
    small = MWSound::InstrumentRoom::estimate(p);
    p.width = 30;
    p.depth = 25;
    p.height = 12;
    auto hall = MWSound::InstrumentRoom::estimate(p);
    check(hall.decay > small.decay && hall.delay > small.delay, "room geometry controls decay and reflections");
    p.absorption = .8f;
    auto soft = MWSound::InstrumentRoom::estimate(p);
    check(soft.decay < hall.decay && soft.damping < hall.damping,
        "material absorption affects tail and high frequencies");
    p.room = false;
    check(MWSound::InstrumentRoom::estimate(p).gain == 0, "local dry bypass");
    p.reference = std::numeric_limits<float>::quiet_NaN();
    check(!p.valid(), "nonfinite preferences rejected");
    p.reference = 250;
    p.wet = 2;
    check(!p.valid(), "unbounded wet gain rejected");
    check(MWSound::InstrumentListening::rangeGain(500) == 1 && MWSound::InstrumentListening::rangeGain(2700) == .5f
            && MWSound::InstrumentListening::rangeGain(3100) == 0,
        "balanced/world preserve soft audible radius");
    std::cout << "PASS room geometry, reflection/material response, validation "
                 "and audible radius\n";
}
