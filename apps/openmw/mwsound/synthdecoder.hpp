#ifndef OPENMW_MWSOUND_SYNTHDECODER_H
#define OPENMW_MWSOUND_SYNTHDECODER_H

#include "proceduralsynth.hpp"
#include "sounddecoder.hpp"
#include <algorithm>
#include <cstring>
#include <memory>

namespace MWSound
{
    class SynthDecoder final : public SoundDecoder
    {
        std::shared_ptr<ProceduralSynth> mSynth;
        std::size_t mOffset = 0;

    public:
        explicit SynthDecoder(std::shared_ptr<ProceduralSynth> synth)
            : SoundDecoder(nullptr)
            , mSynth(std::move(synth))
        {
        }
        void open(VFS::Path::NormalizedView) override {}
        void close() override { mSynth->close(); }
        bool isInteractive() const override { return true; }
        std::string getName() override { return "procedural synthesizer"; }
        void getInfo(int* rate, ChannelConfig* channels, SampleType* type) override
        {
            *rate = ProceduralSynth::SampleRate;
            *channels = ChannelConfig_Mono;
            *type = SampleType_Int16;
        }
        std::size_t getSampleOffset() override { return mOffset; }
        std::size_t read(char* buffer, std::size_t bytes) override
        {
            if (mSynth->closed() || mSynth->expired())
                return 0;
            std::array<float, 480> samples;
            std::size_t frames = bytes / sizeof(short), written = 0;
            while (written < frames)
            {
                const std::size_t count = std::min(samples.size(), frames - written);
                mSynth->render(samples.data(), count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const short pcm = static_cast<short>(samples[i] * 32767);
                    std::memcpy(buffer + (written + i) * sizeof(pcm), &pcm, sizeof(pcm));
                }
                written += count;
            }
            mOffset += frames;
            return frames * sizeof(short);
        }
    };
}
#endif
