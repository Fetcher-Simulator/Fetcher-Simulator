#ifndef OPENMW_MWSOUND_PROCEDURALAUDIO_H
#define OPENMW_MWSOUND_PROCEDURALAUDIO_H
#include <cstddef>
namespace MWSound
{
    class ProceduralAudio
    {
    public:
        virtual ~ProceduralAudio() = default;
        virtual void render(float* output, std::size_t frames) = 0;
        virtual void close() = 0;
        virtual bool closed() const = 0;
        virtual void keepAlive() = 0;
        virtual bool expired() const = 0;
        virtual float peak() const = 0;
        virtual unsigned voices() const = 0;
        virtual unsigned units() const { return 1; }
    };
}
#endif
