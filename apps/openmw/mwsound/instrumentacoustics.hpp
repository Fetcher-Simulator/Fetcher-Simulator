#ifndef OPENMW_MWSOUND_INSTRUMENTACOUSTICS_H
#define OPENMW_MWSOUND_INSTRUMENTACOUSTICS_H
#include <algorithm>
#include <cmath>
namespace MWSound
{
    // Local listener preferences and bounded geometry estimates, never score data.
    struct InstrumentListening
    {
        bool balanced = false, room = true;
        float reference = 250, rolloff = .45f, wet = .3f;
        float width = 12, depth = 12, height = 4, enclosure = 0, absorption = .35f, scattering = .3f;
        bool valid() const
        {
            return std::isfinite(reference) && reference >= 50 && reference <= 1000 && std::isfinite(rolloff)
                && rolloff >= .1f && rolloff <= 2 && std::isfinite(wet) && wet >= 0 && wet <= 1 && std::isfinite(width)
                && width >= 1 && width <= 200 && std::isfinite(depth) && depth >= 1 && depth <= 200
                && std::isfinite(height) && height >= 1 && height <= 100 && std::isfinite(enclosure) && enclosure >= 0
                && enclosure <= 1 && std::isfinite(absorption) && absorption >= .05f && absorption <= .95f
                && std::isfinite(scattering) && scattering >= 0 && scattering <= 1;
        }
        static float rangeGain(float distance) { return std::clamp((3000.f - distance) / 600.f, 0.f, 1.f); }
    };
    struct InstrumentRoom
    {
        float decay = 1, delay = .01f, lateDelay = .02f, diffusion = 1, density = 1, damping = .7f, gain = 0;
        static InstrumentRoom estimate(const InstrumentListening& p)
        {
            InstrumentRoom r;
            const float volume = p.width * p.depth * p.height;
            const float area = 2 * (p.width * p.depth + p.width * p.height + p.depth * p.height);
            const float path = 4 * volume / area;
            r.decay = std::clamp(.161f * volume / (area * p.absorption), .15f, 4.f);
            r.delay = std::clamp(path / 343.f, .002f, .15f);
            r.lateDelay = std::clamp(1.7f * path / 343.f, .004f, .08f);
            const float longest = std::max({ p.width, p.depth, p.height }),
                        shortest = std::min({ p.width, p.depth, p.height });
            r.diffusion = std::clamp(.65f + .3f * p.scattering - .12f * (longest / shortest - 1), .35f, .95f);
            r.density = std::clamp(std::cbrt(volume) / 12.f, .2f, 1.f);
            r.damping = std::clamp(1 - p.absorption, .15f, .95f);
            r.gain = p.room ? p.wet * p.enclosure : 0;
            return r;
        }
    };
}
#endif
