#include "app/Application.h"

#include <cstring>
#include <iostream>

namespace {

struct CliOptions {
    bool safeMode = false;
    bool wantHelp = false;
};

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions o;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--safe-mode")      == 0 ||
            std::strcmp(a, "--safe-graphics")  == 0 ||
            std::strcmp(a, "--low-graphics")   == 0) {
            o.safeMode = true;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            o.wantHelp = true;
        }
    }
    return o;
}

void printHelp() {
    std::cout <<
        "Materializr - parametric 3D CAD\n"
        "\n"
        "Usage: materializr [options]\n"
        "\n"
        "Options:\n"
        "  --safe-mode | --safe-graphics | --low-graphics\n"
        "      Bring the app up in a known-safe configuration: MSAA off,\n"
        "      mesh quality Low, default lights, autosave off, auto-open\n"
        "      last project off. The safe values are written to the settings\n"
        "      file, so subsequent normal launches stay recovered. Use this\n"
        "      if a previously-saved setting crashes the app at startup or\n"
        "      if a complex auto-opened project hangs a lower-core machine.\n"
        "\n"
        "  -h, --help\n"
        "      Print this help and exit.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    CliOptions opts = parseArgs(argc, argv);
    if (opts.wantHelp) {
        printHelp();
        return 0;
    }
    try {
        materializr::Application app(opts.safeMode);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
