#include "bz/util.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace bz {

namespace {

constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

constexpr char to_upper(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

constexpr char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

double suffix_multiplier(char suffix) noexcept {
    switch (suffix) {
    case 'k':
        return 1e3;
    case 'm':
        return 1e6;
    case 'b':
        return 1e9;
    case 't':
        return 1e12;
    default:
        return 1.0;
    }
}

} // namespace

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string normalize_item_id(std::string_view raw) {
    std::string id{trim(raw)};
    std::transform(id.begin(), id.end(), id.begin(),
                   [](char c) { return c == ':' ? '-' : to_upper(c); });
    return id;
}

std::optional<double> parse_coins(std::string_view text) {
    std::string cleaned;
    for (const char c : trim(text)) {
        if (c != ',' && c != '_') {
            cleaned.push_back(to_lower(c));
        }
    }
    if (cleaned.empty()) {
        return std::nullopt;
    }

    const double multiplier = suffix_multiplier(cleaned.back());
    if (multiplier != 1.0) {
        cleaned.pop_back();
    }

    double value = 0.0;
    const char* const first = cleaned.data();
    const char* const last = first + cleaned.size();
    const auto [end, error] = std::from_chars(first, last, value);
    if (cleaned.empty() || error != std::errc{} || end != last) {
        return std::nullopt;
    }

    const double coins = value * multiplier;
    if (!std::isfinite(coins) || coins < 0.0) {
        return std::nullopt;
    }
    return coins;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error("cannot open file: " + path.string());
    }

    // Allocate once and read in a single call instead of growing the string per character.
    std::string content;
    content.resize(std::filesystem::file_size(path));
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (file.bad() || static_cast<std::size_t>(file.gcount()) != content.size()) {
        throw std::runtime_error("cannot read file: " + path.string());
    }
    return content;
}

} // namespace bz
