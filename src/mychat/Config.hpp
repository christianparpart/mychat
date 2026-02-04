// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>
#include <mcp/ServerManager.hpp>

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mychat
{

/// @brief Voice input mode.
enum class VoiceMode : std::uint8_t
{
    PushToTalk,
    Vad,
};

/// @brief LLM configuration section.
struct LlmConfig
{
    std::string modelPath;
    int contextSize = 8192;
    int gpuLayers = -1;
    float temperature = 0.7f;
    std::string systemPrompt = "You are a helpful assistant with access to tools.";
};

/// @brief Audio configuration section.
struct AudioConfig
{
    bool enabled = false;
    std::string whisperModelPath;
    std::string vadModelPath;
    std::string language = "en";
    std::string deviceName;
    VoiceMode mode = VoiceMode::PushToTalk;

    /// @brief Whether to pause microphone recording during TTS playback.
    /// Prevents the mic from picking up spoken output. Defaults to true.
    bool muteWhileSpeaking = true;
};

/// @brief Text-to-speech configuration section.
struct TtsConfig
{
    /// @brief Whether TTS output is enabled.
    bool enabled = false;

    /// @brief Path to the piper voice model (.onnx file).
    std::string modelPath;

    /// @brief Path to the espeak-ng-data directory (optional, defaults to built-in).
    std::string espeakDataPath;
};

/// @brief Agent loop configuration section.
struct AgentLoopConfig
{
    int maxToolSteps = 10;
    int maxRetries = 3;
    bool verbose = false;
};

/// @brief Top-level application configuration.
struct AppConfig
{
    LlmConfig llm;
    AudioConfig audio;
    TtsConfig tts;
    std::map<std::string, McpServerConfig> mcpServers;
    AgentLoopConfig agent;

    /// @brief Whether to expand the log panel on startup (set via --log CLI flag).
    bool logPanelExpanded = false;
};

/// @brief Loads the application configuration from the default config path.
/// @return The loaded configuration or an error.
[[nodiscard]] auto loadConfig() -> Result<AppConfig>;

/// @brief Loads the application configuration from a specific file path.
/// @param path The path to the config file.
/// @return The loaded configuration or an error.
[[nodiscard]] auto loadConfigFromFile(std::string_view path) -> Result<AppConfig>;

/// @brief Saves the application configuration to a file.
/// @param path The path to the config file.
/// @param config The configuration to save.
/// @return Success or an error.
[[nodiscard]] auto saveConfigToFile(std::string_view path, const AppConfig& config) -> VoidResult;

/// @brief Returns the default config directory path for the current platform.
[[nodiscard]] auto defaultConfigDir() -> std::string;

/// @brief Returns the default config file path for the current platform.
[[nodiscard]] auto defaultConfigPath() -> std::string;

/// @brief Returns the default data directory path for the current platform.
/// On Linux: $XDG_DATA_HOME/mychat or ~/.local/share/mychat
/// On macOS: ~/Library/Application Support/mychat
/// On Windows: %APPDATA%\mychat
[[nodiscard]] auto defaultDataDir() -> std::string;

/// @brief Returns the default model directory path.
[[nodiscard]] auto defaultModelDir() -> std::string;

/// @brief Returns the default model file path.
[[nodiscard]] auto defaultModelPath() -> std::string;

/// @brief Returns the download URL of the default model.
[[nodiscard]] auto defaultModelUrl() -> std::string_view;

/// @brief Returns the filename of the default model.
[[nodiscard]] auto defaultModelFilename() -> std::string_view;

/// @brief Downloads the default model to the default model directory.
/// @return Success or an error with download details.
[[nodiscard]] auto downloadDefaultModel() -> VoidResult;

/// @brief Returns the default whisper model file path.
[[nodiscard]] auto defaultWhisperModelPath() -> std::string;

/// @brief Returns the filename of the default whisper model.
[[nodiscard]] auto defaultWhisperModelFilename() -> std::string_view;

/// @brief Returns the download URL of the default whisper model.
[[nodiscard]] auto defaultWhisperModelUrl() -> std::string_view;

/// @brief Downloads the default whisper model to the default model directory.
/// @return Success or an error with download details.
[[nodiscard]] auto downloadDefaultWhisperModel() -> VoidResult;

/// @brief Returns the default TTS voice model file path.
[[nodiscard]] auto defaultTtsModelPath() -> std::string;

/// @brief Returns the filename of the default TTS voice model.
[[nodiscard]] auto defaultTtsModelFilename() -> std::string_view;

/// @brief Returns the download URL of the default TTS voice model.
[[nodiscard]] auto defaultTtsModelUrl() -> std::string_view;

/// @brief Downloads the default TTS voice model (and its JSON config) to the default model directory.
/// @return Success or an error with download details.
[[nodiscard]] auto downloadDefaultTtsModel() -> VoidResult;

/// @brief Metadata for a downloadable LLM model.
struct ModelInfo
{
    std::string_view name;        ///< Display name (e.g. "Qwen3-8B").
    std::string_view filename;    ///< Local filename (e.g. "Qwen3-8B-Q4_K_M.gguf").
    std::string_view url;         ///< Download URL.
    std::string_view description; ///< One-line description for the selection prompt.
    std::string_view sizeLabel;   ///< Human-readable download size (e.g. "~5.5 GB").
};

/// @brief Returns the list of available LLM models for download.
[[nodiscard]] auto availableModels() -> std::span<const ModelInfo>;

/// @brief Returns the local file path for a given model.
/// @param model The model info.
/// @return Absolute path under the default model directory.
[[nodiscard]] auto modelFilePath(const ModelInfo& model) -> std::string;

/// @brief Downloads a model to the default model directory.
/// @param model The model to download.
/// @return Success or an error with download details.
[[nodiscard]] auto downloadModel(const ModelInfo& model) -> VoidResult;

} // namespace mychat
