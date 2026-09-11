#include "Mate.h"
#include "../core/Document.h"

namespace materializr {

bool bodyIsMatePlaced(const Document& doc, int bodyId) {
    for (const auto& m : doc.getMates()) {
        // A BROKEN mate still owns its body: letting a move through means the
        // solve silently undoes it once the anchors resolve again.
        if (m.suppressed) continue;
        if (m.bodyB == bodyId) return true;
    }
    return false;
}

} // namespace materializr
