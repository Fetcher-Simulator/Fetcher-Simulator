#ifndef OPENMW_SERVER_SERVERCOLLISIONWORLD_HPP
#define OPENMW_SERVER_SERVERCOLLISIONWORLD_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

class btBroadphaseInterface;
class btCollisionConfiguration;
class btCollisionDispatcher;
class btCollisionWorld;

namespace Resource
{
    class BulletShapeManager;
}

namespace mwmp
{
    class ServerContentRegistry;

    /// Query-only collision backend used to measure dedicated-server LOS
    /// feasibility. It owns static world/door/heightfield collision only and
    /// never creates actors, steps simulation, or initializes rendering.
    class ServerCollisionWorld
    {
    public:
        struct CellSpec
        {
            std::string interior;
            int x = 0;
            int y = 0;
            bool exterior = false;
        };

        struct Stats
        {
            std::size_t cells = 0;
            std::size_t objects = 0;
            std::size_t heightfields = 0;
            std::size_t triangles = 0;
            std::size_t actorSamples = 0;
        };

        explicit ServerCollisionWorld(ServerContentRegistry& content);
        ~ServerCollisionWorld();

        ServerCollisionWorld(const ServerCollisionWorld&) = delete;
        ServerCollisionWorld& operator=(const ServerCollisionWorld&) = delete;

        double load(const std::vector<CellSpec>& cells);
        double clear();
        bool hasLineOfSight(const osg::Vec3f& from, const osg::Vec3f& to) const;

        const Stats& stats() const { return mStats; }
        const std::vector<osg::Vec3f>& actorEyeSamples() const { return mActorEyeSamples; }

    private:
        struct CollisionEntry;
        struct HeightfieldEntry;

        void loadCell(const CellSpec& cell);

        ServerContentRegistry& mContent;
        std::unique_ptr<Resource::BulletShapeManager> mShapeManager;
        std::unique_ptr<btCollisionConfiguration> mCollisionConfiguration;
        std::unique_ptr<btCollisionDispatcher> mDispatcher;
        std::unique_ptr<btBroadphaseInterface> mBroadphase;
        std::unique_ptr<btCollisionWorld> mCollisionWorld;
        std::vector<std::unique_ptr<CollisionEntry>> mObjects;
        std::vector<std::unique_ptr<HeightfieldEntry>> mHeightfields;
        std::vector<osg::Vec3f> mActorEyeSamples;
        Stats mStats;
    };

    int runServerCollisionBenchmark(ServerContentRegistry& content,
        const std::filesystem::path& outputPath, std::string_view scenarioFilter = {});
}

#endif
