#pragma once
#include <string>

namespace materializr {

struct SelectionChangedEvent {};

struct DocumentModifiedEvent {
    bool meshDirty = true;
};

struct ToolActivatedEvent {
    std::string toolName;
};

struct ToolDeactivatedEvent {
    std::string toolName;
};

struct SketchModeEnteredEvent {
    int sketchId = -1;
};

struct SketchModeExitedEvent {};

struct HistoryStepEvent {
    int stepIndex = -1;
    bool isUndo = false;
};

// Fired when the user commits a constraint/value edit on a known sketch from
// outside sketch-draw mode (Properties → Constraints panel, History → Apply
// Changes on a sketchedit step). Application subscribes to drive the cascade
// that re-executes any ExtrudeOp downstream of this sketch.
struct SketchEditedEvent {
    int sketchId = -1;
};

// Fired when Document::removeBody actually erases a body. The renderer
// listens so it can drop the body's mesh + edge slots IMMEDIATELY — without
// this, a preview-undo during a push/pull drag leaves the previous prism's
// mesh in the ShapeRenderer until something else triggers a full rebuild,
// producing the "banding" effect of N overlapping prism previews drawn on
// top of each other during the drag.
struct BodyRemovedEvent {
    int bodyId = -1;
};

struct ShutdownEvent {};

} // namespace materializr
