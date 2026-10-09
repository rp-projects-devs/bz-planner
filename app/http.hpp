/**
 * @file http.hpp
 * @brief Retrieval of the Bazaar API response, with an on-disk cache.
 *
 * This is the only part of the project that performs network access.
 */
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace bz::app {

/// Base URL of the Hypixel public API.
inline constexpr std::string_view kApiBaseUrl = "https://api.hypixel.net";

/// Path of the Bazaar endpoint (no API key required).
inline constexpr std::string_view kBazaarPath = "/v2/skyblock/bazaar";

/// A Bazaar response body and where it came from.
struct BazaarPayload {
    std::string json;        ///< Response body.
    bool from_cache = false; ///< True if read from the cache file.
    bool stale = false;      ///< True if the cache was older than allowed (network failure).
};

/**
 * @brief Downloads the Bazaar response.
 * @param timeout Connection and read timeout.
 * @return The response body.
 * @throws std::runtime_error On network failure or non-200 status.
 */
[[nodiscard]] std::string fetch_bazaar_json(std::chrono::seconds timeout = std::chrono::seconds{
                                                15});

/**
 * @brief Returns the Bazaar response, using the cache when it is recent enough.
 *
 * A fresh download replaces the cache atomically. If the download fails and a cache file
 * exists, the stale cache is returned with BazaarPayload::stale set.
 *
 * @param cache_file Cache location; parent directories are created as needed.
 * @param max_age Maximum age of a cache file considered fresh.
 * @param force_refresh Ignore the cache and download.
 * @return The response body and its origin.
 * @throws std::runtime_error If the download fails and no cache is available.
 */
[[nodiscard]] BazaarPayload load_bazaar_json(const std::filesystem::path& cache_file,
                                             std::chrono::seconds max_age, bool force_refresh);

} // namespace bz::app
