#include "../plugin/PluginMacro.h"
#include "../core/Document.h"
#include "../io/ImageExport.h"
#include "../io/FileDialogs.h"
#include <cstdio>

REGISTER_PLUGIN(ImageExport, [](materializr::PluginContext& ctx) {
    ctx.registerIOFormat({"PNG Screenshot", {"png"}, false, true,
        nullptr,
        [](materializr::PluginContext& ctx, const std::string&) {
            materializr::FileDialogs::saveFile("Export PNG", "screenshot.png",
                {{"PNG Images", "*.png"}},
                [](const std::string& path) {
                    if (path.empty()) return;
                    std::fprintf(stdout, "Image export to %s (requires viewport integration)\n",
                                 path.c_str());
                });
            return true;
        }});
})
