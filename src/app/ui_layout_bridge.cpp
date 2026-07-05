#include "ui_layout_bridge.h"

namespace materializr {

namespace {
std::function<int()>     g_get;
std::function<void(int)> g_set;
} // namespace

int currentUiLayoutIndex() { return g_get ? g_get() : 0; }

void requestUiLayout(int index) {
    if (g_set && index >= 0 && index <= 2) g_set(index);
}

void bindUiLayoutBridge(std::function<int()> get, std::function<void(int)> set) {
    g_get = std::move(get);
    g_set = std::move(set);
}

} // namespace materializr
