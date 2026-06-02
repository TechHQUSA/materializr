#pragma once
#include <set>

class History;
class Document;

namespace materializr {
class EventBus;
}

namespace materializr {

class HistoryPanel {
public:
    HistoryPanel();

    void setHistory(History* history);
    void setDocument(Document* doc);
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // Render the panel. Returns true if history was modified (undo/redo/edit).
    bool render();

    // Open a given step in the inline editor (e.g. when the user clicks the face
    // a fillet/chamfer produced). -1 closes the editor.
    void setEditingStep(int step) { m_editingStep = step; m_showProperties = (step >= 0); }
    int getEditingStep() const { return m_editingStep; }

private:
    History* m_history = nullptr;
    Document* m_document = nullptr;
    materializr::EventBus* m_eventBus = nullptr;
    int m_editingStep = -1;
    bool m_showProperties = false;
    bool m_deleteConflict = false; // last delete was blocked by a dependent step
    // Steps with same typeId in a row collapse into a single expandable group
    // header. The set holds the START step index of each group the user has
    // currently collapsed. Groups default to expanded; the user clicks ▼ / ▶
    // to toggle. Keyed by start index — adding / deleting steps shifts the
    // run and the saved state effectively resets, which is acceptable.
    std::set<int> m_collapsedGroupStarts;
    // Set of group start indices we've already auto-classified once. Lets us
    // give historical (non-today) date buckets a collapsed default the first
    // frame they appear, while letting the user override afterwards.
    std::set<int> m_seenGroupStarts;
};

} // namespace materializr
