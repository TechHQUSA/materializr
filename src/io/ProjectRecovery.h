#pragma once
#include <string>

class Document;

namespace materializr {

struct ProjectHistory; // ProjectIO.h

// Crash/hang recovery for the WHOLE project. SketchRecovery only guards the
// in-progress (uncommitted) sketch; this guards the committed model — bodies
// and the full operation history — including an UNSAVED project that has no
// .materializr path yet (the case that loses the most work). The active project
// is periodically snapshotted to a sidecar, independent of the user's own save
// file, so a crash or a hang never costs more than a few seconds of committed
// work. The snapshot is deleted on a clean exit, so one surviving to the next
// launch means "the last session ended unexpectedly with unsaved work" → offer
// to restore it.
struct ProjectRecoveryMeta {
    bool        valid = false;
    std::string projectPath;  // the project's own save path ("" = never saved)
    long long   savedAtUnix = 0;
    int         bodyCount = 0;
    int         stepCount = 0;
};

// Absolute path of the recovery snapshot (~/.config/materializr/recovery/...).
std::string projectRecoveryPath();

// Snapshot the document (+ optional history) to the recovery sidecar, written to
// a temp file then atomically renamed so a crash mid-write never leaves a
// truncated snapshot. `projectPath` is the project's own save path ("" if
// unsaved). Best-effort; returns success.
bool writeProjectRecovery(const Document& doc, const ProjectHistory* history,
                          const std::string& projectPath, int bodyCount,
                          int stepCount);

// True if a recovery snapshot currently exists on disk.
bool hasProjectRecovery();

// Read just the sidecar metadata (for the restore prompt). The geometry itself
// is restored by loading projectRecoveryPath() through the normal loader.
bool readProjectRecoveryMeta(ProjectRecoveryMeta& meta);

// Delete the recovery snapshot + its meta (clean exit / user discard).
void clearProjectRecovery();

} // namespace materializr
