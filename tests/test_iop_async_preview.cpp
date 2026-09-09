// The SnapshotBody engine's off-thread preview (previewOffThread): inline
// until one frame is slow, then one worker job at a time whose result lands
// through pollPreview; the body trails the parameters, a mid-job change is
// re-asked, cancel restores the snapshot, and commit records the op even
// while a job is in flight.
#include "app/DeferredChain.h"
#include "app/InteractiveOpController.h"
#include "core/Document.h"
#include "core/History.h"
#include "core/Operation.h"
#include "core/SelectionManager.h"

#include <gtest/gtest.h>

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace materializr;

namespace {

double volume(const TopoDS_Shape& s) {
    GProp_GProps g;
    BRepGProp::VolumeProperties(s, g);
    return g.Mass();
}

// Replaces the body with a 20x20xH box after a deliberately slow pause, so
// the very first inline frame trips the async threshold. H <= 0 is refused.
class SlowHeightOp : public Operation {
public:
    SlowHeightOp(int id, double h) : m_id(id), m_h(h) {}
    bool execute(Document& doc) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        if (m_h <= 0.0) return false;
        m_prev = doc.getBody(m_id);
        doc.updateBody(m_id, BRepPrimAPI_MakeBox(20.0, 20.0, m_h).Shape());
        return true;
    }
    bool undo(Document& doc) override { doc.updateBody(m_id, m_prev); return true; }
    std::string name() const override { return "SlowHeight"; }
    std::string description() const override { return name(); }
    void renderProperties() override {}
    std::string typeId() const override { return "slow_height"; }
    std::string serializeParams() const override { return "h=" + std::to_string(m_h); }
    std::vector<int> plannedBodyIds() const override { return {m_id}; }
private:
    int m_id;
    double m_h;
    TopoDS_Shape m_prev;
};

class AsyncController : public InteractiveOpController {
public:
    int target = -1;
    double height = 10.0;   // 0 = nothing to preview (buildOp gives null)
protected:
    const char* title() const override { return "Async"; }
    int onBegin(const IopContext&) override { return target; }
    std::unique_ptr<Operation> buildOp(const IopContext&) override {
        if (height == 0.0) return nullptr;
        return std::make_unique<SlowHeightOp>(target, height);
    }
    void panelBody(const IopContext&, bool&) override {}
    bool previewOffThread() const override { return true; }
};

// The LiveOp shape (Push/Pull, Extrude): the preview is an applied instance,
// and the commit records a DIFFERENT op built at the final values.
class LiveController : public InteractiveOpController {
public:
    int target = -1;
    double height = 12.0;     // what the preview applied
    double commitHeight = 15.0;
    bool wentAsync = true;
    using InteractiveOpController::livePreviewApplied;
protected:
    PreviewModel previewModel() const override { return PreviewModel::LiveOp; }
    bool previewWentAsync() const override { return wentAsync; }
    const char* title() const override { return "Live"; }
    int onBegin(const IopContext&) override { return target; }
    std::unique_ptr<Operation> buildOp(const IopContext&) override {
        return std::make_unique<SlowHeightOp>(target, height);
    }
    std::unique_ptr<Operation> buildCommitOp(const IopContext&) override {
        return std::make_unique<SlowHeightOp>(target, commitHeight);
    }
    void panelBody(const IopContext&, bool&) override {}
};

struct Rig {
    Document doc;
    History history;
    SelectionManager selection;
    int id;
    int bodyDirtyMarks = 0;
    AsyncController ctl;
    Rig() : id(doc.addBody(BRepPrimAPI_MakeBox(20.0, 20.0, 10.0).Shape(), "b")) { ctl.target = id; }
    IopContext ctx() {
        IopContext c{doc, history, selection};
        c.markBodyDirty = [this](int) { ++bodyDirtyMarks; };
        return c;
    }
    // Poll like the frame loop does until no job is pending (or 5 s).
    bool settle() {
        for (int i = 0; i < 5000; ++i) {
            ctl.pollPreview(ctx());
            if (!ctl.previewPending()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    double bodyVolume() { return volume(doc.getBody(id)); }
};

} // namespace

TEST(IopAsyncPreview, FirstFrameIsInlineThenTheGestureGoesAsync) {
    Rig r;
    ASSERT_TRUE(r.ctl.begin(r.ctx()));
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6); // the inline frame landed
    EXPECT_TRUE(r.ctl.previewOk());
    EXPECT_FALSE(r.ctl.previewPending());
    r.ctl.update(r.ctx()); // same value: the slow frame's result is already on screen
    EXPECT_FALSE(r.ctl.previewPending());

    r.ctl.height = 15.0;
    r.ctl.update(r.ctx());
    EXPECT_TRUE(r.ctl.previewPending());                     // a worker job, not an inline execute
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6);   // the body trails: last landed preview stays
    ASSERT_TRUE(r.settle());
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 15.0, 1e-6);
    EXPECT_TRUE(r.ctl.previewOk());
    EXPECT_GT(r.bodyDirtyMarks, 0);

    // The same parameters again launch nothing.
    r.ctl.update(r.ctx());
    EXPECT_FALSE(r.ctl.previewPending());
}

TEST(IopAsyncPreview, AChangeMidJobIsAskedAgainAndTheFinalValueLands) {
    Rig r;
    ASSERT_TRUE(r.ctl.begin(r.ctx()));
    r.ctl.height = 15.0;
    r.ctl.update(r.ctx());
    r.ctl.height = 25.0;
    r.ctl.update(r.ctx()); // one job at a time: this waits for the first to finish
    ASSERT_TRUE(r.settle());
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 25.0, 1e-6);
    EXPECT_TRUE(r.ctl.previewOk());
}

TEST(IopAsyncPreview, ARefusedValueShowsTheSnapshot) {
    Rig r;
    ASSERT_TRUE(r.ctl.begin(r.ctx()));
    r.ctl.height = 15.0;
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.settle());
    // Nothing to preview at all: the snapshot shows at once, no job.
    r.ctl.height = 0.0;
    r.ctl.update(r.ctx());
    EXPECT_FALSE(r.ctl.previewPending());
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6);
    EXPECT_FALSE(r.ctl.previewOk());
    r.ctl.height = 15.0;
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.settle());
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 15.0, 1e-6); // the retracted key is asked again
    r.ctl.height = -1.0; // execute() refuses
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.settle());
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6);
    EXPECT_FALSE(r.ctl.previewOk());
}

TEST(IopAsyncPreview, CancelMidJobRestoresTheSnapshotAndAbandonsTheJob) {
    Rig r;
    ASSERT_TRUE(r.ctl.begin(r.ctx()));
    r.ctl.height = 15.0;
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.ctl.previewPending());
    r.ctl.cancel(r.ctx());
    EXPECT_FALSE(r.ctl.active());
    EXPECT_FALSE(r.ctl.previewPending());
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6);
    // The abandoned job finishes on its own and must not land.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    r.ctl.pollPreview(r.ctx());
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6);
    EXPECT_EQ(r.history.operations().size(), 0u);
}

TEST(IopAsyncPreview, CommitMidJobRecordsTheCurrentParameters) {
    Rig r;
    ASSERT_TRUE(r.ctl.begin(r.ctx()));
    r.ctl.height = 15.0;
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.settle());
    r.ctl.height = 30.0;
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.ctl.previewPending());  // previewOk still describes h=15
    r.ctl.commit(r.ctx());                // no deferHeavy in this ctx: pushed inline
    EXPECT_FALSE(r.ctl.active());
    ASSERT_EQ(r.history.operations().size(), 1u);
    EXPECT_EQ(r.history.operations()[0]->serializeParams(), "h=30.000000");
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 30.0, 1e-6);
}

TEST(IopAsyncPreview, CommitWithOnlyARefusedPreviewLandedStillRecordsTheOp) {
    Rig r;
    ASSERT_TRUE(r.ctl.begin(r.ctx()));
    r.ctl.height = -1.0; // refused: previewOk goes false
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.settle());
    ASSERT_FALSE(r.ctl.previewOk());
    r.ctl.height = 12.0; // a valid value, its job still in flight
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.ctl.previewPending());
    r.ctl.commit(r.ctx());
    // previewOk described the refused frame, not these parameters: the commit
    // must still run the op (pushOperation is the judge, not the stale flag).
    ASSERT_EQ(r.history.operations().size(), 1u);
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 12.0, 1e-6);
    EXPECT_FALSE(r.ctl.active());
}

TEST(IopAsyncPreview, CommitAfterAnAsyncGestureIsDeferredWhenTheAppCan) {
    Rig r;
    ASSERT_TRUE(r.ctl.begin(r.ctx()));
    r.ctl.height = 15.0;
    r.ctl.update(r.ctx());
    ASSERT_TRUE(r.settle());
    std::function<void()> deferred;
    IopContext c = r.ctx();
    c.progress = [](float, const char*) { return false; };
    c.deferHeavy = [&](std::function<void()> fn) { deferred = std::move(fn); };
    r.ctl.commit(c);
    ASSERT_TRUE(deferred) << "a gesture that went async proved the op slow: commit behind the progress window";
    EXPECT_EQ(r.history.operations().size(), 0u);
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6); // snapshot until the deferred task runs
    deferred();
    ASSERT_EQ(r.history.operations().size(), 1u);
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 15.0, 1e-6);
}

namespace {
// A context with the app's deferral wired the way Application does it.
struct DeferRig {
    Rig rig;
    std::function<void()> slot;
    IopContext ctx() {
        IopContext c = rig.ctx();
        c.progress = [](float, const char*) { return false; };
        c.deferHeavy = [this](std::function<void()> fn) {
            materializr::chainDeferred(slot, std::move(fn));
        };
        return c;
    }
};
} // namespace

TEST(IopLiveOpCommit, AnAsyncGestureCommitsBehindTheProgressWindow) {
    DeferRig r;
    LiveController ctl;
    ctl.target = r.rig.id;
    ASSERT_TRUE(ctl.begin(r.ctx()));
    EXPECT_NEAR(r.rig.bodyVolume(), 20.0 * 20.0 * 12.0, 1e-6); // preview applied

    ctl.commit(r.ctx());
    EXPECT_FALSE(ctl.active());
    ASSERT_TRUE(r.slot) << "the one real execute of a Push/Pull-shaped gesture "
                           "must not freeze the frame that confirmed it";
    EXPECT_EQ(r.rig.history.operations().size(), 0u);
    EXPECT_NEAR(r.rig.bodyVolume(), 20.0 * 20.0 * 10.0, 1e-6); // preview undone

    r.slot();
    ASSERT_EQ(r.rig.history.operations().size(), 1u);
    EXPECT_EQ(r.rig.history.operations()[0]->serializeParams(), "h=15.000000");
    EXPECT_NEAR(r.rig.bodyVolume(), 20.0 * 20.0 * 15.0, 1e-6);
}

TEST(IopLiveOpCommit, AnInlineGestureStillCommitsInTheFrame) {
    DeferRig r;
    LiveController ctl;
    ctl.target = r.rig.id;
    ctl.wentAsync = false;   // small bodies previewed inline: nothing to defer
    ASSERT_TRUE(ctl.begin(r.ctx()));
    ctl.commit(r.ctx());
    EXPECT_FALSE(r.slot);
    ASSERT_EQ(r.rig.history.operations().size(), 1u);
    EXPECT_NEAR(r.rig.bodyVolume(), 20.0 * 20.0 * 15.0, 1e-6);
}

TEST(IopLiveOpCommit, WithoutADeferralSlotTheCommitRunsInline) {
    // Headless embeddings (and the tests above) hand the controller no
    // progress reporter: deferCommit must decline and leave the op to run.
    Rig r;
    LiveController ctl;
    ctl.target = r.id;
    ASSERT_TRUE(ctl.begin(r.ctx()));
    ctl.commit(r.ctx());
    ASSERT_EQ(r.history.operations().size(), 1u);
    EXPECT_NEAR(r.bodyVolume(), 20.0 * 20.0 * 15.0, 1e-6);
}

TEST(DeferredChain, QueuesBehindAPendingTaskInOrder) {
    std::function<void()> slot;
    std::string log;
    materializr::chainDeferred(slot, [&] { log += "a"; });
    materializr::chainDeferred(slot, [&] { log += "b"; });
    materializr::chainDeferred(slot, [&] { log += "c"; });
    ASSERT_TRUE(slot);
    slot();
    EXPECT_EQ(log, "abc") << "a confirmed operation waiting in the slot must "
                             "never be dropped by the next one";
}

TEST(DeferredChain, AnEmptySlotTakesTheTaskAndANullTaskIsIgnored) {
    std::function<void()> slot;
    materializr::chainDeferred(slot, {});
    EXPECT_FALSE(slot);
    int ran = 0;
    materializr::chainDeferred(slot, [&] { ++ran; });
    ASSERT_TRUE(slot);
    materializr::chainDeferred(slot, {});
    slot();
    EXPECT_EQ(ran, 1);
}
