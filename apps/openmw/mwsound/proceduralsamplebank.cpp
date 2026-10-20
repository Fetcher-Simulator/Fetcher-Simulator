#include "proceduralsamplebank.hpp"
#include "ffmpegdecoder.hpp"
#include "sounddecoder.hpp"
#include <components/files/hash.hpp>
#include <components/vfs/manager.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace MWSound
{
    namespace
    {
        auto failed(const char* reason)
        {
            auto result = std::make_shared<ProceduralSample>();
            result->error = reason; result->status.store(ProceduralSample::Error, std::memory_order_release);
            return result;
        }
    }
    bool ProceduralSampleBank::valid(std::string_view path, double offset, double duration)
    {
        if (path.empty() || path.size() > 240 || !VFS::Path::isNormalized(path)
            || !(path.starts_with("sound/") || path.starts_with("music/"))
            || path.find("..") != path.npos || path.find(':') != path.npos
            || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; })
            || !std::isfinite(offset) || offset < 0 || offset > 600
            || !std::isfinite(duration) || duration < .01 || duration > 30) return false;
        for (auto extension : {".mp3", ".wav", ".ogg", ".flac", ".opus"})
            if (path.ends_with(extension)) return true;
        return false;
    }
    ProceduralSampleBank::ProceduralSampleBank(const VFS::Manager* vfs)
        : mVfs(vfs), mWorker([this] { run(); }) {}
    ProceduralSampleBank::~ProceduralSampleBank()
    {
        mStopping = true; mWake.notify_one(); mWorker.join();
    }
    std::shared_ptr<ProceduralSample> ProceduralSampleBank::request(std::string path, double offset, double duration)
    {
        if (!valid(path, offset, duration)) return failed("Invalid sample reference");
        const auto start = static_cast<unsigned>(std::lround(offset * 1000));
        const auto length = static_cast<unsigned>(std::lround(duration * 1000));
        std::lock_guard lock(mMutex);
        for (const auto& entry : mCache)
            if (entry.path == path && entry.offset == start && entry.duration == length) return entry.data;
        // At most 32 * 30 * 48000 * 4 = 176 MiB of cached PCM. Control and
        // audio owners pin entries; never evict a buffer still used by a voice.
        if (mCache.size() >= 32)
        {
            auto unused = std::find_if(mCache.begin(), mCache.end(), [](const Job& job) {
                return job.data.use_count() == 1 && job.data->status.load(std::memory_order_acquire) != ProceduralSample::Loading;
            });
            if (unused == mCache.end()) return failed("Sample cache full; release another sample part");
            mCache.erase(unused);
        }
        if (mQueue.size() >= 16) return failed("Sample loader busy; try again");
        Job job{std::move(path), start, length, std::make_shared<ProceduralSample>()};
        mCache.push_back(job); mQueue.push_back(job); mWake.notify_one(); return job.data;
    }
    void ProceduralSampleBank::run()
    {
        while (!mStopping)
        {
            Job job;
            {
                std::unique_lock lock(mMutex);
                mWake.wait(lock, [this] { return mStopping || !mQueue.empty(); });
                if (mStopping) return;
                job = std::move(mQueue.front()); mQueue.pop_front();
            }
            try { decode(job); job.data->status.store(ProceduralSample::Ready, std::memory_order_release); }
            catch (const std::exception& e)
            {
                job.data->pcm.clear(); job.data->pcm.shrink_to_fit(); job.data->error = e.what();
                job.data->status.store(ProceduralSample::Error, std::memory_order_release);
            }
        }
    }
    void ProceduralSampleBank::decode(const Job& job)
    {
        const VFS::Path::Normalized path(job.path);
        auto encoded = mVfs->get(path);
        encoded->seekg(0, std::ios::end);
        const auto bytes = encoded->tellg();
        if (bytes <= 0 || bytes > 64 * 1024 * 1024) throw std::runtime_error("Sample file exceeds 64 MiB limit or is empty");
        encoded->seekg(0);
        const auto hash = Files::getHash(job.path, *encoded);
        std::ostringstream fingerprint;
        fingerprint << std::hex << std::setfill('0') << std::setw(16) << hash[0] << std::setw(16) << hash[1];
        job.data->hash = fingerprint.str(); encoded.reset();
        std::shared_ptr<SoundDecoder> decoder = std::make_shared<FFmpegDecoder>(mVfs);
        decoder->open(path);
        int rate; ChannelConfig channels; SampleType type;
        decoder->getInfo(&rate, &channels, &type);
        if (rate < 8000 || rate > 192000) throw std::runtime_error("Unsupported sample rate");
        const auto frameBytes = framesToBytes(1, channels, type);
        const unsigned unit = type == SampleType_UInt8 ? 1 : type == SampleType_Int16 ? 2 : 4;
        const auto channelCount = frameBytes / unit;
        if (!channelCount || channelCount > 8) throw std::runtime_error("Unsupported channel count");
        std::vector<char> buffer(4096 * frameBytes);
        std::vector<float> source;
        const std::size_t first = static_cast<std::size_t>(rate) * job.offset / 1000;
        const std::size_t wanted = static_cast<std::size_t>(rate) * job.duration / 1000;
        source.reserve(wanted);
        std::size_t position = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (source.size() < wanted)
        {
            if (mStopping || std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error("Sample decode cancelled or exceeded ten seconds");
            const auto read = decoder->read(buffer.data(), buffer.size());
            if (!read) break;
            const auto count = read / frameBytes;
            if (!count) break;
            for (std::size_t i = 0; i < count && source.size() < wanted; ++i, ++position)
            {
                if (position < first) continue;
                double sum = 0;
                for (std::size_t c = 0; c < channelCount; ++c)
                {
                    const auto* address = buffer.data() + i * frameBytes + c * unit;
                    double value;
                    if (type == SampleType_UInt8) value = (static_cast<unsigned char>(*address) - 128) / 128.0;
                    else if (type == SampleType_Int16) { std::int16_t n; std::memcpy(&n, address, 2); value = n / 32768.0; }
                    else { float n; std::memcpy(&n, address, 4); value = std::isfinite(n) ? std::clamp(n, -1.f, 1.f) : 0; }
                    sum += value;
                }
                source.push_back(static_cast<float>(sum / channelCount));
            }
        }
        if (source.size() < 2) throw std::runtime_error("No audio at this trim position");
        auto& pcm = job.data->pcm;
        pcm.resize(std::min(std::size_t(1440000), source.size() * 48000 / rate));
        for (std::size_t i = 0; i < pcm.size(); ++i)
        {
            const double at = static_cast<double>(i) * rate / 48000;
            const auto a = static_cast<std::size_t>(at);
            const auto b = std::min(a + 1, source.size() - 1);
            pcm[i] = static_cast<float>(source[a] + (source[b] - source[a]) * (at - a));
        }
    }
}
