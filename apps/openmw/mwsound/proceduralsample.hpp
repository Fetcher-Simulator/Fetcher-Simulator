#ifndef OPENMW_MWSOUND_PROCEDURALSAMPLE_H
#define OPENMW_MWSOUND_PROCEDURALSAMPLE_H

#include <atomic>
#include <algorithm>
#include <array>
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
            unsigned char flags = 0, group = 0, offBy = 0;
            float releaseDecay = 4; // dB per held second, release-zone Q8 metadata
        };
        enum Status { Loading, Ready, Error };
        std::atomic<Status> status{Loading};
        std::vector<float> pcm; // mono 48 kHz; clips <=30 s, banks <=128 MiB
        std::vector<Zone> zones; // nonempty only for immutable multisample banks
        bool hasReleases = false; // v2 flag 2: recorded note-off zones
        bool hasHammer = false; // v2 flag 4: recorded upward connected attacks
        std::array<float,256> waveMin{},waveMax{};
        bool waveformReady=false;
        void buildWaveform() // loader thread only, raw excerpts only
        {
            if(pcm.empty())return;
            for(std::size_t n=0;n<waveMin.size();++n)
            {
                const auto first=n*pcm.size()/waveMin.size();
                const auto last=std::max(first+1,(n+1)*pcm.size()/waveMin.size());
                const auto bounds=std::minmax_element(pcm.begin()+first,pcm.begin()+last);
                waveMin[n]=*bounds.first;waveMax[n]=*bounds.second;
            }
            waveformReady=true;
        }
        std::string hash, error;
    };
}
#endif
