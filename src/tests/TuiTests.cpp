// SPDX-License-Identifier: Apache-2.0
#include <core/Log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <tui/Box.hpp>
#include <tui/Dialog.hpp>
#include <tui/InputEvent.hpp>
#include <tui/InputField.hpp>
#include <tui/KeyCode.hpp>
#include <tui/List.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/Modifier.hpp>
#include <tui/Sixel.hpp>
#include <tui/Spinner.hpp>
#include <tui/StatusBar.hpp>
#include <tui/TerminalOutput.hpp>
#include <tui/Text.hpp>
#include <tui/Theme.hpp>
#include <tui/VtParser.hpp>

using namespace mychat::tui;

// Forward declare for testing — LogPanel is fully self-contained
#include <tui/LogPanel.hpp>

// =============================================================================
// Helper functions
// =============================================================================

namespace
{
/// @brief Creates a KeyEvent for a printable character with no modifiers.
auto charEvent(char ch) -> InputEvent
{
    auto const cp = static_cast<char32_t>(ch);
    return KeyEvent { .key = keyCodeFromCodepoint(cp), .codepoint = cp };
}

/// @brief Creates a KeyEvent for a special key.
auto keyEvent(KeyCode key, Modifier mods = Modifier::None) -> InputEvent
{
    return KeyEvent { .key = key, .modifiers = mods };
}

/// @brief Creates a KeyEvent for Ctrl+letter.
auto ctrlEvent(char letter) -> InputEvent
{
    auto const cp = static_cast<char32_t>(letter);
    return KeyEvent { .key = keyCodeFromCodepoint(cp), .modifiers = Modifier::Ctrl, .codepoint = cp };
}

/// @brief Creates a KeyEvent for Alt+letter.
auto altEvent(char letter) -> InputEvent
{
    auto const cp = static_cast<char32_t>(letter);
    return KeyEvent { .key = keyCodeFromCodepoint(cp), .modifiers = Modifier::Alt, .codepoint = cp };
}

/// @brief Extracts a KeyEvent from an InputEvent, or fails the test.
auto getKey(InputEvent const& event) -> KeyEvent const&
{
    REQUIRE(std::holds_alternative<KeyEvent>(event));
    return std::get<KeyEvent>(event);
}

/// @brief Extracts a MouseEvent from an InputEvent, or fails the test.
auto getMouse(InputEvent const& event) -> MouseEvent const&
{
    REQUIRE(std::holds_alternative<MouseEvent>(event));
    return std::get<MouseEvent>(event);
}

/// @brief Extracts a PasteEvent from an InputEvent, or fails the test.
auto getPaste(InputEvent const& event) -> PasteEvent const&
{
    REQUIRE(std::holds_alternative<PasteEvent>(event));
    return std::get<PasteEvent>(event);
}

/// @brief Feeds an event into an InputField, discarding the result.
void feed(InputField& field, InputEvent const& event)
{
    static_cast<void>(field.processEvent(event));
}

/// @brief Types a string into an InputField character by character.
void type(InputField& field, std::string_view text)
{
    for (auto ch: text)
        feed(field, charEvent(ch));
}
} // namespace

// =============================================================================
// Modifier tests
// =============================================================================

TEST_CASE("Modifier: bitmask operations", "[tui][modifier]")
{
    SECTION("None is zero")
    {
        CHECK(Modifier::None == static_cast<Modifier>(0));
    }

    SECTION("OR combines flags")
    {
        auto const mods = Modifier::Shift | Modifier::Ctrl;
        CHECK(hasModifier(mods, Modifier::Shift));
        CHECK(hasModifier(mods, Modifier::Ctrl));
        CHECK_FALSE(hasModifier(mods, Modifier::Alt));
    }

    SECTION("AND tests flags")
    {
        auto const mods = Modifier::Alt | Modifier::Super;
        CHECK((mods & Modifier::Alt) != Modifier::None);
        CHECK((mods & Modifier::Shift) == Modifier::None);
    }
}

// =============================================================================
// KeyCode tests
// =============================================================================

TEST_CASE("KeyCode: printable detection", "[tui][keycode]")
{
    CHECK(isPrintable(keyCodeFromCodepoint('a')));
    CHECK(isPrintable(keyCodeFromCodepoint(' ')));
    CHECK(isPrintable(keyCodeFromCodepoint('~')));
    CHECK_FALSE(isPrintable(KeyCode::Enter));
    CHECK_FALSE(isPrintable(KeyCode::F1));
}

TEST_CASE("KeyCode: codepoint round-trip", "[tui][keycode]")
{
    auto const cp = char32_t { 0x00E9 }; // é
    auto const key = keyCodeFromCodepoint(cp);
    CHECK(codepointFromKeyCode(key) == cp);
}

// =============================================================================
// VtParser tests
// =============================================================================

TEST_CASE("VtParser: ASCII printable", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("a");
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.codepoint == 'a');
    CHECK(key.modifiers == Modifier::None);
}

TEST_CASE("VtParser: multiple ASCII characters", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("hello");
    REQUIRE(events.size() == 5);
    CHECK(getKey(events[0]).codepoint == 'h');
    CHECK(getKey(events[1]).codepoint == 'e');
    CHECK(getKey(events[4]).codepoint == 'o');
}

TEST_CASE("VtParser: UTF-8 two-byte", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\xC3\xA9"); // é (U+00E9)
    REQUIRE(events.size() == 1);
    CHECK(getKey(events[0]).codepoint == 0x00E9);
}

TEST_CASE("VtParser: UTF-8 three-byte", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\xE4\xB8\xAD"); // 中 (U+4E2D)
    REQUIRE(events.size() == 1);
    CHECK(getKey(events[0]).codepoint == 0x4E2D);
}

TEST_CASE("VtParser: UTF-8 four-byte emoji", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\xF0\x9F\x98\x80"); // 😀 (U+1F600)
    REQUIRE(events.size() == 1);
    CHECK(getKey(events[0]).codepoint == 0x1F600);
}

TEST_CASE("VtParser: Enter key", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\r");
    REQUIRE(events.size() == 1);
    CHECK(getKey(events[0]).key == KeyCode::Enter);
}

TEST_CASE("VtParser: Tab key", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\t");
    REQUIRE(events.size() == 1);
    CHECK(getKey(events[0]).key == KeyCode::Tab);
}

TEST_CASE("VtParser: Backspace (0x7F)", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\x7F");
    REQUIRE(events.size() == 1);
    CHECK(getKey(events[0]).key == KeyCode::Backspace);
}

TEST_CASE("VtParser: Ctrl+C", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\x03"); // Ctrl+C = 0x03
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.codepoint == 'c');
    CHECK(hasModifier(key.modifiers, Modifier::Ctrl));
}

TEST_CASE("VtParser: CSI cursor keys", "[tui][vtparser]")
{
    auto parser = VtParser {};

    SECTION("Up")
    {
        auto events = parser.feed("\033[A");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::Up);
    }

    SECTION("Down")
    {
        auto events = parser.feed("\033[B");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::Down);
    }

    SECTION("Right")
    {
        auto events = parser.feed("\033[C");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::Right);
    }

    SECTION("Left")
    {
        auto events = parser.feed("\033[D");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::Left);
    }

    SECTION("Home")
    {
        auto events = parser.feed("\033[H");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::Home);
    }

    SECTION("End")
    {
        auto events = parser.feed("\033[F");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::End);
    }
}

TEST_CASE("VtParser: CSI Delete key", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[3~");
    REQUIRE(events.size() == 1);
    CHECK(getKey(events[0]).key == KeyCode::Delete);
}

TEST_CASE("VtParser: CSI function keys", "[tui][vtparser]")
{
    auto parser = VtParser {};

    SECTION("F5")
    {
        auto events = parser.feed("\033[15~");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::F5);
    }

    SECTION("F12")
    {
        auto events = parser.feed("\033[24~");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::F12);
    }
}

TEST_CASE("VtParser: CSI with modifiers (Shift+Right)", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[1;2C"); // 2 = Shift+1
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.key == KeyCode::Right);
    CHECK(hasModifier(key.modifiers, Modifier::Shift));
}

TEST_CASE("VtParser: CSI with Ctrl modifier", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[1;5C"); // 5 = Ctrl+1
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.key == KeyCode::Right);
    CHECK(hasModifier(key.modifiers, Modifier::Ctrl));
}

TEST_CASE("VtParser: CSIu (Kitty) printable", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[97;1u"); // 'a' with no modifiers
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.codepoint == 'a');
}

TEST_CASE("VtParser: CSIu (Kitty) Ctrl+a", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[97;5u"); // 97='a', 5=Ctrl+1
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.codepoint == 'a');
    CHECK(hasModifier(key.modifiers, Modifier::Ctrl));
}

TEST_CASE("VtParser: CSIu (Kitty) Shift+Enter", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[13;2u"); // 13=Enter, 2=Shift+1
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.key == KeyCode::Enter);
    CHECK(hasModifier(key.modifiers, Modifier::Shift));
}

TEST_CASE("VtParser: Alt+Enter (ESC CR)", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033\r"); // ESC followed by CR
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.key == KeyCode::Enter);
    CHECK(hasModifier(key.modifiers, Modifier::Alt));
}

TEST_CASE("VtParser: Alt+Enter (ESC LF)", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033\n"); // ESC followed by LF
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.key == KeyCode::Enter);
    CHECK(hasModifier(key.modifiers, Modifier::Alt));
}

TEST_CASE("VtParser: SS3 sequences", "[tui][vtparser]")
{
    auto parser = VtParser {};

    SECTION("SS3 Up")
    {
        auto events = parser.feed("\033OA");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::Up);
    }

    SECTION("SS3 F1")
    {
        auto events = parser.feed("\033OP");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::F1);
    }

    SECTION("SS3 Home")
    {
        auto events = parser.feed("\033OH");
        REQUIRE(events.size() == 1);
        CHECK(getKey(events[0]).key == KeyCode::Home);
    }
}

TEST_CASE("VtParser: SGR mouse press", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[<0;10;20M"); // Left button press at (10,20)
    REQUIRE(events.size() == 1);
    auto const& mouse = getMouse(events[0]);
    CHECK(mouse.type == MouseEvent::Type::Press);
    CHECK(mouse.button == 0);
    CHECK(mouse.x == 10);
    CHECK(mouse.y == 20);
}

TEST_CASE("VtParser: SGR mouse release", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[<0;10;20m"); // Left button release at (10,20)
    REQUIRE(events.size() == 1);
    auto const& mouse = getMouse(events[0]);
    CHECK(mouse.type == MouseEvent::Type::Release);
}

TEST_CASE("VtParser: SGR mouse scroll up", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[<64;5;10M"); // Scroll up
    REQUIRE(events.size() == 1);
    auto const& mouse = getMouse(events[0]);
    CHECK(mouse.type == MouseEvent::Type::ScrollUp);
}

TEST_CASE("VtParser: SGR mouse scroll down", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[<65;5;10M"); // Scroll down
    REQUIRE(events.size() == 1);
    auto const& mouse = getMouse(events[0]);
    CHECK(mouse.type == MouseEvent::Type::ScrollDown);
}

TEST_CASE("VtParser: bracketed paste", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[200~Hello, world!\033[201~");
    REQUIRE(events.size() == 1);
    auto const& paste = getPaste(events[0]);
    CHECK(paste.text == "Hello, world!");
}

TEST_CASE("VtParser: bracketed paste with newlines", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[200~line1\nline2\nline3\033[201~");
    REQUIRE(events.size() == 1);
    CHECK(getPaste(events[0]).text == "line1\nline2\nline3");
}

TEST_CASE("VtParser: bare ESC timeout", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033");
    CHECK(events.empty()); // ESC is ambiguous, waiting for more data

    auto resolved = parser.timeout();
    REQUIRE(resolved.size() == 1);
    CHECK(getKey(resolved[0]).key == KeyCode::Escape);
}

TEST_CASE("VtParser: Alt+letter", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033f"); // Alt+f
    REQUIRE(events.size() == 1);
    auto const& key = getKey(events[0]);
    CHECK(key.codepoint == 'f');
    CHECK(hasModifier(key.modifiers, Modifier::Alt));
}

TEST_CASE("VtParser: private marker sequences ignored", "[tui][vtparser]")
{
    auto parser = VtParser {};
    auto events = parser.feed("\033[>1u"); // Kitty protocol response — should be ignored
    CHECK(events.empty());
}

TEST_CASE("VtParser: incremental feeding", "[tui][vtparser]")
{
    auto parser = VtParser {};

    // Feed CSI sequence in parts
    auto e1 = parser.feed("\033");
    CHECK(e1.empty());

    auto e2 = parser.feed("[");
    CHECK(e2.empty());

    auto e3 = parser.feed("A");
    REQUIRE(e3.size() == 1);
    CHECK(getKey(e3[0]).key == KeyCode::Up);
}

// =============================================================================
// InputField tests
// =============================================================================

TEST_CASE("InputField: initial state", "[tui][inputfield]")
{
    auto field = InputField {};
    CHECK(field.text().empty());
    CHECK(field.cursor() == 0);
    CHECK(field.prompt().empty());
}

TEST_CASE("InputField: insert characters", "[tui][inputfield]")
{
    auto field = InputField {};
    CHECK(field.processEvent(charEvent('h')) == InputFieldAction::Changed);
    CHECK(field.processEvent(charEvent('i')) == InputFieldAction::Changed);
    CHECK(field.text() == "hi");
    CHECK(field.cursor() == 2);
}

TEST_CASE("InputField: Enter submits", "[tui][inputfield]")
{
    auto field = InputField {};
    feed(field, charEvent('a'));
    CHECK(field.processEvent(keyEvent(KeyCode::Enter)) == InputFieldAction::Submit);
}

TEST_CASE("InputField: Ctrl+C aborts", "[tui][inputfield]")
{
    auto field = InputField {};
    feed(field, charEvent('a'));
    CHECK(field.processEvent(ctrlEvent('c')) == InputFieldAction::Abort);
}

TEST_CASE("InputField: Ctrl+D on empty is Eof", "[tui][inputfield]")
{
    auto field = InputField {};
    CHECK(field.processEvent(ctrlEvent('d')) == InputFieldAction::Eof);
}

TEST_CASE("InputField: Ctrl+D on non-empty deletes char", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, keyEvent(KeyCode::Home));
    CHECK(field.processEvent(ctrlEvent('d')) == InputFieldAction::Changed);
    CHECK(field.text() == "b");
}

TEST_CASE("InputField: Backspace", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, keyEvent(KeyCode::Backspace));
    CHECK(field.text() == "a");
    CHECK(field.cursor() == 1);
}

TEST_CASE("InputField: Delete key", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, keyEvent(KeyCode::Home));
    feed(field, keyEvent(KeyCode::Delete));
    CHECK(field.text() == "b");
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField: Ctrl+A moves to start", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, ctrlEvent('a'));
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField: Ctrl+E moves to end", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, keyEvent(KeyCode::Home));
    feed(field, ctrlEvent('e'));
    CHECK(field.cursor() == 2);
}

TEST_CASE("InputField: Ctrl+K kills to end", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "abc");
    feed(field, keyEvent(KeyCode::Home));
    feed(field, ctrlEvent('k'));
    CHECK(field.text().empty());
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField: Ctrl+U kills to start", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "abc");
    feed(field, ctrlEvent('u'));
    CHECK(field.text().empty());
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField: Ctrl+Y yanks killed text", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "abc");
    feed(field, ctrlEvent('u')); // Kill "abc"
    feed(field, charEvent('x'));
    feed(field, ctrlEvent('y')); // Yank "abc"
    CHECK(field.text() == "xabc");
}

TEST_CASE("InputField: Ctrl+W kills word backward", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "hello world");
    feed(field, ctrlEvent('w'));
    CHECK(field.text() == "hello ");
}

TEST_CASE("InputField: cursor movement with arrows", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, keyEvent(KeyCode::Left));
    CHECK(field.cursor() == 1);
    feed(field, keyEvent(KeyCode::Left));
    CHECK(field.cursor() == 0);
    feed(field, keyEvent(KeyCode::Right));
    CHECK(field.cursor() == 1);
}

TEST_CASE("InputField: Home and End keys", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, keyEvent(KeyCode::Home));
    CHECK(field.cursor() == 0);
    feed(field, keyEvent(KeyCode::End));
    CHECK(field.cursor() == 2);
}

TEST_CASE("InputField: word movement Alt+F/B", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "hello world");
    feed(field, keyEvent(KeyCode::Home));

    feed(field, altEvent('f')); // Forward word
    CHECK(field.cursor() == 5); // After "hello"

    feed(field, altEvent('b')); // Backward word
    CHECK(field.cursor() == 0); // Back to "hello"
}

TEST_CASE("InputField: word movement Ctrl+Left/Right", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "hello world");

    feed(field, keyEvent(KeyCode::Left, Modifier::Ctrl)); // Back one word
    CHECK(field.cursor() == 6);                           // Before "world"
}

TEST_CASE("InputField: history navigation", "[tui][inputfield]")
{
    auto field = InputField {};
    field.addHistory("first");
    field.addHistory("second");

    feed(field, keyEvent(KeyCode::Up));
    CHECK(field.text() == "second");

    feed(field, keyEvent(KeyCode::Up));
    CHECK(field.text() == "first");

    feed(field, keyEvent(KeyCode::Down));
    CHECK(field.text() == "second");

    feed(field, keyEvent(KeyCode::Down));
    CHECK(field.text().empty()); // Back to current line
}

TEST_CASE("InputField: history preserves current line", "[tui][inputfield]")
{
    auto field = InputField {};
    field.addHistory("old");

    type(field, "new");

    feed(field, keyEvent(KeyCode::Up));
    CHECK(field.text() == "old");

    feed(field, keyEvent(KeyCode::Down));
    CHECK(field.text() == "new"); // Restored
}

TEST_CASE("InputField: history dedup", "[tui][inputfield]")
{
    auto field = InputField {};
    field.addHistory("same");
    field.addHistory("same");

    // Only one entry should exist
    feed(field, keyEvent(KeyCode::Up));
    CHECK(field.text() == "same");
    feed(field, keyEvent(KeyCode::Up));
    CHECK(field.text() == "same"); // Still same, no second entry
}

TEST_CASE("InputField: Ctrl+T transpose", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ab");
    feed(field, ctrlEvent('t'));
    CHECK(field.text() == "ba");
}

TEST_CASE("InputField: paste event", "[tui][inputfield]")
{
    auto field = InputField {};
    feed(field, charEvent('x'));
    auto action = field.processEvent(PasteEvent { .text = "pasted" });
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.text() == "xpasted");
}

TEST_CASE("InputField: clear and setText", "[tui][inputfield]")
{
    auto field = InputField {};
    feed(field, charEvent('a'));
    field.clear();
    CHECK(field.text().empty());
    CHECK(field.cursor() == 0);

    field.setText("hello");
    CHECK(field.text() == "hello");
    CHECK(field.cursor() == 5);
}

TEST_CASE("InputField: prompt get/set", "[tui][inputfield]")
{
    auto field = InputField {};
    CHECK(field.prompt().empty());
    field.setPrompt(">>> ");
    CHECK(field.prompt() == ">>> ");
}

TEST_CASE("InputField: Shift+letter capitalizes (Kitty protocol)", "[tui][inputfield]")
{
    auto field = InputField {};

    // Simulate Kitty keyboard protocol: lowercase codepoint + Shift modifier
    // Kitty sends the base key code (lowercase) with Shift modifier
    auto shiftA = KeyEvent {
        .key = keyCodeFromCodepoint('a'),
        .modifiers = Modifier::Shift,
        .codepoint = 'a',
    };
    (void) field.processEvent(shiftA);
    CHECK(field.text() == "A");

    auto shiftZ = KeyEvent {
        .key = keyCodeFromCodepoint('z'),
        .modifiers = Modifier::Shift,
        .codepoint = 'z',
    };
    (void) field.processEvent(shiftZ);
    CHECK(field.text() == "AZ");

    // Regular lowercase should still work
    (void) field.processEvent(charEvent('b'));
    CHECK(field.text() == "AZb");
}

TEST_CASE("InputField: Alt+D kills word forward", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "hello world");
    feed(field, keyEvent(KeyCode::Home));
    feed(field, altEvent('d'));
    CHECK(field.text() == " world");
}

TEST_CASE("InputField: insert in middle", "[tui][inputfield]")
{
    auto field = InputField {};
    type(field, "ac");
    feed(field, keyEvent(KeyCode::Left));
    feed(field, charEvent('b'));
    CHECK(field.text() == "abc");
    CHECK(field.cursor() == 2);
}

// =============================================================================
// InputField multiline tests
// =============================================================================

TEST_CASE("InputField: multiline mode disabled by default", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    CHECK_FALSE(field.isMultiline());
    CHECK(field.lineCount() == 1);
}

TEST_CASE("InputField: enable multiline mode", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    CHECK(field.isMultiline());
}

TEST_CASE("InputField: Shift+Enter inserts newline in multiline mode", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    type(field, "line1");

    // Shift+Enter should insert newline
    auto shiftEnter = KeyEvent {
        .key = KeyCode::Enter,
        .modifiers = Modifier::Shift,
        .codepoint = 0,
    };
    auto action = field.processEvent(shiftEnter);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.text() == "line1\n");
    CHECK(field.lineCount() == 2);

    type(field, "line2");
    CHECK(field.text() == "line1\nline2");
    CHECK(field.lineCount() == 2);
}

TEST_CASE("InputField: Shift+Enter submits in single-line mode", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    // Multiline disabled (default)
    type(field, "text");

    auto shiftEnter = KeyEvent {
        .key = KeyCode::Enter,
        .modifiers = Modifier::Shift,
        .codepoint = 0,
    };
    // In single-line mode, Shift+Enter behaves like Enter (submit)
    auto action = field.processEvent(shiftEnter);
    CHECK(action == InputFieldAction::Submit);
}

TEST_CASE("InputField: Alt+Enter inserts newline in multiline mode", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    type(field, "line1");

    // Alt+Enter should insert newline (works in all terminals)
    auto altEnter = KeyEvent {
        .key = KeyCode::Enter,
        .modifiers = Modifier::Alt,
        .codepoint = 0,
    };
    auto action = field.processEvent(altEnter);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.text() == "line1\n");
    CHECK(field.lineCount() == 2);
}

TEST_CASE("InputField: lineAt returns correct content", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("first\nsecond\nthird");

    CHECK(field.lineCount() == 3);
    CHECK(field.lineAt(0) == "first");
    CHECK(field.lineAt(1) == "second");
    CHECK(field.lineAt(2) == "third");
    CHECK(field.lineAt(3) == "");  // Out of range
    CHECK(field.lineAt(-1) == ""); // Negative
}

TEST_CASE("InputField: cursorLine and cursorColumn", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("abc\nde\nfghij");

    // Cursor at end: line 2, column 5
    CHECK(field.cursorLine() == 2);
    CHECK(field.cursorColumn() == 5);

    // Move to start
    feed(field, ctrlEvent('a'));  // In multiline, goes to line start
    CHECK(field.cursorLine() == 2);
    CHECK(field.cursorColumn() == 0);
}

TEST_CASE("InputField: moveUp in multiline mode", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("line1\nline2");
    // Cursor at end of line2

    feed(field, keyEvent(KeyCode::Up));
    CHECK(field.cursorLine() == 0);
    // Column should try to maintain position (5 in line1)
    CHECK(field.cursorColumn() == 5);
}

TEST_CASE("InputField: moveDown in multiline mode", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("line1\nline2");

    // Move to start
    feed(field, keyEvent(KeyCode::Home));
    feed(field, keyEvent(KeyCode::Home));  // Ensure at buffer start in case Ctrl wasn't held

    // Actually use Ctrl+Home for buffer start if needed
    field.setText("line1\nline2");
    // Position cursor at start of line1
    for (int i = 0; i < 11; ++i)
        feed(field, keyEvent(KeyCode::Left));

    CHECK(field.cursorLine() == 0);

    feed(field, keyEvent(KeyCode::Down));
    CHECK(field.cursorLine() == 1);
}

TEST_CASE("InputField: Up on first line triggers history in multiline mode", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.addHistory("previous");
    field.setText("current");

    // Move to first line, first column
    feed(field, keyEvent(KeyCode::Home));
    CHECK(field.cursorLine() == 0);

    // Up should trigger history since we're on first line
    feed(field, keyEvent(KeyCode::Up));
    CHECK(field.text() == "previous");
}

TEST_CASE("InputField: maxLines limits newline insertion", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setMaxLines(2);
    CHECK(field.maxLines() == 2);

    type(field, "line1");
    auto shiftEnter = KeyEvent {
        .key = KeyCode::Enter,
        .modifiers = Modifier::Shift,
        .codepoint = 0,
    };
    (void) field.processEvent(shiftEnter);
    type(field, "line2");
    CHECK(field.lineCount() == 2);

    // Try to add another line - should be blocked
    (void) field.processEvent(shiftEnter);
    CHECK(field.lineCount() == 2);  // Still 2, not 3
}

TEST_CASE("InputField: Home goes to line start in multiline", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("line1\nline2");
    // Cursor at end of line2

    feed(field, keyEvent(KeyCode::Home));
    CHECK(field.cursorLine() == 1);  // Still on line2
    CHECK(field.cursorColumn() == 0);  // At start of line
}

TEST_CASE("InputField: Ctrl+Home goes to buffer start in multiline", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("line1\nline2");
    // Cursor at end of line2

    feed(field, keyEvent(KeyCode::Home, Modifier::Ctrl));
    CHECK(field.cursorLine() == 0);
    CHECK(field.cursorColumn() == 0);
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField: End goes to line end in multiline", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("line1\nline2");

    // Move to start of buffer
    feed(field, keyEvent(KeyCode::Home, Modifier::Ctrl));
    CHECK(field.cursorColumn() == 0);

    feed(field, keyEvent(KeyCode::End));
    CHECK(field.cursorLine() == 0);  // Still on line1
    CHECK(field.cursorColumn() == 5);  // At end of "line1"
}

TEST_CASE("InputField: Ctrl+End goes to buffer end in multiline", "[tui][inputfield][multiline]")
{
    auto field = InputField {};
    field.setMultiline(true);
    field.setText("line1\nline2");

    // Move to start
    feed(field, keyEvent(KeyCode::Home, Modifier::Ctrl));

    feed(field, keyEvent(KeyCode::End, Modifier::Ctrl));
    CHECK(field.cursorLine() == 1);
    CHECK(field.cursorColumn() == 5);
    CHECK(field.cursor() == field.text().size());
}

// =============================================================================
// TerminalOutput tests (non-interactive, buffer inspection)
// =============================================================================

TEST_CASE("TerminalOutput: Style SGR generation", "[tui][output]")
{
    auto output = TerminalOutput {};
    static_cast<void>(output.initialize());

    auto style = Style {};
    style.bold = true;
    style.fg = RgbColor { .r = 255, .g = 0, .b = 0 };

    output.write("Hello", style);
    output.writeRaw(" ");
    output.write("World");
    // Verify no crash (can't easily capture stdout in unit tests)
}

TEST_CASE("TerminalOutput: dimensions default", "[tui][output]")
{
    auto output = TerminalOutput {};
    CHECK(output.columns() > 0);
    CHECK(output.rows() > 0);
}

// =============================================================================
// MarkdownRenderer tests
// =============================================================================

namespace
{
struct MarkdownTestHelper
{
    TerminalOutput output;
    MarkdownRenderer renderer;

    MarkdownTestHelper(): output(), renderer(output) { static_cast<void>(output.initialize()); }
};
} // namespace

TEST_CASE("MarkdownRenderer: heading detection", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("# Heading 1\n## Heading 2\n### Heading 3\n");
}

TEST_CASE("MarkdownRenderer: code block", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("```python\nprint('hello')\n```\n");
}

TEST_CASE("MarkdownRenderer: inline formatting", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("This is **bold** and *italic* and `code`.\n");
}

TEST_CASE("MarkdownRenderer: list items", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("- Item 1\n- Item 2\n1. Ordered 1\n2. Ordered 2\n");
}

TEST_CASE("MarkdownRenderer: blockquote", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("> This is a quote\n> Second line\n");
}

TEST_CASE("MarkdownRenderer: link", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("Check [this link](https://example.com) out.\n");
}

TEST_CASE("MarkdownRenderer: think block", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("<think>\nI'm thinking...\n</think>\nResult here.\n");
}

TEST_CASE("MarkdownRenderer: streaming basic", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.beginStream();
    helper.renderer.feedToken("Hello ");
    helper.renderer.feedToken("world\n");
    helper.renderer.endStream();
}

TEST_CASE("MarkdownRenderer: streaming code block", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.beginStream();
    helper.renderer.feedToken("```\n");
    helper.renderer.feedToken("code here\n");
    helper.renderer.feedToken("```\n");
    helper.renderer.endStream();
}

TEST_CASE("MarkdownRenderer: empty input", "[tui][markdown]")
{
    auto helper = MarkdownTestHelper {};
    helper.renderer.render("");
}

TEST_CASE("MarkdownRenderer: default theme is valid", "[tui][markdown]")
{
    auto const theme = MarkdownRenderer::defaultTheme();
    CHECK(theme.heading1.bold);
    CHECK(theme.heading2.underline);
    CHECK(theme.codeBlock.dim);
    CHECK(theme.italic.italic);
}

// =============================================================================
// Sixel tests
// =============================================================================

TEST_CASE("Sixel: encode small image", "[tui][sixel]")
{
    auto const pixels = std::vector<std::uint8_t> {
        255, 0,   0,   255, // red
        0,   255, 0,   255, // green
        0,   0,   255, 255, // blue
        255, 255, 255, 255, // white
    };
    auto const image = ImageData {
        .pixels = std::span<const std::uint8_t>(pixels),
        .width = 2,
        .height = 2,
    };

    auto const result = encodeSixel(image, 4);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->empty());
    CHECK(result->find('#') != std::string::npos);
}

TEST_CASE("Sixel: invalid dimensions", "[tui][sixel]")
{
    auto const pixels = std::vector<std::uint8_t> {};
    auto const image = ImageData {
        .pixels = std::span<const std::uint8_t>(pixels),
        .width = 0,
        .height = 0,
    };
    auto const result = encodeSixel(image);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Sixel: insufficient pixel data", "[tui][sixel]")
{
    auto const pixels = std::vector<std::uint8_t> { 0, 0, 0, 255 }; // Only 1 pixel
    auto const image = ImageData {
        .pixels = std::span<const std::uint8_t>(pixels),
        .width = 2,
        .height = 2, // Expects 4 pixels
    };
    auto const result = encodeSixel(image);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Sixel: single color image", "[tui][sixel]")
{
    auto const pixels = std::vector<std::uint8_t> {
        0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
    };
    auto const image = ImageData {
        .pixels = std::span<const std::uint8_t>(pixels),
        .width = 3,
        .height = 1,
    };
    auto const result = encodeSixel(image, 1);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->empty());
}

// =============================================================================
// LogPanel tests
// =============================================================================

TEST_CASE("LogPanel: initial state", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    CHECK(panel.entryCount() == 0);
    CHECK_FALSE(panel.isExpanded());
    CHECK(panel.totalHeight() == 1); // Just header row when collapsed
}

TEST_CASE("LogPanel: addLog increases count", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    panel.addLog(LogLevel::Info, "test message");
    CHECK(panel.entryCount() == 1);
    panel.addLog(LogLevel::Error, "error message");
    CHECK(panel.entryCount() == 2);
}

TEST_CASE("LogPanel: toggle expands and collapses", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    panel.addLog(LogLevel::Info, "msg1");
    panel.addLog(LogLevel::Warning, "msg2");

    CHECK_FALSE(panel.isExpanded());
    CHECK(panel.totalHeight() == 1);

    panel.toggle();
    CHECK(panel.isExpanded());
    CHECK(panel.totalHeight() == 3); // 1 header + 2 entries

    panel.toggle();
    CHECK_FALSE(panel.isExpanded());
    CHECK(panel.totalHeight() == 1);
}

TEST_CASE("LogPanel: totalHeight caps at MaxVisibleExpanded", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    for (auto i = 0; i < 20; ++i)
        panel.addLog(LogLevel::Info, std::format("message {}", i));

    CHECK(panel.entryCount() == 20);

    panel.toggle();
    // 1 header + min(20, MaxVisibleExpanded=7) = 8
    CHECK(panel.totalHeight() == 1 + LogPanel::MaxVisibleExpanded);
}

TEST_CASE("LogPanel: MaxEntries eviction", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    for (auto i = 0; i < LogPanel::MaxEntries + 10; ++i)
        panel.addLog(LogLevel::Info, std::format("msg {}", i));

    CHECK(panel.entryCount() == static_cast<std::size_t>(LogPanel::MaxEntries));
}

TEST_CASE("LogPanel: handleClick on header row toggles", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    auto const panelStart = 20;

    CHECK_FALSE(panel.isExpanded());
    CHECK(panel.handleClick(5, panelStart, panelStart));
    CHECK(panel.isExpanded());
    CHECK(panel.handleClick(5, panelStart, panelStart));
    CHECK_FALSE(panel.isExpanded());
}

TEST_CASE("LogPanel: handleClick off header row returns false", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    auto const panelStart = 20;
    CHECK_FALSE(panel.handleClick(5, panelStart + 1, panelStart));
    CHECK_FALSE(panel.handleClick(5, panelStart - 1, panelStart));
}

TEST_CASE("LogPanel: scroll when expanded", "[tui][logpanel]")
{
    auto panel = LogPanel {};
    for (auto i = 0; i < 15; ++i)
        panel.addLog(LogLevel::Info, std::format("msg {}", i));

    panel.toggle(); // expand

    // Initially at bottom (newest entries visible)
    // Scroll up should not crash, scroll down at bottom should be no-op
    panel.scrollDown(); // already at bottom, no-op
    panel.scrollUp();
    panel.scrollUp();
    panel.scrollDown();
}

TEST_CASE("LogPanel: render does not crash", "[tui][logpanel]")
{
    auto output = TerminalOutput {};
    static_cast<void>(output.initialize());

    auto panel = LogPanel {};
    panel.addLog(LogLevel::Info, "info msg");
    panel.addLog(LogLevel::Warning, "warn msg");
    panel.addLog(LogLevel::Error, "error msg");

    // Collapsed render
    panel.render(output, 20, 80);

    // Expanded render
    panel.toggle();
    panel.render(output, 20, 80);
}

// =============================================================================
// TerminalOutput extension tests
// =============================================================================

TEST_CASE("TerminalOutput: showCursor/hideCursor", "[tui][output]")
{
    auto output = TerminalOutput {};
    static_cast<void>(output.initialize());
    output.showCursor();
    output.hideCursor();
    // No crash; sequences are buffered internally
}

TEST_CASE("TerminalOutput: saveCursor/restoreCursor", "[tui][output]")
{
    auto output = TerminalOutput {};
    static_cast<void>(output.initialize());
    output.saveCursor();
    output.restoreCursor();
}

TEST_CASE("TerminalOutput: setScrollRegion/resetScrollRegion", "[tui][output]")
{
    auto output = TerminalOutput {};
    static_cast<void>(output.initialize());
    output.setScrollRegion(1, 20);
    output.resetScrollRegion();
}

// =============================================================================
// log::setCallback tests
// =============================================================================

TEST_CASE("log::setCallback: routes messages to callback", "[core][log]")
{
    auto received = std::vector<std::pair<mychat::log::Level, std::string>> {};

    mychat::log::setCallback([&](mychat::log::Level level, std::string_view message) {
        received.emplace_back(level, std::string(message));
    });

    mychat::log::info("hello {}", "world");
    mychat::log::warning("warn msg");
    mychat::log::error("err msg");

    // Restore stderr output for other tests
    mychat::log::setCallback(nullptr);

    REQUIRE(received.size() == 3);

    CHECK(received[0].first == mychat::log::Level::Info);
    CHECK(received[0].second == "hello world");

    CHECK(received[1].first == mychat::log::Level::Warning);
    CHECK(received[1].second == "warn msg");

    CHECK(received[2].first == mychat::log::Level::Error);
    CHECK(received[2].second == "err msg");
}

TEST_CASE("log::setCallback(nullptr): reverts to stderr without crash", "[core][log]")
{
    mychat::log::setCallback(nullptr);

    // Should not crash — goes to stderr
    mychat::log::info("this goes to stderr");
}

// =============================================================================
// LogPanel thread-safety tests
// =============================================================================

TEST_CASE("LogPanel: concurrent addLog from multiple threads", "[tui][logpanel][thread]")
{
    auto panel = LogPanel {};
    constexpr auto ThreadCount = 4;
    constexpr auto MessagesPerThread = 25; // 4*25=100 = MaxEntries, no eviction

    auto threads = std::vector<std::thread> {};
    threads.reserve(ThreadCount);

    for (auto t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&panel, t]() {
            for (auto i = 0; i < MessagesPerThread; ++i)
                panel.addLog(LogLevel::Info, std::format("thread {} msg {}", t, i));
        });
    }

    for (auto& th: threads)
        th.join();

    CHECK(panel.entryCount() == static_cast<std::size_t>(ThreadCount * MessagesPerThread));
}

// =============================================================================
// Box tests
// =============================================================================

TEST_CASE("Box: basic configuration", "[tui][box]")
{
    auto config = BoxConfig {
        .row = 5,
        .col = 10,
        .width = 40,
        .height = 10,
        .border = BorderStyle::Rounded,
        .borderStyle = {},
        .title = "Test Box",
        .titleAlign = TitleAlign::Center,
        .titleStyle = {},
        .paddingLeft = 1,
        .paddingRight = 1,
        .paddingTop = 0,
        .paddingBottom = 0,
        .fillBackground = false,
        .backgroundStyle = {},
    };

    auto box = Box(config);

    CHECK(box.config().row == 5);
    CHECK(box.config().col == 10);
    CHECK(box.config().width == 40);
    CHECK(box.config().height == 10);
}

TEST_CASE("Box: inner dimensions", "[tui][box]")
{
    auto config = BoxConfig {
        .row = 1,
        .col = 1,
        .width = 20,
        .height = 10,
        .border = BorderStyle::Single,
        .borderStyle = {},
        .title = std::nullopt,
        .titleAlign = TitleAlign::Left,
        .titleStyle = {},
        .paddingLeft = 2,
        .paddingRight = 2,
        .paddingTop = 1,
        .paddingBottom = 1,
        .fillBackground = false,
        .backgroundStyle = {},
    };

    auto box = Box(config);

    // Width: 20 - 2 (borders) - 2 (paddingLeft) - 2 (paddingRight) = 14
    CHECK(box.innerWidth() == 14);

    // Height: 10 - 2 (borders) - 1 (paddingTop) - 1 (paddingBottom) = 6
    CHECK(box.innerHeight() == 6);

    // Content starts at row 1 + 1 (border) + 1 (paddingTop) = 3
    CHECK(box.contentStartRow() == 3);

    // Content starts at col 1 + 1 (border) + 2 (paddingLeft) = 4
    CHECK(box.contentStartCol() == 4);
}

TEST_CASE("Box: border styles", "[tui][box]")
{
    SECTION("Single border")
    {
        auto chars = BorderChars::fromStyle(BorderStyle::Single);
        CHECK(chars.topLeft == "\u250C");
        CHECK(chars.horizontal == "\u2500");
    }

    SECTION("Rounded border")
    {
        auto chars = BorderChars::fromStyle(BorderStyle::Rounded);
        CHECK(chars.topLeft == "\u256D");
    }

    SECTION("Double border")
    {
        auto chars = BorderChars::fromStyle(BorderStyle::Double);
        CHECK(chars.topLeft == "\u2554");
        CHECK(chars.horizontal == "\u2550");
    }

    SECTION("Heavy border")
    {
        auto chars = BorderChars::fromStyle(BorderStyle::Heavy);
        CHECK(chars.topLeft == "\u250F");
    }
}

TEST_CASE("Box: render does not crash", "[tui][box]")
{
    auto output = TerminalOutput {};
    static_cast<void>(output.initialize());

    auto config = BoxConfig {
        .row = 1,
        .col = 1,
        .width = 30,
        .height = 8,
        .border = BorderStyle::Rounded,
        .borderStyle = {},
        .title = "Test",
        .titleAlign = TitleAlign::Center,
        .titleStyle = {},
        .paddingLeft = 1,
        .paddingRight = 1,
        .paddingTop = 0,
        .paddingBottom = 0,
        .fillBackground = true,
        .backgroundStyle = {},
    };

    auto box = Box(config);
    box.render(output);
    box.clearContent(output);
}

// =============================================================================
// List tests
// =============================================================================

TEST_CASE("List: initial state", "[tui][list]")
{
    auto list = List {};
    CHECK(list.empty());
    CHECK(list.size() == 0);
    CHECK(list.selectedIndex() == 0);
    CHECK(!list.selectedItem().has_value());
}

TEST_CASE("List: set items", "[tui][list]")
{
    auto items = std::vector<ListItem> {
        { .label = "Item 1", .description = "", .filterText = "", .enabled = true },
        { .label = "Item 2", .description = "", .filterText = "", .enabled = true },
        { .label = "Item 3", .description = "", .filterText = "", .enabled = true },
    };

    auto list = List(items);

    CHECK(list.size() == 3);
    CHECK(!list.empty());
    CHECK(list.selectedIndex() == 0);
    CHECK(list.selectedItem().has_value());
    CHECK(list.selectedItem().value()->label == "Item 1");
}

TEST_CASE("List: navigation", "[tui][list]")
{
    auto items = std::vector<ListItem> {
        { .label = "Item 1", .description = "", .filterText = "", .enabled = true },
        { .label = "Item 2", .description = "", .filterText = "", .enabled = true },
        { .label = "Item 3", .description = "", .filterText = "", .enabled = true },
    };

    auto list = List(items);

    list.selectNext();
    CHECK(list.selectedIndex() == 1);

    list.selectNext();
    CHECK(list.selectedIndex() == 2);

    list.selectNext(); // Should not wrap
    CHECK(list.selectedIndex() == 2);

    list.selectPrevious();
    CHECK(list.selectedIndex() == 1);

    list.selectFirst();
    CHECK(list.selectedIndex() == 0);

    list.selectLast();
    CHECK(list.selectedIndex() == 2);
}

TEST_CASE("List: filtering", "[tui][list]")
{
    auto items = std::vector<ListItem> {
        { .label = "Apple", .description = "", .filterText = "", .enabled = true },
        { .label = "Banana", .description = "", .filterText = "", .enabled = true },
        { .label = "Cherry", .description = "", .filterText = "", .enabled = true },
    };

    auto list = List(items);

    CHECK(list.visibleItems().size() == 3);

    list.setFilter("an");
    CHECK(list.visibleItems().size() == 1); // Only "Banana" matches

    list.setFilter("a");
    CHECK(list.visibleItems().size() == 2); // "Apple" and "Banana" match

    list.clearFilter();
    CHECK(list.visibleItems().size() == 3);
}

TEST_CASE("List: keyboard events", "[tui][list]")
{
    auto items = std::vector<ListItem> {
        { .label = "Item 1", .description = "", .filterText = "", .enabled = true },
        { .label = "Item 2", .description = "", .filterText = "", .enabled = true },
    };

    auto list = List(items);

    auto action = list.processEvent(keyEvent(KeyCode::Down));
    CHECK(action == ListAction::Changed);
    CHECK(list.selectedIndex() == 1);

    action = list.processEvent(keyEvent(KeyCode::Up));
    CHECK(action == ListAction::Changed);
    CHECK(list.selectedIndex() == 0);

    action = list.processEvent(keyEvent(KeyCode::Enter));
    CHECK(action == ListAction::Selected);

    action = list.processEvent(keyEvent(KeyCode::Escape));
    CHECK(action == ListAction::Cancelled);
}

TEST_CASE("List: disabled items", "[tui][list]")
{
    auto items = std::vector<ListItem> {
        { .label = "Item 1", .description = "", .filterText = "", .enabled = true },
        { .label = "Item 2", .description = "", .filterText = "", .enabled = false },
        { .label = "Item 3", .description = "", .filterText = "", .enabled = true },
    };

    auto list = List(items);

    list.selectNext(); // Should skip disabled Item 2
    CHECK(list.selectedIndex() == 2);
}

// =============================================================================
// Text tests
// =============================================================================

TEST_CASE("Text: basic text", "[tui][text]")
{
    auto text = Text("Hello, world!");
    CHECK(text.text() == "Hello, world!");
    CHECK(text.lines().size() == 1);
}

TEST_CASE("Text: multiline text", "[tui][text]")
{
    auto text = Text("Line 1\nLine 2\nLine 3");
    CHECK(text.lines().size() == 3);
}

TEST_CASE("Text: word wrap", "[tui][text]")
{
    auto wrapped = wordWrap("Hello world this is a test", 10);
    CHECK(wrapped.size() >= 2);
    for (auto const& line: wrapped)
        CHECK(displayWidth(line) <= 10);
}

TEST_CASE("Text: truncate", "[tui][text]")
{
    auto result = mychat::tui::truncate("Hello, world!", 8);
    CHECK(mychat::tui::displayWidth(result) <= 8);
    CHECK(result.find("\u2026") != std::string::npos); // Contains ellipsis
}

TEST_CASE("Text: display width", "[tui][text]")
{
    CHECK(displayWidth("hello") == 5);
    CHECK(displayWidth("") == 0);
    CHECK(displayWidth("test") == 4);
}

TEST_CASE("Text: alignment", "[tui][text]")
{
    auto text = Text("Test");
    CHECK(text.align() == TextAlign::Left);

    text.setAlign(TextAlign::Center);
    CHECK(text.align() == TextAlign::Center);

    text.setAlign(TextAlign::Right);
    CHECK(text.align() == TextAlign::Right);
}

// =============================================================================
// Spinner tests
// =============================================================================

TEST_CASE("Spinner: basic functionality", "[tui][spinner]")
{
    auto spinner = Spinner(SpinnerType::Dots);

    CHECK(spinner.frameCount() > 0);
    CHECK(spinner.frameIndex() == 0);
    CHECK(!spinner.currentFrame().empty());
}

TEST_CASE("Spinner: different types", "[tui][spinner]")
{
    SECTION("Dots spinner")
    {
        auto frames = spinnerFrames(SpinnerType::Dots);
        CHECK(frames.size() == 10);
    }

    SECTION("Line spinner")
    {
        auto frames = spinnerFrames(SpinnerType::Line);
        CHECK(frames.size() == 4);
    }

    SECTION("Circle spinner")
    {
        auto frames = spinnerFrames(SpinnerType::Circle);
        CHECK(frames.size() == 4);
    }
}

TEST_CASE("Spinner: reset", "[tui][spinner]")
{
    auto spinner = Spinner(SpinnerType::Line);
    // Force advance by manipulating internal state through API
    spinner.reset();
    CHECK(spinner.frameIndex() == 0);
}

TEST_CASE("ProgressBar: basic functionality", "[tui][spinner]")
{
    auto bar = ProgressBar(20);

    CHECK(bar.progress() == 0.0f);
    CHECK(bar.width() == 20);

    bar.setProgress(0.5f);
    CHECK(bar.progress() == 0.5f);

    bar.setProgress(1.5f); // Should clamp to 1.0
    CHECK(bar.progress() == 1.0f);

    bar.setProgress(-0.5f); // Should clamp to 0.0
    CHECK(bar.progress() == 0.0f);
}

// =============================================================================
// StatusBar tests
// =============================================================================

TEST_CASE("StatusBar: hints management", "[tui][statusbar]")
{
    auto bar = StatusBar {};

    bar.addHint("Ctrl+C", "Quit");
    bar.addHint("Enter", "Submit");

    auto hints = std::vector<KeyHint> {
        { .key = "Ctrl+N", .action = "New" },
        { .key = "Ctrl+O", .action = "Open" },
    };
    bar.setHints(hints);

    bar.clearHints();
}

TEST_CASE("StatusBar: text sections", "[tui][statusbar]")
{
    auto bar = StatusBar {};

    bar.setLeftText("Left");
    bar.setCenterText("Center");
    bar.setRightText("Right");
}

TEST_CASE("StatusBar: styling", "[tui][statusbar]")
{
    auto style = defaultStatusBarStyle();
    CHECK(style.keyStyle.bold);
}

TEST_CASE("StatusBar: render does not crash", "[tui][statusbar]")
{
    auto output = TerminalOutput {};
    static_cast<void>(output.initialize());

    auto bar = StatusBar {};
    bar.addHint("Ctrl+C", "Quit");
    bar.setRightText("mychat v1.0");

    bar.render(output, 24, 80);
}

// =============================================================================
// Dialog tests
// =============================================================================

TEST_CASE("SelectDialog: basic functionality", "[tui][dialog]")
{
    auto config = SelectDialogConfig {
        .title = "Select Item",
        .items = {
            { .label = "Option 1", .description = "", .filterText = "", .enabled = true },
            { .label = "Option 2", .description = "", .filterText = "", .enabled = true },
        },
        .width = 40,
        .maxHeight = 20,
        .border = BorderStyle::Rounded,
        .borderStyle = {},
        .titleStyle = {},
        .dimBackground = true,
        .confirmHint = "Enter",
        .cancelHint = "Esc",
    };

    auto dialog = SelectDialog(config);

    CHECK(dialog.selectedIndex() == 0);

    auto result = dialog.processEvent(keyEvent(KeyCode::Down));
    CHECK(result == DialogResult::Changed);
    CHECK(dialog.selectedIndex() == 1);

    result = dialog.processEvent(keyEvent(KeyCode::Enter));
    CHECK(result == DialogResult::Confirmed);
}

TEST_CASE("SelectDialog: cancel", "[tui][dialog]")
{
    auto config = SelectDialogConfig {
        .title = "Test",
        .items = {{ .label = "Item", .description = "", .filterText = "", .enabled = true }},
        .width = 30,
        .maxHeight = 10,
        .border = BorderStyle::Single,
        .borderStyle = {},
        .titleStyle = {},
        .dimBackground = false,
        .confirmHint = "Enter",
        .cancelHint = "Esc",
    };

    auto dialog = SelectDialog(config);

    auto result = dialog.processEvent(keyEvent(KeyCode::Escape));
    CHECK(result == DialogResult::Cancelled);
}

TEST_CASE("ConfirmDialog: yes/no navigation", "[tui][dialog]")
{
    auto config = ConfirmDialogConfig {
        .title = "Confirm",
        .message = "Are you sure?",
        .confirmLabel = "Yes",
        .cancelLabel = "No",
        .defaultConfirm = false,
        .width = 40,
        .border = BorderStyle::Rounded,
        .borderStyle = {},
        .titleStyle = {},
        .messageStyle = {},
        .dimBackground = true,
    };

    auto dialog = ConfirmDialog(config);

    CHECK(!dialog.isConfirmSelected()); // Default to "No"

    auto result = dialog.processEvent(keyEvent(KeyCode::Left));
    CHECK(result == DialogResult::Changed);
    CHECK(dialog.isConfirmSelected()); // Now on "Yes"

    result = dialog.processEvent(keyEvent(KeyCode::Enter));
    CHECK(result == DialogResult::Confirmed);
}

TEST_CASE("ConfirmDialog: shortcuts", "[tui][dialog]")
{
    auto config = ConfirmDialogConfig {
        .title = "Test",
        .message = "Test?",
        .confirmLabel = "Yes",
        .cancelLabel = "No",
        .defaultConfirm = false,
        .width = 30,
        .border = BorderStyle::Single,
        .borderStyle = {},
        .titleStyle = {},
        .messageStyle = {},
        .dimBackground = false,
    };

    auto dialog = ConfirmDialog(config);

    // 'y' should confirm
    auto yEvent = KeyEvent { .key = keyCodeFromCodepoint('y'), .modifiers = Modifier::None, .codepoint = 'y' };
    auto result = dialog.processEvent(yEvent);
    CHECK(result == DialogResult::Confirmed);

    // 'n' should cancel
    auto nEvent = KeyEvent { .key = keyCodeFromCodepoint('n'), .modifiers = Modifier::None, .codepoint = 'n' };
    result = dialog.processEvent(nEvent);
    CHECK(result == DialogResult::Cancelled);
}

TEST_CASE("InputDialog: text input", "[tui][dialog]")
{
    auto config = InputDialogConfig {
        .title = "Input",
        .prompt = "Enter name:",
        .placeholder = "Name...",
        .initialValue = "",
        .width = 50,
        .border = BorderStyle::Rounded,
        .borderStyle = {},
        .titleStyle = {},
        .inputStyle = {},
        .dimBackground = true,
    };

    auto dialog = InputDialog(config);
    CHECK(dialog.value().empty());

    // Type a character
    auto aEvent = KeyEvent { .key = keyCodeFromCodepoint('a'), .modifiers = Modifier::None, .codepoint = 'a' };
    auto result = dialog.processEvent(aEvent);
    CHECK(result == DialogResult::Changed);
    CHECK(dialog.value() == "a");

    // Backspace
    result = dialog.processEvent(keyEvent(KeyCode::Backspace));
    CHECK(result == DialogResult::Changed);
    CHECK(dialog.value().empty());

    dialog.setValue("test");
    CHECK(dialog.value() == "test");
}

// =============================================================================
// Theme tests
// =============================================================================

TEST_CASE("Theme: dark theme", "[tui][theme]")
{
    auto theme = darkTheme();

    CHECK(theme.colors.primary.r > 0);
    CHECK(theme.colors.background.r < 100); // Dark background
    CHECK(theme.textNormal.fg.index() != 0); // Has a color set
    CHECK(theme.borderStyle == BorderStyle::Rounded);
}

TEST_CASE("Theme: light theme", "[tui][theme]")
{
    auto theme = lightTheme();

    CHECK(theme.colors.background.r > 200); // Light background
    CHECK(theme.borderStyle == BorderStyle::Rounded);
}

TEST_CASE("Theme: mono theme", "[tui][theme]")
{
    auto theme = monoTheme();

    CHECK(theme.borderStyle == BorderStyle::Single);
    CHECK(theme.textBold.bold);
}

TEST_CASE("ThemeManager: singleton", "[tui][theme]")
{
    auto& manager1 = ThemeManager::instance();
    auto& manager2 = ThemeManager::instance();

    CHECK(&manager1 == &manager2);
}

TEST_CASE("ThemeManager: set theme", "[tui][theme]")
{
    auto& manager = ThemeManager::instance();

    manager.setCurrent(lightTheme());
    CHECK(manager.current().colors.background.r > 200);

    manager.reset();
    CHECK(manager.current().colors.background.r < 100); // Back to dark theme
}

TEST_CASE("currentTheme: convenience function", "[tui][theme]")
{
    auto const& theme = currentTheme();
    CHECK(theme.borderStyle == BorderStyle::Rounded);
}
