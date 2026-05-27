#include "../plugin/PluginMacro.h"
#include "../core/Document.h"
#include "../io/IgesIO.h"
#include "../io/FileDialogs.h"
#include <cstdio>

REGISTER_PLUGIN(IgesIO, [](materializr::PluginContext& ctx) {
    ctx.registerIOFormat({"IGES", {"iges", "igs"}, true, true,
        [](materializr::PluginContext& ctx, const std::string&) {
            materializr::FileDialogs::openFile("Import IGES",
                {{"IGES Files", "*.iges *.igs *.IGES *.IGS"}},
                [&ctx](const std::string& path) {
                    if (path.empty()) return;
                    auto result = materializr::IgesIO::import(path, ctx.document());
                    if (result.success) {
                        ctx.markMeshesDirty();
                    }
                });
            return true;
        },
        [](materializr::PluginContext& ctx, const std::string&) {
            materializr::FileDialogs::saveFile("Export IGES", "export.iges",
                {{"IGES Files", "*.iges *.igs"}},
                [&ctx](const std::string& path) {
                    if (path.empty()) return;
                    materializr::IgesIO::exportFile(path, ctx.document());
                });
            return true;
        }});
})
