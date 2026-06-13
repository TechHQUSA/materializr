// Tutorial / onboarding plugin.
//
// A short, skippable, step-by-step "Getting Started" overlay that teaches the
// basic Materializr workflow. It is cross-platform: each step explains both the
// mouse and the touch way of doing things, highlighting whichever input model
// the current run is using (materializr::touchMode()).
//
// Built entirely as a plugin (no Application changes beyond the generic plugin
// hooks): it registers a Help > Getting Started menu item to launch it, and an
// OverlayContribution to draw the panel each frame. It auto-shows once on first
// run, then writes a marker so it stays out of the way until the user reopens it.
//
// Screenshots could be slotted in per step later (load a PNG from assets into a
// GL texture and ImGui::Image it under the body); the step table is laid out to
// make that a localized change.

#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../touch_mode.h"
#include "../ui_scale.h"

#include <imgui.h>
#include <SDL.h>
#include <algorithm>
#include <cfloat>
#include <fstream>
#include <string>

namespace {

// One tour step. `mouse` / `touch` are optional input-specific lines shown under
// the body (null = omit). Where the two inputs do the same thing, fold it into
// `body` and leave both null.
struct Step {
    const char* title;
    const char* body;
    const char* mouse;
    const char* touch;
};

const Step kSteps[] = {
    {
        "Welcome to Materializr",
        "Materializr is a parametric 3D CAD app: sketch a 2D shape, turn it into "
        "a solid, then keep refining it. This quick tour covers the essentials — "
        "it takes a minute. You can reopen it anytime from Help > Getting Started.",
        nullptr, nullptr
    },
    {
        "Moving the view",
        "Orbit, pan and zoom to look at your model from any angle. The View menu "
        "has Frame Selection (F) to zoom onto what's selected and Reset Camera "
        "(Home). The cube in the top-right corner snaps to standard views.",
        "Orbit, pan and zoom with the mouse buttons and wheel (set them in File > "
        "Settings). The Interactions panel always lists the current bindings.",
        "Drag one finger to orbit; use two fingers to pan and pinch-zoom. The "
        "Interactions panel lists every gesture."
    },
    {
        "Selecting things",
        "Click a body, face or edge to select it — most tools act on the current "
        "selection. A small context menu gives quick actions on whatever you're "
        "pointing at.",
        "Click to select, Ctrl+click to add more. Right-click for the context menu.",
        "Tap to select; use the on-screen Multi toggle to add more. Press and hold "
        "(long-press) for the context menu."
    },
    {
        "Sketching a shape",
        "Solids start as 2D sketches. From the Tools panel, start a sketch and pick "
        "a flat face or a plane to draw on, then lay down lines, rectangles and "
        "circles on the grid. Type a number while drawing to set an exact size, then "
        "finish the sketch.",
        "Click to place points; press Enter to finish a shape, Esc to cancel.",
        "Tap to place points, or press-drag-release to draw a segment. Use the "
        "Finish Sketch / Exit Sketch buttons."
    },
    {
        "From sketch to solid",
        "Turn a closed sketch into a 3D body: select it and use Push/Pull to give it "
        "depth, or Revolve to spin it around an axis. On an existing solid, Push/Pull "
        "a face to move it, or add a Fillet/Chamfer to round or bevel edges. Drag the "
        "arrow — or type a distance — to set the amount.",
        nullptr, nullptr
    },
    {
        "History and undo",
        "Every operation is recorded in the History panel. Materializr is "
        "parametric, so you can go back and change an earlier step: double-click a "
        "history entry to edit its parameters and the whole model rebuilds.",
        "Undo / redo with Ctrl+Z / Ctrl+Y, or from the Edit menu.",
        "Undo / redo from the Edit menu (or with a keyboard, if one is attached)."
    },
    {
        "Organize, save and export",
        "The Items panel lists your bodies and sketches — group them into folders to "
        "stay organized. Save your work as a project, or export to STL or STEP for "
        "3D printing or other CAD tools.",
        "Right-click an item to move it into a folder. Use File > Export for STL / "
        "STEP.",
        "Long-press an item to move it into a folder. Export offers Share (send to "
        "another app) or save-to-file."
    },
    {
        "Need more room?",
        "Short on screen space? Collapse the side panels to enlarge the 3D view. On "
        "a keyboard press F9 (or View > Hide Panels). On a touch screen, tap the "
        "small tabs on the left and right edges of the viewport to fold a column "
        "away — tap again to bring it back. Drag a panel's list up or down to scroll.",
        nullptr, nullptr
    },
    {
        "You're ready",
        "That's the basics. Explore the Tools panel, and see Help > User Guide and "
        "Keyboard Shortcuts for more detail. Have fun building.",
        nullptr, nullptr
    },
};
const int kStepCount = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));

// --- State (process-lifetime; the plugin is a singleton overlay) -------------
bool g_open = false;
int  g_step = 0;
bool g_firstRunChecked = false;

// A writable, persistent, per-user marker so the tour auto-shows only on first
// run. SDL_GetPrefPath resolves to the right place on every platform (and
// creates the directory), including Android's app storage.
std::string markerPath() {
    char* base = SDL_GetPrefPath("Materializr", "Materializr");
    std::string p = base ? std::string(base) + "tutorial_seen" : std::string();
    if (base) SDL_free(base);
    return p;
}
bool tutorialSeen() {
    std::string p = markerPath();
    if (p.empty()) return false;
    std::ifstream f(p);
    return f.good();
}
void markSeen() {
    std::string p = markerPath();
    if (p.empty()) return;
    std::ofstream f(p);
    if (f) f << "1\n";
}

void renderTutorial(materializr::PluginContext&) {
    using namespace materializr;

    // First overlay frame: auto-open on a fresh install, otherwise stay hidden.
    if (!g_firstRunChecked) {
        g_firstRunChecked = true;
        if (!tutorialSeen()) { g_open = true; g_step = 0; }
    }
    if (!g_open) return;

    g_step = std::clamp(g_step, 0, kStepCount - 1);
    const Step& s = kSteps[g_step];

    auto close = []() { g_open = false; markSeen(); };

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Fixed (slightly wider) width, but height auto-fits the CURRENT step every
    // frame (the 0 axis) so a longer step grows the window instead of forcing a
    // scrollbar — the body and the Next/Skip row stay visible without scrolling.
    // Re-centered each frame so it grows symmetrically and never runs off the
    // bottom; both width and height are clamped to the available screen.
    const float w = std::min(uiW(520.0f), vp->WorkSize.x * 0.90f);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
                                        ImVec2(FLT_MAX, vp->WorkSize.y * 0.92f));

    bool keepOpen = true;
    if (ImGui::Begin("Getting Started###Tutorial", &keepOpen,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove)) {
        ImGui::TextDisabled("Step %d of %d", g_step + 1, kStepCount);
        ImGui::SeparatorText(s.title);

        ImGui::PushTextWrapPos(0.0f); // wrap at the window's right edge
        ImGui::TextUnformatted(s.body);

        // Input-specific guidance: highlight the model this run is using, but
        // show both so the tour reads the same on every platform.
        if (s.mouse || s.touch) {
            const bool touch = touchMode();
            auto guide = [](const char* tag, const char* text, bool active) {
                if (!text) return;
                const ImVec4 col = active ? ImVec4(0.55f, 0.80f, 1.00f, 1.0f)
                                          : ImVec4(0.60f, 0.60f, 0.64f, 1.0f);
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextWrapped("%s  %s", tag, text);
                ImGui::PopStyleColor();
            };
            // Active input first so the reader sees the relevant one immediately.
            if (touch) { guide("Touch:", s.touch, true);  guide("Mouse:", s.mouse, false); }
            else       { guide("Mouse:", s.mouse, true);  guide("Touch:", s.touch, false); }
        }
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const bool last = (g_step == kStepCount - 1);
        if (g_step > 0) {
            if (ImGui::Button("Back", uiSz(80, 0))) g_step--;
            ImGui::SameLine();
        }
        if (ImGui::Button(last ? "Finish" : "Next", uiSz(80, 0))) {
            if (last) close();
            else      g_step++;
        }
        // Skip / Close pinned to the right edge.
        const float skipW = uiW(80.0f);
        const float rightX = ImGui::GetWindowContentRegionMax().x - skipW;
        if (rightX > ImGui::GetCursorPosX()) ImGui::SameLine(rightX);
        else                                 ImGui::SameLine();
        if (ImGui::Button(last ? "Close" : "Skip", uiSz(80, 0))) close();
    }
    ImGui::End();

    if (!keepOpen) close(); // window's [x]
}

} // namespace

REGISTER_PLUGIN(Tutorial, [](materializr::PluginContext& ctx) {
    // Launcher: Help > Getting Started (rendered via Application's generic
    // plugin-menu wiring).
    materializr::MenuContribution menu;
    menu.path = "Help > Getting Started";
    menu.priority = 10;
    menu.action = [](materializr::PluginContext&) { g_open = true; g_step = 0; };
    ctx.registerMenuItem(std::move(menu));

    // The per-frame overlay that draws the panel.
    materializr::OverlayContribution overlay;
    overlay.name = "Tutorial";
    overlay.render = [](materializr::PluginContext& c) { renderTutorial(c); };
    ctx.registerOverlay(std::move(overlay));
})
