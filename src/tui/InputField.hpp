// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <tui/InputEvent.hpp>

namespace mychat::tui
{

/// @brief Result of processing an input event in InputField.
enum class InputFieldAction : std::uint8_t
{
    Changed, ///< Buffer content or cursor changed, re-render needed.
    Submit,  ///< User pressed Enter.
    Abort,   ///< User pressed Ctrl+C.
    Eof,     ///< User pressed Ctrl+D on an empty line.
    None,    ///< Event not consumed by InputField.
};

/// @brief Pure-model line editor with Emacs keybindings, history, and kill ring.
///
/// Accepts InputEvent objects and updates internal state. No I/O — the caller
/// renders based on text() and cursor(). Operates on grapheme cluster boundaries
/// using libunicode for correct Unicode handling.
class InputField
{
  public:
    /// @brief Processes an input event and returns the resulting action.
    /// @param event The input event to process.
    /// @return The action resulting from the event.
    [[nodiscard]] auto processEvent(InputEvent const& event) -> InputFieldAction;

    /// @brief Returns the current buffer content.
    [[nodiscard]] auto text() const noexcept -> std::string_view;

    /// @brief Returns the cursor position as a byte offset into text().
    [[nodiscard]] auto cursor() const noexcept -> std::size_t;

    /// @brief Sets the prompt string.
    /// @param prompt The prompt to display before user input.
    void setPrompt(std::string_view prompt);

    /// @brief Returns the current prompt string.
    [[nodiscard]] auto prompt() const noexcept -> std::string_view;

    /// @brief Clears the buffer and resets cursor to position 0.
    void clear();

    /// @brief Sets the buffer content programmatically.
    /// @param text New buffer content.
    void setText(std::string_view text);

    /// @brief Adds an entry to the history ring.
    /// @param entry The line to add.
    void addHistory(std::string entry);

    /// @brief Sets the maximum number of history entries to retain.
    /// @param n Maximum history size.
    void setMaxHistory(std::size_t n);

  private:
    std::string _buffer;
    std::size_t _cursor = 0;
    std::string _prompt;

    // History
    std::vector<std::string> _history;
    std::size_t _historyIndex = 0;
    std::string _savedLine;
    std::size_t _maxHistory = 100;

    // Kill ring (Emacs-style)
    std::vector<std::string> _killRing;
    std::size_t _killRingIndex = 0;
    static constexpr std::size_t MaxKillRing = 16;
    bool _lastWasKill = false; ///< Whether the last action was a kill (for kill ring appending).

    /// @brief Dispatches a KeyEvent to the appropriate editing operation.
    [[nodiscard]] auto handleKey(KeyEvent const& key) -> InputFieldAction;

    // Editing operations
    void killToEnd();          ///< Ctrl+K: Kill from cursor to end of line.
    void killToStart();        ///< Ctrl+U: Kill from cursor to start of line.
    void killWord();           ///< Alt+D: Kill word forward.
    void killWordBackward();   ///< Alt+Backspace / Ctrl+W: Kill word backward.
    void yank();               ///< Ctrl+Y: Yank (paste) from kill ring.
    void yankPop();            ///< Alt+Y: Cycle kill ring.
    void deleteChar();         ///< Delete / Ctrl+D: Delete character at cursor.
    void deleteCharBackward(); ///< Backspace: Delete character before cursor.
    void moveToStart();        ///< Ctrl+A / Home: Move cursor to start.
    void moveToEnd();          ///< Ctrl+E / End: Move cursor to end.
    void moveForwardChar();    ///< Ctrl+F / Right: Move cursor forward one grapheme.
    void moveBackwardChar();   ///< Ctrl+B / Left: Move cursor backward one grapheme.
    void moveForwardWord();    ///< Alt+F / Ctrl+Right: Move cursor forward one word.
    void moveBackwardWord();   ///< Alt+B / Ctrl+Left: Move cursor backward one word.
    void historyPrev();        ///< Up / Ctrl+P: Previous history entry.
    void historyNext();        ///< Down / Ctrl+N: Next history entry.
    void transpose();          ///< Ctrl+T: Transpose characters before cursor.

    /// @brief Pushes text onto the kill ring.
    void pushKillRing(std::string text);

    /// @brief Inserts a UTF-8 encoded codepoint at the cursor.
    void insertCodepoint(char32_t cp);

    /// @brief Inserts a UTF-8 string at the cursor.
    void insertText(std::string_view text);

    // Unicode helpers (using libunicode)
    [[nodiscard]] auto nextGraphemeCluster(std::size_t pos) const -> std::size_t;
    [[nodiscard]] auto prevGraphemeCluster(std::size_t pos) const -> std::size_t;
    [[nodiscard]] static auto isWordCharAt(char c) -> bool;
};

} // namespace mychat::tui
