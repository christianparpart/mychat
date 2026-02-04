// SPDX-License-Identifier: Apache-2.0
#include "App.hpp"

#include <agent/AgentLoop.hpp>
#include <audio/AudioPipeline.hpp>
#include <audio/TtsSpeaker.hpp>
#include <core/Log.hpp>
#include <llm/ChatSession.hpp>
#include <llm/LlmEngine.hpp>
#include <mcp/ServerManager.hpp>
#include <mychat/Config.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <print>
#include <string>
#include <vector>

#include <tui/InputField.hpp>
#include <tui/LogPanel.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/Terminal.hpp>

namespace mychat
{

namespace
{
    // Voice meter constants
    constexpr auto MeterBars = std::array { "▁", "▂", "▃", "▅", "▇" };
    constexpr auto MeterBarCount = MeterBars.size();
    constexpr auto FloorDb = -48.0f;

    // Layout constants
    constexpr auto InputBoxHeight = 3;
    constexpr auto InputBoxMaxWidth = 64;

    // Style helpers for terminal output
    auto greenStyle() -> tui::Style
    {
        auto style = tui::Style {};
        style.fg = static_cast<std::uint8_t>(2);
        return style;
    }

    auto cyanStyle() -> tui::Style
    {
        auto style = tui::Style {};
        style.fg = static_cast<std::uint8_t>(6);
        return style;
    }

    auto grayStyle() -> tui::Style
    {
        auto style = tui::Style {};
        style.fg = static_cast<std::uint8_t>(8);
        return style;
    }
    auto borderStyle() -> tui::Style
    {
        auto style = tui::Style {};
        style.fg = static_cast<std::uint8_t>(8); // Gray border
        return style;
    }
} // namespace

/// @brief Layout geometry computed from terminal dimensions and current mode.
struct LayoutGeometry
{
    int chatTop = 1;     ///< First row of the chat scroll region.
    int chatBottom = 1;  ///< Last row of the chat scroll region.
    int inputRow = 1;    ///< Top row of the 3-row input box.
    int inputCol = 1;    ///< Left column of the input box.
    int inputWidth = 64; ///< Width of the input box (including borders).
    int logStartRow = 1; ///< First row of the log panel.
};

struct App::Impl
{
    AppConfig config;
    LlmEngine engine;
    ChatSession session;
    ServerManager servers;
    tui::Terminal terminal;
    tui::InputField inputField;
    std::unique_ptr<AgentLoop> agent;

    // Audio pipeline
    std::unique_ptr<AudioPipeline> audioPipeline;
    bool audioInitialized = false;
    bool voiceEnabled = false;
    bool recording = false;
    std::mutex transcriptionMutex;
    std::deque<std::string> transcriptionQueue;

    // TTS
    std::unique_ptr<TtsSpeaker> ttsSpeaker;
    bool ttsEnabled = false;
    std::string sentenceBuffer;
    std::vector<std::string> ttsPendingSentences;
    bool ttsInsideThink = false;
    std::string ttsTagBuffer;

    // Layout state
    tui::LogPanel logPanel;
    bool conversationStarted = false;
    bool tuiActive = false;
    std::atomic<bool> logPanelDirty = false;
    LayoutGeometry geo;

    /// @brief Messages queued before TUI is active, replayed into the log panel on TUI start.
    std::vector<tui::LogEntry> pendingLogs;

    explicit Impl(AppConfig cfg): config(std::move(cfg)), session(config.llm.systemPrompt) {}

    /// @brief Replays all pending log messages into the log panel. Called once after TUI init.
    void replayPendingLogs()
    {
        for (auto& entry: pendingLogs)
            logPanel.addLog(entry.level, std::move(entry.message));
        pendingLogs.clear();
    }

    // --- Layout computation ---

    /// @brief Computes the layout geometry based on terminal dimensions and current mode.
    void computeGeometry()
    {
        auto const cols = terminal.output().columns();
        auto const rows = terminal.output().rows();
        auto const logHeight = logPanel.totalHeight();

        geo.inputWidth = std::min(InputBoxMaxWidth, cols - 4);
        geo.inputCol = (cols - geo.inputWidth) / 2 + 1;

        if (conversationStarted)
        {
            geo.logStartRow = rows - logHeight + 1;
            geo.inputRow = geo.logStartRow - InputBoxHeight;
            geo.chatTop = 1;
            geo.chatBottom = geo.inputRow - 1;
        }
        else
        {
            geo.logStartRow = rows - logHeight + 1;
            geo.inputRow = rows / 2 - 1;
            geo.inputCol = (cols - geo.inputWidth) / 2 + 1;
        }
    }

    // --- Terminal output helpers ---

    /// @brief Logs an info message to the log panel and re-renders it.
    /// @param message The info message.
    void logInfo(std::string_view message)
    {
        logPanel.addLog(tui::LogLevel::Info, std::string(message));
        renderLogPanel();
    }

    /// @brief Logs an error message to the log panel and re-renders it.
    /// @param message The error message.
    void logError(std::string_view message)
    {
        logPanel.addLog(tui::LogLevel::Error, std::string(message));
        renderLogPanel();
    }

    /// @brief Logs a warning message to the log panel and re-renders it.
    /// @param message The warning message.
    void logWarning(std::string_view message)
    {
        logPanel.addLog(tui::LogLevel::Warning, std::string(message));
        renderLogPanel();
    }

    /// @brief Writes output into the chat scroll region (for /help, /tools, etc. in conversation mode).
    /// @param text The text to write.
    void writeToChatArea(std::string_view text)
    {
        auto& out = terminal.output();
        if (conversationStarted)
        {
            out.saveCursor();
            out.setScrollRegion(geo.chatTop, geo.chatBottom);
            out.moveTo(geo.chatBottom, 1);
            out.writeRaw(text);
            out.resetScrollRegion();
            out.restoreCursor();
            out.flush();
        }
        else
        {
            out.writeRaw(text);
            out.flush();
        }
    }

    /// @brief Prints the user's message label in the chat area.
    /// @param text The user's input text.
    void printUserMessage(std::string_view text)
    {
        auto& out = terminal.output();
        if (conversationStarted)
        {
            out.setScrollRegion(geo.chatTop, geo.chatBottom);
            out.moveTo(geo.chatBottom, 1);
            out.writeRaw("\n");
            out.write("You: ", greenStyle());
            out.writeRaw(text);
            out.writeRaw("\n\n");
            out.resetScrollRegion();
            out.flush();
        }
    }

    /// @brief Prints available commands to the chat area.
    void printHelp()
    {
        auto helpText = std::string {};
        helpText += "\033[1mAvailable commands:\033[m\n";
        helpText += "  /quit   \u2014 Exit the application\n";
        helpText += "  /clear  \u2014 Clear conversation history\n";
        helpText += "  /voice  \u2014 Toggle voice input (requires audio config)\n";
        helpText += "  /tts    \u2014 Toggle text-to-speech output (requires tts config)\n";
        helpText += "  /tools  \u2014 List available MCP tools\n";
        helpText += "  /help   \u2014 Show this help message\n";
        helpText += "\n";
        writeToChatArea(helpText);
    }

    /// @brief Prints a voice transcription label in the chat area.
    /// @param text The transcribed text.
    void printVoiceTranscription(std::string_view text)
    {
        auto& out = terminal.output();
        if (conversationStarted)
        {
            out.setScrollRegion(geo.chatTop, geo.chatBottom);
            out.moveTo(geo.chatBottom, 1);
            out.writeRaw("\n");
            out.write("Voice> ", cyanStyle());
            out.writeRaw(text);
            out.writeRaw("\n\n");
            out.resetScrollRegion();
            out.flush();
        }
    }

    // --- Rendering ---

    /// @brief Renders the full screen layout based on the current mode.
    void renderFullScreen()
    {
        auto& out = terminal.output();
        auto sync = out.syncGuard();
        out.clearScreen();
        computeGeometry();

        if (conversationStarted)
            renderConversationMode();
        else
            renderInitialMode();

        renderLogPanel();
        renderInputBox();
        positionCursorInInputBox();
        out.flush();
    }

    /// @brief Renders the initial (Google-style) centered input layout.
    void renderInitialMode()
    {
        auto& out = terminal.output();
        auto const cols = out.columns();

        // Center a title above the input box
        auto const title = std::string_view { "mychat" };
        auto const titleCol = (cols - static_cast<int>(title.size())) / 2 + 1;
        auto const titleRow = geo.inputRow - 2;
        if (titleRow >= 1)
        {
            out.moveTo(titleRow, titleCol);
            out.write(title, cyanStyle());
        }
    }

    /// @brief Renders the conversation mode layout (just sets up the scroll region area).
    void renderConversationMode()
    {
        // The chat area is the scroll region. Content is already scrolled into it
        // by writeToChatArea / printUserMessage. Nothing to redraw here on initial render.
    }

    /// @brief Renders the bordered input box at the computed position.
    void renderInputBox()
    {
        auto& out = terminal.output();
        auto const w = geo.inputWidth;
        auto const row = geo.inputRow;
        auto const col = geo.inputCol;

        // Top border: ╭───────────────╮
        out.moveTo(row, col);
        out.write("\u256D", borderStyle()); // ╭
        auto topFill = std::string {};
        for (auto i = 0; i < w - 2; ++i)
            topFill += "\u2500"; // ─

        // Integrate voice meter into top border if active
        if (voiceEnabled && audioPipeline)
        {
            // Replace first few chars with voice meter
            auto& output = terminal.output();
            output.write(topFill.substr(0, 3), borderStyle()); // a few ─ chars
            renderVoiceMeter(audioPipeline->peakLevel());
            // remaining border (estimate meter width ~ 6 chars)
            auto const meterWidth = static_cast<int>(MeterBarCount) + 1; // bars + space
            auto const remainingBorderChars = w - 2 - 3 - meterWidth;
            if (remainingBorderChars > 0)
            {
                auto remainFill = std::string {};
                for (auto i = 0; i < remainingBorderChars; ++i)
                    remainFill += "\u2500";
                output.write(remainFill, borderStyle());
            }
        }
        else
        {
            out.write(topFill, borderStyle());
        }
        out.write("\u256E", borderStyle()); // ╮

        // Middle row: │ text... │
        out.moveTo(row + 1, col);
        out.write("\u2502", borderStyle()); // │
        out.write(" ", {});                 // left padding

        auto const innerWidth = w - 4; // 2 for borders, 2 for padding
        auto const& text = inputField.text();
        if (text.empty())
        {
            // Placeholder
            auto placeholder = std::string_view { "Ask anything\u2026" };
            out.write(placeholder, grayStyle());
            auto const placeholderLen = 14; // "Ask anything…" display width
            auto const pad = std::max(0, innerWidth - placeholderLen);
            out.writeRaw(std::string(static_cast<std::size_t>(pad), ' '));
        }
        else
        {
            // Actual text (truncated to fit)
            auto displayText = std::string {};
            auto displayLen = 0;
            for (auto const ch: text)
            {
                if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80)
                    ++displayLen;
                if (displayLen > innerWidth)
                    break;
                displayText += ch;
            }
            out.writeRaw(displayText);
            auto const pad = std::max(0, innerWidth - displayLen);
            out.writeRaw(std::string(static_cast<std::size_t>(pad), ' '));
        }
        out.write(" ", {});                 // right padding
        out.write("\u2502", borderStyle()); // │

        // Bottom border: ╰───────────────╯
        out.moveTo(row + 2, col);
        out.write("\u2570", borderStyle()); // ╰
        out.write(topFill, borderStyle());
        out.write("\u256F", borderStyle()); // ╯
    }

    /// @brief Positions the terminal cursor inside the input box at the correct column.
    void positionCursorInInputBox()
    {
        auto& out = terminal.output();
        auto const row = geo.inputRow + 1;     // middle row of input box
        auto const textCol = geo.inputCol + 2; // after │ and space

        // Calculate cursor column from InputField cursor position
        auto cursorDisplayCol = 0;
        auto const cursorPos = inputField.cursor();
        auto const& text = inputField.text();
        auto byteIdx = std::size_t { 0 };
        for (auto const ch: text)
        {
            if (byteIdx >= static_cast<std::size_t>(cursorPos))
                break;
            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80)
                ++cursorDisplayCol;
            ++byteIdx;
        }

        out.moveTo(row, textCol + cursorDisplayCol);
        out.showCursor();
    }

    /// @brief Renders the log panel at its computed position.
    void renderLogPanel()
    {
        auto& out = terminal.output();
        computeGeometry(); // recompute in case log panel height changed
        logPanel.render(out, geo.logStartRow, out.columns());
        out.flush();
    }

    // --- Voice meter ---

    /// @brief Renders the voice level meter using styled block characters.
    /// @param level Peak audio level in [0.0, 1.0].
    void renderVoiceMeter(float level)
    {
        auto& output = terminal.output();

        // Convert linear amplitude to perceptual dB scale
        auto scaledLevel = 0.0f;
        if (level > 0.0f)
        {
            auto const db = 20.0f * std::log10(level);
            scaledLevel = std::clamp((db - FloorDb) / -FloorDb, 0.0f, 1.0f);
        }

        auto const activeBars = static_cast<std::size_t>(scaledLevel * static_cast<float>(MeterBarCount));

        for (auto i = std::size_t { 0 }; i < MeterBarCount; ++i)
        {
            auto style = tui::Style {};
            if (i < activeBars)
            {
                if (i < 2)
                    style.fg = static_cast<std::uint8_t>(2); // Green
                else if (i < 4)
                    style.fg = static_cast<std::uint8_t>(3); // Yellow
                else
                    style.fg = static_cast<std::uint8_t>(1); // Red
            }
            else
            {
                style.fg = static_cast<std::uint8_t>(8); // Gray
            }
            output.write(MeterBars[i], style);
        }
        output.writeRaw(" ");
    }

    /// @brief Transitions from initial mode to conversation mode.
    void transitionToConversation()
    {
        conversationStarted = true;
        computeGeometry();
        renderFullScreen();
    }

    // --- Audio/TTS helpers ---

    /// @brief Maps the config VoiceMode enum to the AudioPipeline AudioMode enum.
    /// @param mode The VoiceMode from configuration.
    /// @return The corresponding AudioMode.
    static auto toAudioMode(VoiceMode mode) -> AudioMode
    {
        switch (mode)
        {
            case VoiceMode::Vad: return AudioMode::VoiceActivityDetection;
            case VoiceMode::PushToTalk: return AudioMode::PushToTalk;
        }
        return AudioMode::PushToTalk;
    }

    /// @brief Thread-safe enqueue of a transcription result.
    /// @param text The transcribed text.
    void enqueueTranscription(std::string text)
    {
        auto lock = std::lock_guard(transcriptionMutex);
        transcriptionQueue.push_back(std::move(text));
    }

    /// @brief Appends a token to the sentence buffer and speaks complete sentences via TTS.
    /// Filters out content inside <think>...</think> tags (used by reasoning models).
    /// @param token The token to append.
    void feedTtsToken(std::string_view token)
    {
        if (!ttsSpeaker || !ttsEnabled)
            return;

        using namespace std::string_view_literals;

        // Filter out <think>...</think> blocks character by character,
        // since tags may span token boundaries.
        for (auto const ch: token)
        {
            if (!ttsTagBuffer.empty())
            {
                ttsTagBuffer += ch;

                if (ttsTagBuffer == "<think>")
                {
                    ttsInsideThink = true;
                    ttsTagBuffer.clear();
                    continue;
                }
                if (ttsTagBuffer == "</think>")
                {
                    ttsInsideThink = false;
                    ttsTagBuffer.clear();
                    continue;
                }

                // Still a valid prefix of either tag? Keep buffering.
                if ("<think>"sv.starts_with(ttsTagBuffer) || "</think>"sv.starts_with(ttsTagBuffer))
                    continue;

                // Not a tag — flush buffered chars to sentence buffer (unless inside think)
                if (!ttsInsideThink)
                    sentenceBuffer.append(ttsTagBuffer);
                ttsTagBuffer.clear();
                continue;
            }

            if (ch == '<')
            {
                ttsTagBuffer = "<";
                continue;
            }

            if (!ttsInsideThink)
                sentenceBuffer += ch;
        }

        // Scan for sentence boundaries: ". ", "! ", "? ", or "\n"
        while (true)
        {
            auto pos = std::string::npos;
            for (auto i = std::size_t { 0 }; i < sentenceBuffer.size(); ++i)
            {
                auto const ch = sentenceBuffer[i];
                if (ch == '\n')
                {
                    pos = i;
                    break;
                }
                if ((ch == '.' || ch == '!' || ch == '?') && i + 1 < sentenceBuffer.size()
                    && sentenceBuffer[i + 1] == ' ')
                {
                    pos = i + 1; // include the trailing space
                    break;
                }
            }

            if (pos == std::string::npos)
                break;

            auto sentence = sentenceBuffer.substr(0, pos + 1);
            sentenceBuffer.erase(0, pos + 1);

            // Trim whitespace from the sentence
            while (!sentence.empty() && (sentence.back() == ' ' || sentence.back() == '\n'))
                sentence.pop_back();

            if (!sentence.empty())
                ttsPendingSentences.push_back(std::move(sentence));
        }
    }

    /// @brief Flushes any remaining text in the sentence buffer to TTS, resets tag state,
    ///        and waits for playback to finish. Pressing ESC cancels remaining playback.
    ///        Pauses microphone recording during playback if muteWhileSpeaking is enabled.
    void flushTts()
    {
        if (!ttsSpeaker || !ttsEnabled)
            return;

        // Reset think-tag filtering state for the next message
        ttsInsideThink = false;
        ttsTagBuffer.clear();

        // Collect whatever remains in the sentence buffer as a final sentence
        while (!sentenceBuffer.empty() && (sentenceBuffer.back() == ' ' || sentenceBuffer.back() == '\n'))
            sentenceBuffer.pop_back();

        if (!sentenceBuffer.empty())
        {
            ttsPendingSentences.push_back(std::move(sentenceBuffer));
            sentenceBuffer.clear();
        }

        if (ttsPendingSentences.empty())
            return;

        // Pause microphone recording to avoid picking up TTS output
        auto const shouldMute = voiceEnabled && audioPipeline && config.audio.muteWhileSpeaking;
        if (shouldMute)
            audioPipeline->pauseRecording();

        // Enqueue all collected sentences for playback
        for (auto& sentence: ttsPendingSentences)
            ttsSpeaker->speak(std::move(sentence));
        ttsPendingSentences.clear();

        // Wait for TTS to finish, using Terminal poll for ESC detection (already in raw mode)
        auto cancelled = false;
        while (!ttsSpeaker->idle() && !cancelled)
        {
            auto events = terminal.poll(50);
            for (auto const& event: events)
            {
                if (auto const* key = std::get_if<tui::KeyEvent>(&event))
                {
                    if (key->key == tui::KeyCode::Escape)
                    {
                        ttsSpeaker->cancel();
                        cancelled = true;
                        break;
                    }
                }
            }
        }

        // Resume microphone recording
        if (shouldMute)
            audioPipeline->resumeRecording();
    }

    /// @brief Resolves the TTS model path, downloading the default if needed, and initializes the speaker.
    /// @return Success or an error.
    auto initializeTts() -> VoidResult
    {
        // Auto-resolve model path if not specified
        if (config.tts.modelPath.empty())
        {
            auto const ttsModelPath = defaultTtsModelPath();
            if (!std::filesystem::exists(ttsModelPath))
            {
                log::info("No TTS model specified. Downloading default ({})...", defaultTtsModelFilename());
                auto downloadResult = downloadDefaultTtsModel();
                if (!downloadResult)
                    return std::unexpected(downloadResult.error());
            }
            config.tts.modelPath = ttsModelPath;
        }

        ttsSpeaker = std::make_unique<TtsSpeaker>();
        auto ttsConfig = TtsSpeakerConfig {
            .modelPath = config.tts.modelPath,
            .espeakDataPath = config.tts.espeakDataPath,
        };
        auto ttsResult = ttsSpeaker->initialize(ttsConfig);
        if (!ttsResult)
        {
            ttsSpeaker.reset();
            return std::unexpected(ttsResult.error());
        }

        ttsEnabled = true;
        log::info("TTS speaker initialized successfully");
        return {};
    }

    /// @brief Thread-safe check whether there are pending transcriptions.
    /// @return True if at least one transcription is queued.
    auto hasTranscriptions() -> bool
    {
        auto lock = std::lock_guard(transcriptionMutex);
        return !transcriptionQueue.empty();
    }

    /// @brief Thread-safe drain of all pending transcriptions.
    /// @return A vector of transcribed strings.
    auto drainTranscriptions() -> std::vector<std::string>
    {
        auto lock = std::lock_guard(transcriptionMutex);
        auto result = std::vector<std::string>(transcriptionQueue.begin(), transcriptionQueue.end());
        transcriptionQueue.clear();
        return result;
    }
};

App::App(AppConfig config): _impl(std::make_unique<Impl>(std::move(config)))
{
    // Install log callback immediately so all messages (including model loading)
    // are captured. Before TUI is active, messages are queued; after, they go
    // directly to the LogPanel.
    log::setCallback([this](log::Level level, std::string_view message) {
        auto tuiLevel = tui::LogLevel::Info;
        switch (level)
        {
            case log::Level::Error: tuiLevel = tui::LogLevel::Error; break;
            case log::Level::Warning: tuiLevel = tui::LogLevel::Warning; break;
            default: tuiLevel = tui::LogLevel::Info; break;
        }
        if (_impl->tuiActive)
        {
            _impl->logPanel.addLog(tuiLevel, std::string(message));
            _impl->logPanelDirty.store(true, std::memory_order_relaxed);
        }
        else
        {
            _impl->pendingLogs.push_back(
                tui::LogEntry { .level = tuiLevel, .message = std::string(message) });
        }
    });
}

App::~App() = default;

auto App::initialize() -> VoidResult
{
    // Resolve model if none specified
    if (_impl->config.llm.modelPath.empty())
    {
        auto const models = availableModels();

        // Check if any known model already exists locally
        auto found = false;
        for (const auto& model: models)
        {
            auto const path = modelFilePath(model);
            if (std::filesystem::exists(path))
            {
                _impl->config.llm.modelPath = path;
                log::info("Using existing model: {}", path);
                found = true;
                break;
            }
        }

        if (!found)
        {
            // Present model selection to the user (cooked mode, before terminal init)
            std::println("");
            std::println("  No LLM model found. Select a model to download:");
            std::println("");
            for (auto i = size_t { 0 }; i < models.size(); ++i)
            {
                auto const& m = models[i];
                auto const* const tag = (i == models.size() - 1) ? " (recommended)" : "";
                std::println("  [{}] {} ({}){}", i + 1, m.name, m.sizeLabel, tag);
                std::println("      {}", m.description);
            }
            std::println("");

            auto choice = size_t { 0 };
            while (choice < 1 || choice > models.size())
            {
                std::print("  Choice [1-{}]: ", models.size());
                auto line = std::string {};
                if (!std::getline(std::cin, line))
                    return makeError(ErrorCode::ConfigError, "No model selected");
                try
                {
                    choice = std::stoull(line);
                }
                catch (...)
                {
                    choice = 0;
                }
            }

            auto const& selected = models[choice - 1];
            std::println("\033[34mInfo:\033[0m Downloading {} ({})...", selected.name, selected.sizeLabel);

            auto downloadResult = downloadModel(selected);
            if (!downloadResult)
                return std::unexpected(downloadResult.error());

            _impl->config.llm.modelPath = modelFilePath(selected);
        }
    }

    auto engineConfig = LlmEngineConfig {
        .modelPath = _impl->config.llm.modelPath,
        .contextSize = _impl->config.llm.contextSize,
        .gpuLayers = _impl->config.llm.gpuLayers,
    };

    auto loadResult = _impl->engine.load(engineConfig);
    if (!loadResult)
        return loadResult;

    // Connect MCP servers
    for (const auto& [name, serverConfig]: _impl->config.mcpServers)
    {
        auto addResult = _impl->servers.addServer(serverConfig);
        if (!addResult)
            log::warning("Failed to connect MCP server '{}': {}", name, addResult.error().message);
    }

    // Create agent loop
    auto agentConfig = AgentConfig {
        .maxToolSteps = _impl->config.agent.maxToolSteps,
        .maxRetries = _impl->config.agent.maxRetries,
        .sampler = SamplerConfig { .temperature = _impl->config.llm.temperature },
    };

    _impl->agent = std::make_unique<AgentLoop>(_impl->engine, _impl->session, _impl->servers, agentConfig);

    // Initialize audio pipeline if configured
    if (_impl->config.audio.enabled)
    {
        // Auto-resolve whisper model path if not specified
        if (_impl->config.audio.whisperModelPath.empty())
        {
            auto const whisperPath = defaultWhisperModelPath();
            if (!std::filesystem::exists(whisperPath))
            {
                log::info("No whisper model specified. Downloading default ({})...",
                          defaultWhisperModelFilename());
                auto downloadResult = downloadDefaultWhisperModel();
                if (!downloadResult)
                {
                    log::error("Whisper model download failed: {}", downloadResult.error().message);
                }
            }
            if (std::filesystem::exists(whisperPath))
                _impl->config.audio.whisperModelPath = whisperPath;
        }

        if (_impl->config.audio.whisperModelPath.empty()
            || !std::filesystem::exists(_impl->config.audio.whisperModelPath))
        {
            log::warning("Audio configured but whisper model not found \u2014 voice input unavailable");
        }
        else
        {
            _impl->audioPipeline = std::make_unique<AudioPipeline>();

            auto pipelineConfig = AudioPipelineConfig {
                .whisperModelPath = _impl->config.audio.whisperModelPath,
                .vadModelPath = _impl->config.audio.vadModelPath,
                .language = _impl->config.audio.language,
                .deviceName = _impl->config.audio.deviceName,
                .mode = Impl::toAudioMode(_impl->config.audio.mode),
            };

            auto initResult = _impl->audioPipeline->initialize(
                pipelineConfig, [this](std::string text) { _impl->enqueueTranscription(std::move(text)); });

            if (initResult)
            {
                _impl->audioInitialized = true;
                log::info("Audio pipeline initialized successfully");
            }
            else
            {
                log::error("Audio initialization failed: {}", initResult.error().message);
                _impl->audioPipeline.reset();
            }
        }
    }

    // Initialize TTS speaker if configured
    if (_impl->config.tts.enabled)
    {
        auto ttsResult = _impl->initializeTts();
        if (!ttsResult)
            log::error("TTS initialization failed: {}", ttsResult.error().message);
    }

    // Auto-create config file with resolved settings if none exists
    auto const configPath = defaultConfigPath();
    if (!std::filesystem::exists(configPath))
    {
        auto saveResult = saveConfigToFile(configPath, _impl->config);
        if (saveResult)
            log::info("Config file created at {}", configPath);
        else
            log::warning("Failed to save config file: {}", saveResult.error().message);
    }

    log::info("Application initialized successfully");
    return {};
}

auto App::run() -> int
{
    // Flush any pending stdout before entering raw mode
    std::cout.flush();

    // Initialize terminal for interactive mode (raw mode, protocols, SIGWINCH)
    auto termResult = _impl->terminal.initialize();
    if (!termResult)
    {
        std::println(stderr, "Failed to initialize terminal: {}", termResult.error().message);
        return 1;
    }

    auto& output = _impl->terminal.output();

    // Enter alt screen for a clean full-screen experience
    output.enterAltScreen();
    output.hideCursor();
    output.flush();

    // Mark TUI as active and replay any messages queued during initialize()
    _impl->tuiActive = true;
    _impl->replayPendingLogs();

    // Expand the log panel on startup if requested via --log CLI flag
    if (_impl->config.logPanelExpanded)
        _impl->logPanel.toggle();

    // Log initial status messages
    if (_impl->audioInitialized)
        _impl->logInfo("Audio pipeline ready \u2014 use /voice to enable voice input");

    if (_impl->ttsSpeaker)
        _impl->logInfo("TTS ready \u2014 use /tts to toggle speech output");

    _impl->logInfo("Type /help for commands, /quit to exit");

    auto mdRenderer = tui::MarkdownRenderer(output);

    // Render initial layout
    _impl->renderFullScreen();

    auto const processTranscription = [&](std::string_view text) {
        if (!_impl->conversationStarted)
            _impl->transitionToConversation();

        _impl->printVoiceTranscription(text);

        // Set scroll region for streaming
        output.setScrollRegion(_impl->geo.chatTop, _impl->geo.chatBottom);
        output.moveTo(_impl->geo.chatBottom, 1);

        mdRenderer.beginStream();
        auto streamCb = [&](std::string_view token) {
            mdRenderer.feedToken(token);
            output.flush();
            _impl->feedTtsToken(token);
        };

        auto result = _impl->agent->processMessage(text, streamCb);
        if (!result)
            _impl->logError(std::format("{}", result.error()));

        mdRenderer.endStream();
        output.resetScrollRegion();
        output.writeRaw("\n");
        output.flush();

        _impl->flushTts();
        _impl->renderInputBox();
        _impl->positionCursorInInputBox();
        output.flush();
    };

    auto running = true;
    while (running)
    {
        // In VAD mode with voice enabled, drain any pending transcriptions
        if (_impl->voiceEnabled && _impl->config.audio.mode == VoiceMode::Vad)
        {
            for (auto const& text: _impl->drainTranscriptions())
                processTranscription(text);
        }

        auto events = _impl->terminal.poll(100);

        if (events.empty())
        {
            // Timeout — redraw input box for voice meter animation
            if (_impl->voiceEnabled && _impl->audioPipeline)
            {
                auto sync = output.syncGuard();
                output.hideCursor();
                _impl->renderInputBox();
                _impl->positionCursorInInputBox();
                output.flush();
            }

            // Re-render log panel if background threads added entries
            if (_impl->logPanelDirty.exchange(false, std::memory_order_relaxed))
            {
                auto sync = output.syncGuard();
                output.hideCursor();
                _impl->renderLogPanel();
                _impl->positionCursorInInputBox();
                output.flush();
            }

            // Check if VAD has transcriptions ready
            if (_impl->voiceEnabled && _impl->config.audio.mode == VoiceMode::Vad
                && _impl->hasTranscriptions())
            {
                for (auto const& text: _impl->drainTranscriptions())
                    processTranscription(text);
            }

            continue;
        }

        for (auto const& event: events)
        {
            // Handle terminal resize
            if (std::holds_alternative<tui::ResizeEvent>(event))
            {
                output.updateDimensions();
                _impl->renderFullScreen();
                continue;
            }

            // Handle mouse events for log panel
            if (auto const* mouse = std::get_if<tui::MouseEvent>(&event))
            {
                if (mouse->type == tui::MouseEvent::Type::Press)
                {
                    if (_impl->logPanel.handleClick(mouse->x, mouse->y, _impl->geo.logStartRow))
                    {
                        auto sync = output.syncGuard();
                        output.hideCursor();
                        _impl->computeGeometry();
                        _impl->renderFullScreen();
                        continue;
                    }
                }
                else if (mouse->type == tui::MouseEvent::Type::ScrollUp && mouse->y >= _impl->geo.logStartRow)
                {
                    _impl->logPanel.scrollUp();
                    auto sync = output.syncGuard();
                    output.hideCursor();
                    _impl->renderLogPanel();
                    _impl->positionCursorInInputBox();
                    output.flush();
                    continue;
                }
                else if (mouse->type == tui::MouseEvent::Type::ScrollDown
                         && mouse->y >= _impl->geo.logStartRow)
                {
                    _impl->logPanel.scrollDown();
                    auto sync = output.syncGuard();
                    output.hideCursor();
                    _impl->renderLogPanel();
                    _impl->positionCursorInInputBox();
                    output.flush();
                    continue;
                }
            }

            // Handle Ctrl+L for log panel toggle
            if (auto const* key = std::get_if<tui::KeyEvent>(&event))
            {
                if (key->codepoint == 'l' && hasModifier(key->modifiers, tui::Modifier::Ctrl))
                {
                    _impl->logPanel.toggle();
                    auto sync = output.syncGuard();
                    output.hideCursor();
                    _impl->computeGeometry();
                    _impl->renderFullScreen();
                    continue;
                }
            }

            auto const action = _impl->inputField.processEvent(event);
            switch (action)
            {
                case tui::InputFieldAction::Submit: {
                    auto line = std::string(_impl->inputField.text());
                    _impl->inputField.addHistory(line);
                    _impl->inputField.clear();

                    // Handle empty Enter with voice enabled in push-to-talk mode
                    if (line.empty() && _impl->voiceEnabled
                        && _impl->config.audio.mode == VoiceMode::PushToTalk)
                    {
                        if (!_impl->recording)
                        {
                            _impl->audioPipeline->startRecording();
                            _impl->recording = true;
                            _impl->logInfo("Recording... Press Enter to stop.");
                        }
                        else
                        {
                            _impl->logInfo("Transcribing...");
                            _impl->audioPipeline->stopRecording();
                            _impl->recording = false;

                            for (auto const& text: _impl->drainTranscriptions())
                                processTranscription(text);
                        }
                        {
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                        }
                        break;
                    }

                    if (line.empty())
                    {
                        auto sync = output.syncGuard();
                        output.hideCursor();
                        _impl->renderInputBox();
                        _impl->positionCursorInInputBox();
                        output.flush();
                        break;
                    }

                    // Handle commands
                    if (line == "/quit" || line == "/exit")
                    {
                        running = false;
                        break;
                    }

                    if (line == "/help")
                    {
                        if (!_impl->conversationStarted)
                            _impl->transitionToConversation();
                        _impl->printHelp();
                        {
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                        }
                        break;
                    }

                    if (line == "/clear")
                    {
                        _impl->session.clear();
                        _impl->logInfo("Conversation cleared");
                        {
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                        }
                        break;
                    }

                    if (line == "/voice")
                    {
                        if (!_impl->audioInitialized)
                        {
                            _impl->logError(
                                "Voice input not available. Configure audio in config.json with a whisper "
                                "model path.");
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                            break;
                        }

                        _impl->voiceEnabled = !_impl->voiceEnabled;

                        if (_impl->voiceEnabled)
                        {
                            auto startResult = _impl->audioPipeline->start();
                            if (!startResult)
                            {
                                _impl->logError(std::format("Failed to start audio pipeline: {}",
                                                            startResult.error().message));
                                _impl->voiceEnabled = false;
                                auto sync = output.syncGuard();
                                output.hideCursor();
                                _impl->renderInputBox();
                                _impl->positionCursorInInputBox();
                                output.flush();
                                break;
                            }

                            auto const* const modeStr =
                                _impl->config.audio.mode == VoiceMode::PushToTalk
                                    ? "Push-to-talk \u2014 press Enter to toggle recording"
                                    : "VAD \u2014 speaking is auto-detected";
                            _impl->logInfo(std::format("Voice input enabled ({})", modeStr));
                        }
                        else
                        {
                            if (_impl->recording)
                            {
                                _impl->audioPipeline->stopRecording();
                                _impl->recording = false;
                            }
                            _impl->audioPipeline->stop();
                            _impl->logInfo("Voice input disabled");
                        }
                        {
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                        }
                        break;
                    }

                    if (line == "/tts")
                    {
                        if (!_impl->ttsSpeaker)
                        {
                            auto ttsResult = _impl->initializeTts();
                            if (!ttsResult)
                            {
                                _impl->logError(
                                    std::format("TTS initialization failed: {}", ttsResult.error().message));
                                auto sync = output.syncGuard();
                                output.hideCursor();
                                _impl->renderInputBox();
                                _impl->positionCursorInInputBox();
                                output.flush();
                                break;
                            }
                            _impl->logInfo("TTS enabled \u2014 agent responses will be spoken aloud");
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                            break;
                        }

                        _impl->ttsEnabled = !_impl->ttsEnabled;

                        if (_impl->ttsEnabled)
                            _impl->logInfo("TTS enabled \u2014 agent responses will be spoken aloud");
                        else
                        {
                            _impl->ttsSpeaker->cancel();
                            _impl->logInfo("TTS disabled");
                        }
                        {
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                        }
                        break;
                    }

                    if (line == "/tools")
                    {
                        if (!_impl->conversationStarted)
                            _impl->transitionToConversation();

                        auto tools = _impl->servers.allTools();
                        if (tools.empty())
                        {
                            _impl->logInfo("No tools available");
                        }
                        else
                        {
                            auto toolsText =
                                std::format("\033[34mInfo:\033[m {} tool(s) available:\n", tools.size());
                            for (auto const& tool: tools)
                                toolsText += std::format("  {} \u2014 {}\n", tool.name, tool.description);
                            toolsText += "\n";
                            _impl->writeToChatArea(toolsText);
                        }
                        {
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                        }
                        break;
                    }

                    if (line.starts_with("/"))
                    {
                        _impl->logError(std::format("Unknown command: {}", line));
                        {
                            auto sync = output.syncGuard();
                            output.hideCursor();
                            _impl->renderInputBox();
                            _impl->positionCursorInInputBox();
                            output.flush();
                        }
                        break;
                    }

                    // Process through agent loop
                    if (!_impl->conversationStarted)
                        _impl->transitionToConversation();

                    _impl->printUserMessage(line);

                    // Set scroll region for streaming output
                    output.setScrollRegion(_impl->geo.chatTop, _impl->geo.chatBottom);
                    output.moveTo(_impl->geo.chatBottom, 1);

                    mdRenderer.beginStream();
                    auto streamCb = [&](std::string_view token) {
                        mdRenderer.feedToken(token);
                        output.flush();
                        _impl->feedTtsToken(token);
                    };

                    auto result = _impl->agent->processMessage(line, streamCb);
                    if (!result)
                        _impl->logError(std::format("{}", result.error()));

                    mdRenderer.endStream();
                    output.resetScrollRegion();
                    output.writeRaw("\n");
                    output.flush();

                    _impl->flushTts();

                    {
                        auto sync = output.syncGuard();
                        output.hideCursor();
                        _impl->renderInputBox();
                        _impl->renderLogPanel();
                        _impl->positionCursorInInputBox();
                        output.flush();
                    }
                    break;
                }

                case tui::InputFieldAction::Abort:
                    _impl->inputField.clear();
                    running = false;
                    break;

                case tui::InputFieldAction::Eof: running = false; break;

                case tui::InputFieldAction::Changed: {
                    auto sync = output.syncGuard();
                    output.hideCursor();
                    _impl->renderInputBox();
                    _impl->positionCursorInInputBox();
                    output.flush();
                    break;
                }

                case tui::InputFieldAction::None: break;
            }

            if (!running)
                break;
        }
    }

    // Shutdown: revert log output to stderr before tearing down the TUI
    log::setCallback(nullptr);

    // Shutdown: leave alt screen and restore cursor
    output.resetScrollRegion();
    output.showCursor();
    output.leaveAltScreen();
    output.flush();

    // Shutdown: stop TTS speaker
    if (_impl->ttsSpeaker)
        _impl->ttsSpeaker->shutdown();

    // Shutdown: stop audio pipeline if active
    if (_impl->voiceEnabled && _impl->audioPipeline)
    {
        if (_impl->recording)
        {
            _impl->audioPipeline->stopRecording();
            _impl->recording = false;
        }
        _impl->audioPipeline->stop();
    }

    _impl->servers.shutdown();
    _impl->terminal.shutdown();
    return 0;
}

} // namespace mychat
