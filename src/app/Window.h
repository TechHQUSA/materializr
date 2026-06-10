#pragma once

#include <string>

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

    SDL_Window* handle() const { return m_window; }
    void* glContext() const { return m_glContext; }   // SDL_GLContext (opaque)
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    bool m_shouldClose = false;
    int m_width;
    int m_height;
};

} // namespace materializr
