#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct SDL_Window;

namespace materializr {

// Windowing/GL-context wrapper. Backed by SDL2 on every platform: a GL 3.3 core
// context on desktop, a GL ES 3.0 context on Android. SDL gives us one input and
// windowing path for both, and maps touch events to mouse so the click-and-drag
// interaction model works on a phone unchanged.
class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    // Non-copyable, non-movable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const { return m_shouldClose; }
    void requestClose(bool close = true) { m_shouldClose = close; }
    void swapBuffers();
    void pollEvents();   // pumps SDL events and forwards them to the ImGui backend

    // Drawable (framebuffer) size in pixels — may exceed window size on HiDPI.
    void framebufferSize(int& w, int& h) const;

    // Hardware Ctrl state, polled directly so it works even while ImGui owns
    // keyboard focus (used by the undo/redo shortcut). Always false on Android.
    static bool isCtrlDown();

    // UI scale factor for HiDPI / touch. 1.0 on desktop; on Android it's derived
    // from the display DPI so fonts and widgets are finger-sized on a tablet.
    float uiScale() const;

    // Raise/lower the Android soft keyboard to match ImGui's WantTextInput. The
    // SDL2 backend no longer calls SDL_StartTextInput itself, which is what shows
    // the keyboard on Android — so we drive it each frame. No-op on desktop.
    void updateTextInput(bool wantTextInput);

    SDL_Window* handle() const { return m_window; }
    void* glContext() const { return m_glContext; }   // SDL_GLContext (opaque)
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Touch-gesture output (Android). pollEvents() recognises two-finger
    // gestures; the viewport camera consumes the accumulated deltas each frame.
    // Returns true and writes the pending delta (then clears it) if any.
    bool consumeTouchPan(float& dx, float& dy);   // centroid movement, pixels
    bool consumeTouchZoom(float& dz);             // pinch delta, wheel-equivalent

    // True once a one-finger press has been held stationary past the hold
    // threshold (and remains true until lift). The viewport uses this to start a
    // box/drag-select instead of orbiting — the touch equivalent of the desktop
    // empty-space left-drag, which trackpad mode otherwise reserves for orbit.
    bool isTouchHoldSelect() const { return m_holdSelect; }

private:
    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    bool m_shouldClose = false;
    int m_width;
    int m_height;

    // Active touch points and the running two-finger gesture state.
    struct Finger { std::int64_t id; float x, y; };
    std::vector<Finger> m_fingers;
    bool  m_leftDown = false;            // is the synthetic left button held?
    bool  m_twoFinger = false;           // a two-finger gesture is active
    bool  m_suppressLeft = false;        // ignore a leftover finger after 2-finger
    float m_lastPinchDist = 0.0f;
    float m_lastCentroidX = 0.0f, m_lastCentroidY = 0.0f;
    float m_panAccX = 0.0f, m_panAccY = 0.0f, m_zoomAcc = 0.0f;

    // One-finger press-and-hold tracking (-> box/drag-select).
    std::uint32_t m_downTicks = 0;        // SDL_GetTicks at single-finger down
    float m_downX = 0.0f, m_downY = 0.0f; // where it went down
    bool  m_movedBeyondHold = false;      // moved too far -> it's a drag, not a hold
    bool  m_holdSelect = false;           // hold threshold passed; select-drag mode
    bool  m_textInputActive = false;      // soft keyboard currently raised

    void handleFingerEvent(unsigned type, std::int64_t id, float nx, float ny);
    void updateHoldSelect();              // per-frame hold check (Android)
};

} // namespace materializr
