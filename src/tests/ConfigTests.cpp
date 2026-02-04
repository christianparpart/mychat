// SPDX-License-Identifier: Apache-2.0
#include <mychat/Config.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace mychat;

TEST_CASE("defaultConfigDir returns a non-empty path", "[config]")
{
    auto const dir = defaultConfigDir();
    REQUIRE(!dir.empty());
}

TEST_CASE("defaultConfigPath returns a path ending with config.json", "[config]")
{
    auto const path = defaultConfigPath();
    REQUIRE(path.ends_with("config.json"));
}

TEST_CASE("AppConfig has expected defaults", "[config]")
{
    auto const config = AppConfig {};
    CHECK(config.llm.contextSize == 8192);
    CHECK(config.llm.temperature == 0.7f);
    CHECK(config.audio.enabled == false);
    CHECK(config.audio.muteWhileSpeaking == true);
    CHECK(config.agent.maxToolSteps == 10);
}

TEST_CASE("loadConfigFromFile parses valid JSON config", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_config.json";
    {
        auto file = std::ofstream(tempPath);
        file << R"({
            "llm": {
                "modelPath": "/tmp/test.gguf",
                "contextSize": 4096,
                "gpuLayers": 32,
                "temperature": 0.5,
                "systemPrompt": "Test prompt"
            },
            "audio": {
                "enabled": true,
                "whisperModelPath": "/tmp/whisper.bin",
                "vadModelPath": "/tmp/silero-vad.ggml",
                "language": "de",
                "mode": "vad",
                "muteWhileSpeaking": false
            },
            "mcpServers": {
                "test-server": {
                    "command": "echo",
                    "args": ["hello"],
                    "env": {"KEY": "value"}
                }
            },
            "agent": {
                "maxToolSteps": 5,
                "maxRetries": 2,
                "verbose": true
            }
        })";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(result.has_value());

    auto const& config = *result;

    SECTION("LLM config")
    {
        CHECK(config.llm.modelPath == "/tmp/test.gguf");
        CHECK(config.llm.contextSize == 4096);
        CHECK(config.llm.gpuLayers == 32);
        CHECK(config.llm.temperature == 0.5f);
        CHECK(config.llm.systemPrompt == "Test prompt");
    }

    SECTION("Audio config")
    {
        CHECK(config.audio.enabled == true);
        CHECK(config.audio.whisperModelPath == "/tmp/whisper.bin");
        CHECK(config.audio.vadModelPath == "/tmp/silero-vad.ggml");
        CHECK(config.audio.language == "de");
        CHECK(config.audio.mode == VoiceMode::Vad);
        CHECK(config.audio.muteWhileSpeaking == false);
    }

    SECTION("MCP server config")
    {
        REQUIRE(config.mcpServers.contains("test-server"));
        auto const& server = config.mcpServers.at("test-server");
        CHECK(server.command == "echo");
        REQUIRE(server.args.size() == 1);
        CHECK(server.args[0] == "hello");
        REQUIRE(server.env.contains("KEY"));
        CHECK(server.env.at("KEY") == "value");
    }

    SECTION("Agent config")
    {
        CHECK(config.agent.maxToolSteps == 5);
        CHECK(config.agent.maxRetries == 2);
        CHECK(config.agent.verbose == true);
    }

    std::filesystem::remove(tempPath);
}

TEST_CASE("loadConfigFromFile parses vadModelPath from audio config", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_vad_config.json";
    {
        auto file = std::ofstream(tempPath);
        file << R"({
            "audio": {
                "enabled": true,
                "whisperModelPath": "/tmp/whisper.bin",
                "vadModelPath": "/tmp/silero-vad.ggml"
            }
        })";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(result.has_value());
    CHECK(result->audio.vadModelPath == "/tmp/silero-vad.ggml");

    std::filesystem::remove(tempPath);
}

TEST_CASE("loadConfigFromFile defaults vadModelPath to empty string", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_vad_default.json";
    {
        auto file = std::ofstream(tempPath);
        file << R"({
            "audio": {
                "enabled": true,
                "whisperModelPath": "/tmp/whisper.bin"
            }
        })";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(result.has_value());
    CHECK(result->audio.vadModelPath.empty());

    std::filesystem::remove(tempPath);
}

TEST_CASE("loadConfigFromFile parses deviceName from audio config", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_device_name.json";
    {
        auto file = std::ofstream(tempPath);
        file << R"({
            "audio": {
                "enabled": true,
                "whisperModelPath": "/tmp/whisper.bin",
                "deviceName": "Webcam"
            }
        })";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(result.has_value());
    CHECK(result->audio.deviceName == "Webcam");

    std::filesystem::remove(tempPath);
}

TEST_CASE("loadConfigFromFile defaults deviceName to empty", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_device_default.json";
    {
        auto file = std::ofstream(tempPath);
        file << R"({
            "audio": {
                "enabled": true,
                "whisperModelPath": "/tmp/whisper.bin"
            }
        })";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(result.has_value());
    CHECK(result->audio.deviceName.empty());

    std::filesystem::remove(tempPath);
}

TEST_CASE("loadConfigFromFile parses TTS config", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_tts_config.json";
    {
        auto file = std::ofstream(tempPath);
        file << R"({
            "tts": {
                "enabled": true,
                "modelPath": "/tmp/en_US-lessac-medium.onnx",
                "espeakDataPath": "/opt/espeak-ng-data"
            }
        })";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(result.has_value());
    CHECK(result->tts.enabled == true);
    CHECK(result->tts.modelPath == "/tmp/en_US-lessac-medium.onnx");
    CHECK(result->tts.espeakDataPath == "/opt/espeak-ng-data");

    std::filesystem::remove(tempPath);
}

TEST_CASE("loadConfigFromFile defaults TTS config", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_tts_default.json";
    {
        auto file = std::ofstream(tempPath);
        file << R"({})";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(result.has_value());
    CHECK(result->tts.enabled == false);
    CHECK(result->tts.modelPath.empty());
    CHECK(result->tts.espeakDataPath.empty());

    std::filesystem::remove(tempPath);
}

TEST_CASE("AppConfig has expected TTS defaults", "[config]")
{
    auto const config = AppConfig {};
    CHECK(config.tts.enabled == false);
    CHECK(config.tts.modelPath.empty());
    CHECK(config.tts.espeakDataPath.empty());
}

TEST_CASE("loadConfigFromFile returns error for non-existent file", "[config]")
{
    auto result = loadConfigFromFile("/nonexistent/path/config.json");
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::ConfigError);
}

TEST_CASE("loadConfigFromFile returns error for invalid JSON", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_invalid.json";
    {
        auto file = std::ofstream(tempPath);
        file << "{ invalid json }}}";
    }

    auto result = loadConfigFromFile(tempPath.string());
    REQUIRE(!result.has_value());

    std::filesystem::remove(tempPath);
}

TEST_CASE("defaultDataDir returns a non-empty path", "[config]")
{
    auto const dir = defaultDataDir();
    REQUIRE(!dir.empty());
}

TEST_CASE("defaultModelDir returns a path under data dir", "[config]")
{
    auto const modelDir = defaultModelDir();
    auto const dataDir = defaultDataDir();
    REQUIRE(modelDir.starts_with(dataDir));
    REQUIRE(modelDir.ends_with("models"));
}

TEST_CASE("defaultModelPath returns a path ending with the default model filename", "[config]")
{
    auto const modelPath = defaultModelPath();
    REQUIRE(modelPath.ends_with(defaultModelFilename()));
    REQUIRE(modelPath.starts_with(defaultModelDir()));
}

TEST_CASE("defaultModelUrl returns a HuggingFace URL", "[config]")
{
    auto const url = defaultModelUrl();
    REQUIRE(!url.empty());
    REQUIRE(url.find("huggingface.co") != std::string_view::npos);
    REQUIRE(url.find(".gguf") != std::string_view::npos);
}

TEST_CASE("defaultModelFilename returns a GGUF filename", "[config]")
{
    auto const filename = defaultModelFilename();
    REQUIRE(!filename.empty());
    REQUIRE(filename.ends_with(".gguf"));
}

TEST_CASE("defaultWhisperModelPath returns a path ending with ggml-small.en.bin", "[config]")
{
    auto const path = defaultWhisperModelPath();
    REQUIRE(path.ends_with("ggml-small.en.bin"));
    REQUIRE(path.starts_with(defaultModelDir()));
}

TEST_CASE("defaultWhisperModelFilename returns ggml-small.en.bin", "[config]")
{
    auto const filename = defaultWhisperModelFilename();
    REQUIRE(filename == "ggml-small.en.bin");
}

TEST_CASE("defaultWhisperModelUrl returns a HuggingFace URL", "[config]")
{
    auto const url = defaultWhisperModelUrl();
    REQUIRE(!url.empty());
    REQUIRE(url.find("huggingface.co") != std::string_view::npos);
    REQUIRE(url.find(".bin") != std::string_view::npos);
}

TEST_CASE("defaultTtsModelPath returns a path ending with the default TTS model filename", "[config]")
{
    auto const path = defaultTtsModelPath();
    REQUIRE(path.ends_with(defaultTtsModelFilename()));
    REQUIRE(path.starts_with(defaultModelDir()));
}

TEST_CASE("defaultTtsModelFilename returns an ONNX filename", "[config]")
{
    auto const filename = defaultTtsModelFilename();
    REQUIRE(!filename.empty());
    REQUIRE(filename.ends_with(".onnx"));
}

TEST_CASE("defaultTtsModelUrl returns a HuggingFace URL", "[config]")
{
    auto const url = defaultTtsModelUrl();
    REQUIRE(!url.empty());
    REQUIRE(url.find("huggingface.co") != std::string_view::npos);
    REQUIRE(url.find(".onnx") != std::string_view::npos);
}

TEST_CASE("availableModels returns at least two models", "[config]")
{
    auto const models = availableModels();
    REQUIRE(models.size() >= 2);
    for (auto const& m: models)
    {
        CHECK(!m.name.empty());
        CHECK(!m.filename.empty());
        CHECK(!m.url.empty());
        CHECK(!m.description.empty());
        CHECK(!m.sizeLabel.empty());
        CHECK(m.filename.ends_with(".gguf"));
        CHECK(m.url.find("huggingface.co") != std::string_view::npos);
    }
}

TEST_CASE("modelFilePath returns path under model directory", "[config]")
{
    auto const models = availableModels();
    REQUIRE(!models.empty());
    auto const path = modelFilePath(models[0]);
    REQUIRE(path.starts_with(defaultModelDir()));
    REQUIRE(path.ends_with(models[0].filename));
}

TEST_CASE("saveConfigToFile writes a valid config that can be loaded back", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_save_config.json";

    auto config = AppConfig {};
    config.llm.modelPath = "/tmp/test-model.gguf";
    config.llm.contextSize = 4096;
    config.llm.gpuLayers = 16;
    config.llm.temperature = 0.5f;
    config.llm.systemPrompt = "Test system prompt";
    config.audio.enabled = true;
    config.audio.whisperModelPath = "/tmp/whisper.bin";
    config.audio.language = "de";
    config.audio.mode = VoiceMode::Vad;
    config.audio.muteWhileSpeaking = false;
    config.tts.enabled = true;
    config.tts.modelPath = "/tmp/tts-model.onnx";
    config.tts.espeakDataPath = "/opt/espeak-ng-data";
    config.agent.maxToolSteps = 5;
    config.agent.maxRetries = 2;
    config.agent.verbose = true;

    auto saveResult = saveConfigToFile(tempPath.string(), config);
    REQUIRE(saveResult.has_value());

    auto loadResult = loadConfigFromFile(tempPath.string());
    REQUIRE(loadResult.has_value());

    auto const& loaded = *loadResult;
    CHECK(loaded.llm.modelPath == "/tmp/test-model.gguf");
    CHECK(loaded.llm.contextSize == 4096);
    CHECK(loaded.llm.gpuLayers == 16);
    CHECK(loaded.llm.temperature == 0.5f);
    CHECK(loaded.llm.systemPrompt == "Test system prompt");
    CHECK(loaded.audio.enabled == true);
    CHECK(loaded.audio.whisperModelPath == "/tmp/whisper.bin");
    CHECK(loaded.audio.language == "de");
    CHECK(loaded.audio.mode == VoiceMode::Vad);
    CHECK(loaded.audio.muteWhileSpeaking == false);
    CHECK(loaded.tts.enabled == true);
    CHECK(loaded.tts.modelPath == "/tmp/tts-model.onnx");
    CHECK(loaded.tts.espeakDataPath == "/opt/espeak-ng-data");
    CHECK(loaded.agent.maxToolSteps == 5);
    CHECK(loaded.agent.maxRetries == 2);
    CHECK(loaded.agent.verbose == true);

    std::filesystem::remove(tempPath);
}

TEST_CASE("saveConfigToFile round-trips MCP server config", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_save_mcp.json";

    auto config = AppConfig {};
    config.mcpServers["test-server"] = McpServerConfig {
        .name = "test-server",
        .command = "echo",
        .args = { "hello", "world" },
        .env = { { "KEY", "value" } },
    };

    auto saveResult = saveConfigToFile(tempPath.string(), config);
    REQUIRE(saveResult.has_value());

    auto loadResult = loadConfigFromFile(tempPath.string());
    REQUIRE(loadResult.has_value());

    REQUIRE(loadResult->mcpServers.contains("test-server"));
    auto const& server = loadResult->mcpServers.at("test-server");
    CHECK(server.command == "echo");
    REQUIRE(server.args.size() == 2);
    CHECK(server.args[0] == "hello");
    CHECK(server.args[1] == "world");
    REQUIRE(server.env.contains("KEY"));
    CHECK(server.env.at("KEY") == "value");

    std::filesystem::remove(tempPath);
}

TEST_CASE("saveConfigToFile with defaults produces loadable config", "[config]")
{
    auto const tempPath = std::filesystem::temp_directory_path() / "mychat_test_save_defaults.json";

    auto saveResult = saveConfigToFile(tempPath.string(), AppConfig {});
    REQUIRE(saveResult.has_value());

    auto loadResult = loadConfigFromFile(tempPath.string());
    REQUIRE(loadResult.has_value());

    auto const& loaded = *loadResult;
    CHECK(loaded.llm.contextSize == 8192);
    CHECK(loaded.llm.temperature == 0.7f);
    CHECK(loaded.audio.enabled == false);
    CHECK(loaded.tts.enabled == false);
    CHECK(loaded.agent.maxToolSteps == 10);

    std::filesystem::remove(tempPath);
}

TEST_CASE("availableModels contains a model with thinking capability", "[config]")
{
    auto const models = availableModels();
    auto found = false;
    for (auto const& m: models)
    {
        if (m.description.find("thinking") != std::string_view::npos)
        {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}
