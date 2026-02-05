// SPDX-License-Identifier: Apache-2.0
#include <tui/Spinner.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace mychat::tui
{

namespace
{

// Spinner frame definitions
constexpr std::array DotsFrames = {
    std::string_view { "\u280B" }, // ⠋
    std::string_view { "\u2819" }, // ⠙
    std::string_view { "\u2839" }, // ⠹
    std::string_view { "\u2838" }, // ⠸
    std::string_view { "\u283C" }, // ⠼
    std::string_view { "\u2834" }, // ⠴
    std::string_view { "\u2826" }, // ⠦
    std::string_view { "\u2827" }, // ⠧
    std::string_view { "\u2807" }, // ⠇
    std::string_view { "\u280F" }, // ⠏
};

constexpr std::array LineFrames = {
    std::string_view { "-" },
    std::string_view { "\\" },
    std::string_view { "|" },
    std::string_view { "/" },
};

constexpr std::array CircleFrames = {
    std::string_view { "\u25D0" }, // ◐
    std::string_view { "\u25D3" }, // ◓
    std::string_view { "\u25D1" }, // ◑
    std::string_view { "\u25D2" }, // ◒
};

constexpr std::array ArcFrames = {
    std::string_view { "\u25DC" }, // ◜
    std::string_view { "\u25E0" }, // ◠
    std::string_view { "\u25DD" }, // ◝
    std::string_view { "\u25DE" }, // ◞
    std::string_view { "\u25E1" }, // ◡
    std::string_view { "\u25DF" }, // ◟
};

constexpr std::array SquareFrames = {
    std::string_view { "\u25F0" }, // ◰
    std::string_view { "\u25F3" }, // ◳
    std::string_view { "\u25F2" }, // ◲
    std::string_view { "\u25F1" }, // ◱
};

constexpr std::array ArrowFrames = {
    std::string_view { "\u2190" }, // ←
    std::string_view { "\u2196" }, // ↖
    std::string_view { "\u2191" }, // ↑
    std::string_view { "\u2197" }, // ↗
    std::string_view { "\u2192" }, // →
    std::string_view { "\u2198" }, // ↘
    std::string_view { "\u2193" }, // ↓
    std::string_view { "\u2199" }, // ↙
};

constexpr std::array BounceFrames = {
    std::string_view { "\u2801" }, // ⠁
    std::string_view { "\u2802" }, // ⠂
    std::string_view { "\u2804" }, // ⠄
    std::string_view { "\u2802" }, // ⠂
};

constexpr std::array PulseFrames = {
    std::string_view { "\u2581" }, // ▁
    std::string_view { "\u2582" }, // ▂
    std::string_view { "\u2583" }, // ▃
    std::string_view { "\u2584" }, // ▄
    std::string_view { "\u2585" }, // ▅
    std::string_view { "\u2586" }, // ▆
    std::string_view { "\u2587" }, // ▇
    std::string_view { "\u2588" }, // █
    std::string_view { "\u2587" }, // ▇
    std::string_view { "\u2586" }, // ▆
    std::string_view { "\u2585" }, // ▅
    std::string_view { "\u2584" }, // ▄
    std::string_view { "\u2583" }, // ▃
    std::string_view { "\u2582" }, // ▂
    std::string_view { "\u2581" }, // ▁
};

constexpr std::array ClockFrames = {
    std::string_view { "\U0001F550" }, // 🕐
    std::string_view { "\U0001F551" }, // 🕑
    std::string_view { "\U0001F552" }, // 🕒
    std::string_view { "\U0001F553" }, // 🕓
    std::string_view { "\U0001F554" }, // 🕔
    std::string_view { "\U0001F555" }, // 🕕
    std::string_view { "\U0001F556" }, // 🕖
    std::string_view { "\U0001F557" }, // 🕗
    std::string_view { "\U0001F558" }, // 🕘
    std::string_view { "\U0001F559" }, // 🕙
    std::string_view { "\U0001F55A" }, // 🕚
    std::string_view { "\U0001F55B" }, // 🕛
};

constexpr std::array MoonFrames = {
    std::string_view { "\U0001F311" }, // 🌑
    std::string_view { "\U0001F312" }, // 🌒
    std::string_view { "\U0001F313" }, // 🌓
    std::string_view { "\U0001F314" }, // 🌔
    std::string_view { "\U0001F315" }, // 🌕
    std::string_view { "\U0001F316" }, // 🌖
    std::string_view { "\U0001F317" }, // 🌗
    std::string_view { "\U0001F318" }, // 🌘
};

constexpr std::array EarthFrames = {
    std::string_view { "\U0001F30D" }, // 🌍
    std::string_view { "\U0001F30E" }, // 🌎
    std::string_view { "\U0001F30F" }, // 🌏
};

} // namespace

auto spinnerFrames(SpinnerType type) -> std::span<std::string_view const>
{
    switch (type)
    {
        case SpinnerType::Dots:
            return DotsFrames;
        case SpinnerType::Line:
            return LineFrames;
        case SpinnerType::Circle:
            return CircleFrames;
        case SpinnerType::Arc:
            return ArcFrames;
        case SpinnerType::Square:
            return SquareFrames;
        case SpinnerType::Arrow:
            return ArrowFrames;
        case SpinnerType::Bounce:
            return BounceFrames;
        case SpinnerType::Pulse:
            return PulseFrames;
        case SpinnerType::Clock:
            return ClockFrames;
        case SpinnerType::Moon:
            return MoonFrames;
        case SpinnerType::Earth:
            return EarthFrames;
    }
    return DotsFrames;
}

auto spinnerInterval(SpinnerType type) -> std::chrono::milliseconds
{
    switch (type)
    {
        case SpinnerType::Dots:
            return std::chrono::milliseconds { 80 };
        case SpinnerType::Line:
            return std::chrono::milliseconds { 100 };
        case SpinnerType::Circle:
            return std::chrono::milliseconds { 120 };
        case SpinnerType::Arc:
            return std::chrono::milliseconds { 100 };
        case SpinnerType::Square:
            return std::chrono::milliseconds { 120 };
        case SpinnerType::Arrow:
            return std::chrono::milliseconds { 120 };
        case SpinnerType::Bounce:
            return std::chrono::milliseconds { 120 };
        case SpinnerType::Pulse:
            return std::chrono::milliseconds { 100 };
        case SpinnerType::Clock:
            return std::chrono::milliseconds { 100 };
        case SpinnerType::Moon:
            return std::chrono::milliseconds { 100 };
        case SpinnerType::Earth:
            return std::chrono::milliseconds { 180 };
    }
    return std::chrono::milliseconds { 100 };
}

Spinner::Spinner(SpinnerType type):
    _frames(spinnerFrames(type)), _interval(spinnerInterval(type)), _lastTick(std::chrono::steady_clock::now())
{
}

Spinner::Spinner(std::span<std::string_view const> frames, std::chrono::milliseconds interval):
    _frames(frames), _interval(interval), _lastTick(std::chrono::steady_clock::now())
{
}

auto Spinner::tick() -> bool
{
    auto const now = std::chrono::steady_clock::now();
    if (now - _lastTick >= _interval)
    {
        _lastTick = now;
        _frameIndex = (_frameIndex + 1) % _frames.size();
        return true;
    }
    return false;
}

auto Spinner::currentFrame() const noexcept -> std::string_view
{
    if (_frames.empty())
        return "";
    return _frames[_frameIndex];
}

auto Spinner::frameIndex() const noexcept -> std::size_t
{
    return _frameIndex;
}

auto Spinner::frameCount() const noexcept -> std::size_t
{
    return _frames.size();
}

void Spinner::reset()
{
    _frameIndex = 0;
    _lastTick = std::chrono::steady_clock::now();
}

void Spinner::render(TerminalOutput& output, Style const& style) const
{
    output.write(currentFrame(), style);
}

void Spinner::renderWithLabel(TerminalOutput& output,
                              std::string_view label,
                              Style const& spinnerStyle,
                              Style const& labelStyle) const
{
    render(output, spinnerStyle);
    output.writeRaw(" ");
    output.write(label, labelStyle);
}

void Spinner::setType(SpinnerType type)
{
    _frames = spinnerFrames(type);
    _interval = spinnerInterval(type);
    _frameIndex = 0;
}

auto Spinner::interval() const noexcept -> std::chrono::milliseconds
{
    return _interval;
}

void Spinner::setInterval(std::chrono::milliseconds interval)
{
    _interval = interval;
}

// =============================================================================
// ProgressBar
// =============================================================================

ProgressBar::ProgressBar(int width): _width(width)
{
}

void ProgressBar::setProgress(float progress)
{
    _progress = std::clamp(progress, 0.0f, 1.0f);
}

auto ProgressBar::progress() const noexcept -> float
{
    return _progress;
}

void ProgressBar::setWidth(int width)
{
    _width = std::max(1, width);
}

auto ProgressBar::width() const noexcept -> int
{
    return _width;
}

void ProgressBar::render(TerminalOutput& output, Style const& filledStyle, Style const& emptyStyle) const
{
    auto const filledWidth = _progress * static_cast<float>(_width);
    auto const fullBlocks = static_cast<int>(filledWidth);
    auto const remainder = filledWidth - static_cast<float>(fullBlocks);

    // Render filled blocks
    for (auto i = 0; i < fullBlocks && i < _width; ++i)
        output.write(FilledChar, filledStyle);

    // Render partial block if any
    if (fullBlocks < _width && remainder > 0.0f)
    {
        auto const partialIdx = static_cast<std::size_t>((1.0f - remainder) * 7.0f);
        auto const safeIdx = std::min(partialIdx, std::size_t { 7 });
        output.write(PartialChars[safeIdx], filledStyle);

        // Render empty blocks
        for (auto i = fullBlocks + 1; i < _width; ++i)
            output.write(EmptyChar, emptyStyle);
    }
    else
    {
        // Render empty blocks
        for (auto i = fullBlocks; i < _width; ++i)
            output.write(EmptyChar, emptyStyle);
    }
}

void ProgressBar::renderWithPercent(TerminalOutput& output,
                                    Style const& filledStyle,
                                    Style const& emptyStyle,
                                    Style const& labelStyle) const
{
    render(output, filledStyle, emptyStyle);
    output.writeRaw(" ");
    output.write(std::format("{:3.0f}%", _progress * 100.0f), labelStyle);
}

} // namespace mychat::tui
