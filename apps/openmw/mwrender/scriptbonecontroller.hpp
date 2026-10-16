#ifndef OPENMW_MWRENDER_SCRIPTBONECONTROLLER_H
#define OPENMW_MWRENDER_SCRIPTBONECONTROLLER_H

#include <components/sceneutil/nodecallback.hpp>

#include <osg/MatrixTransform>
#include <osg/Camera>
#include <osgAnimation/Bone>

#include <chrono>
#include <cmath>

namespace MWRender
{
    // Match osg::computeLocalToWorld's world-space convention: the viewer's
    // absolute camera (and its ancestors) is not part of an object's world pose.
    // A custom resolver lets IK omit scripted offsets without touching the graph.
    template <class MatrixResolver>
    osg::Matrixf computeBoneWorldTransform(const osg::NodePath& path, MatrixResolver resolve)
    {
        std::size_t first = 0;
        for (std::size_t i = path.size(); i > 0; --i)
        {
            const auto* camera = path[i - 1]->asCamera();
            if (camera && (camera->getReferenceFrame() != osg::Transform::RELATIVE_RF || camera->getParents().empty()))
            {
                first = i;
                break;
            }
        }
        osg::Matrix result;
        for (std::size_t i = first; i < path.size(); ++i)
        {
            if (const auto* node = dynamic_cast<const osg::MatrixTransform*>(path[i]))
            {
                if (node->getReferenceFrame() == osg::Transform::RELATIVE_RF)
                    result.preMult(resolve(node));
                else
                    result = resolve(node);
            }
            else if (const auto* transform = dynamic_cast<const osg::Transform*>(path[i]))
                transform->computeLocalToWorldMatrix(result, nullptr);
        }
        return osg::Matrixf(result);
    }

    // One restore callback before the ordinary animation chain and one overlay
    // after it. Restoring first also handles unkeyed/partially keyed transforms.
    class ScriptBoneController : public SceneUtil::NodeCallback<ScriptBoneController, osg::MatrixTransform*>
    {
    public:
        using Clock = std::chrono::steady_clock;

        static bool valid(const osg::Vec3f& position, const osg::Quat& rotation, float weight, float timeout)
        {
            for (unsigned i = 0; i < 3; ++i)
                if (!std::isfinite(position[i]) || std::abs(position[i]) > 1000.f)
                    return false;
            for (unsigned i = 0; i < 4; ++i)
                if (!std::isfinite(rotation[i]))
                    return false;
            return std::isfinite(rotation.length2()) && rotation.length2() > 1e-12 && std::isfinite(weight)
                && weight >= 0.f && weight <= 1.f && std::isfinite(timeout) && timeout >= 0.05f && timeout <= 2.f;
        }

        void set(const osg::Vec3f& position, osg::Quat rotation, float weight, float timeout)
        {
            rotation /= rotation.length();
            mPosition = position * weight;
            mRotation.slerp(weight, osg::Quat(), rotation);
            mDeadline
                = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(timeout));
        }

        bool expired() const { return Clock::now() >= mDeadline; }
        osg::Matrixf baseMatrix(const osg::MatrixTransform* node) const
        {
            return mApplied ? mBase : osg::Matrixf(node->getMatrix());
        }

        static void setMatrix(osg::MatrixTransform* node, const osg::Matrixf& matrix)
        {
            // Do not change NifOsg's stored keyframe components: this is a visual
            // overlay and must never feed back into the underlying animation.
            node->setMatrix(matrix);
            if (auto* bone = dynamic_cast<osgAnimation::Bone*>(node))
            {
                osg::Matrixf skeleton = matrix;
                if (auto* parent = bone->getBoneParent())
                    skeleton *= parent->getMatrixInSkeletonSpace();
                bone->setMatrixInSkeletonSpace(skeleton);
            }
        }

        void restore(osg::MatrixTransform* node)
        {
            if (mApplied)
            {
                setMatrix(node, mBase);
                mApplied = false;
            }
        }

        void operator()(osg::MatrixTransform* node, osg::NodeVisitor* visitor)
        {
            if (!expired())
            {
                mBase = node->getMatrix();
                osg::Matrixf posed = osg::Matrixf::rotate(mRotation) * mBase;
                posed.setTrans(mBase.getTrans() + mPosition);
                setMatrix(node, posed);
                mApplied = true;
            }
            traverse(node, visitor);
        }

    private:
        osg::Vec3f mPosition;
        osg::Quat mRotation;
        osg::Matrixf mBase;
        Clock::time_point mDeadline{};
        bool mApplied = false;
    };

    class RestoreScriptBoneController
        : public SceneUtil::NodeCallback<RestoreScriptBoneController, osg::MatrixTransform*>
    {
    public:
        explicit RestoreScriptBoneController(ScriptBoneController* controller)
            : mController(controller)
        {
        }
        void operator()(osg::MatrixTransform* node, osg::NodeVisitor* visitor)
        {
            mController->restore(node);
            traverse(node, visitor);
        }

    private:
        osg::ref_ptr<ScriptBoneController> mController;
    };
}

#endif
