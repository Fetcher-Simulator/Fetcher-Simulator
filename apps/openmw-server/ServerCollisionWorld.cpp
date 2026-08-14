#include "ServerCollisionWorld.hpp"

#include "ServerContentRegistry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btScaledBvhTriangleMeshShape.h>

#include <components/bullethelpers/collisionobject.hpp>
#include <components/bullethelpers/heightfield.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>

namespace
{
    using Clock = std::chrono::steady_clock;

    double elapsedMilliseconds(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    std::uint64_t residentSetKiB()
    {
#ifdef __linux__
        std::ifstream status("/proc/self/status");
        std::string key;
        while (status >> key)
        {
            if (key == "VmRSS:")
            {
                std::uint64_t value = 0;
                std::string unit;
                status >> value >> unit;
                return value;
            }
            status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
#endif
        return 0;
    }

    std::size_t triangleCount(const btCollisionShape& shape)
    {
        switch (shape.getShapeType())
        {
            case TRIANGLE_MESH_SHAPE_PROXYTYPE:
            {
                const auto& mesh = static_cast<const btBvhTriangleMeshShape&>(shape);
                const btStridingMeshInterface* interface = mesh.getMeshInterface();
                std::size_t result = 0;
                for (int part = 0; part < interface->getNumSubParts(); ++part)
                {
                    const unsigned char* vertices = nullptr;
                    const unsigned char* indices = nullptr;
                    int vertexCount = 0;
                    int vertexStride = 0;
                    int faceCount = 0;
                    int indexStride = 0;
                    PHY_ScalarType vertexType = PHY_FLOAT;
                    PHY_ScalarType indexType = PHY_INTEGER;
                    interface->getLockedReadOnlyVertexIndexBase(&vertices, vertexCount, vertexType, vertexStride,
                        &indices, indexStride, faceCount, indexType, part);
                    result += static_cast<std::size_t>(std::max(faceCount, 0));
                    interface->unLockReadOnlyVertexBase(part);
                }
                return result;
            }
            case SCALED_TRIANGLE_MESH_SHAPE_PROXYTYPE:
                return triangleCount(*static_cast<const btScaledBvhTriangleMeshShape&>(shape).getChildShape());
            case COMPOUND_SHAPE_PROXYTYPE:
            {
                const auto& compound = static_cast<const btCompoundShape&>(shape);
                std::size_t result = 0;
                for (int i = 0; i < compound.getNumChildShapes(); ++i)
                    result += triangleCount(*compound.getChildShape(i));
                return result;
            }
            default:
                return 0;
        }
    }

    bool isStaticLosBlocker(const MWWorld::ConstPtr& ptr)
    {
        switch (ptr.getType())
        {
            case ESM::REC_ACTI:
            case ESM::REC_CONT:
            case ESM::REC_DOOR:
            case ESM::REC_STAT:
                return true;
            case ESM::REC_LIGH:
                return (ptr.get<ESM::Light>()->mBase->mData.mFlags & ESM::Light::Carry) == 0;
            default:
                return false;
        }
    }

    std::vector<mwmp::ServerCollisionWorld::CellSpec> exteriorSquare(int centerX, int centerY, int radius)
    {
        std::vector<mwmp::ServerCollisionWorld::CellSpec> result;
        for (int y = centerY - radius; y <= centerY + radius; ++y)
            for (int x = centerX - radius; x <= centerX + radius; ++x)
                result.push_back({ {}, x, y, true });
        return result;
    }

    struct QueryMeasurement
    {
        double wallMs = 0;
        double cpuMs = 0;
        double raysPerSecond = 0;
        std::uint64_t visible = 0;
    };

    QueryMeasurement measureQueries(const mwmp::ServerCollisionWorld& world,
        const std::vector<std::pair<osg::Vec3f, osg::Vec3f>>& pairs, std::size_t queries, bool cached)
    {
        QueryMeasurement result;
        if (pairs.empty() || queries == 0)
            return result;

        std::vector<std::int8_t> cache(pairs.size(), -1);
        const std::clock_t cpuStart = std::clock();
        const Clock::time_point wallStart = Clock::now();
        for (std::size_t i = 0; i < queries; ++i)
        {
            const std::size_t index = i % pairs.size();
            bool visible = false;
            if (cached && cache[index] >= 0)
                visible = cache[index] != 0;
            else
            {
                visible = world.hasLineOfSight(pairs[index].first, pairs[index].second);
                if (cached)
                    cache[index] = visible ? 1 : 0;
            }
            result.visible += visible ? 1 : 0;
        }
        result.wallMs = elapsedMilliseconds(wallStart);
        result.cpuMs = 1000.0 * static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
        if (result.wallMs > 0)
            result.raysPerSecond = static_cast<double>(queries) * 1000.0 / result.wallMs;
        return result;
    }

    std::vector<std::pair<osg::Vec3f, osg::Vec3f>> makeActorPairs(
        const std::vector<osg::Vec3f>& input, float alarmRadius)
    {
        std::vector<osg::Vec3f> samples = input;
        if (samples.size() < 2)
        {
            samples = { osg::Vec3f(0.f, 0.f, 128.f), osg::Vec3f(512.f, 0.f, 128.f),
                osg::Vec3f(0.f, 512.f, 128.f), osg::Vec3f(512.f, 512.f, 128.f) };
        }

        std::vector<std::pair<osg::Vec3f, osg::Vec3f>> result;
        constexpr std::size_t MaximumPairs = 4096;
        const float radius2 = alarmRadius * alarmRadius;
        for (std::size_t i = 0; i < samples.size() && result.size() < MaximumPairs; ++i)
        {
            for (std::size_t j = i + 1; j < samples.size() && result.size() < MaximumPairs; ++j)
            {
                const float distance2 = (samples[i] - samples[j]).length2();
                if (distance2 > 1.f && distance2 <= radius2)
                    result.emplace_back(samples[i], samples[j]);
            }
        }
        if (result.empty())
            result.emplace_back(samples[0], samples[1]);
        return result;
    }
}

struct mwmp::ServerCollisionWorld::CollisionEntry
{
    osg::ref_ptr<Resource::BulletShapeInstance> shape;
    std::unique_ptr<btCollisionObject> object;
};

struct mwmp::ServerCollisionWorld::HeightfieldEntry
{
    std::vector<float> sourceHeights;
#if BT_BULLET_VERSION < 310
    std::vector<btScalar> bulletHeights;
#endif
    std::unique_ptr<btHeightfieldTerrainShape> shape;
    std::unique_ptr<btCollisionObject> object;
};

mwmp::ServerCollisionWorld::ServerCollisionWorld(ServerContentRegistry& content)
    : mContent(content)
{
    static std::once_flag registerClasses;
    std::call_once(registerClasses, [] { MWClass::registerClasses(); });

    Resource::ResourceSystem& resources = mContent.resourceSystem();
    mShapeManager = std::make_unique<Resource::BulletShapeManager>(resources.getVFS(),
        resources.getSceneManager(), resources.getNifFileManager(), 0.0);
    resources.addResourceManager(mShapeManager.get());

    mCollisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>();
    mDispatcher = std::make_unique<btCollisionDispatcher>(mCollisionConfiguration.get());
    mBroadphase = std::make_unique<btDbvtBroadphase>();
    mCollisionWorld = std::make_unique<btCollisionWorld>(
        mDispatcher.get(), mBroadphase.get(), mCollisionConfiguration.get());
    mCollisionWorld->setForceUpdateAllAabbs(false);
}

mwmp::ServerCollisionWorld::~ServerCollisionWorld()
{
    clear();
    mContent.resourceSystem().removeResourceManager(mShapeManager.get());
}

double mwmp::ServerCollisionWorld::load(const std::vector<CellSpec>& cells)
{
    const Clock::time_point start = Clock::now();
    for (const CellSpec& cell : cells)
        loadCell(cell);
    return elapsedMilliseconds(start);
}

void mwmp::ServerCollisionWorld::loadCell(const CellSpec& spec)
{
    MWWorld::CellStore* cell = nullptr;
    if (spec.exterior)
        cell = &mContent.worldModel().getExterior(ESM::ExteriorCellLocation(spec.x, spec.y, ESM::RefId{}));
    else
        cell = &mContent.worldModel().getInterior(spec.interior);

    cell->load();
    ++mStats.cells;

    cell->forEachConst([&](const MWWorld::ConstPtr& ptr) {
        if (ptr.getClass().isActor())
        {
            osg::Vec3f eye = ptr.getRefData().getPosition().asVec3();
            eye.z() += 128.f;
            mActorEyeSamples.push_back(eye);
            ++mStats.actorSamples;
            return true;
        }
        if (!isStaticLosBlocker(ptr)
            || Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId()))
            return true;

        VFS::Path::Normalized model = ptr.getClass().getCorrectedModel(ptr);
        if (model.empty())
            return true;
        if (ptr.getClass().useAnim())
            model = Misc::ResourceHelpers::correctActorModelPath(model, mContent.resourceSystem().getVFS());

        try
        {
            osg::ref_ptr<Resource::BulletShapeInstance> shape = mShapeManager->getInstance(model);
            if (!shape || !shape->mCollisionShape)
                return true;

            shape->setLocalScaling(btVector3(ptr.getCellRef().getScale(), ptr.getCellRef().getScale(),
                ptr.getCellRef().getScale()));
            const ESM::Position& position = ptr.getRefData().getPosition();
            const osg::Quat rotation = Misc::Convert::makeOsgQuat(position);

            int group = ptr.getClass().isDoor() ? MWPhysics::CollisionType_Door : MWPhysics::CollisionType_World;
            if (shape->mVisualCollisionType == Resource::VisualCollisionType::Default)
                group = MWPhysics::CollisionType_VisualOnly;
            else if (shape->mVisualCollisionType == Resource::VisualCollisionType::Camera)
                group = MWPhysics::CollisionType_CameraOnly;

            auto entry = std::make_unique<CollisionEntry>();
            entry->shape = std::move(shape);
            entry->object = BulletHelpers::makeCollisionObject(entry->shape->mCollisionShape.get(),
                Misc::Convert::toBullet(position.asVec3()), Misc::Convert::toBullet(rotation));
            mCollisionWorld->addCollisionObject(entry->object.get(), group,
                MWPhysics::CollisionType_Actor | MWPhysics::CollisionType_HeightMap
                    | MWPhysics::CollisionType_Projectile);
            mStats.triangles += triangleCount(*entry->shape->mCollisionShape);
            ++mStats.objects;
            mObjects.push_back(std::move(entry));
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[CollisionBenchmark] Failed blocker ref="
                                << ptr.getCellRef().getRefId() << " model=" << model.value()
                                << " error=" << e.what();
        }
        return true;
    });

    if (!cell->isExterior())
        return;

    const MWWorld::Cell* cellRecord = cell->getCell();
    const ESM::RefId worldspace = cellRecord->getWorldSpace();
    if (ESM::isEsm4Ext(worldspace))
        return;

    const int x = cellRecord->getGridX();
    const int y = cellRecord->getGridY();
    const int verts = ESM::getLandSize(worldspace);
    const int worldSize = ESM::getCellSize(worldspace);
    const ESM::Land* land = mContent.store().get<ESM::Land>().search(x, y);
    const ESM::Land::LandData* data = land ? land->getLandData(ESM::Land::DATA_VHGT) : nullptr;

    auto heightfield = std::make_unique<HeightfieldEntry>();
    float minHeight = ESM::Land::DEFAULT_HEIGHT;
    float maxHeight = ESM::Land::DEFAULT_HEIGHT;
    if (data)
    {
        heightfield->sourceHeights.assign(data->mHeights.begin(), data->mHeights.end());
        minHeight = data->mMinHeight;
        maxHeight = data->mMaxHeight;
    }
    else
        heightfield->sourceHeights.assign(static_cast<std::size_t>(verts * verts), ESM::Land::DEFAULT_HEIGHT);

#if BT_BULLET_VERSION < 310
    heightfield->bulletHeights.assign(
        heightfield->sourceHeights.begin(), heightfield->sourceHeights.end());
    heightfield->shape = std::make_unique<btHeightfieldTerrainShape>(verts, verts,
        heightfield->bulletHeights.data(), 1, minHeight, maxHeight, 2,
        std::is_same_v<btScalar, float> ? PHY_FLOAT : PHY_DOUBLE, false);
#else
    heightfield->shape = std::make_unique<btHeightfieldTerrainShape>(verts, verts,
        heightfield->sourceHeights.data(), minHeight, maxHeight, 2, false);
#endif
    heightfield->shape->setUseDiamondSubdivision(true);
    const float scaling = static_cast<float>(worldSize) / static_cast<float>(verts - 1);
    heightfield->shape->setLocalScaling(btVector3(scaling, scaling, 1));
#if BT_BULLET_VERSION >= 289
    heightfield->shape->buildAccelerator();
#endif
    heightfield->object = std::make_unique<btCollisionObject>();
    heightfield->object->setCollisionShape(heightfield->shape.get());
    heightfield->object->setWorldTransform(btTransform(btQuaternion::getIdentity(),
        BulletHelpers::getHeightfieldShift(x, y, worldSize, minHeight, maxHeight)));
    mCollisionWorld->addCollisionObject(heightfield->object.get(), MWPhysics::CollisionType_HeightMap,
        MWPhysics::CollisionType_Actor | MWPhysics::CollisionType_Projectile);
    mStats.triangles += static_cast<std::size_t>((verts - 1) * (verts - 1) * 2);
    ++mStats.heightfields;
    mHeightfields.push_back(std::move(heightfield));
}

double mwmp::ServerCollisionWorld::clear()
{
    const Clock::time_point start = Clock::now();
    for (const std::unique_ptr<CollisionEntry>& entry : mObjects)
        mCollisionWorld->removeCollisionObject(entry->object.get());
    for (const std::unique_ptr<HeightfieldEntry>& entry : mHeightfields)
        mCollisionWorld->removeCollisionObject(entry->object.get());
    mObjects.clear();
    mHeightfields.clear();
    mActorEyeSamples.clear();
    mStats = {};
    mShapeManager->clearCache();
    return elapsedMilliseconds(start);
}

bool mwmp::ServerCollisionWorld::hasLineOfSight(const osg::Vec3f& from, const osg::Vec3f& to) const
{
    if (from == to)
        return true;
    const btVector3 bulletFrom = Misc::Convert::toBullet(from);
    const btVector3 bulletTo = Misc::Convert::toBullet(to);
    btCollisionWorld::ClosestRayResultCallback callback(bulletFrom, bulletTo);
    callback.m_collisionFilterGroup = MWPhysics::CollisionType_AnyPhysical;
    callback.m_collisionFilterMask = MWPhysics::CollisionType_World
        | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Door;
    mCollisionWorld->rayTest(bulletFrom, bulletTo, callback);
    return !callback.hasHit();
}

int mwmp::runServerCollisionBenchmark(ServerContentRegistry& content,
    const std::filesystem::path& outputPath, std::string_view scenarioFilter)
{
    struct Scenario
    {
        std::string name;
        std::vector<ServerCollisionWorld::CellSpec> cells;
        bool actorScaling = false;
    };

    std::vector<Scenario> scenarios;
    scenarios.push_back({ "interior", { { "Balmora, Guild of Mages", 0, 0, false } } });
    scenarios.push_back({ "exterior_open", { { {}, -2, -9, true } } });
    scenarios.push_back({ "balmora_dense", { { {}, -3, -2, true } } });
    scenarios.push_back({ "balmora_3x3", exteriorSquare(-3, -2, 1), true });
    std::vector<ServerCollisionWorld::CellSpec> disjoint;
    for (const auto [x, y] : std::array<std::pair<int, int>, 4>{ {
             { -3, -2 }, { -2, -9 }, { 3, -10 }, { -3, 7 } } })
    {
        std::vector<ServerCollisionWorld::CellSpec> area = exteriorSquare(x, y, 1);
        disjoint.insert(disjoint.end(), area.begin(), area.end());
    }
    scenarios.push_back({ "disjoint_areas", std::move(disjoint) });

    if (!scenarioFilter.empty() && scenarioFilter != "all"
        && std::none_of(scenarios.begin(), scenarios.end(), [&](const Scenario& value) {
               return value.name == scenarioFilter;
           }))
        throw std::runtime_error("Unknown collision benchmark scenario: " + std::string(scenarioFilter));

    if (!outputPath.parent_path().empty())
        std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream output(outputPath);
    if (!output)
        throw std::runtime_error("Could not open collision benchmark output: " + outputPath.string());
    output << "kind,scenario,cells,objects,heightfields,triangles,actor_samples,queries,wall_ms,cpu_ms,"
              "rays_per_second,rss_kib,rss_delta_kib,load_ms,unload_ms,visible\n";

    const std::uint64_t baselineRss = residentSetKiB();
    output << "baseline,content,0,0,0,0,0,0,0,0,0," << baselineRss << ",0,0,0,0\n";
    std::cout << "COLLISION_BENCHMARK baseline content rss_kib=" << baselineRss << '\n';

    float alarmRadius = 2000.f;
    if (const ESM::GameSetting* setting
        = content.store().get<ESM::GameSetting>().search(ESM::RefId::stringRefId("fAlarmRadius")))
        alarmRadius = setting->mValue.getFloat();

    for (const Scenario& scenario : scenarios)
    {
        if (!scenarioFilter.empty() && scenarioFilter != "all" && scenario.name != scenarioFilter)
            continue;

        ServerCollisionWorld world(content);
        const std::uint64_t beforeRss = residentSetKiB();
        const double loadMs = world.load(scenario.cells);
        const std::uint64_t loadedRss = residentSetKiB();
        const ServerCollisionWorld::Stats stats = world.stats();
        const std::vector<std::pair<osg::Vec3f, osg::Vec3f>> pairs
            = makeActorPairs(world.actorEyeSamples(), alarmRadius);

        output << "load," << scenario.name << ',' << stats.cells << ',' << stats.objects << ','
               << stats.heightfields << ',' << stats.triangles << ',' << stats.actorSamples
               << ",0,0,0,0," << loadedRss << ',' << static_cast<std::int64_t>(loadedRss) - beforeRss
               << ',' << loadMs << ",0,0\n";
        std::cout << "COLLISION_BENCHMARK load scenario=" << scenario.name
                  << " cells=" << stats.cells << " objects=" << stats.objects
                  << " heightfields=" << stats.heightfields << " triangles=" << stats.triangles
                  << " actor_samples=" << stats.actorSamples << " load_ms=" << loadMs
                  << " rss_kib=" << loadedRss
                  << " rss_delta_kib=" << static_cast<std::int64_t>(loadedRss) - beforeRss << '\n';

        for (std::size_t queries : { 1000u, 10000u, 100000u })
        {
            const QueryMeasurement measured = measureQueries(world, pairs, queries, false);
            output << "query," << scenario.name << ',' << stats.cells << ',' << stats.objects << ','
                   << stats.heightfields << ',' << stats.triangles << ',' << stats.actorSamples << ','
                   << queries << ',' << measured.wallMs << ',' << measured.cpuMs << ','
                   << measured.raysPerSecond << ',' << residentSetKiB() << ",0," << loadMs
                   << ",0," << measured.visible << '\n';
            std::cout << "COLLISION_BENCHMARK query scenario=" << scenario.name
                      << " queries=" << queries << " wall_ms=" << measured.wallMs
                      << " cpu_ms=" << measured.cpuMs << " rays_per_second="
                      << measured.raysPerSecond << " visible=" << measured.visible << '\n';
        }

        const QueryMeasurement repeated = measureQueries(world, { pairs.front() }, 100000, false);
        const QueryMeasurement cached = measureQueries(world, pairs, 100000, true);
        for (const auto& [kind, measured] : std::array<std::pair<std::string_view, QueryMeasurement>, 2>{ {
                 { "repeated_pair", repeated }, { "cached_pairs", cached } } })
        {
            output << kind << ',' << scenario.name << ',' << stats.cells << ',' << stats.objects << ','
                   << stats.heightfields << ',' << stats.triangles << ',' << stats.actorSamples
                   << ",100000," << measured.wallMs << ',' << measured.cpuMs << ','
                   << measured.raysPerSecond << ',' << residentSetKiB() << ",0," << loadMs
                   << ",0," << measured.visible << '\n';
            std::cout << "COLLISION_BENCHMARK " << kind << " scenario=" << scenario.name
                      << " queries=100000 wall_ms=" << measured.wallMs
                      << " cpu_ms=" << measured.cpuMs << " rays_per_second="
                      << measured.raysPerSecond << " visible=" << measured.visible << '\n';
        }

        if (scenario.actorScaling)
        {
            for (std::size_t candidates : { 5u, 20u, 50u, 100u })
            {
                std::vector<std::pair<osg::Vec3f, osg::Vec3f>> eventPairs;
                eventPairs.reserve(candidates);
                for (std::size_t i = 0; i < candidates; ++i)
                    eventPairs.push_back(pairs[i % pairs.size()]);
                constexpr std::size_t Events = 1000;
                const std::size_t queries = candidates * Events;
                const QueryMeasurement measured = measureQueries(world, eventPairs, queries, false);
                output << "actor_scale_" << candidates << ',' << scenario.name << ',' << stats.cells << ','
                       << stats.objects << ',' << stats.heightfields << ',' << stats.triangles << ','
                       << stats.actorSamples << ',' << queries << ',' << measured.wallMs << ','
                       << measured.cpuMs << ',' << measured.raysPerSecond << ',' << residentSetKiB()
                       << ",0," << loadMs << ",0," << measured.visible << '\n';
                std::cout << "COLLISION_BENCHMARK actor_scale scenario=" << scenario.name
                          << " candidates=" << candidates << " events=" << Events
                          << " wall_ms=" << measured.wallMs
                          << " per_event_ms=" << measured.wallMs / Events << '\n';
            }
        }

        const double unloadMs = world.clear();
        const std::uint64_t unloadedRss = residentSetKiB();
        output << "unload," << scenario.name << ",0,0,0,0,0,0,0,0,0," << unloadedRss << ','
               << static_cast<std::int64_t>(unloadedRss) - beforeRss << "," << loadMs << ','
               << unloadMs << ",0\n";
        std::cout << "COLLISION_BENCHMARK unload scenario=" << scenario.name
                  << " unload_ms=" << unloadMs << " rss_kib=" << unloadedRss << '\n';

        if (scenario.name == "balmora_dense")
        {
            for (std::size_t transition = 1; transition <= 5; ++transition)
            {
                const std::uint64_t transitionBaselineRss = residentSetKiB();
                const double transitionLoadMs = world.load(scenario.cells);
                const ServerCollisionWorld::Stats transitionStats = world.stats();
                const std::uint64_t transitionLoadedRss = residentSetKiB();
                const double transitionUnloadMs = world.clear();
                output << "transition_" << transition << ',' << scenario.name << ','
                       << transitionStats.cells << ',' << transitionStats.objects << ','
                       << transitionStats.heightfields << ',' << transitionStats.triangles << ','
                       << transitionStats.actorSamples << ",0,0,0,0," << transitionLoadedRss << ','
                       << static_cast<std::int64_t>(transitionLoadedRss) - transitionBaselineRss << ','
                       << transitionLoadMs << ',' << transitionUnloadMs << ",0\n";
                std::cout << "COLLISION_BENCHMARK transition scenario=" << scenario.name
                          << " iteration=" << transition << " load_ms=" << transitionLoadMs
                          << " unload_ms=" << transitionUnloadMs
                          << " rss_kib=" << transitionLoadedRss << '\n';
            }
        }
    }
    return 0;
}
