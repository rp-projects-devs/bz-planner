/**
 * @file console.hpp
 * @brief Terminal setup needed by the output of display.hpp (Windows consoles only).
 */
#pragma once

#include <memory>

namespace bz::app {

/**
 * @brief Prepares the terminal for UTF-8 text and ANSI colors, and restores it on destruction.
 *
 * On Windows, when standard output or error is a console, `std::cout` / `std::cerr` are routed
 * to the console as UTF-16, so recipe trees and arrows display whatever the console code page
 * and C runtime locale, and ANSI escape sequences are enabled on standard output. Redirected
 * output stays UTF-8 bytes. Elsewhere, terminals already handle both and nothing is changed.
 */
class ConsoleSession {
public:
    ConsoleSession();
    ~ConsoleSession();

    ConsoleSession(const ConsoleSession&) = delete;
    ConsoleSession& operator=(const ConsoleSession&) = delete;

    /// False when standard output cannot show ANSI colors (Windows only: redirected output or
    /// a console without virtual terminal support).
    [[nodiscard]] bool supports_color() const noexcept { return supports_color_; }

private:
    struct State;
    std::unique_ptr<State> state_;
    bool supports_color_ = true;
};

} // namespace bz::app
