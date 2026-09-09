#pragma once

#include <cmath>

namespace materializr {

// What a push/pull preview frame asked for. Two frames with the same key
// would produce the same document, so a job for one answers the other.
struct PushPullKey {
    double distance = 0.0;
    bool symmetric = false;
    bool operator==(const PushPullKey& o) const
    {
        return distance == o.distance && symmetric == o.symmetric;
    }
};

// Per-gesture state behind the push/pull preview's three modes. Pure state,
// so the rules are testable:
//
//   inline  the live op runs its boolean in the frame, as it always did;
//   async   once one inline preview took kAsyncPreviewMs or more, the rest
//           of the gesture draws the ghost tool volume every frame and runs
//           the boolean on a worker (PushPullPreview), one job at a time; a
//           finished job whose key no longer matches the arrow is dropped
//           and a new one launched, so the preview trails the arrow instead
//           of freezing on the ghost.
//
// (The heavy path, a ghost with no boolean until commit, lives in
// PushPullState::heavyPreview and is decided before the first frame.)
class PushPullDispatch {
public:
    // One inline preview at or above this switches the gesture to async.
    // Below it a worker round trip would only add a frame of latency.
    static constexpr double kAsyncPreviewMs = 30.0;

    bool async() const { return m_async; }
    bool running() const { return m_running; }

    void inlinePreviewTook(double millis)
    {
        if (millis >= kAsyncPreviewMs) m_async = true;
    }

    // Start a job for `want` now? Only in async mode, with no job running,
    // and not for the key already applied on screen.
    bool shouldLaunch(const PushPullKey& want) const
    {
        if (!m_async || m_running) return false;
        if (m_hasApplied && m_applied == want) return false;
        return true;
    }

    void launched(const PushPullKey& k)
    {
        m_running = true;
        m_launched = k;
    }

    // Nothing could be prepared for `k` (no usable target, a copy OCCT
    // refused): treat it as answered so the next frame does not try again
    // until the arrow moves.
    void refused(const PushPullKey& k)
    {
        m_applied = k;
        m_hasApplied = true;
    }

    // The running job finished. True when its key is what the arrow shows
    // now, so the caller applies it; false means the arrow moved and the
    // result is stale (the caller then launches again at `now`). A current
    // key counts as applied even when the op refused it (a cut that would
    // remove the whole body): asking again at the same distance would only
    // get the same refusal, so nothing is retried until the arrow moves.
    bool finished(const PushPullKey& now)
    {
        m_running = false;
        if (!(m_launched == now)) return false;
        m_applied = m_launched;
        m_hasApplied = true;
        return true;
    }

    void reset() { *this = PushPullDispatch{}; }

private:
    bool m_async = false;
    bool m_running = false;
    bool m_hasApplied = false;
    PushPullKey m_launched;
    PushPullKey m_applied;
};

} // namespace materializr
