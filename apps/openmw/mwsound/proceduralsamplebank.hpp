#ifndef OPENMW_MWSOUND_PROCEDURALSAMPLEBANK_H
#define OPENMW_MWSOUND_PROCEDURALSAMPLEBANK_H

#include "proceduralsample.hpp"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <string_view>

namespace VFS { class Manager; }
namespace MWSound
{
    class ProceduralSampleBank
    {
        struct Job
        {
            std::string path;
            unsigned offset, duration; // milliseconds, also the cache key
            std::shared_ptr<ProceduralSample> data;
        };
        const VFS::Manager* mVfs;
        std::mutex mMutex;
        std::condition_variable mWake;
        std::vector<Job> mCache;
        std::deque<Job> mQueue;
        std::atomic<bool> mStopping{false};
        std::thread mWorker;
        void run();
        void decode(const Job& job);
    public:
        explicit ProceduralSampleBank(const VFS::Manager* vfs);
        ~ProceduralSampleBank();
        static bool valid(std::string_view path, double offset, double duration);
        std::shared_ptr<ProceduralSample> request(std::string path, double offset, double duration);
    };
}
#endif
