// SPDX-License-Identifier: Apache-2.0
#include <core/Log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <tui/InputEvent.hpp>
#include <tui/InputField.hpp>
#include <tui/KeyCode.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/Modifier.hpp>
#include <tui/Sixel.hpp>
#include <tui/TerminalOutput.hpp>
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
