#ifndef OPENMW_MWSOUND_PROCEDURALAUDIO_H
#define OPENMW_MWSOUND_PROCEDURALAUDIO_H
#include <atomic>
#include <cstddef>
namespace MWSound
{
    class ProceduralAudio
    {
        std::atomic<bool> mMonitorEligible{ false };
        std::atomic<float> mObstruction{ 0 };

    public:
        void monitorEligible(bool value) { mMonitorEligible = value; }
        bool monitorEligible() const { return mMonitorEligible; }
        bool obstruction(float value)
        {
            if (!(value >= 0 && value <= 1))
                return false;
            mObstruction = value;
            return true;
        }
        float obstruction() const { return mObstruction; }
        // Two complete 32-part keyboards plus routed outputs and local monitors.
        static constexpr unsigned MaxUnits = 128;
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
