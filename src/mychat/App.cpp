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

#include <tui/Box.hpp>
#include <tui/InputField.hpp>
#include <tui/LogPanel.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/Spinner.hpp>
#include <tui/StatusBar.hpp>
#include <tui/Terminal.hpp>
#include <tui/Theme.hpp>

namespace mychat
{

namespace
{
    // Voice meter constants
    constexpr auto MeterBars = std::array { "▁", "▂", "▃", "▅", "▇" };
    constexpr auto MeterBarCount = MeterBars.size();
    constexpr auto FloorDb = -48.0f;

    // Layout constants
    constexpr auto InputBoxMinHeight = 3;   ///< Minimum input box height (1 content line + 2 borders)
    constexpr auto InputBoxMaxHeight = 10;  ///< Maximum input box height before scrolling
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
    int chatTop = 1;      ///< First row of the chat scroll region.
    int chatBottom = 1;   ///< Last row of the chat scroll region.
    int inputRow = 1;     ///< Top row of the input box.
    int inputCol = 1;     ///< Left column of the input box.
    int inputWidth = 64;  ///< Width of the input box (including borders).
    int inputHeight = 3;  ///< Height of the input box (dynamic based on content).
    int logStartRow = 1;  ///< First row of the log panel.
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
    tui::StatusBar statusBar;
    tui::Spinner spinner { tui::SpinnerType::Dots };
    bool conversationStarted = false;
    bool tuiActive = false;
    bool isProcessing = false;
    std::atomic<bool> logPanelDirty = false;
    LayoutGeometry geo;

    // Input scroll state
    int inputScrollOffset = 0;  ///< First visible character position (grapheme index)
    int inputVerticalScrollOffset = 0;  ///< First visible line (for multiline scrolling)

    /// @brief Computes the current input box height based on line count.
    [[nodiscard]] auto computeInputBoxHeight() const -> int
    {
        auto const lines = inputField.lineCount();
        auto const contentHeight = std::clamp(lines, 1, InputBoxMaxHeight - 2);
        return std::max(InputBoxMinHeight, contentHeight + 2);  // +2 for top and bottom borders
    }

    /// @brief Messages queued before TUI is active, replayed into the log panel on TUI start.
    std::vector<tui::LogEntry> pendingLogs;

    explicit Impl(AppConfig cfg): config(std::move(cfg)), session(config.llm.systemPrompt)
    {
        inputField.setMultiline(true);
        // No line limit - box grows to InputBoxMaxHeight then scrolls vertically
    }

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
        auto constexpr StatusBarHeight = 1;

        geo.inputWidth = std::min(InputBoxMaxWidth, cols - 4);
        geo.inputCol = (cols - geo.inputWidth) / 2 + 1;
        geo.inputHeight = computeInputBoxHeight();

        if (conversationStarted)
        {
            // Layout: [chat area] [input box] [log panel] [status bar]
            geo.logStartRow = rows - StatusBarHeight - logHeight + 1;
            geo.inputRow = geo.logStartRow - geo.inputHeight;
            geo.chatTop = 1;
            geo.chatBottom = geo.inputRow - 1;
        }
        else
        {
            // Initial mode: centered input with log and status at bottom
            geo.logStartRow = rows - StatusBarHeight - logHeight + 1;
            geo.inputRow = rows / 2 - geo.inputHeight / 2;
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
        renderStatusBar();
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

    /// @brief Calculates grapheme positions from the input text.
    /// Returns a vector of byte offsets for each grapheme cluster start.
    auto calculateGraphemePositions(std::string_view text) const -> std::vector<std::size_t>
    {
        auto positions = std::vector<std::size_t> {};
        positions.push_back(0);

        for (auto i = std::size_t { 0 }; i < text.size();)
        {
            auto const ch = static_cast<unsigned char>(text[i]);
            auto charLen = std::size_t { 1 };

            // Determine UTF-8 character length
            if ((ch & 0x80) == 0)
                charLen = 1;
            else if ((ch & 0xE0) == 0xC0)
                charLen = 2;
            else if ((ch & 0xF0) == 0xE0)
                charLen = 3;
            else if ((ch & 0xF8) == 0xF0)
                charLen = 4;

            i += charLen;
            if (i <= text.size())
                positions.push_back(i);
        }

        return positions;
    }

    /// @brief Updates the scroll offset to ensure the cursor is visible.
    void updateInputScrollOffset(int availableWidth)
    {
        auto const& text = inputField.text();
        auto const cursorByte = inputField.cursor();
        auto const graphemes = calculateGraphemePositions(text);

        // Find cursor grapheme index
        auto cursorGrapheme = 0;
        for (auto i = std::size_t { 0 }; i < graphemes.size(); ++i)
        {
            if (graphemes[i] >= cursorByte)
            {
                cursorGrapheme = static_cast<int>(i);
                break;
            }
            cursorGrapheme = static_cast<int>(i);
        }

        // Reserve space for scroll indicators
        auto const hasLeftOverflow = inputScrollOffset > 0;
        auto const totalGraphemes = static_cast<int>(graphemes.size()) - 1; // -1 for trailing position
        auto const hasRightOverflow = (totalGraphemes - inputScrollOffset) > availableWidth;

        auto effectiveWidth = availableWidth;
        if (hasLeftOverflow)
            effectiveWidth -= 1;  // Reserve space for left indicator
        if (hasRightOverflow)
            effectiveWidth -= 1;  // Reserve space for right indicator

        effectiveWidth = std::max(1, effectiveWidth);

        // Adjust scroll offset to keep cursor visible
        if (cursorGrapheme < inputScrollOffset)
        {
            // Cursor is before visible area - scroll left
            inputScrollOffset = cursorGrapheme;
        }
        else if (cursorGrapheme >= inputScrollOffset + effectiveWidth)
        {
            // Cursor is after visible area - scroll right
            inputScrollOffset = cursorGrapheme - effectiveWidth + 1;
        }

        // Clamp scroll offset
        inputScrollOffset = std::max(0, inputScrollOffset);
        inputScrollOffset = std::min(inputScrollOffset, std::max(0, totalGraphemes - 1));
    }

    /// @brief Renders the bordered input box at the computed position.
    void renderInputBox()
    {
        auto& out = terminal.output();
        auto const w = geo.inputWidth;
        auto const row = geo.inputRow;
        auto const col = geo.inputCol;

        auto const& theme = tui::currentTheme();

        // Build the box configuration
        auto boxConfig = tui::BoxConfig {
            .row = row,
            .col = col,
            .width = w,
            .height = geo.inputHeight,
            .border = theme.borderStyle,
            .borderStyle = borderStyle(),
            .title = std::nullopt,
            .titleAlign = tui::TitleAlign::Left,
            .titleStyle = {},
            .paddingLeft = 1,
            .paddingRight = 1,
            .paddingTop = 0,
            .paddingBottom = 0,
            .fillBackground = false,
            .backgroundStyle = {},
        };

        auto box = tui::Box(boxConfig);
        box.render(out);

        // Render content inside the box
        auto const innerWidth = box.innerWidth();
        auto const contentRow = box.contentStartRow();
        auto const contentCol = box.contentStartCol();

        // Voice meter in top-right if active
        if (voiceEnabled && audioPipeline)
        {
            out.moveTo(row, col + w - static_cast<int>(MeterBarCount) - 3);
            renderVoiceMeter(audioPipeline->peakLevel());
        }

        // Calculate available width for text
        auto const& text = inputField.text();
        auto const prefixWidth = isProcessing ? 2 : 0;
        auto availableWidth = innerWidth - prefixWidth;

        // Calculate content lines (visible rows in the box)
        auto const contentLines = geo.inputHeight - 2;  // -2 for borders
        auto const totalLines = inputField.lineCount();
        auto const cursorLine = inputField.cursorLine();

        // Update vertical scroll to keep cursor visible
        if (cursorLine < inputVerticalScrollOffset)
            inputVerticalScrollOffset = cursorLine;
        else if (cursorLine >= inputVerticalScrollOffset + contentLines)
            inputVerticalScrollOffset = cursorLine - contentLines + 1;

        // Clamp vertical scroll
        inputVerticalScrollOffset = std::max(0, inputVerticalScrollOffset);
        inputVerticalScrollOffset = std::min(inputVerticalScrollOffset, std::max(0, totalLines - contentLines));

        // Render each visible line
        for (auto lineIdx = 0; lineIdx < contentLines; ++lineIdx)
        {
            auto const actualLine = inputVerticalScrollOffset + lineIdx;
            out.moveTo(contentRow + lineIdx, contentCol);

            // Show spinner only on the cursor line
            auto linePrefixWidth = 0;
            if (isProcessing && actualLine == cursorLine)
            {
                spinner.render(out, theme.textAccent);
                out.writeRaw(" ");
                linePrefixWidth = prefixWidth;
            }
            else if (isProcessing)
            {
                out.writeRaw("  ");  // Space for alignment
                linePrefixWidth = prefixWidth;
            }

            auto lineAvailableWidth = availableWidth - linePrefixWidth;

            if (actualLine >= totalLines)
            {
                // Empty line (beyond buffer content)
                out.writeRaw(std::string(static_cast<std::size_t>(lineAvailableWidth), ' '));
                continue;
            }

            auto lineText = inputField.lineAt(actualLine);

            if (lineText.empty() && text.empty() && actualLine == 0 && !isProcessing)
            {
                // Placeholder on first line when empty
                inputScrollOffset = 0;
                auto placeholder = std::string_view { "Ask anything\u2026" };
                out.write(placeholder, grayStyle());
                auto const placeholderLen = 14;
                auto const pad = std::max(0, lineAvailableWidth - placeholderLen);
                out.writeRaw(std::string(static_cast<std::size_t>(pad), ' '));
                continue;
            }

            if (lineText.empty())
            {
                out.writeRaw(std::string(static_cast<std::size_t>(lineAvailableWidth), ' '));
                continue;
            }

            // Calculate grapheme positions for this line
            auto const graphemes = calculateGraphemePositions(lineText);
            auto const totalGraphemes = static_cast<int>(graphemes.size()) - 1;

            // For the cursor line, handle horizontal scrolling
            auto lineScrollOffset = 0;
            if (actualLine == cursorLine)
            {
                // Use cursor column to determine scroll
                auto const cursorCol = inputField.cursorColumn();
                if (cursorCol < inputScrollOffset)
                    inputScrollOffset = cursorCol;
                else if (cursorCol >= inputScrollOffset + lineAvailableWidth)
                    inputScrollOffset = cursorCol - lineAvailableWidth + 1;

                inputScrollOffset = std::max(0, inputScrollOffset);
                inputScrollOffset = std::min(inputScrollOffset, std::max(0, totalGraphemes - 1));
                lineScrollOffset = inputScrollOffset;
            }

            // Determine overflow indicators
            auto const hasLeftOverflow = lineScrollOffset > 0;
            auto const visibleEnd = lineScrollOffset + lineAvailableWidth;
            auto const hasRightOverflow = visibleEnd < totalGraphemes;

            // Adjust available width for indicators
            auto textWidth = lineAvailableWidth;
            if (hasLeftOverflow)
                textWidth -= 1;
            if (hasRightOverflow)
                textWidth -= 1;

            textWidth = std::max(1, textWidth);

            // Render left overflow indicator
            if (hasLeftOverflow)
            {
                auto indicatorStyle = tui::Style {};
                indicatorStyle.fg = tui::RgbColor { .r = 100, .g = 100, .b = 100 };
                out.write("\u25C0", indicatorStyle);  // ◀
            }

            // Extract visible portion of line
            auto const startByte = (lineScrollOffset < static_cast<int>(graphemes.size()))
                                       ? graphemes[static_cast<std::size_t>(lineScrollOffset)]
                                       : lineText.size();

            auto visibleGraphemeEnd = std::min(lineScrollOffset + textWidth, totalGraphemes);
            auto const endByte = (visibleGraphemeEnd < static_cast<int>(graphemes.size()))
                                     ? graphemes[static_cast<std::size_t>(visibleGraphemeEnd)]
                                     : lineText.size();

            auto visibleText = lineText.substr(startByte, endByte - startByte);
            out.writeRaw(visibleText);

            // Calculate actual display width of visible text
            auto displayedGraphemes = 0;
            for (auto i = startByte; i < endByte;)
            {
                auto const ch = static_cast<unsigned char>(lineText[i]);
                if ((ch & 0xC0) != 0x80)
                    ++displayedGraphemes;
                ++i;
            }

            // Padding
            auto const pad = std::max(0, textWidth - displayedGraphemes);
            out.writeRaw(std::string(static_cast<std::size_t>(pad), ' '));

            // Render right overflow indicator
            if (hasRightOverflow)
            {
                auto indicatorStyle = tui::Style {};
                indicatorStyle.fg = tui::RgbColor { .r = 100, .g = 100, .b = 100 };
                out.write("\u25B6", indicatorStyle);  // ▶
            }
            else if (hasLeftOverflow)
            {
                // Fill the space where right indicator would be
                out.writeRaw(" ");
            }
        }

        // Show vertical scroll indicators when content exceeds visible area
        auto const hasContentAbove = inputVerticalScrollOffset > 0;
        auto const hasContentBelow = (inputVerticalScrollOffset + contentLines) < totalLines;

        if (hasContentAbove || hasContentBelow)
        {
            auto indicatorStyle = tui::Style {};
            indicatorStyle.fg = tui::RgbColor { .r = 100, .g = 100, .b = 100 };

            // ▲ indicator in top-right when content above
            if (hasContentAbove)
            {
                out.moveTo(row, col + w - 2);
                out.write("\u25B2", indicatorStyle);  // ▲
            }

            // ▼ indicator in bottom-right when content below
            if (hasContentBelow)
            {
                out.moveTo(row + geo.inputHeight - 1, col + w - 2);
                out.write("\u25BC", indicatorStyle);  // ▼
            }
        }

        // Show line count in bottom border when multiline
        if (totalLines > 1)
        {
            auto countStr = std::format(" {}/{} ", cursorLine + 1, totalLines);
            auto const countLen = static_cast<int>(countStr.size());
            auto const bottomRow = row + geo.inputHeight - 1;
            auto const countCol = col + w - countLen - 3;  // -3 for potential ▼ indicator

            if (countCol > col + 2)
            {
                out.moveTo(bottomRow, countCol);
                auto countStyle = tui::Style {};
                countStyle.dim = true;
                out.write(countStr, countStyle);
            }
        }
        // Show character count in bottom border when text is non-trivial (single line)
        else if (!text.empty() && text.size() > 20)
        {
            auto const graphemes = calculateGraphemePositions(text);
            auto const totalChars = static_cast<int>(graphemes.size()) - 1;

            auto countStr = std::format(" {} ", totalChars);
            auto const countLen = static_cast<int>(countStr.size());
            auto const bottomRow = row + geo.inputHeight - 1;
            auto const countCol = col + w - countLen - 2;

            if (countCol > col + 2)
            {
                out.moveTo(bottomRow, countCol);
                auto countStyle = tui::Style {};
                countStyle.dim = true;
                out.write(countStr, countStyle);
            }
        }
    }

    /// @brief Positions the terminal cursor inside the input box at the correct column.
    void positionCursorInInputBox()
    {
        auto& out = terminal.output();

        // Calculate cursor's line and column
        auto const cursorLine = inputField.cursorLine();
        auto const cursorColumn = inputField.cursorColumn();

        // Calculate which row in the box the cursor is on
        auto const visibleLine = cursorLine - inputVerticalScrollOffset;
        auto const row = geo.inputRow + 1 + visibleLine;  // +1 for top border

        auto textCol = geo.inputCol + 2;  // after │ and space

        // Account for prefix (spinner) width
        auto const prefixWidth = isProcessing ? 2 : 0;
        textCol += prefixWidth;

        // Account for left overflow indicator (only on cursor line)
        auto const hasLeftOverflow = inputScrollOffset > 0;
        if (hasLeftOverflow)
            textCol += 1;

        // Calculate cursor position relative to scroll offset
        auto const cursorCol = cursorColumn - inputScrollOffset;

        out.moveTo(row, textCol + cursorCol);
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

    /// @brief Renders the status bar at the bottom of the screen.
    void renderStatusBar()
    {
        auto& out = terminal.output();
        auto const cols = out.columns();
        auto const rows = out.rows();

        statusBar.clearHints();
        statusBar.addHint("Ctrl+C", "Quit");
        statusBar.addHint("Shift+Enter", "Newline");
        statusBar.addHint("Ctrl+L", "Logs");
        if (audioInitialized)
            statusBar.addHint("/voice", voiceEnabled ? "Voice On" : "Voice Off");
        if (ttsSpeaker)
            statusBar.addHint("/tts", ttsEnabled ? "TTS On" : "TTS Off");
        statusBar.addHint("/help", "Help");

        statusBar.setStyle(tui::defaultStatusBarStyle());
        statusBar.render(out, rows, cols);
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
                    _impl->inputScrollOffset = 0;
                    _impl->inputVerticalScrollOffset = 0;

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

                    // Mark as processing (for spinner display)
                    _impl->isProcessing = true;

                    // Set scroll region for streaming output
                    output.setScrollRegion(_impl->geo.chatTop, _impl->geo.chatBottom);
                    output.moveTo(_impl->geo.chatBottom, 1);

                    mdRenderer.beginStream();
                    auto streamCb = [&](std::string_view token) {
                        mdRenderer.feedToken(token);
                        output.flush();
                        _impl->feedTtsToken(token);
                        // Tick the spinner during streaming
                        if (_impl->spinner.tick())
                        {
                            output.saveCursor();
                            _impl->renderInputBox();
                            output.restoreCursor();
                            output.flush();
                        }
                    };

                    auto result = _impl->agent->processMessage(line, streamCb);

                    // Mark processing as complete
                    _impl->isProcessing = false;

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
                        _impl->renderStatusBar();
                        _impl->positionCursorInInputBox();
                        output.flush();
                    }
                    break;
                }

                case tui::InputFieldAction::Abort:
                    _impl->inputField.clear();
                    _impl->inputScrollOffset = 0;
                    _impl->inputVerticalScrollOffset = 0;
                    running = false;
                    break;

                case tui::InputFieldAction::Eof: running = false; break;

                case tui::InputFieldAction::Changed: {
                    auto sync = output.syncGuard();
                    output.hideCursor();
                    // Check if input box height changed (line added/removed)
                    auto const newHeight = _impl->computeInputBoxHeight();
                    if (newHeight != _impl->geo.inputHeight)
                    {
                        // Height changed - need full redraw to reposition elements
                        _impl->renderFullScreen();
                    }
                    else
                    {
                        _impl->renderInputBox();
                        _impl->positionCursorInInputBox();
                    }
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
