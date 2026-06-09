#pragma once
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <iosfwd>
#include <TopoDS_Shape.hxx>

class Document;
class History;

namespace materializr {
class SketchEditOp;
class Sketch;
}

namespace materializr {

struct ProjectSaveResult {
    bool success = false;
    std::string errorMessage;
};

struct ProjectLoadResult {
    bool success = false;
    std::string errorMessage;
    int bodiesLoaded = 0;
};

// One persisted operation: identity/labels for the History panel plus a body
// diff (changed bodies' resulting shapes + deleted ids) relative to the prior
// step. Replaying these diffs from `initialState` reproduces every step.
struct ProjectHistoryStep {
    std::string typeId, name, description;
    bool enabled = true;
    std::vector<std::pair<int, TopoDS_Shape>> changed; // id -> shape after this step
    std::vector<int> deleted;                          // ids removed at this step
    // Opaque per-op parameter blob (radii, distances, etc.) produced by
    // Operation::serializeParams() and consumed by deserializeParams() on
    // load. Empty for ops that don't override serialisation or for project
    // files that predate the params extension.
    std::string params;
    // Unix-epoch seconds at which the op was originally created. 0 == "not
    // recorded" (legacy projects without timestamps); the loader bumps those
    // to (now - 1 day) so the History panel buckets them under "Yesterday".
    long long timestampUnix = 0;
};

struct ProjectHistory {
    bool present = false;
    std::vector<std::pair<int, TopoDS_Shape>> initialState; // bodies before step 0
    std::vector<ProjectHistoryStep> steps;
};

class ProjectIO {
public:
    // `history` is optional; when provided it is written as a HISTORY section.
    static ProjectSaveResult save(const std::string& filePath, const Document& doc,
                                  const ProjectHistory* history = nullptr);
    // `historyOut` is optional; when provided it receives the parsed HISTORY
    // section (left empty/.present=false if the file has none).
    static ProjectLoadResult load(const std::string& filePath, Document& doc,
                                  ProjectHistory* historyOut = nullptr);

    // Reconstructs a SketchEditOp from the params blob that
    // SketchEditOp::serializeWithDocument produced. The blob carries the
    // before+after sketch snapshots plus the live sketch's id; we use that
    // id to bind m_target via doc.getSketch(). Returns nullptr if the blob
    // is malformed or its sketch id isn't in the document (in which case
    // the caller falls back to a ReplayOp for that step).
    static std::unique_ptr<SketchEditOp> rehydrateSketchEditOp(
        const std::string& paramsBlob, Document& doc);

    // Single-sketch body I/O in the project file's SKETCH_START/END line
    // schema. Exposed so the draft-recovery sidecar (SketchRecovery) round-
    // trips through the exact same format as the project file. writeSketchBody
    // emits PLANE + all element blocks and a trailing SKETCH_END;
    // parseSketchBody reads them back into `sk`, stopping at `endTok`.
    static void writeSketchBody(std::ostream& os, const Sketch& sk);
    static void parseSketchBody(std::istream& is, Sketch& sk,
                                const char* endTok = "SKETCH_END");
};

} // namespace materializr
