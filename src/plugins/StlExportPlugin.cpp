#include "../plugin/PluginMacro.h"
#include "../core/Document.h"
#include "../io/StlExport.h"
#include "../io/FileDialogs.h"
#include <cstdio>

REGISTER_PLUGIN(StlExport, [](materializr::PluginContext& ctx) {
    ctx.registerIOFormat({"STL", {"stl"}, false, true,
        nullptr,
        [](materializr::PluginContext& ctx, const std::string&) {
#if defined(__ANDROID__)
            materializr::FileDialogs::androidExportShareOrSave("export.stl",
                "application/octet-stream",
                [&ctx](const std::string& path) {
                    return materializr::StlExport::exportFile(path, ctx.document()).success;
                });
#else
            materializr::FileDialogs::saveFile("Export STL", "export.stl",
                {{"STL Files", "*.stl"}},
                [&ctx](const std::string& path) {
                    if (path.empty()) return;
                    materializr::StlExport::exportFile(path, ctx.document());
                });
#endif
            return true;
        }});
})
