// SPDX-License-Identifier: Apache-2.0
#include "Config.hpp"

#include <core/JsonUtils.hpp>
#include <core/Log.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace mychat
{

namespace
{

    constexpr auto DefaultModelFilename = std::string_view { "SmolLM2-360M-Instruct-Q8_0.gguf" };

    constexpr auto DefaultModelDownloadUrl =
        std::string_view { "https://huggingface.co/bartowski/SmolLM2-360M-Instruct-GGUF/resolve/main/"
                           "SmolLM2-360M-Instruct-Q8_0.gguf" };

    constexpr auto DefaultWhisperModelFilename = std::string_view { "ggml-small.en.bin" };

    constexpr auto DefaultWhisperModelDownloadUrl =
        std::string_view { "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin" };

    constexpr auto DefaultTtsModelFilename = std::string_view { "en_US-lessac-medium.onnx" };

    constexpr auto DefaultTtsModelDownloadUrl =
        std::string_view { "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/"
                           "en_US-lessac-medium.onnx" };

    constexpr auto DefaultTtsModelConfigDownloadUrl =
        std::string_view { "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/"
                           "en_US-lessac-medium.onnx.json" };

    constexpr auto LlmModels = std::array<ModelInfo, 2> { {
        {
            .name = "SmolLM2-360M-Instruct",
            .filename = "SmolLM2-360M-Instruct-Q8_0.gguf",
            .url = "https://huggingface.co/bartowski/SmolLM2-360M-Instruct-GGUF/resolve/main/"
                   "SmolLM2-360M-Instruct-Q8_0.gguf",
            .description = "Lightweight and fast — good for quick testing",
            .sizeLabel = "~360 MB",
        },
        {
            .name = "DeepSeek-R1-Distill-Qwen-7B",
            .filename = "DeepSeek-R1-Distill-Qwen-7B-Q4_K_M.gguf",
            .url = "https://huggingface.co/bartowski/DeepSeek-R1-Distill-Qwen-7B-GGUF/resolve/main/"
                   "DeepSeek-R1-Distill-Qwen-7B-Q4_K_M.gguf",
            .description = "Full-featured with thinking/reasoning mode — recommended for daily use",
            .sizeLabel = "~4.7 GB",
        },
    } };

} // namespace

auto defaultConfigDir() -> std::string
{
#ifdef _WIN32
    auto const* const appData = std::getenv("APPDATA");
    if (appData)
        return std::string(appData) + "\\mychat";
    return ".";
#elif defined(__APPLE__)
    auto const* const home = std::getenv("HOME");
    if (home)
        return std::string(home) + "/Library/Application Support/mychat";
    return ".";
#else
    auto const* const xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig)
        return std::string(xdgConfig) + "/mychat";
    auto const* const home = std::getenv("HOME");
    if (home)
        return std::string(home) + "/.config/mychat";
    return ".";
#endif
}

auto defaultDataDir() -> std::string
{
#ifdef _WIN32
    auto const* const appData = std::getenv("APPDATA");
    if (appData)
        return std::string(appData) + "\\mychat";
    return ".";
#elif defined(__APPLE__)
    auto const* const home = std::getenv("HOME");
    if (home)
        return std::string(home) + "/Library/Application Support/mychat";
    return ".";
#else
    auto const* const xdgData = std::getenv("XDG_DATA_HOME");
    if (xdgData)
        return std::string(xdgData) + "/mychat";
    auto const* const home = std::getenv("HOME");
    if (home)
        return std::string(home) + "/.local/share/mychat";
    return ".";
#endif
}

auto defaultModelDir() -> std::string
{
    return defaultDataDir() + "/models";
}

auto defaultModelPath() -> std::string
{
    return defaultModelDir() + "/" + std::string(DefaultModelFilename);
}

auto defaultModelUrl() -> std::string_view
{
    return DefaultModelDownloadUrl;
}

auto defaultModelFilename() -> std::string_view
{
    return DefaultModelFilename;
}

auto defaultConfigPath() -> std::string
{
    return defaultConfigDir() + "/config.json";
}

auto loadConfigFromFile(std::string_view path) -> Result<AppConfig>
{
    auto file = std::ifstream(std::string(path));
    if (!file.is_open())
        return makeError(ErrorCode::ConfigError, std::format("Cannot open config file: {}", path));

    auto ss = std::stringstream {};
    ss << file.rdbuf();
    auto const content = ss.str();

    auto parseResult = json::parse(content);
    if (!parseResult)
        return std::unexpected(parseResult.error());

    auto const& root = *parseResult;
    auto config = AppConfig {};

    // LLM section
    if (root.contains("llm"))
    {
        auto const& llm = root["llm"];
        config.llm.modelPath = json::getStringOr(llm, "modelPath", "");
        config.llm.contextSize = json::getIntOr(llm, "contextSize", 8192);
        config.llm.gpuLayers = json::getIntOr(llm, "gpuLayers", -1);
        config.llm.temperature = json::getFloatOr(llm, "temperature", 0.7f);
        config.llm.systemPrompt =
            json::getStringOr(llm, "systemPrompt", "You are a helpful assistant with access to tools.");
    }

    // Audio section
    if (root.contains("audio"))
    {
        auto const& audio = root["audio"];
        config.audio.enabled = json::getBoolOr(audio, "enabled", false);
        config.audio.whisperModelPath = json::getStringOr(audio, "whisperModelPath", "");
        config.audio.vadModelPath = json::getStringOr(audio, "vadModelPath", "");
        config.audio.language = json::getStringOr(audio, "language", "en");
        config.audio.deviceName = json::getStringOr(audio, "deviceName", "");

        auto const modeStr = json::getStringOr(audio, "mode", "push-to-talk");
        config.audio.mode = (modeStr == "vad") ? VoiceMode::Vad : VoiceMode::PushToTalk;
        config.audio.muteWhileSpeaking = json::getBoolOr(audio, "muteWhileSpeaking", true);
    }

    // TTS section
    if (root.contains("tts"))
    {
        auto const& tts = root["tts"];
        config.tts.enabled = json::getBoolOr(tts, "enabled", false);
        config.tts.modelPath = json::getStringOr(tts, "modelPath", "");
        config.tts.espeakDataPath = json::getStringOr(tts, "espeakDataPath", "");
    }

    // MCP servers section
    if (root.contains("mcpServers") && root["mcpServers"].is_object())
    {
        for (const auto& [name, serverJson]: root["mcpServers"].items())
        {
            auto serverConfig = McpServerConfig {
                .name = name,
                .command = json::getStringOr(serverJson, "command", ""),
                .args = {},
                .env = {},
            };

            if (serverJson.contains("args") && serverJson["args"].is_array())
            {
                for (const auto& arg: serverJson["args"])
                {
                    if (arg.is_string())
                        serverConfig.args.push_back(arg.get<std::string>());
                }
            }

            if (serverJson.contains("env") && serverJson["env"].is_object())
            {
                for (const auto& [key, value]: serverJson["env"].items())
                {
                    if (value.is_string())
                        serverConfig.env[key] = value.get<std::string>();
                }
            }

            config.mcpServers[name] = std::move(serverConfig);
        }
    }

    // Agent section
    if (root.contains("agent"))
    {
        auto const& agent = root["agent"];
        config.agent.maxToolSteps = json::getIntOr(agent, "maxToolSteps", 10);
        config.agent.maxRetries = json::getIntOr(agent, "maxRetries", 3);
        config.agent.verbose = json::getBoolOr(agent, "verbose", false);
    }

    return config;
}

auto saveConfigToFile(std::string_view path, const AppConfig& config) -> VoidResult
{
    auto root = nlohmann::json::object();

    // LLM section
    auto llm = nlohmann::json::object();
    if (!config.llm.modelPath.empty())
        llm["modelPath"] = config.llm.modelPath;
    llm["contextSize"] = config.llm.contextSize;
    llm["gpuLayers"] = config.llm.gpuLayers;
    llm["temperature"] = config.llm.temperature;
    llm["systemPrompt"] = config.llm.systemPrompt;
    root["llm"] = std::move(llm);

    // Audio section
    auto audio = nlohmann::json::object();
    audio["enabled"] = config.audio.enabled;
    if (!config.audio.whisperModelPath.empty())
        audio["whisperModelPath"] = config.audio.whisperModelPath;
    if (!config.audio.vadModelPath.empty())
        audio["vadModelPath"] = config.audio.vadModelPath;
    audio["language"] = config.audio.language;
    if (!config.audio.deviceName.empty())
        audio["deviceName"] = config.audio.deviceName;
    audio["mode"] = (config.audio.mode == VoiceMode::Vad) ? "vad" : "push-to-talk";
    audio["muteWhileSpeaking"] = config.audio.muteWhileSpeaking;
    root["audio"] = std::move(audio);

    // TTS section
    auto tts = nlohmann::json::object();
    tts["enabled"] = config.tts.enabled;
    if (!config.tts.modelPath.empty())
        tts["modelPath"] = config.tts.modelPath;
    if (!config.tts.espeakDataPath.empty())
        tts["espeakDataPath"] = config.tts.espeakDataPath;
    root["tts"] = std::move(tts);

    // MCP servers section
    if (!config.mcpServers.empty())
    {
        auto servers = nlohmann::json::object();
        for (const auto& [name, serverConfig]: config.mcpServers)
        {
            auto server = nlohmann::json::object();
            server["command"] = serverConfig.command;
            if (!serverConfig.args.empty())
            {
                auto args = nlohmann::json::array();
                for (const auto& arg: serverConfig.args)
                    args.push_back(arg);
                server["args"] = std::move(args);
            }
            if (!serverConfig.env.empty())
            {
                auto env = nlohmann::json::object();
                for (const auto& [key, value]: serverConfig.env)
                    env[key] = value;
                server["env"] = std::move(env);
            }
            servers[name] = std::move(server);
        }
        root["mcpServers"] = std::move(servers);
    }

    // Agent section
    auto agent = nlohmann::json::object();
    agent["maxToolSteps"] = config.agent.maxToolSteps;
    agent["maxRetries"] = config.agent.maxRetries;
    agent["verbose"] = config.agent.verbose;
    root["agent"] = std::move(agent);

    // Create parent directory if needed
    auto const dir = std::filesystem::path(path).parent_path();
    if (!dir.empty())
    {
        auto ec = std::error_code {};
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return makeError(
                ErrorCode::ConfigError,
                std::format("Failed to create config directory '{}': {}", dir.string(), ec.message()));
    }

    auto file = std::ofstream(std::string(path));
    if (!file.is_open())
        return makeError(ErrorCode::ConfigError, std::format("Cannot write config file: {}", path));

    file << root.dump(4) << '\n';
    return {};
}

auto loadConfig() -> Result<AppConfig>
{
    auto const path = defaultConfigPath();
    if (!std::filesystem::exists(path))
    {
        log::info("No config file found at {}, using defaults", path);
        return AppConfig {};
    }

    return loadConfigFromFile(path);
}

auto downloadDefaultModel() -> VoidResult
{
    auto const modelDir = defaultModelDir();
    auto const modelPath = defaultModelPath();

    // Create model directory if it doesn't exist
    auto ec = std::error_code {};
    std::filesystem::create_directories(modelDir, ec);
    if (ec)
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to create model directory '{}': {}", modelDir, ec.message()));

    // Download using curl
    log::info("Downloading default model to {}", modelPath);
    auto const command =
        std::format("curl -fSL --progress-bar -o '{}' '{}'", modelPath, DefaultModelDownloadUrl);
    auto const exitCode = std::system(command.c_str());
    if (exitCode != 0)
    {
        // Clean up partial download
        std::filesystem::remove(modelPath, ec);
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to download default model (curl exit code: {}). "
                                     "Ensure curl is installed and you have internet access.",
                                     exitCode));
    }

    // Verify the file was actually created and has content
    if (!std::filesystem::exists(modelPath) || std::filesystem::file_size(modelPath) == 0)
    {
        std::filesystem::remove(modelPath, ec);
        return makeError(ErrorCode::DownloadError, "Downloaded model file is empty or missing");
    }

    log::info("Default model downloaded successfully");
    return {};
}

auto defaultWhisperModelPath() -> std::string
{
    return defaultModelDir() + "/" + std::string(DefaultWhisperModelFilename);
}

auto defaultWhisperModelFilename() -> std::string_view
{
    return DefaultWhisperModelFilename;
}

auto defaultWhisperModelUrl() -> std::string_view
{
    return DefaultWhisperModelDownloadUrl;
}

auto downloadDefaultWhisperModel() -> VoidResult
{
    auto const modelDir = defaultModelDir();
    auto const modelPath = defaultWhisperModelPath();

    // Create model directory if it doesn't exist
    auto ec = std::error_code {};
    std::filesystem::create_directories(modelDir, ec);
    if (ec)
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to create model directory '{}': {}", modelDir, ec.message()));

    // Download using curl
    log::info("Downloading default whisper model to {}", modelPath);
    auto const command =
        std::format("curl -fSL --progress-bar -o '{}' '{}'", modelPath, DefaultWhisperModelDownloadUrl);
    auto const exitCode = std::system(command.c_str());
    if (exitCode != 0)
    {
        // Clean up partial download
        std::filesystem::remove(modelPath, ec);
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to download default whisper model (curl exit code: {}). "
                                     "Ensure curl is installed and you have internet access.",
                                     exitCode));
    }

    // Verify the file was actually created and has content
    if (!std::filesystem::exists(modelPath) || std::filesystem::file_size(modelPath) == 0)
    {
        std::filesystem::remove(modelPath, ec);
        return makeError(ErrorCode::DownloadError, "Downloaded whisper model file is empty or missing");
    }

    log::info("Default whisper model downloaded successfully");
    return {};
}

auto defaultTtsModelPath() -> std::string
{
    return defaultModelDir() + "/" + std::string(DefaultTtsModelFilename);
}

auto defaultTtsModelFilename() -> std::string_view
{
    return DefaultTtsModelFilename;
}

auto defaultTtsModelUrl() -> std::string_view
{
    return DefaultTtsModelDownloadUrl;
}

auto downloadDefaultTtsModel() -> VoidResult
{
    auto const modelDir = defaultModelDir();
    auto const modelPath = defaultTtsModelPath();
    auto const configPath = modelPath + ".json";

    // Create model directory if it doesn't exist
    auto ec = std::error_code {};
    std::filesystem::create_directories(modelDir, ec);
    if (ec)
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to create model directory '{}': {}", modelDir, ec.message()));

    // Download the ONNX model
    log::info("Downloading default TTS voice model to {}", modelPath);
    auto command =
        std::format("curl -fSL --progress-bar -o '{}' '{}'", modelPath, DefaultTtsModelDownloadUrl);
    auto exitCode = std::system(command.c_str());
    if (exitCode != 0)
    {
        std::filesystem::remove(modelPath, ec);
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to download default TTS model (curl exit code: {}). "
                                     "Ensure curl is installed and you have internet access.",
                                     exitCode));
    }

    if (!std::filesystem::exists(modelPath) || std::filesystem::file_size(modelPath) == 0)
    {
        std::filesystem::remove(modelPath, ec);
        return makeError(ErrorCode::DownloadError, "Downloaded TTS model file is empty or missing");
    }

    // Download the JSON config
    log::info("Downloading TTS model config to {}", configPath);
    command =
        std::format("curl -fSL --progress-bar -o '{}' '{}'", configPath, DefaultTtsModelConfigDownloadUrl);
    exitCode = std::system(command.c_str());
    if (exitCode != 0)
    {
        std::filesystem::remove(configPath, ec);
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to download TTS model config (curl exit code: {}). "
                                     "Ensure curl is installed and you have internet access.",
                                     exitCode));
    }

    if (!std::filesystem::exists(configPath) || std::filesystem::file_size(configPath) == 0)
    {
        std::filesystem::remove(configPath, ec);
        return makeError(ErrorCode::DownloadError, "Downloaded TTS model config file is empty or missing");
    }

    log::info("Default TTS voice model downloaded successfully");
    return {};
}

auto availableModels() -> std::span<const ModelInfo>
{
    return LlmModels;
}

auto modelFilePath(const ModelInfo& model) -> std::string
{
    return defaultModelDir() + "/" + std::string(model.filename);
}

auto downloadModel(const ModelInfo& model) -> VoidResult
{
    auto const modelDir = defaultModelDir();
    auto const path = modelFilePath(model);

    auto ec = std::error_code {};
    std::filesystem::create_directories(modelDir, ec);
    if (ec)
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to create model directory '{}': {}", modelDir, ec.message()));

    log::info("Downloading {} to {}", model.name, path);
    auto const command = std::format("curl -fSL --progress-bar -o '{}' '{}'", path, model.url);
    auto const exitCode = std::system(command.c_str());
    if (exitCode != 0)
    {
        std::filesystem::remove(path, ec);
        return makeError(ErrorCode::DownloadError,
                         std::format("Failed to download {} (curl exit code: {}). "
                                     "Ensure curl is installed and you have internet access.",
                                     model.name,
                                     exitCode));
    }

    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0)
    {
        std::filesystem::remove(path, ec);
        return makeError(ErrorCode::DownloadError,
                         std::format("Downloaded model file is empty or missing: {}", path));
    }

    log::info("{} downloaded successfully", model.name);
    return {};
}

} // namespace mychat
