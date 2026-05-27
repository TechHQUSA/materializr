#pragma once
#include "Contributions.h"
#include <memory>

class Document;
class History;
class SelectionManager;

namespace materializr {

class EventBus;
class Camera;
class InteractiveTool;

class PluginContext {
public:
    Document& document();
    History& history();
    SelectionManager& selection();
    EventBus& events();
    const Camera& camera() const;

    void markMeshesDirty();

    void registerToolbarButton(ToolbarContribution contrib);
    void registerCommand(CommandContribution contrib);
    void registerMenuItem(MenuContribution contrib);
    void registerIOFormat(IOFormatContribution contrib);
    void registerRenderPass(RenderPassContribution contrib);
    void registerPropertySection(PropertyContribution contrib);

    void _bind(Document* doc, History* hist, SelectionManager* sel,
               EventBus* bus, Camera* cam, bool* meshesDirtyFlag);

private:
    Document* m_document = nullptr;
    History* m_history = nullptr;
    SelectionManager* m_selection = nullptr;
    EventBus* m_eventBus = nullptr;
    Camera* m_camera = nullptr;
    bool* m_meshesDirtyFlag = nullptr;
};

} // namespace materializr
