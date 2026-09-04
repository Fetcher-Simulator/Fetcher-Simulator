#ifndef OPENMW_SERVER_SERVERCOLLISIONWORLD_HPP
#define OPENMW_SERVER_SERVERCOLLISIONWORLD_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include "ObservationService.hpp"
#include "ServerCollisionLifecycle.hpp"

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

    /// Query-only server collision backend. It owns static world, door, and
    /// heightfield collision only and never creates actors, steps simulation,
    /// or initializes rendering. The benchmark is one consumer of this reusable
    /// cell-lifecycle API.
    class ServerCollisionWorld : public ObservationCollisionBackend
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

        struct DoorReference
        {
            std::string refId;
            std::uint32_t refNum = 0;
            osg::Vec3f position;
            bool baseLocked = false;
            int baseLockLevel = 0;
        };

        explicit ServerCollisionWorld(ServerContentRegistry& content);
        ~ServerCollisionWorld();

        ServerCollisionWorld(const ServerCollisionWorld&) = delete;
        ServerCollisionWorld& operator=(const ServerCollisionWorld&) = delete;

        double load(const std::vector<CellSpec>& cells);
        double clear();
        std::uint64_t acquireCell(const CellSpec& cell);
        bool releaseCell(const CellSpec& cell);
        std::uint64_t cellGeneration(std::string_view cellId) const;
        std::size_t cellRefCount(std::string_view cellId) const;
        std::size_t setDoorOpen(std::string_view cellId, std::string_view refId, std::uint32_t refNum, bool open);
        std::optional<DoorReference> findDoor(
            std::string_view cellId, std::string_view refId, std::uint32_t refNum) const;

        struct RaycastDiagnostic
        {
            bool hit = false;
            float fraction = 1.f;
            osg::Vec3f hitPoint;
            std::string refId;
            std::uint32_t refNum = 0;
            bool heightfield = false;
        };

        bool hasLineOfSight(const osg::Vec3f& from, const osg::Vec3f& to) const;
        RaycastDiagnostic diagnoseLineOfSight(const osg::Vec3f& from, const osg::Vec3f& to) const;
        CollisionObservation lineOfSight(const std::vector<std::string>& cellIds, const ObservationVector& from,
            const ObservationVector& to) const override;

        static std::string cellKey(const CellSpec& cell);

        const Stats& stats() const { return mStats; }
        const std::vector<osg::Vec3f>& actorEyeSamples() const { return mActorEyeSamples; }

    private:
        struct CollisionEntry;
        struct HeightfieldEntry;
        struct CellCollisionState;

        void loadCell(const CellSpec& cell, CellCollisionState& state);
        void unloadCell(CellCollisionState& state);
        void rebuildStats();

        ServerContentRegistry& mContent;
        std::unique_ptr<Resource::BulletShapeManager> mShapeManager;
        std::unique_ptr<btCollisionConfiguration> mCollisionConfiguration;
        std::unique_ptr<btCollisionDispatcher> mDispatcher;
        std::unique_ptr<btBroadphaseInterface> mBroadphase;
        std::unique_ptr<btCollisionWorld> mCollisionWorld;
        ServerCollisionLifecycle mLifecycle;
        std::map<std::string, std::unique_ptr<CellCollisionState>> mCells;
        std::vector<osg::Vec3f> mActorEyeSamples;
        Stats mStats;
    };

    int runServerCollisionBenchmark(ServerContentRegistry& content,
        const std::filesystem::path& outputPath, std::string_view scenarioFilter = {});
}

#endif
