#pragma once

class History;
class Document;

namespace materializr {

class HistoryPanel {
public:
    HistoryPanel();

    void setHistory(History* history);
    void setDocument(Document* doc);

    // Render the panel. Returns true if history was modified (undo/redo/edit).
    bool render();

    // Open a given step in the inline editor (e.g. when the user clicks the face
    // a fillet/chamfer produced). -1 closes the editor.
    void setEditingStep(int step) { m_editingStep = step; m_showProperties = (step >= 0); }
    int getEditingStep() const { return m_editingStep; }

private:
    History* m_history = nullptr;
    Document* m_document = nullptr;
    int m_editingStep = -1;
    bool m_showProperties = false;
    bool m_deleteConflict = false; // last delete was blocked by a dependent step
};

} // namespace materializr
