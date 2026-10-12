#include "console.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <iostream>
#include <streambuf>
#include <string>
#include <string_view>
#endif

namespace bz::app {

#ifdef _WIN32

namespace {

/// Length of the longest prefix of @p text that does not end inside a UTF-8 sequence.
std::size_t complete_utf8_length(std::string_view text) {
    for (std::size_t back = 1; back <= 4 && back <= text.size(); ++back) {
        const auto byte = static_cast<unsigned char>(text[text.size() - back]);
        if ((byte & 0xC0U) != 0x80U) {
            const std::size_t length = byte >= 0xF0U   ? 4
                                       : byte >= 0xE0U ? 3
                                       : byte >= 0xC0U ? 2
                                                       : 1;
            return length > back ? text.size() - back : text.size();
        }
    }
    return text.size();
}

/**
 * Stream buffer writing UTF-8 text to a console with WriteConsoleW. Writing bytes instead
 * depends on the console code page and on how the C runtime translates console output.
 */
class ConsoleBuffer final : public std::streambuf {
public:
    explicit ConsoleBuffer(HANDLE console) : console_(console) {}

protected:
    int_type overflow(int_type ch) override {
        if (!traits_type::eq_int_type(ch, traits_type::eof())) {
            pending_.push_back(traits_type::to_char_type(ch));
            if (ch == '\n' && !flush()) {
                return traits_type::eof();
            }
        }
        return traits_type::not_eof(ch);
    }

    std::streamsize xsputn(const char* text, std::streamsize count) override {
        pending_.append(text, static_cast<std::size_t>(count));
        if (pending_.find('\n') != std::string::npos && !flush()) {
            return 0;
        }
        return count;
    }

    int sync() override { return flush() ? 0 : -1; }

private:
    /// Writes every complete UTF-8 sequence; an incomplete one waits for the next write.
    bool flush() {
        const std::size_t length = complete_utf8_length(pending_);
        if (length == 0) {
            return true;
        }
        const int bytes = static_cast<int>(length);
        const int units = MultiByteToWideChar(CP_UTF8, 0, pending_.data(), bytes, nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(units), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, pending_.data(), bytes, wide.data(), units);
        pending_.erase(0, length);

        DWORD written = 0;
        return WriteConsoleW(console_, wide.data(), static_cast<DWORD>(wide.size()), &written,
                             nullptr) != 0;
    }

    HANDLE console_;
    std::string pending_;
};

/// Routes @p stream to a console, if @p handle is one, until destruction.
class ConsoleRedirect {
public:
    ConsoleRedirect(std::ostream& stream, HANDLE handle) : stream_(stream), buffer_(handle) {
        DWORD mode = 0;
        if (handle != INVALID_HANDLE_VALUE && handle != nullptr && GetConsoleMode(handle, &mode)) {
            stream_.flush();
            previous_ = stream_.rdbuf(&buffer_);
        }
    }

    ~ConsoleRedirect() {
        if (previous_ != nullptr) {
            stream_.flush();
            stream_.rdbuf(previous_);
        }
    }

    ConsoleRedirect(const ConsoleRedirect&) = delete;
    ConsoleRedirect& operator=(const ConsoleRedirect&) = delete;

private:
    std::ostream& stream_;
    ConsoleBuffer buffer_;
    std::streambuf* previous_ = nullptr;
};

} // namespace

struct ConsoleSession::State {
    ConsoleRedirect out{std::cout, GetStdHandle(STD_OUTPUT_HANDLE)};
    ConsoleRedirect err{std::cerr, GetStdHandle(STD_ERROR_HANDLE)};
    DWORD previous_mode = 0;
    bool mode_changed = false;
};

ConsoleSession::ConsoleSession() : state_(std::make_unique<State>()) {
    // GetConsoleMode fails when the output is redirected to a file or a pipe: no colors then.
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE || output == nullptr || !GetConsoleMode(output, &mode)) {
        supports_color_ = false;
        return;
    }
    state_->previous_mode = mode;
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        state_->mode_changed =
            SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        supports_color_ = state_->mode_changed;
    }
}

ConsoleSession::~ConsoleSession() {
    if (state_->mode_changed) {
        std::cout.flush();
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), state_->previous_mode);
    }
}

#else

struct ConsoleSession::State {};

ConsoleSession::ConsoleSession() = default;
ConsoleSession::~ConsoleSession() = default;

#endif

} // namespace bz::app
