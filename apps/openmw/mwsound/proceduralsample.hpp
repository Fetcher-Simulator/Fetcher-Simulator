#ifndef OPENMW_MWSOUND_PROCEDURALSAMPLE_H
#define OPENMW_MWSOUND_PROCEDURALSAMPLE_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace MWSound
{
    // Published once by the loader. Ready/Error use release/acquire; the PCM,
    // fingerprint and diagnostic are immutable after publication.
    struct ProceduralSample
    {
        struct Zone
        {
            unsigned char lowKey, highKey, lowVelocity, highVelocity, root, alternate, alternates;
            std::uint32_t start, length;
        };
        enum Status { Loading, Ready, Error };
        std::atomic<Status> status{Loading};
        std::vector<float> pcm; // mono 48 kHz; clips <=30 s, banks <=128 MiB
        std::vector<Zone> zones; // nonempty only for immutable multisample banks
        std::string hash, error;
    };
}
#endif
