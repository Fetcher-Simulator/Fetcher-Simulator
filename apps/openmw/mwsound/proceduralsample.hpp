#ifndef OPENMW_MWSOUND_PROCEDURALSAMPLE_H
#define OPENMW_MWSOUND_PROCEDURALSAMPLE_H

#include <atomic>
#include <string>
#include <vector>

namespace MWSound
{
    // Published once by the loader. Ready/Error use release/acquire; the PCM,
    // fingerprint and diagnostic are immutable after publication.
    struct ProceduralSample
    {
        enum Status { Loading, Ready, Error };
        std::atomic<Status> status{Loading};
        std::vector<float> pcm; // mono, 48 kHz, at most thirty seconds
        std::string hash, error;
    };
}
#endif
