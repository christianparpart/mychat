// SPDX-License-Identifier: Apache-2.0
#include <string>

#include <libunicode/utf8_grapheme_segmenter.h>
#include <tui/InputField.hpp>

namespace mychat::tui
{

namespace
{
    /// @brief Encodes a Unicode codepoint as UTF-8.
    /// @param cp The codepoint to encode.
    /// @return UTF-8 encoded string.
    auto encodeUtf8(char32_t cp) -> std::string
    {
        auto result = std::string {};
        if (cp < 0x80)
        {
            result += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x110000)
        {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return result;
    }

    /// @brief Advances past one UTF-8 codepoint.
    auto nextUtf8(std::string_view s, std::size_t pos) -> std::size_t
    {
        if (pos >= s.size())
            return pos;
        ++pos;
        while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
            ++pos;
        return pos;
    }

    /// @brief Moves back one UTF-8 codepoint.
    auto prevUtf8(std::string_view s, std::size_t pos) -> std::size_t
    {
        if (pos == 0)
            return 0;
        --pos;
        while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
            --pos;
        return pos;
    }
} // namespace

auto InputField::processEvent(InputEvent const& event) -> InputFieldAction
{
    if (auto const* key = std::get_if<KeyEvent>(&event))
        return handleKey(*key);

    if (auto const* paste = std::get_if<PasteEvent>(&event))
    {
        insertText(paste->text);
        _lastWasKill = false;
        return InputFieldAction::Changed;
    }

    return InputFieldAction::None;
}

auto InputField::text() const noexcept -> std::string_view
{
    return _buffer;
}

auto InputField::cursor() const noexcept -> std::size_t
{
    return _cursor;
}

void InputField::setPrompt(std::string_view prompt)
{
    _prompt = std::string(prompt);
}

auto InputField::prompt() const noexcept -> std::string_view
{
    return _prompt;
}

void InputField::clear()
{
    _buffer.clear();
    _cursor = 0;
}

void InputField::setText(std::string_view text)
{
    _buffer = std::string(text);
    _cursor = _buffer.size();
}

void InputField::addHistory(std::string entry)
{
    if (entry.empty())
        return;
    // Avoid consecutive duplicates
    if (!_history.empty() && _history.back() == entry)
        return;
    _history.push_back(std::move(entry));
    if (_history.size() > _maxHistory)
        _history.erase(_history.begin());
    _historyIndex = _history.size();
}

void InputField::setMaxHistory(std::size_t n)
{
    _maxHistory = n;
    while (_history.size() > _maxHistory)
        _history.erase(_history.begin());
}

auto InputField::handleKey(KeyEvent const& key) -> InputFieldAction
{
    auto const ctrl = hasModifier(key.modifiers, Modifier::Ctrl);
    auto const alt = hasModifier(key.modifiers, Modifier::Alt);

    // Special keys
    switch (key.key)
    {
        case KeyCode::Enter: _lastWasKill = false; return InputFieldAction::Submit;
        case KeyCode::Tab:
            // Tab could be expanded in the future; for now insert literal tab
            _lastWasKill = false;
            return InputFieldAction::None;
        case KeyCode::Escape: _lastWasKill = false; return InputFieldAction::None;
        case KeyCode::Backspace:
            if (ctrl || alt)
            {
                killWordBackward();
                return InputFieldAction::Changed;
            }
            deleteCharBackward();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        case KeyCode::Delete:
            deleteChar();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        case KeyCode::Up:
            historyPrev();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        case KeyCode::Down:
            historyNext();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        case KeyCode::Left:
            if (ctrl)
                moveBackwardWord();
            else
                moveBackwardChar();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        case KeyCode::Right:
            if (ctrl)
                moveForwardWord();
            else
                moveForwardChar();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        case KeyCode::Home:
            moveToStart();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        case KeyCode::End:
            moveToEnd();
            _lastWasKill = false;
            return InputFieldAction::Changed;
        default: break;
    }

    // Ctrl+letter combinations
    if (ctrl && key.codepoint != 0)
    {
        switch (key.codepoint)
        {
            case 'a':
                moveToStart();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'e':
                moveToEnd();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'f':
                moveForwardChar();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'b':
                moveBackwardChar();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'p':
                historyPrev();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'n':
                historyNext();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'k': killToEnd(); return InputFieldAction::Changed;
            case 'u': killToStart(); return InputFieldAction::Changed;
            case 'w': killWordBackward(); return InputFieldAction::Changed;
            case 'y':
                yank();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 't':
                transpose();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'd':
                if (_buffer.empty())
                    return InputFieldAction::Eof;
                deleteChar();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'c': return InputFieldAction::Abort;
            default: break;
        }
    }

    // Alt+letter combinations
    if (alt && key.codepoint != 0)
    {
        switch (key.codepoint)
        {
            case 'f':
                moveForwardWord();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'b':
                moveBackwardWord();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'd': killWord(); return InputFieldAction::Changed;
            case 'y':
                yankPop();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            default: break;
        }
    }

    // Printable codepoint
    if (key.codepoint != 0 && !ctrl && !alt && isPrintable(key.key))
    {
        insertCodepoint(key.codepoint);
        _lastWasKill = false;
        return InputFieldAction::Changed;
    }

    return InputFieldAction::None;
}

void InputField::killToEnd()
{
    if (_cursor < _buffer.size())
    {
        auto killed = _buffer.substr(_cursor);
        _buffer.erase(_cursor);
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::killToStart()
{
    if (_cursor > 0)
    {
        auto killed = _buffer.substr(0, _cursor);
        _buffer.erase(0, _cursor);
        _cursor = 0;
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::killWord()
{
    auto const start = _cursor;
    moveForwardWord();
    if (_cursor > start)
    {
        auto killed = _buffer.substr(start, _cursor - start);
        _buffer.erase(start, _cursor - start);
        _cursor = start;
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::killWordBackward()
{
    auto const end = _cursor;
    moveBackwardWord();
    if (_cursor < end)
    {
        auto killed = _buffer.substr(_cursor, end - _cursor);
        _buffer.erase(_cursor, end - _cursor);
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::yank()
{
    if (_killRing.empty())
        return;
    _killRingIndex = _killRing.size() - 1;
    insertText(_killRing[_killRingIndex]);
}

void InputField::yankPop()
{
    if (_killRing.empty())
        return;
    // Remove the previously yanked text and insert the next one in the ring
    if (_killRingIndex < _killRing.size())
    {
        auto const& prev = _killRing[_killRingIndex];
        if (_cursor >= prev.size())
        {
            _buffer.erase(_cursor - prev.size(), prev.size());
            _cursor -= prev.size();
        }
    }
    _killRingIndex = (_killRingIndex == 0) ? _killRing.size() - 1 : _killRingIndex - 1;
    insertText(_killRing[_killRingIndex]);
}

void InputField::deleteChar()
{
    if (_cursor >= _buffer.size())
        return;
    auto const next = nextGraphemeCluster(_cursor);
    _buffer.erase(_cursor, next - _cursor);
}

void InputField::deleteCharBackward()
{
    if (_cursor == 0)
        return;
    auto const prev = prevGraphemeCluster(_cursor);
    _buffer.erase(prev, _cursor - prev);
    _cursor = prev;
}

void InputField::moveToStart()
{
    _cursor = 0;
}

void InputField::moveToEnd()
{
    _cursor = _buffer.size();
}

void InputField::moveForwardChar()
{
    _cursor = nextGraphemeCluster(_cursor);
}

void InputField::moveBackwardChar()
{
    _cursor = prevGraphemeCluster(_cursor);
}

void InputField::moveForwardWord()
{
    auto const size = _buffer.size();
    // Emacs forward-word: skip non-word chars, then skip word chars
    while (_cursor < size && !isWordCharAt(_buffer[_cursor]))
        _cursor = nextUtf8(_buffer, _cursor);
    while (_cursor < size && isWordCharAt(_buffer[_cursor]))
        _cursor = nextUtf8(_buffer, _cursor);
}

void InputField::moveBackwardWord()
{
    // Skip whitespace/non-word characters
    while (_cursor > 0 && !isWordCharAt(_buffer[prevUtf8(_buffer, _cursor)]))
        _cursor = prevUtf8(_buffer, _cursor);
    // Skip word characters
    while (_cursor > 0 && isWordCharAt(_buffer[prevUtf8(_buffer, _cursor)]))
        _cursor = prevUtf8(_buffer, _cursor);
}

void InputField::historyPrev()
{
    if (_history.empty())
        return;
    if (_historyIndex == _history.size())
        _savedLine = _buffer;
    if (_historyIndex > 0)
    {
        --_historyIndex;
        _buffer = _history[_historyIndex];
        _cursor = _buffer.size();
    }
}

void InputField::historyNext()
{
    if (_historyIndex >= _history.size())
        return;
    ++_historyIndex;
    if (_historyIndex == _history.size())
    {
        _buffer = _savedLine;
        _savedLine.clear();
    }
    else
    {
        _buffer = _history[_historyIndex];
    }
    _cursor = _buffer.size();
}

void InputField::transpose()
{
    if (_cursor == 0 || _buffer.size() < 2)
        return;
    // If at end, transpose the two characters before cursor
    auto pos = _cursor;
    if (pos == _buffer.size())
        pos = prevGraphemeCluster(pos);
    auto const prevPos = prevGraphemeCluster(pos);
    auto const nextPos = nextGraphemeCluster(pos);

    auto first = _buffer.substr(prevPos, pos - prevPos);
    auto second = _buffer.substr(pos, nextPos - pos);

    _buffer.replace(prevPos, nextPos - prevPos, second + first);
    _cursor = nextPos;
}

void InputField::pushKillRing(std::string text)
{
    if (text.empty())
        return;
    if (_lastWasKill && !_killRing.empty())
    {
        // Append to the last kill ring entry
        _killRing.back() += text;
    }
    else
    {
        _killRing.push_back(std::move(text));
        if (_killRing.size() > MaxKillRing)
            _killRing.erase(_killRing.begin());
    }
}

void InputField::insertCodepoint(char32_t cp)
{
    auto const utf8 = encodeUtf8(cp);
    _buffer.insert(_cursor, utf8);
    _cursor += utf8.size();
}

void InputField::insertText(std::string_view text)
{
    _buffer.insert(_cursor, text);
    _cursor += text.size();
}

auto InputField::nextGraphemeCluster(std::size_t pos) const -> std::size_t
{
    if (pos >= _buffer.size())
        return pos;

    // Use libunicode utf8_grapheme_segmenter for proper grapheme cluster boundaries.
    // The iterator's _clusterStart pointer tracks byte positions in the original string_view.
    auto const sv = std::string_view(_buffer).substr(pos);
    auto segmenter = unicode::utf8_grapheme_segmenter(sv);
    auto it = segmenter.begin();
    if (it == segmenter.end())
        return nextUtf8(_buffer, pos);

    ++it; // Advance past the first grapheme cluster
    if (it != segmenter.end())
    {
        // The iterator's _clusterStart points into the original sv data
        auto const byteOffset = static_cast<std::size_t>(it._clusterStart - sv.data());
        return pos + byteOffset;
    }

    // The first cluster spans to the end
    return _buffer.size();
}

auto InputField::prevGraphemeCluster(std::size_t pos) const -> std::size_t
{
    if (pos == 0)
        return 0;

    // Segment the text up to pos and find the last cluster boundary.
    auto const sv = std::string_view(_buffer).substr(0, pos);
    auto segmenter = unicode::utf8_grapheme_segmenter(sv);

    auto lastBoundaryOffset = std::size_t { 0 };
    for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
    {
        auto const currentOffset = static_cast<std::size_t>(it._clusterStart - sv.data());
        lastBoundaryOffset = currentOffset;
    }

    return lastBoundaryOffset;
}

auto InputField::isWordCharAt(char c) -> bool
{
    return c != ' ' && c != '\t' && c != '\n' && c != '\r';
}

} // namespace mychat::tui
