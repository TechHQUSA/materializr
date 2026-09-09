#pragma once

#include <functional>
#include <utility>

namespace materializr {

// Queue `next` behind whatever `slot` already holds instead of replacing it.
//
// The app runs one deferred heavy task between frames, in a single slot. A
// plain assignment there drops a task that has not run yet, and for a commit
// deferred behind the progress window that means silently losing an operation
// the user confirmed. Chaining keeps the single-slot contract (the startup
// auto-open and session-restore paths still assign it directly, and do mean
// to replace one another) while making the controller path additive.
inline void chainDeferred(std::function<void()>& slot, std::function<void()> next) {
    if (!next) return;
    if (!slot) {
        slot = std::move(next);
        return;
    }
    auto first = std::move(slot);
    slot = [first = std::move(first), next = std::move(next)]() {
        first();
        next();
    };
}

} // namespace materializr
