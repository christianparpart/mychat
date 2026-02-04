// SPDX-License-Identifier: Apache-2.0
#include <core/Log.hpp>
#include <mychat/App.hpp>
#include <mychat/Config.hpp>

#include <CLI/CLI.hpp>

#include <format>

int main(int argc, char** argv)
{
    auto app = CLI::App { "mychat — Local LLM Chatbot with MCP tool integration" };

    auto modelPath = std::string {};
    auto configPath = std::string {};
    auto contextSize = 0;
    auto gpuLayers = -1;
    auto temperature = 0.0f;
    auto voiceMode = std::string {};
    auto verbose = false;
    auto showLog = false;

    app.add_option("-m,--model", modelPath, "Path to GGUF model file");
    app.add_option("-c,--config", configPath, "Path to config file");
    app.add_option("--context-size", contextSize, "Context window size");
    app.add_option("--gpu-layers", gpuLayers, "Number of GPU layers (-1 = auto)");
    app.add_option("--temperature", temperature, "Sampling temperature");
    app.add_option("--voice-mode", voiceMode, "Voice input mode (push-to-talk|vad)");
    app.add_flag("-v,--verbose", verbose, "Enable verbose logging");
    app.add_flag("--log", showLog, "Expand the log panel on startup");

    CLI11_PARSE(app, argc, argv);

    if (verbose)
        mychat::log::setLevel(mychat::log::Level::Debug);

    // Load config
    auto configResult = configPath.empty() ? mychat::loadConfig() : mychat::loadConfigFromFile(configPath);

    if (!configResult)
    {
        mychat::log::error("Failed to load config: {}", configResult.error().message);
        return 1;
    }

    auto& config = *configResult;

    // Apply CLI overrides
    if (!modelPath.empty())
        config.llm.modelPath = modelPath;
    if (contextSize > 0)
        config.llm.contextSize = contextSize;
    if (gpuLayers != -1)
        config.llm.gpuLayers = gpuLayers;
    if (temperature > 0.0f)
        config.llm.temperature = temperature;
    if (!voiceMode.empty())
    {
        if (voiceMode == "vad")
            config.audio.mode = mychat::VoiceMode::Vad;
        else
            config.audio.mode = mychat::VoiceMode::PushToTalk;
        config.audio.enabled = true;
    }
    if (showLog)
        config.logPanelExpanded = true;

    auto application = mychat::App(std::move(config));
    auto initResult = application.initialize();
    if (!initResult)
    {
        mychat::log::error("Initialization failed: {}", initResult.error().message);
        return 1;
    }

    return application.run();
}
