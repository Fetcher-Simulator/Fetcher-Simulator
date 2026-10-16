#include <apps/openmw/mwrender/scriptbonecontroller.hpp>
#include <iostream>
#include <limits>
#include <osg/FrameStamp>
#include <osgUtil/UpdateVisitor>
#include <stdexcept>
#include <thread>

void require(bool test, const char* message)
{
    if (!test)
        throw std::runtime_error(message);
}
bool near(const osg::Vec3f& a, const osg::Vec3f& b)
{
    return (a - b).length() < 0.001f;
}

class PartialAnimation : public SceneUtil::NodeCallback<PartialAnimation, osg::MatrixTransform*>
{
public:
    void operator()(osg::MatrixTransform* node, osg::NodeVisitor* nv)
    {
        // A keyed translation with an unkeyed rotation must not feed back.
        auto m = node->getMatrix();
        m.setTrans(10, 20, 30);
        node->setMatrix(m);
        traverse(node, nv);
    }
};

void run()
{
    using MWRender::ScriptBoneController;
    const osg::Quat turn(0.3, osg::Vec3f(0, 0, 1));
    require(ScriptBoneController::valid({}, turn, 1, 0.5), "valid pose rejected");
    require(!ScriptBoneController::valid({ 1001, 0, 0 }, turn, 1, 0.5), "unbounded offset accepted");
    require(
        !ScriptBoneController::valid({}, turn, 1, std::numeric_limits<float>::infinity()), "infinite lease accepted");
    require(!ScriptBoneController::valid({}, osg::Quat(0, 0, 0, 0), 1, 0.5), "zero quaternion accepted");
    require(!ScriptBoneController::valid({}, turn, 2, 0.5), "invalid weight accepted");
    osg::ref_ptr<osg::MatrixTransform> node = new osg::MatrixTransform;
    node->setMatrix(osg::Matrixf::scale(2, 2, 2));
    osg::ref_ptr<ScriptBoneController> apply = new ScriptBoneController;
    osg::ref_ptr<MWRender::RestoreScriptBoneController> reset = new MWRender::RestoreScriptBoneController(apply);
    node->addUpdateCallback(reset);
    node->addUpdateCallback(new PartialAnimation);
    node->addUpdateCallback(apply);
    osgUtil::UpdateVisitor visitor;
    osg::ref_ptr<osg::FrameStamp> stamp = new osg::FrameStamp;
    visitor.setFrameStamp(stamp);
    apply->set({ 2, 4, 6 }, turn, 0.5, 0.5);
    for (unsigned i = 0; i < 200; ++i)
    {
        stamp->setFrameNumber(i);
        node->accept(visitor);
        require(near(node->getMatrix().getTrans(), { 11, 22, 33 }), "position accumulated");
        require(near(osg::Vec3f(1, 0, 0) * node->getMatrix() - node->getMatrix().getTrans(),
                    { 2 * std::cos(0.15f), 2 * std::sin(0.15f), 0 }),
            "rotation accumulated");
        require(near(node->getMatrix().getScale(), { 2, 2, 2 }), "scale changed");
        require(near(apply->baseMatrix(node).getTrans(), { 10, 20, 30 }), "base transform contaminated");
    }
    apply->set({}, turn, 1, 0.05);
    node->accept(visitor);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    node->accept(visitor);
    require(near(node->getMatrix().getTrans(), { 10, 20, 30 }), "expiry failed to restore position");
    require(near(osg::Vec3f(1, 0, 0) * node->getMatrix() - node->getMatrix().getTrans(), { 2, 0, 0 }),
        "expiry failed to restore rotation");
    // Unkeyed transforms also restore on every update.
    osg::ref_ptr<osgAnimation::Bone> parent = new osgAnimation::Bone;
    parent->setMatrixInSkeletonSpace(osg::Matrixf::translate(0, 10, 0));
    osg::ref_ptr<osgAnimation::Bone> child = new osgAnimation::Bone;
    parent->addChild(child);
    child->setMatrix(osg::Matrixf::translate(1, 2, 3));
    apply = new ScriptBoneController;
    child->addUpdateCallback(new MWRender::RestoreScriptBoneController(apply));
    child->addUpdateCallback(apply);
    apply->set({ 0, 0, 2 }, {}, 1, 0.5);
    for (int i = 0; i < 50; ++i)
        child->accept(visitor);
    require(near(child->getMatrix().getTrans(), { 1, 2, 5 }), "unkeyed transform accumulated");
    require(near(child->getMatrixInSkeletonSpace().getTrans(), { 1, 12, 5 }), "skeleton space stale");
    apply->restore(child);
    require(near(child->getMatrix().getTrans(), { 1, 2, 3 }), "explicit reset failed");
    std::cout << "PASS bone offsets, partial/unkeyed animation, weighting, scale, expiry, reset, skeleton space\n";
}

int main()
{
    try
    {
        run();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
