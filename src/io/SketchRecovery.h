#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace materializr {

class Sketch;

// Crash/kill recovery for the in-progress (uncommitted) sketch. While the user
// is drawing, the active sketch lives only in Application::m_activeSketch and is
// added to the document — and therefore the saved project — only on Finish
// Sketch. So an unexpected exit (crash, or the app being killed) loses the whole
// in-progress sketch. To guard against that we periodically write the active
// sketch to a sidecar draft file, independent of the project file, and offer to
// restore it on the next launch. The draft is deleted on any clean exit from
// sketch mode (Finish or discard), so a surviving draft means "last session
// ended mid-sketch".
struct SketchDraftMeta {
    bool        valid = false;
    int         sourceBodyId = -1;   // body the sketch was drawn on (-1 = freestanding)
    std::string projectPath;         // owning project ("" = unsaved/new document)
};

// Absolute path of the draft sidecar (~/.config/materializr/recovery/...).
std::string sketchDraftPath();

// Serialize `sk` + context to the draft file. Best-effort; returns success.
bool writeSketchDraft(const Sketch& sk, int sourceBodyId,
                      const std::string& projectPath);

// True if a draft file currently exists on disk.
bool hasSketchDraft();

// Load the draft into `sk` and `meta`. Returns false if absent/unreadable.
bool readSketchDraft(Sketch& sk, SketchDraftMeta& meta);

// Delete the draft file (clean finish/discard). No-op if none exists.
void clearSketchDraft();

// Which open tab a restored draft belongs in.
//
// The draft records the project it was being drawn in, but the restore used to
// ignore that and simply re-entered sketch mode on whatever tab was active —
// which after project recovery is always tab 0, because that path restores the
// newest snapshot there and switches back to it. A sketch started in an
// untitled tab therefore reappeared grafted on top of an unrelated project
// (Steve, 2026-09-04).
//
// `sessionPaths` is every open session's project path in tab order, `active`
// the tab in front, and `activeIsScratch` whether that tab is an untouched
// empty workspace. Returns the index to restore into, or sessionPaths.size()
// for ""give it its own tab"" — the draft's project isn't open, or it came
// from an untitled tab and the active one is already occupied.
//
// Header-inline and dependency-free so the decision is testable on its own:
// it is the entire bug, and Application itself isn't linkable from the tests.
inline size_t sketchDraftTargetSession(const std::string& draftProjectPath,
                                       const std::vector<std::string>& sessionPaths,
                                       size_t active,
                                       bool activeIsScratch) {
    const size_t newTab = sessionPaths.size();
    if (!draftProjectPath.empty()) {
        // A saved project is unambiguous: restore into the tab holding it.
        for (size_t i = 0; i < sessionPaths.size(); ++i)
            if (sessionPaths[i] == draftProjectPath) return i;
        return newTab;                       // that project isn't open
    }
    // An untitled draft matches EVERY never-saved tab by path, so the path is
    // no help. The one tab it can safely land in is an untouched empty
    // workspace — a fresh launch's single tab, which is what it came from.
    // Anything else already holds work that isn't the draft's.
    if (activeIsScratch && active < sessionPaths.size()) return active;
    return newTab;
}

} // namespace materializr
