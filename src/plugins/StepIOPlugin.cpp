#include "../plugin/PluginMacro.h"
#include "../core/Document.h"
#include "../io/StepIO.h"
#include "../io/FileDialogs.h"
#include <cstdio>

REGISTER_PLUGIN(StepIO, [](materializr::PluginContext& ctx) {
    ctx.registerIOFormat({"STEP", {"step", "stp"}, true, true,
        [](materializr::PluginContext& ctx, const std::string&) {
            materializr::FileDialogs::openFile("Import STEP",
                {{"STEP Files", "*.step *.stp *.STEP *.STP"}},
                [&ctx](const std::string& path) {
                    if (path.empty()) return;
                    auto result = materializr::StepIO::import(path, ctx.document());
                    if (result.success) {
                        ctx.markMeshesDirty();
                    }
                });
            return true;
        },
        [](materializr::PluginContext& ctx, const std::string&) {
#if defined(__ANDROID__)
            materializr::FileDialogs::androidExportShareOrSave("export.step",
                "application/octet-stream",
                [&ctx](const std::string& path) {
                    return materializr::StepIO::exportFile(path, ctx.document()).success;
                });
#else
            materializr::FileDialogs::saveFile("Export STEP", "export.step",
                {{"STEP Files", "*.step *.stp"}},
                [&ctx](const std::string& path) {
                    if (path.empty()) return;
                    materializr::StepIO::exportFile(path, ctx.document());
                });
#endif
            return true;
        }});
})
