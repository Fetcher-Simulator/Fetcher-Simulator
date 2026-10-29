#ifndef OPENMW_MWSOUND_PROCEDURALINSTRUMENT_H
#define OPENMW_MWSOUND_PROCEDURALINSTRUMENT_H
#include "proceduralsample.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <istream>
#include <stdexcept>

namespace MWSound
{
    // Fetcher sample bank v1/v2: bounded little-endian note/velocity zones followed
    // by mono 48-kHz signed 16-bit PCM. No paths, scripts, SFZ or external data.
    // V2 adds per-zone one-shot and choke groups. Loader thread only;
    // Ready publication makes both vectors immutable.
    inline void readInstrument(std::istream& stream, std::uint64_t bytes, ProceduralSample& out,
        const std::function<void(std::size_t)>& reserve)
    {
        auto number=[&](unsigned n) {
            std::uint32_t value=0;
            for(unsigned i=0;i<n;++i) {int b=stream.get();if(b<0)throw std::runtime_error("Truncated instrument bank");value|=std::uint32_t(b)<<(i*8);}
            return value;
        };
        std::array<char,8> magic{};stream.read(magic.data(),magic.size());
        const bool version2=magic==std::array<char,8>{'F','S','B','A','N','K','2',0};
        if(!version2 && magic!=std::array<char,8>{'F','S','B','A','N','K','1',0})throw std::runtime_error("Invalid instrument bank signature");
        const auto zones=number(4),frames=number(4),rate=number(4),reserved=number(4);
        if(!zones || zones>512 || frames<2 || frames>32u*1024*1024 || rate!=48000 || reserved
            || bytes!=24ull+zones*(version2?20ull:16ull)+frames*2ull)throw std::runtime_error("Invalid instrument bank bounds");
        out.zones.reserve(zones);
        for(unsigned n=0;n<zones;++n)
        {
            ProceduralSample::Zone z{};
            const auto byte=[&] {return static_cast<unsigned char>(number(1));};
            z.lowKey=byte();z.highKey=byte();z.lowVelocity=byte();z.highVelocity=byte();
            z.root=byte();z.alternate=byte();z.alternates=byte();const auto flags=number(1);
            unsigned extra=0;
            if(version2) {z.flags=static_cast<unsigned char>(flags);z.group=byte();z.offBy=byte();extra=number(2);}
            z.start=number(4);z.length=number(4);
            if(z.lowKey>z.highKey || z.highKey>127 || !z.lowVelocity || z.lowVelocity>z.highVelocity
                || z.highVelocity>127 || z.root>127 || !z.alternates || z.alternates>8 || z.alternate>=z.alternates
                || (version2 ? flags>1 || z.group>15 || z.offBy>15 || extra : flags!=0)
                || z.length<2 || z.length>1440000 || std::uint64_t(z.start)+z.length>frames)
                throw std::runtime_error("Invalid instrument sample zone");
            for(const auto& previous:out.zones)
            {
                const bool overlaps=z.lowKey<=previous.highKey && previous.lowKey<=z.highKey
                    && z.lowVelocity<=previous.highVelocity && previous.lowVelocity<=z.highVelocity;
                if(overlaps && (z.lowKey!=previous.lowKey || z.highKey!=previous.highKey
                    || z.lowVelocity!=previous.lowVelocity || z.highVelocity!=previous.highVelocity
                    || z.alternates!=previous.alternates || z.alternate==previous.alternate
                    || z.flags!=previous.flags || z.group!=previous.group || z.offBy!=previous.offBy))
                    throw std::runtime_error("Ambiguous instrument sample zones");
            }
            out.zones.push_back(z);
        }
        for(const auto& z:out.zones)
        {
            unsigned count=0;
            for(const auto& other:out.zones)if(z.lowKey==other.lowKey && z.highKey==other.highKey
                && z.lowVelocity==other.lowVelocity && z.highVelocity==other.highVelocity)++count;
            if(count!=z.alternates)throw std::runtime_error("Incomplete instrument alternate group");
        }
        reserve(std::size_t(frames)*sizeof(float));out.pcm.resize(frames);
        std::array<unsigned char,8192> buffer{};
        for(std::size_t at=0;at<frames;)
        {
            const auto count=std::min(std::size_t(frames)-at,buffer.size()/2);
            if(!stream.read(reinterpret_cast<char*>(buffer.data()),count*2))throw std::runtime_error("Truncated instrument PCM");
            for(std::size_t i=0;i<count;++i) {const unsigned u=buffer[i*2]+256u*buffer[i*2+1];out.pcm[at++]=(u>=32768?int(u)-65536:int(u))/32768.f;}
        }
    }
}
#endif
