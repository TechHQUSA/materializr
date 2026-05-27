#pragma once
#include <vector>
#include <TopoDS_Shape.hxx>

namespace materializr { class EventBus; }

enum class SelectionType {
    None,
    Body,
    Face,
    Edge,
    Vertex,
    Sketch,
    SketchRegion,
    Plane
};

struct SelectionEntry {
    SelectionType type = SelectionType::None;
    int bodyId = -1;
    int subShapeIndex = -1; // for face/edge/vertex within a body; region index for SketchRegion
    int sketchId = -1;      // when type == Sketch or SketchRegion
    TopoDS_Shape shape;     // the actual selected sub-shape
};

class SelectionManager {
public:
    SelectionManager() = default;
    ~SelectionManager() = default;

    void clear();
    void select(const SelectionEntry& entry);
    void addToSelection(const SelectionEntry& entry);
    void removeFromSelection(const SelectionEntry& entry);
    void toggleSelection(const SelectionEntry& entry);

    bool hasSelection() const;
    SelectionType primaryType() const; // type of first selected item
    const std::vector<SelectionEntry>& getSelection() const;

    int selectedBodyCount() const;
    int selectedFaceCount() const;
    int selectedEdgeCount() const;
    int selectedSketchCount() const;
    int selectedSketchRegionCount() const;

    void setEventBus(materializr::EventBus* bus) { m_eventBus = bus; }

    // For adaptive toolbar
    bool hasSelectedBodies() const;
    bool hasSelectedFaces() const;
    bool hasSelectedEdges() const;
    bool hasSelectedSketches() const;
    bool hasSelectedSketchRegions() const;

private:
    int findEntry(const SelectionEntry& entry) const;
    void publishChanged();

    std::vector<SelectionEntry> m_selection;
    materializr::EventBus* m_eventBus = nullptr;
};
