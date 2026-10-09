/**
 * @file util.hpp
 * @brief Small helpers: item id normalization, coin amount parsing and file reading.
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace bz {

/**
 * @brief Removes leading and trailing ASCII whitespace.
 * @param text Input text.
 * @return A view into @p text without surrounding whitespace.
 */
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

/**
 * @brief Converts an item id to the canonical form used as catalog key.
 *
 * Bazaar and NEU spell some ids differently (`INK_SACK:3` versus `INK_SACK-3`).
 * The canonical form is trimmed, upper-case, and uses `-` as variant separator.
 *
 * @param raw Item id as found in any data source.
 * @return The normalized id; empty if @p raw is blank.
 */
[[nodiscard]] std::string normalize_item_id(std::string_view raw);

/**
 * @brief Parses a human-readable coin amount.
 *
 * Accepts plain numbers, thousands separators (`,` or `_`) and the case-insensitive
 * suffixes `k`, `m`, `b` and `t`. Examples: `5000000`, `5,000,000`, `5m`, `2.5b`, `750k`.
 *
 * @param text Amount to parse.
 * @return The amount in coins, or `std::nullopt` if @p text is invalid or negative.
 */
[[nodiscard]] std::optional<double> parse_coins(std::string_view text);

/**
 * @brief Reads a whole file into memory.
 * @param path File to read.
 * @return The file content.
 * @throws std::runtime_error If the file cannot be opened or read.
 */
[[nodiscard]] std::string read_text_file(const std::filesystem::path& path);

} // namespace bz
