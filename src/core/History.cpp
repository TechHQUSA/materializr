#include "History.h"
#include "EventBus.h"
#include "Events.h"

History::History() = default;

bool History::pushOperation(std::unique_ptr<Operation> op, Document& doc) {
    if (!op) {
        return false;
    }

    // Execute the operation
    if (!op->execute(doc)) {
        return false;
    }

    // Clear any redo stack (operations beyond current index)
    if (m_currentIndex + 1 < static_cast<int>(m_operations.size())) {
        m_operations.erase(m_operations.begin() + m_currentIndex + 1, m_operations.end());
    }

    // Push the operation
    m_operations.push_back(std::move(op));
    m_currentIndex = static_cast<int>(m_operations.size()) - 1;

    if (m_eventBus) m_eventBus->publish(materializr::HistoryStepEvent{m_currentIndex, false});
    return true;
}

void History::pushExecuted(std::unique_ptr<Operation> op) {
    if (!op) return;
    if (m_currentIndex + 1 < static_cast<int>(m_operations.size())) {
        m_operations.erase(m_operations.begin() + m_currentIndex + 1, m_operations.end());
    }
    m_operations.push_back(std::move(op));
    m_currentIndex = static_cast<int>(m_operations.size()) - 1;
    if (m_eventBus) m_eventBus->publish(materializr::HistoryStepEvent{m_currentIndex, false});
}

bool History::canUndo() const {
    return m_currentIndex >= 0;
}

bool History::canRedo() const {
    return m_currentIndex < static_cast<int>(m_operations.size()) - 1;
}

bool History::undo(Document& doc) {
    if (!canUndo()) {
        return false;
    }

    Operation* op = m_operations[m_currentIndex].get();
    if (!op->undo(doc)) {
        return false;
    }

    m_currentIndex--;
    if (m_eventBus) m_eventBus->publish(materializr::HistoryStepEvent{m_currentIndex, true});
    return true;
}

bool History::redo(Document& doc) {
    if (!canRedo()) {
        return false;
    }

    m_currentIndex++;
    Operation* op = m_operations[m_currentIndex].get();
    if (!op->execute(doc)) {
        m_currentIndex--;
        return false;
    }

    if (m_eventBus) m_eventBus->publish(materializr::HistoryStepEvent{m_currentIndex, false});
    return true;
}

int History::stepCount() const {
    return static_cast<int>(m_operations.size());
}

int History::currentStep() const {
    return m_currentIndex;
}

const Operation* History::getStep(int index) const {
    if (index < 0 || index >= static_cast<int>(m_operations.size())) {
        return nullptr;
    }
    return m_operations[index].get();
}

bool History::editStep(int index, Document& doc) {
    if (index < 0 || index >= static_cast<int>(m_operations.size())) {
        return false;
    }

    int limit = m_currentIndex;
    if (m_breakpoint >= 0 && m_breakpoint < limit) {
        limit = m_breakpoint;
    }
    if (index > limit) return false; // step isn't part of the current state

    // Rebuild IN PLACE rather than clearing the document: clearing would also
    // wipe bodies that aren't operations (the base/imported bodies) and reset
    // body ids, so dependent ops (which reference body ids) would fail. Instead
    // roll back to just before `index` by undoing the applied ops in reverse,
    // then re-execute from `index` forward so the edited parameters take effect.
    for (int i = limit; i >= index; --i) {
        Operation* op = m_operations[i].get();
        if (op->isEnabled()) op->undo(doc);
    }
    for (int i = index; i <= limit; ++i) {
        Operation* op = m_operations[i].get();
        if (op->isEnabled()) {
            if (!op->execute(doc)) {
                m_currentIndex = i - 1;
                return false;
            }
        }
    }

    return true;
}

bool History::removeStep(int index, Document& doc) {
    int count = static_cast<int>(m_operations.size());
    if (index < 0 || index >= count) return false;

    // If the step is beyond the current state (in the redo region) it has no
    // effect on the document right now — just drop it.
    if (index > m_currentIndex) {
        m_operations.erase(m_operations.begin() + index);
        if (m_breakpoint > index) m_breakpoint--;
        else if (m_breakpoint == index) m_breakpoint = -1;
        return true;
    }

    // Roll the document back to just before `index` by undoing applied ops.
    for (int i = m_currentIndex; i >= index; --i) {
        Operation* op = m_operations[i].get();
        if (op->isEnabled()) op->undo(doc);
    }

    // Detach the op so we can restore it if a later dependent op fails.
    std::unique_ptr<Operation> removed = std::move(m_operations[index]);
    m_operations.erase(m_operations.begin() + index);

    int savedCurrent = m_currentIndex;
    int savedBreak = m_breakpoint;
    m_currentIndex--; // one fewer applied op
    if (m_breakpoint > index) m_breakpoint--;
    else if (m_breakpoint == index) m_breakpoint = -1;

    // Re-execute the remaining ops from `index` forward.
    for (int i = index; i <= m_currentIndex; ++i) {
        Operation* op = m_operations[i].get();
        if (op->isEnabled()) {
            if (!op->execute(doc)) {
                // Conflict: a later op depended on the removed one. Roll back the
                // partial rebuild, reinsert the removed op, and replay the
                // original chain so the model is left exactly as it was.
                for (int j = i - 1; j >= index; --j) {
                    Operation* o = m_operations[j].get();
                    if (o->isEnabled()) o->undo(doc);
                }
                m_operations.insert(m_operations.begin() + index, std::move(removed));
                m_currentIndex = savedCurrent;
                m_breakpoint = savedBreak;
                for (int j = index; j <= m_currentIndex; ++j) {
                    Operation* o = m_operations[j].get();
                    if (o->isEnabled()) o->execute(doc);
                }
                return false;
            }
        }
    }

    if (m_eventBus) m_eventBus->publish(materializr::HistoryStepEvent{m_currentIndex, true});
    return true;
}

void History::setBreakpoint(int index) {
    m_breakpoint = index;
}

int History::getBreakpoint() const {
    return m_breakpoint;
}

bool History::replayAll(Document& doc) {
    doc.clear();

    int limit = static_cast<int>(m_operations.size()) - 1;
    if (m_breakpoint >= 0 && m_breakpoint < limit) {
        limit = m_breakpoint;
    }

    for (int i = 0; i <= limit; ++i) {
        Operation* op = m_operations[i].get();
        if (op->isEnabled()) {
            if (!op->execute(doc)) {
                m_currentIndex = i - 1;
                return false;
            }
        }
    }

    m_currentIndex = limit;
    return true;
}

void History::clear() {
    m_operations.clear();
    m_currentIndex = -1;
    m_breakpoint = -1;
}

const std::vector<std::unique_ptr<Operation>>& History::operations() const {
    return m_operations;
}
