/**
 * @file http.hpp
 * @brief Download of the Bazaar API response.
 *
 * This is the only part of the project that performs network access. It is used by
 * `bz update` alone: every other command reads the prices cached on disk.
 */
#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace bz::app {

/// Base URL of the Hypixel public API.
inline constexpr std::string_view kApiBaseUrl = "https://api.hypixel.net";

/// Path of the Bazaar endpoint (no API key required).
inline constexpr std::string_view kBazaarPath = "/v2/skyblock/bazaar";

/**
 * @brief Downloads the Bazaar response.
 * @param timeout Connection and read timeout.
 * @return The response body.
 * @throws std::runtime_error On network failure or non-200 status.
 */
[[nodiscard]] std::string fetch_bazaar_json(std::chrono::seconds timeout = std::chrono::seconds{
                                                15});

} // namespace bz::app
