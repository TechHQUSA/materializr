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

struct ShutdownEvent {};

} // namespace materializr
