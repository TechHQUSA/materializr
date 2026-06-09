#pragma once
#include <string>

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

} // namespace materializr
