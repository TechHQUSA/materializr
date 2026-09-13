#include "AiConnectionTest.h"
#include "AnthropicClient.h"
#include "OpenAiCompatibleClient.h"

#include <memory>

namespace materializr { namespace ai {

std::future<LlmTurnResult> testConnection(const AppSettings::AiSettings& ai) {
    std::unique_ptr<LlmClient> client;
    if (ai.provider == AiProvider::Anthropic)
        client = std::make_unique<AnthropicClient>(ai.anthropicApiKey, ai.anthropicModel);
    else
        client = std::make_unique<OpenAiCompatibleClient>(
            ai.openAiApiKey, ai.openAiBaseUrl, ai.openAiModel);
    return std::async(std::launch::async,
        [c = std::shared_ptr<LlmClient>(std::move(client))]() {
            std::vector<ChatMessage> msgs = {{ChatRole::User, "Reply with OK.", ""}};
            return c->sendTurn(msgs, {});
        });
}

} } // namespace materializr::ai
