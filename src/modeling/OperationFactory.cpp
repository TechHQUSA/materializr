#include "OperationFactory.h"
#include "../core/Operation.h"

#include "PatternOp.h"

namespace OperationFactory {

std::unique_ptr<Operation> create(const std::string& typeId) {
    // Tier 1 — ops that reference whole bodies by id and can therefore be
    // rehydrated to a fully editable state from (params + reload body sets).
    // Add new entries here as each op implements serializeParams /
    // deserializeParams / rehydrateFromReload.
    if (typeId == "pattern") return std::make_unique<PatternOp>();

    return nullptr;
}

} // namespace OperationFactory
