// Android entry point. SDL2 owns the real platform main on Android (it runs an
// activity that loads this shared library and calls SDL_main); including
// SDL_main.h renames the function below to SDL_main. The body mirrors the
// desktop main() minus the CLI parsing — a phone passes no arguments.
#include "app/Application.h"
#include "core/Verbose.h"
#include "android_platform.h"

#include <OSD.hxx>
#include <SDL_main.h>

#include <iostream>

int main(int /*argc*/, char* /*argv*/[]) {
    // Prepare writable storage, extract bundled fonts + OCCT resources, and set
    // HOME/CWD/CSF_* so settings, fonts and OpenCASCADE find their data. Must
    // run before constructing Application (which loads settings and touches OCCT).
    materializr::androidInitRuntime();

    // Convert OCCT internal faults (SIGSEGV/SIGFPE inside the kernel) into
    // catchable Standard_Failure exceptions, matching the desktop build.
    OSD::SetSignal(Standard_False);

    try {
        materializr::Application app(/*safeMode=*/false);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
